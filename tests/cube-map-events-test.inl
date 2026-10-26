Bytes<0x40> qualityRatios{};
void* __fastcall NativeRatios(int,int,int){return qualityRatios.data();}
void NativeEventQuality(uint8_t* image){
    auto stub=[&](uint32_t rva,auto fn){const uint8_t jump[]{0xff,0x25,0,0,0,0};memcpy(image+rva,jump,6);auto target=reinterpret_cast<uintptr_t>(fn);memcpy(image+rva+6,&target,8);};
    std::array<Bytes<0x1b4>,2> items{};Bytes<0xe4> type{};type[0x15]=1;
    Put(image,0x1ca0390,items.data());Put(image,0x1ca0398,uint64_t(items.size()));Put(image,0x1c9fbf0,type.data());Put(image,0x1c9fbf8,uint64_t(1));
    for(size_t o:{size_t(0),size_t(0x0c),size_t(0x18),size_t(0x24),size_t(0x30),size_t(0x38)}){Put(qualityRatios.data(),o,int(1000));Put(qualityRatios.data(),o+4,int(1));}
    stub(0x1f0110,NativeRatios);FlushInstructionCache(GetCurrentProcess(),image,0x2000000);
    Unit monster{};monster.dwUnitType=1;Bytes<0x18> ratios{};const int denominator=999*128;
    int ordinary=0,boosted=0,sets=0;
    for(uint32_t seed=0;seed<30000;++seed){
        const uint64_t first=uint64_t(seed)*0x6ac690c5u,second=uint64_t(uint32_t(first))*0x6ac690c5u+(first>>32);
        auto roll=[&](Quality fn){Put(&monster,0x28,seed);Put(&monster,0x2c,uint32_t(0));return fn(nullptr,&monster,nullptr,1,0,ratios.data());};
        const int both=roll(reinterpret_cast<Quality>(treasureQuality->code));
        assert((both==7)==(uint32_t(first)%denominator<768));
        assert((both==5)==(uint32_t(first)%denominator>=768 && uint32_t(second)%denominator<768));
        const int normal=roll(Native<Quality>(0x2f8a60));const int unique=roll(reinterpret_cast<Quality>(uniqueQuality->code));const int set=roll(reinterpret_cast<Quality>(setQuality->code));
        assert((normal==7)==(uint32_t(first)%denominator<128));
        assert((unique==7)==(uint32_t(first)%denominator<768));
        const bool expectedSet=uint32_t(first)%denominator>=128 && uint32_t(second)%denominator<768;
        assert((set==5)==expectedSet);ordinary+=normal==7;boosted+=unique==7;sets+=set==5;
    }
    assert(boosted>ordinary*5 && boosted<ordinary*7 && sets>0);
    assert(Field<uint32_t>(uniqueQuality->code,0x274)==768 && Field<uint32_t>(uniqueQuality->code,0x31f)==128);
    assert(Field<uint32_t>(setQuality->code,0x274)==128 && Field<uint32_t>(setQuality->code,0x31f)==768);
    assert(Field<uint32_t>(treasureQuality->code,0x274)==768 && Field<uint32_t>(treasureQuality->code,0x31f)==768);
    DWORD64 runtimeBase=0;assert(RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(setQuality->code+0x300),&runtimeBase,nullptr));
    std::cout<<"Actual native quality RNG: unique/set events use sixfold thresholds, preserve unique priority and stat-generation inputs, and retain unwind metadata.\n";
}
std::array<Unit,2> eventPlayers{};std::array<Client,2> eventClients{};std::array<D2PlayerDataStrc,2> eventPlayerData{};
Unit* __fastcall EventLookup(Game*,int type,uint32_t id){for(auto& p:eventPlayers)if(type==0 && p.dwUnitId==id)return &p;return nullptr;}
Room* __fastcall ParticipantRoom(Unit*){return reinterpret_cast<Room*>(fixture.r1[0].data());}
std::vector<Packet> eventPackets;
void __fastcall CaptureEventPacket(void*,uint32_t,void** range){assert(static_cast<uint8_t*>(range[1])-static_cast<uint8_t*>(range[0])==sizeof(Packet));if(static_cast<uint8_t*>(range[0])[9]==PacketOperation)eventPackets.push_back(*static_cast<Packet*>(range[0]));}
void EventSynchronization(){
    Reset();games.clear();eventPlayers={};eventClients={};eventPlayerData={};eventPackets.clear();view={};receivedAt=0;
    auto game=fixture.GamePtr();game->dwGameFrame=1000;game->dwInitSeed=91;game->pClientList=&eventClients[0];
    Put(fixture.level.data(),0x1f8,int(109));
    for(size_t n=0;n<2;++n){auto& p=eventPlayers[n];p.dwUnitType=0;p.dwUnitId=55+uint32_t(n);p.dwAnimMode=1;p.pPlayerData=&eventPlayerData[n];Put(&eventPlayerData[n],0xf0,&eventClients[n]);auto& c=eventClients[n];c.pGame=game;c.dwClientState=4;c.dwUnitGUID=p.dwUnitId;c.dwClientId=uint32_t(n+1);if(n==0)c.pNext=&eventClients[1];}
    bindings[0x2c49c0]=reinterpret_cast<uintptr_t>(EventLookup);bindings[0x209530]=reinterpret_cast<uintptr_t>(ParticipantRoom);
    bindings[0x2d0130]=reinterpret_cast<uintptr_t>(Nothing);bindings[0x422ac0]=reinterpret_cast<uintptr_t>(CaptureEventPacket);
    auto& state=State(game);state.run=std::make_unique<Run>();auto& run=*state.run;run.id=123;run.event=Event::Trial;run.eventPhase=EventPhase::WaitingTown;run.phase=Phase::Closed;run.eventFrame=1000;run.die=6;
    run.returns[55]={109,10,20};run.returns[56]={109,10,20};
    CommandPacket packet;packet.run=123;packet.session=91;
    auto command=[&](Unit* player){return Command(game,player,reinterpret_cast<uint8_t*>(&packet),sizeof(packet));};
    assert(command(&eventPlayers[0]) && run.ready==std::set<uint32_t>{55});
    assert(ProcessEvent(game,run,state) && run.eventPhase==EventPhase::WaitingTown);packet.run=124;assert(command(&eventPlayers[1]) && !run.ready.contains(56));packet.run=123;
    packet.session=92;assert(command(&eventPlayers[1]) && !run.ready.contains(56));packet.session=91;
    eventClients[1].dwClientState=3;assert(command(&eventPlayers[1]) && !run.ready.contains(56));eventClients[0].dwClientState=3;
    // Both clients crossing acts still belong to this run, even when Players()
    // temporarily has no ready unit. Their run and resources must survive.
    assert(ProcessEvent(game,run,state) && state.run && run.eventPhase==EventPhase::WaitingTown);
    eventClients[0].dwClientState=eventClients[1].dwClientState=4;assert(command(&eventPlayers[1]));
    assert(ProcessEvent(game,run,state) && run.eventPhase==EventPhase::Ceremony);
    for(const auto& p:eventPackets){assert(Data::Valid(p) && p.run==123 && p.event==Event::Trial && p.die==6 && !p.reward);}
    game->dwGameFrame+=CeremonyFrames-1;assert(ProcessEvent(game,run,state) && run.eventPhase==EventPhase::Ceremony);
    ++game->dwGameFrame;assert(ProcessEvent(game,run,state) && run.eventPhase==EventPhase::Briefing);
    game->dwGameFrame+=BriefingFrames-1;assert(ProcessEvent(game,run,state) && run.eventPhase==EventPhase::Briefing);
    run.event=Event::GoblinGrotto;run.eventPhase=EventPhase::Active;run.phase=Phase::Running;run.level=39;run.eventStarted=game->dwGameFrame;run.census.ready=run.census.entered=true;run.census.Observe(1,false);
    game->dwGameFrame+=1499;assert(!ProcessEvent(game,run,state) && run.eventPhase==EventPhase::Active);
    ++game->dwGameFrame;assert(!ProcessEvent(game,run,state) && run.eventPhase==EventPhase::Returning && run.timedOut && !run.success && run.returning==std::set<uint32_t>({55,56}) && run.rewards.empty());
    // Trial's requested five-minute duration uses the same authoritative deadline.
    run.event=Event::Trial;run.eventPhase=EventPhase::Active;run.phase=Phase::Running;run.timedOut=false;run.eventStarted=game->dwGameFrame;
    assert(Info(Event::Trial).seconds==300);
    game->dwGameFrame+=7499;assert(!ProcessEvent(game,run,state) && run.eventPhase==EventPhase::Active);
    ++game->dwGameFrame;assert(!ProcessEvent(game,run,state) && run.eventPhase==EventPhase::Returning && run.timedOut && !run.success);
    game->pClientList=nullptr;assert(ProcessEvent(game,run,state) && !state.run);
    games.clear();populationCode=nullptr;installed=false;
    std::cout<<"Shared event waits for both authenticated participants, survives simultaneous act loading, rejects stale/foreign readiness and presents a full ten-second briefing.\n";
}
int manaCalls=0,manaValue=0,manaAdds=0;
int __fastcall ManaMaximum(Unit*,uint32_t stat,uint16_t){assert(stat==9);return 200<<8;}
void __fastcall OrdinaryManaSet(Unit*,int stat,int value,uint16_t layer){assert(stat==8 && !layer);manaValue=value;}
void __fastcall OrdinaryManaAdd(Unit*,int stat,int value,uint16_t layer){assert(stat==8 && !layer);manaValue+=value;++manaAdds;}

