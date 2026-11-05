<?php
declare(strict_types=1);
require __DIR__.'/../tools/marketplace/server/Market.php';
function check(bool $ok):void{if(!$ok)throw new RuntimeException('Trading assertion failed: '.json_encode(debug_backtrace(DEBUG_BACKTRACE_IGNORE_ARGS,1)));}
function refuses(callable $f,int $status):void{try{$f();}catch(MarketError $e){check($e->status===$status);return;}throw new RuntimeException('Invalid trading action allowed.');}
$m=new Market(new PDO('sqlite::memory:'));$m->migrate();$m->migrate();
$s=$m->user('Seller');$b=$m->user('Buyer');$other=$m->user('Other');$cat=str_repeat('a',64);
foreach([$s,$b,$other] as $a)$m->browserLink(['code'=>$m->browserTicket($a)['code']]);
$sa=$m->authAgent($m->pairAgent($s['id'],$cat)['token']);$ba=$m->authAgent($m->pairAgent($b['id'],$cat)['token']);
$gold=fn($q)=>['kind'=>'gold','code'=>'gld','label'=>'Gold','quality'=>2,'index'=>-1,'ethereal'=>-1,'quantity'=>$q];
$pages=[['page'=>11,'before'=>str_repeat('b',64),'after'=>str_repeat('c',64)]];
$deposit=function($agent,$items=[],$balances=[])use($m,$cat,$pages){$id=Market::id();foreach([['type'=>'deposit_prepare','items'=>$items,'balances'=>$balances,'pages'=>$pages],['type'=>'write_authorize','pages'=>$pages],['type'=>'commit','pages'=>$pages]] as $packet)$m->custody($agent,$packet+['id'=>$id,'catalog'=>$cat],false);};
$item=['code'=>'rin','quality'=>6,'index'=>-1,'page'=>11,'tab'=>1,'saved_id'=>0,'ethereal'=>0,'identified'=>1,'quantity'=>1,'quantity_bit'=>-1,'item_hex'=>str_repeat('ab',24),'price'=>Market::openPrice(),'display'=>['level'=>88,'saved_properties'=>[['id'=>0,'value'=>17]]]];
check(Market::price(Market::openPrice())===Market::openPrice());refuses(fn()=>Market::price([Market::openPrice()[0],$gold(1)]),422);refuses(fn()=>Market::price([array_replace(Market::openPrice()[0],['quantity'=>2])]),422);
$deposit($sa,[$item]);$deposit($ba,[],[$gold(1000)]);
$m->command($s,['id'=>Market::id(),'type'=>'preferences','auto_trade'=>true],false);
$m->custody($sa,['type'=>'web_sync','catalog'=>$cat,'settings'=>['page'=>11,'withdraw_page'=>11,'enabled'=>true,'auto_trade'=>true],'inventory'=>[],'features'=>['open_offers'],'companion_version'=>'0.1.14'],false);
check($m->browserSnapshot($s,false)['companion_version']==='0.1.14');check(in_array('open_offers',$m->browserSnapshot($s,false)['companion_features'],true));check($m->browserSnapshot($b,false)['companion_version']===null);
$snapshot=$m->browserSnapshot($b,false);$l=$snapshot['listings'][0];check($l['price']===Market::openPrice());check($m->snapshot($b,false)['listings']===[]); // old HUDs remain usable
$propose=['id'=>Market::id(),'type'=>'propose','listing_id'=>$l['id'],'listing_revision'=>1,'price'=>[$gold(100)],'message'=>'I offer 100 gold.'];
$o=$m->command($b,$propose,false);check($o['status']==='pending');check($m->command($b,$propose,false)===$o);
refuses(fn()=>$m->command($b,array_replace($propose,['id'=>Market::id(),'price'=>Market::openPrice()]),false),422);
$messages=$m->tradeMessages($b,false);check(count($messages['conversations'])===1);check($messages['conversations'][0]['peer']['id']===$s['id']);
$m->command($s,['id'=>Market::id(),'type'=>'trade_reply','offer_id'=>$o['offer_id'],'message'=>'Thank you, checking it now.'],false);
$thread=$m->tradeMessages($b,false,$o['offer_id'])['conversation'];check(count($thread['events'])===2&&$thread['events'][0]['message']==='I offer 100 gold.'&&$thread['events'][1]['message']==='Thank you, checking it now.');
refuses(fn()=>$m->tradeMessages($other,false,$o['offer_id']),403);check($m->tradeMessages($b,true,$o['offer_id'])['market']['hardcore']===false);refuses(fn()=>$m->command($s,['id'=>Market::id(),'type'=>'accept','offer_id'=>$o['offer_id'],'expected_revision'=>$thread['revision']],true),404);
$read=$m->command($b,['id'=>Market::id(),'type'=>'message_read','trade_thread'=>$o['offer_id']],false);check($read['unread']===0);
$accepted=$m->command($s,['id'=>Market::id(),'type'=>'accept','offer_id'=>$o['offer_id'],'expected_revision'=>$thread['revision']],false);check($accepted['status']==='completed');
check($m->run('SELECT owner FROM items WHERE id=?',[$l['item_id']])->fetchColumn()===$b['id']);
check($m->publicEscrow($b['id'],false)['items']===[]); // incoming delivery is reserved
$item['item_hex']=str_repeat('cd',24);unset($item['price']);$deposit($sa,[$item],[$gold(300)]);
$public=$m->publicEscrow($s['id'],false);check(count($public['items'])===1&&$public['items'][0]['item']['display']['saved_properties'][0]['value']===17);check(!str_contains(json_encode($public),'item_hex')&&!str_contains(json_encode($public),'token'));check($m->publicEscrow($s['id'],true)['items']===[]);
$wanted=['code'=>'rin','label'=>'Ring','quality'=>2,'index'=>-1,'quantity'=>1,'ethereal'=>-1,'any_quality'=>true];
$create=['id'=>Market::id(),'type'=>'wanted_create','wanted'=>$wanted,'price'=>[$gold(50)],'contact'=>false,'note'=>'A nice rare ring, please.'];$w=$m->command($b,$create,false);check($m->command($b,$create,false)===$w);check(count($m->browserSnapshot(null,false)['wanted'])===1);check($m->wantedListings(true)===[]);
check($m->run('SELECT COUNT(*) FROM items')->fetchColumn()==2); // WTB did not invent an item
$respond=['id'=>Market::id(),'type'=>'wanted_respond','wanted_id'=>$w['wanted_id'],'expected_revision'=>1,'item_id'=>$public['items'][0]['item_id'],'price'=>[$gold(50)],'message'=>'This ring matches your request.'];
refuses(fn()=>$m->command($other,$respond,false),409);refuses(fn()=>$m->command($s,$respond,true),409);
$quote=$m->command($s,$respond,false);check($quote['status']==='pending'&&$m->command($s,$respond,false)===$quote);
$thread=$m->tradeMessages($b,false,$quote['offer_id'])['conversation'];check($thread['proposer']===$s['id']&&$thread['buyer']===$b['id']);
refuses(fn()=>$m->command($s,['id'=>Market::id(),'type'=>'accept','offer_id'=>$quote['offer_id'],'expected_revision'=>1],false),403);
$result=$m->command($b,['id'=>Market::id(),'type'=>'accept','offer_id'=>$quote['offer_id'],'expected_revision'=>1],false);check($result['status']==='completed');check($m->wantedListings(false,$w['wanted_id'])['status']==='fulfilled');
$contact=['id'=>Market::id(),'type'=>'wanted_create','wanted'=>$wanted,'contact'=>true,'note'=>'Please message me.'];$w=$m->command($s,$contact,false);check($m->wantedListings(false,$w['wanted_id'])['price']===[]);
refuses(fn()=>$m->command($b,['id'=>Market::id(),'type'=>'wanted_close','wanted_id'=>$w['wanted_id'],'expected_revision'=>1],false),403);
$m->command($s,['id'=>Market::id(),'type'=>'wanted_close','wanted_id'=>$w['wanted_id'],'expected_revision'=>1],false);check($m->wantedListings(false)===[]);
refuses(fn()=>$m->command($s,array_replace($create,['id'=>Market::id(),'price'=>Market::openPrice()]),false),422);
$m->db->exec("UPDATE escrow_items SET state='available'"); // isolate availability/scope-only browse regression
check(count($m->publicEscrow($b['id'],false)['items'])===0); // delivery_requested still excludes them
echo "Open offers: exact terms, manual approval despite auto-trade, escrow conservation, legacy HUD isolation. WTB: catalog requests, no fabricated goods, price/contact modes, owned matching escrow responses, manual buyer consent, idempotency and scope/access checks. Public escrow: safe display, balances and delivery exclusions. Messages: both directions, grouped transaction history, private access and scoped read acknowledgements passed.\n";
