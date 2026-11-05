<?php
declare(strict_types=1);
require __DIR__.'/../tools/marketplace/server/Market.php';
function check(bool $v):void {if(!$v)throw new RuntimeException('Escrow assertion failed at '.json_encode(debug_backtrace(DEBUG_BACKTRACE_IGNORE_ARGS,1)));}
function refuses(callable $fn,int $status):void {try{$fn();}catch(MarketError $e){if($e->status!==$status)throw $e;return;}throw new RuntimeException('Unexpectedly accepted operation.');}
$m=new Market(new PDO('sqlite::memory:'));$m->migrate();$m->migrate();
$s=$m->user('Seller');$b=$m->user('Buyer');$c=$m->user('Buyer 2');$t=$m->user('Development','tester');$catalog=str_repeat('a',64);
$sa=$m->authAgent($m->pairAgent($s['id'],$catalog)['token']);$ba=$m->authAgent($m->pairAgent($b['id'],$catalog)['token']);$ca=$m->authAgent($m->pairAgent($c['id'],$catalog)['token']);
refuses(fn()=>$m->authAgent($s['token']),401);refuses(fn()=>$m->pairAgent($t['id'],$catalog),422);
$gold=[['kind'=>'gold','code'=>'gld','label'=>'Gold','quality'=>2,'index'=>-1,'ethereal'=>-1,'quantity'=>1000]];
$page=[['page'=>11,'before'=>str_repeat('b',64),'after'=>str_repeat('c',64)]];
$item=['page'=>11,'tab'=>1,'saved_id'=>0,'quality'=>7,'index'=>42,'code'=>'rin','ethereal'=>0,'identified'=>1,'quantity'=>1,'quantity_bit'=>-1,'item_hex'=>str_repeat('ab',24),'price'=>$gold];
$call=fn($a,$d,$hc=false)=>$m->custody($a,$d+['catalog'=>$catalog],$hc);
$pub=['schema'=>1,'game_mode'=>'2.4','mod'=>'RMD-MP','hardcore'=>false,'custody'=>'local_save','purchasable'=>false,'id'=>Market::id(),'captured_at'=>time(),'expected_revision'=>0,'page'=>11,'enabled'=>true,'source'=>'saved',
    'items'=>[array_replace($item,['ethereal'=>false,'socketed_items'=>[]])]];
$m->publish($s,$pub,false);$legacy=$m->snapshot($t,false)['listings'][0];
$testOffer=$m->command($t,['id'=>Market::id(),'type'=>'propose','listing_id'=>$legacy['id'],'listing_revision'=>1,'price'=>$gold],false);
$m->command($s,['id'=>Market::id(),'type'=>'accept','offer_id'=>$testOffer['offer_id'],'expected_revision'=>1],false);
$deposit=function($a,$items=[],$balances=[])use($call,$page){$id=Market::id();$d=['id'=>$id,'type'=>'deposit_prepare','items'=>$items,'balances'=>$balances,'pages'=>$page];$r=$call($a,$d);check($r===$call($a,$d));$call($a,['id'=>$id,'type'=>'write_authorize','pages'=>$page]);$r=$call($a,['id'=>$id,'type'=>'commit','pages'=>$page]);check($r===$call($a,['id'=>$id,'type'=>'commit','pages'=>$page]));return $id;};
$id=Market::id();$prepared=['id'=>$id,'type'=>'deposit_prepare','items'=>[$item],'pages'=>$page];
$call($sa,$prepared);check($m->snapshot($s,false)['listings']===[]);
refuses(fn()=>$call($ba,['id'=>$id,'type'=>'status']),404);refuses(fn()=>$call($sa,['id'=>$id,'type'=>'status'],true),404);
refuses(fn()=>$call($sa,['id'=>$id,'type'=>'commit','pages'=>$page]),409);
refuses(fn()=>$call($sa,array_replace($prepared,['items'=>[]])),409);
refuses(fn()=>$m->pairAgent($s['id'],$catalog),409);
$call($sa,['id'=>$id,'type'=>'write_authorize','pages'=>$page]);refuses(fn()=>$call($sa,['id'=>$id,'type'=>'cancel']),409);
$call($sa,['id'=>$id,'type'=>'commit','pages'=>$page]);
$feed=$m->snapshot($b,false)['listings'];check(count($feed)===1&&$feed[0]['custody']==='escrow'&&$feed[0]['purchasable']);$l=$feed[0];
check($l['id']===$legacy['id']&&$l['item_id']===$legacy['item_id']);check($m->run('SELECT status FROM offers WHERE id=?',[$testOffer['offer_id']])->fetchColumn()==='superseded');
$proposal=fn($actor,$listing,$price)=>$m->command($actor,['id'=>Market::id(),'type'=>'propose','listing_id'=>$listing['id'],'listing_revision'=>(int)$listing['revision'],'price'=>$price],false);
$offer=$proposal($b,$l,$gold);$m->command($s,['id'=>Market::id(),'type'=>'accept','offer_id'=>$offer['offer_id'],'expected_revision'=>1],false);
check($m->snapshot($b,false)['offers'][0]['status']==='accepted'&&$m->snapshot($b,false)['history']===[]);
$funding=$call($ba,['id'=>Market::id(),'type'=>'delivery_queue'])['funding_requests'];check(count($funding)===1&&$funding[0]['id']===$offer['offer_id']&&$funding[0]['revision']===2&&$funding[0]['price']==$gold);
$staleFunding=['id'=>Market::id(),'type'=>'deposit_prepare','funding_offer'=>$offer['offer_id'],'funding_revision'=>1,'items'=>[],'balances'=>$gold,'pages'=>$page];
refuses(fn()=>$call($ba,$staleFunding),409);refuses(fn()=>$call($ba,['id'=>$staleFunding['id'],'type'=>'status']),404);

