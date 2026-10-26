#include "WidescreenBars.h"
#include "WidescreenBarsNative.h"
#include "../Pattern.h"
#include <MinHook.h>
#include <atomic>
#include <mutex>

namespace WidescreenBars {
namespace {
std::mutex lifecycle;
std::atomic<bool> ready{false}, enabled{false}, refreshPending{false};
// Keep the saved preference separate from the effective native relay state.
std::atomic<bool> requested{false}, inGame{false};
alignas(4) std::atomic<uint32_t> aspect{Native::NativeAspect};
static_assert(std::atomic<uint32_t>::is_always_lock_free && sizeof(aspect)==4);
static_assert(std::atomic<bool>::is_always_lock_free && sizeof(enabled)==1);
std::atomic<const char*> failure{"Widescreen bars control is not installed."};
struct Hook { uint32_t rva; void* target=nullptr; void* trampoline=nullptr; bool attached=false; };
std::array<Hook,4> hooks{{{Native::Store},{Native::Bars},{Native::Cursor},{Native::Refresh}}};
void* relay=nullptr;
bool created=false;
bool Equal(const void* address,std::span<const uint8_t> expected) {
    std::array<uint8_t,Native::CallbackSize> bytes{}; SIZE_T size=0;
    return expected.size()<=bytes.size() && ReadProcessMemory(GetCurrentProcess(),address,bytes.data(),expected.size(),&size) &&
        size==expected.size() && std::memcmp(bytes.data(),expected.data(),size)==0;
}
// Called with lifecycle held. Scene transitions refresh the cached surface
// without changing the user's saved preference.
void ApplyState() {
    const bool active=requested.load() && inGame.load() && ready.load();
    aspect.store(active?Native::UnmaskedAspect:Native::NativeAspect);
    if(enabled.exchange(active)!=active) refreshPending=true;
}
bool Supported() {
    std::array<uint8_t,Native::CallbackSize> bytes{};
    for(const auto& signature:Native::Signatures) {
        SIZE_T size=0;
        if(signature.size>bytes.size() || !ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(Pattern::Address(signature.rva)),
            bytes.data(),signature.size,&size) || size!=signature.size ||
            Native::Hash({bytes.data(),size})!=signature.hash) return false;
    }
    return true;
}
}
bool Install() {
    std::lock_guard guard(lifecycle);
    if(ready.load()) return true;
    if(!created) {
        // Verify every path before installing any of them. Partial support
        // could expose scenery while leaving the side mouse restriction active.
        if(!Supported()) {
            failure="Unavailable: this game's widescreen rendering code differs from the supported version.";
            return false;
        }
        auto init=MH_Initialize();
        if(init!=MH_OK && init!=MH_ERROR_ALREADY_INITIALIZED) {
            failure="Widescreen bars control could not initialize."; return false;
        }
        // MinHook requires executable endpoints; no entry detour is enabled
        // until all four relays have been populated and changed to RX.
        relay=VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);
        if(!relay) { failure="Widescreen bars control could not allocate its renderer."; return false; }
        auto fail=[] {
            for(auto& hook:hooks) if(hook.trampoline) { MH_RemoveHook(hook.target); hook.trampoline=nullptr; }
            VirtualFree(relay,0,MEM_RELEASE); relay=nullptr;
            failure="Widescreen bars control could not prepare its renderer."; return false;
        };
        for(size_t i=0;i<hooks.size();++i) {
            auto& hook=hooks[i]; hook.target=reinterpret_cast<void*>(Pattern::Address(hook.rva));
            if(MH_CreateHook(hook.target,static_cast<uint8_t*>(relay)+64*i,&hook.trampoline)!=MH_OK) return fail();
        }
        if(!Equal(hooks[0].trampoline,Native::StoreBytes)) return fail();
        auto aspectCode=Native::Relay(reinterpret_cast<uintptr_t>(&aspect),reinterpret_cast<uintptr_t>(hooks[0].trampoline)+8);
        auto barsCode=Native::BranchRelay(reinterpret_cast<uintptr_t>(&enabled),reinterpret_cast<uintptr_t>(hooks[1].trampoline),Pattern::Address(Native::BarsResume));
        auto cursorCode=Native::BranchRelay(reinterpret_cast<uintptr_t>(&enabled),reinterpret_cast<uintptr_t>(hooks[2].trampoline),Pattern::Address(Native::CursorResume));
        auto refreshCode=Native::RefreshRelay(reinterpret_cast<uintptr_t>(&refreshPending),reinterpret_cast<uintptr_t>(hooks[3].trampoline));
        std::memcpy(relay,aspectCode.data(),aspectCode.size());
        std::memcpy(static_cast<uint8_t*>(relay)+64,barsCode.data(),barsCode.size());
        std::memcpy(static_cast<uint8_t*>(relay)+128,cursorCode.data(),cursorCode.size());
        std::memcpy(static_cast<uint8_t*>(relay)+192,refreshCode.data(),refreshCode.size());
        DWORD previous=0;
        if(!VirtualProtect(relay,4096,PAGE_EXECUTE_READ,&previous)) return fail();
        FlushInstructionCache(GetCurrentProcess(),relay,256);
        HMODULE module=nullptr;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&Install),&module)) return fail();
        created=true;
    }
    aspect.store(Native::NativeAspect);
    for(auto& hook:hooks) {
        if(hook.attached) continue;
        if(MH_EnableHook(hook.target)!=MH_OK) {
            // Retain executable storage even after rollback: a render thread
            // can already have entered a relay before the entry is disabled.
            enabled=false; aspect=Native::NativeAspect;
            for(auto& other:hooks) if(other.attached && MH_DisableHook(other.target)==MH_OK) other.attached=false;
            failure="Widescreen bars control could not enable its renderer."; return false;
        }
        hook.attached=true;
    }
    ready=true;
    ApplyState();
    refreshPending=true;
    return true;
}
void Uninstall() {
    std::lock_guard guard(lifecycle);
    enabled=false; aspect=Native::NativeAspect; refreshPending=true; ready=false;
    bool complete=true;
    for(auto& hook:hooks) if(hook.attached) {
        if(MH_DisableHook(hook.target)==MH_OK) hook.attached=false;
        else complete=false;
    }
    failure=complete?"Widescreen bars control is not installed.":"Widescreen bars control could not detach completely.";
    // Keep relays and backing atomics valid until process exit. Normal option
    // changes use SetEnabled(false), allowing the render thread to refresh.
}
bool Available() { return ready.load(); }
void SetEnabled(bool value) {
    std::lock_guard guard(lifecycle);
    requested=value;
    ApplyState();
}
void SetInGame(bool value) {
    if(inGame.load()==value) return;
    std::lock_guard guard(lifecycle);
    inGame=value;
    ApplyState();
}
const char* Status() {
    if(!ready.load()) return failure.load();
    if(requested.load() && !inGame.load()) return "Enabled for in-game play. Main menu bars are restored.";
    return enabled.load()?"No Widescreen Bars is enabled. Mouse targeting uses the full width. Monster awareness is unchanged.":
        "Normal HD widescreen side vignette. Enable to remove the side bars.";
}
}
