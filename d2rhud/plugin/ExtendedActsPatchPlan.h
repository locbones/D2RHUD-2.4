#pragma once
#include "ExtendedActsContracts.h"
#include "ExtendedActsNative.h"
#include <algorithm>
#include <limits>
#include <optional>
#include <vector>

namespace ExtendedActs::Native {
inline constexpr size_t StorageBytes=0x5000,TextOffset=0x1000,SelectionOffset=0x2000,CodeOffset=0x3000;
inline constexpr size_t TownCodeOffset=CodeOffset+0x100,TownCodeStride=40;
inline constexpr size_t PresetCacheOffset=0x2100,PresetPathsOffset=0x2200,PresetPathOffset=0x2300;
struct PresetCache {uintptr_t allocator,buffer;uint32_t count,capacity;};
struct PresetPath {uintptr_t text;uint64_t size;};
static_assert(sizeof(PresetCache)==24 && sizeof(PresetPath)==16);
struct Edit {uint32_t rva;std::vector<uint8_t> before,after;};
inline bool Relative32(uint8_t* out,uintptr_t target,uintptr_t origin) {
    const auto delta=int64_t(target)-int64_t(origin);
    if(delta<(std::numeric_limits<int32_t>::min)() || delta>(std::numeric_limits<int32_t>::max)())return false;
    const int32_t value=int32_t(delta);std::memcpy(out,&value,4);return true;
}
inline std::optional<std::vector<Edit>> BuildPlan(uint8_t* image,uint8_t* storage,
    std::span<const QuestLabels,QuestsPerAct> labels,int townLevel=240) {
    if(townLevel<240 || townLevel>=1024)return {};
    std::vector<Edit> edits;
    for(const auto& source:Contracts::Patches) {
        if(std::memcmp(image+source.rva,source.before.data(),source.size))return {};
        edits.push_back({source.rva,{source.before.begin(),source.before.begin()+source.size},
            {source.after.begin(),source.after.begin()+source.size}});
    }
    for(const auto& source:Contracts::Relocations) {
        if(std::memcmp(image+source.rva,source.before.data(),source.size))return {};
        Edit edit{source.rva,{source.before.begin(),source.before.begin()+source.size},
            {source.before.begin(),source.before.begin()+source.size}};
        constexpr size_t offsets[]{0,SelectionOffset,PresetCacheOffset,PresetPathsOffset};
        if(source.table>=std::size(offsets))return {};
        const auto table=storage+offsets[source.table];
        const uintptr_t origin=reinterpret_cast<uintptr_t>(image)+(source.rip?source.rva+source.size:0);
        if(!Relative32(edit.after.data()+source.displacement,reinterpret_cast<uintptr_t>(table)+source.offset,origin))return {};
        edits.push_back(std::move(edit));
    }
    std::memset(storage,0,StorageBytes);
    BuildPresentation(std::span<QuestRecord,48>{reinterpret_cast<QuestRecord*>(storage),48},
        std::span<QuestText,6>{reinterpret_cast<QuestText*>(storage+TextOffset),6},
        std::span<const QuestRecord,41>{reinterpret_cast<QuestRecord*>(image+QuestTableRva),41},labels);
    auto selected=reinterpret_cast<int32_t*>(storage+SelectionOffset);
    std::memcpy(selected,image+0x184ec28,5*sizeof(int32_t));selected[5]=-1;
    // These five owning arrays were constructed by the EXE before the HUD
    // loads. Relocate every reference, including its six-entry destructor;
    // the adjacent original global is NOT spare storage for a sixth array.
    auto caches=reinterpret_cast<PresetCache*>(storage+PresetCacheOffset);
    std::memcpy(caches,image+0x1ca0b30,5*sizeof(PresetCache));
    caches[5].allocator=caches[0].allocator;
    auto paths=reinterpret_cast<PresetPath*>(storage+PresetPathsOffset);
    std::memcpy(paths,image+0x1854720,5*sizeof(PresetPath));
    constexpr char sixthPath[]="data/global/tiles/act6/";
    std::memcpy(storage+PresetPathOffset,sixthPath,sizeof(sixthPath));
    paths[5]={reinterpret_cast<uintptr_t>(storage+PresetPathOffset),sizeof(sixthPath)-1};
    // The original stack table supplies the previous act's final quest for
    // enabling each tab. Add Baal (flag record 40) as the sixth prerequisite.
    constexpr uint32_t unlock=0xbb73a1;
    const std::array<uint8_t,8> before{0xc7,0x44,0x24,0x48,0x1a,0,0,0};
    if(std::memcmp(image+unlock,before.data(),before.size()))return {};
    auto stub=storage+CodeOffset;
    // A leaf call preserves registers/flags and gives Windows a normal return
    // address for stack walking. Its pushed return address shifts offsets by 8.
    constexpr uint8_t initialize[]{0xc7,0x44,0x24,0x50,0x1a,0,0,0,
                                  0xc7,0x44,0x24,0x54,0x28,0,0,0,0xc3};
    std::memcpy(stub,initialize,sizeof(initialize));
    Edit edit{unlock,{before.begin(),before.end()},std::vector<uint8_t>(8,0x90)};
    edit.after[0]=0xe8;
    if(!Relative32(edit.after.data()+1,reinterpret_cast<uintptr_t>(stub),reinterpret_cast<uintptr_t>(image)+unlock+5))return {};
    edits.push_back(std::move(edit));
    // Expand only verified, complete town predicates. Many are inlined in
    // selection, NPC interaction, movement and server object handlers. A town
    // in Levels.txt alone does not make those paths recognize it as safe.
    static_assert(TownCodeOffset+Contracts::TownTests.size()*TownCodeStride<=StorageBytes);
    size_t townIndex=0;
    for(const auto& town:Contracts::TownTests) {
        if(std::memcmp(image+town.rva,town.before.data(),town.size))return {};
        auto code=storage+TownCodeOffset+townIndex++*TownCodeStride;
        size_t at=0;
        const bool extended=town.reg>=8;
        auto cmp=[&](int value,bool wide){
            if(extended)code[at++]=0x41;
            code[at++]=wide?0x81:0x83;code[at++]=uint8_t(0xf8+(town.reg&7));
            if(wide){std::memcpy(code+at,&value,4);at+=4;}else code[at++]=uint8_t(value);
        };
        auto jump=[&](uint32_t target,bool equal){
            if(equal){code[at++]=0x0f;code[at++]=0x84;}else code[at++]=0xe9;
            const size_t operand=at;at+=4;
            return Relative32(code+operand,reinterpret_cast<uintptr_t>(image)+target,reinterpret_cast<uintptr_t>(code)+at);
        };
        cmp(townLevel-103,true);
        code[at++]=0x74;const size_t special=at++;
        cmp(6,false);
        if(!jump(town.yes,true) || !jump(town.no,false))return {};
        code[special]=uint8_t(at-special-1);
        // Match the original final town case's scratch register and flags.
        if(extended)code[at++]=0x41;
        code[at++]=uint8_t(0xb8+(town.reg&7));const uint32_t lastTown=6;
        std::memcpy(code+at,&lastTown,4);at+=4;
        if(!jump(town.yes,false) || at>TownCodeStride)return {};
        Edit expanded{town.rva,{town.before.begin(),town.before.begin()+town.size},std::vector<uint8_t>(town.size,0x90)};
        expanded.after[0]=0xe9;
        if(!Relative32(expanded.after.data()+1,reinterpret_cast<uintptr_t>(code),reinterpret_cast<uintptr_t>(image)+town.rva+5))return {};
        edits.push_back(std::move(expanded));
    }
    return edits;
}
}
