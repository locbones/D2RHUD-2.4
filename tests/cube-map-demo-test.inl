int __fastcall DemoLevelStat(Unit*,uint32_t stat,uint16_t){assert(stat==12);return 85;}
void EventDemoShortcuts(){
    uint8_t held=0;
    for(uint8_t key=1;key<=7;++key){
        const auto mask=uint8_t(1u<<(key-1));
        assert(TakeDemoShortcut(held,mask)==key);
        for(int tick=0;tick<100;++tick)assert(!TakeDemoShortcut(held,mask));
        assert(!TakeDemoShortcut(held,0));
    }
    assert(!TakeDemoShortcut(held,3) && !TakeDemoShortcut(held,2));
    assert(!TakeDemoShortcut(held,0) && TakeDemoShortcut(held,2)==2);
    assert(TakeDemoShortcut(held,4)==3); // A different single-key press.
    Reset();eventPlayers={};eventClients={};eventPlayerData={};eventPackets.clear();
    auto game=fixture.GamePtr();game->dwGameFrame=1000;game->dwInitSeed=91;game->pClientList=&eventClients[0];
    Put(fixture.level.data(),0x1f8,109);
    for(size_t i=0;i<2;++i){
        auto& p=eventPlayers[i];p.dwUnitId=55+uint32_t(i);p.dwAnimMode=1;p.pPlayerData=&eventPlayerData[i];
        auto& c=eventClients[i];c.pGame=game;c.dwClientState=4;c.dwUnitGUID=p.dwUnitId;c.dwClientId=uint32_t(i+1);
        if(!i)c.pNext=&eventClients[1];Put(&eventPlayerData[i],0xf0,&c);
    }
    bindings[0x2c49c0]=uintptr_t(EventLookup);bindings[0x209530]=uintptr_t(ParticipantRoom);
    bindings[0x1e5620]=uintptr_t(DemoLevelStat);bindings[0x2d0130]=uintptr_t(Nothing);bindings[0x422ac0]=uintptr_t(CaptureEventPacket);
    void* linker=reinterpret_cast<void*>(1);uint64_t stateCount=260;
    std::vector<Bytes<0x40>> states(stateCount);uint8_t* stateRows=states[0].data();
    bindings[0x1c9ec08]=uintptr_t(&linker);bindings[0x6d0470]=uintptr_t(EncounterName);encounterNames={{"hud_cube_map",259}};
    bindings[0x1c9ebf8]=uintptr_t(&stateCount);bindings[0x1c9ebf0]=uintptr_t(&stateRows);
    auto& state=State(game);
    assert(!StartDemoEvent(game,state,&eventPlayers[0],0) && !state.run);
    assert(!StartDemoEvent(game,state,&eventPlayers[0],8) && !state.run);
    Put(fixture.level.data(),0x1f8,108);assert(!StartDemoEvent(game,state,&eventPlayers[0],1) && !state.run);Put(fixture.level.data(),0x1f8,109);
    eventPlayers[0].dwAnimMode=0;assert(!StartDemoEvent(game,state,&eventPlayers[0],1) && !state.run);eventPlayers[0].dwAnimMode=1;
    eventClients[0].pGame=nullptr;assert(!StartDemoEvent(game,state,&eventPlayers[0],1) && !state.run);eventClients[0].pGame=game;
    state.restoreLevels.insert(39);assert(!StartDemoEvent(game,state,&eventPlayers[0],1) && !state.run);state.restoreLevels.clear();
    states[259][0x10]=1;assert(!StartDemoEvent(game,state,&eventPlayers[0],1) && !state.run);states[259][0x10]=0;
    for(uint8_t key=1;key<=7;++key){
        eventPackets.clear();assert(StartDemoEvent(game,state,&eventPlayers[0],key));
        auto& run=*state.run;const auto expected=key==7 ? Event::Trial : RegularEvents[key-1];
        assert(run.event==expected && run.die==(key==7?6:key) && run.creator==55 && run.characterLevel==85 && run.state==259);
        assert(run.phase==Phase::Closed && run.eventPhase==EventPhase::WaitingTown && run.ready.empty());
        assert(run.returns.size()==2 && run.returns.contains(55) && run.returns.contains(56));
        assert(run.rewards.empty() && !run.success && run.rolls.empty());
        assert(eventPackets.size()==2);
        for(const auto& packet:eventPackets)assert(Data::Valid(packet) && packet.event==expected && !packet.reward && packet.eventPhase==EventPhase::WaitingTown);
        const auto id=run.id;assert(!StartDemoEvent(game,state,&eventPlayers[0],1) && state.run->id==id);
        CommandPacket ready;ready.run=run.id;ready.session=91;
        for(auto& player:eventPlayers)assert(Command(game,&player,reinterpret_cast<uint8_t*>(&ready),sizeof(ready)));
        assert(ProcessEvent(game,run,state) && run.eventPhase==EventPhase::Ceremony);
        game->dwGameFrame+=CeremonyFrames;assert(ProcessEvent(game,run,state) && run.eventPhase==EventPhase::Briefing);
        // A completed event can be replaced directly during its result screen.
        run.phase=Phase::Closed;run.eventPhase=EventPhase::Finished;
    }
    state.run.reset();eventPlayers[1].dwAnimMode=0;
    assert(StartDemoEvent(game,state,&eventPlayers[0],7) && state.run->returns.size()==1);
    state.run.reset();state.run=std::make_unique<Run>();state.run->id=999;state.run->phase=Phase::Open;
    assert(!StartDemoEvent(game,state,&eventPlayers[0],1) && state.run->id==999);
    games.clear();encounterNames.clear();installed=false;populationCode=nullptr;
    std::cout<<"Ctrl+F1-F7 event simulation: all seven outcomes, held-key debounce, admission guards, authenticated party ceremony and active-run preservation passed.\n";
}
