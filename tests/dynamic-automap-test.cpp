#define NOMINMAX
#include "../d2rhud/plugin/D2RHUD/HudPreferences.cpp"
#include "../d2rhud/plugin/DynamicAutomap.cpp"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

std::map<uint32_t, uintptr_t> addresses;
DWORD64 Pattern::Address(uint32_t rva) { return addresses[rva]; }
namespace {
using namespace DynamicAutomap;
using namespace DynamicAutomap::Data;
std::vector<Cell> drawn;
void* drawnTree = nullptr;
int placements = 0;
void __fastcall Placed(Unit*, int, void*) { ++placements; }
void __fastcall Drawn(int style, void* tree, uint8_t*, void* opacity) {
    assert(style == 0 && opacity == reinterpret_cast<void*>(123));
    drawnTree = tree;
    drawn.clear();
    const auto collect = [&](auto&& self, Node* node) -> void {
        if (!node)
            return;
        self(self, node->left);
        drawn.push_back({node->image, node->x, node->y});
        self(self, node->right);
    };
    collect(collect, static_cast<Tree*>(tree)->root);
}
bool Has(Cell cell) { return std::find(drawn.begin(), drawn.end(), cell) != drawn.end(); }
template <class T> void Put(void* data, size_t offset, T value) {
    memcpy(static_cast<uint8_t*>(data) + offset, &value, sizeof(value));
}
template <class T> void Bind(uint32_t rva, T& value) { addresses[rva] = reinterpret_cast<uintptr_t>(&value); }
struct LayerData {
    int32_t id = 17, special = 0;
    Tree groups[4]{};
};
static_assert(offsetof(LayerData, groups) + 2 * sizeof(Tree) == 0x58);
void Snapshots() {
    Reset();
    addresses.clear();
    placements = 0;
    std::vector<uint8_t> monsters(2 * 0x1dc), extras(2 * 0x128), objects(2 * 0x168), levels(3 * 0x9c);
    Put<int16_t>(monsters.data(), 0x48, 0);
    Put<int16_t>(monsters.data(), 0x1dc + 0x48, 1);
    Put<int32_t>(extras.data(), 0x118, 99);
    Put<int32_t>(extras.data(), 0x128 + 0x118, 120);
    Put<int32_t>(objects.data(), 0x164, 100);
    Put<int32_t>(objects.data(), 0x168 + 0x164, 120);
    Put<int32_t>(levels.data(), 2 * 0x9c + 8, 17);
    auto monPtr = monsters.data(), exPtr = extras.data(), obPtr = objects.data(), lvPtr = levels.data();
    uint64_t two = 2;
    int levelCount = 3, imageCount = 600, spriteCount = 100;
    uint8_t loading = 0;
    Bind(0x1c9f878, monPtr);
    Bind(0x1c9f8b8, exPtr);
    Bind(0x1c9fdb0, obPtr);
    Bind(0x1c9f880, two);
    Bind(0x1c9f8c0, two);
    Bind(0x1c9fdb8, two);
    Bind(0x1c9fd10, levelCount);
    Bind(0x1c9fd28, lvPtr);
    Bind(0x1c9b89c, imageCount);
    Bind(0x1c9b898, spriteCount);
    Bind(0x1cb410a, loading);
    D2DrlgLevelStrc level{};
    level.nLevelId = 2;
    D2DrlgRoomStrc drlg{};
    drlg.pLevel = &level;
    D2ActiveRoomStrc room{};
    Put(&room, 0x18, &drlg);
    D2DynamicPathStrc path{}, otherPath{};
    path.pRoom = otherPath.pRoom = &room;
    path.tUnkCoord = {120, 200};
    otherPath.tUnkCoord = {400, 600};
    Unit monster{}, shared{};
    monster.dwUnitType = shared.dwUnitType = 1;
    monster.dwClassId = 0;
    shared.dwClassId = 1;
    monster.dwAnimMode = shared.dwAnimMode = 1;
    monster.dwFlags = shared.dwFlags = 0x30000000;
    monster.pDynamicPath = &path;
    shared.pDynamicPath = &otherPath;
    monster.pListNext = &shared;
    std::array<Unit*, 128> buckets{};
    buckets[0] = &monster;
    Bind(0x1d446e0, buckets);
    const auto old = Position(99, 120, 200), sharedOld = Position(120, 400, 600);
    const Cell object{100, 50, 50}, ambiguousOld{120, 70, 70}, legacyMonster{99, 90, 90};
    Geometry source({old, sharedOld, object, ambiguousOld, legacyMonster});
    LayerData layer;
    layer.groups[2] = source.tree;
    auto layerPtr = &layer;
    Bind(0x1d56150, layerPtr);
    auto tree = &layer.groups[2];
    uint8_t transform[0x40]{};
    const auto before = source.nodes;
    ready = true;
    originalPlace = Placed;
    Configure(false, 100);
    PlaceHook(&monster, 99, tree);
    PlaceHook(&shared, 120, tree);
    auto draw = [&] { Draw(0, tree, transform, reinterpret_cast<void*>(123), Drawn); };
    draw();
    assert(drawnTree == tree && Has(legacyMonster));
    Configure(true, 100);
    draw();
    assert(drawnTree != tree && Has(old) && Has(sharedOld) && Has(object) && Has(ambiguousOld) &&
           !Has(legacyMonster));
    assert(drawn.size() == 4 && placements == 2);
    // Throttling: movement is incorporated at the next configured refresh.
    path.tUnkCoord.X = 240;
    cached.time = GetTickCount64();
    draw();
    assert(Has(old));
    cached.time -= 100;
    draw();
    assert(!Has(old) && Has(Position(99, 240, 200)));
    monster.dwAnimMode = 0;
    shared.dwAnimMode = 12;
    cached.time -= 100;
    draw();
    assert(drawn.size() == 2 && Has(object) && Has(ambiguousOld));
    // Unloaded, undiscovered and removed units disappear; no server-side scan.
    monster.dwAnimMode = 1;
    monster.dwFlags = 0;
    cached.time -= 100;
    draw();
    assert(drawn.size() == 2);
    monster.dwFlags = 0x30010000;
    cached.time -= 100;
    draw();
    assert(drawn.size() == 2);
    monster.dwFlags = 0x30000000;
    buckets[0] = nullptr;
    cached.time -= 100;
    draw();
    assert(drawn.size() == 2);
    buckets[0] = &monster;
    // Reused layer address with a different ID invalidates the cached snapshot.
    layer.id = 18;
    draw();
    assert(!Has(Position(99, 240, 200)));
    layer.id = 17;
    draw();
    assert(Has(Position(99, 240, 200)));
    // Only the unit-cell tree is replaced; terrain/special cells pass through.
    Draw(0, &layer.groups[0], transform, reinterpret_cast<void*>(123), Drawn);
    assert(drawnTree == &layer.groups[0]);
    spriteCount = 0;
    draw();
    assert(drawnTree == tree);
    spriteCount = 100;
    loading = 1;
    draw();
    assert(drawnTree == tree);
    loading = 0;
    // Corrupt lists fail safely and retries are rate-limited.
    monster.pListNext = &monster;
    cached.time -= 100;
    draw();
    assert(drawnTree == tree);
    monster.pListNext = &shared;
    draw();
    assert(drawnTree == tree);
    cached.time -= 100;
    draw();
    assert(drawnTree != tree);
    // Objects win even when a monster uses the same image at the same point.
    Unit objectUnit{};
    objectUnit.dwUnitType = 2;
    objectUnit.pStaticPath = reinterpret_cast<D2StaticPathStrc*>(&otherPath);
    PlaceHook(&objectUnit, 120, tree);
    cached.time -= 100;
    draw();
    assert(Has(sharedOld));
    // Exercise the actual RPM scanner under a dense client-unit snapshot.
    // This is an offscreen synthetic benchmark, not an in-game FPS claim.
    std::vector<Unit> dense(2048, monster);
    std::vector<D2DynamicPathStrc> densePaths(2048, path);
    for (size_t i = 0; i < dense.size(); ++i) {
        densePaths[i].tUnkCoord.X = uint32_t(100 + i * 10);
        dense[i].pDynamicPath = &densePaths[i];
        dense[i].pListNext = i + 1 < dense.size() ? &dense[i + 1] : nullptr;
    }
    buckets[0] = dense.data();
    const auto begin = std::chrono::steady_clock::now();
    for (int i = 0; i < 25; ++i) {
        cached.time -= 100;
        draw();
        assert(drawnTree != tree && drawn.size() >= dense.size());
    }
    const auto elapsed =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
    std::cout << "Synthetic 2,048-monster refresh: " << elapsed / 25 << " ms average (25 refreshes).\n";
    buckets[0] = &monster;
    Configure(false, 100);
    draw();
    assert(drawnTree == tree && Has(old) && Has(legacyMonster));
    assert(before.size() == source.nodes.size() &&
           !memcmp(before.data(), source.nodes.data(), before.size() * sizeof(Node)));
    // Session teardown clears discovery and sprite tables, regardless of toggle.
    Reset();
    assert(ownership.empty() && monsterIcons.empty() && !cached.geometry);
    ready = false;
    assert(Position(99, -19, -29) == (Cell{99, 0, -5}));
    assert(Interval(-1) == 50 && Interval(100000) == 5000);
}
void Preferences() {
    using namespace HudPreferences;
    for (bool rmd : {false, true}) {
        Config config;
        Load(config, HudConfig::Json::object(), rmd);
        assert(config.DynamicAutomap && config.DynamicAutomapInterval == 100);
        for (int value : {50, 100, 5000}) {
            config.DynamicAutomap = true;
            config.DynamicAutomapInterval = value;
            HudConfig::Json json;
            Save(config, json);
            Config loaded;
            Load(loaded, json, rmd);
            assert(loaded.DynamicAutomap && loaded.DynamicAutomapInterval == value);
        }
        Config low;
        Load(low, {{"DynamicAutomapInterval", -500}}, rmd);
        assert(low.DynamicAutomapInterval == 50);
        Config high;
        Load(high, {{"DynamicAutomapInterval", 100000}}, rmd);
        assert(high.DynamicAutomapInterval == 5000);
        Config invalid;
        Load(invalid, {{"DynamicAutomap", "yes"}, {"DynamicAutomapInterval", "fast"}}, rmd);
        assert(invalid.DynamicAutomap && invalid.DynamicAutomapInterval == 100);
        Config disabled;
        Load(disabled, {{"DynamicAutomap", false}}, rmd);
        assert(!disabled.DynamicAutomap);
        HudConfig::Json saved;
        Save(disabled, saved);
        Config restored;
        Load(restored, saved, rmd);
        assert(!restored.DynamicAutomap);
    }
}
void NativeSignatures(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    assert(input);
    std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(input), {}};
    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes.data());
    const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(bytes.data() + dos->e_lfanew);
    const auto sections = IMAGE_FIRST_SECTION(nt);
    addresses.clear();
    for (const auto& signature : signatures) {
        for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
            const auto& section = sections[i];
            if (signature.rva >= section.VirtualAddress &&
                signature.rva + signature.size <= section.VirtualAddress + section.SizeOfRawData)
                addresses[signature.rva] = uintptr_t(bytes.data() + section.PointerToRawData + signature.rva -
                                                     section.VirtualAddress);
        }
    }
    assert(Supported());
    *reinterpret_cast<uint8_t*>(addresses[0x961c0]) ^= 1;
    assert(!Supported()); // Unsupported engine builds cannot activate the hooks.

    // Execute only the audited routines in an owned, offscreen PE copy. The
    // game's entry point is never run, and no live process is accessed.
    auto image = static_cast<uint8_t*>(
        VirtualAlloc(nullptr, nt->OptionalHeader.SizeOfImage, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    assert(image);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        const auto& section = sections[i];
        memcpy(image + section.VirtualAddress, bytes.data() + section.PointerToRawData,
               section.SizeOfRawData);
    }
    // Restore the deliberately damaged signature in the private copy.
    image[0x961c0] ^= 1;
    struct NativeCapture {
        static inline std::vector<Cell>* Destination() {
            static std::vector<Cell> value;
            return &value;
        }
        static void __fastcall Sprite(void*, int32_t* point, uint8_t* properties, float) {
            Destination()->push_back({*reinterpret_cast<int32_t*>(properties + 0x18), point[0], point[1]});
        }
        static void* __fastcall Insert(void*, uint64_t* hint, const uint8_t* key) {
            Destination()->push_back({*reinterpret_cast<const int16_t*>(key + 2),
                                      *reinterpret_cast<const int32_t*>(key + 4),
                                      *reinterpret_cast<const int32_t*>(key + 8)});
            hint[0] = hint[1] = 0;
            return hint;
        }
    };
    const auto stub = [&](uint32_t rva, auto fn) {
        const uint8_t jump[]{0xff, 0x25, 0, 0, 0, 0};
        memcpy(image + rva, jump, 6);
        auto target = reinterpret_cast<uintptr_t>(fn);
        memcpy(image + rva + 6, &target, 8);
    };
    stub(0x573440, &NativeCapture::Sprite);
    stub(0x98750, &NativeCapture::Insert);
    image[0x1434c50] = 0xc3;
    Put<int32_t>(image, 0x1d56130, 68);
    Put<int32_t>(image, 0x1d56134, 36);
    Put<int32_t>(image, 0x1d5613c, 0);
    Put<int32_t>(image, 0x1d5614c, 0);
    Put<int32_t>(image, 0x1c9b898, 1000);
    Put<int32_t>(image, 0x1d56110, 0);
    Put<int32_t>(image, 0x1befea8, 0);
    Put<int32_t>(image, 0x1d54114, 0);
    std::array<uint64_t, 2> sprite{};
    Put(image, 0x1c9b880, sprite.data());
    DWORD previous = 0;
    assert(VirtualProtect(image, nt->OptionalHeader.SizeOfImage, PAGE_EXECUTE_READWRITE, &previous));
    FlushInstructionCache(GetCurrentProcess(), image, nt->OptionalHeader.SizeOfImage);
    uint8_t transform[0x40]{}, opacity[0x18]{};
    for (size_t offset : {size_t(8), size_t(12), size_t(0x20), size_t(0x24)})
        Put<int32_t>(transform, offset, 1000);
    Put<int32_t>(transform, 0x28, 100);
    Put<int32_t>(transform, 0x2c, 100);
    Put<float>(transform, 0x30, 1);
    Put<float>(transform, 0x34, 1);
    Put<float>(transform, 0x38, 1);
    for (size_t i = 0; i < 6; ++i)
        Put<float>(opacity, i * 4, 0.85f);
    Geometry geometry({{99, 100, 100}, {120, 200, 150}, {100, 10000, 10000}});
    const auto renderer = reinterpret_cast<DrawFunction>(image + 0x969f0);
    NativeCapture::Destination()->clear();
    renderer(0, &geometry.tree, transform, opacity);
    // Native automap CELs retain their own sprite origin (including the
    // vertical offset); the HUD supplies map coordinates, never screen offsets.
    assert(*NativeCapture::Destination() == (std::vector<Cell>{{99, 192, 173}, {120, 292, 223}}));
    Geometry empty({});
    NativeCapture::Destination()->clear();
    renderer(0, &empty.tree, transform, opacity);
    assert(NativeCapture::Destination()->empty());
    Unit unit{};
    D2DynamicPathStrc projected{};
    unit.dwUnitType = 1;
    unit.pDynamicPath = &projected;
    projected.tUnkCoord = {1000, 2000};
    NativeCapture::Destination()->clear();
    reinterpret_cast<Place>(image + 0x94400)(&unit, 99, &geometry.tree);
    assert(*NativeCapture::Destination() == (std::vector<Cell>{Position(99, 1000, 2000)}));
    assert(VirtualFree(image, 0, MEM_RELEASE));
    std::cout << "Actual native CEL renderer traverses/clips private trees; native unit projection matches "
                 "refresh coordinates.\n";
}
} // namespace
int main(int argc, char** argv) {
    Snapshots();
    Preferences();
    if (argc > 1)
        NativeSignatures(argv[1]);
    std::cout << "Dynamic Automap passed: native snapshots, discovery, movement/death/unload, timing, "
                 "layers, object collisions, corruption fallback, native-data preservation, settings and "
                 "executable signatures.\n";
}
