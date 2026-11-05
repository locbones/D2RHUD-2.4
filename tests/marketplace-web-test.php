<?php
declare(strict_types=1);
require __DIR__.'/../tools/marketplace/server/Market.php';
function check(bool $ok):void{if(!$ok)throw new RuntimeException('Browser test failed at '.json_encode(debug_backtrace(DEBUG_BACKTRACE_IGNORE_ARGS,1)));}
function refuses(callable $fn,int $status):void{try{$fn();}catch(MarketError $e){check($e->status===$status);return;}throw new RuntimeException('Unexpected browser operation allowed.');}
$m=new Market(new PDO('sqlite::memory:'));$m->migrate();$m->migrate();
$s=$m->user('Seller');$b=$m->user('Buyer');$catalog=str_repeat('a',64);$sa=$m->authAgent($m->pairAgent($s['id'],$catalog)['token']);
$code=$m->browserTicket($s)['code'];check((bool)preg_match('/^[A-HJ-NP-Z2-9]{4}(-[A-HJ-NP-Z2-9]{4}){2}$/D',$code));check($m->browserLink(['code'=>strtolower(str_replace('-',' ',$code))])['id']===$s['id']);refuses(fn()=>$m->browserLink(['code'=>$code]),401);
refuses(fn()=>$m->browserLink(['username'=>'seller','password'=>'anything']),401);
$link=$m->custody($sa,['type'=>'browser_link','catalog'=>$catalog],false);check(!$link['valid']);
check($m->custody($sa,['type'=>'browser_link','catalog'=>$catalog,'code'=>$link['code']],false)['valid']);
$another=$m->browserTicket($s);check($m->custody($sa,['type'=>'browser_link','catalog'=>$catalog,'code'=>$link['code']],false)['valid']);
check($m->browserLink(['code'=>$link['code']])['id']===$s['id']);check($m->browserLink(['code'=>$another['code']])['id']===$s['id']);
check(!$m->custody($sa,['type'=>'browser_link','catalog'=>$catalog,'code'=>$link['code']],false)['valid']);
refuses(fn()=>$m->custody($sa,['type'=>'browser_link','catalog'=>str_repeat('f',64)],false),401);
for($i=0;$i<30;$i++)$m->browserThrottle('fixture');refuses(fn()=>$m->browserThrottle('fixture'),429);
$settings=['page'=>11,'withdraw_page'=>2,'enabled'=>true,'auto_trade'=>false];
// A newly code-linked player can configure pages before any stash snapshot.
$fresh=$m->user('Fresh player');$freshAgent=$m->authAgent($m->pairAgent($fresh['id'],$catalog)['token']);
$m->browserLink(['code'=>$m->browserTicket($fresh)['code']]);check($m->browserSnapshot($fresh,false)['profile']===null);
$firstSetup=['type'=>'web_setup','id'=>Market::id(),'expected_revision'=>0,'page'=>17,'withdraw_page'=>4,'enabled'=>true,'auto_trade'=>false];
// One market page is valid; obsolete delivery choices normalize to it.
$queued=$m->command($fresh,$firstSetup,false);check($queued['queued']);check($m->command($fresh,$firstSetup,false)===$queued);
$waiting=$m->browserSnapshot($fresh,false);check($waiting['profile']['settings']===null&&$waiting['profile']['seen']===0);
check($waiting['profile']['desired']['page']===17&&$waiting['profile']['desired']['withdraw_page']===17);
check(count($waiting['pending'])===1&&$waiting['pending'][0]['status']==='queued');check($waiting['local_items']===[]);
check($m->browserSnapshot($fresh,true)['profile']===null);check($m->browserSnapshot($b,false)['profile']===null);
refuses(fn()=>$m->command($fresh,array_replace($firstSetup,['id'=>Market::id()]),false),409);
$handoff=$m->custody($freshAgent,['type'=>'web_sync','catalog'=>$catalog,'settings'=>$settings,'inventory'=>[]],false);
check(count($handoff['intents'])===1&&$handoff['intents'][0]['id']===$firstSetup['id']);
$applied=$handoff['intents'][0]['packet'];
$m->custody($freshAgent,['type'=>'web_sync','catalog'=>$catalog,'settings'=>$applied,'inventory'=>[],'acknowledged'=>[['id'=>$firstSetup['id'],'status'=>'applied','message'=>'']]],false);
$ready=$m->browserSnapshot($fresh,false);check(!isset($ready['profile']['desired'])&&$ready['profile']['settings']===$applied&&$ready['profile']['seen']>0);
check($ready['pending'][0]['status']==='applied');
$item=['code'=>'rin','saved_id'=>0,'quality'=>6,'index'=>-1,'ethereal'=>0,'identified'=>1,'quantity'=>1,'quantity_bit'=>-1,'tab'=>1,'page'=>11,'item_hex'=>str_repeat('ab',24),'display'=>['level'=>88,'saved_properties'=>[['id'=>0,'value'=>17,'param'=>0,'list'=>'base']],'socketed_items'=>[]]];
$item['display']['saved_properties'][]=['id'=>194,'value'=>3];
$item['display']['socketed_items']=[['code'=>'r33','quality'=>2,'flags'=>16,'saved_properties'=>[]],['code'=>'jew','quality'=>6,'identified'=>true,'name_ids'=>['rare_prefix'=>12,'rare_suffix'=>40],'saved_properties'=>[['id'=>17,'value'=>40]]]];
$sync=fn($ack=[])=>$m->custody($sa,['type'=>'web_sync','catalog'=>$catalog,'settings'=>$settings,'inventory'=>[$item],'acknowledged'=>$ack],false);
check($sync()===['intents'=>[],'delivery_items'=>[],'wallet_withdrawals'=>[]]);$snapshot=$m->browserSnapshot($s,false);check(count($snapshot['local_items'])===1);check($snapshot['local_items'][0]['display']['saved_properties'][0]['value']===17);
$children=$snapshot['local_items'][0]['display']['socketed_items'];check(count($children)===2&&$children[0]['identified']===true&&$children[1]['name_ids']['rare_prefix']===12&&$children[1]['display']['saved_properties'][0]['value']===40);

