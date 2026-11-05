<?php
declare(strict_types=1);
// CLI only. Every SQL table is rewritten to a fresh, validated test prefix.
// The real service tables are never read, migrated, or mutated by this suite.
if(PHP_SAPI!=='cli')exit(1);
final class ValidationPDO extends PDO {
    public const TABLES=['market_gift_campaigns','market_gift_claims','market_gift_notices','market_wanted','market_wanted_offers','market_browser_features','market_companion_versions','market_message_seen','market_trade_assets','market_catalog_families','market_catalog_versions','market_agent_catalogs','market_player_ids','market_profile_recoveries','market_installations','actors','items','listings','offers','events','commands','inventory_sources','published_items','custody_agents','custody_serial','custody_tickets','escrow_items','escrow_balances','settlements','market_preferences','custody_sources','custody_catalogs','web_tickets','web_item_display','web_attempts','web_profiles','web_intents','item_identities','identity_reservations','market_names','market_favorites','market_bidder_aliases','market_public_offers','wallet_withdrawals','wallet_entries','market_players','market_characters','market_messages','market_message_reads','market_mods','market_memberships','market_ticket_scopes','market_scope_migrations','custody_agents_scoped','market_moderators','market_reports','market_report_events','market_owners','market_control','market_client_resets','market_owner_actions','market_bundles','market_bundle_items','market_searches','market_search_hits','market_search_notices','market_search_poll','market_notifications','market_player_controls','market_activity_reads','market_companion_seen'];
    public function __construct(array $config,public string $prefix){if(!preg_match('/^validation_[a-f0-9]{12}_$/D',$prefix))throw new RuntimeException('Invalid validation namespace.');parent::__construct($config['dsn'],$config['username']??null,$config['password']??null);$this->setAttribute(PDO::ATTR_ERRMODE,PDO::ERRMODE_EXCEPTION);}
    private function rewritten(string $sql): string{return preg_replace_callback('/\b('.implode('|',self::TABLES).')\b/',fn($m)=>$this->prefix.$m[1],$sql);}
    public function exec(string $statement): int|false{return parent::exec($this->rewritten($statement));}
    public function prepare(string $query,array $options=[]): PDOStatement|false{return parent::prepare($this->rewritten($query),$options);}
    public function cleanup(): void{foreach(array_reverse(self::TABLES) as $table)parent::exec('DROP TABLE IF EXISTS `'.$this->prefix.$table.'`');}
}
$server=$argv[1]??'';$configPath=$argv[2]??'';
$config=require $configPath;
require $server.'/Market.php';
if(($argv[3]??'')==='worker'){
    $job=json_decode(file_get_contents($argv[4]),true,512,JSON_THROW_ON_ERROR);$db=new ValidationPDO($config,$job['prefix']);$m=new Market($db);
    while(microtime(true)<$job['start'])usleep(1000);
    try{$result=$m->command(['id'=>$job['owner'],'role'=>'player'],$job['command'],false);echo json_encode(['status'=>200,'result'=>$result]);}
    catch(MarketError $e){echo json_encode(['status'=>$e->status]);}exit;
}
$prefix='validation_'.bin2hex(random_bytes(6)).'_';$db=new ValidationPDO($config,$prefix);
$report=['driver'=>$db->getAttribute(PDO::ATTR_DRIVER_NAME),'isolated_prefix'=>$prefix];$jobs=[];
try{
    if(in_array($argv[3]??'',['players','notifications','transfers','mods','moderation','owner','averages','web','item-names','experience','bundles','enrollment','player-identity','social','compatibility','owner-audit','impersonation','trading','torch','gifts'],true)){
        $suite=$argv[3];
        $fixture=$argv[4]??null;$argv=$fixture?[__FILE__,$fixture]:[__FILE__];
        $file=match($suite){'players'=>'players','notifications'=>'trade-notifications',default=>$suite};$code=file_get_contents(__DIR__.'/marketplace-'.$file.'-test.php');
        $code=str_replace(["<?php", "require __DIR__.'/../tools/marketplace/server/Market.php';", "new PDO('sqlite::memory:')"],['','', '$db'],$code);
        $code=str_replace("require __DIR__.'/../tools/marketplace/server/OwnerDemo.php';","require \$server.'/OwnerDemo.php';",$code);
        $code=str_replace("__DIR__.'/../tools/marketplace/server/item-identity.json'",var_export($server.'/item-identity.json',true),$code);eval($code);$report[$suite.'_domain']='passed';
        if($suite==='gifts'){
            $before=(int)$m->run('SELECT COUNT(*) FROM items WHERE owner=?',[$a['id']])->fetchColumn();$start=microtime(true)+1;$children=[];
            for($n=0;$n<2;++$n){
                $path=__DIR__.'/race-'.bin2hex(random_bytes(8)).'.json';$jobs[]=$path;
                file_put_contents($path,json_encode(['prefix'=>$prefix,'owner'=>$a['id'],'start'=>$start,'command'=>['id'=>Market::id(),'type'=>'campaign_claim','campaign_id'=>$noExpiry['campaign_id']]],JSON_THROW_ON_ERROR));chmod($path,0600);
                $process=proc_open([PHP_BINARY,__FILE__,$server,$configPath,'worker',$path],[0=>['pipe','r'],1=>['pipe','w'],2=>['pipe','w']],$pipes);if(!is_resource($process))throw new RuntimeException('Cannot start gift claim race.');fclose($pipes[0]);$children[]=[$process,$pipes];
            }
            $replays=[];foreach($children as [$process,$pipes]){$out=stream_get_contents($pipes[1]);$error=stream_get_contents($pipes[2]);fclose($pipes[1]);fclose($pipes[2]);if(proc_close($process)!==0)throw new RuntimeException('Gift worker failed: '.$error);$r=json_decode($out,true,512,JSON_THROW_ON_ERROR);check($r['status']===200);$replays[]=$r['result']['already_claimed'];}
            sort($replays);check($replays===[false,true]);check((int)$m->run('SELECT COUNT(*) FROM items WHERE owner=?',[$a['id']])->fetchColumn()===$before+count($ids));
            check((int)$m->run('SELECT COUNT(*) FROM market_gift_claims WHERE campaign=? AND owner=?',[$noExpiry['campaign_id'],$a['id']])->fetchColumn()===1);
            $report['parallel_gift_claim']='one complete grant, one replay receipt; exactly one entitlement and one bundle';
        }
        if($suite==='bundles'){
            $bundle=$m->command($seller,['id'=>Market::id(),'type'=>'bundle_create','title'=>'Concurrent free bundle','items'=>$ids,'price'=>Market::freePrice()],false);
            $before=(int)$m->run('SELECT COUNT(*) FROM settlements')->fetchColumn();$start=microtime(true)+1;$children=[];
            foreach([$buyer,$other] as $actor){
                $path=__DIR__.'/race-'.bin2hex(random_bytes(8)).'.json';$jobs[]=$path;
                file_put_contents($path,json_encode(['prefix'=>$prefix,'owner'=>$actor['id'],'start'=>$start,'command'=>['id'=>Market::id(),'type'=>'propose','listing_id'=>$bundle['listing_id'],'listing_revision'=>1,'price'=>Market::freePrice()]],JSON_THROW_ON_ERROR));chmod($path,0600);
                $process=proc_open([PHP_BINARY,__FILE__,$server,$configPath,'worker',$path],[0=>['pipe','r'],1=>['pipe','w'],2=>['pipe','w']],$pipes);if(!is_resource($process))throw new RuntimeException('Cannot start bundle race.');fclose($pipes[0]);$children[]=[$process,$pipes];
            }
            $statuses=[];foreach($children as [$process,$pipes]){$out=stream_get_contents($pipes[1]);$error=stream_get_contents($pipes[2]);fclose($pipes[1]);fclose($pipes[2]);if(proc_close($process)!==0)throw new RuntimeException('Bundle worker failed: '.$error);$statuses[]=json_decode($out,true,512,JSON_THROW_ON_ERROR)['status'];}
            sort($statuses);check($statuses===[200,409]);check((int)$m->run('SELECT COUNT(*) FROM settlements')->fetchColumn()===$before+1);
            $owners=[];foreach($ids as $id)$owners[]=$m->run('SELECT owner FROM items WHERE id=?',[$id])->fetchColumn();check(count(array_unique($owners))===1&&in_array($owners[0],[$buyer['id'],$other['id']],true));
            $report['parallel_bundle_claim']='one winner receives every member; one stale refusal; one settlement';
        }
        if($suite==='transfers'){
            $raceItem=array_replace($item,['item_hex'=>str_repeat('fa',24),'price'=>Market::freePrice()]);$deposit([$raceItem]);
            $list=$m->snapshot($buyer,false)['listings'];$listing=end($list);check($listing['price']===Market::freePrice());
            $before=(int)$m->run('SELECT COUNT(*) FROM settlements')->fetchColumn();$start=microtime(true)+1;$children=[];
            foreach([$buyer,$other] as $actor){
                $path=__DIR__.'/race-'.bin2hex(random_bytes(8)).'.json';$jobs[]=$path;
                file_put_contents($path,json_encode(['prefix'=>$prefix,'owner'=>$actor['id'],'start'=>$start,'command'=>['id'=>Market::id(),'type'=>'propose','listing_id'=>$listing['id'],'listing_revision'=>(int)$listing['revision'],'price'=>Market::freePrice()]],JSON_THROW_ON_ERROR));chmod($path,0600);
                $process=proc_open([PHP_BINARY,__FILE__,$server,$configPath,'worker',$path],[0=>['pipe','r'],1=>['pipe','w'],2=>['pipe','w']],$pipes);if(!is_resource($process))throw new RuntimeException('Cannot start free-claim worker.');fclose($pipes[0]);$children[]=[$process,$pipes];
            }
            $statuses=[];foreach($children as [$process,$pipes]){$out=stream_get_contents($pipes[1]);$error=stream_get_contents($pipes[2]);fclose($pipes[1]);fclose($pipes[2]);if(proc_close($process)!==0)throw new RuntimeException('Free-claim worker failed: '.$error);$statuses[]=json_decode($out,true,512,JSON_THROW_ON_ERROR)['status'];}
            sort($statuses);check($statuses===[200,409]);check((int)$m->run('SELECT COUNT(*) FROM settlements')->fetchColumn()===$before+1);
            $report['parallel_free_claim']='one claimant wins, one stale refusal, exactly one transfer and delivery';
        }

    }else{
    $code=file_get_contents(__DIR__.'/marketplace-wallet-test.php');
    $code=str_replace(["<?php", "require __DIR__.'/../tools/marketplace/server/Market.php';", "new PDO('sqlite::memory:')"],['','', '$db'],$code);
    // The exact shipped wallet domain suite runs against the prefixed MySQL tables.
    eval($code);$report['wallet_domain']='passed';
    $racer=$m->user('Concurrency validation');$ra=$m->authAgent($m->pairAgent($racer['id'],$catalog)['token']);$deposit($ra,[$gold(100000)]);
    $start=microtime(true)+1;$children=[];
    for($n=0;$n<2;$n++){
        $path=__DIR__.'/race-'.bin2hex(random_bytes(8)).'.json';$jobs[]=$path;
        file_put_contents($path,json_encode(['prefix'=>$prefix,'owner'=>$racer['id'],'start'=>$start,'command'=>['id'=>Market::id(),'type'=>'wallet_withdraw','balances'=>[$gold(70000)]]],JSON_THROW_ON_ERROR));chmod($path,0600);
        $process=proc_open([PHP_BINARY,__FILE__,$server,$configPath,'worker',$path],[0=>['pipe','r'],1=>['pipe','w'],2=>['pipe','w']],$pipes);if(!is_resource($process))throw new RuntimeException('Cannot start concurrency worker.');fclose($pipes[0]);$children[]=[$process,$pipes];
    }
    $results=[];foreach($children as [$process,$pipes]){$out=stream_get_contents($pipes[1]);$error=stream_get_contents($pipes[2]);fclose($pipes[1]);fclose($pipes[2]);if(proc_close($process)!==0)throw new RuntimeException('Concurrency worker failed: '.$error);$results[]=json_decode($out,true,512,JSON_THROW_ON_ERROR);}
    $statuses=array_column($results,'status');sort($statuses);check($statuses===[200,409]);
    check($balance($racer,'gold')===30000);$requests=$m->snapshot($racer,false)['escrow']['wallet']['withdrawals'];check(count($requests)===1&&$requests[0]['remaining'][0]['quantity']===70000);
    $cancel($racer,$requests[0]['id'],$requests[0]['revision']);check($balance($racer,'gold')===100000);
    $report['parallel_overdraw']='one success, one refusal; exact conservation after cancellation';
    // Two funded buyers simultaneously offer the exact ask. Scope locking must
    // transfer ownership/payment once and refuse the losing stale proposal.
    $saleItem=array_replace($item,['item_hex'=>str_repeat('ae',24),'price'=>[$gold(5000)]]);$deposit($sa,[],[$saleItem]);
    $deposit($ba,[$gold(10000)]);$deposit($oa,[$gold(10000)]);
    $listing=$m->snapshot($buyer,false)['listings'][0];$m->command($seller,['id'=>Market::id(),'type'=>'preferences','auto_trade'=>true],false);
    $beforeSales=(int)$m->run('SELECT COUNT(*) FROM settlements')->fetchColumn();$start=microtime(true)+1;$children=[];
    foreach([$buyer,$other] as $actor){
        $path=__DIR__.'/race-'.bin2hex(random_bytes(8)).'.json';$jobs[]=$path;
        file_put_contents($path,json_encode(['prefix'=>$prefix,'owner'=>$actor['id'],'start'=>$start,'command'=>['id'=>Market::id(),'type'=>'propose','listing_id'=>$listing['id'],'listing_revision'=>(int)$listing['revision'],'price'=>[$gold(5000)]]],JSON_THROW_ON_ERROR));chmod($path,0600);
        $process=proc_open([PHP_BINARY,__FILE__,$server,$configPath,'worker',$path],[0=>['pipe','r'],1=>['pipe','w'],2=>['pipe','w']],$pipes);if(!is_resource($process))throw new RuntimeException('Cannot start trade worker.');fclose($pipes[0]);$children[]=[$process,$pipes];
    }
    $results=[];foreach($children as [$process,$pipes]){$out=stream_get_contents($pipes[1]);$error=stream_get_contents($pipes[2]);fclose($pipes[1]);fclose($pipes[2]);if(proc_close($process)!==0)throw new RuntimeException('Trade worker failed: '.$error);$results[]=json_decode($out,true,512,JSON_THROW_ON_ERROR);}
    $statuses=array_column($results,'status');sort($statuses);check($statuses===[200,409]);
    check((int)$m->run('SELECT COUNT(*) FROM settlements')->fetchColumn()===$beforeSales+1&&$balance($seller,'gold')===5000);
    check($balance($buyer,'gold')+$balance($other,'gold')===15300);
    $report['parallel_purchase']='one sale, one stale refusal; one item owner and exact payment conservation';
    }
}finally{
    $db->cleanup();foreach($jobs as $path)if(is_file($path))unlink($path);
}
$report['test_tables_removed']=true;echo json_encode($report,JSON_PRETTY_PRINT)."\n";
