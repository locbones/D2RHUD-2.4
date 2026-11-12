<?php
declare(strict_types=1);

final class MarketReadBusy extends RuntimeException {}

// Heavy GETs use at most two non-blocking slots. Reserve one for opening
// the site and refreshing listings, so background native reads cannot fill it.
function marketReadSlot(bool $browse=false) {
    $config=getenv('D2_MARKET_CONFIG')?:__DIR__.'/config.php';
    foreach($browse?[1,0]:[0] as $slot){
        $handle=@fopen(dirname($config).'/.market-read-'.$slot.'.lock','c');
        if($handle===false)continue;
        if(flock($handle,LOCK_EX|LOCK_NB))return $handle;
        fclose($handle);
    }
    throw new MarketReadBusy('Listing updates are busy. Try again in a moment.');
}

function marketReadBusyResponse(MarketReadBusy $error): void {
    http_response_code(503);
    header('Retry-After: 2');
    header('X-Market-Read-Busy: 1');
    echo json_encode(['error'=>$error->getMessage()]);
}
