<?php
declare(strict_types=1);

// Catalog revisions describe saved fields, not whole files or installation paths.
// A mod keeps one compatible family; additions may coexist. Conflicting saved
// definitions never silently replace a family or cross a mod boundary.
trait MarketplaceCompatibility {
    private function compatibilitySchema(string $engine): void {
        foreach([
            'market_catalog_families'=>'`mod` VARCHAR(64) PRIMARY KEY, family CHAR(64) NOT NULL, definition MEDIUMTEXT NOT NULL',
            'market_catalog_versions'=>'`mod` VARCHAR(64) NOT NULL, fingerprint CHAR(64) NOT NULL, family CHAR(64) NOT NULL, definition MEDIUMTEXT NOT NULL, PRIMARY KEY(`mod`,fingerprint)',
            'market_agent_catalogs'=>'agent CHAR(32) PRIMARY KEY, fingerprint CHAR(64) NOT NULL',
        ] as $table=>$columns)$this->db->exec("CREATE TABLE IF NOT EXISTS $table ($columns)$engine");
    }
    public static function saveCatalogHash(array $definition): string {
        $sort=function($value)use(&$sort){if(!is_array($value))return $value;if(!array_is_list($value))ksort($value,SORT_STRING);foreach($value as &$v)$v=$sort($v);unset($v);return $value;};
        $definition=$sort($definition);$definition['bags']=(object)$definition['bags'];
        return hash('sha256',json_encode($definition,JSON_THROW_ON_ERROR|JSON_UNESCAPED_UNICODE|JSON_UNESCAPED_SLASHES));
    }
    private static function validateSaveCatalog(mixed $d): array {
        if(!is_array($d)||array_diff(array_keys($d),['schema','game_mode','reader','codes','items','stats','bags'])||($d['schema']??null)!==2||($d['game_mode']??null)!=='2.4'||($d['reader']??null)!=='d2r-98-v1')throw new MarketError(422,'Unsupported marketplace save-format description. Update Pandemonium.');
        foreach(['codes','items','stats','bags'] as $key)if(!is_array($d[$key]??null))throw new MarketError(422,'Incomplete marketplace save-format description.');
        if(count($d['codes'])!==256||!array_is_list($d['codes'])||count($d['items'])<1||count($d['items'])>100000||count($d['stats'])<1||count($d['stats'])>4096||count($d['bags'])>32)throw new MarketError(422,'Invalid marketplace save-format limits.');
        foreach($d['codes'] as $code)if(!is_string($code)||!preg_match('/^[01]{1,17}$/D',$code))throw new MarketError(422,'Invalid save code dictionary.');
        foreach($d['items'] as $code=>$row){if(!is_string($code)||strlen($code)!==4||!is_array($row)||!array_is_list($row)||count($row)!==10)throw new MarketError(422,'Invalid saved item definition.');foreach(array_slice($row,0,8) as $flag)if(!is_bool($flag))throw new MarketError(422,'Invalid saved item flag.');foreach(array_slice($row,8) as $size)self::number($size,0,255);}
        foreach($d['stats'] as $id=>$row){if(!preg_match('/^s[0-9]{1,4}$/D',(string)$id)||!is_array($row)||!array_is_list($row)||count($row)!==4)throw new MarketError(422,'Invalid saved stat definition.');self::number($row[0],0,32);self::number($row[1],0,32);self::number($row[2],-2147483648,2147483647);self::number($row[3],0,32);}
        foreach($d['bags'] as $name=>$id){if(!is_string($name)||!preg_match('/^[A-Za-z0-9_]{1,64}$/D',$name)||!isset($d['stats']['s'.$id]))throw new MarketError(422,'Invalid saved bag resource.');self::number($id,0,4095);}
        return $d;
    }
    private function mergeSaveCatalog(array $old,array $next): array {
        foreach(['reader','codes'] as $key)if($old[$key]!==$next[$key])throw new MarketError(409,'This mod uses an incompatible item encoding. Install a compatible mod version; game launch remains available.');
        foreach(['items','stats','bags'] as $group)foreach($next[$group] as $key=>$row){
            if(isset($old[$group][$key])&&$old[$group][$key]!==$row)throw new MarketError(409,'This mod changes saved '.$group.' definition '.trim((string)$key).'. These mod versions cannot safely exchange that data. Game launch remains available.');
            $old[$group][$key]=$row;
        }
        return $old;
    }
    // Operator migration of an existing opaque catalog, from its verified files.
    public function seedLegacyCompatibility(string $family,array $definition): array {
        $definition=self::validateSaveCatalog($definition);self::digest($family);
        if($this->run('SELECT catalog FROM market_mods WHERE `mod`=?',[$this->mod])->fetchColumn()!==$family)throw new MarketError(409,'Legacy catalog does not match this mod.');
        $old=$this->run('SELECT family,definition FROM market_catalog_families WHERE `mod`=?',[$this->mod])->fetch();
        if($old){if($old['family']!==$family)throw new MarketError(409,'This mod already has another catalog family.');$this->mergeSaveCatalog(json_decode($old['definition'],true,512,JSON_THROW_ON_ERROR),$definition);return ['family'=>$family];}
        $this->run('INSERT INTO market_catalog_families VALUES (?,?,?)',[$this->mod,$family,self::json($definition)]);
        $this->run('INSERT INTO market_catalog_versions VALUES (?,?,?,?)',[$this->mod,$family,$family,self::json($definition)]);
        return ['family'=>$family];
    }
    private function registerSaveCatalog(string $fingerprint,mixed $definition): string {
        $definition=self::validateSaveCatalog($definition);
        if(!hash_equals(self::saveCatalogHash($definition),$fingerprint))throw new MarketError(422,'Save-format fingerprint does not match its definitions.');
        $this->run('SELECT phase FROM market_control WHERE community=?'.$this->lock(),[$this->community])->fetch();
        $old=$this->run('SELECT family,definition FROM market_catalog_families WHERE `mod`=?'.$this->lock(),[$this->mod])->fetch();
        if(!$old){
            $legacy=$this->run('SELECT catalog FROM market_mods WHERE `mod`=?',[$this->mod])->fetchColumn();
            if($legacy)throw new MarketError(409,'This mod’s legacy marketplace needs its one-time save-format migration. Your player and Vault are preserved.');
            $family=$fingerprint;$merged=$definition;
            $this->run('INSERT INTO market_catalog_families VALUES (?,?,?)',[$this->mod,$family,self::json($merged)]);
        }else{
            $family=$old['family'];$merged=$this->mergeSaveCatalog(json_decode($old['definition'],true,512,JSON_THROW_ON_ERROR),$definition);
            $this->run('UPDATE market_catalog_families SET definition=? WHERE `mod`=?',[self::json($merged),$this->mod]);
        }
        $insert=$this->db->getAttribute(PDO::ATTR_DRIVER_NAME)==='mysql'?'INSERT IGNORE':'INSERT OR IGNORE';
        $this->run("$insert INTO market_catalog_versions VALUES (?,?,?,?)",[$this->mod,$fingerprint,$family,self::json($definition)]);
        $this->ensureMod($this->mod,$family);
        $identity=['schema'=>1,'game_mode'=>'2.4','codes'=>$merged['codes'],'items'=>[]];
        foreach($merged['items'] as $code=>$r)$identity['items'][$code]=['gold'=>$r[3],'quest_bits'=>$r[7]?($merged['stats']['s356'][0]??0):0,'tome'=>$r[4],'body'=>$r[5],'charm'=>$r[6]];
        $this->run('UPDATE market_mods SET identity_data=? WHERE `mod`=?',[self::json($identity),$this->mod]);
        unset($this->identityDefinitions[$this->mod]);
        return $family;
    }
    private function bindAgentCatalog(string $agent,string $fingerprint): void {
        $owner=$this->run('SELECT owner FROM custody_agents_scoped WHERE id=?',[$agent])->fetchColumn();
        if($owner&&$this->run("SELECT id FROM custody_tickets WHERE owner=? AND scope IN (?,?) AND status IN ('prepared','writing')",[$owner,$this->scope(false),$this->scope(true)])->fetchColumn())throw new MarketError(409,'Finish the pending save transfer before refreshing marketplace compatibility.');
        $this->run('DELETE FROM market_agent_catalogs WHERE agent=?',[$agent]);
        $this->run('INSERT INTO market_agent_catalogs VALUES (?,?)',[$agent,$fingerprint]);
    }
    private function agentSaveDefinition(array $agent): ?array {
        $fingerprint=$this->run('SELECT fingerprint FROM market_agent_catalogs WHERE agent=?',[$agent['id']])->fetchColumn()?:$agent['catalog'];
        $raw=$this->run('SELECT definition FROM market_catalog_versions WHERE `mod`=? AND fingerprint=?',[$this->mod,$fingerprint])->fetchColumn();
        return $raw?json_decode($raw,true,512,JSON_THROW_ON_ERROR):null;
    }
    public function refreshAgentCatalog(array $agent,array $data): array {
        $this->requireAgentMod($agent);
        return $this->transaction(function()use($agent,$data){
            $this->lockScope(false);$this->lockScope(true);
            if($this->run("SELECT id FROM custody_tickets WHERE owner=? AND scope IN (?,?) AND status IN ('prepared','writing')",[$agent['owner'],$this->scope(false),$this->scope(true)])->fetchColumn())throw new MarketError(409,'Finish the pending save transfer before updating marketplace compatibility.');
            if(!$this->run('SELECT id FROM custody_agents_scoped WHERE id=? AND owner=?',[$agent['id'],$agent['owner']])->fetchColumn())throw new MarketError(401,'The save-agent pairing changed.');
            $family=$this->registerSaveCatalog(self::digest($data['fingerprint']??null),$data['compatibility']??null);
            if($family!==$agent['catalog'])throw new MarketError(409,'The save-format family belongs to a different pairing.');
            $this->bindAgentCatalog($agent['id'],$data['fingerprint']);
            return ['catalog'=>$family,'fingerprint'=>$data['fingerprint'],'compatible'=>true];
        });
    }
    private function playerSaveDefinition(string $owner): ?array {
        $agent=$this->run("SELECT id,catalog FROM custody_agents_scoped WHERE owner=? AND `mod`=? AND game_mode='2.4'",[$owner,$this->mod])->fetch();
        return $agent?$this->agentSaveDefinition($agent):null;
    }
    private function supportsEscrowItem(string $owner,string $item): bool {
        $d=$this->playerSaveDefinition($owner);if(!$d)return true;
        $row=$this->run('SELECT i.code,e.metadata FROM items i LEFT JOIN escrow_items e ON e.item=i.id WHERE i.id=?',[$item])->fetch();
        if(!$row)return false;$metadata=$row['metadata']?json_decode($row['metadata'],true,512,JSON_THROW_ON_ERROR):[];
        $check=function($code,$display)use(&$check,$d){
            if(!isset($d['items'][str_pad($code,4,' ')])&&!isset($d['items'][str_pad($code,4,"\0")]))return false;
            foreach($display['saved_properties']??[] as $property)if(!isset($d['stats']['s'.$property['id']]))return false;
            foreach($display['socketed_items']??[] as $child)if(!$check($child['code'],$child['display']??$child))return false;
            return true;
        };
        return $check($row['code'],$metadata['display']??[]);
    }
    private function supportsBalance(string $owner,array $balance): bool {
        if(($balance['kind']??'')!=='bag')return true;
        $d=$this->playerSaveDefinition($owner);return !$d||isset($d['bags'][$balance['stat']]);
    }
}
