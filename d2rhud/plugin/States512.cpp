#include "States512.h"
#include "States512Data.h"
#include "States512Native.h"
#include "LegacyTransmogData.h"
#include "../Pattern.h"
#include "../d2/Reclass.h"
#include <MinHook.h>
#include <atomic>
#include <limits>

// Startup-loader ABI: 0 = initializing, 1 = all hooks ready, 2 = rejected.
// The launcher reads this exported DWORD while the table-loader thread is held.
extern "C" { __declspec(dllexport) volatile LONG D2RHUD_States512Startup = 0; }

namespace States512 {
namespace {
// Audited D2R 1.2.69270 only. D2StatListEx is 0xb58 bytes: its 32
// inline DWORDs at 0xac8 hold active then graphics-change flags, each
// ceil(stateCount/32) words. At 512 states both end before callbacks at
// 0xb48. Increasing the loader check alone would still overflow three
// native 8-word stack snapshots and truncate network state IDs.
using Unit=D2UnitStrc;
using Client=D2ClientStrc;
using Sender=void(__fastcall*)(Unit*,Client*);
using Receiver=int64_t(__fastcall*)(uint8_t*);
using Remove=void(__fastcall*)(Unit*);
Sender originalAll=nullptr,originalUpdates=nullptr;
Receiver originalOn=nullptr,originalStats=nullptr,originalOff=nullptr;
Remove originalRemove=nullptr;
uint8_t* receiverCode=nullptr;
std::atomic<bool> installed{false};
std::atomic<const char*> status{"Extended states are not installed."};
struct Diagnostics {std::atomic<uint32_t> snapshots{0},updates{0},extendedSent{0},extendedReceived{0},rejected{0};} diagnostics;
constexpr std::array<uint32_t,6> Hooks{0x342480,0x3429d0,0xdd7d0,0xdd830,0xdda30,0x358180};
constexpr std::array<LegacyTransmog::Data::Signature,12> Signatures{{
    {0x1db3b0,0x13c3,0x002bbd10e9d3a737ULL},
    {0x1e4910,0x4aa,0x7ae92b4214fa7ea5ULL},
    {0x342480,0x546,0x8ffafbb0bacc9d73ULL},
    {0x3429d0,0x633,0x7d9b68304c201b16ULL},
    {0xdd7d0,0x5f,0xfbd8d938579ac41bULL},
    {0xdd830,0x1fd,0xc8c7be3634714bd8ULL},
    {0xdda30,0x136,0x23f276a32a1aeb74ULL},
    {0x358180,0x3c4,0xd3db8e07a44b5104ULL},
    {0x1e4300,0x2a1,0x015983bf65395029ULL},
    {0x1e45b0,0x149,0x0c26cf3a466881e7ULL},
    {0x22e750,0x60,0xd21f5213a0e01a5fULL},
    {0x9fe30,0xed,0x2294d998c222e8d0ULL},
}};
template<class T> T Native(uint32_t rva){return reinterpret_cast<T>(Pattern::Address(rva));}
template<class T> bool Read(uintptr_t address,T& value){SIZE_T n=0;return address && ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),&value,sizeof(value),&n) && n==sizeof(value);}
unsigned Count(){uint64_t n=0;return Read(Pattern::Address(0x1c9ebf8),n) && n<=Data::MaxStates ? unsigned(n) : 0;}
bool Supported(){
    std::array<uint8_t,0x1400> bytes{};
    for(const auto& s:Signatures){SIZE_T n=0;if(!ReadProcessMemory(GetCurrentProcess(),Native<void*>(s.rva),bytes.data(),s.size,&n) || n!=s.size || LegacyTransmog::Data::Hash({bytes.data(),s.size})!=s.hash)return false;}
    return true;
}
bool LoaderLimit(uint32_t expected,uint32_t value){
    auto address=Native<uint32_t*>(0x1dc27a);
    if(*address!=expected)return false;
    DWORD previous=0;if(!VirtualProtect(address,4,PAGE_EXECUTE_READWRITE,&previous))return false;
    memcpy(address,&value,4);FlushInstructionCache(GetCurrentProcess(),address,4);
    DWORD unused=0;VirtualProtect(address,4,previous,&unused);return true;
}
bool BuildReceivers(){
    if(receiverCode)return true;
    // Keep every relocated rel32 call/RIP operand within its signed range.
    SYSTEM_INFO info{};GetSystemInfo(&info);
    const auto base=Pattern::Address(0);
    const auto step=uintptr_t(info.dwAllocationGranularity);
    for(uintptr_t delta=step;delta<0x40000000 && !receiverCode;delta+=step){
        for(int direction:{1,-1}){
            const auto candidate=(direction>0 ? base+delta : base>delta ? base-delta : 0)&~(step-1);
            if(candidate)receiverCode=static_cast<uint8_t*>(VirtualAlloc(reinterpret_cast<void*>(candidate),0x1000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
            if(receiverCode)break;
        }
    }
    if(!receiverCode)return false;
    auto fail=[](){VirtualFree(receiverCode,0,MEM_RELEASE);receiverCode=nullptr;return false;};
    for(const auto& c:NativeData::Copies)memcpy(receiverCode+c.destination,Native<void*>(c.source),c.size);
    for(const auto& f:NativeData::Fixups){
        const int64_t offset=int64_t(Pattern::Address(f.target))-int64_t(reinterpret_cast<uintptr_t>(receiverCode)+f.offset+4);
        if(offset<INT32_MIN || offset>INT32_MAX)return fail();
        const int32_t disp=int32_t(offset);memcpy(receiverCode+f.offset,&disp,4);
    }
    // The network packet framing remains unchanged. These copies read a validated,
    // local WORD-state view and retain all native visuals/callback cleanup.
    receiverCode[0x3e+2]=0xb7;       // A7: movzx ebx, word [packet+6]
    receiverCode[0x100+0x5c+2]=0xb7; // A8: movzx r12d, word [packet+7]
    receiverCode[0x100+0x75+3]=9;    // A8: payload starts after the WORD
    const int32_t capacityBias=-72;memcpy(receiverCode+0x100+0x95+4,&capacityBias,4);
    receiverCode[0x400+0x3f+2]=0xb7; // A9: movzx esi, word [packet+6]
    for(const auto& u:NativeData::UnwindBlocks)memcpy(receiverCode+u.offset,u.bytes.data(),u.size);
    auto functions=reinterpret_cast<RUNTIME_FUNCTION*>(receiverCode+0xc00);
    static_assert(sizeof(RUNTIME_FUNCTION)==12);
    for(size_t i=0;i<NativeData::RuntimeFunctions.size();++i){const auto& r=NativeData::RuntimeFunctions[i];functions[i]={r.begin,r.end,r.unwind};}
    DWORD previous=0;
    if(!VirtualProtect(receiverCode,0x1000,PAGE_EXECUTE_READ,&previous))return fail();
    FlushInstructionCache(GetCurrentProcess(),receiverCode,0x1000);
    if(!RtlAddFunctionTable(functions,DWORD(NativeData::RuntimeFunctions.size()),reinterpret_cast<uintptr_t>(receiverCode)))return fail();
    return true;
}
void Send(Client* client,std::span<const uint8_t> packet){
    if(packet.empty())return;
    struct View {const uint8_t* begin;const uint8_t* end;} view{packet.data(),packet.data()+packet.size()};
    Native<void(__fastcall*)(Client*)>(0x2d0130)(client);
    uintptr_t vtable=0,send=0;
    const auto transport=Pattern::Address(0x18682b0);
    if(!Read(transport,vtable) || !Read(vtable+0x28,send) || !send){++diagnostics.rejected;return;}
    reinterpret_cast<void(__fastcall*)(void*,uint32_t,const View*)>(send)(reinterpret_cast<void*>(transport),client->dwClientId,&view);
    if(packet[1]&Data::ExtendedType)++diagnostics.extendedSent;
}
Data::Schema Describe(uint16_t stat){
    uintptr_t table=0;uint64_t count=0;std::array<uint8_t,0x144> record{};uint32_t flags=0;
    if(!Read(Pattern::Address(0x1c9fb78),table) || !Read(Pattern::Address(0x1c9fb80),count) || stat>=count || !Read(table+stat*record.size(),record))return {};
    memcpy(&flags,record.data()+4,4);return {record[8],record[9],(flags&2)!=0};
}
uintptr_t FindStateList(Unit* unit,unsigned state){
    uintptr_t extended=0;
    if(!Read(reinterpret_cast<uintptr_t>(unit)+0x88,extended) || !extended)return 0;
    for(unsigned chain:{0x68,0x70}){
        uintptr_t list=0;if(!Read(extended+chain,list))return {};
        for(size_t i=0;list && i<1024;++i){
            uint32_t id=0;if(!Read(list+0x20,id))return {};
            if(id==state)return list;
            uintptr_t next=0;if(!Read(list+0x48,next) || next==list)return {};list=next;
        }
    }
    return {};
}
std::vector<Data::Stat> StateStats(Unit* unit,unsigned state){
    const auto list=FindStateList(unit,state);uintptr_t values=0;uint64_t count=0;
    if(!list || !Read(list+0x30,values) || !Read(list+0x38,count) || count>65536)return {};
    std::vector<Data::Stat> result(size_t((std::min)(count,uint64_t(16))));SIZE_T n=0;
    if(result.empty() || (ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(values),result.data(),result.size()*sizeof(Data::Stat),&n) && n==result.size()*sizeof(Data::Stat)))return result;
    return {};
}
void __fastcall RemoveCurable(Unit* unit){
    const unsigned count=Count();
    if(count<=Data::NativeStates){originalRemove(unit);return;}
    Unit copy{};uintptr_t mask=0,table=0;
    if(!unit || !Read(reinterpret_cast<uintptr_t>(unit),copy) || !copy.pStatListEx ||
       !Read(Pattern::Address(0x1c9ec60),mask) || !mask || !Read(Pattern::Address(0x1c9ebf0),table) || !table)return;
    const auto root=reinterpret_cast<uintptr_t>(copy.pStatListEx);const unsigned words=(count+31)/32;
    std::array<uint32_t,16> snapshot{},removable{};SIZE_T n=0;int32_t flags=0;
    if(!Read(root+0x1c,flags) || flags>=0 ||
       !ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(root+0xac8),snapshot.data(),words*4,&n) || n!=words*4 ||
       !ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(mask),removable.data(),words*4,&n) || n!=words*4)return;
    for(unsigned state=0;state<count;++state){
        const uint32_t bit=uint32_t(1)<<(state%32);
        if(!(snapshot[state/32]&removable[state/32]&bit))continue;
        const auto list=FindStateList(unit,state);
        if(list){
            Native<void(__fastcall*)(void*)>(0x1e4300)(reinterpret_cast<void*>(list));
            if(Read(list+0x1c,flags) && flags>=0)Native<void(__fastcall*)(void*)>(0x1e45b0)(reinterpret_cast<void*>(list));
        }
        Native<void(__fastcall*)(Unit*,int)>(0x9fe30)(unit,int(state));
    }
}
void Synchronize(Unit* unit,Client* client,bool updates){
    if(!unit || !client)return;
    const unsigned count=Count();
    if(!count)return;
    if(count<=Data::NativeStates){(updates ? originalUpdates : originalAll)(unit,client);return;}
    Unit copy{};uintptr_t table=0;
    if(!Read(reinterpret_cast<uintptr_t>(unit),copy) || !copy.pStatListEx || copy.dwUnitType>=6 || !Read(Pattern::Address(0x1c9ebf0),table) || !table)return;
    const auto root=reinterpret_cast<uintptr_t>(copy.pStatListEx);
    int32_t flags=0;if(!Read(root+0x1c,flags) || flags>=0)return;
    const unsigned words=(count+31)/32;
    std::array<uint32_t,16> active{},changed{};
    SIZE_T n=0;
    if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(root+0xac8),active.data(),words*4,&n) || n!=words*4)return;
    if(updates && (!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(root+0xac8+words*4),changed.data(),words*4,&n) || n!=words*4))return;
    for(unsigned state=0;state<count;++state){
        const auto bit=uint32_t(1)<<(state%32);
        if(!((updates ? changed : active)[state/32]&bit))continue;
        uint8_t stateFlags=1;if(!Read(table+state*0x40+0x10,stateFlags) || (stateFlags&1))continue;
        const bool on=(active[state/32]&bit)!=0;
        const auto stats=on ? StateStats(unit,state) : std::vector<Data::Stat>{};
        const auto packet=Data::Packet(copy.dwUnitType,copy.dwUnitId,state,on,stats,Describe);
        if(packet.empty()){++diagnostics.rejected;continue;}
        Send(client,packet);
    }
    if(updates)++diagnostics.updates;else ++diagnostics.snapshots;
}
void __fastcall SendAll(Unit* u,Client* c){Synchronize(u,c,false);}
void __fastcall SendUpdates(Unit* u,Client* c){Synchronize(u,c,true);}
int64_t Receive(uint8_t* packet,Receiver original,size_t destination){
    std::array<uint8_t,7> header{};
    if(!packet || !Read(reinterpret_cast<uintptr_t>(packet),header))return 0;
    if(!(header[1]&Data::ExtendedType))return original(packet);
    const size_t size=header[0]==0xa8 ? header[6] : 7;
    if(size<7 || size>254){++diagnostics.rejected;return 0;}
    std::vector<uint8_t> bytes(size);SIZE_T n=0;
    if(!ReadProcessMemory(GetCurrentProcess(),packet,bytes.data(),bytes.size(),&n) || n!=bytes.size())return 0;
    auto view=Data::NativeView(bytes,Count());
    if(view.empty()){++diagnostics.rejected;return 0;}
    ++diagnostics.extendedReceived;
    return reinterpret_cast<Receiver>(receiverCode+destination)(view.data());
}
int64_t __fastcall On(uint8_t* p){return Receive(p,originalOn,0);}
int64_t __fastcall Stats(uint8_t* p){return Receive(p,originalStats,0x100);}
int64_t __fastcall Off(uint8_t* p){return Receive(p,originalOff,0x400);}
}
void FinishStartup(bool success){InterlockedExchange(&D2RHUD_States512Startup,success?1:2);}
bool Install(bool publishReady){
    if(installed.load())return true;
    if(!Supported()){status="Extended states unavailable: executable signature mismatch.";InterlockedExchange(&D2RHUD_States512Startup,2);return false;}
    if(!BuildReceivers()){status="Extended states unavailable: native receiver relocation failed.";InterlockedExchange(&D2RHUD_States512Startup,2);return false;}
    const auto init=MH_Initialize();if(init!=MH_OK && init!=MH_ERROR_ALREADY_INITIALIZED){InterlockedExchange(&D2RHUD_States512Startup,2);return false;}
    void* replacements[]{reinterpret_cast<void*>(SendAll),reinterpret_cast<void*>(SendUpdates),reinterpret_cast<void*>(On),reinterpret_cast<void*>(Stats),reinterpret_cast<void*>(Off),reinterpret_cast<void*>(RemoveCurable)};
    void** originals[]{reinterpret_cast<void**>(&originalAll),reinterpret_cast<void**>(&originalUpdates),reinterpret_cast<void**>(&originalOn),reinterpret_cast<void**>(&originalStats),reinterpret_cast<void**>(&originalOff),reinterpret_cast<void**>(&originalRemove)};
    size_t created=0;for(;created<Hooks.size();++created)if(MH_CreateHook(Native<void*>(Hooks[created]),replacements[created],originals[created])!=MH_OK)break;
    if(created==Hooks.size()){
        bool queued=true;for(auto r:Hooks)if(MH_QueueEnableHook(Native<void*>(r))!=MH_OK)queued=false;
        if(queued && MH_ApplyQueued()==MH_OK && LoaderLimit(256,513)){
            // Native receiver code/unwind entries and extended state handling
            // must remain available until this game process exits.
            HMODULE pinned=nullptr;GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&Install),&pinned);
            installed=true;status="Extended states: up to 512 rows (IDs 0-511).";if(publishReady)FinishStartup(true);return true;
        }
    }
    for(size_t i=0;i<created;++i)MH_QueueDisableHook(Native<void*>(Hooks[i]));MH_ApplyQueued();
    for(size_t i=0;i<created;++i)MH_RemoveHook(Native<void*>(Hooks[i]));
    status="Extended states could not be installed; the native limit was retained.";InterlockedExchange(&D2RHUD_States512Startup,2);return false;
}
void Uninstall(){
    // Changing table width with a live character requires a game restart.
    if(!installed.load() || Count()>Data::NativeStates)return;
    if(!LoaderLimit(513,256))return;
    for(auto r:Hooks)MH_QueueDisableHook(Native<void*>(r));MH_ApplyQueued();
    for(auto r:Hooks)MH_RemoveHook(Native<void*>(r));installed=false;
}
const char* Status(){return status.load();}
}
