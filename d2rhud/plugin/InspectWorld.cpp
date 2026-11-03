#include "../Pattern.h"
#include "../d2/D2Ptrs.h"
#include "CubeMapData.h"
#include "InspectNative.h"
#include "InspectWorldData.h"
#include "InspectEquipment.h"
#include "NativePanels.h"
#include <Windows.h>
#include <atomic>
#include <set>

namespace Inspect {
namespace World {
using Unit = D2UnitStrc;
using Guard = void (*)(void (*)(void*), void*);
std::atomic<Guard> guard{nullptr};
std::atomic<const char*> status{"World icon scan has not run yet."};
template <class T> T Fn(uint32_t rva) { return reinterpret_cast<T>(Pattern::Address(rva)); }
template <class T> bool Read(const T* pointer, T& value) {
    SIZE_T count = 0;
    return pointer && ReadProcessMemory(GetCurrentProcess(), pointer, &value, sizeof(value), &count) &&
           count == sizeof(value);
}
template <class T> T Field(const void* pointer, size_t offset = 0) {
    T value{};
    if (pointer)
        Read(reinterpret_cast<const T*>(uintptr_t(pointer) + offset), value);
    return value;
}
Unit* Lookup(uint32_t type, uint32_t id) {
    return Fn<Unit*(__fastcall*)(void*, uint32_t, uint32_t, uint32_t)>(0x66510)(
        Fn<void*>(0x1d442e0 + type * 0x400), id & 127, id, type);
}
int Area(const Unit& unit) {
    const auto room = Field<D2ActiveRoomStrc*>(unit.pDynamicPath, 0x20);
    const auto drlg = Field<D2DrlgRoomStrc*>(room, 0x18);
    const auto level = Field<D2DrlgLevelStrc*>(drlg, offsetof(D2DrlgRoomStrc, pLevel));
    return Field<uint32_t>(level, offsetof(D2DrlgLevelStrc, nLevelId));
}
bool Alive(const Unit& unit) {
    return unit.dwUnitType <= 1 && unit.pDynamicPath && !(unit.dwFlags & 0x10000) && unit.dwAnimMode &&
           unit.dwAnimMode != (unit.dwUnitType ? 12u : 17u);
}
bool Viewer(Unit& unit) {
    const auto id = Native::LocalId();
    return id && Read(Lookup(0, *id), unit) && Alive(unit) && CubeMap::Data::Town(Area(unit));
}
bool Eligible(const Unit& unit, const Unit& viewer, int area = 0) {
    if (!Alive(unit) || (!unit.dwUnitType && unit.dwUnitId == viewer.dwUnitId))
        return false;
    if (!Equipment::Capable(unit, [](auto* pointer, auto& value) { return Read(pointer, value); }))
        return false;
    if (!unit.dwUnitType)
        return unit.pPlayerData && Area(unit) == (area ? area : Area(viewer));
    // This registry includes custom summons as well as hirelings. Vendor
    // alignment and inventory presence do not establish companion ownership.
    const auto pet = Fn<const uint8_t*(__fastcall*)(uint32_t)>(0xe9a60)(unit.dwUnitId);
    const auto owner = Field<uint32_t>(pet, 0xc);
    Unit player{};
    return pet && owner != UINT32_MAX && Read(Lookup(0, owner), player) && player.pPlayerData &&
           Alive(player) && Area(unit) == (area ? area : Area(viewer));
}
bool Signatures() {
    constexpr std::pair<uint32_t, uint64_t> signatures[]{{0xe9a60, 0x22d783006f45007dULL},
                                                         {0x66510, 0x269d02b3e6718045ULL},
                                                         {0x7ce7d0, 0xe13960fce8984b43ULL},
                                                         {0x7cf5c0, 0x2daabc3de0834809ULL},
                                                         {0x7ce930, 0x017951b1932efc9dULL}};
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
bool Ready() {
    static const bool valid = Signatures();
    return valid;
}
struct Query {
    bool town = false, valid = false;
    uint32_t type = 0, id = 0;
    float width = 0, height = 0;
    std::vector<Native::Eye> eyes;
};
void TownProtected(void* context) {
    Unit viewer{};
    static_cast<Query*>(context)->town = Viewer(viewer);
}
void CheckProtected(void* context) {
    auto& query = *static_cast<Query*>(context);
    Unit viewer{}, unit{};
    query.valid = query.type <= 1 && Viewer(viewer) && Read(Lookup(query.type, query.id), unit) &&
                  unit.dwUnitType == query.type && unit.dwUnitId == query.id && Eligible(unit, viewer);
}
void EyesProtected(void* context) {
    auto& query = *static_cast<Query*>(context);
    Unit viewer{};
    struct Cache {
        uint32_t viewer = UINT32_MAX;
        int area = 0;
        uint64_t scanned = 0;
        std::vector<std::pair<uint32_t, uint32_t>> units;
    };
    static Cache cache;
    if (!Viewer(viewer)) {
        cache = {};
        status.store("No live local player in town.");
        return;
    }
    // Keep icons off native menus and dialogs. Flags are the game's UI state,
    // including inventory, stash, chat, waypoint and pause. Automap is allowed.
    HudNativePanels::Flags flags{};
    if (!Read(Fn<decltype(flags)*>(0x1d53fb8), flags))
        return;
    if (HudNativePanels::BlocksWorldInput(flags)) {
        status.store("Eye icons hidden by a native menu or dialog.");
        return;
    }
    const auto now = GetTickCount64();
    const int area = Area(viewer);
    if (cache.viewer != viewer.dwUnitId || cache.area != area || now - cache.scanned >= 200) {
        cache = {viewer.dwUnitId, area, now};
        std::set<Unit*> seen;
        for (uint32_t type = 0; type < 2; ++type) {
            std::array<Unit*, 128> buckets{};
            if (!Read(Fn<decltype(buckets)*>(0x1d442e0 + type * 0x400), buckets))
                continue;
            for (auto pointer : buckets)
                while (pointer && seen.size() < 4096 && cache.units.size() < 128) {
                    Unit unit{};
                    if (!seen.insert(pointer).second || !Read(pointer, unit))
                        break;
                    if (unit.dwUnitType == type && Eligible(unit, viewer, area))
                        cache.units.emplace_back(type, unit.dwUnitId);
                    pointer = unit.pListNext;
                }
        }
    }
    if (cache.units.empty()) {
        status.store("No nearby allied players or registered companions.");
        return;
    }
    query.eyes.reserve(cache.units.size());
    // Same active-camera choice as native HD mouse unprojection (0x523f70).
    // Accessors only: no engine hooks, selection edits or camera writes.
    using Camera = uint8_t*(__fastcall*)();
    auto primary = Fn<Camera>(0x7ce7d0)();
    auto camera = Fn<Camera>(0x7cf5c0)();
    auto alternate = Fn<Camera>(0x7ce930)();
    if (camera != primary && alternate)
        camera = alternate;
    WorldData::Matrix view{}, projection{};
    if (!camera || !Read(reinterpret_cast<WorldData::Matrix*>(camera), view) ||
        !Read(reinterpret_cast<WorldData::Matrix*>(camera + 0x80), projection)) {
        status.store("Native world camera is unavailable.");
        return;
    }
    const auto appearance = GetAppearance();
    for (auto [type, id] : cache.units) {
        Unit unit{};
        D2DynamicPathStrc path{};
        if (!Read(Lookup(type, id), unit) || !Eligible(unit, viewer, area) || !Read(unit.pDynamicPath, path))
            continue;
        const float x = path.wPosX + float(Field<uint16_t>(&path)) / 65536.f;
        const float y = path.wPosY + float(Field<uint16_t>(&path, 4)) / 65536.f;
        if (auto point =
                WorldData::EyePosition(view, projection, x, y, query.width, query.height, appearance.lift))
            query.eyes.push_back({type, id, (*point)[0], (*point)[1]});
    }
    status.store(query.eyes.empty() ? "All eligible allies are outside the visible world area."
                                    : "Eye icons available for visible allies in town.");
}
} // namespace World
void SetReadGuard(void (*value)(void (*)(void*), void*)) { World::guard.store(value); }
const char* WorldStatus() { return World::status.load(); }
namespace Native {
bool CursorEligible(D2UnitStrc* pointer) {
    // Called inside the engine cursor walk, which owns the candidate lifetime.
    // Avoid taking the render thread's item mutex inside an engine callback.
    D2UnitStrc viewer{}, unit{};
    HudNativePanels::Flags flags{};
    return Enabled() && World::Ready() && World::Viewer(viewer) && World::Read(pointer, unit) &&
           !(unit.dwFlags & 0x200000) && World::Eligible(unit, viewer) &&
           World::Read(World::Fn<decltype(flags)*>(0x1d53fb8), flags) &&
           !HudNativePanels::BlocksWorldInput(flags);
}
bool InTown() {
    World::Query query;
    if (auto read = World::guard.load())
        read(World::TownProtected, &query);
    return query.town;
}
bool CanInspect(uint32_t type, uint32_t id) {
    World::Query query;
    query.type = type;
    query.id = id;
    if (Enabled() && World::Ready())
        if (auto read = World::guard.load())
            read(World::CheckProtected, &query);
    return query.valid;
}
std::vector<Eye> Eyes(float width, float height) {
    World::Query query;
    query.width = width;
    query.height = height;
    if (!Enabled())
        World::status.store("Inspect is disabled in HUD options.");
    else if (!World::Ready())
        World::status.store("World icon accessors failed executable validation.");
    else if (auto read = World::guard.load())
        read(World::EyesProtected, &query);
    else
        World::status.store("World read guard is not initialized.");
    return query.eyes;
}
} // namespace Native
} // namespace Inspect
