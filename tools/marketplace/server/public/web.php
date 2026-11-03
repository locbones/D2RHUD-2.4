<?php
declare(strict_types=1);
require dirname(__DIR__).'/bootstrap.php';
require dirname(__DIR__).'/OwnerDemo.php';
header('Content-Type: application/json; charset=utf-8');header('Cache-Control: no-store');header('X-Content-Type-Options: nosniff');header('Referrer-Policy: no-referrer');
try{
    $local=getenv('D2_MARKET_LOCAL_TEST')==='1';
    if(($_SERVER['HTTPS']??'')!=='on'&&!$local)throw new MarketError(400,'HTTPS is required.');
    $sessions=$local?sys_get_temp_dir().'/d2market-'.hash('sha256',getenv('D2_MARKET_CONFIG')?:__DIR__):dirname(__DIR__).'/sessions';
    if(!is_dir($sessions)&&!mkdir($sessions,0700,true)&&!is_dir($sessions))throw new RuntimeException('Session storage is unavailable.');
    session_save_path($sessions);ini_set('session.use_strict_mode','1');ini_set('session.use_only_cookies','1');ini_set('session.gc_maxlifetime','7776000');session_name($local?'market_local':'__Host-market');
    session_set_cookie_params(['lifetime'=>7776000,'path'=>'/','secure'=>!$local,'httponly'=>true,'samesite'=>'Strict']);session_start();
    if(isset($_SESSION['expires'])&&$_SESSION['expires']<time())$_SESSION=[];
    if(!isset($_SESSION['csrf']))$_SESSION['csrf']=bin2hex(random_bytes(32));
    $m=market();$action=$_GET['action']??'snapshot';$method=$_SERVER['REQUEST_METHOD'];$hc=$_GET['hardcore']??'0';
    if(!in_array($hc,['0','1'],true))throw new MarketError(422,'Invalid character mode.');$hc=$hc==='1';
    $actor=isset($_SESSION['actor'])?$m->browserActor($_SESSION['actor']):null;
    if($actor&&!hash_equals($m->browserSessionStamp($actor),$_SESSION['stamp']??'')){unset($_SESSION['actor']);$actor=null;}
    $live=$m;$environment=$_GET['environment']??'live';if(!in_array($environment,['live','demo'],true))throw new MarketError(422,'Invalid marketplace environment.');
    if($environment==='demo'&&!in_array($action,['owner-status','owner-command','link','unlink'],true)){$live->requireOwner($actor);$demoGate=MarketplaceDemo::gate(marketOwnerDirectory(),LOCK_SH);$m=MarketplaceDemo::open(marketOwnerDirectory(),$actor);}
    if(!in_array($action,['link','unlink','markets','owner-status','owner-command'],true))$m=$m->selectMod($_GET['mod']??'RMD-MP',$_GET['version']??'2.4');
    if($method==='POST'){
        if(!hash_equals($_SESSION['csrf'],$_SERVER['HTTP_X_CSRF_TOKEN']??''))throw new MarketError(403,'This page expired. Refresh before trying again.');
        if(!str_starts_with(strtolower($_SERVER['CONTENT_TYPE']??''),'application/json'))throw new MarketError(415,'Use JSON.');
        $raw=file_get_contents('php://input',false,null,0,32769);if(strlen($raw)>32768)throw new MarketError(413,'Request too large.');$data=json_decode($raw,true,32,JSON_THROW_ON_ERROR);if(!is_array($data))throw new MarketError(422,'Invalid request.');
        if($action==='link'){
            $m->browserThrottle('ip:'.($_SERVER['REMOTE_ADDR']??''));
            $actor=$m->browserLink($data);session_regenerate_id(true);$_SESSION=['actor'=>$actor['id'],'stamp'=>$m->browserSessionStamp($actor),'csrf'=>bin2hex(random_bytes(32)),'expires'=>time()+7776000];$result=['self'=>$actor,'csrf'=>$_SESSION['csrf'],'mod'=>$actor['mod'],'game_mode'=>$actor['game_mode']];
        }elseif($action==='unlink'){
            $_SESSION=[];session_destroy();setcookie(session_name(),'', ['expires'=>1,'path'=>'/','secure'=>!$local,'httponly'=>true,'samesite'=>'Strict']);$result=['signed_out'=>true];
        }elseif($action==='owner-command'){
            $live->requireOwner($actor);$result=$live->ownerCommand($actor,$data,marketOwnerDirectory(),fn()=>($data['type']??'')==='demo_generate'?MarketplaceDemo::generate($live,$actor,$data,marketOwnerDirectory()):MarketplaceDemo::clear($actor,$data,marketOwnerDirectory()));
        }else{
            if(!$actor)throw new MarketError(401,'Link your player to trade.');
            if($action==='command'){$result=$m->controlledRequest($actor,'command',$data,$hc,fn()=>$m->command($actor,$data,$hc));if($environment==='demo')MarketplaceDemo::respond($m,$actor,$data,$result);}
            else throw new MarketError(404,'Endpoint not found.');
        }
    }elseif($method==='GET'&&$action==='owner-status'){
        $live->requireOwner($actor);$gate=MarketplaceDemo::gate(marketOwnerDirectory(),LOCK_SH);$result=$live->ownerStatus($actor)+['demo'=>MarketplaceDemo::status(marketOwnerDirectory(),$actor)];
    }elseif($method==='GET'&&$action==='markets'){$result=$m->modDirectory($actor);$result['csrf']=$_SESSION['csrf'];$result['self']=$actor;}
    elseif($method==='GET'&&$action==='catalog'){$result=$m->browserCatalog();}
    elseif($method==='GET'&&$action==='snapshot'){$result=$m->browserSnapshot($actor,$hc);$result['csrf']=$_SESSION['csrf'];$result['environment']=$environment;}
    elseif($method==='GET'&&in_array($action,['reports','report'],true)){
        if(!$actor)throw new MarketError(401,'Link your player to review reports.');$query=$_GET;if(isset($query['before'])){if(!preg_match('/^[0-9]{1,18}$/D',$query['before']))throw new MarketError(422,'Invalid report cursor.');$query['before']=(int)$query['before'];}
        $result=$action==='reports'?$m->moderationQueue($actor,$query):$m->moderationReport($actor,$_GET['id']??'');
    }
    elseif($method==='GET'&&$action==='activity'){if(!$actor)throw new MarketError(401,'Link your player to see activity.');$result=$m->activity($actor,$hc);}
    elseif($method==='GET'&&$action==='stats'){$result=$m->tradeStats($hc,$_GET);}
    elseif($method==='GET'&&$action==='players'){$result=$m->players($hc,$_GET);}
    elseif($method==='GET'&&$action==='player'){$result=$m->playerProfile($_GET['id']??'',$hc,$_GET['character']??null);}
    elseif($method==='GET'&&in_array($action,['inbox','messages'],true)){
        if(!$actor)throw new MarketError(401,'Link your player to read messages.');
        foreach(['after','before'] as $k)if(isset($_GET[$k])&&!preg_match('/^[0-9]{1,18}$/D',$_GET[$k]))throw new MarketError(422,'Invalid message cursor.');
        $result=$action==='inbox'?$m->inbox($actor,(int)($_GET['after']??0)):$m->conversations($actor,$_GET['peer']??null,(int)($_GET['before']??PHP_INT_MAX));
    }
    elseif($method==='GET'&&$action==='conversation'){if(!$actor)throw new MarketError(401,'Link your player to read a conversation.');$result=$m->privateConversation($actor,$hc,$_GET['id']??'');}
    elseif($method==='GET'&&$action==='listing-offers'){
        $query=['listing_id'=>$_GET['listing_id']??null];if(isset($_GET['before'])){if(!preg_match('/^[0-9]{1,18}$/D',$_GET['before']))throw new MarketError(422,'Invalid page.');$query['before']=(int)$_GET['before'];}$result=$m->publicOffers($hc,$query,$actor);
    }
    elseif($method==='GET'&&$action==='history'){
        $query=$_GET;foreach(['seed','quality','item_index','ethereal','sockets','before'] as $key)if(isset($query[$key])){if(!preg_match('/^-?\d{1,18}$/D',$query[$key]))throw new MarketError(422,'Invalid history selector.');$query[$key]=(int)$query[$key];}$query['similar']=($query['similar']??'0')==='1';$query['averages']=($query['averages']??'0')==='1';$result=$m->historyPage($hc,$query);
    }else throw new MarketError(404,'Endpoint not found.');
    session_write_close();echo json_encode($result,JSON_THROW_ON_ERROR|JSON_UNESCAPED_UNICODE);
}catch(MarketError $e){http_response_code($e->status);echo json_encode(['error'=>$e->getMessage()]);}
catch(JsonException $e){http_response_code(422);echo json_encode(['error'=>'Malformed JSON.']);}
catch(Throwable $e){http_response_code(500);error_log('D2 marketplace web: '.$e->getMessage());echo json_encode(['error'=>'The marketplace is temporarily unavailable.']);}
