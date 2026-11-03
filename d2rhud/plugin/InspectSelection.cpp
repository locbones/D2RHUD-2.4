#include "../Pattern.h"
#include "../d2/D2Ptrs.h"
#include "InspectNative.h"
#include <MinHook.h>
#include <Windows.h>
#include <atomic>
#include <intrin.h>

namespace Inspect {
namespace Selection {
using Unit = D2UnitStrc;
using Select = int(__fastcall*)(Unit*, int, int, int);
Select original = nullptr;
std::atomic<bool> installed{false};
std::atomic<const char*> status{"Native ally hover is not installed."};
template <class T> T Fn(uint32_t rva) { return reinterpret_cast<T>(Pattern::Address(rva)); }
template <class T> bool Read(const T* pointer, T& value) {
    SIZE_T count = 0;
    return pointer && ReadProcessMemory(GetCurrentProcess(), pointer, &value, sizeof(value), &count) &&
           count == sizeof(value);
}
bool CursorCaller(void* caller) {
    // Cursor search, previous-hover validation, hovered getter and setter only.
    // Combat/skill callers retain the original predicate and relations.
    for (auto rva : {0xe00cf, 0xdfc7a, 0xdee1a, 0xdef81})
        if (caller == Fn<void*>(rva))
            return true;
    return false;
}
bool Geometry(Unit* unit, int x, int y) {
    D2DynamicPathStrc path{};
    if (!Read(unit->pDynamicPath, path))
        return false;
    // The engine's normal mouse cursor uses these same coarse and animated
    // sprite bounds. Extend eligibility without making the entire map clickable.
    if (!Fn<int(__fastcall*)(Unit*, int, int, int, int)>(0xc33f0)(unit, int(path.tUnkCoord.X),
                                                                  int(path.tUnkCoord.Y), x, y))
        return false;
    int frame = 0;
    if (!Read(reinterpret_cast<const int*>(reinterpret_cast<uintptr_t>(unit) + 0x5c), frame))
        return false;
    std::array<int, 4> box{}; // native order: left, bottom, right, top
    if (!Fn<int(__fastcall*)(Unit*, int, uint8_t, int, int*)>(0x6b290)(unit, 0, 1, frame >> 8, box.data()))
        return false;
    return x >= box[0] && x <= box[2] && y >= box[3] && y <= box[1];
}
int ForCaller(Unit* unit, int x, int y, int mode, void* caller) {
    const int retail = original(unit, x, y, mode);
    if (retail || !unit || !CursorCaller(caller) || (mode != -1 && mode != 0) ||
        !Native::CursorEligible(unit))
        return retail;
    // Retail fails before its geometry phase for allies whose selectability or
    // current-skill relationship forbids targeting. That must not block Inspect.
    // No unit flags, inventories or combat relation results are modified.
    return mode == -1 || Geometry(unit, x, y);
}
int __fastcall Hook(Unit* unit, int x, int y, int mode) {
    return ForCaller(unit, x, y, mode, _ReturnAddress());
}
bool Signatures() {
    constexpr std::pair<uint32_t, uint64_t> signatures[]{
        {0x6baf0, 0xe1913331d5784845ULL}, {0xc33f0, 0x8de13564c1c82f9fULL}, {0x6b290, 0x14b8e082212ca3d6ULL}};
    for (const auto& [rva, expected] : signatures) {
        std::array<uint8_t, 32> bytes{};
        if (!Read(Fn<decltype(bytes)*>(rva), bytes))
            return false;
        uint64_t hash = 14695981039346656037ULL;
        for (auto byte : bytes)
            hash = (hash ^ byte) * 1099511628211ULL;
        if (hash != expected)
            return false;
    }
    return true;
}
} // namespace Selection
const char* SelectionStatus() { return Selection::status.load(); }
bool InstallSelection() {
    using namespace Selection;
    if (installed)
        return true;
    if (!Signatures()) {
        status.store("Native ally hover unavailable: cursor function signature mismatch.");
        return false;
    }
    const auto initialized = MH_Initialize();
    if (initialized != MH_OK && initialized != MH_ERROR_ALREADY_INITIALIZED)
        return false;
    auto target = Fn<void*>(0x6baf0);
    if (MH_CreateHook(target, reinterpret_cast<void*>(Hook), reinterpret_cast<void**>(&original)) != MH_OK)
        return false;
    if (MH_EnableHook(target) != MH_OK) {
        MH_RemoveHook(target);
        status.store("Native ally hover unavailable: could not enable cursor hook.");
        return false;
    }
    installed.store(true);
    status.store("Native ally hover installed: town-only, cursor-only; combat targeting unchanged.");
    return true;
}
void UninstallSelection() {
    if (Selection::installed.exchange(false)) {
        auto target = Selection::Fn<void*>(0x6baf0);
        MH_DisableHook(target);
        MH_RemoveHook(target);
    }
    Selection::status.store("Native ally hover is not installed.");
}
} // namespace Inspect