int __fastcall OrdinaryMana(Unit*,int){++manaCalls;return 0;}
int __fastcall OrdinaryManaCheck(Unit*){++manaCalls;return 0;}
void EventMana(){
    Reset();games.clear();view={};receivedAt=0;auto game=fixture.GamePtr();auto& state=State(game);state.run=std::make_unique<Run>();auto& run=*state.run;
    run.level=39;run.event=Event::FountainOfYouth;run.eventPhase=EventPhase::Active;Put(fixture.level.data(),0x1f8,int(39));
    eventPlayers={};auto& player=eventPlayers[0];player.dwUnitType=0;player.dwUnitId=55;player.dwAnimMode=1;
    Bytes<0x28> skills{};Bytes<0x58> skill{};Put(&player,0x100,skills.data());Put(skills.data(),0x18,skill.data());Put(skill.data(),0x44,int(-1));
    bindings[0x2c49c0]=reinterpret_cast<uintptr_t>(EventLookup);bindings[0x209530]=reinterpret_cast<uintptr_t>(ParticipantRoom);
    originalManaConsume=OrdinaryMana;originalManaCheck=OrdinaryManaCheck;originalManaSet=OrdinaryManaSet;originalManaAdd=OrdinaryManaAdd;manaCalls=0;manaAdds=0;
    bindings[0x1e5620]=reinterpret_cast<uintptr_t>(ManaMaximum);
    ManaSetHook(&player,8,0,0);assert(manaValue==(200<<8));ManaAddHook(&player,8,-100000,0);assert(manaValue==(200<<8) && !manaAdds);
    assert(ManaConsumeHook(&player,INT_MAX)==1 && ManaCheckHook(&player)==1 && !manaCalls);
    Put(skill.data(),0x44,int(10));assert(!ManaCheckHook(&player) && manaCalls==1);
    Put(skill.data(),0x44,int(-1));run.eventPhase=EventPhase::Returning;
    assert(!ManaConsumeHook(&player,1) && !ManaCheckHook(&player) && manaCalls==3);
    ManaSetHook(&player,8,100,0);ManaAddHook(&player,8,-20,0);assert(manaValue==80 && manaAdds==1);
    games.clear();populationCode=nullptr;installed=false;
    std::cout<<"Fountain waives arbitrarily large spell mana requirements/consumption, preserves charged-item checks, and stops as soon as the event ends.\n";
}

