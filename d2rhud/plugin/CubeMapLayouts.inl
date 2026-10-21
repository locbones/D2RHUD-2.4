// The DRLG's act-wide placement seed remains unchanged. Override only the
// requested level's native generation, on both server and client. Native
// outdoor connections/fixed presets still constrain the layouts they support.
using InitLevel=void(__fastcall*)(void*);
using AllocateLevel=void*(__fastcall*)(void*,int);
using FreeAct=void(__fastcall*)(void*);
InitLevel originalInitLevel=nullptr;FreeAct originalFreeAct=nullptr;
AllocateLevel originalAllocateLevel=nullptr;
std::recursive_mutex layoutMutex;
struct LayoutSeed {Game* game=nullptr;uint32_t seed=0;};
std::map<std::pair<void*,int>,LayoutSeed> serverLayoutSeeds;
std::map<int,uint32_t> clientLayoutSeeds;
std::map<std::pair<void*,int>,uint32_t> clientLayoutsApplied;
std::map<void*,std::vector<void*>> retiredLayoutRecords;
struct JungleTemplate {int width=0;std::vector<int> original,rolled;uint32_t seed=0;bool known=false;};
std::map<std::pair<void*,int>,JungleTemplate> jungleTemplates;
uint32_t clientLayoutSession=0;bool clientLayoutSessionKnown=false;
uint32_t clientLayoutEpoch=0,pendingLayoutEpoch=0;
std::set<int> pendingLayoutReset;
thread_local std::map<void*,uint32_t> generationBases;
struct GenerationSeed {
    void* drlg;uint32_t previous;bool outer;
    GenerationSeed(void* root,uint32_t seed):drlg(root),previous(Field<uint32_t>(root,0x860)),outer(!generationBases.contains(root)){
        if(outer)generationBases[root]=previous;Set(root,0x860,seed);
    }
    ~GenerationSeed(){Set(drlg,0x860,previous);if(outer)generationBases.erase(drlg);}
};
uint32_t GenerationFor(void* drlg,int id){
    uint32_t seed=generationBases.contains(drlg) ? generationBases.at(drlg) : Field<uint32_t>(drlg,0x860);
    if(Field<uint32_t>(drlg,0x110)&1){if(auto it=clientLayoutSeeds.find(id);it!=clientLayoutSeeds.end())seed=it->second;}
    else if(auto it=serverLayoutSeeds.find({drlg,id});it!=serverLayoutSeeds.end())seed=it->second.seed;
    return seed;
}
JungleTemplate* JungleFor(void* level){
    const auto drlg=Field<void*>(level,0x1c8);const auto id=Field<int>(level,0x1f8);
    if(id<76 || id>78 || Field<int>(level,0)!=3 || !Field<void*>(level,0x1d8))return nullptr;
    auto key=std::pair{drlg,id};if(auto found=jungleTemplates.find(key);found!=jungleTemplates.end())return &found->second;
    const auto difficulty=Field<uint8_t>(drlg,0x830);if(difficulty>2)return nullptr;
    auto definitions=Field<void*>(Native<void*>(0x1c9fd28),0);if(!definitions || Field<int>(Native<void*>(0x1c9fd10),0)<=76)return nullptr;
    const auto record=static_cast<uint8_t*>(definitions)+76*0x9c;
    const int width=Field<int>(record,0xc+difficulty*4)/32,height=Field<int>(record,0x18+difficulty*4)/32;
    if(width!=2 || height<4 || height>16)return nullptr;
    JungleTemplate data;data.width=width;auto native=Field<void*>(level,0x1d8);
    for(int n=0;n<width*height;++n)data.original.push_back(Field<int>(native,n*4));
    return &jungleTemplates.emplace(key,std::move(data)).first->second;
}
const std::vector<int>& RollJungle(JungleTemplate& data,int id,uint32_t seed){
    if(!data.known || data.seed!=seed){data.rolled=JungleData::Roll(data.original,data.width,id,seed);data.seed=seed;data.known=true;}
    return data.rolled;
}
void ApplyJungleLayout(void* level,uint32_t seed){
    if(auto data=JungleFor(level)){
        const auto drlg=Field<void*>(level,0x1c8);const auto id=Field<int>(level,0x1f8);
        const auto actSeed=generationBases.contains(drlg)?generationBases.at(drlg):Field<uint32_t>(drlg,0x860);
        const auto& cells=seed==actSeed?data->original:RollJungle(*data,id,seed);
        auto native=Field<void*>(level,0x1d8);for(size_t n=0;n<cells.size();++n)Set(native,n*4,cells[n]);
    }
}
void TraceLayout(void* level,uint32_t seed){
    std::vector<std::array<int,4>> rectangles;size_t n=0;
    for(auto room=Field<void*>(level,0x10);room && n++<MaxRooms;room=Field<void*>(room,0x48))
        rectangles.push_back({Field<int>(room,0x60),Field<int>(room,0x64),Field<int>(room,0x68),Field<int>(room,0x6c)});
    std::sort(rectangles.begin(),rectangles.end());uint32_t hash=2166136261u;
    for(const auto& rectangle:rectangles)for(auto value:rectangle)hash=(hash^uint32_t(value))*16777619u;
    auto drlg=Field<void*>(level,0x1c8);uint32_t terrain=0;
    if(auto data=JungleFor(level)){terrain=2166136261u;auto native=Field<void*>(level,0x1d8);for(size_t n=0;n<data->original.size();++n)terrain=(terrain^Field<uint32_t>(native,n*4))*16777619u;}
    char stage[180];
    sprintf_s(stage,"layout-%s level=%d seed=%08X rooms=%zu geometry=%08X jungle=%08X",(Field<uint32_t>(drlg,0x110)&1)?"client":"server",Field<int>(level,0x1f8),seed,rectangles.size(),hash,terrain);
    CrashDiagnostics::Event(stage,0,0,0);
}
void __fastcall InitLevelHook(void* level){
    const auto drlg=Field<void*>(level,0x1c8);const int id=Field<int>(level,0x1f8);
    std::lock_guard lock(layoutMutex);const auto seed=GenerationFor(drlg,id);
    if(serverLayoutSeeds.contains({drlg,id}) || ((Field<uint32_t>(drlg,0x110)&1) && clientLayoutSeeds.contains(id)))ApplyJungleLayout(level,seed);
    GenerationSeed scoped(drlg,seed);originalInitLevel(level);
    if(serverLayoutSeeds.contains({drlg,id}) || ((Field<uint32_t>(drlg,0x110)&1) && clientLayoutSeeds.contains(id)))TraceLayout(level,seed);
}
void* __fastcall AllocateLevelHook(void* drlg,int id){
    std::lock_guard lock(layoutMutex);GenerationSeed scoped(drlg,GenerationFor(drlg,id));return originalAllocateLevel(drlg,id);
}
void __fastcall FreeActHook(void* act){
    const auto drlg=Field<void*>(act,0x70);
    {std::lock_guard lock(layoutMutex);
        std::erase_if(serverLayoutSeeds,[&](const auto& entry){return entry.first.first==drlg;});
        std::erase_if(clientLayoutsApplied,[&](const auto& entry){return entry.first.first==drlg;});
        std::erase_if(jungleTemplates,[&](const auto& entry){return entry.first.first==drlg;});
        if(auto found=retiredLayoutRecords.find(drlg);found!=retiredLayoutRecords.end()){
            for(auto level:found->second){Set(level,0x1b8,Field<void*>(drlg,0x868));Set(drlg,0x868,level);}
            retiredLayoutRecords.erase(found);
        }
    }
    originalFreeAct(act);
}
void ClearClientLayouts(){std::lock_guard lock(layoutMutex);clientLayoutSeeds.clear();clientLayoutsApplied.clear();clientLayoutSessionKnown=false;clientLayoutEpoch=pendingLayoutEpoch=0;pendingLayoutReset.clear();}
void* FindLayoutLevel(void* drlg,int id){
    size_t count=0;for(auto level=Field<void*>(drlg,0x868);level && count++<16384;level=Field<void*>(level,0x1b8))if(Field<int>(level,0x1f8)==id)return level;
    return nullptr;
}
bool LayoutRoomReferenced(void* room){
    for(size_t n=0;n<4;++n)if(Field<uint16_t>(room,0x28+n*2))return true;
    return false;
}
void FreeLayoutAllocation(void* allocation){
    if(!allocation)return;
    Native<void(__fastcall*)()>(0x6cd130)();
    auto allocator=Native<void*>(0x1b6e318);auto table=Field<void**>(allocator,0);
    reinterpret_cast<void(__fastcall*)(void*,void*)>(table[4])(allocator,allocation);
}
bool DetachLayoutGraph(void* drlg,void* previous){
    std::set<void*> retiring;std::vector<void*> cached;
    for(auto room=Field<void*>(previous,0x10);room;room=Field<void*>(room,0x48)){
        if(!retiring.insert(room).second || retiring.size()>MaxRooms || LayoutRoomReferenced(room))return false;
    }
    if(retiring.empty())return true;
    // Neighbor lists are cached across level boundaries. Native FreeLevel
    // frees only the retiring level's own lists, leaving those caches stale.
    std::set<void*> levels;
    for(auto level=Field<void*>(drlg,0x868);level;level=Field<void*>(level,0x1b8)){
        if(!levels.insert(level).second || levels.size()>16384)return false;
        std::set<void*> rooms;
        for(auto room=Field<void*>(level,0x10);room;room=Field<void*>(room,0x48)){
            if(!rooms.insert(room).second || rooms.size()>MaxRooms)return false;
            if(retiring.contains(room))continue;
            bool references=false;const auto count=Field<uint64_t>(room,0x18);auto adjacent=Field<void**>(room,0x10);
            if(count>MaxRooms || (count && !adjacent))return false;
            for(size_t n=0;n<count;++n)if(retiring.contains(Field<void*>(adjacent,n*sizeof(void*))))references=true;
            std::set<void*> tiles;
            for(auto tile=Field<void*>(room,0x78);tile;tile=Field<void*>(tile,8)){
                if(!tiles.insert(tile).second || tiles.size()>MaxRooms)return false;
                if(retiring.contains(Field<void*>(tile,0)))references=true;
            }
            if(references)cached.push_back(room);
        }
    }
    for(auto room:cached){
        for(auto tile=Field<void*>(room,0x78);tile;){auto next=Field<void*>(tile,8);FreeLayoutAllocation(tile);tile=next;}
        Set(room,0x78,static_cast<void*>(nullptr));
        // Preserve the native vector allocation/capacity; its builder refills
        // an empty vector using the new room graph on the next sight update.
        Set(room,0x18,uint64_t(0));
    }
    if(retiring.contains(Field<void*>(drlg,0x848)))Set(drlg,0x848,static_cast<void*>(nullptr));
    for(auto room:retiring)Native<void(__fastcall*)(void*,uint8_t)>(0x241fa0)(room,4);
    return true;
}
void RebindLayoutLinks(void* drlg,void* previous,void* fresh){
    const auto rebind=[&](void* first){
        size_t n=0;for(auto node=first;node && n++<MaxRooms;node=Field<void*>(node,0x20))if(Field<void*>(node,0)==previous){
            Set(node,0,fresh);
            if(Field<void*>(node,0x18)==static_cast<uint8_t*>(previous)+0x24)Set(node,0x18,static_cast<uint8_t*>(fresh)+0x24);
        }
    };
    size_t n=0;for(auto level=Field<void*>(drlg,0x868);level && n++<16384;level=Field<void*>(level,0x1b8)){
        if(Field<int>(level,0)==3)rebind(Field<void*>(Field<void*>(level,0x18),0x338));
        size_t m=0;for(auto room=Field<void*>(level,0x10);room && m++<MaxRooms;room=Field<void*>(room,0x48))rebind(Field<void*>(room,0));
    }
}
// Native cached level records can be referenced outside the room graph. Keep
// the old, empty record until native act teardown. Detach it from the lookup
// list; reattach only for the native destructor. Geometry/data are freed now.
void* FreshLayoutLevel(void* drlg,int id,uint32_t seed,void* previous){
    if(previous && !DetachLayoutGraph(drlg,previous))return nullptr;
    GenerationSeed scoped(drlg,seed);
    auto fresh=Native<AllocateLevel>(0x1e7f70)(drlg,id);
    if(!fresh)return nullptr;
    if(previous){
        if(Field<int>(previous,0)==3){
            // Outdoor allocation does not run act placement. The act already
            // chose this rectangle, border links and Act III jungle template.
            // Transfer their ownership before native teardown frees them.
            memcpy(static_cast<uint8_t*>(fresh)+0x24,static_cast<uint8_t*>(previous)+0x24,0x10);
            Set(fresh,4,Field<uint32_t>(previous,4));
            Set(fresh,0x1d0,Field<int>(previous,0x1d0));
            Set(fresh,0x1d8,Field<void*>(previous,0x1d8));Set(previous,0x1d8,static_cast<void*>(nullptr));
            auto oldInfo=Field<void*>(previous,0x18),newInfo=Field<void*>(fresh,0x18);
            Set(newInfo,0,Field<uint32_t>(oldInfo,0)&~0x60u); // Native outdoor reset clears generated flags.
            Set(newInfo,0x338,Field<void*>(oldInfo,0x338));Set(oldInfo,0x338,static_cast<void*>(nullptr));
            // The original act template must be captured before applying a map
            // seed. Native InitLevel only varies preset files, not this route.
            ApplyJungleLayout(fresh,seed);
        }
        RebindLayoutLinks(drlg,previous,fresh);
        Native<void(__fastcall*)(void*,int)>(0x1e7860)(previous,0);
        // Preset teardown dereferences its type-specific record even when
        // already freed. An unlinked tombstone must skip that second teardown.
        Set(previous,0,int(0));
    }
    if(fresh && previous){
        void* parent=drlg;size_t offset=0x868,count=0;
        for(auto node=Field<void*>(parent,offset);node && count++<16384;node=Field<void*>(parent,offset)){
            if(node==previous){Set(parent,offset,Field<void*>(node,0x1b8));Set(node,0x1b8,static_cast<void*>(nullptr));retiredLayoutRecords[drlg].push_back(node);break;}
            parent=node;offset=0x1b8;
        }
    }
    return fresh;
}
bool ClientLayoutReady(const LayoutData::Wire& packet){
    auto player=LocalPlayer();if(!player || player->dwUnitId!=packet.recipient)return false;
    if(Level(player)==packet.level)return false;
    const auto act=player->pDrlgAct;
    if(!act || Field<uint8_t>(act,0x20)!=Act(packet.level))return true; // The next act load uses InitLevelHook.
    const auto drlg=Field<void*>(act,0x70);if(!drlg)return false;
    const auto key=std::pair{drlg,int(packet.level)};
    if(const auto applied=clientLayoutsApplied.find(key);applied!=clientLayoutsApplied.end() && applied->second==packet.seed)return true;
    auto old=FindLayoutLevel(drlg,packet.level);
    std::vector<Room*> rooms;
    size_t count=0;for(auto r2=Field<void*>(old,0x10);r2 && count++<MaxRooms;r2=Field<void*>(r2,0x48)){
        if(LayoutRoomReferenced(r2))return false;
        auto room=Field<Room*>(r2,0x58);if(!room)continue;
        // Client actors must finish their ordinary out-of-view teardown.
        if(!RoomUnits(room).empty() || Field<uint64_t>(room,0x70))return false;
        rooms.push_back(room);
    }
    if(count>=MaxRooms)return false;
    for(auto room:rooms)Native<void(__fastcall*)(void*,Room*)>(0x1d1420)(act,room);
    if(old && !FreshLayoutLevel(drlg,packet.level,packet.seed,old))return false;
    clientLayoutsApplied[key]=packet.seed;return true;
}
bool ReceiveLayout(const LayoutData::Wire& packet){
    if(packet.opcode!=0x2b || !LayoutData::Valid(packet) || !installed.load())return false;
    std::lock_guard lock(layoutMutex);
    if(!packet.level){
        if(!clientLayoutSessionKnown || packet.epoch!=clientLayoutEpoch || packet.session!=clientLayoutSession){
            if(pendingLayoutEpoch!=packet.epoch){
                pendingLayoutReset.clear();for(const auto& [level,seed]:clientLayoutSeeds)pendingLayoutReset.insert(level);
                clientLayoutSeeds.clear();clientLayoutsApplied.clear();pendingLayoutEpoch=packet.epoch;
            }
            auto player=LocalPlayer();if(!player || player->dwUnitId!=packet.recipient)return true;
            const auto drlg=Field<void*>(player->pDrlgAct,0x70);
            for(auto it=pendingLayoutReset.begin();it!=pendingLayoutReset.end();){
                LayoutData::Wire ordinary=packet;ordinary.level=uint16_t(*it);ordinary.seed=Field<uint32_t>(drlg,0x860);
                if(!ClientLayoutReady(ordinary))return true;
                it=pendingLayoutReset.erase(it);
            }
            clientLayoutSession=packet.session;clientLayoutEpoch=packet.epoch;clientLayoutSessionKnown=true;pendingLayoutEpoch=0;
        }
        LayoutData::Ack ack;ack.session=packet.session;ack.epoch=packet.epoch;
        Native<void(__fastcall*)(void*,int)>(0x10dce0)(&ack,sizeof(ack));return true;
    }
    if(!clientLayoutSessionKnown || packet.session!=clientLayoutSession || packet.epoch!=clientLayoutEpoch)return true;
    clientLayoutSeeds[packet.level]=packet.seed;
    if(!ClientLayoutReady(packet))return true;
    LayoutData::Ack ack;ack.session=packet.session;ack.epoch=packet.epoch;ack.level=packet.level;ack.seed=packet.seed;
    Native<void(__fastcall*)(void*,int)>(0x10dce0)(&ack,sizeof(ack));return true;
}
void SendLayouts(Game* game,GameState& state,bool force=false){
    if(!force && game->dwGameFrame-state.lastLayoutSent<25)return;
    state.lastLayoutSent=game->dwGameFrame;
    size_t count=0;for(auto client=game->pClientList;client && count++<64;client=client->pNext)if(client->pGame==game && client->dwClientState==4){
        if(!state.layoutResets.contains({client->dwClientId,client->dwUnitGUID})){
            LayoutData::Wire packet;packet.session=uint32_t(game->dwInitSeed);packet.epoch=state.layoutEpoch;packet.recipient=client->dwUnitGUID;
            Native<void(__fastcall*)(Client*)>(0x2d0130)(client);void* range[]{&packet,reinterpret_cast<uint8_t*>(&packet)+sizeof(packet)};
            Native<void(__fastcall*)(void*,uint32_t,void**)>(0x422ac0)(Native<void*>(0x18682b0),client->dwClientId,range);continue;
        }
        for(const auto [level,seed]:state.layoutSeeds){
            if(state.layoutAcks[{client->dwClientId,client->dwUnitGUID,level}]==seed)continue;
            LayoutData::Wire packet;packet.session=uint32_t(game->dwInitSeed);packet.epoch=state.layoutEpoch;packet.recipient=client->dwUnitGUID;packet.level=uint16_t(level);packet.seed=seed;
            Native<void(__fastcall*)(Client*)>(0x2d0130)(client);
            void* range[]{&packet,reinterpret_cast<uint8_t*>(&packet)+sizeof(packet)};
            Native<void(__fastcall*)(void*,uint32_t,void**)>(0x422ac0)(Native<void*>(0x18682b0),client->dwClientId,range);
        }
    }
}
bool AcknowledgeLayout(Game* game,Unit* player,const LayoutData::Ack& packet){
    if(packet.opcode!=3 || !LayoutData::Valid(packet) || !installed.load() || !game || !player || player->dwUnitType!=0)return true;
    if(packet.session!=uint32_t(game->dwInitSeed))return true;
    const auto client=Field<Client*>(player->pPlayerData,0xf0);
    if(!client || client->pGame!=game || client->dwUnitGUID!=player->dwUnitId)return true;
    std::lock_guard lock(serverMutex);auto& state=State(game);const auto found=state.layoutSeeds.find(packet.level);
    if(packet.epoch!=state.layoutEpoch)return true;
    if(!packet.level){state.layoutResets.insert({client->dwClientId,player->dwUnitId});return true;}
    if(!state.layoutResets.contains({client->dwClientId,player->dwUnitId}))return true;
    if(found!=state.layoutSeeds.end() && found->second==packet.seed)state.layoutAcks[{client->dwClientId,player->dwUnitId,int(packet.level)}]=packet.seed;
    return true;
}
bool RerollLayout(Game* game,Run& run,Unit* creator){
    const auto act=LoadAct(game,run.level);if(!act)return false;
    const auto drlg=Field<void*>(act,0x70);if(!drlg)return false;
    // Do not spawn/populate an unused destination just to discard it. Outdoor
    // act placement may already own a level record without any generated rooms.
    auto level=FindLayoutLevel(drlg,run.level);std::vector<Pin> pins;
    std::set<void*> seen;
    auto release=[&](){for(const auto pin:pins)if(!pin.original)Set(pin.room,0x50,Field<uint32_t>(pin.room,0x50)&~Pinned);};
    for(auto r2=Field<void*>(level,0x10);r2;r2=Field<void*>(r2,0x48)){
        if(!seen.insert(r2).second || seen.size()>MaxRooms){release();return false;}
        if(LayoutRoomReferenced(r2)){release();return false;}
        const auto flags=Field<uint32_t>(r2,0x50);pins.push_back({r2,(flags&Pinned)!=0});Set(r2,0x50,flags|Pinned);
        auto room=Activate(r2);if(!room || Occupied(room)){release();return false;}
    }
    size_t clients=0;for(auto client=game->pClientList;client && clients++<64;client=client->pNext)if(client->pGame==game){
        auto cached=Field<Room*>(client,0x240);
        if(cached && seen.contains(Field<void*>(cached,0x18))){release();return false;}
    }
    // Restore only saved inactive units. 27d2d0 is the full room population
    // handler: it creates preset gold/objects as well as ordinary monsters.
    for(const auto pin:pins)Native<void(__fastcall*)(Game*,Room*)>(0x2ed040)(game,Field<Room*>(pin.room,0x58));
    for(const auto pin:pins)if(Occupied(Field<Room*>(pin.room,0x58))){release();return false;}
    auto town=GetRoom(creator);Position loot=Location(creator);
    if(!town || !FreePosition(town,loot)){release();return false;}
    run.populationChanged=true;
    for(const auto pin:pins){
        auto room=Field<Room*>(pin.room,0x58);
        for(auto unit:RoomUnits(room)){
            if(unit->dwUnitType==4){
                // The second argument is the IN/OUT placement coordinate.
                // The third is an optional pathfinding origin, not an output.
                // No path is needed when relocating loot between levels.
                struct Coord{int x,y;} drop{loot.x,loot.y};
                auto dropRoom=Native<Room*(__fastcall*)(Room*,Coord*,Coord*,int,int,int,int,int,int)>(0x231510)(town,&drop,nullptr,1,0x3e01,0x801,1,50,1);
                if(!dropRoom){dropRoom=town;drop={loot.x,loot.y};}
                // Keep existing ground items alive across changed geometry.
                // Publish their departure before the normal native ground drop.
                Native<void(__fastcall*)(Room*,int,uint32_t)>(0x1d1280)(room,4,unit->dwUnitId);
                Native<void(__fastcall*)(Unit*)>(0x22e7f0)(unit);
                Native<void(__fastcall*)(Game*,Unit*,Unit*,Room*,int,int)>(0x2fa090)(game,nullptr,unit,dropRoom,drop.x,drop.y);
            }else originalRemove(game,unit);
        }
        Native<void(__fastcall*)(void*,Room*)>(0x1d1420)(act,room);
    }
    release();
    auto& state=State(game);uint32_t seed=0;
    std::lock_guard lock(layoutMutex);
    auto jungle=JungleFor(level);std::vector<int> previousJungle;
    if(jungle){auto native=Field<void*>(level,0x1d8);for(size_t n=0;n<jungle->original.size();++n)previousJungle.push_back(Field<int>(native,n*4));}
    for(int attempt=0;attempt<32;++attempt){
        do{seed=run.random();}while(!seed || seed==state.layoutSeeds[run.level] || seed==Field<uint32_t>(drlg,0x860));
        if(!jungle || RollJungle(*jungle,run.level,seed)!=previousJungle)break;
    }
    serverLayoutSeeds[{drlg,run.level}]={game,seed};
    if(!FreshLayoutLevel(drlg,run.level,seed,level))return false;
    state.layoutSeeds[run.level]=seed;run.layoutSeed=seed;return true;
}
