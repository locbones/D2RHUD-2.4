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
