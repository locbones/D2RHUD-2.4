<?php
declare(strict_types=1);
require __DIR__.'/../tools/marketplace/server/Market.php';
function check(bool $ok):void{if(!$ok)throw new RuntimeException('Sale average assertion failed: '.json_encode(debug_backtrace(DEBUG_BACKTRACE_IGNORE_ARGS,1)));}
$m=new Market(new PDO('sqlite::memory:'));$m->migrate();$item=Market::id();
$m->run('INSERT INTO items VALUES (?,?,?,?,?,?,?,?,?,?)',[$item,$m->scope(false),1,'rin',7,42,0,0,'seller','00000100'.str_repeat('00',32)]);
$gold=fn($n)=>['kind'=>'gold','code'=>'gld','quality'=>2,'index'=>-1,'ethereal'=>-1,'quantity'=>$n,'label'=>'Gold'];
$rune=fn($n)=>['code'=>'r33','quality'=>2,'index'=>-1,'ethereal'=>-1,'quantity'=>$n,'label'=>'Zod Rune'];
$add=function($price,$at=null,$kind='completed_sale',$id=null)use($m,$item){$m->run('INSERT INTO events (id,item,at,actor,kind,message,price,seller,buyer) VALUES (?,?,?,?,?,?,?,?,?)',[Market::id(),$id??$item,$at??time(),'seller',$kind,'',json_encode($price),'seller','buyer']);};
foreach([[$gold(100)],[$gold(300)],[$rune(2)],[$rune(4)],[$gold(100),$rune(2)],[$rune(4),$gold(300)],Market::freePrice()] as $price)$add($price);
$add([$gold(9999)],time()-31*86400);$add([$gold(9999)],time()+86400);$add([$gold(9999)],null,'proposed');
foreach([$m->scope(true),'other|RMD-MP|2.4|SC','tcpip|Other|2.4|SC'] as $scope){$other=Market::id();$m->run('INSERT INTO items VALUES (?,?,?,?,?,?,?,?,?,?)',[$other,$scope,2,'rin',7,42,0,0,'seller','00000100'.str_repeat('00',32)]);$add([$gold(9999)],null,'completed_sale',$other);}
$query=['code'=>'rin','quality'=>7,'item_index'=>42,'ethereal'=>0,'sockets'=>0,'similar'=>true,'averages'=>true];
$result=$m->historyPage(false,$query);$averages=$result['averages'];check($averages['sales']===7&&$averages['free_sales']===1&&count($averages['groups'])===3&&!$averages['truncated']);
foreach($averages['groups'] as $group){check($group['sales']===2);foreach($group['price'] as $p)check($p['quantity']===($p['code']==='gld'?200:3));foreach($group['median_price'] as $p)check($p['quantity']===($p['code']==='gld'?200:3));foreach($group['low_price'] as $p)check($p['quantity']===($p['code']==='gld'?100:2));foreach($group['high_price'] as $p)check($p['quantity']===($p['code']==='gld'?300:4));}
$tail=$m->historyPage(false,$query+['before'=>1]);check(!$tail['rows']&&$tail['averages']===$averages);check(!array_key_exists('averages',$m->historyPage(false,['item_id'=>$item])));
check($m->historyPage(false,['item_id'=>Market::id(),'averages'=>true])['averages']['sales']===0);
$m->transaction(function()use($add,$gold){for($n=0;$n<5001;++$n)$add([$gold(5)]);});$sample=$m->historyPage(false,$query)['averages'];check($sample['truncated']&&$sample['sales']===5000&&$sample['sample_limit']===5000&&$sample['groups'][0]['price'][0]['quantity']===5);
echo "Averages: completed sales only, 30-day/mode/mod/community isolation, exact currency and mixed-bundle grouping independent of line order, separate free transfers, pagination-independent counts and explicit 5,000-sale sampling passed.\n";
