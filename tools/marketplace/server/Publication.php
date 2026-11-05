<?php
declare(strict_types=1);

// Authenticated discovery only. A client snapshot never establishes escrow.
trait MarketplacePublication {
    private function withdrawPhysical(array $actor,array $data,bool $hc): array {
        return $this->transaction(function()use($actor,$data,$hc){
            $this->lockScope($hc);$owner=$actor['id'];$scope=$this->scope($hc);$id=self::ticketId($data['id']??null);$hash=hash('sha256',$scope.'|'.self::json($data));
            $old=$this->run('SELECT hash,result FROM commands WHERE actor=? AND id=?',[$owner,$id])->fetch();if($old){if(!hash_equals($old['hash'],$hash))throw new MarketError(409,'Command ID reused.');return json_decode($old['result'],true,512,JSON_THROW_ON_ERROR);}
            $hex=self::text($data['item_hex']??null,65536);if(strlen($hex)<14||strlen($hex)%2||!preg_match('/^[0-9a-f]+$/D',$hex))throw new MarketError(422,'Invalid physical item bytes.');
            if(!isset($data['item_id'])){
                $tab=self::number($data['tab']??null,1,7);self::number($data['page']??null,1,64);
                $matches=$this->run('SELECT i.id FROM items i JOIN published_items p ON p.item=i.id WHERE i.item_hex=? AND p.owner=? AND p.scope=? AND p.tab=?',[$hex,$owner,$scope,$tab])->fetchAll();
                if(count($matches)>1)throw new MarketError(409,'Ambiguous physical item. Refresh before withdrawing it.');
                $item=$matches[0]['id']??null;
            }else $item=self::ticketId($data['item_id']);
            if($item===null){$row=['owner'=>$owner,'scope'=>$scope,'item_hex'=>$hex];}else{
            $row=$this->run('SELECT owner,scope,item_hex FROM items WHERE id=?',[$item])->fetch();
            }
            if(!$row||$row['owner']!==$owner||$row['scope']!==$scope)throw new MarketError(404,'Physical item not found.');
            if($row['item_hex']!==$hex)throw new MarketError(409,'The item changed. Refresh before withdrawing it.');
            $held=$this->run('SELECT state FROM escrow_items WHERE item=?',[$item])->fetchColumn();
            if($held!==false&&$held!=='delivered')throw new MarketError(409,'This item is held in Vault. Use Return to delivery.');
            $listings=$this->run("SELECT id FROM listings WHERE item=? AND seller=? AND status IN ('open','agreed')",[$item,$owner])->fetchAll();
            foreach($listings as $listing){$this->invalidateOffers($listing['id'],$actor,'The seller withdrew this item before moving it.');$this->run("UPDATE listings SET status='withdrawn',revision=revision+1 WHERE id=?",[$listing['id']]);}
            $this->run('UPDATE published_items SET expires=0 WHERE item=? AND owner=? AND scope=?',[$item,$owner,$scope]);
            foreach($this->run("SELECT id,packet FROM web_intents WHERE owner=? AND scope=? AND kind='price' AND status='queued'",[$owner,$scope])->fetchAll() as $intent){$packet=json_decode($intent['packet'],true,512,JSON_THROW_ON_ERROR);if(($packet['item_hex']??null)===$hex)$this->run("UPDATE web_intents SET status='failed',message=? WHERE id=?",['Item withdrawn before moving.',$intent['id']]);}
            if($listings)$this->event($item,null,$actor,'withdrawn','Item withdrawn before moving; open offers cancelled.');
            $result=['withdrawn'=>true,'item_id'=>$item];$this->run('INSERT INTO commands VALUES (?,?,?,?)',[$owner,$id,$hash,self::json($result)]);return $result;
        });
    }
    public function publication(array $actor,bool $hardcore): array {
        $row=$this->run('SELECT revision,snapshot_id,captured_at FROM inventory_sources WHERE owner=? AND scope=?',[$actor['id'],$this->scope($hardcore)])->fetch();
        return $row?['revision'=>(int)$row['revision'],'id'=>$row['snapshot_id'],'captured_at'=>(int)$row['captured_at']]:['revision'=>0,'id'=>'','captured_at'=>0];
    }
    private function invalidateOffers(string $listing,array $actor,string $reason,?string $except=null): void {
        $exclude=$except?' AND offers.id<>?':'';$args=$except?[$listing,$except]:[$listing];
        $rows=$this->run("SELECT offers.id,listings.item FROM offers JOIN listings ON listings.id=offers.listing WHERE listing=? AND offers.status IN ('pending','accepted')$exclude",$args)->fetchAll();
        foreach($rows as $o)$this->event($o['item'],$o['id'],$actor,'superseded',$reason);
        $this->run("UPDATE offers SET status='superseded',revision=revision+1 WHERE listing=? AND status IN ('pending','accepted')$exclude",$args);
    }
    private function publishedFresh(string $listing): bool {
        $row=$this->run('SELECT p.expires,i.item_hex,e.state FROM published_items p JOIN items i ON i.id=p.item LEFT JOIN escrow_items e ON e.item=i.id WHERE p.listing=?',[$listing])->fetch();
        if($row&&$row['state']!=='listed'&&($native=$this->itemIdentity($row['item_hex']))&&str_starts_with($native['key'],'bytes:'))return false;
        return !$row||(int)$row['expires']>time();
    }
    public function publish(array $actor,array $data,bool $hardcore): array {
        $this->requireMod($actor['id']);
        if(($data['schema']??null)!==1||($data['game_mode']??null)!=='2.4'||($data['mod']??null)!==$this->mod||($data['hardcore']??null)!==$hardcore||($data['custody']??null)!=='local_save'||($data['purchasable']??null)!==false)throw new MarketError(422,'Invalid discovery snapshot. Inventory is not escrowed.');
        $id=self::text($data['id']??null,64);$captured=self::number($data['captured_at']??null,time()-86400,time()+300);
        $expected=self::number($data['expected_revision']??null,0,2147483647);$page=self::number($data['page']??null,1,64);
        if(!is_bool($data['enabled']??null)||!in_array($data['source']??null,['live','saved'],true)||!is_array($data['items']??null)||!array_is_list($data['items'])||count($data['items'])>512||(!$data['enabled']&&$data['items']))throw new MarketError(422,'Invalid publication items or page state.');
        $rows=[];
        foreach($data['items'] as $r){
            if(!is_array($r)||!is_bool($r['ethereal']??null))throw new MarketError(422,'Invalid published item.');
            $tab=self::number($r['tab']??null,1,7);$hex=self::text($r['item_hex']??null,65536);
            if(strlen($hex)%2||!preg_match('/^[0-9a-f]+$/D',$hex))throw new MarketError(422,'Invalid serialized publication.');
            $code=self::text($r['code']??null,4);if(!preg_match('/^[\x21-\x7e]{1,4}$/D',$code))throw new MarketError(422,'Invalid item code.');
            $children=$r['socketed_items']??[];if(!is_array($children)||!array_is_list($children)||count($children)>6)throw new MarketError(422,'Invalid socket count.');
            $key=hash('sha256',$page.'|'.$tab.'|'.$hex);if(isset($rows[$key]))throw new MarketError(422,'Duplicate item in publication.');
            $rows[$key]=['tab'=>$tab,'hex'=>$hex,'code'=>$code,'seed'=>self::number($r['saved_id']??null,0,4294967295),'quality'=>self::number($r['quality']??null,1,9),'index'=>self::number($r['index']??null,-1,1000000),'ethereal'=>(int)$r['ethereal'],'sockets'=>count($children),'price'=>self::price($r['price']??null),'display'=>$this->displayItem($r),'comment'=>array_key_exists('comment',$r)?self::listingComment($r['comment']):null];
        }
        foreach($rows as &$r)if($native=$this->itemIdentity($r['hex'])){$r['seed']=$native['saved_id'];}unset($r);
        $identities=[];$duplicates=[];
        foreach($rows as $key=>$r){$identity=$this->identityKey($r['hex']);if($identity==='')continue;if(isset($identities[$identity])){$duplicates[$key]=true;$duplicates[$identities[$identity]]=true;}else $identities[$identity]=$key;}
        $hashData=$data;unset($hashData['expected_revision']);$hash=hash('sha256',json_encode($hashData,JSON_THROW_ON_ERROR));
        return $this->transaction(function()use($actor,$hardcore,$rows,$id,$captured,$expected,$hash,$duplicates){
            $this->lockScope($hardcore);
            $scope=$this->scope($hardcore);$owner=$actor['id'];$unverified=0;
            foreach($rows as $key=>$r){$native=$this->itemIdentity($r['hex']);if($native&&str_starts_with($native['key'],'bytes:')){unset($rows[$key],$duplicates[$key]);++$unverified;}}
            $blocked=count($duplicates);foreach($duplicates as $key=>$_)unset($rows[$key]);
            foreach($rows as $key=>$r){try{$rows[$key]['ledger']=$this->identityAdmission($owner,$scope,$r['hex'],null,null,true);}catch(MarketError $e){if($e->status!==409)throw $e;unset($rows[$key]);++$blocked;}}
            // A delayed discovery upload must not recreate a physical copy of
            // inventory already removed by the save agent.
            foreach(array_keys($rows) as $key)if($this->run('SELECT source_key FROM custody_sources WHERE owner=? AND scope=? AND source_key=?',[$owner,$scope,$key])->fetch())unset($rows[$key]);
            $this->run('SELECT id FROM actors WHERE id=?'.$this->lock(),[$owner])->fetch();
            $previous=$this->run('SELECT * FROM inventory_sources WHERE owner=? AND scope=?'.$this->lock(),[$owner,$scope])->fetch();
            if($previous&&$previous['snapshot_id']===$id){if(!hash_equals($previous['hash'],$hash))throw new MarketError(409,'Snapshot ID reused for different content.');return ['revision'=>(int)$previous['revision'],'id'=>$id,'published'=>count($rows),'duplicate'=>true];}
            $revision=$previous?(int)$previous['revision']:0;
            if($revision!==$expected||($previous&&$captured<(int)$previous['captured_at']))throw new MarketError(409,'A newer inventory publication exists. Refresh before publishing.');
            $existing=$this->run('SELECT p.*,l.status,l.price FROM published_items p JOIN listings l ON l.id=p.listing WHERE p.owner=? AND p.scope=? ORDER BY p.source_key',[$owner,$scope])->fetchAll();
            $mapped=[];foreach($existing as $p)$mapped[$p['source_key']]=$p;
            foreach($rows as $key=>$r){$price=json_encode($r['price'],JSON_THROW_ON_ERROR);$expires=$captured+86400;
                if(isset($mapped[$key])){
                    $p=$mapped[$key];$current=$this->run('SELECT status,price FROM listings WHERE id=?'.$this->lock(),[$p['listing']])->fetch();$p=array_replace($p,$current);$item=$this->run('SELECT * FROM items WHERE id=?'.$this->lock(),[$p['item']])->fetch();
                    if(!$item||$item['owner']!==$owner||$p['status']==='sold')throw new MarketError(409,'Published item ownership changed. Operator reconciliation is required.');
                    if($item['seed']!=$r['seed']||$item['code']!==$r['code']||$item['quality']!=$r['quality']||$item['item_index']!=$r['index']||$item['ethereal']!=$r['ethereal']||$item['sockets']!=$r['sockets'])throw new MarketError(409,'Published item metadata changed for the same bytes.');
                    if($p['price']!==$price||!in_array($p['status'],['open','agreed'],true)){
                        $this->invalidateOffers($p['listing'],$actor,'The seller changed or republished this listing. Review the new terms.');
                        $this->run("UPDATE listings SET status='open',price=?,revision=revision+1 WHERE id=?",[$price,$p['listing']]);
                        $this->event($p['item'],null,$actor,'price_changed','Unsecured asking terms updated.',$r['price'],$owner);
                    }
                    $this->run('UPDATE published_items SET expires=? WHERE listing=?',[$expires,$p['listing']]);
                }else{
                    $item=$r['ledger']??self::id();$listing=self::id();
                    if($r['ledger']){
                        foreach($this->run("SELECT id FROM listings WHERE item=? AND status IN ('open','agreed')",[$item])->fetchAll() as $prior){$this->invalidateOffers($prior['id'],$actor,'The physical item moved. Review its current listing.');$this->run("UPDATE listings SET status='withdrawn',revision=revision+1 WHERE id=?",[$prior['id']]);}
                        $this->run('UPDATE items SET item_hex=? WHERE id=?',[$r['hex'],$item]);
                    }else $this->run('INSERT INTO items VALUES (?,?,?,?,?,?,?,?,?,?)',[$item,$scope,$r['seed'],$r['code'],$r['quality'],$r['index'],$r['ethereal'],$r['sockets'],$owner,$r['hex']]);
                    $this->run('INSERT INTO listings VALUES (?,?,?,?,?,?,?)',[$listing,$item,$owner,1,'open',$price,time()]);
                    $this->run('INSERT INTO published_items VALUES (?,?,?,?,?,?,?,?)',[$owner,$scope,$key,$item,$listing,$r['tab'],$expires,1]);
                    $this->event($item,null,$actor,'listed','Client-reported item; not escrowed.',$r['price'],$owner);
                }
                $this->setListingComment(isset($mapped[$key])?$mapped[$key]['listing']:$listing,$r['comment']);
                $displayItem=isset($mapped[$key])?$mapped[$key]['item']:$item;$this->recordIdentity($displayItem,$scope,$r['hex']);
                $this->run('DELETE FROM web_item_display WHERE item=?',[$displayItem]);$this->run('INSERT INTO web_item_display VALUES (?,?)',[$displayItem,self::json($r['display'])]);
            }
            foreach($mapped as $key=>$p)if(!isset($rows[$key])){
                $current=$this->run('SELECT status FROM listings WHERE id=?'.$this->lock(),[$p['listing']])->fetch();$p['status']=$current['status'];
                if(in_array($p['status'],['open','agreed'],true)){
                    $this->invalidateOffers($p['listing'],$actor,'The seller removed this listing from the published page.');
                    $this->run("UPDATE listings SET status='withdrawn',revision=revision+1 WHERE id=?",[$p['listing']]);
                    $this->event($p['item'],null,$actor,'withdrawn','Removed from the published page.');
                }
                $this->run('UPDATE published_items SET expires=0 WHERE listing=?',[$p['listing']]);
            }
            if($previous)$this->run('UPDATE inventory_sources SET revision=?,snapshot_id=?,captured_at=?,hash=? WHERE owner=? AND scope=?',[$revision+1,$id,$captured,$hash,$owner,$scope]);
            else $this->run('INSERT INTO inventory_sources VALUES (?,?,?,?,?,?)',[$owner,$scope,$revision+1,$id,$captured,$hash]);
            return ['revision'=>$revision+1,'id'=>$id,'published'=>count($rows),'blocked_identities'=>$blocked,'unverified_identities'=>$unverified,'duplicate'=>false];
        });
    }
}
