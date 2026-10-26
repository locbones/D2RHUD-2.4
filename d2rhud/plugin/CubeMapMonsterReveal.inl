// The host grants a short, area-specific lease. No character stat, option or
// native exploration flag is changed by this temporary monster reveal.
std::mutex monsterRevealMutex;
MonsterRevealData::View monsterRevealView;
void ClearMonsterReveal(){std::lock_guard lock(monsterRevealMutex);monsterRevealView={};}
uint16_t MonsterRevealLease(Game* game,GameState& state,Unit* player,uint32_t& mapRun){
    mapRun=0;const int level=Level(player);
    if(!player || player->dwUnitType!=0 || Dead(player) || level<=0 || level>=1024 || Town(level))return 0;
    uint32_t remaining=0;
    if(const auto participant=state.timedRewards.find(TimedRewardOwner(game,player));participant!=state.timedRewards.end())
        for(const auto [event,start]:participant->second){
            const uint32_t duration=RewardsData::DurationSeconds(event)*25,elapsed=game->dwGameFrame-start;
            if(elapsed<duration)remaining=(std::max)(remaining,(duration-elapsed)*40);
        }
    if(remaining)return uint16_t(std::min<uint32_t>(MonsterRevealData::LeaseMilliseconds,remaining));
    const auto run=state.run.get();
    if(run && run->event==Event::None && run->id && run->level==level && run->phase!=Phase::Preparing && run->phase!=Phase::Closed){
        mapRun=run->id;return MonsterRevealData::LeaseMilliseconds;
    }
    return 0; // Special event arenas already use their scoped automap stream.
}
std::vector<AutomapData::Entry> RevealMonsterMarkers(Game* game,int level,const Run* run){
    std::vector<AutomapData::Entry> result;std::set<uint32_t> seen;
    const auto add=[&](Unit* unit){
        if(result.size()>=AutomapData::MaxMonsters || !Monster(unit,false) || Dead(unit) || !unit->pDynamicPath ||
           Level(unit)!=level || !seen.insert(unit->dwUnitId).second)return;
        result.push_back({unit->dwUnitId,Field<int>(unit->pDynamicPath,8)/10+1,Field<int>(unit->pDynamicPath,12)/10-3,
            AutomapData::MonsterKind(unit->pMonsterData->nTypeFlag,(unit->pMonsterData->pMonstatsTxt->dwMonStatsFlags&(1u<<6))!=0,false)});
    };
    // Include distant map rooms, not just the client's network activation area.
    if(run && run->level==level)for(const auto& pin:run->pins)for(auto unit:RoomUnits(Field<Room*>(pin.room,0x58)))add(unit);
    size_t visited=0;std::set<Unit*> traversed;
    for(auto first:game->pUnitList[1])for(auto unit=first;unit && visited++<MaxUnits;unit=unit->pListNext){
        if(!traversed.insert(unit).second)break;add(unit);
    }
    return result;
}
void ProcessMonsterReveal(Game* game,GameState& state){
    const bool due=game->dwGameFrame-state.lastRevealSent>=5;
    if(due)state.lastRevealSent=game->dwGameFrame;
    std::map<int,std::vector<AutomapData::Entry>> markers;std::set<uint32_t> connected;
    size_t count=0;
    for(auto client=game->pClientList;client && count++<64;client=client->pNext){
        if(client->pGame!=game || client->dwClientState!=4)continue;
        auto player=Lookup(game,0,client->dwUnitGUID);if(!player || player->dwUnitType!=0)continue;
        connected.insert(player->dwUnitId);uint32_t mapRun=0;const auto lease=MonsterRevealLease(game,state,player,mapRun);
        const int level=Level(player);const auto scope=std::pair{lease?level:0,mapRun};
        const auto previous=state.revealSent.find(player->dwUnitId);
        if(!lease && previous==state.revealSent.end())continue;
        if(!due && previous!=state.revealSent.end() && previous->second==scope)continue;
        if(level<=0 || level>=1024){state.revealSent.erase(player->dwUnitId);continue;}
        if(!++state.revealRevision)++state.revealRevision;
        std::vector<AutomapData::Entry> empty;
        if(lease && !markers.contains(level))markers.emplace(level,RevealMonsterMarkers(game,level,state.run.get()));
        const auto& entries=lease?markers.at(level):empty;
        Native<void(__fastcall*)(Client*)>(0x2d0130)(client);
        for(size_t offset=0;;offset+=AutomapData::EntriesPerPacket){
            MonsterRevealData::Wire packet;packet.session=uint32_t(game->dwInitSeed);packet.epoch=state.layoutEpoch;
            packet.recipient=player->dwUnitId;packet.revision=state.revealRevision;packet.run=mapRun;packet.level=uint16_t(level);packet.lease=lease;
            packet.total=uint32_t(entries.size());packet.offset=uint32_t(offset);
            packet.count=uint8_t(std::min<size_t>(AutomapData::EntriesPerPacket,entries.size()-offset));std::copy_n(entries.begin()+offset,packet.count,packet.entries.begin());
            void* range[]{&packet,reinterpret_cast<uint8_t*>(&packet)+sizeof(packet)};
            Native<void(__fastcall*)(void*,uint32_t,void**)>(0x422ac0)(Native<void*>(0x18682b0),client->dwClientId,range);
            if(offset+packet.count==entries.size())break;
        }
        if(lease)state.revealSent[player->dwUnitId]=scope;else state.revealSent.erase(player->dwUnitId);
    }
    std::erase_if(state.revealSent,[&](const auto& entry){return !connected.contains(entry.first);});
}
bool ReceiveMonsterReveal(const uint8_t* bytes,size_t size){
    if(!bytes || size!=sizeof(MonsterRevealData::Wire))return false;
    MonsterRevealData::Wire packet;memcpy(&packet,bytes,sizeof(packet));if(!MonsterRevealData::Valid(packet))return false;
    auto player=LocalPlayer();if(!player || player->dwUnitId!=packet.recipient)return false;
    std::lock_guard layoutLock(layoutMutex);
    if(!clientLayoutSessionKnown || packet.session!=clientLayoutSession || packet.epoch!=clientLayoutEpoch)return false;
    std::lock_guard lock(monsterRevealMutex);return monsterRevealView.Accept(packet,GetTickCount64());
}
std::shared_ptr<const std::vector<AutomapData::Entry>> VisibleMonsterReveal(Unit* player,const Packet& map,uint64_t mapAge){
    if(!player)return {};
    std::lock_guard layoutLock(layoutMutex);if(!clientLayoutSessionKnown)return {};
    std::lock_guard lock(monsterRevealMutex);
    return monsterRevealView.Visible(GetTickCount64(),player->dwUnitId,Level(player),clientLayoutSession,clientLayoutEpoch,map,mapAge)?monsterRevealView.ready:nullptr;
}
