<?php
declare(strict_types=1);
require __DIR__.'/../tools/marketplace/server/Market.php';
function check(bool $v):void {if(!$v)throw new RuntimeException('Torch assertion failed at '.json_encode(debug_backtrace(DEBUG_BACKTRACE_IGNORE_ARGS,1)));}
function refuses(callable $f,int $status):void {try{$f();}catch(MarketError $e){check($e->status===$status);return;}throw new RuntimeException('Invalid Torch action accepted.');}
$m=new Market(new PDO('sqlite::memory:'));$m->migrate();
$s=$m->user('Torch seller');$b=$m->user('Torch buyer');$cat=str_repeat('a',64);
foreach([$s,$b] as $a)$m->browserLink(['code'=>$m->browserTicket($a)['code']]);
$sa=$m->authAgent($m->pairAgent($s['id'],$cat)['token']);$ba=$m->authAgent($m->pairAgent($b['id'],$cat)['token']);
$catalog=$m->browserCatalog();$torchIndex=null;
// Catalog rows are keyed by compiled index, which differs between mods.
foreach($catalog['unique'] as $id=>$d)if($d['name']==='Hellfire Torch')$torchIndex=(int)$id;
check(is_int($torchIndex));
$torch=['code'=>'cm2','label'=>'Hellfire Torch','quality'=>7,'index'=>$torchIndex,'quantity'=>1,'ethereal'=>-1,'class_id'=>1];
$necro=array_replace($torch,['class_id'=>2]);$any=$torch;unset($any['class_id']);
check(Market::price([$torch])[0]['class_id']===1);
check(!isset(Market::price([array_replace($torch,['class_id'=>-1])])[0]['class_id']));
check(count(Market::price([$torch,$necro]))===2);
foreach([[$torch,$torch],[$any,$torch]] as $rows)refuses(fn()=>Market::price($rows),422);
foreach([7,-2,true,1.5,'1'] as $bad)refuses(fn()=>Market::price([array_replace($torch,['class_id'=>$bad])]),422);
refuses(fn()=>Market::price([array_replace($torch,['code'=>'rin'])]),422);
refuses(fn()=>Market::price([Market::freePrice()[0]+['class_id'=>1]]),422);
$gold=fn($q)=>['kind'=>'gold','code'=>'gld','label'=>'Gold','quality'=>2,'index'=>-1,'quantity'=>$q,'ethereal'=>-1];
refuses(fn()=>Market::price([$gold(1)+['class_id'=>1]]),422);
$pages=[['page'=>11,'before'=>str_repeat('b',64),'after'=>str_repeat('c',64)]];
$deposit=function($agent,$items=[],$balances=[])use($m,$cat,$pages){$id=Market::id();foreach([['type'=>'deposit_prepare','items'=>$items,'balances'=>$balances,'pages'=>$pages],['type'=>'write_authorize','pages'=>$pages],['type'=>'commit','pages'=>$pages]] as $p)$m->custody($agent,$p+['id'=>$id,'catalog'=>$cat],false);};
$item=function(int $class,string $hex,bool $identified=true)use($torchIndex){return ['code'=>'cm2','quality'=>7,'index'=>$torchIndex,'page'=>11,'tab'=>1,'saved_id'=>0,'ethereal'=>0,'identified'=>(int)$identified,'quantity'=>1,'quantity_bit'=>-1,'item_hex'=>str_repeat($hex,24),'display'=>['level'=>99,'saved_properties'=>[['id'=>83,'param'=>$class,'value'=>3,'list'=>'base']]]];};
$sale=$item(2,'ab');$sale['price']=[$torch];$deposit($sa,[$sale]);
$wrong=$item(2,'cd');$hidden=$item(1,'ef',false);$socketOnly=$item(1,'12');$socketOnly['display']['saved_properties'][0]['list']='set';
$ambiguous=$item(1,'34');$ambiguous['display']['saved_properties'][]=['id'=>83,'param'=>2,'value'=>3,'list'=>'base'];
$deposit($ba,[$wrong,$hidden,$socketOnly,$ambiguous]);
$listing=$m->browserSnapshot($b,false)['listings'][0];
check($listing['price'][0]['class_id']===1);
check($m->snapshot($b,false)['listings']===[]); // old HUD must not drop the constraint
check($m->snapshot($b,false,false,true)['listings'][0]['price'][0]['class_id']===1);
$proposal=['id'=>Market::id(),'type'=>'propose','listing_id'=>$listing['id'],'listing_revision'=>1,'price'=>[$torch]];
$offer=$m->command($b,$proposal,false);check((bool)array_filter($m->inbox($s)['messages'],fn($r)=>str_contains($r['body'],'Sorceress')));$accepted=$m->command($s,['id'=>Market::id(),'type'=>'accept','offer_id'=>$offer['offer_id'],'expected_revision'=>1],false);
check($accepted['status']==='accepted');
check($m->run('SELECT owner FROM items WHERE id=?',[$listing['item_id']])->fetchColumn()===$s['id']);
check(count($m->snapshot($b,false)['offers'])===0&&count($m->snapshot($b,false,false,true)['offers'])===1);
// Scope is intentionally obtained from the stored item rather than assumed.
$scope=$m->run('SELECT scope FROM items WHERE id=?',[$listing['item_id']])->fetchColumn();
$requests=(new ReflectionMethod($m,'fundingRequests'))->invoke($m,$b['id'],$scope);check($requests===[]);
$m->custody($ba,['type'=>'web_sync','catalog'=>$cat,'settings'=>['page'=>11,'withdraw_page'=>11,'enabled'=>true,'auto_trade'=>false],'inventory'=>[],'features'=>['torch_class']],false);
$requests=(new ReflectionMethod($m,'fundingRequests'))->invoke($m,$b['id'],$scope);
check($requests[0]['price'][0]['class_id']===1);
$correct=$item(1,'56');$deposit($ba,[$correct]);
check($m->run('SELECT status FROM offers WHERE id=?',[$offer['offer_id']])->fetchColumn()==='completed');
check($m->run('SELECT owner FROM items WHERE id=?',[$listing['item_id']])->fetchColumn()===$b['id']);
check($m->run('SELECT owner FROM items WHERE item_hex=?',[$correct['item_hex']])->fetchColumn()===$s['id']);
check($m->run('SELECT owner FROM items WHERE item_hex=?',[$wrong['item_hex']])->fetchColumn()===$b['id']);
$history=$m->browserSnapshot($b,false)['history'];check($history[0]['price'][0]['class_id']===1);check($m->snapshot($b,false)['history']===[]);
// WTB responses and counter-offers carry the same exact class requirement.
$wanted=$m->command($s,['id'=>Market::id(),'type'=>'wanted_create','wanted'=>$torch,'contact'=>true],false);
$public=$m->publicEscrow($b['id'],false)['items'];$wrongId=null;
foreach($public as $r)if($r['item']['display']['saved_properties'][0]['param']===2)$wrongId=$r['item_id'];
check(is_string($wrongId));
refuses(fn()=>$m->command($b,['id'=>Market::id(),'type'=>'wanted_respond','wanted_id'=>$wanted['wanted_id'],'expected_revision'=>1,'item_id'=>$wrongId,'price'=>[$gold(1)]],false),409);
$second=$item(1,'78');$deposit($ba,[$second]);$rightId=$m->run('SELECT id FROM items WHERE item_hex=?',[$second['item_hex']])->fetchColumn();
$response=$m->command($b,['id'=>Market::id(),'type'=>'wanted_respond','wanted_id'=>$wanted['wanted_id'],'expected_revision'=>1,'item_id'=>$rightId,'price'=>[$gold(1)]],false);
check($response['status']==='pending');
$m->command($s,['id'=>Market::id(),'type'=>'counter','offer_id'=>$response['offer_id'],'expected_revision'=>1,'price'=>[$necro]],false);
check(json_decode($m->run('SELECT price FROM offers WHERE id=?',[$response['offer_id']])->fetchColumn(),true)[0]['class_id']===2);
echo "Torch classes: strict validation, any-class compatibility, distinct class bundles, wrong/unidentified/ambiguous/root-list checks, funding requests, exact settlement, history, WTB, counters and legacy HUD isolation passed.\n";
