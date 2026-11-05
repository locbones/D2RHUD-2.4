<?php
declare(strict_types=1);
require __DIR__.'/../tools/marketplace/server/Market.php';
function check(bool $ok):void{if(!$ok)throw new RuntimeException('Compatibility test failed: '.json_encode(debug_backtrace(DEBUG_BACKTRACE_IGNORE_ARGS,1)));}
function refuses(callable $f,int $status):void{try{$f();}catch(MarketError $e){check($e->status===$status);return;}throw new RuntimeException('Incompatible save definition accepted.');}
$m=new Market(new PDO('sqlite::memory:'));$m->migrate();
$identity=json_decode(file_get_contents(__DIR__.'/../tools/marketplace/server/item-identity.json'),true);
$d=['schema'=>2,'game_mode'=>'2.4','reader'=>'d2r-98-v1','codes'=>$identity['codes'],'items'=>['rin '=>[false,false,false,false,false,false,false,false,1,1]],'stats'=>['s0'=>[8,0,32,8]],'bags'=>[]];
$request=fn($secret,$definition,$mod='SchemaFixture')=>['installation'=>hash('sha256',$secret),'mod'=>$mod,'game_mode'=>'2.4','catalog'=>Market::saveCatalogHash($definition),'compatibility'=>$definition,'identity_catalog'=>$identity];
$a=$m->enrollInstallation($request('one',$d),'192.0.2.1');$family=$a['agent']['catalog'];$other=$m->enrollInstallation($request('other',$d),'192.0.2.1');
check($family===Market::saveCatalogHash($d));
$added=$d;$added['items']['new ']=[false,false,false,false,false,false,false,false,1,1];$added['stats']['s1']=[12,0,0,0];
$next=$m->enrollInstallation($request('new',$added),'192.0.2.1');check($next['agent']['catalog']===$family);
check($m->enrollInstallation($request('new',$d),'192.0.2.1')['player']['id']===$next['player']['id']);
// Existing sessions, agent IDs/tokens and custody family remain unchanged.
$scoped=new Market($m->db,'tcpip','SchemaFixture');$agent=$m->authAgent($a['agent']['token']);
$refresh=$scoped->refreshAgentCatalog($agent,['fingerprint'=>Market::saveCatalogHash($added),'compatibility'=>$added]);check($refresh['catalog']===$family&&$m->authAgent($a['agent']['token'])['id']===$agent['id']);
$bad=$added;$bad['stats']['s0'][0]=9;refuses(fn()=>$m->enrollInstallation($request('bad',$bad),'192.0.2.1'),409);
$bad=$added;$bad['items']['rin '][2]=true;refuses(fn()=>$scoped->refreshAgentCatalog($agent,['fingerprint'=>Market::saveCatalogHash($bad),'compatibility'=>$bad]),409);
refuses(fn()=>$m->enrollInstallation(array_replace($request('hash',$d),['catalog'=>str_repeat('f',64)]),'192.0.2.1'),422);
$different=$m->enrollInstallation($request('other mod',$bad,'OtherSchemaFixture'),'192.0.2.2');check($different['agent']['mod']==='OtherSchemaFixture');
refuses(fn()=>(new Market($m->db,'tcpip','OtherSchemaFixture'))->refreshAgentCatalog($agent,['fingerprint'=>Market::saveCatalogHash($d),'compatibility'=>$d]),403);
// A newer mod's new item/property cannot be transferred to an older receiver.
$seller=$scoped->browserActor($next['player']['id']);$buyer=$scoped->browserActor($other['player']['id']);
$item=['seed'=>1234,'code'=>'new','quality'=>2,'index'=>-1,'ethereal'=>0,'sockets'=>0,'item_hex'=>str_repeat('ab',24)];
$listing=$scoped->listing(['id'=>'operator','role'=>'admin'],['owner'=>$seller['id'],'item'=>$item,'price'=>Market::freePrice()],false);
refuses(fn()=>$scoped->command($buyer,['id'=>Market::id(),'type'=>'propose','listing_id'=>$listing['listing_id'],'listing_revision'=>1,'price'=>Market::freePrice()],false),409);
$ring=$scoped->listing(['id'=>'operator','role'=>'admin'],['owner'=>$seller['id'],'item'=>array_replace($item,['seed'=>4321,'code'=>'rin','item_hex'=>str_repeat('cd',24)]),'price'=>Market::freePrice()],false);
$scoped->run('INSERT INTO escrow_items VALUES (?,?,?,?,?,?)',[$ring['item_id'],$seller['id'],$scoped->scope(false),'listed',json_encode(['display'=>['saved_properties'=>[['id'=>1,'value'=>1]]]]),Market::id()]);
refuses(fn()=>$scoped->command($buyer,['id'=>Market::id(),'type'=>'propose','listing_id'=>$ring['listing_id'],'listing_revision'=>1,'price'=>Market::freePrice()],false),409);
$scoped->run('UPDATE escrow_items SET metadata=? WHERE item=?',[json_encode(['display'=>['socketed_items'=>[['code'=>'new','display'=>[]]]]]),$ring['item_id']]);
refuses(fn()=>$scoped->command($buyer,['id'=>Market::id(),'type'=>'propose','listing_id'=>$ring['listing_id'],'listing_revision'=>1,'price'=>Market::freePrice()],false),409);
$pending=Market::id();$scoped->run('INSERT INTO custody_tickets VALUES (?,?,?,?,?,?,?,?,?,?)',[$pending,$a['player']['id'],$scoped->scope(false),$agent['id'],'deposit',str_repeat('a',64),'writing','{}','[]',time()]);
refuses(fn()=>$scoped->refreshAgentCatalog($agent,['fingerprint'=>Market::saveCatalogHash($d),'compatibility'=>$d]),409);
$scoped->run('DELETE FROM custody_tickets WHERE id=?',[$pending]);
// Legacy migration requires a known baseline rather than trusting the first
// newly connecting client to replace a production catalog.
$legacy=$m->user('Legacy');$legacyAgent=$m->pairAgent($legacy['id'],str_repeat('a',64));
refuses(fn()=>$m->refreshAgentCatalog($m->authAgent($legacyAgent['token']),['fingerprint'=>Market::saveCatalogHash($d),'compatibility'=>$d]),409);
$m->seedLegacyCompatibility(str_repeat('a',64),$d);
check($m->refreshAgentCatalog($m->authAgent($legacyAgent['token']),['fingerprint'=>Market::saveCatalogHash($d),'compatibility'=>$d])['catalog']===str_repeat('a',64));
if(isset($argv[1])){$fixture=json_decode(file_get_contents($argv[1]),true,512,JSON_THROW_ON_ERROR);check(Market::saveCatalogHash($fixture['definition'])===$fixture['fingerprint']);}
echo "Save compatibility: versioned schemas, additive updates, stable account/agent recovery, targeted incompatible-definition rejection, old-client receive guards, hash verification, legacy baseline and mod isolation passed.\n";
