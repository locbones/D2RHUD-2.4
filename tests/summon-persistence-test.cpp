#include "../d2rhud/plugin/SummonPersistence.cpp"
#include <cassert>
#include <iostream>
#include <memory>

namespace {
std::map<uint32_t, uintptr_t> bindings;
}
DWORD64 Pattern::Address(uint32_t rva) { return bindings[rva]; }
bool CubeMap::SummonLifecycleAvailable() { return true; }
const char* CubeMap::Status() { return "Fixture lifecycle available"; }
const char* CubeMap::SummonLifecycleStatus() { return "Fixture lifecycle available"; }

using namespace SummonPersistence;
template<size_t N> using Bytes = std::array<uint8_t, N>;
template<class T> void Put(void* destination, size_t offset, T value) {
    memcpy(static_cast<uint8_t*>(destination) + offset, &value, sizeof(value));
}
template<class T> void Bind(uint32_t rva, T address) { bindings[rva] = reinterpret_cast<uintptr_t>(address); }

struct PetFixture {
    Unit unit{};
    D2MonsterDataStrc monster{};
    Bytes<0x38> node{};
    Bytes<0x38> event{};
    int type = 0;
    explicit PetFixture(int id, int monsterId, int petType) : type(petType) {
        unit.dwUnitType = 1; unit.dwUnitId = id; unit.dwClassId = monsterId;
        unit.dwAnimMode = 1; unit.pMonsterData = &monster;
        unit.pSeed.LowSeed() = id * 17;
        Put(node.data(), 4, uint32_t(id));
    }
};
struct Fixture {
    Game game{};
    Unit player{};
    D2PlayerDataStrc playerData{};
    Client client{};
    Bytes<0x100> path{}, skills{};
    Bytes<0x10> list{}, headers{};
    Bytes<0x14f> save{};
    Bytes<0x100> room{};
    std::array<Bytes<0x10>, 32> entries{};
    std::vector<Bytes<0x284>> rows{1024};
    std::vector<Bytes<0x1dc>> monsters{1600};
    std::map<int, Bytes<0x48>> skillObjects;
    std::vector<std::unique_ptr<PetFixture>> owned;
    std::map<uint32_t, Unit*> live;
    uint8_t* rowTable = nullptr;
    uint64_t rowCount = rows.size();
    uint8_t* monsterTable = nullptr;
    uint64_t monsterCount = monsters.size();
    uint8_t* formulaTable = nullptr;
    uint64_t formulaCount = 16;
    Bytes<16> formulas{};
    int typeCount = 32, nextId = 1000, spawnCalls = 0, passiveCalls = 0;
    int restoreMaximum = 32;
    int skeletonComponents = 0, mageComponents = 0, hydraPreparations = 0, townCastAttempts = 0;
    int failSkill = -1, remainingFailures = 0, forcedAlignmentCalls = 0;
    int restoredLevel = 0;
    int summonMode = 8; // S1, including Blood Golem's HD hide/reveal timeline.
    std::vector<int> castModes, restoredModes;
    std::array<SkillDo, 256> implementations{};
    std::array<char, 128> mod{};
    void Initialize() {
        pets.clear(); sessions.clear(); loading.clear(); restoring = nullptr;
        ready.store(true); enabled.store(true); configured.store(true);
        player.dwUnitType = 0; player.dwUnitId = 1; player.dwClassId = 17;
        player.dwAnimMode = 1; player.pPlayerData = &playerData;
        player.pDynamicPath = reinterpret_cast<D2DynamicPathStrc*>(path.data());
        Put(&player, 0x100, skills.data());
        Put(skills.data(), 0x18, reinterpret_cast<void*>(uintptr_t(0x1234)));
        Put(&playerData, 0x98, list.data()); Put(list.data(), 0, entries.data());
        playerData.pClient = &client; client.pGame = &game; client.pPlayer = &player; client.dwClientState = 4;
        client.pSaveHeaders = reinterpret_cast<D2SaveHeadersStrc*>(headers.data());
        Put(headers.data(), 0, save.data()); Put(headers.data(), 8, uint64_t(save.size()));
        Put(save.data(), 0, uint32_t(0xaa55aa55)); Put(save.data(), 4, uint32_t(98));
        Put(save.data(), 8, uint32_t(save.size())); Put(save.data(), 12, uint32_t(0xabc123));
        Put(save.data(), 0x24, uint32_t(0x24)); Put(save.data(), 0x28, uint8_t(17));
        Put(save.data(), 0x2c, uint32_t(1700000123));
        memcpy(save.data() + 0x10b, "CustomSummoner", 14);
        memcpy(mod.data(), "summon-fixture", 15);
        rowTable = reinterpret_cast<uint8_t*>(rows.data()); formulaTable = formulas.data();
        monsterTable = reinterpret_cast<uint8_t*>(monsters.data());
        Bind(0x1bf084f, mod.data()); Bind(0x1c9fad0, &rowTable); Bind(0x1c9fad8, &rowCount);
        Bind(0x1c9fbd0, &typeCount); Bind(0x1c9eab8, &formulaTable); Bind(0x1c9eac0, &formulaCount);
        Bind(0x14bf500, implementations.data());
        Bind(0x1c9f878, &monsterTable); Bind(0x1c9f880, &monsterCount);
    }
    void Define(int skill, int function, int type) {
        rows[skill].fill(0xff);
        Put(rows[skill].data(), NativeData::DoOffset, int16_t(function));
        Put(rows[skill].data(), 0xde, int8_t(type));
        Put(rows[skill].data(), 0xdc, int16_t(1500));
        Put(rows[skill].data(), 0xe0, uint32_t(0));
        auto& object = skillObjects[skill]; Put(object.data(), 0x38, int(20));
    }
    Unit* Add(int skill, int monsterId, int type, int level = 12) {
        SummonArg arguments; arguments.owner = &player; arguments.monster = int16_t(monsterId);
        arguments.petType = type; arguments.maximum = restoreMaximum;
        arguments.mode = summonMode;
        auto unit = FactoryHook(&game, &arguments);
        if (unit) PassivesHook(&game, &player, unit, skill, level, 0);
        return unit;
    }
    void ClearWorld() { live.clear(); owned.clear(); entries = {}; Destroyed(&game); }
    Data::Snapshot Snapshot() {
        Data::Snapshot header; assert(Header(save.data(), save.size(), header));
        auto snapshot = Data::Read(RosterPath(header.identity), header.identity, header.checksum, header.saveSize);
        assert(snapshot); return *snapshot;
    }
    void Rejoin() { BeforeLoad(&game, &client); AfterLoad(&game, &client, &player, true); Tick(&game); }
};
Fixture* current = nullptr;

