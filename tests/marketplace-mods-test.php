<?php
declare(strict_types=1);
require __DIR__.'/../tools/marketplace/server/Market.php';
function check(bool $ok):void{if(!$ok)throw new RuntimeException('Mod assertion failed at '.json_encode(debug_backtrace(DEBUG_BACKTRACE_IGNORE_ARGS,1)));}
function refuses(callable $f,int $status):void{try{$f();}catch(MarketError $e){check($e->status===$status);return;}throw new RuntimeException('Cross-mod operation allowed.');}
$m=new Market(new PDO('sqlite::memory:'));$m->migrate();$s=$m->user('Sender');$b=$m->user('Receiver');$c=str_repeat('a',64);$d=str_repeat('b',64);
$sa=$m->pairAgent($s['id'],$c);$ba=$m->pairAgent($b['id'],$c);
$m->browserLink(['code'=>$m->browserTicket($s)['code']]);$m->browserLink(['code'=>$m->browserTicket($b)['code']]);
$other=new Market($m->db,'tcpip','Another Mod');$oa=$other->pairAgent($s['id'],$d);
check(array_column($m->modDirectory($s)['mods'],'mod')===['RMD-MP']);
$definitions=json_decode(file_get_contents(__DIR__.'/../tools/marketplace/server/item-identity.json'),true,512,JSON_THROW_ON_ERROR);
$definitions['items']['rin ']['name']='Other-mod ring';
$context=['mod'=>'Another Mod','game_mode'=>'2.4','catalog'=>$d,'identity_catalog'=>$definitions];
refuses(fn()=>$m->browserTicket($b,$context),403);
refuses(fn()=>$m->browserTicket($s,array_replace($context,['catalog'=>$c])),403);
refuses(fn()=>$m->browserTicket($s,array_replace($context,['game_mode'=>'3.x'])),409);
$t=$m->browserTicket($s,$context);check($t['mod']==='Another Mod');$linked=$m->browserLink(['code'=>$t['code']]);check($linked['mod']==='Another Mod');
$other=$m->selectMod('Another Mod');check($other->browserCatalog()['bases']['rin']['name']==='Other-mod ring');check($other->browserCatalog()['stats']===[]); // Never borrow RMD definitions.
check(count($m->modDirectory($s)['mods'])===2);check(count($other->players(false)['players'])===1);refuses(fn()=>$other->playerProfile($b['id'],false),404);
check($m->authAgent($sa['token'])['id']===$sa['id']);check($m->authAgent($oa['token'])['mod']==='Another Mod');
refuses(fn()=>$other->pairAgent($b['id'],$c),409);refuses(fn()=>$m->selectMod('Another Mod','3.x'),409);refuses(fn()=>$m->selectMod('missing'),404);
$pages=[['page'=>11,'before'=>str_repeat('c',64),'after'=>str_repeat('d',64)]];
$item=['page'=>11,'tab'=>1,'saved_id'=>0,'quality'=>6,'index'=>-1,'code'=>'rin','ethereal'=>0,'identified'=>1,'quantity'=>1,'quantity_bit'=>-1,'item_hex'=>str_repeat('cb',24),'price'=>Market::freePrice()];
$deposit=function($market,$agent,$catalog)use($pages,$item){$agent=$market->authAgent($agent['token']);$id=Market::id();foreach([['type'=>'deposit_prepare','items'=>[$item]],['type'=>'write_authorize'],['type'=>'commit']] as $data)$market->custody($agent,$data+['id'=>$id,'catalog'=>$catalog,'pages'=>$pages],false);};
refuses(fn()=>$other->custody($m->authAgent($sa['token']),['type'=>'status','catalog'=>$c],false),403);
$deposit($m,$sa,$c);$deposit($other,$oa,$d);$listing=$other->snapshot($s,false)['listings'][0];
refuses(fn()=>$other->command($b,['id'=>Market::id(),'type'=>'propose','listing_id'=>$listing['id'],'listing_revision'=>1,'price'=>Market::freePrice()],false),403);
refuses(fn()=>$m->command($b,['id'=>Market::id(),'type'=>'propose','listing_id'=>$listing['id'],'listing_revision'=>1,'price'=>Market::freePrice()],false),404);
refuses(fn()=>$other->command($s,['id'=>Market::id(),'type'=>'gift_item','item_id'=>$listing['item_id'],'to'=>$b['id']],false),403);
$other->browserSnapshot($b,false);refuses(fn()=>$other->command($b,['id'=>Market::id(),'type'=>'wallet_withdraw','balances'=>[]],false),403); // Browsing does not enroll.
check(count($m->snapshot($s,false)['listings'])===1&&count($other->snapshot($s,false)['listings'])===1);
$ob=$other->pairAgent($b['id'],$d);$m->browserLink(['code'=>$m->browserTicket($b,$context)['code']]);
$result=$other->command($b,['id'=>Market::id(),'type'=>'propose','listing_id'=>$listing['id'],'listing_revision'=>1,'price'=>Market::freePrice()],false);check($result['status']==='completed');
check(count($m->snapshot($s,false)['listings'])===1&&count($other->snapshot($s,false)['listings'])===0);
// Re-running additive migration preserves all scoped agents, identities and sales.
$m->migrate();check($m->authAgent($oa['token'])['id']===$oa['id']);check($m->authAgent($sa['token'])['id']===$sa['id']);check(count($other->snapshot($b,false)['history'])===1);
echo "Mods: detected link context, dynamic directory, remembered-ticket scope, scoped catalogs/pairings/profiles/escrow, wrong-mod free/gift rejection, same-mod settlement, browsing without enrollment, 3.x refusal and additive migration passed.\n";
