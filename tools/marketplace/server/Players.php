<?php
declare(strict_types=1);

trait MarketplacePlayers {
    private function playersSchema(string $engine): void {
        foreach([
            'market_players'=>'owner VARCHAR(64) PRIMARY KEY, linked BIGINT NOT NULL',
            'market_characters'=>'owner VARCHAR(64) NOT NULL, scope VARCHAR(255) NOT NULL, character_key CHAR(64) NOT NULL, name VARCHAR(64) NOT NULL, class_id INT NOT NULL, level INT NOT NULL, saved_at BIGINT NOT NULL, updated BIGINT NOT NULL, snapshot MEDIUMTEXT NOT NULL, PRIMARY KEY(owner,scope,character_key)',
            'market_messages'=>($engine?'seq BIGINT AUTO_INCREMENT PRIMARY KEY':'seq INTEGER PRIMARY KEY AUTOINCREMENT').', id CHAR(32) NOT NULL UNIQUE, sender VARCHAR(64) NOT NULL, recipient VARCHAR(64) NOT NULL, scope VARCHAR(255) NOT NULL, kind VARCHAR(16) NOT NULL, thread VARCHAR(64) NOT NULL, body TEXT NOT NULL, created BIGINT NOT NULL',
            'market_message_reads'=>'owner VARCHAR(64) PRIMARY KEY, through_seq BIGINT NOT NULL',
        ] as $name=>$columns)$this->db->exec("CREATE TABLE IF NOT EXISTS $name ($columns)$engine");
    }
    private function linkedPlayer(string $owner): void {
        $this->joinMod($owner,true);
        $insert=$this->db->getAttribute(PDO::ATTR_DRIVER_NAME)==='mysql'?'INSERT IGNORE':'INSERT OR IGNORE';
        $this->run("$insert INTO market_players VALUES (?,?)",[$owner,time()]);
    }
    public function players(bool $hc,array $query=[]): array {
        $search=trim(self::text(($query['q']??'')===''?' ':$query['q'],64));
        $after=isset($query['after'])?self::text($query['after'],64):'';
        $like='%'.str_replace(['!','%','_'],['!!','!%','!_'],strtolower($search)).'%';
        $rows=$this->run("SELECT a.id,a.name,p.linked,(SELECT COUNT(*) FROM market_characters c WHERE c.owner=a.id AND c.scope=?) AS characters,(SELECT MAX(updated) FROM market_characters c WHERE c.owner=a.id AND c.scope=?) AS updated FROM actors a JOIN market_memberships p ON p.owner=a.id AND p.`mod`=? AND p.game_mode='2.4' AND p.linked>0 WHERE a.role IN ('player','tester') AND a.id>? AND LOWER(a.name) LIKE ? ESCAPE '!' ORDER BY a.id LIMIT 25",[$this->scope($hc),$this->scope($hc),$this->mod,$after,$like])->fetchAll();
        $more=count($rows)>24;if($more)array_pop($rows);
        foreach($rows as &$r){$r['characters']=(int)$r['characters'];$r['updated']=(int)$r['updated'];$r['linked']=(int)$r['linked'];}unset($r);
        return ['players'=>$rows,'next_after'=>$more?end($rows)['id']:null];
    }
    public function playerProfile(string $owner,bool $hc,?string $character=null): array {
        $owner=self::text($owner,64);$actor=$this->run("SELECT a.id,a.name,p.linked FROM actors a JOIN market_memberships p ON p.owner=a.id AND p.`mod`=? AND p.game_mode='2.4' AND p.linked>0 WHERE a.id=? AND a.role IN ('player','tester')",[$this->mod,$owner])->fetch();
        if(!$actor)throw new MarketError(404,'Player profile not found.');
        $rows=$this->run('SELECT character_key,name,class_id,level,saved_at,updated FROM market_characters WHERE owner=? AND scope=? ORDER BY name,character_key',[$owner,$this->scope($hc)])->fetchAll();
        foreach($rows as &$r)foreach(['class_id','level','saved_at','updated'] as $k)$r[$k]=(int)$r[$k];unset($r);
        $result=['player'=>$actor,'characters'=>$rows,'character'=>null];
        if($character!==null){$character=self::digest($character);$value=$this->run('SELECT snapshot FROM market_characters WHERE owner=? AND scope=? AND character_key=?',[$owner,$this->scope($hc),$character])->fetchColumn();if($value===false)throw new MarketError(404,'Character not found in this game mode.');$result['character']=json_decode($value,true,512,JSON_THROW_ON_ERROR);}
        return $result;
    }
    private function profileAgent(array $agent,array $data): bool {
        $this->requireAgentMod($agent);
        if(($data['catalog']??null)!==$agent['catalog']||!$this->run('SELECT id FROM custody_agents_scoped WHERE id=? AND owner=?',[$agent['id'],$agent['owner']])->fetch())throw new MarketError(401,'The save-agent pairing changed.');
        return (bool)$this->run("SELECT owner FROM market_memberships WHERE owner=? AND `mod`=? AND game_mode='2.4' AND linked>0",[$agent['owner'],$this->mod])->fetchColumn();
    }
    public function publishCharacter(array $agent,array $data,bool $hc): array {
        return $this->transaction(function()use($agent,$data,$hc){
            if(!$this->profileAgent($agent,$data))throw new MarketError(409,'Link your player code before sharing characters.');
            $p=$data['character']??[];
            if(($p['schema']??null)!==1||($p['game_mode']??null)!=='2.4'||($p['mod']??null)!==$this->mod||($p['hardcore']??null)!==$hc)throw new MarketError(422,'Character belongs to another mod or game mode.');
            $name=self::text($p['name']??null,16);if(!preg_match('/^[\x20-\x7e]{1,16}$/D',$name))throw new MarketError(422,'Invalid character name.');
            $key=hash('sha256',strtolower($name));$saved=self::number($data['saved_at']??null,0,time()+300);
            $clean=['name'=>$name,'class_id'=>self::number($p['class_id']??null,0,6),'level'=>self::number($p['level']??null,1,255),'hardcore'=>$hc,'saved_at'=>$saved,'equipment'=>[],'inventory'=>[],'mercenary'=>['present'=>false,'equipment'=>[]]];
            foreach(['equipment','inventory'] as $kind){$rows=$p[$kind]??null;if(!is_array($rows)||!array_is_list($rows)||count($rows)>512)throw new MarketError(422,'Invalid character inventory.');foreach($rows as $row)$clean[$kind][]=$this->profileItem($row,$kind);}
            $merc=$p['mercenary']??[];if(!is_bool($merc['present']??null)||!is_array($merc['equipment']??null)||!array_is_list($merc['equipment'])||count($merc['equipment'])>16)throw new MarketError(422,'Invalid mercenary equipment.');
            $clean['mercenary']['present']=$merc['present'];foreach($merc['equipment'] as $row)$clean['mercenary']['equipment'][]=$this->profileItem($row,'equipment');
            $clean['character_key']=$key;
            // Never store a source path, save bytes, stash/cube/corpse rows or
            // authority to trade these display-only items.
            $scope=$this->scope($hc);$old=$this->run('SELECT character_key FROM market_characters WHERE owner=? AND scope=? AND character_key=?'.$this->lock(),[$agent['owner'],$scope,$key])->fetchColumn();
            if($old)$this->run('UPDATE market_characters SET name=?,class_id=?,level=?,saved_at=?,updated=?,snapshot=? WHERE owner=? AND scope=? AND character_key=?',[$name,$clean['class_id'],$clean['level'],$saved,time(),self::json($clean),$agent['owner'],$scope,$key]);
            else{if((int)$this->run('SELECT COUNT(*) FROM market_characters WHERE owner=? AND scope=?',[$agent['owner'],$scope])->fetchColumn()>=128)throw new MarketError(422,'Character limit reached.');$this->run('INSERT INTO market_characters VALUES (?,?,?,?,?,?,?,?,?)',[$agent['owner'],$scope,$key,$name,$clean['class_id'],$clean['level'],$saved,time(),self::json($clean)]);}
            return ['character_key'=>$key];
        });
    }
    private function profileItem(array $row,string $kind): array {
        $hex=self::text($row['item_hex']??null,65536);if(strlen($hex)<14||strlen($hex)%2||!ctype_xdigit($hex))throw new MarketError(422,'Invalid character item.');
        $location=self::readBits($hex,35,3);$body=self::readBits($hex,38,4);$x=self::readBits($hex,42,4);$y=self::readBits($hex,46,4);$container=self::readBits($hex,50,3);
        if($kind==='equipment'?($location!==1||$body<1||$body>12):!(($location===0&&$container===1)||$location===2))throw new MarketError(422,'Only carried inventory and equipped gear may be shared.');
        $native=$this->itemIdentity($hex);
        $item=['code'=>self::text($row['code']??null,4),'quality'=>self::number($row['quality']??null,1,9),'index'=>self::number($row['index']??-1,-1,1000000),'quantity'=>self::number($row['quantity']??1,0,4294967295),
            'identified'=>(bool)($row['identified']??false),'ethereal'=>(bool)($row['ethereal']??false),'saved_id'=>self::number($row['saved_id']??0,0,4294967295),'item_uuid'=>'','sockets'=>count($row['socketed_items']??[]),'display'=>$this->displayItem($row),
            'location'=>$location,'body'=>$body,'x'=>$x,'y'=>$y,'container'=>$container,'grid_width'=>self::number($row['grid_width']??null,1,16),'grid_height'=>self::number($row['grid_height']??null,1,16)];
        if($native){foreach(['code','quality','index','saved_id','item_uuid','identified','name_ids'] as $k)$item[$k]=$native[$k];}
        return $item;
    }
    public function characterManifest(array $agent,array $data,bool $hc): array {
        return $this->transaction(function()use($agent,$data,$hc){
            if(!$this->profileAgent($agent,$data))throw new MarketError(409,'Link your player first.');
            $keys=$data['characters']??null;if(!is_array($keys)||!array_is_list($keys)||count($keys)>128)throw new MarketError(422,'Invalid character manifest.');$keys=array_values(array_unique(array_map(fn($k)=>self::digest($k),$keys)));
            $sql='DELETE FROM market_characters WHERE owner=? AND scope=?';$args=[$agent['owner'],$this->scope($hc)];if($keys){$sql.=' AND character_key NOT IN ('.implode(',',array_fill(0,count($keys),'?')).')';$args=array_merge($args,$keys);}$this->run($sql,$args);return ['characters'=>count($keys)];
        });
    }
    private function notifyTrade(string $event,string $offer,array $actor,string $kind,string $message,array $price): void {
        $labels=['message'=>'Trade message','proposed'=>'New offer','counter'=>'Counter-offer','accept'=>'Offer accepted','reject'=>'Offer declined','withdraw'=>'Offer withdrawn','superseded'=>'Offer closed','completed_sale'=>'Trade completed','delivered'=>'Item delivered'];
        if(!isset($labels[$kind]))return;
        $o=$this->run('SELECT o.buyer,o.seller,o.price,i.* FROM offers o JOIN listings l ON l.id=o.listing JOIN items i ON i.id=l.item WHERE o.id=?',[$offer])->fetch();if(!$o)return;
        $title=$this->notificationItemName($o);$bundle=$this->run('SELECT b.title FROM market_bundles b JOIN offers o ON o.listing=b.listing WHERE o.id=?',[$offer])->fetchColumn();if($bundle)$title='Bundle: '.$bundle;$gift=$kind==='completed_sale'&&(bool)$this->run("SELECT id FROM events WHERE offer=? AND kind='gifted' LIMIT 1",[$offer])->fetchColumn();
        if(!$price&&in_array($kind,['superseded','delivered'],true))$price=json_decode($o['price'],true,512,JSON_THROW_ON_ERROR);
        foreach(array_unique([$o['buyer'],$o['seller']]) as $recipient){
            // Completion is a system outcome, so both sides need confirmation.
            // Delivery confirms the recipient's own guarded save write only.
            if($kind==='delivered'?($recipient!==$actor['id']):($kind!=='completed_sale'&&$recipient===$actor['id']))continue;
            $sender=in_array($kind,['completed_sale','delivered'],true)?($recipient===$o['buyer']?$o['seller']:$o['buyer']):$actor['id'];
            $body=($bundle&&$kind==='delivered'?'Bundle delivered':($gift?($recipient===$o['buyer']?'Gift received':'Gift sent'):$labels[$kind])).': '.$title;
            if($gift)$body.=$recipient===$o['buyer']?' — item queued for delivery.':' — ownership transferred; item queued for the recipient.';
            elseif($kind==='completed_sale')$body.=$recipient===$o['buyer']?' — purchase complete; item queued for delivery.':' — sale complete; payment is available in escrow.';
            elseif($kind==='delivered')$body.=' — delivery page updated. Switch to that page to load it.';
            elseif($message!=='')$body.=' — '.$message;
            elseif($kind==='accept')$body.=' — terms agreed; settlement requires both sides’ escrow.';
            if($price){$terms=[];foreach($price as $p)$terms[]=(($p['kind']??'')==='free'?'Free':$p['quantity'].' × '.(($p['kind']??'')==='gold'?'Gold':(($p['kind']??'')==='bag'?$p['label']:$this->notificationItemName(['code'=>$p['code'],'quality'=>$p['quality'],'item_index'=>$p['index']]))));$body.=' | Terms: '.implode(' + ',$terms);}
            $body=substr($body,0,4096);while(!preg_match('//u',$body))$body=substr($body,0,-1);
            $this->run('INSERT INTO market_messages (id,sender,recipient,scope,kind,thread,body,created) VALUES (?,?,?,?,?,?,?,?)',[hash('md5',$event.'|'.$recipient),$sender,$recipient,$o['scope'],'trade',$offer,$body,time()]);
        }
    }
    private function notificationItemName(array $item): string {
        // Read-only presentation data. Catalog failure must never block a trade.
        static $default=null;
        if($default===null){$default=[];try{$path=__DIR__.'/public/assets/catalog.json';if(is_file($path))$default=json_decode(file_get_contents($path),true,512,JSON_THROW_ON_ERROR);}catch(Throwable){$default=[];}}
        $catalog=$this->mod==='RMD-MP'?$default:$this->browserCatalog();
        $native=isset($item['item_hex'])?$this->itemIdentity($item['item_hex']):null;
        $code=$native['code']??$item['code'];$q=(int)($native['quality']??$item['quality']);$index=$native['index']??$item['item_index']??-1;
        $base=$catalog['bases'][$code]??[];$name=$base['name']??'Item '.$code;
        if(preg_match('/^[¶ⅷ\s]+$/u',$name)){preg_match('/^g([cfszp])[bgvrwy]$/D',$code,$gem);$grade=isset($gem[1])?(['c'=>'Chipped','f'=>'Flawed','s'=>'','z'=>'Flawless','p'=>'Perfect'][$gem[1]]):(['skc'=>'Chipped','skf'=>'Flawed','sku'=>'','skl'=>'Flawless','skz'=>'Perfect'][$code]??'');$name=trim($grade.' '.($base['type']??'Gem'));}
        if($native&& !$native['identified'])return 'Unidentified '.$name;
        $names=$native['name_ids']??$item['name_ids']??[];
        if($q===4)return implode(' ',array_filter([$catalog['names']['magic_prefix'][$names['magic_prefix']??0]??null,$name,$catalog['names']['magic_suffix'][$names['magic_suffix']??0]??null]));
        if(in_array($q,[6,8,9],true)){$rare=implode(' ',array_filter([$catalog['names']['rare'][$names['rare_prefix']??0]??null,$catalog['names']['rare'][$names['rare_suffix']??0]??null]));if($rare!=='')return $rare;}
        $named=$catalog[$q===7?'unique':($q===5?'set':'')][$index]??null;
        return $named&&($named['code']===$code||in_array($code,$named['codes']??[],true))?$named['name']:$name;
    }
    private function notifyTradeDelivery(string $event,string $item,array $actor): void {
        $offer=$this->run("SELECT offer FROM events WHERE item=? AND buyer=? AND kind IN ('completed_sale','bundle_sale') AND offer IS NOT NULL ORDER BY seq DESC LIMIT 1",[$item,$actor['id']])->fetchColumn();
        if(!$offer)return;$listing=$this->run('SELECT listing FROM offers WHERE id=?',[$offer])->fetchColumn();$members=$this->bundleGoods($listing,$item);
        if(count($members)>1){$sale=(int)$this->run("SELECT seq FROM events WHERE offer=? AND kind='completed_sale' ORDER BY seq DESC LIMIT 1",[$offer])->fetchColumn();foreach($members as $member)if(!$this->run("SELECT id FROM events WHERE item=? AND actor=? AND kind='delivered' AND seq>? LIMIT 1",[$member,$actor['id'],$sale])->fetchColumn())return;}
        $this->notifyTrade($event,$offer,$actor,'delivered','',[]);
    }
    public function inbox(array $actor,int $after=0,string $channel='web',bool $preferencesOnly=false): array {
        $after=self::number($after,0,PHP_INT_MAX);$owner=$actor['id'];$this->evaluateSearches($actor);
        $rows=$preferencesOnly?[]:$this->run('SELECT m.*,a.name AS sender_name FROM market_messages m JOIN actors a ON a.id=m.sender WHERE m.recipient=? AND m.seq>? ORDER BY m.seq LIMIT 100',[$owner,$after])->fetchAll();
        $next=$rows?(int)end($rows)['seq']:$after;$preferences=$this->notificationSettings($owner);
        foreach($rows as &$r){$notice=$this->run('SELECT listing,search FROM market_search_notices WHERE message=?',[$r['id']])->fetch();$r['category']=$notice?'search':$r['kind'];if($notice)$r['listing_id']=$notice['listing'];}unset($r);
        if($channel==='game')$rows=array_values(array_filter($rows,function($r)use($owner,$preferences){
            if(!$preferences['game_'.$r['category']])return false;
            if($r['kind']==='trade'&&!str_starts_with($r['scope'],$this->community.'|'.$this->mod.'|2.4|'))return false;
            return !$this->run('SELECT owner FROM market_player_controls WHERE owner=? AND target=? AND (muted=1 OR blocked=1)',[$owner,$r['sender']])->fetchColumn();
        }));
        foreach($rows as &$r){$r['seq']=(int)$r['seq'];$r['created']=(int)$r['created'];}unset($r);
        $max=(int)$this->run('SELECT MAX(seq) FROM market_messages WHERE recipient=?',[$owner])->fetchColumn();$read=(int)$this->run('SELECT through_seq FROM market_message_reads WHERE owner=?',[$owner])->fetchColumn();
        $filters=$channel==='game'?['delivery_preferences'=>$preferences,'muted_senders'=>$this->run('SELECT target FROM market_player_controls WHERE owner=? AND (muted=1 OR blocked=1)',[$owner])->fetchAll(PDO::FETCH_COLUMN)]:[];
        return $filters+['owner'=>$owner,'messages'=>$rows,'next'=>$next,'latest'=>$max,'unread'=>(int)$this->run('SELECT COUNT(*) FROM market_messages WHERE recipient=? AND seq>?',[$owner,$read])->fetchColumn()];
    }
    public function conversations(array $actor,?string $peer=null,int $before=PHP_INT_MAX): array {
        $owner=$actor['id'];$before=self::number($before,1,PHP_INT_MAX);
        if($peer!==null){$peer=self::text($peer,64);$target=$this->run('SELECT a.id,a.name FROM actors a JOIN market_players p ON p.owner=a.id WHERE a.id=?',[$peer])->fetch();if(!$target)throw new MarketError(404,'Player not found.');
            $rows=$this->run("SELECT m.*,a.name AS sender_name FROM market_messages m JOIN actors a ON a.id=m.sender WHERE m.kind='direct' AND ((m.sender=? AND m.recipient=?) OR (m.sender=? AND m.recipient=?)) AND m.seq<? ORDER BY m.seq DESC LIMIT 51",[$owner,$peer,$peer,$owner,$before])->fetchAll();$more=count($rows)>50;if($more)array_pop($rows);
            foreach($rows as &$r){$r['seq']=(int)$r['seq'];$r['created']=(int)$r['created'];}unset($r);
            return ['peer'=>$target,'messages'=>array_reverse($rows),'next_before'=>$more?(int)end($rows)['seq']:null];
        }
        $rows=$this->run("SELECT CASE WHEN sender=? THEN recipient ELSE sender END AS peer,MAX(seq) AS last_seq FROM market_messages WHERE kind='direct' AND (sender=? OR recipient=?) GROUP BY CASE WHEN sender=? THEN recipient ELSE sender END ORDER BY last_seq DESC LIMIT 100",[$owner,$owner,$owner,$owner])->fetchAll();$out=[];
        foreach($rows as $r){$m=$this->run('SELECT body,created,sender FROM market_messages WHERE seq=?',[$r['last_seq']])->fetch();$p=$this->run('SELECT id,name FROM actors WHERE id=?',[$r['peer']])->fetch();$out[]=array_merge($p,['body'=>$m['body'],'created'=>(int)$m['created'],'last_seq'=>(int)$r['last_seq'],'outgoing'=>$m['sender']===$owner]);}
        return ['conversations'=>$out];
    }
    private function playerCommand(array $actor,array $data): array {
        return $this->transaction(function()use($actor,$data){
            $owner=$actor['id'];$this->run('SELECT id FROM actors WHERE id=?'.$this->lock(),[$owner])->fetch();
            $id=self::ticketId($data['id']??null);$hash=hash('sha256',self::json($data));$old=$this->run('SELECT hash,result FROM commands WHERE actor=? AND id=?',[$owner,$id])->fetch();
            if($old){if(!hash_equals($old['hash'],$hash))throw new MarketError(409,'Command ID reused.');return json_decode($old['result'],true,512,JSON_THROW_ON_ERROR);}
            if(($data['type']??'')==='direct_message'){
                if(!$this->run('SELECT owner FROM market_players WHERE owner=?',[$owner])->fetch())throw new MarketError(403,'Link your player code before messaging.');
                $to=self::text($data['to']??null,64);$recipient=$this->run('SELECT a.id FROM actors a JOIN market_players p ON p.owner=a.id WHERE a.id=? OR LOWER(a.name)=LOWER(?) ORDER BY a.id LIMIT 1',[$to,$to])->fetchColumn();
                // Self messages use the same inbox and native delivery path so
                // a linked player can test website-to-game chat without a peer.
                if(!$recipient)throw new MarketError(422,'Choose a linked player.');if($this->communicationBlocked($owner,$recipient))throw new MarketError(403,'Messaging is unavailable between these players.');$body=trim(self::message($data['message']??null));if($body==='')throw new MarketError(422,'Write a message first.');
                if((int)$this->run("SELECT COUNT(*) FROM market_messages WHERE sender=? AND kind='direct' AND created>?",[$owner,time()-60])->fetchColumn()>=30)throw new MarketError(429,'Wait a moment before sending more messages.');
                $pair=[$owner,$recipient];sort($pair);$thread=hash('sha256',implode('|',$pair));
                $this->run("INSERT INTO market_messages (id,sender,recipient,scope,kind,thread,body,created) VALUES (?,?,?,?,?,?,?,?)",[$id,$owner,$recipient,'','direct',$thread,$body,time()]);$result=['message_id'=>$id,'peer'=>$recipient];
            }elseif(($data['type']??'')==='message_read'){
                $max=(int)$this->run('SELECT MAX(seq) FROM market_messages WHERE recipient=?',[$owner])->fetchColumn();$through=self::number($data['through']??null,0,$max);$oldRead=$this->run('SELECT through_seq FROM market_message_reads WHERE owner=?'.$this->lock(),[$owner])->fetchColumn();
                if($oldRead===false)$this->run('INSERT INTO market_message_reads VALUES (?,?)',[$owner,$through]);else $this->run('UPDATE market_message_reads SET through_seq=? WHERE owner=?',[max($through,(int)$oldRead),$owner]);$result=['through'=>$through];
            }else throw new MarketError(422,'Unknown messaging command.');
            $this->run('INSERT INTO commands VALUES (?,?,?,?)',[$owner,$id,$hash,self::json($result)]);return $result;
        });
    }
}