int __fastcall MockStat(Unit*, int stat, uint16_t) { return stat == 7 ? 1000 : stat == 6 ? 1000 : 0; }
Unit* __fastcall MockLookup(Game*, int type, uint32_t id) {
    assert(type == 1); auto found = current->live.find(id); return found == current->live.end() ? nullptr : found->second;
}
Room* __fastcall MockRoom(Unit*) { return reinterpret_cast<Room*>(current->room.data()); }
void* __fastcall MockSkill(Unit*, int id) { auto it = current->skillObjects.find(id); return it == current->skillObjects.end() ? nullptr : it->second.data(); }
int __fastcall MockBonus(Unit*, void*, bool) { return 0; }
int __fastcall MockFormula(const uint8_t* bytes, int, void*, void*, int, void*) {
    return bytes == current->formulaTable + 1 ? 250 : bytes == current->formulaTable ? current->restoreMaximum : 0;
}
void __fastcall MockTarget(Unit* unit, Unit*) { memset(reinterpret_cast<uint8_t*>(unit->pDynamicPath) + 0x70, 0, 16); }
Unit* __fastcall MockFactory(Game*, SummonArg* args) {
    ++current->spawnCalls;
    if (restoring && restoring->record->skill == current->failSkill && current->remainingFailures > 0) {
        --current->remainingFailures;
        return nullptr;
    }
    auto pet = std::make_unique<PetFixture>(current->nextId++, args->monster, args->petType);
    (restoring ? current->restoredModes : current->castModes).push_back(args->mode);
    pet->unit.dwAnimMode = args->mode;
    auto& entry = current->entries.at(args->petType);
    Put(pet->node.data(), 0x18, Field<void*>(entry.data()));
    Put(entry.data(), 0, pet->node.data()); Put(entry.data(), 8, Field<int>(entry.data(), 8) + 1);
    auto unit = &pet->unit; current->live[unit->dwUnitId] = unit;
    current->owned.push_back(std::move(pet)); return unit;
}
int __fastcall MockPassives(Game*, Unit*, Unit*, int, int, int) { ++current->passiveCalls; return 19; }
int __fastcall MockMonster(Unit*, int, int skill, int, int* mode, int*, int*) { *mode = current->summonMode; return Field<int16_t>(current->rows[skill].data(), 0xdc); }
int __fastcall MockSkeletonStats(Game*, Unit*, Unit*, int, int) { return 1; }
void __fastcall MockPhysicalComponents(Unit*, Unit*, int, int, int) { ++current->skeletonComponents; }
void __fastcall MockMageComponents(Unit*, Unit*, int, int, int, int) { ++current->mageComponents; }
void __fastcall MockAlign(Unit*, Unit*) {}
void __fastcall MockAiEvent(Game*, Unit*, int, int) {}
void __fastcall MockSummonSkill(Unit*, int, int, int) { ++current->hydraPreparations; }
void __fastcall MockSummonFlags(Unit*, int, int) { ++current->forcedAlignmentCalls; }
int __fastcall MockDo(Game* game, Unit* owner, int skill, int level) {
    current->restoredLevel = level;
    const auto record = restoring->record;
    SummonArg args; args.owner = owner; args.monster = int16_t(record->monster);
    args.petType = record->petType; args.maximum = current->restoreMaximum;
    args.mode = current->summonMode;
    if (auto unit = FactoryHook(game, &args)) PassivesHook(game, owner, unit, skill, level, 0);
    return 1;
}
int __fastcall MockTownDenied(Game*, Unit*, int, int) { ++current->townCastAttempts; return 0; }
void BindFunctions() {
    Bind(0x1e5620, MockStat); Bind(0x2c49c0, MockLookup); Bind(0x209530, MockRoom);
    Bind(0x218ce0, MockSkill); Bind(0x21a990, MockBonus); Bind(0x6d08c0, MockFormula);
    Bind(0x2095a0, MockTarget); originalFactory = MockFactory; originalPassives = MockPassives;
    Bind(0x21e3a0, MockMonster); Bind(0x368a90, MockSkeletonStats);
    Bind(0x367d40, MockPhysicalComponents); Bind(0x367af0, MockMageComponents);
    Bind(0x367910, MockAlign); Bind(0x3c5410, MockAiEvent);
    Bind(0x29bf70, MockSummonSkill); Bind(0x2c74a0, MockSummonFlags);
}
struct TemporaryDirectory {
    std::wstring old;
    std::filesystem::path path;
    TemporaryDirectory() {
        const auto count = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
        if (count) { old.resize(count); GetEnvironmentVariableW(L"LOCALAPPDATA", old.data(), count); old.resize(count - 1); }
        path = std::filesystem::temp_directory_path() / (L"D2RHUD-summon-test-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
        assert(std::filesystem::create_directories(path));
        assert(SetEnvironmentVariableW(L"LOCALAPPDATA", path.c_str()));
    }
    ~TemporaryDirectory() {
        SetEnvironmentVariableW(L"LOCALAPPDATA", old.empty() ? nullptr : old.c_str());
        // The test owns this unique, absolute temporary directory only.
        assert(path.is_absolute() && path.filename().wstring().starts_with(L"D2RHUD-summon-test-"));
        std::filesystem::remove_all(path);
    }
};

int main() {
    TemporaryDirectory directory;
    auto fixture = std::make_unique<Fixture>(); current = fixture.get();
    fixture->Initialize(); BindFunctions(); fixture->implementations[56] = MockDo;
    // Enabling prepares the folder before the first save, including when native
    // installation is unavailable. It must not manufacture a character roster.
    assert(!std::filesystem::exists(Directory()));
    ready.store(false); SetEnabled(true);
    assert(std::filesystem::is_directory(Directory()));
    assert(std::filesystem::is_empty(Directory()));
    ready.store(true);
    const auto blocked = directory.path / "blocked";
    { std::ofstream file(blocked); file << "occupied"; }
    assert(!PrepareDirectory(blocked / "Summons"));
    assert(StorageStatus().find("Cannot create summon folder") != std::string::npos);
    SetEnabled(true);
    fixture->Define(900, 56, 20); fixture->Define(901, 56, 21);
    fixture->Define(902, 114, 22); Put(fixture->rows[902].data(), 0x160, uint32_t(1));
    fixture->Define(903, 144, 23); Put(fixture->rows[903].data(), 0x158, uint32_t(1));
    fixture->Define(904, 144, 24); Put(fixture->rows[904].data(), 0x164, uint32_t(1));
    const auto saveBefore = fixture->save;
    auto survivor = fixture->Add(900, 1500, 20);
    assert(survivor->dwAnimMode == 8 && fixture->castModes.back() == 8);
    auto dead = fixture->Add(900, 1500, 20); dead->dwAnimMode = 12;
    auto gone = fixture->Add(900, 1500, 20); Removed(&fixture->game, gone);
    auto iron = fixture->Add(900, 291, 20);
    auto ironType = fixture->Add(900, 1500, 7);
    auto timed = fixture->Add(900, 1500, 20);
    fixture->owned.back()->monster.nMonUmod[0] = 21;
    Bytes<0x38> periodicModifier{}; periodicModifier[0] = 7;
    Put(survivor, 0x140, periodicModifier.data());
    assert(Eligible(&fixture->player, survivor, pets.at({&fixture->game, survivor->dwUnitId}).record));
    auto deleted = fixture->Add(900, 1500, 20);
    fixture->owned.back()->event[0] = 4; Put(deleted, 0x140, fixture->owned.back()->event.data());
    for (int index = 0; auto ai : {86, 101, 102, 104, 107}) {
        const int skill = 910 + index, monsterId = 1510 + index++;
        fixture->Define(skill, 56, 21); Put(fixture->rows[skill].data(), 0xdc, int16_t(monsterId));
        Put(fixture->monsters[monsterId].data(), 0x4e, int16_t(ai));
        fixture->Add(skill, monsterId, 21);
    }
    // Actual summoned variant controls finite lifetime, even when the skill's
    // base monster row identifies a different permanent AI.
    fixture->Add(900, 1510, 20);
    Bytes<0x38> auraExpiration{}; auraExpiration[0] = 12;
    Put(survivor, 0x140, auraExpiration.data());
    const auto survivorSeed = uint32_t(survivor->pSeed.LowSeed());
    Saved(&fixture->game, &fixture->player, fixture->save.data(), fixture->save.size());
    auto snapshot = fixture->Snapshot();
    if (snapshot.pets.size() != 1) std::cerr << "Unexpected survivor snapshot: " << Data::Encode(snapshot) << '\n';
    assert(snapshot.pets.size() == 1 && snapshot.pets.front().skill == 900 && snapshot.pets.front().monster == 1500);
    assert(iron->dwAnimMode == 8 && ironType->dwAnimMode == 8 && timed->dwAnimMode == 8);
    assert(fixture->save == saveBefore);

    fixture->ClearWorld();
    const int spawnBefore = fixture->spawnCalls;
    const auto oldSkills = fixture->skills; const auto oldPath = fixture->path; fixture->player.dwFlags = 0x40;
    fixture->Rejoin();
    assert(fixture->spawnCalls == spawnBefore + 1 && fixture->live.size() == 1);
    assert(fixture->owned.front()->unit.dwAnimMode == 1 && fixture->restoredModes.back() == 1);
    assert(uint32_t(fixture->owned.front()->unit.pSeed.LowSeed()) == survivorSeed);
    assert(fixture->skills == oldSkills && fixture->path == oldPath && fixture->player.dwFlags == 0x40);
    Tick(&fixture->game); assert(fixture->spawnCalls == spawnBefore + 1);
    assert(fixture->save == saveBefore);
    assert(std::all_of(fixture->restoredModes.begin(), fixture->restoredModes.end(), [](int mode) { return mode == 1; }));

    fixture->ClearWorld(); Put(fixture->save.data(), 12, uint32_t(0xabc124));
    fixture->Rejoin(); assert(fixture->live.empty());
    fixture->save = saveBefore;
    BeforeLoad(&fixture->game, &fixture->client);
    AfterLoad(&fixture->game, &fixture->client, &fixture->player, false);
    Tick(&fixture->game); assert(fixture->live.empty());

    // Current skills and current native pet limits control restoration.
    fixture->ClearWorld(); fixture->skillObjects.erase(900); fixture->Rejoin(); assert(fixture->live.empty());
    fixture->Define(900, 56, 20); Put(fixture->skillObjects[900].data(), 0x38, int(3));
    fixture->ClearWorld(); fixture->Rejoin(); assert(fixture->restoredLevel == 3);
    fixture->ClearWorld(); fixture->restoreMaximum = 1;
    auto existing = fixture->Add(900, 1500, 20); const int atLimit = fixture->spawnCalls;
    fixture->Rejoin(); assert(fixture->spawnCalls == atLimit && fixture->live.at(existing->dwUnitId) == existing);

    fixture->ClearWorld(); SetEnabled(false); fixture->Rejoin(); assert(fixture->live.empty());
    Saved(&fixture->game, &fixture->player, fixture->save.data(), fixture->save.size());
    assert(fixture->Snapshot().pets.empty());
    SetEnabled(true); fixture->ClearWorld(); fixture->Rejoin(); assert(fixture->live.empty());
    assert(fixture->save == saveBefore);

    // Exercise every supported native permanent summon implementation. Native
    // class and skill IDs never select eligibility; custom rows share routines.
    fixture->restoreMaximum = 32;
    int index = 0;
    for (int function : {16, 49, 56, 114, 115, 119, 144}) {
        fixture->implementations[function] = MockDo;
        fixture->Define(920 + index, function, 25);
        fixture->Add(920 + index++, 1500, 25);
    }
    for (int monsterId : {363, 364}) {
        const int skill = monsterId == 363 ? 930 : 931;
        fixture->Define(skill, 31, 26); Put(fixture->rows[skill].data(), 0xdc, int16_t(monsterId));
        auto pet = fixture->Add(skill, monsterId, 26);
        for (int component = 0; component < 16; ++component) pet->pMonsterData->nComponent[component] = uint8_t(component + 11);
    }
    Saved(&fixture->game, &fixture->player, fixture->save.data(), fixture->save.size());
    assert(fixture->Snapshot().pets.size() == 9);
    fixture->ClearWorld(); fixture->Rejoin(); assert(fixture->live.size() == 9);
    assert(fixture->skeletonComponents == 1 && fixture->mageComponents == 1);
    for (const auto& pet : fixture->owned) if (pet->type == 26)
        for (int component = 0; component < 16; ++component) assert(pet->monster.nComponent[component] == component + 11);
    assert(fixture->save == saveBefore);

    // Positive calc values can mean HP or attack counters for custom AI and
    // must not suppress permanent companions. Hydra's three saved variants
    // restore once each without invoking its normal three-spawn/town gate.
    fixture->ClearWorld();
    fixture->implementations[144] = MockTownDenied;
    fixture->Define(905, 144, 27);
    Put(fixture->rows[905].data(), 0x158, uint32_t(1)); Put(fixture->rows[905].data(), 0x164, uint32_t(1));
    fixture->Add(905, 1500, 27); fixture->Add(905, 1501, 27); fixture->Add(905, 1502, 27);
    fixture->Add(902, 1500, 22);
    Saved(&fixture->game, &fixture->player, fixture->save.data(), fixture->save.size());
    assert(fixture->Snapshot().pets.size() == 4);
    const int preparations = fixture->hydraPreparations;
    fixture->ClearWorld(); fixture->Rejoin(); assert(fixture->live.size() == 4);
    assert(fixture->hydraPreparations == preparations + 3);
    assert(fixture->townCastAttempts == 0);
    assert(fixture->forcedAlignmentCalls == 0); // Preserve factory-created friendly alignment.
    const int once = fixture->spawnCalls; Tick(&fixture->game); assert(fixture->spawnCalls == once);
    assert(fixture->save == saveBefore);

    // A crowded joining tile can reject only part of an army. Keep successful
    // placements, preserve the unplaced record on an early save, and retry the
    // failed placement at the scheduled frame without duplicating survivors.
    fixture->ClearWorld();
    fixture->Define(940, 56, 28); fixture->Define(941, 56, 28);
    auto first = fixture->Add(940, 1500, 28); auto second = fixture->Add(941, 1501, 28);
    const uint32_t firstSeed = uint32_t(first->pSeed.LowSeed()), secondSeed = uint32_t(second->pSeed.LowSeed());
    Saved(&fixture->game, &fixture->player, fixture->save.data(), fixture->save.size());
    fixture->ClearWorld(); fixture->failSkill = 941; fixture->remainingFailures = 1;
    fixture->Rejoin(); assert(fixture->live.size() == 1);
    const auto successfulId = fixture->owned.front()->unit.dwUnitId;
    assert(uint32_t(fixture->owned.front()->unit.pSeed.LowSeed()) == firstSeed);
    assert(sessions.at({&fixture->game, fixture->player.dwUnitId}).pending);
    Saved(&fixture->game, &fixture->player, fixture->save.data(), fixture->save.size());
    auto earlySnapshot = fixture->Snapshot(); assert(earlySnapshot.pets.size() == 2);
    assert(std::count_if(earlySnapshot.pets.begin(), earlySnapshot.pets.end(), [](const auto& record) { return record.skill == 940; }) == 1);
    assert(std::count_if(earlySnapshot.pets.begin(), earlySnapshot.pets.end(), [](const auto& record) { return record.skill == 941; }) == 1);
    const int waiting = fixture->spawnCalls;
    Tick(&fixture->game); fixture->game.dwGameFrame += 4; Tick(&fixture->game);
    assert(fixture->spawnCalls == waiting && fixture->live.size() == 1);
    ++fixture->game.dwGameFrame; Tick(&fixture->game);
    assert(fixture->spawnCalls == waiting + 1 && fixture->live.size() == 2);
    assert(fixture->live.contains(successfulId));
    assert(uint32_t(fixture->owned.back()->unit.pSeed.LowSeed()) == secondSeed);
    assert(!sessions.at({&fixture->game, fixture->player.dwUnitId}).pending);
    const int recovered = fixture->spawnCalls;
    fixture->game.dwGameFrame += 100; Tick(&fixture->game); assert(fixture->spawnCalls == recovered);
    Saved(&fixture->game, &fixture->player, fixture->save.data(), fixture->save.size());
    assert(fixture->Snapshot().pets.size() == 2);
    assert(fixture->save == saveBefore && fixture->forcedAlignmentCalls == 0);

    // Player mode 12 is a skill animation; player death uses mode 17 or the
    // explicit dead flag even when the mocked stat query still reports HP.
    fixture->player.dwAnimMode = 12;
    Saved(&fixture->game, &fixture->player, fixture->save.data(), fixture->save.size());
    assert(fixture->Snapshot().pets.size() == 2);
    fixture->player.dwAnimMode = 17;
    Saved(&fixture->game, &fixture->player, fixture->save.data(), fixture->save.size());
    assert(fixture->Snapshot().pets.empty());
    fixture->player.dwAnimMode = 1; fixture->player.dwFlags |= 0x10000;
    Saved(&fixture->game, &fixture->player, fixture->save.data(), fixture->save.size());
    assert(fixture->Snapshot().pets.empty());
    fixture->player.dwFlags &= ~0x10000u;
    Saved(&fixture->game, &fixture->player, fixture->save.data(), fixture->save.size());
    const auto aliveSnapshot = fixture->Snapshot(); assert(aliveSnapshot.pets.size() == 2);
    for (bool deadFlag : {false, true}) {
        fixture->ClearWorld();
        assert(Data::AtomicWrite(RosterPath(aliveSnapshot.identity), aliveSnapshot));
        fixture->player.dwAnimMode = deadFlag ? 1 : 17;
        fixture->player.dwFlags = deadFlag ? 0x10000 : 0;
        const int beforeDeath = fixture->spawnCalls;
        fixture->Rejoin(); assert(fixture->live.empty() && fixture->spawnCalls == beforeDeath);
        Saved(&fixture->game, &fixture->player, fixture->save.data(), fixture->save.size());
        assert(fixture->Snapshot().pets.empty());
    }
    fixture->player.dwAnimMode = 1; fixture->player.dwFlags = 0;

    // Persistent crowding slows retries after 125 attempts; it must never
    // discard an unplaced surviving pet or erase it from an early save.
    fixture->ClearWorld(); fixture->Add(941, 1501, 28);
    Saved(&fixture->game, &fixture->player, fixture->save.data(), fixture->save.size());
    fixture->ClearWorld(); fixture->remainingFailures = 150;
    fixture->Rejoin(); assert(fixture->live.empty());
    const Key sessionKey{&fixture->game, fixture->player.dwUnitId};
    for (int attempt = 1; attempt < 126; ++attempt) {
        fixture->game.dwGameFrame = sessions.at(sessionKey).nextFrame;
        Tick(&fixture->game);
    }
    assert(fixture->live.empty() && sessions.at(sessionKey).pending && sessions.at(sessionKey).attempts == 126);
    assert(sessions.at(sessionKey).nextFrame == fixture->game.dwGameFrame + 25);
    Saved(&fixture->game, &fixture->player, fixture->save.data(), fixture->save.size());
    assert(fixture->Snapshot().pets.size() == 1);
    const int crowded = fixture->spawnCalls;
    fixture->game.dwGameFrame += 24; Tick(&fixture->game); assert(fixture->spawnCalls == crowded);
    fixture->remainingFailures = 0; ++fixture->game.dwGameFrame; Tick(&fixture->game);
    assert(fixture->spawnCalls == crowded + 1 && fixture->live.size() == 1 && !sessions.at(sessionKey).pending);
    fixture->game.dwGameFrame += 25; Tick(&fixture->game); assert(fixture->spawnCalls == crowded + 1);
    assert(fixture->save == saveBefore);
    assert(std::all_of(fixture->castModes.begin(), fixture->castModes.end(), [](int mode) { return mode == 8; }));
    assert(std::all_of(fixture->restoredModes.begin(), fixture->restoredModes.end(), [](int mode) { return mode == 1; }));
    std::cout << "Summon save/rejoin, survivor filtering, custom rows, Iron Golem exclusions, native limits, skill changes, crowded-room retries, pending-save preservation, one-shot restore and unchanged native save tests passed.\n";
}
