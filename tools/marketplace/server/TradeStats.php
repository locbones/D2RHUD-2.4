<?php
declare(strict_types=1);
trait MarketplaceTradeStats {
    private function saleAverages(array $selectors,array $arguments): array {
        $since=time()-30*86400;$selectors[]="NOT EXISTS (SELECT b.listing FROM market_bundles b JOIN offers o ON o.listing=b.listing WHERE o.id=e.offer)";
        $rows=$this->run("SELECT e.price,e.at FROM events e JOIN items i ON i.id=e.item WHERE ".implode(' AND ',$selectors)." AND e.at>=? AND e.at<=? ORDER BY e.at DESC,e.seq DESC LIMIT 5001",array_merge($arguments,[$since,time()]))->fetchAll();
        $truncated=count($rows)>5000;if($truncated)array_pop($rows);$groups=[];$free=0;
        foreach($rows as $sale){
            $price=json_decode($sale['price'],true,512,JSON_THROW_ON_ERROR);
            if(self::isFree($price)){++$free;continue;}
            $identity=fn($r)=>[$r['kind']??'item',$r['code'],$r['quality'],$r['index'],$r['ethereal'],$r['stat']??''];
            usort($price,fn($a,$b)=>$identity($a)<=>$identity($b));$key=self::json(array_map($identity,$price));
            if(!isset($groups[$key]))$groups[$key]=['sales'=>0,'price'=>array_map(fn($r)=>array_replace($r,['quantity'=>0]),$price),'last_at'=>(int)$sale['at'],'samples'=>array_fill(0,count($price),[])];
            ++$groups[$key]['sales'];foreach($price as $i=>$r){$groups[$key]['price'][$i]['quantity']+=$r['quantity'];$groups[$key]['samples'][$i][]=$r['quantity'];}
        }
        foreach($groups as &$group){foreach(['median','low','high'] as $key)$group[$key.'_price']=$group['price'];foreach($group['price'] as $i=>&$payment){$payment['quantity']=$payment['quantity']/$group['sales'];$samples=$group['samples'][$i];sort($samples,SORT_NUMERIC);$n=count($samples);$group['median_price'][$i]['quantity']=$n%2?$samples[intdiv($n,2)]:($samples[$n/2-1]+$samples[$n/2])/2;$group['low_price'][$i]['quantity']=$samples[0];$group['high_price'][$i]['quantity']=$samples[$n-1];}unset($payment);unset($group['samples']);}unset($group);
        $groups=array_values($groups);usort($groups,fn($a,$b)=>($b['sales']<=>$a['sales'])?:($b['last_at']<=>$a['last_at']));
        return ['days'=>30,'sales'=>count($rows),'free_sales'=>$free,'groups'=>array_slice($groups,0,4),'other_groups'=>max(0,count($groups)-4),'sample_limit'=>5000,'truncated'=>$truncated];
    }
    public function tradeStats(bool $hardcore,array $query=[]): array {
        $window=$query['window']??'30';
        if(!in_array($window,['7','30','all'],true))throw new MarketError(422,'Choose the last 7 days, 30 days or all time.');
        return $this->transaction(function()use($hardcore,$window){
            $scope=$this->scope($hardcore);$now=time();$since=$window==='all'?0:$now-(int)$window*86400;
            $where="i.scope=? AND e.kind='completed_sale' AND e.at>=? AND e.at<=?";$args=[$scope,$since,$now];
            $from=' FROM events e JOIN items i ON i.id=e.item WHERE '.$where;
            $counts=$this->run('SELECT COUNT(*) AS trades,COUNT(DISTINCT e.seller) AS sellers,COUNT(DISTINCT e.buyer) AS buyers'.$from,$args)->fetch();
            $active=" FROM listings l JOIN items i ON i.id=l.item LEFT JOIN published_items p ON p.listing=l.id WHERE i.scope=? AND l.status='open' AND (p.listing IS NULL OR p.expires>?)";
            $counts['active_listings']=(int)$this->run('SELECT COUNT(*)'.$active,[$scope,$now])->fetchColumn();
            foreach($counts as &$n)$n=(int)$n;unset($n);
            $names=$this->actorNames();
            $recent=$this->run('SELECT e.seq,e.item,e.at,e.price,e.seller'.$from.' ORDER BY e.at DESC,e.seq DESC LIMIT 10',$args)->fetchAll();
            foreach($recent as &$row){$row['at']=(int)$row['at'];$row['price']=json_decode($row['price'],true,512,JSON_THROW_ON_ERROR);$row['seller_name']=$names[$row['seller']]??'Player';$row['item']=$this->statsItem($row['item']);unset($row['seq']);}unset($row);
            $popular=$this->run('SELECT i.code,i.quality,i.item_index,COUNT(*) AS trades,MAX(e.seq) AS last_seq'.$from.' GROUP BY i.code,i.quality,i.item_index ORDER BY trades DESC,last_seq DESC LIMIT 8',$args)->fetchAll();
            foreach($popular as &$row){$id=$this->run('SELECT item FROM events WHERE seq=?',[$row['last_seq']])->fetchColumn();$row=['trades'=>(int)$row['trades'],'item'=>$this->statsItem($id)];}unset($row);
            $sellers=$this->run('SELECT e.seller,COUNT(*) AS trades,MAX(e.at) AS last_sale'.$from.' GROUP BY e.seller ORDER BY trades DESC,last_sale DESC,e.seller LIMIT 8',$args)->fetchAll();
            foreach($sellers as &$row){$row=['id'=>$row['seller'],'name'=>$names[$row['seller']]??'Player','trades'=>(int)$row['trades'],'active_listings'=>(int)$this->run('SELECT COUNT(*)'.$active.' AND l.seller=?',[$scope,$now,$row['seller']])->fetchColumn()];}unset($row);
            return ['window'=>$window,'since'=>$since,'generated_at'=>$now,'mod'=>$this->mod,'game_mode'=>'2.4','hardcore'=>$hardcore,'summary'=>$counts,'recent'=>$recent,'popular'=>$popular,'sellers'=>$sellers];
        });
    }
    private function statsItem(string $id): array {
        $i=$this->run('SELECT i.*,e.metadata,d.metadata AS discovery FROM items i LEFT JOIN escrow_items e ON e.item=i.id LEFT JOIN web_item_display d ON d.item=i.id WHERE i.id=?',[$id])->fetch();
        $metadata=$i['metadata']?json_decode($i['metadata'],true,512,JSON_THROW_ON_ERROR):[];
        return $this->browserItemNames(['code'=>$i['code'],'quality'=>(int)$i['quality'],'index'=>(int)$i['item_index'],'saved_id'=>(int)$i['seed'],'ledger_id'=>$id,'ethereal'=>(bool)$i['ethereal'],'sockets'=>(int)$i['sockets'],'identified'=>$metadata['identified']??1,'quantity'=>$metadata['quantity']??1,'display'=>$metadata['display']??($i['discovery']?json_decode($i['discovery'],true,512,JSON_THROW_ON_ERROR):null)],$i['item_hex']);
    }
}
