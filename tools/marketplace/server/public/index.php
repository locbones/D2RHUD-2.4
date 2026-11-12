<?php
declare(strict_types=1);
require dirname(__DIR__).'/bootstrap.php';
require dirname(__DIR__).'/HttpTransport.php';
require dirname(__DIR__).'/ReadBudget.php';
if(!isset($_GET['action'])){
    header("Content-Security-Policy: default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' data:; font-src 'self'; connect-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'self'");
    header('X-Content-Type-Options: nosniff');header('Referrer-Policy: same-origin');header('Cache-Control: no-cache');
    require __DIR__.'/market.html';exit;
}
header('Content-Type: application/json; charset=utf-8');header('Cache-Control: no-store');header('X-Content-Type-Options: nosniff');
try {
    if(($_SERVER['HTTPS']??'')!=='on'&&getenv('D2_MARKET_LOCAL_TEST')!=='1')throw new MarketError(400,'HTTPS is required.');
    if(isset($_GET['environment'])&&$_GET['environment']!=='live')throw new MarketError(403,'Demo environments are available only in the owner browser workspace.');
    $header=$_SERVER['HTTP_AUTHORIZATION']??$_SERVER['REDIRECT_HTTP_AUTHORIZATION']??'';
    $action=$_GET['action']??'';$method=$_SERVER['REQUEST_METHOD'];
    if($action!=='enroll'&&!preg_match('/^Bearer ([a-f0-9]{64})$/D',$header,$match))throw new MarketError(401,'Authentication required.');
    if($method==='GET'&&in_array($action,['sync','history'],true))$readSlot=marketReadSlot();
    $service=market();
    if(($_GET['action']??'')==='enroll'){
        if($_SERVER['REQUEST_METHOD']!=='POST')throw new MarketError(405,'Use POST.');
        if(!str_starts_with(strtolower($_SERVER['CONTENT_TYPE']??''),'application/json'))throw new MarketError(415,'Use JSON.');
        $raw=file_get_contents('php://input',false,null,0,2097153);if(strlen($raw)>2097152)throw new MarketError(413,'Installation context too large.');
        $data=json_decode($raw,true,32,JSON_THROW_ON_ERROR);if(!is_array($data))throw new MarketError(422,'Invalid installation context.');
        echo json_encode($service->enrollInstallation($data,$_SERVER['REMOTE_ADDR']??'unknown'),JSON_THROW_ON_ERROR);exit;
    }
    $actor=$action==='custody'?$service->authAgent($match[1]):$service->auth($match[1]);$hc=$_GET['hardcore']??'0';if(!in_array($hc,['0','1'],true))throw new MarketError(422,'Invalid character mode.');
    if($action==='custody'){
        if(isset($_GET['mod'])&&$_GET['mod']!==$actor['mod'])throw new MarketError(403,'The save companion belongs to another mod.');
        $service=$service->selectMod($actor['mod'],$_GET['version']??$actor['game_mode']);
    }elseif($action!=='browser-ticket')$service=$service->selectMod($_GET['mod']??'RMD-MP',$_GET['version']??'2.4');
    if($method==='POST'&&$action==='browser-ticket'){
        if(!str_starts_with(strtolower($_SERVER['CONTENT_TYPE']??''),'application/json'))throw new MarketError(415,'Use JSON.');
        $raw=file_get_contents('php://input',false,null,0,2097153);if(strlen($raw)>2097152)throw new MarketError(413,'Link context too large.');
        $context=json_decode($raw?:'{}',true,32,JSON_THROW_ON_ERROR);if(!is_array($context))throw new MarketError(422,'Invalid link context.');
        $result=$service->browserTicket($actor,$context);
    }
    elseif($method==='GET'&&$action==='sync')$result=$service->snapshot($actor,$hc==='1',false,($_GET['torch_class']??'')==='1');
    elseif($method==='GET'&&$action==='inbox'){
        $after=$_GET['after']??'0';if(!preg_match('/^[0-9]{1,18}$/D',$after))throw new MarketError(422,'Invalid message cursor.');$filtered=($_GET['notification_filters']??'0')==='1';$result=$service->inbox($actor,(int)$after,$filtered?'game':'web',$filtered&&($_GET['preferences_only']??'0')==='1');
    }
    elseif($method==='GET'&&$action==='history'){
        $query=$_GET;foreach(['seed','quality','item_index','ethereal','sockets','before'] as $key)if(isset($query[$key])){if(!preg_match('/^-?\d{1,18}$/D',$query[$key]))throw new MarketError(422,'Invalid history selector.');$query[$key]=(int)$query[$key];}
        $query['similar']=($query['similar']??'0')==='1';$result=$service->historyPage($hc==='1',$query);
    }
    elseif($method==='POST'&&in_array($action,['command','publish','custody'],true)){
        if(!str_starts_with(strtolower($_SERVER['CONTENT_TYPE']??''),'application/json'))throw new MarketError(415,'Use JSON.');
        $limit=in_array($action,['publish','custody'],true)?8*1024*1024:32768;$raw=file_get_contents('php://input',false,null,0,MarketplaceHttpTransport::wireLimit($limit)+1);
        $data=MarketplaceHttpTransport::decode($raw,$limit);
        $result=$service->controlledRequest($actor,$action,$data,$hc==='1',fn()=>match($action){'publish'=>$service->publish($actor,$data,$hc==='1'),'custody'=>$service->custody($actor,$data,$hc==='1'),default=>$service->command($actor,$data,$hc==='1')});
    }else throw new MarketError(404,'Endpoint not found.');
    echo json_encode($result,JSON_THROW_ON_ERROR|JSON_UNESCAPED_UNICODE);
}catch(MarketReadBusy $e){marketReadBusyResponse($e);}
catch(MarketError $e){http_response_code($e->status);if($e->status===503)header('Retry-After: 15');echo json_encode(['error'=>$e->getMessage()]+$e->details);}
catch(JsonException $e){http_response_code(422);echo json_encode(['error'=>'Malformed JSON.']);}
catch(Throwable $e){http_response_code(500);error_log('D2 marketplace: '.$e->getMessage());echo json_encode(['error'=>'The marketplace service is unavailable.']);}
finally{if(isset($readSlot))fclose($readSlot);}
