#include "MultipleAuras.h"
#include "MultipleAurasNative.h"
#include "../Pattern.h"
#include <atomic>
#include <mutex>

namespace MultipleAuras {
namespace {
using Byte = uint8_t;
template<class T> T Native(uint32_t rva) { return reinterpret_cast<T>(Pattern::Address(rva)); }
template<class T> T Field(const void* pointer, size_t offset = 0) {
    T value{}; if (pointer) std::memcpy(&value, static_cast<const Byte*>(pointer)+offset, sizeof(value));
    return value;
}
std::atomic<bool> requested{true}, ready{false};
std::atomic<const char*> status{"Multiple Auras Per Item is not installed."};
std::mutex lifecycle;
// Removal loses its skill argument before the native deletion loop. Scope it
// on the owning simulation thread; nested callbacks restore the previous ID.
thread_local int removalSkill = -1;
void __fastcall FilteredDelete(void* game, void* event, int skill) noexcept {
    if (event && NativeData::KeepEvent(requested.load(std::memory_order_relaxed),
                                     Field<uint8_t>(event), Field<int>(event,0x1c), skill)) return;
    Native<void(__fastcall*)(void*,void*)>(NativeData::DeleteEvent)(game,event);
}
void __fastcall RemovalDelete(void* game, void* event) noexcept {
    FilteredDelete(game,event,removalSkill);
}
void __fastcall RemovalScope(void* game, void* unit, int key, int skill) noexcept {
    const int previous = removalSkill;
    removalSkill = skill;
    Native<void(__fastcall*)(void*,void*,int,int)>(NativeData::Deactivate)(game,unit,key,skill);
    removalSkill = previous;
}
constexpr size_t Allocation = 4096, RelayStride = 32;
Byte* relay = nullptr;
bool relayReady = false;
struct Patch { std::array<Byte,5> original{}, replacement{}; };
std::array<Patch,std::size(NativeData::Sites)> patches;
bool Equal(uintptr_t address, const void* bytes, size_t size) {
    std::array<Byte,64> actual{}; SIZE_T got = 0;
    return size <= actual.size() && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address),
        actual.data(), size, &got) && got == size && !std::memcmp(actual.data(),bytes,size);
}
bool Signature(uint32_t rva, size_t size, uint64_t hash) {
    std::array<Byte,64> bytes{}; SIZE_T got = 0;
    return size <= bytes.size() && ReadProcessMemory(GetCurrentProcess(), Native<void*>(rva),
        bytes.data(), size, &got) && got == size && NativeData::Hash(bytes.data(),size) == hash;
}
bool Supported() {
    for (const auto& signature : NativeData::Functions)
        if (!Signature(signature.rva,signature.size,signature.hash)) return false;
    for (size_t i = 0; i < patches.size(); ++i) {
        const auto& site = NativeData::Sites[i];
        if (!Signature(site.rva-8,24,site.hash)) return false;
        patches[i].original = NativeData::Call(Pattern::Address(site.rva),
            Pattern::Address(site.adapter == NativeData::Adapter::RemovalScope ?
                             NativeData::Deactivate : NativeData::DeleteEvent));
        if (!Equal(Pattern::Address(site.rva),patches[i].original.data(),5)) return false;
    }
    return true;
}
bool Prepare() {
    if (!relayReady) {
        // An unpublished allocation from an earlier failed preparation is safe
        // to discard; a published relay is retained for process lifetime.
        if (relay) { VirtualFree(relay,0,MEM_RELEASE); relay = nullptr; }
        SYSTEM_INFO system{}; GetSystemInfo(&system);
        const uintptr_t base = Pattern::Address(0), step = system.dwAllocationGranularity;
        if (!step) return false;
        for (uintptr_t delta = 0x4000000; delta < 0x40000000 && !relay; delta += step)
            relay = static_cast<Byte*>(VirtualAlloc(reinterpret_cast<void*>((base+delta)&~(step-1)),
                Allocation,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
        if (!relay) return false;
        for (size_t i = 0; i < patches.size(); ++i) {
            const auto code = NativeData::Relay(NativeData::Sites[i].adapter,
                uintptr_t(&FilteredDelete),uintptr_t(&RemovalDelete),uintptr_t(&RemovalScope));
            if (code.size() > RelayStride) return false;
            std::memcpy(relay+i*RelayStride,code.data(),code.size());
        }
        DWORD previous = 0;
        if (!VirtualProtect(relay,Allocation,PAGE_EXECUTE_READ,&previous)) return false;
        FlushInstructionCache(GetCurrentProcess(),relay,Allocation);
        HMODULE module = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
                               reinterpret_cast<LPCWSTR>(&Install),&module)) return false;
        relayReady = true;
    }
    for (size_t i = 0; i < patches.size(); ++i) {
        patches[i].replacement = NativeData::Call(Pattern::Address(NativeData::Sites[i].rva),
                                                uintptr_t(relay+i*RelayStride));
        if (patches[i].replacement[0] != 0xe8) return false;
    }
    return true;
}
bool Apply(bool restore) {
    std::array<DWORD,std::size(NativeData::Sites)> protections{};
    size_t count = 0;
    for (; count < patches.size(); ++count)
        if (!VirtualProtect(Native<void*>(NativeData::Sites[count].rva),5,PAGE_EXECUTE_READWRITE,
                            &protections[count])) break;
    const bool success = count == patches.size();
    if (success) for (size_t i = 0; i < patches.size(); ++i) {
        const auto& expected = restore ? patches[i].replacement : patches[i].original;
        if (!Equal(Pattern::Address(NativeData::Sites[i].rva),expected.data(),5)) {
            while (count) { const auto n = --count; DWORD unused = 0;
                VirtualProtect(Native<void*>(NativeData::Sites[n].rva),5,protections[n],&unused); }
            return false; // Nothing has been written.
        }
    }
    if (success) for (size_t i = 0; i < patches.size(); ++i) {
        const auto& bytes = restore ? patches[i].original : patches[i].replacement;
        std::memcpy(Native<void*>(NativeData::Sites[i].rva),bytes.data(),5);
        FlushInstructionCache(GetCurrentProcess(),Native<void*>(NativeData::Sites[i].rva),5);
    }
    // Shared code pages must recover their first, original protection.
    while (count) { const auto n = --count; DWORD unused = 0;
        VirtualProtect(Native<void*>(NativeData::Sites[n].rva),5,protections[n],&unused); }
    return success;
}
}
bool Install() {
    std::lock_guard lock(lifecycle);
    if (ready.load()) return true;
    if (!Supported()) { status = "Multiple Auras Per Item unavailable: native aura signature mismatch."; return false; }
    try {
        if (!Prepare() || !Apply(false)) {
            status = "Multiple Auras Per Item could not prepare its native event adapters."; return false;
        }
    } catch (...) { status = "Multiple Auras Per Item could not allocate its native event adapters."; return false; }
    ready = true;
    status = requested.load() ? "Multiple Auras Per Item active: distinct aura timers are preserved for items and monster properties." :
                               "Multiple Auras Per Item disabled: native aura timer matching retained.";
    return true;
}
void Uninstall() {
    std::lock_guard lock(lifecycle);
    if (!ready.load()) return;
    if (!Apply(true)) { status = "Multiple Auras Per Item could not detach: another hook changed its call sites."; return; }
    ready = false; status = "Multiple Auras Per Item is not installed.";
    // A game thread can already be in a leaf relay. Retain its RX page and
    // pinned callback module until process exit, including after reinstall.
}
void SetEnabled(bool enabled) {
    requested = enabled;
    if (ready.load()) status = enabled ?
        "Multiple Auras Per Item active: distinct aura timers are preserved for items and monster properties." :
        "Multiple Auras Per Item disabled: native aura timer matching retained.";
}
bool Available() { return ready.load(); }
const char* Status() { return status.load(); }
}
