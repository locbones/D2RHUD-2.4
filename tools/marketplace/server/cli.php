<?php
declare(strict_types=1);
require __DIR__.'/bootstrap.php';
if(PHP_SAPI!=='cli'){http_response_code(404);exit;}
try {
    $m=market();$command=$argv[1]??'';
    $r=match($command){
        'init'=>(function()use($m){$m->migrate();return ['initialized'=>true];})(),
        'pair-agent'=>(new Market($m->db,$m->community,Market::modName($argv[4]??$m->mod)))->pairAgent($argv[2]??'', $argv[3]??''),
        'owner'=>$m->grantOwner($argv[2]??''),
        'moderator'=>$m->grantModerator($argv[2]??''),
        'web-code'=>$m->browserTicket($m->browserActor($argv[2]??'')),
        'user'=>$m->user($argv[2]??'', $argv[3]??'player'),
        'import-listing'=>$m->listing(['id'=>'operator','role'=>'admin'],json_decode(file_get_contents($argv[2]),true,512,JSON_THROW_ON_ERROR),($argv[3]??'SC')==='HC'),
        'record-sale'=>$m->sale(['id'=>'operator','role'=>'admin'],json_decode(file_get_contents($argv[2]),true,512,JSON_THROW_ON_ERROR),($argv[3]??'SC')==='HC'),
        default=>throw new RuntimeException('Usage: cli.php init | owner PLAYER_ID | moderator PLAYER_ID | user NAME [player|admin|settler|tester] | import-listing FILE [SC|HC] | record-sale FILE [SC|HC]')};
    echo json_encode($r,JSON_PRETTY_PRINT|JSON_THROW_ON_ERROR).PHP_EOL;
}catch(Throwable $e){fwrite(STDERR,$e->getMessage().PHP_EOL);exit(1);}
