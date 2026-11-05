<?php
declare(strict_types=1);
require __DIR__.'/../tools/marketplace/server/Market.php';
function check(bool $ok):void{if(!$ok)throw new RuntimeException('Owner audit assertion failed at '.json_encode(debug_backtrace(DEBUG_BACKTRACE_IGNORE_ARGS,1)));}
function refuses(callable $fn,int $status):void{try{$fn();}catch(MarketError $e){check($e->status===$status);return;}throw new RuntimeException('Unexpected owner audit access.');}
$m=new Market(new PDO('sqlite::memory:'));$m->migrate();$m->migrate();
$owner=$m->user('Owner');$seller=$m->user('Seller');$buyer=$m->user('Buyer');$other=$m->user('Owner');$m->grantOwner($owner['id']);$m->grantModerator($other['id']);$catalog=str_repeat('a',64);
$sa=$m->authAgent($m->pairAgent($seller['id'],$catalog)['token']);$ba=$m->authAgent($m->pairAgent($buyer['id'],$catalog)['token']);
$gold=['kind'=>'gold','code'=>'gld','label'=>'Gold','quality'=>2,'index'=>-1,'ethereal'=>-1,'quantity'=>100];
$rune=['kind'=>'item','code'=>'r33','label'=>'Zod Rune','quality'=>2,'index'=>-1,'ethereal'=>-1,'quantity'=>1];$price=[$gold,$rune];
$ring=['code'=>'rin','saved_id'=>14,'quality'=>6,'index'=>-1,'ethereal'=>0,'identified'=>1,'quantity'=>1,'quantity_bit'=>-1,'tab'=>1,'page'=>11,'item_hex'=>str_repeat('ab',24),'price'=>$price,'display'=>['level'=>88,'saved_properties'=>[['id'=>39,'value'=>17]]]];
$payment=array_replace($ring,['code'=>'r33','saved_id'=>15,'quality'=>2,'item_hex'=>str_repeat('cd',24)]);unset($payment['price']);
$pages=[['page'=>11,'before'=>str_repeat('b',64),'after'=>str_repeat('c',64)]];
$deposit=function($agent,$items,$balances=[])use($m,$catalog,$pages){$id=Market::id();foreach(['deposit_prepare','write_authorize','commit'] as $type)$m->custody($agent,['type'=>$type,'id'=>$id,'catalog'=>$catalog,'pages'=>$pages,'items'=>$items,'balances'=>$balances],false);};
$deposit($sa,[$ring]);$deposit($ba,[$payment],[$gold]);$listing=$m->snapshot($seller,false)['listings'][0];
$proposal=$m->command($buyer,['id'=>Market::id(),'type'=>'propose','listing_id'=>$listing['id'],'listing_revision'=>1,'price'=>$price],false);$id=$proposal['offer_id'];
$pending=$m->ownerTrade($owner,false,$id);check($pending['status']==='pending'&&$pending['assets']===[]&&str_contains($pending['evidence_note'],'has not settled'));
refuses(fn()=>$m->ownerTrade($other,false,$id),403);refuses(fn()=>$m->ownerTrades($buyer,false),403);refuses(fn()=>$m->ownerTrade($owner,true,$id),404);refuses(fn()=>(new Market($m->db,'tcpip','Other Mod'))->ownerTrade($owner,false,$id),404);
$m->command($seller,['id'=>Market::id(),'type'=>'accept','offer_id'=>$id,'expected_revision'=>1],false);
$settled=$m->ownerTrade($owner,false,$id);check($settled['status']==='completed'&&count($settled['assets'])===3&&$settled['exact_assets_recorded']);
$goods=current(array_filter($settled['assets'],fn($a)=>$a['leg']==='goods'));$payments=array_values(array_filter($settled['assets'],fn($a)=>$a['leg']==='payment'));
check($goods['recipient']['id']===$buyer['id']&&$goods['delivery']['state']==='pending'&&$goods['snapshot']['display']['saved_properties'][0]['value']===17);
check(count($payments)===2);check(current(array_filter($payments,fn($a)=>$a['kind']==='gold'))['delivery']['state']==='wallet');
$paidRune=current(array_filter($payments,fn($a)=>$a['kind']==='item'));check($paidRune['snapshot']['code']==='r33'&&$paidRune['recipient']['id']===$seller['id']);
check(!str_contains(json_encode($settled),'item_hex')&&!str_contains(json_encode($settled),'token_hash'));
// No delivery claim is confirmed before the durable commit acknowledgement.
$ticket=Market::id();$claim=$m->custody($ba,['id'=>$ticket,'type'=>'claim_prepare','catalog'=>$catalog,'item_ids'=>[$goods['item_id']],'balance_caps'=>[]],false)['packet'];
$writing=$m->ownerTrade($owner,false,$id);check(current(array_filter($writing['assets'],fn($a)=>$a['leg']==='goods'))['delivery']['state']==='delivering');
$m->custody($ba,['id'=>$ticket,'type'=>'cancel','catalog'=>$catalog],false);
check(current(array_filter($m->ownerTrade($owner,false,$id)['assets'],fn($a)=>$a['leg']==='goods'))['delivery']['state']==='pending');
$ticket=Market::id();$claim=$m->custody($ba,['id'=>$ticket,'type'=>'claim_prepare','catalog'=>$catalog,'item_ids'=>[$goods['item_id']],'balance_caps'=>[]],false)['packet'];
$proof=[['page'=>2,'before'=>str_repeat('d',64),'after'=>str_repeat('e',64)]];
$m->custody($ba,['id'=>$ticket,'type'=>'write_authorize','catalog'=>$catalog,'pages'=>$proof,'delivered'=>$claim['items']],false);
$commit=['id'=>$ticket,'type'=>'commit','catalog'=>$catalog,'pages'=>$proof];$m->custody($ba,$commit,false);$m->custody($ba,$commit,false);
$delivered=$m->ownerTrade($owner,false,$id);$received=current(array_filter($delivered['assets'],fn($a)=>$a['leg']==='goods'));
check($received['delivery']['state']==='delivered'&&$received['delivery']['receipt']===$ticket&&$received['delivery']['pages']===[2]);
check((int)$m->run('SELECT COUNT(*) FROM market_trade_assets')->fetchColumn()===3);
// Snapshot evidence survives later changes to the current escrow metadata.
$metadata=json_decode($m->run('SELECT metadata FROM escrow_items WHERE item=?',[$paidRune['item_id']])->fetchColumn(),true);$metadata['display']['saved_properties'][0]['value']=999;
$m->run('UPDATE escrow_items SET metadata=? WHERE item=?',[json_encode($metadata),$paidRune['item_id']]);
$immutable=$m->ownerTrade($owner,false,$id);check(current(array_filter($immutable['assets'],fn($a)=>$a['item_id']===$paidRune['item_id']))['snapshot']['display']['saved_properties'][0]['value']===17);
$uid=$m->publicPlayerId($seller['id']);check(count($m->ownerTrades($owner,false,['q'=>$uid])['rows'])===1);check($m->ownerTrades($owner,false,['q'=>'%'])['rows']===[]);
refuses(fn()=>$m->ownerTrades($owner,false,['status'=>'bogus']),422);
// Historical imported sales remain visible without invented asset receipts.
$legacy=$m->listing(['id'=>'operator','role'=>'admin'],['owner'=>$seller['id'],'item'=>['seed'=>77,'code'=>'amu','quality'=>6,'item_index'=>-1,'ethereal'=>0,'sockets'=>0,'item_hex'=>str_repeat('ef',24)],'price'=>[$gold]],false);
$m->sale(['id'=>'operator','role'=>'admin'],['receipt'=>'old-sale','item_id'=>$legacy['item_id'],'buyer'=>$buyer['id'],'seller'=>$seller['id'],'price'=>[$gold]],false);
$old=current(array_filter($m->ownerTrades($owner,false)['rows'],fn($r)=>$r['record_type']==='legacy'));
$historical=$m->ownerTrade($owner,false,$old['id']);check(!$historical['exact_assets_recorded']&&str_contains($historical['evidence_note'],'does not prove non-delivery'));
// Stable pagination handles many trades with the same creation second.
for($n=0;$n<35;$n++)$m->run('INSERT INTO offers VALUES (?,?,?,?,?,?,?,?,?,?)',[Market::id(),$listing['id'],$buyer['id'],$seller['id'],1,1,'rejected',$seller['id'],json_encode($price),$pending['created']]);
$one=$m->ownerTrades($owner,false);check(count($one['rows'])===30&&$one['next']);$two=$m->ownerTrades($owner,false,$one['next']);check(count($two['rows'])===7&&$two['next']===null);check(!array_intersect(array_column($one['rows'],'id'),array_column($two['rows'],'id')));
// Older records can still prove delivery through an event after this sale and
// before any later sale. Do not fabricate the missing exact transfer snapshot.
$m->run('DELETE FROM market_trade_assets WHERE receipt=?',[$settled['settlement']['id']]);
$recovered=$m->ownerTrade($owner,false,$id);check(!$recovered['exact_assets_recorded']);
$oldGoods=current(array_filter($recovered['assets'],fn($a)=>$a['leg']==='goods'));
check($oldGoods['historical']&&$oldGoods['quantity']===null&&$oldGoods['delivery']['state']==='delivered'&&$oldGoods['delivery']['receipt']===$ticket);
check(current(array_filter($recovered['assets'],fn($a)=>$a['kind']==='gold'))['delivery']['state']==='wallet');
echo "Owner audit: UID-only authorization, moderator/name refusal, exact barter/wallet transfers, pending vs committed delivery, page receipts, immutable snapshots, historic uncertainty, scope isolation, literal search and stable pagination passed.\n";
