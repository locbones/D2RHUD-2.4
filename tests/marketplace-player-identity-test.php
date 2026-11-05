<?php
declare(strict_types=1);
require __DIR__.'/../tools/marketplace/server/Market.php';
function check(bool $ok):void{if(!$ok)throw new RuntimeException('Player identity test failed: '.json_encode(debug_backtrace(DEBUG_BACKTRACE_IGNORE_ARGS,1)));}
function refuses(callable $f,int $status):void{try{$f();}catch(MarketError $e){check($e->status===$status);return;}throw new RuntimeException('Unexpected player identity action allowed.');}
$m=new Market(new PDO('sqlite::memory:'));$m->migrate();
$link=function(string $name)use($m){$a=$m->user($name);$m->browserLink(['code'=>$m->browserTicket($a)['code']]);return $a;};
$rename=fn($a,$name,$expected)=>$m->command($a,['id'=>Market::id(),'type'=>'profile_name','name'=>$name,'expected_name'=>$expected],false);
$first=$link('One');$second=$link('Two');$third=$link('Three');
$id=$m->publicPlayerId($first['id']);check(preg_match('/^P-[0-9]{6,}$/D',$id)===1);
$rename($first,'Shared Name','One');$rename($second,'Shared Name','Two');
check($m->publicPlayerId($first['id'])===$id&&$m->publicPlayerId($second['id'])!==$id);
$m->migrate();check($m->publicPlayerId($first['id'])===$id);
check(count($m->players(false,['q'=>'Shared Name'])['players'])===2);
$found=$m->players(false,['q'=>$id])['players'];check(count($found)===1&&$found[0]['id']===$first['id']);
check($m->playerProfile($first['id'],false)['player']['player_id']===$id);
$send=fn($to)=>$m->command($third,['id'=>Market::id(),'type'=>'direct_message','to'=>$to,'message'=>'Exact recipient test'],false);
refuses(fn()=>$send('Shared Name'),422);check($send($id)['peer']===$first['id']);check($send($second['id'])['peer']===$second['id']);
refuses(fn()=>$m->command($third,['id'=>Market::id(),'type'=>'gift_recipient','to'=>'Shared Name'],false),422);
check($m->command($third,['id'=>Market::id(),'type'=>'gift_recipient','to'=>$id],false)['recipient_id']===$first['id']);
check($m->conversations($third,$first['id'])['peer']['player_id']===$id);
check($m->inbox($first)['messages'][0]['sender_player_id']===$m->publicPlayerId($third['id']));
// A name is never an authorization credential. Recovery is explicitly armed
// by an operator for one code-linked replacement and consumed exactly once.
$old=$link('Bonesy');$replacement=$link('Fresh Player');$m->grantOwner($old['id']);$m->grantModerator($old['id']);
$retired=$m->retirePlayer($old['id'],$replacement['id'],'Bonesy');check($retired['permissions_pending']['owner']);
refuses(fn()=>$m->auth($old['token']),401);refuses(fn()=>$m->browserActor($old['id']),401);refuses(fn()=>$m->playerProfile($old['id'],false),404);
refuses(fn()=>$m->controlledRequest($old,'command',[],false,fn()=>['stale'=>'actor']),401);
$rename($third,'Bonesy','Three');check(!$m->isOwner($third)&&!$m->moderationCapabilities($third)['moderator']);
$rename($replacement,'Someone Else','Fresh Player');check(!$m->isOwner($replacement));
$rename($replacement,'Bonesy','Someone Else');check($m->isOwner($replacement)&&$m->moderationCapabilities($replacement)['moderator']);
$rename($replacement,'Different Name','Bonesy');check($m->isOwner($replacement));
$m->run('DELETE FROM market_moderators WHERE owner=?',[$replacement['id']]);
$rename($replacement,'Bonesy','Different Name');check(!$m->moderationCapabilities($replacement)['moderator']);
check((int)$m->run('SELECT claimed FROM market_profile_recoveries WHERE owner=?',[$replacement['id']])->fetchColumn()>0);
// Retiring an account must never strand even a small wallet balance.
$held=$link('Held Player');$m->run('INSERT INTO escrow_balances VALUES (?,?,?,?,?)',[$held['id'],$m->scope(false),'gold','',1]);
refuses(fn()=>$m->retirePlayer($held['id'],$first['id'],'Held Player'),409);check($m->auth($held['token'])['id']===$held['id']);
echo "Player identity: duplicate names, immutable compact IDs, migration, ID search, unambiguous messages/gifts, safe retirement, bound single-use permission recovery passed.\n";
