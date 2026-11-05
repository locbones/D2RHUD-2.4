<?php
declare(strict_types=1);
require __DIR__.'/../tools/marketplace/server/Market.php';
function check(bool $v):void {if(!$v)throw new RuntimeException('Gift assertion failed at '.json_encode(debug_backtrace(DEBUG_BACKTRACE_IGNORE_ARGS,1)));}
function refuses(callable $f,int $status):void {try{$f();}catch(MarketError $e){check($e->status===$status);return;}throw new RuntimeException('Gift guard did not refuse.');}
$fixture=json_decode(file_get_contents($argv[1]),true,512,JSON_THROW_ON_ERROR);
$m=new Market(new PDO('sqlite::memory:'));$m->migrate();$m->migrate();$owner=$m->user('Gift owner');$a=$m->user('First recipient');$b=$m->user('Second recipient');$outsider=$m->user('Unlinked player');$moderator=$m->user('Moderator');$catalog=str_repeat('a',64);
foreach([$owner,$a,$b,$moderator] as $p)$m->browserLink(['code'=>$m->browserTicket($p)['code']]);
$m->grantOwner($owner['id']);$agent=$m->authAgent($m->pairAgent($owner['id'],$catalog)['token']);$m->pairAgent($a['id'],$catalog);$m->pairAgent($b['id'],$catalog);$m->seedLegacyCompatibility($catalog,$fixture['definition']);
$items=$fixture['items'];foreach($items as &$i){$i['ethereal']=(int)$i['ethereal'];$i['identified']=(int)$i['identified'];}unset($i);$pages=[['page'=>11,'before'=>str_repeat('b',64),'after'=>str_repeat('c',64)]];$ticket=Market::id();
foreach([['type'=>'deposit_prepare','items'=>$items,'balances'=>[],'pages'=>$pages],['type'=>'write_authorize','pages'=>$pages],['type'=>'commit','pages'=>$pages]] as $p)$m->custody($agent,$p+['id'=>$ticket,'catalog'=>$catalog],false);
$source=$m->run('SELECT id,item_hex FROM items WHERE owner=? ORDER BY id',[$owner['id']])->fetchAll();$ids=array_column($source,'id');$originals=array_column($source,'item_hex','id');
$create=['id'=>Market::id(),'type'=>'campaign_create','title'=>'Community celebration','announcement'=>'A bundle for everyone!','message'=>'Thank you for playing. Enjoy your gift!','items'=>$ids,'expires'=>time()+3600];
refuses(fn()=>$m->campaignOwnerCommand($a,$create,false),403);
$campaign=$m->campaignOwnerCommand($owner,$create,false);check($m->campaignOwnerCommand($owner,$create,false)===$campaign);
refuses(fn()=>$m->campaignOwnerCommand($owner,array_replace($create,['title'=>'Changed']),false),409);
$public=$m->giftCampaigns(null,false);check(count($public)===1&&!$public[0]['claimable']&&!str_contains(json_encode($public),'item_hex')&&!str_contains(json_encode($public),'Thank you for playing'));
check($m->giftCampaigns($a,true)===[]);check($m->giftCampaigns($a,false)[0]['claimable']);
check($m->snapshot($a,false)['gifts'][0]['items'][0]['item_hex']!=='');
$inbox=$m->inbox($a);check(count(array_filter($inbox['messages'],fn($r)=>str_starts_with($r['body'],'[Community gift]')))===1);$m->inbox($a);check((int)$m->run('SELECT COUNT(*) FROM market_gift_notices WHERE owner=?',[$a['id']])->fetchColumn()===1);
$claim=['id'=>Market::id(),'type'=>'campaign_claim','campaign_id'=>$campaign['campaign_id']];
refuses(fn()=>$m->command($outsider,$claim,false),403);refuses(fn()=>$m->command($a,$claim,true),404);
$r=$m->command($a,$claim,false);check($r['claimed']&&!$r['already_claimed']&&count($r['items'])===count($ids)&&$r['message']===$create['message']);
check($m->command($a,$claim,false)===$r);$again=$m->command($a,array_replace($claim,['id'=>Market::id()]),false);check($again['already_claimed']&&$again['items']===$r['items']);
$second=$m->command($b,array_replace($claim,['id'=>Market::id()]),false);check(count(array_intersect($r['items'],$second['items']))===0);
$new=$m->user('Joined after publication');$m->browserLink(['code'=>$m->browserTicket($new)['code']]);$m->pairAgent($new['id'],$catalog);check($m->command($new,array_replace($claim,['id'=>Market::id()]),false)['claimed']);
$rootUuids=[];$copies=[];
foreach($m->run('SELECT i.*,e.metadata FROM items i JOIN escrow_items e ON e.item=i.id WHERE i.owner<>?',[$owner['id']])->fetchAll() as $i){$meta=json_decode($i['metadata'],true);$rootUuids[]=$meta['item_uuid'];check($meta['delivery_requested']&&$meta['gift_campaign']===$campaign['campaign_id']&&$meta['price']===[]);$before=$originals[$m->run('SELECT source_item FROM market_trade_assets WHERE item=?',[$i['id']])->fetchColumn()];$copies[]=['before'=>$before,'after'=>$i['item_hex']];}
check(count(array_unique($rootUuids))===count($rootUuids));
foreach($originals as $id=>$hex)check($m->run('SELECT item_hex FROM items WHERE id=?',[$id])->fetchColumn()===$hex);
// Use the normal delivery protocol, first with one free cell, then the rest.
// A full page may defer delivery; it never consumes a second campaign claim.
$recipientAgent=$m->authAgent($m->pairAgent($b['id'],$catalog)['token']);
$deliver=function(int $limit,array $page)use($m,$recipientAgent,$catalog){$ticket=Market::id();$packet=$m->custody($recipientAgent,['id'=>$ticket,'type'=>'claim_prepare','catalog'=>$catalog,'queued_only'=>true,'item_limit'=>$limit],false)['packet'];$placed=[];foreach($packet['items'] as $item)$placed[]=['item'=>$item['item'],'item_hex'=>$item['item_hex']];foreach([['type'=>'write_authorize','pages'=>$page,'delivered'=>$placed],['type'=>'commit','pages'=>$page],['type'=>'commit','pages'=>$page]] as $command)$m->custody($recipientAgent,$command+['id'=>$ticket,'catalog'=>$catalog],false);return count($placed);};
check($deliver(1,$pages)===1);
check((int)$m->run("SELECT COUNT(*) FROM escrow_items WHERE owner=? AND state='available'",[$b['id']])->fetchColumn()===count($ids)-1);
check($deliver(16,[['page'=>11,'before'=>str_repeat('c',64),'after'=>str_repeat('d',64)]])===count($ids)-1);
foreach($second['items'] as $item){check($m->run('SELECT state FROM escrow_items WHERE item=?',[$item])->fetchColumn()==='delivered');check((int)$m->run('SELECT delivered_at FROM market_trade_assets WHERE item=? AND receipt=?',[$item,$second['claim_id']])->fetchColumn()>0);}
check((int)$m->run("SELECT COUNT(*) FROM events WHERE kind='completed_sale'")->fetchColumn()===0);
$reset=(new ReflectionMethod($m,'resetSelections'))->invoke($m);foreach(['market_gift_claims','market_gift_notices','market_gift_campaigns'] as $table)check(isset($reset[$table]));
check((int)$m->run('SELECT COUNT(*) FROM market_gift_claims')->fetchColumn()===3);
$audit=$m->ownerGiftCampaigns($owner,false,$campaign['campaign_id'])['campaigns'][0];check($audit['claims']===3&&count($audit['recent_claims'])===3);
check($m->giftCampaigns($a,false)[0]['claimed']&&!$m->giftCampaigns($a,false)[0]['claimable']);
check($m->giftCampaigns($a,false)[0]['claim']['message']===$create['message']);
$close=['id'=>Market::id(),'type'=>'campaign_close','campaign_id'=>$campaign['campaign_id']];$m->campaignOwnerCommand($owner,$close,false);check($m->giftCampaigns($b,false)===[]);check($m->command($a,array_replace($claim,['id'=>Market::id()]),false)['already_claimed']);
$expired=$m->campaignOwnerCommand($owner,array_replace($create,['id'=>Market::id(),'expires'=>time()+10]),false);$m->run('UPDATE market_gift_campaigns SET expires=? WHERE id=?',[time()-1,$expired['campaign_id']]);refuses(fn()=>$m->command($a,array_replace($claim,['id'=>Market::id(),'campaign_id'=>$expired['campaign_id']]),false),409);
$noExpiry=$m->campaignOwnerCommand($owner,array_replace($create,['id'=>Market::id(),'expires'=>null]),false);check($noExpiry['expires']===null);
$bad=array_replace($create,['id'=>Market::id(),'items'=>[$ids[0],$ids[0]]]);refuses(fn()=>$m->campaignOwnerCommand($owner,$bad,false),422);
refuses(fn()=>$m->campaignOwnerCommand($owner,array_replace($create,['id'=>Market::id(),'items'=>[$r['items'][0]]]),false),409);
// Every truncated template is rejected; no partially copied item is returned.
$raw=$fixture['items'][0]['item_hex'];for($n=0;$n<strlen($raw);$n+=2){try{GiftItemFactory::copy(substr($raw,0,$n),$fixture['definition']);throw new LogicException('Truncated item accepted.');}catch(RuntimeException $e){if($e instanceof LogicException)throw $e;}}
if(isset($argv[2]))file_put_contents($argv[2],json_encode($copies,JSON_THROW_ON_ERROR));
echo "Gift campaigns: owner-only controls, frozen unchanged templates, atomic complete bundles, one entitlement per account, replay/new-command idempotency, post-publication accounts, expiry/closure, scoped announcements, private claim messages, partial/replayed delivery receipts and fresh root identities passed.\n";
