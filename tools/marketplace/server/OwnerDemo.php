<?php
declare(strict_types=1);

// A private, separate SQLite database. No pairing or bearer endpoint points at
// this database, so synthetic items can never enter a real save or public feed.
final class MarketplaceDemo {
    private const SCHEMA_VERSION=2026110402;
    public static function path(string $directory): string {return $directory.'/demo.sqlite';}
    public static function gate(string $directory,int $mode) {
        if(!is_dir($directory)&&!mkdir($directory,0700,true))throw new RuntimeException('Owner storage is unavailable.');
        $f=fopen($directory.'/demo.lock','c');if(!$f||!flock($f,$mode))throw new RuntimeException('Demo workspace is busy.');chmod($directory.'/demo.lock',0600);return $f;
    }
    public static function open(string $directory,array $actor,?Market $authority=null): Market {
        if($authority)$authority->requireOwner($actor);
        if(!is_file(self::path($directory)))throw new MarketError(409,'Generate a demo environment first.');
        $m=new Market(new PDO('sqlite:'.self::path($directory)));
        if((int)$m->db->query('PRAGMA user_version')->fetchColumn()<self::SCHEMA_VERSION){$m->migrate();$m->db->exec('PRAGMA user_version='.self::SCHEMA_VERSION);}
        // The live UID is authoritative after an account recovery. A private
        // demo's cached owner must never revoke live owner access.
        if($authority&&!$m->isOwner($actor))$m->transaction(function()use($m,$actor){
            $old=$m->run('SELECT owner FROM market_owners WHERE community=?',[$m->community])->fetchColumn();
            if(!$m->run('SELECT id FROM actors WHERE id=?',[$actor['id']])->fetchColumn())$m->run('INSERT INTO actors VALUES (?,?,?,?)',[$actor['id'],$actor['name'],hash('sha256',random_bytes(32)),'player']);
            if($old)$m->run('UPDATE market_owners SET owner=? WHERE community=?',[$actor['id'],$m->community]);else $m->grantOwner($actor['id']);
            $m->grantModerator($actor['id']);
            $result=['previous_owner'=>$old?:null,'owner'=>$actor['id']];
            $m->run('INSERT INTO market_owner_actions VALUES (?,?,?,?,?,?)',[Market::id(),$actor['id'],'owner_rebind',hash('sha256',json_encode($result)),json_encode($result),time()]);
        });
        $m->requireOwner($actor);return $m;
    }
    public static function status(string $directory,array $actor,?Market $authority=null): array {
        if($authority)$authority->requireOwner($actor);
        if(!is_file(self::path($directory)))return ['available'=>false];
        $m=self::open($directory,$actor,$authority);
        return ['available'=>true,'id'=>$m->controlState()['reset_id'],'players'=>(int)$m->run('SELECT COUNT(*) FROM actors')->fetchColumn(),'listings'=>(int)$m->run("SELECT COUNT(*) FROM listings WHERE status='open'")->fetchColumn(),'offers'=>(int)$m->run('SELECT COUNT(*) FROM offers')->fetchColumn(),'sales'=>(int)$m->run("SELECT COUNT(*) FROM events WHERE kind='completed_sale'")->fetchColumn()];
    }
    public static function generate(Market $live,array $actor,array $packet,string $directory): array {
        $live->requireOwner($actor);$gate=self::gate($directory,LOCK_EX);
        try{
            $old=self::status($directory,$actor,$live);if(($old['id']??'')===$packet['id'])return $old;
            if(($old['id']??'')!==($packet['expected_demo']??''))throw new MarketError(409,'The demo changed. Refresh owner tools first.');
            $size=$packet['size']??'busy';$sizes=['small'=>[24,100,80],'busy'=>[64,320,240],'festival'=>[100,600,480]];
            if(!isset($sizes[$size]))throw new MarketError(422,'Choose a demo size.');
            [$players,$listings,$sales]=$sizes[$size];$temporary=$directory.'/demo-'.bin2hex(random_bytes(8)).'.new';
            $m=new Market(new PDO('sqlite:'.$temporary));chmod($temporary,0600);$m->migrate();
            $m->db->exec('PRAGMA user_version='.self::SCHEMA_VERSION);
            $m->run('INSERT INTO actors VALUES (?,?,?,?)',[$actor['id'],$actor['name'],hash('sha256',random_bytes(32)),'player']);$m->grantOwner($actor['id']);$m->grantModerator($actor['id']);
            $m->run('INSERT INTO market_memberships VALUES (?,?,?,?)',[$actor['id'],'RMD-MP','2.4',time()]);$m->run('INSERT INTO market_players VALUES (?,?)',[$actor['id'],time()]);
            $m->run('UPDATE market_control SET reset_id=?',[$packet['id']]);
            self::seed($m,$actor,$players,$listings,$sales);
            $m=null;
            if(!rename($temporary,self::path($directory)))throw new RuntimeException('Could not activate the demo.');chmod(self::path($directory),0600);
            return self::status($directory,$actor,$live);
        }finally{flock($gate,LOCK_UN);fclose($gate);if(isset($temporary)&&is_file($temporary))@unlink($temporary);}
    }
    public static function clear(array $actor,array $packet,string $directory,?Market $authority=null): array {
        $gate=self::gate($directory,LOCK_EX);
        try{
            $old=self::status($directory,$actor,$authority);if(!$old['available'])return ['cleared'=>true];
            if(($packet['confirm']??'')!=='CLEAR DEMO'||($packet['expected_demo']??'')!==$old['id'])throw new MarketError(409,'Review the current demo and type CLEAR DEMO.');
            $backup=$directory.'/demo-cleared-'.$packet['id'].'.sqlite';
            if(!rename(self::path($directory),$backup))throw new RuntimeException('Could not archive the demo.');chmod($backup,0600);return ['cleared'=>true];
        }finally{flock($gate,LOCK_UN);fclose($gate);}
    }
    private static function gold(int $quantity): array {return [['kind'=>'gold','code'=>'gld','label'=>'Gold','quality'=>2,'index'=>-1,'ethereal'=>-1,'quantity'=>$quantity]];}
    private static function rune(int $quantity): array {return [['code'=>'r33','label'=>'Zod Rune','quality'=>2,'index'=>-1,'ethereal'=>-1,'quantity'=>$quantity]];}
    private static function seed(Market $m,array $owner,int $playerCount,int $listingCount,int $saleCount): void {
        $m->transaction(function()use($m,$owner,$playerCount,$listingCount,$saleCount){
            $catalog=$m->browserCatalog();$pool=[];
            foreach(['unique'=>7,'set'=>5] as $kind=>$quality)foreach($catalog[$kind] as $index=>$item)if(!empty($item['image'])&&!empty($item['code'])&&($catalog['bases'][$item['code']]['category']??'')!=='misc')$pool[]=$item+['quality'=>$quality,'index'=>(int)$index];
            $popular=array_flip(['Harlequin Crest',"Mara's Kaleidoscope",'The Stone of Jordan','Arachnid Mesh','The Grandfather','War Traveler',"Griffon's Eye",'Windforce',"Death's Web",'Skin of the Vipermagi',"Bul-Kathos' Wedding Band",'Guardian Angel']);
            usort($pool,fn($a,$b)=>($popular[$a['name']]??999)<=>($popular[$b['name']]??999));
            $sets=array_values(array_filter($pool,fn($r)=>$r['quality']===5));
            $soj=current(array_filter($pool,fn($r)=>$r['name']==='The Stone of Jordan'));
            if(count($pool)<30)throw new RuntimeException('The demo item catalog is unavailable.');
            $names=['Ash','Cinder','Raven','Thorn','Ember','Frost','Dusk','Iron','Moon','Void','Rune','Storm','Stone','Wolf','Grim','Night','Flame','Wraith','Mist','Silver'];$ends=['Warden','Walker','Seeker','Forge','Hunter','Keeper','Sage','Runner','Heart','Blade'];$actors=[$owner];
            for($i=0;$i<$playerCount;++$i){$a=$m->user($names[$i%count($names)].$ends[intdiv($i,count($names))%count($ends)]);$m->run("UPDATE market_memberships SET linked=? WHERE owner=?",[time()-($i+1)*900,$a['id']]);$m->run('INSERT INTO market_players VALUES (?,?)',[$a['id'],time()]);$actors[]=$a;}
            $make=function(array $a,array $template,int $n,array $price=[],bool $listed=true)use($m):array{
                $id=Market::id();$listing=Market::id();$hex='00000100'.hash('sha256','demo-'.$id);$seed=hexdec(substr($id,0,7))+1;
                $props=[['id'=>39,'param'=>0,'value'=>15+$n%21,'list'=>'base'],['id'=>43,'param'=>0,'value'=>15+$n%21,'list'=>'base'],['id'=>0,'param'=>0,'value'=>8+$n%13,'list'=>'base']];
                if(($template['category']??'')==='weapon')$props=[['id'=>17,'param'=>0,'value'=>180+$n%121,'list'=>'base'],['id'=>19,'param'=>0,'value'=>80+$n%160,'list'=>'base']];
                $meta=['quantity'=>1,'quantity_bit'=>-1,'identified'=>1,'price'=>$price,'display'=>['level'=>75+$n%20,'saved_properties'=>$props,'socketed_items'=>[]]];
                // Display-only socket examples remain wholly inside the demo.
                if(($template['name']??'')==='The Grandfather'){
                    $meta['display']['saved_properties'][]=['id'=>194,'param'=>0,'value'=>4,'list'=>'base'];
                    $meta['display']['socketed_items']=self::socketExamples();
                }
                $m->run('INSERT INTO items VALUES (?,?,?,?,?,?,?,?,?,?)',[$id,$m->scope(false),$seed,$template['code'],$template['quality'],$template['index'],0,count($meta['display']['socketed_items']),$a['id'],$hex]);
                $m->run('INSERT INTO escrow_items VALUES (?,?,?,?,?,?)',[$id,$a['id'],$m->scope(false),$listed?'listed':'available',json_encode($meta),Market::id()]);
                if($listed)$m->run('INSERT INTO listings VALUES (?,?,?,?,?,?,?)',[$listing,$id,$a['id'],1,'open',json_encode($price),time()-$n*37]);
                return ['id'=>$listing,'item'=>$id,'seller'=>$a['id'],'hex'=>$hex,'template'=>$template,'meta'=>$meta,'price'=>$price,'seed'=>$seed];
            };
            foreach($actors as $n=>$a){$m->run('INSERT INTO escrow_balances VALUES (?,?,?,?,?)',[$a['id'],$m->scope(false),'gold','',$n===0?1000000000:250000000]);$m->run('INSERT INTO escrow_balances VALUES (?,?,?,?,?)',[$a['id'],$m->scope(false),'bag','RB_High',100000]);
                for($r=0;$r<6;++$r)$make($a,['code'=>'r33','quality'=>2,'index'=>-1],10000+$n*6+$r,[],false);
                $m->run('INSERT INTO web_profiles VALUES (?,?,?,?,?,?)',[$a['id'],$m->scope(false),1,json_encode(['page'=>11,'withdraw_page'=>2,'enabled'=>true,'auto_trade'=>false]),'[]',time()]);
                $m->run('INSERT INTO market_preferences VALUES (?,?,?)',[$a['id'],$m->scope(false),0]);if($soj)for($r=0;$r<3;++$r)$make($a,$soj,50000+$n*3+$r,[],false);
            }
            $m->pairAgent($owner['id'],str_repeat('a',64)); // Private demo pairing only; no live endpoint accepts its token.
            // Give the demonstrator enough runes to exercise large barter asks.
            for($r=0;$r<100;++$r)$make($owner,['code'=>'r33','quality'=>2,'index'=>-1],30000+$r,[],false);
            $trades=[];$historySales=0;$featured=min(24,intdiv($saleCount,4));
            for($n=0;$n<$listingCount;++$n){$a=$actors[1+$n%$playerCount];if($n%19===0)$a=$owner;$template=$n>0&&$n%5===0&&$sets?$sets[intdiv($n,5)%count($sets)]:$pool[$n%count($pool)];
                $price=match($n%9){0=>Market::freePrice(),1=>self::rune(1+$n%4),2=>array_merge(self::gold(1500000+$n*1000),self::rune(1)),3=>self::rune(4+$n%10),5=>$soj?[['code'=>$soj['code'],'label'=>$soj['name'],'quality'=>7,'index'=>$soj['index'],'ethereal'=>-1,'quantity'=>2]]:self::gold(500000),default=>self::gold((5+$n%95)*25000)};
                $trade=$make($a,$template,$n,$n<$featured?self::gold(100000+$n*15000):$price);
                if($n<$featured){
                    // Resell this very ledger item before its current listing.
                    // All ownership/payment changes use the ordinary domain;
                    // delivery stays simulated inside this private database.
                    $holder=$a;$hops=2+$n%3;
                    for($hop=0;$hop<$hops;++$hop){
                        $buyer=$a;
                        if($hop!==$hops-1){$j=1+($n+11+$hop*7)%$playerCount;while(in_array($actors[$j]['id'],[$holder['id'],$a['id']],true))$j=1+$j%$playerCount;$buyer=$actors[$j];}
                        $historical=match($n%3){1=>self::rune(1+$hop%2),2=>array_merge(self::gold(100000+$n*13000+$hop*22000),self::rune(1)),default=>self::gold(100000+$n*15000+$hop*35000)};
                        $offer=$m->command($buyer,['id'=>Market::id(),'type'=>'propose','listing_id'=>$trade['id'],'listing_revision'=>1,'price'=>$historical,'message'=>'Agreed on the listed roll and payment.'],false);
                        $m->command($holder,['id'=>Market::id(),'type'=>'accept','offer_id'=>$offer['offer_id'],'expected_revision'=>1],false);
                        if($m->run('SELECT status FROM offers WHERE id=?',[$offer['offer_id']])->fetchColumn()!=='completed')throw new RuntimeException('Demo history did not settle.');
                        $at=time()-(30+$n%5-$hop*5)*86400;
                        $m->run('UPDATE events SET at=? WHERE offer=?',[$at,$offer['offer_id']]);$m->run('UPDATE offers SET created=? WHERE id=?',[$at-1800,$offer['offer_id']]);$m->run('UPDATE listings SET created=? WHERE id=?',[$at-3600,$trade['id']]);$m->run('UPDATE settlements SET created=? WHERE offer=?',[$at,$offer['offer_id']]);
                        ++$historySales;$holder=$buyer;
                        $relisted=$m->command($holder,['id'=>Market::id(),'type'=>'escrow_price','item_id'=>$trade['item'],'price'=>$hop===$hops-1?$price:self::gold(150000+$n*15000+$hop*35000)],false);$trade['id']=$relisted['listing_id'];
                    }
                    $m->run('UPDATE listings SET created=? WHERE id=?',[time()-$n*37,$trade['id']]);
                }
                $trade['price']=$price;$trades[]=$trade;
            }
            foreach($trades as $n=>$trade){
                if(($trade['price'][0]['kind']??'')==='free')continue;
                $bids=$n<$featured?2+$n%3:($n%3===0?1+(int)($n%12===0):0);$used=[];
                for($bid=0;$bid<$bids;++$bid){
                    $j=1+($n+7+$bid*9)%$playerCount;$buyer=$n%5===0&&$bid===0&&$trade['seller']!==$owner['id']?$owner:$actors[$j];
                    while($buyer['id']===$trade['seller']||isset($used[$buyer['id']])){$j=1+$j%$playerCount;$buyer=$actors[$j];}$used[$buyer['id']]=true;
                    $goldAsk=($trade['price'][0]['kind']??'')==='gold'?(int)$trade['price'][0]['quantity']:250000+$n*15000;
                    $price=$bid===2?self::rune(1+$n%3):self::gold(max(25000,(int)round($goldAsk*(0.65+$bid*0.1)/1000)*1000));
                    $offer=$m->command($buyer,['id'=>Market::id(),'type'=>'propose','listing_id'=>$trade['id'],'listing_revision'=>1,'price'=>$price,'message'=>$bid?'I can offer this payment for the item.':'Interested in this roll. Would you consider this offer?'],false);
                    if($bid===0&&$n%2===0)$m->command($m->browserActor($trade['seller']),['id'=>Market::id(),'type'=>'counter','offer_id'=>$offer['offer_id'],'expected_revision'=>1,'price'=>$trade['price'],'message'=>'Thanks! Here are the terms I would accept.'],false);
                }
            }
            for($n=0;$n<$saleCount-$historySales;++$n){$a=$actors[1+(match($n%5){0,1=>0,2=>1,default=>$n%$playerCount})];$template=$pool[(match($n%10){0,1,2,3=>0,4,5=>1,default=>($n%12)*17})%count($pool)];$price=$n%11===0?self::rune(1):($n%17===0?Market::freePrice():self::gold((5+$n%27)*25000));$trade=$make($a,$template,40000+$n,$price);$buyer=$actors[1+($n+11)%$playerCount];if($buyer['id']===$a['id'])$buyer=$actors[1+($n+12)%$playerCount];$receipt='demo-sale-'.$n;
                $offer=$m->command($buyer,['id'=>Market::id(),'type'=>'propose','listing_id'=>$trade['id'],'listing_revision'=>1,'price'=>$price],false);
                if($m->run('SELECT status FROM offers WHERE id=?',[$offer['offer_id']])->fetchColumn()!=='completed')$m->command($a,['id'=>Market::id(),'type'=>'accept','offer_id'=>$offer['offer_id'],'expected_revision'=>1],false);
                $m->run("UPDATE escrow_items SET state='delivered' WHERE item=?",[$trade['item']]);$m->run("UPDATE events SET at=? WHERE offer=?",[time()-($saleCount-$n)*18000,$offer['offer_id']]);

            }
            // Equipment and carried inventory snapshots are display-only demo rows.
            $gearCodes=[1=>'uap',2=>'amu',3=>'utp',4=>'7cr',5=>'uit',6=>'rin',7=>'rin',8=>'ulc',9=>'utb',10=>'uhg'];
            foreach($actors as $n=>$a){$equipment=[];foreach($gearCodes as $body=>$code){if(!isset($catalog['bases'][$code]))continue;$b=$catalog['bases'][$code];$equipment[]=['body'=>$body,'location'=>1,'container'=>0,'x'=>0,'y'=>0,'grid_width'=>1,'grid_height'=>1,'code'=>$code,'quality'=>2,'index'=>-1,'identified'=>true,'quantity'=>1,'saved_id'=>10000+$n*20+$body,'display'=>['level'=>85,'saved_properties'=>[['id'=>39,'param'=>0,'value'=>25,'list'=>'base']],'socketed_items'=>[]]];}
                $inventory=[];for($i=0;$i<18;++$i)$inventory[]=['body'=>0,'location'=>0,'container'=>1,'x'=>$i%10,'y'=>intdiv($i,10),'grid_width'=>1,'grid_height'=>1,'code'=>$i%3===0?'r33':'rin','quality'=>$i%3===0?2:6,'index'=>-1,'identified'=>true,'quantity'=>1,'saved_id'=>80000+$n*20+$i,'display'=>['level'=>85,'saved_properties'=>[['id'=>0,'param'=>0,'value'=>10+$i,'list'=>'base']],'socketed_items'=>[]]];
                $key=hash('sha256','demo-character-'.$a['id']);$name=substr($a['name'],0,12).'Main';$character=['character_key'=>$key,'name'=>$name,'class_id'=>$n%7,'level'=>80+$n%20,'hardcore'=>false,'saved_at'=>time()-$n*60,'equipment'=>$equipment,'inventory'=>$inventory,'mercenary'=>['present'=>true,'equipment'=>array_values(array_filter($equipment,fn($r)=>in_array($r['body'],[1,3,4],true)))]];
                $m->run('INSERT INTO market_characters VALUES (?,?,?,?,?,?,?,?,?)',[$a['id'],$m->scope(false),$key,$name,$n%7,$character['level'],$character['saved_at'],time(),json_encode($character)]);
            }
            foreach(array_slice($actors,1,6) as $a)$m->command($a,['id'=>Market::id(),'type'=>'direct_message','to'=>$owner['id'],'message'=>'Hello! I saw your marketplace listings. Let me know if you would like to make a trade.'],false);
            // Two existing listings become bundles, keeping each preset's
            // listing/sale totals stable while exercising the actual domain.
            foreach([38,40] as $n){$trade=$trades[$n]??null;if(!$trade)continue;$a=$m->browserActor($trade['seller']);$extra=$m->run("SELECT e.item FROM escrow_items e JOIN items i ON i.id=e.item WHERE e.owner=? AND e.state='available' AND i.code='r33' ORDER BY e.item LIMIT 1",[$a['id']])->fetchColumn();if(!$extra)continue;
                $bundle=$m->command($a,['id'=>Market::id(),'type'=>'bundle_create','title'=>$n===38?'Sanctuary starter pack':'Equipment & Zod bundle','items'=>[$trade['item'],$extra],'price'=>self::gold(350000)],false);
                foreach([1,2] as $j){$bidder=$actors[1+($n+$j)%$playerCount];if($bidder['id']!==$a['id'])$m->command($bidder,['id'=>Market::id(),'type'=>'propose','listing_id'=>$bundle['listing_id'],'listing_revision'=>1,'price'=>self::gold(250000+$j*20000),'message'=>'Would you take this for the complete bundle?'],false);}
            }
            $m->command($owner,['id'=>Market::id(),'type'=>'search_save','name'=>'Zod trades','enabled'=>false,'criteria'=>['q'=>'Zod Rune']],false);
            $m->command($owner,['id'=>Market::id(),'type'=>'search_save','name'=>'Fire resistance 20+','enabled'=>false,'criteria'=>['stats'=>[['id'=>39,'min'=>20]]]],false);
        });
    }
    public static function socketExamples(): array {
        return [
            ['code'=>'r33','quality'=>2,'index'=>-1,'identified'=>true,'display'=>['saved_properties'=>[],'socketed_items'=>[]]],
            ['code'=>'gpr','quality'=>2,'index'=>-1,'identified'=>true,'display'=>['saved_properties'=>[],'socketed_items'=>[]]],
            ['code'=>'jew','quality'=>6,'index'=>-1,'identified'=>true,'display'=>['level'=>85,'saved_properties'=>[['id'=>17,'value'=>40,'param'=>0,'list'=>'base'],['id'=>18,'value'=>40,'param'=>0,'list'=>'base'],['id'=>93,'value'=>15,'param'=>0,'list'=>'base']],'socketed_items'=>[]]],
        ];
    }
    public static function respond(Market $m,array $owner,array $command,array $result): void {
        $m->transaction(function()use($m,$owner,$command,$result){
            if($m->run('SELECT id FROM commands WHERE actor=? AND id=?',['demo-bot',$command['id']])->fetchColumn())return;
            try{self::respondOnce($m,$owner,$command,$result);}catch(MarketError $e){if(!in_array($e->status,[409,429],true))throw $e;}
            $m->run('INSERT INTO commands VALUES (?,?,?,?)',['demo-bot',$command['id'],hash('sha256',json_encode($command)),'{}']);
        });
    }
    private static function respondOnce(Market $m,array $owner,array $command,array $result): void {
        $type=$command['type']??'';
        if($type==='direct_message'){
            $to=$command['to']??'';if($to&&$to!==$owner['id'])$m->command($m->browserActor($to),['id'=>Market::id(),'type'=>'direct_message','to'=>$owner['id'],'message'=>'Thanks for your message! I am happy to discuss a trade. Send an offer on the item you like.'],false);return;
        }
        $id=$result['offer_id']??$command['offer_id']??null;if(!$id||!in_array($type,['propose','counter','message'],true))return;
        $o=$m->run('SELECT * FROM offers WHERE id=?',[$id])->fetch();if(!$o||$o['status']!=='pending')return;
        $peer=$o['seller']===$owner['id']?$o['buyer']:$o['seller'];if($peer===$owner['id'])return;$actor=$m->browserActor($peer);
        $packet=['id'=>Market::id(),'offer_id'=>$id,'expected_revision'=>(int)$o['revision']];
        if($type==='message')$packet+=['type'=>'message','message'=>'I received your note. I can trade for the asking price, or review another counter-offer.'];
        elseif($o['proposer']===$owner['id']){
            $ask=json_decode($m->run('SELECT price FROM listings WHERE id=?',[$o['listing']])->fetchColumn(),true);$price=json_decode($o['price'],true);
            $same=Market::price($price)===Market::price($ask);$packet+=($same||hexdec(substr($command['id'],0,2))%3===0)?['type'=>'accept']:['type'=>'counter','price'=>$ask,'message'=>'I would prefer this price. What do you think?'];
        }else return;
        $m->command($actor,$packet,false);
    }
}
