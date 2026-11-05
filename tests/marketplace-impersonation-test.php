<?php
declare(strict_types=1);
require __DIR__.'/../tools/marketplace/server/Market.php';
function check(bool $ok):void{if(!$ok)throw new RuntimeException('Impersonation assertion failed: '.json_encode(debug_backtrace(DEBUG_BACKTRACE_IGNORE_ARGS,1)));}
function refuses(callable $f,int $status):void{try{$f();}catch(MarketError $e){check($e->status===$status);return;}throw new RuntimeException('Unexpected impersonation access.');}
$m=new Market(new PDO('sqlite::memory:'));$m->migrate();
$owner=$m->user('Owner');$target=$m->user('Target');$other=$m->user('Other');$moderator=$m->user('Moderator');$m->grantOwner($owner['id']);$m->grantModerator($moderator['id']);
foreach([$owner,$target,$other,$moderator] as $actor)$m->browserLink(['code'=>$m->browserTicket($actor)['code']]);
$m->run('UPDATE actors SET name=? WHERE id=?',[$owner['name'],$other['id']]);
$packet=['id'=>Market::id(),'type'=>'start','player'=>$target['id']];
foreach([$other,$moderator] as $actor){refuses(fn()=>$m->startImpersonation($actor,$packet,false),403);refuses(fn()=>$m->impersonationHistory($actor),403);}
$session=$m->startImpersonation($owner,$packet,false);check($m->startImpersonation($owner,$packet,false)===$session);
refuses(fn()=>$m->startImpersonation($owner,array_replace($packet,['player'=>$other['id']]),false),409);
refuses(fn()=>$m->startImpersonation($owner,['id'=>Market::id(),'player'=>$owner['id']],false),422);
$context=$m->impersonationContext($owner,$session['id']);check($context['player']['id']===$target['id']&&$context['owner']['id']===$owner['id']);check(!str_contains(json_encode($context),'stamp'));
refuses(fn()=>$m->impersonationContext($other,$session['id']),404);
$command=['id'=>Market::id(),'type'=>'web_setup','expected_revision'=>0,'page'=>17,'withdraw_page'=>4,'enabled'=>false,'auto_trade'=>false];
$audit=$m->beginImpersonationAction($owner,$context,$command,false);$result=$m->command($target,$command,false);$m->finishImpersonationAction($owner,$audit,200,$result);
check($m->browserSnapshot($target,false)['profile']['desired']['page']===17&&$m->browserSnapshot($owner,false)['profile']===null);
$failure=['id'=>Market::id(),'type'=>'direct_message','to'=>$target['id'],'message'=>'Private text that must not enter operator logs'];
$audit=$m->beginImpersonationAction($owner,$context,$failure,false);$m->finishImpersonationAction($owner,$audit,422,['error'=>'Example refusal']);
$history=$m->impersonationHistory($owner,$session['id'])['sessions'][0];check(count($history['actions'])===2&&$history['owner_player_id']===$context['owner']['player_id']);
check(!str_contains(json_encode($history),$failure['message'])&&!str_contains(json_encode($history),'target_stamp'));
check(in_array('failed',array_column($history['actions'],'outcome'),true)&&in_array('completed',array_column($history['actions'],'outcome'),true));
$m->stopImpersonation($owner,$session['id']);$m->stopImpersonation($owner,$session['id']);check($m->impersonationContext($owner,$session['id'])===null);
refuses(fn()=>$m->beginImpersonationAction($owner,$context,$command,false),409);
$expired=$m->startImpersonation($owner,['id'=>Market::id(),'player'=>$target['id']],false);$expired['expires']=time()-1;$m->run('UPDATE market_owner_actions SET result=? WHERE id=?',[json_encode($expired),$expired['id']]);check($m->impersonationContext($owner,$expired['id'])===null);
$changed=$m->startImpersonation($owner,['id'=>Market::id(),'player'=>$target['id']],false);$m->run('UPDATE actors SET token_hash=? WHERE id=?',[hash('sha256','changed'),$target['id']]);check($m->impersonationContext($owner,$changed['id'])===null);
$revoked=$m->startImpersonation($owner,['id'=>Market::id(),'player'=>$target['id']],false);$m->run('DELETE FROM market_owners WHERE owner=?',[$owner['id']]);check($m->impersonationContext($owner,$revoked['id'])===null);
$m->grantOwner($owner['id']);$different=new Market($m->db,'tcpip','OtherMod');refuses(fn()=>$different->startImpersonation($owner,['id'=>Market::id(),'player'=>$target['id']],false),403);
echo "Owner impersonation: UID-only authorization, no moderator/name bypass, private target isolation, one-hour expiry, authority/link revocation, idempotent start/stop, mod isolation and redacted action audit passed.\n";
