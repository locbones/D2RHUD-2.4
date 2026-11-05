<?php
declare(strict_types=1);
require __DIR__.'/../tools/marketplace/server/Market.php';
function check(bool $v):void {if(!$v)throw new RuntimeException('Gift assertion failed at '.json_encode(debug_backtrace(DEBUG_BACKTRACE_IGNORE_ARGS,1)));}
function refuses(callable $f,int $status):void {try{$f();}catch(MarketError $e){check($e->status===$status);return;}throw new RuntimeException('Gift guard did not refuse.');}
$fixture=json_decode(file_get_contents($argv[1]),true,512,JSON_THROW_ON_ERROR);
$fixture['definition']['bags']=['RB_High'=>39];
$m=new Market(new PDO('sqlite::memory:'));$m->migrate();$m->migrate();$owner=$m->user('Gift owner');$a=$m->user('First recipient');$b=$m->user('Second recipient');$outsider=$m->user('Unlinked player');$moderator=$m->user('Moderator');$catalog=str_repeat('a',64);
foreach([$owner,$a,$b,$moderator] as $p)$m->browserLink(['code'=>$m->browserTicket($p)['code']]);
$m->grantOwner($owner['id']);$agent=$m->authAgent($m->pairAgent($owner['id'],$catalog)['token']);$m->pairAgent($a['id'],$catalog);$m->pairAgent($b['id'],$catalog);$m->seedLegacyCompatibility($catalog,$fixture['definition']);
$items=$fixture['items'];foreach($items as &$i){$i['ethereal']=(int)$i['ethereal'];$i['identified']=(int)$i['identified'];}unset($i);$pages=[['page'=>11,'before'=>str_repeat('b',64),'after'=>str_repeat('c',64)]];$ticket=Market::id();
// A new owner with no Vault holdings can use an unpriced physical item.
$m->custody($agent,['type'=>'web_sync','catalog'=>$catalog,'settings'=>['page'=>11,'withdraw_page'=>11,'enabled'=>true,'auto_trade'=>false],'inventory'=>[$items[1]],'acknowledged'=>[]],false);
$soloKey=$m->browserSnapshot($owner,false)['local_items'][0]['key'];
$soloCampaign=$m->campaignOwnerCommand($owner,['id'=>Market::id(),'type'=>'campaign_create','title'=>'No asking price required','items'=>['market:'.$soloKey]],false);
check($soloCampaign['items']===1&&(int)$m->run('SELECT COUNT(*) FROM escrow_items WHERE owner=?',[$owner['id']])->fetchColumn()===0);
$m->campaignOwnerCommand($owner,['id'=>Market::id(),'type'=>'campaign_close','campaign_id'=>$soloCampaign['campaign_id']],false);
$m->custody($agent,['type'=>'web_sync','catalog'=>$catalog,'settings'=>['page'=>11,'withdraw_page'=>11,'enabled'=>true,'auto_trade'=>false],'inventory'=>[],'acknowledged'=>[]],false);
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
// Unpriced physical items are immutable templates, not deposits or listings.
$fresh=function(array $row)use($fixture):array{$copy=GiftItemFactory::copy($row['item_hex'],$fixture['definition']);unset($row['price']);return array_replace($row,['item_hex'=>$copy['hex'],'saved_id'=>$copy['seed'],'item_uuid'=>$copy['uuid'],'quantity_bit'=>$copy['quantity_bit']]);};
$marketItem=$fresh($items[0]);$otherPage=array_replace($fresh($items[1]),['page'=>12]);$unidentified=array_replace($fresh($items[1]),['identified'=>0]);$priced=array_replace($fresh($items[1]),['price'=>Market::freePrice()]);
$sync=function(array $rows)use($m,$agent,$catalog):void{$m->custody($agent,['type'=>'web_sync','catalog'=>$catalog,'settings'=>['page'=>11,'withdraw_page'=>11,'enabled'=>true,'auto_trade'=>false],'inventory'=>$rows,'acknowledged'=>[]],false);};
$sync([$marketItem,$otherPage,$unidentified,$priced]);
$snapshot=$m->browserSnapshot($owner,false);$local=$snapshot['local_items'];$marketKey=$local[0]['key'];$marketRef='market:'.$marketKey;
check($local[0]['price']===[]&&!isset($local[0]['item_hex']));
$newCampaign=function(array $refs)use($m,$owner,$create):array{return $m->campaignOwnerCommand($owner,array_replace($create,['id'=>Market::id(),'title'=>'Unpriced market-page gift','items'=>$refs]),false);};
$beforeItems=(int)$m->run('SELECT COUNT(*) FROM items')->fetchColumn();$beforeListings=(int)$m->run('SELECT COUNT(*) FROM listings')->fetchColumn();
$mixed=$newCampaign([$marketRef,$ids[1]]);
check((int)$m->run('SELECT COUNT(*) FROM items')->fetchColumn()===$beforeItems&&(int)$m->run('SELECT COUNT(*) FROM listings')->fetchColumn()===$beforeListings);
check($m->browserSnapshot($owner,false)['local_items']===$local);
check($m->ownerGiftCampaigns($owner,false,$mixed['campaign_id'])['campaigns'][0]['items'][0]['quantity']===$marketItem['quantity']);
refuses(fn()=>$newCampaign(['market:'.str_repeat('f',64)]),409);
refuses(fn()=>$newCampaign(['market:'.$local[1]['key']]),409);
refuses(fn()=>$newCampaign(['market:'.$local[2]['key']]),409);
refuses(fn()=>$newCampaign(['market:'.$local[3]['key']]),409);
refuses(fn()=>$newCampaign([$marketRef,$marketRef]),422);
refuses(fn()=>$newCampaign([['item_hex'=>$marketItem['item_hex']]]),422);
refuses(fn()=>$newCampaign(['market:not-a-key']),422);
refuses(fn()=>$m->campaignOwnerCommand($owner,array_replace($create,['id'=>Market::id(),'items'=>[$marketRef]]),true),409);
$foreign=$fresh($items[1]);$foreignKey=hash('sha256',$foreign['page'].'|'.$foreign['tab'].'|'.$foreign['item_hex']);
$foreignAgent=$m->authAgent($m->pairAgent($b['id'],$catalog)['token']);$m->custody($foreignAgent,['type'=>'web_sync','catalog'=>$catalog,'settings'=>['page'=>11,'withdraw_page'=>11,'enabled'=>true,'auto_trade'=>false],'inventory'=>[$foreign],'acknowledged'=>[]],false);
refuses(fn()=>$newCampaign(['market:'.$foreignKey]),409);
$m->command($owner,['id'=>Market::id(),'type'=>'web_price','key'=>$marketKey,'price'=>Market::freePrice()],false);
refuses(fn()=>$newCampaign([$marketRef]),409);
$m->run("UPDATE web_intents SET status='failed' WHERE owner=? AND kind='price'",[$owner['id']]);
$m->run('INSERT INTO custody_sources VALUES (?,?,?,?)',[$owner['id'],$m->scope(false),$marketKey,$ticket]);
refuses(fn()=>$newCampaign([$marketRef]),409);
$m->run('DELETE FROM custody_sources WHERE owner=? AND source_key=?',[$owner['id'],$marketKey]);
// Native gift visibility and later claims use the frozen bytes, even after
// the original is moved off the market page and its private snapshot clears.
$sync([]);
check(in_array($mixed['campaign_id'],array_column($m->snapshot($a,false)['gifts'],'id'),true));
$gift=$m->command($new,['id'=>Market::id(),'type'=>'campaign_claim','campaign_id'=>$mixed['campaign_id']],false);
check(count($gift['items'])===2);
$granted=$m->run('SELECT item_hex FROM items WHERE id=?',[$gift['items'][0]])->fetchColumn();
check($granted!==$marketItem['item_hex']);$copies[]=['before'=>$marketItem['item_hex'],'after'=>$granted];
check((int)$m->run('SELECT COUNT(*) FROM items')->fetchColumn()===$beforeItems+2);
foreach($originals as $id=>$hex)check($m->run('SELECT item_hex FROM items WHERE id=?',[$id])->fetchColumn()===$hex);
// One claim atomically grants real item copies AND minted resources. No owner
// funds are consumed, and neither a repeated command nor a new ID grants twice.
$gold=['kind'=>'gold','code'=>'gld','label'=>'Gold','quality'=>2,'index'=>-1,'ethereal'=>-1,'quantity'=>1000000];
$points=['kind'=>'bag','code'=>'Z01','stat'=>'RB_High','label'=>'High rune points','quality'=>2,'index'=>-1,'ethereal'=>-1,'quantity'=>3];
$resourcePacket=array_replace($create,['id'=>Market::id(),'title'=>'Items + gold + rune points','items'=>[$ids[0]],'balances'=>[$gold,$points]]);
$resourceCampaign=$m->campaignOwnerCommand($owner,$resourcePacket,false);$resourceBalances=$resourceCampaign['balances'];
check(count($resourceBalances)===2&&$m->giftCampaigns($a,false)[0]['balances']!==null);
check(!in_array($resourceCampaign['campaign_id'],array_column($m->snapshot($a,false)['gifts'],'id'),true)); // Old HUD is never given an unreadable resource bundle.
$m->run('INSERT INTO market_browser_features VALUES (?,?,?)',[$a['id'],$m->scope(false),'["gift_resources"]']);
check(in_array($resourceCampaign['campaign_id'],array_column($m->snapshot($a,false)['gifts'],'id'),true));
$resourceClaim=['id'=>Market::id(),'type'=>'campaign_claim','campaign_id'=>$resourceCampaign['campaign_id']];
$itemsBefore=(int)$m->run('SELECT COUNT(*) FROM items WHERE owner=?',[$a['id']])->fetchColumn();
$m->run('INSERT INTO escrow_balances VALUES (?,?,?,?,?)',[$a['id'],$m->scope(false),'gold','',9000000000000000]);
refuses(fn()=>$m->command($a,$resourceClaim,false),409);
check((int)$m->run('SELECT COUNT(*) FROM items WHERE owner=?',[$a['id']])->fetchColumn()===$itemsBefore);
check((int)$m->run('SELECT COUNT(*) FROM market_gift_claims WHERE campaign=? AND owner=?',[$resourceCampaign['campaign_id'],$a['id']])->fetchColumn()===0);
$m->run('UPDATE escrow_balances SET quantity=0 WHERE owner=?',[$a['id']]);
$received=$m->command($a,$resourceClaim,false);check(count($received['items'])===1&&$received['balances']===$resourceBalances);
check($m->command($a,$resourceClaim,false)===$received);
check($m->command($a,array_replace($resourceClaim,['id'=>Market::id()]),false)['already_claimed']);
check((int)$m->run('SELECT quantity FROM escrow_balances WHERE owner=? AND kind=?',[$a['id'],'gold'])->fetchColumn()===1000000);
check((int)$m->run('SELECT quantity FROM escrow_balances WHERE owner=? AND stat=?',[$a['id'],'RB_High'])->fetchColumn()===3);
check((int)$m->run('SELECT COUNT(*) FROM wallet_entries WHERE reference=? AND action=?',[$resourceClaim['id'],'campaign_claim'])->fetchColumn()===2);
check((int)$m->run('SELECT COUNT(*) FROM market_trade_assets WHERE receipt=?',[$resourceClaim['id']])->fetchColumn()===3);
$withdrawGift=$m->command($a,['id'=>Market::id(),'type'=>'wallet_withdraw','balances'=>$resourceBalances],false);check($withdrawGift['status']==='queued');
$m->command($a,['id'=>Market::id(),'type'=>'wallet_cancel','withdrawal_id'=>$withdrawGift['withdrawal_id'],'expected_revision'=>1],false);
check((int)$m->run('SELECT quantity FROM escrow_balances WHERE owner=? AND stat=?',[$a['id'],'RB_High'])->fetchColumn()===3);
check((int)$m->run('SELECT COUNT(*) FROM escrow_balances WHERE owner=?',[$owner['id']])->fetchColumn()===0);
$audit=$m->ownerGiftCampaigns($owner,false,$resourceCampaign['campaign_id'])['campaigns'][0];check($audit['recent_claims'][0]['balances']===$resourceBalances);
$currencyOnly=$m->campaignOwnerCommand($owner,array_replace($resourcePacket,['id'=>Market::id(),'items'=>[]]),false);
check($currencyOnly['items']===0&&count($m->command($b,['id'=>Market::id(),'type'=>'campaign_claim','campaign_id'=>$currencyOnly['campaign_id']],false)['balances'])===2);
refuses(fn()=>$m->campaignOwnerCommand($owner,array_replace($resourcePacket,['id'=>Market::id(),'items'=>[],'balances'=>[]]),false),422);
foreach([0,-1,1.5,2000000001,true] as $quantity)refuses(fn()=>$m->campaignOwnerCommand($owner,array_replace($resourcePacket,['id'=>Market::id(),'balances'=>[array_replace($gold,['quantity'=>$quantity])]]),false),422);
refuses(fn()=>$m->campaignOwnerCommand($owner,array_replace($resourcePacket,['id'=>Market::id(),'balances'=>[$gold,$gold]]),false),422);
refuses(fn()=>$m->campaignOwnerCommand($owner,array_replace($resourcePacket,['id'=>Market::id(),'balances'=>[array_replace($points,['stat'=>'RB_Ultra'])]]),false),422);
refuses(fn()=>$m->campaignOwnerCommand($owner,array_replace($resourcePacket,['id'=>Market::id(),'balances'=>Market::freePrice()]),false),422);
check(isset((new ReflectionMethod($m,'resetSelections'))->invoke($m)['market_gift_balances']));
$resourceRace=$m->campaignOwnerCommand($owner,array_replace($resourcePacket,['id'=>Market::id(),'items'=>$ids,'expires'=>null]),false);
// Every truncated template is rejected; no partially copied item is returned.
$raw=$fixture['items'][0]['item_hex'];for($n=0;$n<strlen($raw);$n+=2){try{GiftItemFactory::copy(substr($raw,0,$n),$fixture['definition']);throw new LogicException('Truncated item accepted.');}catch(RuntimeException $e){if($e instanceof LogicException)throw $e;}}
if(isset($argv[2]))file_put_contents($argv[2],json_encode($copies,JSON_THROW_ON_ERROR));
echo "Gift campaigns: mixed item/gold/bag and currency-only gifts, atomic resource rollback, exact-once wallet credits/audit, old-HUD isolation, unpriced market-page and mixed Vault templates, unchanged physical originals, stale/foreign/priced/wrong-page refusal, native visibility after moving originals, owner-only controls, frozen unchanged templates, atomic complete bundles, one entitlement per account, replay/new-command idempotency, post-publication accounts, expiry/closure, scoped announcements, private claim messages, partial/replayed delivery receipts and fresh root identities passed.\n";
