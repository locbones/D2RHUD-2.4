// Runs the production projection against native-shaped stat lists. With an
// executable argument, hooks and executes a private image, never a live game.
#include "../d2rhud/plugin/SunderSystem.cpp"
#include "../d2rhud/plugin/ImmunityLabels.cpp"
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <tuple>
#include <vector>
#include <string_view>
#include "sunder-resistance-cost-fixture.inl"

template<class T> static void Put(void* address, size_t offset, T value) {
    memcpy(static_cast<unsigned char*>(address) + offset, &value, sizeof(value));
}
template<size_t N> using Bytes = std::array<unsigned char, N>;
static unsigned char* image = nullptr;
static Bytes<0x1300> tables{};
static std::array<uintptr_t, 8> localClients{};
static std::vector<Bytes<0x144>> costs(512);
DWORD64 Pattern::Address(uint32_t rva) {
    if (image) return reinterpret_cast<uintptr_t>(image + rva);
    if (rva == 0x1d637f0) return reinterpret_cast<uintptr_t>(localClients.data());
    if (rva == 0x1c9e980) return reinterpret_cast<uintptr_t>(tables.data());
    return 0;
}
using LookupKey = std::tuple<D2GameStrc*, uint32_t, uint32_t>;
static std::map<LookupKey, D2UnitStrc*> units;
static std::map<std::pair<D2UnitStrc*, uint32_t>, int> playerStats;
static D2UnitStrc* __fastcall Lookup(D2GameStrc* game, D2C_UnitTypes type, uint32_t id) {
    auto found = units.find({game, uint32_t(type), id});
    return found == units.end() ? nullptr : found->second;
}
static int __fastcall PlayerStat(D2UnitStrc* player, uint32_t stat, uint16_t) {
    return playerStats[{player, stat}];
}
struct NativeEntry { uint32_t key; int value; };
struct List {
    Bytes<0xb58> memory{};
    std::vector<NativeEntry> base, aggregate;
    D2StatListExStrc* get() { return reinterpret_cast<D2StatListExStrc*>(memory.data()); }
    void Sync() {
        Put(memory.data(), 0x30, base.data()); Put(memory.data(), 0x38, uint64_t(base.size()));
        Put(memory.data(), 0x80, aggregate.data()); Put(memory.data(), 0x88, uint64_t(aggregate.size()));
    }
    void Values(int nativeBase, int nativeTotal) {
        base = {{39u << 16, nativeBase}}; aggregate = {{39u << 16, nativeTotal}}; Sync();
    }
    void Root(D2UnitStrc& owner, int nativeBase, int nativeTotal) {
        memory = {}; Values(nativeBase, nativeTotal);
        Put(memory.data(), 8, owner.dwUnitType); Put(memory.data(), 0xc, owner.dwUnitId);
        Put(memory.data(), 0x1c, uint32_t(0x80000000)); Put(memory.data(), 0x78, &owner);
        owner.pStatListEx = get();
    }
    void Child(List& root, int value, uint32_t flags = 0) {
        memory = {}; Values(value, value);
        Put(memory.data(), 0x1c, flags); Put(memory.data(), 0x58, root.get());
    }
};
static int __fastcall MockBase(D2StatListExStrc* list, uint32_t encoded, const void*) {
    int value = 0; assert(SunderSystem::Raw(reinterpret_cast<uintptr_t>(list), false, encoded >> 16, value)); return value;
}
static int __fastcall MockTotal(D2StatListExStrc* list, uint32_t encoded, const void*) {
    int value = 0; assert(SunderSystem::Raw(reinterpret_cast<uintptr_t>(list), true, encoded >> 16, value)); return value;
}
struct Fixture {
    D2GameStrc game{}, otherGame{};
    D2ClientStrc first{}, second{}, otherClient{};
    D2UnitStrc monster{}, otherMonster{}, player{}, teammate{}, otherPlayer{};
    List root, otherRoot, playerRoot, teammateRoot, otherPlayerRoot;
    Fixture() {
        units.clear(); playerStats.clear(); localClients = {}; tables = {};
        for (auto& row : costs) row = {};
        for (auto* unit : {&monster, &otherMonster}) { unit->dwUnitType = UNIT_MONSTER; unit->dwUnitId = 123; unit->dwClassId = 10; }
        for (auto* unit : {&player, &teammate, &otherPlayer}) unit->dwUnitType = UNIT_PLAYER;
        player.dwUnitId = 1; teammate.dwUnitId = 2; otherPlayer.dwUnitId = 3;
        root.Root(monster, 180, 180); otherRoot.Root(otherMonster, 180, 180);
        playerRoot.Root(player, 0, 0); teammateRoot.Root(teammate, 0, 0); otherPlayerRoot.Root(otherPlayer, 0, 0);
        units[{&game, UNIT_MONSTER, 123}] = &monster; units[{&otherGame, UNIT_MONSTER, 123}] = &otherMonster;
        units[{&game, UNIT_PLAYER, 1}] = &player; units[{&game, UNIT_PLAYER, 2}] = &teammate;
        units[{&otherGame, UNIT_PLAYER, 3}] = &otherPlayer;
        Client(first, game, player.dwUnitId, &second); Client(second, game, teammate.dwUnitId, nullptr);
        Client(otherClient, otherGame, otherPlayer.dwUnitId, nullptr);
        Put(&game, 0x150, &first); Put(&otherGame, 0x150, &otherClient);
        localClients[0] = reinterpret_cast<uintptr_t>(&first); localClients[1] = reinterpret_cast<uintptr_t>(&otherClient);
        Publish(); UNITS_GetServerUnitByTypeAndId = Lookup; STATLIST_GetUnitStatSigned = PlayerStat;
        SunderSystem::enabled.store(true); SunderSystem::ready.store(true);
        SunderSystem::originalBase = MockBase; ImmunityLabels::originalRead = MockTotal;
        Pierce(85, 40, 1);
    }
    static void Client(D2ClientStrc& client, D2GameStrc& game, uint32_t id, D2ClientStrc* next) {
        Put(&client, 0x230, &game); Put(&client, 0x190, id); Put(&client, 0x558, next);
    }
    void Publish() {
        auto* data = image ? image + 0x1c9e980 : tables.data();
        Put(data, 0x11f8, costs.data()); Put(data, 0x1200, int64_t(costs.size()));
        if (image) {
            memcpy(image + 0x1d637f0, localClients.data(), sizeof(localClients));
            Put(image, 0x1c9ee58, uintptr_t(0)); Put(image, 0x1c9ee60, uint64_t(0));
        }
    }
    void Pierce(int own, int shared, int other) {
        playerStats[{&player, 189}] = own; playerStats[{&teammate, 189}] = shared; playerStats[{&otherPlayer, 189}] = other;
    }
    int Project(int native) { return SunderSystem::ProjectResistance(&game, &monster, 39, native); }
    void Attach(List* firstChild) { Put(root.memory.data(), 0x68, firstChild ? firstChild->get() : nullptr); }
};

