<?php
declare(strict_types=1);
require __DIR__.'/../tools/marketplace/server/Market.php';
function check(bool $ok):void{if(!$ok)throw new RuntimeException('Experience check failed at '.json_encode(debug_backtrace(DEBUG_BACKTRACE_IGNORE_ARGS,1)));}
function refuses(callable $f,int $status):void{try{$f();}catch(MarketError $e){check($e->status===$status);return;}throw new RuntimeException('Invalid experience action allowed.');}
$m=new Market(new PDO('sqlite::memory:'));$m->migrate();$m->migrate();$seller=$m->user('Search seller');$buyer=$m->user('Search buyer');$other=$m->user('Search outsider');$cat=str_repeat('a',64);
foreach([$seller,$buyer,$other] as $a)$m->browserLink(['code'=>$m->browserTicket($a)['code']]);$agent=$m->authAgent($m->pairAgent($seller['id'],$cat)['token']);
$gold=fn($n)=>['kind'=>'gold','code'=>'gld','label'=>'Gold','quality'=>2,'index'=>-1,'ethereal'=>-1,'quantity'=>$n];$pages=[['page'=>11,'before'=>str_repeat('b',64),'after'=>str_repeat('c',64)]];
$cmd=fn($actor,$data)=>$m->command($actor,$data+['id'=>Market::id()],false);
$criteria=['q'=>'Ring','searchScope'=>'offered','stats'=>[['id'=>39,'min'=>30]],'sockets_min'=>2,'sockets_max'=>3,'cap'=>['code'=>'gld','quantity'=>500]];
$search=['id'=>Market::id(),'type'=>'search_save','name'=>'Resist ring','enabled'=>true,'criteria'=>$criteria];$saved=$cmd($buyer,$search);check($saved===$cmd($buyer,$search));
refuses(fn()=>$cmd($other,['type'=>'search_delete','search_id'=>$saved['search_id']]),404);refuses(fn()=>$cmd($buyer,array_replace($search,['id'=>Market::id(),'criteria'=>['stats'=>[['id'=>97,'min'=>1]]]])),422);
$deposit=function($hex,$value=35,$price=null)use($m,$agent,$cat,$pages,$gold){$id=Market::id();$item=['page'=>11,'tab'=>1,'saved_id'=>0,'quality'=>6,'index'=>-1,'code'=>'rin','ethereal'=>0,'identified'=>1,'quantity'=>1,'quantity_bit'=>-1,'item_hex'=>$hex,'price'=>$price??[$gold(100)],'display'=>['saved_properties'=>[['id'=>39,'value'=>$value],['id'=>194,'value'=>2]],'socketed_items'=>[]]];foreach([['type'=>'deposit_prepare','items'=>[$item],'pages'=>$pages],['type'=>'write_authorize','pages'=>$pages],['type'=>'commit','pages'=>$pages]] as $p)$m->custody($agent,$p+['id'=>$id,'catalog'=>$cat],false);};
$deposit(str_repeat('ab',24));$m->evaluateSearches($buyer,true);$inbox=$m->inbox($buyer);check(count($inbox['messages'])===1&&$inbox['messages'][0]['category']==='search'&&isset($inbox['messages'][0]['listing_id']));$m->evaluateSearches($buyer,true);check(count($m->inbox($buyer)['messages'])===1);
$deposit(str_repeat('ac',24),29);$deposit(str_repeat('ad',24),35,[$gold(501)]);$m->evaluateSearches($buyer,true);check(count($m->inbox($buyer)['messages'])===1);
// Baseline current matches without a burst of old notifications.
$second=$cmd($buyer,['type'=>'search_save','name'=>'All rings','enabled'=>true,'criteria'=>['q'=>'Ring']]);check(count($m->inbox($buyer)['messages'])===1);
$settings=['type'=>'notification_settings','game_direct'=>true,'game_trade'=>true,'game_search'=>false];$cmd($buyer,$settings);$game=$m->inbox($buyer,0,'game');check($game['messages']===[]&&$game['next']>0);check(count($m->inbox($buyer)['messages'])===1);
$message=$cmd($seller,['type'=>'direct_message','to'=>$buyer['id'],'message'=>'Hello from the website']);check(count($m->inbox($buyer,0,'game')['messages'])===1);
$cmd($buyer,['type'=>'player_control','target'=>$seller['id'],'muted'=>true,'blocked'=>false]);check($m->inbox($buyer,0,'game')['messages']===[]);check(count($m->conversations($buyer,$seller['id'])['messages'])===1);
$cmd($buyer,['type'=>'player_control','target'=>$seller['id'],'muted'=>true,'blocked'=>true]);refuses(fn()=>$cmd($seller,['type'=>'direct_message','to'=>$buyer['id'],'message'=>'Blocked']),403);refuses(fn()=>$cmd($buyer,['type'=>'direct_message','to'=>$seller['id'],'message'=>'Also blocked']),403);
$deposit(str_repeat('ae',24));$m->evaluateSearches($buyer,true);check(count($m->inbox($buyer)['messages'])===2); // search + earlier direct message only
$cmd($buyer,['type'=>'player_control','target'=>$seller['id'],'muted'=>false,'blocked'=>false]);$m->evaluateSearches($buyer,true);check(count($m->inbox($buyer)['messages'])===4); // one digest per saved search
$state=$m->browserSnapshot($buyer,false)['experience'];check(count($state['searches'])===2&&$state['players']===[]&&!$state['notifications']['game_search']);check($m->browserSnapshot($buyer,true)['experience']['searches']===[]);
$cmd($buyer,['type'=>'search_delete','search_id'=>$saved['search_id']]);check(count($m->browserSnapshot($buyer,false)['experience']['searches'])===1);
$own=$cmd($buyer,['type'=>'search_save','name'=>'Disabled','enabled'=>false,'criteria'=>['q'=>'Ring']]);$deposit(str_repeat('af',24));$m->evaluateSearches($buyer,true);check(count($m->inbox($buyer)['messages'])===5);
$activity=$m->activity($buyer,false);$read=['type'=>'activity_read','through'=>$activity['through']];$cmd($buyer,$read);check($m->activity($buyer,false)['read_through']===$activity['through']);
echo "Experience: exact property/socket/currency filters, current-match baselines, new revision digests, idempotency, private scoped searches, hidden-property refusal, persistent preferences, game cursor suppression, mute/archive behavior and two-way blocking passed.\n";

