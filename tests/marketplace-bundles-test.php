<?php
declare(strict_types=1);
require __DIR__.'/../tools/marketplace/server/Market.php';
function check(bool $ok):void{if(!$ok)throw new RuntimeException('Bundle check failed at '.json_encode(debug_backtrace(DEBUG_BACKTRACE_IGNORE_ARGS,1)));}
function refuses(callable $f,int $status):void{try{$f();}catch(MarketError $e){check($e->status===$status);return;}throw new RuntimeException('Unsafe bundle action allowed.');}
$m=new Market(new PDO('sqlite::memory:'));$m->migrate();$m->migrate();$seller=$m->user('Bundle seller');$buyer=$m->user('Bundle buyer');$other=$m->user('Second buyer');$cat=str_repeat('a',64);
foreach([$seller,$buyer,$other] as $a)$m->browserLink(['code'=>$m->browserTicket($a)['code']]);
$sa=$m->authAgent($m->pairAgent($seller['id'],$cat)['token']);$ba=$m->authAgent($m->pairAgent($buyer['id'],$cat)['token']);
$gold=fn($n)=>['kind'=>'gold','code'=>'gld','label'=>'Gold','quality'=>2,'index'=>-1,'ethereal'=>-1,'quantity'=>$n];$pages=[['page'=>11,'before'=>str_repeat('b',64),'after'=>str_repeat('c',64)]];
$call=fn($a,$d)=>$m->custody($a,$d+['catalog'=>$cat],false);
$deposit=function($a,$items=[],$balances=[])use($call,$pages){$id=Market::id();$call($a,['type'=>'deposit_prepare','id'=>$id,'items'=>$items,'balances'=>$balances,'pages'=>$pages]);$call($a,['type'=>'write_authorize','id'=>$id,'pages'=>$pages]);$call($a,['type'=>'commit','id'=>$id,'pages'=>$pages]);};
$item=['page'=>11,'tab'=>1,'saved_id'=>0,'quality'=>6,'index'=>-1,'code'=>'rin','ethereal'=>0,'identified'=>1,'quantity'=>1,'quantity_bit'=>-1,'item_hex'=>str_repeat('cb',24)];
$deposit($sa,[$item,array_replace($item,['item_hex'=>str_repeat('cc',24),'code'=>'amu'])]);$deposit($ba,[],[$gold(1000)]);
$members=array_column($m->browserSnapshot($seller,false)['escrow']['items'],'item_id');
$bundle=['id'=>Market::id(),'type'=>'bundle_create','title'=>'A complete starter pack','items'=>$members,'price'=>[$gold(100)]];
refuses(fn()=>$m->command($other,$bundle,false),409);refuses(fn()=>$m->command($seller,$bundle,true),409);
refuses(fn()=>$m->command($seller,array_replace($bundle,['items'=>[$members[0],$members[0]]]),false),422);
$r=$m->command($seller,$bundle,false);check($r===$m->command($seller,$bundle,false));$listing=$r['listing_id'];
check(count($m->browserSnapshot(null,false)['listings'])===1&&count($m->browserSnapshot(null,false)['listings'][0]['bundle']['members'])===2);
check($m->snapshot($buyer,false)['listings']===[]); // older native HUDs cannot accidentally accept unseen bundle terms
foreach($members as $id){refuses(fn()=>$m->command($seller,['id'=>Market::id(),'type'=>'escrow_return','item_id'=>$id],false),409);refuses(fn()=>$m->command($seller,['id'=>Market::id(),'type'=>'gift_item','item_id'=>$id,'to'=>$other['id']],false),409);}
$o=$m->command($buyer,['id'=>Market::id(),'type'=>'propose','listing_id'=>$listing,'listing_revision'=>1,'price'=>[$gold(100)]],false);
$conversation=$m->privateConversation($seller,false,$o['offer_id']);check(count($conversation['bundle']['members'])===2);
// A forced failure after the first member and payment have moved must roll
// the entire transaction back, including acceptance and the other member.
if($m->db->getAttribute(PDO::ATTR_DRIVER_NAME)==='sqlite'){
    $m->db->exec("CREATE TEMP TRIGGER bundle_transfer_fault BEFORE UPDATE OF owner ON escrow_items WHEN OLD.item='".$members[1]."' AND NEW.owner='".$buyer['id']."' BEGIN SELECT RAISE(ABORT,'simulated interrupted transfer'); END");
    try{$m->command($seller,['id'=>Market::id(),'type'=>'accept','offer_id'=>$o['offer_id'],'expected_revision'=>1],false);throw new RuntimeException('Injected failure was ignored.');}catch(PDOException $e){check(str_contains($e->getMessage(),'simulated interrupted transfer'));}
    $m->db->exec('DROP TRIGGER bundle_transfer_fault');
    check($m->run('SELECT status FROM offers WHERE id=?',[$o['offer_id']])->fetchColumn()==='pending');
    check((int)$m->run('SELECT quantity FROM escrow_balances WHERE owner=? AND kind=?',[$buyer['id'],'gold'])->fetchColumn()===1000);
    foreach($members as $id)check($m->run('SELECT owner FROM items WHERE id=?',[$id])->fetchColumn()===$seller['id']);
}
// A lost or changed member must prevent every debit and ownership transfer.
$m->run("UPDATE escrow_items SET state='available' WHERE item=?",[$members[1]]);
$accept=['id'=>Market::id(),'type'=>'accept','offer_id'=>$o['offer_id'],'expected_revision'=>1];$agreed=$m->command($seller,$accept,false);check($agreed['status']==='accepted');
check((int)$m->run('SELECT quantity FROM escrow_balances WHERE owner=? AND kind=?',[$buyer['id'],'gold'])->fetchColumn()===1000);
foreach($members as $id)check($m->run('SELECT owner FROM items WHERE id=?',[$id])->fetchColumn()===$seller['id']);
$m->run("UPDATE escrow_items SET state='listed' WHERE item=?",[$members[1]]);
$m->command($seller,['id'=>Market::id(),'type'=>'preferences','auto_trade'=>true],false);
check($m->run('SELECT status FROM offers WHERE id=?',[$o['offer_id']])->fetchColumn()==='completed');
check((int)$m->run('SELECT COUNT(*) FROM settlements')->fetchColumn()===1);
check((int)$m->run("SELECT COUNT(*) FROM events WHERE kind='completed_sale'")->fetchColumn()===1);
check((int)$m->run("SELECT COUNT(*) FROM events WHERE kind='bundle_sale'")->fetchColumn()===1);
foreach($members as $id){check($m->run('SELECT owner FROM items WHERE id=?',[$id])->fetchColumn()===$buyer['id']);check(count($m->historyPage(false,['item_id'=>$id])['rows'])===1);}
check($m->historyPage(false,['code'=>'rin','similar'=>true,'quality'=>6,'item_index'=>-1,'ethereal'=>0,'sockets'=>0,'averages'=>true])['averages']['sales']===0);
refuses(fn()=>$m->command($other,['id'=>Market::id(),'type'=>'propose','listing_id'=>$listing,'listing_revision'=>1,'price'=>[$gold(100)]],false),409);
$a=$m->activity($buyer,false);check($a['summary']['purchases']===1&&$a['summary']['deliveries']===2&&$a['sales'][0]['progress']==='ready');
// Simulate a full stash/failed placement: cancel only a prepared claim, then
// retry. Once authorized, the same durable receipt must be recovered instead.
$claim=Market::id();$packet=$call($ba,['id'=>$claim,'type'=>'claim_prepare','item_ids'=>$members,'balance_caps'=>[]]);check(count($packet['packet']['items'])===2);
$call($ba,['id'=>$claim,'type'=>'cancel']);check($call($ba,['id'=>$claim,'type'=>'status'])['status']==='cancelled');
$claim=Market::id();$request=['id'=>$claim,'type'=>'claim_prepare','item_ids'=>$members,'balance_caps'=>[]];$ticket=$call($ba,$request);check($ticket===$call($ba,$request));
$delivered=array_map(fn($r)=>['item'=>$r['item'],'item_hex'=>$r['item_hex']],$ticket['packet']['items']);$proof=[['page'=>2,'before'=>str_repeat('d',64),'after'=>str_repeat('e',64)]];
$call($ba,['id'=>$claim,'type'=>'write_authorize','pages'=>$proof,'delivered'=>$delivered]);refuses(fn()=>$call($ba,['id'=>$claim,'type'=>'cancel']),409);
refuses(fn()=>$call($ba,['id'=>$claim,'type'=>'commit','pages'=>$pages]),409);check($call($ba,['id'=>$claim,'type'=>'status'])['status']==='writing');
$commit=['id'=>$claim,'type'=>'commit','pages'=>$proof];$done=$call($ba,$commit);check($done===$call($ba,$commit));
check($m->activity($buyer,false)['sales'][0]['progress']==='delivered');check(count(array_filter($m->inbox($buyer)['messages'],fn($r)=>str_starts_with($r['body'],'Bundle delivered:')))===1);check((int)$m->run("SELECT COUNT(*) FROM events WHERE kind='delivered'")->fetchColumn()===2);
check((int)$m->run('SELECT quantity FROM escrow_balances WHERE owner=? AND kind=?',[$buyer['id'],'gold'])->fetchColumn()===900);
// Withdrawal of a still-open bundle releases all members and invalidates bids.
$deposit($sa,[array_replace($item,['item_hex'=>str_repeat('ce',24)]),array_replace($item,['item_hex'=>str_repeat('cf',24)])]);$ids=array_column($m->browserSnapshot($seller,false)['escrow']['items'],'item_id');$b=$m->command($seller,array_replace($bundle,['id'=>Market::id(),'items'=>$ids]),false);
$offer=$m->command($other,['id'=>Market::id(),'type'=>'propose','listing_id'=>$b['listing_id'],'listing_revision'=>1,'price'=>[$gold(90)]],false);
refuses(fn()=>$m->command($seller,['id'=>Market::id(),'type'=>'bundle_dissolve','listing_id'=>$b['listing_id'],'expected_revision'=>2],false),409);
$unbundle=['id'=>Market::id(),'type'=>'bundle_dissolve','listing_id'=>$b['listing_id'],'expected_revision'=>1];check($m->command($seller,$unbundle,false)===$m->command($seller,$unbundle,false));check($m->run('SELECT status FROM offers WHERE id=?',[$offer['offer_id']])->fetchColumn()==='superseded');
echo "Bundles: complete member ownership, single payment/statistics, per-item history, hidden legacy feed, missing-member conservation, mode/owner/stale guards, retries, capacity cancellation, authorized-write recovery and exactly-once delivery passed.\n";