Unit nativeGold{};int nativeDropCreates=0,nativeSuperCreates=0;
Room* __fastcall DropRoom(Unit*){return reinterpret_cast<Room*>(fixture.r1[0].data());}
Room* __fastcall DropAdjacent(Room* room,int,int){return room;}
Room* __fastcall DropCollision(Room* room,void*,void*,int,int,int,int,int,int){return room;}
int __fastcall DropLevel(Unit*,int value){assert(!value);return 85;}
Unit* __fastcall DropCreated(Game*,void* drop,int force){
    assert(!force && Field<Unit*>(drop,0)==&fixture.old[0]);
    assert(Field<int>(drop,0x18)==85 && Field<int>(drop,0x20)==17 && Field<int>(drop,0x40)==2 && Field<int>(drop,0x50)==0);
    ++nativeDropCreates;return &nativeGold;
}
void __fastcall EncounterFree(Room* room,void*,int,int,Room** out){*out=room;}
Unit* __fastcall SuperCreated(Game* game,Room* room,void* unit,int x,int y,int flags,int monster,int unknown){
    assert(game==fixture.GamePtr() && room==reinterpret_cast<Room*>(fixture.r1[0].data()) && !unit && x==103 && y==204 && flags==-1 && monster==9 && !unknown);++nativeSuperCreates;
    nativeGold.dwUnitType=2;return &nativeGold; // Stop before unrelated monster-specific initialization.
}
void NativeEventCreation(uint8_t* image){
    const std::array<uint32_t,8> restore{0x209530,0x2232c0,0x231510,0x2f7440,0x2f58c0,0x1434c50,0x2fad40,0x2a80d0};std::array<Bytes<14>,8> saved{};
    for(size_t n=0;n<restore.size();++n)memcpy(saved[n].data(),image+restore[n],14);
    auto stub=[&](uint32_t rva,auto fn){const uint8_t jump[]{0xff,0x25,0,0,0,0};memcpy(image+rva,jump,6);auto target=reinterpret_cast<uintptr_t>(fn);memcpy(image+rva+6,&target,8);};
    stub(0x209530,DropRoom);stub(0x2232c0,DropAdjacent);stub(0x231510,DropCollision);stub(0x2f7440,DropLevel);stub(0x2f58c0,DropCreated);image[0x1434c50]=0xc3; // Cookie helper preserves the native return value in RAX.
    auto game=fixture.GamePtr();fixture.old[0]={};fixture.old[0].dwUnitType=1;nativeGold={};nativeGold.dwUnitType=4;
    FlushInstructionCache(GetCurrentProcess(),image,0x2000000);
    // This routine takes quality, unique/set index, then flags. Its item level
    // comes from the monster. Passing level as arg4 was caught by this fixture.
    auto dropped=Native<Unit*(__fastcall*)(Game*,Unit*,int,int,int,int)>(0x2f8820)(game,&fixture.old[0],17,2,0,0);assert(dropped==&nativeGold && nativeDropCreates==1);
    Bytes<0x30> unique{};Put(unique.data(),4,int16_t(9));Put(image,0x1c9f998,unique.data());Put(image,0x1c9f9a0,uint64_t(1));
    stub(0x2fad40,SuperCreated);stub(0x2a80d0,EncounterFree);FlushInstructionCache(GetCurrentProcess(),image,0x2000000);
    Put(game,0x104,uint8_t(0));Put(game,0x582c,int(0));Put(game,0x5670,uint8_t(0xa5));
    Put(fixture.level.data(),0x1f8,int(39));Run run;assert(SpawnEncounter(game,run,reinterpret_cast<Room*>(fixture.r1[0].data()),{39,103,204},{9,0})==&nativeGold && nativeSuperCreates==1 && Field<uint8_t>(game,0x5670)==0xa5);
    for(size_t n=0;n<restore.size();++n)memcpy(image+restore[n],saved[n].data(),14);FlushInstructionCache(GetCurrentProcess(),image,0x2000000);
    std::cout<<"Actual native gold creation uses the monster level and correct quality/index ABI; native superunique creation receives coordinates/class correctly and preserves spawn bits.\n";
}

