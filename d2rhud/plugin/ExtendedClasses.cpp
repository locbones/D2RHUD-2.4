#include "ExtendedClasses.h"
#include "ExtendedClassesData.h"
#include "LegacyTransmogData.h"
#include "../Pattern.h"
#include <Windows.h>
#include <tlhelp32.h>
#include <MinHook.h>
#include "../json.hpp"
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <vector>

namespace ExtendedClasses {
namespace {
template<class T> T Native(uint32_t rva) { return reinterpret_cast<T>(Pattern::Address(rva)); }
std::atomic<bool> installed{false};
std::atomic<const char*> status{"Additional player classes are disabled for this mod."};
constexpr char Model[]="data/hd/character/player/warlock.json";
using Registry=void*(__fastcall*)();
using ModelLookup=const char*(__fastcall*)(void*,int,int);
Registry originalRegistry=nullptr;
ModelLookup originalModel=nullptr;
std::once_flag registryOnce;
std::atomic<bool> registryReady{false};
uint8_t* storage=nullptr;
struct PresentationBranch {uint32_t rva,size,skip;bool r13;};
// Four seven-entry string arrays in the native skill panel. Warlock has no
// skill tree in this class-only port. Skip unavailable labels instead of
// indexing adjacent object fields or changing the actual player class.
constexpr std::array<PresentationBranch,4> PresentationBranches{{
    {0xbc4da4,12,0xbc4ded,false},
    {0xbc4e9d,11,0xbc4f0d,true},
    {0xbc4f52,11,0xbc4fc0,true},
    {0xbc5005,11,0xbc5070,true},
}};
std::array<Data::Patch,Data::Patches.size()+PresentationBranches.size()+2> runtimePatches{};
struct Map { uint64_t buckets; uintptr_t* heads; uint64_t count; float load; };
struct CreationInfo {
    uint32_t name,description;
    uint8_t classId;uint8_t padding[7]{};
    const char* hoverSound;const char* selectSound;
};
struct CreationNode {
    CreationNode *parent,*left,*right;
    uint8_t color;uint8_t padding[7]{};
    const char* key;uint64_t length;
    CreationInfo info;
};
static_assert(sizeof(CreationNode)==0x50 && offsetof(CreationNode,info)==0x30);
bool ExtendCreationInfo() {
    auto header=Native<CreationNode*>(0x1baed88);
    auto count=Native<uint64_t*>(0x1baeda8);
    if(*count!=7 || !header->parent)return false;
    struct Hint {CreationNode* parent;CreationNode** link;};
    struct Key {const char* text;uint64_t length;};
    const Key key{"Warlock",7};Hint hint{};
    // Use the native map's insertion search and red/black rebalancer. Its
    // registry also supplies class IDs to the Create button, not just labels.
    Native<Hint*(__fastcall*)(void*,Hint*,CreationNode*,const Key*)>(0xc2e740)(nullptr,&hint,header,&key);
    if(!hint.parent || !hint.link)return false;
    auto allocator=Native<void*(__fastcall*)()>(0xacdd80)();
    if(!allocator)return false;
    auto allocate=reinterpret_cast<void*(__fastcall*)(void*,uint64_t,uint64_t)>((*reinterpret_cast<uintptr_t**>(allocator))[1]);
    auto node=static_cast<CreationNode*>(allocate(allocator,sizeof(CreationNode),16));
    if(!node)return false;
    memset(node,0,sizeof(*node));node->parent=hint.parent;node->key="Warlock";node->length=7;
    node->info={27566,27568,7,{},"",""};
    ++*count;
    if(hint.parent==header)header->parent=header->left=header->right=node;
    else {
        *hint.link=node;
        if(hint.parent==header->left && hint.link==&hint.parent->left)header->left=node;
        if(hint.parent==header->right && hint.link==&hint.parent->right)header->right=node;
    }
    Native<void(__fastcall*)(void*,CreationNode*)>(0xc2e9d0)(nullptr,node);
    return true;
}
// Native allocator ownership matters: the game's registered map destructor
// frees these nodes and strings, so neither the HUD CRT nor VirtualAlloc can
// supply them. Existing entries and their identities remain intact.
void ExtendRegistry(Map* map) {
    if(!map || !map->heads || !map->buckets || map->count!=7)return;
    using Hash=uint64_t(__fastcall*)(const char*,uint64_t);
    using Allocator=void*(__fastcall*)();
    using Allocate=void*(__fastcall*)(void*,uint64_t,uint64_t);
    using StringInit=void(__fastcall*)(void*,const char*,uint64_t);
    auto allocator=Native<Allocator>(0xacdd80)();
    if(!allocator)return;
    auto allocate=reinterpret_cast<Allocate>((*reinterpret_cast<uintptr_t**>(allocator))[1]);
    auto node=static_cast<uint8_t*>(allocate(allocator,0x38,16));
    if(!node)return;
    memset(node,0,0x38);
    Native<StringInit>(0x5a8c0)(node+8,"Warlock",7);
    const auto bucket=Native<Hash>(0x5d3e0)("Warlock",7)&(map->buckets-1);
    *reinterpret_cast<uintptr_t*>(node)=map->heads[bucket];
    *reinterpret_cast<int32_t*>(node+0x30)=7;
    map->heads[bucket]=reinterpret_cast<uintptr_t>(node);
    ++map->count;
    registryReady=true;
}
void* __fastcall RegistryHook() {
    auto map=originalRegistry();
    std::call_once(registryOnce,[&]{ExtendRegistry(static_cast<Map*>(map));});
    return map;
}
const char* __fastcall ModelHook(void* manager,int type,int id) {
    // The native player model vector contains seven inline 48-byte paths.
    // Intercept before its unchecked class index; item/monster lookups retain
    // their normal lookup and the existing cube visual hooks.
    // The caller adds data/hd/character/player/ and the file extension.
    if(type==0 && id==7)return "warlock";
    return originalModel(manager,type,id);
}
bool Read(uintptr_t address,void* out,size_t size) {
    SIZE_T n=0;return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),out,size,&n) && n==size;
}
bool Supported() {
    for(const auto& s:Data::Signatures) {
        std::vector<uint8_t> bytes(s.size);
        if(!Read(Pattern::Address(s.rva),bytes.data(),bytes.size()) || LegacyTransmog::Data::Hash(bytes)!=s.hash)return false;
    }
    return true;
}
bool Write(uint32_t rva,const void* data,size_t size) {
    auto target=Native<void*>(rva);DWORD old=0;
    if(!VirtualProtect(target,size,PAGE_EXECUTE_READWRITE,&old))return false;
    memcpy(target,data,size);FlushInstructionCache(GetCurrentProcess(),target,size);
    DWORD unused=0;VirtualProtect(target,size,old,&unused);return true;
}
enum class Configuration { Disabled, Warlock, Invalid };
bool WarlockTable(const std::filesystem::path& excel,const char* name,size_t stride,size_t nameOffset){
    std::ifstream text(excel/(std::string(name)+".txt"));
    if(text){
        std::string line;unsigned rows=0;bool warlock=false;std::getline(text,line);
        while(std::getline(text,line)){
            auto field=line.substr(0,line.find('\t'));
            if(!field.empty()&&field.back()=='\r')field.pop_back();
            if(field.empty() || field=="Expansion" || field=="expansion")continue;
            if(rows==7 && field=="Warlock")warlock=true;
            ++rows;
        }
        return rows==8 && warlock;
    }
    // These three native BIN layouts retain the class display name. Check the
    // eighth row's identity rather than trusting a count or a manifest alone.
    std::ifstream binary(excel/(std::string(name)+".bin"),std::ios::binary|std::ios::ate);
    if(!binary || binary.tellg()!=std::streamoff(4+8*stride))return false;
    uint32_t count=0;binary.seekg(0);if(!binary.read(reinterpret_cast<char*>(&count),4) || count!=8)return false;
    std::array<char,8> title{};binary.seekg(4+7*stride+nameOffset);
    return bool(binary.read(title.data(),title.size())) && !memcmp(title.data(),"Warlock",8);
}
Configuration Config() {
    wchar_t exe[32768]{};char mod[128]{};
    if(!GetModuleFileNameW(nullptr,exe,32768) || !Read(Pattern::Address(0x1bf084f),mod,sizeof(mod)))return Configuration::Invalid;
    mod[127]=0;const std::string name=mod;
    if(name.empty())return Configuration::Disabled;
    if(name.find_first_of("/\\:")!=name.npos || name=="." || name=="..")return Configuration::Invalid;
    auto data=std::filesystem::path(exe).parent_path()/"Mods"/name/(name+".mpq")/"data";
    std::ifstream file(data/"Pandemonium/extended_classes.json");
    if(!file)return Configuration::Disabled;
    try {
        const auto config=nlohmann::json::parse(file);
        if(config.value("version",0)!=1 || !config.value("warlock",false))return Configuration::Invalid;
        // A class manifest cannot silently reinterpret a different eighth row.
        const auto excel=data/"global/excel";
        if(!WarlockTable(excel,"charstats",0xd0,0x20) || !WarlockTable(excel,"playerclass",0x34,0) || !WarlockTable(excel,"plrtype",0x24,0))return Configuration::Invalid;
        if(!std::filesystem::is_regular_file(data/(Model+5)))return Configuration::Invalid;
        return Configuration::Warlock;
    } catch(...) { return Configuration::Invalid; }
}
bool PrepareStorage() {
    SYSTEM_INFO info{};GetSystemInfo(&info);
    const auto base=Pattern::Address(0),step=uintptr_t(info.dwAllocationGranularity);
    for(uintptr_t delta=step;delta<0x40000000 && !storage;delta+=step)
        storage=static_cast<uint8_t*>(VirtualAlloc(reinterpret_cast<void*>(base+delta),0x2000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if(!storage)return false;
    // Preserve all inventory IDs. The archived inventory.txt inserts rows in
    // the middle; use the existing Necromancer grid for Warlock instead.
    memcpy(storage,Native<void*>(0x1bb3a70),7*8);
    const int32_t warlock[]{7,2};memcpy(storage+7*8,warlock,8);
    // Native preload records use movaps, requiring this 16-byte alignment.
    memcpy(storage+0x100,Native<void*>(0x1bb5950),11*16);
    const uintptr_t extra[]{reinterpret_cast<uintptr_t>(Model),0};memcpy(storage+0x100+11*16,extra,16);
    // The eighth name slot used to mean "no class". Retain that name in a
    // ninth slot while assigning the archived localized Warlock name to 7.
    memcpy(storage+0x300,Native<void*>(0x14c1fa8),8*4);
    const uint32_t names[]{27566,5383};memcpy(storage+0x300+7*4,names,8);
    // Native player foley indexes this table directly. Slot 7 was null and
    // crashes on creation-screen footstep events as well as in-game walking.
    // Use the existing Necromancer profile until Warlock audio is ported.
    const uint32_t profileRvas[]{0x17f9990,0x17f9bd0,0x17f9ab0,0x17fada0,0x17f9c60,0x17fb340,0x17fb2e0};
    uintptr_t profiles[8]{};
    if(!Read(Pattern::Address(0x14b7578),profiles,sizeof(profiles)))return false;
    for(size_t i=0;i<7;++i)if(profiles[i]!=Pattern::Address(profileRvas[i]))return false;
    if(profiles[7])return false;
    profiles[7]=profiles[2];memcpy(storage+0x400,profiles,sizeof(profiles));
    // Forty DWORD residency flags per class (two sets of twenty modes). The
    // original seven-class array ends immediately before the COF cache, so a
    // class-7 write used to overwrite its linked-list heads. Keep this array
    // on a separate writable page; the metadata/trampolines stay read/execute.
    memcpy(storage+0x1000,Native<void*>(0x1d590c0),7*40*4);
    std::copy(Data::Patches.begin(),Data::Patches.end(),runtimePatches.begin());
    for(size_t i=0;i<PresentationBranches.size();++i){
        const auto& branch=PresentationBranches[i];auto& patch=runtimePatches[Data::Patches.size()+i];
        patch.rva=branch.rva;patch.size=uint8_t(branch.size);
        memcpy(patch.before.data(),Native<void*>(branch.rva),branch.size);
        auto stub=storage+0x500+i*0x40;auto out=stub;
        if(branch.r13)*out++=0x41;
        *out++=0x83;*out++=branch.r13?0xfd:0xf8;*out++=7; // cmp r13d/eax,7
        *out++=0x0f;*out++=0x83; // jae past unavailable class label
        auto displacement=int32_t(Pattern::Address(branch.skip)-reinterpret_cast<uintptr_t>(out+4));
        memcpy(out,&displacement,4);out+=4;
        memcpy(out,patch.before.data(),branch.size);out+=branch.size;
        *out++=0xe9;
        displacement=int32_t(Pattern::Address(branch.rva+branch.size)-reinterpret_cast<uintptr_t>(out+4));
        memcpy(out,&displacement,4);
        patch.after.fill(0x90);patch.after[0]=0xe9;
        displacement=int32_t(reinterpret_cast<uintptr_t>(stub)-Pattern::Address(branch.rva+5));
        memcpy(patch.after.data()+1,&displacement,4);
    }
    // The stock header records the largest skill tree (30), while the body
    // writes only this class's skills. Warlock currently has zero. Record its
    // actual count so the loader does not skip thirty bytes of item data.
    const auto detour=[&](size_t index,uint32_t rva,uint8_t size,uint8_t* stub){
        auto& patch=runtimePatches[Data::Patches.size()+PresentationBranches.size()+index];
        patch.rva=rva;patch.size=size;memcpy(patch.before.data(),Native<void*>(rva),size);
        patch.after.fill(0x90);patch.after[0]=0xe9;
        const int32_t relative=int32_t(reinterpret_cast<uintptr_t>(stub)-Pattern::Address(rva+5));
        memcpy(patch.after.data()+1,&relative,4);
    };
    auto out=storage+0x600;
    const auto emit=[&](std::initializer_list<uint8_t> bytes){for(auto byte:bytes)*out++=byte;};
    const auto rel=[&](uintptr_t target){const int32_t d=int32_t(target-reinterpret_cast<uintptr_t>(out+4));memcpy(out,&d,4);out+=4;};
    detour(0,0x317bb7,7,out);
    emit({0x9c,0x41,0x80,0x7d,0x08,0x07,0x75});auto normal=out++; // pushfq; cmp class,7; jne
    emit({0x48,0x8b,0x05});rel(Pattern::Address(0x1c9faf0));
    emit({0x8b,0x40,0x1c,0x9d,0xe9});rel(Pattern::Address(0x317bbe));
    *normal=uint8_t(out-normal-1);
    emit({0x0f,0xb6,0x05});rel(Pattern::Address(0x1c9fb08));
    emit({0x9d,0xe9});rel(Pattern::Address(0x317bbe));

    // Compatibility for the class-only prototype's already-written saves:
    // header=30, no class skills, and JM immediately after the if marker.
    // Normalize the local reader count only; leave the file/checksum intact.
    out=storage+0x680;detour(1,0x31d918,9,out);
    std::array<uint8_t*,5> unchanged{};size_t conditions=0;
    const auto otherwise=[&](uint8_t condition){emit({condition});unchanged[conditions++]=out++;};
    emit({0x50,0x83,0xfe,0x07});otherwise(0x75); // push rax; cmp esi,7
    emit({0x40,0x80,0xff,0x1e});otherwise(0x75); // cmp dil,30
    emit({0x48,0x8b,0x05});rel(Pattern::Address(0x1c9faf0));
    emit({0x83,0x78,0x1c,0x00});otherwise(0x75);
    emit({0x49,0x8d,0x45,0x02,0x4c,0x39,0xf8});otherwise(0x77); // r13+2 <= end
    emit({0x66,0x41,0x81,0x7d,0x00,0x4a,0x4d});otherwise(0x75);
    emit({0x33,0xff,0x58,0x40,0x88,0xbc,0x24,0xa8,0x00,0x00,0x00,0xeb});auto normalized=out++;
    for(auto p:unchanged)*p=uint8_t(out-p-1);
    emit({0x58});*normalized=uint8_t(out-normalized-1);
    emit({0x40,0x84,0xff,0x0f,0x84});rel(Pattern::Address(0x31d9af));
    emit({0xe9});rel(Pattern::Address(0x31d921));
    FlushInstructionCache(GetCurrentProcess(),storage,0x1000);
    DWORD unused=0;return VirtualProtect(storage,0x1000,PAGE_EXECUTE_READ,&unused)!=FALSE;
}
int32_t Relative(uint32_t next,const void* value) { return int32_t(reinterpret_cast<uintptr_t>(value)-Pattern::Address(next)); }
struct Redirect {uint32_t displacement,next,target,offset;};
constexpr std::array<Redirect,10> Redirects{{
    {0x209e95,0x209e99,0x1bb3a70,0},
    {0x474a7e,0x474a82,0x1bb5950,0x100},
    {0x474a85,0x474a89,0x1bb5a00,0x100+12*16},
    {0xc049b0,0xc049b4,0x14c1fa8,0x300},
    {0xc1cdfc,0xc1ce00,0x14c1fa8,0x300},
    {0xc3910e,0xc39112,0x14c1fa8,0x300},
    {0xc3d734,0xc3d738,0x14c1fa8,0x300},
    {0x10a2ef,0x10a2f3,0x14b7578,0x400},
    {0xc0e77,0xc0e7b,0x1d590c0,0x1000},
    {0xc10ee,0xc10f2,0x1d590c0,0x1000},
}};
struct PausedThreads {
    std::array<HANDLE,512> handles{};std::array<bool,512> paused{};size_t count=0;
    ~PausedThreads(){for(size_t i=0;i<count;++i){if(paused[i])ResumeThread(handles[i]);CloseHandle(handles[i]);}}
    bool Pause() {
        HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);
        if(snapshot==INVALID_HANDLE_VALUE)return false;
        THREADENTRY32 e{};e.dwSize=sizeof(e);BOOL more=Thread32First(snapshot,&e);bool ok=more!=FALSE;
        while(more) {
            if(e.th32OwnerProcessID==GetCurrentProcessId() && e.th32ThreadID!=GetCurrentThreadId()) {
                if(count==handles.size()){ok=false;break;}
                auto h=OpenThread(THREAD_SUSPEND_RESUME|THREAD_GET_CONTEXT|THREAD_QUERY_INFORMATION,FALSE,e.th32ThreadID);
                if(h)handles[count++]=h;
                else if(GetLastError()!=ERROR_INVALID_PARAMETER){ok=false;break;}
            }
            more=Thread32Next(snapshot,&e);
        }
        CloseHandle(snapshot);if(!ok)return false;
        for(size_t i=0;i<count;++i) {
            if(SuspendThread(handles[i])==DWORD(-1)) {
                DWORD exit=STILL_ACTIVE;if(!GetExitCodeThread(handles[i],&exit) || exit==STILL_ACTIVE)return false;
            } else paused[i]=true;
        }
        for(size_t i=0;i<count;++i)if(paused[i]) {
            CONTEXT c{};c.ContextFlags=CONTEXT_CONTROL;if(!GetThreadContext(handles[i],&c))return false;
            auto rva=c.Rip-Pattern::Address(0);
            for(const auto& p:runtimePatches)if(rva>=p.rva && rva<p.rva+p.size)return false;
            for(const auto& r:Redirects)if(rva>=r.next-7 && rva<r.next)return false;
        }
        return true;
    }
};
bool ApplyPatches(bool enable) {
    // Validate the whole transaction before changing any bytes. All storage is
    // fixed-size because this runs while the other game threads are suspended.
    std::array<int32_t,Redirects.size()> previous{},replacement{};
    std::array<uint8_t,12> bytes{};
    for(const auto& p:runtimePatches)
        if(!Read(Pattern::Address(p.rva),bytes.data(),p.size) || memcmp(bytes.data(),(enable?p.before:p.after).data(),p.size))return false;
    for(size_t i=0;i<Redirects.size();++i) {
        const auto& r=Redirects[i];
        previous[i]=Relative(r.next,enable?Native<void*>(r.target):storage+r.offset);
        replacement[i]=Relative(r.next,enable?storage+r.offset:Native<void*>(r.target));
        int32_t current=0;
        if(!Read(Pattern::Address(r.displacement),&current,4) || current!=previous[i])return false;
    }
    size_t done=0;
    for(const auto& p:runtimePatches) {
        if(!Write(p.rva,(enable?p.after:p.before).data(),p.size))break;
        ++done;
    }
    if(done!=runtimePatches.size()) {
        while(done){const auto& p=runtimePatches[--done];Write(p.rva,(enable?p.before:p.after).data(),p.size);}
        return false;
    }
    size_t redirected=0;
    for(;redirected<Redirects.size();++redirected)
        if(!Write(Redirects[redirected].displacement,&replacement[redirected],4))break;
    if(redirected!=Redirects.size()) {
        while(redirected){--redirected;Write(Redirects[redirected].displacement,&previous[redirected],4);}
        while(done){const auto& p=runtimePatches[--done];Write(p.rva,(enable?p.before:p.after).data(),p.size);}
        return false;
    }
    return true;
}
}
bool Install() {
    if(installed)return true;
    const auto config=Config();
    if(config==Configuration::Disabled)return true;
    if(config==Configuration::Invalid){status="Warlock rejected: invalid manifest, tables or missing model.";return false;}
    uint64_t classes=1,skills=1;
    if(!Read(Pattern::Address(0x1c9fb68),&classes,8) || !Read(Pattern::Address(0x1c9fad8),&skills,8) || classes || skills) {
        status="Warlock requires startup injection before the class/skill tables load.";return false;
    }
    if(!Supported() || !PrepareStorage()){status="Warlock unavailable: executable signature or storage mismatch.";return false;}
    const auto init=MH_Initialize();if(init!=MH_OK && init!=MH_ERROR_ALREADY_INITIALIZED)return false;
    const uint32_t hooks[]{0x59df70,0x42d0f0};
    void* replacements[]{reinterpret_cast<void*>(RegistryHook),reinterpret_cast<void*>(ModelHook)};
    void** originals[]{reinterpret_cast<void**>(&originalRegistry),reinterpret_cast<void**>(&originalModel)};
    size_t created=0;
    for(;created<2;++created)if(MH_CreateHook(Native<void*>(hooks[created]),replacements[created],originals[created])!=MH_OK)break;
    bool patched=false;
    if(created==2) {
        PausedThreads threads;
        patched=threads.Pause() && ApplyPatches(true);
    }
    bool ok=patched;
    if(ok) {
        for(auto r:hooks)if(MH_QueueEnableHook(Native<void*>(r))!=MH_OK)ok=false;
        ok=ok && MH_ApplyQueued()==MH_OK;
    }
    if(ok)ok=ExtendCreationInfo();
    if(!ok) {
        for(size_t i=0;i<created;++i){MH_DisableHook(Native<void*>(hooks[i]));MH_RemoveHook(Native<void*>(hooks[i]));}
        if(patched){PausedThreads threads;if(threads.Pause())ApplyPatches(false);}
        status="Warlock startup patch installation failed.";return false;
    }
    HMODULE pinned=nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&Install),&pinned);
    installed=true;status="Warlock class prototype active (class ID 7; skills deferred).";
    return true;
}
bool Installed(){return installed.load();}
const char* Status(){return status.load();}
}
