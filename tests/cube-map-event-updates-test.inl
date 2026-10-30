// Integration of event admission, random-monster tables and quest isolation.
int eventOrdinaryOperations=0;
int __fastcall EventOrdinaryOperate(Operation*,int){++eventOrdinaryOperations;return 7;}
void EventUpdates(){
    Reset();games.clear();eventPlayers={};eventClients={};eventPlayerData={};eventPackets.clear();
    auto game=fixture.GamePtr();game->dwInitSeed=91;game->dwGameFrame=1000;game->pClientList=&eventClients[0];
    Put(fixture.level.data(),0x1f8,109);
    for(size_t i=0;i<2;++i){auto& p=eventPlayers[i];p.dwUnitId=55+uint32_t(i);p.dwAnimMode=1;p.pPlayerData=&eventPlayerData[i];auto& c=eventClients[i];c.pGame=game;c.dwClientState=4;c.dwUnitGUID=p.dwUnitId;c.dwClientId=uint32_t(i+1);if(!i)c.pNext=&eventClients[1];Put(&eventPlayerData[i],0xf0,&c);}
    bindings[0x2c49c0]=uintptr_t(EventLookup);bindings[0x209530]=uintptr_t(ParticipantRoom);bindings[0x2d0130]=uintptr_t(Nothing);bindings[0x422ac0]=uintptr_t(CaptureEventPacket);
    void* linker=reinterpret_cast<void*>(1);bindings[0x1c9f6a8]=uintptr_t(&linker);bindings[0x6d0470]=uintptr_t(EncounterName);encounterNames={{"MapGroupA",21},{"ChampClash",22},{"TormentGroupA",23}};
    auto& state=State(game);state.run=std::make_unique<Run>();auto& run=*state.run;run.returns[55]={109,10,20};run.id=12;run.event=Event::TreasureTrove;
    assert(BeginEvent(game,run) && run.eventPhase==EventPhase::Finished && run.phase==Phase::Closed && run.success && run.pins.empty() && !run.portals[0] && !run.portals[1]);
    assert(HasTimedReward(game,&eventPlayers[0],Event::TreasureTrove) && !HasTimedReward(game,&eventPlayers[1],Event::TreasureTrove));
    run.event=Event::GoldenGoose;assert(BeginEvent(game,run));run.event=Event::PlentifulPirating;assert(BeginEvent(game,run));assert(state.timedRewards.at(55).size()==3);
    // The legacy row still exists; it must not be selected as a fallback.
    encounterNames.erase("MapGroupA");run.event=Event::TreasureTrove;assert(!BeginEvent(game,run) && run.failure==EventFailure::TreasureClass);encounterNames["MapGroupA"]=21;
    run.event=Event::ChampionClash;encounterNames.erase("ChampClash");assert(!BeginEvent(game,run) && run.failure==EventFailure::ChampionTreasureClass);encounterNames["ChampClash"]=22;
    run.event=Event::SoulSurprise;assert(!BeginEvent(game,run) && run.failure==EventFailure::SoulTreasureClass);
    encounterNames["SoulSurprise"]=24;
    // Stop after root resolution: the fixture deliberately lacks this arena.
    const auto previousLevels=levels;levels=239;
    assert(!BeginEvent(game,run) && run.failure==EventFailure::Reset && run.eventTreasureClass==24);
    levels=previousLevels;run.event=Event::ChampionClash;
    run.level=108;run.phase=Phase::Running;run.eventPhase=EventPhase::Active;run.eventStarted=1;run.census.ready=run.census.entered=true;run.census.Observe(123,false);
    game->dwGameFrame=2500000;assert(!ProcessEvent(game,run,state) && run.eventPhase==EventPhase::Active && !run.timedOut);
    originalOperate=EventOrdinaryOperate;eventOrdinaryOperations=0;auto& seal=fixture.objects[0];Operation op{};op.game=game;op.object=&seal;op.player=&eventPlayers[0];Put(fixture.level.data(),0x1f8,108);
    for(int id=392;id<=396;++id){seal.dwClassId=id;assert(!OperateHook(&op,0));}assert(!eventOrdinaryOperations);
    seal.dwClassId=397;assert(OperateHook(&op,0)==7);seal.dwClassId=392;Put(fixture.level.data(),0x1f8,109);assert(OperateHook(&op,0)==7);Put(fixture.level.data(),0x1f8,108);run.event=Event::None;assert(OperateHook(&op,0)==7);run.event=Event::ChampionClash;run.phase=Phase::Closed;assert(OperateHook(&op,0)==7 && eventOrdinaryOperations==4);
    // Generic arena tables remain scoped, while Grotto keeps goblin tables.
    bindings[0x209530]=uintptr_t(MockRoom);auto& monster=fixture.old[0];run.phase=Phase::Running;run.eventTreasureClass=22;
    assert(EventTreasureClass(game,&run,&monster,false)==22);run.event=Event::Survivors;assert(EventTreasureClass(game,&run,&monster,false)==-1);run.event=Event::ChampionClash;Put(fixture.level.data(),0x1f8,109);assert(EventTreasureClass(game,&run,&monster,false)==-1);
    std::array<Bytes<0x1dc>,1> records{};Put(records[0].data(),0x3c,uint32_t((1u<<25)|(1u<<15)|1));uint8_t* table=records[0].data();uint64_t rows=1;bindings[0x1c9f878]=uintptr_t(&table);bindings[0x1c9f880]=uintptr_t(&rows);monster.dwClassId=0;monster.pMonsterData->nTypeFlag=0;
    assert(EventTreasureClass(game,nullptr,&monster,true)==21);monster.pMonsterData->nTypeFlag=2;assert(EventTreasureClass(game,nullptr,&monster,true)==-1);monster.pMonsterData->nTypeFlag=0;assert(EventTreasureClass(game,nullptr,&seal,true)==-1);
    run.event=Event::GoblinGrotto;run.objectives.insert(monster.dwUnitId);run.level=39;run.eventTreasureClass=21;Put(fixture.level.data(),0x1f8,39);monster.pMonsterData->pMonstatsTxt->dwMonStatsFlags|=1u<<18;
    assert(EventTreasureClass(game,&run,&monster,false)==-1 && EventTreasureClass(game,&run,&monster,true)==-1);
    // Also preserve specialized roots when the mod counts goblins normally:
    // a timed bonus must not fall through to its generic random-monster TC.
    monster.pMonsterData->pMonstatsTxt->dwMonStatsFlags&=~(1u<<18);
    assert(EventTreasureClass(game,&run,&monster,false)==-1 && EventTreasureClass(game,&run,&monster,true)==-1);
    run.event=Event::SoulSurprise;run.level=239;run.eventTreasureClass=24;Put(fixture.level.data(),0x1f8,239);
    assert(EventTreasureClass(game,&run,&monster,false)==24 && EventTreasureClass(game,&run,&monster,true)==24);
    Put(fixture.level.data(),0x1f8,109);assert(EventTreasureClass(game,&run,&monster,false)==-1);
    assert(EventLevel(Event::ChampionClash)==108 && EventLevel(Event::SoulSurprise)==239 && EventLevel(Event::GoblinGrotto)==39 && !EventLevel(Event::TreasureTrove));
    games.clear();populationCode=nullptr;installed=false;
    std::cout<<"Event integration grants no-location rewards only to participants, preflights exact tables, leaves Champion Clash untimed, blocks only its quest seals and scopes random-monster loot correctly.\n";
}

