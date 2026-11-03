#include "MercIcons.h"
#include "../Pattern.h"
#include "GameAssets.h"
#include "MercIconsData.h"
#include "MercIconsNative.h"
#include <MinHook.h>
#include <atomic>
#include <memory>
#include <mutex>

namespace MercIcons {
namespace {
std::atomic<bool> requested{true}, ready{false};
std::atomic<const char*> status{"Merc Icons Fix is not installed."};
std::atomic<std::shared_ptr<const Data::Frames>> source;
std::mutex lifecycle, cacheMutex;
template <class T> T Native(uint32_t rva) { return reinterpret_cast<T>(Pattern::Address(rva)); }
template <class T> bool Read(const void* address, T& value) {
    SIZE_T size = 0;
    return address && ReadProcessMemory(GetCurrentProcess(), address, &value, sizeof(value), &size) &&
           size == sizeof(value);
}
template <class T> T Field(const void* address, size_t offset = 0) {
    T value{};
    if (address)
        Read(reinterpret_cast<void*>(uintptr_t(address) + offset), value);
    return value;
}
struct Cache {
    uintptr_t linker = 0, descriptions = 0;
    uint64_t count = 0;
    std::shared_ptr<const Data::Frames> source;
    std::vector<int> frames;
} cache;
int __fastcall Resolve(int skill) noexcept {
    if (!ready.load() || !requested.load() || skill < 0)
        return -1;
    try {
        const auto data = source.load();
        if (!data || data->empty())
            return -1;
        const auto skills = Field<void*>(Native<void*>(0x1c9fad0));
        const auto skillCount = Field<uint64_t>(Native<void*>(0x1c9fad8));
        if (!skills || skillCount > Data::MaxRows || uint64_t(skill) >= skillCount)
            return -1;
        int16_t description = -1;
        if (!Read(reinterpret_cast<void*>(uintptr_t(skills) + size_t(skill) * 0x284 + 0x1d4), description) ||
            description < 0)
            return -1;
        const auto descriptions = Field<uintptr_t>(Native<void*>(0x1c9e9e0));
        const auto count = Field<uint64_t>(Native<void*>(0x1c9e9e8));
        const auto linker = Field<uintptr_t>(Native<void*>(0x1c9e9f8));
        if (!descriptions || !linker || !count || count > Data::MaxRows || uint64_t(description) >= count)
            return -1;
        std::lock_guard lock(cacheMutex);
        if (cache.linker != linker || cache.descriptions != descriptions || cache.count != count ||
            cache.source != data) {
            Cache next{linker, descriptions, count, data, std::vector<int>(size_t(count), -1)};
            auto lookup = Native<int(__fastcall*)(void*, const char*)>(0x6d0470);
            for (const auto& [name, frame] : *data)
                if (frame >= 0) {
                    const int id = lookup(reinterpret_cast<void*>(linker), name.c_str());
                    if (id >= 0 && uint64_t(id) < count)
                        next.frames[size_t(id)] = frame;
                }
            cache = std::move(next);
        }
        return cache.frames[size_t(description)];
    } catch (...) {
        status = "Merc Icons Fix could not resolve skill descriptions; native icons retained.";
        return -1;
    }
}
void* trampoline = nullptr;
uint8_t* relay = nullptr;
bool created = false, registered = false;
constexpr size_t Allocation = 4096, UnwindOffset = 0x800, TableOffset = 0xc00;
bool Supported() {
    for (const auto& signature : NativeData::Signatures) {
        std::vector<uint8_t> bytes(signature.size);
        SIZE_T got = 0;
        if (!ReadProcessMemory(GetCurrentProcess(), Native<void*>(signature.rva), bytes.data(), bytes.size(),
                               &got) ||
            got != bytes.size() || NativeData::Hash(bytes) != signature.hash)
            return false;
    }
    return true;
}
void DisposeUninstalled() {
    if (trampoline) {
        MH_RemoveHook(Native<void*>(NativeData::Site));
        trampoline = nullptr;
    }
    if (registered) {
        RtlDeleteFunctionTable(reinterpret_cast<RUNTIME_FUNCTION*>(relay + TableOffset));
        registered = false;
    }
    if (relay) {
        VirtualFree(relay, 0, MEM_RELEASE);
        relay = nullptr;
    }
}
bool Prepare() {
    if (!Supported()) {
        status = "Merc Icons Fix unavailable: native icon code signature mismatch.";
        return false;
    }
    const auto initialized = MH_Initialize();
    if (initialized != MH_OK && initialized != MH_ERROR_ALREADY_INITIALIZED) {
        status = "Merc Icons Fix could not initialize native hooks.";
        return false;
    }
    const auto base = Pattern::Address(0);
    SYSTEM_INFO system{};
    GetSystemInfo(&system);
    const auto step = uintptr_t(system.dwAllocationGranularity);
    for (uintptr_t delta = step; delta < 0x40000000 && !relay; delta += step)
        relay =
            static_cast<uint8_t*>(VirtualAlloc(reinterpret_cast<void*>((base + delta) & ~(step - 1)),
                                               Allocation, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!relay) {
        status = "Merc Icons Fix could not allocate its native icon relay.";
        return false;
    }
    if (MH_CreateHook(Native<void*>(NativeData::Site), relay, &trampoline) != MH_OK) {
        status = "Merc Icons Fix could not prepare native icon selection.";
        return false;
    }
    const auto code =
        NativeData::Relay(uintptr_t(Resolve), uintptr_t(trampoline), Pattern::Address(NativeData::Resume));
    if (code.size() > UnwindOffset)
        return false;
    std::memcpy(relay, code.data(), code.size());
    DWORD64 parentBase = 0;
    const auto parent = RtlLookupFunctionEntry(base + NativeData::Site, &parentBase, nullptr);
    if (!parent || parentBase != base) {
        status = "Merc Icons Fix could not resolve native unwind records.";
        return false;
    }
    const auto offset = uintptr_t(relay) - base;
    auto record = reinterpret_cast<RUNTIME_FUNCTION*>(relay + TableOffset);
    *record = {DWORD(offset), DWORD(offset + code.size()), DWORD(offset + UnwindOffset)};
    const auto prefix = NativeData::UnwindPrefix(*reinterpret_cast<uint8_t*>(base + parent->UnwindData + 3));
    std::memcpy(relay + UnwindOffset, prefix.data(), prefix.size());
    std::memcpy(relay + UnwindOffset + prefix.size(), parent, sizeof(*parent));
    if (!RtlAddFunctionTable(record, 1, base)) {
        status = "Merc Icons Fix could not register native unwind records.";
        return false;
    }
    registered = true;
    DWORD previous = 0;
    if (!VirtualProtect(relay, Allocation, PAGE_EXECUTE_READ, &previous)) {
        status = "Merc Icons Fix could not protect its native icon relay.";
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), relay, Allocation);
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                            reinterpret_cast<LPCWSTR>(&Install), &module)) {
        status = "Merc Icons Fix could not retain its native icon callback.";
        return false;
    }
    return true;
}
} // namespace
void Load(const std::filesystem::path& install, const std::string& mod) {
    try {
        GameAssets assets(install, mod);
        source.store(std::make_shared<const Data::Frames>(Data::Parse(assets.ReadTableOverlay("skilldesc"))));
    } catch (...) {
        source.store({});
    }
}
bool Install() {
    std::lock_guard lock(lifecycle);
    if (ready.load())
        return true;
    if (!created) {
        if (!Prepare()) {
            DisposeUninstalled();
            return false;
        }
        created = true;
    }
    if (MH_EnableHook(Native<void*>(NativeData::Site)) != MH_OK) {
        status = "Merc Icons Fix could not enable native icon selection.";
        return false;
    }
    ready = true;
    const auto data = source.load();
    status =
        data && !data->empty()
            ? "Merc Icons Fix ready: HireableIconCel overrides native hireling skill icons."
            : "Merc Icons Fix ready; no HireableIconCel source/metadata available, native icons retained.";
    return true;
}
void Uninstall() {
    std::lock_guard lock(lifecycle);
    requested = false;
    if (created && MH_DisableHook(Native<void*>(NativeData::Site)) == MH_OK)
        ready = false;
    // Native calls already in flight can still return through these immutable
    // relays. Retain their trampoline, unwind records and pinned module.
}
void SetEnabled(bool enabled) { requested = enabled; }
bool Available() { return ready.load(); }
const char* Status() { return status.load(); }
} // namespace MercIcons
