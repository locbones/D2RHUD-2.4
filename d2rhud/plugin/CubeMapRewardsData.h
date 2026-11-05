#pragma once
#include "CubeMapData.h"
#include "HudLocalization.h"
#include <cstdio>
#include <optional>

namespace CubeMap::RewardsData {
constexpr uint8_t Operation=6;
constexpr uint32_t FramesPerSecond=25;
inline constexpr std::array Types{Data::Event::TreasureTrove,Data::Event::GoldenGoose,Data::Event::PlentifulPirating};
inline constexpr std::array<const char*,3> Names{"Treasure Trove","Golden Goose","Plentiful Pirating"};
inline int Index(Data::Event event){for(size_t i=0;i<Types.size();++i)if(Types[i]==event)return int(i);return -1;}
inline uint32_t DurationSeconds(Data::Event event){return Index(event)>=0 ? Data::Info(event).seconds : 0;}
inline uint16_t Remaining(uint32_t now,uint32_t start,Data::Event event){
    const uint32_t elapsed=now-start,duration=DurationSeconds(event)*FramesPerSecond;
    return elapsed>=duration ? 0 : uint16_t((duration-elapsed+FramesPerSecond-1)/FramesPerSecond);
}
// A server-frame clock for each independently active bonus. No pause count or
// cumulative pause limit: selection time never consumes the remaining duration.
struct Timer {
    uint32_t start=0;
    std::optional<uint32_t> pausedAt;
    Timer()=default;
    Timer(uint32_t frame):start(frame){}
    uint32_t FramesAt(uint32_t frame,Data::Event event)const{
        const auto duration=DurationSeconds(event)*FramesPerSecond,elapsed=pausedAt.value_or(frame)-start;
        return elapsed>=duration?0:duration-elapsed;
    }
    uint16_t RemainingAt(uint32_t frame,Data::Event event)const{
        return uint16_t((FramesAt(frame,event)+FramesPerSecond-1)/FramesPerSecond);
    }
    bool Pause(uint32_t frame,bool paused){
        if(paused==pausedAt.has_value())return false;
        if(paused)pausedAt=frame;
        else{start+=frame-*pausedAt;pausedAt.reset();}
        return true;
    }
};
#pragma pack(push,1)
struct Wire {
    uint8_t opcode=0x2b;uint64_t magic=Data::PacketMagic;uint8_t operation=Operation,version=1;
    uint32_t session=0,epoch=0,recipient=0,sequence=0;
    std::array<uint16_t,Types.size()> remaining{};
    // Uses the first formerly reserved byte; old peers still read the same size.
    uint8_t paused=0;
    std::array<uint8_t,172> padding{};
};
#pragma pack(pop)
static_assert(sizeof(Wire)==0xce && sizeof(Wire)==sizeof(Data::Packet));
inline bool Valid(const Wire& packet){
    if(packet.opcode!=0x2b || packet.magic!=Data::PacketMagic || packet.operation!=Operation || packet.version!=1 || !packet.epoch || !packet.sequence || packet.paused>1)return false;
    for(size_t i=0;i<Types.size();++i)if(packet.remaining[i]>DurationSeconds(Types[i]))return false;
    return true;
}
// The wire is authorized against the current layout handshake before Accept.
// A fresh epoch may reuse both the offline world seed and the player GUID.
struct View {
    Wire packet{};uint64_t receivedAt=0;bool known=false;
    bool Accept(const Wire& next,uint64_t now){
        if(!Valid(next))return false;
        if(known && next.session==packet.session && next.epoch==packet.epoch && int32_t(next.sequence-packet.sequence)<=0)return false;
        packet=next;receivedAt=now;known=true;return true;
    }
    std::vector<std::string> Lines(uint64_t now,uint32_t recipient,uint32_t session,uint32_t epoch)const{
        std::vector<std::string> result;
        if(!known || now<receivedAt || now-receivedAt>4000 || packet.recipient!=recipient || packet.session!=session || packet.epoch!=epoch)return result;
        const auto elapsed=packet.paused ? 0 : (now-receivedAt)/1000;
        for(size_t i=0;i<Types.size();++i)if(packet.remaining[i]>elapsed){
            const auto seconds=uint32_t(packet.remaining[i]-elapsed);char line[96]{};
            snprintf(line,sizeof(line),"%s: %u:%02u",HudLocale::Tr(Names[i]),seconds/60,seconds%60);result.emplace_back(line);
        }
        return result;
    }
};
}