check(!str_contains(json_encode($m->browserSnapshot(null,false)),'saved_properties'));check(!str_contains(json_encode($m->browserSnapshot($b,false)),'saved_properties'));check(!str_contains(json_encode($snapshot),'item_hex'));check(!str_contains(json_encode($snapshot),$s['token']));
$price=[['kind'=>'gold','code'=>'gld','label'=>'Gold','quality'=>2,'index'=>-1,'ethereal'=>-1,'quantity'=>40]];
$command=['type'=>'web_price','id'=>Market::id(),'key'=>$snapshot['local_items'][0]['key'],'price'=>$price];check($m->command($s,$command,false)===$m->command($s,$command,false));refuses(fn()=>$m->command($b,$command,false),409);refuses(fn()=>$m->command($s,$command,true),409);
$queue=$sync();check(count($queue['intents'])===1);check($queue['intents'][0]['packet']['price']==$price);check($sync([['id'=>$command['id'],'status'=>'applied','message'=>'']])===['intents'=>[],'delivery_items'=>[],'wallet_withdrawals'=>[]]);
$setup=['type'=>'web_setup','id'=>Market::id(),'expected_revision'=>$snapshot['profile']['revision'],'page'=>12,'withdraw_page'=>3,'enabled'=>true,'auto_trade'=>true];$m->command($s,$setup,false);refuses(fn()=>$m->command($s,array_replace($setup,['id'=>Market::id()]),false),409);check($m->browserSnapshot($s,false)['escrow']['auto_trade']);
$bad=$item;$bad['display']['saved_properties'][0]['value']='not a number';refuses(fn()=>$m->custody($sa,['type'=>'web_sync','catalog'=>$catalog,'settings'=>$settings,'inventory'=>[$bad]],false),422);
$id=Market::id();$pages=[['page'=>11,'before'=>str_repeat('b',64),'after'=>str_repeat('c',64)]];$m->custody($sa,['type'=>'deposit_prepare','id'=>$id,'catalog'=>$catalog,'items'=>[$item+['price'=>$price]],'pages'=>$pages],false);$m->custody($sa,['type'=>'write_authorize','id'=>$id,'catalog'=>$catalog,'pages'=>$pages],false);$m->custody($sa,['type'=>'commit','id'=>$id,'catalog'=>$catalog,'pages'=>$pages],false);
$guest=$m->browserSnapshot(null,false);check(count($guest['listings'])===1);check($guest['listings'][0]['item']['display']['saved_properties'][0]['value']===17);check(count($m->browserSnapshot($s,false)['local_items'])===0);check(!str_contains(json_encode($guest),'item_hex'));check($guest['self']===null);check($guest['offers']===[]);check($m->browserSnapshot(null,true)['listings']===[]);
// Returns remain discoverable until delivery is acknowledged. A selected claim
// cannot also withdraw unrelated payment items or the player's wallet balance.
$stock=$item;$stock['item_hex']=str_repeat('cd',24);$stock['code']='amu';
$funds=$price;$funds[0]['quantity']=1000;
$deposit=Market::id();$m->custody($sa,['type'=>'deposit_prepare','id'=>$deposit,'catalog'=>$catalog,'items'=>[$stock],'balances'=>$funds,'pages'=>$pages],false);
$m->custody($sa,['type'=>'write_authorize','id'=>$deposit,'catalog'=>$catalog,'pages'=>$pages],false);
$m->custody($sa,['type'=>'commit','id'=>$deposit,'catalog'=>$catalog,'pages'=>$pages],false);
$ring=$guest['listings'][0]['item_id'];
$m->command($s,['id'=>Market::id(),'type'=>'escrow_return','item_id'=>$ring],false);
check($sync()['delivery_items']===[$ring]);check($sync()['delivery_items']===[$ring]);
$claim=Market::id();$packet=$m->custody($sa,['id'=>$claim,'type'=>'claim_prepare','catalog'=>$catalog,'item_ids'=>[$ring],'balance_caps'=>[]],false)['packet'];
check(array_column($packet['items'],'item')===[$ring]);check($packet['balances']===[]);
$m->custody($sa,['id'=>$claim,'type'=>'cancel','catalog'=>$catalog],false);
check($sync()['delivery_items']===[$ring]); // capacity failure leaves the request pending
$claim=Market::id();$packet=$m->custody($sa,['id'=>$claim,'type'=>'claim_prepare','catalog'=>$catalog,'item_ids'=>[$ring],'balance_caps'=>[]],false)['packet'];
$delivery=[['page'=>2,'before'=>str_repeat('d',64),'after'=>str_repeat('e',64)]];
$m->custody($sa,['id'=>$claim,'type'=>'write_authorize','catalog'=>$catalog,'pages'=>$delivery,'delivered'=>$packet['items']],false);
$m->custody($sa,['id'=>$claim,'type'=>'commit','catalog'=>$catalog,'pages'=>$delivery],false);
check($sync()['delivery_items']===[]);
$wallet=$m->snapshot($s,false)['escrow'];check(count($wallet['items'])===1);check($wallet['items'][0]['metadata']['code']==='amu');check($wallet['balances'][0]['quantity']===1000);

echo "Browser: short one-use codes, folder-link refresh, pairing isolation, rate limits, guest/private separation, safe metadata, mode isolation, idempotent queued setup/prices consumed-item suppression and persistent targeted return delivery passed.\n";