Unit midasGold{},midasOther{},midasMonster{};int goldValue=0,midasCalls=0,forcedGold=0;bool naturalGold=false;
void __fastcall MidasOriginal(Game*,Unit*,Unit*,int,int,int,int,Unit** items,int* count,int maximum){assert(maximum>0 && maximum<=32);++midasCalls;*count=1;items[0]=naturalGold ? &midasGold : &midasOther;if(naturalGold)goldValue=10;}
int __fastcall MidasStat(Unit* unit,uint32_t stat,uint16_t){assert(unit==&midasGold && stat==14);return goldValue;}
void __fastcall MidasSet(Unit* unit,int stat,int value,uint16_t){assert(unit==&midasGold && stat==14);goldValue=value;}
Unit* __fastcall MidasCreate(Game*,Unit* monster,int id,int quality,int index,int flags){assert(monster==&midasMonster && !id && quality==2 && !index && !flags);++forcedGold;goldValue=15;return &midasGold;}
void __fastcall MidasBonus(Unit* item,Unit*){assert(item==&midasGold);}
void MidasDrops(){
    Reset();games.clear();auto game=fixture.GamePtr();auto& state=State(game);state.run=std::make_unique<Run>();auto& run=*state.run;
    run.level=39;run.monsterLevel=85;run.event=Event::MidasMeddling;run.eventPhase=EventPhase::Active;Put(fixture.level.data(),0x1f8,int(39));
    midasGold={};midasOther={};midasMonster={};midasGold.dwUnitType=midasOther.dwUnitType=4;midasOther.dwClassId=1;midasMonster.dwUnitType=1;midasMonster.dwUnitId=90;
    std::array<Bytes<0x1b4>,2> items{};Put(items[0].data(),0x80,uint32_t(0x20646c67));uint8_t* table=items[0].data();uint64_t countRows=2;
    bindings[0x1ca0390]=reinterpret_cast<uintptr_t>(&table);bindings[0x1ca0398]=reinterpret_cast<uintptr_t>(&countRows);
    bindings[0x209530]=reinterpret_cast<uintptr_t>(ParticipantRoom);bindings[0x1e5620]=reinterpret_cast<uintptr_t>(MidasStat);bindings[0x1e5440]=reinterpret_cast<uintptr_t>(MidasSet);bindings[0x2f8820]=reinterpret_cast<uintptr_t>(MidasCreate);bindings[0x2f9060]=reinterpret_cast<uintptr_t>(MidasBonus);
    naturalGold=true;midasCalls=forcedGold=0;std::array<Unit*,2> output{};int count=99;
    assert(Drop(game,&midasMonster,nullptr,1,0,85,0,output.data(),&count,2,MidasOriginal) && count==1 && output[0]==&midasGold && goldValue==60 && !forcedGold);
    naturalGold=false;++midasMonster.dwUnitId;count=99;assert(Drop(game,&midasMonster,nullptr,1,0,85,0,output.data(),&count,2,MidasOriginal) && count==2 && output[0]==&midasOther && output[1]==&midasGold && goldValue==90 && forcedGold==1);
    assert(Drop(game,&midasMonster,nullptr,1,0,85,0,nullptr,nullptr,0,MidasOriginal) && forcedGold==1);
    ++midasMonster.dwUnitId;assert(Drop(game,&midasMonster,nullptr,1,0,85,0,nullptr,&count,0,MidasOriginal) && count==2 && forcedGold==2);
    run.eventPhase=EventPhase::Returning;const int previous=midasCalls;assert(!Drop(game,&midasMonster,nullptr,1,0,85,0,nullptr,nullptr,0,MidasOriginal) && previous==midasCalls);
    games.clear();populationCode=nullptr;installed=false;
    std::cout<<"Midas multiplies native gold sixfold, guarantees one extra pile when absent, preserves other drops/output bounds, and does not leak outside the active event.\n";
}

