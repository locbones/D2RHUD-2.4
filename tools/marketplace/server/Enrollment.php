<?php
declare(strict_types=1);

// A fresh installation proves possession of its own random secret. It can only
// create/recover that installation's ordinary player, never an existing account.
trait MarketplaceEnrollment {
    private function enrollmentSchema(string $engine): void {
        $this->db->exec("CREATE TABLE IF NOT EXISTS market_installations (installation CHAR(64) PRIMARY KEY, owner CHAR(32) NOT NULL, `mod` VARCHAR(64) NOT NULL, catalog CHAR(64) NOT NULL, created BIGINT NOT NULL)$engine");
    }
    public function enrollInstallation(array $data,string $address): array {
        $secret=self::digest($data['installation']??null);
        $mod=self::modName($data['mod']??'');
        if(($data['game_mode']??'')!=='2.4')throw new MarketError(409,'The 3.x marketplace is coming soon.');
        $catalog=self::digest($data['catalog']??null);
        $key=hash('sha256','installation|'.$secret);
        $playerToken=hash_hmac('sha256','player',$secret);
        $agentToken=hash_hmac('sha256','agent|'.$mod.'|'.$catalog,$secret);
        return $this->transaction(function()use($data,$secret,$mod,$catalog,$key,$playerToken,$agentToken,$address){
            $selected=new self($this->db,$this->community,$mod);
            $fingerprint=$catalog;
            if(isset($data['compatibility'])){
                $catalog=$selected->registerSaveCatalog($fingerprint,$data['compatibility']);
                $agentToken=hash_hmac('sha256','agent|'.$mod.'|'.$catalog,$secret);
            }
            // Registration is serialized with catalog registration and retries.
            $insert=$this->db->getAttribute(PDO::ATTR_DRIVER_NAME)==='mysql'?'INSERT IGNORE':'INSERT OR IGNORE';
            $scope=$this->community.'|enrollment';$this->run("$insert INTO custody_serial VALUES (?,0)",[$scope]);
            $this->run('SELECT version FROM custody_serial WHERE scope=?'.$this->lock(),[$scope])->fetch();
            $old=$this->run('SELECT * FROM market_installations WHERE installation=?',[$key])->fetch();
            if($old){
                if($old['mod']!==$mod||!hash_equals($old['catalog'],$catalog))throw new MarketError(409,'This installation is already paired to another mod or catalog. Revalidate it instead of creating another identity.');
                $actor=$this->auth($playerToken);
                $agent=$this->authAgent($agentToken);
                if($agent['owner']!==$actor['id']||$agent['catalog']!==$catalog)throw new MarketError(409,'This installation pairing needs recovery.');
                if(isset($data['compatibility']))$selected->bindAgentCatalog($agent['id'],$fingerprint);
                return ['player'=>$actor+['token'=>$playerToken],'agent'=>$agent+['token'=>$agentToken]];
            }
            if($this->controlState()['phase']!=='live')throw new MarketError(409,'Marketplace setup is temporarily paused. Your game remains available.');
            // Limit new identities, not harmless reconnects. Never trust forwarded
            // addresses supplied by callers, and never retain the raw address.
            $rate=hash('sha256','enroll|'.$address);$now=time();
            $this->run("$insert INTO web_attempts VALUES (?,?,0)",[$rate,$now]);
            $r=$this->run('SELECT * FROM web_attempts WHERE identity=?'.$this->lock(),[$rate])->fetch();
            $attempts=(int)$r['started']<$now-3600?0:(int)$r['attempts'];
            if($attempts>=12)throw new MarketError(429,'Too many new marketplace installations. Try again in an hour.');
            $this->run('UPDATE web_attempts SET started=?,attempts=? WHERE identity=?',[$attempts?(int)$r['started']:$now,$attempts+1,$rate]);
            $selected=new self($this->db,$this->community,$mod);
            $name='Player-'.substr(hash('sha256',$secret),0,7);
            $player=$selected->user($name);
            $this->run('UPDATE actors SET token_hash=? WHERE id=?',[hash('sha256',$playerToken),$player['id']]);$player['token']=$playerToken;
            $agent=$selected->pairAgent($player['id'],$catalog);
            if(isset($data['compatibility']))$selected->bindAgentCatalog($agent['id'],$fingerprint);
            $this->run('UPDATE custody_agents_scoped SET token_hash=? WHERE id=?',[hash('sha256',$agentToken),$agent['id']]);$agent['token']=$agentToken;
            // Same validation as link codes: definitions are scoped to this mod,
            // and an existing catalog can never be replaced by another client.
            $selected->registerLinkContext($player,['mod'=>$mod,'game_mode'=>'2.4','catalog'=>$catalog,'identity_catalog'=>$data['identity_catalog']??null]);
            $this->run('INSERT INTO market_installations VALUES (?,?,?,?,?)',[$key,$player['id'],$mod,$catalog,$now]);
            return ['player'=>$player,'agent'=>$agent];
        });
    }
}