static void ProjectionCases() {
    Fixture f;
    assert(f.Project(180) == 99);
    const auto pristine = f.root.memory; const auto base = f.root.base; const auto total = f.root.aggregate;
    for (int i = 0; i < 100; ++i) assert(f.Project(180) == 99);
    assert(f.root.memory == pristine && f.root.base[0].value == base[0].value && f.root.aggregate[0].value == total[0].value);
    f.Pierce(60, 80, 500); assert(f.Project(180) == 100); // Per-game maximum, never sum or other game.
    f.Pierce(0, 0, 500); assert(f.Project(180) == 180);
    f.Pierce(85, 0, 0); assert(f.Project(180) == 99);
    assert(SunderSystem::ProjectResistance(nullptr, &f.monster, 39, 180) == 99);
    assert(SunderSystem::ProjectResistance(&f.otherGame, &f.monster, 39, 180) == 180);
    assert(SunderSystem::ProjectResistance(&f.otherGame, &f.otherMonster, 39, 180) == 180);
    f.root.Values(174, 174); assert(f.Project(174) == 99); // Final UMod values are read afresh.
    f.root.Values(99, 99); assert(f.Project(99) == 99);
    f.root.Values(100, 100); f.Pierce(1, 0, 0); assert(f.Project(100) == 99);
    f.root.Values(180, 180); f.Pierce(85, 0, 0);

    List negative, positive, nested;
    negative.Child(f.root, -50); f.Attach(&negative); f.root.Values(180, 130);
    assert(f.Project(130) == 49); // Unknown/custom negative state is downstream of sunder.
    positive.Child(f.root, 75); Put(positive.memory.data(), 0x48, negative.get()); f.Attach(&positive);
    f.root.Values(99, 124); assert(f.Project(124) == 49); // Positive state creates 174 immunity.
    positive.Values(95, 95); f.root.Values(99, 144); assert(f.Project(144) == 59);
    f.root.Values(160, 205); assert(f.Project(205) == 120); // Insufficient pierce retains immunity view.
    Sunder::Projection projection{}; const void* cost = nullptr;
    assert(SunderSystem::Project(&f.game, &f.monster, 39, projection, cost)); assert(projection.immunity == 170);

    // Nested extended lists contribute their aggregate once; child base is not added again.
    nested.Child(f.root, 5, 0x80000000u); nested.Values(5, 75); f.Attach(&nested);
    f.root.Values(99, 174); assert(f.Project(174) == 99);
    nested.Values(5, 90); f.root.Values(99, 189); assert(f.Project(189) == 104);
    // SET lists and malformed ownership are unsupported and preserve native resistance.
    Put(nested.memory.data(), 0x1c, uint32_t(0x2000)); assert(f.Project(189) == 189);
    Put(nested.memory.data(), 0x1c, uint32_t(0x80000000)); Put(nested.memory.data(), 0x58, f.otherRoot.get());
    assert(f.Project(189) == 189); Put(nested.memory.data(), 0x58, f.root.get());
    Put(nested.memory.data(), 0x48, nested.get()); assert(f.Project(189) == 189);
    Put(nested.memory.data(), 0x48, static_cast<D2StatListExStrc*>(nullptr));
    f.root.Values(99, 188); assert(f.Project(188) == 188); // Inconsistent fold.
    f.root.Values(INT_MAX, INT_MAX); nested.Values(1, 1); assert(f.Project(INT_MAX) == INT_MAX);
    f.Attach(nullptr); f.root.Values(180, 180);
    assert(f.Project(179) == 179); // Native getter mismatch fails closed.
    costs[39][0x4e] = 1; assert(f.Project(180) == 180); costs[39][0x4e] = 0;
    SunderSystem::SetEnabled(false); assert(f.Project(180) == 180); SunderSystem::SetEnabled(true);
    Put(&f.second, 0x558, &f.first); assert(f.Project(180) == 180); Put(&f.second, 0x558, static_cast<D2ClientStrc*>(nullptr));
    // Dynamic lists skipped by native stat flags must also be skipped by the projection.
    costs[39][4] = 4; positive.Child(f.root, 75, 0x40000000u); f.Attach(&positive);
    assert(f.Project(180) == 99); costs[39][4] = 0; assert(f.Project(180) == 180);
    f.Attach(nullptr); costs[39][4] = 0;
    costs[39][5] = 2; Put(costs[39].data(), 0x28, 70); assert(f.Project(180) == 99);
    negative.Child(f.root, -50); f.Attach(&negative); f.root.Values(180, 130); assert(f.Project(130) == 70);
    costs[39][0x14] = 31; assert(f.Project(130) == 130);
    assert(!SunderSystem::Project(&f.game, &f.monster, 39, projection, cost));
    costs[39][0x14] = 0; Put(costs[39].data(), 0x28, 100);
    assert(f.Project(130) == 130);
    assert(!SunderSystem::Project(&f.game, &f.monster, 39, projection, cost));
    // Native raw total is already minimum-clamped: both views still agree.
    Put(costs[39].data(), 0x28, 70);
    negative.Values(-150, -150); f.root.Values(180, 30);
    assert(f.Project(70) == 70);
    assert(SunderSystem::Project(&f.game, &f.monster, 39, projection, cost));
    assert(projection.immunity == 99);

    std::cout << "Runtime projection: game isolation, equipment maximum, finalized UMods, states, nested folds and malformed data passed.\n";
}

