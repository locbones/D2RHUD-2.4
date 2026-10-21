#pragma once
#include <array>
#include <cstdint>
namespace CubeMap::LayoutData {
// Operations 0..4 belong to stash, maps, automap, Survivors and Whirlwind.
// Version 3 includes deterministic jungle-template regeneration. Older peers
// must not acknowledge the same seed while generating different terrain.
constexpr uint8_t Operation=5,Version=3;
#pragma pack(push,1)
struct Wire {
    uint8_t opcode=0x2b;uint64_t magic=Data::PacketMagic;
    uint8_t operation=Operation,version=Version;
    uint32_t session=0,epoch=1,recipient=0;uint16_t level=0;uint32_t seed=0;
    std::array<uint8_t,0xce-29> padding{};
};
struct Ack {
    uint8_t opcode=3;uint64_t magic=Data::PacketMagic;
    uint8_t operation=Operation,version=Version;
    uint32_t session=0,epoch=1;uint16_t level=0;uint32_t seed=0;
    std::array<uint8_t,0xd2-25> padding{};
};
#pragma pack(pop)
static_assert(sizeof(Wire)==0xce && sizeof(Ack)==0xd2);
template<class T>bool Valid(const T& packet){return packet.magic==Data::PacketMagic && packet.operation==Operation && packet.version==Version && packet.epoch && ((!packet.level && !packet.seed) || (packet.level>0 && packet.level<1024 && !Data::Town(packet.level) && packet.seed));}
}
