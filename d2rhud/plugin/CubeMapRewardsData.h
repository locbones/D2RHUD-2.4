#pragma once
#include "CubeMapData.h"
#include <cstdio>

namespace CubeMap::RewardsData {
constexpr uint8_t Operation=6;
constexpr uint32_t DurationSeconds=600,FramesPerSecond=25,DurationFrames=DurationSeconds*FramesPerSecond;
inline constexpr std::array Types{Data::Event::TreasureTrove,Data::Event::GoldenGoose,Data::Event::PlentifulPirating};
inline constexpr std::array<const char*,3> Names{"Treasure Trove","Golden Goose","Plentiful Pirating"};
inline int Index(Data::Event event){for(size_t i=0;i<Types.size();++i)if(Types[i]==event)return int(i);return -1;}
inline uint16_t Remaining(uint32_t now,uint32_t start){
    const uint32_t elapsed=now-start;
    return elapsed>=DurationFrames ? 0 : uint16_t((DurationFrames-elapsed+FramesPerSecond-1)/FramesPerSecond);
}
#pragma pack(push,1)
struct Wire {
    uint8_t opcode=0x2b;uint64_t magic=Data::PacketMagic;uint8_t operation=Operation,version=1;
    uint32_t session=0,epoch=0,recipient=0,sequence=0;
    std::array<uint16_t,Types.size()> remaining{};
    std::array<uint8_t,173> padding{};
};
#pragma pack(pop)
static_assert(sizeof(Wire)==0xce && sizeof(Wire)==sizeof(Data::Packet));
inline bool Valid(const Wire& packet){
    if(packet.opcode!=0x2b || packet.magic!=Data::PacketMagic || packet.operation!=Operation || packet.version!=1 || !packet.epoch || !packet.sequence)return false;
    for(auto seconds:packet.remaining)if(seconds>DurationSeconds)return false;
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
        const auto elapsed=(now-receivedAt)/1000;
        for(size_t i=0;i<Types.size();++i)if(packet.remaining[i]>elapsed){
            const auto seconds=uint32_t(packet.remaining[i]-elapsed);char line[96]{};
            snprintf(line,sizeof(line),"%s: %u:%02u",Names[i],seconds/60,seconds%60);result.emplace_back(line);
        }
        return result;
    }
};
}
