std::map<Unit*,Room*> revealRooms;
std::vector<std::pair<uint32_t,MonsterRevealData::Wire>> revealPackets;
Room* __fastcall RevealRoom(Unit* unit){return revealRooms.contains(unit)?revealRooms.at(unit):MockRoom(unit);}
int __fastcall RevealStat(Unit* unit,uint32_t stat,uint16_t){assert(stat==172);return unit==&fixture.old[1]?1:0;}
void __fastcall CaptureReveal(void*,uint32_t client,void** range){
    assert(static_cast<uint8_t*>(range[1])-static_cast<uint8_t*>(range[0])==sizeof(Packet));
    auto packet=*static_cast<MonsterRevealData::Wire*>(range[0]);assert(MonsterRevealData::Valid(packet));revealPackets.emplace_back(client,packet);
}
void MapMonsterReveal(){
    using W=MonsterRevealData::Wire;
    static_assert(MonsterRevealData::Operation!=QuestJournal::Data::Operation);
    Reset();ClearMonsterReveal();revealPackets.clear();revealRooms.clear();eventPlayers={};eventClients={};eventPlayerData={};
    auto game=fixture.GamePtr();game->dwInitSeed=91;game->dwGameFrame=1000;game->pClientList=&eventClients[0];
    Bytes<0x288> outsideLevel{};Bytes<0x1c0> outsideR2{};Bytes<0x108> outsideR1{};
    Put(outsideLevel.data(),0x1f8,2);Put(outsideR2.data(),0x90,outsideLevel.data());Put(outsideR1.data(),0x18,outsideR2.data());
    auto outside=reinterpret_cast<Room*>(outsideR1.data());auto inside=reinterpret_cast<Room*>(fixture.r1[0].data());
    for(size_t n=0;n<2;++n){auto& p=eventPlayers[n];p.dwUnitId=55+uint32_t(n);p.dwAnimMode=1;p.pPlayerData=&eventPlayerData[n];Put(&eventPlayerData[n],0xf0,&eventClients[n]);
        auto& c=eventClients[n];c.pGame=game;c.dwClientState=4;c.dwUnitGUID=p.dwUnitId;c.dwClientId=uint32_t(n+1);if(!n)c.pNext=&eventClients[1];revealRooms[&p]=n?outside:inside;}
    bindings[0x2c49c0]=uintptr_t(EventLookup);bindings[0x209530]=uintptr_t(RevealRoom);bindings[0x1e5620]=uintptr_t(RevealStat);
    bindings[0x2d0130]=uintptr_t(Nothing);bindings[0x422ac0]=uintptr_t(CaptureReveal);
    std::array<D2DynamicPathStrc,3> paths{};
    for(int i=0;i<3;++i){fixture.old[i].pDynamicPath=&paths[i];Put(&paths[i],8,1000+i*100);Put(&paths[i],12,2000+i*100);}
    fixture.old[2].dwAnimMode=12;
    Unit distant=fixture.old[0],foreign=distant;distant.dwUnitId=44;foreign.dwUnitId=45;
    revealRooms[&distant]=inside;revealRooms[&foreign]=outside;
    game->pUnitList[1][44]=&distant;game->pUnitList[1][45]=&foreign;
    game->pUnitList[1][1]=&fixture.old[0];fixture.old[0].pListNext=&fixture.old[1];fixture.old[1].pListNext=&fixture.old[0]; // Bounded cycle.
    auto& state=State(game);state.run=std::make_unique<Run>();auto& run=*state.run;run.id=77;run.level=256;run.phase=Phase::Running;
    for(auto& room:fixture.r2)run.pins.push_back({room.data(),true});
    ProcessMonsterReveal(game,state);assert(revealPackets.size()==1);
    const auto mapPacket=revealPackets.front().second;
    assert(mapPacket.recipient==55 && mapPacket.run==77 && mapPacket.level==256 && mapPacket.total==2 && mapPacket.lease==4000);
    assert(mapPacket.entries[0].id==1 && mapPacket.entries[1].id==44); // No summons, corpses, duplicate or foreign-area monsters.
    fixture.old[0].dwAnimMode=distant.dwAnimMode=12;assert(RevealMonsterMarkers(game,256,&run).empty());
    fixture.old[0].dwAnimMode=distant.dwAnimMode=1;
    ProcessMonsterReveal(game,state);assert(revealPackets.size()==1);
    revealRooms[&eventPlayers[0]]=outside;ProcessMonsterReveal(game,state);assert(revealPackets.size()==2 && !revealPackets.back().second.lease);
    revealRooms[&eventPlayers[0]]=inside;ProcessMonsterReveal(game,state);assert(revealPackets.size()==3 && revealPackets.back().second.lease);
    run.phase=Phase::Closed;ProcessMonsterReveal(game,state);assert(revealPackets.size()==4 && !revealPackets.back().second.lease);
    // Timed rewards work outside any map and belong only to their recipient.
    GrantTimedReward(game,56,Event::PlentifulPirating);ProcessMonsterReveal(game,state);
    assert(revealPackets.size()==5 && revealPackets.back().second.recipient==56 && !revealPackets.back().second.run && revealPackets.back().second.level==2);
    auto rewardPacket=revealPackets.back().second;
    game->dwGameFrame=3999;ProcessMonsterReveal(game,state);assert(revealPackets.back().second.lease==40);
    game->dwGameFrame=4000;ProcessMonsterReveal(game,state);assert(!revealPackets.back().second.lease);
    GrantTimedReward(game,56,Event::TreasureTrove);GrantTimedReward(game,56,Event::GoldenGoose);GrantTimedReward(game,56,Event::PlentifulPirating);
    game->dwGameFrame=7000;ProcessMonsterReveal(game,state);assert(revealPackets.back().second.lease==4000);
    game->dwGameFrame=19000;ProcessMonsterReveal(game,state);assert(!revealPackets.back().second.lease);
    uint32_t scope=0;run.phase=Phase::Running;
    assert(MonsterRevealLease(game,state,&eventPlayers[0],scope)==4000 && scope==77);
    eventPlayers[0].dwAnimMode=17;assert(!MonsterRevealLease(game,state,&eventPlayers[0],scope));eventPlayers[0].dwAnimMode=1;
    // A reward expiring while still in a map keeps that map's area reveal.
    state.timedRewards[55][Event::PlentifulPirating]=game->dwGameFrame-3000;
    assert(MonsterRevealLease(game,state,&eventPlayers[0],scope)==4000 && scope==77);
    game->dwGameFrame=UINT32_MAX-10;state.timedRewards[55][Event::PlentifulPirating]=game->dwGameFrame;
    game->dwGameFrame+=2999;assert(MonsterRevealLease(game,state,&eventPlayers[0],scope)==40 && !scope);
    ++game->dwGameFrame;assert(MonsterRevealLease(game,state,&eventPlayers[0],scope)==4000 && scope==77);
    game->dwGameFrame=19000;Put(outsideLevel.data(),0x1f8,109);GrantTimedReward(game,56,Event::TreasureTrove);
    assert(!MonsterRevealLease(game,state,&eventPlayers[1],scope));Put(outsideLevel.data(),0x1f8,2);
    // Client scope checks the current map, level, player and authenticated game epoch.
    Packet map;map.active=1;map.run=77;map.session=91;map.recipient=55;map.level=256;
    MonsterRevealData::View model;assert(model.Accept(mapPacket,1000));
    assert(model.Visible(1000,55,256,91,state.layoutEpoch,map,0));
    assert(!model.Visible(1000,55,2,91,state.layoutEpoch,map,0) && !model.Visible(1000,56,256,91,state.layoutEpoch,map,0));
    assert(!model.Visible(5000,55,256,91,state.layoutEpoch,map,0) && !model.Visible(999,55,256,91,state.layoutEpoch,map,0));
    map.active=0;assert(!model.Visible(1001,55,256,91,state.layoutEpoch,map,0));map.active=1;
    assert(!model.Visible(1001,55,256,91,state.layoutEpoch+1,map,0) && !model.Visible(1001,55,256,92,state.layoutEpoch,map,0));
    auto cleared=mapPacket;++cleared.revision;cleared.total=cleared.count=0;
    assert(model.Accept(cleared,1001) && model.ready->empty());
    model={};rewardPacket.lease=40;assert(model.Accept(rewardPacket,1000));
    assert(model.Visible(1039,56,2,91,state.layoutEpoch,{},UINT64_MAX) && !model.Visible(1040,56,2,91,state.layoutEpoch,{},UINT64_MAX));
    auto expired=rewardPacket;++expired.revision;expired.lease=0;expired.total=expired.count=0;
    assert(model.Accept(expired,1001) && !model.Visible(1001,56,2,91,state.layoutEpoch,{},0) && !model.Accept(rewardPacket,1002));
    // Multi-part snapshots publish atomically and cannot extend their lease by delaying the last chunk.
    W head=rewardPacket;head.revision+=10;head.lease=4000;head.total=13;head.count=12;head.entries={};
    auto tail=head;tail.offset=12;tail.count=1;model={};assert(model.Accept(head,1000) && !model.ready);
    assert(!model.Accept(head,1001));auto invalid=tail;invalid.lease=3999;assert(!model.Accept(invalid,1002));
    assert(model.Accept(tail,5000) && model.ready->size()==13 && !model.Visible(5000,56,2,91,state.layoutEpoch,{},0));
    invalid=head;invalid.offset=12;invalid.count=0;invalid.total=12;assert(!MonsterRevealData::Valid(invalid));
    invalid=head;invalid.entries[0].kind=4;assert(!MonsterRevealData::Valid(invalid));invalid=head;invalid.epoch=0;assert(!MonsterRevealData::Valid(invalid));
    invalid=head;invalid.lease=4001;assert(!MonsterRevealData::Valid(invalid));
    // Exercise real receive and draw hooks with the ordinary-map stream.
    const auto savedKnown=clientLayoutSessionKnown;const auto savedSession=clientLayoutSession,savedEpoch=clientLayoutEpoch;
    clientLayoutSessionKnown=true;clientLayoutSession=91;clientLayoutEpoch=state.layoutEpoch;
    automapPlayer=eventPlayers[0];uint32_t selector=0;
    bindings[0x1d442d8]=uintptr_t(&selector);bindings[0x6b050]=uintptr_t(AutomapLocal);revealRooms[&automapPlayer]=inside;
    assert(Receive(reinterpret_cast<const uint8_t*>(&mapPacket),sizeof(mapPacket)));
    assert(!Receive(reinterpret_cast<const uint8_t*>(&mapPacket),sizeof(mapPacket)));
    auto bad=mapPacket;bad.epoch++;bad.revision++;assert(!Receive(reinterpret_cast<const uint8_t*>(&bad),sizeof(bad)));
    view=map;receivedAt=GetTickCount64();Bytes<0xb0> layer{};void* current=layer.data();bindings[0x1d56150]=uintptr_t(&current);
    Bytes<0x40> transform{};Put(transform.data(),0x20,1000);Put(transform.data(),0x24,1000);Put(transform.data(),0x30,1.0f);Put(transform.data(),0x34,1.0f);Put(transform.data(),0x38,1.0f);
    bindings[0x977a0]=uintptr_t(CaptureMapIcon);originalMapCells=CaptureMapCells;originalMapReset=CaptureMapReset;
    automapIcons=0;MapCellsHook(0,layer.data()+0x58,transform.data(),nullptr);assert(automapIcons==2);
    revealRooms[&automapPlayer]=outside;MapCellsHook(0,layer.data()+0x58,transform.data(),nullptr);assert(automapIcons==2);
    revealRooms[&automapPlayer]=inside;view.active=0;MapCellsHook(0,layer.data()+0x58,transform.data(),nullptr);assert(automapIcons==2);
    MapResetHook();assert(!monsterRevealView.known && !monsterRevealView.ready);
    clientLayoutSessionKnown=savedKnown;clientLayoutSession=savedSession;clientLayoutEpoch=savedEpoch;
    games.clear();revealRooms.clear();installed=false;populationCode=nullptr;
    std::cout<<"Monster reveal covers ordinary maps and personal timed rewards, includes distant enemies, excludes allies/corpses/other areas, expires at the reward boundary and clears on zone/map/game exit.\n";
}

