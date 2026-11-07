<?php
declare(strict_types=1);
require __DIR__.'/../tools/marketplace/server/Market.php';
function check(bool $ok):void {if(!$ok)throw new RuntimeException('Whole-stack assertion failed at '.json_encode(debug_backtrace(DEBUG_BACKTRACE_IGNORE_ARGS,1)));}
function refuses(callable $fn):void {try{$fn();}catch(MarketError $e){check($e->status===422);return;}throw new RuntimeException('Unsafe stack metadata accepted.');}
$db=new PDO('sqlite::memory:');$m=new Market($db);$m->migrate();
$seller=$m->user('Stack seller');$buyer=$m->user('Stack buyer');$catalog=str_repeat('a',64);
$sa=$m->authAgent($m->pairAgent($seller['id'],$catalog)['token']);$ba=$m->authAgent($m->pairAgent($buyer['id'],$catalog)['token']);
$page=[['page'=>20,'before'=>str_repeat('b',64),'after'=>str_repeat('c',64)]];
$base=['page'=>20,'tab'=>1,'code'=>'jav','saved_id'=>0,'quality'=>7,'index'=>42,'ethereal'=>0,'identified'=>1,'sockets'=>0,'quantity'=>140,'quantity_bit'=>-1,'item_hex'=>str_repeat('ab',24)];
$call=fn($a,$d)=>$m->custody($a,$d+['catalog'=>$catalog],false);
$sync=fn($inventory)=>$call($ba,['type'=>'web_sync','settings'=>['page'=>20,'enabled'=>true,'auto_trade'=>false],'inventory'=>$inventory]);
// A single non-splittable record must not block the entire private inventory.
$rows=[];foreach([1,2,3,4,5,6,7,8,9] as $q)$rows[]=array_replace($base,['quality'=>$q]);
$rows[]=array_replace($base,['quality'=>2,'sockets'=>1]);
$sync($rows);$saved=json_decode($m->run('SELECT inventory FROM web_profiles WHERE owner=?',[$buyer['id']])->fetchColumn(),true);
check(count($saved)===10&&array_column($saved,'quantity')===array_fill(0,10,140));
// Readable quantity is never permission to edit arbitrary serialized bits.
refuses(fn()=>$sync([array_replace($base,['quantity_bit'=>100])]));
refuses(fn()=>$sync([array_replace($base,['quality'=>2,'quantity_bit'=>9999])]));
refuses(fn()=>$sync([array_replace($base,['quantity'=>512])]));
$deposit=function($a,$items)use($call,$page){$id=Market::id();$packet=['id'=>$id,'type'=>'deposit_prepare','items'=>$items,'pages'=>$page];$r=$call($a,$packet);check($r===$call($a,$packet));$call($a,['id'=>$id,'type'=>'write_authorize','pages'=>$page]);$r=$call($a,['id'=>$id,'type'=>'commit','pages'=>$page]);check($r===$call($a,['id'=>$id,'type'=>'commit','pages'=>$page]));};
$terms=[['code'=>'jav','label'=>'Unique javelin','quality'=>7,'index'=>42,'ethereal'=>-1,'quantity'=>2]];
$goods=array_replace($base,['code'=>'amu','quantity'=>1,'item_hex'=>str_repeat('ac',24),'price'=>$terms]);$deposit($sa,[$goods]);
$deposit($ba,[$base]);$listing=$m->snapshot($buyer,false)['listings'][0];
$offer=$m->command($buyer,['type'=>'propose','id'=>Market::id(),'listing_id'=>$listing['id'],'listing_revision'=>(int)$listing['revision'],'price'=>$terms],false);
$m->command($seller,['type'=>'accept','id'=>Market::id(),'offer_id'=>$offer['offer_id'],'expected_revision'=>1],false);
check($m->run('SELECT status FROM offers WHERE id=?',[$offer['offer_id']])->fetchColumn()==='accepted');
$needed=$call($ba,['type'=>'delivery_queue','id'=>Market::id()])['funding_requests'];check($needed[0]['price'][0]['quantity']===1);
// 140 ammunition cannot satisfy a request for two distinct unique weapons.
$second=array_replace($base,['quantity'=>83,'item_hex'=>str_repeat('ad',24)]);$deposit($ba,[$second]);
check($m->run('SELECT status FROM offers WHERE id=?',[$offer['offer_id']])->fetchColumn()==='completed');
$paid=$m->run("SELECT i.item_hex,e.metadata FROM items i JOIN escrow_items e ON e.item=i.id WHERE i.code='jav' AND e.owner=?",[$seller['id']])->fetchAll();
check(count($paid)===2);$hex=array_column($paid,'item_hex');sort($hex);$expected=[$base['item_hex'],$second['item_hex']];sort($expected);check($hex===$expected);
$counts=array_map(fn($r)=>json_decode($r['metadata'],true)['quantity'],$paid);sort($counts);check($counts===[83,140]);
$snap=$m->browserSnapshot($seller,false);foreach($snap['escrow']['items'] as $r)if($r['item']['code']==='jav')check($r['item']['trade_quantity']===1&&in_array($r['item']['quantity'],[83,140],true));
echo "Whole stacks: private sync, indivisible payments, exact saved-byte conservation, replay and unsafe-offset rejection passed.\n";

