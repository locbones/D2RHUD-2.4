void SurvivorsProgression(){
    using namespace Survivors;
    Build build;std::mt19937 random(91);assert(build.ranks[0]==1 && !build.Pending());
    build.xp=RequiredXP(1)-1;build.Offer(random,100);assert(!build.Pending());
    ++build.xp;build.Offer(random,100);assert(build.Pending() && build.offer);
    const auto choices=build.choices;assert(std::set<uint8_t>(choices.begin(),choices.end()).size()==3);
    assert(!build.Choose(build.offer+1,0) && !build.Choose(build.offer,3));
    const auto offer=build.offer;assert(build.Choose(offer,1) && build.level==2 && build.xp==0 && !build.Pending());
    assert(!build.Choose(offer,1));
    build.xp=100000;
    for(int n=0;n<50;++n){build.Offer(random,100+n);if(!build.Pending())break;assert(build.Choose(build.offer,0));}
    assert(build.level==30);for(size_t n=0;n<Count;++n)assert(build.ranks[n]<=Upgrades[n].maximum);
    Snapshot packet;packet.data.recipient=55;packet.data.requiredXP=RequiredXP(1);assert(Valid(packet.data));
    packet.data.choices={0,0,1};packet.data.offer=1;assert(!Valid(packet.data));packet.data.choices={0,1,2};assert(Valid(packet.data));
    packet.data.ranks[0]=Upgrades[0].maximum;assert(!Valid(packet.data));packet.data.ranks[0]=0;packet.data.choiceSeconds=13;assert(!Valid(packet.data));
    Request request;assert(Valid(request.data));request.data.classId=7;assert(!Valid(request.data));request.data.classId=0;request.data.profile.level=2;assert(!Valid(request.data));request.data.profile={};request.data.action=Action::Choose;request.data.slot=3;assert(!Valid(request.data));
    Request allocate;allocate.data.action=Action::Allocate;allocate.data.points=1;assert(Valid(allocate.data));
    allocate.data.points=146;assert(!Valid(allocate.data));allocate.data.points=0;assert(!Valid(allocate.data));allocate.data.points=1;allocate.data.slot=4;assert(!Valid(allocate.data));allocate.data.slot=3;allocate.data.spent=146;assert(!Valid(allocate.data));
    Snapshot totals;totals.data.recipient=55;totals.data.totalAttributes[0]=-1;assert(!Valid(totals.data));totals.data.totalAttributes[0]=1000001;assert(!Valid(totals.data));
    Packet state;state.run=12;state.event=Event::Survivors;state.eventPhase=EventPhase::Briefing;state.eventSeconds=600;assert(Data::Valid(state));state.eventSeconds=601;assert(!Data::Valid(state));
    state.eventSeconds=0;state.eventPhase=EventPhase::Active;state.active=1;state.recipient=55;state.level=39;
    state.eventElapsed=599000;assert(AutomapData::Visible(state,999,55,39) && !AutomapData::Visible(state,1000,55,39));assert(!AutomapData::Visible(state,4001,55,39));
    std::cout<<"Survivors class profiles offer distinct capped upgrades and reject Warlock, malformed progression and replayed/invalid choices.\n";
}
std::array<Unit,2> survivorMonsters{};D2MonsterDataStrc survivorMonsterData{};D2MonStatsTxt survivorMonsterRow{};
std::array<D2DynamicPathStrc,3> survivorPaths{};
int survivorHits=0,survivorDeaths=0;bool survivorOrder=false;
Unit* __fastcall SurvivorLookup(Game*,int type,uint32_t id){
    if(!type){for(auto& p:eventPlayers)if(p.dwUnitId==id)return &p;}
    else for(auto& m:survivorMonsters)if(m.dwUnitId==id)return &m;
    return nullptr;
}
int __fastcall SurvivorStats(Unit*,uint32_t stat,uint16_t){if(stat==172)return 0;if(stat==7 || stat==6)return 100*256;return 0;}
Bytes<0x284> survivorSkillRow{};Bytes<0x50> survivorSkill{};
void* __fastcall SurvivorSkill(Unit*,int id){assert(id==Field<int16_t>(survivorSkillRow.data(),0));return survivorSkill.data();}
void __fastcall SurvivorSelect(Unit* player,int left,int id,int owner){
    assert(!left && id==Field<int16_t>(survivorSkillRow.data(),0) && owner==-1);
    auto list=Field<void*>(player,0x100);assert(list);Put(list,0x10,survivorSkill.data());
}
void __fastcall SurvivorTarget(Unit* player,Unit* target){Put(player->pDynamicPath,0x70,target);Put(player->pDynamicPath,0x78,int(target->dwUnitType));Put(player->pDynamicPath,0x7c,target->dwUnitId);}
int __fastcall SurvivorStart(Game*,Unit* player,int id,int level,int free){assert(id==35 && level==1 && free && Field<void*>(Field<void*>(player,0x100),0x18)==survivorSkill.data());return 1;}
int __fastcall SurvivorAction(Game*,Unit* player,int id,int level,int mana,int force,int itemOverride){
    assert(player==&eventPlayers[0] && id==35 && level==1 && !mana && force==1 && !itemOverride);
    assert(Field<Unit*>(player->pDynamicPath,0x70)==&survivorMonsters[0]);
    survivorMonsters[0].dwAnimMode=12;++survivorHits;++survivorDeaths;return 1;
}
void __fastcall SurvivorSet(Unit*,int,int,uint16_t){}
void SurvivorsCombat(){
    Reset();auto game=fixture.GamePtr();game->dwGameFrame=100;Put(fixture.level.data(),0x1f8,int(39));
    eventPlayers={};auto& player=eventPlayers[0];player.dwUnitType=0;player.dwUnitId=55;player.dwAnimMode=1;player.pDynamicPath=&survivorPaths[0];
    Bytes<0x20> skills{};Put(&player,0x100,skills.data());
    survivorMonsters={};survivorMonsterRow={};survivorMonsterData={};survivorMonsterRow.dwMonStatsFlags=1u<<15;survivorMonsterData.pMonstatsTxt=&survivorMonsterRow;
    for(int n=0;n<2;++n){auto& m=survivorMonsters[n];m.dwUnitType=1;m.dwUnitId=90+n;m.dwAnimMode=1;m.pMonsterData=&survivorMonsterData;m.pDynamicPath=&survivorPaths[n+1];Put(m.pDynamicPath,2,uint16_t(n?50:2));}
    survivorMonsters[0].pRoomNext=&survivorMonsters[1];Put(fixture.r1[0].data(),0xa8,&survivorMonsters[0]);
    bindings[0x2c49c0]=reinterpret_cast<uintptr_t>(SurvivorLookup);bindings[0x209530]=reinterpret_cast<uintptr_t>(ParticipantRoom);bindings[0x1e5620]=reinterpret_cast<uintptr_t>(SurvivorStats);
    bindings[0x218ce0]=reinterpret_cast<uintptr_t>(SurvivorSkill);bindings[0x298810]=reinterpret_cast<uintptr_t>(SurvivorAction);bindings[0x298550]=reinterpret_cast<uintptr_t>(SurvivorStart);bindings[0x2095a0]=reinterpret_cast<uintptr_t>(SurvivorTarget);bindings[0x1e5440]=reinterpret_cast<uintptr_t>(SurvivorSet);
    bindings[0x29a2a0]=reinterpret_cast<uintptr_t>(SurvivorSelect);
    survivorSkillRow={};survivorSkill={};Put(survivorSkill.data(),0,survivorSkillRow.data());Put(survivorSkill.data(),0x38,int(1));Put(survivorSkillRow.data(),0,int16_t(35));
    Run run;run.event=Event::Survivors;run.eventPhase=EventPhase::Active;run.phase=Phase::Running;run.level=39;run.characterLevel=80;run.eventStarted=100;run.survival.nextWave=10000;
    run.pins.push_back({fixture.r2[0].data(),true});run.returns[55]={1,0,0};run.ready.insert(55);run.census.ready=run.census.entered=true;
    // Stored builds for departed classes remain in a shared run, but must not
    // receive either census or removed-corpse XP while another class plays.
    for(uint8_t cls=1;cls<Survivors::Classes.size();++cls){auto& inactive=run.survival.builds[1000+cls];inactive.classId=cls;inactive.xp=cls;}
    survivorHits=survivorDeaths=0;ProcessSurvivors(game,run,{&player});assert(survivorHits==1 && survivorDeaths==1 && !Dead(&survivorMonsters[1]));
    game->dwGameFrame+=5;ProcessSurvivors(game,run,{&player});auto& build=run.survival.builds.at(55);assert(build.kills==1 && build.xp==3 && !survivorOrder);
    game->dwGameFrame+=5;ProcessSurvivors(game,run,{&player});assert(build.kills==1 && build.xp==3 && survivorHits==1);
    for(uint8_t cls=1;cls<Survivors::Classes.size();++cls)assert(run.survival.builds.at(1000+cls).xp==cls && !run.survival.builds.at(1000+cls).kills);
    for(unsigned mode:{4u,9u,19u}){ // Hit recovery, block and knockback are alive.
        player.dwAnimMode=mode;player.dwFlags=0x8000;game->dwGameFrame+=5;
        ProcessSurvivors(game,run,{&player});assert(run.eventPhase==EventPhase::Active && !run.timedOut);
    }
    player.dwAnimMode=1;player.dwFlags=0;
    // A transient missing participant must not immediately end a live run.
    game->dwGameFrame+=5;ProcessSurvivors(game,run,{});assert(run.eventPhase==EventPhase::Active && run.survival.unavailableSince);
    game->dwGameFrame+=5;ProcessSurvivors(game,run,{&player});assert(run.eventPhase==EventPhase::Active && !run.survival.unavailableSince);
    run.survival.bossSpawned=true;run.survival.boss=90;survivorMonsters[1].dwAnimMode=12;
    run.census.Observe(99,false);game->dwGameFrame+=5;ProcessSurvivors(game,run,{&player});assert(!run.success && !run.clearedAt);run.census.Observe(99,true);
    game->dwGameFrame+=5;ProcessSurvivors(game,run,{&player});assert(!run.success && run.eventPhase==EventPhase::Active);
    for(uint8_t cls=1;cls<Survivors::Classes.size();++cls)assert(run.survival.builds.at(1000+cls).xp==cls && !run.survival.builds.at(1000+cls).kills);
    game->dwGameFrame+=50;ProcessSurvivors(game,run,{&player});assert(run.success && run.eventPhase==EventPhase::Returning && run.returning.contains(55));
    run.success=false;run.eventPhase=EventPhase::Active;run.phase=Phase::Running;game->dwGameFrame=100+600*25;
    ProcessSurvivors(game,run,{&player});assert(!run.success && run.timedOut && run.eventPhase==EventPhase::Returning);
    run.eventPhase=EventPhase::Active;run.phase=Phase::Running;run.timedOut=false;game->dwGameFrame=200;player.dwAnimMode=0;
    ProcessSurvivors(game,run,{&player});assert(run.eventPhase==EventPhase::Returning && !run.success);
    games.clear();populationCode=nullptr;installed=false;
    std::cout<<"Survivors auto-attacks respect range/cooldown, execute engine damage in order, credit deaths once, require the boss and remaining adds, and enforce the ten-minute deadline.\n";
}
int survivorNativeChecks=0;
int survivorCreates=0;std::map<int,int> survivorSpawnStats;
Unit* __fastcall SurvivorCreated(Game* game,Room* room,int type,int monster,int x,int y,int mode){
    assert(type==1 && monster==2 && mode==1 && survivorCreates<2);
    auto& unit=survivorMonsters[survivorCreates];unit={};unit.dwUnitType=1;unit.dwClassId=2;unit.dwUnitId=90+survivorCreates;unit.dwAnimMode=1;unit.pMonsterData=&survivorMonsterData;unit.pDynamicPath=&survivorPaths[++survivorCreates];Put(unit.pDynamicPath,2,uint16_t(x));Put(unit.pDynamicPath,6,uint16_t(y));assert(InitializeMonster(game,room,&unit));return &unit;
}
int __fastcall SurvivorSpawnStat(Unit*,uint32_t stat,uint16_t){return stat==7?10000*256:stat==39?150:0;}
void __fastcall SurvivorSpawnSet(Unit*,int stat,int value,uint16_t){survivorSpawnStats[stat]=value;}
void SurvivorsSpawningAndLoot(){
    Reset();auto game=fixture.GamePtr();game->dwGameFrame=100;Put(fixture.level.data(),0x1f8,int(39));
    survivorPaths={};survivorMonsters={};survivorMonsterRow={};survivorMonsterData={};survivorMonsterRow.dwMonStatsFlags=1u<<15;survivorMonsterData.pMonstatsTxt=&survivorMonsterRow;
    eventPlayers={};auto& player=eventPlayers[0];player.dwUnitId=55;player.dwAnimMode=1;player.pDynamicPath=&survivorPaths[0];Put(player.pDynamicPath,2,uint16_t(100));Put(player.pDynamicPath,6,uint16_t(100));
    for(const auto [offset,value]:{std::pair{0x80,0},std::pair{0x84,0},std::pair{0x88,256},std::pair{0x8c,256}})Put(fixture.r1[0].data(),offset,value);
    bindings[0x209530]=reinterpret_cast<uintptr_t>(ParticipantRoom);bindings[0x2a80d0]=reinterpret_cast<uintptr_t>(EncounterFree);bindings[0x2c5760]=reinterpret_cast<uintptr_t>(SurvivorCreated);
    bindings[0x1e4700]=reinterpret_cast<uintptr_t>(+[](uint32_t,int,uint32_t,uint32_t)->void*{return nullptr;});bindings[0x1e5620]=reinterpret_cast<uintptr_t>(SurvivorSpawnStat);bindings[0x1e5440]=reinterpret_cast<uintptr_t>(SurvivorSpawnSet);
    auto& state=State(game);state.run=std::make_unique<Run>();auto& run=*state.run;run.event=Event::Survivors;run.eventPhase=EventPhase::Active;run.phase=Phase::Running;run.level=39;run.monsterLevel=85;run.eventStarted=100;run.pins.push_back({fixture.r2[0].data(),true});run.random.seed(42);
    survivorCreates=0;survivorSpawnStats.clear();auto ordinary=SurvivorSpawn(game,run,&player,2,false);
    assert(ordinary && survivorSpawnStats.at(7)==2500*256 && survivorSpawnStats.at(6)==2500*256 && !survivorSpawnStats.at(13) && !survivorSpawnStats.at(74) && survivorSpawnStats.at(12)==85 && survivorSpawnStats.at(39)==75 && !run.survival.elites.contains(ordinary->dwUnitId));
    const int resistance=survivorSpawnStats.at(39);
    for(int n=0;n<20;++n){const auto projection=Sunder::Calculate(resistance,0,0,50);assert(projection.valid && projection.effective==75 && !projection.applied);}
    assert(survivorSpawnStats.at(39)==75);
    auto elite=SurvivorSpawn(game,run,&player,2,true);assert(elite && survivorSpawnStats.at(7)==10000*256 && run.survival.elites.contains(elite->dwUnitId));
    int count=99;assert(Drop(game,ordinary,&player,1,0,85,0,nullptr,&count,0,MidasOriginal) && !count);
    assert(!Drop(game,elite,&player,1,0,85,0,nullptr,&count,0,MidasOriginal));run.survival.boss=ordinary->dwUnitId;assert(!Drop(game,ordinary,&player,1,0,85,0,nullptr,&count,0,MidasOriginal));
    run.event=Event::None;assert(!Drop(game,ordinary,&player,1,0,85,0,nullptr,&count,0,MidasOriginal));
    games.clear();populationCode=nullptr;installed=false;
    std::cout<<"Survivors uses native collision/spawn wrappers, scales horde life once, caps immunities, zeroes native XP/regen, and limits item drops to elites/bosses inside this mode.\n";
}
void SurvivorsEmptyArena(){
    Reset();auto game=fixture.GamePtr();auto& state=State(game);state.run=std::make_unique<Run>();auto& run=*state.run;
    run.level=256;run.event=Event::Survivors;run.roster={0};originalSpawnPreset=MockPreset;
    bindings[0x2c59d0]=reinterpret_cast<uintptr_t>(EventPresets);
    assert(Reload(game,run));for(int n=0;n<3;++n){assert(!Field<Unit*>(fixture.r1[n].data(),0xa8));assert(std::count(steps.begin(),steps.end(),"fresh"+std::to_string(n))==0);assert(Field<uint32_t>(fixture.preset[n].data(),0x28)&1);}
    Unpin(run);games.clear();populationCode=nullptr;installed=false;
    std::cout<<"Survivors loads an empty protected arena, suppressing ambient packs and preset bosses before its own waves begin.\n";
}
std::vector<Survivors::SnapshotBody> survivorPackets;
void __fastcall CaptureSurvivorPacket(void*,uint32_t,void** range){
    assert(static_cast<uint8_t*>(range[1])-static_cast<uint8_t*>(range[0])==sizeof(Packet));
    if(static_cast<uint8_t*>(range[0])[9]==Survivors::Operation){auto p=static_cast<Survivors::Snapshot*>(range[0]);assert(Survivors::Valid(p->data));survivorPackets.push_back(p->data);}
}
void SurvivorsAuthentication(){
    Reset();games.clear();survivorPackets.clear();view={};receivedAt=survivorReceived=0;survivorView={};
    auto game=fixture.GamePtr();game->dwInitSeed=91;game->dwGameFrame=100;Put(fixture.level.data(),0x1f8,int(39));
    eventPlayers={};eventClients={};eventPlayerData={};auto& player=eventPlayers[0];auto& client=eventClients[0];
    player.dwUnitType=0;player.dwUnitId=55;player.dwAnimMode=1;player.pPlayerData=&eventPlayerData[0];Put(player.pPlayerData,0xf0,&client);
    client.pGame=game;client.dwClientState=4;client.dwUnitGUID=55;client.dwClientId=1;game->pClientList=&client;
    bindings[0x2c49c0]=reinterpret_cast<uintptr_t>(SurvivorLookup);bindings[0x209530]=reinterpret_cast<uintptr_t>(ParticipantRoom);
    bindings[0x2d0130]=reinterpret_cast<uintptr_t>(Nothing);bindings[0x422ac0]=reinterpret_cast<uintptr_t>(CaptureSurvivorPacket);
    auto& state=State(game);state.run=std::make_unique<Run>();auto& run=*state.run;
    run.id=123;run.level=39;run.event=Event::Survivors;run.eventPhase=EventPhase::Active;run.phase=Phase::Running;run.eventFrame=run.eventStarted=100;run.returns[55]={1,0,0};
    auto& b=run.survival.builds[55];b.xp=Survivors::RequiredXP(1);b.offer=7;b.choices={3,4,5};
    const auto savedTick=originalTick;originalTick=+[](Game* game){++game->dwGameFrame;};run.survival.pauseSynced=GetTickCount64();
    for(int n=0;n<20;++n)TickHook(game);assert(game->dwGameFrame==100 && b.Pending());
    Survivors::Request request;request.data.action=Survivors::Action::Choose;request.data.run=123;request.data.session=91;request.data.offer=7;
    // Turning Workshop off must preserve an active avatar's upgrade/resume path.
    SetSurvivorsEnabled(false);
    auto command=[&](){return Command(game,&player,reinterpret_cast<uint8_t*>(&request),sizeof(request));};
    request.data.session=92;assert(command() && b.level==1);request.data.session=91;request.data.run=124;assert(command() && b.level==1);request.data.run=123;
    request.data.offer=8;assert(command() && b.level==1);request.data.offer=7;client.dwUnitGUID=56;assert(command() && b.level==1);client.dwUnitGUID=55;
    Put(fixture.level.data(),0x1f8,int(1));assert(command() && b.level==1);Put(fixture.level.data(),0x1f8,int(39));
    assert(command() && b.level==2 && b.ranks[3]==1 && b.xp==0 && b.Unspent()==5 && survivorPackets.size()==1 && survivorPackets.back().paused && survivorPackets.back().allocating);assert(command() && b.level==2 && survivorPackets.size()==1);
    for(int n=0;n<20;++n)TickHook(game);assert(game->dwGameFrame==100 && b.allocating && !b.Pending());
    request.data.action=Survivors::Action::Ready;request.data.offer=8;assert(command() && b.allocating);request.data.offer=7;
    assert(command() && !b.allocating && !b.Paused() && survivorPackets.size()==2 && !survivorPackets.back().paused);
    assert(command() && survivorPackets.size()==2); // A replay cannot resume another offer.
    installed=false;TickHook(game);assert(game->dwGameFrame==101);installed=true;originalTick=savedTick;
    request.data.action=Survivors::Action::Start;
    assert(command() && survivorPackets.back().failure==Survivors::Failure::Disabled && state.run->id==123);
    SetSurvivorsEnabled(true);
    assert(command() && survivorPackets.back().failure==Survivors::Failure::Busy && state.run->id==123);
    Packet p;p.run=123;p.session=91;p.sequence=1;p.recipient=55;p.active=1;p.level=39;p.event=Event::Survivors;p.eventPhase=EventPhase::Active;
    assert(Receive(reinterpret_cast<uint8_t*>(&p),sizeof(p)));
    Survivors::Snapshot snapshot;snapshot.data=survivorPackets.front();snapshot.data.sequence=5;
    assert(Receive(reinterpret_cast<uint8_t*>(&snapshot),sizeof(snapshot)));assert(!Receive(reinterpret_cast<uint8_t*>(&snapshot),sizeof(snapshot)));
    ++snapshot.data.sequence;++snapshot.data.session;assert(!Receive(reinterpret_cast<uint8_t*>(&snapshot),sizeof(snapshot)));
    games.clear();populationCode=nullptr;installed=false;
    std::cout<<"Survivors host rejects foreign sessions/participants, stale offers, off-arena upgrades and concurrent runs; snapshots reject replay and foreign state.\n";
}
Room* __fastcall SurvivorNearby(Room* room,int,int){return room;}
Bytes<0x20> survivorHeroSlot{},survivorAvatarSlot{};
Bytes<0x58> survivorSkillList{};
D2InventoryStrc survivorInventory{};D2StatListExStrc survivorStatsList{};
std::vector<Bytes<0x284>> survivorSkillRows(263);uint8_t* survivorSkillTable=survivorSkillRows[0].data();uint64_t survivorSkillCount=survivorSkillRows.size();
int survivorAssignments=0,survivorRemovals=0,survivorSaves=0;
std::array<int,3> survivorLearned{};
std::map<int,int> survivorPublished;
int survivorSelected=-1;
Unit survivorStarterItem{};D2ItemDataStrc survivorStarterItemData{};
int survivorStarterCalls=0;bool survivorStarterFail=false;
std::vector<uint32_t> survivorStarterCodes;
void __fastcall SurvivorClientBind(Client* client,Unit* player){client->pPlayer=player;client->dwUnitGUID=player->dwUnitId;client->dwUnitType=0;}
void __fastcall SurvivorUnitBind(Unit* player,Client* client){Put(player->pPlayerData,0xf0,client);}
int __fastcall SurvivorCreate(Game* game,Client* client,Unit** result){
    assert(!client->pPlayer && !client->nSaveHeaderSize && reinterpret_cast<void*>(client->pSaveHeaders)==reinterpret_cast<uint8_t*>(client)+0x1b8 && client->dwClientId==UINT32_MAX && !Field<void*>(game,0x21e8));
    auto& unit=eventPlayers[1];unit={};unit.dwClassId=Field<uint8_t>(client,8);unit.dwAnimMode=1;unit.dwUnitId=56;unit.pPlayerData=&eventPlayerData[1];unit.pStatListEx=&survivorStatsList;unit.pInventory=&survivorInventory;unit.pDynamicPath=&survivorPaths[2];Put(unit.pDynamicPath,0x20,fixture.r1[0].data());
    SurvivorClientBind(client,&unit);SurvivorUnitBind(&unit,client);Put(survivorAvatarSlot.data(),0,&unit);Put(game,0x21e8,survivorAvatarSlot.data());SurvivorStarterHook(&unit,game);*result=&unit;return 0;
}
int __fastcall SurvivorReassign(Client* client,Game* game){Unit* unit=nullptr;assert(SurvivorLoadHook(game,client,&unit)==0 && unit==client->pPlayer);++survivorAssignments;return 0;}
void __fastcall SurvivorRemoveAvatar(Game* game,Unit* unit){assert(unit==&eventPlayers[1] && Field<Client*>(unit->pPlayerData,0xf0) && Field<void*>(game,0x21e8)==survivorAvatarSlot.data());Put(game,0x21e8,static_cast<void*>(nullptr));++survivorRemovals;}
int __fastcall SurvivorSaveOriginal(Game* game,Unit* unit,uint8_t*,uint32_t* size,uint32_t capacity){
    assert(unit==&eventPlayers[0] && unit->dwClassId==5 && unit->pInventory==reinterpret_cast<D2InventoryStrc*>(uintptr_t(0x1234)) && capacity==8192);
    auto client=Field<Client*>(unit->pPlayerData,0xf0);assert(client && Field<uint8_t>(client,8)==5 && Field<uint16_t>(client,0xa)==0x225 && Field<int>(client,0x468)==123 && client->dwUnitGUID==55);
    assert(Field<void*>(game,0x21e8)==survivorHeroSlot.data());++survivorSaves;*size=100;return 0;
}
void SurvivorAvatarBindings(){
    static void* itemLinker=reinterpret_cast<void*>(1);
    bindings[0x1c9eba0]=reinterpret_cast<uintptr_t>(&itemLinker);
    bindings[0x6d01a0]=reinterpret_cast<uintptr_t>(+[](void*,uint32_t code){survivorStarterCodes.push_back(code);return 1;});
    bindings[0x28c520]=reinterpret_cast<uintptr_t>(+[](Game* game,Unit* player,void* row,void* entry,int slot,int id)->Unit*{
        assert(game==fixture.GamePtr() && player==&eventPlayers[1] && Field<int16_t>(row,0xb0)==-1 && Field<uint8_t>(entry,6)==2 && Field<uint8_t>(entry,5)==1 && slot<2 && id==1);
        survivorStarterItem={};survivorStarterItem.dwUnitType=4;survivorStarterItem.dwUnitId=123;survivorStarterItem.pItemData=&survivorStarterItemData;++survivorStarterCalls;return &survivorStarterItem;
    });
    bindings[0x2d5890]=reinterpret_cast<uintptr_t>(+[](void* placement,Game*,Unit*,Unit*){Put(placement,0x10,uint8_t(1));});
    bindings[0x2d5e10]=reinterpret_cast<uintptr_t>(+[](Unit* player,void*,uint32_t* id,int update,bool inventory){assert(player==&eventPlayers[1] && *id==123 && update==1 && !inventory);return !survivorStarterFail;});
    bindings[0x2f70a0]=reinterpret_cast<uintptr_t>(+[](Game*,Unit* item){assert(item==&survivorStarterItem);});
    originalSurvivorStarter=+[](Unit*,Game*){assert(!survivorStarter);++survivorStarterCalls;};
    for(size_t cls=0;cls<Survivors::Classes.size();++cls)for(size_t slot=0;slot<3;++slot){const int id=Survivors::Classes[cls].ids[slot];Put(survivorSkillRows[id].data(),0,int16_t(id));Put(survivorSkillRows[id].data(),0x2c,uint8_t(cls));strcpy_s(reinterpret_cast<char*>(survivorSkillRows[id].data()+2),32,Survivors::Classes[cls].skills[slot]);}
    auto& hero=eventPlayers[0];hero.dwClassId=5;hero.pInventory=reinterpret_cast<D2InventoryStrc*>(uintptr_t(0x1234));Put(&eventClients[0],8,uint8_t(5));Put(&eventClients[0],0xa,uint16_t(0x225));Put(&eventClients[0],0x468,int(123));Put(survivorHeroSlot.data(),0,&hero);Put(fixture.GamePtr(),0x21e8,survivorHeroSlot.data());
    bindings[0x1c9fad0]=reinterpret_cast<uintptr_t>(&survivorSkillTable);bindings[0x1c9fad8]=reinterpret_cast<uintptr_t>(&survivorSkillCount);
    bindings[0x31a280]=reinterpret_cast<uintptr_t>(SurvivorCreate);bindings[0x29d890]=reinterpret_cast<uintptr_t>(SurvivorClientBind);bindings[0x2c52e0]=reinterpret_cast<uintptr_t>(SurvivorUnitBind);
    bindings[0x1d1280]=reinterpret_cast<uintptr_t>(+[](Room*,int,uint32_t){});bindings[0x1f73e0]=reinterpret_cast<uintptr_t>(+[](Unit*,int){});bindings[0x22e8a0]=reinterpret_cast<uintptr_t>(+[](Unit* u){Put(u->pDynamicPath,0x20,static_cast<Room*>(nullptr));});
    bindings[0x29d8f0]=reinterpret_cast<uintptr_t>(+[](Client*,Unit*,Game*,Room*,int,int){});bindings[0x341670]=reinterpret_cast<uintptr_t>(+[](Game*,Unit*,Client*){});bindings[0x29de20]=reinterpret_cast<uintptr_t>(SurvivorReassign);
    bindings[0x1e5440]=reinterpret_cast<uintptr_t>(SurvivorSet);bindings[0x2aaa00]=reinterpret_cast<uintptr_t>(+[](Unit*,Unit*,int){});
    bindings[0x2192d0]=reinterpret_cast<uintptr_t>(+[](Unit*,int,int,int){});bindings[0x2c7a90]=reinterpret_cast<uintptr_t>(+[](Game*,Unit* hero,Room* room,int,int,int){assert(!Field<void*>(hero->pDynamicPath,0x20));Put(hero->pDynamicPath,0x20,room);return 1;});
    bindings[0x2cce40]=reinterpret_cast<uintptr_t>(+[](Client* client,Unit* unit,uint16_t skill,uint8_t level,uint8_t flags){assert(client==&eventClients[0] && unit==&eventPlayers[1] && !flags);survivorPublished[skill]=level;});
    bindings[0x29a2a0]=reinterpret_cast<uintptr_t>(+[](Unit* unit,int left,int skill,int owner){assert(unit==&eventPlayers[1] && !left && owner==-1);survivorSelected=skill;});
    originalRemove=SurvivorRemoveAvatar;originalSurvivorSave=SurvivorSaveOriginal;
}
void SurvivorsNpcEntry(){
    Reset();survivorPackets.clear();survivorsAvailable=true;auto game=fixture.GamePtr();game->dwInitSeed=91;game->dwGameFrame=100;Put(fixture.level.data(),0x1f8,int(1));
    eventPlayers={};eventClients={};eventPlayerData={};survivorMonsters={};survivorPaths={};encounterNames={{"charsi",2},{"hud_cube_map",259}};
    void* linker=reinterpret_cast<void*>(1);uint64_t stateCount=260;std::vector<Bytes<0x40>> states(stateCount);uint8_t* stateRows=states[0].data();
    auto& player=eventPlayers[0];auto& client=eventClients[0];player.dwUnitType=0;player.dwUnitId=55;player.dwAnimMode=1;player.pPlayerData=&eventPlayerData[0];player.pDynamicPath=&survivorPaths[0];Put(player.pPlayerData,0xf0,&client);
    client.pGame=game;client.dwClientState=4;client.dwUnitGUID=55;game->pClientList=&client;
    auto& npc=survivorMonsters[0];npc.dwUnitType=1;npc.dwUnitId=90;npc.dwClassId=2;npc.dwAnimMode=1;npc.pDynamicPath=&survivorPaths[1];Put(npc.pDynamicPath,2,uint16_t(50));Put(fixture.r1[0].data(),0xa8,&npc);
    bindings[0x1c9f890]=bindings[0x1c9ec08]=reinterpret_cast<uintptr_t>(&linker);bindings[0x6d0470]=reinterpret_cast<uintptr_t>(EncounterName);
    bindings[0x1c9ebf8]=reinterpret_cast<uintptr_t>(&stateCount);bindings[0x1c9ebf0]=reinterpret_cast<uintptr_t>(&stateRows);
    bindings[0x2c49c0]=reinterpret_cast<uintptr_t>(SurvivorLookup);bindings[0x209530]=reinterpret_cast<uintptr_t>(ParticipantRoom);bindings[0x2232c0]=reinterpret_cast<uintptr_t>(SurvivorNearby);
    bindings[0x1e5620]=reinterpret_cast<uintptr_t>(SurvivorStats);bindings[0x2d0130]=reinterpret_cast<uintptr_t>(Nothing);bindings[0x422ac0]=reinterpret_cast<uintptr_t>(CaptureSurvivorPacket);
    Survivors::Request request;auto command=[&](){return Command(game,&player,reinterpret_cast<uint8_t*>(&request),sizeof(request));};
    SetSurvivorsEnabled(false);
    assert(command() && !State(game).run && survivorPackets.back().failure==Survivors::Failure::Disabled);
    assert(strstr(Status(),"Workshop"));
    Survivors::SnapshotBody disabled;disabled.recipient=55;disabled.failure=Survivors::Failure::Disabled;
    assert(Survivors::Valid(disabled));disabled.failure=Survivors::Failure(unsigned(Survivors::Failure::Disabled)+1);
    assert(!Survivors::Valid(disabled));
    SetSurvivorsEnabled(true);
    assert(!NearCharsi(&player) && command() && !State(game).run && survivorPackets.back().failure==Survivors::Failure::Npc);
    Put(npc.pDynamicPath,2,uint16_t(2));assert(NearCharsi(&player));states[259][0x10]=1;
    assert(command() && !State(game).run && survivorPackets.back().failure==Survivors::Failure::State);states[259][0x10]=0;
    SurvivorAvatarBindings();
    assert(command() && State(game).run);auto& run=*State(game).run;
    assert(run.event==Event::Survivors && run.eventPhase==EventPhase::Briefing && run.phase==Phase::Closed && run.state==259 && run.returns.contains(56) && run.survival.builds.at(56).ranks[0]==1 && run.characterLevel==1);
    assert(client.dwUnitGUID==56 && Field<uint8_t>(&client,8)==0 && player.dwClassId==5 && player.pInventory==reinterpret_cast<D2InventoryStrc*>(uintptr_t(0x1234)) && Field<Client*>(player.pPlayerData,0xf0)->dwClientId==UINT32_MAX);
    assert(Field<void*>(player.pDynamicPath,0x20)==fixture.r1[0].data());
    uint32_t saved=0;survivorSaves=0;assert(SurvivorSaveHook(game,&eventPlayers[1],nullptr,&saved,8192)==0 && saved==100 && survivorSaves==1);
    assert(Field<Client*>(player.pPlayerData,0xf0)->dwClientId==UINT32_MAX && Field<void*>(game,0x21e8)==survivorAvatarSlot.data());
    assert(RestoreSurvivor(game,&eventPlayers[1]) && survivorAvatars.empty());assert(client.dwUnitGUID==55 && Field<uint8_t>(&client,8)==5 && Field<uint16_t>(&client,0xa)==0x225 && Field<int>(&client,0x468)==123 && Field<void*>(game,0x21e8)==survivorHeroSlot.data() && run.returns.contains(55));
    // Switching classes on the same hero rekeys the old build during native
    // handoff. Start must replace it with the selected class's own profile,
    // rather than retaining the previous class's banked XP or pending offer.
    for(uint8_t cls:{uint8_t(1),uint8_t(3),uint8_t(1),uint8_t(3),uint8_t(0),uint8_t(2),uint8_t(4),uint8_t(5),uint8_t(6)}){
        auto& old=run.survival.builds.at(55);old.xp=75;old.choices={3,4,5};old.offer=7;old.allocating=true;
        request.data.classId=cls;request.data.profile={};request.data.profile.xp=cls==3?0:unsigned(cls+1)*2;
        assert(command() && client.dwUnitGUID==56 && run.survival.builds.size()==1);
        const auto& selected=run.survival.builds.at(56);
        assert(selected.classId==cls && selected.Save()==request.data.profile && !selected.Paused() && !selected.offer && !selected.kills);
        assert(survivorPackets.back().classId==cls && survivorPackets.back().xp==request.data.profile.xp && survivorPackets.back().level==1);
        assert(RestoreSurvivor(game,&eventPlayers[1]) && run.survival.builds.at(55).classId==cls);
    }
    run.event=Event::None;assert(command() && survivorPackets.back().failure==Survivors::Failure::Busy);
    originalSurvivorDisconnect=+[](Game* game,uint32_t id){assert(id==eventClients[0].dwClientId && eventClients[0].pPlayer==&eventPlayers[0] && survivorAvatars.empty());uint32_t size=0;assert(SurvivorSaveHook(game,eventClients[0].pPlayer,nullptr,&size,8192)==0 && size==100);};
    originalSurvivorSendSave=+[](Game* game,Unit* unit,const char*,Client* c){assert(c==&eventClients[0]);uint32_t size=0;assert(SurvivorSaveOriginal(game,unit,nullptr,&size,8192)==0);};
    originalSurvivorPreview=+[](Client* c,void*){assert(c!=&eventClients[0] && c->pPlayer==&eventPlayers[0] && c->dwUnitGUID==55 && Field<uint8_t>(c,8)==5 && Field<uint16_t>(c,0xa)==0x225);};
    for(uint8_t cls=0;cls<Survivors::Classes.size();++cls){
        Survivors::Build b;b.classId=cls;const auto normalInventory=player.pInventory;
        survivorStarterCalls=0;survivorStarterCodes.clear();
        auto avatar=CreateSurvivor(game,&player,b);assert(avatar && avatar->dwClassId==cls && avatar->pInventory!=normalInventory && player.dwClassId==5 && player.pInventory==normalInventory);
        assert(survivorStarterCalls==(cls==1?1:2) && !survivorStarter && survivorStarterCodes.size()==survivorStarterCalls);
        if(cls!=1)assert(survivorStarterCodes.back()==0x20637562);
        const auto ids=Survivors::Classes[cls].ids;
        assert(survivorPublished.at(ids[0])==1 && !survivorPublished.at(ids[1]) && !survivorPublished.at(ids[2]) && survivorSelected==ids[0]);
        SurvivorSendSaveHook(game,avatar,client.szName,&client);SurvivorPreviewHook(&client,nullptr);
        avatar->dwAnimMode=0; // A dead avatar must never strand the real hero.
        if(cls%2)SurvivorDisconnectHook(game,client.dwClientId);else assert(RestoreSurvivor(game,avatar));
        assert(client.dwUnitGUID==55 && client.pPlayer==&player && player.dwClassId==5 && player.pInventory==normalInventory && !Dead(&player));
    }
    static void* skillLinker=reinterpret_cast<void*>(2);
    bindings[0x1c9fae8]=reinterpret_cast<uintptr_t>(&skillLinker);
    bindings[0x6d0470]=reinterpret_cast<uintptr_t>(+[](void* linker,const char* name)->int{if(linker==reinterpret_cast<void*>(2))return !strcmp(name,"Fire Ball")?41:-1;return EncounterName(linker,name);});
    Put(survivorSkillRows[41].data(),0,int16_t(41));Put(survivorSkillRows[41].data(),0x2c,uint8_t(1));
    memset(survivorSkillRows[47].data()+2,0,32);strcpy_s(reinterpret_cast<char*>(survivorSkillRows[41].data()+2),32,"Fire Ball");
    Survivors::Build moved;moved.classId=1;auto sorceress=CreateSurvivor(game,&player,moved);
    assert(sorceress && survivorSelected==41 && survivorPublished.at(41)==1 && RestoreSurvivor(game,sorceress));
    // The same handoff must work with either missing or empty native linkers.
    skillLinker=nullptr;
    sorceress=CreateSurvivor(game,&player,moved);assert(sorceress && survivorSelected==41 && RestoreSurvivor(game,sorceress));
    skillLinker=reinterpret_cast<void*>(2);
    bindings[0x6d0470]=reinterpret_cast<uintptr_t>(+[](void*,const char*){return -1;});
    sorceress=CreateSurvivor(game,&player,moved);assert(sorceress && survivorSelected==41 && RestoreSurvivor(game,sorceress));
    bindings.erase(0x1c9fae8);bindings[0x6d0470]=reinterpret_cast<uintptr_t>(EncounterName);
    survivorStarterCalls=0;SurvivorStarterHook(&player,game);assert(survivorStarterCalls==1);
    survivorStarterFail=true;assert(!CreateSurvivor(game,&player,Survivors::Build{}) && !survivorStarter && survivorAvatars.empty() && client.pPlayer==&player && Field<void*>(game,0x21e8)==survivorHeroSlot.data());survivorStarterFail=false;
    bindings[0x31a280]=reinterpret_cast<uintptr_t>(+[](Game*,Client*,Unit** out)->int{*out=nullptr;return 14;});
    assert(!CreateSurvivor(game,&player,Survivors::Build{}) && survivorAvatars.empty() && client.pPlayer==&player && Field<void*>(game,0x21e8)==survivorHeroSlot.data() && Field<Client*>(player.pPlayerData,0xf0)==&client);
    games.clear();populationCode=nullptr;installed=false;survivorsAvailable=false;
    std::cout<<"All seven native avatar wrappers preserve hero gear/identity and save buffers; autosave, preview, shared-stash save, death, disconnect and failed creation restore the normal hero.\n";
}
std::map<int,int> survivorAttributeWrites;
void SurvivorsAttributes(){
    Reset();eventPlayers={};eventClients={};auto& player=eventPlayers[1];player.pStatListEx=&survivorStatsList;survivorStatsList={};
    SurvivorAvatar a;a.avatar=&player;a.client=&eventClients[0];a.baseAttributes={10,20,30,40};a.baseMaximum={1000,2000,3000};
    std::array<Bytes<0xd0>,7> rows{};auto rowPtr=rows.data();uint64_t count=7;Put(rows[1].data(),0x42,uint8_t(8));Put(rows[1].data(),0x43,uint8_t(4));Put(rows[1].data(),0x44,uint8_t(8));
    bindings[0x1c9fb60]=uintptr_t(&rowPtr);bindings[0x1c9fb68]=uintptr_t(&count);
    bindings[0x1e5440]=reinterpret_cast<uintptr_t>(+[](Unit*,int stat,int value,uint16_t){survivorAttributeWrites[stat]=value;});
    bindings[0x2192d0]=reinterpret_cast<uintptr_t>(+[](Unit*,int,int,int){});bindings[0x2cce40]=reinterpret_cast<uintptr_t>(+[](Client*,Unit*,uint16_t,uint8_t,uint8_t){});bindings[0x29a2a0]=reinterpret_cast<uintptr_t>(+[](Unit*,int,int,int){});
    Survivors::Build b;b.classId=1;b.level=4;b.ranks[0]=4;b.attributes={0,10,0,5};assert(Survivors::Valid(b.Save()));
    survivorAttributeWrites.clear();SurvivorApplyLevel(a,b,true);
    assert(!survivorAttributeWrites.at(4) && survivorAttributeWrites.at(0)==10 && survivorAttributeWrites.at(1)==30 && survivorAttributeWrites.at(2)==30 && survivorAttributeWrites.at(3)==45);
    assert(survivorAttributeWrites.at(7)==1000+3*8*256+5*8*64 && survivorAttributeWrites.at(9)==2000+3*4*256+10*8*64 && survivorAttributeWrites.at(11)==3000+3*4*256+5*4*64);
    assert(SurvivorSkillLevel(b,4)==6 && !SurvivorSkillLevel(b,0));
    std::array<std::array<int,2>,4> values{{{0,10},{1<<16,30},{2<<16,30},{3<<16,45}}};Put(player.pStatListEx,0x30,values.data());Put(player.pStatListEx,0x38,uint64_t(4));
    assert(!SurvivorCaptureAttributes(a,b));values[1][1]=29;assert(SurvivorCaptureAttributes(a,b) && b.attributes[1]==9 && b.Unspent()==1);
    values[1][1]=31;assert(!SurvivorCaptureAttributes(a,b) && b.attributes[1]==9);values[0][1]=9;assert(!SurvivorCaptureAttributes(a,b));
    std::cout<<"Survivors stat points persist as bounded base allocations; native class vitality/energy gains survive upgrades and Energy strengthens learned skills without changing normal heroes.\n";
}
int survivorClientTicks=0;
void SurvivorsClientClockPhases(){
    Bytes<0x90> clock{},other{};SurvivorClientClock time;
    Put(clock.data(),8,uint32_t(1017));Put(clock.data(),0xc,uint32_t(1000));
    assert(time.Time(clock.data(),1017000000,1017,false)==1017);
    assert(time.Time(clock.data(),1034000000,1034,true)==1017);
    assert(time.Time(clock.data(),21034000000,21034,true)==1017);
    assert(time.Time(clock.data(),21051000000,21051,false)==1051);
    assert(time.Time(clock.data(),21068000000,21068,false)==1068);
    assert(time.offset%40==0);
    // Returning to town retains the same clock mapping, without replaying
    // the paused twenty seconds; the next upgrade adds only its own pause.
    Put(clock.data(),8,uint32_t(1068));
    assert(time.Time(clock.data(),21085000000,21085,true)==1068);
    assert(time.Time(clock.data(),31068000000,31068,true)==1068);
    assert(time.Time(clock.data(),31085000000,31085,false)==1085);
    assert(time.Time(clock.data(),31102000000,31102,false)==1102);
    assert(time.offset%40==0);
    // A new native game resets time even if it reuses the same allocation.
    assert(time.Time(clock.data(),17000000,17,false)==17);
    assert(time.Time(other.data(),34000000,34,false)==34);
    assert(Field<uint32_t>(clock.data(),0xc)==1000);
    std::cout<<"Client world clock excludes repeated upgrade pauses, preserves fractional phase in town, and resets for new native game clocks.\n";
}
void SurvivorsClientPause(){
    SurvivorsClientClockPhases();
    Reset();survivorClientClock={};survivorsAvailable=true;eventPlayers={};auto& player=eventPlayers[0];player.dwUnitId=55;
    uint32_t selector=0;Bytes<0x40> clock{};auto clockPtr=clock.data();
    bindings[0x1d442d8]=uintptr_t(&selector);bindings[0x6b050]=reinterpret_cast<uintptr_t>(+[](uint32_t index)->Unit*{assert(!index);return &eventPlayers[0];});
    bindings[0x209530]=reinterpret_cast<uintptr_t>(ParticipantRoom);Put(fixture.level.data(),0x1f8,int(39));bindings[0x1c9b3f0]=uintptr_t(&clockPtr);
    static uint8_t classic=0,hd=0;bindings[0x1840a0c]=uintptr_t(&classic);bindings[0x1d47325]=uintptr_t(&hd);
    originalSurvivorClientTick=+[](uint32_t now){assert(now>=5000);if(Field<uint8_t>(Native<void*>(0x1840a0c),0) && Field<uint8_t>(Native<void*>(0x1d47325),0))return false;++survivorClientTicks;++eventPlayers[0].dwAnimMode;return true;};survivorClientTicks=0;
    view={};view.run=7;view.session=91;view.recipient=55;view.level=39;view.active=1;view.event=Event::Survivors;view.eventPhase=EventPhase::Active;
    survivorView={};survivorView.run=7;survivorView.session=91;survivorView.recipient=55;survivorView.paused=1;survivorView.phase=EventPhase::Active;receivedAt=survivorReceived=GetTickCount64();
    Put(clock.data(),8,uint32_t(5000));Put(clock.data(),0xc,uint32_t(4980));Put(clock.data(),0x18,float(.5));
    for(uint32_t now:{5000u,9000u,15000u}){assert(!SurvivorClientTickHook(now));assert(!classic && !hd);assert(!survivorClientTicks && !player.dwAnimMode && Field<uint32_t>(clock.data(),8)==5000 && Field<uint32_t>(clock.data(),0xc)==4980 && Field<float>(clock.data(),0x18)==.5f);}
    survivorView.paused=0;assert(SurvivorClientTickHook(15040) && survivorClientTicks==1 && player.dwAnimMode==1);
    survivorView.paused=1;++survivorView.session;assert(SurvivorClientTickHook(15080));--survivorView.session;
    survivorReceived-=4001;assert(SurvivorClientTickHook(15120));survivorReceived=GetTickCount64();
    Put(fixture.level.data(),0x1f8,int(1));assert(SurvivorClientTickHook(15160));Put(fixture.level.data(),0x1f8,int(39));
    view.event=Event::Trial;assert(SurvivorClientTickHook(15200));view.event=Event::Survivors;survivorView.phase=EventPhase::Finished;assert(SurvivorClientTickHook(15240));
    assert(survivorClientTicks==6);
    survivorsAvailable=false;view={};receivedAt=survivorReceived=0;
    std::cout<<"Survivors pause suppresses client world/animation ticks, freezes world time without catch-up while the input clock continues, and resumes for choices, town, other events and stale/foreign snapshots.\n";
}
void SurvivorsRosterPersistence(){
    using namespace Survivors;
    const auto path=std::filesystem::temp_directory_path()/("d2rhud-survivors-test-"+std::to_string(GetCurrentProcessId()))/"roster.dat";
    Roster roster(path);assert(roster.good);
    for(uint8_t cls=0;cls<Classes.size();++cls){Build b;b.classId=cls;b.xp=RequiredXP(1);b.offer=1;b.choices={3,4,5};assert(b.Choose(1,0));b.attributes={1,1,1,2};assert(Valid(b.Save()) && !b.Unspent());assert(roster.Store(cls,b.Save()));}
    Roster loaded(path);assert(loaded.good);for(uint8_t cls=0;cls<Classes.size();++cls)assert((loaded.Get(cls).level==2 && loaded.Get(cls).ranks[3]==1 && loaded.Get(cls).attributes==std::array<uint16_t,4>{1,1,1,2}));
    auto overspent=loaded.Get(0);++overspent.attributes[0];assert(!Valid(overspent) && !loaded.Store(0,overspent));
    std::array<uint8_t,118> legacy{};const uint64_t magic=0x31524f5356525332ULL;const uint32_t version=1;memcpy(legacy.data(),&magic,8);memcpy(legacy.data()+8,&version,4);
    for(size_t n=0;n<7;++n)memcpy(legacy.data()+12+n*14,&loaded.Get(uint8_t(n)),14);
    uint64_t checksum=14695981039346656037ULL;for(size_t n=0;n<110;++n)checksum=(checksum^legacy[n])*1099511628211ULL;memcpy(legacy.data()+110,&checksum,8);
    const auto oldPath=path.parent_path()/"legacy.dat";{std::ofstream out(oldPath,std::ios::binary);out.write(reinterpret_cast<char*>(legacy.data()),legacy.size());}
    Roster upgraded(oldPath);assert((upgraded.good && upgraded.Get(0).level==2 && upgraded.Get(0).ranks[3]==1 && upgraded.Get(0).attributes==std::array<uint16_t,4>{}));
    assert(upgraded.Store(0,loaded.Get(0)) && std::filesystem::file_size(oldPath)==sizeof(RosterFile) && std::filesystem::file_size(oldPath.wstring()+L".bak")==118);
    auto invalid=loaded.Get(0);invalid.level=7;assert(!loaded.Store(0,invalid) && loaded.Get(0).level==2);
    {std::ofstream bad(path,std::ios::binary|std::ios::app);bad.put('x');}Roster corrupt(path);assert(!corrupt.good && !corrupt.Store(0,Profile{}));
    auto backup=path;backup+=".bak";Roster recovered(backup);assert(recovered.good && recovered.Get(0).level==2);
    RosterFile file;{std::ifstream read(backup,std::ios::binary);assert(read.read(reinterpret_cast<char*>(&file),sizeof(file)));}
    ++file.version;file.checksum=RosterChecksum(file);{std::ofstream write(path,std::ios::binary|std::ios::trunc);write.write(reinterpret_cast<const char*>(&file),sizeof(file));}
    Roster unknown(path);assert(!unknown.good && !unknown.Store(0,Profile{}));
    {std::ofstream write(path,std::ios::binary|std::ios::trunc);write.write(reinterpret_cast<const char*>(&file),sizeof(file)/2);}Roster truncated(path);assert(!truncated.good && !truncated.Store(0,Profile{}) && std::filesystem::file_size(path)==sizeof(file)/2);
    survivorRoster=std::make_unique<Roster>(path.parent_path()/"packets.dat");
    // Exercise the actual snapshot-to-save path with distinct class XP. A
    // homogeneous seven-class fixture would hide accidental slot aliasing.
    std::array<Profile,7> expected{};
    const auto isolated=path.parent_path()/"isolated-classes.dat";
    survivorRoster=std::make_unique<Roster>(isolated);
    for(uint8_t cls=0;cls<Classes.size();++cls){
        SnapshotBody p;p.run=123;p.classId=cls;p.ranks={1};p.xp=unsigned(cls+1)*3;
        SaveSurvivorProfile(p,true);expected[cls]={p.xp,p.level,p.ranks,p.attributes};
        Roster actual(isolated);assert(actual.good);
        for(uint8_t other=0;other<Classes.size();++other){assert(actual.Get(other)==expected[other] && survivorRoster->Get(other)==expected[other]);Build b;b.Load(actual.Get(other),other);assert(b.classId==other && b.Save()==expected[other]);}
    }
    // Repeated switching/updating cannot turn Sorceress XP into Paladin XP,
    // or overwrite any class's upgrades or allocated attributes.
    for(uint8_t cls:{uint8_t(1),uint8_t(3),uint8_t(1),uint8_t(3)}){
        SnapshotBody p;p.run=124;p.classId=cls;p.level=2;p.ranks={1,0,0,1};p.attributes={0,0,0,5};p.xp=expected[cls].xp+1;
        SaveSurvivorProfile(p,true);expected[cls]={p.xp,p.level,p.ranks,p.attributes};
        Roster actual(isolated);for(uint8_t other=0;other<Classes.size();++other)assert(actual.Get(other)==expected[other]);
    }
    std::cout<<"Selected-class snapshots save XP/ranks/attributes to only that slot; all other six profiles survive updates and Sorceress/Paladin switching unchanged.\n";
    std::cout<<"All seven class profiles persist across characters/process reloads; malformed ranks, truncated/oversized saves and unknown formats fail closed without erasing progress.\n";
}
std::vector<uint8_t> survivorNativePackets;
void __fastcall SurvivorNativeSend(void*,uint32_t id,void** range){assert(id==71);survivorNativePackets.push_back(*static_cast<uint8_t*>(range[0]));}
void NativeMobileSurvivorEffect(uint8_t* image){
    auto stub=[&](uint32_t rva,auto fn){const uint8_t jump[]{0xff,0x25,0,0,0,0};memcpy(image+rva,jump,6);auto target=reinterpret_cast<uintptr_t>(fn);memcpy(image+rva+6,&target,8);};
    stub(0x218ce0,SurvivorSkill);stub(0x1e5440,SurvivorSet);stub(0x1e5620,SurvivorStats);
    stub(0x20c230,+[](Unit* unit)->void*{return unit->pPlayerData;});
    stub(0x209530,+[](Unit*)->Room*{return reinterpret_cast<Room*>(fixture.r1[0].data());});
    stub(0x2082d0,+[](Unit* unit)->void*{return unit->pDynamicPath;});
    stub(0x2c5d50,+[](Game*,Unit*){});stub(0x2c49c0,SurvivorLookup);
    stub(0x2ce500,+[](Client*,Unit*){return true;});stub(0x2d0130,+[](Client*){});image[0x1434c50]=0xc3;
    // Use the real generic skill action and effect packet with RMD's moved
    // Fire Ball ID. Only the native projectile creation boundary is mocked.
    Put(image,0x1c9fae8,reinterpret_cast<void*>(1));
    stub(0x6d0470,+[](void* linker,const char* name){assert(linker==reinterpret_cast<void*>(1) && !strcmp(name,"Fire Ball"));return 41;});
    survivorNativeChecks=0;
    stub(0x3151a0,+[](Game*,void* p)->Unit*{
        assert(Field<Unit*>(p,8)==&eventPlayers[0] && Field<int>(p,0x20)==22 && Field<int>(p,0x3c)==41 && Field<int>(p,0x40)==1);
        assert(Field<int>(p,0x24)==12 && Field<int>(p,0x28)==34 && Field<int>(p,0x2c)==72 && Field<int>(p,0x30)==84);++survivorNativeChecks;return &survivorMonsters[1];
    });
    auto game=fixture.GamePtr();Put(fixture.level.data(),0x1f8,int(39));
    std::vector<Bytes<0x284>> rows(263);Put(rows[41].data(),0,int16_t(41));Put(rows[41].data(),0x2c,uint8_t(1));strcpy_s(reinterpret_cast<char*>(rows[41].data()+2),32,"Fire Ball");Put(rows[41].data(),0x4e,int16_t(0));Put(rows[41].data(),0x66,int16_t(22));
    Put(image,0x1c9fad0,rows.data());Put(image,0x1c9fad8,uint64_t(rows.size()));Put(image,0x1c9fa48,int(100));
    assert(SurvivorSkillId(1,0)==41 && Survivors::Classes[1].ids[0]==47);
    Put(image,0x14bf500,uintptr_t(0));
    Bytes<16> formula{};Put(image,0x1c9eab8,formula.data());Put(image,0x1c9eac0,uint64_t(formula.size()));
    auto& player=eventPlayers[0];player={};player.dwClassId=1;player.dwUnitId=55;player.dwAnimMode=3;player.dwFlags=2;
    D2PlayerDataStrc data{};Client client{};Bytes<0x20> skills{};D2DynamicPathStrc path{},targetPath{};
    player.pPlayerData=&data;player.pDynamicPath=&path;Put(&player,0x100,skills.data());Put(&data,0xf0,&client);
    client.dwClientId=71;client.dwClientState=4;client.pPlayer=&player;client.pGame=game;client.dwUnitGUID=55;game->pClientList=&client;
    Put(&path,2,uint16_t(12));Put(&path,6,uint16_t(34));Put(&path,0x10,uint16_t(123));Put(&path,0x12,uint16_t(234));Put(&path,0x78,int(6));Put(&path,0x7c,int(-1));
    auto& target=survivorMonsters[0];target.dwUnitType=1;target.dwUnitId=90;target.dwAnimMode=1;target.pDynamicPath=&targetPath;Put(&targetPath,2,uint16_t(72));Put(&targetPath,6,uint16_t(84));
    survivorSkill={};survivorSkillRow=rows[41];Put(survivorSkill.data(),0,rows[41].data());Put(survivorSkill.data(),0x38,int(1));Put(survivorSkill.data(),0x44,int(-1));
    std::array<uintptr_t,6> network{};network[5]=reinterpret_cast<uintptr_t>(+[](void*,uint32_t id,void** range){
        const auto bytes=static_cast<uint8_t*>(range[0]);assert(id==71 && static_cast<uint8_t*>(range[1])-bytes==20 && bytes[0]==0x99 && !bytes[1]);
        assert(Field<uint32_t>(bytes,2)==55 && Field<uint16_t>(bytes,6)==41 && bytes[8]==1 && bytes[9]==1 && Field<uint32_t>(bytes,10)==90);
        assert(Field<uint16_t>(bytes,14)==12 && Field<uint16_t>(bytes,16)==34 && !Field<uint16_t>(bytes,18));survivorNativePackets.push_back(bytes[0]);
    });Put(image,0x18682b0,network.data());
    const auto originalPath=path;Survivors::Build b;b.classId=1;survivorNativePackets.clear();FlushInstructionCache(GetCurrentProcess(),image,0x2000000);
    for(unsigned mode:{1u,2u,3u}){
        player.dwAnimMode=mode;assert(SurvivorCast(game,&player,&target,b,0));
        assert(player.dwAnimMode==mode && !memcmp(&path,&originalPath,sizeof(path)) && !Field<void*>(skills.data(),0x18) && player.dwFlags==2);
    }
    assert(survivorNativeChecks==3 && survivorNativePackets.size()==3);
    // A failed preparation must not send a projectile or an effect packet.
    Put(rows[41].data(),0x4c,int16_t(32));Put(survivorSkill.data(),0,rows[41].data());
    Put(image,0x14bf220+32*8,reinterpret_cast<uintptr_t>(+[](Game*,Unit*,int,int){return 0;}));
    assert(!SurvivorCast(game,&player,&target,b,0) && survivorNativeChecks==3 && survivorNativePackets.size()==3 && !memcmp(&path,&originalPath,sizeof(path)));
    Put(rows[41].data(),0x4c,int16_t(38));assert(!SurvivorMobileSkill(rows[41].data()));
    stub(0x6d0470,+[](void*,const char*){return -1;});FlushInstructionCache(GetCurrentProcess(),image,0x2000000);
    assert(SurvivorSkillId(1,0)==41); // Empty linkers cannot hide compiled Fire Ball.
    memset(rows[41].data()+2,0,32);assert(SurvivorSkillId(1,0)==-1); // Never substitute Meteor.
    Put(image,0x1c9fae8,static_cast<void*>(nullptr));game->pClientList=nullptr;
    std::cout<<"Native Fire Ball resolves RMD's moved skill ID, aims projectiles and client effects at the same enemy, preserves idle/walk/run paths, and sends nothing on failed preparation.\n";
}
std::array<std::array<std::array<int,2>,16>,2> survivorAllocationStats{};
int survivorStatRequests=0,survivorStatPackets=0,survivorVitals=0;
int& SurvivorAllocationValue(Unit* unit,int stat){
    assert(stat>=0 && stat<16);return survivorAllocationStats[unit==&eventPlayers[0]?0:1][stat][1];
}
void NativeSurvivorStatAllocation(uint8_t* image){
    auto stub=[&](uint32_t rva,auto fn){const uint8_t jump[]{0xff,0x25,0,0,0,0};memcpy(image+rva,jump,6);auto target=reinterpret_cast<uintptr_t>(fn);memcpy(image+rva+6,&target,8);};
    eventPlayers={};auto& server=eventPlayers[0];auto& local=eventPlayers[1];
    Bytes<0xb58> serverRoot{},clientRoot{};D2PlayerDataStrc data{};Client client{};
    auto game=fixture.GamePtr();game->dwGameFrame=100;game->dwInitSeed=91;
    server.dwUnitId=local.dwUnitId=55;server.dwClassId=local.dwClassId=1;server.dwAnimMode=local.dwAnimMode=1;
    server.pStatListEx=reinterpret_cast<D2StatListExStrc*>(serverRoot.data());local.pStatListEx=reinterpret_cast<D2StatListExStrc*>(clientRoot.data());server.pPlayerData=&data;Put(&data,0xf0,&client);
    client.dwClientId=71;client.dwClientState=4;client.dwUnitGUID=55;client.pGame=game;client.pPlayer=&server;Put(fixture.level.data(),0x1f8,int(39));
    survivorAllocationStats={};survivorStatRequests=survivorStatPackets=survivorVitals=0;
    for(size_t u=0;u<2;++u){for(size_t stat=0;stat<16;++stat)survivorAllocationStats[u][stat][0]=int(stat<<16);auto root=u?clientRoot.data():serverRoot.data();Put(root,0x30,survivorAllocationStats[u].data());Put(root,0x38,uint64_t(16));}
    auto avatar=std::make_unique<SurvivorAvatar>();avatar->game=game;avatar->client=&client;avatar->avatar=&server;avatar->baseAttributes={10,20,30,40};avatar->baseMaximum={10000,8000,6000};
    for(size_t stat=0;stat<4;++stat)SurvivorAllocationValue(&server,int(stat))=avatar->baseAttributes[stat];
    SurvivorAllocationValue(&server,4)=5;SurvivorAllocationValue(&server,12)=2;
    for(int n=0;n<3;++n)SurvivorAllocationValue(&server,6+n*2)=SurvivorAllocationValue(&server,7+n*2)=avatar->baseMaximum[n]+(n?4:8)*256;
    survivorAvatars[&client]=std::move(avatar);
    std::array<Bytes<0x144>,16> costs{};std::array<Bytes<0xd0>,2> classes{};classes[1][0x42]=8;classes[1][0x43]=4;classes[1][0x44]=8;
    Put(image,0x1c9fb78,costs.data());Put(image,0x1c9fb80,uint64_t(costs.size()));Put(image,0x1c9fb60,classes.data());Put(image,0x1c9fb68,uint64_t(classes.size()));Put(image,0x1c9fae8,static_cast<void*>(nullptr));Put(image,0x1d60af8,uint32_t(0));
    stub(0x1e2e70,+[](void* root,uint32_t key,void*){auto values=Field<int*>(root,0x30);return values[(key>>16)*2+1];});
    stub(0x1e5620,+[](Unit* unit,uint32_t stat,uint16_t){return stat<16?SurvivorAllocationValue(unit,int(stat)):0;});
    stub(0x1e5440,+[](Unit* unit,int stat,int value,uint16_t){SurvivorAllocationValue(unit,stat)=value;});
    stub(0x1e5590,+[](Unit* unit,int stat,int delta){assert(unit==&eventPlayers[0]);SurvivorAllocationValue(unit,stat)+=delta;});
    stub(0x22e750,+[](Unit* unit){assert(unit==&eventPlayers[0]);});stub(0x2d67e0,+[](Game*,Unit*,int){});
    stub(0x6b050,+[](uint32_t)->Unit*{return &eventPlayers[1];});stub(0x209530,ParticipantRoom);stub(0x2c49c0,SurvivorLookup);
    stub(0x143d184,+[](const char* value){return atoi(value);});image[0x1434c50]=0xc3;
    Put(image,0x149cef8,reinterpret_cast<uintptr_t>(+[](int)->SHORT{return 0;})); // No modifier keys.
    stub(0x2d0130,+[](Client*){});
    stub(0x2aaa00,+[](Unit* source,Unit* recipient,int force){assert(source==&eventPlayers[0] && recipient==source && !force);++survivorVitals;});
    stub(0x2192d0,+[](Unit*,int,int,int){});stub(0x2cce40,+[](Client*,Unit*,uint16_t,uint8_t,uint8_t){});stub(0x29a2a0,+[](Unit*,int,int,int){});
    stub(0x422ac0,CaptureSurvivorPacket);
    std::array<uintptr_t,6> network{};network[5]=reinterpret_cast<uintptr_t>(+[](void*,uint32_t id,void** range){
        const auto bytes=static_cast<uint8_t*>(range[0]);const auto size=static_cast<uint8_t*>(range[1])-bytes;assert(id==71 && bytes[1]<=12);
        const uint32_t value=bytes[0]==0x1d?bytes[2]:bytes[0]==0x1e?Field<uint16_t>(bytes,2):Field<uint32_t>(bytes,2);
        assert((bytes[0]==0x1d && size==3) || (bytes[0]==0x1e && size==4) || (bytes[0]==0x1f && size==6));
        SurvivorAllocationValue(&eventPlayers[1],bytes[1])=int(value);++survivorStatPackets;
    });Put(image,0x18682b0,network.data());
    stub(0x10e710,+[](uint8_t* bytes,int size){
        assert(size==5 && bytes[0]==0x3a);++survivorStatRequests;
        assert(!Native<int64_t(__fastcall*)(Game*,Unit*,uint8_t*,int)>(0x2bbd10)(fixture.GamePtr(),&eventPlayers[0],bytes,size));
    });
    games.clear();auto& state=State(game);state.run=std::make_unique<Run>();auto& run=*state.run;installed=true;game->pClientList=&client;
    run.phase=Phase::Running;run.id=123;run.level=39;run.event=Event::Survivors;run.eventPhase=EventPhase::Active;run.returns[55]={1,0,0};
    auto& build=run.survival.builds[55];build.classId=1;build.level=2;build.ranks[0]=2;build.offer=7;build.allocating=true;
    Bytes<0x20> message{};Put(message.data(),0x10,"0");Put(message.data(),0x18,uint8_t(1));
    FlushInstructionCache(GetCurrentProcess(),image,0x2000000);
    // Reproduce the failure with the actual native '+' callback: the server
    // has five points, but the paused client still sees zero and sends nothing.
    Native<void(__fastcall*)(void*,void*)>(0xbcb8a0)(nullptr,message.data());assert(!survivorStatRequests && SurvivorAllocationValue(&server,4)==5);
    SendSurvivorState(game,run,&client);assert(SurvivorAllocationValue(&local,4)==5 && survivorStatPackets==6);
    // Keep the world frame frozen while the actual UI callback, five-byte
    // request handler and native spending functions process every attribute.
    for(const char* button:{"0","3","2","1","3"}){
        Put(message.data(),0x10,button);Native<void(__fastcall*)(void*,void*)>(0xbcb8a0)(nullptr,message.data());
        SendSurvivorState(game,run,&client);assert(game->dwGameFrame==100 && build.allocating && !build.Pending());
    }
    assert(survivorStatRequests==5 && !build.Unspent() && !SurvivorAllocationValue(&local,4));
    assert((build.attributes==std::array<uint16_t,4>{1,2,1,1}));
    assert(SurvivorAllocationValue(&server,7)==10000+8*256+8*64 && SurvivorAllocationValue(&server,9)==8000+4*256+2*8*64);
    Native<void(__fastcall*)(void*,void*)>(0xbcb8a0)(nullptr,message.data());assert(survivorStatRequests==5); // Zero points disables spending again.
    const auto sent=survivorStatPackets;build.allocating=false;SendSurvivorState(game,run,&client);assert(survivorStatPackets==sent);
    // The active HUD stat controls use the same custom command route as
    // upgrade/resume buttons, so they do not depend on native modal UI input.
    build.allocating=true;build.attributes={};auto& a=*survivorAvatars.at(&client);SurvivorApplyLevel(a,build,true);
    Survivors::Request request;auto& p=request.data;p.action=Survivors::Action::Allocate;p.run=123;p.session=91;p.offer=7;p.points=1;
    auto command=[&](){assert(SurvivorCommand(game,&server,reinterpret_cast<const uint8_t*>(&request),sizeof(request)));};
    p.run=124;command();p.run=123;p.session=92;command();p.session=91;p.offer=8;command();p.offer=7;
    p.slot=4;command();p.slot=0;p.points=0;command();p.points=6;command();p.points=1;p.spent=1;command();p.spent=0;
    assert(SurvivorAllocationValue(&server,4)==5 && build.Unspent()==5);
    command();assert(build.attributes[0]==1 && build.Unspent()==4 && survivorPackets.back().totalAttributes[0]==11);
    command();assert(build.attributes[0]==1 && build.Unspent()==4); // Replay rejected.
    p.spent=1;p.slot=1;p.points=2;command();assert(build.attributes[1]==2 && build.Unspent()==2);
    p.spent=3;p.slot=3;p.points=1;command();p.spent=4;p.slot=2;command();
    assert((build.attributes==std::array<uint16_t,4>{1,2,1,1}) && !build.Unspent() && game->dwGameFrame==100 && build.allocating);
    p.spent=5;command();assert(!build.Unspent() && !SurvivorAllocationValue(&server,4));
    assert(SurvivorAllocationValue(&server,7)==10000+8*256+8*64 && SurvivorAllocationValue(&server,9)==8000+4*256+2*8*64);
    p.action=Survivors::Action::Ready;command();assert(!build.Paused());
    p.action=Survivors::Action::Allocate;command();assert(!build.Paused() && !build.Unspent());
    // A complete five-point bulk request is bounded by the same host budget.
    build.allocating=true;build.attributes={};SurvivorApplyLevel(a,build,true);p.slot=1;p.points=5;p.spent=0;command();
    assert(build.attributes[1]==5 && !build.Unspent() && game->dwGameFrame==100);
    installed=false;games.clear();game->pClientList=nullptr;
    survivorAvatars.clear();
    std::cout<<"Actual native '+' callback reproduces the stale zero-point client; paused stat publication enables all four attributes through real allocation handlers without advancing the world or overspending.\n";
}
std::array<int,3> survivorPauseCalls{};
void NativeSurvivorPause(uint8_t* image){
    auto stub=[&](uint32_t rva,auto fn){const uint8_t jump[]{0xff,0x25,0,0,0,0};memcpy(image+rva,jump,6);auto target=reinterpret_cast<uintptr_t>(fn);memcpy(image+rva+6,&target,8);};
    static Bytes<0x180> profiler{};
    stub(0xb09710,+[]()->void*{return profiler.data();});
    stub(0x6a9d0,+[](){++survivorPauseCalls[0];});
    stub(0x6aef0,+[](){assert(!Field<float>(Field<void*>(Native<void*>(0x1c9b3f0),0),0x18));++survivorPauseCalls[1];});
    stub(0xdfb30,+[](uint32_t i){assert(i<8);++survivorPauseCalls[2];});
    stub(0x6b050,+[](uint32_t i)->Unit*{return i?nullptr:&eventPlayers[0];});stub(0x209530,ParticipantRoom);
    Bytes<0x90> clock{};Put(image,0x1c9b3f0,clock.data());Put(image,0x1d442d8,uint32_t(0));Put(image,0x1840a0f,uint8_t(1));
    Put(image,0x1840a0c,uint8_t(0));Put(image,0x1d47325,uint8_t(0));Put(image,0x1d64170,float(0));
    eventPlayers[0]={};eventPlayers[0].dwUnitId=55;Put(fixture.level.data(),0x1f8,int(39));
    view={};view.run=7;view.session=91;view.recipient=55;view.level=39;view.active=1;view.event=Event::Survivors;view.eventPhase=EventPhase::Active;
    survivorView={};survivorView.run=7;survivorView.session=91;survivorView.recipient=55;survivorView.paused=1;survivorView.phase=EventPhase::Active;
    receivedAt=survivorReceived=GetTickCount64();installed=true;survivorsAvailable=true;survivorPauseCalls={};
    originalSurvivorClientTick=Native<SurvivorClientTick>(0x6ff60);FlushInstructionCache(GetCurrentProcess(),image,0x2000000);
    // Explicitly clear both *actual* native flags: the PE default for classic
    // is already one, which previously hid an incorrect RIP-target address.
    // Exercise HD as well as classic, preserving both fractional phase and
    // all neighboring bytes across a long pause and a second upgrade.
    Put(image,0x1840a05,uint8_t(0x5a));Put(image,0x1d4731e,uint8_t(0xa5));
    for(float selector:{0.0f,1.0f}){
        survivorClientClock={};Put(image,0x1d64170,selector);
        Put(clock.data(),8,uint32_t(5000));Put(clock.data(),0xc,uint32_t(4980));Put(clock.data(),0x18,float(.5));
        for(uint32_t now:{5000u,9000u,15000u}){
            Put(clock.data(),0,uint64_t(now)*1000000);
            assert(!SurvivorClientTickHook(now));
            assert(Field<uint32_t>(clock.data(),8)==5000 && Field<uint32_t>(clock.data(),0xc)==4980 && Field<float>(clock.data(),0x18)==.5f);
            assert(!Field<uint8_t>(image,0x1840a0c) && !Field<uint8_t>(image,0x1d47325));
            assert(Field<uint8_t>(image,0x1840a05)==0x5a && Field<uint8_t>(image,0x1d4731e)==0xa5);
        }
    }
    assert((survivorPauseCalls==std::array<int,3>{6,6,6}));
    assert(!Field<uint8_t>(image,0x1840a0c) && !Field<uint8_t>(image,0x1d47325) && !Field<uint32_t>(clock.data(),0x14));
    // Resume through the actual normal world branch, trapping simulation
    // steps. A long upgrade must not produce a catch-up burst, in the arena
    // or after the normal hero is restored in town.
    static int worldSteps=0;worldSteps=0;
    for(uint32_t rva:{0x6fd90u,0x6dac0u,0x6d3c0u,0xa6f20u,0x69b30u,0x1d16e0u,0xe2590u,0xc28d0u,0x2424d0u,0x964c0u,0x1d1520u,0x6de00u,0xb88a30u,0xb86d00u})stub(rva,+[](){});
    stub(0x69130,+[](){++worldSteps;});stub(0x21fff0,+[](){return 0;});
    static Bytes<0x500> context{};static Bytes<0x80> act{};
    stub(0x214900,+[]()->void*{return context.data();});Put(image,0x1bf1ea0,act.data());
    // Interpolation after resume receives a positive, bounded render delta.
    stub(0x6aef0,+[](){auto c=Field<void*>(Native<void*>(0x1c9b3f0),0);auto delta=Field<float>(c,0x18);assert(delta>=0 && delta<=1);});
    FlushInstructionCache(GetCurrentProcess(),image,0x2000000);
    survivorView.paused=0;Put(clock.data(),0,uint64_t(15017)*1000000);
    assert(SurvivorClientTickHook(15017) && !worldSteps);
    assert(Field<uint32_t>(clock.data(),8)==5017 && Field<uint32_t>(clock.data(),0xc)==4980);
    Put(clock.data(),0,uint64_t(15034)*1000000);
    assert(SurvivorClientTickHook(15034) && worldSteps==1);
    Put(fixture.level.data(),0x1f8,int(1));Put(clock.data(),0,uint64_t(15051)*1000000);
    assert(SurvivorClientTickHook(15051) && worldSteps==1);
    assert(Field<uint32_t>(clock.data(),8)==5051 && Field<uint32_t>(clock.data(),0xc)==5020);
    // New games use native elapsed time again, including reused allocations.
    Put(clock.data(),0,uint64_t(17)*1000000);Put(clock.data(),8,uint32_t(0));Put(clock.data(),0xc,uint32_t(0));
    assert(SurvivorClientTickHook(17) && Field<uint32_t>(clock.data(),8)==17 && worldSteps==1);
    // The caller pumps the local server on its original 40-ms schedule and
    // marks clock+10 before this update. Execute real reconciliation in the
    // native world routine: it must copy a server tick at that tick's phase,
    // never partway through a fractional client frame after Resume.
    static uint32_t serverTick=0;static int phaseErrors=0,copies=0;
    serverTick=0;phaseErrors=copies=0;
    stub(0x6d3c0,+[](Unit*){
        auto c=Field<void*>(Native<void*>(0x1c9b3f0),0);
        if(Field<uint32_t>(c,0xc)+survivorClientClock.offset!=serverTick)++phaseErrors;
        ++copies;
    });
    memset(image+0x1d442e0,0,0x800);Put(image,0x1d442e0,&eventPlayers[0]);
    Put(image,0x1bf1f20,int(0));clock={};survivorClientClock={};
    Put(fixture.level.data(),0x1f8,int(39));
    FlushInstructionCache(GetCurrentProcess(),image,0x2000000);
    uint32_t nextServer=0;
    for(uint32_t now=0;now<24000;now+=17){
        const bool paused=(now>=4000 && now<14017) || (now>=16000 && now<17123);survivorView.paused=paused;
        if(now>=18000)Put(fixture.level.data(),0x1f8,int(1));
        Put(clock.data(),0,uint64_t(now)*1000000);
        if(now>=nextServer){serverTick=nextServer;nextServer+=40;Put(clock.data(),0x10,uint8_t(1));}
        if(SurvivorClientTickHook(now))Put(clock.data(),0x10,uint8_t(0));
    }
    assert(!phaseErrors && copies>=300);
    std::cout<<"All "<<copies<<" native reconciliations retain the server tick phase across two non-integral upgrade pauses and town; the old fractional offset mismatched every post-resume copy.\n";
    installed=false;survivorsAvailable=false;view={};survivorView={};receivedAt=survivorReceived=0;
    survivorClientClock={};
    std::cout<<"Actual classic/HD pause preserves render phase and neighboring flags; real native resume and town updates advance only elapsed frames, and new games reset time.\n";
}
void NativeSurvivorAssignment(uint8_t* image){
    auto stub=[&](uint32_t rva,auto fn){const uint8_t jump[]{0xff,0x25,0,0,0,0};memcpy(image+rva,jump,6);auto target=reinterpret_cast<uintptr_t>(fn);memcpy(image+rva+6,&target,8);};
    stub(0x2934b0,SurvivorLoadHook);stub(0x341670,+[](Game*,Unit*,Client*){});
    stub(0x218a70,+[](Unit*){});stub(0x2df880,+[](Game*,Unit*){});
    stub(0x1e6100,+[](Unit*,Unit*){});stub(0x2aaa00,+[](Unit*,Unit*,int){});
    stub(0x21a440,+[](Unit*,void*)->bool{return false;});stub(0x218c70,+[](Unit*,int,int)->void*{return nullptr;});stub(0x29a2a0,+[](Unit*,int,int,int){});
    stub(0x2d0130,+[](Client*){});image[0x1434c50]=0xc3;
    std::array<uintptr_t,6> network{};network[5]=reinterpret_cast<uintptr_t>(SurvivorNativeSend);Put(image,0x18682b0,network.data());
    Client client{};Unit avatar{},hero{};D2PlayerDataStrc data{};Bytes<0x20> skills{};auto game=fixture.GamePtr();Put(game,0x108,int(0));Put(game,0x5b8c,int(0));
    avatar.dwAnimMode=1;avatar.dwUnitId=99;avatar.pPlayerData=&data;Put(&avatar,0x100,skills.data());
    client.dwClientId=71;client.pPlayer=&avatar;client.pGame=game;client.dwUnitGUID=99;Put(&data,0xf0,&client);
    for(size_t i=0;i<16;++i)Put(&client,0x468+i*8,int16_t(-1));
    // A non-inline incoming save vector must survive native AddPlayer's two
    // buffer-consumption calls. The routine runs for real in this fixture.
    Bytes<0x78> buffers{};for(size_t i=0;i<buffers.size();++i)buffers[i]=uint8_t(i+31);
    memcpy(reinterpret_cast<uint8_t*>(&client)+0x1a0,buffers.data(),buffers.size());
    SurvivorAvatar a;a.game=game;a.client=&client;a.hero=&hero;a.avatar=&avatar;
    survivorNativePackets.clear();FlushInstructionCache(GetCurrentProcess(),image,0x2000000);
    assert(SurvivorBind(a,&avatar,true));assert(client.dwUnitGUID==99 && client.pPlayer==&avatar && Field<Client*>(&data,0xf0)==&client);
    assert(!survivorHandoff && !memcmp(reinterpret_cast<uint8_t*>(&client)+0x1a0,buffers.data(),buffers.size()));
    assert(std::find(survivorNativePackets.begin(),survivorNativePackets.end(),uint8_t(0xb))!=survivorNativePackets.end());
    std::cout<<"Real native player re-assignment sends the local-player packet and preserves every incoming save-vector byte across both native buffer-free calls.\n";
}
void NativeSurvivorSaveDispatch(uint8_t* image){
    // Execute the actual native dispatch and stack probe/cookie check for both
    // capacities. Mock only serialization, logging, unit data and packet output;
    // no files or live game state are touched by this private mapped fixture.
    static Game* expectedGame=nullptr;static Unit* expectedHero=nullptr;
    static uint32_t expectedCapacity=0,sends=0;
    std::array<uint8_t,0x200> game{};Unit hero{};D2PlayerDataStrc playerData{};Client client{};
    expectedGame=reinterpret_cast<Game*>(game.data());expectedHero=&hero;hero.pPlayerData=&playerData;
    client.dwClientId=71;Put(game.data(),0x108,int(1));
    const auto clientSlot=0x1d637f0+(client.dwClientId&255)*8;
    const auto previousClient=Field<void*>(image,clientSlot);Put(image,clientSlot,static_cast<void*>(nullptr));
    std::map<uint32_t,std::array<uint8_t,16>> originals;
    auto stub=[&](uint32_t rva,auto fn){
        memcpy(originals[rva].data(),image+rva,16);
        const uint8_t jump[]{0xff,0x25,0,0,0,0};memcpy(image+rva,jump,6);
        const auto target=reinterpret_cast<uintptr_t>(fn);memcpy(image+rva+6,&target,8);
    };
    stub(0x2a08a0,+[](Client*){});
    stub(0x27aff0,+[](){});
    stub(0x208330,+[](Unit* unit)->void*{assert(unit==expectedHero);return unit->pPlayerData;});
    stub(0x318c80,+[](Game* current,Unit* unit,uint8_t* buffer,uint32_t* written,uint32_t capacity){
        assert(current==expectedGame && unit==expectedHero && capacity==expectedCapacity);
        // Filling the entire buffer verifies relocated cookie and saved-register
        // slots still survive a 32-KiB save, beyond the original 8-KiB boundary.
        memset(buffer,0xa5,capacity);*written=capacity;return 0;
    });
    stub(0x2a06f0,+[](uint32_t id,const uint8_t* buffer,uint32_t size,bool final){
        assert(id==71 && size==expectedCapacity && final);
        for(uint32_t i=0;i<size;++i)assert(buffer[i]==0xa5);
        ++sends;
    });
    for(const auto capacity:{0x2000u,0x8000u}){
        for(const auto rva:SurvivorsNative::SaveLimitPatchSites)image[rva]=uint8_t(capacity>>8);
        expectedCapacity=capacity;sends=0;
        FlushInstructionCache(GetCurrentProcess(),image,0x261a000);
        Native<SurvivorSendSave>(0x28f2b0)(expectedGame,&hero,"fixture",&client);
        assert(sends==1 && (Field<uint32_t>(&client,0x460)&0x10));
    }
    for(const auto& [rva,bytes]:originals)memcpy(image+rva,bytes.data(),bytes.size());
    for(const auto rva:SurvivorsNative::SaveLimitPatchSites)image[rva]=0x20;
    Put(image,clientSlot,previousClient);FlushInstructionCache(GetCurrentProcess(),image,0x261a000);
    std::cout<<"Native save dispatch fills and transmits 8-KiB/32-KiB buffers with its real stack probe, cookie and register slots intact.\n";
}
using NativePetReturn=int(__fastcall*)(Game*,Unit*,Unit*);
uintptr_t survivorPetFault=0;Room* survivorPetOwnerRoom=nullptr;int survivorPetMoves=0;
LONG SurvivorPetException(EXCEPTION_POINTERS* exception){
    if(exception->ExceptionRecord->ExceptionCode!=EXCEPTION_ACCESS_VIOLATION)return EXCEPTION_CONTINUE_SEARCH;
    survivorPetFault=reinterpret_cast<uintptr_t>(exception->ExceptionRecord->ExceptionAddress);return EXCEPTION_EXECUTE_HANDLER;
}
int SurvivorTryPetReturn(NativePetReturn function,Game* game,Unit* owner,Unit* pet){
    __try{return function(game,owner,pet);}__except(SurvivorPetException(GetExceptionInformation())){return -1;}
}
void NativeSurvivorOwnerRoom(uint8_t* image){
    // Execute the exact native pet relocation routine from the live crash.
    // Its owner-room dereference and native GetRoom remain unmodified.
    std::map<uint32_t,Bytes<14>> saved;
    auto stub=[&](uint32_t rva,auto function){Bytes<14> bytes{};memcpy(bytes.data(),image+rva,bytes.size());saved.emplace(rva,bytes);
        const uint8_t jump[]{0xff,0x25,0,0,0,0};memcpy(image+rva,jump,6);auto target=reinterpret_cast<uintptr_t>(function);memcpy(image+rva+6,&target,8);};
    survivorPetOwnerRoom=reinterpret_cast<Room*>(fixture.r1[0].data());survivorPetMoves=0;
    stub(0x25b420,+[](void* room,int,int)->void*{assert(room==Field<void*>(survivorPetOwnerRoom,0x18));return room;});
    stub(0x2dc620,+[](Game*,Room*,void*,int,int*,int*,int){return 0;});
    stub(0x2090c0,+[](Unit*)->uint32_t{return (60u<<16)|40u;});
    stub(0x2232c0,+[](Room* room,int,int)->Room*{assert(room==survivorPetOwnerRoom);return room;});
    stub(0x2c7a90,+[](Game*,Unit* pet,Room* room,int,int,int){assert(pet->dwUnitType==1 && room==survivorPetOwnerRoom);++survivorPetMoves;return 1;});
    stub(0x22e750,+[](Unit*){});stub(0x3c3da0,+[](Game*,Unit*,int){});
    Unit hero{},pet{};hero.dwUnitType=0;pet.dwUnitType=1;pet.dwClassId=2;D2DynamicPathStrc path{};hero.pDynamicPath=&path;
    Put(&path,2,uint16_t(40));Put(&path,6,uint16_t(60));
    SurvivorAvatar avatar;avatar.hero=&hero;avatar.townRoom=survivorPetOwnerRoom;
    const auto relocate=reinterpret_cast<NativePetReturn>(image+0x38d1a0);
    FlushInstructionCache(GetCurrentProcess(),image,0x400000);
    assert(SurvivorTryPetReturn(relocate,fixture.GamePtr(),&hero,&pet)==-1 && survivorPetFault==reinterpret_cast<uintptr_t>(image)+0x38d21b);
    SurvivorParkedRoom(avatar,true);
    assert(GetRoom(&hero)==survivorPetOwnerRoom && SurvivorTryPetReturn(relocate,fixture.GamePtr(),&hero,&pet)==1 && survivorPetMoves==1);
    SurvivorParkedRoom(avatar,false);assert(!GetRoom(&hero));
    for(const auto& [rva,bytes]:saved)memcpy(image+rva,bytes.data(),bytes.size());
    FlushInstructionCache(GetCurrentProcess(),image,0x400000);
    std::cout<<"Actual native pet relocation reproduces the parked-owner 38d21b crash; retaining its pinned town room permits relocation and clears before hero reattachment.\n";
}
void NativeSurvivorsABI(uint8_t* image){
    auto stub=[&](uint32_t rva,auto fn){const uint8_t jump[]{0xff,0x25,0,0,0,0};memcpy(image+rva,jump,6);auto target=reinterpret_cast<uintptr_t>(fn);memcpy(image+rva+6,&target,8);};
    stub(0x21a990,+[](Unit*,void*,int){return 0;});stub(0x2d0130,+[](Client*){});image[0x1434c50]=0xc3;
    eventPlayers={};auto& player=eventPlayers[0];player.dwUnitId=55;
    Client client{};client.dwClientId=71;
    // Exercise the real learned-skill packet writer, rather than confusing it
    // with the health/mana update routine. All classes publish their primary
    // at level one and keep their other two skills locked.
    stub(0x21a990,+[](Unit*,void*,int){return 0;});stub(0x2d0130,+[](Client*){});image[0x1434c50]=0xc3;
    std::array<uintptr_t,6> network{};
    network[5]=reinterpret_cast<uintptr_t>(+[](void*,uint32_t id,void** range){
        auto bytes=static_cast<uint8_t*>(range[0]);assert(id==71 && static_cast<uint8_t*>(range[1])-bytes==12 && bytes[0]==0x21 && !bytes[1] && !bytes[2]);
        assert(Field<uint32_t>(bytes,3)==55 && !bytes[10] && !bytes[11]);survivorPublished[Field<uint16_t>(bytes,7)]=bytes[9];
    });
    Put(image,0x18682b0,network.data());client.dwClientId=71;survivorPublished.clear();FlushInstructionCache(GetCurrentProcess(),image,0x2000000);
    for(const auto& cls:Survivors::Classes)for(size_t i=0;i<3;++i){
        Native<void(__fastcall*)(Client*,Unit*,uint16_t,uint8_t,uint8_t)>(0x2cce40)(&client,&player,uint16_t(cls.ids[i]),uint8_t(i==0),0);
        assert(survivorPublished.at(cls.ids[i])==int(i==0));
    }
    std::cout<<"Native learned-skill packets publish the correct unit, class skill IDs, level-one primary and locked secondary skills for all seven classes.\n";
    NativeMobileSurvivorEffect(image);
    NativeSurvivorAssignment(image);
    NativeSurvivorPause(image);
    NativeSurvivorStatAllocation(image);
}
