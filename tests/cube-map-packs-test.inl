// Included after the base cube-map Fixture helpers.
Bytes<0x2d0> packRegion{};
std::array<Bytes<0x1dc>,3> packRecords{};
std::vector<uint8_t> packLevels;
uint8_t* packTable=nullptr;uint8_t* packLevelTable=nullptr;
uint64_t packRecordCount=3;
std::array<Unit,6000> packUnits{};
std::array<D2MonsterDataStrc,6000> packData{};
size_t packUnitsUsed=0;int packLeaderCalls=0,packGroups=0;
bool packNoSpace=false,packPartial=false,packForceChampion=false;
Room* packRoom=nullptr;
Unit* AddPackMember(Room* room,int monster,bool champion=false){
    assert(packUnitsUsed<packUnits.size());const auto n=packUnitsUsed++;
    auto unit=&packUnits[n];*unit={};packData[n]={};
    unit->dwUnitType=1;unit->dwClassId=monster;unit->dwUnitId=uint32_t(1000+n);unit->dwAnimMode=1;
    unit->pMonsterData=&packData[n];packData[n].pMonstatsTxt=reinterpret_cast<D2MonStatsTxt*>(packRecords[monster].data());
    packData[n].nTypeFlag=champion ? 5 : 1;
    unit->pRoomNext=Field<Unit*>(room,0xa8);Put(room,0xa8,unit);return unit;
}
void __fastcall PackFree(Room* room,void*,int size,int mask,Room** out){assert(size==3 && mask==0x400);*out=packNoSpace ? nullptr : room;}
Unit* __fastcall PackLeader(Game* game,Room* room,void* coords,int x,int y,int guid,int monster,int flags){
    assert(game==fixture.GamePtr() && !coords && x>=100 && y>=100 && guid==-1 && flags==1);
    ++packLeaderCalls;packRoom=room;
    Put(packRegion.data(),0x2c8,Field<uint32_t>(packRegion.data(),0x2c8)+1);
    return AddPackMember(room,monster);
}
void __fastcall PackAssign(Game*,Unit* leader,int champions){assert(champions==1);if(packForceChampion || packGroups%2==0)leader->pMonsterData->nTypeFlag|=4;}
void __fastcall PackFinish(Game*,Room* room,void* coords,Unit* leader,int spawn,int low,int high){
    assert(!coords && spawn==1 && low==3 && high==6);
    if(!(leader->pMonsterData->nTypeFlag&4) && !packPartial)for(int n=0;n<3;++n)AddPackMember(room,int(leader->dwClassId));
}
void __fastcall PackChampions(Game*,Unit* leader,void* coords,int monster){
    assert(!coords && int(leader->dwClassId)==monster);
    if((leader->pMonsterData->nTypeFlag&4) && !packPartial)for(int n=0;n<3;++n)AddPackMember(packRoom,monster,true);
    ++packGroups;
}
void __fastcall PackRemove(Game*,Unit* unit){
    auto link=reinterpret_cast<Unit**>(fixture.r1[0].data()+0xa8);
    while(*link && *link!=unit)link=&(*link)->pRoomNext;
    assert(*link==unit);*link=unit->pRoomNext;
}
Run PackSetup(){
    Reset();packRegion={};packRecords={};packUnitsUsed=0;packLeaderCalls=packGroups=0;packNoSpace=packPartial=packForceChampion=false;
    packTable=packRecords[0].data();packLevels.assign(size_t(levels)*0x184,0xff);packLevelTable=packLevels.data();
    for(int id=0;id<3;++id){Put(packRecords[id].data(),0x3c,uint32_t((1u<<25)|(1u<<15)|1u));Put(packRecords[id].data(),0x56,int16_t(-1));packRecords[id][0x80]=0;}
    Put(packLevels.data()+size_t(256)*0x184,0x9e,int16_t(1));
    bindings[0x1c9f878]=reinterpret_cast<uintptr_t>(&packTable);bindings[0x1c9f880]=reinterpret_cast<uintptr_t>(&packRecordCount);
    bindings[0x1c9fd08]=reinterpret_cast<uintptr_t>(&packLevelTable);bindings[0x2a80d0]=reinterpret_cast<uintptr_t>(PackFree);
    bindings[0x2fad40]=reinterpret_cast<uintptr_t>(PackLeader);bindings[0x2faae0]=reinterpret_cast<uintptr_t>(PackAssign);
    bindings[0x2fddd0]=reinterpret_cast<uintptr_t>(PackFinish);bindings[0x2dce30]=reinterpret_cast<uintptr_t>(PackChampions);
    originalRemove=PackRemove;
    Put(fixture.game.data(),0x1d8+size_t(256)*8,packRegion.data());Put(packRegion.data(),0x2bc,uint8_t(31));Put(packRegion.data(),0x2bd,uint8_t(42));
    Put(fixture.r1[0].data(),0xa8,uintptr_t(0));Put(fixture.r1[0].data(),0x80,int(100));Put(fixture.r1[0].data(),0x84,int(100));Put(fixture.r1[0].data(),0x88,int(200));Put(fixture.r1[0].data(),0x8c,int(200));
    Run run;run.level=256;run.roster={0,1};run.pins.push_back({fixture.r2[0].data(),false});return run;
}
void MapPacks(){
    auto run=PackSetup();auto game=fixture.GamePtr();
    {MapPackPopulationScope scope(game,run,true);assert(scope.Valid() && !Field<uint16_t>(packRegion.data(),0x2bc));}
    assert(Field<uint8_t>(packRegion.data(),0x2bc)==31 && Field<uint8_t>(packRegion.data(),0x2bd)==42);
    for(const auto event:{Event::Trial,Event::Survivors,Event::MidasMeddling}){run.event=event;MapPackPopulationScope scope(game,run,true);assert(scope.Valid() && Field<uint8_t>(packRegion.data(),0x2bc)==31);assert(PopulateMapPacks(game,run,{256,103,103}));}
    run.event=Event::None;{MapPackPopulationScope scope(game,run,false);assert(scope.Valid() && Field<uint8_t>(packRegion.data(),0x2bc)==31);}
    Put(game,0x1d8+size_t(256)*8,uintptr_t(0));{MapPackPopulationScope scope(game,run,true);assert(!scope.Valid());}
    run=PackSetup();assert(MapPackRoster(run)==std::vector<int>{1});
    Put(packRecords[1].data(),0x56,int16_t(2));Put(packRecords[2].data(),0x3c,uint32_t(0));assert(!MapPackCandidate(1));
    run=PackSetup();auto room=reinterpret_cast<Room*>(fixture.r1[0].data());
    for(int n=0;n<3;++n){auto named=AddPackMember(room,1);named->pMonsterData->nTypeFlag=11;for(int m=0;m<3;++m)AddPackMember(room,1)->pMonsterData->nTypeFlag=0x11;}
    assert(ExistingMapPacks(run)==3);assert(PopulateMapPacks(game,run,{256,103,103}));
    assert(run.spawnedPacks==run.targetPacks && packLeaderCalls==run.targetPacks-3);
    run=PackSetup();AddPackMember(room,1,true);assert(!ExistingMapPacks(run));assert(!PopulateMapPacks(game,run,{256,103,103}) && !packLeaderCalls);
    run=PackSetup();for(int n=0;n<14;++n)AddPackMember(room,1)->pMonsterData->nTypeFlag=11;
    assert(!PopulateMapPacks(game,run,{256,103,103}) && !packLeaderCalls);
    Put(packRecords[1].data(),0x3c,uint32_t((1u<<15)|(1u<<6)));assert(ExistingMapPacks(run)==0);
    std::set<int> targets;
    for(unsigned seed=0;seed<80;++seed){
        run=PackSetup();run.random.seed(seed);assert(PopulateMapPacks(game,run,{256,103,103}));
        assert(run.targetPacks>=8 && run.targetPacks<=13 && run.spawnedPacks==run.targetPacks);
        assert(packLeaderCalls==run.targetPacks && packGroups==run.targetPacks && packUnitsUsed==size_t(run.targetPacks)*4);
        targets.insert(run.targetPacks);const auto used=packUnitsUsed;assert(PopulateMapPacks(game,run,{256,103,103}) && packUnitsUsed==used);
    }
    assert(targets.size()==6);
    run=PackSetup();packNoSpace=true;assert(!PopulateMapPacks(game,run,{256,103,103}) && !run.spawnedPacks && !packLeaderCalls);
    run=PackSetup();packPartial=true;assert(!PopulateMapPacks(game,run,{256,103,103}) && !run.spawnedPacks && packLeaderCalls<=512 && !Field<uint32_t>(packRegion.data(),0x2c8) && !Field<Unit*>(fixture.r1[0].data(),0xa8));
    std::cout<<"Ordinary maps contain exactly 8..13 elite groups, champions count once, UMon/minions stay eligible, scoped suppression restores settings, and insufficient placement refuses preparation.\n";
}
Unit* __fastcall PackNativeSibling(Game*,void* coords,Unit* leader,int monster,int mode,int distance,uint16_t flags){
    // This D2R wrapper's fifth argument is unused and its caller does not set it.
    assert(!coords && distance==4 && !flags && leader->pMonsterData->nTypeFlag&4);
    return AddPackMember(packRoom,monster,true);
}
void __fastcall PackNativeChampionMod(Game*,Unit* unit,int modifier){assert(modifier==16 && unit->pMonsterData->nTypeFlag&4);}
int packNormalGroups=0;
int __fastcall PackNativeChoose(Game*,void*,Room*,void** record,uint8_t chance,int unique){assert(chance==20 && !unique);*record=packRecords[1].data();return 1;}
Unit* __fastcall PackNativeNormal(Game*,Room*,void*,int monster,uint8_t low,uint8_t high){assert(monster==1 && low==1 && high==1);++packNormalGroups;return nullptr;}
void NativeMapPacks(uint8_t* image){
    const auto savedPopulation=populationCode;
    const std::array<uint32_t,9> addresses{0x2a80d0,0x2fad40,0x2faae0,0x2fddd0,0x304ec0,0x302820,0x2dce30,0x28b920,0x2dca20};std::array<Bytes<14>,9> saved{};
    for(size_t n=0;n<addresses.size();++n)memcpy(saved[n].data(),image+addresses[n],14);
    auto stub=[&](uint32_t rva,auto fn){const uint8_t jump[]{0xff,0x25,0,0,0,0};memcpy(image+rva,jump,6);auto target=reinterpret_cast<uintptr_t>(fn);memcpy(image+rva+6,&target,8);};
    auto run=PackSetup();packForceChampion=true;
    Put(image,0x1c9f878,packTable);Put(image,0x1c9f880,packRecordCount);Put(image,0x1c9fd08,packLevelTable);Put(image,0x1c9fd10,levels);
    stub(0x2a80d0,PackFree);stub(0x2fad40,PackLeader);stub(0x2faae0,PackAssign);stub(0x2fddd0,PackFinish);
    stub(0x304ec0,PackNativeSibling);stub(0x302820,PackNativeChampionMod);FlushInstructionCache(GetCurrentProcess(),image,0x2000000);
    for(int n=0;n<10;++n){const auto before=packUnitsUsed;assert(SpawnMapPack(fixture.GamePtr(),run,reinterpret_cast<Room*>(fixture.r1[0].data()),{256,150,150},1));assert(packUnitsUsed-before>=2 && packUnitsUsed-before<=4);assert(run.spawnedPacks==n+1);}
    // Exercise the real population routine's inline elite decision. Its
    // original min/max would choose an elite here; the scope forces a normal
    // group while retaining the native density and ordinary spawn logic.
    Put(image,0x17f79bc,int(1));Put(packRegion.data(),0x0c,int(1));Put(packRegion.data(),0x2b8,int(10000));Put(packRegion.data(),0x2c8,uint32_t(0));
    Put(fixture.nodes[0].data(),0x20,int(0));Put(fixture.nodes[0].data(),0x28,int(1));
    for(const auto [offset,value]:{std::pair{0x10,100},std::pair{0x14,100},std::pair{0x18,101},std::pair{0x1c,101}})Put(fixture.nodes[0].data(),offset,value);
    packRecords[1][0x5f]=packRecords[1][0x60]=1;packNormalGroups=0;
    stub(0x28b920,PackNativeChoose);stub(0x2dca20,PackNativeNormal);FlushInstructionCache(GetCurrentProcess(),image,0x2000000);
    const auto leaders=packLeaderCalls;{MapPackPopulationScope scope(fixture.GamePtr(),run,true);assert(scope.Valid());Native<void(__fastcall*)(Game*,Room*)>(NativeData::Source)(fixture.GamePtr(),reinterpret_cast<Room*>(fixture.r1[0].data()));}
    assert(packNormalGroups==1 && packLeaderCalls==leaders && Field<uint8_t>(packRegion.data(),0x2bc)==31 && Field<uint8_t>(packRegion.data(),0x2bd)==42);
    for(size_t n=0;n<addresses.size();++n)memcpy(image+addresses[n],saved[n].data(),14);FlushInstructionCache(GetCurrentProcess(),image,0x2000000);
    populationCode=savedPopulation;
    std::cout<<"Actual native champion-group routine creates 1..3 siblings per pack; actual native population suppression reaches normal groups without random elites.\n";
}
