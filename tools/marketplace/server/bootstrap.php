<?php
declare(strict_types=1);
require_once __DIR__.'/Market.php';
function market(): Market {
    $path=getenv('D2_MARKET_CONFIG')?:__DIR__.'/config.php';
    if(!$path||!is_file($path))throw new RuntimeException('Server configuration is missing.');
    $c=require $path;
    return new Market(new PDO($c['dsn'],$c['username']??null,$c['password']??null),$c['community']??'tcpip',$c['mod']??'RMD-MP');
}

function marketOwnerDirectory(): string {
    $config=getenv('D2_MARKET_CONFIG')?:__DIR__.'/config.php';
    return dirname($config).'/operator-data';
}
