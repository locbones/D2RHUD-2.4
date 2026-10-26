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
    run.event=Event::SoulSurprise;assert(EventTreasureClass(game,&run,&monster,false)==21);
    assert(EventLevel(Event::ChampionClash)==108 && EventLevel(Event::SoulSurprise)==239 && EventLevel(Event::GoblinGrotto)==39 && !EventLevel(Event::TreasureTrove));
    games.clear();populationCode=nullptr;installed=false;
    std::cout<<"Event integration grants no-location rewards only to participants, preflights exact tables, leaves Champion Clash untimed, blocks only its quest seals and scopes random-monster loot correctly.\n";
}
