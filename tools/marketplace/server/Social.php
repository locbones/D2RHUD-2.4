<?php
declare(strict_types=1);

trait MarketplaceSocial {
    private ?array $publicPlayerNumbers=null;
    private function socialSchema(string $engine): void {
        foreach([
            'market_player_ids'=>($engine?'number BIGINT AUTO_INCREMENT PRIMARY KEY':'number INTEGER PRIMARY KEY AUTOINCREMENT').', owner VARCHAR(64) NOT NULL UNIQUE',
            'market_profile_recoveries'=>'owner VARCHAR(64) NOT NULL PRIMARY KEY, community VARCHAR(64) NOT NULL, name VARCHAR(128) NOT NULL, previous_owner VARCHAR(64) NOT NULL, grant_owner INT NOT NULL, grant_moderator INT NOT NULL, claimed BIGINT NOT NULL',
            'market_names'=>'name_key CHAR(64) PRIMARY KEY, owner VARCHAR(64) NOT NULL UNIQUE',
            'market_favorites'=>'owner VARCHAR(64) NOT NULL, scope VARCHAR(255) NOT NULL, kind VARCHAR(16) NOT NULL, target VARCHAR(64) NOT NULL, PRIMARY KEY(owner,scope,kind,target)',
            'market_bidder_aliases'=>'listing CHAR(32) NOT NULL, buyer VARCHAR(64) NOT NULL, alias VARCHAR(32) NOT NULL, PRIMARY KEY(listing,buyer)',
            'market_public_offers'=>($engine?'seq BIGINT AUTO_INCREMENT PRIMARY KEY':'seq INTEGER PRIMARY KEY AUTOINCREMENT').', offer VARCHAR(64) NOT NULL UNIQUE, listing CHAR(32) NOT NULL',
        ] as $name=>$columns)$this->db->exec("CREATE TABLE IF NOT EXISTS $name ($columns)$engine");
        $insert=$engine?'INSERT IGNORE':'INSERT OR IGNORE';
        foreach($this->run('SELECT id FROM actors ORDER BY id')->fetchAll() as $actor)$this->publicPlayerId($actor['id']);
        foreach($this->run('SELECT id,listing,buyer FROM offers ORDER BY created,id')->fetchAll() as $offer)$this->publicOfferRegister($offer);
    }
    public function publicPlayerId(string $owner): string {
        if($owner==='')return '';
        $this->publicPlayerNumbers??=$this->run('SELECT owner,number FROM market_player_ids')->fetchAll(PDO::FETCH_KEY_PAIR);
        $number=$this->publicPlayerNumbers[$owner]??$this->run('SELECT number FROM market_player_ids WHERE owner=?',[$owner])->fetchColumn();
        if($number===false){
            $insert=$this->db->getAttribute(PDO::ATTR_DRIVER_NAME)==='mysql'?'INSERT IGNORE':'INSERT OR IGNORE';
            $this->run("$insert INTO market_player_ids (owner) VALUES (?)",[$owner]);
            $number=$this->run('SELECT number FROM market_player_ids WHERE owner=?',[$owner])->fetchColumn();
        }
        return 'P-'.str_pad((string)$number,6,'0',STR_PAD_LEFT);
    }
    private function resolveLinkedPlayer(string $value): array {
        $sql="SELECT a.id,a.name FROM actors a JOIN market_players p ON p.owner=a.id WHERE a.role IN ('player','tester')";
        $rows=$this->run($sql.' AND a.id=?',[$value])->fetchAll();
        if(!$rows&&preg_match('/^P-([0-9]+)$/iD',$value,$match))$rows=$this->run($sql.' AND a.id=(SELECT owner FROM market_player_ids WHERE number=?)',[$match[1]])->fetchAll();
        if(!$rows)$rows=$this->run($sql.' AND LOWER(a.name)=LOWER(?) LIMIT 2',[$value])->fetchAll();
        if(count($rows)>1)throw new MarketError(422,'More than one player uses that name. Choose their player ID or open their profile.');
        if(!$rows)throw new MarketError(422,'Choose a linked player by name or player ID.');
        return $rows[0];
    }
    private function claimProfileRecovery(string $owner,string $name): void {
        $r=$this->run('SELECT * FROM market_profile_recoveries WHERE owner=? AND community=?'.$this->lock(),[$owner,$this->community])->fetch();
        if(!$r||(int)$r['claimed']!==0||strcasecmp($r['name'],$name)!==0)return;
        if($r['grant_owner'])$this->grantOwner($owner);
        if($r['grant_moderator'])$this->grantModerator($owner);
        $this->run('UPDATE market_profile_recoveries SET claimed=? WHERE owner=? AND claimed=0',[time(),$owner]);
    }
    // Operator-only replacement. Preserve financial/audit references as a
    // tombstone, revoke every login/agent, and bind recovery to one proven ID.
    public function retirePlayer(string $previous,string $replacement,string $name): array {
        return $this->transaction(function()use($previous,$replacement,$name){
            if($previous===$replacement)throw new MarketError(422,'Choose a different replacement player.');
            if(!preg_match('/^[A-Za-z0-9][A-Za-z0-9 ._-]{2,14}$/D',$name))throw new MarketError(422,'Choose a valid public name.');
            $this->run('SELECT phase FROM market_control WHERE community=?'.$this->lock(),[$this->community])->fetch();
            $old=$this->browserActor($previous);$new=$this->browserActor($replacement);
            if($old['role']!=='player'||$new['role']!=='player'||!$this->run('SELECT owner FROM market_players WHERE owner=?',[$replacement])->fetchColumn())throw new MarketError(422,'Choose real players and a code-linked replacement.');
            foreach([
                "SELECT COUNT(*) FROM escrow_items WHERE owner=? AND state<>'delivered'"=>[$previous],
                'SELECT COUNT(*) FROM escrow_balances WHERE owner=? AND quantity>0'=>[$previous],
                "SELECT COUNT(*) FROM custody_tickets WHERE owner=? AND status='prepared'"=>[$previous],
                "SELECT COUNT(*) FROM wallet_withdrawals WHERE owner=? AND status NOT IN ('completed','cancelled')"=>[$previous],
                "SELECT COUNT(*) FROM listings WHERE seller=? AND status IN ('open','agreed')"=>[$previous],
                "SELECT COUNT(*) FROM offers WHERE (buyer=? OR seller=?) AND status IN ('pending','accepted')"=>[$previous,$previous],
            ] as $sql=>$args)if($this->run($sql,$args)->fetchColumn())throw new MarketError(409,'Return or transfer all holdings and close pending trades before retiring this player.');
            $owner=$this->isOwner($old);$moderator=$this->isModerator($previous);
            $this->run('INSERT INTO market_profile_recoveries VALUES (?,?,?,?,?,?,0)',[$replacement,$this->community,$name,$previous,(int)$owner,(int)$moderator]);
            foreach(['market_owners','market_moderators'] as $table)$this->run("DELETE FROM $table WHERE owner=? AND community=?",[$previous,$this->community]);
            foreach(['market_players','market_memberships','market_characters','market_names','web_tickets','web_profiles','market_installations','custody_agents_scoped','custody_agents'] as $table)$this->run("DELETE FROM $table WHERE owner=?",[$previous]);
            $this->run("UPDATE web_intents SET status='cancelled',message='Player account retired.' WHERE owner=? AND status='queued'",[$previous]);
            $this->run("UPDATE actors SET name='Deleted player',role='retired',token_hash=? WHERE id=?",[hash('sha256',random_bytes(32)),$previous]);
            return ['retired'=>$previous,'replacement'=>$replacement,'claim_name'=>$name,'permissions_pending'=>['owner'=>$owner,'moderator'=>$moderator]];
        });
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
                $this->run('DELETE FROM market_names WHERE owner=?',[$owner]);
                $this->run('UPDATE actors SET name=? WHERE id=?',[$name,$owner]);
                $this->claimProfileRecovery($owner,$name);
                $result=['name'=>$name,'player_id'=>$this->publicPlayerId($owner)];
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
        $item=$this->run('SELECT i.seed,i.code,i.item_hex,i.scope,l.item FROM listings l JOIN items i ON i.id=l.item WHERE l.id=?',[$offer['listing']])->fetch();
        $o=array_merge($offer,$item);unset($o['seq'],$o['alias'],$o['scope']);$o['price']=json_decode($o['price'],true,512,JSON_THROW_ON_ERROR);
        if($o['status']==='accepted')$o['settlement_progress']=$this->settlementProgress($o,$item['scope'],$o['price']);
        $o['events']=$this->run('SELECT id,at,actor,kind,message,price FROM events WHERE offer=? AND kind<>\'bundle_sale\' ORDER BY seq LIMIT 512',[$o['id']])->fetchAll();
        foreach($o['events'] as &$e){$e['at']=(int)$e['at'];$e['actor_name']=$names[$e['actor']]??$e['actor'];$e['price']=json_decode($e['price'],true,512,JSON_THROW_ON_ERROR);}unset($e);
        foreach(['revision','listing_revision','created','seed'] as $key)$o[$key]=(int)$o[$key];$o['seller_name']=$names[$o['seller']]??'Player';$o['buyer_name']=$names[$o['buyer']]??'Player';return $o;
    }
}
