// Included after CubeMapSurvivors.inl, before CubeMapEvents.inl.
std::mutex timedRewardViewMutex;
RewardsData::View timedRewardView;
uint32_t TimedRewardOwner(Game* game,Unit* player){
    if(auto avatar=AvatarFor(game,player);avatar && avatar->hero)return avatar->hero->dwUnitId;
    return player->dwUnitId;
}
bool HasTimedReward(Game* game,Unit* player,Event event){
    if(!game || !player || player->dwUnitType!=0 || RewardsData::Index(event)<0)return false;
    std::lock_guard lock(serverMutex);const auto found=games.find(game);
    if(found==games.end() || found->second.seed!=game->dwInitSeed || found->second.frame>game->dwGameFrame)return false;
    const auto participant=found->second.timedRewards.find(TimedRewardOwner(game,player));
    if(participant==found->second.timedRewards.end())return false;
    const auto reward=participant->second.find(event);
    return reward!=participant->second.end() && RewardsData::Remaining(game->dwGameFrame,reward->second)!=0;
}
void GrantTimedReward(Game* game,uint32_t guid,Event event){
    if(!game || RewardsData::Index(event)<0)return;
    std::lock_guard lock(serverMutex);auto& state=State(game);
    // Assignment refreshes one type while leaving every other reward intact.
    state.timedRewards[guid][event]=game->dwGameFrame;
}
void ProcessTimedRewards(Game* game,GameState& state,bool force=false){
    if(!game)return;
    std::lock_guard lock(serverMutex);const auto found=games.find(game);
    if(found==games.end() || &found->second!=&state || state.seed!=game->dwInitSeed)return;
    for(auto player=state.timedRewards.begin();player!=state.timedRewards.end();){
        for(auto reward=player->second.begin();reward!=player->second.end();){
            if(RewardsData::Index(reward->first)<0 || !RewardsData::Remaining(game->dwGameFrame,reward->second))reward=player->second.erase(reward);
            else ++reward;
        }
        if(player->second.empty())player=state.timedRewards.erase(player);else ++player;
    }
    if(!force && game->dwGameFrame-state.lastRewardSent<RewardsData::FramesPerSecond)return;
    state.lastRewardSent=game->dwGameFrame;if(!++state.rewardSequence)++state.rewardSequence;
    size_t count=0;
    for(auto client=game->pClientList;client && count++<64;client=client->pNext){
        if(client->pGame!=game || client->dwClientState!=4)continue;
        auto player=Lookup(game,0,client->dwUnitGUID);if(!player || player->dwUnitType!=0)continue;
        RewardsData::Wire packet;packet.session=uint32_t(game->dwInitSeed);packet.epoch=state.layoutEpoch;
        packet.recipient=client->dwUnitGUID;packet.sequence=state.rewardSequence;
        if(const auto participant=state.timedRewards.find(TimedRewardOwner(game,player));participant!=state.timedRewards.end())
            for(const auto [event,start]:participant->second)if(const int index=RewardsData::Index(event);index>=0)
                packet.remaining[size_t(index)]=RewardsData::Remaining(game->dwGameFrame,start);
        Native<void(__fastcall*)(Client*)>(0x2d0130)(client);
        void* range[]{&packet,reinterpret_cast<uint8_t*>(&packet)+sizeof(packet)};
        Native<void(__fastcall*)(void*,uint32_t,void**)>(0x422ac0)(Native<void*>(0x18682b0),client->dwClientId,range);
    }
}
bool ReceiveRewards(const uint8_t* bytes,size_t size){
    if(!bytes || size!=sizeof(RewardsData::Wire))return false;
    RewardsData::Wire packet;memcpy(&packet,bytes,sizeof(packet));if(!RewardsData::Valid(packet))return false;
    auto player=LocalPlayer();if(!player || player->dwUnitId!=packet.recipient)return false;
    std::lock_guard layoutLock(layoutMutex);
    if(!clientLayoutSessionKnown || packet.session!=clientLayoutSession || packet.epoch!=clientLayoutEpoch)return false;
    std::lock_guard viewLock(timedRewardViewMutex);return timedRewardView.Accept(packet,GetTickCount64());
}
void ClearClientRewards(){std::lock_guard lock(timedRewardViewMutex);timedRewardView={};}
std::vector<std::string> TimedRewardLines(){
    auto player=LocalPlayer();if(!player)return {};
    std::lock_guard layoutLock(layoutMutex);if(!clientLayoutSessionKnown)return {};
    std::lock_guard viewLock(timedRewardViewMutex);
    return timedRewardView.Lines(GetTickCount64(),player->dwUnitId,clientLayoutSession,clientLayoutEpoch);
}
