#include "LoadingPerformance.h"
#include "LoadingPerformanceNative.h"
#include "../Pattern.h"
#include <MinHook.h>
#include <atomic>
#include <mutex>

namespace LoadingPerformance {
namespace {
std::mutex lifecycle;
std::atomic<const char*> status{"Loading acceleration is not installed."};
uint8_t* relay=nullptr;
void* target=nullptr;
void* trampoline=nullptr;
bool enabled=false,registered=false;
constexpr size_t Allocation=4096,TableOffset=0x60,UnwindOffset=0x80;

bool Supported() {
    std::array<uint8_t,0x70> bytes{};
    for(const auto& signature:Native::Signatures){
        SIZE_T read=0;
        if(signature.size>bytes.size() || !ReadProcessMemory(GetCurrentProcess(),
            reinterpret_cast<void*>(Pattern::Address(signature.rva)),bytes.data(),signature.size,&read) ||
            read!=signature.size || Native::Hash({bytes.data(),size_t(read)})!=signature.hash)return false;
    }
    return true;
}
// Keep unwind RVAs relative to the EXE, including the chained original frame.
// The relay changes no stack/nonvolatile state and has no C++ callbacks.
bool RegisterUnwind(uintptr_t base) {
    DWORD64 originalBase=0;
    const auto original=RtlLookupFunctionEntry(base+Native::Budget,&originalBase,nullptr);
    if(!original || originalBase!=base)return false;
    const auto offset=reinterpret_cast<uintptr_t>(relay)-base;
    if(offset>UINT32_MAX-Allocation)return false;
    auto table=reinterpret_cast<RUNTIME_FUNCTION*>(relay+TableOffset);
    *table={DWORD(offset),DWORD(offset+Native::RelaySize),DWORD(offset+UnwindOffset)};
    auto unwind=relay+UnwindOffset;
    unwind[0]=1|(UNW_FLAG_CHAININFO<<3);unwind[1]=0;unwind[2]=0;
    // Chained records must use the same frame register/offset as the parent.
    unwind[3]=*reinterpret_cast<const uint8_t*>(base+original->UnwindData+3);
    std::memcpy(unwind+4,original,sizeof(*original));
    registered=RtlAddFunctionTable(table,1,base)!=FALSE;
    return registered;
}
}
bool Install() {
    std::lock_guard lock(lifecycle);
    if(enabled)return true;
    if(!relay){
        if(!Supported()){
            status="Loading acceleration unavailable: this game's texture upload code differs from the supported build.";
            return false;
        }
        const auto initialized=MH_Initialize();
        if(initialized!=MH_OK && initialized!=MH_ERROR_ALREADY_INITIALIZED){
            status="Loading acceleration unavailable: hook initialization failed.";return false;
        }
        const uintptr_t base=Pattern::Address(0);
        SYSTEM_INFO system{};GetSystemInfo(&system);
        const uintptr_t step=system.dwAllocationGranularity;
        // Above the image keeps both dynamic unwind RVAs and relative branches valid.
        for(uintptr_t delta=step;delta<0x40000000 && !relay;delta+=step){
            const auto candidate=(base+delta)&~(step-1);
            relay=static_cast<uint8_t*>(VirtualAlloc(reinterpret_cast<void*>(candidate),Allocation,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
        }
        if(!relay){status="Loading acceleration unavailable: near-code allocation failed.";return false;}
        const auto fail=[](){
            if(trampoline){MH_RemoveHook(target);trampoline=nullptr;}
            if(registered){RtlDeleteFunctionTable(reinterpret_cast<RUNTIME_FUNCTION*>(relay+TableOffset));registered=false;}
            VirtualFree(relay,0,MEM_RELEASE);relay=nullptr;
            status="Loading acceleration unavailable: renderer relay preparation failed.";return false;
        };
        const auto code=Native::Relay(reinterpret_cast<uintptr_t>(relay),base+Native::Loading,base+Native::Resume);
        if(!code)return fail();
        std::memcpy(relay,code->data(),code->size());
        if(!RegisterUnwind(base))return fail();
        DWORD previous=0;
        if(!VirtualProtect(relay,Allocation,PAGE_EXECUTE_READ,&previous))return fail();
        FlushInstructionCache(GetCurrentProcess(),relay,code->size());
        target=reinterpret_cast<void*>(base+Native::Budget);
        if(MH_CreateHook(target,relay,&trampoline)!=MH_OK)return fail();
        // Verify MinHook relocated exactly the inspected MOV instruction.
        if(std::memcmp(trampoline,Native::Original.data(),Native::Original.size())!=0)return fail();
        HMODULE pinned=nullptr;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&Install),&pinned))return fail();
    }
    if(MH_EnableHook(target)!=MH_OK){status="Loading acceleration unavailable: renderer hook could not be enabled.";return false;}
    enabled=true;
    status="Loading acceleration active: up to 64 texture uploads while loading; 16 during play. Native readiness checks retained.";
    return true;
}
void Uninstall() {
    std::lock_guard lock(lifecycle);
    if(enabled && MH_DisableHook(target)!=MH_OK){status="Loading acceleration could not detach completely.";return;}
    enabled=false;status="Loading acceleration is not installed.";
    // A render thread can already be inside the relay. Keep code/unwind storage
    // and its native-only references valid until the OS reclaims the process.
}
const char* Status(){return status.load();}
}
