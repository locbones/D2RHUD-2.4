<?php
declare(strict_types=1);
require __DIR__.'/../tools/marketplace/server/Market.php';
function check(bool $ok):void{if(!$ok)throw new RuntimeException('Enrollment check failed at '.json_encode(debug_backtrace(DEBUG_BACKTRACE_IGNORE_ARGS,1)));}
function refuses(callable $call,int $status):void{try{$call();}catch(MarketError $e){check($e->status===$status);return;}throw new RuntimeException('Unsafe enrollment accepted.');}
$m=new Market(new PDO('sqlite::memory:'));$m->migrate();$m->migrate();
$request=['installation'=>str_repeat('a',64),'mod'=>'RMD-MP','game_mode'=>'2.4','catalog'=>str_repeat('b',64)];
$first=$m->enrollInstallation($request,'127.0.0.1');$again=$m->enrollInstallation($request,'127.0.0.1');
check($first==$again&&$first['player']['role']==='player'&&$first['agent']['owner']===$first['player']['id']);
check($m->auth($first['player']['token'])['id']===$first['player']['id']);check($m->authAgent($first['agent']['token'])['owner']===$first['player']['id']);
check((int)$m->run('SELECT COUNT(*) FROM actors')->fetchColumn()===1);
check($m->run('SELECT installation FROM market_installations')->fetchColumn()!==$request['installation']);
refuses(fn()=>$m->enrollInstallation(array_replace($request,['game_mode'=>'3.x']),'127.0.0.1'),409);
refuses(fn()=>$m->enrollInstallation(array_replace($request,['catalog'=>str_repeat('c',64)]),'127.0.0.1'),409);
refuses(fn()=>$m->enrollInstallation(array_replace($request,['mod'=>'OtherMod']),'127.0.0.1'),409);
refuses(fn()=>$m->enrollInstallation(array_replace($request,['installation'=>'bad']),'127.0.0.1'),422);
// Random installation secrets create ordinary independent players. A caller
// cannot nominate an existing owner or elevate its role.
$next=$m->enrollInstallation(array_replace($request,['installation'=>str_repeat('d',64),'owner'=>$first['player']['id'],'role'=>'admin']),'127.0.0.1');
check($next['player']['id']!==$first['player']['id']&&$next['player']['role']==='player');
for($i=0;$i<10;$i++)$m->enrollInstallation(array_replace($request,['installation'=>hash('sha256',(string)$i)]),'127.0.0.1');
refuses(fn()=>$m->enrollInstallation(array_replace($request,['installation'=>str_repeat('e',64)]),'127.0.0.1'),429);
check($m->enrollInstallation($request,'127.0.0.1')==$first); // reconnect is still available
// New-mod registrations validate their own definitions, preserving separation.
$before=(int)$m->run('SELECT COUNT(*) FROM actors')->fetchColumn();
refuses(fn()=>$m->enrollInstallation(array_replace($request,['installation'=>str_repeat('f',64),'mod'=>'FixtureMod']),'192.0.2.2'),422);
check((int)$m->run('SELECT COUNT(*) FROM actors')->fetchColumn()===$before);
$definitions=json_decode(file_get_contents(__DIR__.'/../tools/marketplace/server/item-identity.json'),true,512,JSON_THROW_ON_ERROR);
$other=$m->enrollInstallation(array_replace($request,['installation'=>str_repeat('f',64),'mod'=>'FixtureMod','identity_catalog'=>$definitions]),'192.0.2.2');
check($other['agent']['mod']==='FixtureMod');check(!$m->isOwner($other['player']));
check($m->browserTicket($first['player'])['mod']==='RMD-MP');
echo "Enrollment: unique ordinary players, retry identity recovery, catalog/mod separation, no escalation, hashed secrets, rollback, rate limits and browser linking passed.\n";
