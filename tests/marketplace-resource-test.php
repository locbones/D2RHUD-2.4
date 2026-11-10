<?php
declare(strict_types=1);
require __DIR__.'/../tools/marketplace/server/Market.php';
class BudgetPDO extends PDO {
    public array $queries=[];public int $transactions=0;
    public function prepare(string $sql,array $options=[]):PDOStatement|false{$this->queries[]=$sql;return parent::prepare($sql,$options);}
    public function beginTransaction():bool{++$this->transactions;return parent::beginTransaction();}
}
function check(bool $ok,string $why):void{if(!$ok)throw new RuntimeException($why);}
$db=new BudgetPDO('sqlite::memory:');$m=new Market($db);$m->migrate();$seller=$m->user('Resource fixture');
$catalog=str_repeat('a',64);$agent=$m->authAgent($m->pairAgent($seller['id'],$catalog)['token']);
$packet=['id'=>Market::id(),'type'=>'delivery_queue','catalog'=>$catalog];
$db->transactions=0;$db->queries=[];
$queue=$m->controlledRequest($agent,'custody',$packet,false,fn()=>$m->custody($agent,$packet,false));
check($db->transactions===0,'Idle queue took the global transaction gate');
check(isset($queue['delivery_items'],$queue['wallet_withdrawals'],$queue['funding_requests']),'Incomplete queue');
check(!array_filter($db->queries,fn($q)=>str_contains($q,'custody_serial')||str_contains($q,'market_control')),'Queue touched global market gate');
try{$m->custody($agent,array_replace($packet,['catalog'=>str_repeat('b',64)]),false);throw new RuntimeException('Bad catalog accepted');}catch(MarketError $e){check($e->status===409,'Catalog guard');}
$settings=['page'=>11,'withdraw_page'=>11,'enabled'=>true,'auto_trade'=>false];
$sync=$m->custody($agent,['type'=>'web_sync','catalog'=>$catalog,'settings'=>$settings,'inventory'=>[],'acknowledged'=>[]],false);
check(isset($sync['funding_requests']),'web_sync cannot reuse queue');
$light=['type'=>'web_poll','catalog'=>$catalog,'settings'=>$settings,'inventory_hash'=>$sync['inventory_hash'],'acknowledged'=>[]];
$poll=$m->custody($agent,$light,false);check($poll['inventory_hash']===$sync['inventory_hash'],'Unchanged poll changed inventory');
$stale=$m->custody($agent,array_replace($light,['inventory_hash'=>str_repeat('f',64)]),false);
check(($stale['inventory_required']??false)===true,'Stale inventory hash accepted');
for($n=0;$n<80;++$n)$m->listing(['id'=>'operator','role'=>'admin'],['owner'=>$seller['id'],'item'=>['seed'=>1000+$n,'code'=>'rin','quality'=>2,'index'=>-1,'ethereal'=>0,'sockets'=>0,'item_hex'=>str_pad(dechex($n+256),48,'0',STR_PAD_LEFT)],'price'=>[['kind'=>'gold','code'=>'gld','label'=>'Gold','quality'=>2,'index'=>-1,'ethereal'=>-1,'quantity'=>1]]],false);
$db->queries=[];$native=$m->snapshot(['id'=>'','name'=>'Guest'],false);
check(count($native['listings'])===80,'Feed lost items');
check(!array_filter($db->queries,fn($q)=>str_contains($q,'SELECT listing FROM market_bundles WHERE listing=?')),'Per-listing bundle queries returned');
check(count(array_filter($db->queries,fn($q)=>str_starts_with($q,'SELECT b.listing FROM market_bundles')))===1,'Expected one bulk bundle query');
$m->run("UPDATE listings SET status='withdrawn' WHERE item IN (SELECT id FROM items WHERE seed<1075)");
$db->queries=[];$web=$m->browserSnapshot(null,false);check(count($web['listings'])===5,'Browser feed changed');
$displayQueries=array_filter($db->queries,fn($q)=>str_starts_with($q,'SELECT i.id,i.code,i.quality,i.item_index,i.seed'));
check(count($displayQueries)===1&&str_contains(array_values($displayQueries)[0],'i.id IN ('),'Browser parsed every historical item');
$m->run("UPDATE actors SET role='retired' WHERE id=?",[$seller['id']]);
try{$m->controlledRequest($agent,'custody',$packet,false,fn()=>$m->custody($agent,$packet,false));throw new RuntimeException('Retired agent accepted');}catch(MarketError $e){check($e->status===401,'Retired guard');}
echo "PASS resource budget: unlocked authenticated queue hints, catalog/retired guards, combined inventory/funding response, one bundle query for 80 listings, five visible item displays instead of 80 historical records.\n";
