<?php
declare(strict_types=1);
require __DIR__.'/../tools/marketplace/server/Market.php';
require __DIR__.'/../tools/marketplace/server/OwnerDemo.php';
function check(bool $ok):void{if(!$ok)throw new RuntimeException('Owner control assertion failed at '.json_encode(debug_backtrace(DEBUG_BACKTRACE_IGNORE_ARGS,1)));}
function refuses(callable $f,int $status):void{try{$f();}catch(MarketError $e){check($e->status===$status);return;}throw new RuntimeException('Owner-only operation allowed.');}
$m=new Market(new PDO('sqlite::memory:'));$m->migrate();$m->migrate();$owner=$m->user('Bonesy');$player=$m->user('Player');$moderator=$m->user('Moderator');$m->grantModerator($moderator['id']);$m->grantOwner($owner['id']);
check($m->isOwner($owner)&&!$m->isOwner($moderator)&&!$m->isOwner(null));refuses(fn()=>$m->grantOwner($player['id']),409);refuses(fn()=>$m->ownerStatus($player),403);
$dir=sys_get_temp_dir().'/market-owner-'.Market::id();mkdir($dir);
$start=['id'=>Market::id(),'type'=>'clear_prepare'];refuses(fn()=>$m->ownerCommand($moderator,$start,$dir),403);
$catalog=str_repeat('a',64);$agent=$m->authAgent($m->pairAgent($player['id'],$catalog)['token']);
$gold=['kind'=>'gold','code'=>'gld','label'=>'Gold','quality'=>2,'index'=>-1,'ethereal'=>-1,'quantity'=>100];$pages=[['page'=>11,'before'=>str_repeat('b',64),'after'=>str_repeat('c',64)]];
$call=function($data)use($m,$agent,$catalog){$data+=['catalog'=>$catalog];return $m->controlledRequest($agent,'custody',$data,false,fn()=>$m->custody($agent,$data,false));};
$id=Market::id();$call(['id'=>$id,'type'=>'deposit_prepare','balances'=>[$gold],'items'=>[],'pages'=>$pages]);refuses(fn()=>$m->ownerCommand($owner,$start,$dir),409);check($m->controlState()['phase']==='live');
$call(['id'=>$id,'type'=>'write_authorize','pages'=>$pages]);$call(['id'=>$id,'type'=>'commit','pages'=>$pages]);
$settings=['page'=>11,'withdraw_page'=>2,'enabled'=>true,'auto_trade'=>true];$call(['type'=>'web_sync','settings'=>$settings,'inventory'=>[]]);$tokenBefore=$m->run('SELECT token_hash FROM actors WHERE id=?',[$owner['id']])->fetchColumn();
$ready=$m->ownerCommand($owner,$start,$dir);check($m->ownerCommand($owner,$start,$dir)===$ready);check(!$ready['ready_to_clear']&&$ready['withdrawals']===1&&$m->controlState()['phase']==='draining');
$finish=['id'=>Market::id(),'type'=>'clear_finish','confirm'=>'CLEAR MARKETPLACE','reset_id'=>$ready['control']['reset_id']];refuses(fn()=>$m->ownerCommand($owner,$finish,$dir),409);
$cmd=['id'=>Market::id(),'type'=>'preferences','auto_trade'=>true];refuses(fn()=>$m->controlledRequest($player,'command',$cmd,false,fn()=>$m->command($player,$cmd,false)),409);
refuses(fn()=>$call(['id'=>Market::id(),'type'=>'deposit_prepare','balances'=>[$gold],'items'=>[],'pages'=>$pages]),409);
$queue=$call(['id'=>Market::id(),'type'=>'delivery_queue']);$wid=$queue['wallet_withdrawals'][0]['id'];$ticket=Market::id();$call(['id'=>$ticket,'type'=>'claim_prepare','withdrawal_id'=>$wid,'balance_caps'=>[$gold]]);
$call(['id'=>$ticket,'type'=>'write_authorize','pages'=>$pages,'delivered'=>[]]);$receipt=$call(['id'=>$ticket,'type'=>'commit','pages'=>$pages]);check($m->ownerStatus($owner)['ready_to_clear']);
$result=$m->ownerCommand($owner,$finish,$dir);check($m->ownerCommand($owner,$finish,$dir)===$result);check(is_file($dir.'/'.$result['archive'].'.json.gz'));
check($m->run('SELECT COUNT(*) FROM items')->fetchColumn()==0&&$m->run('SELECT COUNT(*) FROM wallet_withdrawals')->fetchColumn()==0&&$m->auth($owner['token'])['name']==='Bonesy');
check($m->run('SELECT token_hash FROM actors WHERE id=?',[$owner['id']])->fetchColumn()===$tokenBefore&&$m->isOwner($owner)&&$m->moderationCapabilities($moderator)['moderator']);
check($call(['id'=>$ticket,'type'=>'commit','pages'=>$pages])===$receipt); // Retained exactly-once custody receipt.
refuses(fn()=>$m->controlledRequest($player,'command',$cmd,false,fn()=>$m->command($player,$cmd,false)),409);
$reset=$m->clientReset($player['id'],false);$call(['type'=>'web_sync','settings'=>array_replace($settings,['enabled'=>false,'auto_trade'=>false]),'inventory'=>[],'acknowledged'=>[['id'=>$reset['id'],'status'=>'applied','message'=>'']]]);check($m->clientReset($player['id'],false)['acknowledged']);
$m->controlledRequest($player,'command',$cmd,false,fn()=>$m->command($player,$cmd,false));
// A moderator has no access even if they copy the owner's current name.
$m->run('UPDATE actors SET name=? WHERE id=?',['Bonesy',$moderator['id']]);check(!$m->isOwner($moderator));
// Full-size private scene. No fake identity, holding, message or trade reaches live.
$liveActors=(int)$m->run('SELECT COUNT(*) FROM actors')->fetchColumn();$packet=['id'=>Market::id(),'size'=>'busy','expected_demo'=>''];
$demo=MarketplaceDemo::generate($m,$owner,$packet,$dir);check($demo['players']===65&&$demo['listings']===320&&$demo['sales']===240&&$demo['offers']>300);check(MarketplaceDemo::generate($m,$owner,$packet,$dir)===$demo);
check((int)$m->run('SELECT COUNT(*) FROM actors')->fetchColumn()===$liveActors&&(int)$m->run('SELECT COUNT(*) FROM listings')->fetchColumn()===0);
refuses(fn()=>MarketplaceDemo::open($dir,$player),403);refuses(fn()=>MarketplaceDemo::generate($m,$moderator,$packet,$dir),403);
$d=MarketplaceDemo::open($dir,$owner);$stats=$d->tradeStats(false,['window'=>'all']);check($stats['summary']['trades']===240&&$stats['summary']['active_listings']===320&&count($stats['recent'])===10&&count($stats['popular'])===8&&count($stats['sellers'])===8);check($stats['sellers'][0]['trades']>= $stats['sellers'][1]['trades']);check(!str_contains(json_encode($stats),'item_hex')&&!str_contains(json_encode($stats),'token_hash'));check($d->tradeStats(true)['summary']['trades']===0);check((new Market($d->db,'other-community'))->tradeStats(false)['summary']['trades']===0);check($d->tradeStats(false,['window'=>'7'])['summary']['trades']<240);refuses(fn()=>$d->tradeStats(false,['window'=>'bad']),422);$snapshot=$d->browserSnapshot($owner,false);
$historyItems=0;$competing=0;foreach($snapshot['listings'] as $liveListing){$past=$d->historyPage(false,['item_id'=>$liveListing['item_id'],'averages'=>true]);if(count($past['rows'])>=2){++$historyItems;check($past['averages']['sales']>0);$chain=array_reverse($past['rows']);for($h=1;$h<count($chain);++$h)check($chain[$h-1]['buyer']===$chain[$h]['seller']);check(end($chain)['buyer']===$liveListing['seller_id']);}if($liveListing['active_offer_count']>=2)++$competing;}
check($historyItems===24&&$competing>=20);$listing=current(array_filter($snapshot['listings'],fn($r)=>!$r['own']&&($r['price'][0]['kind']??'')==='gold'));
$offer=['id'=>Market::id(),'type'=>'propose','listing_id'=>$listing['id'],'listing_revision'=>1,'price'=>$listing['price']];$r=$d->command($owner,$offer,false);MarketplaceDemo::respond($d,$owner,$offer,$r);$count=(int)$d->run('SELECT COUNT(*) FROM commands')->fetchColumn();MarketplaceDemo::respond($d,$owner,$offer,$r);check((int)$d->run('SELECT COUNT(*) FROM commands')->fetchColumn()===$count);
check($d->run('SELECT status FROM offers WHERE id=?',[$r['offer_id']])->fetchColumn()==='completed');
$d=null;refuses(fn()=>MarketplaceDemo::clear($owner,['id'=>Market::id(),'confirm'=>'CLEAR DEMO','expected_demo'=>'stale'],$dir),409);MarketplaceDemo::clear($owner,['id'=>Market::id(),'confirm'=>'CLEAR DEMO','expected_demo'=>$demo['id']],$dir);check(!MarketplaceDemo::status($dir,$owner)['available']);
echo "Owner controls: stable owner-only capability, moderator refusal, isolated 65-player/320-listing/240-sale demo, real-domain bot settlement and retry dedupe, guarded escrow drain, blocked early purge/new deposits, private backup, retained links/permissions and exactly-once save receipts, late-client reset fence and stale-demo refusal passed.\n";
