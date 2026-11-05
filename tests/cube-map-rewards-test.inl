// Include after the driver's Put/Bytes definitions; call TimedRewards().
namespace {
Game* rewardTestGame=nullptr;Unit* rewardTestLocal=nullptr;
std::map<uint32_t,Unit*> rewardTestPlayers;
std::vector<std::pair<uint32_t,RewardsData::Wire>> rewardPackets;
Unit* __fastcall RewardLookup(Game* game,int type,uint32_t guid){assert(game==rewardTestGame && type==0);return rewardTestPlayers.contains(guid) ? rewardTestPlayers.at(guid) : nullptr;}
Unit* __fastcall RewardLocal(uint32_t selector){assert(selector==0);return rewardTestLocal;}
void __fastcall RewardFlush(Client*){}
void __fastcall RewardNetwork(void*,uint32_t client,void** range){
    assert(reinterpret_cast<uintptr_t>(range[1])-reinterpret_cast<uintptr_t>(range[0])==0xce);
    RewardsData::Wire packet;memcpy(&packet,range[0],sizeof(packet));assert(RewardsData::Valid(packet));rewardPackets.emplace_back(client,packet);
}
}
void TimedRewards(){
    using namespace RewardsData;
    static_assert(RewardsData::Operation!=LayoutData::Operation && RewardsData::Operation!=D3Whirlwind::Data::Operation && RewardsData::Operation!=AutomapData::Operation);
    assert(Index(Event::TreasureTrove)==0 && Index(Event::GoldenGoose)==1 && Index(Event::PlentifulPirating)==2 && Index(Event::ChampionClash)<0);
    assert(Remaining(100,100,Event::TreasureTrove)==600 && Remaining(124,100,Event::TreasureTrove)==600 && Remaining(125,100,Event::TreasureTrove)==599);
    assert(Remaining(15099,100,Event::TreasureTrove)==1 && !Remaining(15100,100,Event::TreasureTrove));
    const auto wrapStart=UINT32_MAX-10;assert(Remaining(wrapStart+25u,wrapStart,Event::TreasureTrove)==599 && !Remaining(wrapStart+15000u,wrapStart,Event::TreasureTrove));
    assert(DurationSeconds(Event::PlentifulPirating)==120 && DurationSeconds(Event::TreasureTrove)==600 && DurationSeconds(Event::GoldenGoose)==600);
    assert(Remaining(100,100,Event::PlentifulPirating)==120 && Remaining(125,100,Event::PlentifulPirating)==119);
    assert(Remaining(3099,100,Event::PlentifulPirating)==1 && !Remaining(3100,100,Event::PlentifulPirating));
    assert(Remaining(wrapStart+25u,wrapStart,Event::PlentifulPirating)==119 && !Remaining(wrapStart+3000u,wrapStart,Event::PlentifulPirating));
    // Thousands of successive selections preserve even the last server tick.
    Timer clock(100);uint32_t now=100+14999;
    for(unsigned i=0;i<2048;++i){
        assert(clock.RemainingAt(now,Event::TreasureTrove)==1);
        assert(clock.Pause(now,true) && !clock.Pause(now,true));now+=5000;
        assert(clock.RemainingAt(now,Event::TreasureTrove)==1);
        assert(clock.Pause(now,false) && !clock.Pause(now,false));
    }
    ++now;assert(!clock.RemainingAt(now,Event::TreasureTrove));
    Timer wrapClock(UINT32_MAX-10);assert(wrapClock.Pause(UINT32_MAX-1,true));
    assert(wrapClock.RemainingAt(50,Event::PlentifulPirating)==120);
    assert(wrapClock.Pause(50,false) && wrapClock.RemainingAt(66,Event::PlentifulPirating)==119);
    Wire packet;packet.epoch=2;packet.sequence=1;packet.recipient=100;packet.remaining={600,599,1};assert(Valid(packet));
    for(auto field:{0,1,2,3,4,5}){auto invalid=packet;switch(field){case 0:invalid.opcode=3;break;case 1:invalid.magic=0;break;case 2:invalid.operation=4;break;case 3:invalid.version=2;break;case 4:invalid.epoch=0;break;case 5:invalid.sequence=0;break;}assert(!Valid(invalid));}
    packet.paused=2;assert(!Valid(packet));packet.paused=0;
    packet.remaining[0]=601;assert(!Valid(packet));packet.remaining[0]=600;
    packet.remaining[2]=121;assert(!Valid(packet));packet.remaining[2]=120;assert(Valid(packet));packet.remaining[2]=1;
    View model;assert(model.Accept(packet,1000));assert(!model.Accept(packet,1001));
    assert(model.Lines(1000,100,0,2)==std::vector<std::string>({"Treasure Trove: 10:00","Golden Goose: 9:59","Plentiful Pirating: 0:01"}));
    assert(model.Lines(2000,100,0,2).size()==2 && model.Lines(5000,100,0,2).size()==2 && model.Lines(5001,100,0,2).empty());
    assert(model.Lines(999,100,0,2).empty() && model.Lines(1000,101,0,2).empty() && model.Lines(1000,100,1,2).empty() && model.Lines(1000,100,0,3).empty());
    packet.paused=1;packet.sequence=2;assert(model.Accept(packet,6000));
    const auto frozen=model.Lines(6000,100,0,2);assert(frozen.size()==3 && model.Lines(9000,100,0,2)==frozen);
    packet.paused=0;packet.sequence=3;assert(model.Accept(packet,9000));assert(model.Lines(10000,100,0,2).size()==2);
    packet.sequence=UINT32_MAX;model={};assert(model.Accept(packet,1000));packet.sequence=1;assert(model.Accept(packet,1001));
    packet.epoch=3;packet.sequence=1;assert(model.Accept(packet,1002));
    const auto savedBindings=bindings;const auto savedKnown=clientLayoutSessionKnown;
    const auto savedSession=clientLayoutSession,savedEpoch=clientLayoutEpoch;
    Bytes<0x5900> memory{};auto game=reinterpret_cast<Game*>(memory.data());rewardTestGame=game;
    game->dwInitSeed=1234;game->dwGameFrame=100;
    Unit hero{},other{},avatar{};hero.dwUnitId=100;other.dwUnitId=101;avatar.dwUnitId=200;
    Client client{},second{},joining{};client.pGame=second.pGame=joining.pGame=game;
    client.dwUnitGUID=100;second.dwUnitGUID=101;joining.dwUnitGUID=200;
    client.dwClientId=1;second.dwClientId=2;joining.dwClientId=3;
    client.dwClientState=second.dwClientState=4;joining.dwClientState=1;
    client.pNext=&second;second.pNext=&joining;game->pClientList=&client;
    rewardTestPlayers={{100,&hero},{101,&other},{200,&avatar}};rewardTestLocal=&hero;
    uint32_t selector=0;
    bindings[0x2c49c0]=reinterpret_cast<uintptr_t>(RewardLookup);bindings[0x2d0130]=reinterpret_cast<uintptr_t>(RewardFlush);
    bindings[0x422ac0]=reinterpret_cast<uintptr_t>(RewardNetwork);bindings[0x1d442d8]=reinterpret_cast<uintptr_t>(&selector);bindings[0x6b050]=reinterpret_cast<uintptr_t>(RewardLocal);
    auto& state=State(game);GrantTimedReward(game,100,Event::TreasureTrove);GrantTimedReward(game,100,Event::GoldenGoose);
    assert(!state.run && HasTimedReward(game,&hero,Event::TreasureTrove) && !HasTimedReward(game,&other,Event::TreasureTrove));
    assert(state.timedRewards.at(100).size()==2 && !HasTimedReward(game,&hero,Event::ChampionClash));
    game->dwGameFrame=125;GrantTimedReward(game,100,Event::TreasureTrove);GrantTimedReward(game,100,Event::PlentifulPirating);
    assert(state.timedRewards.at(100).size()==3 && state.timedRewards.at(100).at(Event::TreasureTrove).start==125 && state.timedRewards.at(100).at(Event::GoldenGoose).start==100);
    rewardPackets.clear();ProcessTimedRewards(game,state,true);assert(rewardPackets.size()==2);
    assert((rewardPackets[0].first==1 && rewardPackets[0].second.remaining==std::array<uint16_t,3>({600,599,120})));
    assert((rewardPackets[1].first==2 && rewardPackets[1].second.remaining==std::array<uint16_t,3>{}));
    const auto sent=state.rewardSequence;ProcessTimedRewards(game,state);assert(state.rewardSequence==sent && rewardPackets.size()==2);
    game->dwGameFrame=150;State(game);ProcessTimedRewards(game,state);assert(state.rewardSequence==sent+1 && rewardPackets.size()==4);
    auto context=std::make_unique<SurvivorAvatar>();context->game=game;context->hero=&hero;context->avatar=&avatar;
    survivorAvatars[&client]=std::move(context);client.dwUnitGUID=200;
    assert(HasTimedReward(game,&avatar,Event::TreasureTrove));rewardPackets.clear();ProcessTimedRewards(game,state,true);
    assert(rewardPackets.front().second.recipient==200 && rewardPackets.front().second.remaining[0]>0);
    survivorAvatars.erase(&client);client.dwUnitGUID=100;
    ClearClientRewards();packet=rewardPackets.front().second;packet.recipient=100;
    clientLayoutSessionKnown=true;clientLayoutSession=packet.session;clientLayoutEpoch=packet.epoch;
    auto receive=[&](const Wire& value){return ReceiveRewards(reinterpret_cast<const uint8_t*>(&value),sizeof(value));};
    assert(!ReceiveRewards(nullptr,sizeof(packet)) && !ReceiveRewards(reinterpret_cast<uint8_t*>(&packet),sizeof(packet)-1));
    assert(receive(packet) && !receive(packet) && !TimedRewardLines().empty());
    auto replay=packet;replay.sequence--;assert(!receive(replay));
    auto foreign=packet;foreign.recipient=101;foreign.sequence++;assert(!receive(foreign));
    foreign=packet;foreign.session++;foreign.sequence++;assert(!receive(foreign));
    foreign=packet;foreign.epoch++;foreign.sequence++;assert(!receive(foreign));
    // A newly authenticated epoch rejects old packets despite the identical
    // offline seed/GUID, but accepts its new sequence starting at one.
    clientLayoutEpoch++;assert(!receive(packet) && TimedRewardLines().empty());packet.epoch=clientLayoutEpoch;packet.sequence=1;
    assert(receive(packet));ClearClientRewards();assert(TimedRewardLines().empty());
    game->dwGameFrame=3124;State(game);ProcessTimedRewards(game,state,true);
    assert(HasTimedReward(game,&hero,Event::PlentifulPirating) && rewardPackets[rewardPackets.size()-2].second.remaining[2]==1);
    game->dwGameFrame=3125;State(game);ProcessTimedRewards(game,state,true);
    assert(!HasTimedReward(game,&hero,Event::PlentifulPirating) && !state.timedRewards.at(100).contains(Event::PlentifulPirating));
    assert(HasTimedReward(game,&hero,Event::TreasureTrove) && HasTimedReward(game,&hero,Event::GoldenGoose));
    assert(rewardPackets[rewardPackets.size()-2].second.remaining[2]==0);
    game->dwGameFrame=15100;State(game);ProcessTimedRewards(game,state,true);
    assert(HasTimedReward(game,&hero,Event::TreasureTrove) && !HasTimedReward(game,&hero,Event::GoldenGoose));
    game->dwGameFrame=15125;State(game);rewardPackets.clear();ProcessTimedRewards(game,state,true);
    assert((state.timedRewards.empty() && rewardPackets.front().second.remaining==std::array<uint16_t,3>{}));
    GrantTimedReward(game,100,Event::TreasureTrove);game->dwInitSeed=1235;assert(!HasTimedReward(game,&hero,Event::TreasureTrove));
    assert(State(game).timedRewards.empty());
    // Scope pauses to participating players and resume on completion, failure or
    // run removal. Other players continue spending their own bonus time.
    game->dwGameFrame=20000;GrantTimedReward(game,100,Event::TreasureTrove);GrantTimedReward(game,100,Event::GoldenGoose);GrantTimedReward(game,100,Event::PlentifulPirating);GrantTimedReward(game,101,Event::TreasureTrove);
    auto& pausedState=State(game);pausedState.run=std::make_unique<Run>();auto& run=*pausedState.run;
    run.event=Event::Trial;run.eventPhase=EventPhase::WaitingTown;run.returns[100]={109,0,0};
    game->dwGameFrame+=25;ProcessTimedRewards(game,pausedState,true);assert(!rewardPackets[rewardPackets.size()-2].second.paused);
    run.eventPhase=EventPhase::Ceremony;ProcessTimedRewards(game,pausedState);
    assert(rewardPackets[rewardPackets.size()-2].second.paused && !rewardPackets.back().second.paused);
    const auto remaining=rewardPackets[rewardPackets.size()-2].second.remaining;
    game->dwGameFrame+=10000;run.eventPhase=EventPhase::Briefing;ProcessTimedRewards(game,pausedState,true);
    assert(rewardPackets[rewardPackets.size()-2].second.remaining==remaining && HasTimedReward(game,&hero,Event::PlentifulPirating));
    assert(rewardPackets.back().second.remaining[0]==199);
    run.eventPhase=EventPhase::Entering;ProcessTimedRewards(game,pausedState,true);
    assert(rewardPackets[rewardPackets.size()-2].second.remaining==remaining);
    game->dwGameFrame+=25;run.eventPhase=EventPhase::Active;ProcessTimedRewards(game,pausedState);
    assert(!rewardPackets[rewardPackets.size()-2].second.paused && rewardPackets[rewardPackets.size()-2].second.remaining==remaining);
    game->dwGameFrame+=25;ProcessTimedRewards(game,pausedState,true);
    for(size_t i=0;i<Types.size();++i)assert(rewardPackets[rewardPackets.size()-2].second.remaining[i]+1==remaining[i]);
    for(auto terminal:{EventPhase::Cancelled,EventPhase::Finished}){
        run.eventPhase=EventPhase::Ceremony;ProcessTimedRewards(game,pausedState);game->dwGameFrame+=10000;
        run.eventPhase=terminal;ProcessTimedRewards(game,pausedState);assert(!rewardPackets[rewardPackets.size()-2].second.paused && HasTimedReward(game,&hero,Event::PlentifulPirating));
    }
    // An avatar's participant GUID pauses the hero's independently owned buffs.
    context=std::make_unique<SurvivorAvatar>();context->game=game;context->hero=&hero;context->avatar=&avatar;
    survivorAvatars[&client]=std::move(context);client.dwUnitGUID=200;run.returns.clear();run.returns[200]={109,0,0};run.event=Event::Survivors;run.eventPhase=EventPhase::Briefing;
    ProcessTimedRewards(game,pausedState);assert(rewardPackets[rewardPackets.size()-2].second.paused);
    game->dwGameFrame+=10000;pausedState.run.reset();ProcessTimedRewards(game,pausedState);
    assert(!rewardPackets[rewardPackets.size()-2].second.paused && HasTimedReward(game,&avatar,Event::PlentifulPirating));
    survivorAvatars.erase(&client);client.dwUnitGUID=100;
    games.erase(game);rewardTestPlayers.clear();rewardTestLocal=nullptr;rewardTestGame=nullptr;ClearClientRewards();
    clientLayoutSessionKnown=savedKnown;clientLayoutSession=savedSession;clientLayoutEpoch=savedEpoch;bindings=savedBindings;
    std::cout<<"Timed rewards refresh/coexist, survive map exit/avatar swaps, pause all participant bonuses through 2,048 selections, resume on completion/cancellation, freeze client countdowns, expire/wrap safely and authenticate replay-resistant packets passed.\n";
}
