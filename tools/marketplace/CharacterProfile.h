#pragma once
#include "CustodyPlan.h"

// Read-only version-98 character inspection. Character bytes are never written,
// uploaded, or passed to the custody planner. Stash/corpse/cube contents are
// parsed for boundary validation and deliberately excluded from the result.
namespace Marketplace::CharacterProfile {
inline Json Read(std::span<const uint8_t> bytes,const Custody::Tables& tables){
    using namespace CandyCornSaveData;
    auto require=[](bool ok){if(!ok)throw std::runtime_error("Unsupported or incomplete character save.");};
    require(bytes.size()>=0x2ff&&bytes.size()<=8*1024*1024);
    require(Detail::Word(bytes,0)==0xaa55aa55&&Detail::Word(bytes,4)==98&&Detail::Word(bytes,8)==bytes.size()&&Detail::Word(bytes,12)==Checksum(bytes));
    require(bytes[0x28]<=6&&bytes[0x2b]>0);
    std::string name;for(unsigned n=0;n<16&&bytes[0x10b+n];++n){const auto ch=bytes[0x10b+n];require(ch>=32&&ch<=126);name+=char(ch);}require(!name.empty());
    Detail::Budget budget;Detail::State state;state.bits={bytes,0,&budget,true};require(Detail::Header(state.bits,tables.save));
    Json equipment=Json::array(),inventory=Json::array(),mercenary=Json::array();
    auto item=[&](){
        const auto start=state.bits.pos;SavedItem value;
        // Ears have no compiled item definition; preserve a display-only entry.
        auto peek=state;Detail::Prefix prefix;require(Detail::ItemPrefix(peek,tables.save,0,prefix));
        if(!prefix.definition){require((prefix.flags&(1u<<16))!=0);state.bits=peek.bits;value.code="ear";value.flags=prefix.flags;value.itemHex=Hex(bytes.subspan(start/8,(state.bits.pos-start)/8));}
        else value=StashDetail::Item(state,tables.save,0);
        auto bits=state.bits;bits.pos=start+35;const auto location=bits.Read(3),body=bits.Read(4),x=bits.Read(4),y=bits.Read(4),container=bits.Read(3);
        auto row=value.ToJson();row["location"]=location;row["body"]=body;row["x"]=x;row["y"]=y;row["container"]=container;
        const auto found=tables.sizes.find(value.code);row["grid_width"]=found==tables.sizes.end()?1:found->second.first;row["grid_height"]=found==tables.sizes.end()?1:found->second.second;
        require(state.bits.ok&&!budget.limit);return row;
    };
    auto list=[&](int target){
        require(state.bits.Mark(0x4d4a));const auto count=state.bits.Read(16);require(count<=4096);
        for(unsigned n=0;n<count;++n){auto row=item();if(target<0)continue;
            if(target==1){require(row["location"]==1);mercenary.push_back(std::move(row));}
            else if(row["location"]==1)equipment.push_back(std::move(row));
            else if((row["location"]==0&&row["container"]==1)||row["location"]==2)inventory.push_back(std::move(row));
        }
    };
    list(0);
    require(state.bits.Mark(0x4d4a));const auto corpses=state.bits.Read(16);require(corpses<=16);
    for(unsigned n=0;n<corpses;++n){state.bits.Skip(96);list(-1);}
    const bool expansion=(bytes[0x24]&0x20)!=0;
    if(expansion){require(state.bits.Mark(0x666a));if(Detail::Word(bytes,0xb3))list(1);
        require(state.bits.Mark(0x666b));const auto golem=state.bits.Read(8);require(golem<=1);if(golem)(void)item();}
    require(state.bits.ok&&state.bits.pos==bytes.size()*8&&!budget.limit);
    return {{"schema",1},{"game_mode","2.4"},{"mod",tables.mod},{"name",name},{"class_id",bytes[0x28]},{"level",bytes[0x2b]},
        {"hardcore",bool(bytes[0x24]&4)},{"expansion",expansion},{"equipment",equipment},{"inventory",inventory},
        {"mercenary",{{"present",expansion&&Detail::Word(bytes,0xb3)!=0},{"type",Detail::Word(bytes,0xb9,2)},{"equipment",mercenary}}}};
}
}