// Browse and saved alerts use identical forgiving text rules.
$textMatch=new ReflectionMethod(Market::class,'searchTextMatches');
foreach(json_decode(file_get_contents(__DIR__.'/fixtures/marketplace-search-cases.json'),true,512,JSON_THROW_ON_ERROR) as [$text,$query,$expected])check($textMatch->invoke(null,$text,$query)===$expected);
$catalog=$m->browserCatalog();$soj=null;foreach($catalog['unique'] as $index=>$item)if(stripos($item['name'],'stone of jordan')!==false){$soj=['code'=>$item['code'],'quality'=>7,'index'=>(int)$index,'quantity'=>1,'ethereal'=>-1,'label'=>$item['name']];break;}check($soj!==null);
$named=$cmd($buyer,['type'=>'search_save','name'=>'Wanted Jordan rings','enabled'=>true,'criteria'=>['q'=>'jordan stone rings','searchScope'=>'wanted']]);
$before=count($m->inbox($buyer)['messages']);$deposit(str_repeat('b1',24),35,[$soj]);$m->evaluateSearches($buyer,true);$messages=$m->inbox($buyer)['messages'];check(count($messages)===$before+2); // existing ring search + wanted-payment search
check((bool)array_filter($messages,fn($r)=>str_contains($r['body'],'Wanted Jordan rings')));
$predicate=new ReflectionMethod(Market::class,'searchMatches');$criteriaMethod=new ReflectionMethod(Market::class,'searchCriteria');
$ring=['code'=>'rin','quality'=>2,'index'=>-1];$row=['id'=>'test','seller_id'=>$seller['id'],'seller'=>'Seller','item'=>$ring,'price'=>[$soj],'purchasable'=>true,'other_offer_count'=>0];
foreach(['stone','jordan stone','stone jordan ring','the stone-of-jordan rings'] as $query){foreach(['both','wanted','offered'] as $scope){$criteria=$criteriaMethod->invoke($m,['q'=>$query,'searchScope'=>$scope]);check($predicate->invoke($m,$row,$criteria,$buyer['id'],$catalog,'RMD-MP|2.4|SC')===($scope!=='offered'));}}
$row['price']=[['code'=>'rin','quality'=>2,'index'=>-1,'label'=>'Legacy Stone of Jordan Ring']];$criteria=$criteriaMethod->invoke($m,['q'=>'stone jordan','searchScope'=>'wanted']);check($predicate->invoke($m,$row,$criteria,$buyer['id'],$catalog,'RMD-MP|2.4|SC'));
// An ordinary ring must not inherit an unrelated set name from its index.
$row['price']=[['code'=>'rin','quality'=>2,'index'=>0,'label'=>'Ring']];$criteria=$criteriaMethod->invoke($m,['q'=>$catalog['set'][0]['name'],'searchScope'=>'wanted']);check(!$predicate->invoke($m,$row,$criteria,$buyer['id'],$catalog,'RMD-MP|2.4|SC'));
echo "Search: browser/PHP text parity, real wanted-payment alert, partial/reordered names, saved labels, scope preservation and valid quality lookup passed.\n";
