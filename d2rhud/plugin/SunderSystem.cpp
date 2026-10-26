#include "SunderSystem.h"
#include "Sunder.h"
#include "ImmunityLabels.h"
#include "../d2/D2Ptrs.h"
#include <Windows.h>
#include <detours/detours.h>
#include <atomic>
#include <limits>
#include <intrin.h>

namespace SunderSystem {
namespace {
using BaseReader = int(__fastcall*)(D2StatListExStrc*, uint32_t, const void*);
BaseReader originalBase = nullptr;
std::atomic<bool> enabled{true}, ready{false};
const char* status = "not installed";
bool attempted = false;
template<class T> bool Read(uintptr_t address, T& value) {
    SIZE_T count = 0;
    return address && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address), &value, sizeof(value), &count) && count == sizeof(value);
}
bool Fingerprint(uint32_t rva, size_t size, uint64_t expected) {
    std::array<unsigned char, 768> bytes{};
    SIZE_T count = 0;
    if (size > bytes.size() || !ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(Pattern::Address(rva)), bytes.data(), size, &count) || count != size) return false;
    uint64_t hash = 14695981039346656037ULL;
    for (size_t i = 0; i < size; ++i) { hash ^= bytes[i]; hash *= 1099511628211ULL; }
    return hash == expected;
}
struct UnitView { uint32_t type, id; D2StatListExStrc* stats; };
bool View(D2UnitStrc* unit, UnitView& out) {
    const auto p = reinterpret_cast<uintptr_t>(unit);
    return unit && Read(p, out.type) && Read(p + 8, out.id) && Read(p + 0x88, out.stats) && out.stats;
}
bool Owns(D2GameStrc* game, D2UnitStrc* monster, uint32_t id) {
    return game && UNITS_GetServerUnitByTypeAndId && UNITS_GetServerUnitByTypeAndId(game, UNIT_MONSTER, id) == monster;
}
D2GameStrc* ResolveGame(D2GameStrc* supplied, D2UnitStrc* monster, uint32_t id) {
    if (supplied) return Owns(supplied, monster, id) ? supplied : nullptr;
    // Native local-client buckets, never a game pointer cached by the renderer.
    for (unsigned bucket = 0; bucket < 8; ++bucket) {
        uintptr_t client = 0;
        if (!Read(Pattern::Address(0x1d637f0) + bucket * sizeof(void*), client)) continue;
        std::array<uintptr_t, 64> visited{};
        for (size_t i = 0; client && i < visited.size(); ++i) {
            if (std::find(visited.begin(), visited.begin() + i, client) != visited.begin() + i) break;
            visited[i] = client;
            D2GameStrc* game = nullptr;
            if (!Read(client + 0x230, game)) break;
            if (Owns(game, monster, id)) return game;
            if (!Read(client + 0x558, client)) break;
        }
    }
    return nullptr;
}
bool SharedPierce(D2GameStrc* game, uint32_t stat, int& pierce) {
    const auto index = std::find(Sunder::ResistanceStats.begin(), Sunder::ResistanceStats.end(), stat) - Sunder::ResistanceStats.begin();
    if (size_t(index) == Sunder::ResistanceStats.size() || !STATLIST_GetUnitStatSigned) return false;
    uintptr_t client = 0;
    if (!Read(reinterpret_cast<uintptr_t>(game) + 0x150, client)) return false;
    std::array<uintptr_t, 64> visited{};
    pierce = 0;
    for (size_t i = 0; client; ++i) {
        if (i == visited.size() || std::find(visited.begin(), visited.begin() + i, client) != visited.begin() + i) return false;
        visited[i] = client;
        D2GameStrc* owner = nullptr; uint32_t id = 0;
        if (!Read(client + 0x230, owner) || owner != game || !Read(client + 0x190, id)) return false;
        auto player = UNITS_GetServerUnitByTypeAndId(game, UNIT_PLAYER, id);
        UnitView view{};
        if (View(player, view) && view.type == UNIT_PLAYER && view.id == id)
            pierce = (std::max)(pierce, STATLIST_GetUnitStatSigned(player, Sunder::ImmunityStats[index], 0));
        if (!Read(client + 0x558, client)) return false;
    }
    return true;
}
struct Entry { uint32_t key; int value; };
bool Raw(uintptr_t list, bool aggregate, uint32_t stat, int& result) {
    uintptr_t entries = 0; uint64_t count = 0;
    const auto offset = aggregate ? 0x80 : 0x30;
    if (!Read(list + offset, entries) || !Read(list + offset + 8, count) || count > 65536 || (count && !entries)) return false;
    uint64_t low = 0, high = count;
    const auto key = stat << 16;
    while (low < high) {
        const auto middle = low + (high - low) / 2;
        Entry entry{};
        if (!Read(entries + middle * sizeof(Entry), entry)) return false;
        if (entry.key < key) low = middle + 1;
        else if (entry.key > key) high = middle;
        else { result = entry.value; return true; }
    }
    result = 0; return true;
}
void UnsupportedStats() {
    static std::atomic_flag reported = ATOMIC_FLAG_INIT;
    if (!reported.test_and_set()) OutputDebugStringA("D2RHUD sunder: unsupported or inconsistent resistance stat-list; preserving native resistance.\n");
}
bool AdditiveOperations(const void* cost) {
    // Compiled elemental resistance rows include op 2 (resistance per level),
    // even when that bonus is absent on this monster. These operations add to
    // the resistance fold; scaling/replacement operations remain unsupported.
    for (size_t index = 0; index < 16; ++index) {
        uint8_t operation = 0;
        if (!Read(reinterpret_cast<uintptr_t>(cost) + 0xde + index * 6, operation)) return false;
        if (!operation) return true;
        if (operation != 2) return false;
    }
    return true;
}
bool Project(D2GameStrc* game, D2UnitStrc* monster, uint32_t stat, Sunder::Projection& projected, const void*& cost) {
    if (!ready.load(std::memory_order_acquire) || !enabled.load(std::memory_order_relaxed) || !Sunder::IsResistance(stat)) return false;
    UnitView unit{};
    if (!View(monster, unit) || unit.type != UNIT_MONSTER) return false;
    const auto root = reinterpret_cast<uintptr_t>(unit.stats);
    uint32_t flags = 0; D2UnitStrc* owner = nullptr;
    if (!Read(root + 0x1c, flags) || !(flags & 0x80000000u) || !Read(root + 0x78, owner) || owner != monster) return false;
    game = ResolveGame(game, monster, unit.id);
    int pierce = 0;
    if (!game || !SharedPierce(game, stat, pierce) || pierce <= 0) return false;
    uintptr_t table = 0; int64_t rows = 0;
    if (!Read(Pattern::Address(0x1c9e980) + 0x11f8, table) || !Read(Pattern::Address(0x1c9e980) + 0x1200, rows) || rows <= stat || !table) return false;
    cost = reinterpret_cast<void*>(table + stat * 0x144);
    uint8_t operations = 0, costFlags = 0;
    if (!Read(reinterpret_cast<uintptr_t>(cost) + 0x4e, operations) ||
        (operations && !AdditiveOperations(cost)) || !Read(reinterpret_cast<uintptr_t>(cost) + 4, costFlags)) { UnsupportedStats(); return false; }
    // Both consumers must accept the same stat-cost semantics. A shifted
    // resistance or a minimum that itself grants immunity cannot use floor 99.
    uint8_t getterFlags = 0, shift = 0; int minimum = 0;
    if (!Read(reinterpret_cast<uintptr_t>(cost) + 5, getterFlags)) return false;
    if (getterFlags & 2) {
        if (!Read(reinterpret_cast<uintptr_t>(cost) + 0x14, shift) || shift != 0 ||
            !Read(reinterpret_cast<uintptr_t>(cost) + 0x28, minimum) || minimum > Sunder::Floor) {
            UnsupportedStats(); return false;
        }
    }
    int base = 0, total = 0;
    uintptr_t child = 0;
    if (!Raw(root, false, stat, base) || !Raw(root, true, stat, total) || !Read(root + 0x68, child)) return false;
    int64_t positive = 0, negative = 0;
    std::array<uintptr_t, 512> visited{};
    for (size_t i = 0; child; ++i) {
        if (i == visited.size() || std::find(visited.begin(), visited.begin() + i, child) != visited.begin() + i) { UnsupportedStats(); return false; }
        visited[i] = child;
        uint32_t childFlags = 0; uintptr_t parent = 0;
        if (!Read(child + 0x1c, childFlags) || !Read(child + 0x58, parent) || parent != root || (childFlags & 0x2000u)) return false;
        if (!(costFlags & 4) || !(childFlags & 0x40000000u)) {
            int contribution = 0;
            if (!Raw(child, (childFlags & 0x80000000u) != 0, stat, contribution)) return false;
            if (contribution > 0) positive += contribution; else negative += contribution;
        }
        if (!Read(child + 0x48, child)) return false;
    }
    if (operations) {
        // Ask the audited native fold to evaluate its own input stats and
        // integer rounding. Validate against the stored aggregate before
        // classifying the additive operation's contribution by its sign.
        const auto fold = reinterpret_cast<int(__fastcall*)(D2StatListExStrc*, uint32_t)>(Pattern::Address(0x1e3300));
        if (!fold || fold(unit.stats, stat << 16) != total) { UnsupportedStats(); return false; }
        const int64_t contribution = int64_t(total) - base - positive - negative;
        if (contribution > 0) positive += contribution; else negative += contribution;
    }
    if (positive > INT_MAX || negative < INT_MIN || int64_t(base) + positive + negative != total) { UnsupportedStats(); return false; }
    projected = Sunder::Calculate(base, int(positive), int(negative), pierce);
    return projected.valid;
}
int ProjectResistance(D2GameStrc* game, D2UnitStrc* unit, uint32_t stat, int native) {
    Sunder::Projection projected{}; const void* cost = nullptr;
    if (!Project(game, unit, stat, projected, cost) || !projected.applied) return native;
    uint8_t flags = 0; int minimum = 0;
    if (!Read(reinterpret_cast<uintptr_t>(cost) + 5, flags)) return native;
    if (flags & 2) {
        if (!Read(reinterpret_cast<uintptr_t>(cost) + 0x28, minimum)) return native;
        if ((std::max)(projected.native, minimum) != native) return native;
        return (std::max)(projected.effective, minimum);
    }
    return projected.native == native ? projected.effective : native;
}
int __fastcall HookBase(D2StatListExStrc* stats, uint32_t encoded, const void* cost) {
    const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    const int native = originalBase(stats, encoded, cost);
    // Run inside the native common helper, after its player/hireling exemptions
    // and immediately before its >=100 test and signed division by five.
    if (caller != Pattern::Address(0x366458) || (encoded & 0xffff)) return native;
    uint32_t flags = 0; D2UnitStrc* owner = nullptr;
    const auto root = reinterpret_cast<uintptr_t>(stats);
    if (!Read(root + 0x1c, flags) || !(flags & 0x80000000u) || !Read(root + 0x78, owner)) return native;
    UnitView view{};
    if (!View(owner, view) || view.stats != stats || view.type != UNIT_MONSTER) return native;
    Sunder::Projection projected{}; const void* selectedCost = nullptr;
    return Project(nullptr, owner, encoded >> 16, projected, selectedCost) ? projected.immunity : native;
}
}
bool Install() {
    if (attempted) return ready.load();
    attempted = true;
    // Audited native stat fold, readers and common aura/curse penalty helper.
    if (!Fingerprint(0x1e2e00, 0x6a, 0x4121c6de71bf6c4aULL) ||
        !Fingerprint(0x1e2e70, 0x78, 0x8c4e203dfc43d561ULL) ||
        !Fingerprint(0x1e3300, 0xf2, 0x35d5e724812d3de9ULL) ||
        !Fingerprint(0x1e33f2, 0x24d, 0x740e07b4d98d10ecULL) ||
        !Fingerprint(0x366380, 0xfb, 0x3e67a1d05797e99dULL)) {
        status = "unavailable: native sunder compatibility check failed"; return false;
    }
    originalBase = reinterpret_cast<BaseReader>(Pattern::Address(0x1e2e00));
    if (DetourTransactionBegin() != NO_ERROR) { status = "unavailable: detour transaction failed"; return false; }
    if (DetourUpdateThread(GetCurrentThread()) != NO_ERROR || DetourAttach(reinterpret_cast<PVOID*>(&originalBase), HookBase) != NO_ERROR) {
        DetourTransactionAbort(); status = "unavailable: immunity check hook failed"; return false;
    }
    if (DetourTransactionCommit() != NO_ERROR) { status = "unavailable: immunity check hook commit failed"; return false; }
    if (!ImmunityLabels::Install(ProjectResistance)) {
        if (DetourTransactionBegin() == NO_ERROR) {
            if (DetourUpdateThread(GetCurrentThread()) == NO_ERROR && DetourDetach(reinterpret_cast<PVOID*>(&originalBase), HookBase) == NO_ERROR) DetourTransactionCommit();
            else DetourTransactionAbort();
        }
        status = "unavailable: resistance reader compatibility/hook failed"; return false;
    }
    ready.store(true, std::memory_order_release);
    status = "installed: immutable resistance projection; shared player maximum; floor 99";
    return true;
}
int EffectiveResistance(D2GameStrc* game, D2UnitStrc* monster, uint32_t stat, int nativeTotal) { return ProjectResistance(game, monster, stat, nativeTotal); }
void SetEnabled(bool value) { enabled.store(value, std::memory_order_relaxed); }
const char* Status() { return status; }
void Uninstall() {
    ready.store(false, std::memory_order_release);
    ImmunityLabels::Uninstall();
    if (originalBase && DetourTransactionBegin() == NO_ERROR) {
        if (DetourUpdateThread(GetCurrentThread()) == NO_ERROR && DetourDetach(reinterpret_cast<PVOID*>(&originalBase), HookBase) == NO_ERROR) {
            if (DetourTransactionCommit() == NO_ERROR) originalBase = nullptr;
        } else DetourTransactionAbort();
    }
}
}