$deposit($ba,[],$gold);$snap=$m->snapshot($b,false);check($snap['offers'][0]['status']==='completed'&&count($snap['history'])===1);
check($m->run('SELECT owner FROM items WHERE id=?',[$l['item_id']])->fetchColumn()===$b['id']);
check($m->snapshot($s,false)['escrow']['balances'][0]['quantity']===1000);
check($m->snapshot($b,false)['escrow']['balances']===[]);
$staleFunding['id']=Market::id();$staleFunding['funding_revision']=2;refuses(fn()=>$call($ba,$staleFunding),409);

// Claim reservations remove assets from spendable funds, and failed capacity can cancel before authorization.
$claim=Market::id();$r=$call($ba,['id'=>$claim,'type'=>'claim_prepare']);check(count($r['packet']['items'])===1);
check($m->snapshot($b,false)['escrow']['items'][0]['state']==='claiming');
$call($ba,['id'=>$claim,'type'=>'cancel']);check($m->snapshot($b,false)['escrow']['items'][0]['state']==='available');
$claim=Market::id();$r=$call($ba,['id'=>$claim,'type'=>'claim_prepare']);$placed=[['item'=>$l['item_id'],'item_hex'=>$item['item_hex']]];
$bad=$placed;$bad[0]['item_hex']='ff'.substr($item['item_hex'],2);
refuses(fn()=>$call($ba,['id'=>$claim,'type'=>'write_authorize','pages'=>$page,'delivered'=>$bad]),422);
$call($ba,['id'=>$claim,'type'=>'write_authorize','pages'=>$page,'delivered'=>$placed]);
$call($ba,['id'=>$claim,'type'=>'commit','pages'=>$page]);check($m->snapshot($b,false)['escrow']['items']===[]);
// Re-deposit preserves the ledger and its history; matching seed alone does not merge other items.
$resale=$item;$resale['ledger_id']=$l['item_id'];$deposit($ba,[$resale]);
check((int)$m->run('SELECT COUNT(*) FROM items')->fetchColumn()===1);
refuses(fn()=>$deposit($sa,[$item]),409);$item['item_hex']=str_repeat('ac',24);$deposit($sa,[$item]);check((int)$m->run('SELECT COUNT(*) FROM items')->fetchColumn()===2);
// Exact automatic approval, even if two funded buyers target the same listing: one atomic settlement.
$m->command($s,['id'=>Market::id(),'type'=>'preferences','auto_trade'=>true],false);
$deposit($ca,[],$gold);$deposit($ba,[],$gold);
$own=array_values(array_filter($m->snapshot($b,false)['listings'],fn($r)=>$r['seller_id']===$s['id']));
$r=$proposal($c,$own[0],$gold);check($r['status']==='completed');
refuses(fn()=>$proposal($b,$own[0],$gold),409);
check((int)$m->run('SELECT COUNT(*) FROM settlements')->fetchColumn()===2);
// A counter differs from the ask: automatic approval must not accept it.
$item['item_hex']=str_repeat('ad',24);$deposit($sa,[$item]);$own=array_values(array_filter($m->snapshot($b,false)['listings'],fn($r)=>$r['seller_id']===$s['id']));$cheap=$gold;$cheap[0]['quantity']=999;
$r=$proposal($b,$own[0],$cheap);check($r['status']==='pending');
// Listed goods cannot be spent as payment; unlisting requires ownership and invalidates offers.
refuses(fn()=>$m->command($b,['id'=>Market::id(),'type'=>'escrow_return','item_id'=>$own[0]['item_id']],false),409);
$m->command($s,['id'=>Market::id(),'type'=>'escrow_return','item_id'=>$own[0]['item_id']],false);
check($m->run('SELECT status FROM offers WHERE id=?',[$r['offer_id']])->fetchColumn()==='superseded');
// Bag resources and two different unique items are AND terms, never OR terms.
$bag=['kind'=>'bag','code'=>'Z01','label'=>'Rune points','stat'=>'RB_High','quality'=>2,'index'=>-1,'ethereal'=>-1,'quantity'=>40];
$unique=['code'=>'rin','label'=>'Unique ring','quality'=>7,'index'=>42,'ethereal'=>-1,'quantity'=>2];
$terms=[$bag,$unique];$special=$item;$special['item_hex']=str_repeat('ae',24);$special['price']=$terms;$special['code']='amu';$deposit($sa,[$special]);
$deposit($ca,[] ,[$bag]);$own=array_values(array_filter($m->snapshot($c,false)['listings'],fn($r)=>$r['seller_id']===$s['id']));
$r=$proposal($c,$own[0],$terms);check($r['status']==='pending'); // only one matching ring held
$payment=$item;$payment['item_hex']=str_repeat('af',24);unset($payment['price']);$deposit($ca,[$payment]);check($m->run('SELECT status FROM offers WHERE id=?',[$r['offer_id']])->fetchColumn()==='completed');
check($m->snapshot($s,true)['history']===[]);
// A partial normal stack is allocated without losing or duplicating units.
$stack=$item;unset($stack['price']);$stack['code']='r33';$stack['quality']=2;$stack['index']=-1;$stack['quantity']=100;$stack['quantity_bit']=100;
$bytes=str_repeat("\0",24);for($bit=0;$bit<9;$bit++){if((100>>$bit)&1){$p=intdiv(100+$bit,8);$bytes[$p]=chr(ord($bytes[$p])|(1<<((100+$bit)%8)));}}$stack['item_hex']=bin2hex($bytes);
$deposit($ba,[$stack]);$runes=[['code'=>'r33','label'=>'Zod Rune','quality'=>2,'index'=>-1,'ethereal'=>-1,'quantity'=>40]];
$runeSale=$item;$runeSale['item_hex']=str_repeat('ba',24);$runeSale['price']=$runes;$deposit($sa,[$runeSale]);$own=array_values(array_filter($m->snapshot($b,false)['listings'],fn($l)=>$l['seller_id']===$s['id']));
$r=$proposal($b,$own[0],$runes);check($r['status']==='completed');
$quantities=$m->run("SELECT metadata FROM escrow_items e JOIN items i ON i.id=e.item WHERE i.code='r33'")->fetchAll(PDO::FETCH_COLUMN);$total=0;$units=[];
foreach($quantities as $row){$q=json_decode($row,true)['quantity'];$total+=$q;$units[]=$q;}sort($units);check($total===100&&$units===[40,60]);
// Delivery can take just what fits, leaving the rest spendable in escrow.
$claim=Market::id();$cap=$gold;$cap[0]['quantity']=100;
$r=$call($sa,['id'=>$claim,'type'=>'claim_prepare','item_limit'=>0,'balance_caps'=>$cap]);check($r['packet']['items']===[]&&$r['packet']['balances'][0]['quantity']===100);
$call($sa,['id'=>$claim,'type'=>'write_authorize','pages'=>$page,'delivered'=>[]]);$call($sa,['id'=>$claim,'type'=>'commit','pages'=>$page]);
check((int)$m->run("SELECT quantity FROM escrow_balances WHERE owner=? AND kind='gold'",[$s['id']])->fetchColumn()===1900);
refuses(fn()=>$call($sa,['id'=>Market::id(),'type'=>'claim_prepare','item_limit'=>0,'balance_caps'=>[]]),409);
// Incoming proceeds fund an already-accepted purchase without another PC request.
$aa=$m->user('Cascade seller');$bb=$m->user('Cascade buyer');$cc=$m->user('Cascade merchant');
$aaa=$m->authAgent($m->pairAgent($aa['id'],$catalog)['token']);$bba=$m->authAgent($m->pairAgent($bb['id'],$catalog)['token']);$cca=$m->authAgent($m->pairAgent($cc['id'],$catalog)['token']);
$m->command($aa,['id'=>Market::id(),'type'=>'preferences','auto_trade'=>true],false);
$item['item_hex']=str_repeat('bb',24);$deposit($aaa,[$item]);$merchant=$item;$merchant['item_hex']=str_repeat('bc',24);$merchant['code']='amu';$deposit($cca,[$merchant]);
$feed=$m->snapshot($aa,false)['listings'];$sell=array_values(array_filter($feed,fn($l)=>$l['seller_id']===$aa['id']))[0];$buy=array_values(array_filter($feed,fn($l)=>$l['seller_id']===$cc['id']))[0];
$want=$proposal($aa,$buy,$gold);$m->command($cc,['id'=>Market::id(),'type'=>'accept','offer_id'=>$want['offer_id'],'expected_revision'=>1],false);
$fund=$proposal($bb,$sell,$gold);check($fund['status']==='pending');$deposit($bba,[],$gold);
check($m->run('SELECT status FROM offers WHERE id=?',[$want['offer_id']])->fetchColumn()==='completed');
check($m->run('SELECT status FROM offers WHERE id=?',[$fund['offer_id']])->fetchColumn()==='completed');
check((int)$m->run("SELECT quantity FROM escrow_balances WHERE owner=? AND kind='gold'",[$cc['id']])->fetchColumn()===1000);
echo "Escrow: separate agent authentication, staged deposits, crash-safe retries, manual/automatic atomic settlement, insufficient-fund waits, claim reservations, immutable delivery properties, ledger resale, seed collisions, AND barter and mode isolation passed.\n";
