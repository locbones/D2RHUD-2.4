<?php
declare(strict_types=1);

trait MarketplaceExperience {
    private function experienceSchema(string $engine): void {
        foreach([
            'market_searches'=>'id CHAR(32) PRIMARY KEY, owner VARCHAR(64) NOT NULL, scope VARCHAR(255) NOT NULL, name VARCHAR(80) NOT NULL, criteria TEXT NOT NULL, enabled INT NOT NULL, created BIGINT NOT NULL',
            'market_search_hits'=>'search CHAR(32) NOT NULL, listing CHAR(32) NOT NULL, revision INT NOT NULL, PRIMARY KEY(search,listing,revision)',
            'market_search_notices'=>'message CHAR(32) PRIMARY KEY, listing CHAR(32) NOT NULL, search CHAR(32) NOT NULL',
            'market_search_poll'=>'owner VARCHAR(64) PRIMARY KEY, checked BIGINT NOT NULL',
            'market_notifications'=>'owner VARCHAR(64) PRIMARY KEY, settings TEXT NOT NULL',
            'market_player_controls'=>'owner VARCHAR(64) NOT NULL, target VARCHAR(64) NOT NULL, muted INT NOT NULL, blocked INT NOT NULL, PRIMARY KEY(owner,target)',
            'market_companion_seen'=>'owner VARCHAR(64) NOT NULL, scope VARCHAR(255) NOT NULL, seen BIGINT NOT NULL, PRIMARY KEY(owner,scope)',
            'market_activity_reads'=>'owner VARCHAR(64) NOT NULL, scope VARCHAR(255) NOT NULL, through_seq BIGINT NOT NULL, PRIMARY KEY(owner,scope)',
        ] as $name=>$columns)$this->db->exec("CREATE TABLE IF NOT EXISTS $name ($columns)$engine");
    }
    private function touchCompanion(string $owner,string $scope): void {
        $now=time();$sql=$this->db->getAttribute(PDO::ATTR_DRIVER_NAME)==='mysql'?'INSERT INTO market_companion_seen VALUES (?,?,?) ON DUPLICATE KEY UPDATE seen=IF(seen<?,VALUES(seen),seen)':'INSERT INTO market_companion_seen VALUES (?,?,?) ON CONFLICT(owner,scope) DO UPDATE SET seen=excluded.seen WHERE seen<?';$this->run($sql,[$owner,$scope,$now,$now-20]);
    }
    private function companionSeen(string $owner,string $scope): int {return (int)$this->run('SELECT seen FROM market_companion_seen WHERE owner=? AND scope=?',[$owner,$scope])->fetchColumn();}
    public function notificationSettings(string $owner): array {
        $r=$this->run('SELECT settings FROM market_notifications WHERE owner=?',[$owner])->fetchColumn();
        // Older clients enabled listing discovery along with trade alerts by
        // default. Require an explicit choice in the clarified controls.
        $stored=$r?json_decode($r,true,512,JSON_THROW_ON_ERROR):[];
        $defaults=['game_direct'=>true,'game_trade'=>true,'game_search'=>false];
        $settings=array_intersect_key($stored,$defaults)+$defaults;
        if(!($stored['search_opt_in']??false))$settings['game_search']=false;
        return $settings;
    }
    private function communicationBlocked(string $a,string $b): bool {
        return (bool)$this->run('SELECT owner FROM market_player_controls WHERE blocked=1 AND ((owner=? AND target=?) OR (owner=? AND target=?))',[$a,$b,$b,$a])->fetchColumn();
    }
    private function searchCriteria(mixed $r): array {
        if(!is_array($r))throw new MarketError(422,'Invalid saved search.');foreach(['q','seller','category','searchScope'] as $k)if(isset($r[$k])&&!is_string($r[$k]))throw new MarketError(422,'Invalid search text.');
        $c=['q'=>trim($r['q']??''),'searchScope'=>$r['searchScope']??'both','quality'=>(string)($r['quality']??''),'category'=>$r['category']??'','seller'=>trim($r['seller']??''),'ethereal'=>$r['ethereal']??-1,'sockets_min'=>$r['sockets_min']??0,'sockets_max'=>$r['sockets_max']??6,'free'=>($r['free']??false),'ready'=>($r['ready']??false),'hideOwn'=>($r['hideOwn']??true),'hideOffers'=>($r['hideOffers']??false),'affordable'=>$r['affordable']??false,'favorites'=>$r['favorites']??'all','stats'=>[],'cap'=>$r['cap']??null];
        foreach(['q','seller'] as $k)if($c[$k]!=='')self::text($c[$k],128);
        if(!in_array($c['searchScope'],['both','offered','wanted'],true)||!in_array($c['quality'],['','1','2','3','4','5','6','7','8','9'],true)||!in_array($c['category'],['','weapon','armor','jewellery','charm','misc'],true))throw new MarketError(422,'Invalid saved search.');
        self::number($c['ethereal'],-1,1);self::number($c['sockets_min'],0,6);self::number($c['sockets_max'],$c['sockets_min'],6);
        if(!in_array($c['favorites'],['all','trades','sellers'],true))throw new MarketError(422,'Invalid favorite search option.');
        foreach(['free','ready','hideOwn','hideOffers','affordable'] as $k)if(!is_bool($c[$k]))throw new MarketError(422,'Choose search options on or off.');
        $catalog=$this->browserCatalog();$stats=$r['stats']??[];
        if(!is_array($stats)||!array_is_list($stats)||count($stats)>8)throw new MarketError(422,'Use up to eight property filters.');
        foreach($stats as $p){$id=self::number($p['id']??null,0,4095);$d=$catalog['stats'][$id]??null;
            if(!$d||empty($d['func'])||empty($d['label'])||stripos($d['name'],'hidden')!==false)throw new MarketError(422,'Choose a visible item property.');
            $min=isset($p['min'])?self::number($p['min'],-1000000000,1000000000):null;$max=isset($p['max'])?self::number($p['max'],-1000000000,1000000000):null;
            if(($min===null&&$max===null)||($min!==null&&$max!==null&&$max<$min))throw new MarketError(422,'Choose a valid property range.');
            $c['stats'][]=['id'=>$id,'param'=>isset($p['param'])?self::number($p['param'],0,4294967295):null,'min'=>$min,'max'=>$max];
        }
        if($c['cap']!==null){$cap=$c['cap'];if(!is_array($cap)||!in_array($cap['code']??'', ['gld','r33'],true))throw new MarketError(422,'Choose a Gold or Zod price limit.');$c['cap']=['code'=>$cap['code'],'quantity'=>self::number($cap['quantity']??null,1,2000000000)];}
        return $c;
    }
    private function searchProperty(array $item,array $p,?array $catalog=null): int|float|null {
        if(isset($item['identified'])&&!$item['identified'])return null;
        $rows=$item['display']['saved_properties']??[];$found=false;$value=0;
        foreach($rows as $r)if($r['id']===$p['id']&&($r['list']??'base')!=='set'&&($p['param']===null||($r['param']??0)===$p['param'])){$found=true;$value+=(int)$r['value'];}
        // Root save lists omit jewel bonuses. Compact rune/gem effects require
        // runtime context, so only exact saved modifier rows participate.
        foreach($item['display']['socketed_items']??[] as $child){$v=$this->searchProperty($child,$p);if($v!==null){$found=true;$value+=$v;}}
        if(!$found)return null;$d=$catalog['stats'][$p['id']]??[];if(($d['func']??0)===5)$value=(int)floor($value*100/128);if(($d['opBase']??'')==='level')$value=$value/(2**$d['opParam']);return $value;
    }
    // Keep these text rules aligned with the browser's searchWords helper.
    private static function searchWords(string $value): array {
        $value=strtolower(str_replace(["’", "'", '`', "\u{200b}", "\u{200c}", "\u{200d}", "\u{feff}"],'',$value));
        $value=preg_replace('/[^\p{L}\p{N}]+/u',' ',$value);
        $value=preg_replace('/\b(rune|ring|gem|jewel|charm|amulet|skull)s\b/','$1',$value);
        return preg_split('/\s+/u',trim($value),-1,PREG_SPLIT_NO_EMPTY);
    }
    private static function searchTextMatches(string $text,string $query): bool {
        $words=self::searchWords($query);$meaningful=array_values(array_diff($words,['a','an','the','of']));$haystack=implode(' ',self::searchWords($text));
        foreach($meaningful?:$words as $word)if(!str_contains($haystack,$word))return false;return true;
    }
    private function searchMatches(array $r,array $c,string $owner,array $catalog,string $scope): bool {
        if($r['seller_id']===$owner&&$c['hideOwn']||$c['ready']&&!$r['purchasable']||$c['free']&&!self::isFree($r['price'])||$c['seller']!==''&&stripos($r['seller'].' '.($r['seller_player_id']??$this->publicPlayerId($r['seller_id'])),$c['seller'])===false||$c['hideOffers']&&$r['other_offer_count'])return false;
        if($c['cap']!==null&&!self::isFree($r['price'])){if(count($r['price'])!==1||$r['price'][0]['code']!==$c['cap']['code']||$r['price'][0]['quantity']>$c['cap']['quantity'])return false;}
        if(($c['affordable']??false)&&$this->allocation($owner,$scope,$r['price'])===null)return false;
        if(($c['favorites']??'all')!=='all'&&!$this->run('SELECT target FROM market_favorites WHERE owner=? AND scope=? AND kind=? AND target=?',[$owner,$scope,$c['favorites']==='trades'?'listing':'seller',$c['favorites']==='trades'?$r['id']:$r['seller_id']])->fetchColumn())return false;
        $physical=function(array $item)use($c,$catalog){
            if($c['ethereal']!==-1&&(!isset($item['ethereal'])||(int)$item['ethereal']!==$c['ethereal']))return false;
            $sockets=(int)($item['sockets']??0);foreach($item['display']['saved_properties']??[] as $p)if($p['id']===194)$sockets=max($sockets,(int)$p['value']);
            if($sockets<$c['sockets_min']||$sockets>$c['sockets_max'])return false;
            foreach($c['stats'] as $p){$v=$this->searchProperty($item,$p,$catalog);if($v===null||$p['min']!==null&&$v<$p['min']||$p['max']!==null&&$v>$p['max'])return false;}return true;
        };
        if(!array_filter($r['members']??[$r['item']],$physical))return false;
        $match=function(array $item,string $label='')use($c,$catalog,$r){
            $b=$catalog['bases'][$item['code']]??[];
            if($c['quality']!==''&&(int)$c['quality']!==$item['quality']||$c['category']!==''&&$c['category']!==($b['category']??''))return false;
            $name=$this->notificationItemName(['code'=>$item['code'],'quality'=>$item['quality'],'item_index'=>$item['index'],'item_hex'=>$item['_hex']??'','name_ids'=>$item['name_ids']??[]]);
            if($c['q']!==''&&!self::searchTextMatches(implode(' ',[$name,$b['name']??'',$item['code'],$b['type']??'',($item['class_id']??-1)>=0?($catalog['classes'][$item['class_id']]??''):'',$label,$r['bundle_title']??'']),$c['q'])&&!self::searchTextMatches($r['seller'],$c['q']))return false;
            return true;
        };
        foreach($r['members']??[$r['item']] as $item)if($c['searchScope']!=='wanted'&&$match($item))return true;
        foreach($r['price'] as $p)if($c['searchScope']!=='offered'&&$match($p,$p['label']??''))return true;
        return false;
    }
    // Request-based hosting needs no daemon. Website and companion inbox polls
    // evaluate new listing revisions, even while the player is only in game.
    public function evaluateSearches(array $actor,bool $force=false,bool $baseline=false,?string $only=null): void {
        if(!$this->run('SELECT id FROM market_searches WHERE owner=? AND enabled=1 LIMIT 1',[$actor['id']])->fetchColumn())return;
        $this->transaction(function()use($actor,$force,$baseline,$only){
            if($this->controlState()['phase']!=='live')return;
            $owner=$actor['id'];$this->run('SELECT id FROM actors WHERE id=?'.$this->lock(),[$owner])->fetch();
            $checked=(int)$this->run('SELECT checked FROM market_search_poll WHERE owner=?',[$owner])->fetchColumn();if(!$force&&$checked>time()-15)return;
            $this->run('DELETE FROM market_search_poll WHERE owner=?',[$owner]);$this->run('INSERT INTO market_search_poll VALUES (?,?)',[$owner,time()]);
            $searches=$this->run('SELECT * FROM market_searches WHERE owner=? AND enabled=1'.($only?' AND id=?':''),$only?[$owner,$only]:[$owner])->fetchAll();$feeds=[];
            foreach($searches as $s){$parts=explode('|',$s['scope']);if(count($parts)!==4||$parts[0]!==$this->community)continue;$selected=$this->selectMod($parts[1],'2.4');$catalog=$selected->browserCatalog();
                if(!isset($feeds[$s['scope']])){$offerCounts=[];foreach($this->run("SELECT o.listing,COUNT(*) AS n FROM offers o JOIN listings l ON l.id=o.listing JOIN items i ON i.id=l.item WHERE i.scope=? AND o.buyer<>? AND o.status IN ('pending','accepted') GROUP BY o.listing",[$s['scope'],$owner])->fetchAll() as $count)$offerCounts[$count['listing']]=(int)$count['n'];$feed=[];$rows=$this->run("SELECT l.*,i.code,i.quality,i.item_index,i.ethereal,i.sockets,i.item_hex,e.metadata,d.metadata AS discovery,a.name AS seller_name,e.state FROM listings l JOIN items i ON i.id=l.item JOIN actors a ON a.id=l.seller LEFT JOIN escrow_items e ON e.item=i.id LEFT JOIN web_item_display d ON d.item=i.id LEFT JOIN published_items p ON p.listing=l.id WHERE i.scope=? AND l.status='open' AND (p.listing IS NULL OR p.expires>?) ORDER BY l.created DESC,l.id LIMIT 4096",[$s['scope'],time()])->fetchAll();
                    foreach($rows as $r){$m=$r['metadata']?json_decode($r['metadata'],true,512,JSON_THROW_ON_ERROR):[];$item=['code'=>$r['code'],'quality'=>(int)$r['quality'],'index'=>(int)$r['item_index'],'ethereal'=>(int)$r['ethereal'],'sockets'=>(int)$r['sockets'],'identified'=>$m['identified']??true,'display'=>$m['display']??($r['discovery']?json_decode($r['discovery'],true,512,JSON_THROW_ON_ERROR):[]),'_hex'=>$r['item_hex']];
                        $feed[]=['id'=>$r['id'],'revision'=>(int)$r['revision'],'seller_id'=>$r['seller'],'seller'=>$r['seller_name'],'price'=>json_decode($r['price'],true,512,JSON_THROW_ON_ERROR),'item'=>$item,'purchasable'=>$r['state']==='listed','other_offer_count'=>$offerCounts[$r['id']]??0];}
                    $titles=$this->run('SELECT b.listing,b.title FROM market_bundles b JOIN listings l ON l.id=b.listing JOIN items i ON i.id=l.item WHERE i.scope=?',[$s['scope']])->fetchAll(PDO::FETCH_KEY_PAIR);foreach($feed as &$entry){$title=$titles[$entry['id']]??null;if($title){$entry['bundle_title']=$title;$entry['members']=array_map(fn($id)=>$selected->statsItem($id),$this->bundleGoods($entry['id'],''));}}unset($entry);
                    $feeds[$s['scope']]=$feed;
                }
                $this->run("DELETE FROM market_search_hits WHERE search=? AND listing IN (SELECT id FROM listings WHERE status NOT IN ('open','agreed'))",[$s['id']]);
                $c=json_decode($s['criteria'],true,512,JSON_THROW_ON_ERROR);$hits=[];$seen=[];foreach($this->run('SELECT listing,revision FROM market_search_hits WHERE search=?',[$s['id']])->fetchAll() as $hit)$seen[$hit['listing'].'|'.$hit['revision']]=true;
                foreach($feeds[$s['scope']] as $r){if(isset($seen[$r['id'].'|'.$r['revision']])||$this->communicationBlocked($owner,$r['seller_id'])||!$selected->searchMatches($r,$c,$owner,$catalog,$s['scope']))continue;
                    $this->run('INSERT INTO market_search_hits VALUES (?,?,?)',[$s['id'],$r['id'],$r['revision']]);$hits[]=$r;}
                if($hits&&!$baseline){$first=$hits[0];$id=self::id();$body='Saved search “'.$s['name'].'”: '.count($hits).' new matching '.(count($hits)===1?'trade':'trades').'. '.$selected->notificationItemName(['code'=>$first['item']['code'],'quality'=>$first['item']['quality'],'item_index'=>$first['item']['index'],'item_hex'=>$first['item']['_hex']]);
                    $this->run('INSERT INTO market_messages (id,sender,recipient,scope,kind,thread,body,created) VALUES (?,?,?,?,?,?,?,?)',[$id,$first['seller_id'],$owner,$s['scope'],'trade','',$body,time()]);$this->run('INSERT INTO market_search_notices VALUES (?,?,?)',[$id,$first['id'],$s['id']]);}
            }
        });
    }
    private function experienceCommand(array $actor,array $data,bool $hc): array {
        return $this->transaction(function()use($actor,$data,$hc){
            $this->lockScope($hc);$owner=$actor['id'];$scope=$this->scope($hc);$this->run('SELECT id FROM actors WHERE id=?'.$this->lock(),[$owner])->fetch();$id=self::ticketId($data['id']??null);$hash=hash('sha256',$scope.'|'.self::json($data));
            $old=$this->run('SELECT hash,result FROM commands WHERE actor=? AND id=?',[$owner,$id])->fetch();if($old){if(!hash_equals($old['hash'],$hash))throw new MarketError(409,'Command ID reused.');return json_decode($old['result'],true,512,JSON_THROW_ON_ERROR);}
            switch($data['type']){
                case 'search_save':
                    if((int)$this->run('SELECT COUNT(*) FROM market_searches WHERE owner=?',[$owner])->fetchColumn()>=12)throw new MarketError(422,'Keep up to twelve saved searches. Remove one first.');$name=trim(self::text($data['name']??null,80));if($name==='')throw new MarketError(422,'Name your saved search.');$criteria=$this->searchCriteria($data['criteria']??[]);if(!is_bool($data['enabled']??null))throw new MarketError(422,'Choose search alerts on or off.');
                    $this->run('INSERT INTO market_searches VALUES (?,?,?,?,?,?,?)',[$id,$owner,$scope,$name,self::json($criteria),(int)$data['enabled'],time()]);$this->evaluateSearches($actor,true,true,$id);$result=['search_id'=>$id];break;
                case 'search_toggle':
                    $search=self::ticketId($data['search_id']??null);$current=$this->run('SELECT enabled FROM market_searches WHERE id=? AND owner=? AND scope=?',[$search,$owner,$scope])->fetchColumn();if($current===false)throw new MarketError(404,'Saved search not found.');if(!is_bool($data['expected_enabled']??null)||(bool)$current!==$data['expected_enabled'])throw new MarketError(409,'Search alerts changed. Refresh before trying again.');$enabled=!(bool)$current;$this->run('UPDATE market_searches SET enabled=? WHERE id=?',[(int)$enabled,$search]);if($enabled)$this->evaluateSearches($actor,true,true,$search);$result=['enabled'=>$enabled];break;
                case 'search_delete':
                    $search=self::ticketId($data['search_id']??null);if(!$this->run('SELECT id FROM market_searches WHERE id=? AND owner=? AND scope=?',[$search,$owner,$scope])->fetchColumn())throw new MarketError(404,'Saved search not found.');$this->run('DELETE FROM market_search_hits WHERE search=?',[$search]);$this->run('DELETE FROM market_searches WHERE id=?',[$search]);$result=['removed'=>true];break;
                case 'notification_settings':
                    $settings=$this->notificationSettings($owner);foreach(array_keys($settings) as $key){if(!is_bool($data[$key]??null))throw new MarketError(422,'Choose notification options on or off.');$settings[$key]=$data[$key];}$this->run('DELETE FROM market_notifications WHERE owner=?',[$owner]);$this->run('INSERT INTO market_notifications VALUES (?,?)',[$owner,self::json($settings+['search_opt_in'=>true])]);$result=$settings;break;
                case 'player_control':
                    $target=self::text($data['target']??null,64);if($target===$owner||!$this->run('SELECT id FROM actors WHERE id=?',[$target])->fetchColumn())throw new MarketError(422,'Choose another player.');foreach(['muted','blocked'] as $k)if(!is_bool($data[$k]??null))throw new MarketError(422,'Choose player options on or off.');
                    $this->run('DELETE FROM market_player_controls WHERE owner=? AND target=?',[$owner,$target]);if($data['muted']||$data['blocked'])$this->run('INSERT INTO market_player_controls VALUES (?,?,?,?)',[$owner,$target,(int)$data['muted'],(int)$data['blocked']]);$result=['saved'=>true];break;
                case 'activity_read':
                    $max=(int)$this->run('SELECT MAX(seq) FROM events')->fetchColumn();$through=self::number($data['through']??null,0,$max);$prior=(int)$this->run('SELECT through_seq FROM market_activity_reads WHERE owner=? AND scope=?',[$owner,$scope])->fetchColumn();$this->run('DELETE FROM market_activity_reads WHERE owner=? AND scope=?',[$owner,$scope]);$this->run('INSERT INTO market_activity_reads VALUES (?,?,?)',[$owner,$scope,max($prior,$through)]);$result=['through'=>max($prior,$through)];break;
                default:throw new MarketError(422,'Unknown marketplace option.');
            }
            $this->run('INSERT INTO commands VALUES (?,?,?,?)',[$owner,$id,$hash,self::json($result)]);return $result;
        });
    }
    private function experienceSnapshot(array $actor,bool $hc): array {
        $owner=$actor['id'];$scope=$this->scope($hc);$searches=$this->run('SELECT id,name,criteria,enabled FROM market_searches WHERE owner=? AND scope=? ORDER BY created,id',[$owner,$scope])->fetchAll();foreach($searches as &$s){$s['criteria']=json_decode($s['criteria'],true,512,JSON_THROW_ON_ERROR);$s['enabled']=(bool)$s['enabled'];}unset($s);
        $controls=$this->run('SELECT p.target,a.name,p.muted,p.blocked FROM market_player_controls p JOIN actors a ON a.id=p.target WHERE p.owner=?',[$owner])->fetchAll();foreach($controls as &$c){$c['muted']=(bool)$c['muted'];$c['blocked']=(bool)$c['blocked'];}unset($c);
        return ['companion_seen'=>$this->companionSeen($owner,$scope),'searches'=>$searches,'notifications'=>$this->notificationSettings($owner),'players'=>$controls];
    }
    public function activity(array $actor,bool $hc): array {
        $owner=$actor['id'];$scope=$this->scope($hc);$read=(int)$this->run('SELECT through_seq FROM market_activity_reads WHERE owner=? AND scope=?',[$owner,$scope])->fetchColumn();
        $sales=$this->run("SELECT e.seq,e.item,e.at,e.price,e.seller,e.buyer,e.offer,e.receipt,o.listing FROM events e JOIN items i ON i.id=e.item LEFT JOIN offers o ON o.id=e.offer WHERE i.scope=? AND e.kind='completed_sale' AND (e.seller=? OR e.buyer=?) ORDER BY e.seq DESC LIMIT 100",[$scope,$owner,$owner])->fetchAll();$summary=['sales'=>0,'purchases'=>0,'offers'=>0,'deliveries'=>0];
        foreach($sales as &$r){$r['price']=json_decode($r['price'],true,512,JSON_THROW_ON_ERROR);$r['at']=(int)$r['at'];$r['item_details']=$this->statsItem($r['item']);$members=$r['listing']?$this->bundleGoods($r['listing'],$r['item']):[$r['item']];$r['item_count']=count($members);$delivered=0;$delivering=0;$owned=0;$requested=0;$relisted=0;
            foreach($members as $item){if($this->run("SELECT id FROM events WHERE item=? AND actor=? AND kind='delivered' AND seq>? LIMIT 1",[$item,$r['buyer'],$r['seq']])->fetchColumn())++$delivered;$e=$this->run('SELECT owner,state,metadata FROM escrow_items WHERE item=?',[$item])->fetch();if($e&&$e['owner']===$owner){++$owned;if($e['state']==='claiming')++$delivering;if($e['state']==='listed')++$relisted;if((json_decode($e['metadata'],true,512,JSON_THROW_ON_ERROR)['delivery_requested']??false)===true)++$requested;}}
            $r['progress']=$r['seller']===$owner?'sold':($delivered===count($members)?'delivered':($delivering?'syncing':($relisted?'relisted':($requested?'ready':($owned?'in_escrow':'transferred')))));$r['delivered_count']=$delivered;if($r['seq']>$read)++$summary[$r['seller']===$owner?'sales':'purchases'];unset($r['seq']);
        }unset($r);
        $unreadSales=$this->run("SELECT SUM(CASE WHEN e.seller=? THEN 1 ELSE 0 END) AS sales,SUM(CASE WHEN e.buyer=? THEN 1 ELSE 0 END) AS purchases FROM events e JOIN items i ON i.id=e.item WHERE i.scope=? AND e.kind='completed_sale' AND e.seq>? AND (e.seller=? OR e.buyer=?)",[$owner,$owner,$scope,$read,$owner,$owner])->fetch();$summary['sales']=(int)$unreadSales['sales'];$summary['purchases']=(int)$unreadSales['purchases'];
        $summary['offers']=(int)$this->run("SELECT COUNT(*) FROM offers o JOIN listings l ON l.id=o.listing JOIN items i ON i.id=l.item WHERE i.scope=? AND o.status='pending' AND o.proposer<>? AND (o.buyer=? OR o.seller=?)",[$scope,$owner,$owner,$owner])->fetchColumn();
        $deliveries=[];foreach($this->run("SELECT item,state,metadata FROM escrow_items WHERE owner=? AND scope=? AND state IN ('available','claiming')",[$owner,$scope])->fetchAll() as $row)if((json_decode($row['metadata'],true,512,JSON_THROW_ON_ERROR)['delivery_requested']??false)===true)$deliveries[]=['id'=>$row['item'],'progress'=>$row['state']==='claiming'?'syncing':'ready','item_details'=>$this->statsItem($row['item'])];$summary['deliveries']=count($deliveries);
        $withdrawals=array_values(array_filter($this->walletRows($owner,$scope),fn($r)=>in_array($r['status'],['queued','delivering'],true)));
        $agreements=$this->run("SELECT o.id,o.price,l.item FROM offers o JOIN listings l ON l.id=o.listing JOIN items i ON i.id=l.item WHERE i.scope=? AND o.status='accepted' AND (o.seller=? OR o.buyer=?) ORDER BY o.created DESC LIMIT 100",[$scope,$owner,$owner])->fetchAll();foreach($agreements as &$agreement){$agreement['price']=json_decode($agreement['price'],true,512,JSON_THROW_ON_ERROR);$agreement['item_details']=$this->statsItem($agreement['item']);}unset($agreement);
        $events=$this->run("SELECT e.seq,e.id,e.at,e.kind,e.item,e.offer,e.message,e.price FROM events e JOIN items i ON i.id=e.item WHERE i.scope=? AND (e.seller=? OR e.buyer=? OR e.actor=? OR e.offer IN (SELECT id FROM offers WHERE seller=? OR buyer=?)) AND e.kind<>'bundle_sale' ORDER BY e.seq DESC LIMIT 100",[$scope,$owner,$owner,$owner,$owner,$owner])->fetchAll();foreach($events as &$r){$r['price']=json_decode($r['price'],true,512,JSON_THROW_ON_ERROR);$r['at']=(int)$r['at'];$r['unread']=(int)$r['seq']>$read;unset($r['seq']);}unset($r);
        return ['companion_seen'=>$this->companionSeen($owner,$scope),'summary'=>$summary,'deliveries'=>$deliveries,'withdrawals'=>$withdrawals,'agreements'=>$agreements,'sales'=>$sales,'events'=>$events,'through'=>(int)$this->run('SELECT MAX(seq) FROM events')->fetchColumn(),'read_through'=>$read];
    }
}
