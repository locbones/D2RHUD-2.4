// Included after cube-map-packs-test.inl.
int clashDensityAttempts=0;
bool clashInjectOrdinary=false;
void __fastcall ClashAssign(Game* game,Unit* leader,int champions){PackAssign(game,leader,champions);leader->pMonsterData->nTypeFlag|=8;}
void __fastcall ClashFinish(Game* game,Room* room,void* coords,Unit* leader,int spawn,int low,int high){
    const auto before=packUnitsUsed;PackFinish(game,room,coords,leader,spawn,low,high);
    for(size_t n=before;n<packUnitsUsed;++n)packUnits[n].pMonsterData->nTypeFlag|=0x10;
}
void __fastcall ClashDensity(Game* game,Room* room){
    for(int n=0;n<clashDensityAttempts;++n)if(auto leader=ChampionClashLeader(game,room,nullptr,150,150,-1,1,1)){
        ClashAssign(game,leader,1);ClashFinish(game,room,nullptr,leader,1,3,6);ChampionClashGroup(game,leader,nullptr,1);
    }
    if(clashInjectOrdinary)AddPackMember(room,1)->pMonsterData->nTypeFlag=0;
}
Run ClashSetup(){
    auto run=PackSetup();run.event=Event::ChampionClash;run.level=108;
    Put(fixture.level.data(),0x1f8,int(108));Put(fixture.game.data(),0x1d8+size_t(108)*8,packRegion.data());
    bindings[0x2faae0]=reinterpret_cast<uintptr_t>(ClashAssign);bindings[0x2fddd0]=reinterpret_cast<uintptr_t>(ClashFinish);
    clashDensityAttempts=9;clashInjectOrdinary=false;return run;
}
void ChampionClashPacks(){
    auto run=ClashSetup();auto game=fixture.GamePtr();auto room=reinterpret_cast<Room*>(fixture.r1[0].data());
    Unit object{};object.dwUnitType=2;object.dwUnitId=99;Put(room,0xa8,&object);
    AddPackMember(room,1)->pMonsterData->nTypeFlag=0;
    assert(PopulateChampionClashWith(game,run,{108,103,103},ClashDensity));
    assert(run.spawnedPacks==9 && packLeaderCalls==9 && !championClashContext);
    assert(Field<Unit*>(room,0xa8) && MapPackUnits(run).size()==36);
    bool preserved=false;for(auto unit:RoomUnits(room))if(unit==&object)preserved=true;assert(preserved);
    for(const auto& [id,unit]:MapPackUnits(run))assert(unit->pMonsterData->nTypeFlag&(4|8|0x10));
    run=ClashSetup();clashDensityAttempts=0;assert(!PopulateChampionClashWith(game,run,{108,103,103},ClashDensity));
    run=ClashSetup();packPartial=true;assert(!PopulateChampionClashWith(game,run,{108,103,103},ClashDensity) && MapPackUnits(run).empty());
    run=ClashSetup();clashDensityAttempts=ChampionClashPackLimit;assert(PopulateChampionClashWith(game,run,{108,103,103},ClashDensity) && run.spawnedPacks==ChampionClashPackLimit);
    run=ClashSetup();clashDensityAttempts=ChampionClashPackLimit+1;assert(!PopulateChampionClashWith(game,run,{108,103,103},ClashDensity));
    assert(packLeaderCalls==ChampionClashPackLimit && MapPackUnits(run).empty() && !championClashContext && !run.spawnedPacks);
    run=ClashSetup();clashInjectOrdinary=true;assert(!PopulateChampionClashWith(game,run,{108,103,103},ClashDensity) && MapPackUnits(run).empty());
    run=ClashSetup();run.level=39;assert(!PopulateChampionClashWith(game,run,{39,103,103},ClashDensity));
    run.event=Event::Trial;assert(PopulateChampionClashWith(game,run,{39,103,103},nullptr));
    std::cout<<"Champion Clash creates complete elite groups at every population attempt, removes old ordinary combat actors, preserves objects, bounds density, rolls back partial/ordinary contamination and scopes its context.\n";
}
int clashNativeChooseCalls=0;
int __fastcall ClashNativeChoose(Game*,void*,Room*,void** record,uint8_t chance,int unique){
    assert((chance==20 && !unique) || (!chance && unique==1));++clashNativeChooseCalls;*record=packRecords[1].data();return 1;
}
Unit* __fastcall ClashNativeLeader(Game* game,Room* room,void* coords,int x,int y,int guid,int monster,int flags){
    assert(coords==fixture.nodes[0].data() && !x && !y);return PackLeader(game,room,nullptr,150,150,guid,monster,flags);
}
void __fastcall ClashNativeFinish(Game* game,Room* room,void* coords,Unit* leader,int spawn,int low,int high){
    // Native 2fddd0 ignores its second argument; the optimized population
    // caller consequently does not initialize RDX at this call boundary.
    assert(coords==fixture.nodes[0].data());ClashFinish(game,packRoom,nullptr,leader,spawn,low,high);
}
void __fastcall ClashNativeChampions(Game* game,Unit* leader,void* coords,int monster){assert(coords==fixture.nodes[0].data());PackChampions(game,leader,nullptr,monster);}
void NativeChampionClashPacks(uint8_t* image){
    const auto savedPopulation=populationCode;
    const std::array<uint32_t,6> addresses{0x28b920,0x2fad40,0x2faae0,0x2fddd0,0x2dce30,0x2dca20};std::array<Bytes<14>,6> saved{};
    for(size_t n=0;n<addresses.size();++n)memcpy(saved[n].data(),image+addresses[n],14);
    auto stub=[&](uint32_t rva,auto fn){const uint8_t jump[]{0xff,0x25,0,0,0,0};memcpy(image+rva,jump,6);const auto target=reinterpret_cast<uintptr_t>(fn);memcpy(image+rva+6,&target,8);};
    auto run=ClashSetup();auto game=fixture.GamePtr();
    Put(image,0x1c9f878,packTable);Put(image,0x1c9f880,packRecordCount);Put(image,0x1c9fd08,packLevelTable);Put(image,0x1c9fd10,levels);
    Put(image,0x17f79bc,int(1));Put(packRegion.data(),0x0c,int(1));Put(packRegion.data(),0x2b8,int(10000));Put(packRegion.data(),0x2c8,uint32_t(0));
    Put(fixture.nodes[0].data(),0x20,int(0));Put(fixture.nodes[0].data(),0x28,int(1));
    for(const auto [offset,value]:{std::pair{0x10,100},std::pair{0x14,100},std::pair{0x18,102},std::pair{0x1c,102}})Put(fixture.nodes[0].data(),offset,value);
    // A retired Chaos quest record is deliberately absent. Level 108's
    // ordinary game population must not consult it in the private event copy.
    Put(game,0x21e0,uintptr_t(0));
    stub(0x28b920,ClashNativeChoose);stub(0x2fad40,ClashNativeLeader);stub(0x2faae0,ClashAssign);stub(0x2fddd0,ClashNativeFinish);stub(0x2dce30,ClashNativeChampions);stub(0x2dca20,PackNativeNormal);
    FlushInstructionCache(GetCurrentProcess(),image,0x2000000);clashNativeChooseCalls=packNormalGroups=0;
    championClashPopulation.reset();assert(BuildChampionClashPopulation());
    assert(championClashPopulation->code[0x283]==0xe9 && image[NativeData::Source+0x283]==0x44);
    assert(PopulateChampionClash(game,run,{108,103,103}));
    assert(run.spawnedPacks==9 && packLeaderCalls==9 && clashNativeChooseCalls==18 && !packNormalGroups);
    championClashPopulation.reset();
    for(size_t n=0;n<addresses.size();++n)memcpy(image+addresses[n],saved[n].data(),14);FlushInstructionCache(GetCurrentProcess(),image,0x2000000);
    populationCode=savedPopulation;
    std::cout<<"Actual private native room population retains all 9 density attempts, forces all 9 to elite groups, preserves the original executable bytes and never invokes ordinary-group creation.\n";
}
