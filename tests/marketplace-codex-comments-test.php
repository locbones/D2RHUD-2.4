<?php
declare(strict_types=1);
require __DIR__.'/../tools/marketplace/server/Market.php';
function check(bool $v,string $why):void{if(!$v)throw new RuntimeException($why);}
function refuses(callable $f,int $status):void{try{$f();}catch(MarketError $e){check($e->status===$status,'Wrong refusal status');return;}throw new RuntimeException('Invalid action accepted');}
$m=new Market(new PDO('sqlite::memory:'));$m->migrate();$m->migrate();
$seller=$m->user('Codex seller');$buyer=$m->user('Codex buyer');$catalog=str_repeat('a',64);
foreach([$seller,$buyer] as $a)$m->browserLink(['code'=>$m->browserTicket($a)['code']]);
$sa=$m->authAgent($m->pairAgent($seller['id'],$catalog)['token']);$ba=$m->authAgent($m->pairAgent($buyer['id'],$catalog)['token']);
$ref=json_decode(file_get_contents(__DIR__.'/../tools/marketplace/assets/rmd-codex.json'),true);$web=$m->browserCatalog();
check(count($ref['items'])===56,'Complete Codex reference');
foreach($ref['items'] as $r){$rows=$r['quality']===2?[$web['bases'][$r['code']]]:array_filter($web['unique'],fn($d)=>$d['code']===$r['code']);check(count($rows)===1&&array_values($rows)[0]['name']===$r['name'],'Catalog and native reference agree');}
$deposit=function($agent,$items)use($m,$catalog){$id=Market::id();$pages=[['page'=>11,'before'=>str_repeat('b',64),'after'=>str_repeat('c',64)]];foreach(['deposit_prepare','write_authorize','commit'] as $type)$m->custody($agent,['id'=>$id,'type'=>$type,'catalog'=>$catalog,'items'=>$items,'balances'=>[],'pages'=>$pages],false);};
$item=fn($code,$hex)=>['code'=>$code,'quality'=>2,'index'=>-1,'page'=>11,'tab'=>1,'saved_id'=>0,'ethereal'=>0,'identified'=>1,'quantity'=>1,'quantity_bit'=>-1,'item_hex'=>str_repeat($hex,24),'display'=>['level'=>80,'saved_properties'=>[]]];
$wanted=['code'=>'a12','label'=>$web['bases']['a12']['name'],'quality'=>2,'index'=>-1,'quantity'=>1,'ethereal'=>-1];
$sale=$item('rin','ab');$sale['price']=[$wanted];$sale['comment']='Seeking Testament 1:3. <img src=x onerror=alert(1)>';
$deposit($sa,[$sale]);$deposit($ba,[$item('a11','cd')]);
$listing=$m->browserSnapshot($buyer,false)['listings'][0];check($listing['comment']===$sale['comment']&&$listing['price'][0]['code']==='a12','Published exact identity and comment');
check($m->snapshot($buyer,false)['listings'][0]['comment']===$sale['comment'],'Native feed comment');
$own=$m->browserSnapshot($seller,false)['escrow']['items'][0];check($own['comment']===$sale['comment'],'Seller can edit existing comment');
refuses(fn()=>$m->command($buyer,['id'=>Market::id(),'type'=>'escrow_price','item_id'=>$own['item_id'],'price'=>[$wanted],'comment'=>'Spoofed seller comment'],false),409);
$offer=$m->command($buyer,['id'=>Market::id(),'type'=>'propose','listing_id'=>$listing['id'],'listing_revision'=>1,'price'=>[$wanted]],false);
$accepted=$m->command($seller,['id'=>Market::id(),'type'=>'accept','offer_id'=>$offer['offer_id'],'expected_revision'=>1],false);
check($accepted['status']==='accepted','A different Testament must not fund the request');
$deposit($ba,[$item('a12','ef')]);check($m->run('SELECT status FROM offers WHERE id=?',[$offer['offer_id']])->fetchColumn()==='completed','Exact requested Testament settles');
check($m->browserSnapshot($buyer,false)['escrow']['items'][0]['comment']==='','New owner must not inherit seller comment');
$more=$item('rin','12');$deposit($sa,[$more]);$id=$m->run('SELECT id FROM items WHERE item_hex=?',[$more['item_hex']])->fetchColumn();
$set=function($comment)use($m,$seller,$id,$wanted){return $m->command($seller,['id'=>Market::id(),'type'=>'escrow_price','item_id'=>$id,'price'=>[$wanted],'comment'=>$comment],false);};
$set('  Friendly note  ');$snap=$m->browserSnapshot(null,false);check($snap['listings'][0]['comment']==='Friendly note','Whitespace normalized');
$m->command($seller,['id'=>Market::id(),'type'=>'escrow_price','item_id'=>$id,'price'=>[$wanted]],false);check($m->browserSnapshot(null,false)['listings'][0]['comment']==='Friendly note','Older client preserves comment');
refuses(fn()=>$set(str_repeat('x',241)),422);refuses(fn()=>$set("bad\0note"),422);refuses(fn()=>$set(['invalid']),422);
$set(str_repeat('é',240));check($m->browserSnapshot(null,false)['listings'][0]['comment']===str_repeat('é',240),'Unicode character limit');
$set('');check($m->browserSnapshot(null,false)['listings'][0]['comment']==='','Seller can clear comment');
$local=$item('a12','34');$local['comment']='Saved draft note';$settings=['page'=>11,'withdraw_page'=>11,'enabled'=>true,'auto_trade'=>false];
$m->custody($sa,['type'=>'web_sync','catalog'=>$catalog,'settings'=>$settings,'inventory'=>[$local],'features'=>['listing_comments']],false);
$snapshot=$m->browserSnapshot($seller,false);$row=$snapshot['local_items'][0];check($row['comment']==='Saved draft note','Local draft comment round trip');
$m->command($seller,['id'=>Market::id(),'type'=>'web_price','key'=>$row['key'],'price'=>[$wanted],'comment'=>'Website draft note'],false);
check($m->browserSnapshot($seller,false)['local_items'][0]['comment']==='Website draft note','Queued website comment displayed');
$post=$m->command($seller,['id'=>Market::id(),'type'=>'wanted_create','wanted'=>$wanted,'contact'=>true],false);
check($m->wantedListings(false,$post['wanted_id'])['wanted']['code']==='a12','Wanted request retains exact Codex code');
echo "Codex/comments passed: all 56 labels, exact-piece payment and settlement, WTB identity, public/native comments, ownership, Unicode limits, legacy clients, clearing and queued stash notes.\n";