std::map<std::string,int> encounterNames;
int __fastcall EncounterName(void*,const char* name){auto it=encounterNames.find(name);return it==encounterNames.end() ? -1 : it->second;}
void EncounterRosters(){
    Reset();encounterNames.clear();std::vector<Bytes<0x1dc>> monsters(30);std::array<Bytes<0x30>,11> uniques{};uint8_t* monsterTable=monsters[0].data();uint8_t* uniqueTable=uniques[0].data();uint64_t monsterCount=monsters.size(),uniqueCount=uniques.size();void* linker=reinterpret_cast<void*>(1);
    int next=0;for(const auto name:{"andariel","duriel","mephisto","diablo","baalcrab","madking","moophista","enderaku","majiko","rakuranth","grizzlepaw","kelthir","forsaken_izual","deddomer","gurka","queen_syndred","anuburak","goblin_gem","goblin_gold","goblin_box","goblin_runes","goblin_rmd","goblin_essence"}){
        encounterNames[name]=next;Put(monsters[next++].data(),0x3c,uint32_t((1u<<25)|(1u<<15)|(1u<<6)|(1u<<7)|1));
    }
    // Actual loaded RMD goblin flags include neverCount, unlike ordinary map
    // population. All six explicit Goblin Grotto targets must remain eligible.
    for(int id=17;id<23;++id)Put(monsters[id].data(),0x3c,uint32_t(0x245a0cb));
    for(size_t n=0;n<10;++n)Put(uniques[n].data(),4,int16_t(7+n));
    // A superunique's ordinary base class remains a valid normal monster;
    // its superunique name/modifiers must never be spawned in regular events.
    Put(uniques[10].data(),4,int16_t(23));
    std::array<Bytes<0x184>,3> levelRows{};uint8_t* levelTable=levelRows[0].data();uint32_t levelCount=levelRows.size();
    for(auto& level:levelRows)for(size_t offset:{size_t(0x3a),size_t(0x6c)})for(size_t n=0;n<25;++n)Put(level.data(),offset+n*2,int16_t(-1));
    for(int id=23;id<30;++id)Put(monsters[id].data(),0x3c,uint32_t((1u<<25)|(1u<<15)|1u));
    for(int id=23;id<29;++id)Put(levelRows[1].data(),0x3a+(id-23)*2,int16_t(id));
    Put(monsters[25].data(),0x3c,Field<uint32_t>(monsters[25].data(),0x3c)|(1u<<6));
    Put(monsters[26].data(),0x80,uint8_t(2));Put(monsters[27].data(),0x3c,uint32_t(1u<<15));
    Put(monsters[28].data(),0x3c,Field<uint32_t>(monsters[28].data(),0x3c)|(1u<<18));
    bindings[0x1c9f878]=reinterpret_cast<uintptr_t>(&monsterTable);bindings[0x1c9f880]=reinterpret_cast<uintptr_t>(&monsterCount);
    bindings[0x1c9f998]=reinterpret_cast<uintptr_t>(&uniqueTable);bindings[0x1c9f9a0]=reinterpret_cast<uintptr_t>(&uniqueCount);bindings[0x1c9f890]=reinterpret_cast<uintptr_t>(&linker);bindings[0x6d0470]=reinterpret_cast<uintptr_t>(EncounterName);
    bindings[0x1c9fd08]=reinterpret_cast<uintptr_t>(&levelTable);bindings[0x1c9fd10]=reinterpret_cast<uintptr_t>(&levelCount);
    Run run;run.event=Event::Trial;run.random.seed(42);std::set<int> seen;
    for(int n=0;n<200;++n){assert(BuildEncounters(run) && run.encounters.size()==5);std::set<int> current;for(auto encounter:run.encounters){assert(encounter.monster>=0 && encounter.monster<17);assert(encounter.unique==(encounter.monster>=7 ? encounter.monster-7 : -1));current.insert(encounter.monster);seen.insert(encounter.monster);}assert(current.size()==5);}
    assert(seen.size()==17);run.event=Event::GoblinGrotto;assert(BuildEncounters(run) && run.encounters.size()==6);for(auto encounter:run.encounters){assert(encounter.monster>=17 && encounter.monster<23 && encounter.unique==-1);assert(!EncounterCandidate(encounter.monster) && EncounterCandidate(encounter.monster,true) && !RandomCandidate(encounter.monster));}
    for(auto event:{Event::GoldenArmada,Event::ForsakenFestival,Event::FountainOfYouth,Event::SpeedDemon,Event::MidasMeddling}){
        run.event=event;assert(BuildEncounters(run) && run.encounters.empty() && std::set<int>(run.roster.begin(),run.roster.end())==std::set<int>({23,24}));
        for(int n=0;n<100;++n)assert(RandomCandidate(RandomMonster(run)));
        assert(!SpawnEncounters(fixture.GamePtr(),run,{}));
    }
    run.event=Event::Trial;assert(BuildEncounters(run) && run.roster.empty() && run.encounters.size()==5);
    run.event=Event::GoblinGrotto;assert(BuildEncounters(run) && run.roster.empty() && run.encounters.size()==6);
    populationCode=nullptr;installed=false;std::cout<<"Only Trial chooses named bosses/superuniques; all five regular events select ordinary level monsters, and Goblin Grotto selects only goblins.\n";
}

