<?php
declare(strict_types=1);
require_once __DIR__.'/GiftItemFactory.php';

trait MarketplaceGiftCampaigns {
    public function giftCampaignSchema(string $engine):void {
        foreach([
            'market_gift_campaigns'=>'id CHAR(32) PRIMARY KEY, scope VARCHAR(255) NOT NULL, owner VARCHAR(64) NOT NULL, title VARCHAR(128) NOT NULL, announcement TEXT NOT NULL, claim_message TEXT NOT NULL, expires BIGINT, created BIGINT NOT NULL, status VARCHAR(16) NOT NULL, templates MEDIUMTEXT NOT NULL',
            'market_gift_claims'=>'campaign CHAR(32) NOT NULL, owner VARCHAR(64) NOT NULL, id CHAR(32) NOT NULL UNIQUE, created BIGINT NOT NULL, grant_items TEXT NOT NULL, PRIMARY KEY(campaign,owner)',
            'market_gift_notices'=>'campaign CHAR(32) NOT NULL, owner VARCHAR(64) NOT NULL, PRIMARY KEY(campaign,owner)',
            'market_gift_balances'=>'campaign CHAR(32) PRIMARY KEY, balances TEXT NOT NULL',
        ] as $name=>$fields)$this->db->exec("CREATE TABLE IF NOT EXISTS $name ($fields)$engine");
    }
    private function campaignBalances(string $id):array {
        return json_decode($this->run('SELECT balances FROM market_gift_balances WHERE campaign=?',[$id])->fetchColumn()?:'[]',true,512,JSON_THROW_ON_ERROR);
    }
    private function giftResourceOptions():array {
        $rows=[['kind'=>'gold','code'=>'gld','label'=>'Gold','quality'=>2,'index'=>-1,'ethereal'=>-1,'quantity'=>1]];
        $raw=$this->run('SELECT definition FROM market_catalog_families WHERE `mod`=?',[$this->mod])->fetchColumn();$definition=$raw?json_decode($raw,true,512,JSON_THROW_ON_ERROR):[];
        $names=['GB_Total'=>'Gem points','RB_Low'=>'Low rune points','RB_Mid'=>'Mid rune points','RB_High'=>'High rune points','RB_Ultra'=>'Ultra rune points','JB_Magic'=>'Magic jewel points','JB_Rare'=>'Rare jewel points','PB_Rejuv'=>'Full rejuvenation potions','KB_Terror'=>'Keys of Terror','KB_Hate'=>'Keys of Hate','KB_Dest'=>'Keys of Destruction','OB_Diablo'=>'Horns','OB_Baal'=>'Eyes','OB_Meph'=>'Brains','score_tracker'=>'Set cores','ucore_tracker'=>'Unique cores'];
        foreach($names as $stat=>$label)if(isset($definition['bags'][$stat]))$rows[]=['kind'=>'bag','code'=>'Z01','stat'=>$stat,'label'=>$label,'quality'=>2,'index'=>-1,'ethereal'=>-1,'quantity'=>1];
        return $rows;
    }
    private function giftBalances(mixed $input):array {
        if($input===[])return [];$rows=self::price($input);$allowed=[];
        foreach($this->giftResourceOptions() as $r)$allowed[$r['kind'].'|'.($r['stat']??'')]=$r;
        foreach($rows as &$r){$key=($r['kind']??'item').'|'.($r['stat']??'');if(!isset($allowed[$key]))throw new MarketError(422,'Choose gold or a bag currency supported by this mod.');$r=array_replace($allowed[$key],['quantity'=>$r['quantity']]);}unset($r);
        return $rows;
    }
    private function giftDefinition():array {
        $raw=$this->run('SELECT definition FROM market_catalog_families WHERE `mod`=?',[$this->mod])->fetchColumn();
        if(!$raw)throw new MarketError(409,'Connect the current mod through Pandemonium before creating gifts. Its verified save definitions are required.');
        return json_decode($raw,true,512,JSON_THROW_ON_ERROR);
    }
    private function campaignTemplate(string $owner,string $scope,string $id,array $definition):array {
        if(str_starts_with($id,'market:')){
            $key=self::digest(substr($id,7));$meta=null;
            $profile=$this->run('SELECT settings,inventory FROM web_profiles WHERE owner=? AND scope=?',[$owner,$scope])->fetch();
            $settings=$profile?json_decode($profile['settings'],true,512,JSON_THROW_ON_ERROR):null;
            if($profile&&$settings)foreach(json_decode($profile['inventory'],true,512,JSON_THROW_ON_ERROR) as $row)if($row['key']===$key&&$row['page']===$settings['page'])$meta=$row;
            if(!$meta||$this->run('SELECT source_key FROM custody_sources WHERE owner=? AND scope=? AND source_key=?',[$owner,$scope,$key])->fetch())throw new MarketError(409,'This market-page item changed or entered the Vault. Refresh and choose it again.');
            if($meta['price']!==[])throw new MarketError(409,'Choose an unpriced market-page item or an available Vault item.');
            foreach($this->run("SELECT packet FROM web_intents WHERE owner=? AND scope=? AND kind='price' AND status='queued'",[$owner,$scope])->fetchAll() as $intent){$packet=json_decode($intent['packet'],true,512,JSON_THROW_ON_ERROR);if($packet['key']===$key&&$packet['price']!==[])throw new MarketError(409,'An asking price is queued for this item. Refresh and choose an unpriced item.');}
            unset($meta['price']);$meta=$this->custodyItem($meta);$hex=$meta['item_hex'];$source='';
            $item=$this->browserItemNames($meta,$hex);unset($item['item_hex'],$item['quantity_bit']);
        }else{
            $id=self::ticketId($id);
            $r=$this->run("SELECT e.metadata,i.item_hex,i.code,i.quality,i.item_index FROM escrow_items e JOIN items i ON i.id=e.item WHERE e.item=? AND e.owner=? AND i.owner=? AND e.scope=? AND e.state IN ('available','listed')",[$id,$owner,$owner,$scope])->fetch();
            if(!$r)throw new MarketError(409,'Choose available items from your market page or Vault in this mod and mode.');
            $meta=json_decode($r['metadata'],true,512,JSON_THROW_ON_ERROR);$hex=$r['item_hex'];$source=$id;$item=$this->statsItem($id);
        }
        if(($meta['delivery_requested']??false)||!($meta['identified']??false))throw new MarketError(409,'Templates must be identified and must not be queued for delivery.');
        try{$probe=GiftItemFactory::copy($hex,$definition);}catch(RuntimeException $e){throw new MarketError(422,'This gift template cannot be copied safely: '.$e->getMessage());}
        if($probe['quantity']!==$meta['quantity'])throw new MarketError(422,'Gift stack metadata does not match its saved quantity.');
        return ['source'=>$source,'market_key'=>$key??null,'hex'=>$hex,'metadata'=>$meta,'item'=>$item];
    }
    public function ownerGiftCampaigns(array $actor,bool $hc,?string $id=null):array {
        $this->requireOwner($actor);$scope=$this->scope($hc);
        $rows=$this->run('SELECT * FROM market_gift_campaigns WHERE scope=?'.($id!==null?' AND id=?':'').' ORDER BY created DESC,id DESC LIMIT 64',$id!==null?[$scope,self::ticketId($id)]:[$scope])->fetchAll();$out=[];
        foreach($rows as $r){$g=$this->campaignView($r,$actor,false,true);$g['claim_message']=$r['claim_message'];$g['claims']=(int)$this->run('SELECT COUNT(*) FROM market_gift_claims WHERE campaign=?',[$r['id']])->fetchColumn();
            if($id!==null){$g['recent_claims']=[];foreach($this->run('SELECT * FROM market_gift_claims WHERE campaign=? ORDER BY created DESC,id DESC LIMIT 100',[$id])->fetchAll() as $c){$assets=[];foreach(json_decode($c['grant_items'],true,512,JSON_THROW_ON_ERROR) as $item){$e=$this->run('SELECT state,metadata FROM escrow_items WHERE item=?',[$item])->fetch();$delivered=$this->run("SELECT delivered_at,delivery_receipt FROM market_trade_assets WHERE receipt=? AND item=?",[$c['id'],$item])->fetch();$assets[]=['item_id'=>$item,'item'=>$this->statsItem($item),'state'=>$e['state']??'unknown','delivery_requested'=>$e?(bool)(json_decode($e['metadata'],true)['delivery_requested']??false):false,'delivery'=>$delivered];}$g['recent_claims'][]=['id'=>$c['id'],'player'=>$this->auditPlayer($c['owner']),'at'=>(int)$c['created'],'items'=>$assets,'balances'=>$g['balances']];}}
            $out[]=$g;
        }
        return ['campaigns'=>$out,'resource_options'=>$this->giftResourceOptions(),'mod'=>$this->mod,'game_mode'=>'2.4','hardcore'=>$hc];
    }
    public function campaignOwnerCommand(array $actor,array $data,bool $hc):array {
        return $this->controlledRequest($actor,'owner',$data,$hc,function()use($actor,$data,$hc){
            $this->lockScope($hc);$scope=$this->scope($hc);$id=self::ticketId($data['id']??null);$hash=hash('sha256',$scope.'|'.self::json($data));
            $old=$this->run('SELECT hash,result FROM market_owner_actions WHERE id=? AND owner=?',[$id,$actor['id']])->fetch();
            if($old){if(!hash_equals($old['hash'],$hash))throw new MarketError(409,'Gift campaign action ID reused.');return json_decode($old['result'],true,512,JSON_THROW_ON_ERROR);}
            if($this->controlState()['phase']!=='live')throw new MarketError(409,'Finish the marketplace reset before changing gift campaigns.');
            $kind=$data['type']??'';
            if($kind==='campaign_create'){
                if((int)$this->run("SELECT COUNT(*) FROM market_gift_campaigns WHERE scope=? AND status='active' AND (expires IS NULL OR expires>?)",[$scope,time()])->fetchColumn()>=8)throw new MarketError(422,'Close an existing gift campaign before adding another.');
                $title=trim(self::text($data['title']??null,80));if($title==='')throw new MarketError(422,'Give the gift a title.');
                $message=($data['message']??'')!==''?self::message($data['message']):'';
                $announcement=($data['announcement']??'')!==''?self::message($data['announcement']):'A new community gift is available. Claim it once from Marketplace → Gifts.';
                if(strlen($announcement)>512)throw new MarketError(422,'Keep the announcement within 512 bytes.');
                $expires=$data['expires']??null;if($expires!==null)$expires=self::number($expires,time()+1,time()+5*365*86400);
                $ids=$data['items']??[];if(!is_array($ids)||!array_is_list($ids)||count($ids)>16||count(array_filter($ids,'is_string'))!==count($ids)||count(array_unique($ids))!==count($ids))throw new MarketError(422,'Choose up to 16 different market-page or Vault item templates.');
                $balances=$this->giftBalances($data['balances']??[]);if(!$ids&&!$balances)throw new MarketError(422,'Add at least one item, gold amount or bag currency to the gift.');
                $definition=$ids?$this->giftDefinition():[];$templates=[];$bytes=0;
                foreach($ids as $item){$t=$this->campaignTemplate($actor['id'],$scope,$item,$definition);$bytes+=strlen($t['hex'])/2;$templates[]=$t;}
                if($bytes>131072)throw new MarketError(422,'This gift bundle exceeds the safe template size.');
                $this->run('INSERT INTO market_gift_campaigns VALUES (?,?,?,?,?,?,?,?,?,?)',[$id,$scope,$actor['id'],$title,$announcement,$message,$expires,time(),'active',self::json($templates)]);
                if($balances)$this->run('INSERT INTO market_gift_balances VALUES (?,?)',[$id,self::json($balances)]);
                $result=['campaign_id'=>$id,'status'=>'active','items'=>count($templates),'balances'=>$balances,'expires'=>$expires];
            }elseif($kind==='campaign_close'){
                $key=self::ticketId($data['campaign_id']??null);$r=$this->run('SELECT id FROM market_gift_campaigns WHERE id=? AND scope=?'.$this->lock(),[$key,$scope])->fetch();if(!$r)throw new MarketError(404,'Gift campaign not found in this mod and mode.');
                $this->run("UPDATE market_gift_campaigns SET status='closed' WHERE id=?",[$key]);$result=['campaign_id'=>$key,'status'=>'closed'];
            }else throw new MarketError(422,'Unknown gift campaign control.');
            $this->run('INSERT INTO market_owner_actions VALUES (?,?,?,?,?,?)',[$id,$actor['id'],$kind,$hash,self::json($result),time()]);return $result;
        });
    }
    private function campaignView(array $r,?array $actor,bool $native=false,bool $owner=false):array {
        $claim=$actor?$this->run('SELECT id,created,grant_items FROM market_gift_claims WHERE campaign=? AND owner=?',[$r['id'],$actor['id']])->fetch():false;
        $items=[];foreach(json_decode($r['templates'],true,512,JSON_THROW_ON_ERROR) as $t){$item=$t['item'];if($native)$item['item_hex']=$t['hex'];$items[]=$item;}
        $active=$r['status']==='active'&&($r['expires']===null||(int)$r['expires']>time());
        $linked=$actor&&(bool)$this->run("SELECT owner FROM market_memberships WHERE owner=? AND `mod`=? AND game_mode='2.4' AND linked>0",[$actor['id'],$this->mod])->fetchColumn();
        $balances=$this->campaignBalances($r['id']);
        return ['id'=>$r['id'],'title'=>$r['title'],'announcement'=>$r['announcement'],'owner'=>$this->auditPlayer($r['owner']),'items'=>$items,'balances'=>$balances,'created'=>(int)$r['created'],'expires'=>$r['expires']===null?null:(int)$r['expires'],'status'=>$active?'active':($r['status']==='closed'?'closed':'expired'),'claimed'=>(bool)$claim,'claimable'=>$active&&!$claim&&$linked,'claim'=>$claim?['id'=>$claim['id'],'at'=>(int)$claim['created'],'items'=>json_decode($claim['grant_items'],true),'balances'=>$balances,'message'=>$r['claim_message']]:null];
    }
    public function giftCampaigns(?array $actor,bool $hc,bool $native=false):array {
        $rows=$this->run("SELECT * FROM market_gift_campaigns WHERE scope=? AND status='active' AND (expires IS NULL OR expires>?) ORDER BY created DESC,id DESC LIMIT 8",[$this->scope($hc),time()])->fetchAll();
        if($native&&$actor)$rows=array_values(array_filter($rows,function($r)use($actor){$templates=json_decode($r['templates'],true,512,JSON_THROW_ON_ERROR);$balances=$this->campaignBalances($r['id']);
            if($balances&&!in_array('gift_resources',json_decode($this->run('SELECT features FROM market_browser_features WHERE owner=? AND scope=?',[$actor['id'],$r['scope']])->fetchColumn()?:'[]',true),true))return false;
            foreach($balances as $b)if(!$this->supportsBalance($actor['id'],$b))return false;
            foreach($templates as $t)if(!$this->supportsItemMetadata($actor['id'],$t['metadata']['code'],$t['metadata']['display']??[]))return false;return true;}));
        return array_map(fn($r)=>$this->campaignView($r,$actor,$native),$rows);
    }
    private function announceGiftCampaigns(array $actor):void {
        if($this->browserActor($actor['id'])['role']!=='player')return;
        foreach([false,true] as $hc){$scope=$this->scope($hc);
            $rows=$this->run("SELECT g.* FROM market_gift_campaigns g JOIN market_memberships m ON m.owner=? AND m.`mod`=? AND m.game_mode='2.4' AND m.linked>0 WHERE g.scope=? AND g.status='active' AND (g.expires IS NULL OR g.expires>?) AND NOT EXISTS (SELECT 1 FROM market_gift_notices n WHERE n.campaign=g.id AND n.owner=?) ORDER BY g.created LIMIT 8",[$actor['id'],$this->mod,$scope,time(),$actor['id']])->fetchAll();
            foreach($rows as $r)$this->transaction(function()use($actor,$r,$hc,$scope){
                $this->run('SELECT id FROM actors WHERE id=?'.$this->lock(),[$actor['id']])->fetch();if($this->run('SELECT campaign FROM market_gift_notices WHERE campaign=? AND owner=?',[$r['id'],$actor['id']])->fetch())return;
                $this->run('INSERT INTO market_gift_notices VALUES (?,?)',[$r['id'],$actor['id']]);
                $body='[Community gift] '.$r['title'].' · '.$this->mod.' 2.4 '.($hc?'Hardcore':'Softcore').' — '.$r['announcement'];
                $this->run('INSERT INTO market_messages (id,sender,recipient,scope,kind,thread,body,created) VALUES (?,?,?,?,?,?,?,?)',[hash('md5','gift-announcement|'.$r['id'].'|'.$actor['id']),$r['owner'],$actor['id'],$scope,'trade','',$body,time()]);
            });
        }
    }
    private function claimGiftCampaign(array $actor,array $data,bool $hc):array {
        $this->requireMod($actor['id']);if($this->browserActor($actor['id'])['role']!=='player')throw new MarketError(403,'Link a real player account to claim a gift.');
        if(!$this->run("SELECT owner FROM market_memberships WHERE owner=? AND `mod`=? AND game_mode='2.4' AND linked>0",[$actor['id'],$this->mod])->fetchColumn())throw new MarketError(403,'Link your player code for this mod before claiming gifts.');
        $id=self::ticketId($data['id']??null);$campaign=self::ticketId($data['campaign_id']??null);$scope=$this->scope($hc);$hash=hash('sha256',$scope.'|'.self::json($data));
        return $this->transaction(function()use($actor,$data,$hc,$id,$campaign,$scope,$hash){
            $this->lockScope($hc);$this->run('SELECT id FROM actors WHERE id=?'.$this->lock(),[$actor['id']])->fetch();
            $old=$this->run('SELECT hash,result FROM commands WHERE actor=? AND id=?',[$actor['id'],$id])->fetch();if($old){if(!hash_equals($old['hash'],$hash))throw new MarketError(409,'Claim command ID reused.');return json_decode($old['result'],true,512,JSON_THROW_ON_ERROR);}
            $r=$this->run('SELECT * FROM market_gift_campaigns WHERE id=? AND scope=?'.$this->lock(),[$campaign,$scope])->fetch();if(!$r)throw new MarketError(404,'This gift belongs to another mod or mode, or is unavailable.');
            $claimed=$this->run('SELECT id,grant_items FROM market_gift_claims WHERE campaign=? AND owner=?',[$campaign,$actor['id']])->fetch();
            $balances=$this->campaignBalances($campaign);
            if($claimed)$result=['campaign_id'=>$campaign,'claim_id'=>$claimed['id'],'claimed'=>true,'already_claimed'=>true,'items'=>json_decode($claimed['grant_items'],true),'balances'=>$balances,'message'=>$r['claim_message']];
            else{
                if($r['status']!=='active'||$r['expires']!==null&&(int)$r['expires']<=time())throw new MarketError(409,'This gift has expired or claiming has ended.');
                $templates=json_decode($r['templates'],true,512,JSON_THROW_ON_ERROR);$definition=$templates?$this->giftDefinition():[];$items=[];
                foreach($balances as $b)if(!$this->supportsBalance($actor['id'],$b))throw new MarketError(409,'Update this mod before claiming these bag currencies. Your claim has not been used.');
                foreach($templates as $t){
                    $copy=GiftItemFactory::copy($t['hex'],$definition);$meta=$t['metadata'];unset($meta['ledger_id']);$meta['item_hex']=$copy['hex'];$meta['saved_id']=$copy['seed'];$meta['item_uuid']=$copy['uuid'];$meta['quantity_bit']=$copy['quantity_bit'];$meta['price']=[];$meta['delivery_requested']=true;$meta['gift_campaign']=$campaign;
                    $item=self::id();$meta['ledger_id']=$item;
                    $this->identityAdmission($actor['id'],$scope,$copy['hex']);
                    $this->run('INSERT INTO items VALUES (?,?,?,?,?,?,?,?,?,?)',[$item,$scope,$copy['seed'],$meta['code'],$meta['quality'],$meta['index'],$meta['ethereal'],$meta['sockets'],$actor['id'],$copy['hex']]);
                    $this->run('INSERT INTO escrow_items VALUES (?,?,?,?,?,?)',[$item,$actor['id'],$scope,'available',self::json($meta),$id]);
                    if(!$this->supportsEscrowItem($actor['id'],$item))throw new MarketError(409,'Update this mod before claiming the gift. Your claim has not been used.');
                    $this->recordIdentity($item,$scope,$copy['hex']);$this->recordTradeAsset($id,$scope,'campaign',$r['owner'],$actor['id'],$item,[],$t['source']);
                    $this->event($item,null,['id'=>$r['owner']],'campaign_claim','Community gift: '.$r['title'],self::freePrice(),$r['owner'],$actor['id'],$id.'|'.$item);$items[]=$item;
                }
                foreach($balances as $b){$this->balance($actor['id'],$scope,$b,$b['quantity'],'campaign_claim',$id);$this->recordTradeAsset($id,$scope,'campaign',$r['owner'],$actor['id'],null,$b);}
                $this->run('INSERT INTO market_gift_claims VALUES (?,?,?,?,?)',[$campaign,$actor['id'],$id,time(),self::json($items)]);
                $body='[Gift claimed] '.$r['title'].' — '.count($items).' item(s) queued for your market page.'.($balances?' Added to Vault: '.implode(' + ',array_map(fn($b)=>$b['quantity'].' × '.$b['label'],$balances)).'.':'').($r['claim_message']!==''?' '.$r['claim_message']:'');
                $this->run('INSERT INTO market_messages (id,sender,recipient,scope,kind,thread,body,created) VALUES (?,?,?,?,?,?,?,?)',[hash('md5','gift-claim|'.$campaign.'|'.$actor['id']),$r['owner'],$actor['id'],$scope,'trade','',$body,time()]);
                $result=['campaign_id'=>$campaign,'claim_id'=>$id,'claimed'=>true,'already_claimed'=>false,'items'=>$items,'balances'=>$balances,'message'=>$r['claim_message']];
            }
            $this->run('INSERT INTO commands VALUES (?,?,?,?)',[$actor['id'],$id,$hash,self::json($result)]);return $result;
        });
    }
}
