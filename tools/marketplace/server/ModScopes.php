<?php
declare(strict_types=1);

trait MarketplaceModScopes {
    private function modSchema(string $engine): void {
        foreach([
            'market_mods'=>'`mod` VARCHAR(64) PRIMARY KEY, catalog CHAR(64) NOT NULL, identity_data MEDIUMTEXT NOT NULL, created BIGINT NOT NULL',
            'market_memberships'=>'owner VARCHAR(64) NOT NULL, `mod` VARCHAR(64) NOT NULL, game_mode VARCHAR(16) NOT NULL, linked BIGINT NOT NULL, PRIMARY KEY(owner,`mod`,game_mode)',
            'market_ticket_scopes'=>'hash CHAR(64) PRIMARY KEY, `mod` VARCHAR(64) NOT NULL, game_mode VARCHAR(16) NOT NULL',
            'market_scope_migrations'=>'name VARCHAR(64) PRIMARY KEY',
            'custody_agents_scoped'=>'id CHAR(32) PRIMARY KEY, owner VARCHAR(64) NOT NULL, token_hash CHAR(64) NOT NULL UNIQUE, catalog CHAR(64) NOT NULL, `mod` VARCHAR(64) NOT NULL, game_mode VARCHAR(16) NOT NULL, UNIQUE(owner,`mod`,game_mode)',
        ] as $name=>$columns)$this->db->exec("CREATE TABLE IF NOT EXISTS $name ($columns)$engine");
        // Previous releases supported only RMD-MP 2.4. Keep those exact IDs,
        // tokens and save tickets; the legacy table remains as a rollback copy.
        if(!$this->run("SELECT name FROM market_scope_migrations WHERE name='mod-scopes-v1'")->fetchColumn()){
            $insert=$this->db->getAttribute(PDO::ATTR_DRIVER_NAME)==='mysql'?'INSERT IGNORE':'INSERT OR IGNORE';
            $this->run("$insert INTO custody_agents_scoped SELECT id,owner,token_hash,catalog,'RMD-MP','2.4' FROM custody_agents");
            $this->run("$insert INTO market_memberships SELECT id,'RMD-MP','2.4',0 FROM actors WHERE role IN ('player','tester')");
            $this->run("UPDATE market_memberships SET linked=(SELECT linked FROM market_players WHERE owner=market_memberships.owner) WHERE `mod`='RMD-MP' AND owner IN (SELECT owner FROM market_players)");
            $this->run("INSERT INTO market_scope_migrations VALUES ('mod-scopes-v1')");
        }
        $catalogs=$this->run("SELECT DISTINCT catalog FROM custody_agents_scoped WHERE `mod`='RMD-MP'")->fetchAll(PDO::FETCH_COLUMN);
        if(count($catalogs)>1)throw new MarketError(409,'Legacy RMD-MP pairings have conflicting item catalogs. Resolve them before enabling mod registration.');
        $this->ensureMod('RMD-MP',$catalogs[0]??'');
    }
    public static function modName(mixed $name): string {
        $name=self::text($name,64);if(!preg_match('/^[A-Za-z0-9][A-Za-z0-9._ -]{0,63}$/D',$name)||str_contains($name,'|')||trim($name)!==$name)throw new MarketError(422,'Invalid mod name.');return $name;
    }
    private function ensureMod(string $name,string $catalog='',?array $identity=null): void {
        if($this->db->inTransaction()){
            $sql=$this->db->getAttribute(PDO::ATTR_DRIVER_NAME)==='mysql'?'INSERT INTO custody_serial VALUES (?,0) ON DUPLICATE KEY UPDATE version=version':'INSERT OR IGNORE INTO custody_serial VALUES (?,0)';
            $this->run($sql,[$this->community.'|mod-registry']);$this->run('SELECT version FROM custody_serial WHERE scope=?'.$this->lock(),[$this->community.'|mod-registry'])->fetch();
        }
        $name=self::modName($name);$old=$this->run('SELECT `mod`,catalog,identity_data FROM market_mods WHERE LOWER(`mod`)=LOWER(?)',[$name])->fetch();
        if($old){if($old['mod']!==$name)throw new MarketError(409,'Use the registered mod name: '.$old['mod']);
            if($catalog!==''&&$old['catalog']!==''&&!hash_equals($old['catalog'],$catalog))throw new MarketError(409,'This mod uses a different verified item catalog. No inventory can be transferred between those catalogs.');
            if($catalog!==''&&$old['catalog']==='')$this->run('UPDATE market_mods SET catalog=? WHERE `mod`=?',[$catalog,$name]);
            if($identity!==null){$encoded=self::json($identity);if($old['identity_data']!==''&&!hash_equals(hash('sha256',$old['identity_data']),hash('sha256',$encoded)))throw new MarketError(409,'The registered mod item definitions differ.');$this->run('UPDATE market_mods SET identity_data=? WHERE `mod`=?',[$encoded,$name]);}
        }else $this->run('INSERT INTO market_mods VALUES (?,?,?,?)',[$name,$catalog,$identity===null?'':self::json($identity),time()]);
    }
    private function joinMod(string $owner,bool $linked=false): void {
        $insert=$this->db->getAttribute(PDO::ATTR_DRIVER_NAME)==='mysql'?'INSERT IGNORE':'INSERT OR IGNORE';
        $this->run("$insert INTO market_memberships VALUES (?,?,?,?)",[$owner,$this->mod,'2.4',$linked?time():0]);
        if($linked)$this->run('UPDATE market_memberships SET linked=? WHERE owner=? AND `mod`=? AND game_mode=?',[time(),$owner,$this->mod,'2.4']);
    }
    private function requireMod(string $owner): void {
        if(!$this->run("SELECT owner FROM market_memberships WHERE owner=? AND `mod`=? AND game_mode='2.4'",[$owner,$this->mod])->fetchColumn())throw new MarketError(403,'Link this player to the selected mod before trading. Cross-mod transfers are not allowed.');
    }
    private function requireAgentMod(array $agent): void {
        if(($agent['mod']??'')!==$this->mod||($agent['game_mode']??'')!=='2.4')throw new MarketError(403,'The save companion belongs to another mod or game version.');
    }
    public function selectMod(string $name,string $version='2.4'): self {
        self::modName($name);if($version!=='2.4')throw new MarketError(409,'The 3.x marketplace is coming soon.');
        if(!$this->run('SELECT `mod` FROM market_mods WHERE `mod`=?',[$name])->fetchColumn())throw new MarketError(404,'This mod has not been linked from Pandemonium yet.');
        return new self($this->db,$this->community,$name);
    }
    public function modDirectory(?array $actor): array {
        $rows=$this->run("SELECT m.`mod` FROM market_mods m WHERE m.`mod`='RMD-MP' OR EXISTS(SELECT 1 FROM market_memberships p WHERE p.`mod`=m.`mod` AND p.linked>0) ORDER BY m.`mod`")->fetchAll();
        foreach($rows as &$r){$r['linked']=$actor&&(bool)$this->run("SELECT owner FROM market_memberships WHERE owner=? AND `mod`=? AND game_mode='2.4' AND linked>0",[$actor['id'],$r['mod']])->fetchColumn();}unset($r);
        return ['mods'=>$rows,'versions'=>[['id'=>'2.4','available'=>true],['id'=>'3.x','available'=>false,'label'=>'Coming soon']]];
    }
    private array $identityDefinitions=[];
    private function itemIdentity(string $hex,?string $scope=null): ?array {
        $name=$scope===null?$this->mod:(explode('|',$scope)[1]??'');
        if($name==='RMD-MP')return SavedItemIdentity::read($hex);
        if(!array_key_exists($name,$this->identityDefinitions)){
            $data=$this->run('SELECT identity_data FROM market_mods WHERE `mod`=?',[$name])->fetchColumn();
            if(!$data)throw new MarketError(409,'Link this mod from Pandemonium to register its item definitions before trading.');
            $this->identityDefinitions[$name]=json_decode($data,true,512,JSON_THROW_ON_ERROR);
        }
        return SavedItemIdentity::read($hex,$this->identityDefinitions[$name]);
    }
    private function identityKey(string $hex,?string $scope=null): string {
        return $this->itemIdentity($hex,$scope)['key']??'bytes:'.hash('sha256',SavedItemIdentity::positionless($hex));
    }
    public function browserCatalog(): array {
        if($this->mod==='RMD-MP')return json_decode(file_get_contents(__DIR__.'/public/assets/catalog.json'),true,512,JSON_THROW_ON_ERROR);
        $data=$this->run('SELECT identity_data FROM market_mods WHERE `mod`=?',[$this->mod])->fetchColumn();
        if(!$data)throw new MarketError(409,'This mod has no verified item catalog yet.');
        $catalog=['bases'=>[],'unique'=>[],'set'=>[],'stats'=>[],'skills'=>[],'names'=>[],'monsters'=>[],'classes'=>[],'tabs'=>[]];
        foreach(json_decode($data,true,512,JSON_THROW_ON_ERROR)['items'] as $code=>$d)$catalog['bases'][rtrim($code)]=['code'=>rtrim($code),'name'=>$d['name']??('Item '.rtrim($code)),'category'=>'misc','image'=>null];
        return $catalog;
    }
    public function registerLinkContext(array $actor,array $context): self {
        $name=self::modName($context['mod']??$this->mod);$version=$context['game_mode']??'2.4';
        if($version!=='2.4')throw new MarketError(409,'The 3.x marketplace is coming soon.');
        $fingerprint=self::digest($context['catalog']??null);
        if(!$this->run("SELECT id FROM custody_agents_scoped WHERE owner=? AND `mod`=? AND game_mode='2.4' AND catalog=?",[$actor['id'],$name,$fingerprint])->fetchColumn())throw new MarketError(403,'The linked save companion does not match this mod and item catalog.');
        $identity=$context['identity_catalog']??null;
        if($identity!==null){if(!is_array($identity)||($identity['schema']??null)!==1||($identity['game_mode']??null)!=='2.4'||!is_array($identity['codes']??null)||count($identity['codes'])!==256||!is_array($identity['items']??null)||count($identity['items'])>65536)throw new MarketError(422,'Invalid mod item definitions.');
            foreach($identity['codes'] as $code)if(!is_string($code)||!preg_match('/^[01]{1,17}$/D',$code))throw new MarketError(422,'Invalid item code dictionary.');
            foreach($identity['items'] as $code=>$d){if(!is_string($code)||strlen($code)!==4||!is_array($d)||!is_int($d['quest_bits']??null)||$d['quest_bits']<0||$d['quest_bits']>32)throw new MarketError(422,'Invalid mod item definition.');foreach(['gold','charm','body','tome'] as $key)if(!is_bool($d[$key]??null))throw new MarketError(422,'Invalid mod item definition.');}
        }
        if($name!=='RMD-MP'&&$identity===null)throw new MarketError(422,'Pandemonium must supply this mod’s verified item definitions.');
        $this->ensureMod($name,$fingerprint,$name==='RMD-MP'?null:$identity);$next=new self($this->db,$this->community,$name);$next->joinMod($actor['id']);return $next;
    }
}