void __fastcall EventPresets(Game* game,Room* room){const int n=fixture.Index(room);SpawnPresetHook(game,room,fixture.preset[n].data());}
void __fastcall EventPopulation(Game* game,Room* room){
    void* record=nullptr;assert(ChooseHook(game,nullptr,room,&record,0,0)==0);
    assert(record==Field<uint8_t*>(Native<void*>(0x1c9f878),0));
    MockPopulation(game,room);
}
void RegularEventPopulation(){
    for(auto event:{Event::GoldenArmada,Event::ForsakenFestival,Event::FountainOfYouth,Event::SpeedDemon,Event::MidasMeddling}){
        Reset();auto game=fixture.GamePtr();auto& state=State(game);state.run=std::make_unique<Run>();auto& run=*state.run;
        run.level=256;run.event=event;run.roster={0};originalSpawnPreset=MockPreset;
        Bytes<0x1dc> monster{};uint8_t* table=monster.data();uint64_t count=1;
        bindings[0x1c9f878]=reinterpret_cast<uintptr_t>(&table);bindings[0x1c9f880]=reinterpret_cast<uintptr_t>(&count);
        populationCode=reinterpret_cast<uint8_t*>(EventPopulation);
        bindings[0x2c59d0]=reinterpret_cast<uintptr_t>(EventPresets);
        assert(Reload(game,run) && run.objectives.empty());
        for(int n=0;n<3;++n){
            assert(std::count(steps.begin(),steps.end(),"fresh"+std::to_string(n))==1);
            assert(std::count(steps.begin(),steps.end(),"preset"+std::to_string(n))==0);
            assert(Field<uint32_t>(fixture.preset[n].data(),0x28)&1);
            auto unit=Field<Unit*>(fixture.r1[n].data(),0xa8);assert(unit==&fixture.fresh[n] && Monster(unit) && !Dead(unit));
        }
        Unpin(run);games.clear();
    }
    populationCode=nullptr;installed=false;
    std::cout<<"Every regular event repopulates all rooms through native pack/champion generation and suppresses fixed monster/superunique presets.\n";
}

