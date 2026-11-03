#include "MercSeller.h"
#include "../Pattern.h"
#include "MercSellerData.h"
#include "MercSellerNative.h"
#include <MinHook.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <set>

namespace MercSeller {
namespace {
using namespace Data;
std::atomic<bool> requested{false}, ready{false};
std::atomic<const char*> status{"Merc Seller Unlock is not installed."};
std::mutex lifecycle, catalogMutex;
template <class T> T Native(uint32_t rva) { return reinterpret_cast<T>(Pattern::Address(rva)); }
template <class T> bool Read(const void* address, T& value) {
    SIZE_T n = 0;
    return address && ReadProcessMemory(GetCurrentProcess(), address, &value, sizeof(value), &n) &&
           n == sizeof(value);
}
template <class T> T Field(const void* address, size_t offset = 0) {
    T value{};
    if (address)
        Read(reinterpret_cast<void*>(uintptr_t(address) + offset), value);
    return value;
}
bool Bytes(uintptr_t address, size_t size, std::vector<uint8_t>& bytes) {
    bytes.resize(size);
    SIZE_T n = 0;
    return address &&
           ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address), bytes.data(), size, &n) &&
           n == size;
}
struct Source {
    uintptr_t rows = 0, monsters = 0;
    uint64_t count = 0, monsterCount = 0;
    bool operator==(const Source&) const = default;
};
Source catalogSource;
std::shared_ptr<const Catalog> catalog;
std::vector<std::shared_ptr<const Catalog>> retiredCatalogs;
std::shared_ptr<const Catalog> Current() {
    const Source source{Field<uintptr_t>(Native<void*>(0x1c9ee58)),
                        Field<uintptr_t>(Native<void*>(0x1c9f878)), Field<uint64_t>(Native<void*>(0x1c9ee60)),
                        Field<uint64_t>(Native<void*>(0x1c9f880))};
    if (!source.rows || !source.monsters || !source.count || !source.monsterCount || source.count > MaxRows ||
        source.monsterCount > MaxRows)
        return {};
    std::lock_guard lock(catalogMutex);
    if (catalog && source == catalogSource)
        return catalog;
    std::vector<uint8_t> rows, monsters, metadata;
    if (!Bytes(source.rows, source.count * HirelingSize, rows) ||
        !Bytes(source.monsters, source.monsterCount * 0x1dc, monsters) ||
        !Bytes(Pattern::Address(0x1852470), NativeNpcCount * MetadataSize, metadata))
        return {};
    auto next = std::make_shared<const Catalog>(Build(rows, monsters, metadata));
    if (catalog)
        retiredCatalogs.push_back(catalog);
    catalog = std::move(next);
    catalogSource = source;
    return catalog;
}
const Metadata* __fastcall Resolve(void* game, uint32_t seller, uint32_t action = 3) noexcept {
    if (!ready.load() || !requested.load() || !game || seller > INT16_MAX || action != 3)
        return nullptr;
    try {
        const auto data = Current();
        return data ? data->Find(int(seller), Field<uint32_t>(game, 0x108) != 0, Field<uint8_t>(game, 0x104))
                    : nullptr;
    } catch (...) {
        status = "Merc Seller Unlock could not read the hireling catalog; native restrictions retained.";
        return nullptr;
    }
}
const uint8_t* __fastcall ResolveClientName(uint32_t name, uint32_t version) noexcept {
    if (!ready.load() || !requested.load() || name > UINT16_MAX || (version != 0 && version != 100) ||
        !Field<uint32_t>(Native<void*>(0x1d60c10)))
        return nullptr;
    try {
        const auto data = Current();
        const auto difficulty = Field<uint8_t>(Native<void*>(0x1bf1ecc));
        const auto seller = Field<uint16_t>(Native<void*>(0x1d60c14));
        if (!data || difficulty > 2 || !data->Find(seller, version == 100, difficulty))
            return nullptr;
        const auto& rows = data->firstRows[size_t(version == 100) * 3 + difficulty];
        const auto found = rows.find(seller);
        if (found == rows.end())
            return nullptr;
        const auto table = Field<uint8_t*>(Native<void*>(0x1c9ee58));
        if (!table)
            return nullptr;
        const auto row = table + found->second * HirelingSize;
        if (Field<uint16_t>(row, 0x14c) <= name && Field<uint16_t>(row, 0x14e) >= name)
            return row;
    } catch (...) {
        status = "Merc Seller Unlock could not resolve this seller's hireling row; native lookup retained.";
    }
    return nullptr;
}
const uint8_t* __fastcall ResolveUnitName(void* game, void* unit, uint32_t name, uint32_t version) noexcept {
    if (!ready.load() || !requested.load() || Field<uint32_t>(unit) != 1 || name > UINT16_MAX ||
        (version != 0 && version != 100))
        return nullptr;
    try {
        const auto data = Current();
        const auto difficulty = game ? Field<uint8_t>(game, 0x104) : Field<uint8_t>(Native<void*>(0x1bf1ecc));
        if (!data || difficulty > 2 || (game && (Field<uint32_t>(game, 0x108) != 0) != (version == 100)))
            return nullptr;
        const auto& group = data->unitRows[size_t(version == 100) * 3 + difficulty];
        const auto found = group.find(Field<int32_t>(unit, 4));
        if (found == group.end())
            return nullptr;
        const auto table = Field<uint8_t*>(Native<void*>(0x1c9ee58));
        for (const auto& row : found->second)
            if (table && name >= row.first && name <= row.last)
                return table + row.index * HirelingSize;
    } catch (...) {
        status = "Merc Seller Unlock could not resolve the unit's hireling profile; native lookup retained.";
    }
    return nullptr;
}
struct NativeVector {
    void* data = nullptr;
    uint64_t size = 0, capacity = 0;
};
static_assert(sizeof(NativeVector) == 24);
struct ExtendedMenus {
    void* data = nullptr;
    uint64_t base = 0, total = 0;
    std::vector<int> added;
} extendedMenus;
void* Allocate(size_t size) {
    auto allocator = Native<void*(__fastcall*)()>(0xacdd80)();
    auto vtable = Field<void*>(allocator);
    auto allocate = Field<void*(__fastcall*)(void*, size_t, size_t)>(vtable, 8);
    return allocate ? allocate(allocator, size, 16) : nullptr;
}
void Free(void* pointer) {
    if (!pointer)
        return;
    auto allocator = Native<void*(__fastcall*)()>(0xacdd80)();
    auto vtable = Field<void*>(allocator);
    auto free = Field<void(__fastcall*)(void*, void*)>(vtable, 0x20);
    if (free)
        free(allocator, pointer);
}
bool EnsureMenus(const Catalog& data) {
    auto vector = Native<NativeVector*>(0x1c9e608);
    NativeVector existing{};
    if (!Read(vector, existing) || !existing.data || !existing.size || existing.size > MaxRows ||
        (existing.capacity & INT64_MAX) < existing.size)
        return false;
    if (extendedMenus.data != existing.data || existing.size < extendedMenus.base ||
        existing.size > extendedMenus.total)
        extendedMenus = {};
    if (!requested.load()) {
        // Only remove our own unmodified tail, on the native menu-building
        // thread. Existing mod/plugin entries and allocation ownership survive.
        if (extendedMenus.data == existing.data && existing.size == extendedMenus.total) {
            bool ours = true;
            for (size_t i = 0; i < extendedMenus.added.size(); ++i) {
                const auto row = static_cast<uint8_t*>(existing.data) + (extendedMenus.base + i) * MenuSize;
                Menu expected{};
                Put<int16_t>(expected.data(), 0, int16_t(extendedMenus.added[i]));
                Put<int32_t>(expected.data(), 4, 1);
                Menu actual{};
                if (!Read(row, actual) || actual != expected)
                    ours = false;
            }
            if (ours)
                vector->size = extendedMenus.base;
        }
        return true;
    }
    if (extendedMenus.data == existing.data && existing.size == extendedMenus.base) {
        vector->size = extendedMenus.total;
        return true;
    }
    std::vector<uint8_t> original;
    if (!Bytes(uintptr_t(existing.data), existing.size * MenuSize, original))
        return false;
    std::set<int> present;
    for (size_t i = 0; i < existing.size; ++i)
        present.insert(Get<int16_t>(original.data(), i * MenuSize));
    std::vector<int> additions;
    for (const auto& group : data.sellers)
        for (const auto& [id, value] : group)
            if (present.insert(id).second)
                additions.push_back(id);
    if (additions.empty())
        return true;
    const auto total = existing.size + additions.size();
    if (total > MaxRows)
        return false;
    const auto replacement = Allocate(total * MenuSize);
    if (!replacement)
        return false;
    std::memcpy(replacement, original.data(), original.size());
    for (size_t i = 0; i < additions.size(); ++i) {
        Menu menu{};
        Put<int16_t>(menu.data(), 0, int16_t(additions[i]));
        Put<int32_t>(menu.data(), 4, 1);
        // The native builder supplies the title and Quit entry; Hire is added
        // to the finished dialog, without replacing an NPC's other services.
        std::memcpy(static_cast<uint8_t*>(replacement) + (existing.size + i) * MenuSize, menu.data(),
                    menu.size());
    }
    extendedMenus = {replacement, existing.size, total, std::move(additions)};
    *vector = {replacement, total, total};
    if (!(existing.capacity & (1ULL << 63)))
        Free(existing.data);
    return true;
}
using MenuBuild = void(__fastcall*)();
MenuBuild originalMenu = nullptr;
#pragma pack(push, 1)
struct RosterRequest {
    uint8_t opcode = 0x38;
    uint32_t action = 3, npc = 0;
};
#pragma pack(pop)
static_assert(sizeof(RosterRequest) == 9);
void __fastcall MenuHook() {
    std::shared_ptr<const Catalog> data;
    try {
        if (ready.load()) {
            data = requested.load() ? Current() : std::shared_ptr<const Catalog>{};
            if (data) {
                if (!EnsureMenus(*data)) {
                    data.reset();
                    status = "Merc Seller Unlock could not prepare native NPC menus.";
                }
            } else if (!requested.load())
                EnsureMenus(Catalog{});
        }
    } catch (...) {
        data.reset();
        status = "Merc Seller Unlock could not prepare native NPC menus.";
    }
    originalMenu();
    if (!requested.load() || !data)
        return;
    try {
        if (!Field<uint32_t>(Native<void*>(0x1d60c10)))
            return;
        const auto seller = Field<uint16_t>(Native<void*>(0x1d60c14));
        const auto configuration = Field<void*>(Native<void*>(0x1c9b3f0));
        const auto offer = data->Find(seller, Field<uint32_t>(configuration, 0x5c) != 0,
                                      Field<uint8_t>(Native<void*>(0x1bf1ecc)));
        if (!offer)
            return;
        // Retail hiring NPCs already request their roster and expose Hire,
        // including their original quest/level rules. Do not bypass those.
        const auto native = data->native.find(seller);
        if (native != data->native.end() && native->second[7])
            return;
        const auto dialog = Field<void*>(Native<void*>(0x1d60c60));
        const auto strings = Field<void*>(Native<void*>(0x1856688));
        const auto label = Native<const char*(__fastcall*)(int, void*)>(0x413860)(3397, strings);
        if (AddHire(dialog, label, Pattern::Address(0x13b910))) {
            RosterRequest packet;
            packet.npc = Field<uint32_t>(Native<void*>(0x1d60c0c));
            Native<void(__fastcall*)(const void*, int)>(0x10e710)(&packet, sizeof(packet));
        }
    } catch (...) {
        status = "Merc Seller Unlock could not update this NPC's native hiring menu.";
    }
}
struct Hook {
    void* target = nullptr;
    void* trampoline = nullptr;
};
std::array<Hook, NativeData::Sites.size()> hooks;
uint8_t* relay = nullptr;
bool registered = false, created = false;
constexpr size_t Allocation = 8192, TableOffset = 0x1800, UnwindOffset = 0x1000;
bool Supported() {
    std::vector<uint8_t> bytes;
    for (const auto& signature : NativeData::Signatures) {
        if (!Bytes(Pattern::Address(signature.rva), signature.size, bytes) ||
            NativeData::Hash(bytes) != signature.hash)
            return false;
    }
    return !NativeData::Signatures.empty();
}
bool Prepare() {
    if (!Supported()) {
        status = "Merc Seller Unlock unavailable: native hiring code signature mismatch.";
        return false;
    }
    const auto initialized = MH_Initialize();
    if (initialized != MH_OK && initialized != MH_ERROR_ALREADY_INITIALIZED) {
        status = "Merc Seller Unlock could not initialize native hooks.";
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
        status = "Merc Seller Unlock could not allocate its native hiring relays.";
        return false;
    }
    auto records = reinterpret_cast<RUNTIME_FUNCTION*>(relay + TableOffset);
    for (size_t i = 0; i < hooks.size(); ++i) {
        const auto& site = NativeData::Sites[i];
        auto& hook = hooks[i];
        hook.target = Native<void*>(site.rva);
        if (MH_CreateHook(hook.target, relay + i * 0x200, &hook.trampoline) != MH_OK) {
            status = "Merc Seller Unlock could not prepare a native seller check.";
            return false;
        }
        const auto callback = site.unitLookup     ? reinterpret_cast<uintptr_t>(ResolveUnitName)
                              : site.clientLookup ? reinterpret_cast<uintptr_t>(ResolveClientName)
                                                  : reinterpret_cast<uintptr_t>(Resolve);
        const auto code =
            NativeData::Relay(site, callback, uintptr_t(hook.trampoline), Pattern::Address(site.resume));
        if (code.size() > 0x200)
            return false;
        std::memcpy(relay + i * 0x200, code.data(), code.size());
        DWORD64 nativeBase = 0;
        const auto parent = RtlLookupFunctionEntry(base + site.rva, &nativeBase, nullptr);
        if (!parent || nativeBase != base) {
            status = "Merc Seller Unlock could not resolve native hiring unwind records.";
            return false;
        }
        const auto offset = uintptr_t(relay) - base;
        const auto unwind = UnwindOffset + i * 0x40;
        records[i] = {DWORD(offset + i * 0x200), DWORD(offset + i * 0x200 + code.size()),
                      DWORD(offset + unwind)};
        const auto prefix =
            NativeData::UnwindPrefix(*reinterpret_cast<uint8_t*>(base + parent->UnwindData + 3));
        std::memcpy(relay + unwind, prefix.data(), prefix.size());
        std::memcpy(relay + unwind + prefix.size(), parent, sizeof(*parent));
    }
    if (!RtlAddFunctionTable(records, DWORD(hooks.size()), base)) {
        status = "Merc Seller Unlock could not register native hiring unwind records.";
        return false;
    }
    registered = true;
    DWORD previous = 0;
    if (!VirtualProtect(relay, Allocation, PAGE_EXECUTE_READ, &previous)) {
        status = "Merc Seller Unlock could not protect its native hiring relays.";
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), relay, Allocation);
    if (MH_CreateHook(Native<void*>(0x13d5c0), MenuHook, reinterpret_cast<void**>(&originalMenu)) != MH_OK) {
        status = "Merc Seller Unlock could not prepare the native NPC menu.";
        return false;
    }
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                            reinterpret_cast<LPCWSTR>(&Install), &module)) {
        status = "Merc Seller Unlock could not retain its native hiring callbacks.";
        return false;
    }
    return true;
}
} // namespace
bool Install() {
    std::lock_guard lock(lifecycle);
    if (ready.load())
        return true;
    if (!created) {
        if (!Prepare()) {
            for (auto& hook : hooks)
                if (hook.trampoline) {
                    MH_RemoveHook(hook.target);
                    hook = {};
                }
            if (originalMenu) {
                MH_RemoveHook(Native<void*>(0x13d5c0));
                originalMenu = nullptr;
            }
            if (registered) {
                RtlDeleteFunctionTable(reinterpret_cast<RUNTIME_FUNCTION*>(relay + TableOffset));
                registered = false;
            }
            if (relay) {
                VirtualFree(relay, 0, MEM_RELEASE);
                relay = nullptr;
            }
            return false;
        }
        created = true;
    }
    bool queued = true;
    for (const auto& hook : hooks)
        queued = (MH_QueueEnableHook(hook.target) == MH_OK) && queued;
    queued = (MH_QueueEnableHook(Native<void*>(0x13d5c0)) == MH_OK) && queued;
    if (!queued || MH_ApplyQueued() != MH_OK) {
        for (const auto& hook : hooks)
            MH_QueueDisableHook(hook.target);
        MH_QueueDisableHook(Native<void*>(0x13d5c0));
        MH_ApplyQueued();
        status = "Merc Seller Unlock could not enable native hiring hooks.";
        return false;
    }
    ready = true;
    status = "Merc Seller Unlock ready: hireling Seller rows authorize NPC hiring on the client and host.";
    return true;
}
void Uninstall() {
    std::lock_guard lock(lifecycle);
    requested = false;
    if (!created)
        return;
    for (const auto& hook : hooks)
        MH_QueueDisableHook(hook.target);
    MH_QueueDisableHook(Native<void*>(0x13d5c0));
    if (MH_ApplyQueued() == MH_OK)
        ready = false;
    // Retain executable relays/trampolines and immutable metadata for native
    // calls that were already in flight. The HUD module is pinned with them.
}
void SetEnabled(bool value) { requested = value; }
bool Available() { return ready.load(); }
const char* Status() { return status.load(); }
} // namespace MercSeller