Room* __fastcall SoulPickupRoom(Unit* unit){return reinterpret_cast<Room*>(fixture.r1[unit->dwUnitId==999?1:0].data());}
void SoulSurprisePickup(){
    Reset();auto game=fixture.GamePtr();game->dwGameFrame=1000;Put(fixture.level.data(),0x1f8,239);
    Bytes<0x288> outside{};Put(outside.data(),0x1f8,109);Put(fixture.r2[1].data(),0x90,outside.data());
    bindings[0x209530]=uintptr_t(SoulPickupRoom);
    std::array<Bytes<0x1b4>,7> records{};uint8_t* table=records[0].data();uint64_t count=records.size();
    for(int i=0;i<6;++i)Put(records[i].data(),0x80,uint32_t(0x20313053+(i<<16))); // S01..S06
    Put(records[6].data(),0x80,uint32_t(0x30323053)); // S020 is not S02.
    bindings[0x1ca0390]=uintptr_t(&table);bindings[0x1ca0398]=uintptr_t(&count);
    Run run;run.event=Event::SoulSurprise;run.phase=Phase::Running;run.eventPhase=EventPhase::Active;run.level=239;
    run.census.ready=run.census.entered=true;run.census.Observe(1,true);run.pins.push_back({fixture.r2[0].data(),false});
    auto& monster=fixture.old[0];monster.dwAnimMode=12;monster.pRoomNext=nullptr;
    // An empty/dead area without a confirmed drop never starts the return.
    UpdateClearTransition(game,run,true);assert(!run.clearedAt&&run.phase==Phase::Running);
    game->dwGameFrame+=10000;UpdateClearTransition(game,run,true);assert(!run.clearedAt);
    run.soulDropObserved=true;monster.dwAnimMode=1;
    monster.pMonsterData->pMonstatsTxt->dwMonStatsFlags|=1u<<18;
    UpdateClearTransition(game,run,true);assert(!run.clearedAt); // Alive even when omitted from the normal census.
    monster.dwAnimMode=12;monster.pMonsterData->pMonstatsTxt->dwMonStatsFlags&=~(1u<<18);
    Unit item{};item.dwUnitType=4;item.dwItemMode=IMODE_ONGROUND;item.dwUnitId=88;monster.pRoomNext=&item;
    for(int i=1;i<=4;++i){run.soulDropObserved=false;item.dwClassId=i;UpdateClearTransition(game,run,true);assert(run.soulDropObserved&&!run.clearedAt&&run.phase==Phase::Running);}
    item.dwItemMode=IMODE_DROPPING;UpdateClearTransition(game,run,true);assert(!run.clearedAt);
    // All fragments must leave the floor, including a second copy from a bonus.
    Unit second=item;second.dwClassId=4;second.dwItemMode=IMODE_ONGROUND;item.pRoomNext=&second;item.dwItemMode=IMODE_ONCURSOR;
    UpdateClearTransition(game,run,true);assert(!run.clearedAt);
    second.dwItemMode=IMODE_STORED;UpdateClearTransition(game,run,true);assert(run.clearedAt==game->dwGameFrame);
    const auto picked=game->dwGameFrame;game->dwGameFrame=picked+49;UpdateClearTransition(game,run,true);assert(run.phase==Phase::Running);
    game->dwGameFrame=picked+50;UpdateClearTransition(game,run,true);assert(run.phase==Phase::Returning&&run.success);
    // A late drop or revived monster cancels a queued return on non-census ticks.
    second.dwItemMode=IMODE_ONGROUND;run.returning.insert(55);UpdateClearTransition(game,run,false);
    assert(run.phase==Phase::Running&&run.eventPhase==EventPhase::Active&&!run.success&&!run.clearedAt&&run.returning.empty());
    second.dwItemMode=IMODE_STORED;monster.pRoomNext=nullptr;item.pRoomNext=nullptr;
    item.dwItemMode=IMODE_ONGROUND;item.dwClassId=1;game->pUnitList[4][17]=&item;
    assert(!SoulSurpriseReady(game,run)); // Registered before room insertion.
    item.dwUnitId=999;assert(SoulSurpriseReady(game,run)); // Other areas do not block this party.
    item.dwUnitId=88;
    for(int index:{0,5,6}){item.dwClassId=index;assert(SoulSurpriseReady(game,run));}
    game->pUnitList[4][17]=nullptr;fixture.fresh[0].pListNext=nullptr;game->pUnitList[1][2]=&fixture.fresh[0];
    assert(!SoulSurpriseReady(game,run));
    run.phase=Phase::Returning;run.eventPhase=EventPhase::Returning;run.success=true;run.clearedAt=game->dwGameFrame;run.returning.insert(55);
    UpdateClearTransition(game,run,false);assert(run.phase==Phase::Running&&run.eventPhase==EventPhase::Active&&!run.clearedAt&&run.returning.empty());
    game->pUnitList[1][2]=nullptr;
    // Inventory/cursor copies and unrelated codes do not hold an ordinary map.
    run.event=Event::None;run.soulDropObserved=false;run.clearedAt.reset();UpdateClearTransition(game,run,true);assert(run.clearedAt);
    games.clear();populationCode=nullptr;installed=false;
    std::cout<<"Soul Surprise return waits for a confirmed fragment drop, every live monster and all S02-S05 floor/dropping copies; cursor/inventory and other areas do not block, pickup starts the delay, and late loot/revival cancels queued returns.\n";
}
