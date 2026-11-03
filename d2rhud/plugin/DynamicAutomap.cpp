#include "DynamicAutomap.h"
#include "../Pattern.h"
#include "../d2/Reclass.h"
#include "DynamicAutomapData.h"
#include <MinHook.h>
#include <array>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace DynamicAutomap {
namespace {
using namespace Data;
using Unit = D2UnitStrc;
using Place = void(__fastcall*)(Unit*, int, void*);
Place originalPlace = nullptr;
std::mutex lifecycle, stateMutex;
std::atomic<bool> ready{false}, enabled{false};
std::atomic<int> interval{DefaultInterval};
std::atomic<uint64_t> revision{0};
std::atomic<const char*> status{"Dynamic Automap is not installed."};
bool created = false;
std::map<int, Ownership> ownership;
size_t remembered = 0;
struct Tables {
    uintptr_t monsters = 0, extras = 0, objects = 0;
    uint64_t monsterCount = 0, extraCount = 0, objectCount = 0;
    bool operator==(const Tables&) const = default;
};
Tables catalogTables;
std::vector<int> monsterIcons;
std::set<int> monsterImages, objectImages;
struct Cached {
    uintptr_t layer = 0;
    int id = -1;
    uint64_t time = 0, revision = UINT64_MAX;
    std::shared_ptr<Geometry> geometry;
} cached;
template <class T> T Native(uint32_t rva) { return reinterpret_cast<T>(Pattern::Address(rva)); }
template <class T> bool Read(const void* address, T& result) {
    SIZE_T count = 0;
    return address && ReadProcessMemory(GetCurrentProcess(), address, &result, sizeof(result), &count) &&
           count == sizeof(result);
}
template <class T> T Field(const void* address, size_t offset = 0) {
    T result{};
    if (address)
        Read(reinterpret_cast<const void*>(uintptr_t(address) + offset), result);
    return result;
}
bool Bytes(uintptr_t address, size_t size, std::vector<uint8_t>& bytes) {
    bytes.resize(size);
    SIZE_T count = 0;
    return address &&
           ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address), bytes.data(), size,
                             &count) &&
           count == size;
}
bool Catalog() {
    const Tables tables{
        Field<uintptr_t>(Native<void*>(0x1c9f878)), Field<uintptr_t>(Native<void*>(0x1c9f8b8)),
        Field<uintptr_t>(Native<void*>(0x1c9fdb0)), Field<uint64_t>(Native<void*>(0x1c9f880)),
        Field<uint64_t>(Native<void*>(0x1c9f8c0)),  Field<uint64_t>(Native<void*>(0x1c9fdb8))};
    if (!tables.monsters || !tables.extras || !tables.objects || !tables.monsterCount || !tables.extraCount ||
        !tables.objectCount || tables.monsterCount > MaxCells || tables.extraCount > MaxCells ||
        tables.objectCount > MaxCells)
        return false;
    if (tables == catalogTables && !monsterIcons.empty())
        return true;
    std::vector<uint8_t> monsters, extras, objects;
    if (!Bytes(tables.monsters, tables.monsterCount * 0x1dc, monsters) ||
        !Bytes(tables.extras, tables.extraCount * 0x128, extras) ||
        !Bytes(tables.objects, tables.objectCount * 0x168, objects))
        return false;
    std::vector<int> icons(tables.monsterCount);
    std::set<int> monsterSet, objectSet;
    for (size_t i = 0; i < icons.size(); ++i) {
        const auto extra = Field<int16_t>(monsters.data(), i * 0x1dc + 0x48);
        if (extra < 0 || uint64_t(extra) >= tables.extraCount)
            continue;
        const auto image = Field<int32_t>(extras.data(), size_t(extra) * 0x128 + 0x118);
        if (image > 0 && image <= INT16_MAX) {
            icons[i] = image;
            monsterSet.insert(image);
        }
    }
    for (size_t i = 0; i < tables.objectCount; ++i) {
        const auto image = Field<int32_t>(objects.data(), i * 0x168 + 0x164);
        if (image > 0 && image <= INT16_MAX)
            objectSet.insert(image);
    }
    monsterIcons = std::move(icons);
    monsterImages = std::move(monsterSet);
    objectImages = std::move(objectSet);
    catalogTables = tables;
    return true;
}
int Layer(void* room) {
    const auto drlg = Field<void*>(room, 0x18);
    const auto level = Field<void*>(drlg, offsetof(D2DrlgRoomStrc, pLevel));
    const auto id = Field<int32_t>(level, offsetof(D2DrlgLevelStrc, nLevelId));
    const auto count = Field<int32_t>(Native<void*>(0x1c9fd10));
    const auto levels = Field<void*>(Native<void*>(0x1c9fd28));
    return level && levels && id >= 0 && id < count ? Field<int32_t>(levels, size_t(id) * 0x9c + 8) : -1;
}
bool LiveCells(int layer, std::vector<Cell>& cells) {
    std::array<Unit*, 128> buckets{};
    if (!Read(Native<void*>(0x1d446e0), buckets))
        return false;
    size_t examined = 0;
    std::unordered_map<void*, int> roomLayers;
    for (auto pointer : buckets) {
        while (pointer) {
            // A corrupt/cyclic list fails the whole snapshot, rather than
            // retaining a misleading subset of moving markers.
            if (++examined > MaxUnits)
                return false;
            Unit unit{};
            if (!Read(pointer, unit))
                return false;
            pointer = unit.pListNext;
            if (unit.dwUnitType != 1 || unit.dwClassId >= monsterIcons.size() || !unit.pDynamicPath ||
                !Live(unit.dwAnimMode, unit.dwFlags) || !monsterIcons[unit.dwClassId])
                continue;
            const auto room = Field<void*>(unit.pDynamicPath, 0x20);
            const auto [found, inserted] = roomLayers.try_emplace(room, -1);
            if (inserted)
                found->second = Layer(room);
            if (found->second != layer)
                continue;
            std::array<int32_t, 2> point{};
            if (!Read(reinterpret_cast<void*>(uintptr_t(unit.pDynamicPath) + 8), point))
                return false;
            cells.push_back(Position(monsterIcons[unit.dwClassId], point[0], point[1]));
        }
    }
    return true;
}
bool StaticCells(const Tree& tree, int layer, int images, std::vector<Cell>& cells) {
    if (tree.count > MaxCells || (tree.count && !tree.root) || (!tree.count && tree.root))
        return false;
    std::vector<Node*> pending;
    if (tree.root)
        pending.push_back(tree.root);
    const auto found = ownership.find(layer);
    const Ownership empty;
    const auto& owners = found == ownership.end() ? empty : found->second;
    size_t visited = 0;
    while (!pending.empty()) {
        if (++visited > tree.count)
            return false;
        const auto pointer = pending.back();
        pending.pop_back();
        Node node{};
        if (!Read(pointer, node))
            return false;
        const Cell cell{node.image, node.x, node.y};
        if (cell.image >= 0 && cell.image < images && !owners.Remove(cell, monsterImages, objectImages))
            cells.push_back(cell);
        if (node.left)
            pending.push_back(node.left);
        if (node.right)
            pending.push_back(node.right);
    }
    return visited == tree.count;
}
void __fastcall PlaceHook(Unit* pointer, int image, void* tree) {
    // The sole native caller (961c0) supplies the unit-marker tree at layer+58.
    // Continue the original placement in both modes: switching the option off
    // immediately restores the native map, and exploration files stay intact.
    try {
        Unit unit{};
        if (image > 0 && image <= INT16_MAX && Read(pointer, unit) &&
            (unit.dwUnitType == 1 || unit.dwUnitType == 2)) {
            const auto layer = Field<int32_t>(reinterpret_cast<void*>(uintptr_t(tree) - 0x58));
            std::array<int32_t, 2> point{};
            if (layer >= 0 && layer <= 1000000 && unit.pDynamicPath &&
                Read(reinterpret_cast<void*>(uintptr_t(unit.pDynamicPath) + 8), point)) {
                std::lock_guard lock(stateMutex);
                if (remembered < MaxCells && (ownership.contains(layer) || ownership.size() < 1024)) {
                    remembered +=
                        ownership[layer].Remember(Position(image, point[0], point[1]), unit.dwUnitType == 1);
                }
            }
        }
    } catch (...) { /* An optional display cache must never stop exploration. */
    }
    originalPlace(pointer, image, tree);
}
struct Signature {
    uint32_t rva, size;
    uint64_t hash;
};
constexpr Signature signatures[]{
    {0x94400, 0x15e, 0xebc17140797f7c4eULL}, // unit projection and placement
    {0x961c0, 0x1e5, 0x6e8c0c30538420c8ULL}, // discovery flags, table offsets, sole placement caller
    {0x969f0, 0x4ac, 0x11a3a735eadba4d1ULL}  // tree layout and native sprite renderer
};
bool Supported() {
    std::vector<uint8_t> bytes;
    for (const auto& signature : signatures) {
        if (!Bytes(Pattern::Address(signature.rva), signature.size, bytes))
            return false;
        uint64_t hash = 14695981039346656037ULL;
        for (auto byte : bytes)
            hash = (hash ^ byte) * 1099511628211ULL;
        if (hash != signature.hash)
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
        // Must precede CubeMap's renderer hook, which shares our draw callback.
        if (!Supported()) {
            status = "Dynamic Automap unavailable: native signature mismatch.";
            return false;
        }
        const auto init = MH_Initialize();
        if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) {
            status = "Dynamic Automap could not initialize its placement observer.";
            return false;
        }
        if (MH_CreateHook(Native<void*>(0x94400), PlaceHook, reinterpret_cast<void**>(&originalPlace)) !=
            MH_OK) {
            status = "Dynamic Automap could not prepare its placement observer.";
            return false;
        }
        created = true;
    }
    if (MH_EnableHook(Native<void*>(0x94400)) != MH_OK) {
        status = "Dynamic Automap could not enable its placement observer.";
        return false;
    }
    ready = true;
    status =
        "Dynamic Automap ready; refreshes discovered monster CEL icons locally, including TCP/IP guests.";
    return true;
}
void Uninstall() {
    std::lock_guard lock(lifecycle);
    enabled = false;
    if (ready.load() && MH_DisableHook(Native<void*>(0x94400)) == MH_OK)
        ready = false;
    if (!ready.load())
        status = "Dynamic Automap is unavailable while its native renderer is stopped.";
    // Retain the trampoline while an in-flight native call could still use it.
}
bool Available() { return ready.load(); }
void Configure(bool value, int milliseconds) {
    const auto changedInterval = interval.exchange(Interval(milliseconds)) != Interval(milliseconds);
    if (enabled.exchange(value) != value || changedInterval)
        ++revision;
}
const char* Status() { return status.load(); }
void Reset() {
    std::lock_guard lock(stateMutex);
    ownership.clear();
    remembered = 0;
    cached = {};
    catalogTables = {};
    monsterIcons.clear();
    monsterImages.clear();
    objectImages.clear();
}
void Draw(int style, void* tree, uint8_t* transform, void* opacity, DrawFunction original) {
    if (!ready.load() || !enabled.load() || !transform) {
        original(style, tree, transform, opacity);
        return;
    }
    std::shared_ptr<Geometry> geometry;
    try {
        const auto layer = Field<uintptr_t>(Native<void*>(0x1d56150));
        if (!layer || uintptr_t(tree) != layer + 0x58 || Field<uint8_t>(Native<void*>(0x1cb410a))) {
            original(style, tree, transform, opacity);
            return;
        }
        const auto id = Field<int32_t>(reinterpret_cast<void*>(layer));
        const auto images = Field<int32_t>(Native<void*>(0x1c9b89c));
        // The renderer divides by this sprite count; never enter it with an
        // uninitialized sprite set, including startup/act-transition frames.
        if (id < 0 || images <= 0 || Field<int32_t>(Native<void*>(0x1c9b898)) <= 0) {
            original(style, tree, transform, opacity);
            return;
        }
        std::lock_guard lock(stateMutex);
        const auto now = GetTickCount64(), version = revision.load();
        if (cached.layer != layer || cached.id != id || cached.revision != version || !cached.time ||
            now - cached.time >= uint64_t(interval.load())) {
            std::vector<Cell> cells;
            Tree native{};
            if (!Catalog() || !Read(tree, native) || !StaticCells(native, id, images, cells) ||
                !LiveCells(id, cells)) {
                cached = {layer, id, now, version, {}};
                status = "Dynamic Automap is waiting for a complete native map/unit snapshot.";
            } else {
                std::erase_if(cells,
                              [&](const Cell& cell) { return cell.image < 0 || cell.image >= images; });
                cached = {layer, id, now, version, std::make_shared<Geometry>(std::move(cells))};
                status = "Dynamic Automap active: monster CEL icons refresh at the configured interval.";
            }
        }
        geometry = cached.geometry;
    } catch (...) {
        status = "Dynamic Automap could not refresh; native automap preserved.";
    }
    // No engine callbacks under our mutex; the private copy remains alive for
    // the complete native draw, even when reset on another thread.
    original(style, geometry ? &geometry->tree : tree, transform, opacity);
}
} // namespace DynamicAutomap
