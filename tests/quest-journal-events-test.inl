void EventTests(const std::filesystem::path& root,const char* cube,const char* config){
    const auto source=ReadText(cube),description=ReadText(config);
    Configure(root,"RMD-MP",source,description);auto rules=Bindings();
    Check(rules->events&&rules->error.empty(),"event definitions unavailable");
    std::vector<std::map<std::string,std::string>> rows;
    {std::istringstream input(source);std::string line;std::getline(input,line);const auto columns=Data::Fields(line);
        while(std::getline(input,line)){const auto cells=Data::Fields(line);std::map<std::string,std::string> row;for(size_t n=0;n<columns.size()&&n<cells.size();++n)row[columns[n]]=cells[n];rows.push_back(std::move(row));}}
    auto rowFor=[&](const char* name){for(size_t n=0;n<rows.size();++n)if(rows[n]["description"]==name)return n;throw std::runtime_error("Activity fixture missing");};
    Check(rules->quests[1].recipes==std::vector<size_t>{rowFor("Remove Gem - Skull")},"skull withdrawal retained a stale recipe ordinal");
    Check(rules->quests[7].recipes==std::vector<size_t>{rowFor("Remove Rune - Amn")},"Amn withdrawal retained a stale recipe ordinal");
    for(const auto& q:rules->quests)if(Data::NamedActivity(q))for(auto row:q.recipes)Check(Data::ActivityRecipe(q,rows.at(row)),"activity binding points to a different operation");
    for(const auto& q:rules->quests){
        Check(!q.trigger.empty()&&q.objectiveRecipes.empty(),"legacy proof objective exposed");
        Check(q.milestone==(q.id==15||q.id==30||q.id==45),"milestone choice changed");
    }
    inventory.pFirstItem=nullptr;auto bytes=Save("Events");Load(hero,client,bytes);CommandAction(2);
    auto& s=sessions.at({&game,hero.dwUnitId});
    Check(s.state.legacyClosed&&!s.blocked,"event journal did not start");
    const auto code=[](const char* text){return ItemCode(text);};
    Crafted(&game,&hero,rules->quests[0].recipes[0],{code("Z01"),code("skz")},{},Outcome::Rejected);
    Crafted(&game,&hero,rules->quests[0].recipes[0],{code("Z01"),code("skz")},{},Outcome::Ambiguous);
    Check(!s.state.evidence&&!s.state.completed,"failed native deposit granted evidence");
    Crafted(&game,&hero,rules->quests[0].recipes[0],{code("Z01"),code("gpv")},{});
    Check(s.state.completed==0,"non-skull gem completed skull deposit");
    Crafted(&game,&hero,rules->quests[0].recipes[0],{code("Z01"),code("skc")},{});
    Check(s.state.completed==1&&!s.state.objectiveReady,"actual skull deposit did not auto-complete");
    Crafted(&game,&hero,rowFor("Remove Gem - Ruby"),{code("Z01"),code("Z06")},{});
    Check(s.state.completed==1,"ruby withdrawal completed skull lesson");
    Crafted(&game,&hero,rules->quests[1].recipes[0],{code("Z01"),code("Z08")},{});
    Check(s.state.completed==2,"actual skull withdrawal did not auto-complete");
    auto missing=source;const auto skullName=missing.find("Remove Gem - Skull");Check(skullName!=missing.npos,"missing skull row");missing.replace(skullName,18,"Removed operation");
    Check(!Data::ParseRules(missing,description)->error.empty(),"missing activity silently retained stale numeric binding");
    // A successful operation can be remembered before its lesson unlocks.
    Crafted(&game,&hero,rules->quests[7].recipes[0],{code("Z01"),code("Z19")},{});
    Check(s.state.evidence&(uint64_t(1)<<7),"future crafting evidence was lost");
    s.state={};s.state.legacyClosed=true;s.state.completed=4;killed.clear();
    for(uint32_t n=0;n<999;++n)Killed(&game,&hero,n,1,0,-1);
    Check(s.state.kills==999&&!s.state.objectiveReady,"kill target completed early");
    Killed(&game,&hero,1,777,0,-1);Check(s.state.kills==999,"mutable monster seed bypassed kill deduplication");
    Killed(&game,&hero,1000,1,0,-1);Check(s.state.kills==1000&&s.state.objectiveReady&&s.state.completed==4,"kill objective or reward readiness failed");
    static int claims=0;claims=0;
    SetRewardClaim(+[](Game*,Unit*,int quest)->bool{Check(quest==5,"wrong automatic reward");++claims;return claims>1;});
    CommandAction(5);Check(s.state.completed==4&&claims==1,"failed reward claim advanced progress");
    CommandAction(5);Check(s.state.completed==5&&claims==2,"reward did not advance once");
    CommandAction(5);Check(claims==2,"reward repeated after completion");
    s.state={};s.state.legacyClosed=true;s.state.completed=6;
    DepositedRune(&game,&hero,code("r10"));Check(s.state.thul&&s.state.completed==6,"rune deposit bypassed Countess");
    Killed(&game,&hero,2000,1,0,6);Check(s.state.completed==7,"Countess and successful Thul deposit did not combine");
    s.state={};s.state.legacyClosed=true;s.state.completed=9;
    auto menu=[&](uint8_t detail){auto packet=Snapshot();Data::CommandPacket c;c.recipient=packet.recipient;c.session=packet.session;c.sequence=packet.sequence;c.action=6;c.detail=detail;Command(&game,&hero,reinterpret_cast<uint8_t*>(&c),sizeof(c));};
    menu(0);menu(1);Check(!s.state.objectiveReady&&!(s.state.evidence&(uint64_t(1)<<9)),"menu visits completed the potion objective");
    const auto potionGate=std::find_if(rules->gates.begin(),rules->gates.end(),[](const auto& gate){return gate.quest==10&&!gate.turnIn;});
    Check(potionGate!=rules->gates.end()&&RecipeObjective(*rules,*potionGate),"potion objective gate missing");
    Check(Begin(&game,&hero,*potionGate),"potion objective refused");Finish(&game,&hero,*potionGate,Outcome::Rejected);Check(!s.state.objectiveReady,"failed potion transmute completed objective");
    Check(Begin(&game,&hero,*potionGate),"potion objective retry refused");Finish(&game,&hero,*potionGate,Outcome::Committed);
    Check(s.state.completed==9&&s.state.objectiveReady&&!Begin(&game,&hero,*potionGate),"successful potion recipe did not unlock exactly one reward");
    static int potionRewards=0;potionRewards=0;SetRewardClaim(+[](Game*,Unit*,int quest){Check(quest==10,"wrong potion quest reward");++potionRewards;return true;});
    CommandAction(5);CommandAction(5);Check(s.state.completed==10&&potionRewards==1,"potion reward repeated or failed to advance");
    for(const int id:{17,24,28,31,33,34}){
        s.state={};s.state.legacyClosed=true;s.state.completed=id-1;
        for(uint8_t detail=0;detail<7;++detail)menu(detail);
        CommandAction(4);Check(s.state.completed==id-1&&!s.state.objectiveReady,"menu/manual acknowledgment bypassed confirmation recipe");
        const auto gate=std::find_if(rules->gates.begin(),rules->gates.end(),[&](const auto& g){return g.quest==id&&!g.turnIn;});
        Check(gate!=rules->gates.end()&&RecipeObjective(*rules,*gate),"menu lesson recipe gate missing");
        Check(Begin(&game,&hero,*gate),"menu lesson cube operation refused");Finish(&game,&hero,*gate,Outcome::Rejected);
        Check(s.state.completed==id-1&&!s.state.objectiveReady,"failed confirmation transmute advanced lesson");
        Check(Begin(&game,&hero,*gate),"confirmation retry refused");Finish(&game,&hero,*gate,Outcome::Committed);
        Check(s.state.completed==id&&!s.state.objectiveReady&&!Begin(&game,&hero,*gate),"confirmation lesson did not automatically advance exactly once");
    }
    // Replay the reported test checkpoint: sixteen completed quests, a real
    // observed Quest 17 transmute, but a manually retained unstarted flag.
    s.state={};s.state.completed=16;s.state.evidence=uint64_t(1)<<16;
    auto simulated=Save("Events",880);Saved(&game,&hero,simulated.data(),simulated.size());Load(hero,client,simulated);
    Check(sessions.at({&game,hero.dwUnitId}).state.legacyClosed,"simulated completed progress remained unstarted");
    Check(Snapshot().completed==17&&!Inspect(&game,&hero).objectiveReady,"recorded Quest 17 recipe did not advance to Quest 18 after normalization");
    auto normalized=Save("Events",881);Saved(&game,&hero,normalized.data(),normalized.size());Load(hero,client,normalized);
    Check(Inspect(&game,&hero).completed==17&&sessions.at({&game,hero.dwUnitId}).state.legacyClosed,"normalized progress did not survive saving");
    s.state={};s.state.evidence=uint64_t(1)<<16;auto freshEvidence=Save("Events",882);
    Saved(&game,&hero,freshEvidence.data(),freshEvidence.size());Load(hero,client,freshEvidence);
    Check(!sessions.at({&game,hero.dwUnitId}).state.legacyClosed&&Snapshot().completed==0,"future observations automatically started a fresh journal");
    // Manual acknowledgment must not bypass a confirmation recipe.
    s.state={};s.state.legacyClosed=true;s.state.completed=16;CommandAction(4);Check(s.state.completed==16,"manual acknowledgment skipped an observed menu");
    std::array<std::array<uint8_t,0x1b4>,2> itemRows{};
    std::array<std::array<uint8_t,0xe4>,2> typeRows{};
    Put(itemRows[0].data(),0x80,code("m27"));Put(itemRows[0].data(),0x126,int16_t(1));Put(typeRows[1].data(),0,code("weap"));
    auto itemTable=itemRows.data();uint64_t itemCount=itemRows.size();auto typeTable=typeRows.data();uint64_t typeCount=typeRows.size();
    bindings[0x1ca0390]=uintptr_t(&itemTable);bindings[0x1ca0398]=uintptr_t(&itemCount);bindings[0x1c9fbf0]=uintptr_t(&typeTable);bindings[0x1c9fbf8]=uintptr_t(&typeCount);
    statue.dwClassId=0;statue.dwAnimMode=0;statueData.nPage=0;statueData.pExtraData.pNextItem=nullptr;statueData.pExtraData.pParentInv=&inventory;inventory.pFirstItem=&statue;
    s.state={};s.state.legacyClosed=true;s.state.completed=10;
    Snapshot();Check(s.state.completed==11,"ordinary owned possession did not complete finding lesson");
    inventory.pFirstItem=nullptr;Snapshot();Check(s.state.completed==11,"moving an item undid character progress");
    s.state={};s.state.legacyClosed=true;s.state.completed=2;s.weapons.clear();
    inventory.pFirstItem=&statue;statue.dwAnimMode=1;Snapshot();Check(s.weapons.contains(statue.dwUnitId),"equipped weapon observation missing");
    bindings[0x2c49c0]=uintptr_t(+[](Game*,int type,uint32_t id)->Unit*{return type==4&&id==statue.dwUnitId?&statue:nullptr;});
    inventory.pFirstItem=nullptr;statue.dwAnimMode=4;
    Snapshot();Snapshot();Check(s.state.completed==2&&s.weapons.contains(statue.dwUnitId),"holding the equipped weapon on the cursor discarded its drop tracking");
    statue.dwAnimMode=3;statueData.pExtraData.pParentInv=nullptr;
    Snapshot();Check(s.state.completed==3,"dropping equipped weapon was not observed");
    auto resetDrop=[&]{s.state={};s.state.legacyClosed=true;s.state.completed=2;s.weapons.clear();inventory.pFirstItem=&statue;statue.dwClassId=0;statue.dwAnimMode=1;statueData.pExtraData.pParentInv=&inventory;};
    resetDrop();Snapshot();statue.dwAnimMode=3;inventory.pFirstItem=nullptr;statueData.pExtraData.pParentInv=nullptr;
    Snapshot();Check(s.state.completed==3,"direct equipped-to-ground drop failed");
    resetDrop();statue.dwAnimMode=0;Snapshot();statue.dwAnimMode=4;Snapshot();statue.dwAnimMode=3;inventory.pFirstItem=nullptr;statueData.pExtraData.pParentInv=nullptr;
    Snapshot();Check(s.state.completed==2,"dropping a never-equipped inventory weapon completed the lesson");
    resetDrop();statue.dwClassId=1;Snapshot();statue.dwAnimMode=3;inventory.pFirstItem=nullptr;statueData.pExtraData.pParentInv=nullptr;
    Snapshot();Check(s.state.completed==2,"dropping equipped non-weapon completed the lesson");
    resetDrop();Snapshot();statue.dwAnimMode=0;Snapshot();Check(s.weapons.empty(),"putting the weapon away retained drop provenance");
    statue.dwAnimMode=3;inventory.pFirstItem=nullptr;statueData.pExtraData.pParentInv=nullptr;
    Snapshot();Check(s.state.completed==2,"dropping a previously stored weapon completed the lesson");
    resetDrop();Snapshot();statue.dwAnimMode=4;inventory.pFirstItem=nullptr;statueData.pExtraData.pParentInv=nullptr;statueData.dwOwnerGUID=hero.dwUnitId;
    Snapshot();Check(s.weapons.contains(statue.dwUnitId),"detached owned cursor weapon lost drop provenance");
    statueData.dwOwnerGUID=hero.dwUnitId+1;Snapshot();Check(s.weapons.empty(),"another player's cursor retained drop provenance");
    statue.dwAnimMode=3;Snapshot();Check(s.state.completed==2,"another player's later drop completed the lesson");
    resetDrop();Snapshot();inventory.pFirstItem=nullptr;const auto oldId=statue.dwUnitId;++statue.dwUnitId;
    Snapshot();Check(s.weapons.empty()&&s.state.completed==2,"deleted weapon was retained or credited");statue.dwUnitId=oldId;
    resetDrop();Snapshot();statue.dwAnimMode=3;inventory.pFirstItem=nullptr;statueData.pExtraData.pParentInv=nullptr;Snapshot();
    s.state.kills=1234;s.state.thul=true;s.state.countess=true;s.state.menus=3;s.state.evidence|=uint64_t(1)<<39;
    auto saved=Save("Events",700);Saved(&game,&hero,saved.data(),saved.size());Removed(&game,&hero);Load(hero,client,saved);
    const auto& restored=sessions.at({&game,hero.dwUnitId}).state;
    Check(restored.kills==1234&&restored.thul&&restored.countess&&restored.menus==3&&(restored.evidence&(uint64_t(1)<<39)),"event evidence or counters did not survive save/reload");
    auto sameText=nlohmann::json::parse(description);sameText["quests"][0]["title"]="New display title";
    Check(Data::ParseRules(source,sameText.dump())->digest==rules->digest,"display-only text invalidated progress");
    sameText["quests"][0]["recipes"][0]=0;Check(Data::ParseRules(source,sameText.dump())->digest!=rules->digest,"changed activity bindings retained old identity");
    Destroyed(&game);inventory.pFirstItem=nullptr;
    auto initial=Save("FirstSave",810);Load(hero,client,initial);
    const auto initialPath=sessions.at({&game,hero.dwUnitId}).path;
    // Record a successful deposit even if setup/import has not been chosen yet.
    Crafted(&game,&hero,rules->quests[0].recipes[0],{code("Z01"),code("skz")},{});
    Check(sessions.at({&game,hero.dwUnitId}).state.evidence&1,"deposit before setup was discarded");
    Check(Inspect(&game,&hero).completed==0,"setup choice was bypassed");CommandAction(2);
    Check(Inspect(&game,&hero).completed==1,"starting journal did not apply observed skull deposit");
    auto firstSave=Save("FirstSave",811);Put(firstSave.data(),0x2c,uint32_t(0));Saved(&game,&hero,firstSave.data(),firstSave.size());
    Check(Inspect(&game,&hero).available&&sessions.at({&game,hero.dwUnitId}).path==initialPath,"first native save timestamp change locked or rekeyed the journal");
    Removed(&game,&hero);Load(hero,client,firstSave);Check(Inspect(&game,&hero).available&&Inspect(&game,&hero).completed==1,"first-save progress did not reload");
    auto oldJournal=[&](const char* name,bool claimed){
        auto oldBytes=Save(name,820);Data::Identity identity;Data::Checkpoint point;Check(Data::Header(oldBytes.data(),oldBytes.size(),identity,point),"legacy fixture header invalid");identity.created=12345;
        if(claimed){point.state.completed=1;point.state.legacyClosed=true;}
        Data::History legacy;legacy.identity=identity;legacy.digest=rules->digest;legacy.checkpoints.push_back(point);
        auto path=directory/(SummonPersistence::Data::Key(identity)+".json");std::string error;Check(Data::AtomicWrite(path,legacy,error),"legacy fixture write failed");return path;
    };
    const auto unusedPath=oldJournal("UnusedTimestamp",false);const auto unusedOriginal=ReadText(unusedPath);
    auto cleared=Save("UnusedTimestamp",821);Put(cleared.data(),0x2c,uint32_t(0));Removed(&game,&hero);Load(hero,client,cleared);
    Check(Inspect(&game,&hero).available&&sessions.at({&game,hero.dwUnitId}).path!=unusedPath&&ReadText(unusedPath)==unusedOriginal,"unused timestamp journal did not migrate safely");
    const auto claimedPath=oldJournal("ClaimedTimestamp",true);const auto claimedOriginal=ReadText(claimedPath);
    auto unmatched=Save("ClaimedTimestamp",821);Put(unmatched.data(),0x2c,uint32_t(0));Removed(&game,&hero);Load(hero,client,unmatched);
    Check(!Inspect(&game,&hero).available&&ReadText(claimedPath)==claimedOriginal,"unmatched claimed legacy progress was reset");
    auto matching=Save("ClaimedTimestamp",820);Removed(&game,&hero);Load(hero,client,matching);
    Check(Inspect(&game,&hero).available&&Inspect(&game,&hero).completed==1,"matching claimed timestamp journal lost progress");
    auto legacyDescription=nlohmann::json::parse(description);for(auto& entry:legacyDescription["quests"])if(ConfirmationQuest(entry["id"].get<int>()))entry["trigger"]="acknowledge";
    const auto legacyRules=Data::ParseRules(source,legacyDescription.dump());
    Check(legacyRules->error.empty()&&rules->previousMenuDigest==legacyRules->digest&&legacyRules->digest!=rules->digest,"Quest 10 compatibility identity is not the exact previous menu definition");
    for(const int completed:{2,9,10,35}){
        const auto name="PotionUpgrade"+std::to_string(completed);auto save=Save(name.c_str(),910+completed);Data::Identity identity;Data::Checkpoint point;
        Check(Data::Header(save.data(),save.size(),identity,point),"potion upgrade fixture header failed");
        point.state.completed=completed;point.state.legacyClosed=true;point.state.objectiveReady=completed==9;point.state.kills=123;point.state.evidence=(uint64_t(1)<<9)|(uint64_t(1)<<39);
        Data::History history;history.identity=identity;history.digest=legacyRules->digest;history.checkpoints.push_back(point);
        const auto path=directory/(SummonPersistence::Data::Key(identity)+".json");std::string error;Check(Data::AtomicWrite(path,history,error),"potion upgrade fixture write failed");const auto original=ReadText(path);
        Removed(&game,&hero);Load(hero,client,save);const auto& upgraded=sessions.at({&game,hero.dwUnitId});
        Check(!upgraded.blocked&&upgraded.state.completed==completed&&upgraded.state.kills==123&&(upgraded.state.evidence&(uint64_t(1)<<39)),"potion migration reset existing progress");
        Check(!upgraded.state.objectiveReady&&bool(upgraded.state.evidence&(uint64_t(1)<<9))==(completed>=10),"potion migration retained old menu eligibility or reset claimed completion");
        auto backup=path;backup+=".recipe-lessons-"+Data::Hex(legacyRules->digest)+"-"+Data::Hex(Data::Hash(original))+".bak";Check(ReadText(backup)==original,"potion migration original backup missing");
    }
    auto potionOnlyDescription=legacyDescription;potionOnlyDescription["quests"][9]["trigger"]="questRecipe";
    const auto potionOnlyRules=Data::ParseRules(source,potionOnlyDescription.dump());
    Check(potionOnlyRules->error.empty()&&rules->previousPotionDigest==potionOnlyRules->digest,"previous potion-only revision identity changed");
    for(const int completed:{9,16,23,27,30,32,33,34}){
        const auto name="MenuUp"+std::to_string(completed);auto save=Save(name.c_str(),1000+completed);Data::Identity identity;Data::Checkpoint point;
        Check(Data::Header(save.data(),save.size(),identity,point),"menu recipe upgrade header failed");
        point.state.completed=completed;point.state.legacyClosed=true;point.state.objectiveReady=true;point.state.evidence=rules->recipeQuestMask;
        Data::History history;history.identity=identity;history.digest=potionOnlyRules->digest;history.checkpoints.push_back(point);std::string error;
        const auto path=directory/(SummonPersistence::Data::Key(identity)+".json");Check(Data::AtomicWrite(path,history,error),"menu recipe upgrade fixture write failed");
        Removed(&game,&hero);Load(hero,client,save);const auto& upgraded=sessions.at({&game,hero.dwUnitId});
        Check(!upgraded.blocked&&upgraded.state.completed==completed&&bool(upgraded.state.evidence&(uint64_t(1)<<9)),("menu recipe migration failed at "+std::to_string(completed)+": "+upgraded.message).c_str());
        Check(upgraded.state.objectiveReady==(completed==9||completed==34),"menu recipe migration reset potion/next-quest readiness or retained old menu readiness");
        if(completed<34)for(const int id:{17,24,28,31,33,34})Check(bool(upgraded.state.evidence&(uint64_t(1)<<(id-1)))==(id<=completed),"menu recipe migration retained unclaimed menu evidence or reset claimed evidence");
    }
    SetRewardClaim(nullptr);Destroyed(&game);
    Check(SupportsMod("RMD-MP")&&SupportsMod("rmd-mp")&&!SupportsMod("")&&!SupportsMod("OtherMod")&&!SupportsMod("RMD-MP-copy"),"mod identity gate failed");
    Configure(root,"OtherMod",source,description);SetAvailable(true);Load(hero,client,saved);
    Check(!Bindings()&&!Inspect(&game,&hero).available&&sessions.empty()&&directory.empty(),"other mod retained quest bindings or created a session");
    Check(Status().find("restricted to the RMD-MP")!=std::string::npos,"other mod status missing");
    Data::CommandPacket foreign;Check(!Command(&game,&hero,reinterpret_cast<uint8_t*>(&foreign),sizeof(foreign)),"other mod intercepted quest commands");
}
