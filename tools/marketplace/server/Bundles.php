<?php
declare(strict_types=1);

// A bundle has one listing, one payment and one atomic settlement. Its members
// remain separate ledgers so each saved item's ownership can still be audited.
trait MarketplaceBundles {
    private function bundleSchema(string $engine): void {
        $this->db->exec("CREATE TABLE IF NOT EXISTS market_bundles (listing CHAR(32) PRIMARY KEY, title VARCHAR(80) NOT NULL)$engine");
        $this->db->exec("CREATE TABLE IF NOT EXISTS market_bundle_items (listing CHAR(32) NOT NULL, item CHAR(32) NOT NULL, ordinal INT NOT NULL, PRIMARY KEY(listing,item))$engine");
    }
    private function activeBundle(string $item): ?array {
        return $this->run("SELECT l.id,l.revision,l.seller,b.title FROM market_bundle_items m JOIN listings l ON l.id=m.listing JOIN market_bundles b ON b.listing=l.id WHERE m.item=? AND l.status IN ('open','agreed')",[$item])->fetch()?:null;
    }
    private function bundleGoods(string $listing,string $root): array {
        $rows=$this->run('SELECT item FROM market_bundle_items WHERE listing=? ORDER BY ordinal',[$listing])->fetchAll(PDO::FETCH_COLUMN);
        return $rows?:[$root];
    }
    private function requireSingleItem(string $item): void {
        if($this->activeBundle($item))throw new MarketError(409,'This item belongs to a bundle. Unbundle the trade on the website before changing or returning individual items.');
    }
    private function bundleCommand(array $actor,array $data,bool $hc): array {
        return $this->transaction(function()use($actor,$data,$hc){
            $this->lockScope($hc);$scope=$this->scope($hc);$owner=$actor['id'];$id=self::ticketId($data['id']??null);$hash=hash('sha256',$scope.'|'.self::json($data));
            $old=$this->run('SELECT hash,result FROM commands WHERE actor=? AND id=?',[$owner,$id])->fetch();
            if($old){if(!hash_equals($old['hash'],$hash))throw new MarketError(409,'Bundle command ID reused.');return json_decode($old['result'],true,512,JSON_THROW_ON_ERROR);}
            if($actor['role']!=='player')throw new MarketError(403,'Only a player can list escrow bundles.');
            if($data['type']==='bundle_dissolve'){
                $listing=self::ticketId($data['listing_id']??null);
                $l=$this->run('SELECT l.* FROM listings l JOIN market_bundles b ON b.listing=l.id JOIN items i ON i.id=l.item WHERE l.id=? AND i.scope=?',[$listing,$scope])->fetch();
                if(!$l||$l['seller']!==$owner||$l['status']!=='open'||(int)$l['revision']!==self::number($data['expected_revision']??null,1,2147483647))throw new MarketError(409,'The bundle changed or has agreed terms. Refresh before changing it.');
                $members=$this->bundleGoods($listing,$l['item']);
                foreach($members as $item){$r=$this->run("SELECT metadata FROM escrow_items WHERE item=? AND owner=? AND scope=? AND state='listed'",[$item,$owner,$scope])->fetchColumn();if(!$r)throw new MarketError(409,'A bundle member changed.');$m=json_decode($r,true,512,JSON_THROW_ON_ERROR);$m['price']=[];unset($m['delivery_requested']);$this->run("UPDATE escrow_items SET state='available',metadata=? WHERE item=?",[self::json($m),$item]);}
                $this->invalidateOffers($listing,$actor,'The seller unbundled this trade.');$this->run("UPDATE listings SET status='withdrawn',revision=revision+1 WHERE id=?",[$listing]);
                $result=['unbundled'=>true,'items'=>$members];
            }else{
                $members=$data['items']??null;
                if(!is_array($members)||!array_is_list($members)||count($members)<2||count($members)>16)throw new MarketError(422,'Choose 2 to 16 different escrow items.');
                $members=array_map(fn($v)=>self::ticketId($v),$members);if(count(array_unique($members))!==count($members))throw new MarketError(422,'Choose different items for the bundle.');$title=trim(self::text($data['title']??null,80));if($title==='')throw new MarketError(422,'Name your bundle.');$price=self::price($data['price']??null);$rows=[];
                foreach($members as $item){$this->requireSingleItem($item);$r=$this->run("SELECT * FROM escrow_items WHERE item=? AND owner=? AND scope=? AND state IN ('available','listed')",[$item,$owner,$scope])->fetch();if(!$r)throw new MarketError(409,'Choose items currently held in your escrow.');
                    if($this->run("SELECT id FROM listings WHERE item=? AND status='agreed'",[$item])->fetchColumn())throw new MarketError(409,'An item already has agreed terms.');$rows[]=$r;}
                $listing=self::id();
                foreach($rows as $n=>$r){foreach($this->run("SELECT id FROM listings WHERE item=? AND status='open'",[$r['item']])->fetchAll() as $l){$this->invalidateOffers($l['id'],$actor,'The seller moved this item into a bundle.');$this->run("UPDATE listings SET status='withdrawn',revision=revision+1 WHERE id=?",[$l['id']]);}
                    $m=json_decode($r['metadata'],true,512,JSON_THROW_ON_ERROR);$m['price']=$n===0?$price:[];unset($m['delivery_requested']);$this->run("UPDATE escrow_items SET state='listed',metadata=? WHERE item=?",[self::json($m),$r['item']]);}
                $this->run('INSERT INTO listings VALUES (?,?,?,?,?,?,?)',[$listing,$members[0],$owner,1,'open',self::json($price),time()]);$this->run('INSERT INTO market_bundles VALUES (?,?)',[$listing,$title]);
                foreach($members as $n=>$item)$this->run('INSERT INTO market_bundle_items VALUES (?,?,?)',[$listing,$item,$n]);
                $this->event($members[0],null,$actor,'listed','Bundle: '.$title.' ('.count($members).' items).',$price,$owner);
                $result=['listing_id'=>$listing,'items'=>$members];
            }
            $this->run('INSERT INTO commands VALUES (?,?,?,?)',[$owner,$id,$hash,self::json($result)]);return $result;
        });
    }
    private function browserBundles(string $scope,array $map): array {
        $out=['listings'=>[],'items'=>[]];
        foreach($this->run('SELECT b.*,l.status,l.revision,m.item FROM market_bundles b JOIN listings l ON l.id=b.listing JOIN items i ON i.id=l.item JOIN market_bundle_items m ON m.listing=b.listing WHERE i.scope=? ORDER BY b.listing,m.ordinal',[$scope])->fetchAll() as $r){
            $id=$r['listing'];if(!isset($out['listings'][$id]))$out['listings'][$id]=['id'=>$id,'title'=>$r['title'],'revision'=>(int)$r['revision'],'members'=>[]];
            $out['listings'][$id]['members'][]=['item_id'=>$r['item'],'item'=>$map[$r['item']]??null];if(in_array($r['status'],['open','agreed'],true))$out['items'][$r['item']]=$id;
        }return $out;
    }
}
