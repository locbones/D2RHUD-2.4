<?php
declare(strict_types=1);

// Version-98 root item prefix, using the exact compiled RMD-MP definitions
// exported by custody-save identity-catalog. No caller-supplied UUID/seed is
// trusted. Runtime unit IDs and stash coordinates are never persistent IDs.
final class SavedItemIdentity {
    public static function read(string $hex,?array $definitions=null): ?array {
        static $default=null;
        if($definitions===null&&$default===null)$default=json_decode(file_get_contents(__DIR__.'/item-identity.json'),true,512,JSON_THROW_ON_ERROR);
        $catalog=$definitions??$default;$codes=array_flip($catalog['codes']);
        if($hex===''||strlen($hex)%2||!ctype_xdigit($hex))return null;
        $bytes=hex2bin($hex);$at=0;
        $read=static function(int $n)use(&$at,$bytes):int{
            if($n<0||$n>32||$at+$n>strlen($bytes)*8)throw new UnderflowException();
            $v=0;for($i=0;$i<$n;$i++)$v|=((ord($bytes[intdiv($at+$i,8)])>>(($at+$i)%8))&1)<<$i;$at+=$n;return $v;
        };
        $skip=static function(int $n)use(&$at,$bytes):void{if($n<0||$at+$n>strlen($bytes)*8)throw new UnderflowException();$at+=$n;};
        try{
            $flags=$read(32);$skip(3);$mode=$read(3);if($mode>6||$mode===3||$mode===5||($flags&(1<<16)))return null;$skip(15);
            $code='';for($i=0;$i<4;$i++){$word='';for($n=0;$n<17;$n++){$word.=$read(1);if(isset($codes[$word])){$code.=chr($codes[$word]);break;}}if(strlen($code)!==$i+1)return null;}
            $d=$catalog['items'][$code]??null;if(!$d)return null;$seed=0;$quality=2;$index=-1;$level=0;
            $nameIds=[];$seedBit=null;$compact=($flags&(1<<21))!==0;
            if($compact){if($d['gold']){$large=$read(1);$skip($large?32:12);$skip(1);}$skip($d['quest_bits']);}
            else{
                $skip(3);$seedBit=$at;$seed=$read(32);$level=$read(7);$quality=$read(4);if(!$level||$quality<1||$quality>9)return null;
                if($read(1))$skip(3);if($read(1))$skip(11);
                switch($quality){
                    case 1:case 3:$skip(3);break;
                    case 2:if($d['charm'])$skip(12);if($d['body'])$skip(10);if($d['tome'])$skip(5);break;
                    case 4:$nameIds=['magic_prefix'=>$read(11),'magic_suffix'=>$read(11)];break;
                    case 5:case 7:$index=$read(12);break;
                    case 6:case 8:$nameIds=['rare_prefix'=>$read(8),'rare_suffix'=>$read(8)];for($n=0;$n<3;$n++){if($read(1))$skip(11);if($read(1))$skip(11);}break;
                    case 9:$nameIds=['rare_prefix'=>$read(8),'rare_suffix'=>$read(8)];break;
                }
                if($flags&(1<<26))$skip(16);
                if($flags&(1<<24)){for($n=0;$n<32;$n++)if(!$read(8))break;if($n===32)return null;}
            }
            $uuid='';$uuidBit=null;if($read(1)){$uuidBit=$at;for($n=0;$n<16;$n++)$uuid.=sprintf('%02x',$read(8));if($uuid===str_repeat('0',32))$uuid='';}
            // Identity-less compact currency is fungible. Do not invent a
            // shared UUID for every Zod rune or merge their separate ledgers.
            $key=$uuid!==''?'uuid:'.$uuid:($seed?'seed:'.rtrim($code).'|'.$seed:(!$compact?'bytes:'.hash('sha256',self::positionless($hex)):''));
            return ['key'=>$key,'item_uuid'=>$uuid,'uuid_bit'=>$uuidBit,'saved_id'=>$seed,'seed_bit'=>$seedBit,'code'=>rtrim($code),'quality'=>$quality,'index'=>$index,'level'=>$level,'identified'=>(int)(bool)($flags&(1<<4)),'name_ids'=>$nameIds];
        }catch(UnderflowException){return null;}
    }
    public static function positionless(string $hex): string {
        $b=hex2bin($hex);for($bit=35;$bit<53&&$bit<strlen($b)*8;$bit++){$i=intdiv($bit,8);$b[$i]=chr(ord($b[$i])&~(1<<($bit%8)));}return bin2hex($b);
    }
    public static function publicationMatchesSave(string $published,string $saved,?array $definitions=null): bool {
        if($published===$saved)return true;
        if(self::positionless($published)===self::positionless($saved))return true;
        // Discovery can contain the native IFLAG_ITEM marker (bit 27) and a
        // previous stash position. Neither changes the saved item. Require a
        // real persistent identity to also ignore that marker, and equality
        // of every remaining bit; seed
        // alone must never make changed properties or a duplicate acceptable.
        $a=self::read($published,$definitions);$b=self::read($saved,$definitions);
        if(!$a||!$b||$a['key']===''||str_starts_with($a['key'],'bytes:')||$a['key']!==$b['key'])return false;
        $normalize=static function(string $hex):string{
            $bytes=hex2bin(self::positionless($hex));$bytes[3]=chr(ord($bytes[3])&~0x08);return bin2hex($bytes);
        };
        return $normalize($published)===$normalize($saved);
    }
    public static function key(string $hex): string {return self::read($hex)['key']??'bytes:'.hash('sha256',self::positionless($hex));}
}

