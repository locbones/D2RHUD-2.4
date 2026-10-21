// Included in CubeMap's server namespace after native room helpers.
bool EncounterCandidate(int id,bool explicitObjective=false){
    const auto count=Field<uint64_t>(Native<void*>(0x1c9f880),0);const auto table=Field<uint8_t*>(Native<void*>(0x1c9f878),0);
    if(id<0 || uint64_t(id)>=count || !table)return false;
    const auto record=table+size_t(id)*0x1dc;const auto flags=Field<uint32_t>(record,0x3c);
    return (flags&((1u<<25)|(1u<<15)))==((1u<<25)|(1u<<15)) && !(flags&0x700) && (explicitObjective || !(flags&(1u<<18))) && record[0x80]==0;
}
bool BuildEncounters(Run& run){
    run.encounters.clear();run.roster.clear();
    // Normal events use the native population path, including its ordinary
    // packs/champion rolls, with the same safe randomized pool as cube maps.
    // Never resolve or spawn a superunique definition for these events.
    if(RegularEvent(run.event) || run.event==Event::Survivors || run.event==Event::ChampionClash || run.event==Event::SoulSurprise)return BuildRoster(run);
    if(run.event==Event::GoblinGrotto){
        for(const auto name:{"goblin_gem","goblin_gold","goblin_box","goblin_runes","goblin_rmd","goblin_essence"}){
            const int id=LookupName(0x1c9f890,name);if(EncounterCandidate(id,true))run.encounters.push_back({id,-1});
        }
        return !run.encounters.empty();
    }
    if(run.event!=Event::Trial)return false;
    std::set<int> bosses;
    for(const auto name:{"andariel","duriel","mephisto","diablo","baalcrab","madking","moophista"}){
        const int id=LookupName(0x1c9f890,name);if(EncounterCandidate(id))bosses.insert(id);
    }
    // These are the named presets found in RMD's icyhell01..50.ds1 maps,
    // resolved against loaded tables rather than hard-coded numeric class IDs.
    for(const auto name:{"enderaku","majiko","rakuranth","grizzlepaw","kelthir","forsaken_izual","deddomer","gurka","queen_syndred","anuburak"}){
        const int id=LookupName(0x1c9f890,name);if(EncounterCandidate(id))bosses.insert(id);
    }
    const auto table=Field<uint8_t*>(Native<void*>(0x1c9f998),0);const auto count=Field<uint64_t>(Native<void*>(0x1c9f9a0),0);
    if(!table || count>512)return false;
    std::map<int,int> named;
    for(size_t n=0;n<count;++n){const int id=Field<int16_t>(table+n*0x30,4);if(bosses.contains(id))named.try_emplace(id,int(n));}
    for(int id:bosses)run.encounters.push_back({id,named.contains(id) ? named.at(id) : -1});
    if(run.encounters.size()<5)return false;
    std::shuffle(run.encounters.begin(),run.encounters.end(),run.random);run.encounters.resize(5);return true;
}
Unit* SpawnEncounter(Game* game,Run& run,Room* room,Position position,Run::Encounter encounter){
    if(!room || !FreePosition(room,position))return nullptr;
    EventTrace(game,run,"spawn-begin",encounter.monster,encounter.unique);
    if(encounter.unique<0){auto unit=Native<Unit*(__fastcall*)(Game*,Room*,int,int,int,int,int)>(0x2c5760)(game,room,1,encounter.monster,position.x,position.y,1);EventTrace(game,run,unit ? "spawn-complete" : "spawn-no-space",encounter.monster,encounter.unique);return unit;}
    // A private encounter must not consume a quest superunique's one-per-game
    // spawn bit. Save and restore its exact byte around the native spawn call.
    const size_t offset=0x5670+size_t(encounter.unique)/8;const auto saved=Field<uint8_t>(game,offset);
    Set(game,offset,uint8_t(saved&~(1u<<(encounter.unique%8))));
    auto unit=Native<Unit*(__fastcall*)(Game*,Room*,int,int,int)>(0x301a00)(game,room,position.x,position.y,encounter.unique);
    Set(game,offset,saved);EventTrace(game,run,unit ? "spawn-complete" : "spawn-no-space",encounter.monster,encounter.unique);return unit;
}
bool SpawnEncounters(Game* game,Run& run,Position entrance){
    if(run.encounters.empty() || (run.event!=Event::Trial && run.event!=Event::GoblinGrotto))return false;
    std::vector<std::pair<Room*,Position>> positions;
    for(const auto pin:run.pins){
        auto room=Field<Room*>(pin.room,0x58);if(!room)continue;
        const int x=Field<int>(room,0x80),y=Field<int>(room,0x84),w=Field<int>(room,0x88),h=Field<int>(room,0x8c);
        for(int dy=6;dy<h-3;dy+=14)for(int dx=6;dx<w-3;dx+=14){
            Position p{run.level,x+dx,y+dy};const int64_t ex=p.x-entrance.x,ey=p.y-entrance.y;
            if(ex*ex+ey*ey<18*18)continue;positions.push_back({room,p});
        }
    }
    std::shuffle(positions.begin(),positions.end(),run.random);
    if(run.event==Event::Trial)std::stable_sort(positions.begin(),positions.end(),[&](const auto& a,const auto& b){
        auto distance=[&](const auto& p){const int64_t x=p.x-entrance.x,y=p.y-entrance.y;return x*x+y*y;};return distance(a.second)<distance(b.second);
    });
    const size_t wanted=run.event==Event::Trial ? 5 : run.event==Event::GoblinGrotto ? 120 : 100;
    size_t spawned=0;for(const auto& [room,p]:positions){
        const auto encounter=run.encounters[run.event==Event::Trial ? spawned : std::uniform_int_distribution<size_t>(0,run.encounters.size()-1)(run.random)];
        if(auto unit=SpawnEncounter(game,run,room,p,encounter)){
            run.objectives.insert(unit->dwUnitId);EnsureMonsterBuff(unit,run);++spawned;
            if(spawned>=wanted)break;
        }
    }
    return run.event==Event::Trial ? spawned==5 : spawned>0;
}
