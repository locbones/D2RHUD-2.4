<?php
declare(strict_types=1);

// Wanted posts describe a request, never a fabricated custody item. Responses
// attach real escrow goods and use the ordinary negotiated settlement journal.
trait MarketplaceTrading {
    public static function openPrice(): array {return [['kind'=>'open','code'=>'open','label'=>'Open to Offers','quality'=>2,'index'=>-1,'ethereal'=>-1,'quantity'=>1]];}
    private static function isOpen(array $price): bool {return count($price)===1&&($price[0]['kind']??'')==='open';}
    private static function paymentPrice(mixed $rows): array {
        $price=self::price($rows);if(self::isOpen($price))throw new MarketError(422,'Propose an exact payment. Open to Offers is an asking-price option only.');return $price;
    }
    private function tradingSchema(string $engine): void {
        foreach([
            'market_wanted'=>'id CHAR(32) PRIMARY KEY, scope VARCHAR(255) NOT NULL, owner VARCHAR(64) NOT NULL, revision INT NOT NULL, status VARCHAR(16) NOT NULL, wanted TEXT NOT NULL, price TEXT NOT NULL, contact INT NOT NULL, note TEXT NOT NULL, created BIGINT NOT NULL',
            'market_wanted_offers'=>'offer VARCHAR(64) PRIMARY KEY, wanted CHAR(32) NOT NULL',
            'market_browser_features'=>'owner VARCHAR(64) NOT NULL, scope VARCHAR(255) NOT NULL, features TEXT NOT NULL, PRIMARY KEY(owner,scope)',
            'market_companion_versions'=>'owner VARCHAR(64) NOT NULL, scope VARCHAR(255) NOT NULL, version VARCHAR(32) NOT NULL, seen BIGINT NOT NULL, PRIMARY KEY(owner,scope)',
        ] as $name=>$columns)$this->db->exec("CREATE TABLE IF NOT EXISTS $name ($columns)$engine");
    }
    private function wantedTarget(mixed $target): array {
        $price=self::paymentPrice([$target]);$r=$price[0];
        if(($r['kind']??'item')!=='item')throw new MarketError(422,'Choose an item to buy.');
        $catalog=$this->browserCatalog();$base=$catalog['bases'][$r['code']]??null;
        $named=$r['quality']===7?($catalog['unique'][$r['index']]??null):($r['quality']===5?($catalog['set'][$r['index']]??null):$base);
        if(!$base||!$named||($named['code']??$r['code'])!==$r['code']&&!in_array($r['code'],$named['codes']??[],true))throw new MarketError(422,'Choose an item from this mod’s catalog.');
        $r['any_quality']=$r['quality']===2&&($target['any_quality']??false)===true;
        return $r;
    }
    public function wantedListings(bool $hc,?string $id=null): array {
        $args=[$this->scope($hc)];$where="w.status='open'";
        if($id!==null){$where='w.id=?';$args[]=self::ticketId($id);}
        $rows=$this->run("SELECT w.*,a.name FROM market_wanted w JOIN actors a ON a.id=w.owner WHERE w.scope=? AND $where ORDER BY w.created DESC,w.id DESC LIMIT 4096",$args)->fetchAll();
        foreach($rows as &$r){$r['wanted']=json_decode($r['wanted'],true,512,JSON_THROW_ON_ERROR);$r['price']=json_decode($r['price'],true,512,JSON_THROW_ON_ERROR);$r['contact']=(bool)$r['contact'];$r['revision']=(int)$r['revision'];$r['created']=(int)$r['created'];$r['player_id']=$this->publicPlayerId($r['owner']);unset($r['scope']);}unset($r);
        if($id!==null&&!$rows)throw new MarketError(404,'Wanted listing not found in this market.');
        return $id!==null?$rows[0]:$rows;
    }
    private function wantedMatches(array $target,array $item,array $meta): bool {
        return self::paymentClassMatches($target,$meta)&&($meta['identified']??false)&&$item['code']===$target['code']&&($target['any_quality']||(int)$item['quality']===$target['quality']&&($target['index']===-1||(int)$item['item_index']===$target['index']))&&($target['ethereal']===-1||(int)$item['ethereal']===$target['ethereal'])&&($meta['quantity']??1)>=$target['quantity'];
    }
    private function wantedCommand(array $actor,array $data,bool $hc): array {
        return $this->transaction(function()use($actor,$data,$hc){
            $this->lockScope($hc);$owner=$actor['id'];$scope=$this->scope($hc);$id=self::ticketId($data['id']??null);$hash=hash('sha256',$scope.'|'.self::json($data));
            $this->run('SELECT id FROM actors WHERE id=?'.$this->lock(),[$owner])->fetch();
            $old=$this->run('SELECT hash,result FROM commands WHERE actor=? AND id=?',[$owner,$id])->fetch();
            if($old){if(!hash_equals($old['hash'],$hash))throw new MarketError(409,'Command ID reused.');return json_decode($old['result'],true,512,JSON_THROW_ON_ERROR);}
            if($data['type']==='wanted_create'){
                if((int)$this->run("SELECT COUNT(*) FROM market_wanted WHERE owner=? AND scope=? AND status='open'",[$owner,$scope])->fetchColumn()>=64)throw new MarketError(422,'Close an existing wanted listing before adding another.');
                $wanted=$this->wantedTarget($data['wanted']??null);$contact=$data['contact']??false;if(!is_bool($contact))throw new MarketError(422,'Choose a price or Please message me.');
                $price=$contact?[]:self::paymentPrice($data['price']??null);$note=$data['note']??'';if(!is_string($note))throw new MarketError(422,'Write a valid note.');$note=trim($note);if($note!=='')$note=self::message($note);
                $this->run('INSERT INTO market_wanted VALUES (?,?,?,?,?,?,?,?,?,?)',[$id,$scope,$owner,1,'open',self::json($wanted),self::json($price),(int)$contact,$note,time()]);$result=['wanted_id'=>$id,'revision'=>1];
            }else{
                $key=self::ticketId($data['wanted_id']??null);$w=$this->run('SELECT * FROM market_wanted WHERE id=? AND scope=?'.$this->lock(),[$key,$scope])->fetch();
                if(!$w||$w['status']!=='open'||(int)$w['revision']!==self::number($data['expected_revision']??null,1,2147483647))throw new MarketError(409,'This wanted listing changed or closed. Refresh it first.');
                if($data['type']==='wanted_close'){
                    if($w['owner']!==$owner)throw new MarketError(403,'Only its owner can close this wanted listing.');
                    $this->run("UPDATE market_wanted SET status='closed',revision=revision+1 WHERE id=?",[$key]);$result=['closed'=>true];
                }elseif($data['type']==='wanted_respond'){
                    if($w['owner']===$owner)throw new MarketError(422,'Choose another player’s wanted listing.');
                    if($this->communicationBlocked($owner,$w['owner']))throw new MarketError(403,'Trading messages are unavailable between these players.');
                    if((int)$this->run('SELECT COUNT(*) FROM market_wanted_offers x JOIN offers o ON o.id=x.offer WHERE x.wanted=? AND o.seller=? AND o.status IN (\'pending\',\'accepted\')',[$key,$owner])->fetchColumn()>=16)throw new MarketError(422,'Review your existing responses to this wanted listing.');
                    $item=self::ticketId($data['item_id']??null);$this->requireSingleItem($item);
                    $e=$this->run("SELECT e.*,i.code,i.quality,i.item_index,i.ethereal,i.owner AS actual_owner FROM escrow_items e JOIN items i ON i.id=e.item WHERE e.item=? AND e.owner=? AND e.scope=? AND e.state IN ('available','listed')",[$item,$owner,$scope])->fetch();
                    if(!$e||$e['actual_owner']!==$owner)throw new MarketError(409,'Choose an available item from your Vault.');
                    $meta=json_decode($e['metadata'],true,512,JSON_THROW_ON_ERROR);if(($meta['delivery_requested']??false)||!$this->wantedMatches(json_decode($w['wanted'],true,512,JSON_THROW_ON_ERROR),$e,$meta))throw new MarketError(409,'This item is reserved for delivery or does not match the request.');
                    if(!$this->supportsEscrowItem($w['owner'],$item))throw new MarketError(409,'The buyer needs to update this mod before receiving this item.');
                    $price=self::paymentPrice($data['price']??null);$message=trim($data['message']??'');if($message!=='')$message=self::message($message);
                    $listing=$this->run("SELECT * FROM listings WHERE item=? AND seller=? AND status IN ('open','agreed') ORDER BY created DESC",[$item,$owner])->fetch();
                    if($listing&&$listing['status']!=='open')throw new MarketError(409,'This item is already agreed to another trade.');
                    if(!$listing){$listing=['id'=>self::id(),'revision'=>1];$this->run('INSERT INTO listings VALUES (?,?,?,?,?,?,?)',[$listing['id'],$item,$owner,1,'open',self::json($price),time()]);$meta['price']=$price;$this->run("UPDATE escrow_items SET state='listed',metadata=? WHERE item=?",[self::json($meta),$item]);}
                    // The seller is the proposer. Neither auto-trade nor the
                    // original WTB budget is consent to purchase this instance.
                    $this->run('INSERT INTO offers VALUES (?,?,?,?,?,?,?,?,?,?)',[$id,$listing['id'],$w['owner'],$owner,1,(int)$listing['revision'],'pending',$owner,self::json($price),time()]);
                    $this->run('INSERT INTO market_wanted_offers VALUES (?,?)',[$id,$key]);$this->publicOfferRegister(['id'=>$id,'listing'=>$listing['id'],'buyer'=>$w['owner']]);
                    $this->event($item,$id,$actor,'proposed',$message!==''?$message:'Response to your wanted listing.',$price,$owner,$w['owner']);$result=['offer_id'=>$id,'status'=>'pending'];
                }else throw new MarketError(422,'Unknown wanted-listing action.');
            }
            $this->run('INSERT INTO commands VALUES (?,?,?,?)',[$owner,$id,$hash,self::json($result)]);return $result;
        });
    }
    public function publicEscrow(string $owner,bool $hc,string $after='',string $query=''): array {
        $player=$this->playerProfile($owner,$hc)['player'];$scope=$this->scope($hc);if($after!=='')self::ticketId($after);
        $query=trim(self::text($query===''?' ':$query,128));$catalog=$query!==''?$this->browserCatalog():[];$cursor=$after;$out=[];
        // Search before pagination, including items beyond the first visible page.
        // Read bounded batches and only build full display/price data for matches.
        do{
            $rows=$this->run("SELECT i.*,e.item,e.state,e.metadata FROM escrow_items e JOIN items i ON i.id=e.item WHERE e.owner=? AND i.owner=? AND e.scope=? AND e.state IN ('available','listed') AND e.item>? ORDER BY e.item LIMIT 128",[$owner,$owner,$scope,$cursor])->fetchAll();
            foreach($rows as $r){
                $cursor=$r['item'];$meta=json_decode($r['metadata'],true,512,JSON_THROW_ON_ERROR);if($meta['delivery_requested']??false)continue;
                if($query!==''){
                    $base=$catalog['bases'][$r['code']]??[];
                    $name=($meta['identified']??true)?$this->notificationItemName($r):'Unidentified '.($base['name']??$r['code']);
                    if(!self::searchTextMatches(implode(' ',[$name,$base['name']??'',$r['code'],$base['type']??'']),$query))continue;
                }
                $out[]=$r;if(count($out)>48)break 2;
            }
        }while(count($rows)===128);
        $more=count($out)>48;if($more)array_pop($out);$next=$more?end($out)['item']:null;
        foreach($out as &$r){$listing=$this->run("SELECT l.id,l.price,c.comment FROM listings l LEFT JOIN market_listing_comments c ON c.listing=l.id WHERE l.item=? AND l.status='open' ORDER BY l.created DESC LIMIT 1",[$r['item']])->fetch();$r=['item_id'=>$r['item'],'item'=>$this->statsItem($r['item']),'state'=>$r['state'],'listing_id'=>$listing['id']??null,'price'=>$listing?json_decode($listing['price'],true,512,JSON_THROW_ON_ERROR):[],'comment'=>$listing['comment']??''];}unset($r);
        $balances=$this->run('SELECT kind,stat,quantity FROM escrow_balances WHERE owner=? AND scope=? AND quantity>0 ORDER BY kind,stat',[$owner,$scope])->fetchAll();foreach($balances as &$b){$b['quantity']=(int)$b['quantity'];$b+=['code'=>$b['kind']==='gold'?'gld':'Z01','label'=>$b['kind']==='gold'?'Gold':$b['stat'],'quality'=>2,'index'=>-1,'ethereal'=>-1];}unset($b);
        return ['player'=>$player,'items'=>$out,'balances'=>$balances,'next_after'=>$next,'query'=>$query];
    }
    public function tradeMessages(array $actor,bool $hc,?string $thread=null,int $before=PHP_INT_MAX): array {
        if($thread==='updates')return $this->tradeUpdates($actor,$before);
        if($thread!==null){$offer=$this->offer(self::text($thread,64),$actor);$scope=$this->run('SELECT i.scope FROM offers o JOIN listings l ON l.id=o.listing JOIN items i ON i.id=l.item WHERE o.id=?',[$thread])->fetchColumn();$parts=explode('|',(string)$scope);if(count($parts)!==4||$parts[0]!==$this->community||$parts[2]!=='2.4')throw new MarketError(404,'Conversation not found.');$market=new self($this->db,$this->community,$parts[1]);$conversation=$market->privateConversation($actor,$parts[3]==='HC',$thread);return ['conversation'=>$conversation,'market'=>['mod'=>$parts[1],'version'=>'2.4','hardcore'=>$parts[3]==='HC']];}
        $owner=$actor['id'];$read=(int)$this->run('SELECT through_seq FROM market_message_reads WHERE owner=?',[$owner])->fetchColumn();
        $rows=$this->run("SELECT o.id,o.buyer,o.seller,o.status,l.item,i.scope,MAX(e.seq) AS last_seq FROM offers o JOIN listings l ON l.id=o.listing JOIN items i ON i.id=l.item JOIN events e ON e.offer=o.id WHERE i.scope LIKE ? ESCAPE '!' AND (o.buyer=? OR o.seller=?) AND e.seq<? GROUP BY o.id,o.buyer,o.seller,o.status,l.item,i.scope ORDER BY last_seq DESC LIMIT 51",[$this->communityPattern(),$owner,$owner,$before])->fetchAll();$more=count($rows)>50;if($more)array_pop($rows);
        foreach($rows as &$r){$peer=$r['buyer']===$owner?$r['seller']:$r['buyer'];$r['peer']=['id'=>$peer,'name'=>$this->run('SELECT name FROM actors WHERE id=?',[$peer])->fetchColumn(),'player_id'=>$this->publicPlayerId($peer)];$parts=explode('|',$r['scope']);$market=new self($this->db,$this->community,$parts[1]);$r['item_details']=$market->statsItem($r['item']);$r['item_name']=$market->notificationItemName($r['item_details']+['item_index'=>$r['item_details']['index']]);$last=$this->run('SELECT at,kind,message FROM events WHERE seq=?',[$r['last_seq']])->fetch();$r+=$last;$r['unread_seq']=(int)$this->run('SELECT MAX(m.seq) FROM market_messages m WHERE m.recipient=? AND m.thread=? AND m.seq>? AND NOT EXISTS(SELECT 1 FROM market_message_seen s WHERE s.message=m.id AND s.owner=?)',[$owner,$r['id'],$read,$owner])->fetchColumn();$r['last_seq']=(int)$r['last_seq'];}unset($r);
        $updates=$this->tradeUpdates($actor);
        return ['conversations'=>$rows,'next_before'=>$more?(int)end($rows)['last_seq']:null,'updates_unread'=>$updates['unread_seq'],'updates_count'=>$updates['total']];
    }
    private function tradeUpdates(array $actor,int $before=PHP_INT_MAX): array {
        $owner=$actor['id'];$read=(int)$this->run('SELECT through_seq FROM market_message_reads WHERE owner=?',[$owner])->fetchColumn();
        $where="m.recipient=? AND m.kind='trade' AND NOT EXISTS(SELECT 1 FROM offers o WHERE o.id=m.thread)";
        $rows=$this->run("SELECT m.*,a.name AS sender_name FROM market_messages m JOIN actors a ON a.id=m.sender WHERE $where AND m.seq<? ORDER BY m.seq DESC LIMIT 51",[$owner,$before])->fetchAll();$more=count($rows)>50;if($more)array_pop($rows);
        foreach($rows as &$row){$row['seq']=(int)$row['seq'];$row['created']=(int)$row['created'];$row['sender_player_id']=$this->publicPlayerId($row['sender']);}unset($row);
        $total=(int)$this->run("SELECT COUNT(*) FROM market_messages m WHERE $where",[$owner])->fetchColumn();
        $unread=(int)$this->run("SELECT MAX(m.seq) FROM market_messages m WHERE $where AND m.seq>? AND NOT EXISTS(SELECT 1 FROM market_message_seen s WHERE s.message=m.id AND s.owner=?)",[$owner,$read,$owner])->fetchColumn();
        return ['messages'=>array_reverse($rows),'next_before'=>$more?(int)end($rows)['seq']:null,'unread_seq'=>$unread,'total'=>$total];
    }
}