std::map<Unit*,int> summonMarkerAlignment;
int __fastcall SummonMarkerStat(Unit* unit,uint32_t stat,uint16_t){assert(stat==172);return summonMarkerAlignment.contains(unit)?summonMarkerAlignment.at(unit):0;}
std::vector<AutomapData::Wire> summonEventPackets;
std::vector<MonsterRevealData::Wire> summonRevealPackets;
void __fastcall CaptureSummonMarkers(void*,uint32_t,void** range){
    assert(static_cast<uint8_t*>(range[1])-static_cast<uint8_t*>(range[0])==sizeof(Packet));
    const auto operation=static_cast<const uint8_t*>(range[0])[9];
    if(operation==AutomapData::Operation){auto w=*static_cast<AutomapData::Wire*>(range[0]);assert(AutomapData::Valid(w));summonEventPackets.push_back(w);}
    else{assert(operation==MonsterRevealData::Operation);auto w=*static_cast<MonsterRevealData::Wire*>(range[0]);assert(MonsterRevealData::Valid(w));summonRevealPackets.push_back(w);}
}
void MapSummonMarkers(){
    Reset();revealRooms.clear();summonMarkerAlignment.clear();eventPlayers={};eventClients={};eventPlayerData={};
    auto game=fixture.GamePtr();game->dwInitSeed=91;game->dwGameFrame=1000;game->pClientList=&eventClients[0];Put(fixture.level.data(),0x1f8,39);
    const auto room=reinterpret_cast<Room*>(fixture.r1[0].data());
    for(size_t i=0;i<2;++i){
        auto& p=eventPlayers[i];p.dwUnitId=55+uint32_t(i);p.dwAnimMode=1;p.pPlayerData=&eventPlayerData[i];
        auto& c=eventClients[i];c.pGame=game;c.dwClientState=4;c.dwUnitGUID=p.dwUnitId;if(!i)c.pNext=&eventClients[1];
        revealRooms[&p]=room;
    }
    bindings[0x2c49c0]=uintptr_t(EventLookup);bindings[0x209530]=uintptr_t(RevealRoom);bindings[0x1e5620]=uintptr_t(SummonMarkerStat);
    int types=6;bindings[0x1c9fbd0]=uintptr_t(&types);
    std::array<Bytes<0x10>,2> lists{};std::array<std::array<Bytes<0x10>,6>,2> headers{};
    for(size_t i=0;i<2;++i){Put(&eventPlayerData[i],0x98,lists[i].data());Put(lists[i].data(),0,headers[i].data());}
    std::array<Unit,8> units{};std::array<D2MonsterDataStrc,8> data{};std::array<Bytes<0x1dc>,8> rows{};std::array<D2DynamicPathStrc,8> paths{};std::array<Bytes<0x20>,8> nodes{};
    for(size_t i=0;i<units.size();++i){
        auto& u=units[i];u.dwUnitType=1;u.dwClassId=42;u.dwUnitId=100+uint32_t(i);u.dwAnimMode=1;u.pDynamicPath=&paths[i];u.pMonsterData=&data[i];
        data[i].pMonstatsTxt=reinterpret_cast<D2MonStatsTxt*>(rows[i].data());data[i].pMonstatsTxt->dwMonStatsFlags=1u<<15;
        Put(&paths[i],8,1000+int(i)*100);Put(&paths[i],12,2000);revealRooms[&u]=room;
        Put(nodes[i].data(),4,u.dwUnitId);if(i+1<units.size())u.pRoomNext=&units[i+1];
    }
    // A standard summon, revived unique, mercenary and teammate's custom pet.
    // Revive deliberately retains enemy alignment and boss flags; ownership wins.
    for(auto i:{1,3,4,5,6,7})summonMarkerAlignment[&units[i]]=1;
    data[2].nTypeFlag=2;data[3].pMonstatsTxt->dwMonStatsFlags=0x700;units[5].dwAnimMode=12;
    Put(nodes[1].data(),0x18,nodes[2].data());Put(nodes[2].data(),0x18,nodes[3].data());Put(nodes[3].data(),0x18,nodes[5].data());
    Put(nodes[5].data(),0x18,nodes[6].data());Put(nodes[6].data(),0x18,nodes[1].data());Put(nodes[6].data(),0,uint32_t(1)); // Removed entry, bounded cycle.
    Put(headers[0][1].data(),0,nodes[1].data());Put(headers[1][5].data(),0,nodes[7].data());
    Put(fixture.r1[0].data(),0xa8,&units[0]);
    Run run;run.id=77;run.level=39;run.event=Event::Trial;run.pins.push_back({fixture.r2[0].data(),true});
    const auto companions=MapCompanionIds(game);assert(companions.size()==5 && companions.contains(102) && companions.contains(107) && !companions.contains(106));
    auto ordinary=RevealMonsterMarkers(game,39,&run),event=EventMonsterMarkers(game,run);
    assert(ordinary.size()==5 && event.size()==ordinary.size());
    for(size_t i=0;i<ordinary.size();++i){
        assert(ordinary[i].id==event[i].id && ordinary[i].kind==event[i].kind);
        assert(ordinary[i].kind==(ordinary[i].id==100?0:AutomapData::CompanionKind));
    }
    // Registry refresh removes ownership immediately, without a class-name cache.
    Put(nodes[2].data(),0,uint32_t(1));const auto changed=RevealMonsterMarkers(game,39,&run);
    assert(changed[2].id==102 && changed[2].kind==3);Put(nodes[2].data(),0,uint32_t(0));
    types=1025;assert(MapCompanionIds(game).empty());types=6;
    AutomapData::Wire eventWire;eventWire.run=77;eventWire.recipient=55;eventWire.revision=1;eventWire.stream=AutomapData::Stream::Monsters;
    eventWire.total=eventWire.count=uint8_t(event.size());AutomapData::EncodeMarkers(eventWire,event,0);assert(AutomapData::Valid(eventWire));
    MonsterRevealData::Wire revealWire;revealWire.epoch=1;revealWire.revision=1;revealWire.level=39;revealWire.lease=4000;revealWire.total=revealWire.count=uint8_t(ordinary.size());
    AutomapData::EncodeMarkers(revealWire,ordinary,0);assert(MonsterRevealData::Valid(revealWire));
    // Reserved bytes add color without changing old packet sizes/kinds/offsets.
    assert(eventWire.companions==30 && revealWire.companions==30);
    for(size_t i=0;i<eventWire.count;++i)assert(eventWire.entries[i].kind<=3 && revealWire.entries[i].kind<=3);
    auto invalidEvent=eventWire;invalidEvent.companions|=1u<<5;assert(!AutomapData::Valid(invalidEvent));
    invalidEvent=eventWire;invalidEvent.stream=AutomapData::Stream::Geometry;assert(!AutomapData::Valid(invalidEvent));
    auto invalidReveal=revealWire;invalidReveal.companions|=1u<<5;assert(!MonsterRevealData::Valid(invalidReveal));
    AutomapData::Assembly eventView;assert(eventView.Accept(eventWire) && eventView.ready->size()==5 && eventView.ready->at(1).kind==AutomapData::CompanionKind);
    MonsterRevealData::View revealView;assert(revealView.Accept(revealWire,1000) && revealView.ready->size()==5 && revealView.ready->at(1).kind==AutomapData::CompanionKind);
    // Exercise both real host send paths, not only manual packet construction.
    auto& state=State(game);state.run=std::make_unique<Run>();state.run->id=77;state.run->level=39;state.run->phase=Phase::Running;state.run->pins=run.pins;
    bindings[0x2d0130]=uintptr_t(Nothing);bindings[0x422ac0]=uintptr_t(CaptureSummonMarkers);summonEventPackets.clear();summonRevealPackets.clear();
    ProcessMonsterReveal(game,state);assert(summonRevealPackets.size()==2);
    for(const auto& w:summonRevealPackets)assert(w.total==5 && w.companions==30);
    Packet statePacket;statePacket.run=77;statePacket.session=91;statePacket.recipient=55;statePacket.level=39;
    SendAutomap(&eventClients[0],statePacket,AutomapData::Stream::Monsters,1,event);
    assert(summonEventPackets.size()==1 && summonEventPackets[0].companions==30);
    Bytes<0x40> transform{};Put(transform.data(),0x20,1000);Put(transform.data(),0x24,1000);Put(transform.data(),0x30,1.0f);Put(transform.data(),0x34,1.0f);Put(transform.data(),0x38,1.0f);
    bindings[0x977a0]=uintptr_t(CaptureMapIcon);automapIconFrames.clear();DrawEventMarkers(ordinary,transform.data());
    assert((automapIconFrames==std::vector<int>{3,0,0,0,0}));
    assert(run.census.dead.empty() && run.objectives.empty());
    // Return fixture bindings before another test can inspect these stack lists.
    bindings[0x1c9fbd0]=0;games.erase(game);revealRooms.clear();summonMarkerAlignment.clear();installed=false;populationCode=nullptr;
    std::cout<<"Map icons use bright-blue native sprites for registered summons/revives/mercenaries from both players; same-class enemies keep rank colors, neutral/dead/removed units stay hidden, registry cycles are bounded, and both packet/render paths preserve companion kinds.\n";
}
