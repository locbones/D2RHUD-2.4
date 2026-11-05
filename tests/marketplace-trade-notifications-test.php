<?php
declare(strict_types=1);
require __DIR__.'/../tools/marketplace/server/Market.php';
function check(bool $ok):void{if(!$ok)throw new RuntimeException('Trade notification assertion failed at '.json_encode(debug_backtrace(DEBUG_BACKTRACE_IGNORE_ARGS,1)));}
$m=new Market(new PDO('sqlite::memory:'));$m->migrate();
$s=$m->user('Notification seller');$b=$m->user('Notification buyer');$c=$m->user('Other bidder');$outsider=$m->user('Uninvolved player');$admin=$m->user('Operator','admin');
$price=fn($n)=>[['kind'=>'gold','code'=>'gld','label'=>'Gold','quality'=>2,'index'=>-1,'ethereal'=>-1,'quantity'=>$n]];
$messages=fn($actor,$thread)=>array_values(array_filter($m->inbox($actor)['messages'],fn($r)=>$r['thread']===$thread));
$last=fn($actor,$thread)=>array_slice($messages($actor,$thread),-1)[0];
$propose=fn($actor,$l,$n=100)=>$m->command($actor,['id'=>Market::id(),'type'=>'propose','listing_id'=>$l['listing_id'],'listing_revision'=>1,'price'=>$price($n),'message'=>'Please consider this price'],false);
$respond=fn($actor,$o,$type,$revision,$extra=[])=>$m->command($actor,['id'=>Market::id(),'type'=>$type,'offer_id'=>$o,'expected_revision'=>$revision]+$extra,false);
$hex=str_repeat('ab',24);
$l=$m->listing($admin,['owner'=>$s['id'],'item'=>['seed'=>0,'code'=>'rin','quality'=>6,'index'=>-1,'item_hex'=>$hex],'price'=>$price(100)],false);
$o=$propose($b,$l)['offer_id'];check(str_starts_with($last($s,$o)['body'],'New offer: Ring'));check(str_contains($last($s,$o)['body'],'Please consider this price'));check($messages($b,$o)===[]);
$counter=['id'=>Market::id(),'type'=>'counter','offer_id'=>$o,'expected_revision'=>1,'price'=>$price(120),'message'=>'Could you do this instead?'];
$r=$m->command($s,$counter,false);check($m->command($s,$counter,false)===$r);
check(count($messages($b,$o))===1&&str_starts_with($last($b,$o)['body'],'Counter-offer: Ring'));check(str_contains($last($b,$o)['body'],'120 × Gold')&&str_contains($last($b,$o)['body'],$counter['message']));
$respond($b,$o,'counter',2,['price'=>$price(110)]);check(str_starts_with($last($s,$o)['body'],'Counter-offer:'));
$respond($s,$o,'reject',3);check(str_starts_with($last($b,$o)['body'],'Offer declined:'));
$withdraw=$propose($b,$l)['offer_id'];$respond($b,$withdraw,'withdraw',1);check(str_starts_with($last($s,$withdraw)['body'],'Offer withdrawn:'));
$accepted=$propose($b,$l)['offer_id'];$competing=$propose($c,$l)['offer_id'];
$respond($s,$accepted,'accept',1);check(str_starts_with($last($b,$accepted)['body'],'Offer accepted:'));check(str_contains($last($c,$competing)['body'],'Another proposal was accepted'));
$m->command($s,['id'=>Market::id(),'type'=>'withdraw_physical','item_id'=>$l['item_id'],'item_hex'=>$hex],false);
check(str_starts_with($last($b,$accepted)['body'],'Offer closed:')&&str_contains($last($b,$accepted)['body'],'withdrew this item'));
// Automatic settlement confirms both participants, without sending a false
// cancellation for the winning offer. Competing bids are still closed.
$catalog=str_repeat('a',64);$sa=$m->authAgent($m->pairAgent($s['id'],$catalog)['token']);$ba=$m->authAgent($m->pairAgent($b['id'],$catalog)['token']);
$pages=[['page'=>11,'before'=>str_repeat('b',64),'after'=>str_repeat('c',64)]];
$call=fn($agent,$packet)=>$m->custody($agent,$packet+['catalog'=>$catalog],false);
$deposit=function($agent,$items=[],$balances=[])use($call,$pages){$id=Market::id();$call($agent,['id'=>$id,'type'=>'deposit_prepare','pages'=>$pages,'items'=>$items,'balances'=>$balances]);$call($agent,['id'=>$id,'type'=>'write_authorize','pages'=>$pages]);$commit=['id'=>$id,'type'=>'commit','pages'=>$pages];$r=$call($agent,$commit);check($r===$call($agent,$commit));};
$item=['page'=>11,'tab'=>1,'saved_id'=>0,'quality'=>2,'index'=>-1,'code'=>'rin','ethereal'=>0,'identified'=>1,'quantity'=>1,'quantity_bit'=>-1,'item_hex'=>str_repeat('bc',24),'price'=>$price(100)];
$deposit($sa,[$item]);$deposit($ba,[],$price(100));
$listed=$m->run("SELECT id AS listing_id,item AS item_id FROM listings WHERE seller=? AND status='open'",[$s['id']])->fetch();
$loser=$propose($c,$listed,99)['offer_id'];
$m->command($s,['id'=>Market::id(),'type'=>'preferences','auto_trade'=>true],false);
$win=$propose($b,$listed)['offer_id'];
foreach([$s,$b] as $actor){$rows=$messages($actor,$win);check(count(array_filter($rows,fn($r)=>str_starts_with($r['body'],'Trade completed:')))===1);check(!array_filter($rows,fn($r)=>str_starts_with($r['body'],'Offer closed:')));}
check(str_contains($last($s,$win)['body'],'payment is available in Vault'));check($last($s,$win)['sender']===$b['id']);check($last($b,$win)['sender']===$s['id']);
check(str_starts_with($last($c,$loser)['body'],'Offer closed:'));check((int)$m->run('SELECT COUNT(*) FROM settlements')->fetchColumn()===1);
$ticket=Market::id();$claim=$call($ba,['id'=>$ticket,'type'=>'claim_prepare']);check(count($claim['packet']['items'])===1);
$placed=[['item'=>$listed['item_id'],'item_hex'=>$item['item_hex']]];$call($ba,['id'=>$ticket,'type'=>'write_authorize','pages'=>$pages,'delivered'=>$placed]);
$commit=['id'=>$ticket,'type'=>'commit','pages'=>$pages];$call($ba,$commit);$call($ba,$commit);
check(str_starts_with($last($b,$win)['body'],'Item delivered:'));check(count(array_filter($messages($b,$win),fn($r)=>str_starts_with($r['body'],'Item delivered:')))===1);
check(!array_filter($messages($s,$win),fn($r)=>str_starts_with($r['body'],'Item delivered:')));
// Repricing invalidates stale terms and notifies the affected bidder privately.
$item['item_hex']=str_repeat('bd',24);$deposit($sa,[$item]);$listed=$m->run("SELECT id AS listing_id,item AS item_id FROM listings WHERE seller=? AND status='open'",[$s['id']])->fetch();
$stale=$propose($b,$listed,50)['offer_id'];$m->command($s,['id'=>Market::id(),'type'=>'escrow_price','item_id'=>$listed['item_id'],'price'=>$price(200)],false);
check(str_starts_with($last($b,$stale)['body'],'Offer closed:'));check(str_contains($last($b,$stale)['body'],'changed or withdrew'));
check($m->inbox($outsider)['messages']===[]);
foreach($m->inbox($b)['messages'] as $row)check($row['kind']==='trade'&&str_ends_with($row['scope'],'|SC'));
echo "Trade notifications: labeled offers/counters/messages, accept/decline/withdraw, superseded and repriced terms, both-party automatic completion, guarded delivery, private recipients, reply targets, retry dedupe and no false winner cancellation passed.\n";

// Listing publication alone never notifies the other participants or outsiders.
$before=(int)$m->run('SELECT COUNT(*) FROM market_messages')->fetchColumn();
$item['item_hex']=str_repeat('ce',24);$deposit($sa,[$item]);
check((int)$m->run('SELECT COUNT(*) FROM market_messages')->fetchColumn()===$before);
check($m->notificationSettings($outsider['id'])===['game_direct'=>true,'game_trade'=>true,'game_search'=>false]);
// A misrouted thread must be suppressed even if its message recipient is set incorrectly.
$m->run('INSERT INTO market_messages (id,sender,recipient,scope,kind,thread,body,created) VALUES (?,?,?,?,?,?,?,?)',[Market::id(),$s['id'],$outsider['id'],'tcpip|RMD-MP|2.4|SC','trade',$win,'Unrelated trade',time()]);
check($m->inbox($outsider,0,'game')['messages']===[]);
echo "Trade alert privacy: publication is silent, discovery is opt-in, unrelated threads suppressed.\n";