void __fastcall SoulSurprisePreset(Game* game,Room* room,void* preset){
    MockPreset(game,room,preset);
    assert(Field<int>(preset,4)==0); // Keep the authored monster despite a different random roster.
    const int n=fixture.Index(room);Put(room,0xa8,&fixture.fresh[n]);
}
void __fastcall SoulSurpriseAmbient(Game*,Room* room){steps.push_back("empty"+std::to_string(fixture.Index(room)));}
void SoulSurprisePresetPopulation(){
    Reset();auto game=fixture.GamePtr();auto& state=State(game);state.run=std::make_unique<Run>();auto& run=*state.run;
    run.level=256;run.event=Event::SoulSurprise;run.roster={1};
    std::array<Bytes<0x1dc>,2> records{};for(auto& row:records)Put(row.data(),0x3c,uint32_t((1u<<25)|(1u<<15)|1));
    uint8_t* table=records[0].data();uint64_t count=records.size();
    bindings[0x1c9f878]=uintptr_t(&table);bindings[0x1c9f880]=uintptr_t(&count);assert(RandomCandidate(0));
    originalSpawnPreset=SoulSurprisePreset;bindings[0x2c59d0]=uintptr_t(EventPresets);populationCode=reinterpret_cast<uint8_t*>(SoulSurpriseAmbient);
    assert(Reload(game,run));
    for(int n=0;n<3;++n){
        assert(std::count(steps.begin(),steps.end(),"preset"+std::to_string(n))==1);
        assert(std::count(steps.begin(),steps.end(),"empty"+std::to_string(n))==1);
        assert(Field<Unit*>(fixture.r1[n].data(),0xa8)==&fixture.fresh[n] && !Dead(&fixture.fresh[n]));
        assert(Field<uint32_t>(fixture.preset[n].data(),0x28)&1);
    }
    // Non-monster objects remain suppressed, as do fixed monsters in other events.
    rebuilding=game;auto preset=fixture.preset[0].data();const auto calls=steps.size();
    Put(preset,0x20,int(2));Put(preset,0x28,uint32_t(0));SpawnPresetHook(game,reinterpret_cast<Room*>(fixture.r1[0].data()),preset);
    assert(steps.size()==calls && (Field<uint32_t>(preset,0x28)&1));Put(preset,0x20,int(1));
    for(auto event:{Event::Trial,Event::GoblinGrotto,Event::Survivors,Event::ChampionClash}){
        run.event=event;Put(preset,0x28,uint32_t(0));SpawnPresetHook(game,reinterpret_cast<Room*>(fixture.r1[0].data()),preset);
        assert(steps.size()==calls && (Field<uint32_t>(preset,0x28)&1));
    }
    rebuilding=nullptr;Unpin(run);games.clear();populationCode=nullptr;installed=false;
    std::cout<<"Soul Surprise restores authored monster presets with zero ambient population, preserves their identities, and retains other event/object suppression.\n";
}

