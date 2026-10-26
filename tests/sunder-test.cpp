// Immutable sunder policy and native label/combat projection regressions.
// The stat API and server lookup are mocked; no running game is modified.
#include <Windows.h>
static BOOL WINAPI GuardedVirtualProtect(void*, SIZE_T, DWORD, PDWORD);
#define VirtualProtect GuardedVirtualProtect
#include "../d2rhud/plugin/ImmunityLabels.cpp"
#undef VirtualProtect
#include <cassert>
#include <fstream>
#include <iostream>
#include <map>
#include <random>
#include <vector>

static int protectionCalls = 0, failProtectionCall = -1;
static BOOL WINAPI GuardedVirtualProtect(void* address, SIZE_T size, DWORD desired, PDWORD previous) {
    if (++protectionCalls == failProtectionCall) return FALSE;
    return VirtualProtect(address, size, desired, previous);
}

static D2ClientStrc* localClient = nullptr;
DWORD64 Pattern::Address(uint32_t rva) { return rva == 0x1d637f0 ? reinterpret_cast<uintptr_t>(&localClient) : 0; }
static std::map<D2StatListExStrc*, std::map<uint32_t,int>> stats;
static D2UnitStrc* serverUnit = nullptr;
static D2GameStrc* expectedGame = nullptr;
static D2StatListExStrc* lastRead = nullptr;
static Sunder::Values pierce{};
static int projectionCount = 0;
static int __fastcall GetStat(D2StatListExStrc* root, uint32_t encoded, const void*) {
    lastRead = root;
    return stats[root][encoded >> 16];
}
static D2UnitStrc* __fastcall Resolve(D2GameStrc* game, D2C_UnitTypes type, uint32_t id) {
    assert(game == expectedGame && type == UNIT_MONSTER && id == 123);
    return serverUnit;
}
static int Project(D2GameStrc* game, D2UnitStrc* unit, uint32_t stat, int nativeTotal) {
    assert((!game || game == expectedGame) && unit == serverUnit);
    ++projectionCount;
    const auto found = std::find(Sunder::ResistanceStats.begin(), Sunder::ResistanceStats.end(), stat);
    assert(found != Sunder::ResistanceStats.end());
    assert(nativeTotal == stats[unit->pStatListEx][stat]);
    const auto value = Sunder::Calculate(nativeTotal, 0, 0, pierce[found - Sunder::ResistanceStats.begin()]);
    return value.valid ? value.effective : nativeTotal;
}

