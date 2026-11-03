<?php
declare(strict_types=1);
require __DIR__.'/../tools/marketplace/server/Market.php';
function check(bool $v):void{if(!$v)throw new RuntimeException('Identity assertion failed at '.json_encode(debug_backtrace(DEBUG_BACKTRACE_IGNORE_ARGS,1)));}
function refuses(callable $f):void{try{$f();}catch(MarketError $e){check($e->status===409);return;}throw new RuntimeException('Duplicate identity was accepted.');}
function gear(int $seed,string $uuid='',int $x=0,string $code='rin '):string{
    $catalog=json_decode(file_get_contents(__DIR__.'/../tools/marketplace/server/item-identity.json'),true);$bits=[];
    $write=function(int $v,int $n)use(&$bits){for($i=0;$i<$n;$i++)$bits[]=($v>>$i)&1;};
    $write(1<<4,32);$write(0,3);$write(0,3);$write(0,4);$write($x,4);$write(0,4);$write(5,3);
    for($i=0;$i<4;$i++)foreach(str_split($catalog['codes'][ord($code[$i])]) as $b)$bits[]=(int)$b;
    $write(0,3);$write($seed,32);$write(88,7);$write(7,4);$write(0,1);$write(0,1);$write(42,12);$write($uuid!==''?1:0,1);
    if($uuid!=='')for($n=0;$n<16;$n++)$write(hexdec(substr($uuid,2*$n,2)),8);
    $write(511,9);while(count($bits)%8)$bits[]=0;$bytes='';for($i=0;$i<count($bits);$i+=8){$v=0;for($n=0;$n<8;$n++)$v|=$bits[$i+$n]<<$n;$bytes.=chr($v);}return bin2hex($bytes);
}
$uuid='00112233445566778899aabbccddeeff';$hex=gear(0,$uuid);$legacy=gear(0xf1234567);
check(SavedItemIdentity::read($hex)['item_uuid']===$uuid&&SavedItemIdentity::read($hex)['saved_id']===0);
check(SavedItemIdentity::read($legacy)['saved_id']===0xf1234567&&SavedItemIdentity::read($legacy)['item_uuid']==='');
check(SavedItemIdentity::key($hex)===SavedItemIdentity::key(gear(0,$uuid,8)));
check(SavedItemIdentity::key($legacy)===SavedItemIdentity::key(gear(0xf1234567,'',9)));
$m=new Market(new PDO('sqlite::memory:'));$m->migrate();$s=$m->user('Seller');$b=$m->user('Buyer');$c=$m->user('Other account');$cat=str_repeat('a',64);
$sa=$m->authAgent($m->pairAgent($s['id'],$cat)['token']);$ba=$m->authAgent($m->pairAgent($b['id'],$cat)['token']);$ca=$m->authAgent($m->pairAgent($c['id'],$cat)['token']);
$gold=[['kind'=>'gold','code'=>'gld','label'=>'Gold','quality'=>2,'index'=>-1,'ethereal'=>-1,'quantity'=>1000]];
$page=[['page'=>11,'before'=>str_repeat('b',64),'after'=>str_repeat('c',64)],['page'=>12,'before'=>str_repeat('d',64),'after'=>str_repeat('e',64)]];
$item=['page'=>11,'tab'=>1,'saved_id'=>0,'quality'=>7,'index'=>42,'code'=>'rin','ethereal'=>0,'identified'=>1,'quantity'=>1,'quantity_bit'=>-1,'item_hex'=>$hex,'price'=>$gold];
$call=fn($a,$r)=>$m->custody($a,$r+['catalog'=>$cat],false);
$prepare=fn($a,$items)=>$call($a,['id'=>Market::id(),'type'=>'deposit_prepare','pages'=>$page,'items'=>$items]);
$commit=function($a,$r)use($call,$page){$call($a,['id'=>$r['id'],'type'=>'write_authorize','pages'=>$page]);$call($a,['id'=>$r['id'],'type'=>'commit','pages'=>$page]);};
$moved=array_replace($item,['page'=>12,'tab'=>7,'item_hex'=>gear(0,$uuid,8)]);
$r=$prepare($sa,[$item]);refuses(fn()=>$prepare($ca,[$moved]));$call($sa,['id'=>$r['id'],'type'=>'cancel']);
$r=$prepare($sa,[$item]);$commit($sa,$r);refuses(fn()=>$prepare($sa,[$moved]));refuses(fn()=>$prepare($ca,[$moved]));
check((int)$m->run('SELECT COUNT(*) FROM custody_tickets WHERE status IN (\'prepared\',\'writing\')')->fetchColumn()===0);
$listing=$m->snapshot($b,false)['listings'][0];
$offer=$m->command($b,['id'=>Market::id(),'type'=>'propose','listing_id'=>$listing['id'],'listing_revision'=>1,'price'=>$gold],false);
$m->command($s,['id'=>Market::id(),'type'=>'accept','offer_id'=>$offer['offer_id'],'expected_revision'=>1],false);
$r=$call($ba,['id'=>Market::id(),'type'=>'deposit_prepare','pages'=>$page,'items'=>[],'balances'=>$gold]);$commit($ba,$r);
refuses(fn()=>$prepare($sa,[$moved]));refuses(fn()=>$prepare($ca,[$moved]));
$pub=['schema'=>1,'game_mode'=>'2.4','mod'=>'RMD-MP','hardcore'=>false,'custody'=>'local_save','purchasable'=>false,'id'=>Market::id(),'captured_at'=>time(),'expected_revision'=>0,'page'=>12,'enabled'=>true,'source'=>'saved','items'=>[array_replace($moved,['ethereal'=>false,'socketed_items'=>[]])]];
check($m->publish($s,$pub,false)['blocked_identities']===1);
check($m->snapshot($s,false)['listings']===[]);
$claim=$call($ba,['id'=>Market::id(),'type'=>'claim_prepare']);$delivered=[['item'=>$listing['item_id'],'item_hex'=>$moved['item_hex']]];
$call($ba,['id'=>$claim['id'],'type'=>'write_authorize','pages'=>$page,'delivered'=>$delivered]);$call($ba,['id'=>$claim['id'],'type'=>'commit','pages'=>$page]);
$resale=$moved;$resale['ledger_id']=$listing['item_id'];$r=$prepare($ba,[$resale]);$commit($ba,$r);
check((int)$m->run('SELECT COUNT(*) FROM items')->fetchColumn()===1);
check($m->historyPage(false,['code'=>'rin','item_uuid'=>$uuid])['rows'][0]['item']===$listing['item_id']);
// Zero legacy seed does not conflate distinct UUIDs; a falsified metadata
// seed cannot bypass the identity extracted from exact serialized bytes.
$other=$item;$other['item_hex']=gear(0,'ffeeddccbbaa99887766554433221100');$r=$prepare($sa,[$other]);$commit($sa,$r);
$spoof=$moved;$spoof['saved_id']=123456;$spoof['item_uuid']='ffeeddccbbaa99887766554433221100';refuses(fn()=>$prepare($sa,[$spoof]));
$pendingActor=$m->user('Unsaved client item');$pending=$pub;$pending['id']=Market::id();$pending['expected_revision']=0;$pending['items']=[array_replace($item,['item_hex'=>gear(0),'ethereal'=>false,'socketed_items'=>[]])];
check($m->publish($pendingActor,$pending,false)['unverified_identities']===1);
check(array_filter($m->snapshot($s,false)['listings'],fn($l)=>$l['seller_id']===$pendingActor['id'])===[]);
$old=$item;$old['item_hex']=$legacy;$r=$prepare($sa,[$old]);$commit($sa,$r);$old['item_hex']=gear(0xf1234567,'',4);refuses(fn()=>$prepare($ca,[$old]));
// Metadata repair/backfill does not change ledger IDs or ownership.
$m->run('UPDATE items SET seed=0 WHERE item_hex=?',[$legacy]);$m->migrate();check((int)$m->run('SELECT seed FROM items WHERE item_hex=?',[$legacy])->fetchColumn()===0xf1234567);
check($m->run('SELECT owner FROM items WHERE id=?',[$listing['item_id']])->fetchColumn()===$b['id']);
// Historical operator-recorded sales must also block the former owner's copy,
// even when that old sale predates the escrow implementation.
$admin=$m->user('Operator','admin');$oldHex=gear(123789);
$import=$m->listing($admin,['owner'=>$s['id'],'item'=>['seed'=>0,'code'=>'rin','quality'=>7,'index'=>42,'item_hex'=>$oldHex],'price'=>$gold],false);
$m->sale($admin,['item_id'=>$import['item_id'],'seller'=>$s['id'],'buyer'=>$b['id'],'price'=>$gold,'receipt'=>'legacy-sale'],false);
$restored=array_replace($item,['item_hex'=>$oldHex]);refuses(fn()=>$prepare($sa,[$restored]));
// Both UUID and legacy payment stacks split into distinct persistent
// identities, conserving units and leaving all saved properties intact.
foreach(['','112233445566778899aabbccddeeff001'] as $case=>$stackUuid){
    $catalog=json_decode(file_get_contents(__DIR__.'/../tools/marketplace/server/item-identity.json'),true);$bits=[];
    $write=function(int $v,int $n)use(&$bits){for($i=0;$i<$n;$i++)$bits[]=($v>>$i)&1;};
    $write(1<<4,32);$write(0,3);$write(0,3);$write(0,4);$write(0,4);$write(0,4);$write(5,3);
    foreach(str_split('Ev11') as $char)foreach(str_split($catalog['codes'][ord($char)]) as $bit)$bits[]=(int)$bit;
    $write(0,3);$write(700+$case,32);$write(50,7);$write(2,4);$write(0,1);$write(0,1);$write($stackUuid!==''?1:0,1);
    if($stackUuid!=='')for($n=0;$n<16;$n++)$write(hexdec(substr($stackUuid,2*$n,2)),8);
    $quantityBit=count($bits);$write(100,9);$write(511,9);while(count($bits)%8)$bits[]=0;$bytes='';for($i=0;$i<count($bits);$i+=8){$v=0;for($n=0;$n<8;$n++)$v|=$bits[$i+$n]<<$n;$bytes.=chr($v);}
    $gs=$m->user('Stack seller '.$case);$gb=$m->user('Stack buyer '.$case);$gsa=$m->authAgent($m->pairAgent($gs['id'],$cat)['token']);$gba=$m->authAgent($m->pairAgent($gb['id'],$cat)['token']);
    $stack=array_replace($item,['code'=>'Ev11','quality'=>2,'index'=>-1,'item_hex'=>bin2hex($bytes),'quantity'=>100,'quantity_bit'=>$quantityBit]);unset($stack['price']);$r=$prepare($gba,[$stack]);$commit($gba,$r);
    $ask=[['code'=>'Ev11','label'=>'Candy Corn','quality'=>2,'index'=>-1,'ethereal'=>-1,'quantity'=>40]];
    $goods=array_replace($item,['item_hex'=>gear(900+$case),'price'=>$ask]);$r=$prepare($gsa,[$goods]);$commit($gsa,$r);
    $l=array_values(array_filter($m->snapshot($gb,false)['listings'],fn($l)=>$l['seller_id']===$gs['id']))[0];
    $o=$m->command($gb,['id'=>Market::id(),'type'=>'propose','listing_id'=>$l['id'],'listing_revision'=>1,'price'=>$ask],false);
    $m->command($gs,['id'=>Market::id(),'type'=>'accept','offer_id'=>$o['offer_id'],'expected_revision'=>1],false);
    $parts=$m->run('SELECT metadata FROM escrow_items WHERE owner IN (?,?)',[$gs['id'],$gb['id']])->fetchAll(PDO::FETCH_COLUMN);$quantities=[];$keys=[];
    foreach($parts as $part){$p=json_decode($part,true);if($p['code']!=='Ev11')continue;$quantities[]=$p['quantity'];$keys[]=SavedItemIdentity::key($p['item_hex']);check(SavedItemIdentity::read($p['item_hex'])['saved_id']===$p['saved_id']);}
    sort($quantities);check($quantities===[40,60]&&count(array_unique($keys))===2);
}
echo "Identity: raw UUID/legacy seed parsing, moved-page/slot replay rejection before writes, cross-account reservations, cancellation, sold-copy hosting refusal, buyer resale/history continuity, metadata spoof resistance and additive backfill passed.\n";
