<?php
declare(strict_types=1);
require __DIR__.'/../tools/marketplace/server/Market.php';
function check(bool $ok):void{if(!$ok)throw new RuntimeException('Transfer assertion failed at '.json_encode(debug_backtrace(DEBUG_BACKTRACE_IGNORE_ARGS,1)));}
function refuses(callable $f,int $status):void{try{$f();}catch(MarketError $e){check($e->status===$status);return;}throw new RuntimeException('Invalid transfer allowed.');}
$m=new Market(new PDO('sqlite::memory:'));$m->migrate();
$seller=$m->user('Gift sender');$buyer=$m->user('Gift receiver');$other=$m->user('Other claimant');$unlinked=$m->user('Unlinked');$cat=str_repeat('a',64);
foreach([$seller,$buyer,$other] as $actor)$m->browserLink(['code'=>$m->browserTicket($actor)['code']]);
$sa=$m->authAgent($m->pairAgent($seller['id'],$cat)['token']);$ba=$m->authAgent($m->pairAgent($buyer['id'],$cat)['token']);
$gold=fn($n)=>['kind'=>'gold','code'=>'gld','label'=>'Gold','quality'=>2,'index'=>-1,'ethereal'=>-1,'quantity'=>$n];
$bag=fn($n)=>['kind'=>'bag','stat'=>'RB_High','code'=>'Z01','label'=>'High rune points','quality'=>2,'index'=>-1,'ethereal'=>-1,'quantity'=>$n];
$pages=[['page'=>11,'before'=>str_repeat('b',64),'after'=>str_repeat('c',64)]];$call=fn($agent,$data)=>$m->custody($agent,$data+['catalog'=>$cat],false);
$deposit=function($items=[],$balances=[])use($call,$sa,$pages){$id=Market::id();$call($sa,['id'=>$id,'type'=>'deposit_prepare','items'=>$items,'balances'=>$balances,'pages'=>$pages]);$call($sa,['id'=>$id,'type'=>'write_authorize','pages'=>$pages]);$call($sa,['id'=>$id,'type'=>'commit','pages'=>$pages]);};
$item=['page'=>11,'tab'=>1,'saved_id'=>0,'quality'=>6,'index'=>-1,'code'=>'rin','ethereal'=>0,'identified'=>1,'quantity'=>1,'quantity_bit'=>-1,'item_hex'=>str_repeat('cb',24),'price'=>Market::freePrice()];
check(Market::price(Market::freePrice())===Market::freePrice());refuses(fn()=>Market::price([Market::freePrice()[0],$gold(1)]),422);
refuses(fn()=>Market::price([array_replace(Market::freePrice()[0],['quantity'=>2])]),422);
$deposit([$item],[$gold(200),$bag(100)]);$l=$m->snapshot($buyer,false)['listings'][0];
$claim=['id'=>Market::id(),'type'=>'propose','listing_id'=>$l['id'],'listing_revision'=>1,'price'=>Market::freePrice()];
refuses(fn()=>$m->command($buyer,array_replace($claim,['price'=>[$gold(1)]]),false),422);
$r=$m->command($buyer,$claim,false);check($r['status']==='completed'&&$r===$m->command($buyer,$claim,false));
refuses(fn()=>$m->command($other,array_replace($claim,['id'=>Market::id()]),false),409);
check($m->run('SELECT owner FROM items WHERE id=?',[$l['item_id']])->fetchColumn()===$buyer['id']);
check($m->snapshot($seller,false)['escrow']['balances'][0]['quantity']>0); // Free claim charges nothing.
$item['item_hex']=str_repeat('cc',24);$item['price']=[$gold(500)];$deposit([$item]);$listing=$m->snapshot($other,false)['listings'][0];
$offer=$m->command($other,['id'=>Market::id(),'type'=>'propose','listing_id'=>$listing['id'],'listing_revision'=>1,'price'=>[$gold(50)]],false);
$gift=['id'=>Market::id(),'type'=>'gift_item','item_id'=>$listing['item_id'],'to'=>$buyer['id'],'message'=>'Enjoy the ring!'];
refuses(fn()=>$m->command($other,$gift,false),409);refuses(fn()=>$m->command($seller,array_replace($gift,['to'=>$unlinked['id']]),false),422);
refuses(fn()=>$m->command($seller,array_replace($gift,['to'=>$seller['id']]),false),422);refuses(fn()=>$m->command($seller,$gift,true),409);
$r=$m->command($seller,$gift,false);check($r['status']==='completed'&&$r===$m->command($seller,$gift,false));
check($m->run('SELECT owner FROM items WHERE id=?',[$listing['item_id']])->fetchColumn()===$buyer['id']);
check($m->run('SELECT status FROM offers WHERE id=?',[$offer['offer_id']])->fetchColumn()==='superseded');
check(count($m->snapshot($buyer,false)['history'])===2&&count($m->snapshot($buyer,false)['listings'])===0);
check((bool)array_filter($m->inbox($buyer)['messages'],fn($r)=>str_starts_with($r['body'],'Gift received:')));
$funds=['id'=>Market::id(),'type'=>'gift_balances','to'=>$buyer['name'],'balances'=>[$gold(120),$bag(40)]];
$r=$m->command($seller,$funds,false);check($r===$m->command($seller,$funds,false));
$balances=$m->snapshot($buyer,false)['escrow']['balances'];check(count($balances)===2);foreach($balances as $r)check($r['quantity']===($r['kind']==='gold'?120:40));
refuses(fn()=>$m->command($seller,array_replace($funds,['id'=>Market::id(),'balances'=>[$gold(81),$bag(1)]]),false),409);
check($balances===$m->snapshot($buyer,false)['escrow']['balances']);
refuses(fn()=>$m->command($seller,array_replace($funds,['id'=>Market::id(),'balances'=>Market::freePrice()]),false),422);
refuses(fn()=>$m->command($seller,['id'=>Market::id(),'type'=>'wallet_withdraw','balances'=>Market::freePrice()],false),422);
refuses(fn()=>$deposit([],Market::freePrice()),422);
check($m->inbox($unlinked)['messages']===[]);
echo "Transfers: explicit free price, first claimant wins without payment/approval, idempotent private gifts, exact item ownership/history, recipient validation, scope/owner isolation, gold+bag conservation, insufficient-fund rollback and free-marker rejection in custody/wallet passed.\n";
