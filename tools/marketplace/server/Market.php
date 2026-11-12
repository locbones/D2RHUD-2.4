<?php
declare(strict_types=1);
require_once __DIR__.'/Publication.php';
require_once __DIR__.'/Escrow.php';
require_once __DIR__.'/Browser.php';
require_once __DIR__.'/Trading.php';
require_once __DIR__.'/Social.php';
require_once __DIR__.'/ItemIdentity.php';
require_once __DIR__.'/Wallet.php';
require_once __DIR__.'/Players.php';
require_once __DIR__.'/Transfers.php';
require_once __DIR__.'/GiftCampaigns.php';
require_once __DIR__.'/ModScopes.php';
require_once __DIR__.'/Moderation.php';
require_once __DIR__.'/OwnerTools.php';
require_once __DIR__.'/OwnerAudit.php';
require_once __DIR__.'/TradeStats.php';
require_once __DIR__.'/Bundles.php';
require_once __DIR__.'/Experience.php';
require_once __DIR__.'/Enrollment.php';
require_once __DIR__.'/Compatibility.php';
require_once __DIR__.'/OwnerImpersonation.php';
require_once __DIR__.'/ListingComments.php';

// Request-based PHP/PDO service. Acceptance settles only against actual escrow;
// an external fulfilment operator uses a separate, restricted audit endpoint.
final class MarketError extends RuntimeException {
    public function __construct(public int $status, string $message) { parent::__construct($message); }
}
final class Market {
    use MarketplacePublication;
    use MarketplaceEscrow;
    use MarketplaceBrowser;
    use MarketplaceTrading;
    use MarketplaceSocial;
    use MarketplaceIdentity;
    use MarketplaceWallet;
    use MarketplacePlayers;
    use MarketplaceTransfers;
    use MarketplaceGiftCampaigns;
    use MarketplaceModScopes;
    use MarketplaceModeration;
    use MarketplaceOwnerTools;
    use MarketplaceOwnerAudit;
    use MarketplaceTradeStats;
    use MarketplaceBundles;
    use MarketplaceExperience;
    use MarketplaceEnrollment;
    use MarketplaceCompatibility;
    use MarketplaceOwnerImpersonation;
    use MarketplaceListingComments;
    public function __construct(public PDO $db, public string $community='tcpip', public string $mod='RMD-MP') {
        $db->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
        $db->setAttribute(PDO::ATTR_DEFAULT_FETCH_MODE, PDO::FETCH_ASSOC);
    }
    public static function id(): string { return bin2hex(random_bytes(16)); }
    public static function text(mixed $s, int $max=128): string {
        if (!is_string($s) || $s==='' || strlen($s)>$max || !preg_match('//u',$s) || preg_match('/[\x00-\x1f\x7f]/',$s)) throw new MarketError(422,'Invalid text.');
        return $s;
    }
    public static function number(mixed $n,int $min,int $max): int {
        if (!is_int($n)||$n<$min||$n>$max) throw new MarketError(422,'Invalid number.'); return $n;
    }
    public static function message(mixed $value): string {
        if(!is_string($value)||$value===''||strlen($value)>2048||!preg_match('//u',$value)||preg_match('/[\x00-\x08\x0b\x0c\x0e-\x1f\x7f]/',$value))throw new MarketError(422,'Messages support up to 2,048 UTF-8 bytes.');
        return $value;
    }
    public static function price(mixed $rows): array {
        if(!is_array($rows)||!array_is_list($rows)||count($rows)<1||count($rows)>16) throw new MarketError(422,'Choose 1 to 16 payment types.');
        if(count($rows)===1&&in_array($rows[0]['kind']??'', ['open','free'],true)&&($rows[0]['class_id']??-1)!==-1)throw new MarketError(422,'Only a Hellfire Torch can specify a class.');
        if(count($rows)===1&&($rows[0]['kind']??'')==='open'){
            $r=$rows[0];if(($r['code']??null)!=='open'||($r['quality']??null)!==2||($r['index']??null)!==-1||($r['ethereal']??null)!==-1||($r['quantity']??null)!==1||($r['stat']??'')!=='')throw new MarketError(422,'Invalid open-ended listing.');return self::openPrice();
        }
        if(count($rows)===1&&($rows[0]['kind']??'')==='free'){
            $r=$rows[0];if(($r['code']??null)!=='free'||($r['quality']??null)!==2||($r['index']??null)!==-1||($r['ethereal']??null)!==-1||($r['quantity']??null)!==1||($r['stat']??'')!=='')throw new MarketError(422,'Invalid free listing.');
            return self::freePrice();
        }
        $symbols=['GB_Total','RB_Low','RB_Mid','RB_High','RB_Ultra','JB_Magic','JB_Rare','PB_Rejuv','KB_Terror','KB_Hate','KB_Dest','OB_Diablo','OB_Baal','OB_Meph','score_tracker','ucore_tracker'];
        $out=[];
        foreach($rows as $r) {
            if(!is_array($r)) throw new MarketError(422,'Invalid payment.');
            $kind=$r['kind']??'item'; $code=self::text($r['code']??null,4); $stat=$r['stat']??'';
            if(!preg_match('/^[\x21-\x7e]{1,4}$/D',$code)||!in_array($kind,['item','gold','bag'],true)) throw new MarketError(422,'Invalid payment identity.');
            $class=self::number($r['class_id']??-1,-1,6);if($class>=0&&($kind!=='item'||$code!=='cm2'||($r['quality']??null)!==7))throw new MarketError(422,'Choose a class for a unique Hellfire Torch.');
            $q=self::number($r['quality']??null,2,7);$index=self::number($r['index']??null,-1,1000000);$eth=self::number($r['ethereal']??null,-1,1);
            if(!in_array($q,[2,5,7],true)||($q===2?$index!==-1:$index<0)) throw new MarketError(422,'Invalid quality or index.');
            if($kind==='item'&&($code==='gld'||$stat!=='') || $kind!=='item'&&($q!==2||$index!==-1||$eth!==-1) ||
               $kind==='gold'&&($code!=='gld'||$stat!=='') || $kind==='bag'&&($code!=='Z01'||!in_array($stat,$symbols,true))) throw new MarketError(422,'Invalid balance.');
            $v=['code'=>$code,'label'=>self::text($r['label']??null,512),'quality'=>$q,'index'=>$index,
                'quantity'=>self::number($r['quantity']??null,1,$kind==='item'?1000000:2000000000),'ethereal'=>$eth];
            if($class>=0)$v['class_id']=$class;
            if($kind!=='item') $v['kind']=$kind;if($kind==='bag')$v['stat']=$stat;
            foreach($out as $o) {
                if($code==='Z01'&&$o['code']==='Z01'&&(($kind==='bag')!==(($o['kind']??'item')==='bag')))throw new MarketError(422,'Choose a bag or its contents, not both.');
                if($code===$o['code']&&$kind===($o['kind']??'item')&&$stat===($o['stat']??'')&&$q===$o['quality']&&$index===$o['index']&&($eth===-1||$o['ethereal']===-1||$eth===$o['ethereal'])&&($class===-1||($o['class_id']??-1)===-1||$class===$o['class_id']))throw new MarketError(422,'Combine overlapping payment lines.');
            }
            $out[]=$v;
        } return $out;
    }
    private static function hasClassPrice(array $rows): bool {return (bool)array_filter($rows,fn($r)=>($r['class_id']??-1)>=0);}
    private static function paymentClassMatches(array $r,array $metadata): bool {
        $wanted=$r['class_id']??-1;if($wanted<0)return true;
        $classes=[];foreach($metadata['display']['saved_properties']??[] as $p)if($p['id']===83&&($p['list']??'base')==='base'&&$p['value']>0){$class=$p['param']??0;if($class<0||$class>6)return false;$classes[$class]=true;}
        return count($classes)===1&&isset($classes[$wanted]);
    }
    public function run(string $sql,array $args=[]): PDOStatement { $s=$this->db->prepare($sql);$s->execute($args);return $s; }
    private function lock(): string {return $this->db->getAttribute(PDO::ATTR_DRIVER_NAME)==='mysql'?' FOR UPDATE':'';}
    public function transaction(callable $f): mixed {
        if($this->db->inTransaction())return $f();
        for($attempt=0;;$attempt++){
            try{$this->db->beginTransaction();$r=$f();$this->db->commit();return $r;}
            catch(Throwable $e){
                if($this->db->inTransaction())$this->db->rollBack();
                $retry=$e instanceof PDOException&&($e->getCode()==='40001'||in_array($e->errorInfo[1]??null,[1213,1205,5,6],true));
                if(!$retry)throw $e;
                if($attempt>=3)throw new MarketError(503,'The marketplace is busy. Retry the same request shortly.');
                // These callbacks contain database work only. A rollback removes
                // the failed attempt; command IDs make a later client retry safe.
                usleep(random_int(10000,30000)*(1+$attempt));
            }
        }
    }
    public function scope(bool $hardcore): string {return $this->community.'|'.$this->mod.'|2.4|'.($hardcore?'HC':'SC');}
    public function migrate(): void {
        $engine=$this->db->getAttribute(PDO::ATTR_DRIVER_NAME)==='mysql'?' ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_bin':'';
        foreach([
            'actors'=>'id VARCHAR(64) PRIMARY KEY, name VARCHAR(128) NOT NULL, token_hash CHAR(64) NOT NULL UNIQUE, role VARCHAR(16) NOT NULL',
            'items'=>'id CHAR(32) PRIMARY KEY, scope VARCHAR(255) NOT NULL, seed BIGINT NOT NULL, code VARCHAR(4) NOT NULL, quality INT NOT NULL, item_index INT NOT NULL, ethereal INT NOT NULL, sockets INT NOT NULL, owner VARCHAR(64) NOT NULL, item_hex MEDIUMTEXT NOT NULL',
            'listings'=>'id CHAR(32) PRIMARY KEY, item CHAR(32) NOT NULL, seller VARCHAR(64) NOT NULL, revision INT NOT NULL, status VARCHAR(16) NOT NULL, price TEXT NOT NULL, created BIGINT NOT NULL',
            'offers'=>'id VARCHAR(64) PRIMARY KEY, listing CHAR(32) NOT NULL, buyer VARCHAR(64) NOT NULL, seller VARCHAR(64) NOT NULL, revision INT NOT NULL, listing_revision INT NOT NULL, status VARCHAR(16) NOT NULL, proposer VARCHAR(64) NOT NULL, price TEXT NOT NULL, created BIGINT NOT NULL',
            'events'=>($engine?'seq BIGINT AUTO_INCREMENT PRIMARY KEY':'seq INTEGER PRIMARY KEY AUTOINCREMENT').', id CHAR(32) NOT NULL UNIQUE, item CHAR(32) NOT NULL, offer VARCHAR(64), at BIGINT NOT NULL, actor VARCHAR(64) NOT NULL, kind VARCHAR(32) NOT NULL, message TEXT NOT NULL, price TEXT NOT NULL, seller VARCHAR(64), buyer VARCHAR(64), receipt VARCHAR(128) UNIQUE',
            'commands'=>'actor VARCHAR(64) NOT NULL, id VARCHAR(64) NOT NULL, hash CHAR(64) NOT NULL, result TEXT NOT NULL, PRIMARY KEY(actor,id)',
            'inventory_sources'=>'owner VARCHAR(64) NOT NULL, scope VARCHAR(255) NOT NULL, revision INT NOT NULL, snapshot_id VARCHAR(64) NOT NULL, captured_at BIGINT NOT NULL, hash CHAR(64) NOT NULL, PRIMARY KEY(owner,scope)',
            'published_items'=>'owner VARCHAR(64) NOT NULL, scope VARCHAR(255) NOT NULL, source_key CHAR(64) NOT NULL, item CHAR(32) NOT NULL, listing CHAR(32) NOT NULL UNIQUE, tab INT NOT NULL, expires BIGINT NOT NULL, version INT NOT NULL, PRIMARY KEY(owner,scope,source_key)',
        ] as $name=>$columns)$this->db->exec("CREATE TABLE IF NOT EXISTS $name ($columns)$engine");
        $this->ownerSchema($engine);
        $this->ensureOwnerAuditSchema($engine);
        $this->escrowSchema($engine);
        $this->compatibilitySchema($engine);
        $this->walletSchema($engine);
        $this->browserSchema($engine);
        $this->tradingSchema($engine);
        $this->giftCampaignSchema($engine);
        $this->enrollmentSchema($engine);
        $this->bundleSchema($engine);
        $this->experienceSchema($engine);
        $this->socialSchema($engine);
        $this->listingCommentsSchema($engine);
        $this->playersSchema($engine);
        $this->modSchema($engine);
        $this->identitySchema($engine);
        $this->moderationSchema($engine);
        // Index creation is a migration action only; never issue DDL inside request transactions.
        foreach(['wanted_feed'=>'market_wanted(scope,status,created)','wanted_responses'=>'market_wanted_offers(wanted,offer)','trade_assets_receipt'=>'market_trade_assets(receipt,seq)','trade_assets_recipient'=>'market_trade_assets(item,recipient,seq)','searches_owner'=>'market_searches(owner,scope)','bundle_members_item'=>'market_bundle_items(item)','player_controls_target'=>'market_player_controls(target,owner)','items_scope_seed'=>'items(scope,seed,code)','offers_parties'=>'offers(buyer,seller,created)','events_item'=>'events(item,at)','events_offer'=>'events(offer,at)','identity_scope'=>'item_identities(scope,identity_key)','public_offers_listing'=>'market_public_offers(listing,seq)','offers_listing'=>'offers(listing,status,buyer)','wallet_owner_queue'=>'wallet_withdrawals(owner,scope,status,created)','wallet_owner_entries'=>'wallet_entries(owner,scope,seq)','messages_inbox'=>'market_messages(recipient,seq)','messages_sender'=>'market_messages(sender,created)','report_queue'=>'market_reports(scope,status,seq)','report_rate'=>'market_reports(reporter,created)','report_duplicate'=>'market_reports(reporter,dedupe_key,status)','report_audit'=>'market_report_events(report,seq)'] as $name=>$target){
            try{$this->db->exec("CREATE INDEX $name ON $target");}catch(PDOException $e){$state=$e->errorInfo[1]??0;if(!in_array($state,[1061,1],true))throw $e;}
        }
    }
    public function user(string $name,string $role='player'): array {
        self::text($name);if(!in_array($role,['player','admin','settler','tester'],true))throw new MarketError(422,'Invalid role.');
        $token=bin2hex(random_bytes(32));$id=self::id();$this->run('INSERT INTO actors VALUES (?,?,?,?)',[$id,$name,hash('sha256',$token),$role]);
        $this->publicPlayerId($id);$this->ensureMod($this->mod);if(in_array($role,['player','tester'],true))$this->joinMod($id);
        return ['id'=>$id,'name'=>$name,'role'=>$role,'token'=>$token];
    }
    public function auth(string $token): array {
        if(!preg_match('/^[0-9a-f]{64}$/D',$token))throw new MarketError(401,'Authentication required.');
        $a=$this->run('SELECT id,name,role FROM actors WHERE token_hash=?',[hash('sha256',$token)])->fetch();if(!$a)throw new MarketError(401,'Authentication required.');return $a;
    }
    private function event(string $item,?string $offer,array $actor,string $kind,string $message,array $price=[],?string $seller=null,?string $buyer=null,?string $receipt=null): void {
        $id=self::id();$this->run('INSERT INTO events (id,item,offer,at,actor,kind,message,price,seller,buyer,receipt) VALUES (?,?,?,?,?,?,?,?,?,?,?)',[$id,$item,$offer,time(),$actor['id'],$kind,$message,json_encode($price,JSON_THROW_ON_ERROR),$seller,$buyer,$receipt]);
        if($offer)$this->notifyTrade($id,$offer,$actor,$kind,$message,$price);
        elseif($kind==='delivered')$this->notifyTradeDelivery($id,$item,$actor);
    }
    public function listing(array $actor,array $data,bool $hardcore): array {
        if($actor['role']!=='admin')throw new MarketError(403,'Only an operator can register imported inventory.');
        return $this->transaction(function()use($actor,$data,$hardcore){
            $this->lockScope($hardcore);
            $owner=self::text($data['owner']??null,64);if(!$this->run('SELECT id FROM actors WHERE id=?',[$owner])->fetch())throw new MarketError(422,'Unknown owner.');
            if(isset($data['item_id'])){
                $item=self::text($data['item_id'],32);$i=$this->run('SELECT * FROM items WHERE id=?'.$this->lock(),[$item])->fetch();
                if(!$i||$i['scope']!==$this->scope($hardcore)||$i['owner']!==$owner)throw new MarketError(409,'Existing item ownership does not match.');
                if($this->run("SELECT id FROM listings WHERE item=? AND status IN ('open','agreed')",[$item])->fetch())throw new MarketError(409,'Item already has an active listing.');
            }else{
            $i=$data['item']??[];$seed=self::number($i['seed']??null,0,4294967295);$code=self::text($i['code']??null,4);$q=self::number($i['quality']??null,1,9);$index=self::number($i['index']??-1,-1,1000000);
            $eth=self::number($i['ethereal']??0,0,1);$sockets=self::number($i['sockets']??0,0,6);$hex=self::text($i['item_hex']??null,65536);
            if(strlen($hex)%2||!preg_match('/^[0-9a-f]+$/D',$hex))throw new MarketError(422,'Invalid serialized item.');
            $item=self::id();
            // Seed is searchable, not UNIQUE: collisions/cloned seeds require
            // separate ledger instances and must never merge ownership chains.
            $this->run('INSERT INTO items VALUES (?,?,?,?,?,?,?,?,?,?)',[$item,$this->scope($hardcore),$seed,$code,$q,$index,$eth,$sockets,$owner,$hex]);
            $this->recordIdentity($item,$this->scope($hardcore),$hex);
            }
            $price=self::price($data['price']??null);$listing=self::id();
            $this->run('INSERT INTO listings VALUES (?,?,?,?,?,?,?)',[$listing,$item,$owner,1,'open',json_encode($price,JSON_THROW_ON_ERROR),time()]);
            $this->event($item,null,$actor,'listed','Inventory imported; no Vault or sale implied.',$price,$owner);
            return ['listing_id'=>$listing,'item_id'=>$item,'revision'=>1];
        });
    }
    private function offer(string $id,array $actor): array {
        $o=$this->run('SELECT * FROM offers WHERE id=?',[$id])->fetch();if(!$o)throw new MarketError(404,'Offer not found.');
        if($o['buyer']!==$actor['id']&&$o['seller']!==$actor['id'])throw new MarketError(403,'This conversation is private.');return $o;
    }
    public function command(array $actor,array $data,bool $hardcore): array {
        if(in_array($data['type']??'',['report_player','review_report'],true))return $this->moderationCommand($actor,$data,$hardcore);
        if(!in_array($data['type']??'',['direct_message','message_read','profile_name'],true))$this->requireMod($actor['id']);
        if(in_array($data['type']??'',['wanted_create','wanted_close','wanted_respond'],true))return $this->wantedCommand($actor,$data,$hardcore);
        if(in_array($data['type']??'',['bundle_create','bundle_dissolve'],true))return $this->bundleCommand($actor,$data,$hardcore);
        if(in_array($data['type']??'',['search_save','search_delete','search_toggle','notification_settings','player_control','activity_read'],true))return $this->experienceCommand($actor,$data,$hardcore);
        if(in_array($data['type']??'',['gift_item','gift_balances','gift_recipient'],true))return $this->giftCommand($actor,$data,$hardcore);
        if(in_array($data['type']??'', ['direct_message','message_read'],true))return $this->playerCommand($actor,$data);
        if(in_array($data['type']??'', ['wallet_withdraw','wallet_cancel'],true))return $this->walletCommand($actor,$data,$hardcore);
        if(($data['type']??'')==='campaign_claim')return $this->claimGiftCampaign($actor,$data,$hardcore);
        if(in_array($data['type']??'', ['profile_name','favorite','offer_page'],true))return $this->socialCommand($actor,$data,$hardcore);
        if(str_starts_with($data['type']??'','web_'))return $this->browserCommand($actor,$data,$hardcore);
        if(($data['type']??'')==='withdraw_physical')return $this->withdrawPhysical($actor,$data,$hardcore);
        if(in_array($data['type']??'', ['preferences','escrow_price','escrow_return'],true))return $this->escrowCommand($actor,$data,$hardcore);
        $id=self::text($data['id']??null,64);$type=$data['type']??'';$hash=hash('sha256',json_encode($data,JSON_THROW_ON_ERROR));
        return $this->transaction(function()use($actor,$data,$hardcore,$id,$type,$hash){
            $this->lockScope($hardcore);
            // Serialize this actor's requests, then the listing, then its offer.
            $this->run('SELECT id FROM actors WHERE id=?'.$this->lock(),[$actor['id']])->fetch();
            $old=$this->run('SELECT hash,result FROM commands WHERE actor=? AND id=?',[$actor['id'],$id])->fetch();
            if($old){if(!hash_equals($old['hash'],$hash))throw new MarketError(409,'Idempotency key reused for different content.');return json_decode($old['result'],true,512,JSON_THROW_ON_ERROR);}
            $offer=$type==='propose'?null:$this->offer(self::text($data['offer_id']??null,64),$actor);
            $listingId=$offer['listing']??self::text($data['listing_id']??null,64);
            $l=$this->run('SELECT l.*,i.scope,i.owner FROM listings l JOIN items i ON i.id=l.item WHERE l.id=?'.$this->lock(),[$listingId])->fetch();
            if(!$l||$l['scope']!==$this->scope($hardcore))throw new MarketError(404,'Listing not found in this game mode.');
            $message=$data['message']??'';if($message!==''){self::message($message);$peer=$actor['id']===$l['seller']?($offer['buyer']??''):$l['seller'];if($peer&&$this->communicationBlocked($actor['id'],$peer))throw new MarketError(403,'Messaging is unavailable between these players.');}
            if($type==='propose'){
                if($l['seller']===$actor['id'])throw new MarketError(422,'You cannot bid on your own listing.');
                if(!$this->supportsEscrowItem($actor['id'],$l['item']))throw new MarketError(409,'Update this mod before receiving this item: your installed version does not contain all of its saved definitions.');
                if((int)$this->run("SELECT COUNT(*) FROM offers o JOIN listings a ON a.id=o.listing JOIN items i ON i.id=a.item WHERE o.buyer=? AND i.scope=? AND o.status IN ('pending','accepted')",[$actor['id'],$this->scope($hardcore)])->fetchColumn()>=256)throw new MarketError(422,'Review or withdraw existing offers before opening more conversations.');
                if($l['status']!=='open'||!$this->publishedFresh($l['id'])||$l['owner']!==$l['seller']||(int)$l['revision']!==self::number($data['listing_revision']??null,1,2147483647))throw new MarketError(409,'Listing changed or expired. Refresh before proposing.');
                $price=self::paymentPrice($data['price']??null);
                if(self::isFree(json_decode($l['price'],true,512,JSON_THROW_ON_ERROR))){
                    if(!self::isFree($price))throw new MarketError(422,'Free listings are claimed without payment.');
                    if($actor['role']!=='player'||!$this->run("SELECT item FROM escrow_items WHERE item=? AND owner=? AND state='listed'",[$l['item'],$l['seller']])->fetchColumn())throw new MarketError(409,'Wait for this free item to reach Vault before claiming it.');
                }elseif(self::isFree($price)&&!self::isOpen(json_decode($l['price'],true,512,JSON_THROW_ON_ERROR)))throw new MarketError(422,'A paid listing requires a payment proposal.');
                $this->run('INSERT INTO offers VALUES (?,?,?,?,?,?,?,?,?,?)',[$id,$l['id'],$actor['id'],$l['seller'],1,(int)$l['revision'],'pending',$actor['id'],json_encode($price,JSON_THROW_ON_ERROR),time()]);
                $this->publicOfferRegister(['id'=>$id,'listing'=>$l['id'],'buyer'=>$actor['id']]);
                $this->event($l['item'],$id,$actor,'proposed',$message,$price);$offerId=$id;
            }else{
                $offer=$this->run('SELECT * FROM offers WHERE id=?'.$this->lock(),[$offer['id']])->fetch();$offerId=$offer['id'];
                // Chat replies append text only. They never accept or change
                // terms, so concurrent messages need no stale offer revision.
                if($type!=='trade_reply'&&(int)$offer['revision']!==self::number($data['expected_revision']??null,1,2147483647))throw new MarketError(409,'Conversation changed. Refresh before responding.');
                if(in_array($type,['message','trade_reply','counter'],true)&&(int)$this->run('SELECT COUNT(*) FROM events WHERE offer=?',[$offerId])->fetchColumn()>=500)throw new MarketError(422,'This conversation log is full. Keep it and start a new offer.');
                if(in_array($type,['message','trade_reply'],true)){
                    if($this->communicationBlocked($actor['id'],$actor['id']===$offer['buyer']?$offer['seller']:$offer['buyer']))throw new MarketError(403,'Messaging is unavailable between these players.');
                    if($message==='')throw new MarketError(422,'Write a message first.');
                    $this->event($l['item'],$offerId,$actor,'message',$message);
                }elseif(in_array($type,['accept','reject','withdraw','counter'],true)){
                    if($offer['status']!=='pending')throw new MarketError(409,'Offer is no longer pending.');
                    if($type==='withdraw'? $offer['proposer']!==$actor['id'] : $offer['proposer']===$actor['id'])throw new MarketError(403,'Only the other party can respond to this proposal.');
                    if($l['status']!=='open'||!$this->publishedFresh($l['id'])||$l['owner']!==$l['seller']||(int)$l['revision']!==(int)$offer['listing_revision'])throw new MarketError(409,'Listing changed or expired. Propose again from the current listing.');
                    $price=$type==='counter'?self::paymentPrice($data['price']??null):json_decode($offer['price'],true,512,JSON_THROW_ON_ERROR);
                    $status=match($type){'accept'=>'accepted','reject'=>'rejected','withdraw'=>'withdrawn',default=>'pending'};
                    $this->run('UPDATE offers SET status=?,price=?,proposer=? WHERE id=?',[$status,json_encode($price,JSON_THROW_ON_ERROR),$type==='counter'?$actor['id']:$offer['proposer'],$offerId]);
                    $this->event($l['item'],$offerId,$actor,$type,$message,$price);
                    if($type==='accept'){
                        $this->run("UPDATE listings SET status='agreed',revision=revision+1 WHERE id=?",[$l['id']]);
                        $others=$this->run("SELECT id FROM offers WHERE listing=? AND id<>? AND status='pending'",[$l['id'],$offerId])->fetchAll();
                        foreach($others as $other)$this->event($l['item'],$other['id'],$actor,'superseded','Another proposal was accepted.');
                        $this->run("UPDATE offers SET status='superseded',revision=revision+1 WHERE listing=? AND id<>? AND status='pending'",[$l['id'],$offerId]);
                    }
                    // Acceptance reserves the negotiation outcome only. Ownership
                    // and completed-sale history are untouched until fulfilment.
                }else throw new MarketError(422,'Unknown command.');
                $this->run('UPDATE offers SET revision=revision+1 WHERE id=?',[$offerId]);
            }
            $this->settleReady($hardcore,$offerId);
            $r=['offer_id'=>$offerId,'status'=>$this->run('SELECT status FROM offers WHERE id=?',[$offerId])->fetchColumn()];
            $this->run('INSERT INTO commands VALUES (?,?,?,?)',[$actor['id'],$id,$hash,json_encode($r,JSON_THROW_ON_ERROR)]);return $r;
        });
    }
    public function sale(array $actor,array $data,bool $hardcore): array {
        if(!in_array($actor['role'],['admin','settler'],true))throw new MarketError(403,'Only the fulfilment operator can record completed sales.');
        return $this->transaction(function()use($actor,$data,$hardcore){
            $this->lockScope($hardcore);
            $receipt=self::text($data['receipt']??null);$id=self::text($data['item_id']??null,32);$buyer=self::text($data['buyer']??null,64);$seller=self::text($data['seller']??null,64);$price=self::price($data['price']??null);
            $i=$this->run('SELECT * FROM items WHERE id=?'.$this->lock(),[$id])->fetch();
            if(!$i||$i['scope']!==$this->scope($hardcore))throw new MarketError(404,'Item not found.');
            $old=$this->run('SELECT item,seller,buyer,price FROM events WHERE receipt=?',[$receipt])->fetch();
            if($old){if($old['item']!==$id||$old['seller']!==$seller||$old['buyer']!==$buyer||$old['price']!==json_encode($price,JSON_THROW_ON_ERROR))throw new MarketError(409,'Receipt reused for a different sale.');return ['receipt'=>$receipt,'duplicate'=>true];}
            if($this->run("SELECT item FROM escrow_items WHERE item=? AND state<>'delivered'",[$id])->fetch())throw new MarketError(409,'Use atomic Vault settlement for custody inventory.');
            if($i['owner']!==$seller||$seller===$buyer||!$this->run('SELECT id FROM actors WHERE id=?',[$buyer])->fetch())throw new MarketError(409,'Ownership changed or buyer is invalid.');
            if($this->run("SELECT id FROM actors WHERE role='tester' AND id IN (?,?)",[$seller,$buyer])->fetch())throw new MarketError(422,'Development traders cannot create completed sales.');
            $this->run('UPDATE items SET owner=? WHERE id=?',[$buyer,$id]);
            $this->run("UPDATE listings SET status='sold',revision=revision+1 WHERE item=? AND status IN ('open','agreed')",[$id]);
            $this->event($id,null,$actor,'completed_sale','Completed trade recorded by the fulfilment operator.',$price,$seller,$buyer,$receipt);
            return ['receipt'=>$receipt,'duplicate'=>false];
        });
    }
    private function actorNames(): array {return $this->run('SELECT id,name FROM actors')->fetchAll(PDO::FETCH_KEY_PAIR);}
    private function formatSales(array $history,array $names): array {
        // A routine sync includes the same recent sales for every player.
        // Resolve bundle labels in batches rather than querying once per sale.
        $bundles=[];
        foreach(array_chunk(array_unique(array_filter(array_column($history,'offer'))),400) as $ids){
            $params=implode(',',array_fill(0,count($ids),'?'));
            foreach($this->run("SELECT o.id,b.listing,b.title,COUNT(m.item) AS item_count FROM offers o JOIN market_bundles b ON b.listing=o.listing JOIN market_bundle_items m ON m.listing=b.listing WHERE o.id IN ($params) GROUP BY o.id,b.listing,b.title",$ids)->fetchAll() as $b)
                $bundles[$b['id']]=['title'=>$b['title'],'item_count'=>(int)$b['item_count'],'listing'=>$b['listing']];
        }
        foreach($history as &$h){if(isset($bundles[$h['offer']]))$h['bundle']=$bundles[$h['offer']];$h['item_uuid']=isset($h['item_hex'])?($this->itemIdentity($h['item_hex'])['item_uuid']??''):'';unset($h['item_hex']);$h['price']=json_decode($h['price'],true,512,JSON_THROW_ON_ERROR);$h['seller_player_id']=$this->publicPlayerId($h['seller']);$h['buyer_player_id']=$this->publicPlayerId($h['buyer']);$h['seller_name']=$names[$h['seller']]??$h['seller'];$h['buyer_name']=$names[$h['buyer']]??$h['buyer'];foreach(['at','seed','quality','item_index','ethereal','sockets','seq'] as $key)$h[$key]=(int)$h[$key];unset($h['message'],$h['actor'],$h['offer']);}unset($h);return $history;
    }
    public function historyPage(bool $hardcore,array $query): array {
        $where=['i.scope=?',"e.kind IN ('completed_sale','bundle_sale')"];$args=[$this->scope($hardcore)];
        if(isset($query['item_id'])){$where[]='i.id=?';$args[]=self::text($query['item_id'],32);}
        else{
            $where[]='i.code=?';$args[]=self::text($query['code']??null,4);
            if(($query['similar']??false)===true){foreach(['quality'=>'quality','item_index'=>'item_index','ethereal'=>'ethereal','sockets'=>'sockets'] as $key=>$column){$where[]="i.$column=?";$args[]=self::number($query[$key]??null,$key==='item_index'?-1:0,$key==='item_index'?1000000:9);}}
            elseif(isset($query['item_uuid'])){$uuid=self::text($query['item_uuid'],32);if(!preg_match('/^[a-f0-9]{32}$/D',$uuid))throw new MarketError(422,'Invalid saved item UUID.');$where[]="i.id IN (SELECT item FROM item_identities WHERE scope=? AND identity_key=?)";$args[]=$this->scope($hardcore);$args[]='uuid:'.$uuid;}
            else {$where[]='i.seed=?';$args[]=self::number($query['seed']??null,1,4294967295);}
        }
        $averages=($query['averages']??false)===true?$this->saleAverages($where,$args):null;
        if(isset($query['before'])){$where[]='e.seq<?';$args[]=self::number($query['before'],1,PHP_INT_MAX);}
        $rows=$this->run('SELECT e.*,i.seed,i.code,i.quality,i.item_index,i.ethereal,i.sockets,i.item_hex FROM events e JOIN items i ON i.id=e.item WHERE '.implode(' AND ',$where).' ORDER BY e.seq DESC LIMIT 201',$args)->fetchAll();
        $more=count($rows)>200;if($more)array_pop($rows);$rows=$this->formatSales($rows,$this->actorNames());
        $result=['rows'=>$rows,'next_before'=>$more?$rows[count($rows)-1]['seq']:null,'complete'=>!$more];if($averages!==null)$result['averages']=$averages;return $result;
    }
    public function snapshot(array $actor,bool $hardcore,bool $includeBundles=false,bool $torchClass=false): array {
        $names=$this->actorNames();$scope=$this->scope($hardcore);$testers=array_fill_keys($this->run("SELECT id FROM actors WHERE role='tester'")->fetchAll(PDO::FETCH_COLUMN),true);
        $history=$this->run("SELECT e.*,i.seed,i.code,i.quality,i.item_index,i.ethereal,i.sockets,i.item_hex FROM events e JOIN items i ON i.id=e.item WHERE i.scope=? AND e.kind='completed_sale' ORDER BY e.at DESC,e.seq DESC LIMIT 1000",[$scope])->fetchAll();
        $history=$this->formatSales($history,$names);if(!$includeBundles)$history=array_values(array_filter($history,fn($r)=>!isset($r['bundle'])));
        $offers=$this->run('SELECT o.*,i.seed,i.code,i.item_hex,i.scope,l.item FROM offers o JOIN listings l ON l.id=o.listing JOIN items i ON i.id=l.item WHERE i.scope=? AND (o.buyer=? OR o.seller=?) ORDER BY o.created DESC,o.id DESC LIMIT 256',[$scope,$actor['id'],$actor['id']])->fetchAll();
        foreach($offers as &$o){$o['price']=json_decode($o['price'],true,512,JSON_THROW_ON_ERROR);$o['events']=$this->run('SELECT id,at,actor,kind,message,price FROM events WHERE offer=? AND kind<>\'bundle_sale\' ORDER BY seq LIMIT 512',[$o['id']])->fetchAll();
            foreach($o['events'] as &$e){$e['at']=(int)$e['at'];$e['actor_player_id']=$this->publicPlayerId($e['actor']);$e['actor_name']=$names[$e['actor']]??$e['actor'];$e['price']=json_decode($e['price'],true,512,JSON_THROW_ON_ERROR);}unset($e);
            foreach(['revision','listing_revision','created','seed'] as $key)$o[$key]=(int)$o[$key];$o['seller_player_id']=$this->publicPlayerId($o['seller']);$o['buyer_player_id']=$this->publicPlayerId($o['buyer']);$o['seller_name']=$names[$o['seller']]??$o['seller'];$o['buyer_name']=$names[$o['buyer']]??$o['buyer'];$o['development']=isset($testers[$o['seller']])||isset($testers[$o['buyer']]);unset($o['scope']);}unset($o);
        $listings=$this->run("SELECT l.*,c.comment,i.item_hex,p.tab,p.expires,e.state AS escrow_state FROM listings l LEFT JOIN market_listing_comments c ON c.listing=l.id JOIN items i ON i.id=l.item LEFT JOIN published_items p ON p.listing=l.id LEFT JOIN escrow_items e ON e.item=l.item WHERE i.scope=? AND l.status='open' AND (p.listing IS NULL OR p.expires>?) ORDER BY l.created DESC,l.id LIMIT 4096",[$scope,time()])->fetchAll();
        if(!$includeBundles){$bundled=array_fill_keys($this->run('SELECT b.listing FROM market_bundles b JOIN listings l ON l.id=b.listing JOIN items i ON i.id=l.item WHERE i.scope=?',[$scope])->fetchAll(PDO::FETCH_COLUMN),true);$listings=array_values(array_filter($listings,fn($r)=>!isset($bundled[$r['id']])));$offers=array_values(array_filter($offers,fn($r)=>!isset($bundled[$r['listing']])));}
        $counts=[];foreach($this->run("SELECT o.listing,COUNT(*) AS total,SUM(CASE WHEN o.status IN ('pending','accepted') THEN 1 ELSE 0 END) AS active,SUM(CASE WHEN o.status IN ('pending','accepted') AND o.buyer<>? THEN 1 ELSE 0 END) AS others FROM offers o JOIN listings l ON l.id=o.listing JOIN items i ON i.id=l.item WHERE i.scope=? GROUP BY o.listing",[$actor['id'],$scope])->fetchAll() as $count)$counts[$count['listing']]=$count;
        $feed=[];foreach($listings as $l){if($l['escrow_state']!=='listed'&&($native=$this->itemIdentity($l['item_hex']))&&str_starts_with($native['key'],'bytes:'))continue;$feed[]=['created'=>(int)$l['created'],'comment'=>$l['comment']??'','offer_count'=>(int)($counts[$l['id']]['total']??0),'active_offer_count'=>(int)($counts[$l['id']]['active']??0),'other_offer_count'=>(int)($counts[$l['id']]['others']??0),'seller'=>$names[$l['seller']]??$l['seller'],'seller_id'=>$l['seller'],'seller_player_id'=>$this->publicPlayerId($l['seller']),'item_id'=>$l['item'],'id'=>$l['id'],'revision'=>(string)$l['revision'],'item_hex'=>$l['item_hex'],'price'=>json_decode($l['price'],true,512,JSON_THROW_ON_ERROR),'own'=>$l['seller']===$actor['id'],'tab'=>(int)($l['tab']??1),'custody'=>$l['escrow_state']==='listed'?'escrow':'local_save','purchasable'=>$l['escrow_state']==='listed','expires_at'=>$l['escrow_state']==='listed'?time()+3600:(int)($l['expires']??time()+3600),'development'=>isset($testers[$l['seller']])];}
        $escrow=$this->escrowSnapshot($actor['id'],$scope);
        foreach($escrow['items'] as &$entry)if(($entry['metadata']['comment_owner']??'')!==$actor['id'])unset($entry['metadata']['comment']);unset($entry);
        // Legacy native clients cannot parse the new asking-price marker. Web
        // snapshots opt into it; concrete negotiated terms remain compatible.
        if(!$includeBundles){$feed=array_values(array_filter($feed,fn($r)=>!self::isOpen($r['price'])));foreach($escrow['items'] as &$entry)if(self::isOpen($entry['metadata']['price']??[]))$entry['metadata']['price']=[];unset($entry);}
        if(!$includeBundles&&!$torchClass){
            $feed=array_values(array_filter($feed,fn($r)=>!self::hasClassPrice($r['price'])));
            $history=array_values(array_filter($history,fn($r)=>!self::hasClassPrice($r['price'])));
            $offers=array_values(array_filter($offers,fn($r)=>!self::hasClassPrice($r['price'])&&!array_filter($r['events'],fn($e)=>self::hasClassPrice($e['price']))));
            foreach($escrow['items'] as &$entry)if(self::hasClassPrice($entry['metadata']['price']??[]))$entry['metadata']['price']=[];unset($entry);
        }
        return ['gifts'=>$includeBundles?[]:$this->giftCampaigns($actor['id']!==''?$actor:null,$hardcore,true),'market_reset'=>$this->clientReset($actor['id'],$hardcore),'market_control'=>array_intersect_key($this->controlState(),array_flip(['phase','generation'])),'schema'=>1,'game_mode'=>'2.4','mod'=>$this->mod,'hardcore'=>$hardcore,'generated_at'=>time(),'expires_at'=>time()+3600,'self'=>['id'=>$actor['id'],'player_id'=>$this->publicPlayerId($actor['id']),'name'=>$names[$actor['id']]??$actor['name']],'social'=>$this->socialSnapshot($actor['id'],$hardcore),'publication'=>$this->publication($actor,$hardcore),'escrow'=>$escrow,'history'=>$history,'offers'=>$offers,'listings'=>$feed];
    }
}