std::map<int,int> trialResistance;int immunityWrites=0;
int __fastcall TrialResistance(Unit*,uint32_t stat,uint16_t){return trialResistance[int(stat)];}
void __fastcall TrialSet(Unit*,int stat,int value,uint16_t){trialResistance[stat]=value;++immunityWrites;}
void TrialImmunities(){
    Reset();bindings[0x1e5620]=reinterpret_cast<uintptr_t>(TrialResistance);bindings[0x1e5440]=reinterpret_cast<uintptr_t>(TrialSet);
    Run run;run.event=Event::Trial;run.random.seed(123);Unit boss{};boss.dwUnitId=55;
    immunityWrites=0;trialResistance.clear();ApplyTrialImmunities(&boss,run);assert(!immunityWrites);
    run.objectives.insert(55);ApplyTrialImmunities(&boss,run);assert(immunityWrites==3 && std::count_if(trialResistance.begin(),trialResistance.end(),[](auto p){return p.second>=100;})==3);
    for(auto& [id,value]:trialResistance)value=99;ApplyTrialImmunities(&boss,run);assert(immunityWrites==3); // A sundered immunity must not be reapplied every scan.
    ++boss.dwUnitId;run.objectives.insert(56);ApplyTrialImmunities(&boss,run);assert(immunityWrites==6);
    populationCode=nullptr;installed=false;std::cout<<"Trial assigns three distinct random immunities once per objective, excludes summons, and preserves sunder changes between census scans.\n";
}

int objectiveLevels=0;
int __fastcall ObjectiveStat(Unit*,uint32_t stat,uint16_t){assert(stat==172);return 0;}
void __fastcall ObjectiveLevel(Unit*,int stat,int value,uint16_t){assert(stat==12 && value==85);++objectiveLevels;}
void GoblinObjectives(){
    Reset();auto game=fixture.GamePtr();game->pClientList=nullptr;Bytes<0x1dc> row{};Put(row.data(),0x3c,uint32_t(0x245a0cb));D2MonsterDataStrc data{};data.pMonstatsTxt=reinterpret_cast<D2MonStatsTxt*>(row.data());
    std::array<Unit,2> goblins{};for(size_t n=0;n<goblins.size();++n){auto& unit=goblins[n];unit.dwUnitType=1;unit.dwUnitId=55+uint32_t(n);unit.dwAnimMode=1;unit.pMonsterData=&data;}goblins[0].pRoomNext=&goblins[1];
    Put(fixture.r1[0].data(),0xa8,&goblins[0]);
    bindings[0x1e5620]=reinterpret_cast<uintptr_t>(ObjectiveStat);bindings[0x1e5440]=reinterpret_cast<uintptr_t>(ObjectiveLevel);bindings[0x1e4700]=reinterpret_cast<uintptr_t>(Nothing);
    Run run;run.event=Event::GoblinGrotto;run.monsterLevel=85;run.pins.push_back({fixture.r2[0].data(),true});run.objectives.insert(55);run.census.ready=run.census.entered=true;objectiveLevels=0;
    assert(!Monster(&goblins[0]) && RunMonster(&goblins[0],run) && !RunMonster(&goblins[1],run));
    CensusAndBuffs(game,run);assert(run.census.Remaining()==1 && !run.census.Complete() && run.census.dead.size()==1 && objectiveLevels==1);
    CensusAndBuffs(game,run);assert(run.census.Remaining()==1 && objectiveLevels==1);
    goblins[0].dwAnimMode=12;CensusAndBuffs(game,run);assert(run.census.Remaining()==0 && run.census.Complete());
    run.event=Event::None;assert(!RunMonster(&goblins[0],run));
    populationCode=nullptr;installed=false;std::cout<<"Actual neverCount goblins remain valid event targets, contribute to census/leveling until death, and stay excluded from ambient and normal-map counts.\n";
}
