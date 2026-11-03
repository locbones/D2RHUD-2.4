<?php
declare(strict_types=1);

trait MarketplaceModeration {
    private const REPORT_REASONS=['modified_item','duplicated_items','scam','abuse','other'];
    private function moderationSchema(string $engine): void {
        foreach([
            'market_moderators'=>'community VARCHAR(64) NOT NULL, owner VARCHAR(64) NOT NULL, granted_by VARCHAR(64) NOT NULL, created BIGINT NOT NULL, PRIMARY KEY(community,owner)',
            'market_reports'=>($engine?'seq BIGINT AUTO_INCREMENT PRIMARY KEY':'seq INTEGER PRIMARY KEY AUTOINCREMENT').', id CHAR(32) NOT NULL UNIQUE, reporter VARCHAR(64) NOT NULL, subject VARCHAR(64) NOT NULL, scope VARCHAR(255) NOT NULL, reason VARCHAR(32) NOT NULL, details TEXT NOT NULL, evidence MEDIUMTEXT NOT NULL, dedupe_key CHAR(64) NOT NULL, status VARCHAR(16) NOT NULL, revision INT NOT NULL, created BIGINT NOT NULL, updated BIGINT NOT NULL',
            'market_report_events'=>($engine?'seq BIGINT AUTO_INCREMENT PRIMARY KEY':'seq INTEGER PRIMARY KEY AUTOINCREMENT').', id CHAR(32) NOT NULL UNIQUE, report CHAR(32) NOT NULL, actor VARCHAR(64) NOT NULL, actor_name VARCHAR(128) NOT NULL, kind VARCHAR(16) NOT NULL, note TEXT NOT NULL, at BIGINT NOT NULL',
        ] as $table=>$columns)$this->db->exec("CREATE TABLE IF NOT EXISTS $table ($columns)$engine");
    }
    // Operator CLI only. A capability grant deliberately preserves the player's
    // trading role, pairing, tokens, ownership and existing browser session.
    public function grantModerator(string $owner): array {
        return $this->transaction(function()use($owner){
            $actor=$this->run("SELECT id,name FROM actors WHERE id=? AND role='player'".$this->lock(),[$owner])->fetch();
            if(!$actor)throw new MarketError(404,'Choose an existing player by stable player ID.');
            $insert=$this->db->getAttribute(PDO::ATTR_DRIVER_NAME)==='mysql'?'INSERT IGNORE':'INSERT OR IGNORE';
            $this->run("$insert INTO market_moderators VALUES (?,?,?,?)",[$this->community,$owner,'operator',time()]);
            return ['moderator'=>true,'player'=>$actor];
        });
    }
    private function isModerator(string $owner): bool {
        return (bool)$this->run('SELECT owner FROM market_moderators WHERE community=? AND owner=?',[$this->community,$owner])->fetchColumn();
    }
    private function requireModerator(array $actor): void {
        if(!$this->isModerator($actor['id']))throw new MarketError(403,'Moderator access is required.');
    }
    public function moderationCapabilities(?array $actor): array {
        $allowed=$actor&&$this->isModerator($actor['id']);
        return ['moderator'=>(bool)$allowed,'open_count'=>$allowed?(int)$this->run("SELECT COUNT(*) FROM market_reports WHERE scope LIKE ? AND status IN ('open','under_review')",[$this->community.'|%'])->fetchColumn():0];
    }
    private function reportItemEvidence(array $r): array {
        $native=$this->itemIdentity($r['item_hex']);$metadata=$r['metadata']?json_decode($r['metadata'],true,512,JSON_THROW_ON_ERROR):[];
        $item=['ledger_id'=>$r['item_id'],'code'=>$r['code'],'quality'=>(int)$r['quality'],'index'=>(int)$r['item_index'],'saved_id'=>(int)$r['seed'],'item_uuid'=>$native['item_uuid']??'',
            'ethereal'=>(bool)$r['ethereal'],'sockets'=>(int)$r['sockets'],'quantity'=>$metadata['quantity']??1,'identified'=>$metadata['identified']??true,
            'display'=>$metadata['display']??($r['discovery']?json_decode($r['discovery'],true,512,JSON_THROW_ON_ERROR):null)];
        return $this->browserItemNames($item,$r['item_hex']);
    }
    private function reportEvidence(array $subject,array $data,bool $hc): array {
        $scope=$this->scope($hc);$e=['captured_at'=>time(),'scope'=>$scope,'player'=>['id'=>$subject['id'],'name'=>$subject['name']],'source'=>'player'];
        if(isset($data['listing_id'])){
            $id=self::ticketId($data['listing_id']);
            $r=$this->run('SELECT l.id,l.seller,l.revision,l.status,l.price,i.id AS item_id,i.scope,i.code,i.quality,i.item_index,i.seed,i.ethereal,i.sockets,i.item_hex,e.metadata,d.metadata AS discovery FROM listings l JOIN items i ON i.id=l.item LEFT JOIN escrow_items e ON e.item=i.id LEFT JOIN web_item_display d ON d.item=i.id WHERE l.id=?',[$id])->fetch();
            if(!$r||$r['scope']!==$scope||$r['seller']!==$subject['id'])throw new MarketError(404,'Trade not found for this player and game mode.');
            if((int)$r['revision']!==self::number($data['listing_revision']??null,1,2147483647))throw new MarketError(409,'This trade changed. Refresh it before reporting.');
            $e['source']='listing';$e['listing']=['id'=>$id,'revision'=>(int)$r['revision'],'status'=>$r['status'],'price'=>json_decode($r['price'],true,512,JSON_THROW_ON_ERROR)];
            $e['item']=$this->reportItemEvidence($r);
            $e['sales']=$this->run("SELECT id,at,price,seller,buyer,receipt FROM events WHERE item=? AND kind='completed_sale' ORDER BY seq DESC LIMIT 50",[$r['item_id']])->fetchAll();
            foreach($e['sales'] as &$sale){$sale['at']=(int)$sale['at'];$sale['price']=json_decode($sale['price'],true,512,JSON_THROW_ON_ERROR);}unset($sale);
            $key=$this->run('SELECT identity_key FROM item_identities WHERE item=?',[$r['item_id']])->fetchColumn();
            $e['identity_matches']=$key?$this->run('SELECT n.item,i.owner,a.name AS owner_name FROM item_identities n JOIN items i ON i.id=n.item JOIN actors a ON a.id=i.owner WHERE n.scope=? AND n.identity_key=? ORDER BY n.item LIMIT 50',[$scope,$key])->fetchAll():[];
        }elseif(isset($data['character_key'])){
            $key=self::digest($data['character_key']);$r=$this->run('SELECT snapshot,updated FROM market_characters WHERE owner=? AND scope=? AND character_key=?',[$subject['id'],$scope,$key])->fetch();
            if(!$r)throw new MarketError(404,'Character snapshot not found in this game mode.');
            if((int)$r['updated']!==self::number($data['character_updated']??null,0,PHP_INT_MAX))throw new MarketError(409,'The character snapshot changed. Refresh it before reporting.');
            $c=json_decode($r['snapshot'],true,512,JSON_THROW_ON_ERROR);$e['source']='character';
            $e['character']=['key'=>$key,'name'=>$c['name'],'class_id'=>$c['class_id'],'level'=>$c['level'],'saved_at'=>$c['saved_at'],'updated'=>(int)$r['updated']];
            if(isset($data['item_slot'])){
                $slot=$data['item_slot'];if(!is_array($slot))throw new MarketError(422,'Invalid item selector.');$kind=$slot['kind']??'';if(!in_array($kind,['equipment','inventory','mercenary'],true))throw new MarketError(422,'Choose an item from this character snapshot.');
                $index=self::number($slot['index']??null,0,511);$rows=$kind==='mercenary'?$c['mercenary']['equipment']:$c[$kind];
                if(!isset($rows[$index]))throw new MarketError(409,'The selected item is no longer in this snapshot.');
                $e['item']=$rows[$index];$e['item_slot']=['kind'=>$kind,'index'=>$index];
            }
        }
        return $e;
    }
    private function moderationCommand(array $actor,array $data,bool $hc): array {
        if(($data['type']??'')==='review_report')$this->requireModerator($actor);
        else $this->requireMod($actor['id']);
        return $this->transaction(function()use($actor,$data,$hc){
            $owner=$actor['id'];$current=$this->run('SELECT name FROM actors WHERE id=?'.$this->lock(),[$owner])->fetchColumn();
            $id=self::ticketId($data['id']??null);$hash=hash('sha256',$this->scope($hc).'|'.self::json($data));
            $old=$this->run('SELECT hash,result FROM commands WHERE actor=? AND id=?',[$owner,$id])->fetch();
            if($old){if(!hash_equals($old['hash'],$hash))throw new MarketError(409,'Report command ID reused.');return json_decode($old['result'],true,512,JSON_THROW_ON_ERROR);}
            if($data['type']==='report_player'){
                if($actor['role']!=='player'||!$this->run("SELECT owner FROM market_memberships WHERE owner=? AND `mod`=? AND game_mode='2.4' AND linked>0",[$owner,$this->mod])->fetchColumn())throw new MarketError(403,'Link your player code before reporting.');
                $subject=self::text($data['subject']??null,64);$target=$this->run("SELECT id,name FROM actors WHERE id=? AND role IN ('player','tester')",[$subject])->fetch();
                if(!$target||$subject===$owner)throw new MarketError(422,'Choose another marketplace player to report.');
                if(!$this->run("SELECT owner FROM market_memberships WHERE owner=? AND `mod`=? AND game_mode='2.4'",[$subject,$this->mod])->fetchColumn())throw new MarketError(404,'Player not found in this mod.');
                $reason=$data['reason']??'';if(!in_array($reason,self::REPORT_REASONS,true))throw new MarketError(422,'Choose a report reason.');
                $details=trim(self::message($data['details']??null));if(strlen($details)<10)throw new MarketError(422,'Describe what you observed in at least 10 characters.');
                if((int)$this->run('SELECT COUNT(*) FROM market_reports WHERE reporter=? AND created>?',[$owner,time()-900])->fetchColumn()>=5||(int)$this->run('SELECT COUNT(*) FROM market_reports WHERE reporter=? AND created>?',[$owner,time()-86400])->fetchColumn()>=30)throw new MarketError(429,'You have sent several reports recently. Please wait before submitting another.');
                $evidence=$this->reportEvidence($target,$data,$hc);
                $key=hash('sha256',$this->scope($hc).'|'.$subject.'|'.($data['listing_id']??$data['character_key']??'player').'|'.self::json($data['item_slot']??[]));
                if($this->run("SELECT id FROM market_reports WHERE reporter=? AND dedupe_key=? AND status IN ('open','under_review')",[$owner,$key])->fetchColumn())throw new MarketError(409,'You already have an open report for this player or item.');
                $this->run('INSERT INTO market_reports (id,reporter,subject,scope,reason,details,evidence,dedupe_key,status,revision,created,updated) VALUES (?,?,?,?,?,?,?,?,?,?,?,?)',[$id,$owner,$subject,$this->scope($hc),$reason,$details,self::json($evidence),$key,'open',1,time(),time()]);
                $this->run('INSERT INTO market_report_events (id,report,actor,actor_name,kind,note,at) VALUES (?,?,?,?,?,?,?)',[self::id(),$id,$owner,$current,'submitted','',time()]);
                $result=['report_id'=>$id,'status'=>'open'];
            }else{
                $report=self::ticketId($data['report_id']??null);$r=$this->run('SELECT * FROM market_reports WHERE id=?'.$this->lock(),[$report])->fetch();
                if(!$r||!str_starts_with($r['scope'],$this->community.'|'))throw new MarketError(404,'Report not found.');
                if((int)$r['revision']!==self::number($data['expected_revision']??null,1,2147483647))throw new MarketError(409,'Another review changed this report. Refresh before saving.');
                $status=$data['status']??'';$note=$data['note']??'';if(!is_string($note))throw new MarketError(422,'Invalid review note.');$note=trim($note);if($note!=='')self::message($note);
                if(!in_array($status,['open','under_review','resolved','dismissed'],true))throw new MarketError(422,'Choose a review status.');
                if($note===''&&($status===$r['status']||in_array($status,['resolved','dismissed'],true)))throw new MarketError(422,'Add a private note explaining this review.');
                $this->run('UPDATE market_reports SET status=?,revision=revision+1,updated=? WHERE id=?',[$status,time(),$report]);
                $this->run('INSERT INTO market_report_events (id,report,actor,actor_name,kind,note,at) VALUES (?,?,?,?,?,?,?)',[self::id(),$report,$owner,$current,$status===$r['status']?'note':$status,$note,time()]);
                $result=['report_id'=>$report,'status'=>$status,'revision'=>(int)$r['revision']+1];
            }
            $this->run('INSERT INTO commands VALUES (?,?,?,?)',[$owner,$id,$hash,self::json($result)]);return $result;
        });
    }
    public function moderationQueue(array $actor,array $query=[]): array {
        $this->requireModerator($actor);$status=$query['status']??'active';
        if(!in_array($status,['active','all','open','under_review','resolved','dismissed'],true))throw new MarketError(422,'Invalid report filter.');
        $before=isset($query['before'])?self::number($query['before'],1,PHP_INT_MAX):PHP_INT_MAX;$args=[$this->community.'|%',$before];$where='r.scope LIKE ? AND r.seq<?';
        if($status==='active')$where.=" AND r.status IN ('open','under_review')";
        elseif($status!=='all'){$where.=' AND r.status=?';$args[]=$status;}
        $q=$query['q']??'';if(!is_string($q))throw new MarketError(422,'Invalid report search.');$q=trim($q);if($q!==''){self::text($q,64);$where.=" AND (LOWER(a.name) LIKE ? ESCAPE '!' OR r.id LIKE ? ESCAPE '!')";$like='%'.str_replace(['!','%','_'],['!!','!%','!_'],strtolower($q)).'%';$args[]=$like;$args[]=$like;}
        $rows=$this->run("SELECT r.seq,r.id,r.subject,r.reporter,r.scope,r.reason,r.details,r.status,r.revision,r.created,r.updated,a.name AS subject_name,b.name AS reporter_name FROM market_reports r JOIN actors a ON a.id=r.subject JOIN actors b ON b.id=r.reporter WHERE $where ORDER BY r.seq DESC LIMIT 21",$args)->fetchAll();
        $more=count($rows)>20;if($more)array_pop($rows);foreach($rows as &$r)foreach(['seq','revision','created','updated'] as $key)$r[$key]=(int)$r[$key];unset($r);
        return ['reports'=>$rows,'next_before'=>$more?(int)end($rows)['seq']:null,'capabilities'=>$this->moderationCapabilities($actor)];
    }
    public function moderationReport(array $actor,string $id): array {
        $this->requireModerator($actor);$id=self::ticketId($id);
        $r=$this->run('SELECT r.*,a.name AS subject_name,b.name AS reporter_name FROM market_reports r JOIN actors a ON a.id=r.subject JOIN actors b ON b.id=r.reporter WHERE r.id=?',[$id])->fetch();
        if(!$r||!str_starts_with($r['scope'],$this->community.'|'))throw new MarketError(404,'Report not found.');
        foreach(['seq','revision','created','updated'] as $key)$r[$key]=(int)$r[$key];$r['evidence']=json_decode($r['evidence'],true,512,JSON_THROW_ON_ERROR);unset($r['dedupe_key']);
        $r['events']=$this->run('SELECT actor,actor_name,kind,note,at FROM market_report_events WHERE report=? ORDER BY seq',[$id])->fetchAll();foreach($r['events'] as &$event)$event['at']=(int)$event['at'];unset($event);return $r;
    }
}
