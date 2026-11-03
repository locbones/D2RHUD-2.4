#define NOMINMAX
#include "../d2rhud/json.hpp"
#include "../d2rhud/plugin/InspectWorld.cpp"
#include <cassert>
#include <fstream>
#include <iostream>
#include <map>
namespace {
D2UnitStrc viewer{}, merc{}, soul{}, npc{}, enemy{}, guest{};
D2DynamicPathStrc path{};
D2PlayerDataStrc data{};
D2MonsterDataStrc monsterData{};
D2MonStatsTxt equipmentRow{};
D2ActiveRoomStrc room{};
D2DrlgRoomStrc drlg{};
D2DrlgLevelStrc level{};
std::array<std::array<D2UnitStrc*, 128>, 2> buckets{};
std::map<std::pair<uint32_t, uint32_t>, D2UnitStrc*> units;
std::array<uint32_t, 4> pet{};
HudNativePanels::Flags flags{};
std::array<uint8_t, 0x164> camera{};
std::map<uint32_t, uintptr_t> bytes;
std::vector<uint8_t> executable;
bool signatureMode = false;
D2UnitStrc* __fastcall Lookup(void*, uint32_t, uint32_t id, uint32_t type) {
    const auto it = units.find({type, id});
    return it == units.end() ? nullptr : it->second;
}
const uint8_t* __fastcall Pet(uint32_t id) {
    return id == 1 || id == 15 ? reinterpret_cast<const uint8_t*>(pet.data()) : nullptr;
}
uint8_t* __fastcall Camera() { return camera.data(); }
uint8_t* __fastcall Alternate() { return nullptr; }
void Guard(void (*read)(void*), void* context) { read(context); }
void Executable(const char* filename) {
    std::ifstream in(filename, std::ios::binary | std::ios::ate);
    assert(in);
    executable.resize(size_t(in.tellg()));
    in.seekg(0);
    in.read(reinterpret_cast<char*>(executable.data()), executable.size());
    assert(in);
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(executable.data());
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(executable.data() + dos->e_lfanew);
    auto sections = IMAGE_FIRST_SECTION(nt);
    for (auto rva : {0xe9a60u, 0x66510u, 0x7ce7d0u, 0x7cf5c0u, 0x7ce930u})
        for (unsigned n = 0; n < nt->FileHeader.NumberOfSections; ++n) {
            const auto& s = sections[n];
            if (rva >= s.VirtualAddress && rva - s.VirtualAddress < s.SizeOfRawData)
                bytes[rva] = uintptr_t(executable.data() + s.PointerToRawData + rva - s.VirtualAddress);
        }
    signatureMode = true;
    assert(Inspect::World::Signatures());
    signatureMode = false;
}
} // namespace
bool Inspect::Enabled() { return true; }
Inspect::Appearance Inspect::GetAppearance() { return {}; }
std::optional<uint32_t> Inspect::Native::LocalId() { return viewer.dwUnitId; }
DWORD64 Pattern::Address(uint32_t rva) {
    if (signatureMode)
        return bytes.at(rva);
    switch (rva) {
    case 0x66510:
        return uintptr_t(Lookup);
    case 0xe9a60:
        return uintptr_t(Pet);
    case 0x1d442e0:
        return uintptr_t(buckets[0].data());
    case 0x1d446e0:
        return uintptr_t(buckets[1].data());
    case 0x1d53fb8:
        return uintptr_t(flags.data());
    case 0x7ce7d0:
    case 0x7cf5c0:
        return uintptr_t(Camera);
    case 0x7ce930:
        return uintptr_t(Alternate);
    default:
        return 0;
    }
}
int main(int argc, char** argv) {
    assert(argc == 2);
    Executable(argv[1]);
    using namespace Inspect;
    room.pDrlgRoom = &drlg;
    drlg.pLevel = &level;
    level.nLevelId = 1;
    path.pRoom = &room;
    path.wPosX = 10;
    path.wPosY = 10;
    monsterData.pMonstatsTxt=&equipmentRow;equipmentRow.dwMonStatsFlags=MONSTATSFLAG_INVENTORY;
    auto unit = [&](D2UnitStrc& u, uint32_t type, uint32_t id, uint32_t cls) {
        u.dwUnitType = type;
        u.dwClassId = cls;
        u.dwUnitId = id;
        u.dwAnimMode = 1;
        u.pDynamicPath = &path;
        if(type==1)u.pMonsterData=&monsterData;else u.pPlayerData=&data;
        units[{type, id}] = &u;
        buckets[type][id & 127] = &u;
    };
    unit(viewer, 0, 0, 3);
    unit(guest, 0, 2, 3);
    unit(merc, 1, 1, 560);
    unit(soul, 1, 15, 821);
    unit(npc, 1, 5, 150);
    unit(enemy, 1, 100, 1);
    SetReadGuard(Guard);
    assert(Native::InTown());
    World::Query q;
    q.type = 1;
    q.id = 15;
    World::CheckProtected(&q);
    assert(q.valid);
    q.id = 5;
    q.valid = false;
    World::CheckProtected(&q);
    assert(!q.valid);
    assert(World::Eligible(merc, viewer) && World::Eligible(soul, viewer));
    equipmentRow.dwMonStatsFlags=0;
    merc.pInventory=reinterpret_cast<D2InventoryStrc*>(1);
    assert(!World::Eligible(merc,viewer) && !World::Eligible(soul,viewer));
    equipmentRow.dwMonStatsFlags=MONSTATSFLAG_INVENTORY;merc.pInventory=nullptr;
    assert(World::Eligible(merc,viewer)); // Equipment-capable, currently empty.
    auto monsterPointer=soul.pMonsterData;soul.pMonsterData=nullptr;
    assert(!World::Eligible(soul,viewer));soul.pMonsterData=monsterPointer;
    assert(!World::Eligible(viewer, viewer) && !World::Eligible(npc, viewer));
    pet[3] = UINT32_MAX;
    assert(!World::Eligible(merc, viewer));
    pet[3] = 0;
    soul.dwAnimMode = 12;
    assert(!World::Eligible(soul, viewer));
    soul.dwAnimMode = 1;
    WorldData::Matrix view{}, projection{};
    view[0] = view[5] = view[10] = view[15] = 1;
    view[12] = view[14] = -20;
    projection[0] = projection[5] = projection[10] = .02f;
    projection[15] = 1;
    memcpy(camera.data(), view.data(), 64);
    memcpy(camera.data() + 0x80, projection.data(), 64);
    for (auto [w, h] : {std::pair{1280.f, 720.f}, std::pair{2560.f, 1080.f}, std::pair{3440.f, 1440.f},
                        std::pair{3840.f, 2160.f}}) {
        q = {};
        q.width = w;
        q.height = h;
        World::EyesProtected(&q);
        assert(q.eyes.size() == 3);
        for (auto eye : q.eyes)
            assert(std::abs(eye.x - w * .5f) < .01f && std::abs(eye.y - (h * .44f - 96 * h / 1080.f)) < .01f);
        auto pt = WorldData::Project(view, projection, 11, 10, 6, w, h);
        assert(pt && (*pt)[0] > w * .5f);
    }
    // Actual normal-game flags captured read-only from the user's town session.
    // In D2R these are not the legacy D2C_UIvars IDs used by the old test.
    for (int flag : {0, 12, 18, 20, 28, 29})
        flags[flag] = 1;
    static_assert(sizeof(flags) == 31);
    q.eyes.clear();
    World::EyesProtected(&q);
    assert(q.eyes.size() == 3);
    flags[UI_INVENTORY] = 1;
    q.eyes.clear();
    World::EyesProtected(&q);
    assert(q.eyes.empty());
    flags.fill(0);
    for (int town : {1, 40, 75, 103, 109}) {
        level.nLevelId = town;
        assert(Native::InTown());
    }
    CubeMap::Data::additionalTown.store(137);
    level.nLevelId = 137;
    assert(Native::InTown());
    level.nLevelId = 2;
    assert(!Native::InTown());
    q.eyes.clear();
    World::EyesProtected(&q);
    assert(q.eyes.empty());
    q.valid = false;
    World::CheckProtected(&q);
    assert(!q.valid);
    projection[15] = -1;
    assert(!WorldData::Project(view, projection, 10, 10, 6, 1920, 1080));
    projection[15] = 1;
    view[0] = NAN;
    assert(!WorldData::Project(view, projection, 10, 10, 6, 1920, 1080));
    // Replay the real world camera and companion near the upper screen edge.
    nlohmann::json capture;
    std::ifstream captured(std::filesystem::path(__FILE__).parent_path() / "fixtures" /
                           "inspect-town-camera.json");
    captured >> capture;
    view = capture["view"].get<WorldData::Matrix>();
    projection = capture["projection"].get<WorldData::Matrix>();
    auto edge = WorldData::EyePosition(view, projection, capture["unit_position"][0].get<float>(),
                                       capture["unit_position"][1].get<float>(), 2560, 1080);
    assert(edge && (*edge)[0] > 1200 && (*edge)[0] < 1300 && (*edge)[1] == 33);
    const auto oldHeight = WorldData::EyePosition(view, projection, 4393, 5628, 2560, 1080, 0);
    const auto raised = WorldData::EyePosition(view, projection, 4393, 5628, 2560, 1080, 96);
    assert(oldHeight && raised && std::abs((*oldHeight)[1] - (*raised)[1] - 96) < .01f);
    for (int active : capture["active_native_panels"])
        flags[active] = 1;
    assert(!HudNativePanels::BlocksWorldInput(flags));
    for (int blocker : {1, 2, 3, 4, 5, 9, 11, 13, 14, 15, 19, 21, 22, 23, 24, 25, 27, 30}) {
        flags[blocker] = 1;
        assert(HudNativePanels::BlocksWorldInput(flags));
        flags[blocker] = 0;
    }
    SetReadGuard(nullptr);
    assert(!Native::InTown());
    std::cout << "PASS: executable accessor signatures; custom companion registry; vendor/self/dead "
                 "exclusions; town and extended-town gates; native matrix projection at four resolutions; UI "
                 "occlusion and nonblocking skill alerts. No hooks or desktop input.\n";
}
