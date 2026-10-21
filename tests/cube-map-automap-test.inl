using AutomapEntry=AutomapData::Entry;
using AutomapWire=AutomapData::Wire;
void EventAutomapPackets(){
    using namespace AutomapData;
    Wire w;w.run=77;w.session=91;w.recipient=55;w.revision=1;w.stream=Stream::Monsters;w.total=13;w.count=12;
    for(unsigned i=0;i<12;++i)w.entries[i]={i,100+int(i),200,0};
    assert(Valid(w));Assembly assembly;auto tail=w;tail.offset=12;tail.count=1;tail.entries[0]={12,112,200,3};
    assert(assembly.Accept(tail) && !assembly.ready);assert(!assembly.Accept(tail));
    assert(assembly.Accept(w) && assembly.ready->size()==13 && assembly.ready->back().id==12);assert(!assembly.Accept(w));
    w.revision=2;w.total=0;w.count=0;assert(assembly.Accept(w) && assembly.ready->empty()); // All died.
    assert(!assembly.Accept(tail));w.count=1;assert(!Valid(w));w.count=0;w.offset=1;assert(!Valid(w));w.offset=0;
    w.total=MaxMonsters+1;assert(!Valid(w));w.total=1;w.count=1;w.entries[0].kind=4;assert(!Valid(w));w.entries[0].kind=0;
    w.entries[0].x=INT_MAX;assert(!Valid(w));w.entries[0].x=0;w.version=2;assert(!Valid(w));w.version=1;w.level=40;assert(!Valid(w));w.level=39;
    Packet p;p.run=w.run;p.session=w.session;p.recipient=w.recipient;p.level=39;p.active=1;p.event=Event::GoblinGrotto;p.eventPhase=EventPhase::Active;p.die=6;
    assert(Visible(p,0,55,39) && !Visible(p,4001,55,39) && !Visible(p,0,56,39) && !Visible(p,0,55,2));
    for(auto phase:{EventPhase::None,EventPhase::WaitingTown,EventPhase::Ceremony,EventPhase::Briefing,EventPhase::Returning,EventPhase::Finished,EventPhase::Cancelled}){p.eventPhase=phase;assert(!Visible(p,0,55,39));}
    p.eventPhase=EventPhase::Entering;assert(Visible(p,0,55,39));p.eventPhase=EventPhase::Active;p.eventElapsed=60000;assert(!Visible(p,0,55,39));p.eventElapsed=59999;assert(Visible(p,0,55,39) && !Visible(p,1,55,39));p.eventElapsed=0;
    for(auto event:{Event::ChampionClash,Event::SoulSurprise}){p.event=event;p.level=uint16_t(EventLevel(event));w.level=p.level;assert(Valid(w) && Visible(p,0,55,p.level) && !Visible(p,0,55,39));}
    p.event=Event::ChampionClash;p.level=108;p.eventElapsed=100000000;assert(Visible(p,4000,55,108));p.event=Event::TreasureTrove;p.level=0;assert(!Visible(p,0,55,0));p.eventElapsed=0;p.level=39;w.level=39;
    p.event=Event::None;assert(!Visible(p,0,55,39));p.event=Event::GoblinGrotto;
    {std::lock_guard lock(viewMutex);view={};receivedAt=0;ClearEventAutomap();}
    p.sequence=10;assert(Receive(reinterpret_cast<uint8_t*>(&p),sizeof(p)));
    assert(Receive(reinterpret_cast<uint8_t*>(&w),sizeof(w)) && automapStreams[1].ready->size()==1);
    ++w.recipient;assert(!ReceiveAutomap(w));--w.recipient;++w.session;assert(!ReceiveAutomap(w));--w.session;
    ++p.sequence;p.eventPhase=EventPhase::Returning;assert(Receive(reinterpret_cast<uint8_t*>(&p),sizeof(p)));assert(!automapStreams[1].ready && !ReceiveAutomap(w));
    assert(SpriteFrame(0)==3 && SpriteFrame(1)==1 && SpriteFrame(2)==1 && SpriteFrame(3)==5);
    assert(MonsterKind(0,false,false)==0 && MonsterKind(4,false,false)==1 && MonsterKind(16,false,false)==2);
    for(uint8_t flags:{uint8_t(2),uint8_t(8)})assert(MonsterKind(flags,false,false)==3);
    assert(MonsterKind(12,false,false)==1); // Champions also carry UNIQUE.
    assert(MonsterKind(0,true,false)==3 && MonsterKind(0,false,true)==3);
    std::cout<<"Event automap packets assemble atomically, reject foreign/stale/bounded input, remove deaths, and stop on every exit/timeout/ordinary-map path.\n";
}
void EventAutomapGeometry(){
    Reset();Run run;run.level=39;run.event=Event::Trial;run.pins.push_back({fixture.r2[0].data(),true});
    Bytes<40*0x9c> defs{};void* definitions=defs.data();Put(defs.data(),39*0x9c+0x34,1);
    std::array<Bytes<32>,2> rows{};for(int i=0;i<2;++i){Put(rows[i].data(),0,1);Put(rows[i].data(),4,i+2);rows[i][8]=255;rows[i][9]=255;Put(rows[i].data(),12,i+42);}
    void* table=rows.data();uint64_t count=rows.size();int levelCount=40;
    bindings[0x1ca0880]=uintptr_t(&table);bindings[0x1ca0888]=uintptr_t(&count);bindings[0x1c9fd28]=uintptr_t(&definitions);bindings[0x1c9fd10]=uintptr_t(&levelCount);
    Bytes<0x30> tiles{};std::array<Bytes<0x48>,2> floors{};Bytes<0x48> wall{};Bytes<0x20> floorDef{},wallDef{};
    Put(floorDef.data(),0x14,2);Put(wallDef.data(),0x14,3);
    for(auto& f:floors){Put(f.data(),0x20,floorDef.data());Put(f.data(),8,3);Put(f.data(),12,4);}
    Put(floors[0].data(),0x18,uint32_t(1<<18));Put(floors[1].data(),0x18,uint32_t(8)); // revealed allowed, hidden excluded
    Put(wall.data(),0x20,wallDef.data());Put(wall.data(),8,3);Put(wall.data(),12,4);Put(wall.data(),0x28,16);
    Put(tiles.data(),0,floors.data());Put(tiles.data(),8,uint32_t(2));Put(tiles.data(),0x20,wall.data());Put(tiles.data(),0x28,uint32_t(1));
    Put(fixture.r1[0].data(),8,tiles.data());Put(fixture.r2[0].data(),0x60,100);Put(fixture.r2[0].data(),0x64,200);
    const auto originalFloors=floors;const auto originalWall=wall;const auto originalR2=fixture.r2[0];
    BuildEventAutomap(run);assert(run.automapCells.size()==2 && floors==originalFloors && wall==originalWall && fixture.r2[0]==originalR2);
    assert(run.automapCells[0].kind==0 && run.automapCells[0].id==43 && run.automapCells[0].x==-808 && run.automapCells[0].y==1252);
    assert(run.automapCells[1].kind==1 && run.automapCells[1].id==42 && run.automapCells[1].y==1228);
    AutomapData::Geometry geometry(run.automapCells);assert(geometry.trees[0].count==1 && geometry.trees[1].count==1);
    for(auto& t:geometry.trees){assert(t.root==t.first && t.last==t.first && t.root->parent==reinterpret_cast<AutomapData::CellNode*>(&t));}
    // Markers traverse all pinned rooms, not the client's nearby room list.
    std::array<D2DynamicPathStrc,3> paths{};
    for(int i=0;i<3;++i){fixture.old[i].pDynamicPath=&paths[i];Put(&paths[i],8,1000+i*100);Put(&paths[i],12,2000+i*100);run.pins.push_back({fixture.r2[i].data(),true});}
    fixture.old[2].dwAnimMode=12;auto markers=EventMonsterMarkers(run);
    assert(markers.size()==2 && markers[0].x==101 && markers[0].y==197 && markers[1].x==111);
    run.objectives.insert(fixture.old[0].dwUnitId);run.event=Event::GoblinGrotto;assert(EventMonsterMarkers(run)[0].kind==0);
    run.event=Event::Trial;assert(EventMonsterMarkers(run)[0].kind==3);
    assert(!run.census.dead.size());populationCode=nullptr;installed=false;
    std::cout<<"Full event geometry leaves room/tile exploration untouched; marker census includes distant pinned rooms and excludes corpses/duplicates.\n";
}
Unit automapPlayer{};uint32_t automapSelector=0;int automapIcons=0,automapResets=0;void* automapTree=nullptr;
Unit* __fastcall AutomapLocal(uint32_t){return &automapPlayer;}
void __fastcall CaptureMapCells(int,void* tree,uint8_t*,void*){automapTree=tree;}
void __fastcall CaptureMapIcon(uint64_t,int,float){++automapIcons;}
void __fastcall CaptureMapReset(){++automapResets;}
void EventAutomapScope(){
    Reset();ClearEventAutomap();Put(fixture.level.data(),0x1f8,39);automapPlayer={};automapPlayer.dwUnitId=55;
    bindings[0x1d442d8]=uintptr_t(&automapSelector);bindings[0x6b050]=uintptr_t(AutomapLocal);bindings[0x209530]=uintptr_t(ParticipantRoom);
    bindings[0x977a0]=uintptr_t(CaptureMapIcon);
    Bytes<0xb0> layer{};void* current=layer.data();bindings[0x1d56150]=uintptr_t(&current);
    int images=100,perSheet=100;bindings[0x1c9b89c]=uintptr_t(&images);bindings[0x1c9b898]=uintptr_t(&perSheet);
    originalMapCells=CaptureMapCells;originalMapReset=CaptureMapReset;
    Bytes<0x40> transform{};Put(transform.data(),0x20,1000);Put(transform.data(),0x24,1000);Put(transform.data(),0x30,1.0f);Put(transform.data(),0x34,1.0f);Put(transform.data(),0x38,1.0f);
    view={};view.active=1;view.run=77;view.session=91;view.recipient=55;view.level=39;view.event=Event::Trial;view.eventPhase=EventPhase::Active;
    receivedAt=GetTickCount64();eventGeometry=std::make_shared<AutomapData::Geometry>(std::vector<AutomapEntry>{{5,100,100,0}});
    automapStreams[1].ready=std::make_shared<const std::vector<AutomapEntry>>(std::vector<AutomapEntry>{{99,100,100,3}});
    automapIcons=0;MapCellsHook(0,layer.data()+8,transform.data(),nullptr);assert(automapTree==&eventGeometry->trees[0] && current==layer.data());
    MapCellsHook(0,layer.data()+0x58,transform.data(),nullptr);assert(automapTree==layer.data()+0x58 && automapIcons==1);
    const auto checkNative=[&]{const auto icons=automapIcons;MapCellsHook(0,layer.data()+8,transform.data(),nullptr);assert(automapTree==layer.data()+8 && icons==automapIcons && current==layer.data());};
    images=5;checkNative();images=100;perSheet=0;checkNative();perSheet=100;
    Put(fixture.level.data(),0x1f8,109);checkNative();Put(fixture.level.data(),0x1f8,39);
    view.event=Event::None;checkNative();view.event=Event::Trial;
    for(auto phase:{EventPhase::WaitingTown,EventPhase::Ceremony,EventPhase::Briefing,EventPhase::Returning,EventPhase::Finished,EventPhase::Cancelled}){view.eventPhase=phase;checkNative();}
    view.eventPhase=EventPhase::Active;view.eventElapsed=120000;checkNative();view.eventElapsed=0;view.recipient=56;checkNative();view.recipient=55;
    receivedAt=GetTickCount64()-4001;checkNative();receivedAt=GetTickCount64();
    automapResets=0;MapResetHook();assert(automapResets==1 && !receivedAt && !view.run && !eventGeometry && !automapStreams[1].ready);checkNative();
    installed=false;populationCode=nullptr;
    std::cout<<"Real draw-hook scope switches back to native trees outside events, in town, on timeout/foreign/stale state, and clears all event data on game teardown.\n";
}
std::vector<AutomapWire> sentAutomap;
std::vector<Packet> sentMapState;
void __fastcall CaptureAutomapPackets(void*,uint32_t recipient,void** range){
    assert(static_cast<uint8_t*>(range[1])-static_cast<uint8_t*>(range[0])==sizeof(Packet));
    if(static_cast<uint8_t*>(range[0])[9]==AutomapData::Operation){auto wire=*static_cast<AutomapWire*>(range[0]);assert(AutomapData::Valid(wire) && recipient==1 && wire.recipient==55);sentAutomap.push_back(wire);}
    else {auto packet=*static_cast<Packet*>(range[0]);assert(Data::Valid(packet));sentMapState.push_back(packet);}
}
void EventAutomapSynchronization(){
    Reset();games.clear();eventPlayers={};eventClients={};eventPlayerData={};sentAutomap.clear();sentMapState.clear();
    auto game=fixture.GamePtr();game->dwInitSeed=91;game->dwGameFrame=1000;game->pClientList=&eventClients[0];Put(fixture.level.data(),0x1f8,39);
    for(size_t n=0;n<2;++n){auto& p=eventPlayers[n];p.dwUnitType=0;p.dwUnitId=55+uint32_t(n);p.dwAnimMode=1;p.pPlayerData=&eventPlayerData[n];Put(&eventPlayerData[n],0xf0,&eventClients[n]);auto& c=eventClients[n];c.pGame=game;c.dwClientState=4;c.dwUnitGUID=p.dwUnitId;c.dwClientId=uint32_t(n+1);if(!n)c.pNext=&eventClients[1];}
    bindings[0x2c49c0]=uintptr_t(EventLookup);bindings[0x209530]=uintptr_t(ParticipantRoom);bindings[0x2d0130]=uintptr_t(Nothing);bindings[0x422ac0]=uintptr_t(CaptureAutomapPackets);
    auto& state=State(game);state.run=std::make_unique<Run>();auto& run=*state.run;run.id=77;run.level=39;run.phase=Phase::Running;run.event=Event::Trial;run.eventPhase=EventPhase::Active;run.eventFrame=run.eventStarted=1000;run.die=6;
    run.returns[55]={109,10,20};run.automapCells={{5,100,100,0},{6,110,110,1}};
    Sync(game,run,true);assert(sentMapState.size()==2 && sentAutomap.size()==2 && sentAutomap[0].stream==AutomapData::Stream::Geometry && sentAutomap[1].stream==AutomapData::Stream::Monsters);
    assert(sentMapState[1].event==Event::None); // Unrelated clients get no reveal.
    Sync(game,run,true);assert(sentAutomap.size()==3 && sentAutomap.back().stream==AutomapData::Stream::Monsters);
    CommandPacket request;request.run=77;request.session=91;request.phase=EventPhase::Active;
    assert(Command(game,&eventPlayers[0],reinterpret_cast<uint8_t*>(&request),sizeof(request)) && sentAutomap.size()==5);
    assert(run.eventStarted==1000 && run.eventPhase==EventPhase::Active && run.rewards.empty());
    auto before=sentAutomap.size();request.session=92;Command(game,&eventPlayers[0],reinterpret_cast<uint8_t*>(&request),sizeof(request));assert(sentAutomap.size()==before);
    request.session=91;Command(game,&eventPlayers[1],reinterpret_cast<uint8_t*>(&request),sizeof(request));assert(sentAutomap.size()==before);
    Put(fixture.level.data(),0x1f8,109);Sync(game,run,true);assert(sentAutomap.size()==before && run.automapSent.empty());
    Put(fixture.level.data(),0x1f8,39);Sync(game,run,true);assert(sentAutomap.size()==before+2);before=sentAutomap.size();
    run.eventPhase=EventPhase::Returning;Sync(game,run,true);assert(sentAutomap.size()==before);
    run.event=Event::None;run.eventPhase=EventPhase::None;Sync(game,run,true);assert(sentAutomap.size()==before);
    games.clear();populationCode=nullptr;installed=false;
    std::cout<<"Host sends event layout/positions only to authenticated participants; re-entry/request recovery preserves timers/rewards and ordinary maps send no reveal.\n";
}
std::vector<std::pair<AutomapData::Point,int>> automapDraws;
void __fastcall CaptureMapSprite(void*,AutomapData::Point* point,uint8_t* properties,float){automapDraws.push_back({*point,Field<int>(properties,0x18)});}
bool __fastcall MapUnitKind(Unit*,int* frame,int* category){*frame=6;*category=1;return true;}
AutomapEntry nativeTileCell{};
void* __fastcall CaptureNativeTile(void*,uint64_t* hint,const uint8_t* key){nativeTileCell={Field<uint16_t>(key,2),Field<int>(key,4),Field<int>(key,8),0};hint[0]=hint[1]=0;return hint;}
void NativeEventAutomap(uint8_t* image){
    auto stub=[&](uint32_t rva,auto fn){const uint8_t j[]{0xff,0x25,0,0,0,0};memcpy(image+rva,j,6);auto to=reinterpret_cast<uintptr_t>(fn);memcpy(image+rva+6,&to,8);};
    stub(0x573440,CaptureMapSprite);stub(0x96ea0,MapUnitKind);image[0x1434c50]=0xc3;
    Put(image,0x1d56130,68);Put(image,0x1d56134,36);Put(image,0x1d5613c,0);Put(image,0x1d5614c,0);
    Bytes<0x40> transform{};Put(transform.data(),0x20,2000);Put(transform.data(),0x24,2000);Put(transform.data(),0x28,100);Put(transform.data(),0x2c,100);
    Put(transform.data(),0x30,1.25f);Put(transform.data(),0x34,0.75f);Put(transform.data(),0x38,1.0f);Put(transform.data(),0x10,3);Put(transform.data(),0x14,-3);
    Unit unit{};unit.dwUnitType=1;D2DynamicPathStrc path{};unit.pDynamicPath=&path;Put(&path,8,1000);Put(&path,12,2000);
    automapDraws.clear();Native<void(__fastcall*)(Unit*,void*)>(0x978a0)(&unit,transform.data());assert(automapDraws.size()==1);auto expected=automapDraws[0];
    automapDraws.clear();DrawEventMarkers({AutomapEntry{1,101,197,0}},transform.data());
    assert(automapDraws.size()==1 && automapDraws[0].first.x==expected.first.x && automapDraws[0].first.y==expected.first.y && automapDraws[0].second==3);
    automapDraws.clear();DrawEventMarkers({AutomapEntry{1,101,197,1},AutomapEntry{2,101,197,3},AutomapEntry{3,-9999,-9999,0}},transform.data());
    assert(automapDraws.size()==2 && automapDraws[0].second==1 && automapDraws[1].second==5);
    std::array<uint64_t,2> sprite{};void* sprites=sprite.data();Put(image,0x1c9b880,sprites);Put(image,0x1c9b898,1000);Put(image,0x1d56110,0);Put(image,0x1befea8,0);
    AutomapData::Geometry geometry({{5,100,100,0},{6,110,110,0},{7,120,120,0}});Bytes<0x18> opacity{};for(size_t i=0;i<6;++i)Put(opacity.data(),i*4,0.85f);
    Put(transform.data(),8,2000);Put(transform.data(),12,2000);
    automapDraws.clear();Native<MapCells>(0x969f0)(0,&geometry.trees[0],transform.data(),opacity.data());assert(automapDraws.size()==3);
    assert(geometry.trees[0].count==3 && geometry.trees[0].root->saved==1);
    // Compare our geometry coordinate/cell extraction with the actual native
    // tile revealer, stopping before insertion. Its input is a private copy.
    Bytes<40*0x9c> definitions{};Put(definitions.data(),39*0x9c+0x34,1);Put(image,0x1c9fd28,definitions.data());Put(image,0x1c9fd10,40);
    Bytes<32> row{};Put(row.data(),0,1);Put(row.data(),4,3);row[8]=255;row[9]=255;Put(row.data(),12,43);Put(image,0x1ca0880,row.data());Put(image,0x1d61650+8,0);Put(image,0x1d61650+12,1);
    Bytes<0x288> level{};Put(level.data(),0x1f8,39);Bytes<0x188> room{};Put(room.data(),0x90,level.data());Put(room.data(),0x60,100);Put(room.data(),0x64,200);
    Bytes<0x20> definition{};Put(definition.data(),0x14,3);Bytes<0x48> tile{};Put(tile.data(),8,3);Put(tile.data(),12,4);Put(tile.data(),0x20,definition.data());Put(tile.data(),0x28,16);
    stub(0x98750,CaptureNativeTile);Native<void(__fastcall*)(void*,void*,void*)>(0x94140)(tile.data(),room.data(),&geometry.trees[0]);
    assert(nativeTileCell.id==43 && nativeTileCell.x==-808 && nativeTileCell.y==1252);
    std::cout<<"Native automap traversal renders private trees; event sprites match real unit projection/zoom and use gray-blue/green/red frames with native clipping.\n";
}