int main(int argc,char** argv) {
    using namespace Sunder;
    static_assert(ImmunityStats[0]==STAT_ITEM_PIERCE_COLD_IMMUNITY && ImmunityStats[1]==STAT_ITEM_PIERCE_FIRE_IMMUNITY &&
        ImmunityStats[2]==STAT_ITEM_PIERCE_LIGHT_IMMUNITY && ImmunityStats[3]==STAT_ITEM_PIERCE_POISON_IMMUNITY &&
        ImmunityStats[4]==STAT_ITEM_PIERCE_DAMAGE_IMMUNITY && ImmunityStats[5]==STAT_ITEM_PIERCE_MAGIC_IMMUNITY);
    static_assert(Floor == 99);
    auto check = [](int base, int positive, int negative, int strength,
                    int native, int effective, int immunity, int applied) {
        const auto value = Calculate(base, positive, negative, strength);
        assert(value.valid && value.native == native && value.effective == effective &&
               value.immunity == immunity && value.applied == applied);
    };
    for (int resistance : {INT_MIN, -100, 0, 50, 99})
        check(resistance, 0, 0, INT_MAX, resistance, resistance, resistance, 0);
    check(100, 0, 0, 0, 100, 100, 100, 0);
    check(100, 0, 0, 1, 100, 99, 99, 1);
    check(174, 0, 0, 74, 174, 100, 100, 74);
    check(174, 0, 0, 75, 174, 99, 99, 75);
    check(174, 0, 0, INT_MAX, 174, 99, 99, 75);
    check(174, 0, 0, -100, 174, 174, 174, 0);
    check(174, 0, -50, 100, 124, 49, 99, 75);
    check(174, 0, -125, 100, 49, -26, 99, 75);
    check(174, 0, -10, 50, 164, 114, 124, 50);
    check(50, 60, -50, 100, 60, 49, 99, 11);
    check(70, 40, 0, 5, 110, 105, 105, 5);
    check(70, 40, -50, 11, 60, 49, 99, 11);
    check(70, 0, -50, 100, 20, 20, 70, 0);
    check(99, 0, -50, 100, 49, 49, 99, 0);

    // UMods always read/write the untouched native resistance, even after an
    // earlier HUD read. Fire Enchanted adds 75 to 160, rather than to 99.
    int nativeFire = 160;
    for (int repeat = 0; repeat < 1000; ++repeat)
        check(nativeFire, 0, 0, 100, 160, 99, 99, 61);
    assert(nativeFire == 160);
    nativeFire += 75;
    check(nativeFire, 0, 0, 100, 235, 135, 135, 100);
    check(nativeFire, 0, 0, 200, 235, 99, 99, 136);
    nativeFire -= 75;
    check(nativeFire, 0, -50, 100, 110, 49, 99, 61);
    check(nativeFire, 0, -50, 0, 110, 110, 160, 0);
    assert(nativeFire == 160);

    check(INT_MAX, 0, 0, INT_MAX, INT_MAX, 99, 99, INT_MAX - 99);
    check(INT_MIN, INT_MAX, 0, INT_MAX, -1, -1, -1, 0);
    check(INT_MAX, 0, INT_MIN, INT_MAX, -1, INT_MIN + 99, 99, INT_MAX - 99);
    for (const auto value : {Calculate(INT_MAX, 1, 0, 1),
                             Calculate(INT_MIN, 0, -1, 1),
                             Calculate(100, -1, 0, 1),
                             Calculate(100, 0, 1, 1)})
        assert(!value.valid);

    Values players{};
    MergeMaximum(players, {10,31,0,0,0,0});
    MergeMaximum(players, {15,10,0,5,0,0});
    MergeMaximum(players, {-1,-1,-1,-1,-1,-1});
    assert((players == Values{15,31,0,5,0,0}));
    for (auto stat : ResistanceStats) assert(IsResistance(stat));
    assert(!IsResistance(0) && !IsResistance(40) && !IsResistance(UINT_MAX));

    std::mt19937 random(13012099);
    int randomized = 0;
    for (int sample = 0; sample < 10000; ++sample) {
        const int base = int(random() % 701) - 100;
        const int positive = int(random() % 301);
        const int negative = -int(random() % 401);
        const int strength = int(random() % 601) - 100;
        const int intrinsic = base + positive;
        const int expectedApplied = intrinsic >= 100 ? (std::min)((std::max)(strength, 0), intrinsic - 99) : 0;
        for (int repeat = 0; repeat < 3; ++repeat) {
            check(base, positive, negative, strength, intrinsic + negative,
                  intrinsic + negative - expectedApplied, intrinsic - expectedApplied, expectedApplied);
            ++randomized;
        }
    }

    using namespace ImmunityLabels;
    D2GameStrc game{};expectedGame=&game;
    D2ClientStrc clientRecord{};clientRecord.pGame=&game;localClient=&clientRecord;
    D2UnitStrc client{},server{};
    D2StatListExStrc clientStats{},serverStats{};
    client.dwUnitType=server.dwUnitType=UNIT_MONSTER;client.dwUnitId=server.dwUnitId=123;
    client.pStatListEx=&clientStats;server.pStatListEx=&serverStats;serverUnit=&server;
    auto owner=&client;memcpy(reinterpret_cast<uint8_t*>(&clientStats)+0x78,&owner,sizeof(owner));
    int32_t rootFlags = int32_t(0x80000000u);
    memcpy(reinterpret_cast<uint8_t*>(&clientStats)+0x1c,&rootFlags,sizeof(rootFlags));
    memcpy(reinterpret_cast<uint8_t*>(&serverStats)+0x1c,&rootFlags,sizeof(rootFlags));
    auto serverOwner=&server;memcpy(reinterpret_cast<uint8_t*>(&serverStats)+0x78,&serverOwner,sizeof(serverOwner));
    originalRead=GetStat;projectResistance=Project;labelReturn=0x8395d;combatReturn=0x32505a;
    UNITS_GetServerUnitByTypeAndId=Resolve;
    for(auto id:Sunder::ResistanceStats) stats[&clientStats][id]=stats[&serverStats][id]=180;
    auto label = [&](uint32_t stat){return ReadForCaller(&clientStats,stat<<16,nullptr,labelReturn);};
    pierce={0,85,0,0,0,0};
    for(int repeat=0;repeat<100;++repeat) {
        assert(label(36)==180);assert(label(39)==99);
        for(auto stat:Sunder::ResistanceStats) assert(stats[&clientStats][stat]==180 && stats[&serverStats][stat]==180);
    }
    assert(projectionCount==200);
    pierce[1]=60;label(36);assert(label(39)==120);
    pierce[1]=80;label(36);assert(label(39)==100);
    pierce[1]=81;label(36);assert(label(39)==99);
    pierce[1]=0;label(36);assert(label(39)==180);
    for(size_t element=0;element<6;++element) {
        pierce={};pierce[element]=81;label(36);
        for(size_t checked=0;checked<6;++checked) assert(label(Sunder::ResistanceStats[checked])==(checked==element?99:180));
        pierce={};label(36);assert(label(Sunder::ResistanceStats[element])==180);
    }
    pierce={0,85,0,0,0,0};label(36);assert(label(39)==99);
    stats[&serverStats][39]+=20;label(36);assert(label(39)==115);
    assert(ReadForCaller(&clientStats,39<<16,nullptr,labelReturn+1)==180 && lastRead==&clientStats);
    assert(HookRead(&clientStats,39<<16,nullptr)==180); // Unrelated caller passes through.
    assert(ReadForCaller(&serverStats,39<<16,nullptr,combatReturn)==115);
    assert(stats[&serverStats][39]==200); // Projection never changes a native UMod input.
    assert(ReadNative(&serverStats,39<<16,nullptr)==200);
    rootFlags=0;memcpy(reinterpret_cast<uint8_t*>(&serverStats)+0x1c,&rootFlags,sizeof(rootFlags));
    assert(ReadForCaller(&serverStats,39<<16,nullptr,combatReturn)==200);
    rootFlags=int32_t(0x80000000u);memcpy(reinterpret_cast<uint8_t*>(&serverStats)+0x1c,&rootFlags,sizeof(rootFlags));
    const auto savedProjection=projectResistance;projectResistance=nullptr;
    assert(ReadForCaller(&serverStats,39<<16,nullptr,combatReturn)==200);
    projectResistance=savedProjection;
    assert(ReadForCaller(&clientStats,(39<<16)|1,nullptr,labelReturn)==180 && lastRead==&clientStats);
    stats[&clientStats][31]=77;assert(label(31)==77 && lastRead==&clientStats);
    server.dwUnitId=456;assert(label(39)==180 && lastRead==&clientStats);server.dwUnitId=123;
    server.dwUnitType=UNIT_PLAYER;assert(label(39)==180);server.dwUnitType=UNIT_MONSTER;
    server.pStatListEx=nullptr;assert(label(39)==180);server.pStatListEx=&serverStats;
    serverUnit=nullptr;assert(label(39)==180);serverUnit=&server;
    clientRecord.pGame=nullptr;assert(label(39)==180);clientRecord.pGame=&game;
    localClient=nullptr;
    for(int value:{180,100,99,0,-100}) {stats[&clientStats][39]=value;assert(label(39)==value);}
    localClient=&clientRecord;
    owner=nullptr;memcpy(reinterpret_cast<uint8_t*>(&clientStats)+0x78,&owner,sizeof(owner));
    stats[&clientStats][39]=180;assert(label(39)==180 && lastRead==&clientStats);

    auto combat=combatInstructions;
    assert(CombatSupported(reinterpret_cast<uintptr_t>(combat.data())));
    for(size_t i=0;i<combat.size();++i) {
        combat[i]^=1;assert(!CombatSupported(reinterpret_cast<uintptr_t>(combat.data())));combat[i]^=1;
    }

    // Match every byte of both native instruction sequences, and fail closed
    // on other builds/hooks. This does not install hooks into the test process.
    auto instructions=labelInstructions;
    std::array<uint8_t,14> reader{0x48,0x89,0x5c,0x24,8,0x57,0x48,0x83,0xec,0x20,0x48,0x63,0x41,0x1c};
    assert(Supported(reinterpret_cast<uintptr_t>(instructions.data()),reinterpret_cast<uintptr_t>(reader.data())));
    for(size_t i=0;i<instructions.size();++i) {
        instructions[i]^=1;assert(!Supported(reinterpret_cast<uintptr_t>(instructions.data()),reinterpret_cast<uintptr_t>(reader.data())));instructions[i]^=1;
    }
    reader[0]^=1;assert(!Supported(reinterpret_cast<uintptr_t>(instructions.data()),reinterpret_cast<uintptr_t>(reader.data())));reader[0]^=1;

    auto uberImmunity = uberImmunityInstructions;
    auto uberModifiers = uberModifiersInstructions;
    const std::array<uintptr_t, 2> guardedSites{reinterpret_cast<uintptr_t>(uberImmunity.data()), reinterpret_cast<uintptr_t>(uberModifiers.data())};
    assert(UberSupported(guardedSites));
    // A changed comparison or jump at either site prevents all writes.
    for (size_t i = 0; i < uberImmunity.size(); ++i) {
        uberImmunity[i] ^= 1; const auto changed = uberImmunity;
        assert(!ChangeUberDisplay(guardedSites, true));
        assert(uberImmunity == changed && uberModifiers == uberModifiersInstructions);
        uberImmunity[i] ^= 1;
    }
    for (size_t i = 0; i < uberModifiers.size(); ++i) {
        uberModifiers[i] ^= 1; const auto changed = uberModifiers;
        assert(!ChangeUberDisplay(guardedSites, true));
        assert(uberModifiers == changed && uberImmunity == uberImmunityInstructions);
        uberModifiers[i] ^= 1;
    }
    auto firstPage = static_cast<uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    auto secondPage = static_cast<uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    assert(firstPage && secondPage);
    memcpy(firstPage, uberImmunityInstructions.data(), uberImmunityInstructions.size());
    memcpy(secondPage, uberModifiersInstructions.data(), uberModifiersInstructions.size());
    DWORD previous = 0;
    assert(VirtualProtect(firstPage, 4096, PAGE_EXECUTE_READ, &previous));
    assert(VirtualProtect(secondPage, 4096, PAGE_EXECUTE_READ, &previous));
    const std::array<uintptr_t, 2> executableSites{reinterpret_cast<uintptr_t>(firstPage), reinterpret_cast<uintptr_t>(secondPage)};
    protectionCalls = 0; failProtectionCall = 2;
    assert(!ChangeUberDisplay(executableSites, true));
    assert(UberSupported(executableSites));
    MEMORY_BASIC_INFORMATION pageInfo{};
    assert(VirtualQuery(firstPage, &pageInfo, sizeof(pageInfo)) && pageInfo.Protect == PAGE_EXECUTE_READ);
    assert(VirtualQuery(secondPage, &pageInfo, sizeof(pageInfo)) && pageInfo.Protect == PAGE_EXECUTE_READ);
    failProtectionCall = -1;
    assert(ChangeUberDisplay(executableSites, true) && UberSupported(executableSites, true));
    assert(VirtualQuery(firstPage, &pageInfo, sizeof(pageInfo)) && pageInfo.Protect == PAGE_EXECUTE_READ);
    assert(VirtualQuery(secondPage, &pageInfo, sizeof(pageInfo)) && pageInfo.Protect == PAGE_EXECUTE_READ);
    assert(!ChangeUberDisplay(executableSites, true));
    // Never restore over a subsequent module's edit to either context.
    assert(VirtualProtect(secondPage, 4096, PAGE_READWRITE, &previous));
    secondPage[0] ^= 1;
    assert(!ChangeUberDisplay(executableSites, false));
    assert(UberSiteSupported(executableSites[0], uberImmunityInstructions, true));
    secondPage[0] ^= 1;
    assert(VirtualProtect(secondPage, 4096, PAGE_EXECUTE_READ, &previous));
    assert(ChangeUberDisplay(executableSites, false) && UberSupported(executableSites));
    assert(VirtualFree(firstPage, 0, MEM_RELEASE)); assert(VirtualFree(secondPage, 0, MEM_RELEASE));

    if(argc>1) {
        std::ifstream input(argv[1],std::ios::binary|std::ios::ate);assert(input);
        std::vector<uint8_t> exe(static_cast<size_t>(input.tellg()));input.seekg(0);input.read(reinterpret_cast<char*>(exe.data()),exe.size());assert(input);
        const auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(exe.data());assert(dos->e_magic==IMAGE_DOS_SIGNATURE);
        const auto nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(exe.data()+dos->e_lfanew);assert(nt->Signature==IMAGE_NT_SIGNATURE);
        const auto sections=IMAGE_FIRST_SECTION(nt);
        auto at=[&](uint32_t rva)->uintptr_t {
            for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i) if(rva>=sections[i].VirtualAddress && rva-sections[i].VirtualAddress<sections[i].SizeOfRawData)
                return reinterpret_cast<uintptr_t>(exe.data()+sections[i].PointerToRawData+rva-sections[i].VirtualAddress);
            return 0;
        };
        assert(Supported(at(0x83920),at(0x1e2e70)));
        assert(CombatSupported(at(0x32501b)));
        assert(UberSupported({at(uberImmunityRva),at(uberModifiersRva)}));
        std::cout<<"Native immunity loop, stat-reader and both Uber display exclusion signatures match the installed executable.\n";
    }
    std::cout<<randomized<<" immutable projection checks plus threshold, equipment, UMod, aura, label/combat selection and build-guard regressions passed.\n";
}
