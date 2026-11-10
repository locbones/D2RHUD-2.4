<?php
declare(strict_types=1);
require __DIR__.'/../tools/marketplace/server/Market.php';
function check(bool $ok,string $why):void{if(!$ok)throw new RuntimeException($why);}
$m=new Market(new PDO('sqlite::memory:'));$m->migrate();
$seller=$m->user('Vault search seller');$other=$m->user('Other player');
foreach([$seller,$other] as $a)$m->browserLink(['code'=>$m->browserTicket($a)['code']]);
$catalog=str_repeat('a',64);$agent=$m->authAgent($m->pairAgent($seller['id'],$catalog)['token']);
$items=[];
for($n=0;$n<180;++$n)$items[]=['code'=>'rin','quality'=>2,'index'=>-1,'page'=>11,'tab'=>1,'saved_id'=>90000+$n,'ethereal'=>0,'identified'=>1,'quantity'=>1,'quantity_bit'=>-1,'item_hex'=>substr(hash('sha256','vault-search-'.$n),0,48),'display'=>['level'=>80,'saved_properties'=>[]]];
$pages=[['page'=>11,'before'=>str_repeat('b',64),'after'=>str_repeat('c',64)]];
foreach(array_chunk($items,64) as $batch){$id=Market::id();foreach(['deposit_prepare','write_authorize','commit'] as $type)$m->custody($agent,['id'=>$id,'type'=>$type,'catalog'=>$catalog,'items'=>$batch,'balances'=>[],'pages'=>$pages],false);}
$all=$m->run('SELECT id FROM items ORDER BY id')->fetchAll(PDO::FETCH_COLUMN);
$definition=array_filter($m->browserCatalog()['unique'],fn($d)=>stripos($d['name'],'stone of jordan')!==false);
$index=array_key_first($definition);check($index!==null,'Catalog fixture missing');
// Keep every named target beyond the initial 48 items, including the next SQL batch.
$targets=array_slice($all,120,60);
foreach($targets as $item)$m->run('UPDATE items SET quality=7,item_index=? WHERE id=?',[(int)$index,$item]);
$p=$m->publicEscrow($seller['id'],false);check(count($p['items'])===48&&$p['next_after']!==null,'Initial page');
foreach(['name','name-desc','quality','quality-asc'] as $sort){
    $seen=[];$after='';do{$p=$m->publicEscrow($seller['id'],false,$after,'',$sort);$seen=array_merge($seen,$p['items']);$after=$p['next_after'];}while($after!==null);
    check(count($seen)===180&&count(array_unique(array_column($seen,'item_id')))===180,'Sorted pagination: '.$sort);
    check((int)$seen[0]['item']['quality']===(in_array($sort,['name-desc','quality'],true)?7:2),'Sort across unseen pages: '.$sort);
    check(!str_contains(json_encode($seen),'_sort_name'),'Internal sort metadata stays private');
}
try{$m->publicEscrow($seller['id'],false,'','','invalid');throw new RuntimeException('Invalid sort accepted');}catch(MarketError $e){check($e->status===422,'Invalid sort status');}
foreach(['stone','STONE','jordan stone ring','the stone-of-jordan rings'] as $query){
    $p=$m->publicEscrow($seller['id'],false,'',$query);check(count($p['items'])===48,'Search must cross unseen pages: '.$query);
    $q=$m->publicEscrow($seller['id'],false,$p['next_after'],$query);check(count($q['items'])===12&&$q['next_after']===null,'Filtered pagination: '.$query);
    check(array_merge(array_column($p['items'],'item_id'),array_column($q['items'],'item_id'))===$targets,'No repeated or skipped matches');
}
check($m->publicEscrow($seller['id'],false,'','not-a-real-item')['items']===[],'No-match search');
check($m->publicEscrow($seller['id'],true,'','stone')['items']===[],'Hardcore isolation');
check($m->publicEscrow($other['id'],false,'','stone')['items']===[],'Player isolation');
$seen=[];$after='';do{$p=$m->publicEscrow($seller['id'],false,$after,'ring');$seen=array_merge($seen,array_column($p['items'],'item_id'));$after=$p['next_after'];}while($after!==null);
check($seen===$all,'Clearing or changing search must include the whole Vault');
// Reserved deliveries must not fill pages or hide available items behind them.
foreach(array_slice($all,0,130) as $item){$meta=json_decode($m->run('SELECT metadata FROM escrow_items WHERE item=?',[$item])->fetchColumn(),true);$meta['delivery_requested']=true;$m->run('UPDATE escrow_items SET metadata=? WHERE item=?',[json_encode($meta),$item]);}
$p=$m->publicEscrow($seller['id'],false);$q=$m->publicEscrow($seller['id'],false,$p['next_after']);
check(count($p['items'])===48&&count($q['items'])===2&&$q['next_after']===null,'Delivery reservations must not break pagination');
$item=$all[179];$meta=json_decode($m->run('SELECT metadata FROM escrow_items WHERE item=?',[$item])->fetchColumn(),true);$meta['identified']=false;$m->run('UPDATE escrow_items SET metadata=? WHERE item=?',[json_encode($meta),$item]);
$p=$m->publicEscrow($seller['id'],false,'','stone');$q=$m->publicEscrow($seller['id'],false,$p['next_after'],'stone');
check(count($p['items'])+count($q['items'])===49,'Search must not expose unidentified unique names');
check(!str_contains(json_encode($p),'item_hex')&&!str_contains(json_encode($p),'token'),'Public response must not include private bytes or credentials');
try{$m->publicEscrow($seller['id'],false,'',str_repeat('x',129));throw new RuntimeException('Oversized query accepted');}catch(MarketError $e){check($e->status===422,'Invalid query status');}
echo "Vault search passed: unseen pages, flexible names, filtered pagination, player/mode isolation, reserved deliveries, unidentified items and safe public metadata.\n";
