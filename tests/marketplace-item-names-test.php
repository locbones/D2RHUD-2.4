<?php
declare(strict_types=1);
require __DIR__.'/../tools/marketplace/server/Market.php';
function check(bool $ok):void{if(!$ok)throw new RuntimeException('Item name assertion failed at '.json_encode(debug_backtrace(DEBUG_BACKTRACE_IGNORE_ARGS,1)));}
function namedItem(int $quality,array $names,bool $identified=true,bool $uuid=false):string{
    $catalog=json_decode(file_get_contents(__DIR__.'/../tools/marketplace/server/item-identity.json'),true);$bits=[];
    $write=function(int $v,int $n)use(&$bits){for($i=0;$i<$n;$i++)$bits[]=($v>>$i)&1;};
    $write(($identified?1<<4:0),32);$write(0,3);$write(0,3);$write(0,4);$write(0,4);$write(0,4);$write(5,3);
    foreach(str_split('rin ') as $char)foreach(str_split($catalog['codes'][ord($char)]) as $b)$bits[]=(int)$b;
    $write(0,3);$write(1234567,32);$write(88,7);$write($quality,4);
    $write(1,1);$write(2,3);$write(1,1);$write(37,11); // Optional picture and class data.
    if($quality===4){$write($names[0],11);$write($names[1],11);}
    elseif($quality===5||$quality===7)$write($names[0],12);
    else{$write($names[0],8);$write($names[1],8);if($quality!==9)for($n=0;$n<3;$n++){$write(1,1);$write(500+$n,11);$write(1,1);$write(100+$n,11);}}
    $write($uuid?1:0,1);if($uuid)for($n=0;$n<16;$n++)$write($n+1,8);
    $write(511,9);while(count($bits)%8)$bits[]=0;$bytes='';for($i=0;$i<count($bits);$i+=8){$v=0;for($n=0;$n<8;$n++)$v|=$bits[$i+$n]<<$n;$bytes.=chr($v);}return bin2hex($bytes);
}
foreach([4,5,6,7,8,9] as $q){$hex=namedItem($q,[156,40],true,true);$r=SavedItemIdentity::read($hex);check($r!==null&&$r['saved_id']===1234567&&$r['item_uuid']==='0102030405060708090a0b0c0d0e0f10');if($q===4)check($r['name_ids']===['magic_prefix'=>156,'magic_suffix'=>40]);elseif(in_array($q,[5,7]))check($r['index']===156);else check($r['name_ids']===['rare_prefix'=>156,'rare_suffix'=>40]);}
check(SavedItemIdentity::read('')===null&&SavedItemIdentity::read('xyz')===null);
$m=new Market(new PDO('sqlite::memory:'));$m->migrate();$actor=$m->user('Name test');$gold=[['kind'=>'gold','code'=>'gld','label'=>'Gold','quality'=>2,'index'=>-1,'ethereal'=>-1,'quantity'=>1000]];
$hex=namedItem(6,[156,40]);$listing=$m->listing($m->user('Operator','admin'),['owner'=>$actor['id'],'item'=>['seed'=>1234567,'code'=>'rin','quality'=>6,'index'=>-1,'item_hex'=>$hex],'price'=>$gold],false);
$s=$m->browserSnapshot(null,false);check($s['listings'][0]['item']['name_ids']===['rare_prefix'=>156,'rare_suffix'=>40]);check(!str_contains(json_encode($s),'item_hex'));
$agent=$m->authAgent($m->pairAgent($actor['id'],str_repeat('a',64))['token']);
$item=['code'=>'rin','saved_id'=>1234567,'quality'=>4,'index'=>-1,'ethereal'=>0,'identified'=>1,'quantity'=>1,'quantity_bit'=>-1,'tab'=>1,'page'=>11,'item_hex'=>namedItem(4,[1,2])];
$m->browserAgentSync($agent,['catalog'=>str_repeat('a',64),'settings'=>['page'=>11,'withdraw_page'=>2,'enabled'=>true,'auto_trade'=>false],'inventory'=>[$item]],false);
check($m->browserSnapshot($actor,false)['local_items'][0]['name_ids']===['magic_prefix'=>1,'magic_suffix'=>2]);
$item['display']=['socketed_items'=>[['code'=>'rin','quality'=>6,'item_hex'=>namedItem(6,[156,40]),'saved_properties'=>[['id'=>17,'value'=>40]]]]];
$m->browserAgentSync($agent,['catalog'=>str_repeat('a',64),'settings'=>['page'=>11,'withdraw_page'=>2,'enabled'=>true,'auto_trade'=>false],'inventory'=>[$item]],false);
$child=$m->browserSnapshot($actor,false)['local_items'][0]['display']['socketed_items'][0];check($child['name_ids']===['rare_prefix'=>156,'rare_suffix'=>40]&&$child['identified']===1&&!isset($child['item_hex']));
$item['item_hex']=namedItem(4,[1,2],false);$m->browserAgentSync($agent,['catalog'=>str_repeat('a',64),'settings'=>['page'=>11,'withdraw_page'=>2,'enabled'=>true,'auto_trade'=>false],'inventory'=>[$item]],false);
$hidden=$m->browserSnapshot($actor,false)['local_items'][0];check($hidden['identified']===0&&$hidden['name_ids']===[]);
echo "Names: magic/rare/crafted/tempered IDs, optional fields, unchanged UUID/seed, old public listings, private inventory, unidentified masking and custody-byte privacy passed.\n";
