<?php
declare(strict_types=1);

trait MarketplaceOwnerAudit {
    public function ensureOwnerAuditSchema(?string $engine=null): void {
        $mysql=$this->db->getAttribute(PDO::ATTR_DRIVER_NAME)==='mysql';
        $engine??=$mysql?' ENGINE=InnoDB DEFAULT CHARSET=utf8mb4':'';
        $sequence=$mysql?'BIGINT AUTO_INCREMENT PRIMARY KEY':'INTEGER PRIMARY KEY AUTOINCREMENT';
        $this->db->exec("CREATE TABLE IF NOT EXISTS market_trade_assets (seq $sequence, receipt CHAR(32) NOT NULL, scope VARCHAR(255) NOT NULL, leg VARCHAR(16) NOT NULL, sender VARCHAR(64) NOT NULL, recipient VARCHAR(64) NOT NULL, item CHAR(32) NOT NULL, source_item CHAR(32) NOT NULL, kind VARCHAR(16) NOT NULL, quantity BIGINT NOT NULL, snapshot MEDIUMTEXT NOT NULL, delivery_receipt CHAR(32) NOT NULL DEFAULT '', delivered_at BIGINT NOT NULL DEFAULT 0)$engine");
    }
    private function recordTradeAsset(string $receipt,string $scope,string $leg,string $sender,string $recipient,?string $item,array $resource=[],?string $source=null): void {
        if($item){$snapshot=$this->statsItem($item);$native=$this->itemIdentity($this->run('SELECT item_hex FROM items WHERE id=?',[$item])->fetchColumn());$snapshot['item_uuid']=$native['item_uuid']??'';$quantity=(int)$snapshot['quantity'];$kind='item';}
        else{$snapshot=$resource;$quantity=(int)$resource['quantity'];$kind=$resource['kind'];}
        $this->run('INSERT INTO market_trade_assets (receipt,scope,leg,sender,recipient,item,source_item,kind,quantity,snapshot) VALUES (?,?,?,?,?,?,?,?,?,?)',[$receipt,$scope,$leg,$sender,$recipient,$item??'',$source??$item??'',$kind,$quantity,self::json($snapshot)]);
    }
    private function recordTradeDelivery(string $item,string $owner,string $scope,string $ticket): void {
        $seq=$this->run('SELECT seq FROM market_trade_assets WHERE item=? AND recipient=? AND scope=? ORDER BY seq DESC LIMIT 1',[$item,$owner,$scope])->fetchColumn();
        if($seq!==false)$this->run("UPDATE market_trade_assets SET delivery_receipt=?,delivered_at=? WHERE seq=? AND delivery_receipt=''",[$ticket,time(),$seq]);
    }
    private function auditPlayer(?string $id): array {
        $row=$id?$this->run('SELECT id,name FROM actors WHERE id=?',[$id])->fetch():false;
        return $row?['id'=>$id,'name'=>$row['name'],'player_id'=>$this->publicPlayerId($id)]:['id'=>$id??'','name'=>'System','player_id'=>''];
    }
    private function auditTradeSource(): string {
        return "SELECT o.id,o.status,o.created,o.seller,o.buyer,l.item,i.scope,o.price,s.id AS receipt,s.created AS settled_at,'offer' AS record_type FROM offers o JOIN listings l ON l.id=o.listing JOIN items i ON i.id=l.item LEFT JOIN settlements s ON s.offer=o.id
            UNION ALL SELECT e.id,'completed',e.at,e.seller,e.buyer,e.item,i.scope,e.price,e.receipt,e.at,'legacy' FROM events e JOIN items i ON i.id=e.item WHERE e.kind='completed_sale' AND e.offer IS NULL";
    }
    private function auditTradeRow(array $row): array {
        return ['id'=>$row['id'],'record_type'=>$row['record_type'],'status'=>$row['status'],'created'=>(int)$row['created'],'seller'=>$this->auditPlayer($row['seller']),'buyer'=>$this->auditPlayer($row['buyer']),'item'=>$this->statsItem($row['item']),'price'=>json_decode($row['price'],true,512,JSON_THROW_ON_ERROR),'settlement'=>$row['receipt']?['id'=>$row['receipt'],'at'=>(int)$row['settled_at']]:null];
    }
    public function ownerTrades(array $actor,bool $hc,array $query=[]): array {
        $this->requireOwner($actor);
        return $this->transaction(function()use($hc,$query){
            $where=['t.scope=?'];$args=[$this->scope($hc)];$status=$query['status']??'all';
            if(!in_array($status,['all','pending','accepted','completed','rejected','withdrawn','superseded'],true))throw new MarketError(422,'Choose a valid trade status.');
            if($status!=='all'){$where[]='t.status=?';$args[]=$status;}
            $search=$query['q']??'';if(!is_string($search))throw new MarketError(422,'Invalid history search.');$search=trim($search);if($search!==''){
                self::text($search,128);$like='%'.str_replace(['!','%','_'],['!!','!%','!_'],$search).'%';
                $terms=["sa.name LIKE ? ESCAPE '!'","ba.name LIKE ? ESCAPE '!'","t.id LIKE ? ESCAPE '!'","t.receipt LIKE ? ESCAPE '!'","t.item LIKE ? ESCAPE '!'"];
                foreach($terms as $_)$args[]=$like;
                $terms[]="EXISTS (SELECT item FROM item_identities ii WHERE ii.item=t.item AND ii.scope=t.scope AND ii.identity_key LIKE ? ESCAPE '!')";$args[]=$like;
                if(preg_match('/^P-([0-9]+)$/iD',$search,$match)){$terms[]='(sp.number=? OR bp.number=?)';$args[]=$match[1];$args[]=$match[1];}
                if(ctype_digit($search)&&strlen($search)<=10){$terms[]='i.seed=?';$args[]=$search;}
                $where[]='('.implode(' OR ',$terms).')';
            }
            if(isset($query['before'])){self::text($query['before'],64);$at=self::number($query['before_at']??null,0,PHP_INT_MAX);$where[]='(COALESCE(t.settled_at,t.created)<(?+0) OR (COALESCE(t.settled_at,t.created)=(?+0) AND t.id<?))';array_push($args,$at,$at,$query['before']);}
            $rows=$this->run('SELECT t.*,COALESCE(t.settled_at,t.created) AS sort_at FROM ('.$this->auditTradeSource().') t JOIN items i ON i.id=t.item LEFT JOIN actors sa ON sa.id=t.seller LEFT JOIN actors ba ON ba.id=t.buyer LEFT JOIN market_player_ids sp ON sp.owner=t.seller LEFT JOIN market_player_ids bp ON bp.owner=t.buyer WHERE '.implode(' AND ',$where).' ORDER BY sort_at DESC,t.id DESC LIMIT 31',$args)->fetchAll();
            $more=count($rows)>30;if($more)array_pop($rows);$last=$rows?end($rows):null;
            return ['rows'=>array_map(fn($r)=>$this->auditTradeRow($r),$rows),'next'=>$more?['before'=>$last['id'],'before_at'=>(int)$last['sort_at']]:null,'mod'=>$this->mod,'hardcore'=>$hc];
        });
    }
    private function auditAssetDelivery(array $asset): array {
        if($asset['kind']!=='item')return ['state'=>'wallet','label'=>'Credited to Vault'];
        if($asset['delivery_receipt']!==''){
            $ticket=$this->run('SELECT status,proof FROM custody_tickets WHERE id=? AND owner=? AND scope=?',[$asset['delivery_receipt'],$asset['recipient'],$asset['scope']])->fetch();
            return ['state'=>'delivered','label'=>'Save write acknowledged','at'=>(int)$asset['delivered_at'],'receipt'=>$asset['delivery_receipt'],'pages'=>$ticket?array_column(json_decode($ticket['proof'],true,512,JSON_THROW_ON_ERROR),'page'):[]];
        }
        $current=$this->run('SELECT owner,state,metadata FROM escrow_items WHERE item=? AND scope=?',[$asset['item'],$asset['scope']])->fetch();
        if(!$current)return ['state'=>'unknown','label'=>'No current Vault record'];
        if($current['owner']!==$asset['recipient'])return ['state'=>'onward','label'=>'Transferred onward in your Vault'];
        return match($current['state']){
            'claiming'=>['state'=>'delivering','label'=>'Delivery in progress'],
            'listed'=>['state'=>'relisted','label'=>'Recipient listed it for sale'],
            'delivered'=>['state'=>'unknown','label'=>'Delivered ledger state; no per-trade receipt'],
            default=>['state'=>'pending','label'=>(json_decode($current['metadata'],true)['delivery_requested']??false)?'Waiting for recipient stash sync':'Held in recipient Vault'],
        };
    }
    private function historicalTradeAssets(array $row): array {
        $goods=[$row['item']];
        if($row['record_type']==='offer'){$listing=$this->run('SELECT listing FROM offers WHERE id=?',[$row['id']])->fetchColumn();if($listing)$goods=$this->bundleGoods($listing,$row['item']);}
        $assets=[];
        foreach($goods as $item){
            $anchor=$row['record_type']==='legacy'?$this->run('SELECT seq,at FROM events WHERE id=?',[$row['id']])->fetch():$this->run("SELECT seq,at FROM events WHERE item=? AND offer=? AND kind IN ('completed_sale','bundle_sale') ORDER BY seq LIMIT 1",[$item,$row['id']])->fetch();
            $delivery=['state'=>'unknown','label'=>'No delivery evidence recorded for this trade'];
            if($anchor){
                $next=$this->run("SELECT seq FROM events WHERE item=? AND seq>? AND kind IN ('completed_sale','bundle_sale') ORDER BY seq LIMIT 1",[$item,$anchor['seq']])->fetchColumn();
                $args=[$item,$anchor['seq'],$row['buyer']];$bound='';if($next!==false){$bound=' AND seq<?';$args[]=$next;}
                $event=$this->run("SELECT at FROM events WHERE item=? AND seq>? AND kind='delivered' AND actor=?$bound ORDER BY seq LIMIT 1",$args)->fetch();
                if($event){
                    $delivery=['state'=>'delivered','label'=>'Save write acknowledged (historical event)','at'=>(int)$event['at']];
                    // Attach a receipt only if the historical time window has
                    // exactly one committed claim containing this ledger item.
                    $matches=[];
                    $claims=$this->run("SELECT id,packet,proof FROM custody_tickets WHERE owner=? AND scope=? AND kind='claim' AND status='committed' AND created>=? AND created<=? ORDER BY created,id LIMIT 201",[$row['buyer'],$row['scope'],$anchor['at'],$event['at']]);$claimCount=0;
                    while($claim=$claims->fetch()){
                        ++$claimCount;
                        $packet=json_decode($claim['packet'],true,512,JSON_THROW_ON_ERROR);
                        if(in_array($item,array_column($packet['delivered']??[],'item'),true)){$matches[]=['id'=>$claim['id'],'proof'=>$claim['proof']];if(count($matches)>1)break;}
                    }
                    if($claimCount<=200&&count($matches)===1){$delivery['receipt']=$matches[0]['id'];$delivery['pages']=array_column(json_decode($matches[0]['proof'],true,512,JSON_THROW_ON_ERROR),'page');}
                }elseif($next!==false)$delivery=['state'=>'onward','label'=>'Transferred onward before a delivery acknowledgement'];
                else{$current=$this->run('SELECT owner,state,metadata FROM escrow_items WHERE item=? AND scope=?',[$item,$row['scope']])->fetch();if($current&&$current['owner']===$row['buyer']&&in_array($current['state'],['available','listed','claiming'],true))$delivery=['state'=>$current['state']==='claiming'?'delivering':'pending','label'=>$current['state']==='claiming'?'Delivery in progress':'Held in recipient Vault; no delivery acknowledgement'];}
            }
            $assets[]=['leg'=>'goods','kind'=>'item','sender'=>$this->auditPlayer($row['seller']),'recipient'=>$this->auditPlayer($row['buyer']),'item_id'=>$item,'source_item'=>$item,'quantity'=>null,'snapshot'=>$this->statsItem($item),'historical'=>true,'delivery'=>$delivery];
        }
        if($row['receipt'])foreach($this->run("SELECT kind,stat,amount FROM wallet_entries WHERE owner=? AND scope=? AND reference=? AND action='trade_proceeds' AND amount>0",[$row['seller'],$row['scope'],$row['receipt']])->fetchAll() as $credit){
            $snapshot=['kind'=>$credit['kind'],'code'=>$credit['kind']==='gold'?'gld':'Z01','label'=>$credit['kind']==='gold'?'Gold':$credit['stat'],'quality'=>2,'index'=>-1,'ethereal'=>-1,'quantity'=>(int)$credit['amount']];if($credit['kind']==='bag')$snapshot['stat']=$credit['stat'];
            $assets[]=['leg'=>'payment','kind'=>$credit['kind'],'sender'=>$this->auditPlayer($row['buyer']),'recipient'=>$this->auditPlayer($row['seller']),'item_id'=>'','source_item'=>'','quantity'=>(int)$credit['amount'],'snapshot'=>$snapshot,'historical'=>true,'delivery'=>['state'=>'wallet','label'=>'Vault credit confirmed by historical ledger']];
        }
        return $assets;
    }
    public function ownerTrade(array $actor,bool $hc,string $id,array $query=[]): array {
        $this->requireOwner($actor);self::text($id,64);
        return $this->transaction(function()use($hc,$id,$query){
            $row=$this->run('SELECT t.* FROM ('.$this->auditTradeSource().') t WHERE t.id=? AND t.scope=?',[$id,$this->scope($hc)])->fetch();if(!$row)throw new MarketError(404,'Trade not found in this market.');
            $result=$this->auditTradeRow($row)+['mod'=>$this->mod,'game_mode'=>'2.4','hardcore'=>$hc];$assets=[];
            if($row['receipt'])foreach($this->run('SELECT * FROM market_trade_assets WHERE receipt=? AND scope=? ORDER BY seq',[$row['receipt'],$row['scope']])->fetchAll() as $asset){
                $assets[]=['leg'=>$asset['leg'],'kind'=>$asset['kind'],'sender'=>$this->auditPlayer($asset['sender']),'recipient'=>$this->auditPlayer($asset['recipient']),'item_id'=>$asset['item'],'source_item'=>$asset['source_item'],'quantity'=>(int)$asset['quantity'],'snapshot'=>json_decode($asset['snapshot'],true,512,JSON_THROW_ON_ERROR),'delivery'=>$this->auditAssetDelivery($asset)];
            }
            $exact=(bool)$assets;if(!$exact&&$row['status']==='completed')$assets=$this->historicalTradeAssets($row);
            $result['assets']=$assets;$result['exact_assets_recorded']=$exact;
            // Historic asking prices are not proof of which payment items moved.
            $result['evidence_note']=$exact?'Delivery receipts confirm a save write was acknowledged; they do not prove a player inspected the item. Gold and bag resources are credited to Vault; later withdrawals cannot be attributed to particular proceeds.':($row['status']==='completed'?'This older trade has no exact asset-transfer snapshot. Existing item-delivery events and Vault credits are shown where available. Original item rolls/quantities and exact payment-item identities may be unavailable; missing delivery evidence does not prove non-delivery.':'This proposal has not settled. No item or payment transfer is recorded for it.');
            $args=[$row['item']];$where='item=?';
            if($row['record_type']==='offer'){$where='offer=?';$args=[$id];}
            if(isset($query['events_before'])){$where.=' AND seq<?';$args[]=self::number($query['events_before'],1,PHP_INT_MAX);}
            $events=$this->run('SELECT seq,at,actor,kind,message,price,receipt FROM events WHERE '.$where.' ORDER BY seq DESC LIMIT 101',$args)->fetchAll();$more=count($events)>100;if($more)array_pop($events);
            $result['events_next']=$more?(int)end($events)['seq']:null;
            $result['events']=array_map(fn($e)=>['seq'=>(int)$e['seq'],'at'=>(int)$e['at'],'actor'=>$this->auditPlayer($e['actor']),'kind'=>$e['kind'],'message'=>$e['message'],'price'=>json_decode($e['price'],true,512,JSON_THROW_ON_ERROR),'receipt'=>$e['receipt']],$events);
            return $result;
        });
    }
}
