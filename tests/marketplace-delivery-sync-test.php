<?php
declare(strict_types=1);
require __DIR__.'/../tools/marketplace/server/Market.php';
function check(bool $ok,string $why):void{if(!$ok)throw new RuntimeException($why);}
$m=new Market(new PDO('sqlite::memory:'));$m->migrate();$a=$m->user('Delivery fixture');$other=$m->user('Other fixture');
$catalog=str_repeat('a',64);$agent=$m->authAgent($m->pairAgent($a['id'],$catalog)['token']);$m->pairAgent($other['id'],$catalog);
$request=['type'=>'web_delivery_sync','id'=>Market::id()];
$r=$m->command($a,$request,false);$repeat=$m->command($a,$request,false);
check($r===$repeat,'Request retry was not idempotent');
check($m->command($a,['type'=>'web_delivery_sync','id'=>Market::id()],false)['request_id']===$r['request_id'],'Click storm created multiple requests');
$queue=['type'=>'delivery_queue','id'=>Market::id(),'catalog'=>$catalog];
check($m->custody($agent,$queue,false)['stash_sync']['request_id']===$r['request_id'],'Companion did not discover web request');
$report=['request_id'=>$r['request_id'],'state'=>'waiting_for_idle','page'=>11,'pending'=>1];
$m->custody($agent,$queue+['stash_sync'=>$report],false);
$status=$m->browserSnapshot($a,false)['stash_sync'];check($status['progress']==$report,'Progress not visible in browser');
$detail=array_replace($report,['state'=>'waiting_for_stash','handoff'=>'native_lock','runtime_build'=>str_repeat('b',20)]);
$m->custody($agent,$queue+['stash_sync'=>$detail],false);
check($m->browserSnapshot($a,false)['stash_sync']['progress']==$detail,'The account sync report lost its handoff reason or runtime build');
$m->custody($agent,$queue+['stash_sync'=>array_replace($detail,['handoff'=>'C:/Users/Example','runtime_build'=>'local path'])],false);
$progress=$m->browserSnapshot($a,false)['stash_sync']['progress'];
check(!isset($progress['handoff'])&&!isset($progress['runtime_build']),'Sync progress accepted private diagnostic strings');
check($m->browserSnapshot($other,false)['stash_sync']['request_id']==='','Sync request leaked to another player');
check($m->browserSnapshot($a,true)['stash_sync']['request_id']==='','Softcore request leaked to hardcore');
$otherMod=new Market($m->db,$m->community,'MyCustomMod');$otherMod->pairAgent($a['id'],$catalog);
check($otherMod->browserSnapshot($a,false)['stash_sync']['request_id']==='','Request crossed mod scopes');
try{$m->custody($agent,$queue+['stash_sync'=>array_replace($report,['state'=>'write_authorized'])],false);throw new RuntimeException('Unsafe progress accepted');}catch(MarketError $e){check($e->status===422,'Invalid progress response');}
check((int)$m->run('SELECT COUNT(*) FROM custody_tickets')->fetchColumn()===0,'Manual sync reserved or authorized goods');
check((int)$m->run('SELECT COUNT(*) FROM escrow_items')->fetchColumn()===0,'Manual sync fabricated holdings');
echo "PASS manual delivery sync: idempotency, click coalescing, companion discovery, progress, owner/mod/mode isolation and no save/ownership authority.\n";
