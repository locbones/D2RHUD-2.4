<?php
declare(strict_types=1);

// Heavy GETs share two non-blocking slots. Leave room on the shared PHP
// pool for small requests, messages and transaction/recovery writes.
function marketReadSlot() {
    $config=getenv('D2_MARKET_CONFIG')?:__DIR__.'/config.php';
    for($slot=0;$slot<2;$slot++){
        $handle=@fopen(dirname($config).'/.market-read-'.$slot.'.lock','c');
        if($handle===false)continue;
        if(flock($handle,LOCK_EX|LOCK_NB))return $handle;
        fclose($handle);
    }
    throw new MarketError(503,'The marketplace is busy. Your current view can stay open; try again shortly.');
}
