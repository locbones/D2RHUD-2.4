#pragma once
#include <cstdint>
#include <optional>
namespace HudMonsterStats::Data {
inline int FontIndex(float height,int count){
    if(count<=0)return -1;
    const int preferred=height<=720?0:height<=900?1:height<=1080?2:height<=1440?3:4;
    return preferred<count?preferred:count-1;
}
inline std::optional<int64_t> HealthPercent(int32_t hp,int32_t maximum){
    if(hp<0 || maximum<=0)return std::nullopt;
    return int64_t(hp)*100/maximum;
}
}
