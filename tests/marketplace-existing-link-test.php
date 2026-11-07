<?php
declare(strict_types=1);
require __DIR__.'/../tools/marketplace/server/Market.php';
function check(bool $ok):void {if(!$ok)throw new RuntimeException('Existing link assertion failed.');}
function refuses(callable $fn):void {try{$fn();}catch(MarketError $e){check(in_array($e->status,[403,404,422],true));return;}throw new RuntimeException('Unlinked scope accepted.');}
$db=new PDO('sqlite::memory:');$m=new Market($db);$m->migrate();$player=$m->user('Existing player');$other=$m->user('Other player');
$agent=$m->pairAgent($player['id'],str_repeat('a',64));$agents=$m->run('SELECT id,owner FROM custody_agents_scoped')->fetchAll();
$context=['existing_profile'=>true,'mod'=>'RMD-MP','game_mode'=>'2.4'];
$ticket=$m->browserTicket($player,$context);$linked=$m->browserLink(['code'=>$ticket['code']]);check($linked['id']===$player['id']);
refuses(fn()=>$m->browserTicket($other,$context));
refuses(fn()=>$m->browserTicket($player,$context+['catalog'=>str_repeat('a',64)]));
refuses(fn()=>$m->browserTicket($player,array_replace($context,['mod'=>'UnlinkedMod'])));
refuses(fn()=>$m->browserTicket($player,array_replace($context,['game_mode'=>'3.x'])));
$custom=new Market($db,'tcpip','CustomMod');$custom->pairAgent($player['id'],str_repeat('b',64));
$ticket=$m->browserTicket($player,array_replace($context,['mod'=>'CustomMod']));check($ticket['mod']==='CustomMod');
check($m->run('SELECT id,owner FROM custody_agents_scoped WHERE `mod`=?',['RMD-MP'])->fetchAll()===$agents);
check((int)$m->run('SELECT COUNT(*) FROM actors')->fetchColumn()===2);
echo "Existing account links: exact owner, existing mod membership, no enrollment/agent rotation, no catalog registration and cross-scope rejection passed.\n";


