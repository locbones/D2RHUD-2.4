<?php
declare(strict_types=1);

// Wallet withdrawals reserve existing balances, never mint save assets. Only
// the paired save agent can acknowledge delivery through a custody ticket.
trait MarketplaceWallet {
    private function walletSchema(string $engine): void {
        $this->db->exec("CREATE TABLE IF NOT EXISTS wallet_withdrawals (id CHAR(32) PRIMARY KEY, owner VARCHAR(64) NOT NULL, scope VARCHAR(255) NOT NULL, status VARCHAR(16) NOT NULL, revision INT NOT NULL, requested TEXT NOT NULL, remaining TEXT NOT NULL, ticket CHAR(32) NOT NULL, message VARCHAR(512) NOT NULL, created BIGINT NOT NULL, updated BIGINT NOT NULL)$engine");
        $seq=$engine?'seq BIGINT AUTO_INCREMENT PRIMARY KEY':'seq INTEGER PRIMARY KEY AUTOINCREMENT';
        $this->db->exec("CREATE TABLE IF NOT EXISTS wallet_entries ($seq, owner VARCHAR(64) NOT NULL, scope VARCHAR(255) NOT NULL, kind VARCHAR(8) NOT NULL, stat VARCHAR(32) NOT NULL, amount BIGINT NOT NULL, available BIGINT NOT NULL, action VARCHAR(32) NOT NULL, reference VARCHAR(64) NOT NULL, created BIGINT NOT NULL)$engine");
    }
    private function walletRequest(string $owner,string $scope,string $id): array {
        $row=$this->run('SELECT * FROM wallet_withdrawals WHERE id=? AND owner=? AND scope=?',[$id,$owner,$scope])->fetch();
        if(!$row)throw new MarketError(404,'Withdrawal not found in this player and game mode.');return $row;
    }
    private function walletRows(string $owner,string $scope,bool $pending=false): array {
        $rows=$this->run('SELECT id,status,revision,requested,remaining,message,created,updated FROM wallet_withdrawals WHERE owner=? AND scope=?'.($pending?" AND status='queued'":'').' ORDER BY created DESC,id DESC LIMIT 64',[$owner,$scope])->fetchAll();
        foreach($rows as &$r){foreach(['requested','remaining'] as $k)$r[$k]=json_decode($r[$k],true,512,JSON_THROW_ON_ERROR);foreach(['revision','created','updated'] as $k)$r[$k]=(int)$r[$k];}unset($r);return $rows;
    }
    private function walletSnapshot(string $owner,string $scope): array {
        $entries=$this->run('SELECT seq,kind,stat,amount,available,action,reference,created FROM wallet_entries WHERE owner=? AND scope=? ORDER BY seq DESC LIMIT 40',[$owner,$scope])->fetchAll();
        foreach($entries as &$r)foreach(['seq','amount','available','created'] as $k)$r[$k]=(int)$r[$k];unset($r);
        return ['withdrawals'=>$this->walletRows($owner,$scope),'entries'=>$entries];
    }
    private function walletCommand(array $actor,array $data,bool $hc): array {
        $id=self::ticketId($data['id']??null);$hash=hash('sha256',$this->scope($hc).'|'.self::json($data));
        return $this->transaction(function()use($actor,$data,$hc,$id,$hash){
            $this->lockScope($hc);$owner=$actor['id'];$scope=$this->scope($hc);
            $old=$this->run('SELECT hash,result FROM commands WHERE actor=? AND id=?',[$owner,$id])->fetch();
            if($old){if(!hash_equals($old['hash'],$hash))throw new MarketError(409,'Command ID reused.');return json_decode($old['result'],true,512,JSON_THROW_ON_ERROR);}
            if(($data['type']??'')==='wallet_withdraw'){
                if(!$this->run('SELECT id FROM custody_agents_scoped WHERE owner=? AND `mod`=? AND game_mode=?',[$owner,$this->mod,'2.4'])->fetch())throw new MarketError(409,'Connect your save companion before withdrawing.');
                if((int)$this->run("SELECT COUNT(*) FROM wallet_withdrawals WHERE owner=? AND scope=? AND status IN ('queued','delivering')",[$owner,$scope])->fetchColumn()>=32)throw new MarketError(409,'Finish or cancel an existing withdrawal first.');
                $rows=self::price($data['balances']??null);
                foreach($rows as $r){if(!in_array($r['kind']??'item',['gold','bag'],true))throw new MarketError(422,'Withdraw gold or bag resources here; return items from My Items.');$this->balance($owner,$scope,$r,-$r['quantity'],'withdrawal_reserved',$id);}
                $now=time();$this->run('INSERT INTO wallet_withdrawals VALUES (?,?,?,?,?,?,?,?,?,?,?)',[$id,$owner,$scope,'queued',1,self::json($rows),self::json($rows),'','',$now,$now]);
                $result=['withdrawal_id'=>$id,'status'=>'queued'];
            }elseif(($data['type']??'')==='wallet_cancel'){
                $request=self::ticketId($data['withdrawal_id']??null);$row=$this->walletRequest($owner,$scope,$request);
                if($row['status']!=='queued'||(int)$row['revision']!==self::number($data['expected_revision']??null,1,2147483647))throw new MarketError(409,'Withdrawal changed or delivery started. Refresh before cancelling.');
                foreach(json_decode($row['remaining'],true,512,JSON_THROW_ON_ERROR) as $r)$this->balance($owner,$scope,$r,$r['quantity'],'withdrawal_cancelled',$request);
                $this->run("UPDATE wallet_withdrawals SET status='cancelled',revision=revision+1,message='',updated=? WHERE id=?",[time(),$request]);
                $result=['withdrawal_id'=>$request,'status'=>'cancelled'];$this->settleReady($hc,null,$owner);
            }else throw new MarketError(422,'Unknown Vault command.');
            $this->run('INSERT INTO commands VALUES (?,?,?,?)',[$owner,$id,$hash,self::json($result)]);return $result;
        });
    }
    private function reserveWalletClaim(string $owner,string $scope,string $ticket,array $data): array {
        $id=self::ticketId($data['withdrawal_id']);$row=$this->walletRequest($owner,$scope,$id);
        if($row['status']!=='queued')throw new MarketError(409,'Withdrawal is no longer queued.');
        $caps=[];foreach(($data['balance_caps']??[])?self::price($data['balance_caps']):[] as $r){if(!in_array($r['kind']??'item',['gold','bag'],true))throw new MarketError(422,'Delivery capacities must be resources.');$caps[$r['kind'].'|'.($r['stat']??'')]=$r['quantity'];}
        $balances=[];foreach(json_decode($row['remaining'],true,512,JSON_THROW_ON_ERROR) as $r){$q=min($r['quantity'],$caps[$r['kind'].'|'.($r['stat']??'')]??0);if($q){$r['quantity']=$q;$balances[]=$r;}}
        if(!$balances)throw new MarketError(409,'No withdrawal resources fit the delivery page.');
        $this->run("UPDATE wallet_withdrawals SET status='delivering',ticket=?,revision=revision+1,message='',updated=? WHERE id=?",[$ticket,time(),$id]);
        return ['items'=>[],'balances'=>$balances,'withdrawal_id'=>$id];
    }
    private function finishWalletClaim(string $owner,string $scope,string $ticket,array $packet,bool $commit): void {
        $row=$this->walletRequest($owner,$scope,$packet['withdrawal_id']);
        if($row['status']!=='delivering'||$row['ticket']!==$ticket)throw new MarketError(409,'Withdrawal reservation changed.');
        $remaining=json_decode($row['remaining'],true,512,JSON_THROW_ON_ERROR);
        if($commit){foreach($packet['balances'] as $paid)foreach($remaining as &$r)if($r['kind']===$paid['kind']&&($r['stat']??'')===($paid['stat']??'')){$r['quantity']-=$paid['quantity'];if($r['quantity']<0)throw new MarketError(409,'Withdrawal exceeds its reservation.');}unset($r);$remaining=array_values(array_filter($remaining,fn($r)=>$r['quantity']>0));}
        $this->run('UPDATE wallet_withdrawals SET status=?,remaining=?,ticket=\'\',revision=revision+1,message=\'\',updated=? WHERE id=?',[$remaining?'queued':'completed',self::json($remaining),time(),$row['id']]);
    }
    private function walletNotice(string $owner,string $scope,array $data): array {
        $id=self::ticketId($data['withdrawal_id']??null);$row=$this->walletRequest($owner,$scope,$id);
        $message=$data['message']??'';if($message!=='')self::text($message,512);
        if($row['status']==='queued')$this->run('UPDATE wallet_withdrawals SET message=?,updated=? WHERE id=?',[$message,time(),$id]);
        return ['noted'=>true];
    }
}
