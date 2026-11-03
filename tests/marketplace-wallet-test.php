<?php
declare(strict_types=1);
require __DIR__.'/../tools/marketplace/server/Market.php';
function check(bool $ok):void{if(!$ok)throw new RuntimeException('Wallet assertion failed at '.json_encode(debug_backtrace(DEBUG_BACKTRACE_IGNORE_ARGS,1)));}
function refuses(callable $fn,int $status):void{try{$fn();}catch(MarketError $e){check($e->status===$status);return;}throw new RuntimeException('Unexpected wallet operation allowed.');}
$m=new Market(new PDO('sqlite::memory:'));$m->migrate();$m->migrate();
$seller=$m->user('Wallet seller');$buyer=$m->user('Wallet buyer');$other=$m->user('Other buyer');$catalog=str_repeat('a',64);
$sa=$m->authAgent($m->pairAgent($seller['id'],$catalog)['token']);$ba=$m->authAgent($m->pairAgent($buyer['id'],$catalog)['token']);$oa=$m->authAgent($m->pairAgent($other['id'],$catalog)['token']);
$gold=fn(int $q)=>['kind'=>'gold','code'=>'gld','label'=>'Gold','quality'=>2,'index'=>-1,'ethereal'=>-1,'quantity'=>$q];
$bag=fn(int $q)=>['kind'=>'bag','stat'=>'RB_High','code'=>'Z01','label'=>'High rune points','quality'=>2,'index'=>-1,'ethereal'=>-1,'quantity'=>$q];
$pages=[['page'=>11,'before'=>str_repeat('b',64),'after'=>str_repeat('c',64)]];
$call=fn($agent,$packet,$hc=false)=>$m->custody($agent,$packet+['catalog'=>$catalog],$hc);
$deposit=function($agent,$balances,$items=[])use($call,$pages){$id=Market::id();$call($agent,['id'=>$id,'type'=>'deposit_prepare','balances'=>$balances,'items'=>$items,'pages'=>$pages]);$call($agent,['id'=>$id,'type'=>'write_authorize','pages'=>$pages]);$call($agent,['id'=>$id,'type'=>'commit','pages'=>$pages]);};
$finish=function($agent,$ticket)use($call,$pages){$call($agent,['id'=>$ticket,'type'=>'write_authorize','pages'=>$pages,'delivered'=>[]]);$r=$call($agent,['id'=>$ticket,'type'=>'commit','pages'=>$pages]);check($r===$call($agent,['id'=>$ticket,'type'=>'commit','pages'=>$pages]));};
$balance=function($actor,$kind)use($m){$b=$m->snapshot($actor,false)['escrow']['balances'];foreach($b as $r)if($r['kind']===$kind)return $r['quantity'];return 0;};
$withdraw=fn($actor,$rows,$id=null)=>$m->command($actor,['id'=>$id??Market::id(),'type'=>'wallet_withdraw','balances'=>$rows],false);
$deposit($ba,[$gold(5000),$bag(100)]);
$requestId=Market::id();$r=$withdraw($buyer,[$gold(2000),$bag(40)],$requestId);check($r===$withdraw($buyer,[$gold(2000),$bag(40)],$requestId));
// Polling while the deposit page is loaded never clears its private snapshot.
$privateSettings=['page'=>11,'withdraw_page'=>2,'enabled'=>true,'auto_trade'=>false];
$privateItem=['page'=>11,'tab'=>1,'saved_id'=>0,'quality'=>7,'index'=>42,'code'=>'rin','ethereal'=>0,'identified'=>1,'quantity'=>1,'quantity_bit'=>-1,'item_hex'=>str_repeat('ab',24)];
$call($ba,['type'=>'web_sync','settings'=>$privateSettings,'inventory'=>[$privateItem]]);
$profileBefore=$m->run('SELECT * FROM web_profiles WHERE owner=?',[$buyer['id']])->fetch();
$queue=$call($ba,['id'=>Market::id(),'type'=>'delivery_queue']);check(count($queue['wallet_withdrawals'])===1&&$queue['wallet_withdrawals'][0]['id']===$requestId);
check($m->run('SELECT * FROM web_profiles WHERE owner=?',[$buyer['id']])->fetch()===$profileBefore);
check($call($oa,['id'=>Market::id(),'type'=>'delivery_queue'])===['delivery_items'=>[],'wallet_withdrawals'=>[]]);
check($call($ba,['id'=>Market::id(),'type'=>'delivery_queue'],true)===['delivery_items'=>[],'wallet_withdrawals'=>[]]);
refuses(fn()=>$m->custody($ba,['id'=>Market::id(),'type'=>'delivery_queue','catalog'=>str_repeat('f',64)],false),409);
check($balance($buyer,'gold')===3000&&$balance($buyer,'bag')===60);
refuses(fn()=>$withdraw($buyer,[$gold(3001)]),409);refuses(fn()=>$withdraw($buyer,[$gold(1),$bag(61)]),409);check($balance($buyer,'gold')===3000);
refuses(fn()=>$m->command($buyer,['id'=>$requestId,'type'=>'wallet_withdraw','balances'=>[$gold(2000),$bag(40)]],true),409);
refuses(fn()=>$withdraw($buyer,[$gold(0)]),422);refuses(fn()=>$withdraw($buyer,[$gold(1),$gold(1)]),422);
$physical=['code'=>'r33','label'=>'Zod Rune','quality'=>2,'index'=>-1,'ethereal'=>-1,'quantity'=>1];refuses(fn()=>$withdraw($buyer,[$physical]),422);
$cancel=fn($actor,$wid,$revision)=>$m->command($actor,['id'=>Market::id(),'type'=>'wallet_cancel','withdrawal_id'=>$wid,'expected_revision'=>$revision],false);
refuses(fn()=>$cancel($other,$requestId,1),404);refuses(fn()=>$call($oa,['id'=>Market::id(),'type'=>'claim_prepare','withdrawal_id'=>$requestId,'balance_caps'=>[$gold(2000)]]),404);
refuses(fn()=>$call($ba,['id'=>Market::id(),'type'=>'claim_prepare','withdrawal_id'=>$requestId,'balance_caps'=>[$gold(2000)]],true),404);
// No space cannot consume a request, create a ticket or change available funds.
$empty=Market::id();refuses(fn()=>$call($ba,['id'=>$empty,'type'=>'claim_prepare','withdrawal_id'=>$requestId,'balance_caps'=>[]]),409);refuses(fn()=>$call($ba,['id'=>$empty,'type'=>'status']),404);
check($m->snapshot($buyer,false)['escrow']['wallet']['withdrawals'][0]['revision']===1);
// A save plan reserves only the amount that fits. Cancel before authorization
// restores the request reservation, never its spendable balance.
$ticket=Market::id();$packet=$call($ba,['id'=>$ticket,'type'=>'claim_prepare','withdrawal_id'=>$requestId,'balance_caps'=>[$gold(700),$bag(10)]]);
check(array_column($packet['packet']['balances'],'quantity')===[700,10]);refuses(fn()=>$cancel($buyer,$requestId,2),409);
$call($ba,['id'=>$ticket,'type'=>'cancel']);check($balance($buyer,'gold')===3000&&$balance($buyer,'bag')===60);
$ticket=Market::id();$request=['id'=>$ticket,'type'=>'claim_prepare','withdrawal_id'=>$requestId,'balance_caps'=>[$gold(700),$bag(10)]];$packet=$call($ba,$request);check($packet===$call($ba,$request));
$finish($ba,$ticket);$row=$m->run('SELECT * FROM wallet_withdrawals WHERE id=?',[$requestId])->fetch();check($row['status']==='queued');
$remaining=json_decode($row['remaining'],true);check(array_column($remaining,'quantity')===[1300,30]);
refuses(fn()=>$cancel($buyer,$requestId,1),409);$cancel($buyer,$requestId,(int)$row['revision']);
check($balance($buyer,'gold')===4300&&$balance($buyer,'bag')===90); // Exactly the undelivered remainder returned.
// Withdrawal reservation cannot also fund a sale. Funding it later completes
// one sale; old bids cannot buy it a second time.
$item=['page'=>11,'tab'=>1,'saved_id'=>0,'quality'=>7,'index'=>42,'code'=>'rin','ethereal'=>0,'identified'=>1,'quantity'=>1,'quantity_bit'=>-1,'item_hex'=>str_repeat('ab',24),'price'=>[$gold(4000),$bag(90)]];
$deposit($sa,[],[$item]);$listing=$m->snapshot($buyer,false)['listings'][0];
$w=$withdraw($buyer,[$gold(1000)]);$proposal=$m->command($buyer,['id'=>Market::id(),'type'=>'propose','listing_id'=>$listing['id'],'listing_revision'=>1,'price'=>$item['price']],false);
$m->command($seller,['id'=>Market::id(),'type'=>'accept','offer_id'=>$proposal['offer_id'],'expected_revision'=>1],false);check((int)$m->run('SELECT COUNT(*) FROM settlements')->fetchColumn()===0);
$cancel($buyer,$w['withdrawal_id'],1);check((int)$m->run('SELECT COUNT(*) FROM settlements')->fetchColumn()===1);check($balance($buyer,'gold')===300&&$balance($seller,'gold')===4000);
check($balance($buyer,'bag')===0&&$balance($seller,'bag')===90);
refuses(fn()=>$m->command($other,['id'=>Market::id(),'type'=>'propose','listing_id'=>$listing['id'],'listing_revision'=>1,'price'=>$item['price']],false),409);
// Fully delivered resources cannot be cancelled or replayed. No unrelated
// purchased item is included in a resource-only delivery.
$w=$withdraw($seller,[$gold(4000),$bag(90)]);$ticket=Market::id();$packet=$call($sa,['id'=>$ticket,'type'=>'claim_prepare','withdrawal_id'=>$w['withdrawal_id'],'balance_caps'=>[$gold(4000),$bag(90)]]);check($packet['packet']['items']===[]);
$call($sa,['id'=>$ticket,'type'=>'write_authorize','pages'=>$pages,'delivered'=>[]]);refuses(fn()=>$call($sa,['id'=>$ticket,'type'=>'cancel']),409);$call($sa,['id'=>$ticket,'type'=>'commit','pages'=>$pages]);
refuses(fn()=>$cancel($seller,$w['withdrawal_id'],3),409);check($m->snapshot($seller,false)['escrow']['wallet']['withdrawals'][0]['status']==='completed');
check($m->browserSnapshot(null,false)['escrow']['wallet']===['withdrawals'=>[],'entries'=>[]]);check(!str_contains(json_encode($m->browserSnapshot($other,false)),$requestId));
$entries=$m->snapshot($buyer,false)['escrow']['wallet']['entries'];check(count(array_filter($entries,fn($e)=>$e['action']==='trade_payment'))===2);
check(count(array_filter($entries,fn($e)=>$e['action']==='withdrawal_cancelled'))===3);
echo "Wallet: atomic/idempotent reservations, invalid/overlapping inputs, owner/mode privacy, capacity waits, partial delivery, cancellation/recovery, held-fund sale blocking, automatic settlement after release, exact balances and activity passed.\n";
