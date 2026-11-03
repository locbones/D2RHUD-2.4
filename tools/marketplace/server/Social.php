<?php
declare(strict_types=1);

trait MarketplaceSocial {
    private function socialSchema(string $engine): void {
        foreach([
            'market_names'=>'name_key CHAR(64) PRIMARY KEY, owner VARCHAR(64) NOT NULL UNIQUE',
            'market_favorites'=>'owner VARCHAR(64) NOT NULL, scope VARCHAR(255) NOT NULL, kind VARCHAR(16) NOT NULL, target VARCHAR(64) NOT NULL, PRIMARY KEY(owner,scope,kind,target)',
            'market_bidder_aliases'=>'listing CHAR(32) NOT NULL, buyer VARCHAR(64) NOT NULL, alias VARCHAR(32) NOT NULL, PRIMARY KEY(listing,buyer)',
            'market_public_offers'=>($engine?'seq BIGINT AUTO_INCREMENT PRIMARY KEY':'seq INTEGER PRIMARY KEY AUTOINCREMENT').', offer VARCHAR(64) NOT NULL UNIQUE, listing CHAR(32) NOT NULL',
        ] as $name=>$columns)$this->db->exec("CREATE TABLE IF NOT EXISTS $name ($columns)$engine");
        $insert=$engine?'INSERT IGNORE':'INSERT OR IGNORE';
        foreach($this->run('SELECT id,name FROM actors ORDER BY id')->fetchAll() as $actor)
            $this->run("$insert INTO market_names VALUES (?,?)",[hash('sha256',strtolower(trim($actor['name']))),$actor['id']]);
        foreach($this->run('SELECT id,listing,buyer FROM offers ORDER BY created,id')->fetchAll() as $offer)$this->publicOfferRegister($offer);
    }
    private function publicOfferRegister(array $offer): void {
        $insert=$this->db->getAttribute(PDO::ATTR_DRIVER_NAME)==='mysql'?'INSERT IGNORE':'INSERT OR IGNORE';
        $this->run("$insert INTO market_bidder_aliases VALUES (?,?,?)",[$offer['listing'],$offer['buyer'],'Trader '.strtoupper(bin2hex(random_bytes(4)))]);
        $this->run("$insert INTO market_public_offers (offer,listing) VALUES (?,?)",[$offer['id'],$offer['listing']]);
    }
    private function socialSnapshot(string $owner,bool $hc): array {
        return ['favorites'=>$this->run('SELECT kind,target FROM market_favorites WHERE owner=? AND scope=? ORDER BY kind,target',[$owner,$this->scope($hc)])->fetchAll()];
    }
    private function socialCommand(array $actor,array $data,bool $hc): array {
        if(($data['type']??'')==='offer_page')return $this->publicOffers($hc,$data,$actor);
        return $this->transaction(function()use($actor,$data,$hc){
            $this->lockScope($hc);$owner=$actor['id'];$id=self::ticketId($data['id']??null);$hash=hash('sha256',$this->scope($hc).'|'.self::json($data));
            $current=$this->run('SELECT name FROM actors WHERE id=?'.$this->lock(),[$owner])->fetchColumn();
            $old=$this->run('SELECT hash,result FROM commands WHERE actor=? AND id=?',[$owner,$id])->fetch();
            if($old){if(!hash_equals($old['hash'],$hash))throw new MarketError(409,'Command ID reused.');return json_decode($old['result'],true,512,JSON_THROW_ON_ERROR);}
            if($data['type']==='profile_name'){
                $name=trim(self::text($data['name']??null,15));
                if(!preg_match('/^[A-Za-z0-9][A-Za-z0-9 ._-]{2,14}$/D',$name))throw new MarketError(422,'Use 3–15 letters, numbers, spaces, dots, underscores or hyphens.');
                if(($data['expected_name']??null)!==$current)throw new MarketError(409,'Your name changed. Refresh before saving it again.');
                if($this->run('SELECT id FROM actors WHERE LOWER(name)=LOWER(?) AND id<>?',[$name,$owner])->fetchColumn())throw new MarketError(409,'That player name is already in use.');
                $key=hash('sha256',strtolower($name));$held=$this->run('SELECT owner FROM market_names WHERE name_key=?',[$key])->fetchColumn();
                if($held&&$held!==$owner)throw new MarketError(409,'That player name is already in use.');
                $this->run('DELETE FROM market_names WHERE owner=?',[$owner]);
                try{$this->run('INSERT INTO market_names VALUES (?,?)',[$key,$owner]);}catch(PDOException $e){if(str_starts_with($e->getCode(),'23'))throw new MarketError(409,'That player name is already in use.');throw $e;}
                $this->run('UPDATE actors SET name=? WHERE id=?',[$name,$owner]);$result=['name'=>$name];
            }elseif($data['type']==='favorite'){
                $kind=$data['kind']??'';$target=self::text($data['target']??null,64);$enabled=$data['enabled']??null;
                if(!in_array($kind,['listing','seller'],true)||!is_bool($enabled))throw new MarketError(422,'Choose a trade or seller favorite.');
                if($enabled){
                    $exists=$kind==='listing'?$this->run('SELECT l.id FROM listings l JOIN items i ON i.id=l.item WHERE l.id=? AND i.scope=?',[$target,$this->scope($hc)])->fetchColumn():$this->run("SELECT id FROM actors WHERE id=? AND role IN ('player','tester')",[$target])->fetchColumn();
                    if(!$exists)throw new MarketError(404,'Trade or seller not found.');
                    if((int)$this->run('SELECT COUNT(*) FROM market_favorites WHERE owner=? AND scope=?',[$owner,$this->scope($hc)])->fetchColumn()>=2048&&!$this->run('SELECT target FROM market_favorites WHERE owner=? AND scope=? AND kind=? AND target=?',[$owner,$this->scope($hc),$kind,$target])->fetchColumn())throw new MarketError(422,'Remove a favorite before adding more.');
                }
                $this->run('DELETE FROM market_favorites WHERE owner=? AND scope=? AND kind=? AND target=?',[$owner,$this->scope($hc),$kind,$target]);
                if($enabled)$this->run('INSERT INTO market_favorites VALUES (?,?,?,?)',[$owner,$this->scope($hc),$kind,$target]);
                $result=['favorite'=>$enabled,'kind'=>$kind,'target'=>$target];
            }else throw new MarketError(422,'Unknown profile command.');
            $this->run('INSERT INTO commands VALUES (?,?,?,?)',[$owner,$id,$hash,self::json($result)]);return $result;
        });
    }
    public function publicOffers(bool $hc,array $query,?array $actor=null): array {
        $listing=self::text($query['listing_id']??null,32);
        if(!$this->run('SELECT l.id FROM listings l JOIN items i ON i.id=l.item WHERE l.id=? AND i.scope=?',[$listing,$this->scope($hc)])->fetchColumn())throw new MarketError(404,'Trade not found in this game mode.');
        $before=isset($query['before'])?self::number($query['before'],1,PHP_INT_MAX):PHP_INT_MAX;
        $rows=$this->run('SELECT p.seq,o.*,a.alias FROM market_public_offers p JOIN offers o ON o.id=p.offer JOIN market_bidder_aliases a ON a.listing=o.listing AND a.buyer=o.buyer WHERE p.listing=? AND p.seq<? ORDER BY p.seq DESC LIMIT 5',[$listing,$before])->fetchAll();
        $more=count($rows)>4;if($more)array_pop($rows);$out=[];$names=$actor?$this->actorNames():[];
        foreach($rows as $r){
            $row=['alias'=>$r['alias'],'status'=>$r['status'],'price'=>json_decode($r['price'],true,512,JSON_THROW_ON_ERROR),'at'=>(int)($this->run("SELECT MAX(at) FROM events WHERE offer=? AND kind IN ('proposed','counter')",[$r['id']])->fetchColumn()?:$r['created']),'from'=>$r['proposer']===$r['seller']?'seller':'bidder'];
            // This branch is the only private addition. Public viewers receive no
            // player IDs, real names, offer IDs, events or message bodies.
            if($actor&&in_array($actor['id'],[$r['seller'],$r['buyer']],true))$row['conversation']=$this->conversationSnapshot($r,$names);
            $out[]=$row;
        }
        return ['listing_id'=>$listing,'rows'=>$out,'next_before'=>$more?(int)end($rows)['seq']:null,'total'=>(int)$this->run('SELECT COUNT(*) FROM offers WHERE listing=?',[$listing])->fetchColumn()];
    }
    public function privateConversation(array $actor,bool $hc,string $id): array {
        $offer=$this->offer(self::text($id,64),$actor);
        if(!$this->run('SELECT l.id FROM listings l JOIN items i ON i.id=l.item WHERE l.id=? AND i.scope=?',[$offer['listing'],$this->scope($hc)])->fetchColumn())throw new MarketError(404,'Conversation not found in this game mode.');
        $row=$this->conversationSnapshot($offer,$this->actorNames());
        $item=$this->run('SELECT i.*,e.metadata,d.metadata AS discovery FROM items i LEFT JOIN escrow_items e ON e.item=i.id LEFT JOIN web_item_display d ON d.item=i.id WHERE i.id=?',[$row['item']])->fetch();
        $meta=$item['metadata']?json_decode($item['metadata'],true,512,JSON_THROW_ON_ERROR):[];
        $row['item_details']=['code'=>$item['code'],'quality'=>(int)$item['quality'],'index'=>(int)$item['item_index'],'saved_id'=>(int)$item['seed'],'ledger_id'=>$item['id'],'ethereal'=>(bool)$item['ethereal'],'sockets'=>(int)$item['sockets'],'identified'=>$meta['identified']??1,'quantity'=>$meta['quantity']??1,'display'=>$meta['display']??($item['discovery']?json_decode($item['discovery'],true,512,JSON_THROW_ON_ERROR):null)];
        $row['item_details']=$this->browserItemNames($row['item_details'],$item['item_hex']);$members=$this->bundleGoods($row['listing'],$row['item']);$title=$this->run('SELECT title FROM market_bundles WHERE listing=?',[$row['listing']])->fetchColumn();if($title)$row['bundle']=['id'=>$row['listing'],'title'=>$title,'members'=>array_map(fn($id)=>['item_id'=>$id,'item'=>$this->statsItem($id)],$members)];
        unset($row['item_hex']);return $row;
    }
    private function conversationSnapshot(array $offer,array $names): array {
        $item=$this->run('SELECT i.seed,i.code,i.item_hex,l.item FROM listings l JOIN items i ON i.id=l.item WHERE l.id=?',[$offer['listing']])->fetch();
        $o=array_merge($offer,$item);unset($o['seq'],$o['alias']);$o['price']=json_decode($o['price'],true,512,JSON_THROW_ON_ERROR);
        $o['events']=$this->run('SELECT id,at,actor,kind,message,price FROM events WHERE offer=? AND kind<>\'bundle_sale\' ORDER BY seq LIMIT 512',[$o['id']])->fetchAll();
        foreach($o['events'] as &$e){$e['at']=(int)$e['at'];$e['actor_name']=$names[$e['actor']]??$e['actor'];$e['price']=json_decode($e['price'],true,512,JSON_THROW_ON_ERROR);}unset($e);
        foreach(['revision','listing_revision','created','seed'] as $key)$o[$key]=(int)$o[$key];$o['seller_name']=$names[$o['seller']]??'Player';$o['buyer_name']=$names[$o['buyer']]??'Player';return $o;
    }
}
