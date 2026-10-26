#pragma once
#include "D3WhirlwindNative.h"

namespace D3Whirlwind::Potion {
using Unit=D2UnitStrc;
using Game=D2GameStrc;
inline constexpr uint32_t ItemDisabledRva=0x9fff0;
inline constexpr uint32_t ItemTypeRva=0x1fe680;
inline constexpr uint32_t LookupRva=0x2c49c0;
struct Signature {uint32_t rva;uint64_t hash;};
// Body windows leave existing LegacyTransmog/CubeMap prologue hooks intact.
inline constexpr std::array<Signature,6> Signatures{{
    {ItemDisabledRva,0x691fc4fd54d83ad2ULL}, {0xa0050,0xb86893b8c03b8ecdULL}, {ItemTypeRva,0x279e05c3ab40a8eaULL}, {LookupRva,0x04d772b7be349296ULL},
    {0x2b7390,0x8caeaa457b5acf25ULL}, {0x2b6470,0x032e8ab358326e4bULL}
}};

inline bool BeltItem(Unit* player,Unit* item){
    using namespace NativeData;
    if(!Alive(player)||!item||item->dwUnitType!=4||item->dwItemMode!=2||!player->pInventory)return false;
    const auto inventory=player->pInventory;
    if(Read<uint32_t>(inventory)!=0x01020304||Read<Unit*>(inventory,8)!=player||
        Read<void*>(item->pItemData,0xa0)!=inventory)return false;
    const auto rows=Read<const uint8_t*>(At<void*>(0x1ca0390));
    const auto count=Read<uint64_t>(At<void*>(0x1ca0398));
    if(!rows||count>1000000||item->dwClassId>=count||!Read<uint8_t>(rows+size_t(item->dwClassId)*0x1b4,0x124))return false;
    // Native type equivalence includes custom descendants and type2. Generic
    // 'poti' also includes non-drinks such as RMD gold bars, so use the six
    // drinkable families rather than broad potion or miscellaneous types.
    const auto type=At<int(__fastcall*)(Unit*,int)>(ItemTypeRva);
    for(int id=76;id<=81;++id)if(type(item,id))return true;
    return false;
}
inline Unit* RequestItem(Game* game,Unit* player,const void* bytes,int size){
    using namespace NativeData;
    if(!game||!player||!bytes||size!=36||Read<uint8_t>(bytes)!=0x26||Read<uint8_t>(bytes,14)!=2)return nullptr;
    const auto item=At<Unit*(__fastcall*)(Game*,int,uint32_t)>(LookupRva)(game,4,Read<uint32_t>(bytes,10));
    return BeltItem(player,item)?item:nullptr;
}

// Native eligibility and item use each check STATE_SKILL_MOVE (54). Mask it
// only for one synchronous potion transaction, leaving damage/path/animation
// and every other state alone. Potion effects may legitimately change states.
class MoveStateScope {
    Unit* unit_;
    void* stats_=nullptr;
public:
    explicit MoveStateScope(Unit* unit):unit_(unit){
        using namespace NativeData;
        const auto stats=Read<void*>(unit,0x88);
        if(stats&&Read<int32_t>(stats,0x1c)<0&&(Read<uint32_t>(stats,0xacc)&0x400000)){
            stats_=stats;Write(stats,0xacc,Read<uint32_t>(stats,0xacc)&~0x400000u);
        }
    }
    ~MoveStateScope(){
        using namespace NativeData;
        if(stats_&&Read<void*>(unit_,0x88)==stats_)Write(stats_,0xacc,Read<uint32_t>(stats_,0xacc)|0x400000u);
    }
    MoveStateScope(const MoveStateScope&)=delete;
    MoveStateScope& operator=(const MoveStateScope&)=delete;
};
}
