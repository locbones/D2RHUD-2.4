<?php
declare(strict_types=1);

// Only the separately paired, closed-game save agent can attest deposits and
// acknowledge deliveries. Player commands can spend existing custody only.
trait MarketplaceEscrow {
    private function escrowSchema(string $engine): void {
        foreach([
            'custody_agents'=>'id CHAR(32) PRIMARY KEY, owner VARCHAR(64) NOT NULL UNIQUE, token_hash CHAR(64) NOT NULL UNIQUE, catalog CHAR(64) NOT NULL',
            'custody_serial'=>'scope VARCHAR(255) PRIMARY KEY, version INT NOT NULL',
            'custody_tickets'=>'id CHAR(32) PRIMARY KEY, owner VARCHAR(64) NOT NULL, scope VARCHAR(255) NOT NULL, agent CHAR(32) NOT NULL, kind VARCHAR(16) NOT NULL, hash CHAR(64) NOT NULL, status VARCHAR(16) NOT NULL, packet MEDIUMTEXT NOT NULL, proof MEDIUMTEXT NOT NULL, created BIGINT NOT NULL',
            'escrow_items'=>'item CHAR(32) PRIMARY KEY, owner VARCHAR(64) NOT NULL, scope VARCHAR(255) NOT NULL, state VARCHAR(16) NOT NULL, metadata MEDIUMTEXT NOT NULL, origin CHAR(32) NOT NULL',
            'escrow_balances'=>'owner VARCHAR(64) NOT NULL, scope VARCHAR(255) NOT NULL, kind VARCHAR(8) NOT NULL, stat VARCHAR(32) NOT NULL, quantity BIGINT NOT NULL, PRIMARY KEY(owner,scope,kind,stat)',
            'settlements'=>'id CHAR(32) PRIMARY KEY, offer VARCHAR(64) NOT NULL UNIQUE, scope VARCHAR(255) NOT NULL, seller VARCHAR(64) NOT NULL, buyer VARCHAR(64) NOT NULL, item CHAR(32) NOT NULL, price TEXT NOT NULL, created BIGINT NOT NULL',
            'market_preferences'=>'owner VARCHAR(64) NOT NULL, scope VARCHAR(255) NOT NULL, auto_trade INT NOT NULL, PRIMARY KEY(owner,scope)',
            'custody_sources'=>'owner VARCHAR(64) NOT NULL, scope VARCHAR(255) NOT NULL, source_key CHAR(64) NOT NULL, receipt CHAR(32) NOT NULL, PRIMARY KEY(owner,scope,source_key)',
            'custody_catalogs'=>'scope VARCHAR(255) PRIMARY KEY, catalog CHAR(64) NOT NULL',
        ] as $name=>$columns)$this->db->exec("CREATE TABLE IF NOT EXISTS $name ($columns)$engine");
    }
    private function lockScope(bool $hc): void {
        // INSERT IGNORE takes a shared duplicate-key lock on MySQL. Two
        // callers then upgrading it with FOR UPDATE can deadlock. The no-op
        // upsert acquires the exclusive scope lock in one operation instead.
        $sql=$this->db->getAttribute(PDO::ATTR_DRIVER_NAME)==='mysql'
            ?'INSERT INTO custody_serial VALUES (?,0) ON DUPLICATE KEY UPDATE version=version'
            :'INSERT OR IGNORE INTO custody_serial VALUES (?,0)';
        $this->run($sql,[$this->scope($hc)]);
        $this->run('SELECT version FROM custody_serial WHERE scope=?'.$this->lock(),[$this->scope($hc)])->fetch();
    }
    public function pairAgent(string $owner,string $catalog): array {
        return $this->transaction(function()use($owner,$catalog){
        $this->lockScope(false);$this->lockScope(true);
        if(!preg_match('/^[a-f0-9]{64}$/D',$catalog)||!$this->run("SELECT id FROM actors WHERE id=? AND role='player'",[$owner])->fetch())throw new MarketError(422,'Pair a player and a verified catalog fingerprint.');
        // Never rotate an agent while an operation could already have changed a save.
        if($this->run("SELECT id FROM custody_tickets WHERE owner=? AND scope IN (?,?) AND status IN ('prepared','writing')",[$owner,$this->scope(false),$this->scope(true)])->fetch())throw new MarketError(409,'Recover pending custody before replacing the agent.');
        $id=self::id();$token=bin2hex(random_bytes(32));
        $this->ensureMod($this->mod,$catalog);$this->joinMod($owner);
        $this->run('DELETE FROM custody_agents_scoped WHERE owner=? AND `mod`=? AND game_mode=?',[$owner,$this->mod,'2.4']);
        $this->run('INSERT INTO custody_agents_scoped VALUES (?,?,?,?,?,?)',[$id,$owner,hash('sha256',$token),$catalog,$this->mod,'2.4']);
        return ['id'=>$id,'owner'=>$owner,'catalog'=>$catalog,'token'=>$token,'mod'=>$this->mod,'game_mode'=>'2.4'];
        });
    }
    public function authAgent(string $token): array {
        if(!preg_match('/^[a-f0-9]{64}$/D',$token))throw new MarketError(401,'Save-agent authentication required.');
        $agent=$this->run('SELECT id,owner,catalog,`mod`,game_mode FROM custody_agents_scoped WHERE token_hash=?',[hash('sha256',$token)])->fetch();
        if(!$agent)throw new MarketError(401,'Save-agent authentication required.');return $agent;
    }
    private static function json(array $value): string {return json_encode($value,JSON_THROW_ON_ERROR|JSON_UNESCAPED_UNICODE);}
    private static function digest(mixed $value): string {
        if(!is_string($value)||!preg_match('/^[a-f0-9]{64}$/D',$value))throw new MarketError(422,'Invalid SHA256 fingerprint.');return $value;
    }
    private static function ticketId(mixed $value): string {
        if(!is_string($value)||!preg_match('/^[a-f0-9]{32}$/D',$value))throw new MarketError(422,'Invalid custody transaction ID.');return $value;
    }
    private function custodyItem(array $r): array {
        $display=$this->displayItem($r);
        $hex=self::text($r['item_hex']??null,65536);
        if(strlen($hex)%2||strlen($hex)<14||!preg_match('/^[0-9a-f]+$/D',$hex))throw new MarketError(422,'Invalid custody item bytes.');
        $code=self::text($r['code']??null,4);if(!preg_match('/^[\x21-\x7e]{1,4}$/D',$code)||$code==='gld')throw new MarketError(422,'Invalid custody item code.');
        $lineage=isset($r['ledger_id'])?self::ticketId($r['ledger_id']):null;
        $r=['item_hex'=>$hex,'code'=>$code,'saved_id'=>self::number($r['saved_id']??null,0,4294967295),
            'quality'=>self::number($r['quality']??null,1,9),'index'=>self::number($r['index']??-1,-1,1000000),
            'ethereal'=>self::number($r['ethereal']??null,0,1),'identified'=>self::number($r['identified']??null,0,1),
            'sockets'=>self::number($r['sockets']??0,0,6),'quantity'=>self::number($r['quantity']??null,1,511),
            'quantity_bit'=>self::number($r['quantity_bit']??-1,-1,262144),'tab'=>self::number($r['tab']??null,1,7),
            'page'=>self::number($r['page']??null,1,64),'price'=>isset($r['price'])?self::price($r['price']):[]];
        if($r['quantity_bit']!==-1){
            if($r['quality']!==2||$r['sockets']||$r['quantity_bit']+9>strlen($hex)*4||self::readBits($hex,$r['quantity_bit'],9)!==$r['quantity'])throw new MarketError(422,'Unsafe stack metadata.');
        }elseif($r['quantity']!==1)throw new MarketError(422,'Stack edit metadata is required.');
        if($native=$this->itemIdentity($hex)){$r['saved_id']=$native['saved_id'];$r['item_uuid']=$native['item_uuid'];}
        if($lineage)$r['ledger_id']=$lineage;$r['display']=$display;return $r;
    }
    private static function readBits(string $hex,int $at,int $n): int {
        $bytes=hex2bin($hex);$value=0;for($i=0;$i<$n;$i++)$value|=((ord($bytes[intdiv($at+$i,8)])>>(($at+$i)%8))&1)<<$i;return $value;
    }
    private static function patchBits(string $hex,int $at,int $n,int $value): string {
        $bytes=hex2bin($hex);for($i=0;$i<$n;$i++){$p=intdiv($at+$i,8);$mask=1<<(($at+$i)%8);$bytes[$p]=chr((ord($bytes[$p])&~$mask)|((($value>>$i)&1)?$mask:0));}return bin2hex($bytes);
    }
    private static function positionless(string $hex): string {return self::patchBits($hex,35,18,0);}
    private function balance(string $owner,string $scope,array $row,int $delta,string $action='adjustment',string $reference=''): void {
        $kind=$row['kind'];$stat=$row['stat']??'';
        $old=$this->run('SELECT quantity FROM escrow_balances WHERE owner=? AND scope=? AND kind=? AND stat=?',[$owner,$scope,$kind,$stat])->fetchColumn();
        $amount=($old===false?0:(int)$old)+$delta;if($amount<0||$amount>9000000000000000)throw new MarketError(409,'Insufficient escrow balance or balance limit.');
        if($old===false)$this->run('INSERT INTO escrow_balances VALUES (?,?,?,?,?)',[$owner,$scope,$kind,$stat,$amount]);
        else $this->run('UPDATE escrow_balances SET quantity=? WHERE owner=? AND scope=? AND kind=? AND stat=?',[$amount,$owner,$scope,$kind,$stat]);
        if($delta)$this->run('INSERT INTO wallet_entries (owner,scope,kind,stat,amount,available,action,reference,created) VALUES (?,?,?,?,?,?,?,?,?)',[$owner,$scope,$kind,$stat,$delta,$amount,$action,$reference,time()]);
    }
    private function noPending(string $owner,string $scope): void {
        if($this->run("SELECT id FROM custody_tickets WHERE owner=? AND scope=? AND status IN ('prepared','writing')",[$owner,$scope])->fetch())throw new MarketError(409,'Recover the pending save transaction first.');
    }
    private function ticket(array $row): array {return ['id'=>$row['id'],'kind'=>$row['kind'],'status'=>$row['status'],'packet'=>json_decode($row['packet'],true,512,JSON_THROW_ON_ERROR),'proof'=>json_decode($row['proof'],true,512,JSON_THROW_ON_ERROR)];}
    private function proofs(mixed $rows): array {
        if(!is_array($rows)||!array_is_list($rows)||count($rows)<1||count($rows)>2)throw new MarketError(422,'Choose one or two exact save pages.');
        $out=[];foreach($rows as $r){$page=self::number($r['page']??null,1,64);if(isset($out[$page]))throw new MarketError(422,'Duplicate save page.');$out[$page]=['page'=>$page,'before'=>self::digest($r['before']??null),'after'=>self::digest($r['after']??null)];if($out[$page]['before']===$out[$page]['after'])throw new MarketError(422,'The custody save did not change.');}return array_values($out);
    }
    public function custody(array $agent,array $data,bool $hc): array {
        if(($data['type']??'')==='profile_sync')return $this->publishCharacter($agent,$data,$hc);
        if(($data['type']??'')==='profile_state')return ['linked'=>$this->profileAgent($agent,$data)];
        if(($data['type']??'')==='profile_manifest')return $this->characterManifest($agent,$data,$hc);
        if(($data['type']??'')==='browser_link')return $this->browserFolderLink($agent,$data);
        if(($data['type']??'')==='web_sync')return $this->browserAgentSync($agent,$data,$hc);
        $this->requireAgentMod($agent);$id=self::ticketId($data['id']??null);$type=$data['type']??'';
        return $this->transaction(function()use($agent,$data,$hc,$id,$type){
            $this->lockScope($hc);$scope=$this->scope($hc);$owner=$agent['owner'];
            if(!$this->run('SELECT id FROM custody_agents_scoped WHERE id=? AND owner=?',[$agent['id'],$owner])->fetch())throw new MarketError(401,'The save-agent pairing was replaced.');
            if(($data['catalog']??null)!==$agent['catalog'])throw new MarketError(409,'The installed save catalog changed. Revalidate the agent.');
            $insert=$this->db->getAttribute(PDO::ATTR_DRIVER_NAME)==='mysql'?'INSERT IGNORE':'INSERT OR IGNORE';
            $this->run("$insert INTO custody_catalogs VALUES (?,?)",[$scope,$agent['catalog']]);
            if($this->run('SELECT catalog FROM custody_catalogs WHERE scope=?',[$scope])->fetchColumn()!==$agent['catalog'])throw new MarketError(409,'This save catalog differs from the community custody catalog.');
            $this->touchCompanion($owner,$scope);
            if($type==='wallet_notice')return $this->walletNotice($owner,$scope,$data);
            // Read-only queue polling must not replace the private inventory
            // snapshot when the deposit page is currently loaded in the game.
            if($type==='delivery_queue')return ['delivery_items'=>$this->requestedDeliveries($owner,$scope),'wallet_withdrawals'=>$this->walletRows($owner,$scope,true)];
            $old=$this->run('SELECT * FROM custody_tickets WHERE id=?',[$id])->fetch();
            if($old&&($old['owner']!==$owner||$old['scope']!==$scope||$old['agent']!==$agent['id']))throw new MarketError(404,'Custody transaction not found.');
            if($type==='status'){if(!$old)throw new MarketError(404,'Custody transaction not found.');return $this->ticket($old);}
            if(in_array($type,['deposit_prepare','claim_prepare'],true)){
                $hash=hash('sha256',self::json($data));
                if($old){if(!hash_equals($old['hash'],$hash))throw new MarketError(409,'Custody ID reused with different content.');return $this->ticket($old);}
                $this->noPending($owner,$scope);
                if($type==='deposit_prepare'){
                    $proof=$this->proofs($data['pages']??null);$items=$data['items']??[];$balances=$data['balances']??[];
                    if(!is_array($items)||!array_is_list($items)||count($items)>64)throw new MarketError(422,'Deposit at most 64 items per transaction.');
                    $items=array_map(fn($r)=>$this->custodyItem($r),$items);
                    if($balances)$balances=self::price($balances);foreach($balances as $b)if(!in_array($b['kind']??'item',['gold','bag'],true))throw new MarketError(422,'Use serialized items for item deposits.');
                    if(!$items&&!$balances)throw new MarketError(422,'Empty deposit.');
                    $pages=array_column($proof,'page');foreach($items as $item)if(!in_array($item['page'],$pages,true))throw new MarketError(422,'Item page has no save proof.');
                    $this->reserveIdentities($owner,$scope,$id,$items);
                    // Validate every legacy explicit lineage before authorizing
                    // any save removal, even when no native UUID is available.
                    foreach($items as $r)if(isset($r['ledger_id'])){
                        $v=$this->run('SELECT e.owner,e.scope,e.state,i.item_hex FROM escrow_items e JOIN items i ON i.id=e.item WHERE e.item=?',[$r['ledger_id']])->fetch();
                        if(!$v||$v['owner']!==$owner||$v['scope']!==$scope||$v['state']!=='delivered'||self::positionless($v['item_hex'])!==self::positionless($r['item_hex']))throw new MarketError(409,'Delivered item lineage does not match this owner and exact item. No save was changed.');
                    }
                    $packet=['items'=>$items,'balances'=>$balances];$kind='deposit';
                }else{
                    $packet=$this->reserveClaim($owner,$scope,$id,$data);$proof=[];$kind='claim';
                }
                $this->run('INSERT INTO custody_tickets VALUES (?,?,?,?,?,?,?,?,?,?)',[$id,$owner,$scope,$agent['id'],$kind,$hash,'prepared',self::json($packet),self::json($proof),time()]);
            }else{
                if(!$old)throw new MarketError(404,'Custody transaction not found.');
                $packet=json_decode($old['packet'],true,512,JSON_THROW_ON_ERROR);$proof=json_decode($old['proof'],true,512,JSON_THROW_ON_ERROR);
                if($type==='cancel'){
                    if($old['status']==='cancelled')return $this->ticket($old);
                    if($old['status']!=='prepared')throw new MarketError(409,'A save write may have started. Recover instead of cancelling.');
                    if($old['kind']==='claim')$this->releaseClaim($owner,$scope,$id,$packet);
                    $this->run("UPDATE custody_tickets SET status='cancelled' WHERE id=?",[$id]);
                    $this->run('DELETE FROM identity_reservations WHERE ticket=?',[$id]);
                    $this->settleReady($hc,null,$owner);
                }elseif($type==='write_authorize'){
                    $submitted=$this->proofs($data['pages']??null);
                    if($old['status']==='committed')return $this->ticket($old);
                    if($old['status']==='cancelled')throw new MarketError(409,'Custody transaction was cancelled.');
                    if($proof&&$proof!==$submitted)throw new MarketError(409,'Save plan changed.');
                    if($old['kind']==='claim')$this->checkDelivered($packet,$data['delivered']??null);
                    if($old['status']==='writing'&&isset($packet['delivered'])&&$packet['delivered']!==$data['delivered'])throw new MarketError(409,'Delivery layout changed after authorization.');
                    if($old['kind']==='claim')$packet['delivered']=$data['delivered'];
                    $this->run("UPDATE custody_tickets SET status='writing',proof=?,packet=? WHERE id=?",[self::json($submitted),self::json($packet),$id]);
                }elseif($type==='commit'){
                    if($old['status']==='committed')return $this->ticket($old);
                    if($old['status']!=='writing')throw new MarketError(409,'Authorize the durable save plan before committing.');
                    if(($data['pages']??null)!==$proof)throw new MarketError(409,'Committed save fingerprints do not match the authorized write.');
                    if($old['kind']==='deposit')$this->activateDeposit($owner,$scope,$id,$packet);
                    else $this->completeClaim($owner,$scope,$id,$packet);
                    $this->run("UPDATE custody_tickets SET status='committed' WHERE id=?",[$id]);
                    $this->run('DELETE FROM identity_reservations WHERE ticket=?',[$id]);
                    $this->settleReady($hc,null,$owner);
                }else throw new MarketError(422,'Unknown custody operation.');
            }
            return $this->ticket($this->run('SELECT * FROM custody_tickets WHERE id=?',[$id])->fetch());
        });
    }
    private function activateDeposit(string $owner,string $scope,string $receipt,array $packet): void {
        $actor=['id'=>$owner];
        foreach($packet['items'] as $r){
            $key=hash('sha256',$r['page'].'|'.$r['tab'].'|'.$r['item_hex']);
            $insert=$this->db->getAttribute(PDO::ATTR_DRIVER_NAME)==='mysql'?'INSERT IGNORE':'INSERT OR IGNORE';
            $this->run("$insert INTO custody_sources VALUES (?,?,?,?)",[$owner,$scope,$key,$receipt]);
            $p=$this->run('SELECT * FROM published_items WHERE owner=? AND scope=? AND source_key=?',[$owner,$scope,$key])->fetch();
            $lineage=$r['ledger_id']??null;$returning=null;
            if($lineage){
                $returning=$this->run('SELECT e.*,i.item_hex FROM escrow_items e JOIN items i ON i.id=e.item WHERE e.item=?',[$lineage])->fetch();
                if(!$returning||$returning['owner']!==$owner||$returning['scope']!==$scope||$returning['state']!=='delivered'||self::positionless($returning['item_hex'])!==self::positionless($r['item_hex']))throw new MarketError(409,'Delivered item lineage no longer matches this owner and exact item.');
            }
            $item=$lineage??($p?$p['item']:self::id());
            if($p){
                if(!$lineage&&$this->run('SELECT item FROM escrow_items WHERE item=?',[$item])->fetch())throw new MarketError(409,'This item already entered escrow. Reconcile the local save.');
                $i=$this->run('SELECT owner,item_hex FROM items WHERE id=?',[$p['item']])->fetch();if(!$i||$i['owner']!==$owner||$i['item_hex']!==$r['item_hex'])throw new MarketError(409,'Published item ownership changed.');
                $this->invalidateOffers($p['listing'],$actor,'This item entered custody. Review and submit a funded offer.');
                $this->run("UPDATE listings SET status='withdrawn',revision=revision+1 WHERE id=?",[$p['listing']]);
                $this->run('DELETE FROM published_items WHERE listing=?',[$p['listing']]);
            }elseif(!$returning){
                // A delivered item can return to custody under its stable ledger ID.
                // Explicit lineage is required; never merge by seed alone.
                $this->run('INSERT INTO items VALUES (?,?,?,?,?,?,?,?,?,?)',[$item,$scope,$r['saved_id'],$r['code'],$r['quality'],$r['index'],$r['ethereal'],$r['sockets'],$owner,$r['item_hex']]);
            }
            $state=$r['price']?'listed':'available';
            if($returning){$this->run('UPDATE escrow_items SET state=?,metadata=?,origin=? WHERE item=?',[$state,self::json($r),$receipt,$item]);$this->run('UPDATE items SET item_hex=? WHERE id=?',[$r['item_hex'],$item]);}
            else $this->run('INSERT INTO escrow_items VALUES (?,?,?,?,?,?)',[$item,$owner,$scope,$state,self::json($r),$receipt]);
            if($r['price']){
                $listing=$p?$p['listing']:self::id();
                if($p)$this->run("UPDATE listings SET item=?,status='open',price=?,revision=revision+1 WHERE id=?",[$item,self::json($r['price']),$listing]);
                else $this->run('INSERT INTO listings VALUES (?,?,?,?,?,?,?)',[$listing,$item,$owner,1,'open',self::json($r['price']),time()]);
            }
            $this->recordIdentity($item,$scope,$r['item_hex']);
            $this->event($item,null,$actor,'escrow_deposit','Save removal acknowledged; item is held in escrow.',$r['price'],$owner);
        }
        foreach($packet['balances'] as $b)$this->balance($owner,$scope,$b,$b['quantity'],'deposit',$receipt);
    }
    private function reserveClaim(string $owner,string $scope,string $id,array $data): array {
        if(isset($data['withdrawal_id']))return $this->reserveWalletClaim($owner,$scope,$id,$data);
        $limit=self::number($data['item_limit']??64,0,64);$caps=null;
        $selector='';$args=[$owner,$scope];
        if(isset($data['item_ids'])){
            if(!is_array($data['item_ids'])||!array_is_list($data['item_ids'])||count($data['item_ids'])>64)throw new MarketError(422,'Invalid delivery item selection.');
            $ids=array_values(array_unique(array_map(fn($v)=>self::ticketId($v),$data['item_ids'])));
            $selector=$ids?' AND e.item IN ('.implode(',',array_fill(0,count($ids),'?')).')':' AND 0=1';
            $args=array_merge($args,$ids);
        }
        if(isset($data['balance_caps'])){$caps=[];foreach($data['balance_caps']?self::price($data['balance_caps']):[] as $r){if(!in_array($r['kind']??'item',['gold','bag'],true))throw new MarketError(422,'Delivery capacities must be resources.');$caps[$r['kind'].'|'.($r['stat']??'')]=$r['quantity'];}}
        $items=$this->run("SELECT e.item,i.item_hex FROM escrow_items e JOIN items i ON i.id=e.item WHERE e.owner=? AND e.scope=? AND e.state='available'$selector ORDER BY e.item LIMIT $limit",$args)->fetchAll();
        foreach($items as $i)$this->run("UPDATE escrow_items SET state='claiming',origin=? WHERE item=?",[$id,$i['item']]);
        $balances=[];
        foreach($this->run('SELECT kind,stat,quantity FROM escrow_balances WHERE owner=? AND scope=? AND quantity>0 ORDER BY kind,stat LIMIT 16',[$owner,$scope])->fetchAll() as $b){
            $q=min((int)$b['quantity'],2000000000,$caps===null?2000000000:($caps[$b['kind'].'|'.$b['stat']]??0));if(!$q)continue;$r=['kind'=>$b['kind'],'code'=>$b['kind']==='gold'?'gld':'Z01','label'=>$b['kind']==='gold'?'Gold':$b['stat'],'quality'=>2,'index'=>-1,'ethereal'=>-1,'quantity'=>$q];if($b['kind']==='bag')$r['stat']=$b['stat'];
            $balances[]=$r;$this->balance($owner,$scope,$r,-$q,'delivery_reserved',$id);
        }
        if(!$items&&!$balances)throw new MarketError(409,'No unlisted escrow assets are ready for delivery.');
        return ['items'=>$items,'balances'=>$balances];
    }
    private function releaseClaim(string $owner,string $scope,string $id,array $packet): void {
        if(isset($packet['withdrawal_id'])){$this->finishWalletClaim($owner,$scope,$id,$packet,false);return;}
        $this->run("UPDATE escrow_items SET state='available' WHERE owner=? AND scope=? AND state='claiming' AND origin=?",[$owner,$scope,$id]);
        foreach($packet['balances'] as $b)$this->balance($owner,$scope,$b,$b['quantity'],'delivery_cancelled',$id);
    }
    private function checkDelivered(array $packet,mixed $delivered): void {
        if(!is_array($delivered)||!array_is_list($delivered)||count($delivered)!==count($packet['items']))throw new MarketError(422,'Every claim item needs its exact placed bytes.');
        $expected=array_column($packet['items'],'item_hex','item');$seen=[];
        foreach($delivered as $d){$id=$d['item']??'';$hex=$d['item_hex']??'';
            if(isset($seen[$id])||!isset($expected[$id])||!is_string($hex)||strlen($hex)!==strlen($expected[$id])||!preg_match('/^[0-9a-f]+$/D',$hex)||self::positionless($hex)!==self::positionless($expected[$id]))throw new MarketError(422,'Delivery altered item properties or identity.');$seen[$id]=true;
        }
    }
    private function completeClaim(string $owner,string $scope,string $id,array $packet): void {
        if(isset($packet['withdrawal_id']))$this->finishWalletClaim($owner,$scope,$id,$packet,true);
        foreach($packet['delivered'] as $d){
            $e=$this->run('SELECT state,origin,metadata FROM escrow_items WHERE item=? AND owner=? AND scope=?',[$d['item'],$owner,$scope])->fetch();
            if(!$e||$e['state']!=='claiming'||$e['origin']!==$id)throw new MarketError(409,'Delivery reservation changed.');
            $r=json_decode($e['metadata'],true,512,JSON_THROW_ON_ERROR);$r['item_hex']=$d['item_hex'];unset($r['delivery_requested']);
            $this->run("UPDATE escrow_items SET state='delivered',metadata=? WHERE item=?",[self::json($r),$d['item']]);
            $this->run('UPDATE items SET item_hex=? WHERE id=?',[$d['item_hex'],$d['item']]);
            $this->event($d['item'],null,['id'=>$owner],'delivered','Delivery save write acknowledged.');
        }
    }
    public function escrowCommand(array $actor,array $data,bool $hc): array {
        $id=self::text($data['id']??null,64);$hash=hash('sha256',self::json($data));
        return $this->transaction(function()use($actor,$data,$hc,$id,$hash){
            $this->lockScope($hc);$owner=$actor['id'];$scope=$this->scope($hc);
            $old=$this->run('SELECT hash,result FROM commands WHERE actor=? AND id=?',[$owner,$id])->fetch();
            if($old){if(!hash_equals($old['hash'],$hash))throw new MarketError(409,'Command ID reused.');return json_decode($old['result'],true,512,JSON_THROW_ON_ERROR);}
            $type=$data['type']??'';
            if($type==='preferences'){
                if(!is_bool($data['auto_trade']??null))throw new MarketError(422,'Choose automatic approval on or off.');
                // A queued website setup owns this choice until the closed-game
                // companion applies it. An older live sidecar cannot undo it.
                $queued=$this->run("SELECT packet FROM web_intents WHERE owner=? AND scope=? AND kind='setup' AND status='queued' ORDER BY seq DESC LIMIT 1",[$owner,$scope])->fetchColumn();
                $approval=$queued?json_decode($queued,true,512,JSON_THROW_ON_ERROR):null;
                $this->run('DELETE FROM market_preferences WHERE owner=? AND scope=?',[$owner,$scope]);$this->run('INSERT INTO market_preferences VALUES (?,?,?)',[$owner,$scope,(int)($approval?($approval['enabled']&&$approval['auto_trade']):$data['auto_trade'])]);$r=['saved'=>true];
            }else{
                $item=self::ticketId($data['item_id']??null);$this->requireSingleItem($item);$e=$this->run('SELECT * FROM escrow_items WHERE item=? AND owner=? AND scope=?',[$item,$owner,$scope])->fetch();
                if(!$e||!in_array($e['state'],['available','listed'],true))throw new MarketError(409,'This item cannot be changed while delivering or after delivery.');
                $listings=$this->run("SELECT id FROM listings WHERE item=? AND status IN ('open','agreed')",[$item])->fetchAll();
                foreach($listings as $l){$this->invalidateOffers($l['id'],$actor,'The seller changed or withdrew this listing.');$this->run("UPDATE listings SET status='withdrawn',revision=revision+1 WHERE id=?",[$l['id']]);}
                if($type==='escrow_price'){
                    $price=self::price($data['price']??null);$listing=self::id();$this->run('INSERT INTO listings VALUES (?,?,?,?,?,?,?)',[$listing,$item,$owner,1,'open',self::json($price),time()]);$state='listed';$r=['listing_id'=>$listing];
                }elseif($type==='escrow_return'){$state='available';$price=[];$r=['ready_for_delivery'=>true];}
                else throw new MarketError(422,'Unknown escrow command.');
                $metadata=json_decode($e['metadata'],true,512,JSON_THROW_ON_ERROR);$metadata['price']=$price;
                if($type==='escrow_return')$metadata['delivery_requested']=true;
                else unset($metadata['delivery_requested']);
                $this->run('UPDATE escrow_items SET state=?,metadata=? WHERE item=?',[$state,self::json($metadata),$item]);
                $this->event($item,null,$actor,$state==='listed'?'price_changed':'unlisted',$state==='listed'?'Escrow asking price updated.':'Item queued for return.',$price,$owner);
            }
            $this->settleReady($hc);$this->run('INSERT INTO commands VALUES (?,?,?,?)',[$owner,$id,$hash,self::json($r)]);return $r;
        });
    }
    private static function samePrice(array $a,array $b): bool {
        $normalize=function($rows){$out=[];foreach($rows as $r){unset($r['label']);ksort($r);$out[]=self::json($r);}sort($out);return $out;};return $normalize($a)===$normalize($b);
    }
    private function allocation(string $buyer,string $scope,array $price): ?array {
        $out=[];$used=[];
        foreach($price as $r){$remaining=$r['quantity'];
            if(($r['kind']??'')==='free')continue;
            if(($r['kind']??'item')!=='item'){
                $q=$this->run('SELECT quantity FROM escrow_balances WHERE owner=? AND scope=? AND kind=? AND stat=?',[$buyer,$scope,$r['kind'],$r['stat']??''])->fetchColumn();
                if($q===false||(int)$q<$remaining)return null;$out[]=['balance'=>$r];continue;
            }
            foreach($this->run("SELECT e.*,i.code,i.quality,i.item_index,i.ethereal FROM escrow_items e JOIN items i ON i.id=e.item WHERE e.owner=? AND e.scope=? AND e.state='available' ORDER BY e.item",[$buyer,$scope])->fetchAll() as $i){
                $m=json_decode($i['metadata'],true,512,JSON_THROW_ON_ERROR);
                if(isset($used[$i['item']])||!$m['identified']||$i['code']!==$r['code']||(int)$i['quality']!==$r['quality']||(int)$i['item_index']!==$r['index']||($r['ethereal']!==-1&&(int)$i['ethereal']!==$r['ethereal']))continue;
                $q=min($remaining,$m['quantity']);if($q!==$m['quantity']&&$m['quantity_bit']===-1)continue;
                $out[]=['item'=>$i,'metadata'=>$m,'quantity'=>$q];$used[$i['item']]=true;$remaining-=$q;if(!$remaining)break;
            }
            if($remaining)return null;
        }return $out;
    }
    private function settleReady(bool $hc,?string $focus=null,?string $funded=null): void {
        $scope=$this->scope($hc);
        $queue=[[$focus,$funded]];$queued=[];$budget=256;$passes=0;
        while($queue&&$budget>0&&++$passes<=513){
        [$focus,$funded]=array_shift($queue);unset($queued[$funded??'*']);
        $selector=$focus?' AND o.id=?':($funded?' AND o.buyer=?':'');$args=[$scope];if($focus||$funded)$args[]=$focus??$funded;
        $offers=$this->run("SELECT o.*,l.item,l.price AS asking,l.status AS listing_status FROM offers o JOIN listings l ON l.id=o.listing JOIN escrow_items e ON e.item=l.item WHERE e.scope=? AND e.state='listed' AND e.owner=o.seller AND o.status IN ('accepted','pending')$selector ORDER BY o.created,o.id LIMIT 256",$args)->fetchAll();
        foreach($offers as $o){
            if($budget<=0)break;
            if((int)$this->run("SELECT COUNT(*) FROM actors WHERE role='player' AND id IN (?,?)",[$o['buyer'],$o['seller']])->fetchColumn()!==2)continue;
            $current=$this->run('SELECT l.status,e.state,e.owner FROM listings l JOIN escrow_items e ON e.item=l.item WHERE l.id=?',[$o['listing']])->fetch();
            $status=$this->run('SELECT status FROM offers WHERE id=?',[$o['id']])->fetchColumn();
            if(!$current||$current['state']!=='listed'||$current['owner']!==$o['seller']||$status!==$o['status'])continue;
            $price=json_decode($o['price'],true,512,JSON_THROW_ON_ERROR);
            if($o['status']==='pending'){
                $auto=$this->run('SELECT auto_trade FROM market_preferences WHERE owner=? AND scope=?',[$o['seller'],$scope])->fetchColumn();
                if((!$auto&&!self::isFree($price))||$o['proposer']!==$o['buyer']||$o['listing_status']!=='open'||!self::samePrice($price,json_decode($o['asking'],true,512,JSON_THROW_ON_ERROR)))continue;
            }elseif($o['listing_status']!=='agreed')continue;
            // A reserved claim cannot also fund a trade. Allocation is all-or-nothing.
            $allocation=$this->allocation($o['buyer'],$scope,$price);if($allocation===null)continue;
            $members=$this->bundleGoods($o['listing'],$o['item']);
            foreach($members as $member)if(!$this->run("SELECT e.item FROM escrow_items e JOIN items i ON i.id=e.item WHERE e.item=? AND e.owner=? AND i.owner=? AND e.scope=? AND e.state='listed'",[$member,$o['seller'],$o['seller'],$scope])->fetchColumn())continue 2;
            $receipt=self::id();
            foreach($allocation as $a){
                if(isset($a['balance'])){$b=$a['balance'];$this->balance($o['buyer'],$scope,$b,-$b['quantity'],'trade_payment',$receipt);$this->balance($o['seller'],$scope,$b,$b['quantity'],'trade_proceeds',$receipt);continue;}
                $m=$a['metadata'];$item=$a['item']['item'];$q=$a['quantity'];
                if($q<$m['quantity']){
                    $remaining=$m;$remaining['quantity']-=$q;$remaining['item_hex']=self::patchBits($m['item_hex'],$m['quantity_bit'],9,$remaining['quantity']);
                    $this->run('UPDATE escrow_items SET metadata=? WHERE item=?',[self::json($remaining),$item]);$this->run('UPDATE items SET item_hex=? WHERE id=?',[$remaining['item_hex'],$item]);
                    $child=self::id();$m['quantity']=$q;$m['item_hex']=self::patchBits($m['item_hex'],$m['quantity_bit'],9,$q);
                    $native=$this->itemIdentity($m['item_hex']);
                    if($native&&$native['uuid_bit']!==null&&$native['item_uuid']!==''){
                        $fresh=bin2hex(random_bytes(16));for($n=0;$n<16;$n++)$m['item_hex']=self::patchBits($m['item_hex'],$native['uuid_bit']+8*$n,8,hexdec(substr($fresh,2*$n,2)));$m['item_uuid']=$fresh;
                    }elseif($native&&$native['saved_id']){
                        // Match the game's native StackSplit behavior: a new
                        // legacy fragment gets a separate saved fingerprint;
                        // already-saved modifiers are not rerolled.
                        do{$seed=random_int(1,4294967295);$candidate=self::patchBits($m['item_hex'],$native['seed_bit'],32,$seed);$key=$this->identityKey($candidate);}
                        while($this->run('SELECT item FROM item_identities WHERE scope=? AND identity_key=?',[$scope,$key])->fetch()||$this->run('SELECT ticket FROM identity_reservations WHERE scope=? AND identity_key=?',[$scope,$key])->fetch());
                        $m['item_hex']=$candidate;$m['saved_id']=$seed;
                    }
                    $m['delivery_requested']=true;
                    $this->run('INSERT INTO items VALUES (?,?,?,?,?,?,?,?,?,?)',[$child,$scope,$m['saved_id'],$m['code'],$m['quality'],$m['index'],$m['ethereal'],$m['sockets'],$o['seller'],$m['item_hex']]);
                    $this->run('INSERT INTO escrow_items VALUES (?,?,?,?,?,?)',[$child,$o['seller'],$scope,'available',self::json($m),$receipt]);$this->recordIdentity($child,$scope,$m['item_hex']);$this->event($child,null,['id'=>$o['buyer']],'stack_split','Payment fragment split from ledger '.$item.'.',[],$o['buyer'],$o['seller']);
                }else{$m['delivery_requested']=true;$this->run('UPDATE escrow_items SET owner=?,metadata=? WHERE item=?',[$o['seller'],self::json($m),$item]);$this->run('UPDATE items SET owner=? WHERE id=?',[$o['seller'],$item]);}
            }
            foreach($members as $member){
                $goods=json_decode($this->run("SELECT metadata FROM escrow_items WHERE item=?",[$member])->fetchColumn(),true,512,JSON_THROW_ON_ERROR);$goods['price']=[];$goods['delivery_requested']=true;
                $this->run("UPDATE escrow_items SET owner=?,state='available',metadata=? WHERE item=?",[$o['buyer'],self::json($goods),$member]);$this->run('UPDATE items SET owner=? WHERE id=?',[$o['buyer'],$member]);
                if($member!==$o['item'])$this->event($member,$o['id'],['id'=>$o['seller']],'bundle_sale','Included in bundle settlement '.$receipt.'. Full price covers the entire bundle.',$price,$o['seller'],$o['buyer'],hash('sha256',$receipt.'|'.$member));
            }
            $this->run("UPDATE listings SET status='sold',revision=revision+1 WHERE id=?",[$o['listing']]);
            $this->invalidateOffers($o['listing'],['id'=>$o['seller']],'Another proposal completed this trade.',$o['id']);
            $this->run("UPDATE offers SET status='completed',revision=revision+1 WHERE id=?",[$o['id']]);
            $this->run('INSERT INTO settlements VALUES (?,?,?,?,?,?,?,?)',[$receipt,$o['id'],$scope,$o['seller'],$o['buyer'],$o['item'],self::json($price),time()]);
            $this->event($o['item'],$o['id'],['id'=>$o['seller']],'completed_sale','Ownership and payment exchanged atomically in escrow. Switch away from the delivery page while the companion syncs, then switch back.',$price,$o['seller'],$o['buyer'],$receipt);
            --$budget;
            // Proceeds can fund another already-agreed trade even if that
            // recipient's computer is asleep. Drain those dependencies here.
            foreach([$o['seller'],$o['buyer']] as $wake)if(!isset($queued[$wake])){$queue[]=[null,$wake];$queued[$wake]=true;}
        }
        }
    }
    private function requestedDeliveries(string $owner,string $scope): array {
        $ids=[];
        foreach($this->run("SELECT item,metadata FROM escrow_items WHERE owner=? AND scope=? AND state='available' ORDER BY item",[$owner,$scope])->fetchAll() as $row){
            $metadata=json_decode($row['metadata'],true,512,JSON_THROW_ON_ERROR);
            if(($metadata['delivery_requested']??false)===true)$ids[]=$row['item'];
            if(count($ids)===64)break;
        }
        return $ids;
    }
    private function escrowSnapshot(string $owner,string $scope): array {
        $items=$this->run("SELECT e.item,e.state,e.metadata,i.item_hex FROM escrow_items e JOIN items i ON i.id=e.item WHERE e.owner=? AND e.scope=? AND e.state<>'delivered' ORDER BY e.item LIMIT 4096",[$owner,$scope])->fetchAll();
        foreach($items as &$i){$i['metadata']=json_decode($i['metadata'],true,512,JSON_THROW_ON_ERROR);}unset($i);
        $balances=$this->run('SELECT kind,stat,quantity FROM escrow_balances WHERE owner=? AND scope=? AND quantity>0',[$owner,$scope])->fetchAll();foreach($balances as &$b)$b['quantity']=(int)$b['quantity'];unset($b);
        $lineage=$this->run("SELECT e.item,i.item_hex FROM escrow_items e JOIN items i ON i.id=e.item WHERE e.owner=? AND e.scope=? AND e.state='delivered' ORDER BY e.item LIMIT 4096",[$owner,$scope])->fetchAll();
        return ['wallet'=>$this->walletSnapshot($owner,$scope),'items'=>$items,'balances'=>$balances,'lineage'=>$lineage,'enrolled'=>(bool)$this->run('SELECT id FROM custody_agents_scoped WHERE owner=? AND `mod`=? AND game_mode=?',[$owner,$this->mod,'2.4'])->fetchColumn(),'auto_trade'=>(bool)$this->run('SELECT auto_trade FROM market_preferences WHERE owner=? AND scope=?',[$owner,$scope])->fetchColumn()];
    }
}
