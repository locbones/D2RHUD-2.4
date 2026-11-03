<?php
declare(strict_types=1);
require __DIR__.'/../tools/marketplace/server/Market.php';
function check(bool $ok):void{if(!$ok)throw new RuntimeException('Social test failed: '.json_encode(debug_backtrace(DEBUG_BACKTRACE_IGNORE_ARGS,1)));}
function refuses(callable $f,int $status):void{try{$f();}catch(MarketError $e){check($e->status===$status);return;}throw new RuntimeException('Unexpected social action allowed.');}
$m=new Market(new PDO('sqlite::memory:'));$m->migrate();$seller=$m->user('Seller');$buyers=[];
$gold=fn($n)=>[['kind'=>'gold','code'=>'gld','label'=>'Gold','quality'=>2,'index'=>-1,'ethereal'=>-1,'quantity'=>$n]];
$listing=$m->listing(['id'=>'operator','role'=>'admin'],['owner'=>$seller['id'],'item'=>['seed'=>8765,'code'=>'rin','quality'=>6,'index'=>-1,'ethereal'=>0,'sockets'=>0,'item_hex'=>str_repeat('ab',24)],'price'=>$gold(1000)],false);
for($n=0;$n<7;$n++){$buyer=$m->user('PrivateBuyer'.$n);$buyers[]=$buyer;$offer=$m->command($buyer,['id'=>Market::id(),'type'=>'propose','listing_id'=>$listing['listing_id'],'listing_revision'=>1,'price'=>$gold(100+$n),'message'=>'Secret private message'],false);}
$page=$m->publicOffers(false,['listing_id'=>$listing['listing_id']]);check($page['total']===7&&count($page['rows'])===4&&$page['next_before']!==null);
$encoded=json_encode($page);check(!str_contains($encoded,'PrivateBuyer')&&!str_contains($encoded,'Secret private')&&!str_contains($encoded,'conversation')&&!str_contains($encoded,'item_hex'));
foreach($buyers as $buyer)check(!str_contains($encoded,$buyer['id'])&&!str_contains($encoded,$buyer['token']));
$next=$m->publicOffers(false,['listing_id'=>$listing['listing_id'],'before'=>$page['next_before']]);check(count($next['rows'])===3&&$next['next_before']===null);
check($m->publicOffers(false,['listing_id'=>$listing['listing_id']])['rows'][0]['alias']===$page['rows'][0]['alias']);
$private=$m->publicOffers(false,['listing_id'=>$listing['listing_id']],$seller);check(isset($private['rows'][0]['conversation']['events']));
$c=$private['rows'][0]['conversation'];check(!isset($m->privateConversation($seller,false,$c['id'])['item_hex']));refuses(fn()=>$m->privateConversation($buyers[0],false,$c['id']),403);refuses(fn()=>$m->privateConversation($seller,true,$c['id']),404);$m->command($seller,['id'=>Market::id(),'type'=>'counter','offer_id'=>$c['id'],'expected_revision'=>1,'price'=>$gold(800)],false);
$after=$m->publicOffers(false,['listing_id'=>$listing['listing_id']]);check($after['rows'][0]['from']==='seller'&&$after['rows'][0]['alias']===$page['rows'][0]['alias']&&$after['rows'][0]['price'][0]['quantity']===800);
$m->migrate();check($m->publicOffers(false,['listing_id'=>$listing['listing_id']])['rows'][0]['alias']===$page['rows'][0]['alias']);
refuses(fn()=>$m->publicOffers(true,['listing_id'=>$listing['listing_id']]),404);
$rename=['id'=>Market::id(),'type'=>'profile_name','name'=>'New Seller','expected_name'=>'Seller'];check($m->command($seller,$rename,false)['name']==='New Seller');check($m->command($seller,$rename,false)['name']==='New Seller');
check($m->snapshot($seller,false)['self']['name']==='New Seller'&&$m->snapshot($buyers[0],false)['listings'][0]['seller']==='New Seller');
refuses(fn()=>$m->command($buyers[0],['id'=>Market::id(),'type'=>'profile_name','name'=>'new seller','expected_name'=>$buyers[0]['name']],false),409);
refuses(fn()=>$m->command($seller,['id'=>Market::id(),'type'=>'profile_name','name'=>'Again','expected_name'=>'Seller'],false),409);
refuses(fn()=>$m->command($seller,['id'=>Market::id(),'type'=>'profile_name','name'=>'<script>','expected_name'=>'New Seller'],false),422);
$favorite=['id'=>Market::id(),'type'=>'favorite','kind'=>'listing','target'=>$listing['listing_id'],'enabled'=>true];$m->command($buyers[0],$favorite,false);$m->command($buyers[0],$favorite,false);
check(count($m->snapshot($buyers[0],false)['social']['favorites'])===1&&count($m->snapshot($seller,false)['social']['favorites'])===0&&count($m->snapshot($buyers[0],true)['social']['favorites'])===0);
$favorite['id']=Market::id();$favorite['enabled']=false;$m->command($buyers[0],$favorite,false);check($m->snapshot($buyers[0],false)['social']['favorites']===[]);
$m->command($buyers[0],['id'=>Market::id(),'type'=>'favorite','kind'=>'seller','target'=>$seller['id'],'enabled'=>true],false);
$feed=$m->snapshot($buyers[0],false)['listings'][0];check($feed['offer_count']===7&&$feed['active_offer_count']===7&&$feed['other_offer_count']===6);
echo "Profiles/favorites: owner/mode isolation, unique names, CAS/idempotency; public offers: stable aliases, pagination, counter terms and no third-party identities/messages passed.\n";
