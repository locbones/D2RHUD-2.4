<?php
declare(strict_types=1);

// Owner capability is independent of moderator status and display names.
trait MarketplaceOwnerTools {
    private function ownerSchema(string $engine): void {
        foreach([
            'market_owners'=>'community VARCHAR(64) PRIMARY KEY, owner VARCHAR(64) NOT NULL',
            'market_control'=>'community VARCHAR(64) PRIMARY KEY, phase VARCHAR(16) NOT NULL, generation INT NOT NULL, reset_id CHAR(32) NOT NULL, updated BIGINT NOT NULL',
            'market_client_resets'=>'owner VARCHAR(64) NOT NULL, scope VARCHAR(255) NOT NULL, id CHAR(32) NOT NULL, generation INT NOT NULL, acknowledged INT NOT NULL, PRIMARY KEY(owner,scope)',
            'market_owner_actions'=>'id CHAR(32) PRIMARY KEY, owner VARCHAR(64) NOT NULL, kind VARCHAR(32) NOT NULL, hash CHAR(64) NOT NULL, result TEXT NOT NULL, created BIGINT NOT NULL',
        ] as $name=>$columns)$this->db->exec("CREATE TABLE IF NOT EXISTS $name ($columns)$engine");
        $insert=$engine?'INSERT IGNORE':'INSERT OR IGNORE';
        $this->run("$insert INTO market_control VALUES (?, 'live',0,'',?)",[$this->community,time()]);
    }
    public function grantOwner(string $owner): array {
        return $this->transaction(function()use($owner){
            $insert=$this->db->getAttribute(PDO::ATTR_DRIVER_NAME)==='mysql'?'INSERT IGNORE':'INSERT OR IGNORE';$this->run("$insert INTO market_control VALUES (?,'live',0,'',?)",[$this->community,time()]);
            $a=$this->browserActor($owner);if($a['role']!=='player')throw new MarketError(422,'Choose a real player.');
            $old=$this->run('SELECT owner FROM market_owners WHERE community=?'.$this->lock(),[$this->community])->fetchColumn();
            if($old&&$old!==$owner)throw new MarketError(409,'This marketplace already has an owner.');
            if(!$old)$this->run('INSERT INTO market_owners VALUES (?,?)',[$this->community,$owner]);
            return ['owner'=>$owner];
        });
    }
    public function isOwner(?array $actor): bool {
        return $actor&&(bool)$this->run('SELECT owner FROM market_owners WHERE community=? AND owner=?',[$this->community,$actor['id']])->fetchColumn();
    }
    public function requireOwner(?array $actor): void {if(!$this->isOwner($actor))throw new MarketError(403,'Marketplace owner access is required.');}
    public function controlState(): array {
        $r=$this->run('SELECT phase,generation,reset_id,updated FROM market_control WHERE community=?',[$this->community])->fetch();
        if(!$r)return ['phase'=>'live','generation'=>0,'reset_id'=>'','updated'=>0];
        $r['generation']=(int)$r['generation'];$r['updated']=(int)$r['updated'];return $r;
    }
    public function clientReset(string $owner,bool $hc): ?array {
        $r=$this->run('SELECT id,generation,acknowledged FROM market_client_resets WHERE owner=? AND scope=?',[$owner,$this->scope($hc)])->fetch();
        if(!$r)return null;$r['generation']=(int)$r['generation'];$r['acknowledged']=(bool)$r['acknowledged'];return $r;
    }
    // HTTP mutation boundary: reset and ordinary requests take the same DB lock.
    // Existing domain transactions join this outer transaction (no partial commit).
    public function controlledRequest(array $actor,string $action,array $data,bool $hc,callable $callback): array {
        if($action==='custody'&&in_array($data['type']??'',['delivery_queue','profile_state','web_poll'],true)){
            if($this->run('SELECT role FROM actors WHERE id=?',[$actor['owner']??$actor['id']])->fetchColumn()==='retired')throw new MarketError(401,'This player account has been retired. Link your current player.');
            if(($data['type']??'')!=='web_poll')return $callback(); // Authenticated hints only.
            $unchanged=$this->unchangedBrowserPoll($actor,$data,$hc);
            if($unchanged!==null)return $unchanged;
            // Settings, acknowledgements or capabilities changed: keep the
            // original control/scope transaction for that state change.
        }
        return $this->transaction(function()use($actor,$action,$data,$hc,$callback){
            $control=$this->run('SELECT phase FROM market_control WHERE community=?'.$this->lock(),[$this->community])->fetchColumn();
            if($this->run('SELECT role FROM actors WHERE id=?',[$actor['owner']??$actor['id']])->fetchColumn()==='retired')throw new MarketError(401,'This player account has been retired. Link your current player.');
            $type=$data['type']??'';
            $operating=$action==='owner';
            if($operating)$this->requireOwner($actor);
            $recovery=$action==='custody'&&in_array($type,['web_sync','web_poll','delivery_queue','claim_prepare','write_authorize','commit','cancel','status','wallet_notice','browser_link','profile_state'],true);
            if(!$operating&&!$recovery){
                if($control==='draining')throw new MarketError(409,'The owner is clearing the marketplace. Vault returns are still available.');
                $reset=$this->clientReset($actor['owner']??$actor['id'],$hc);
                if($reset&&!$reset['acknowledged'])throw new MarketError(409,'Marketplace reset: let the updated save companion clear old trade drafts first.');
            }
            return $callback();
        });
    }
    private function resetSelections(): array {
        $scope="scope LIKE ? ESCAPE '!'";
        $items="item IN (SELECT id FROM items WHERE $scope)";
        $listings="listing IN (SELECT id FROM listings WHERE $items)";
        return [
            'market_gift_balances'=>"campaign IN (SELECT id FROM market_gift_campaigns WHERE $scope)",
            'market_gift_notices'=>"campaign IN (SELECT id FROM market_gift_campaigns WHERE $scope)",
            'market_gift_claims'=>"campaign IN (SELECT id FROM market_gift_campaigns WHERE $scope)",
            'market_gift_campaigns'=>$scope,
            'market_wanted_offers'=>"wanted IN (SELECT id FROM market_wanted WHERE $scope)",
            'market_wanted'=>$scope,
            'market_browser_features'=>$scope,
            'market_companion_versions'=>$scope,
            'market_message_seen'=>"message IN (SELECT id FROM market_messages WHERE $scope)",
            'market_search_notices'=>"search IN (SELECT id FROM market_searches WHERE $scope)",
            'market_search_hits'=>"search IN (SELECT id FROM market_searches WHERE $scope)",
            'market_listing_comments'=>$listings,'market_bundle_items'=>$listings,'market_bundles'=>$listings,'market_activity_reads'=>$scope,
            'market_report_events'=>"report IN (SELECT id FROM market_reports WHERE $scope)",
            'market_reports'=>$scope,'market_public_offers'=>$listings,'market_bidder_aliases'=>$listings,
            'events'=>$items,'offers'=>$listings,'published_items'=>$scope,
            'web_item_display'=>$items,'item_identities'=>$scope,'identity_reservations'=>$scope,
            'escrow_items'=>$scope,'escrow_balances'=>$scope,'settlements'=>$scope,'market_trade_assets'=>$scope,
            'wallet_entries'=>$scope,'wallet_withdrawals'=>$scope,'market_favorites'=>$scope,
            'market_messages'=>$scope,'market_characters'=>$scope,'web_intents'=>$scope,
            'web_profiles'=>$scope,'market_preferences'=>$scope,'custody_sources'=>$scope,'inventory_sources'=>$scope,
            'market_delivery_sync'=>$scope,
            'listings'=>$items,'items'=>$scope,
        ];
    }
    private function communityPattern(): string {return str_replace(['!','%','_'],['!!','!%','!_'],$this->community).'|%';}
    public function ownerStatus(array $actor): array {
        $this->requireOwner($actor);$pattern=$this->communityPattern();$counts=[];
        foreach($this->resetSelections() as $table=>$where)$counts[$table]=(int)$this->run("SELECT COUNT(*) FROM $table WHERE $where",[$pattern])->fetchColumn();
        $held=$this->run("SELECT e.owner,a.name,e.scope,COUNT(*) AS items FROM escrow_items e JOIN actors a ON a.id=e.owner WHERE e.scope LIKE ? ESCAPE '!' AND e.state<>'delivered' GROUP BY e.owner,a.name,e.scope",[$pattern])->fetchAll();
        $funds=$this->run("SELECT b.owner,a.name,b.scope,b.kind,b.stat,b.quantity FROM escrow_balances b JOIN actors a ON a.id=b.owner WHERE b.scope LIKE ? ESCAPE '!' AND b.quantity>0",[$pattern])->fetchAll();
        $withdrawals=(int)$this->run("SELECT COUNT(*) FROM wallet_withdrawals WHERE scope LIKE ? ESCAPE '!' AND status IN ('queued','delivering')",[$pattern])->fetchColumn();
        $pending=(int)$this->run("SELECT COUNT(*) FROM custody_tickets WHERE scope LIKE ? ESCAPE '!' AND status IN ('prepared','writing')",[$pattern])->fetchColumn();
        return ['owner_player_id'=>$this->publicPlayerId($actor['id']),'control'=>$this->controlState(),'counts'=>$counts,'held_items'=>$held,'held_balances'=>$funds,'withdrawals'=>$withdrawals,'pending_save_operations'=>$pending,'ready_to_clear'=>!$held&&!$funds&&!$withdrawals&&!$pending];
    }
    private function enqueueResetIntent(string $owner,string $scope,string $kind,array $packet,string $id): void {
        $this->run('INSERT INTO web_intents (id,owner,scope,kind,packet,status,message,created) VALUES (?,?,?,?,?,?,?,?)',[$id,$owner,$scope,$kind,self::json($packet),'queued','',time()]);
    }
    private function beginClear(array $actor): array {
        $state=$this->controlState();$pattern=$this->communityPattern();
        // A prepared deposit could already own a changed save. Finish it before
        // closing the intake gate, then retry; no custody record is discarded.
        if($this->run("SELECT id FROM custody_tickets WHERE scope LIKE ? ESCAPE '!' AND status IN ('prepared','writing')",[$pattern])->fetch())throw new MarketError(409,'A save operation is in progress. Retry when the companion finishes it.');
        if($state['phase']!=='draining'){
            $state['reset_id']=self::id();$state['generation']++;
            $this->run("UPDATE market_control SET phase='draining',generation=?,reset_id=?,updated=? WHERE community=?",[$state['generation'],$state['reset_id'],time(),$this->community]);
            $this->run("UPDATE listings SET status='withdrawn',revision=revision+1 WHERE item IN (SELECT id FROM items WHERE scope LIKE ? ESCAPE '!') AND status IN ('open','agreed')",[$pattern]);
            $this->run("UPDATE offers SET status='withdrawn',revision=revision+1 WHERE listing IN (SELECT id FROM listings WHERE item IN (SELECT id FROM items WHERE scope LIKE ? ESCAPE '!')) AND status IN ('pending','accepted')",[$pattern]);
            $this->run("UPDATE market_preferences SET auto_trade=0 WHERE scope LIKE ? ESCAPE '!'",[$pattern]);
            $this->run("UPDATE web_intents SET status='failed',message='Cancelled by marketplace reset.' WHERE scope LIKE ? ESCAPE '!' AND status='queued'",[$pattern]);
            foreach($this->run("SELECT owner,scope,settings FROM web_profiles WHERE scope LIKE ? ESCAPE '!'",[$pattern])->fetchAll() as $p){$settings=json_decode($p['settings'],true,512,JSON_THROW_ON_ERROR);if($settings===null)continue;$settings['enabled']=$settings['auto_trade']=false;$this->enqueueResetIntent($p['owner'],$p['scope'],'setup',$settings,self::id());}
        }
        foreach($this->run("SELECT item,metadata FROM escrow_items WHERE scope LIKE ? ESCAPE '!' AND state IN ('available','listed')",[$pattern])->fetchAll() as $e){$meta=json_decode($e['metadata'],true,512,JSON_THROW_ON_ERROR);$meta['price']=[];$meta['delivery_requested']=true;$this->run("UPDATE escrow_items SET state='available',metadata=? WHERE item=?",[self::json($meta),$e['item']]);}
        foreach($this->run("SELECT * FROM escrow_balances WHERE scope LIKE ? ESCAPE '!' AND quantity>0",[$pattern])->fetchAll() as $b){
            $market=new self($this->db,$this->community,explode('|',$b['scope'])[1]);
            $queued=(int)$this->run("SELECT COUNT(*) FROM wallet_withdrawals WHERE owner=? AND scope=? AND status IN ('queued','delivering')",[$b['owner'],$b['scope']])->fetchColumn();
            $remaining=(int)$b['quantity'];
            while($remaining>0&&$queued++<32){$q=min($remaining,2000000000);$remaining-=$q;$r=['kind'=>$b['kind'],'code'=>$b['kind']==='gold'?'gld':'Z01','label'=>$b['kind']==='gold'?'Gold':$b['stat'].' (bag)','quality'=>2,'index'=>-1,'ethereal'=>-1,'quantity'=>$q];if($b['kind']==='bag')$r['stat']=$b['stat'];
                $id=self::id();$market->balance($b['owner'],$b['scope'],$r,-$q,'withdrawal_reserved',$id);
                $this->run('INSERT INTO wallet_withdrawals VALUES (?,?,?,?,?,?,?,?,?,?,?)',[$id,$b['owner'],$b['scope'],'queued',1,self::json([$r]),self::json([$r]),'','Marketplace reset: returning held funds.',time(),time()]);
            }
        }
        return $this->ownerStatus($actor);
    }
    private function finishClear(array $actor,array $data,string $directory): array {
        $status=$this->ownerStatus($actor);$control=$status['control'];
        if(($data['confirm']??'')!=='CLEAR MARKETPLACE'||($data['reset_id']??'')!==$control['reset_id']||$control['phase']!=='draining')throw new MarketError(409,'Review the current reset and type CLEAR MARKETPLACE.');
        if(!$status['ready_to_clear'])throw new MarketError(409,'Vault returns or save operations are still outstanding. No data was purged.');
        $pattern=$this->communityPattern();$backup=['schema'=>1,'community'=>$this->community,'created'=>time(),'tables'=>[]];
        foreach($this->resetSelections() as $table=>$where)$backup['tables'][$table]=$this->run("SELECT * FROM $table WHERE $where",[$pattern])->fetchAll();
        if(!is_dir($directory)&&!mkdir($directory,0700,true))throw new RuntimeException('Cannot create the private reset archive.');
        $archive='reset-'.$control['reset_id'].'-'.bin2hex(random_bytes(4));$path=$directory.'/'.$archive.'.json.gz';$bytes=gzencode(self::json($backup),6);
        $stream=fopen($path,'xb');if(!$stream)throw new RuntimeException('Cannot create the private reset archive.');chmod($path,0600);
        try{if(fwrite($stream,$bytes)!==strlen($bytes)||!fflush($stream)||!fsync($stream))throw new RuntimeException('Cannot save the reset archive.');}finally{fclose($stream);}
        foreach($this->resetSelections() as $table=>$where)$this->run("DELETE FROM $table WHERE $where",[$pattern]);
        // Retire old explicitly designated service test traders once no holdings
        // or other-community profile/messages refer to them. Real players stay.
        $testers=$this->run("SELECT id FROM actors a WHERE role='tester' AND NOT EXISTS(SELECT 1 FROM items i WHERE i.owner=a.id) AND NOT EXISTS(SELECT 1 FROM market_messages m WHERE m.sender=a.id OR m.recipient=a.id) AND NOT EXISTS(SELECT 1 FROM market_characters c WHERE c.owner=a.id) AND NOT EXISTS(SELECT 1 FROM custody_agents_scoped g WHERE g.owner=a.id)")->fetchAll(PDO::FETCH_COLUMN);
        foreach($testers as $id){foreach(['market_memberships','market_players','market_names','market_message_reads','market_message_seen'] as $table)$this->run("DELETE FROM $table WHERE owner=?",[$id]);$this->run('DELETE FROM actors WHERE id=?',[$id]);}
        // Delayed clients must clear old drafts/outboxes before they may publish
        // again. Real names, pairing tokens, accounts and capabilities stay put.
        foreach($this->run("SELECT owner,`mod` FROM market_memberships WHERE game_mode='2.4'")->fetchAll() as $p)foreach(['SC','HC'] as $mode){
            $scope=$this->community.'|'.$p['mod'].'|2.4|'.$mode;$id=self::id();
            $this->run('DELETE FROM market_client_resets WHERE owner=? AND scope=?',[$p['owner'],$scope]);
            $this->run('INSERT INTO market_client_resets VALUES (?,?,?,?,0)',[$p['owner'],$scope,$id,$control['generation']]);
            $this->enqueueResetIntent($p['owner'],$scope,'reset',['generation'=>$control['generation']],$id);
        }
        $this->run("UPDATE market_control SET phase='live',updated=? WHERE community=?",[time(),$this->community]);
        return ['cleared'=>true,'archive'=>$archive,'generation'=>$control['generation'],'kept'=>'Real player accounts, names, link codes, companion pairings and owner/moderator permissions.'];
    }
    private function cancelClear(array $data): array {
        $s=$this->controlState();if($s['phase']!=='draining'||($data['reset_id']??'')!==$s['reset_id'])throw new MarketError(409,'The reset changed. Refresh owner tools.');
        $this->run("UPDATE market_control SET phase='live',updated=? WHERE community=?",[time(),$this->community]);return ['resumed'=>true];
    }
    public function ownerCommand(array $actor,array $data,string $directory,?callable $special=null): array {
        return $this->controlledRequest($actor,'owner',$data,false,function()use($actor,$data,$directory,$special){
            $id=self::ticketId($data['id']??null);$hash=hash('sha256',self::json($data));
            $old=$this->run('SELECT hash,result FROM market_owner_actions WHERE id=? AND owner=?',[$id,$actor['id']])->fetch();
            if($old){if(!hash_equals($old['hash'],$hash))throw new MarketError(409,'Owner action ID reused.');return json_decode($old['result'],true,512,JSON_THROW_ON_ERROR);}
            $kind=$data['type']??'';$result=match($kind){'clear_prepare'=>$this->beginClear($actor),'clear_finish'=>$this->finishClear($actor,$data,$directory),'clear_cancel'=>$this->cancelClear($data),default=>$special&&in_array($kind,['demo_generate','demo_clear'],true)?$special():throw new MarketError(422,'Unknown owner control.')};
            $this->run('INSERT INTO market_owner_actions VALUES (?,?,?,?,?,?)',[$id,$actor['id'],$kind,$hash,self::json($result),time()]);
            return $result;
        });
    }
}
