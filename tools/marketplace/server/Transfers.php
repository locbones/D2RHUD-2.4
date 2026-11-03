<?php
declare(strict_types=1);

// Gifts and free claims transfer existing escrow only. They never establish
// custody from a browser snapshot or write a game save.
trait MarketplaceTransfers {
    public static function freePrice(): array {return [['kind'=>'free','code'=>'free','label'=>'Free','quality'=>2,'index'=>-1,'ethereal'=>-1,'quantity'=>1]];}
    private static function isFree(array $price): bool {return count($price)===1&&($price[0]['kind']??'')==='free';}
    private function giftRecipient(string $to,string $owner): array {
        $rows=$this->run("SELECT a.id,a.name FROM actors a JOIN market_players p ON p.owner=a.id WHERE a.role='player' AND a.id=?",[$to])->fetchAll();
        if(!$rows)$rows=$this->run("SELECT a.id,a.name FROM actors a JOIN market_players p ON p.owner=a.id WHERE a.role='player' AND LOWER(a.name)=LOWER(?) LIMIT 2",[$to])->fetchAll();
        if(count($rows)!==1)throw new MarketError(422,'Choose one linked player by exact name or player ID.');
        $this->requireMod($rows[0]['id']);
        if($rows[0]['id']===$owner)throw new MarketError(422,'Choose another player to receive the gift.');return $rows[0];
    }
    private function giftCommand(array $actor,array $data,bool $hc): array {
        $id=self::ticketId($data['id']??null);$scope=$this->scope($hc);$hash=hash('sha256',$scope.'|'.self::json($data));
        return $this->transaction(function()use($actor,$data,$hc,$id,$scope,$hash){
            $this->lockScope($hc);$owner=$actor['id'];
            if($actor['role']!=='player')throw new MarketError(403,'Only player accounts can send gifts.');
            $old=$this->run('SELECT hash,result FROM commands WHERE actor=? AND id=?',[$owner,$id])->fetch();
            if($old){if(!hash_equals($old['hash'],$hash))throw new MarketError(409,'Gift ID reused.');return json_decode($old['result'],true,512,JSON_THROW_ON_ERROR);}
            $recipient=$this->giftRecipient(self::text($data['to']??null,64),$owner);$to=$recipient['id'];
            $message=($data['message']??'')!==''?self::message($data['message']):'';
            if($data['type']==='gift_recipient')$result=['recipient_id'=>$to,'recipient_name'=>$recipient['name']];
            elseif($data['type']==='gift_item'){
                $item=self::ticketId($data['item_id']??null);$this->requireSingleItem($item);$e=$this->run('SELECT state FROM escrow_items WHERE item=? AND owner=? AND scope=?',[$item,$owner,$scope])->fetch();
                if(!$e||!in_array($e['state'],['available','listed'],true))throw new MarketError(409,'Only an available item held in your escrow can be gifted.');
                foreach($this->run("SELECT id FROM listings WHERE item=? AND status IN ('open','agreed')",[$item])->fetchAll() as $l){$this->invalidateOffers($l['id'],$actor,'The seller gifted this item to another player.');$this->run("UPDATE listings SET status='withdrawn',revision=revision+1 WHERE id=?",[$l['id']]);}
                // A private, already agreed free transfer uses the same atomic
                // settlement and delivery journal as paid trades.
                $listing=self::id();$price=self::freePrice();$encoded=self::json($price);
                $this->run('INSERT INTO listings VALUES (?,?,?,?,?,?,?)',[$listing,$item,$owner,1,'agreed',$encoded,time()]);
                $this->run('INSERT INTO offers VALUES (?,?,?,?,?,?,?,?,?,?)',[$id,$listing,$to,$owner,1,1,'accepted',$owner,$encoded,time()]);
                $this->event($item,$id,$actor,'gifted',$message!==''?$message:'Gift addressed to '.$recipient['name'],$price,$owner,$to);
                $this->run("UPDATE escrow_items SET state='listed' WHERE item=?",[$item]);
                $this->settleReady($hc,$id);
                if($this->run('SELECT status FROM offers WHERE id=?',[$id])->fetchColumn()!=='completed')throw new MarketError(409,'The gift could not settle. No ownership changed.');
                $result=['gift_id'=>$id,'offer_id'=>$id,'recipient'=>$to,'status'=>'completed'];
            }else{
                $rows=self::price($data['balances']??null);
                foreach($rows as $r){if(!in_array($r['kind']??'item',['gold','bag'],true))throw new MarketError(422,'Choose gold or bag contents to gift.');$this->balance($owner,$scope,$r,-$r['quantity'],'gift_sent',$id);$this->balance($to,$scope,$r,$r['quantity'],'gift_received',$id);}
                $terms=implode(' + ',array_map(fn($r)=>$r['quantity'].' × '.$r['label'],$rows));
                foreach([$owner,$to] as $who){$sender=$who===$owner?$to:$owner;$body=($who===$owner?'Gift sent to '.$recipient['name']:'Gift received').': '.$terms.($message!==''?' — '.$message:'');$body=substr($body,0,4096);while(!preg_match('//u',$body))$body=substr($body,0,-1);
                    $this->run('INSERT INTO market_messages (id,sender,recipient,scope,kind,thread,body,created) VALUES (?,?,?,?,?,?,?,?)',[hash('md5',$id.'|'.$who),$sender,$who,$scope,'trade','',$body,time()]);}
                $this->settleReady($hc,null,$to);$result=['gift_id'=>$id,'recipient'=>$to,'status'=>'completed'];
            }
            $this->run('INSERT INTO commands VALUES (?,?,?,?)',[$owner,$id,$hash,self::json($result)]);return $result;
        });
    }
}
