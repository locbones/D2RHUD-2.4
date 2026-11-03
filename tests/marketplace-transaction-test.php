<?php
declare(strict_types=1);
require __DIR__.'/../tools/marketplace/server/Market.php';
function check(bool $ok):void{if(!$ok)throw new RuntimeException('Transaction retry assertion failed.');}
function contention(): PDOException{$e=new PDOException('Simulated database deadlock');$e->errorInfo=['40001',1213,'deadlock'];return $e;}
$m=new Market(new PDO('sqlite::memory:'));$m->migrate();$attempts=0;
$result=$m->transaction(function()use($m,&$attempts){$m->run('INSERT INTO commands VALUES (?,?,?,?)',['test','retry','hash','{}']);if(++$attempts<3)throw contention();return 'committed';});
check($result==='committed'&&$attempts===3&&(int)$m->run('SELECT COUNT(*) FROM commands')->fetchColumn()===1);
$attempts=0;
try{$m->transaction(function()use($m,&$attempts){++$attempts;$m->run('INSERT INTO commands VALUES (?,?,?,?)',['test','busy','hash','{}']);throw contention();});throw new RuntimeException('Unbounded retry accepted.');}
catch(MarketError $e){check($e->status===503&&$attempts===4&&(int)$m->run('SELECT COUNT(*) FROM commands')->fetchColumn()===1);}
$attempts=0;try{$m->transaction(function()use(&$attempts){++$attempts;throw new MarketError(422,'Invalid amount');});}catch(MarketError $e){check($e->status===422&&$attempts===1);}
echo "Transactions: rollback before bounded contention retry, exactly one committed result, clean busy refusal and no retry of validation errors passed.\n";
