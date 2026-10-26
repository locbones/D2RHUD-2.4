#pragma once
#include "CubeMapAutomapData.h"

namespace CubeMap::MonsterRevealData {
constexpr uint8_t Operation=8;
constexpr uint16_t LeaseMilliseconds=4000;
#pragma pack(push,1)
struct Wire {
    uint8_t opcode=0x2b;uint64_t magic=Data::PacketMagic;uint8_t operation=Operation,version=1;
    uint32_t session=0,epoch=0,recipient=0,revision=0,run=0;
    uint16_t level=0,lease=0;
    uint32_t offset=0,total=0;uint8_t count=0;
    std::array<AutomapData::Entry,AutomapData::EntriesPerPacket> entries{};
    std::array<uint8_t,6> padding{};
};
#pragma pack(pop)
static_assert(sizeof(Wire)==sizeof(Data::Packet));
inline bool Valid(const Wire& w){
    if(w.opcode!=0x2b || w.magic!=Data::PacketMagic || w.operation!=Operation || w.version!=1 || !w.epoch || !w.revision ||
       !w.level || w.level>=1024 || w.lease>LeaseMilliseconds || (!w.lease && w.total) || w.total>AutomapData::MaxMonsters ||
       w.offset>w.total || w.offset%AutomapData::EntriesPerPacket || (w.total && !w.count) ||
       w.count!=std::min<size_t>(AutomapData::EntriesPerPacket,w.total-w.offset))return false;
    for(size_t i=0;i<w.count;++i){const auto& e=w.entries[i];
        if(e.kind>3 || e.x<-(1<<24) || e.x>(1<<24) || e.y<-(1<<24) || e.y>(1<<24))return false;
    }return true;
}
struct View {
    Wire pending{},packet{};uint64_t startedAt=0,receivedAt=0;
    bool known=false;size_t received=0;
    std::vector<AutomapData::Entry> entries;std::vector<bool> parts;
    std::shared_ptr<const std::vector<AutomapData::Entry>> ready;
    bool Accept(const Wire& next,uint64_t now){
        if(!Valid(next))return false;
        const bool context=known && next.session==pending.session && next.epoch==pending.epoch && next.recipient==pending.recipient;
        if(context && int32_t(next.revision-pending.revision)<0)return false;
        if(!context || next.revision!=pending.revision){
            if(!context || next.level!=pending.level || next.run!=pending.run)ready.reset();
            pending=next;known=true;startedAt=now;received=0;entries.resize(next.total);
            parts.assign(std::max<size_t>(1,(next.total+AutomapData::EntriesPerPacket-1)/AutomapData::EntriesPerPacket),false);
        }
        if(next.level!=pending.level || next.run!=pending.run || next.total!=pending.total || next.lease!=pending.lease)return false;
        const auto part=next.offset/AutomapData::EntriesPerPacket;if(parts[part])return false;
        std::copy_n(next.entries.begin(),next.count,entries.begin()+next.offset);parts[part]=true;
        if(++received==parts.size()){
            ready=std::make_shared<const std::vector<AutomapData::Entry>>(std::move(entries));packet=pending;receivedAt=startedAt;
        }return true;
    }
    bool Visible(uint64_t now,uint32_t player,int level,uint32_t session,uint32_t epoch,const Data::Packet& map,uint64_t mapAge)const{
        if(!ready || now<receivedAt || now-receivedAt>=packet.lease || packet.recipient!=player || packet.level!=level ||
           packet.session!=session || packet.epoch!=epoch)return false;
        return !packet.run || (mapAge<=4000 && map.active && map.run==packet.run && map.session==session &&
            map.recipient==player && map.level==level && map.event==Data::Event::None);
    }
};
}
