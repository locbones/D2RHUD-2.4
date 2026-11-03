<?php
declare(strict_types=1);
require __DIR__.'/../tools/marketplace/server/Market.php';
function check(bool $ok):void{if(!$ok)throw new RuntimeException('Moderation assertion failed at '.json_encode(debug_backtrace(DEBUG_BACKTRACE_IGNORE_ARGS,1)));}
function refuses(callable $f,int $status):void{try{$f();}catch(MarketError $e){check($e->status===$status);return;}throw new RuntimeException('Unauthorized report operation allowed.');}
$m=new Market(new PDO('sqlite::memory:'));$m->migrate();$m->migrate();$seller=$m->user('Seller');$reporter=$m->user('Reporter');$moderator=$m->user('Bonesy');$other=$m->user('Other player');$admin=$m->user('Operator','admin');
$link=function($who)use($m){$m->browserLink(['code'=>$m->browserTicket($who)['code']]);};foreach([$seller,$reporter,$moderator] as $a)$link($a);
$grant=$m->grantModerator($moderator['id']);check($grant['moderator']&&$m->grantModerator($moderator['id'])===$grant);
check($m->auth($moderator['token'])['role']==='player'&&$m->moderationCapabilities($moderator)['moderator']);
check(!$m->moderationCapabilities($reporter)['moderator']&&$m->moderationCapabilities($reporter)['open_count']===0);
refuses(fn()=>$m->moderationQueue($reporter),403);refuses(fn()=>$m->moderationQueue($admin),403);refuses(fn()=>$m->grantModerator('missing'),404);
$gold=[['kind'=>'gold','code'=>'gld','label'=>'Gold','quality'=>2,'index'=>-1,'ethereal'=>-1,'quantity'=>100]];
$listing=$m->listing($admin,['owner'=>$seller['id'],'item'=>['seed'=>123,'code'=>'rin','quality'=>6,'item_hex'=>str_repeat('cb',24)],'price'=>$gold],false);
$packet=['id'=>Market::id(),'type'=>'report_player','subject'=>$seller['id'],'listing_id'=>$listing['listing_id'],'listing_revision'=>1,'reason'=>'duplicated_items','details'=>"I observed two identical items. <script>plain text</script>\nPlease compare their saved identities."];
refuses(fn()=>$m->command($other,$packet,false),403);refuses(fn()=>$m->command($reporter,array_replace($packet,['subject'=>$reporter['id']]),false),422);
refuses(fn()=>$m->command($reporter,array_replace($packet,['reason'=>'invalid']),false),422);refuses(fn()=>$m->command($reporter,array_replace($packet,['details'=>'short']),false),422);
refuses(fn()=>$m->command($reporter,$packet,true),404);refuses(fn()=>$m->command($reporter,array_replace($packet,['listing_revision'=>2]),false),409);
$result=$m->command($reporter,$packet,false);check($m->command($reporter,$packet,false)===$result);check($m->moderationCapabilities($moderator)['open_count']===1);
refuses(fn()=>$m->command($reporter,array_replace($packet,['id'=>Market::id()]),false),409);refuses(fn()=>$m->moderationReport($seller,$result['report_id']),403);refuses(fn()=>$m->moderationReport($reporter,$result['report_id']),403);
$r=$m->moderationReport($moderator,$result['report_id']);check($r['details']===$packet['details']&&$r['evidence']['item']['ledger_id']===$listing['item_id']);
check(!str_contains(json_encode($r),'item_hex')&&!str_contains(json_encode($r),'token'));
$capturedPrice=$r['evidence']['listing']['price'];check($capturedPrice[0]['quantity']===100&&$capturedPrice[0]['kind']==='gold');
$m->run('UPDATE listings SET revision=2,price=? WHERE id=?',[json_encode(Market::freePrice()),$listing['listing_id']]);check($m->moderationReport($moderator,$r['id'])['evidence']['listing']['price']===$capturedPrice);
$review=['id'=>Market::id(),'type'=>'review_report','report_id'=>$r['id'],'expected_revision'=>1,'status'=>'under_review','note'=>'Comparing the item identity and previous owners.'];
refuses(fn()=>$m->command($reporter,$review,false),403);$saved=$m->command($moderator,$review,false);check($m->command($moderator,$review,false)===$saved);
refuses(fn()=>$m->command($moderator,array_replace($review,['id'=>Market::id()]),false),409);
$resolve=array_replace($review,['id'=>Market::id(),'expected_revision'=>2,'status'=>'resolved','note'=>'']);refuses(fn()=>$m->command($moderator,$resolve,false),422);
$resolve['note']='Verified legitimate ownership changes. No duplicate was demonstrated.';$m->command($moderator,$resolve,false);check($m->moderationQueue($moderator)['reports']===[]);
$r=$m->moderationReport($moderator,$r['id']);check($r['status']==='resolved'&&count($r['events'])===3&&$r['events'][1]['note']===$review['note']);
check($m->moderationQueue($moderator,['status'=>'all','q'=>'Seller'])['reports'][0]['id']===$r['id']);check($m->moderationQueue($moderator,['q'=>'%'])['reports']===[]);
// A character report freezes the selected carried/equipped/mercenary record.
$key=str_repeat('a',64);$item=['code'=>'rin','quality'=>6,'index'=>-1,'saved_id'=>456,'item_uuid'=>'00112233445566778899aabbccddeeff','identified'=>true,'display'=>['saved_properties'=>[['id'=>0,'param'=>0,'value'=>10,'list'=>'base']]]];
$character=['name'=>'Test Character','class_id'=>1,'level'=>80,'hardcore'=>false,'saved_at'=>123,'character_key'=>$key,'equipment'=>[$item],'inventory'=>[$item],'mercenary'=>['present'=>true,'equipment'=>[$item]]];
$m->run('INSERT INTO market_characters VALUES (?,?,?,?,?,?,?,?,?)',[$seller['id'],$m->scope(false),$key,'Test Character',1,80,123,456,json_encode($character)]);
$profile=['id'=>Market::id(),'type'=>'report_player','subject'=>$seller['id'],'character_key'=>$key,'character_updated'=>456,'item_slot'=>['kind'=>'mercenary','index'=>0],'reason'=>'modified_item','details'=>'The mercenary item appears to have an impossible property.'];
$pr=$m->command($reporter,$profile,false);$e=$m->moderationReport($moderator,$pr['report_id'])['evidence'];check($e['item']['item_uuid']===$item['item_uuid']&&$e['character']['name']==='Test Character'&&$e['item_slot']['kind']==='mercenary');
$m->run('DELETE FROM market_characters WHERE owner=?',[$seller['id']]);check($m->moderationReport($moderator,$pr['report_id'])['evidence']===$e);
// Name changes neither grant nor remove stable-ID moderator permission.
$m->command($moderator,['id'=>Market::id(),'type'=>'profile_name','name'=>'Moderator','expected_name'=>'Bonesy'],false);$link($other);
$m->command($other,['id'=>Market::id(),'type'=>'profile_name','name'=>'Bonesy','expected_name'=>'Other player'],false);check(!$m->moderationCapabilities($other)['moderator']&&$m->moderationCapabilities($moderator)['moderator']);
refuses(fn()=>$m->command($other,['id'=>Market::id(),'type'=>'grant_moderator','owner'=>$other['id']],false),422);
// Spam is bounded and serialized by the reporter row; retries consume no quota.
$spam=$m->user('Spam test');$link($spam);
for($i=0;$i<5;$i++){$target=$m->user('Target '.$i);$m->command($spam,['id'=>Market::id(),'type'=>'report_player','subject'=>$target['id'],'reason'=>'other','details'=>'A sufficiently detailed observation for review.'],false);}
refuses(fn()=>$m->command($spam,['id'=>Market::id(),'type'=>'report_player','subject'=>$seller['id'],'reason'=>'other','details'=>'Another sufficiently detailed observation.'],false),429);
check($m->run('SELECT status FROM listings WHERE id=?',[$listing['listing_id']])->fetchColumn()==='open');
$foreign=new Market($m->db,'other-community');refuses(fn()=>$foreign->moderationQueue($moderator),403);
$m->run('DELETE FROM market_moderators WHERE owner=?',[$moderator['id']]);refuses(fn()=>$m->moderationReport($moderator,$r['id']),403);
echo "Moderation: stable-ID capability without trading-role changes, private reports/notes, server-captured immutable item/character evidence, no save bytes or trade changes, CAS decisions, idempotency, name-spoof resistance, rate limits, community isolation and revocation passed.\n";
