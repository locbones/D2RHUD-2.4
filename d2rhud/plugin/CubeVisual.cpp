#include "CubeVisual.h"
#include "CubeVisualData.h"
#include "CubeMapData.h"
#include "LegacyTransmogData.h"
#include "GameAssets.h"
#include "CompiledNames.h"
#include "QuestJournal.h"
#include <intrin.h>
#include <set>
#include "../Pattern.h"
#include "../d2/Reclass.h"
#include <MinHook.h>
#include <atomic>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <climits>
#include <shellapi.h>

namespace CubeVisual {
namespace {
using namespace Data;
using Unit=D2UnitStrc;
using Game=D2GameStrc;
using Location=LegacyTransmog::Data::Location;
template<class T> T Native(uint32_t rva) { return reinterpret_cast<T>(Pattern::Address(rva)); }
template<class T> bool Read(uintptr_t address,T& out) {
    SIZE_T size=0;
    return address && ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),&out,sizeof(out),&size) && size==sizeof(out);
}
using Linker=void(__fastcall*)(char*,Recipe*,int,int,int,int);
using Parser=int(__fastcall*)(Output*,char*,int,int);
struct Input { Unit* item; int32_t base,level; };
static_assert(sizeof(Input)==16);
using Create=void(__fastcall*)(Game*,Unit*,Recipe*,Input*);
using PortalAction=int(__fastcall*)(Game*,Unit*,Output*);
PortalAction originalPortalAction=nullptr;
std::atomic<MapPrepare> mapPrepare{nullptr};
struct PreparedMap {Game* game;Unit* player;int level;};
thread_local PreparedMap* preparedMap=nullptr;
using Add=bool(__fastcall*)(Unit*,const Location*,const uint32_t*,size_t,bool);
using Notify=void(__fastcall*)(Game*,Unit*,Unit*,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t);
Linker originalLinker=nullptr;
Create originalCreate=nullptr;
Add originalAdd=nullptr;
Notify originalNotify=nullptr;
std::atomic<bool> installed{false};
std::atomic<const char*> status{"Cube visual recipes are not installed."};
struct Diagnostics {
    std::atomic<uint32_t> sourceMatches{0}, applied{0}, models{0};
    std::atomic<int> lastValue{0};
} diagnostics;
std::mutex sourceMutex;
SourceTable sourceTable;
std::filesystem::path sourcePath;
std::filesystem::path installRoot;
std::string sourceMod;
bool sourceCompilation=false;
std::filesystem::file_time_type sourceTime{};
void LoadSource() {
    wchar_t exe[MAX_PATH]{};char mod[128]{};
    if(!GetModuleFileNameW(nullptr,exe,MAX_PATH) || !Read(Pattern::Address(0x1bf084f),mod))return;
    mod[sizeof(mod)-1]=0;
    const auto root=std::filesystem::path(exe).parent_path();
    const std::string name=mod;
    if(!name.empty())for(const auto& part:std::filesystem::path(name))if(part==".." || part=="." || part.has_root_path())return;
    GameAssets assets(root,name);
    int argc=0;auto args=CommandLineToArgvW(GetCommandLineW(),&argc);
    bool compiling=false;if(args){for(int n=1;n<argc;++n)if(!_wcsicmp(args[n],L"-txt"))compiling=true;LocalFree(args);}
    // Ordinary games execute BIN recipes even if leftover TXT files exist.
    // Only a compilation launch may bind its freshly edited source instead.
    const auto text=assets.ReadTableSource("cubemain",!compiling);
    const auto questConfig=assets.Read("D2RHUD/quests.json");
    QuestJournal::Configure(root,name,text,std::string(questConfig.begin(),questConfig.end()));
    auto table=ParseSource(text);
    std::lock_guard lock(sourceMutex);
    sourceTable=std::move(table);
    installRoot=root;sourceMod=name;sourceCompilation=compiling;
    sourcePath=compiling?(name.empty() ? root/"data/global/excel/cubemain.txt" : root/"Mods"/name/(name+".mpq")/"data/global/excel/cubemain.txt"):std::filesystem::path{};
    std::error_code error;sourceTime=std::filesystem::last_write_time(sourcePath,error);
}
std::optional<SourceRecipe> SourceOptions(Recipe* recipe) {
    uintptr_t table=0;uint64_t count=0;
    if(!Read(Pattern::Address(0x1ca0b18),table) || !table || !Read(Pattern::Address(0x1ca0b20),count) || count>100000 ||
       reinterpret_cast<uintptr_t>(recipe)<table)return {};
    const auto offset=reinterpret_cast<uintptr_t>(recipe)-table;
    if(offset%sizeof(Recipe) || offset/sizeof(Recipe)>=count)return {};
    std::lock_guard lock(sourceMutex);
    char currentMod[128]{};
    if(!installRoot.empty() && Read(Pattern::Address(0x1bf084f),currentMod)) {
        currentMod[sizeof(currentMod)-1]=0;
        const std::string name=currentMod;
        if(name!=sourceMod && name.find_first_of("/\\:")==name.npos && name!=".." && name!=".") {
            sourceMod=name;
            sourcePath=sourceCompilation?(name.empty() ? installRoot/"data/global/excel/cubemain.txt" : installRoot/"Mods"/name/(name+".mpq")/"data/global/excel/cubemain.txt"):std::filesystem::path{};
            sourceTime={};sourceTable={};
        }
    }
    // Only loose-file changes are read on a transmute. CASC access belongs to
    // the startup loader, never to the render or game simulation callbacks.
    if(!sourcePath.empty()) {
        std::error_code error;const auto changed=std::filesystem::last_write_time(sourcePath,error);
        if(!error && changed!=sourceTime) {
            std::ifstream input(sourcePath,std::ios::binary|std::ios::ate);
            if(input && input.tellg()>=0 && input.tellg()<64*1024*1024) {
                std::string text(size_t(input.tellg()),'\0');input.seekg(0);
                if(input.read(text.data(),text.size())){sourceTable=ParseSource(text);sourceTime=changed;}
            }
        }
    }
    const auto entry=sourceTable.visuals.find(offset/sizeof(Recipe));
    if(entry==sourceTable.visuals.end())return {};
    auto result=entry->second;
    if(sourceTable.rows!=count || result.enabled!=recipe->header[0] || result.inputs!=recipe->header[0x10])
    {
        result.matches=false;
        for(auto& option:result.outputs)if(option.present)option.valid=false;
    }
    return result;
}
constexpr std::array<uint32_t,6> Hooks{0x2620a0,0x306c50,0x2d5e10,0x316b80,0x306a40,0x308840};
constexpr std::array<LegacyTransmog::Data::Signature,18> Signatures{{
    {0x2620a0,0x29,0x6896e3b7223f1d7fULL},
    {0x2616b0,0x9bc,0x756d85c58479530eULL},
    {0x306c50,0x1bec,0xac545ecdc36232fcULL},
    {0x2d5e10,0x6b0,0x3aa57b1f7d9fffc6ULL},
    {0x42d430,0x112,0x0e4df4d316d92e3fULL},
    {0x6d0470,0x128,0x8aa82476087867bdULL},
    {0x1e5620,0x72,0xa1e6f0ef87a796f0ULL},
    {0x1e5440,0x149,0xe0541ca8ff237e82ULL},
    {0x316b80,0x752,0x31b033f05b413172ULL},
    {0x1e5270,0xe1,0x98749f8d1bae5c5bULL},
    {0x1e4700,0x12c,0xeb9f1b40828b3daaULL},
    {0x1e4ea0,0x2b7,0xfa3f42c47ae830d1ULL},
    {0x306a40,0x210,0xa06b3f86a31c57c3ULL},
    {0x308840,0x82c,0xb01c7cc2c185785fULL},
    {0x2c49c0,0x20,0x04d772b7be349296ULL},
    {0x2d5890,0x15a,0xb0f96ee700803344ULL},
    {0x2f58c0,0x980,0x5d3da95b188dffa5ULL},
    {0x2f70a0,0xd0,0xec62a147c9ab56ecULL},
}};
int StatId(int value=0) {
    uintptr_t linker=0,table=0;uint64_t count=0;
    if(!Read(Pattern::Address(0x1c9fb78),table) || !table || !Read(Pattern::Address(0x1c9fb80),count))return -1;
    int id=-1;
    if(Read(Pattern::Address(0x1c9fb90),linker) && linker){
        const auto lookup=Native<int(__fastcall*)(void*,const char*)>(0x6d0470);
        id=lookup(reinterpret_cast<void*>(linker),"hud_visual");
        if(id<0)id=lookup(reinterpret_cast<void*>(linker),"transmog");
    }
    if(id<0)id=CompiledNames::Resolve(0x1c9fb90,"hud_visual");
    if(id<0)id=CompiledNames::Resolve(0x1c9fb90,"transmog");
    if(id<0 || id>=511 || uint64_t(id)>=count)return -1;
    std::array<uint8_t,0x144> record{};
    if(!Read(table+size_t(id)*record.size(),record))return -1;
    uint32_t flags=0,bias=0;memcpy(&flags,record.data()+4,4);memcpy(&bias,record.data()+0x18,4);
    // Save/packet widths must preserve the negative cosmetic ID exactly.
    if(!(flags&2) || record[8]<15 || record[8]>32 || !record[0x15] || record[0x15]>32 ||
       !bias || (record[0x15]<32 && bias>=(uint64_t(1)<<record[0x15])) || record[0x14] || value>0 ||
       -int64_t(value)>bias || (record[8]<32 && -int64_t(value)>(int64_t(1)<<(record[8]-1))))return -1;
    return id;
}
Target Resolve(const std::string& name,int row,int output) {
    Output parsed{};std::string text=name;
    // The native set-name branch fills a valid record but returns zero. Its
    // linker ignores that return too; validate the resolved record and tables.
    Native<Parser>(0x2616b0)(&parsed,text.data(),row,output);
    if(parsed.type==252){
        if(!(parsed.flags&8) && parsed.base<IndexSlots)return {0,parsed.base};
        if(parsed.special && parsed.quality==7)return {1,int(parsed.special)-1};
        if(parsed.special && parsed.quality==5)return {2,int(parsed.special)-1};
    }
    // The BIN tables retain item codes and set/unique names even when the
    // source-only parser linkers are empty. Resolve against those records.
    uintptr_t table=0;uint64_t count=0;
    if(!name.empty() && name.size()<=4 && Read(Pattern::Address(0x1ca0390),table) && table &&
       Read(Pattern::Address(0x1ca0398),count) && count<=1000000){
        uint32_t wanted=0x20202020;memcpy(&wanted,name.data(),name.size());
        for(size_t i=0;i<count;++i){uint32_t code=0;if(!Read(table+i*0x1b4+0x80,code))break;if(code==wanted)return {0,int(i)};}
    }
    for(const int kind:{1,2}){
        const uint32_t address=kind==1 ? 0x1c9fc70 : 0x1c9fc50;
        const size_t stride=kind==1 ? 0x150 : 0x1bc;
        if(!Read(Pattern::Address(address),table) || !table || !Read(Pattern::Address(address+8),count) || count>65536)continue;
        for(size_t i=0;i<count;++i){
            std::array<char,32> title{};if(!Read(table+i*stride+2,title))break;
            const auto end=std::find(title.begin(),title.end(),'\0');
            const std::string value(title.begin(),end);
            if(!_stricmp(value.c_str(),name.c_str()))return {kind,int(i)};
        }
    }
    return {};
}
bool Base(Target target,uint32_t& id) {
    if(!target.Valid())return false;
    uintptr_t table=0;uint64_t count=0;
    if(!Read(Pattern::Address(0x1ca0390),table) || !table || !Read(Pattern::Address(0x1ca0398),count) || count>1000000)return false;
    if(target.kind==0) {id=target.index;return id<count;}
    uintptr_t special=0;uint64_t total=0;
    const uint32_t address=target.kind==1 ? 0x1c9fc70 : 0x1c9fc50;
    const size_t stride=target.kind==1 ? 0x150 : 0x1bc;
    uint32_t code=0;
    if(!Read(Pattern::Address(address),special) || !special || !Read(Pattern::Address(address+8),total) ||
       uint64_t(target.index)>=total || !Read(special+size_t(target.index)*stride+0x28,code))return false;
    for(uint32_t i=0;i<count;++i) {
        uint32_t candidate=0;
        if(!Read(table+size_t(i)*0x1b4+0x80,candidate))return false;
        if(code==candidate){id=i;return true;}
    }
    return false;
}
void Reject(Recipe* recipe,const char* reason) {
    recipe->header[0]=0;
    status.store(reason);
    OutputDebugStringA(reason);
}
void __fastcall LinkerHook(char* text,Recipe* recipe,int output,int position,int row,int column) {
    if(!installed.load() || !text || !recipe || output<0 || output>=3) {originalLinker(text,recipe,output,position,row,column);return;}
    const auto map=CubeMap::Data::Parse(text);
    if(map.present) {
        if(!map.valid){Reject(recipe,"Cube map recipe disabled: use map=<positive level ID> or map=zzz, optionally with useitem.");return;}
        char portal[]="Red Portal";
        originalLinker(portal,recipe,output,position,row,column);
        recipe->outputs[output]=CubeMap::Data::Encode(map.level);
        if(map.useItem)recipe->outputs[output].flags|=CubeMap::Data::PreserveInput;
        return;
    }
    const auto option=Parse(text);
    if(!option.present){originalLinker(text,recipe,output,position,row,column);return;}
    if(!option.valid){Reject(recipe,"Cube visual recipe disabled: use only useitem,vis=<code or set/unique name>.");return;}
    const auto target=Resolve(option.target,row,output);
    uint32_t base=0;
    if(!target.Valid() || !Base(target,base)) {Reject(recipe,"Cube visual recipe disabled: unknown or out-of-range visual target.");return;}
    if(StatId(target.Encode())<0) {Reject(recipe,"Cube visual recipe disabled: configure a signed saved hud_visual or transmog stat with sufficient save/send bits.");return;}
    char useitem[]="useitem";
    originalLinker(useitem,recipe,output,position,row,column);
    auto& result=recipe->outputs[output];
    result.flags|=VisualFlag;
    result.base=static_cast<uint16_t>(-target.Encode());
}
// Item serialization writes state-zero MAGIC lists, not the unit's base stats.
// Keep the selector beside saved item modifiers so packets and saves retain it.
void* SavedList(Unit* item,bool create=false) {
    uintptr_t extended=0,list=0;int32_t flags=0;
    if(!Read(reinterpret_cast<uintptr_t>(item)+0x88,extended) || !extended ||
       !Read(extended+0x1c,flags) || flags>=0 || !Read(extended+0x68,list))return nullptr;
    for(size_t n=0;list && n<1024;++n) {
        int32_t state=0;
        if(!Read(list+0x1c,flags) || !Read(list+0x20,state))return nullptr;
        if((flags&0x40) && !state)return reinterpret_cast<void*>(list);
        uintptr_t next=0;if(!Read(list+0x48,next) || next==list)return nullptr;
        list=next;
    }
    if(list || !create)return nullptr;
    const auto result=Native<void*(__fastcall*)(uint32_t,int,uint32_t,uint32_t)>(0x1e4700)(0x40,0,4,item->dwUnitId);
    if(!result)return nullptr;
    Native<void(__fastcall*)(Unit*,void*,int)>(0x1e4ea0)(item,result,1);
    return SavedList(item);
}
int SavedStat(void* list,int stat) {
    uintptr_t values=0;uint64_t count=0;
    if(!list || !Read(reinterpret_cast<uintptr_t>(list)+0x30,values) ||
       !Read(reinterpret_cast<uintptr_t>(list)+0x38,count) || count>65536)return 0;
    for(size_t n=0;n<count;++n) {
        struct Entry {uint32_t key;int32_t value;} entry{};
        if(!Read(values+n*sizeof(entry),entry))return 0;
        if(entry.key==(uint32_t(stat)<<16))return entry.value;
    }
    return 0;
}
void SetSavedStat(void* list,int stat,int value) {
    Native<int(__fastcall*)(void*,int,int,uint16_t)>(0x1e5270)(list,stat,value,0);
}
struct Change { Unit* item=nullptr; uint32_t id=0; int value=0; void* saved=nullptr; };
struct Context { Unit* player; int stat; std::array<Change,3> changes{}; };
thread_local Context* current=nullptr;
int GetStat(Unit* item,int stat) { return Native<int(__fastcall*)(Unit*,uint32_t,uint16_t)>(0x1e5620)(item,stat,0); }
struct RuneMatch;
bool TryQuestMatch(RuneMatch*,Game*,Unit*,Input*);
#include "CubeRuneBag.inl"
#include "QuestCubeNative.inl"
#include "QuestCubeRuntime.inl"
void __fastcall CreateHook(Game* game,Unit* player,Recipe* recipe,Input* inputs) {
    if(!installed.load() || !recipe || !inputs) {originalCreate(game,player,recipe,inputs);return;}
    if(CreateQuest(game,player,recipe,inputs))return;
    if(DepositRunes(game,player,recipe,inputs))return;
    size_t questRow=SIZE_MAX;
    const auto questTable=RuneField<Recipe*>(Native<void*>(0x1ca0b18));const auto questCount=RuneField<uint64_t>(Native<void*>(0x1ca0b20));
    const auto questAt=reinterpret_cast<uintptr_t>(recipe),questFirst=reinterpret_cast<uintptr_t>(questTable);
    if(questTable&&questAt>=questFirst&&(questAt-questFirst)%sizeof(Recipe)==0&&(questAt-questFirst)/sizeof(Recipe)<questCount)questRow=(questAt-questFirst)/sizeof(Recipe);
    Recipe augmented=*recipe;
    if(const auto source=SourceOptions(recipe)) {
        for(size_t i=0;i<3;++i) {
            const auto map=CubeMap::Data::Parse(source->rawOutputs[i]);
            if(!map.present)continue;
            if(!map.valid || !source->matches){status.store("Cube map rejected before consumption: invalid or mismatched source recipe.");return;}
            // Keep separate TXT mod columns visible to validation; silently
            // dropping them would make an invalid mixed recipe appear valid.
            auto& output=augmented.outputs[i];
            if(!map.useItem)for(const auto& mod:output.mods)if(mod.property>=0){status.store("Cube map rejected: item modifiers require useitem on the map output.");return;}
            if(!CubeMap::Data::IsMap(output)) {
                if(map.useItem){output.type=4;output.special=CubeMap::Data::Marker;output.base=uint16_t(map.level);}
                else output=CubeMap::Data::Encode(map.level);
            }else output.base=uint16_t(map.level);
            if(map.useItem)output.flags|=CubeMap::Data::PreserveInput;
        }
        for(size_t i=0;i<3;++i)if(source->outputs[i].present && !augmented.outputs[i].Visual()) {
            const auto& option=source->outputs[i];
            const auto target=option.valid ? Resolve(option.target,0,int(i)) : Target{};
            uint32_t base=0;
            if(!target.Valid() || !Base(target,base) || augmented.outputs[i].type!=254) {
                status.store("Cube visual recipe rejected before consumption: source tables do not match the loaded recipe or its visual target is invalid.");return;
            }
            augmented.outputs[i].flags|=VisualFlag;
            augmented.outputs[i].base=uint16_t(-target.Encode());
            ++diagnostics.sourceMatches;
        }
    }
    recipe=&augmented;
    int mapLevel=0,mapCount=0;
    for(const auto& output:recipe->outputs)if(CubeMap::Data::IsMap(output)){mapLevel=output.base;++mapCount;}
    if(mapCount) {
        for(const auto& output:recipe->outputs)if(output.Visual()){
            status.store("Cube map rejected: use ordinary item outputs alongside a map, not visual-only outputs.");return;
        }
        for(const auto& output:recipe->outputs)if(CubeMap::Data::IsMap(output)) {
            if(!(output.flags&CubeMap::Data::PreserveInput)) {
                if(output.flags || output.quality || output.quantity){status.store("Cube map rejected: item output modifiers require useitem.");return;}
                for(auto id:output.prefixes)if(id)return;
                for(auto id:output.suffixes)if(id)return;
                for(const auto& mod:output.mods)if(mod.property>=0)return;
            }
        }
        const auto prepare=mapPrepare.load();
        if(mapCount!=1 || !prepare || !prepare(game,player,augmented))return;
        // Preparation resolves map=zzz in this invocation's copy. Authenticate
        // the actual portal destination while retaining the reusable marker in
        // the loaded recipe for the next transmute.
        for(const auto& output:augmented.outputs)if(CubeMap::Data::IsMap(output))mapLevel=output.base;
        // The map was prepared before native input consumption. Preserve the
        // ordinary item outputs, including useitem on the map-bearing slot.
        for(auto& output:augmented.outputs)if(CubeMap::Data::IsMap(output) && (output.flags&CubeMap::Data::PreserveInput)) {
            output.flags&=~CubeMap::Data::PreserveInput;output.base=0;output.special=0;output.type=254;
        }
        PreparedMap context{game,player,mapLevel};
        struct Scope {PreparedMap* previous;Scope(PreparedMap* value):previous(preparedMap){preparedMap=value;}~Scope(){preparedMap=previous;}} scope(&context);
        originalCreate(game,player,recipe,inputs);
        return;
    }
    bool visual=false;
    for(const auto& output:recipe->outputs)visual|=output.Visual();
    if(!visual){ObserveCraft(game,player,recipe,inputs,questRow);return;}
    Recipe nativeRecipe=*recipe;
    Context context{player,-1};
    for(size_t i=0;i<3;++i)if(recipe->outputs[i].Visual()) {
        const auto& output=recipe->outputs[i];uint32_t base=0;Unit unit{};
        const auto target=Target::Decode(-int(output.base));
        const int stat=StatId(target.Encode());
        if(!output.PureVisual() || stat<0 || !Base(target,base) ||
           !Read(reinterpret_cast<uintptr_t>(inputs[i].item),unit) || unit.dwUnitType!=4 || !unit.pItemData) {
            status.store("Cube visual recipe rejected before consumption: visual outputs must preserve the input without other output modifiers.");
            return;
        }
        for(size_t j=0;j<i;++j)if(inputs[j].item==inputs[i].item && context.changes[j].item)return;
        context.stat=stat;context.changes[i]={inputs[i].item,unit.dwUnitId,target.Encode()};
        nativeRecipe.outputs[i].flags=0;nativeRecipe.outputs[i].base=0;
        // Ordinary recipes often retain lvl/plvl/ilvl values. Visual outputs
        // preserve the source item level, so ignore these generation settings.
        nativeRecipe.outputs[i].level=0;nativeRecipe.outputs[i].playerLevel=0;nativeRecipe.outputs[i].itemLevel=0;
    }
    for(auto& change:context.changes)if(change.item) {
        change.saved=SavedList(change.item,true);
        if(!change.saved){status.store("Cube visual recipe rejected before consumption: saved item modifiers are unavailable.");return;}
    }
    struct Scope { Context* previous;Scope(Context* value):previous(current){current=value;}~Scope(){current=previous;} } scope(&context);
    ObserveCraft(game,player,&nativeRecipe,inputs,questRow);
}
bool __fastcall AddHook(Unit* player,const Location* locations,const uint32_t* ids,size_t count,bool special) {
    const auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
    if(!current || current->player!=player || !ids || !count){
        const bool result=originalAdd(player,locations,ids,count,special);RuneAdded(result,player,ids,count);QuestAdded(result,player,ids,count,caller);return result;
    }
    std::array<int,3> old{};std::array<bool,3> applied{};
    for(size_t i=0;i<3;++i) {
        const auto& change=current->changes[i];
        if(!change.item || std::find(ids,ids+count,change.id)==ids+count)continue;
        old[i]=SavedStat(change.saved,current->stat);SetSavedStat(change.saved,current->stat,change.value);applied[i]=true;
    }
    const bool result=originalAdd(player,locations,ids,count,special);
    RuneAdded(result,player,ids,count);
    QuestAdded(result,player,ids,count,caller);
    if(result)for(size_t i=0;i<3;++i)if(applied[i]){++diagnostics.applied;diagnostics.lastValue=current->changes[i].value;}
    if(!result)for(size_t i=0;i<3;++i)if(applied[i])SetSavedStat(current->changes[i].saved,current->stat,old[i]);
    return result;
}
int __fastcall PortalActionHook(Game* game,Unit* player,Output* output) {
    if(output && CubeMap::Data::IsMap(*output))
        return preparedMap && preparedMap->game==game && preparedMap->player==player && preparedMap->level==output->base ? 1 : 0;
    return originalPortalAction(game,player,output);
}
void __fastcall NotifyHook(Game* game,Unit* owner,Unit* item,uint32_t flags,uint32_t command,
    uint32_t mode,uint32_t page,uint32_t body,uint32_t xy) {
    if(current && owner==current->player && command==2) {
        for(const auto& change:current->changes)if(change.item==item) {
            // useitem reuses its GUID. A normal move packet does not republish
            // changed saved stats; NEWITEM requests the full item description.
            // The native notification scope restores this flag after sending.
            flags|=1;
            break;
        }
    }
    if(runeTransaction && owner==runeTransaction->player && item==runeTransaction->bag && command==2)flags|=1;
    originalNotify(game,owner,item,flags,command,mode,page,body,xy);
}
bool Supported() {
    std::array<uint8_t,0x1c00> bytes{};
    for(const auto& signature:Signatures) {
        SIZE_T count=0;
        if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(Pattern::Address(signature.rva)),bytes.data(),signature.size,&count) ||
           count!=signature.size || LegacyTransmog::Data::Hash({bytes.data(),signature.size})!=signature.hash)return false;
    }
    return true;
}
}
const char* WorldPath(void* manager,Unit* item) {
    if(!installed.load() || !manager || !item)return nullptr;
    Unit copy{};D2ItemDataStrc data{};
    if(!Read(reinterpret_cast<uintptr_t>(item),copy) || copy.dwUnitType!=4 || !Read(reinterpret_cast<uintptr_t>(copy.pItemData),data))return nullptr;
    const int stat=StatId();
    if(stat<0)return nullptr;
    const auto target=Target::Decode(GetStat(item,stat));
    uint32_t base=0;
    if(!Base(target,base))return nullptr;
    copy.dwClassId=base;copy.pItemData=&data;
    data.dwQualityNo=target.kind==1 ? 7 : target.kind==2 ? 5 : 2;
    data.dwFileIndex=target.kind==0 ? uint32_t(-1) : uint32_t(target.index);
    data.dwItemFlags|=0x10;
    // Only a stack copy reaches the asset lookup. The real item's code,
    // quality, stats, sockets, seeds and inventory sprite are never replaced.
    const auto path=Native<const char*(__fastcall*)(void*,Unit*)>(0x42d430)(manager,&copy);
    if(path && *path)++diagnostics.models;
    return path;
}
bool Install() {
    if(installed.load())return true;
    if(!Supported()){status.store("Cube visual recipes unavailable: executable signature mismatch.");return false;}
    LoadSource();
    InitQuestMatcher();
    QuestJournal::SetProofRecovery(RecoverQuestProof);QuestJournal::SetRewardClaim(ClaimQuestBonus);
    const auto initialized=MH_Initialize();
    if(initialized!=MH_OK && initialized!=MH_ERROR_ALREADY_INITIALIZED)return false;
    void* replacements[]{reinterpret_cast<void*>(LinkerHook),reinterpret_cast<void*>(CreateHook),reinterpret_cast<void*>(AddHook),reinterpret_cast<void*>(NotifyHook),reinterpret_cast<void*>(PortalActionHook),reinterpret_cast<void*>(RuneMatchHook)};
    void** originals[]{reinterpret_cast<void**>(&originalLinker),reinterpret_cast<void**>(&originalCreate),reinterpret_cast<void**>(&originalAdd),reinterpret_cast<void**>(&originalNotify),reinterpret_cast<void**>(&originalPortalAction),reinterpret_cast<void**>(&originalRuneMatch)};
    size_t created=0;
    for(;created<Hooks.size();++created)if(MH_CreateHook(reinterpret_cast<void*>(Pattern::Address(Hooks[created])),replacements[created],originals[created])!=MH_OK)break;
    if(created==Hooks.size()) {
        bool queued=true;
        for(auto rva:Hooks)if(MH_QueueEnableHook(reinterpret_cast<void*>(Pattern::Address(rva)))!=MH_OK)queued=false;
        if(queued && MH_ApplyQueued()==MH_OK) {
            installed.store(true);status.store("Cube recipes: vis= / visual= and Z01 mixed-rune deposits active.");return true;
        }
    }
    for(size_t i=0;i<created;++i)MH_QueueDisableHook(reinterpret_cast<void*>(Pattern::Address(Hooks[i])));
    MH_ApplyQueued();
    for(size_t i=0;i<created;++i){auto address=reinterpret_cast<void*>(Pattern::Address(Hooks[i]));MH_DisableHook(address);MH_RemoveHook(address);}
    status.store("Cube visual recipe hooks could not be installed.");return false;
}
bool QuestJournalSupported(){return installed.load()&&QuestNative::ready.load();}
bool Installed(){return installed.load();}
void SetMapPrepare(MapPrepare handler){mapPrepare.store(handler);}
const char* Status(){return status.load();}
void Uninstall() {
    if(!installed.exchange(false))return;
    ShutdownQuestMatcher();QuestJournal::SetProofRecovery(nullptr);QuestJournal::SetRewardClaim(nullptr);
    for(auto rva:Hooks)MH_QueueDisableHook(reinterpret_cast<void*>(Pattern::Address(rva)));
    if(MH_ApplyQueued()==MH_OK)for(auto rva:Hooks)MH_RemoveHook(reinterpret_cast<void*>(Pattern::Address(rva)));
}
}