trait MarketplaceIdentity {
    private function identitySchema(string $engine): void {
        $this->db->exec("CREATE TABLE IF NOT EXISTS item_identities (item CHAR(32) PRIMARY KEY, scope VARCHAR(255) NOT NULL, identity_key VARCHAR(128) NOT NULL)$engine");
        $this->db->exec("CREATE TABLE IF NOT EXISTS identity_reservations (scope VARCHAR(255) NOT NULL, identity_key VARCHAR(128) NOT NULL, ticket CHAR(32) NOT NULL, PRIMARY KEY(scope,identity_key))$engine");
        // Additive backfill: preserve every ledger and its ownership chain,
        // including ambiguous historical collisions. Future admission refuses
        // ambiguous identities instead of silently merging unrelated histories.
        $this->transaction(function(){
        $this->lockScope(false);$this->lockScope(true);
        foreach($this->run('SELECT DISTINCT scope FROM items ORDER BY scope')->fetchAll(PDO::FETCH_COLUMN) as $scope){$parts=explode('|',$scope);if(count($parts)!==4||$parts[0]!==$this->community)continue;$selected=new Market($this->db,$this->community,$parts[1]);$selected->lockScope($parts[3]==='HC');}
        foreach($this->run('SELECT id,scope,item_hex,seed FROM items')->fetchAll() as $r)$this->recordIdentity($r['id'],$r['scope'],$r['item_hex']);
        foreach($this->run("SELECT id,scope,packet FROM custody_tickets WHERE kind='deposit' AND status IN ('prepared','writing')")->fetchAll() as $t)foreach(json_decode($t['packet'],true,512,JSON_THROW_ON_ERROR)['items'] as $i){$key=$this->identityKey($i['item_hex'],$t['scope']);if($key!==''&&!$this->run('SELECT ticket FROM identity_reservations WHERE scope=? AND identity_key=?',[$t['scope'],$key])->fetch())$this->run('INSERT INTO identity_reservations VALUES (?,?,?)',[$t['scope'],$key,$t['id']]);}
        });
    }
    private function recordIdentity(string $item,string $scope,string $hex): void {
        $key=$this->identityKey($hex,$scope);$native=$this->itemIdentity($hex,$scope);
        if($native)$this->run('UPDATE items SET seed=? WHERE id=?',[$native['saved_id'],$item]);
        $this->run('DELETE FROM item_identities WHERE item=?',[$item]);
        if($key!=='')$this->run('INSERT INTO item_identities VALUES (?,?,?)',[$item,$scope,$key]);
    }
    private function identityAdmission(string $owner,string $scope,string $hex,?string $ledger=null,?string $ticket=null,bool $discovery=false): ?string {
        $key=$this->identityKey($hex,$scope);if($key==='')return null;
        $reserved=$this->run('SELECT ticket FROM identity_reservations WHERE scope=? AND identity_key=?',[$scope,$key])->fetchColumn();
        if($reserved!==false&&$reserved!==$ticket)throw new MarketError(409,'This item identity is reserved by another save transaction.',['retryable'=>true]);
        $rows=$this->run("SELECT n.item,i.owner,e.state,EXISTS(SELECT 1 FROM events WHERE events.item=i.id AND events.kind='completed_sale') AS sold FROM item_identities n JOIN items i ON i.id=n.item LEFT JOIN escrow_items e ON e.item=i.id WHERE n.scope=? AND n.identity_key=?",[$scope,$key])->fetchAll();
        $custody=array_values(array_filter($rows,fn($r)=>$r['state']!==null||(int)$r['sold']===1));
        if(count($custody)>1){
            // Audited stack splits can share a legacy seed. Only the exact
            // delivered fragment's existing ledger can re-enter; never select
            // a ledger by the seed alone.
            $matches=array_values(array_filter($custody,fn($r)=>$r['item']===$ledger&&$r['owner']===$owner&&$r['state']==='delivered'));
            if(count($matches)!==1)throw new MarketError(409,'Item identity collision: operator review is required. No save was changed.');
            $custody=$matches;
        }
        if($custody){$r=$custody[0];
            if($r['owner']!==$owner)throw new MarketError(409,'This item has already changed owner. A restored copy cannot be hosted.');
            if($r['state']!=='delivered')throw new MarketError(409,'This item is already held in Vault. A restored copy cannot be deposited.');
            if(!$discovery&&$ledger!==$r['item'])throw new MarketError(409,'This delivered item needs its existing ledger reference. Refresh the companion before depositing.');
            return $r['item'];
        }
        // Discovery does not establish ownership. Prevent simultaneous public
        // copies, but let the authoritative save agent reconcile its own row.
        foreach($rows as $r)if($r['owner']!==$owner&&$this->run("SELECT id FROM listings WHERE item=? AND status IN ('open','agreed')",[$r['item']])->fetch())throw new MarketError(409,'This identity is already advertised by another player. Operator review is required.');
        $own=array_values(array_filter($rows,fn($r)=>$r['owner']===$owner));
        if($discovery&&count($own)===1)return $own[0]['item'];
        return null;
    }
    private function reserveIdentities(string $owner,string $scope,string $ticket,array $items): void {
        $indicesByKey=[];
        foreach($items as $index=>$r){
            $key=$this->identityKey($r['item_hex'],$scope);
            if($key!=='')$indicesByKey[$key][]=$index;
        }
        $duplicates=[];
        foreach($indicesByKey as $indices)if(count($indices)>1)array_push($duplicates,...$indices);
        if($duplicates)throw new MarketError(409,'Two copies of the same persistent item identity were found. No save was changed.',['deposit_item_indices'=>$duplicates]);
        $this->validateDepositItems($items,function(array $r)use($owner,$scope,$ticket){
            $this->identityAdmission($owner,$scope,$r['item_hex'],$r['ledger_id']??null,$ticket);
        });
        foreach(array_keys($indicesByKey) as $key){
            $this->run('INSERT INTO identity_reservations VALUES (?,?,?)',[$scope,$key,$ticket]);
        }
    }
}
