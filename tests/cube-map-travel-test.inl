int travelCasts=0,travelCharges=0,travelOpens=0,travelRequests=0,travelPortals=0;
int travelUses=0,travelCreates=0;
bool __fastcall TravelCast(Game*,Unit*){++travelCasts;--travelCharges;return true;}
int __fastcall TravelUse(Game*,Unit*,const uint8_t*,bool){++travelUses;--travelCharges;return 4;}
int __fastcall TravelCreate(Game*,Unit*,Room*,int x,int y,int destination,Unit** output,int object,int permanent,int flags){
    assert(x==10&&y==20&&destination==1&&(object==59||object==60)&&permanent==7&&flags==9);++travelCreates;if(output)*output=nullptr;return 1;
}
int __fastcall TravelOpen(Operation*,int operation){assert(operation==7);++travelOpens;return 23;}
int __fastcall TravelRequest(Game*,Unit*,const void* packet,int size){assert(packet && size==5);++travelRequests;return 31;}
int __fastcall TravelPortal(Operation*,int operation){assert(operation==7);++travelPortals;return 15;}
void MapTravelLock(){
    Reset();auto game=fixture.GamePtr();auto& state=State(game);state.run=std::make_unique<Run>();auto& run=*state.run;
    run.level=256;run.phase=Phase::Running;run.portals={10,11};
    Unit player{},other{},blue{};player.dwUnitId=1;other.dwUnitId=2;blue.dwUnitType=2;blue.dwClassId=59;blue.dwUnitId=99;
    Bytes<0x108> town{};Bytes<0x1c0> town2{};Bytes<0x288> townLevel{};
    Put(town.data(),0x18,town2.data());Put(town2.data(),0x90,townLevel.data());Put(townLevel.data(),0x1f8,int(1));
    gateRooms={{&player,reinterpret_cast<Room*>(fixture.r1[0].data())},{&other,reinterpret_cast<Room*>(town.data())}};
    bindings[0x209530]=uintptr_t(GateRoom);
    originalTownPortalCast=TravelCast;originalWaypointOperate=TravelOpen;originalWaypointRequest=TravelRequest;originalOperate=TravelPortal;
    originalItemUse=TravelUse;originalCreatePortal=TravelCreate;
    Unit tome{};tome.dwUnitType=4;tome.dwUnitId=77;gateUnits[77]=&tome;
    Bytes<0x1b4> itemRow{};auto itemRows=itemRow.data();uint64_t itemCount=1;
    Put(itemRow.data(),0x80,uint32_t(0x206b6274));
    bindings[0x1ca0390]=uintptr_t(&itemRows);bindings[0x1ca0398]=uintptr_t(&itemCount);bindings[0x2c49c0]=uintptr_t(GateLookup);
    std::array<uint8_t,36> use{};Put(use.data(),10,uint32_t(77));
    travelCasts=travelOpens=travelRequests=travelPortals=0;travelCharges=10;
    Operation op{game,&blue,&player};std::array<uint8_t,5> request{0x4b,1};
    auto create=[&](int object){return CreatePortalHook(game,op.player,nullptr,10,20,1,nullptr,object,7,9);};
    auto allowed=[&]{
        const auto casts=travelCasts,charges=travelCharges,opens=travelOpens,requests=travelRequests,portals=travelPortals;
        assert(TownPortalCastHook(game,op.player) && travelCasts==casts+1 && travelCharges==charges-1);
        assert(WaypointOperateHook(&op,7)==23 && travelOpens==opens+1);
        assert(WaypointRequestHook(game,op.player,request.data(),int(request.size()))==31 && travelRequests==requests+1);
        assert(OperateHook(&op,7)==15 && travelPortals==portals+1);
        assert(ItemUseHook(game,op.player,use.data(),false)==4);
        assert(ItemUseHook(game,op.player,use.data(),true)==4);
        assert(create(59)==1&&create(60)==1);
    };
    auto blocked=[&]{
        const auto casts=travelCasts,charges=travelCharges,opens=travelOpens,requests=travelRequests,portals=travelPortals;
        assert(!TownPortalCastHook(game,op.player));
        assert(WaypointOperateHook(&op,7)==0);
        assert(WaypointRequestHook(game,op.player,request.data(),int(request.size()))==0);
        assert(OperateHook(&op,7)==0);
        const auto uses=travelUses,creates=travelCreates;
        assert(ItemUseHook(game,op.player,use.data(),false)==1);
        assert(ItemUseHook(game,op.player,use.data(),true)==1);
        assert(!create(59)&&travelUses==uses&&travelCreates==creates);
        assert(travelCasts==casts && travelCharges==charges && travelOpens==opens && travelRequests==requests && travelPortals==portals);
    };
    allowed(); // The party is still gathering; red portals remain open.
    run.entryClosed=true;run.portals={};blocked();
    Put(itemRow.data(),0x80,uint32_t(0x20637374));blocked(); // Loose town-portal scroll.
    Put(itemRow.data(),0x80,uint32_t(0x206b6269)); // Identify tomes still work.
    assert(ItemUseHook(game,&player,use.data(),false)==4);
    Put(itemRow.data(),0x80,uint32_t(0x206b6274));
    assert(create(60)==1); // Red portals are never suppressed.
    Unit* output=&blue;assert(!CreatePortalHook(game,&player,nullptr,10,20,1,&output,59,7,9)&&!output);
    run.phase=Phase::Returning;blocked(); // Automatic return is separate from these player actions.
    op.player=&other;allowed();op.player=&player; // Another player in town is unaffected.
    for(int event=1;event<=int(Event::SoulSurprise);++event){run.event=Event(event);allowed();}
    run.event=Event::None;run.phase=Phase::Closed;allowed();
    run.phase=Phase::Running;installed=false;allowed();installed=true;
    ++game->dwInitSeed;allowed();--game->dwInitSeed;
    assert(!MapTravelLocked(nullptr,&player) && !MapTravelLocked(game,nullptr) && !MapTravelLocked(game,&blue));
    state.run.reset();allowed();games.clear();installed=false;
    std::cout<<"Closed ordinary maps reject portal casts without consuming quantity, existing blue portals, waypoint menus and direct travel; town, open entry, all events, finished/foreign runs remain unaffected.\n";
}
void NativeMapTravelHooks(uint8_t* image){
    // Establish the native table identities as well as the three entry signatures.
    const uint64_t preferredBase=0x140000000ULL;
    assert(Field<uint64_t>(image,0x14c0af0)==preferredBase+0x34fa70); // Item spell 2.
    assert(Field<uint64_t>(image,0x14c1098)==preferredBase+0x3c20e0); // Object operation 23.
    assert(Field<uint64_t>(image,0x14bfc50+0x4b*8)==preferredBase+0x2bcaf0);
    Unit player{},portal{};D2DynamicPathStrc path{};player.pDynamicPath=&path;portal.dwUnitType=2;portal.dwClassId=59;
    Bytes<0x108> room{};Bytes<0x1c0> room2{};Bytes<0x288> level{};
    Put(&path,0x20,room.data());Put(room.data(),0x18,room2.data());Put(room2.data(),0x90,level.data());Put(level.data(),0x1f8,int(256));
    auto game=fixture.GamePtr();auto& state=State(game);state.run=std::make_unique<Run>();auto& run=*state.run;
    run.level=256;run.phase=Phase::Running;run.entryClosed=true;
    Operation op{game,&portal,&player};std::array<uint8_t,5> request{0x4b,1};
    // Execute the installed MinHook entries in the private executable image.
    // The guards must return before the native inventory/UI/warp side effects.
    assert(!Native<TownPortalCast>(0x34fa70)(game,&player));
    Unit tome{};tome.dwUnitType=4;tome.dwUnitId=77;gateUnits[77]=&tome;
    Bytes<0x1b4> itemRow{};Put(itemRow.data(),0x80,uint32_t(0x206b6274));
    const auto oldItems=Field<uintptr_t>(image,0x1ca0390),oldCount=Field<uint64_t>(image,0x1ca0398);
    Put(image,0x1ca0390,itemRow.data());Put(image,0x1ca0398,uint64_t(1));
    std::array<uint8_t,14> lookupBytes{};memcpy(lookupBytes.data(),image+0x2c49c0,14);
    const uint8_t jump[]{0xff,0x25,0,0,0,0};memcpy(image+0x2c49c0,jump,6);Put(image,0x2c49c0+6,uintptr_t(GateLookup));
    std::array<uint8_t,36> use{};use[0]=0x26;Put(use.data(),10,uint32_t(77));
    assert(Native<ItemUse>(0x2b6450)(game,&player,use.data(),false)==1);
    // Real packet 0x26 entry dispatches into the guarded inventory transaction.
    assert(Native<WaypointRequest>(0x2b7370)(game,&player,use.data(),int(use.size()))==1);
    Put(itemRow.data(),0x80,uint32_t(0x20637374));
    assert(Native<WaypointRequest>(0x2b7370)(game,&player,use.data(),int(use.size()))==1);
    Unit* output=&portal;
    assert(!Native<CreatePortal>(0x29ab60)(game,&player,nullptr,10,20,1,&output,59,7,9)&&!output);
    memcpy(image+0x2c49c0,lookupBytes.data(),14);Put(image,0x1ca0390,oldItems);Put(image,0x1ca0398,oldCount);
    assert(Native<Operate>(0x3c20e0)(&op,0)==0);
    assert(Native<WaypointRequest>(0x2bcaf0)(game,&player,request.data(),int(request.size()))==0);
    assert(Native<Operate>(0x3c1080)(&op,0)==0);
    games.clear();
    std::cout<<"Real 0x26 inventory packet and queued item transaction, portal creation, waypoint menu/request and blue-portal hook entries reject locked-map travel in the private native image.\n";
}
