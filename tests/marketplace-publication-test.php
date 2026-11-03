<?php
declare(strict_types=1);
require __DIR__.'/../tools/marketplace/server/Market.php';
function check(bool $v):void {if(!$v)throw new RuntimeException('Publication assertion failed.');}
function refuses(callable $fn,int $status):void {try{$fn();}catch(MarketError $e){check($e->status===$status);return;}throw new RuntimeException('Unexpected publication acceptance.');}
$m=new Market(new PDO('sqlite::memory:'));$m->migrate();$m->migrate();
$a=$m->user('Seller');$b=$m->user('Buyer');$seller=$m->auth($a['token']);$buyer=$m->auth($b['token']);
$gold=[['code'=>'gld','label'=>'Gold','quality'=>2,'index'=>-1,'quantity'=>1000,'ethereal'=>-1,'kind'=>'gold']];
$row=['tab'=>3,'item_hex'=>'abcd','code'=>'rin','saved_id'=>4294967295,'quality'=>7,'index'=>42,'ethereal'=>false,'socketed_items'=>[],'price'=>$gold];
$pub=['schema'=>1,'game_mode'=>'2.4','mod'=>'RMD-MP','hardcore'=>false,'custody'=>'local_save','purchasable'=>false,'id'=>Market::id(),'captured_at'=>time(),'expected_revision'=>0,'page'=>11,'enabled'=>true,'source'=>'live','items'=>[$row]];
refuses(fn()=>$m->publish($seller,array_replace($pub,['purchasable'=>true]),false),422);
refuses(fn()=>$m->publish($seller,$pub,true),422);
$first=$m->publish($seller,$pub,false);check($first['revision']===1);
$listing=$m->snapshot($buyer,false)['listings'][0];check($listing['tab']===3&&!$listing['own']&&!$listing['purchasable']);
check($m->publish($seller,$pub,false)['duplicate']);
check(count($m->snapshot($buyer,false)['listings'])===1);
$offer=$m->command($buyer,['id'=>Market::id(),'type'=>'propose','listing_id'=>$listing['id'],'listing_revision'=>1,'price'=>$gold],false);
$reprice=$pub;$reprice['id']=Market::id();$reprice['expected_revision']=1;$reprice['items'][0]['price'][0]['quantity']=2000;
$m->publish($seller,$reprice,false);$updated=$m->snapshot($buyer,false)['listings'][0];
check($updated['id']===$listing['id']&&$updated['item_id']===$listing['item_id']&&$updated['revision']==='2');
check($m->snapshot($buyer,false)['offers'][0]['status']==='superseded');
refuses(fn()=>$m->publish($seller,$pub,false),409);
refuses(fn()=>$m->publish($seller,array_replace($reprice,['source'=>'saved']),false),409);
$o=$m->command($buyer,['id'=>Market::id(),'type'=>'propose','listing_id'=>$updated['id'],'listing_revision'=>2,'price'=>$gold],false);
$m->command($seller,['id'=>Market::id(),'type'=>'accept','offer_id'=>$o['offer_id'],'expected_revision'=>1],false);
$renew=$reprice;$renew['id']=Market::id();$renew['expected_revision']=2;$m->publish($seller,$renew,false);
check($m->snapshot($buyer,false)['listings']===[]); // An accepted negotiation is not reopened by a heartbeat.
$remove=$renew;$remove['id']=Market::id();$remove['expected_revision']=3;$remove['items']=[];$remove['enabled']=false;$m->publish($seller,$remove,false);
check($m->run('SELECT status FROM offers WHERE id=?',[$o['offer_id']])->fetchColumn()==='superseded');
check($m->snapshot($seller,false)['history']===[]&&$m->run('SELECT owner FROM items WHERE id=?',[$listing['item_id']])->fetchColumn()===$seller['id']);
$restore=$reprice;$restore['id']=Market::id();$restore['expected_revision']=4;$m->publish($seller,$restore,false);
$returned=$m->snapshot($buyer,false)['listings'][0];check($returned['id']===$listing['id']&&$returned['item_id']===$listing['item_id']);
$bad=$restore;$bad['id']=Market::id();$bad['expected_revision']=5;$bad['items'][0]['saved_id']=12;
refuses(fn()=>$m->publish($seller,$bad,false),409);check($m->publication($seller,false)['revision']===5);
$stale=$restore;$stale['id']=Market::id();$stale['expected_revision']=5;$stale['captured_at']-=10;refuses(fn()=>$m->publish($seller,$stale,false),409);
$duplicate=$restore;$duplicate['id']=Market::id();$duplicate['expected_revision']=5;$duplicate['items'][]=$duplicate['items'][0];refuses(fn()=>$m->publish($seller,$duplicate,false),422);
$collision=$restore;$collision['id']=Market::id();$collision['expected_revision']=5;$second=$collision['items'][0];$second['item_hex']='abce';$collision['items'][]=$second;$m->publish($seller,$collision,false);
$feed=$m->snapshot($buyer,false)['listings'];check(count($feed)===2&&count(array_unique(array_column($feed,'item_id')))===2);
$m->run('UPDATE published_items SET expires=?',[time()-1]);check($m->snapshot($buyer,false)['listings']===[]);
refuses(fn()=>$m->command($buyer,['id'=>Market::id(),'type'=>'propose','listing_id'=>$feed[0]['id'],'listing_revision'=>(int)$feed[0]['revision'],'price'=>$gold],false),409);
$hc=$pub;$hc['hardcore']=true;$hc['owner']=$buyer['id'];$m->publish($seller,$hc,true);check($m->snapshot($seller,true)['listings'][0]['own']);check($m->publication($seller,true)['revision']===1);
$buyerPub=$pub;$buyerPub['owner']=$seller['id'];$buyerPub['items'][0]['item_hex']='abcf';$m->publish($buyer,$buyerPub,false);check($m->snapshot($buyer,false)['listings'][0]['own']);
$t=$m->user('Development trader','tester');$tester=$m->auth($t['token']);$testPub=$pub;$testPub['expected_revision']=0;$testPub['items'][0]['item_hex']='abdd';$m->publish($tester,$testPub,false);
$testListing=array_values(array_filter($m->snapshot($seller,false)['listings'],fn($l)=>$l['seller_id']===$tester['id']))[0];check($testListing['development']);
$admin=$m->auth($m->user('Operator','admin')['token']);refuses(fn()=>$m->sale($admin,['item_id'=>$testListing['item_id'],'seller'=>$tester['id'],'buyer'=>$seller['id'],'price'=>$gold,'receipt'=>'fake-sale'],false),422);
check($m->snapshot($seller,false)['history']===[]);
echo "Publication: authenticated ownership, idempotency, stable revisions/ledger IDs, changed-price and removal logs, accepted-offer invalidation, expiry, seed collisions and mode separation passed.\n";
