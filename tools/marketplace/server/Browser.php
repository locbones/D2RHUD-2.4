<?php
declare(strict_types=1);

// Browser and game identities are the same actor. Only the paired save agent
// can apply queued changes to local settings, under the existing game lease.
trait MarketplaceBrowser {
    private function browserSchema(string $engine): void {
        foreach([
            'web_tickets'=>'hash CHAR(64) PRIMARY KEY, owner VARCHAR(64) NOT NULL, expires BIGINT NOT NULL',
            'web_item_display'=>'item CHAR(32) PRIMARY KEY, metadata MEDIUMTEXT NOT NULL',
            'web_attempts'=>'identity CHAR(64) PRIMARY KEY, started BIGINT NOT NULL, attempts INT NOT NULL',
            'web_profiles'=>'owner VARCHAR(64) NOT NULL, scope VARCHAR(255) NOT NULL, revision INT NOT NULL, settings TEXT NOT NULL, inventory MEDIUMTEXT NOT NULL, seen BIGINT NOT NULL, PRIMARY KEY(owner,scope)',
            'web_intents'=>($engine?'seq BIGINT AUTO_INCREMENT PRIMARY KEY':'seq INTEGER PRIMARY KEY AUTOINCREMENT').', id CHAR(32) NOT NULL UNIQUE, owner VARCHAR(64) NOT NULL, scope VARCHAR(255) NOT NULL, kind VARCHAR(16) NOT NULL, packet TEXT NOT NULL, status VARCHAR(16) NOT NULL, message TEXT NOT NULL, created BIGINT NOT NULL',
        ] as $name=>$columns)$this->db->exec("CREATE TABLE IF NOT EXISTS $name ($columns)$engine");
    }
    public function browserTicket(array $actor,array $context=[]): array {
        // Linking an already-enrolled player is account access, not save setup.
        // Require an existing membership; never register a mod/catalog here.
        if(($context['existing_profile']??false)===true){
            if(count(array_diff(array_keys($context),['existing_profile','mod','game_mode']))||!is_string($context['mod']??null)||($context['game_mode']??'')!=='2.4')throw new MarketError(422,'Invalid existing player link.');
            $selected=$this->selectMod($context['mod']??'', '2.4');$selected->requireMod($actor['id']);
            if(!$this->run("SELECT id FROM custody_agents_scoped WHERE owner=? AND `mod`=? AND game_mode='2.4'",[$actor['id'],$selected->mod])->fetchColumn())throw new MarketError(403,'Connect this player to the selected mod first.');
            return $selected->browserTicket($actor);
        }
        if($context)return $this->transaction(fn()=>$this->registerLinkContext($actor,$context))->browserTicket($actor);
        $this->requireMod($actor['id']);
        return $this->transaction(function()use($actor){
            $this->run('SELECT id FROM actors WHERE id=?'.$this->lock(),[$actor['id']])->fetch();
            return $this->issueBrowserTicket($actor);
        });
    }
    private function issueBrowserTicket(array $actor): array {
        if(!in_array($actor['role'],['player','tester'],true))throw new MarketError(403,'Use a player account.');
        $alphabet='ABCDEFGHJKLMNPQRSTUVWXYZ23456789';$plain='';for($i=0;$i<12;$i++)$plain.=$alphabet[random_int(0,31)];
        $this->run('DELETE FROM web_tickets WHERE expires<?',[time()]);
        $this->run('DELETE FROM market_ticket_scopes WHERE hash NOT IN (SELECT hash FROM web_tickets)');
        $old=$this->run('SELECT hash FROM web_tickets WHERE owner=? ORDER BY expires DESC',[$actor['id']])->fetchAll(PDO::FETCH_COLUMN);
        foreach(array_slice($old,15) as $hash){$this->run('DELETE FROM web_tickets WHERE hash=?',[$hash]);$this->run('DELETE FROM market_ticket_scopes WHERE hash=?',[$hash]);}
        $expires=time()+300;$hash=hash('sha256',$plain);$this->run('INSERT INTO web_tickets VALUES (?,?,?)',[$hash,$actor['id'],$expires]);
        $this->run('INSERT INTO market_ticket_scopes VALUES (?,?,?)',[$hash,$this->mod,'2.4']);
        return ['code'=>implode('-',str_split($plain,4)),'expires_in'=>300,'expires_at'=>$expires,'mod'=>$this->mod,'game_mode'=>'2.4'];
    }
    private static function browserCode(mixed $code): string {
        if(!is_string($code)||strlen($code)>128)throw new MarketError(401,'That link code is invalid or expired.');
        $plain=preg_replace('/[\s-]+/','',strtoupper($code));
        if(preg_match('/^[A-HJ-NP-Z2-9]{12}$/D',$plain))return $plain;
        // Existing single-use deep links remain usable during the rollout.
        if(preg_match('/^[A-F0-9]{48}$/D',$plain))return strtolower($plain);
        throw new MarketError(401,'That link code is invalid or expired.');
    }
    public function browserFolderLink(array $agent,array $data): array {
        $this->requireAgentMod($agent);
        if($this->mod!=='RMD-MP'&&isset($data['identity_catalog']))$this->transaction(fn()=>$this->registerLinkContext($this->browserActor($agent['owner']),$data+['mod'=>$this->mod,'game_mode'=>'2.4']));
        return $this->transaction(function()use($agent,$data){
            $actor=$this->browserActor($agent['owner']);
            $this->run('SELECT id FROM actors WHERE id=?'.$this->lock(),[$actor['id']])->fetch();
            if(($data['catalog']??null)!==$agent['catalog']||!$this->run('SELECT id FROM custody_agents_scoped WHERE id=? AND owner=?',[$agent['id'],$actor['id']])->fetch())throw new MarketError(401,'The companion pairing changed.');
            if(isset($data['code'])){
                try{$plain=self::browserCode($data['code']);}catch(MarketError){$plain='';}
                $expires=$this->run("SELECT t.expires FROM web_tickets t LEFT JOIN market_ticket_scopes s ON s.hash=t.hash WHERE t.hash=? AND t.owner=? AND COALESCE(s.`mod`,'RMD-MP')=? AND COALESCE(s.game_mode,'2.4')='2.4'",[hash('sha256',$plain),$actor['id'],$this->mod])->fetchColumn();
                if($expires&&(int)$expires>time()+30)return ['valid'=>true,'expires_at'=>(int)$expires];
            }
            return $this->issueBrowserTicket($actor)+['valid'=>false,'player_name'=>$actor['name']];
        });
    }
    public function browserThrottle(string $identity): void {
        $this->transaction(function()use($identity){
            $key=hash('sha256',$identity);$insert=$this->db->getAttribute(PDO::ATTR_DRIVER_NAME)==='mysql'?'INSERT IGNORE':'INSERT OR IGNORE';
            $this->run("$insert INTO web_attempts VALUES (?,?,0)",[$key,time()]);
            $r=$this->run('SELECT * FROM web_attempts WHERE identity=?'.$this->lock(),[$key])->fetch();
            if((int)$r['started']<time()-900){$r=['started'=>time(),'attempts'=>0];}
            if((int)$r['attempts']>=30)throw new MarketError(429,'Too many linking attempts. Try again in 15 minutes.');
            $this->run('UPDATE web_attempts SET started=?,attempts=? WHERE identity=?',[(int)$r['started'],(int)$r['attempts']+1,$key]);
            $this->run('DELETE FROM web_attempts WHERE started<?',[time()-86400]);
        });
    }
    public function browserLink(array $data): array {
        return $this->transaction(function()use($data){
            $code=self::browserCode($data['code']??null);
            $hash=hash('sha256',$code);$row=$this->run('SELECT * FROM web_tickets WHERE hash=?'.$this->lock(),[$hash])->fetch();
            if(!$row||(int)$row['expires']<time())throw new MarketError(401,'That link code is invalid or expired. Get a new code from Pandemonium.');
            $context=$this->run('SELECT `mod`,game_mode FROM market_ticket_scopes WHERE hash=?',[$hash])->fetch()?:['mod'=>'RMD-MP','game_mode'=>'2.4'];
            $selected=$this->selectMod($context['mod'],$context['game_mode']);$selected->linkedPlayer($row['owner']);
            $this->run('DELETE FROM web_tickets WHERE hash=?',[$hash]);$this->run('DELETE FROM market_ticket_scopes WHERE hash=?',[$hash]);return $this->browserActor($row['owner'])+$context;
        });
    }
    public function browserActor(string $id): array {
        $a=$this->run("SELECT id,name,role FROM actors WHERE id=? AND role IN ('player','tester')",[$id])->fetch();
        if(!$a)throw new MarketError(401,'Link your player again.');return $a;
    }
    public function browserSessionStamp(array $actor): string {
        return hash('sha256',$this->run('SELECT token_hash FROM actors WHERE id=?',[$actor['id']])->fetchColumn());
    }
    private function displayItem(array $r,int $depth=0): array {
        // Display metadata is informational; exact custody bytes remain authority.
        $source=$r['display']??$r;$out=[];
        foreach(['level','picture','flags'] as $k)if(isset($source[$k]))$out[$k]=self::number($source[$k],$k==='picture'?-1:0,4294967295);
        $props=$source['saved_properties']??[];if(!is_array($props)||!array_is_list($props)||count($props)>512)throw new MarketError(422,'Invalid item properties.');
        $out['saved_properties']=[];
        foreach($props as $p){$out['saved_properties'][]=['id'=>self::number($p['id']??null,0,4095),'param'=>self::number($p['param']??0,0,4294967295),'value'=>self::number($p['value']??null,-4294967295,4294967295),'list'=>self::text($p['list']??'base',32)];}
        $children=$source['socketed_items']??[];if(!is_array($children)||!array_is_list($children)||count($children)>6||($depth>=6&&$children))throw new MarketError(422,'Invalid socketed items.');
        $out['socketed_items']=[];foreach($children as $child){
            $clean=['code'=>self::text($child['code']??null,4),'quality'=>self::number($child['quality']??2,1,9),'index'=>self::number($child['index']??-1,-1,1000000),'display'=>$this->displayItem($child,$depth+1)];
            if(isset($child['identified']))$clean['identified']=is_bool($child['identified'])?$child['identified']:(bool)self::number($child['identified'],0,1);
            elseif(isset($clean['display']['flags']))$clean['identified']=(bool)($clean['display']['flags']&(1<<4));
            // Resolve jewel names from the saved prefix, never expose its bytes.
            if(isset($child['item_hex'])&&is_string($child['item_hex'])&&strlen($child['item_hex'])<=65536)$clean=$this->browserItemNames($clean,$child['item_hex']);
            // Preserve sanitized names when already-clean metadata is reprocessed.
            elseif(isset($child['name_ids'])&&($clean['identified']??true))foreach(['magic_prefix','magic_suffix','rare_prefix','rare_suffix'] as $key)if(isset($child['name_ids'][$key]))$clean['name_ids'][$key]=self::number($child['name_ids'][$key],0,2047);
            $out['socketed_items'][]=$clean;
        }return $out;
    }
    private function routeSettings(array $r): array {
        $page=self::number($r['page']??null,1,64);
        if(!is_bool($r['enabled']??null)||!is_bool($r['auto_trade']??null))throw new MarketError(422,'Choose a valid market setup.');
        // Older companions retain their routes until upgraded; web setup always uses one page.
        $delivery=isset($r['withdraw_page'])?self::number($r['withdraw_page'],1,64):$page;
        return ['page'=>$page,'withdraw_page'=>$delivery,'enabled'=>$r['enabled'],'auto_trade'=>$r['auto_trade']];
    }
    public function browserAgentSync(array $agent,array $data,bool $hc): array {
        $this->requireAgentMod($agent);
        return $this->transaction(function()use($agent,$data,$hc){
            $this->lockScope($hc);$owner=$agent['owner'];$scope=$this->scope($hc);
            if(($data['catalog']??null)!==$agent['catalog']||!$this->run('SELECT id FROM custody_agents_scoped WHERE id=? AND owner=?',[$agent['id'],$owner])->fetch())throw new MarketError(401,'The save-agent pairing changed.');
            $this->touchCompanion($owner,$scope);
            if(isset($data['companion_version'])){$version=self::text($data['companion_version'],32);if(!preg_match('/^[0-9]+\.[0-9]+\.[0-9]+$/D',$version))throw new MarketError(422,'Invalid companion version.');$this->run('DELETE FROM market_companion_versions WHERE owner=? AND scope=?',[$owner,$scope]);$this->run('INSERT INTO market_companion_versions VALUES (?,?,?,?)',[$owner,$scope,$version,time()]);}
            if(isset($data['features'])){if(!is_array($data['features'])||!array_is_list($data['features'])||count($data['features'])>16)throw new MarketError(422,'Invalid companion capabilities.');$features=array_values(array_intersect($data['features'],['open_offers','torch_class','listing_comments','gift_resources']));$this->run('DELETE FROM market_browser_features WHERE owner=? AND scope=?',[$owner,$scope]);$this->run('INSERT INTO market_browser_features VALUES (?,?,?)',[$owner,$scope,self::json($features)]);}
            $settings=$this->routeSettings($data['settings']??[]);$inventory=$data['inventory']??[];
            if(!is_array($inventory)||!array_is_list($inventory)||count($inventory)>2048)throw new MarketError(422,'Invalid private inventory.');
            $items=[];foreach($inventory as $r){$row=$this->custodyItem($r);$key=hash('sha256',$row['page'].'|'.$row['tab'].'|'.$row['item_hex']);$row['key']=$key;$items[]=$row;}
            $acks=$data['acknowledged']??[];if(!is_array($acks)||!array_is_list($acks)||count($acks)>128)throw new MarketError(422,'Invalid setting acknowledgements.');
            foreach($acks as $a){$id=self::ticketId($a['id']??null);$status=$a['status']??'';if(!in_array($status,['applied','failed'],true))throw new MarketError(422,'Invalid setting result.');$message=$a['message']??'';if($message!=='')self::text($message,512);$this->run("UPDATE web_intents SET status=?,message=? WHERE id=? AND owner=? AND scope=? AND status='queued'",[$status,$message,$id,$owner,$scope]);if($status==='applied')$this->run('UPDATE market_client_resets SET acknowledged=1 WHERE owner=? AND scope=? AND id=?',[$owner,$scope,$id]);}
            $old=$this->run('SELECT * FROM web_profiles WHERE owner=? AND scope=?',[$owner,$scope])->fetch();$encoded=self::json($settings);$rev=$old?(int)$old['revision']+(int)($old['settings']!==$encoded):1;
            if($old)$this->run('UPDATE web_profiles SET revision=?,settings=?,inventory=?,seen=? WHERE owner=? AND scope=?',[$rev,$encoded,self::json($items),time(),$owner,$scope]);
            else $this->run('INSERT INTO web_profiles VALUES (?,?,?,?,?,?)',[$owner,$scope,$rev,$encoded,self::json($items),time()]);
            $intents=$this->run("SELECT id,kind,packet FROM web_intents WHERE owner=? AND scope=? AND status='queued' ORDER BY seq LIMIT 128",[$owner,$scope])->fetchAll();foreach($intents as &$i)$i['packet']=json_decode($i['packet'],true,512,JSON_THROW_ON_ERROR);unset($i);
            return ['intents'=>$intents,'delivery_items'=>$this->requestedDeliveries($owner,$scope),'wallet_withdrawals'=>$this->walletRows($owner,$scope,true)];
        });
    }
    private function browserCommand(array $actor,array $data,bool $hc): array {
        return $this->transaction(function()use($actor,$data,$hc){
            $this->lockScope($hc);$id=self::ticketId($data['id']??null);$owner=$actor['id'];$scope=$this->scope($hc);$hash=hash('sha256',$scope.'|'.self::json($data));
            $old=$this->run('SELECT hash,result FROM commands WHERE actor=? AND id=?',[$owner,$id])->fetch();if($old){if(!hash_equals($old['hash'],$hash))throw new MarketError(409,'Command ID reused.');return json_decode($old['result'],true,512,JSON_THROW_ON_ERROR);}
            if((int)$this->run("SELECT COUNT(*) FROM web_intents WHERE owner=? AND scope=? AND status='queued'",[$owner,$scope])->fetchColumn()>=128)throw new MarketError(409,'Start your marketplace companion to apply pending changes first.');
            $profile=$this->run('SELECT * FROM web_profiles WHERE owner=? AND scope=?',[$owner,$scope])->fetch();
            if(($data['type']??'')==='web_setup'){
                if(self::number($data['expected_revision']??null,0,2147483647)!==($profile?(int)$profile['revision']:0))throw new MarketError(409,'Setup changed. Refresh and review it again.');
                $packet=$this->routeSettings(array_merge($data,['withdraw_page'=>$data['page']??null]));$kind='setup';
                if($profile)$this->run('UPDATE web_profiles SET revision=revision+1 WHERE owner=? AND scope=?',[$owner,$scope]);
                // A linked player can choose routes before their first stash sync.
                // Null settings / seen=0 distinguish this receipt from applied PC settings.
                else $this->run('INSERT INTO web_profiles VALUES (?,?,?,?,?,?)',[$owner,$scope,1,'null','[]',0]);
                $this->run('DELETE FROM market_preferences WHERE owner=? AND scope=?',[$owner,$scope]);$this->run('INSERT INTO market_preferences VALUES (?,?,?)',[$owner,$scope,(int)($packet['enabled']&&$packet['auto_trade'])]);
                $this->settleReady($hc);
            }elseif(($data['type']??'')==='web_price'){
                if(!$profile)throw new MarketError(409,'Keep Pandemonium running to sync your stash before pricing an item.');
                $key=self::digest($data['key']??null);$found=null;foreach(json_decode($profile['inventory'],true,512,JSON_THROW_ON_ERROR) as $r)if($r['key']===$key)$found=$r;
                if(!$found||$this->run('SELECT source_key FROM custody_sources WHERE owner=? AND scope=? AND source_key=?',[$owner,$scope,$key])->fetch())throw new MarketError(409,'This stash item changed or entered Vault. Refresh and choose it again.');
                $this->identityAdmission($owner,$scope,$found['item_hex'],$found['ledger_id']??null,null,true);
                $price=($data['price']??null)===[]?[]:self::price($data['price']??null);if(self::hasClassPrice($price)&&!in_array('torch_class',json_decode($this->run('SELECT features FROM market_browser_features WHERE owner=? AND scope=?',[$owner,$scope])->fetchColumn()?:'[]',true),true))throw new MarketError(409,'Update the HUD and marketplace companion before requesting a Torch class on a stash item. Vault items support it now.');if(self::isOpen($price)&&!in_array('open_offers',json_decode($this->run('SELECT features FROM market_browser_features WHERE owner=? AND scope=?',[$owner,$scope])->fetchColumn()?:'[]',true),true))throw new MarketError(409,'Update Pandemonium before listing a stash item as Open to Offers. Existing Vault items support it now.');
                $packet=['key'=>$key,'page'=>$found['page'],'tab'=>$found['tab'],'item_hex'=>$found['item_hex'],'price'=>$price];if(array_key_exists('comment',$data)){$packet['comment']=self::listingComment($data['comment']);if($packet['comment']!==''&&!in_array('listing_comments',json_decode($this->run('SELECT features FROM market_browser_features WHERE owner=? AND scope=?',[$owner,$scope])->fetchColumn()?:'[]',true),true))throw new MarketError(409,'Update your HUD and marketplace companion before adding a comment to a stash listing. Vault listings support comments now.');}$kind='price';
            }else throw new MarketError(422,'Unknown website command.');
            $this->run('INSERT INTO web_intents (id,owner,scope,kind,packet,status,message,created) VALUES (?,?,?,?,?,?,?,?)',[$id,$owner,$scope,$kind,self::json($packet),'queued','',time()]);
            $result=['queued'=>true,'id'=>$id];$this->run('INSERT INTO commands VALUES (?,?,?,?)',[$owner,$id,$hash,self::json($result)]);return $result;
        });
    }
    private function browserItemNames(array $item,string $hex): array {
        // Resolve the existing saved name IDs at read time, including old
        // escrow/discovery rows. Never send serialized custody bytes to browsers.
        if($native=$this->itemIdentity($hex)){
            foreach(['code','quality','index','identified'] as $key)$item[$key]=$native[$key];
            $item['name_ids']=$native['identified']?$native['name_ids']:[];
        }
        return $item;
    }
    public function browserSnapshot(?array $actor,bool $hc): array {
        $guest=['id'=>'','name'=>'Guest'];$snapshot=$this->snapshot($actor??$guest,$hc,true);unset($snapshot['publication'],$snapshot['escrow']['lineage']);
        $scope=$this->scope($hc);$all=$this->run('SELECT i.id,i.code,i.quality,i.item_index,i.seed,i.ethereal,i.sockets,i.item_hex,e.metadata,d.metadata AS discovery FROM items i LEFT JOIN escrow_items e ON e.item=i.id LEFT JOIN web_item_display d ON d.item=i.id WHERE i.scope=?',[$scope])->fetchAll();$map=[];
        foreach($all as $i){$metadata=$i['metadata']?json_decode($i['metadata'],true,512,JSON_THROW_ON_ERROR):[];$map[$i['id']]=['code'=>$i['code'],'quality'=>(int)$i['quality'],'index'=>(int)$i['item_index'],'saved_id'=>(int)$i['seed'],'item_uuid'=>$this->itemIdentity($i['item_hex'])['item_uuid']??'','ledger_id'=>$i['id'],'ethereal'=>(bool)$i['ethereal'],'sockets'=>(int)$i['sockets'],'quantity'=>$metadata['quantity']??1,'trade_quantity'=>self::tradeQuantity($metadata),'identified'=>$metadata['identified']??1,'display'=>$metadata['display']??($i['discovery']?json_decode($i['discovery'],true,512,JSON_THROW_ON_ERROR):null)];$map[$i['id']]=$this->browserItemNames($map[$i['id']],$i['item_hex']);}
        $bundles=$this->browserBundles($scope,$map);
        foreach($snapshot['listings'] as &$r){unset($r['item_hex']);$r['item']=$map[$r['item_id']]??null;$r['bundle']=$bundles['listings'][$r['id']]??null;}unset($r);
        foreach($snapshot['offers'] as &$r){unset($r['item_hex']);$r['item_details']=$map[$r['item']]??null;$r['bundle']=$bundles['listings'][$r['listing']]??null;}unset($r);
        foreach($snapshot['escrow']['items'] as &$r){$group=$bundles['items'][$r['item']]??null;$r=['bundle'=>$group?($bundles['listings'][$group]??null):null,'item_id'=>$r['item'],'state'=>$r['state'],'delivery_requested'=>(bool)($r['metadata']['delivery_requested']??false),'item'=>$map[$r['item']]??null,'price'=>$r['metadata']['price']??[],'comment'=>($r['metadata']['comment_owner']??'')===($actor['id']??'')?($r['metadata']['comment']??''):''];}unset($r);
        $snapshot['experience']=$actor?$this->experienceSnapshot($actor,$hc):null;
        $snapshot['wanted']=$this->wantedListings($hc);$snapshot['gifts']=$this->giftCampaigns($actor,$hc);
        $snapshot['companion_version']=$actor?($this->run('SELECT version FROM market_companion_versions WHERE owner=? AND scope=?',[$actor['id'],$scope])->fetchColumn()?:null):null;
        $snapshot['companion_features']=$actor?json_decode($this->run('SELECT features FROM market_browser_features WHERE owner=? AND scope=?',[$actor['id'],$scope])->fetchColumn()?:'[]',true):[];
        $snapshot['self']=$actor?$snapshot['self']:null;
        $snapshot['linked_mod']=$actor&&(bool)$this->run("SELECT owner FROM market_memberships WHERE owner=? AND `mod`=? AND game_mode='2.4' AND linked>0",[$actor['id'],$this->mod])->fetchColumn();
        $snapshot['moderation']=$this->moderationCapabilities($actor);$snapshot['owner_tools']=$this->isOwner($actor);$snapshot['market_control']=$this->controlState();$snapshot['market_reset']=$actor?$this->clientReset($actor['id'],$hc):null;
        $snapshot['profile']=null;$snapshot['local_items']=[];$snapshot['pending']=[];
        if($actor){
            $p=$this->run('SELECT * FROM web_profiles WHERE owner=? AND scope=?',[$actor['id'],$scope])->fetch();
            if($p){$snapshot['profile']=['revision'=>(int)$p['revision'],'settings'=>json_decode($p['settings'],true,512,JSON_THROW_ON_ERROR),'seen'=>(int)$p['seen']];foreach(json_decode($p['inventory'],true,512,JSON_THROW_ON_ERROR) as $r){if(!$this->run('SELECT source_key FROM custody_sources WHERE owner=? AND scope=? AND source_key=?',[$actor['id'],$scope,$r['key']])->fetch()){$r=$this->browserItemNames($r,$r['item_hex']);unset($r['item_hex'],$r['quantity_bit']);$snapshot['local_items'][]=$r;}}}
            $queued=$this->run("SELECT kind,packet FROM web_intents WHERE owner=? AND scope=? AND status='queued' ORDER BY seq LIMIT 128",[$actor['id'],$scope])->fetchAll();
            foreach($queued as $intent){$packet=json_decode($intent['packet'],true,512,JSON_THROW_ON_ERROR);
                if($intent['kind']==='setup'&&$snapshot['profile'])$snapshot['profile']['desired']=$packet;
                if($intent['kind']==='price')foreach($snapshot['local_items'] as &$local)if($local['key']===$packet['key']){$local['price']=$packet['price'];if(array_key_exists('comment',$packet))$local['comment']=$packet['comment'];$local['price_pending']=true;}unset($local);
            }
            $snapshot['pending']=$this->run('SELECT id,kind,status,message,created FROM web_intents WHERE owner=? AND scope=? ORDER BY seq DESC LIMIT 32',[$actor['id'],$scope])->fetchAll();
        }
        return $snapshot;
    }
}