static void ResetHooks() {
    SunderSystem::ready.store(false); SunderSystem::attempted = false; SunderSystem::originalBase = nullptr;
    ImmunityLabels::attempted = ImmunityLabels::installed = false; ImmunityLabels::originalRead = nullptr;
    ImmunityLabels::projectResistance = nullptr; ImmunityLabels::labelReturn = ImmunityLabels::combatReturn = 0;
}
static void NativeCases(const char* path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate); assert(input);
    std::vector<unsigned char> file(size_t(input.tellg())); input.seekg(0); input.read(reinterpret_cast<char*>(file.data()), file.size()); assert(input);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(file.data()); assert(dos->e_magic == IMAGE_DOS_SIGNATURE);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(file.data() + dos->e_lfanew); assert(nt->Signature == IMAGE_NT_SIGNATURE);
    image = static_cast<unsigned char*>(VirtualAlloc(nullptr, nt->OptionalHeader.SizeOfImage, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE)); assert(image);
    memcpy(image, file.data(), nt->OptionalHeader.SizeOfHeaders);
    const auto* sections = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i)
        memcpy(image + sections[i].VirtualAddress, file.data() + sections[i].PointerToRawData, sections[i].SizeOfRawData);
    Fixture f; f.Publish(); ResetHooks();
    const auto capture = [](uint32_t rva, size_t count) { return std::vector<unsigned char>(image + rva, image + rva + count); };
    const auto originalBase = capture(0x1e2e00, 0x6a), originalTotal = capture(0x1e2e70, 0x78);
    const auto uberFirst = capture(0x83794, 20), uberSecond = capture(0xb27d8, 23);
    image[0x366380] ^= 1; const auto damaged = capture(0x366380, 0xfb);
    assert(!SunderSystem::Install()); assert(capture(0x366380, 0xfb) == damaged);
    assert(capture(0x1e2e00, 0x6a) == originalBase && capture(0x1e2e70, 0x78) == originalTotal);
    assert(capture(0x83794, 20) == uberFirst && capture(0xb27d8, 23) == uberSecond);
    image[0x366380] ^= 1; ResetHooks();
    image[0x1e33f2] ^= 1; assert(!SunderSystem::Install());
    assert(capture(0x1e2e00, 0x6a) == originalBase && capture(0x1e2e70, 0x78) == originalTotal);
    image[0x1e33f2] ^= 1; ResetHooks();
    image[0x32501b] ^= 1; assert(!SunderSystem::Install());
    assert(capture(0x1e2e00, 0x6a) == originalBase && capture(0x1e2e70, 0x78) == originalTotal);
    assert(capture(0x83794, 20) == uberFirst && capture(0xb27d8, 23) == uberSecond);
    image[0x32501b] ^= 1; ResetHooks();
    assert(SunderSystem::Install()); assert(SunderSystem::ready.load());
    using Penalty = int(__fastcall*)(D2UnitStrc*, int, int);
    const auto penalty = reinterpret_cast<Penalty>(image + 0x366380);
    assert(penalty(&f.monster, 39, -100) == -100);
    f.Pierce(60, 0, 0); assert(penalty(&f.monster, 39, -100) == -20);
    assert(penalty(&f.monster, 39, -4) == 0); assert(penalty(&f.monster, 39, 100) == 100);
    assert(penalty(&f.monster, 38, -100) == -100); assert(penalty(&f.player, 39, -100) == -100);
    Bytes<0x150> hireling{}; Put(hireling.data(), 8, f.monster.dwClassId); Put(hireling.data(), 0xc, 1);
    Put(image, 0x1c9ee58, hireling.data()); Put(image, 0x1c9ee60, uint64_t(1));
    assert(penalty(&f.monster, 39, -100) == -100); Put(image, 0x1c9ee60, uint64_t(0));
    List positive, negative; positive.Child(f.root, 75); f.Attach(&positive); f.root.Values(99, 174);
    assert(penalty(&f.monster, 39, -100) == -20); // 174-60 is still immune, despite native base 99.
    f.Pierce(75, 0, 0); assert(penalty(&f.monster, 39, -100) == -100);
    f.Attach(&negative); negative.Child(f.root, -50); f.root.Values(180, 130); f.Pierce(85, 0, 0);
    assert(penalty(&f.monster, 39, -100) == -100); // Existing negative state never consumes the sunder budget.
    D2DamageInfoStrc damage{}; damage.pGame = &f.game; damage.pAttacker = &f.player; damage.pDefender = &f.monster; damage.bDefenderIsMonster = 1;
    D2DamageStatTableStrc record{}; record.nResStatId = 39; record.nPierceStatId = 333;
    f.playerRoot.aggregate = {{333u << 16, 25}}; f.playerRoot.Sync();
    const auto effective = reinterpret_cast<int(__fastcall*)(D2DamageInfoStrc*, D2DamageStatTableStrc*)>(image + 0x324ff0);
    assert(effective(&damage, &record) == 24); // (180-81)-50-25 ordinary pierce.
    SunderSystem::SetEnabled(false); assert(penalty(&f.monster, 39, -100) == -20);
    assert(effective(&damage, &record) == 130); // Native immune target rejects ordinary pierce.
    SunderSystem::SetEnabled(true); assert(effective(&damage, &record) == 24);
    assert(f.root.base[0].value == 180 && f.root.aggregate[0].value == 130);
    {
        const auto savedCosts = costs;
        const auto fold = reinterpret_cast<int(__fastcall*)(D2StatListExStrc*, uint32_t)>(image + 0x1e3300);
        for (const auto& captured : capturedResistanceCosts) {
            assert(captured.bytes.size() == 0x288);
            auto digit = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
            for (size_t n = 0; n < 0x144; ++n)
                costs[captured.stat][n] = static_cast<unsigned char>((digit(captured.bytes[n*2]) << 4) | digit(captured.bytes[n*2+1]));
            const auto stat = captured.stat, pierceStat = captured.immunity;
            const auto perLevel = uint32_t(*reinterpret_cast<uint16_t*>(costs[stat].data() + 0xdc));
            f.Attach(nullptr); record.nResStatId = stat; record.nPierceStatId = -1;
            auto values = [&](int total, int bonus = 0) {
                f.root.base = {{12u << 16, 45}, {stat << 16, 250}};
                if (bonus) f.root.base.push_back({perLevel << 16, bonus});
                f.root.aggregate = f.root.base; f.root.aggregate[1].value = total; f.root.Sync();
            };
            playerStats[{&f.player, pierceStat}] = 100;
            playerStats[{&f.teammate, pierceStat}] = 0;
            values(250);
            assert(fold(f.root.get(), stat << 16) == 250); // Compiled op present, bonus absent.
            const auto rootBytes = f.root.memory;
            assert(SunderSystem::EffectiveResistance(&f.game, &f.monster, stat, 250) == 150);
            assert(penalty(&f.monster, stat, -100) == -20 && effective(&damage, &record) == 150);
            assert(f.root.memory == rootBytes && f.root.base[1].value == 250 && f.root.aggregate[1].value == 250);
            // The actual native label reader must display the same projection.
            assert(ImmunityLabels::ReadForCaller(f.root.get(), stat << 16, costs[stat].data(), ImmunityLabels::labelReturn) == 150);
            playerStats[{&f.player, pierceStat}] = 200;
            assert(effective(&damage, &record) == 99 && penalty(&f.monster, stat, -100) == -100);
            playerStats[{&f.player, pierceStat}] = 100;
            values(295, 8); // Actual native op: 45 * 8 / 8 adds 45 resistance.
            assert(fold(f.root.get(), stat << 16) == 295);
            assert(effective(&damage, &record) == 195);
            negative.Child(f.root, -50); negative.base[0].key = negative.aggregate[0].key = stat << 16; negative.Sync();
            f.Attach(&negative); values(245, 8);
            assert(fold(f.root.get(), stat << 16) == 245 && effective(&damage, &record) == 145);
            f.Attach(nullptr); values(251);
            assert(fold(f.root.get(), stat << 16) == 250);
            assert(SunderSystem::EffectiveResistance(&f.game, &f.monster, stat, 251) == 251); // Stale fold refused.
            values(250); costs[stat][0xde] = 1;
            assert(SunderSystem::EffectiveResistance(&f.game, &f.monster, stat, 250) == 250); // Nonlinear op refused.
            costs[stat][0xde] = 2;
            if (stat == 39) {
                const auto umod = reinterpret_cast<void(__fastcall*)(D2UnitStrc*, int, int)>(image + 0x2fc530);
                // Native Fire Enchanted adds 75 with one existing immunity.
                values(250); playerStats[{&f.player, pierceStat}] = 200;
                umod(&f.monster, 9, 1);
                assert(fold(f.root.get(), stat << 16) == 325);
                assert(SunderSystem::EffectiveResistance(&f.game, &f.monster, 39, 325) == 125);
                // Its native two-immunity limit skips the bonus entirely.
                f.root.base = {{12u << 16,45},{36u << 16,250},{37u << 16,250},
                    {39u << 16,250},{41u << 16,250},{43u << 16,250},{45u << 16,250}};
                f.root.aggregate = f.root.base; f.root.Sync();
                umod(&f.monster, 9, 1);
                assert(fold(f.root.get(), stat << 16) == 250);
                assert(SunderSystem::EffectiveResistance(&f.game, &f.monster, 39, 250) == 99);
                std::cout << "Native Fire Enchanted: one immunity yields 325 -> 125 with 200 pierce; six existing immunities skip its bonus and yield 250 -> 99.\n";
            }
        }
        costs = savedCosts; f.Publish();
        f.root.Values(180, 130); negative.Child(f.root, -50); f.Attach(&negative);
        f.Pierce(85, 0, 0); record.nResStatId = 39; record.nPierceStatId = 333;
        std::cout << "Captured RMD resistance rows: real native folds, labels, damage and curse penalties agree for 250/100 partial pierce, floor 99, active per-level bonuses and downstream curses.\n";
    }
    SunderSystem::Uninstall();
    assert(capture(0x1e2e00, 0x6a) == originalBase && capture(0x1e2e70, 0x78) == originalTotal);
    assert(capture(0x83794, 20) == uberFirst && capture(0xb27d8, 23) == uberSecond);
    assert(penalty(&f.monster, 39, -100) == -20 && effective(&damage, &record) == 130);
    ResetHooks(); assert(VirtualFree(image, 0, MEM_RELEASE)); image = nullptr;
    std::cout << "Private native image: compatibility failures, hook rollback, actual aura/curse division, hireling/player exemptions, damage pierce and uninstall restoration passed.\n";
}
int main(int argc, char** argv) {
    ProjectionCases(); ResetHooks();
    if (argc > 1) NativeCases(argv[1]);
    else std::cout << "Native executable tests skipped: pass D2R.exe to run actual machine code.\n";
}
