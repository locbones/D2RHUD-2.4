<?php
declare(strict_types=1);
require __DIR__.'/../tools/marketplace/server/Market.php';
function check(bool $ok):void{if(!$ok)throw new RuntimeException('Player assertion failed at '.json_encode(debug_backtrace(DEBUG_BACKTRACE_IGNORE_ARGS,1)));}
function refuses(callable $f,int $status):void{try{$f();}catch(MarketError $e){check($e->status===$status);return;}throw new RuntimeException('Unexpected player operation allowed.');}
$m=new Market(new PDO('sqlite::memory:'));$m->migrate();$m->migrate();
$a=$m->user('Alice');$b=$m->user('Bonesy');$c=$m->user('Private Player');$cat=str_repeat('a',64);$agent=$m->authAgent($m->pairAgent($a['id'],$cat)['token']);
check($m->players(false)['players']===[]);
$link=function($actor)use($m){$ticket=$m->browserTicket($actor);$m->browserLink(['code'=>$ticket['code']]);};$link($a);$link($b);
check(count($m->players(false)['players'])===2);check($m->players(false,['q'=>'ALI'])['players'][0]['id']===$a['id']);check($m->players(false,['q'=>'%'])['players']===[]);
refuses(fn()=>$m->playerProfile($c['id'],false),404);
$call=fn($data,$hc=false)=>$m->custody($agent,$data+['catalog'=>$cat],$hc);
$fixture=isset($argv[1])?json_decode(file_get_contents($argv[1]),true,512,JSON_THROW_ON_ERROR):null;
if($fixture){
    $result=$call(['type'=>'profile_sync','character'=>$fixture,'saved_at'=>time()]);$key=$result['character_key'];
    $profile=$m->playerProfile($a['id'],false,$key);$character=$profile['character'];
    check(count($character['equipment'])===count($fixture['equipment'])&&count($character['inventory'])===count($fixture['inventory']));
    check(count($character['mercenary']['equipment'])===count($fixture['mercenary']['equipment']));
    check(!str_contains(json_encode($profile),'item_hex')&&!str_contains(json_encode($profile),'source_directory'));
    foreach($character['inventory'] as $row)check(($row['location']===0&&$row['container']===1)||$row['location']===2);
    $forbidden=$fixture;$row=$forbidden['inventory'][0];$hex=$row['item_hex'];$bytes=hex2bin($hex);for($i=0;$i<3;$i++){$at=50+$i;$p=intdiv($at,8);$bytes[$p]=chr((ord($bytes[$p])&~(1<<($at%8)))|(((5>>$i)&1)<<($at%8)));}$row['item_hex']=bin2hex($bytes);$forbidden['inventory']=[$row];
    refuses(fn()=>$call(['type'=>'profile_sync','character'=>$forbidden,'saved_at'=>time()]),422);
    refuses(fn()=>$call(['type'=>'profile_sync','character'=>$fixture,'saved_at'=>time()],true),422);
    refuses(fn()=>$m->playerProfile($a['id'],true,$key),404);
    $call(['type'=>'profile_manifest','characters'=>[]]);check($m->playerProfile($a['id'],false)['characters']===[]);
}
$packet=['id'=>Market::id(),'type'=>'direct_message','to'=>$b['id'],'message'=>"Hello <script>alert(1)</script>\nA private UTF-8 reply: é 中"];
$r=$m->command($a,$packet,false);check($r===$m->command($a,$packet,true));check(count($m->inbox($b)['messages'])===1);check($m->inbox($a)['messages']===[]&&$m->inbox($c)['messages']===[]);
check($m->inbox($b)['messages'][0]['body']===$packet['message']);check($m->inbox($b)['unread']===1);
$seq=$m->inbox($b)['next'];check($m->inbox($b,$seq)['messages']===[]);$m->command($b,['id'=>Market::id(),'type'=>'message_read','through'=>$seq],false);check($m->inbox($b)['unread']===0);
refuses(fn()=>$m->command($a,array_replace($packet,['message'=>'Different text']),false),409);
refuses(fn()=>$m->command($a,['id'=>Market::id(),'type'=>'direct_message','to'=>$c['id'],'message'=>'unlinked'],false),422);
refuses(fn()=>$m->command($c,['id'=>Market::id(),'type'=>'direct_message','to'=>$a['id'],'message'=>'unlinked'],false),403);
$m->command($b,['id'=>Market::id(),'type'=>'direct_message','to'=>'Alice','message'=>'Reply by public profile name'],false);
check(count($m->conversations($a,$b['id'])['messages'])===2&&count($m->conversations($b,$a['id'])['messages'])===2);
check($m->conversations($c,$a['id'])['messages']===[]);check(count($m->conversations($a)['conversations'])===1);
// Trade notifications stay private; chat replies append text without accepting
// or changing terms, and use stable idempotency even after revision changes.
$admin=$m->user('Operator','admin');$price=[['kind'=>'gold','code'=>'gld','label'=>'Gold','quality'=>2,'index'=>-1,'ethereal'=>-1,'quantity'=>100]];
$l=$m->listing($admin,['owner'=>$b['id'],'item'=>['seed'=>1,'code'=>'rin','quality'=>7,'index'=>1,'item_hex'=>'abcd'],'price'=>$price],false);
$o=$m->command($a,['id'=>Market::id(),'type'=>'propose','listing_id'=>$l['listing_id'],'listing_revision'=>1,'price'=>$price,'message'=>'Offer from Alice'],false);
$reply=['id'=>Market::id(),'type'=>'trade_reply','offer_id'=>$o['offer_id'],'message'=>'Private chat reply from Bonesy'];$m->command($b,$reply,false);$m->command($b,$reply,false);
check($m->inbox($a)['messages'][1]['kind']==='trade'&&str_contains($m->inbox($a)['messages'][1]['body'],$reply['message']));
refuses(fn()=>$m->command($c,array_replace($reply,['id'=>Market::id()]),false),403);
refuses(fn()=>$m->command($b,array_replace($reply,['id'=>Market::id()]),true),404);
check($m->inbox($c)['messages']===[]&&count($m->conversations($a,$b['id'])['messages'])===2);
check($m->privateConversation($a,false,$o['offer_id'])['status']==='pending');
// The same owner is both sender and recipient; retries and SC/HC refreshes
// must produce one inbox row and one private conversation entry.
$selfPacket=['id'=>Market::id(),'type'=>'direct_message','to'=>$a['id'],'message'=>'Self-test from the website'];
$selfResult=$m->command($a,$selfPacket,false);check($selfResult===$m->command($a,$selfPacket,true));
$selfRows=array_values(array_filter($m->inbox($a)['messages'],fn($r)=>$r['id']===$selfPacket['id']));
check(count($selfRows)===1&&$selfRows[0]['sender']===$a['id']&&$selfRows[0]['recipient']===$a['id']);
check(count($m->conversations($a,$a['id'])['messages'])===1&&$m->conversations($b,$a['id'])['messages'][0]['id']!==$selfPacket['id']);
check($m->conversations($c,$a['id'])['messages']===[]);
$m->command($a,['id'=>Market::id(),'type'=>'direct_message','to'=>'Alice','message'=>'Self-test by public name'],false);
check(count($m->conversations($a,$a['id'])['messages'])===2);
// Reading one visible conversation must not acknowledge another conversation,
// a trade update, a message arriving afterward, or another player's inbox.
$reader=$m->user('Read test');$sender=$m->user('Read sender');$link($reader);$link($sender);
$sendTo=fn($from,$to,$body)=>$m->command($from,['id'=>Market::id(),'type'=>'direct_message','to'=>$to['id'],'message'=>$body],false)['message_id'];
$first=$sendTo($a,$reader,'First conversation');$second=$sendTo($sender,$reader,'Other conversation');
$trade=Market::id();$m->run('INSERT INTO market_messages (id,sender,recipient,scope,kind,thread,body,created) VALUES (?,?,?,?,?,?,?,?)',[$trade,$a['id'],$reader['id'],'tcpip|RMD-MP|2.4|SC','trade',$o['offer_id'],'A trade update',time()]);
check($m->inbox($reader)['unread']===3);
$conversations=$m->conversations($reader)['conversations'];check($conversations[0]['id']===$sender['id']&&$conversations[0]['unread_seq']>0&&$conversations[1]['unread_seq']>0);
$read=['id'=>Market::id(),'type'=>'message_read','messages'=>[$first,$first]];
$ack=$m->command($reader,$read,false);check($ack['unread']===2&&$ack['messages']===[$first]);
check($m->command($reader,$read,true)===$ack&&$m->inbox($reader)['unread']===2);
$conversations=$m->conversations($reader)['conversations'];check($conversations[0]['unread_seq']>0&&$conversations[1]['unread_seq']===0);
$later=$sendTo($a,$reader,'Arrived after reading');check($m->inbox($reader)['unread']===3);
check($m->command($reader,['id'=>Market::id(),'type'=>'message_read','messages'=>[$trade]],false)['unread']===2);
$otherUnread=$m->inbox($a)['unread'];
refuses(fn()=>$m->command($a,['id'=>Market::id(),'type'=>'message_read','messages'=>[$second]],false),422);
check($m->inbox($a)['unread']===$otherUnread&&$m->inbox($reader)['unread']===2);
foreach([[],[$second,str_repeat('f',32)],array_fill(0,201,$second)] as $ids)refuses(fn()=>$m->command($reader,['id'=>Market::id(),'type'=>'message_read','messages'=>$ids],false),422);
check($m->inbox($reader)['unread']===2); // Failed batches roll back every receipt.
$through=$m->inbox($reader)['latest'];$last=$sendTo($a,$reader,'New arrival during mark all');
check($m->command($reader,['id'=>Market::id(),'type'=>'message_read','through'=>$through],false)['unread']===1);
check($m->command($reader,['id'=>Market::id(),'type'=>'message_read','messages'=>[$last]],false)['unread']===0);
foreach($m->conversations($reader)['conversations'] as $conversation)check($conversation['unread_seq']===0);
check(count($m->inbox($reader)['messages'])===5); // Reading never removes native chat delivery.
echo "Players/messages: code-linked public directory, equipment/inventory/mercenary-only snapshots, stash exclusion, mode isolation, escaped-content preservation, private direct/trade threads, name lookup, read cursors, append-only trade replies and idempotency passed.\n";
