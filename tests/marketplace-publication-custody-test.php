<?php
declare(strict_types=1);
require __DIR__.'/../tools/marketplace/server/Market.php';
function check(bool $ok,string $why):void{if(!$ok)throw new RuntimeException($why);}
function refuses(callable $fn,string $why):void{try{$fn();}catch(MarketError $e){check($e->status===409,$why.': '.$e->status.' '.$e->getMessage());return;}throw new RuntimeException($why);}
function bits(string $hex,int $at,int $count,int $value):string{$b=hex2bin($hex);for($n=0;$n<$count;$n++){$i=intdiv($at+$n,8);$mask=1<<(($at+$n)%8);$b[$i]=chr((ord($b[$i])&~$mask)|((($value>>$n)&1)?$mask:0));}return bin2hex($b);}
// Actual native/save encodings that triggered the incident: only stash
// coordinates and IFLAG_ITEM differ, with the same seed and saved properties.
$live='1000800805f815b5dd06d79020b3bee03f';$saved='10008000057816b5dd06d79020b3bee03f';
check(SavedItemIdentity::publicationMatchesSave($live,$saved),'Native flag and position differences rejected');
foreach([[35,3,0],[38,4,9],[42,4,8],[46,4,7],[50,3,5]] as [$at,$n,$v])check(SavedItemIdentity::publicationMatchesSave(bits($live,$at,$n,$v),$saved),'Storage position affected verification');
foreach([4,22,79,111,120] as $bit)refuses(function()use($live,$saved,$bit){if(!SavedItemIdentity::publicationMatchesSave(bits($live,$bit,1,1-((ord(hex2bin($live)[intdiv($bit,8)])>>($bit%8))&1)),$saved))throw new MarketError(409,'Changed content');},'Meaningful item bit accepted');
check(!SavedItemIdentity::publicationMatchesSave(str_repeat('00',17),bits(str_repeat('00',17),27,1,1)),'Missing identity accepted');
$m=new Market(new PDO('sqlite::memory:'));$m->migrate();$seller=$m->user('Fixture seller');$other=$m->user('Fixture other');$catalog=str_repeat('a',64);$agent=$m->authAgent($m->pairAgent($seller['id'],$catalog)['token']);
$price=[['kind'=>'gold','code'=>'gld','label'=>'Gold','quality'=>2,'index'=>-1,'ethereal'=>-1,'quantity'=>123]];
$row=['tab'=>1,'item_hex'=>$live,'code'=>'y33','saved_id'=>1715544494,'quality'=>2,'index'=>-1,'ethereal'=>false,'socketed_items'=>[],'price'=>$price];
$publish=function(string $hex,int $page=20,int $tab=1)use($m,&$seller,$row){$previous=$m->publication($seller,false);return $m->publish($seller,['schema'=>1,'game_mode'=>'2.4','mod'=>'RMD-MP','hardcore'=>false,'custody'=>'local_save','purchasable'=>false,'id'=>Market::id(),'captured_at'=>time(),'expected_revision'=>$previous['revision'],'page'=>$page,'enabled'=>true,'source'=>'live','items'=>[array_replace($row,['item_hex'=>$hex,'tab'=>$tab])]],false);};
$publish($saved);$ledger=$m->run('SELECT id FROM items')->fetchColumn();$publish($live);
check((int)$m->run('SELECT COUNT(*) FROM items')->fetchColumn()===1,'Sorting replaced ledger');
check((int)$m->run('SELECT COUNT(*) FROM published_items')->fetchColumn()===2,'Fixture must retain both historical positions');
$packet=array_replace($row,['page'=>20,'item_hex'=>$saved,'ethereal'=>0,'identified'=>1,'quantity'=>1,'quantity_bit'=>-1]);
$proof=[['page'=>20,'before'=>str_repeat('b',64),'after'=>str_repeat('c',64)]];
$call=fn(array $r)=>$m->custody($agent,$r+['catalog'=>$catalog],false);
// Genuine changes are rejected before any write can be authorized.
$original=$m->run('SELECT item_hex FROM items WHERE id=?',[$ledger])->fetchColumn();
foreach(['owner','contents'] as $fault){
 if($fault==='owner')$m->run('UPDATE items SET owner=? WHERE id=?',[$other['id'],$ledger]);
 else $m->run('UPDATE items SET item_hex=? WHERE id=?',[bits($original,111,7,124),$ledger]);
 $bad=Market::id();refuses(fn()=>$call(['id'=>$bad,'type'=>'deposit_prepare','items'=>[$packet],'pages'=>$proof]),'Unsafe prepare accepted');
 check(!$m->run('SELECT id FROM custody_tickets WHERE id=?',[$bad])->fetchColumn(),'Rejected prepare left a ticket');
 check((int)$m->run('SELECT COUNT(*) FROM identity_reservations')->fetchColumn()===0,'Rejected prepare retained a reservation');
 $m->run('UPDATE items SET owner=?,item_hex=? WHERE id=?',[$seller['id'],$original,$ledger]);
}
$id=Market::id();$call(['id'=>$id,'type'=>'deposit_prepare','items'=>[$packet],'pages'=>$proof]);$call(['id'=>$id,'type'=>'write_authorize','pages'=>$proof]);
// Linking another browser during recovery never replaces the player/agent.
$stamp=$m->browserSessionStamp($seller);$agentBefore=$m->run('SELECT id,token_hash,catalog FROM custody_agents_scoped WHERE owner=?',[$seller['id']])->fetch();
$pc=$m->browserLink(['code'=>$m->browserTicket($seller)['code']]);$phone=$m->browserLink(['code'=>$m->browserTicket($seller)['code']]);
check($pc['id']===$seller['id']&&$phone['id']===$seller['id']&&$m->browserSessionStamp($seller)===$stamp,'Second browser replaced the player or invalidated the first session');
check($m->run('SELECT id,token_hash,catalog FROM custody_agents_scoped WHERE owner=?',[$seller['id']])->fetch()===$agentBefore,'Browser linking replaced the companion pairing');
check($m->run('SELECT status FROM custody_tickets WHERE id=?',[$id])->fetchColumn()==='writing','Browser linking changed pending transfer');
// A real ownership change after authorization must still fail at commit.
$m->run('UPDATE items SET owner=? WHERE id=?',[$other['id'],$ledger]);refuses(fn()=>$call(['id'=>$id,'type'=>'commit','pages'=>$proof]),'Commit ignored ownership');
check($m->run('SELECT status FROM custody_tickets WHERE id=?',[$id])->fetchColumn()==='writing','Failed commit discarded recovery');
$m->run('UPDATE items SET owner=? WHERE id=?',[$seller['id'],$ledger]);
$call(['id'=>$id,'type'=>'commit','pages'=>$proof]);$call(['id'=>$id,'type'=>'commit','pages'=>$proof]);
check((int)$m->run('SELECT COUNT(*) FROM items')->fetchColumn()===1,'Deposit changed ledger');
check($m->run('SELECT item_hex FROM items WHERE id=?',[$ledger])->fetchColumn()===$saved,'Vault did not keep authoritative saved bytes');
check((int)$m->run('SELECT COUNT(*) FROM published_items')->fetchColumn()===0,'Stale positions survived');
check((int)$m->run("SELECT COUNT(*) FROM events WHERE kind='escrow_deposit'")->fetchColumn()===1,'Retry repeated deposit');
check($m->run('SELECT item FROM escrow_items')->fetchColumn()===$ledger,'History disconnected from deposit');
// A different page and shared tab still resolve the one physical item.
$seller2=$m->user('Moved-page seller');$agent2=$m->authAgent($m->pairAgent($seller2['id'],$catalog)['token']);$seller=$seller2;
$unique=bits($saved,79,32,987654321);$publish($unique,11,1);$ledger2=$m->run('SELECT id FROM items WHERE owner=?',[$seller2['id']])->fetchColumn();
$moved=array_replace($packet,['page'=>35,'tab'=>7,'item_hex'=>bits($unique,42,4,4)]);$pages=[['page'=>35,'before'=>str_repeat('d',64),'after'=>str_repeat('e',64)]];$id2=Market::id();
foreach(['deposit_prepare','write_authorize','commit'] as $type)$m->custody($agent2,['id'=>$id2,'type'=>$type,'catalog'=>$catalog,'items'=>[$moved],'pages'=>$pages],false);
check($m->run('SELECT item FROM escrow_items WHERE owner=?',[$seller2['id']])->fetchColumn()===$ledger2,'Page/tab movement broke ledger continuity');
$seller=$m->user('Sorting withdrawal seller');$physical=bits($saved,79,32,246813579);$publish($physical,11,1);$publish(bits($physical,42,4,6),11,4);
$physicalLedger=$m->run('SELECT id FROM items WHERE owner=?',[$seller['id']])->fetchColumn();$sorted=bits($physical,46,4,8);
$intent=Market::id();$scope='tcpip|RMD-MP|2.4|SC';$m->run('INSERT INTO web_intents(id,owner,scope,kind,packet,status,message,created) VALUES (?,?,?,?,?,?,?,?)',[$intent,$seller['id'],$scope,'price',json_encode(['item_hex'=>$physical]),'queued','',time()]);
refuses(fn()=>$m->command($seller,['id'=>Market::id(),'type'=>'withdraw_physical','item_id'=>$physicalLedger,'item_hex'=>bits($sorted,111,7,123)],false),'Withdrawal ignored changed properties');
$w=$m->command($seller,['id'=>Market::id(),'type'=>'withdraw_physical','page'=>35,'tab'=>7,'item_hex'=>$sorted],false);
check($w['item_id']===$physicalLedger&&$w['withdrawn'],'Sorting made physical withdrawal lose its item');
check($m->run('SELECT status FROM web_intents WHERE id=?',[$intent])->fetchColumn()==='failed','Moved item retained an old queued price');
echo "Publication custody: native/save flag and all position fields, stable ledger across pages/tabs, authoritative saved bytes, historical position cleanup, pre-write ownership/property rejection, protected commit and exactly-once retry passed.\n";
