uint32_t observedGeneration=0;void* nestedGeneration=nullptr;std::vector<uint32_t> generationHistory;
void __fastcall GenerateLayout(void* level){
    const auto drlg=Field<void*>(level,0x1c8);const auto seed=Field<uint32_t>(level,0x1e4);
    generationHistory.push_back(seed);observedGeneration=Field<uint32_t>(drlg,0x860);
    if(nestedGeneration){auto next=nestedGeneration;nestedGeneration=nullptr;InitLevelHook(next);}
}
void __fastcall NoFreeAct(void*){}
int releasedLayoutData=0;std::vector<void*> releasedLayoutPointers;std::set<void*> poisonReleasedRooms;
void __fastcall ReleaseLayoutData(void*,void* pointer){
    ++releasedLayoutData;releasedLayoutPointers.push_back(pointer);
    if(poisonReleasedRooms.contains(pointer))Set(pointer,0x90,reinterpret_cast<void*>(0x1100110001001100ULL));
}
int layoutFrees=0;void* allocatedLayout=nullptr;
void __fastcall FreeLayout(void*,int keep){assert(!keep);++layoutFrees;}
void* __fastcall AllocateLayout(void* drlg,int id){
    assert(id==256 || id==78);observedGeneration=Field<uint32_t>(drlg,0x860);
    Set(allocatedLayout,0x1f8,id);Set(allocatedLayout,0x1c8,drlg);
    Set(allocatedLayout,0x1b8,Field<void*>(drlg,0x868));Set(drlg,0x868,allocatedLayout);return allocatedLayout;
}
void LayoutRecords(){
    Reset();serverLayoutSeeds.clear();clientLayoutSeeds.clear();clientLayoutsApplied.clear();retiredLayoutRecords.clear();
    Bytes<0x880> drlg{};Bytes<0x288> old{},fresh{};Bytes<0x90> act{};
    Put(drlg.data(),0x860,uint32_t(7));Put(drlg.data(),0x868,old.data());Put(old.data(),0x1f8,int(256));Put(fresh.data(),0x1f8,int(256));Put(act.data(),0x70,drlg.data());
    bindings[0x1e7860]=reinterpret_cast<uintptr_t>(FreeLayout);bindings[0x1e7f70]=reinterpret_cast<uintptr_t>(AllocateLayout);allocatedLayout=fresh.data();layoutFrees=0;
    assert(FreshLayoutLevel(drlg.data(),256,123,old.data())==fresh.data());
    assert(layoutFrees==1 && observedGeneration==123 && Field<uint32_t>(drlg.data(),0x860)==7);
    assert(Field<void*>(drlg.data(),0x868)==fresh.data() && !Field<void*>(fresh.data(),0x1b8));
    assert(retiredLayoutRecords[drlg.data()]==std::vector<void*>({old.data()}));
    originalFreeAct=NoFreeAct;FreeActHook(act.data());
    assert(retiredLayoutRecords.empty() && Field<void*>(drlg.data(),0x868)==old.data() && Field<void*>(old.data(),0x1b8)==fresh.data());
    LayoutData::Wire wire;wire.level=256;wire.seed=123;assert(LayoutData::Valid(wire));wire.level=1;assert(!LayoutData::Valid(wire));wire.level=1024;assert(!LayoutData::Valid(wire));wire.level=256;wire.seed=0;assert(!LayoutData::Valid(wire));
    wire.seed=123;wire.version=2;assert(!LayoutData::Valid(wire));
    installed=false;
    std::cout<<"Layout records are replaced without duplicate lookup IDs, generation restores the act seed, and native teardown owns retired records.\n";
    const std::vector<int> jungle{541,0,570,575,539,534,576,569,535,538,570,575};
    std::vector<JungleData::Tile> decoded;for(auto id:jungle)decoded.push_back(JungleData::Decode(id));
    assert(JungleData::Valid(decoded,2));std::set<std::vector<int>> alternatives;
    for(uint32_t seed=1;seed<=64;++seed){
        auto rolled=JungleData::Roll(jungle,2,78,seed);assert(rolled==JungleData::Roll(jungle,2,78,seed));
        std::vector<JungleData::Tile> tiles;for(auto id:rolled)tiles.push_back(JungleData::Decode(id));
        assert(JungleData::Valid(tiles,2) && tiles[0].id==jungle[0] && tiles[1].id==jungle[1]);
        assert(std::count_if(rolled.begin(),rolled.end(),[](int id){return id>574;})==3);
        for(size_t n=0;n<tiles.size();++n){
            for(int bit:{1,2,4,8})if((bit==1 && n%2==0) || (bit==2 && n%2==1) || (bit==8 && n<2) || (bit==4 && n>=10))
                assert(JungleData::Edge(tiles[n],bit)==JungleData::Edge(decoded[n],bit));
        }
        alternatives.insert(rolled);
    }
    assert(alternatives.size()>8);
    for(int id:{76,77})for(uint32_t seed=1;seed<=16;++seed){
        auto rolled=JungleData::Roll(jungle,2,id,seed);std::vector<JungleData::Tile> tiles;
        for(auto cell:rolled)tiles.push_back(JungleData::Decode(cell));assert(JungleData::Valid(tiles,2));
        if(id==76)assert(std::equal(rolled.end()-2,rolled.end(),jungle.end()-2));
    }
    auto unknown=jungle;unknown[4]=9999;assert(JungleData::Roll(unknown,2,78,1)==unknown);
    assert(JungleData::Roll(jungle,3,78,1)==jungle);
    std::cout<<"Jungle rerolls change connected native river paths deterministically, keep every exterior exit and head/tail row, and retain three clearings across 64 seeds.\n";
}
Unit* layoutClientPlayer=nullptr;LayoutData::Ack receivedLayoutAck;int layoutAckCount=0;
Unit* __fastcall LayoutClientPlayer(uint32_t){return layoutClientPlayer;}
void __fastcall LayoutSend(void* bytes,int size){
    assert(size==sizeof(LayoutData::Ack));
    assert(!D3Whirlwind::Data::Ours({static_cast<const uint8_t*>(bytes),size_t(size)}));
    memcpy(&receivedLayoutAck,bytes,size);++layoutAckCount;
}
void LayoutSynchronization(){
    Reset();ClearClientLayouts();retiredLayoutRecords.clear();serverLayoutSeeds.clear();
    Bytes<0x880> drlg{};Bytes<0x288> fresh{};Put(fixture.act.data(),0x20,uint8_t(2));Put(fixture.act.data(),0x70,drlg.data());
    Put(drlg.data(),0x860,uint32_t(7));Put(drlg.data(),0x868,fixture.level.data());Put(drlg.data(),0x110,uint32_t(1));
    Unit player{};D2PlayerDataStrc playerData{};Client client{};player.dwUnitType=0;player.dwUnitId=1;player.pPlayerData=&playerData;
    player.pDrlgAct=reinterpret_cast<int64_t*>(fixture.act.data());playerData.pClient=&client;client.pGame=fixture.GamePtr();client.dwUnitGUID=1;client.dwClientId=17;
    fixture.GamePtr()->pClientList=&client;
    Bytes<0x108> town{};Bytes<0x1c0> town2{};Bytes<0x288> townLevel{};
    Put(town.data(),0x18,town2.data());Put(town2.data(),0x90,townLevel.data());Put(townLevel.data(),0x1f8,int(75));
    gateRooms[&player]=reinterpret_cast<Room*>(town.data());layoutClientPlayer=&player;
    uint32_t selector=0;bindings[0x1d442d8]=reinterpret_cast<uintptr_t>(&selector);bindings[0x6b050]=reinterpret_cast<uintptr_t>(LayoutClientPlayer);bindings[0x209530]=reinterpret_cast<uintptr_t>(GateRoom);
    bindings[0x10dce0]=reinterpret_cast<uintptr_t>(LayoutSend);bindings[0x1e7860]=reinterpret_cast<uintptr_t>(FreeLayout);bindings[0x1e7f70]=reinterpret_cast<uintptr_t>(AllocateLayout);
    allocatedLayout=fresh.data();layoutFrees=0;layoutAckCount=0;
    LayoutData::Wire packet;packet.recipient=1;packet.level=256;packet.seed=123;packet.session=uint32_t(fixture.GamePtr()->dwInitSeed);
    packet.epoch=State(fixture.GamePtr()).layoutEpoch;
    auto reset=packet;reset.level=0;reset.seed=0;
    assert(Receive(reinterpret_cast<const uint8_t*>(&reset),sizeof(reset)) && layoutAckCount==1);layoutAckCount=0;
    assert(Command(fixture.GamePtr(),&player,reinterpret_cast<const uint8_t*>(&receivedLayoutAck),sizeof(receivedLayoutAck)));
    assert(State(fixture.GamePtr()).layoutResets.contains({client.dwClientId,player.dwUnitId}));
    // Cached client actors prevent a reset and ACK until native teardown ends.
    assert(ReceiveLayout(packet) && !layoutAckCount && !layoutFrees);
    for(auto& room:fixture.r1)Put(room.data(),0xa8,static_cast<Unit*>(nullptr));
    assert(ReceiveLayout(packet) && layoutAckCount==1 && layoutFrees==1);
    assert(receivedLayoutAck.seed==123 && receivedLayoutAck.level==256 && receivedLayoutAck.session==packet.session);
    assert(Field<uint32_t>(drlg.data(),0x860)==7);
    assert(ReceiveLayout(packet) && layoutAckCount==2 && layoutFrees==1);
    auto& state=State(fixture.GamePtr());state.layoutSeeds[256]=123;
    auto stale=receivedLayoutAck;stale.seed=124;assert(AcknowledgeLayout(fixture.GamePtr(),&player,stale));assert(state.layoutAcks.empty());
    stale=receivedLayoutAck;stale.session^=1;assert(AcknowledgeLayout(fixture.GamePtr(),&player,stale));assert(state.layoutAcks.empty());
    stale=receivedLayoutAck;stale.epoch^=1;assert(AcknowledgeLayout(fixture.GamePtr(),&player,stale));assert(state.layoutAcks.empty());
    assert(AcknowledgeLayout(fixture.GamePtr(),&player,receivedLayoutAck));assert(state.layoutAcks.at({17,1,256})==123);
    client.dwUnitGUID=2;state.layoutAcks.clear();AcknowledgeLayout(fixture.GamePtr(),&player,receivedLayoutAck);assert(state.layoutAcks.empty());
    // Layer identity comes from the actual level-def layout, independent of
    // the player's newly switched level while the prior layer is saved.
    std::vector<Bytes<0x9c>> definitions(600);uint8_t* table=definitions[0].data();
    Put(definitions[256].data(),8,uint32_t(99));bindings[0x1c9fd28]=reinterpret_cast<uintptr_t>(&table);
    assert(ClientLayerSeed(99) && !ClientLayerSeed(98));const auto identity=ClientLayerSeed(99);
    clientLayoutSeeds[256]=124;assert(ClientLayerSeed(99)!=identity);
    // Act teardown must retain plans. The next game can reuse the offline
    // world seed; a new authenticated epoch still clears the old layout.
    for(auto& room:fixture.r1)Put(room.data(),0xa8,static_cast<Unit*>(nullptr));
    ++reset.epoch;const auto beforeReset=layoutAckCount;assert(ReceiveLayout(reset) && layoutAckCount==beforeReset+1 && clientLayoutSeeds.empty());
    assert(receivedLayoutAck.epoch==reset.epoch && !receivedLayoutAck.level && !receivedLayoutAck.seed);
    ClearClientLayouts();assert(!ClientLayerSeed(99));retiredLayoutRecords.clear();gateRooms.clear();installed=false;
    std::cout<<"Layout synchronization waits for cached-actor teardown, ACKs once per generation, rejects stale/foreign ACKs and isolates automap identities by seed.\n";
}
Unit* rerollCreator=nullptr;Unit* rerollLoot=nullptr;Room* rerollTown=nullptr;
void __fastcall ForbidOldPopulation(Game*,Room*){assert(false && "Rerolls must not populate old rooms and move newly generated loot to town");}
int lootMoves=0,lootNotifications=0;std::vector<uint32_t> rerollRemoved;
Room* __fastcall RerollRoom(Unit* unit){if(unit==rerollCreator || unit==rerollLoot)return gateRooms[unit];return MockRoom(unit);}
void __fastcall RerollRemove(Game*,Unit* unit){
    rerollRemoved.push_back(unit->dwUnitId);auto room=RerollRoom(unit);auto head=Field<Unit*>(room,0xa8);
    if(head==unit)Put(room,0xa8,unit->pRoomNext);
}
void __fastcall RerollSpace(Room* room,void*,int,int,Room** result){*result=room;}
Room* __fastcall RerollItemSpace(Room* room,void* coordinate,void* pathOrigin,int size,int mask,int pathMask,int a7,int distance,int a9){
    assert(room==rerollTown && coordinate && !pathOrigin); // A zeroed third argument used to hang native pathfinding.
    assert(Field<int>(coordinate,0)==100 && Field<int>(coordinate,4)==200);
    assert(size==1 && mask==0x3e01 && pathMask==0x801 && a7==1 && distance==50 && a9==1);
    Put(coordinate,0,int(101));Put(coordinate,4,int(201));return rerollTown;
}
void __fastcall RerollUnlink(Unit* unit){assert(unit==rerollLoot);for(auto& room:fixture.r1)if(Field<Unit*>(room.data(),0xa8)==unit)Put(room.data(),0xa8,unit->pRoomNext);}
void __fastcall RerollNotify(Room*,int type,uint32_t id){assert(type==4 && id==rerollLoot->dwUnitId);++lootNotifications;}
void __fastcall RerollGround(Game*,Unit* source,Unit* item,Room* room,int x,int y){assert(!source && item==rerollLoot && room==rerollTown && x==101 && y==201);++lootMoves;gateRooms[item]=room;}
void LayoutRerolls(){
    Reset();retiredLayoutRecords.clear();serverLayoutSeeds.clear();gateRooms.clear();rerollRemoved.clear();lootMoves=lootNotifications=0;
    Unit creator{},loot{};Bytes<0x40> path{};Bytes<0x108> town{};Bytes<0x1c0> town2{};Bytes<0x288> townLevel{},fresh{},second{};
    Put(town.data(),0x18,town2.data());Put(town2.data(),0x90,townLevel.data());Put(townLevel.data(),0x1f8,int(75));
    creator.dwUnitType=0;creator.pDynamicPath=reinterpret_cast<decltype(creator.pDynamicPath)>(path.data());Put(path.data(),2,uint16_t(100));Put(path.data(),6,uint16_t(200));
    rerollCreator=&creator;rerollLoot=&loot;rerollTown=reinterpret_cast<Room*>(town.data());gateRooms[&creator]=rerollTown;gateRooms[&loot]=reinterpret_cast<Room*>(fixture.r1[0].data());
    loot.dwUnitType=4;loot.dwUnitId=777;loot.pRoomNext=&fixture.objects[0];fixture.old[0].pRoomNext=&loot;
    Put(fixture.level.data(),0x1c8,fixture.drlg.data());Put(fixture.drlg.data(),0x868,fixture.level.data());Put(fixture.drlg.data(),0x860,uint32_t(7));
    bindings[0x209530]=reinterpret_cast<uintptr_t>(RerollRoom);bindings[0x2a80d0]=reinterpret_cast<uintptr_t>(RerollSpace);
    bindings[0x27d2d0]=reinterpret_cast<uintptr_t>(ForbidOldPopulation);
    bindings[0x231510]=reinterpret_cast<uintptr_t>(RerollItemSpace);bindings[0x22e7f0]=reinterpret_cast<uintptr_t>(RerollUnlink);bindings[0x1d1280]=reinterpret_cast<uintptr_t>(RerollNotify);bindings[0x2fa090]=reinterpret_cast<uintptr_t>(RerollGround);
    bindings[0x1e7860]=reinterpret_cast<uintptr_t>(FreeLayout);bindings[0x1e7f70]=reinterpret_cast<uintptr_t>(AllocateLayout);originalRemove=RerollRemove;allocatedLayout=fresh.data();
    Run run;run.level=256;run.random.seed(42);Put(fixture.r1[2].data(),0x70,uint64_t(1));
    assert(!RerollLayout(fixture.GamePtr(),run,&creator) && steps.empty() && rerollRemoved.empty() && !lootMoves);
    Put(fixture.r1[2].data(),0x70,uint64_t(0));assert(RerollLayout(fixture.GamePtr(),run,&creator));
    assert(lootMoves==1 && lootNotifications==1 && std::find(rerollRemoved.begin(),rerollRemoved.end(),777)==rerollRemoved.end());
    const auto firstSeed=run.layoutSeed;assert(firstSeed && Field<uint32_t>(fixture.drlg.data(),0x860)==7);
    assert(State(fixture.GamePtr()).layoutSeeds.at(256)==firstSeed && FindLayoutLevel(fixture.drlg.data(),256)==fresh.data());
    // Supply the freshly generated graph for another recipe in the same game.
    Put(fresh.data(),0x1f8,int(256));Put(fresh.data(),0x1c8,fixture.drlg.data());Put(fresh.data(),0x10,fixture.r2[0].data());
    for(int i=0;i<3;++i){Put(fixture.r2[i].data(),0x90,fresh.data());Put(fixture.r2[i].data(),0x58,fixture.r1[i].data());Put(fixture.r2[i].data(),0x50,uint32_t((1u<<20)|(1u<<24)|(1u<<25)));Put(fixture.r1[i].data(),0xa8,static_cast<Unit*>(nullptr));}
    allocatedLayout=second.data();assert(RerollLayout(fixture.GamePtr(),run,&creator));
    assert(run.layoutSeed!=firstSeed && Field<uint32_t>(fixture.drlg.data(),0x860)==7 && lootMoves==1);
    // An untouched destination has no room graph. Do not invoke Spawn or room
    // population on the old level; just replace the unused record and seed it.
    Put(fixture.drlg.data(),0x868,static_cast<void*>(nullptr));fresh={};allocatedLayout=fresh.data();
    const auto oldSteps=steps.size();assert(RerollLayout(fixture.GamePtr(),run,&creator));
    assert(steps.size()==oldSteps && lootMoves==1);
    retiredLayoutRecords.clear();serverLayoutSeeds.clear();gateRooms.clear();installed=false;
    std::cout<<"Same-game rerolls choose distinct seeds, preserve ground loot through native town relocation and refuse occupied areas before changing any unit.\n";
}
int __fastcall ReplacementPetStat(Unit* unit,uint32_t stat,uint16_t){assert(stat==172);return unit==&fixture.old[0] ? 2 : 0;}
void MapReplacement(){
    Reset();gateUnits.clear();gateRooms.clear();gateDeletes.clear();
    auto& state=State(fixture.GamePtr());state.run=std::make_unique<Run>();auto& run=*state.run;
    run.level=256;run.phase=Phase::Running;run.populationChanged=true;
    for(auto& room:fixture.r2){run.pins.push_back({room.data(),false});Put(room.data(),0x50,Field<uint32_t>(room.data(),0x50)|Pinned);}
    assert(CanReplaceMap(fixture.GamePtr(),run)); // Living hostile monsters do not require a clear.
    Put(fixture.r1[2].data(),0x70,uint64_t(1));assert(!ReleaseMapForRecipe(fixture.GamePtr(),256));
    assert(state.run && run.populationChanged && state.restoreLevels.empty());Put(fixture.r1[2].data(),0x70,uint64_t(0));
    fixture.old[0].dwUnitType=0;assert(!CanReplaceMap(fixture.GamePtr(),run));fixture.old[0].dwUnitType=1;
    bindings[0x1e5620]=reinterpret_cast<uintptr_t>(ReplacementPetStat);assert(!CanReplaceMap(fixture.GamePtr(),run));bindings[0x1e5620]=reinterpret_cast<uintptr_t>(MockStat);
    run.event=Event::Trial;assert(!CanReplaceMap(fixture.GamePtr(),run));run.event=Event::None;
    run.phase=Phase::Returning;assert(!CanReplaceMap(fixture.GamePtr(),run));run.phase=Phase::Running;
    Unit player{},entry{},exit{};player.dwUnitId=33;entry.dwUnitId=10;exit.dwUnitId=11;
    Client client{};client.pGame=fixture.GamePtr();client.dwClientState=4;client.dwUnitGUID=33;fixture.GamePtr()->pClientList=&client;
    gateUnits={{33,&player},{10,&entry},{11,&exit}};gateRooms={{&player,reinterpret_cast<Room*>(fixture.r1[0].data())},{&entry,reinterpret_cast<Room*>(fixture.r1[0].data())},{&exit,reinterpret_cast<Room*>(fixture.r1[1].data())}};
    bindings[0x2c49c0]=reinterpret_cast<uintptr_t>(GateLookup);bindings[0x209530]=reinterpret_cast<uintptr_t>(GateRoom);bindings[0x1d1280]=reinterpret_cast<uintptr_t>(GateDelete);originalRemove=GateRemove;
    assert(!CanReplaceMap(fixture.GamePtr(),run)); // Also check connected players outside the pinned unit lists.
    fixture.GamePtr()->pClientList=nullptr;run.portals={10,11};
    assert(ReleaseMapForRecipe(fixture.GamePtr(),256));
    assert(!state.run && state.restoreLevels.contains(256) && gateDeletes==std::vector<uint32_t>({10,11}));
    for(auto& room:fixture.r2)assert(!(Field<uint32_t>(room.data(),0x50)&Pinned));
    // Moving to another map restores the abandoned area's normal monsters.
    Reset();bindings[NativeData::Source]=reinterpret_cast<uintptr_t>(MockPopulation);
    auto& other=State(fixture.GamePtr());other.run=std::make_unique<Run>();other.run->level=256;other.run->phase=Phase::Running;other.run->populationChanged=true;
    assert(ReleaseMapForRecipe(fixture.GamePtr(),257) && !other.run && other.restoreLevels.empty());
    for(int n=0;n<3;++n)assert(std::count(steps.begin(),steps.end(),"fresh"+std::to_string(n))==1);
    populationCode=nullptr;installed=false;gateUnits.clear();gateRooms.clear();
    std::cout<<"Unfinished ordinary maps can be replaced after players leave; observers, corpses, pets and active events block reset. Old portals/pins close and other destinations regain ordinary monsters.\n";
}
int nativePlacementChecks=0;
int nativeJunglePlacements=0;
std::vector<std::array<int,4>> nativeJungleTiles;
void __fastcall CaptureJunglePreset(void*,int x,int y,int preset,int flags,bool){nativeJungleTiles.push_back({x,y,preset,flags});}
void __fastcall JunglePreset(void* level,int x,int y,int preset,int flags,bool){
    assert(Field<int>(level,0x1f8)==78 && x==0 && y==0 && preset==574 && flags==1);++nativeJunglePlacements;
}
Room* __fastcall PlacementRoom(Room* room,int x,int y){assert(room==rerollTown && x==100 && y==200);return room;}
uint16_t __fastcall PlacementCollision(Room*,int,int,int,int){++nativePlacementChecks;return 0;}
void NativeLayouts(uint8_t* image){
    assert(memcmp(image+0x158aa50,JungleData::Junctions.data(),sizeof(JungleData::Junctions))==0);
    auto stub=[&](uint32_t rva,auto fn){const uint8_t jump[]{0xff,0x25,0,0,0,0};memcpy(image+rva,jump,6);auto target=reinterpret_cast<uintptr_t>(fn);memcpy(image+rva+6,&target,8);};
    // Run the real native placement routine with no pathfinding origin. Its
    // second coordinate is in/out; the third would request a path flood.
    stub(0x2232c0,PlacementRoom);stub(0x230ad0,PlacementCollision);
    Bytes<0x108> town{};rerollTown=reinterpret_cast<Room*>(town.data());
    struct Coord{int x,y;} drop{100,200};nativePlacementChecks=0;
    auto place=reinterpret_cast<Room*(__fastcall*)(Room*,Coord*,Coord*,int,int,int,int,int,int)>(image+0x231510);
    assert(place(rerollTown,&drop,nullptr,1,0x3e01,0x801,1,50,1)==rerollTown);
    assert(nativePlacementChecks==1 && drop.x==100 && drop.y==200);
    std::cout<<"Native loot placement accepts an in/out coordinate without starting a path flood.\n";
    // Execute the actual native initializer; substitute only the three heavy
    // asset-dependent geometry generators at their audited call boundary.
    stub(0x2669f0,GenerateLayout);stub(0x25fba0,GenerateLayout);stub(0x268dc0,GenerateLayout);
    originalInitLevel=reinterpret_cast<InitLevel>(image+0x1e8390);
    Bytes<0x880> drlg{};Bytes<0x288> level{},neighbor{};
    Put(drlg.data(),0x860,uint32_t(100));Put(level.data(),0x1c8,drlg.data());Put(level.data(),0x1f8,int(256));
    Put(neighbor.data(),0x1c8,drlg.data());Put(neighbor.data(),0x1f8,int(257));Put(neighbor.data(),0,int(1));
    serverLayoutSeeds.clear();clientLayoutSeeds.clear();serverLayoutSeeds[{drlg.data(),256}]={fixture.GamePtr(),900};
    for(int type:{1,2,3}){
        Put(level.data(),0,type);generationHistory.clear();nestedGeneration=neighbor.data();InitLevelHook(level.data());
        assert(generationHistory==std::vector<uint32_t>({1156,357}));assert(Field<uint32_t>(drlg.data(),0x860)==100 && generationBases.empty());
    }
    Put(drlg.data(),0x110,uint32_t(1));clientLayoutSeeds[256]=700;generationHistory.clear();InitLevelHook(level.data());
    assert(generationHistory==std::vector<uint32_t>({956}) && Field<uint32_t>(drlg.data(),0x860)==100);
    ClearClientLayouts();serverLayoutSeeds.clear();
    // Exercise the actual first teardown with valid type-specific storage,
    // then the type-zero tombstone used during final act destruction.
    stub(0x6cd130,Nothing);std::array<void*,5> allocator{};allocator[4]=reinterpret_cast<void*>(ReleaseLayoutData);
    Put(image,0x1b6e318,allocator.data());releasedLayoutData=0;
    stub(0xacdd80,+[]()->void*{return Native<void*>(0x1b6e318);});
    for(int type:{1,2,3}){
        Bytes<0x288> empty{};Bytes<0x1000> info{};Put(empty.data(),0,type);Put(empty.data(),0x1c8,drlg.data());Put(empty.data(),0x1f8,int(256));
        if(type>1)Put(empty.data(),0x18,info.data());
        auto freeLevel=reinterpret_cast<void(__fastcall*)(void*,int)>(image+0x1e7860);
        freeLevel(empty.data(),0);Put(empty.data(),0,int(0));freeLevel(empty.data(),0);
    }
    {
        // Execute real room freeing and the exact native status propagation
        // from the second-entry crash. Poison freed room headers to make any
        // surviving cross-level neighbor reference fail deterministically.
        Bytes<0x288> oldLevel{},freshLevel{},adjacentLevel{};Bytes<0x340> adjacentOutdoor{};
        Bytes<0x1c0> a{},b{},outside{},statusHead{};Bytes<0x28> tile{},levelOrth{},roomOrth{};
        std::array<void*,2> links{a.data(),b.data()};
        Put(drlg.data(),0x868,oldLevel.data());Put(drlg.data(),0x848,a.data());
        Put(oldLevel.data(),0x1c8,drlg.data());Put(oldLevel.data(),0x1f8,int(256));Put(oldLevel.data(),8,int(2));Put(oldLevel.data(),0x10,a.data());Put(oldLevel.data(),0x1b8,adjacentLevel.data());
        Put(adjacentLevel.data(),0,int(3));Put(adjacentLevel.data(),0x18,adjacentOutdoor.data());Put(adjacentLevel.data(),0x10,outside.data());Put(adjacentLevel.data(),0x1c8,drlg.data());
        Put(a.data(),0x48,b.data());
        for(auto room:{a.data(),b.data()}){Put(room,0x90,oldLevel.data());Put(room,0x70,uint8_t(3));}
        Put(statusHead.data(),0x38,a.data());Put(statusHead.data(),0x1b0,b.data());
        Put(a.data(),0x38,b.data());Put(a.data(),0x1b0,statusHead.data());Put(b.data(),0x38,statusHead.data());Put(b.data(),0x1b0,a.data());
        Put(outside.data(),0x90,adjacentLevel.data());Put(outside.data(),0x10,links.data());Put(outside.data(),0x18,uint64_t(2));Put(outside.data(),0x20,uint64_t(2));
        Put(tile.data(),0,a.data());Put(outside.data(),0x78,tile.data());
        Put(levelOrth.data(),0,oldLevel.data());Put(levelOrth.data(),0x18,oldLevel.data()+0x24);Put(adjacentOutdoor.data(),0x338,levelOrth.data());
        Put(roomOrth.data(),0,oldLevel.data());Put(roomOrth.data(),0x18,oldLevel.data()+0x24);Put(outside.data(),0,roomOrth.data());
        stub(0x1e7f70,AllocateLayout);allocatedLayout=freshLevel.data();releasedLayoutPointers.clear();poisonReleasedRooms={a.data(),b.data()};
        Put(a.data(),0x2a,uint16_t(1));assert(!FreshLayoutLevel(drlg.data(),256,500,oldLevel.data()) && releasedLayoutPointers.empty());Put(a.data(),0x2a,uint16_t(0));
        assert(FreshLayoutLevel(drlg.data(),256,500,oldLevel.data())==freshLevel.data());
        assert(!Field<void*>(drlg.data(),0x848) && !Field<uint64_t>(outside.data(),0x18) && !Field<void*>(outside.data(),0x78));
        assert(Field<void*>(statusHead.data(),0x38)==statusHead.data() && Field<void*>(statusHead.data(),0x1b0)==statusHead.data());
        assert(Field<void*>(levelOrth.data(),0)==freshLevel.data() && Field<void*>(levelOrth.data(),0x18)==freshLevel.data()+0x24);
        assert(Field<void*>(roomOrth.data(),0)==freshLevel.data() && Field<void*>(roomOrth.data(),0x18)==freshLevel.data()+0x24);
        for(auto pointer:{a.data(),b.data(),tile.data()})assert(std::count(releasedLayoutPointers.begin(),releasedLayoutPointers.end(),pointer)==1);
        reinterpret_cast<void(__fastcall*)(void*,uint8_t)>(image+0x242400)(outside.data(),1);
        poisonReleasedRooms.clear();retiredLayoutRecords.clear();
        std::cout<<"Native second-entry status propagation never visits poisoned retired rooms: neighbor/tile caches, status lists, iteration cursor and cross-level links are detached before actual room destruction.\n";
    }
    // Reproduce the failing Act III jungle path using real outdoor teardown and
    // native jungle placement, with only asset-backed preset emission stubbed.
    Bytes<0x288> old{},fresh{};Bytes<0x340> oldOutdoor{},freshOutdoor{};Bytes<0x28> border{};
    std::array<int,2> jungle{0,0};std::vector<Bytes<0x9c>> definitions(79);
    Put(definitions[76].data(),0xc,int(32));Put(definitions[76].data(),0x18,int(32));
    Put(image,0x1c9fd10,uint32_t(79));Put(image,0x1c9fd28,definitions[0].data());
    Put(drlg.data(),0x868,old.data());Put(drlg.data(),0x830,uint8_t(0));
    Put(old.data(),0,int(3));Put(old.data(),0x1c8,drlg.data());Put(old.data(),0x1f8,int(78));
    Put(old.data(),0x18,oldOutdoor.data());Put(old.data(),0x24,int(1000));Put(old.data(),0x28,int(2000));
    Put(old.data(),0x2c,int(64));Put(old.data(),0x30,int(96));Put(old.data(),0x1d0,int(2));Put(old.data(),0x1d8,jungle.data());
    Put(oldOutdoor.data(),0,uint32_t(0x67));Put(oldOutdoor.data(),0x338,border.data());
    Put(fresh.data(),0,int(3));Put(fresh.data(),0x18,freshOutdoor.data());
    stub(0x1e7f70,AllocateLayout);allocatedLayout=fresh.data();releasedLayoutPointers.clear();
    assert(FreshLayoutLevel(drlg.data(),78,900,old.data())==fresh.data());
    assert(Field<int>(fresh.data(),0x24)==1000 && Field<int>(fresh.data(),0x28)==2000 && Field<int>(fresh.data(),0x2c)==64 && Field<int>(fresh.data(),0x30)==96);
    assert(Field<int>(fresh.data(),0x1d0)==2 && Field<void*>(fresh.data(),0x1d8)==jungle.data());
    assert(Field<uint32_t>(freshOutdoor.data(),0)==7 && Field<void*>(freshOutdoor.data(),0x338)==border.data());
    assert(releasedLayoutPointers==std::vector<void*>({oldOutdoor.data()}));
    stub(0x267a30,JunglePreset);nativeJunglePlacements=0;
    reinterpret_cast<void(__fastcall*)(void*)>(image+0x2782b0)(fresh.data());assert(nativeJunglePlacements==1);
    Bytes<0x288> second{};Bytes<0x340> secondOutdoor{};
    Put(second.data(),0,int(3));Put(second.data(),0x18,secondOutdoor.data());allocatedLayout=second.data();
    assert(FreshLayoutLevel(drlg.data(),78,901,fresh.data())==second.data());
    assert(Field<int>(second.data(),0x24)==1000 && Field<int>(second.data(),0x2c)==64 && Field<int>(second.data(),0x30)==96);
    assert(Field<void*>(second.data(),0x1d8)==jungle.data() && Field<void*>(secondOutdoor.data(),0x338)==border.data());
    assert(std::find(releasedLayoutPointers.begin(),releasedLayoutPointers.end(),jungle.data())==releasedLayoutPointers.end());
    reinterpret_cast<void(__fastcall*)(void*)>(image+0x2782b0)(second.data());assert(nativeJunglePlacements==2);
    reinterpret_cast<void(__fastcall*)(void*,int)>(image+0x1e7860)(old.data(),0);
    reinterpret_cast<void(__fastcall*)(void*,int)>(image+0x1e7860)(fresh.data(),0);
    reinterpret_cast<void(__fastcall*)(void*,int)>(image+0x1e7860)(second.data(),0);
    for(auto pointer:{oldOutdoor.data(),freshOutdoor.data(),secondOutdoor.data(),border.data(),reinterpret_cast<uint8_t*>(jungle.data())})assert(std::count(releasedLayoutPointers.begin(),releasedLayoutPointers.end(),pointer)==1);
    retiredLayoutRecords.clear();
    std::cout<<"Actual Act III jungle placement survives layout replacement; outdoor bounds, jungle template and border links retain single native ownership through teardown.\n";
    {
        // Execute the actual native preset emitter against two new templates;
        // verify changed path geometry, not merely changed RNG/room rectangles.
        jungleTemplates.clear();retiredLayoutRecords.clear();
        Bytes<0x288> source{},first{},next{};Bytes<0x340> sourceInfo{},firstInfo{},nextInfo{};
        std::vector<int> cells{541,0,570,575,539,534,576,569,535,538,570,575};const auto ordinary=cells;
        Put(definitions[76].data(),0xc,int(64));Put(definitions[76].data(),0x18,int(192));
        Put(drlg.data(),0x868,source.data());Put(drlg.data(),0x860,uint32_t(100));
        for(auto pair:{std::pair{source.data(),sourceInfo.data()},std::pair{first.data(),firstInfo.data()},std::pair{next.data(),nextInfo.data()}}){
            Put(pair.first,0,int(3));Put(pair.first,0x18,pair.second);Put(pair.first,0x1c8,drlg.data());Put(pair.first,0x1f8,int(78));
        }
        Put(source.data(),0x2c,int(64));Put(source.data(),0x30,int(192));Put(source.data(),0x1d0,int(3));Put(source.data(),0x1d8,cells.data());
        allocatedLayout=first.data();assert(FreshLayoutLevel(drlg.data(),78,900,source.data())==first.data());
        const auto firstCells=cells;assert(firstCells!=ordinary);
        stub(0x267a30,CaptureJunglePreset);nativeJungleTiles.clear();
        reinterpret_cast<void(__fastcall*)(void*)>(image+0x2782b0)(first.data());const auto firstTiles=nativeJungleTiles;
        auto verify=[&](const std::vector<int>& expected){
            assert(nativeJungleTiles.front()==(std::array<int,4>{0,0,574,expected[1]==0}));
            size_t index=1;int clearings=0;
            for(size_t n=2;n<expected.size();++n)if(expected[n]){
                assert(index<nativeJungleTiles.size());const auto& tile=nativeJungleTiles[index++];
                assert(tile[0]==int(n%2)*4 && tile[1]==int(n/2)*4 && tile[2]==expected[n]+(expected[n]>574?20:0));
                if(expected[n]>574){assert(tile[3]>=0 && tile[3]<3);++clearings;}else assert(tile[3]==-1);
            }
            assert(index==nativeJungleTiles.size() && clearings==3);
        };
        verify(firstCells);
        allocatedLayout=next.data();assert(FreshLayoutLevel(drlg.data(),78,901,first.data())==next.data());
        assert(cells!=firstCells && cells!=ordinary);nativeJungleTiles.clear();
        reinterpret_cast<void(__fastcall*)(void*)>(image+0x2782b0)(next.data());verify(cells);assert(nativeJungleTiles!=firstTiles);
        // A newly loaded client starts from the original act template and
        // reaches the same rolled cells without seeing previous map runs.
        cells=ordinary;jungleTemplates.clear();ApplyJungleLayout(next.data(),901);assert(cells==JungleData::Roll(ordinary,2,78,901));
        ApplyJungleLayout(next.data(),100);assert(cells==ordinary);
        jungleTemplates.clear();retiredLayoutRecords.clear();
        std::cout<<"Actual native Flayer Jungle emission produces different connected path presets across rerolls; first-load clients agree and ordinary-template reset restores the original cells.\n";
    }
    std::cout<<"Native maze/preset/outdoor initialization uses matching per-level seeds, nested neighbors keep the original act seed, and retired records survive final native teardown.\n";
}
