#include "ExtendedActs.h"
#include "CubeMap.h"
#include "ExtendedActsPatchPlan.h"
#include "ExtendedActsProgress.h"
#include "ExtendedActsTransition.h"
#include "RestartGame.h"
#include "LegacyTransmogData.h"
#include "../Pattern.h"
#include "../d2/Reclass.h"
#include "../json.hpp"
#include <Windows.h>
#include <shellapi.h>
#include <MinHook.h>
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <mutex>
#include <intrin.h>
#include <list>
#pragma comment(lib,"shell32.lib")

namespace ExtendedActs {
namespace {
template<class T>T At(uint32_t rva){return reinterpret_cast<T>(Pattern::Address(rva));}
template<class T>T Field(const void* p,size_t offset=0){T value{};SIZE_T n=0;if(p)ReadProcessMemory(GetCurrentProcess(),static_cast<const uint8_t*>(p)+offset,&value,sizeof(value),&n);return n==sizeof(value)?value:T{};}
std::atomic<bool> installed{false};
std::atomic<bool> requested{false};
std::atomic<const char*> message{"Act VI is not configured for this mod."};
std::mutex selectionMutex;
std::filesystem::path configuredData;
nlohmann::json configuredManifest;
bool configured=false,compileText=false,pendingRestart=false;
std::list<std::string> configurationMessages;
void ConfigurationMessage(std::string text){configurationMessages.push_back(std::move(text));message=configurationMessages.back().c_str();}
uint8_t* storage=nullptr;
std::vector<Native::Edit> edits;
std::vector<uint32_t> hooksCreated;
std::array<Native::QuestLabels,6> labels{};
std::array<int,9> levels{};
using Factory=void*(__fastcall*)(void*,void*,void*);
using Description=void(__fastcall*)(int,void*);
Factory originalWaypointFactory=nullptr;
Description originalDescription=nullptr;
using Modal=void(__fastcall*)(void*);
Modal originalCreditsOpen=nullptr,originalCreditsClose=nullptr;
using Screen=void(__fastcall*)(uint64_t);
Screen originalScreen=nullptr;
using Tile=void(__fastcall*)(void*,uint64_t,int,int,int,int);
Tile originalTile=nullptr;
constexpr std::array<uint32_t,6> Hooks{0x1531a0,0x147a70,0xc32320,0xc32560,0xb9a720,0x26e880};
std::mutex transitionMutex;
Transition::Flow transition;
struct BitBuffer {uint8_t* data;uint64_t bits;};

bool Flag(BitBuffer* p,unsigned quest,unsigned bit){return p&&p->data&&p->bits>=768 && quest<48 && bit<16 && (p->data[quest*2+bit/8]&(1u<<(bit%8)));}
bool SetFlag(BitBuffer* p,unsigned quest,unsigned bit){if(Flag(p,quest,bit))return false;p->data[quest*2+bit/8]|=uint8_t(1u<<(bit%8));return true;}
void __fastcall TileHook(void* room,uint64_t tile,int type,int x,int y,int flags) {
    // An exit needs both Warp art metadata and a linked destination. Maze
    // templates still contain stairs in this waypoint-only prototype. Native
    // tile construction otherwise asserts while finding their room link.
    // Keep connected exits and every tile outside Act VI entirely native.
    if(installed.load(std::memory_order_relaxed) && (type==10 || type==11)) {
        const unsigned index=unsigned(tile>>20)&63;
        if(index<8) {
            const auto level=Field<void*>(room,0x90);
            const int id=Field<int>(level,0x1f8);
            if(id>=levels.front() && id<=levels.back()) {
                const auto table=Field<uint8_t*>(At<void*>(0x1c9fd28));
                const int count=Field<int>(At<void*>(0x1c9fd10));
                if(table && id>=0 && id<count && Field<int>(table+size_t(id)*0x9c,0x48+4*index)==0)return;
            }
        }
    }
    originalTile(room,tile,type,x,y,flags);
}
void SyncProgress(D2UnitStrc* player,BitBuffer* flags,unsigned difficulty) {
    auto client=Field<uint8_t*>(player->pPlayerData,0xf0);if(!client)return;
    const auto before=Field<uint16_t>(client,0xa);
    const auto after=Progress::Complete(before,difficulty,Flag(flags,Progress::Baal,0),Flag(flags,Progress::FinalQuest,0));
    if(after!=before)memcpy(client+0xa,&after,sizeof(after));
}
void ReturnFinished(bool success){if(!success){std::lock_guard lock(transitionMutex);transition.Finish();message="Act VI credits return cancelled: the same offline character did not become ready.";}}
void EndingReached(uintptr_t caller,uint64_t screen){
    // Only the native expansion-ending notification arms a credits return.
    // Opening Credits from the main menu must never start a character.
    if(screen!=10 || caller!=Pattern::Address(0x148adb) ||
       Field<int>(At<void*>(0x1bfa31c))!=0 || Field<uint8_t>(At<void*>(0x18882e8))==2)return;
    auto client=Field<D2ClientStrc*>(At<void*>(0x1d637f0));
    auto game=Field<D2GameStrc*>(client,0x230);
    if(!game || Field<uint32_t>(game,0x158)!=1)return;
    std::lock_guard lock(transitionMutex);
    if(transition.game==reinterpret_cast<uintptr_t>(game) && transition.difficulty==Field<uint8_t>(game,0x104))transition.Ending();
}
void __fastcall RequestScreen(uint64_t screen){EndingReached(reinterpret_cast<uintptr_t>(_ReturnAddress()),screen);originalScreen(screen);}
void __fastcall CreditsOpen(void* panel) {
    {std::lock_guard lock(transitionMutex);transition.Open();}
    originalCreditsOpen(panel);
}
void __fastcall CreditsClose(void* panel) {
    originalCreditsClose(panel);
    std::array<char,16> name{};int difficulty=0;
    {std::lock_guard lock(transitionMutex);if(!transition.Close(GetTickCount64()))return;name=transition.hero;difficulty=int(transition.difficulty);}
    // Some endings retain a live game; the authoritative tick moves that hero.
    // Normal offline endings have already saved/exited and need native reentry.
    if(Field<uint8_t>(At<void*>(0x1dc76f8))==1 && Field<void*>(At<void*>(0x1d637f0)))return;
    if(!RestartGame::AfterCredits(name.data(),difficulty,&ReturnFinished))ReturnFinished(false);
}
void* __fastcall WaypointFactory(void* context,void* layout,void* parent) {
    auto result=originalWaypointFactory(context,layout,parent);
    // The native 0x1c0-byte panel already has this unused final DWORD.
    if(result)*reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(result)+0x1bc)=40;
    return result;
}
void __fastcall Describe(int quest,void* result) {
    if(quest>=42 && quest<48) {
        auto buffer=Field<BitBuffer*>(At<void*>(0x1d60c28));
        // Custom objectives have no stock scripted-quest status packet.
        // Resolve their active state from the same saved native flags.
        *At<uint8_t*>(0x1d60d10+quest)=Flag(buffer,quest,15)?1:0;
    }
    originalDescription(quest,result);
}
using Rows=std::vector<std::vector<std::string>>;
Rows Table(const std::filesystem::path& path) {
    std::ifstream file(path);if(!file)throw std::runtime_error("Missing Act VI table");
    Rows result;std::string line;
    while(std::getline(file,line)){
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        if(result.empty() && line.starts_with("\xef\xbb\xbf"))line.erase(0,3);
        std::vector<std::string> row;size_t begin=0;
        do{const auto end=line.find('\t',begin);row.push_back(line.substr(begin,end==line.npos?end:end-begin));if(end==line.npos)break;begin=end+1;}while(true);
        result.push_back(std::move(row));
    }return result;
}
size_t Column(const Rows& table,const char* name) {
    if(table.empty())throw std::runtime_error("Empty Act VI table");
    auto found=std::find(table[0].begin(),table[0].end(),name);
    if(found==table[0].end())throw std::runtime_error("Missing Act VI column");
    return size_t(found-table[0].begin());
}
enum class Config {Disabled,Enabled,Invalid,NeedsText};
enum class DataGroup {All,Launch,Layouts};
struct NeedsCompilation {};
void ValidateCompiledActs(const std::filesystem::path& data,uint64_t loadedActs,const std::array<int,9>& areas){
    auto read=[&](const char* name,size_t stride){
        std::ifstream input(data/"global/excel"/(std::string(name)+".bin"),std::ios::binary|std::ios::ate);
        if(!input || input.tellg()<4 || input.tellg()>64*1024*1024)throw std::runtime_error(std::string("Missing compiled Act VI table: ")+name+".bin");
        std::vector<uint8_t> bytes(size_t(input.tellg()));input.seekg(0);uint32_t count=0;
        if(!input.read(reinterpret_cast<char*>(bytes.data()),bytes.size()))throw std::runtime_error("Unreadable compiled Act VI table");
        memcpy(&count,bytes.data(),4);
        if(!count || count>4096 || bytes.size()!=4+size_t(count)*stride)throw std::runtime_error("Unsupported compiled Act VI table layout");
        return bytes;
    };
    const auto acts=read("actinfo",0x8c);uint32_t count=0;memcpy(&count,acts.data(),4);
    if(count!=loadedActs)throw std::runtime_error("ActInfo.bin does not match the native loaded act count");
    for(size_t i=0;i<count;++i){uint32_t id=0;memcpy(&id,acts.data()+4+i*0x8c,4);if(id!=i+1)throw std::runtime_error("Compiled ActInfo order is invalid");}
    const auto levels=read("levels",0x184);memcpy(&count,levels.data(),4);
    for(size_t n=0;n<areas.size();++n){
        if(size_t(areas[n])>=count)throw std::runtime_error("Compiled Act VI level is missing");
        const auto row=levels.data()+4+size_t(areas[n])*0x184;uint32_t id=0;memcpy(&id,row,4);
        if(id!=areas[n] || row[7]!=5 || row[0xe8]!=39+n)throw std::runtime_error("Compiled Act VI level/waypoint does not match its manifest");
    }
}
bool SelectData(const std::filesystem::path& data,const nlohmann::json& config,bool enabled,bool canCompile=true,DataGroup group=DataGroup::All) {
    struct Replacement {std::filesystem::path path;std::string before,after;};
    std::vector<Replacement> replacements;
    auto read=[](const std::filesystem::path& path){std::ifstream f(path,std::ios::binary);if(!f){const auto name=path.u8string();throw std::runtime_error("Act VI data missing: "+std::string(reinterpret_cast<const char*>(name.data()),name.size()));}return std::string(std::istreambuf_iterator<char>(f),{});};
    auto hash=[](const std::string& text){return LegacyTransmog::Data::Hash({reinterpret_cast<const uint8_t*>(text.data()),text.size()});};
    auto path=[&](const std::string& relative){auto p=std::filesystem::path(relative);if(p.is_absolute() || p.has_root_name())throw std::runtime_error("Invalid Act VI path");for(const auto& part:p)if(part=="..")throw std::runtime_error("Invalid Act VI path");return data/p;};
    std::set<std::string> layouts;
    if(config.contains("native_layouts"))for(const auto& name:config.at("native_layouts"))layouts.insert(name.get<std::string>());
    for(const auto& file:config.at("switch_files")) {
        const auto relative=file.at("path").get<std::string>();
        const bool layout=layouts.contains(relative);
        if((group==DataGroup::Launch && layout) || (group==DataGroup::Layouts && !layout))continue;
        const auto target=path(file.at("path"));
        // Theme variants are optional installed content. In particular, modern
        // Pandemonium packages no longer ship the old D2RLAN theme folder.
        // Required global layouts and existing edited themes remain validated.
        const bool theme=relative.starts_with("D2RLAN/UI Theme/") || relative.starts_with("Pandemonium/UI Theme/");
        if(layout && theme && !std::filesystem::exists(target))continue;
        const auto before=read(target);
        const uint64_t on=file.at("enabled_hash"),off=file.at("disabled_hash");
        if(hash(before)!=on && hash(before)!=off)throw std::runtime_error("Act VI target was edited: "+relative);
        const auto after=read(path(file.at(enabled?"enabled":"disabled")));
        if(hash(after)!=(enabled?on:off))throw std::runtime_error("Act VI template mismatch: "+relative);
        if(before!=after)replacements.push_back({target,before,after});
    }
    auto write=[](const std::filesystem::path& target,const std::string& bytes){
        auto temporary=target;temporary+=L".act6-pending";
        {std::ofstream f(temporary,std::ios::binary|std::ios::trunc);f.write(bytes.data(),bytes.size());f.close();if(!f)return false;}
        return MoveFileExW(temporary.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
    };
    size_t applied=0;
    if(!replacements.empty() && !canCompile)throw NeedsCompilation{};
    for(;applied<replacements.size();++applied)if(!write(replacements[applied].path,replacements[applied].after)){
        while(applied){--applied;write(replacements[applied].path,replacements[applied].before);}return false;
    }
    return true;
}
Config ConfigureData(const std::filesystem::path& data,const nlohmann::json& config,bool want,bool txt,uint64_t loadedActs) {
    requested=want;
    if(config.value("version",0)!=1 || !config.value("enabled",false))throw std::runtime_error("Act VI manifest is disabled or has an unsupported version");
    if(config.value("mod",std::string{})!="RMD-MP" || config.value("act",0)!=6 || config.value("first_waypoint",0)!=39)throw std::runtime_error("Act VI manifest identity is invalid");
    if(loadedActs && loadedActs!=5 && loadedActs!=6)throw std::runtime_error("Unexpected native ActInfo count: "+std::to_string(loadedActs));
    auto list=config.at("levels").get<std::vector<int>>();if(list.size()!=9 || list[0]<240 || list[8]>=1024 || config.at("town").get<int>()!=list[0])throw std::runtime_error("Act VI level range is invalid");
    for(size_t i=0;i<9;++i){if(list[i]!=list[0]+int(i))throw std::runtime_error("Act VI levels must be contiguous");levels[i]=list[i];}
    const auto text=config.at("quest_labels");if(text.size()!=6)throw std::runtime_error("Act VI requires six quest labels");
    for(size_t q=0;q<6;++q){
        for(const char* field:{"title","objective","completed"})if(text[q].at(field).get<int>()<1 || text[q].at(field).get<int>()>=65535)throw std::runtime_error("Invalid Act VI string ID");
        labels[q]={uint16_t(text[q]["title"].get<int>()),uint16_t(text[q]["objective"].get<int>()),uint16_t(text[q]["completed"].get<int>())};
    }
    // The startup injector pauses at States, after ActInfo has compiled. Match
    // the current process to its loaded tables, not the next launch's checkbox.
    const bool running=loadedActs ? loadedActs==6 : want;
    if(!txt){
        // An already compiled package can run without any authoring TXT
        // templates. The loaded tables determine this process's native mode.
        // Switching modes still needs a matching five/six-act BIN package;
        // never pretend that changing a checkbox changes compiled tables.
        if(!loadedActs)throw std::runtime_error("Native ActInfo is not available for compiled-data validation");
        if(running)ValidateCompiledActs(data,loadedActs,levels);
        if(!SelectData(data,config,running,true,DataGroup::Layouts))throw std::runtime_error("Act VI layout selection could not be written");
        configuredData=data;configuredManifest=config;configured=true;compileText=false;pendingRestart=running!=want;
        if(pendingRestart)ConfigurationMessage(running ?
            "Act VI remains active: this package contains six-act BIN tables. Install the five-act compiled package to disable it after restarting." :
            "Act VI requested, but this package contains five-act BIN tables. Install the six-act compiled package and restart to activate it.");
        else ConfigurationMessage(running ? "Act VI uses the validated six-act BIN package; TXT compilation is not required." : "Act VI disabled. Five-act BIN progression is active.");
        return running?Config::Enabled:Config::Disabled;
    }
    if(!SelectData(data,config,want,txt,DataGroup::Launch) ||
       !SelectData(data,config,running,true,DataGroup::Layouts))throw std::runtime_error("Act VI data selection could not be written");
    // Validate the enabled templates even when this process is still five-act.
    // Changing already compiled native tables in place is deliberately avoided.
    const auto enabled=data/"Pandemonium/ActVI/enabled/global/excel";
    auto acts=Table(enabled/"actinfo.txt");const auto act=Column(acts,"act");
    size_t count=0;for(size_t row=1;row<acts.size();++row)if(acts[row].size()>act&&!acts[row][act].empty()){++count;if(std::stoi(acts[row][act])!=int(count))throw std::runtime_error("Act VI ActInfo order is invalid");}
    if(count!=6)throw std::runtime_error("Act VI ActInfo template requires six rows");
    const auto areas=Table(enabled/"levels.txt");const auto id=Column(areas,"Id"),areaAct=Column(areas,"Act"),waypoint=Column(areas,"Waypoint");
    std::set<int> seen;
    for(size_t row=1;row<areas.size();++row){const auto& fields=areas[row];if(fields.size()<=waypoint || fields[id].empty())continue;
        const int number=std::stoi(fields[id]);if(number<levels[0]||number>levels[8])continue;
        if(fields[areaAct]!="5" || std::stoi(fields[waypoint])!=39+number-levels[0] || !seen.insert(number).second)throw std::runtime_error("Act VI level/waypoint template is invalid");
    }
    if(seen.size()!=9)throw std::runtime_error("Act VI level templates are incomplete");
    configuredData=data;configuredManifest=config;configured=true;compileText=txt;pendingRestart=running!=want;
    if(!running){
        ConfigurationMessage(std::string(pendingRestart ? "Act VI data is prepared. Fully restart D2R to activate it; this process retains five-act progression." : "Act VI disabled. Five-act progression is active. Restart D2R after changing this option.")+" Native ActInfo at HUD startup: "+std::to_string(loadedActs)+" rows.");
        return Config::Disabled;
    }
    return Config::Enabled;
}
Config Configure() {
    std::lock_guard lock(selectionMutex);
    wchar_t exe[32768]{};char mod[128]{};SIZE_T n=0;
    if(!GetModuleFileNameW(nullptr,exe,DWORD(std::size(exe))) ||
       !ReadProcessMemory(GetCurrentProcess(),At<void*>(0x1bf084f),mod,sizeof(mod),&n) || n!=sizeof(mod))return Config::Invalid;
    mod[127]=0;std::string name=mod;std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return char(tolower(c));});
    if(name!="rmd-mp")return Config::Disabled;
    const auto data=std::filesystem::path(exe).parent_path()/"Mods"/mod/(std::string(mod)+".mpq")/"data";
    std::ifstream input(data/"Pandemonium/act6.json");if(!input)return Config::Disabled;
    try {
        const auto config=nlohmann::json::parse(input);
        std::ifstream preferences(std::filesystem::current_path()/("HUDConfig_"+std::string(mod)+".json"));
        if(preferences)requested=nlohmann::json::parse(preferences,nullptr,true,true).value("ActVI",false);
        int argc=0;auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);bool txt=false;
        if(argv){for(int i=1;i<argc;++i)if(!_wcsicmp(argv[i],L"-txt"))txt=true;LocalFree(argv);}
        return ConfigureData(data,config,requested.load(),txt,Field<uint64_t>(At<void*>(0x1c9ea88)));
    }catch(const NeedsCompilation&){return Config::NeedsText;}
    catch(const std::exception& error){ConfigurationMessage(std::string("Act VI unavailable: ")+error.what());return Config::Invalid;}
}
bool Supported() {
    for(const auto& signature:Contracts::Signatures){
        std::vector<uint8_t> bytes(signature.size);SIZE_T n=0;
        if(!ReadProcessMemory(GetCurrentProcess(),At<void*>(signature.rva),bytes.data(),bytes.size(),&n) || n!=bytes.size() || LegacyTransmog::Data::Hash(bytes)!=signature.hash)return false;
    }return true;
}
bool Write(uint32_t rva,std::span<const uint8_t> bytes) {
    DWORD before=0;auto where=At<void*>(rva);
    if(!VirtualProtect(where,bytes.size(),PAGE_EXECUTE_READWRITE,&before))return false;
    memcpy(where,bytes.data(),bytes.size());FlushInstructionCache(GetCurrentProcess(),where,bytes.size());
    DWORD ignored=0;VirtualProtect(where,bytes.size(),before,&ignored);return true;
}
void Release() {
    for(auto rva:hooksCreated)MH_QueueDisableHook(At<void*>(rva));MH_ApplyQueued();
    for(auto rva:hooksCreated)MH_RemoveHook(At<void*>(rva));hooksCreated.clear();
    if(installed.load() && storage) {
        // Restore ownership before restoring the five-entry native references.
        // This path is used only after native tables are gone; active sessions
        // keep the pinned module and its six-entry arrays alive.
        auto caches=reinterpret_cast<Native::PresetCache*>(storage+Native::PresetCacheOffset);
        memcpy(At<void*>(0x1ca0b30),caches,5*sizeof(*caches));
        if(caches[5].allocator && caches[5].buffer)
            At<void(__fastcall*)(void*)>(0x25b920)(&caches[5]);
    }
    for(auto it=edits.rbegin();it!=edits.rend();++it)Write(it->rva,it->before);
    edits.clear();if(storage)VirtualFree(storage,0,MEM_RELEASE);storage=nullptr;
}
}
bool Activate() {
    if(!CubeMap::SummonLifecycleAvailable()){message="Act VI unavailable: authoritative game and character lifecycle hooks are required.";return false;}
    // ActInfo normally exists at the States startup gate. Six loaded rows are
    // valid; live characters or a five-act snapshot cannot use six-act hooks.
    const auto loaded=Field<uint64_t>(At<void*>(0x1c9ea88));
    if((loaded && loaded!=6) || (loaded==6 && !Field<void*>(At<void*>(0x1c9ea80))) ||
       Field<uint8_t>(At<void*>(0x1dc76f8))==1){message="Act VI unavailable: native tables do not match six acts, or a game is already active.";return false;}
    if(!Supported()){message="Act VI unavailable: native executable contracts do not match.";return false;}
    SYSTEM_INFO system{};GetSystemInfo(&system);const auto step=uintptr_t(system.dwAllocationGranularity),base=Pattern::Address(0);
    for(uintptr_t delta=0x3000000;delta<0x40000000&&!storage;delta+=step)
        storage=static_cast<uint8_t*>(VirtualAlloc(reinterpret_cast<void*>((base+delta)&~(step-1)),Native::StorageBytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if(!storage){message="Act VI unavailable: native table allocation failed.";return false;}
    auto plan=Native::BuildPlan(At<uint8_t*>(0),storage,labels,levels.front());
    if(!plan){Release();message="Act VI unavailable: native patch relocation failed.";return false;}
    DWORD ignored=0;if(!VirtualProtect(storage+Native::CodeOffset,Native::StorageBytes-Native::CodeOffset,PAGE_EXECUTE_READ,&ignored)){Release();message="Act VI unavailable: native helper protection failed.";return false;}
    FlushInstructionCache(GetCurrentProcess(),storage+Native::CodeOffset,Native::StorageBytes-Native::CodeOffset);
    auto init=MH_Initialize();if(init!=MH_OK && init!=MH_ERROR_ALREADY_INITIALIZED){Release();message="Act VI unavailable: hook initialization failed.";return false;}
    void* replacements[]{reinterpret_cast<void*>(&WaypointFactory),reinterpret_cast<void*>(&Describe),reinterpret_cast<void*>(&CreditsOpen),reinterpret_cast<void*>(&CreditsClose),reinterpret_cast<void*>(&RequestScreen),reinterpret_cast<void*>(&TileHook)};
    void** originals[]{reinterpret_cast<void**>(&originalWaypointFactory),reinterpret_cast<void**>(&originalDescription),reinterpret_cast<void**>(&originalCreditsOpen),reinterpret_cast<void**>(&originalCreditsClose),reinterpret_cast<void**>(&originalScreen),reinterpret_cast<void**>(&originalTile)};
    static_assert(std::size(replacements)==Hooks.size() && std::size(originals)==Hooks.size());
    for(size_t i=0;i<Hooks.size();++i){
        if(MH_CreateHook(At<void*>(Hooks[i]),replacements[i],originals[i])!=MH_OK){Release();message="Act VI unavailable: native hooks could not be created.";return false;}
        hooksCreated.push_back(Hooks[i]);
    }
    for(const auto& edit:*plan){if(!Write(edit.rva,edit.after)){Release();message="Act VI unavailable: native edit failed.";return false;}edits.push_back(edit);}
    bool ready=true;for(auto rva:Hooks)if(MH_QueueEnableHook(At<void*>(rva))!=MH_OK)ready=false;
    if(!ready || MH_ApplyQueued()!=MH_OK){Release();message="Act VI unavailable: native hooks could not be enabled.";return false;}
    HMODULE pinned=nullptr;GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&Install),&pinned);
    installed=true;CubeMap::SetActTown(levels[0]);CubeMap::SetActTick(&Tick);message="Act VI prototype: Dawn's Refuge, native quest/waypoint tabs and saved quest flags. Gameplay acceptance pending.";return true;
}
bool Install() {
    if(installed.load())return true;
    const auto config=Configure();if(config==Config::Disabled)return true;
    if(config==Config::NeedsText){message="Act VI unavailable: launch the prototype with -txt to compile all six-act tables.";return false;}
    if(config!=Config::Enabled)return false;
    const bool ready=Activate();
    if(ready && pendingRestart)message=compileText ?
        "Act VI remains active for this process. Fully restart D2R to apply the disabled option." :
        "Act VI remains active: this package contains six-act BIN tables. Install the five-act compiled package and restart to disable it.";
    return ready;
}
void Tick(D2GameStrc* game) {
    if(!installed.load() || !game || game->dwGameFrame%5 || Field<int>(At<void*>(0x1c9ea88))!=6)return;
    const auto difficulty=Field<uint8_t>(game,0x104);if(difficulty>=3)return;
    std::set<D2ClientStrc*> seen;
    for(auto client=game->pClientList;client&&seen.size()<64;client=client->pNext){
        if(!seen.insert(client).second)break;
        auto player=client->pPlayer;
        if(client->pGame!=game || client->dwClientState!=4 || !player || player->dwUnitType || !player->pPlayerData || player->dwUnitId!=client->dwUnitGUID)continue;
        auto flags=reinterpret_cast<BitBuffer*>(player->pPlayerData->pQuestData[difficulty]);
        if(!flags || !flags->data || flags->bits<768)continue;
        SyncProgress(player,flags,difficulty);
        auto waypoint=Field<uint16_t*>(player->pPlayerData,0x58+8*difficulty);if(!waypoint || waypoint[0]!=0x102)continue;
        if(Flag(flags,40,0))At<void(__fastcall*)(void*,uint16_t)>(0x259250)(waypoint,39);
        auto room=At<void*(__fastcall*)(D2UnitStrc*)>(0x209530)(player);
        auto levelOf=[](void* r){return Field<int>(Field<void*>(Field<void*>(r,0x18),0x90),0x1f8);};
        int level=levelOf(room);
        const bool offline=game->nClients==1 && Field<int>(At<void*>(0x1bfa31c))==0 && Field<uint8_t>(At<void*>(0x18882e8))!=2 && Field<D2ClientStrc*>(At<void*>(0x1d637f0))==client;
        if(offline && memchr(player->pPlayerData->szName,0,16)) {
            bool move=false;
            {std::lock_guard lock(transitionMutex);
                transition.Observe(player->pPlayerData->szName,difficulty,reinterpret_cast<uintptr_t>(game),game->dwInitSeed,game->dwGameFrame,Flag(flags,40,0)||Flag(flags,40,1));
                if(transition.Due(player->pPlayerData->szName,difficulty,GetTickCount64())) {
                    if(level==levels[0])transition.Finish();
                    else if(level>0 && game->dwGameFrame%25==0){++transition.attempts;move=true;}
                }
            }
            if(move){At<void(__fastcall*)(D2GameStrc*,D2UnitStrc*,int,int)>(0x2e16d0)(game,player,levels[0],11);room=At<void*(__fastcall*)(D2UnitStrc*)>(0x209530)(player);level=levelOf(room);}
        }
        if(!Flag(flags,40,0))continue;
        if(level<levels[0] || level>levels[8])continue;
        // The content prototype exposes its cloned areas for exploration.
        // These are explicit placeholder exploration quests, without rewards.
        bool changed=false;
        for(unsigned q=0;q<6;++q){changed|=SetFlag(flags,42+q,15);if(level==levels[q+1])changed|=SetFlag(flags,42+q,0);}
        SyncProgress(player,flags,difficulty);
        for(uint16_t w=39;w<48;++w)At<void(__fastcall*)(void*,uint16_t)>(0x259250)(waypoint,w);
        if(changed){
            std::array<uint8_t,103> packet{};packet[0]=0x28;packet[1]=6;memcpy(packet.data()+7,flags->data,96);
            void* range[]{packet.data(),packet.data()+packet.size()};
            At<void(__fastcall*)(void*,uint32_t,void**)>(0x422ac0)(At<void*>(0x18682b0),client->dwClientId,range);
        }
    }
}
void Uninstall(){if(!installed.load() || Field<uint64_t>(At<void*>(0x1c9ea88)))return;CubeMap::SetActTick(nullptr);CubeMap::SetActTown(0);Release();installed=false;}
const char* Status(){return message.load();}
void SetRequested(bool value){
    std::lock_guard lock(selectionMutex);
    if(requested.exchange(value)==value || !configured)return;
    try{
        if(!compileText){
            pendingRestart=(Field<uint64_t>(At<void*>(0x1c9ea88))==6)!=value;
            message=pendingRestart ? "Act VI preference saved. Install matching five/six-act BIN tables and restart to change the compiled act count." :
                value ? "Act VI preference matches the loaded six-act BIN package." : "Act VI disabled. Five-act BIN progression is active.";
            return;
        }
        // Tables/strings are startup resources; prepare them now, while leaving
        // current-process quest/waypoint layouts and hooks in their current mode.
        if(!SelectData(configuredData,configuredManifest,value,compileText,DataGroup::Launch))throw std::runtime_error("Could not write startup data");
        message="Act VI preference prepared. Fully restart D2R to apply it.";
    }catch(const NeedsCompilation&){message="Act VI preference requires -txt. Relaunch with -txt to prepare its tables, then restart D2R.";}
    catch(const std::exception& error){ConfigurationMessage(std::string("Act VI preference could not be prepared: ")+error.what());}
}
bool Requested(){return requested.load();}
bool Active(){return installed.load();}
}
