<?php
declare(strict_types=1);

// Browser-only delegation. The authenticated owner never becomes the target:
// each request rechecks both identities and retains the real author in audit.
trait MarketplaceOwnerImpersonation {
    private function impersonationRecord(array $owner,string $id): array {
        $row=$this->run("SELECT * FROM market_owner_actions WHERE id=? AND owner=? AND kind='impersonate_start'",[self::ticketId($id),$owner['id']])->fetch();
        if(!$row)throw new MarketError(404,'Impersonation session not found.');
        return json_decode($row['result'],true,512,JSON_THROW_ON_ERROR);
    }
    public function startImpersonation(array $owner,array $data,bool $hc): array {
        $this->requireOwner($owner);
        return $this->transaction(function()use($owner,$data,$hc){
            $this->requireOwner($owner);$id=self::ticketId($data['id']??null);$hash=hash('sha256',self::json([$this->scope($hc),$data]));
            $old=$this->run('SELECT * FROM market_owner_actions WHERE id=?',[$id])->fetch();
            if($old){if($old['owner']!==$owner['id']||$old['kind']!=='impersonate_start'||!hash_equals($old['hash'],$hash))throw new MarketError(409,'Command ID reused.');return $this->impersonationRecord($owner,$id);}
            $target=$this->resolveLinkedPlayer(self::text($data['player']??null,64));
            if($target['id']===$owner['id'])throw new MarketError(422,'Choose another player to impersonate.');
            $target=$this->browserActor($target['id']);$this->requireMod($target['id']);
            $session=['id'=>$id,'owner'=>$owner['id'],'target'=>$target['id'],'target_stamp'=>$this->browserSessionStamp($target),'scope'=>$this->scope($hc),'created'=>time(),'expires'=>time()+3600];
            $this->run('INSERT INTO market_owner_actions VALUES (?,?,?,?,?,?)',[$id,$owner['id'],'impersonate_start',$hash,self::json($session),time()]);return $session;
        });
    }
    public function stopImpersonation(array $owner,string $id,string $reason='Owner exited'): void {
        $session=$this->impersonationRecord($owner,$id);$end=md5('impersonate_end|'.$id);
        $this->transaction(function()use($owner,$session,$end,$reason){
            $this->run('SELECT id FROM actors WHERE id=?'.$this->lock(),[$owner['id']])->fetch();
            if(!$this->run('SELECT id FROM market_owner_actions WHERE id=?',[$end])->fetchColumn())$this->run('INSERT INTO market_owner_actions VALUES (?,?,?,?,?,?)',[$end,$owner['id'],'impersonate_end',hash('sha256',$session['id']),self::json(['session'=>$session['id'],'target'=>$session['target'],'reason'=>$reason]),time()]);
        });
    }
    public function impersonationContext(array $owner,string $id): ?array {
        $session=$this->impersonationRecord($owner,$id);
        if($this->run("SELECT id FROM market_owner_actions WHERE id=? AND kind='impersonate_end'",[md5('impersonate_end|'.$id)])->fetchColumn())return null;
        $reason=!$this->isOwner($owner)?'Owner access changed':($session['expires']<=time()?'Session expired':'');
        try{$target=$this->browserActor($session['target']);if(!hash_equals($session['target_stamp'],$this->browserSessionStamp($target)))$reason='Player link changed';}
        catch(MarketError){$reason='Player unavailable';}
        if($reason){$this->stopImpersonation($owner,$id,$reason);return null;}
        return ['id'=>$id,'expires'=>$session['expires'],'owner'=>['id'=>$owner['id'],'name'=>$owner['name'],'player_id'=>$this->publicPlayerId($owner['id'])],'player'=>['id'=>$target['id'],'name'=>$target['name'],'player_id'=>$this->publicPlayerId($target['id'])]];
    }
    public function beginImpersonationAction(array $owner,array $context,array $data,bool $hc): string {
        $this->requireOwner($owner);if(!$this->impersonationContext($owner,$context['id']))throw new MarketError(409,'Impersonation ended. Refresh before acting.');
        $safe=[];foreach(['id','type','item_id','offer_id','listing_id','listing_revision','expected_revision','key','to','page','withdraw_page','enabled','auto_trade','name','kind','target','quantity','price','balances','messages','through'] as $key)if(array_key_exists($key,$data))$safe[$key]=$data[$key];
        // Message bodies and serialized saves are available through their normal
        // records; never copy credentials or private free text into operator logs.
        if(isset($data['message'])&&is_string($data['message']))$safe['message_bytes']=strlen($data['message']);
        $id=self::id();$record=['session'=>$context['id'],'owner'=>$owner['id'],'target'=>$context['player']['id'],'scope'=>$this->scope($hc),'request'=>$safe,'request_hash'=>hash('sha256',self::json($data)),'outcome'=>'pending'];
        $this->run('INSERT INTO market_owner_actions VALUES (?,?,?,?,?,?)',[$id,$owner['id'],'impersonate_action',hash('sha256',$context['id']),self::json($record),time()]);return $id;
    }
    public function finishImpersonationAction(array $owner,string $id,int $status,array $result): void {
        $row=$this->run("SELECT result FROM market_owner_actions WHERE id=? AND owner=? AND kind='impersonate_action'",[$id,$owner['id']])->fetchColumn();if(!$row)throw new RuntimeException('The owner action audit is unavailable.');
        $record=json_decode($row,true,512,JSON_THROW_ON_ERROR);$record['outcome']=$status<400?'completed':'failed';$record['http_status']=$status;$record['finished']=time();$safe=[];
        foreach(['status','id','offer_id','listing_id','item_id','revision','queued','ready_for_delivery','withdrawal_id','error'] as $key)if(isset($result[$key])&&(is_scalar($result[$key])||$result[$key]===null))$safe[$key]=$result[$key];
        $record['result']=$safe;$this->run('UPDATE market_owner_actions SET result=? WHERE id=? AND owner=?',[self::json($record),$id,$owner['id']]);
    }
    public function impersonationHistory(array $owner,?string $id=null): array {
        $this->requireOwner($owner);
        if($id!==null){$sessions=[$this->impersonationRecord($owner,$id)];}
        else{$sessions=array_map(fn($r)=>json_decode($r['result'],true,512,JSON_THROW_ON_ERROR),$this->run("SELECT result FROM market_owner_actions WHERE owner=? AND kind='impersonate_start' ORDER BY created DESC,id DESC LIMIT 12",[$owner['id']])->fetchAll());}
        $out=[];foreach($sessions as $session){
            $target=$this->run('SELECT id,name FROM actors WHERE id=?',[$session['target']])->fetch()?:['id'=>$session['target'],'name'=>'Deleted player'];$target['player_id']=$this->publicPlayerId($target['id']);
            $ended=$this->run("SELECT result,created FROM market_owner_actions WHERE id=? AND kind='impersonate_end'",[md5('impersonate_end|'.$session['id'])])->fetch();
            $entry=['id'=>$session['id'],'owner_player_id'=>$this->publicPlayerId($owner['id']),'player'=>$target,'scope'=>$session['scope'],'created'=>$session['created'],'expires'=>$session['expires'],'ended'=>$ended?(int)$ended['created']:null,'reason'=>$ended?json_decode($ended['result'],true)['reason']:($session['expires']<=time()?'Session expired':null)];
            if($id!==null)$entry['actions']=array_map(function($r){$a=json_decode($r['result'],true,512,JSON_THROW_ON_ERROR);unset($a['request_hash']);return ['id'=>$r['id'],'created'=>(int)$r['created']]+$a;},$this->run("SELECT id,result,created FROM market_owner_actions WHERE owner=? AND kind='impersonate_action' AND hash=? ORDER BY created DESC,id DESC LIMIT 100",[$owner['id'],hash('sha256',$session['id'])])->fetchAll());
            $out[]=$entry;
        }return ['sessions'=>$out];
    }
}
