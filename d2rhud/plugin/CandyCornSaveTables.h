#pragma once
#include "CandyCornSaveData.h"
#include <cstring>

namespace CandyCornSaveData {
// Read the compiled definitions used by the game, including custom item type
// inheritance. Source TXT can differ from the BINs actually loaded by a mod.
inline bool DecodeTables(std::span<const uint8_t> items,std::span<const uint8_t> stats,
    std::span<const uint32_t> types,uint32_t typeCount,uint32_t typeStride,Tables& output) {
    if(items.empty() || items.size()%0x1b4 || items.size()/0x1b4>100000 ||
        stats.empty() || stats.size()%0x144 || stats.size()/0x144>4096 ||
        typeCount<=50 || typeCount>4096 || typeStride!=(typeCount+31)/32 || types.size()!=size_t(typeCount)*typeStride)return false;
    Tables next;
    auto word=[]<class T>(const uint8_t* p,size_t offset){T value{};memcpy(&value,p+offset,sizeof(value));return value;};
    for(size_t at=0;at<items.size();at+=0x1b4) {
        const auto row=items.data()+at;
        const auto primary=word.operator()<int16_t>(row,0x126),secondary=word.operator()<int16_t>(row,0x128);
        if(primary<0 || primary>=int(typeCount) || secondary<0 || secondary>=int(typeCount))return false;
        auto family=[&](unsigned id){const auto bit=uint32_t(1)<<(id%32);return
            (types[size_t(primary)*typeStride+id/32]&bit) || (secondary>0 && (types[size_t(secondary)*typeStride+id/32]&bit));};
        ItemDefinition item;memcpy(item.code.data(),row+0x80,4);
        item.armor=family(50);item.weapon=family(45);item.stackable=row[0x13a]!=0;
        item.gold=family(4);item.tome=family(18)||family(22);item.bodyPart=family(40)&&!family(7);
        item.charm=family(13);item.questDifficulty=row[0x132]&&row[0x133];next.items.push_back(item);
    }
    for(size_t at=0;at<stats.size();at+=0x144) {
        const auto row=stats.data()+at;const auto param=word.operator()<int32_t>(row,0x20);
        if(word.operator()<uint16_t>(row,0)!=at/0x144 || param<0 || param>32 || row[0x15]>32 || row[0x0a]>32)return false;
        next.stats.push_back({row[0x15],uint8_t(param),row[0xa],word.operator()<int32_t>(row,0x18),true});
    }
    output=std::move(next);return true;
}
}
