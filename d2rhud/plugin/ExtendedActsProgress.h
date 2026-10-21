#pragma once
#include <cstdint>

namespace ExtendedActs::Progress {
inline constexpr unsigned Baal=40, FinalQuest=47;
// Keep the original five-step difficulty/title encoding. Act VI owns its
// final step; adding a sixth step would misinterpret existing characters.
constexpr uint16_t Complete(uint16_t flags,unsigned difficulty,bool baal,bool finalQuest) {
    if(difficulty>=3 || !(flags&0x20) || !baal || !finalQuest)return flags;
    const unsigned progress=(flags>>8)&31, earned=5*(difficulty+1);
    return earned>progress ? uint16_t((flags&0xe0ff)|(earned<<8)) : flags;
}
}
