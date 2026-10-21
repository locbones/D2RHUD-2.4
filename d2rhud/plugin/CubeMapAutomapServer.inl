// All rooms are pinned/active before this snapshot. Read their tiles without
// setting the native revealed bit or advancing the game's automap RNG.
void BuildEventAutomap(Run& run){
    using namespace AutomapData;
    run.automapCells.clear();run.automapSent.clear();run.automapRevision=0;
    const auto rows=Field<uint8_t*>(Native<void*>(0x1ca0880),0);
    const auto count=Field<uint64_t>(Native<void*>(0x1ca0888),0);
    const auto definitions=Field<uint8_t*>(Native<void*>(0x1c9fd28),0);
    const auto levels=Field<int>(Native<void*>(0x1c9fd10),0);
    if(!rows || !count || count>65536 || !definitions || run.level<=0 || run.level>=levels)return;
    std::vector<std::array<uint8_t,32>> table(size_t(count),std::array<uint8_t,32>{});SIZE_T got=0;
    if(!ReadProcessMemory(GetCurrentProcess(),rows,table.data(),table.size()*32,&got) || got!=table.size()*32)return;
    const int type=Field<int>(definitions+size_t(run.level)*0x9c,0x34);
    const auto integer=[](const uint8_t* p,size_t offset){int value=0;memcpy(&value,p+offset,4);return value;};
    const auto image=[&](int tile,int style,int sequence,int x,int y){
        for(const auto& row:table)if(integer(row.data(),0)==type && integer(row.data(),4)==tile &&
            (row[8]==255 || row[8]==style) && (row[9]==255 || (sequence>=row[9] && sequence<=row[10]))){
            const int choices=std::clamp(integer(row.data(),28),1,4);
            const unsigned selected=(uint32_t(x)*2654435761u^uint32_t(y)*2246822519u)%unsigned(choices);
            return integer(row.data(),12+selected*4);
        }return -1;
    };
    std::set<std::tuple<uint8_t,int,int,uint32_t>> cells;
    for(const auto& pin:run.pins){
        auto room=Field<Room*>(pin.room,0x58);auto tiles=Field<void*>(room,8);if(!tiles)continue;
        const int roomX=Field<int>(pin.room,0x60),roomY=Field<int>(pin.room,0x64);
        for(unsigned group=0;group<2;++group){
            const auto start=Field<uint8_t*>(tiles,group==0?0x20:0);
            const auto n=Field<uint32_t>(tiles,group==0?0x28:8);
            if(n>MaxCells || (n && !start)){run.automapCells.clear();return;}
            for(size_t i=0;i<n;++i){
                std::array<uint8_t,0x48> tile{};if(!Read(reinterpret_cast<uintptr_t>(start+i*0x48),tile))continue;
                if(Field<uint32_t>(tile.data(),0x18)&8)continue;
                auto definition=Field<void*>(tile.data(),0x20);if(!definition)continue;
                const int x=roomX+Field<int>(tile.data(),8),y=roomY+Field<int>(tile.data(),12);
                const int cel=image(Field<int>(definition,0x14),Field<int>(definition,0x18),Field<int>(definition,0x1c),x,y);
                if(cel<0 || cel>32767)continue;
                const int mapX=(x-y)*8,mapY=(x+y)*4+(Field<int>(tile.data(),0x28)>=16?24:0);
                cells.emplace(uint8_t(group),mapY,mapX,uint32_t(cel));
                if(cells.size()>MaxCells){run.automapCells.clear();return;}
            }
        }
    }
    for(const auto& [group,y,x,cel]:cells)run.automapCells.push_back({cel,x,y,group});
}
std::vector<AutomapData::Entry> EventMonsterMarkers(const Run& run){
    using namespace AutomapData;
    std::vector<Entry> result;std::set<uint32_t> seen;
    for(const auto& pin:run.pins)for(auto unit:RoomUnits(Field<Room*>(pin.room,0x58))){
        if(!RunMonster(unit,run) || Dead(unit) || !unit->pDynamicPath || !seen.insert(unit->dwUnitId).second)continue;
        // Match the native unit marker's cached isometric coordinates exactly.
        const int x=Field<int>(unit->pDynamicPath,8)/10+1,y=Field<int>(unit->pDynamicPath,12)/10-3;
        const auto kind=run.event==Event::Survivors && unit->dwUnitId==run.survival.boss ? uint8_t(3) :
            run.event==Event::Survivors && run.survival.elites.contains(unit->dwUnitId) ? uint8_t(1) :
            MonsterKind(unit->pMonsterData->nTypeFlag,(unit->pMonsterData->pMonstatsTxt->dwMonStatsFlags&(1u<<6))!=0,run.event==Event::Trial && run.objectives.contains(unit->dwUnitId));
        result.push_back({unit->dwUnitId,x,y,kind});if(result.size()==MaxMonsters)return result;
    }return result;
}
void SendAutomap(Client* client,const Packet& state,AutomapData::Stream stream,uint32_t revision,const std::vector<AutomapData::Entry>& entries){
    using namespace AutomapData;
    for(size_t offset=0;;offset+=EntriesPerPacket){
        Wire packet;packet.run=state.run;packet.session=state.session;packet.recipient=state.recipient;packet.level=state.level;
        packet.stream=stream;packet.revision=revision;packet.total=uint32_t(entries.size());packet.offset=uint32_t(offset);
        packet.count=uint8_t(std::min<size_t>(EntriesPerPacket,entries.size()-offset));std::copy_n(entries.begin()+offset,packet.count,packet.entries.begin());
        void* range[]{&packet,reinterpret_cast<uint8_t*>(&packet)+sizeof(packet)};
        Native<void(__fastcall*)(void*,uint32_t,void**)>(0x422ac0)(Native<void*>(0x18682b0),client->dwClientId,range);
        if(offset+packet.count==entries.size())break;
    }
}
