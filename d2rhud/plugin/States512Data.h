#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace States512::Data {
inline constexpr unsigned MaxStates=512, NativeStates=255;
inline constexpr uint8_t ExtendedType=0x80;
struct Stat {uint16_t layer,id;int32_t value;};
static_assert(sizeof(Stat)==8);
struct Schema {unsigned bits=0,parameterBits=0;bool signedValue=false;};
struct Bits {
    std::vector<uint8_t> bytes;
    size_t length=0;
    bool Write(uint32_t value,unsigned count) {
        if(count>32 || length+count>240*8)return false;
        bytes.resize((length+count+7)/8);
        for(unsigned i=0;i<count;++i,++length)if(value&(uint32_t(1)<<i))bytes[length/8]|=uint8_t(1u<<(length%8));
        return true;
    }
};
template<class Describe>
std::vector<uint8_t> Packet(uint32_t type,uint32_t unit,unsigned state,bool active,std::span<const Stat> stats,Describe&& describe) {
    if(type>=6 || state>=MaxStates)return {};
    Bits payload;
    if(active)for(const auto& stat:stats.first((std::min)(stats.size(),size_t(16)))) {
        if(stat.id>=511)continue;
        const auto format=describe(stat.id);
        if(!format.bits)continue;
        if(format.bits>32 || format.parameterBits>32)return {};
        int64_t value=stat.value;
        if(format.bits<32) {
            const int64_t maximum=format.signedValue ? (int64_t(1)<<(format.bits-1))-1 : (int64_t(1)<<format.bits)-1;
            const int64_t minimum=format.signedValue ? -(int64_t(1)<<(format.bits-1)) : 0;
            value=std::clamp(value,minimum,maximum);
        }
        if(!payload.Write(stat.id,9) || !payload.Write(stat.layer,format.parameterBits) ||
           !payload.Write(uint32_t(value),format.bits))return {};
    }
    const bool withStats=payload.length!=0;
    if(withStats && !payload.Write(511,9))return {};
    std::vector<uint8_t> result(withStats ? 8+payload.bytes.size() : 7);
    if(result.size()>254)return {};
    result[0]=active ? (withStats ? 0xa8 : 0xa7) : 0xa9;
    // Preserve the native packet framing. Only extended state IDs use the
    // otherwise unused high bit of the six-value unit type as their ninth bit.
    result[1]=uint8_t(type)|(state>=256 ? ExtendedType : 0);
    memcpy(result.data()+2,&unit,4);
    result[withStats ? 7 : 6]=uint8_t(state);
    if(withStats){result[6]=uint8_t(result.size());memcpy(result.data()+8,payload.bytes.data(),payload.bytes.size());}
    return result;
}
inline std::vector<uint8_t> NativeView(std::span<const uint8_t> packet,unsigned stateCount) {
    if(packet.size()<7 || !(packet[1]&ExtendedType) || (packet[1]&~ExtendedType)>=6 || stateCount>MaxStates)return {};
    const bool stats=packet[0]==0xa8;
    if(!stats && packet[0]!=0xa7 && packet[0]!=0xa9)return {};
    const size_t offset=stats ? 7 : 6;
    if(stats ? packet.size()<10 || packet.size()>254 || packet[6]!=packet.size() : packet.size()!=7)return {};
    const unsigned state=256+packet[offset];
    if(state>=stateCount)return {};
    std::vector<uint8_t> result(packet.begin(),packet.end());
    result[1]&=~ExtendedType;
    result.insert(result.begin()+offset+1,1);
    if(stats)++result[6];
    return result;
}
}
