// Included in CubeMap's server namespace after CubeMapEncounters.inl.
// Population is rebuilt synchronously on the game thread. Only this map's
// per-game region is changed, and its ordinary settings are restored on exit.
struct MapPackPopulationScope {
    void* region=nullptr;
    uint8_t minimum=0,maximum=0;
    bool required=false;
    MapPackPopulationScope(Game* game,const Run& run,bool mapping):required(mapping && run.event==Event::None){
        if(!required)return;
        if(run.level<=0 || run.level>=1024)return;
        region=Field<void*>(game,0x1d8+size_t(run.level)*8);
        if(!region)return;
        minimum=Field<uint8_t>(region,0x2bc);maximum=Field<uint8_t>(region,0x2bd);
        Set(region,0x2bc,uint8_t(0));Set(region,0x2bd,uint8_t(0));
    }
    ~MapPackPopulationScope(){if(region){Set(region,0x2bc,minimum);Set(region,0x2bd,maximum);}}
    MapPackPopulationScope(const MapPackPopulationScope&)=delete;
    MapPackPopulationScope& operator=(const MapPackPopulationScope&)=delete;
    bool Valid()const{return !required || region;}
};
bool MapPackCandidate(int id){
    if(!RandomCandidate(id))return false;
    const auto table=Field<uint8_t*>(Native<void*>(0x1c9f878),0);
    const auto record=table+size_t(id)*0x1dc;
    if(Field<uint32_t>(record,0x3c)&((1u<<4)|(1u<<5)))return false;
    // The native unique generator honors minion1. Do not let a harmless
    // roster entry introduce a quest actor or multipart monster as its minion.
    const int minion=Field<int16_t>(record,0x56);
    return minion<0 || minion==id || RandomCandidate(minion);
}
std::vector<int> MapPackRoster(const Run& run){
    const auto levels=Field<uint8_t*>(Native<void*>(0x1c9fd08),0);
    const auto count=Field<uint32_t>(Native<void*>(0x1c9fd10),0);
    if(!levels || !count || count>1024)return {};
    // UMon is the mod's explicit champion/unique-compatible population list.
    // This also respects custom monster rows without assuming class ID ranges.
    std::set<int> eligible;
    for(size_t level=1;level<count;++level)for(size_t n=0;n<25;++n){
        const int id=Field<int16_t>(levels+level*0x184,0x9e+n*2);
        if(MapPackCandidate(id))eligible.insert(id);
    }
    std::vector<int> result;
    for(int id:run.roster)if(eligible.contains(id))result.push_back(id);
    if(result.empty())result.assign(eligible.begin(),eligible.end());
    return result;
}
std::map<uint32_t,Unit*> MapPackUnits(const Run& run){
    std::map<uint32_t,Unit*> result;
    for(const auto pin:run.pins)if(auto room=Field<Room*>(pin.room,0x58))
        for(auto unit:RoomUnits(room))if(unit->dwUnitType==1)result.emplace(unit->dwUnitId,unit);
    return result;
}
std::optional<size_t> ExistingMapPacks(const Run& run){
    size_t count=0;
    for(const auto& [id,unit]:MapPackUnits(run)){
        if(Dead(unit) || !unit->pMonsterData || !unit->pMonsterData->pMonstatsTxt)continue;
        const auto flags=unit->pMonsterData->nTypeFlag;
        // Scripted act bosses are combat objectives, separate from random
        // champion/unique packs. A superunique and its minions count once.
        if(unit->pMonsterData->pMonstatsTxt->dwMonStatsFlags&((1u<<6)|(1u<<7)))continue;
        if(flags&0x10)continue;
        // Champion siblings have no stable group owner in the native unit.
        // Ordinary population is suppressed, so they can only come from a
        // custom scripted preset we cannot group reliably. Refuse this map
        // rather than silently promise an incorrect total.
        if(flags&4)return std::nullopt;
        if(flags&(2|8))++count;
    }
    return count;
}
bool SpawnMapPack(Game* game,Run& run,Room* room,Position position,int monster){
    if(!room || !MapPackCandidate(monster) || !FreePosition(room,position))return false;
    auto region=Field<void*>(game,0x1d8+size_t(run.level)*8);
    if(!region)return false;
    const auto count=Field<uint32_t>(region,0x2c8);
    const auto before=MapPackUnits(run);
    // Coordinates are explicit. A null room-coordinate list lets native
    // placement search around this point, including its adjacent rooms.
    auto leader=Native<Unit*(__fastcall*)(Game*,Room*,void*,int,int,int,int,int)>(0x2fad40)(game,room,nullptr,position.x,position.y,-1,monster,1);
    if(!leader)return false;
    Native<void(__fastcall*)(Game*,Unit*,int)>(0x2faae0)(game,leader,1);
    Native<void(__fastcall*)(Game*,Room*,void*,Unit*,int,int,int)>(0x2fddd0)(game,room,nullptr,leader,1,3,6);
    // Native champions have 1..3 additional champions; uniques instead got
    // their ordinary 3..6 minions above. Count either group exactly once.
    Native<void(__fastcall*)(Game*,Unit*,void*,int)>(0x2dce30)(game,leader,nullptr,monster);
    const auto after=MapPackUnits(run);
    size_t members=0;
    for(const auto& [id,unit]:after)if(!before.contains(id) && !Dead(unit))++members;
    const auto flags=leader->pMonsterData ? leader->pMonsterData->nTypeFlag : uint8_t(0);
    const size_t minimum=(flags&4) ? 2 : 4;
    if(leader->dwUnitType==1 && (flags&1) && members>=minimum){
        ++run.spawnedPacks;return true;
    }
    // A single leader in an obstructed corner does not satisfy a pack. Roll
    // back the partial group and its native region count before trying again.
    for(const auto& [id,unit]:after)if(!before.contains(id))originalRemove(game,unit);
    Set(region,0x2c8,count);return false;
}
bool PopulateMapPacks(Game* game,Run& run,Position entrance){
    if(run.event!=Event::None)return true;
    if(!run.spawnedPacks){
        const auto existing=ExistingMapPacks(run);if(!existing || *existing>13)return false;
        run.spawnedPacks=uint8_t(*existing);
        if(!run.targetPacks)run.targetPacks=uint8_t(std::uniform_int_distribution<int>((std::max)(8,int(*existing)),13)(run.random));
        else run.targetPacks=uint8_t((std::max)(unsigned(run.targetPacks),unsigned(run.spawnedPacks)));
    }
    if(run.targetPacks<8 || run.targetPacks>13 || run.spawnedPacks>run.targetPacks)return false;
    if(run.spawnedPacks==run.targetPacks)return true;
    const auto roster=MapPackRoster(run);if(roster.empty())return false;
    std::vector<std::pair<Room*,Position>> candidates;
    for(const auto pin:run.pins)if(auto room=Field<Room*>(pin.room,0x58)){
        const int x=Field<int>(room,0x80),y=Field<int>(room,0x84),w=Field<int>(room,0x88),h=Field<int>(room,0x8c);
        if(w<=0 || h<=0 || w>2048 || h>2048)continue;
        for(int dy=5;dy<h-3 && candidates.size()<MaxUnits;dy+=8)for(int dx=5;dx<w-3 && candidates.size()<MaxUnits;dx+=8){
            Position p{run.level,x+dx,y+dy};const int64_t ex=int64_t(p.x)-entrance.x,ey=int64_t(p.y)-entrance.y;
            if(ex*ex+ey*ey>=24*24)candidates.push_back({room,p});
        }
    }
    std::shuffle(candidates.begin(),candidates.end(),run.random);
    std::vector<Position> placed;
    size_t attempts=0;
    for(const auto& [room,p]:candidates){
        bool tooClose=false;for(const auto previous:placed){const int64_t x=int64_t(p.x)-previous.x,y=int64_t(p.y)-previous.y;if(x*x+y*y<18*18){tooClose=true;break;}}
        if(tooClose)continue;
        if(attempts++>=512)break;
        const int monster=roster[std::uniform_int_distribution<size_t>(0,roster.size()-1)(run.random)];
        if(SpawnMapPack(game,run,room,p,monster)){
            placed.push_back(p);if(run.spawnedPacks==run.targetPacks)return true;
        }
    }
    // Preparation must fail before consuming ingredients or opening a portal
    // when this destination cannot accommodate the promised population.
    return false;
}
