// Included after relocation, transport and completion helpers.
bool BeginEvent(Game* game,Run& run){
    if(!State(game).restoreLevels.empty())return false;
    EventTrace(game,run,"event-begin");
    const auto treasureName=run.event==Event::ChampionClash ? "ChampClash" : "TormentGroupA";
    run.eventTreasureClass=LookupName(0x1c9f6a8,treasureName);
    if(run.eventTreasureClass<=0){run.failure=run.event==Event::ChampionClash ? EventFailure::ChampionTreasureClass : EventFailure::TreasureClass;return false;}
    if(TimedEvent(run.event)){
        size_t granted=0;for(auto player:Players(game))if(run.returns.contains(player->dwUnitId) && !Dead(player) && Town(Level(player))){GrantTimedReward(game,player->dwUnitId,run.event);++granted;}
        if(!granted){run.failure=EventFailure::Participant;return false;}
        run.phase=Phase::Closed;run.success=true;run.eventPhase=EventPhase::Finished;run.eventFrame=game->dwGameFrame;run.completedAt=game->dwGameFrame;
        run.rolls.clear();run.failure=EventFailure::None;ProcessTimedRewards(game,State(game),true);
        status.store("Ten-minute event bonus granted. You can continue exploring or open another map.");return true;
    }
    run.level=EventLevel(run.event);run.phase=Phase::Preparing;run.rolls.clear();run.players.clear();run.monsters.clear();run.bosses.clear();
    if(!ValidLevel(run.level)){run.failure=EventFailure::Reset;return false;}
    run.roster.clear();run.census={};run.clearedAt.reset();run.completedAt.reset();run.returning.clear();run.immunities.clear();run.objectives.clear();run.levelsFinalized.clear();run.eventPlayerStats.clear();
    run.portals={};run.monsterLevel=run.event==Event::Trial ? 125 : (std::min)(255,run.characterLevel+5);
    run.failure=EventFailure::Initializer;
    run.initializer=BuildInitializer(run.monsterLevel);
    if(!run.initializer)return false;
    EventTrace(game,run,"initializer-ready");
    run.failure=EventFailure::Roster;if(!BuildEncounters(run))return false;
    if(run.event==Event::SpeedDemon){run.failure=EventFailure::PlayerStats;for(const auto& [name,value]:{std::pair{"item_fastermovevelocity",255},std::pair{"item_fasterattackrate",255},std::pair{"item_fastercastrate",255},std::pair{"velocitypercent",250},std::pair{"attackrate",175},std::pair{"other_animrate",175}}){
        const int id=LookupName(0x1c9fb90,name);if(id<0 || id>=511)return false;run.eventPlayerStats.push_back({id,value});
    }}
    if(!Reload(game,run))return false;
    BuildEventAutomap(run);
    EventTrace(game,run,"encounters-ready");
    run.failure=EventFailure::Arrival;
    Position arrival;auto arena=Spawn(game,run.level,arrival);if(!arena)return false;
    Unit* source=nullptr;for(auto player:Players(game))if(run.returns.contains(player->dwUnitId) && !Dead(player) && Town(Level(player))){source=player;if(player->dwUnitId==run.creator)break;}
    run.failure=EventFailure::Participant;if(!source)return false;
    run.entrance=Location(source);auto townRoom=GetRoom(source);
    run.failure=EventFailure::Portal;
    auto exit=Portal(game,arena,arrival,run.entrance.level);if(exit)run.portals[1]=exit->dwUnitId;
    auto entry=Portal(game,townRoom,run.entrance,run.level);if(entry)run.portals[0]=entry->dwUnitId;
    if(!exit || !entry)return false;
    EventTrace(game,run,"portals-ready");
    auto sourceR2=Field<void*>(GetRoom(entry),0x18);run.entrancePin=Pin{sourceR2,(Field<uint32_t>(sourceR2,0x50)&Pinned)!=0};Set(sourceR2,0x50,Field<uint32_t>(sourceR2,0x50)|Pinned);
    run.phase=Phase::Open;CensusAndBuffs(game,run);run.census.ready=true;
    run.failure=EventFailure::None;run.eventPhase=EventPhase::Entering;run.eventFrame=game->dwGameFrame;run.ready.clear();EventTrace(game,run,"entering");return true;
}
bool ProcessEvent(Game* game,Run& run,GameState& state){
    if(run.event==Event::None)return false;
    const auto frame=game->dwGameFrame;
    auto age=[&](){return frame-run.eventFrame;};
    auto connected=[&](){std::vector<Unit*> players;for(auto player:Players(game))if(run.returns.contains(player->dwUnitId))players.push_back(player);return players;};
    auto players=connected();
    size_t expected=0,nClients=0;for(auto client=game->pClientList;client && nClients++<64;client=client->pNext)if(client->pGame==game && run.returns.contains(client->dwUnitGUID))++expected;
    if(!expected){
        // No eligible living participant remains; never reset or reward an
        // unrelated player's cow level, and never leave event buffs behind.
        Clean(game,run);state.run.reset();return true;
    }
    if(run.eventPhase==EventPhase::WaitingTown){
        bool ready=players.size()==expected;for(auto player:players)if(Dead(player) || !Town(Level(player)) || !run.ready.contains(player->dwUnitId))ready=false;
        if(ready){run.eventPhase=EventPhase::Ceremony;run.eventFrame=frame;Sync(game,run,true);}else Sync(game,run);
        return true;
    }
    if(run.eventPhase==EventPhase::Ceremony){
        if(age()>=CeremonyFrames){run.eventPhase=EventPhase::Briefing;run.eventFrame=frame;EventTrace(game,run,"briefing");Sync(game,run,true);}else Sync(game,run);
        return true;
    }
    if(run.eventPhase==EventPhase::Briefing){
        if(age()>=BriefingFrames && state.restoreLevels.empty()){
            if(!BeginEvent(game,run)){
                Clean(game,run);run.eventPhase=EventPhase::Cancelled;run.eventFrame=frame;run.completedAt=frame;
                status.store(FailureMessage(run.failure));
            }
            Sync(game,run,true);
        }else Sync(game,run);
        return true;
    }
    if(run.eventPhase==EventPhase::Entering){
        Position arrival;const auto room=Spawn(game,run.level,arrival);bool all=room!=nullptr && players.size()==expected;
        for(auto player:players){
            if(Dead(player)){all=false;continue;}
            if(Level(player)!=run.level){run.ready.erase(player->dwUnitId);EventTrace(game,run,"warp-begin");Native<void(__fastcall*)(Game*,Unit*,int,int)>(0x2e16d0)(game,player,run.level,11);EventTrace(game,run,"warp-returned");all=false;}
            if(!run.ready.contains(player->dwUnitId))all=false;
        }
        CensusAndBuffs(game,run);
        if(all){run.eventPhase=EventPhase::Active;run.eventStarted=run.eventFrame=frame;run.phase=Phase::Running;run.census.entered=true;EventTrace(game,run,"active");Sync(game,run,true);}
        else if(age()>25*120){run.timedOut=true;run.phase=Phase::Returning;run.eventPhase=EventPhase::Returning;for(const auto& [id,p]:run.returns)run.returning.insert(id);}
        else Sync(game,run);
        return true;
    }
    if(run.eventPhase==EventPhase::Active){
        if(run.event==Event::Survivors){ProcessSurvivors(game,run,players);return false;}
        if(run.event==Event::FountainOfYouth)for(auto player:players)if(Level(player)==run.level && !Dead(player)){
            const int maximum=Stat(player,9);if(maximum>0 && Stat(player,8)<maximum)UnitStat(player,8,maximum);
        }
        if(Info(run.event).seconds && !EventRemaining(frame,run.eventStarted,run.event)){
            // Capture objective deaths on the boundary before deciding Trial
            // success. A kill after the deadline never awards stat points.
            CensusAndBuffs(game,run);run.success=run.census.Complete();run.timedOut=true;
            run.phase=Phase::Returning;run.eventPhase=EventPhase::Returning;run.eventFrame=frame;for(const auto& [id,p]:run.returns)run.returning.insert(id);
        }
        return false;
    }
    if(run.eventPhase==EventPhase::Returning)return false;
    if(run.eventPhase==EventPhase::Finished || run.eventPhase==EventPhase::Cancelled){
        Sync(game,run);if(age()>=25*10)state.run.reset();return true;
    }
    return false;
}
