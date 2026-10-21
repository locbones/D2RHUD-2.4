// Source-port regressions from D2RHUD-dynamic-immunity-labels.zip. The native
// stat API and server lookup are mocked; no running game is modified.
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
static Sunder::Values applied{}, pierce{};
static int refreshCount = 0;
static int __fastcall GetStat(D2StatListExStrc* root, uint32_t encoded, const void*) {
    lastRead = root;
    return stats[root][encoded >> 16];
}
static D2UnitStrc* __fastcall Resolve(D2GameStrc* game, D2C_UnitTypes type, uint32_t id) {
    assert(game == expectedGame && type == UNIT_MONSTER && id == 123);
    return serverUnit;
}
static void Refresh(D2GameStrc* game, D2UnitStrc* unit) {
    assert(game == expectedGame && unit == serverUnit); ++refreshCount;
    Sunder::Refresh(applied,pierce,99,
        [&](uint32_t stat){return stats[unit->pStatListEx][stat];},
        [&](uint32_t stat,int value){stats[unit->pStatListEx][stat]=value;});
}

int main(int argc,char** argv) {
    using namespace Sunder;
    static_assert(ImmunityStats[0]==STAT_ITEM_PIERCE_COLD_IMMUNITY && ImmunityStats[1]==STAT_ITEM_PIERCE_FIRE_IMMUNITY &&
        ImmunityStats[2]==STAT_ITEM_PIERCE_LIGHT_IMMUNITY && ImmunityStats[3]==STAT_ITEM_PIERCE_POISON_IMMUNITY &&
        ImmunityStats[4]==STAT_ITEM_PIERCE_DAMAGE_IMMUNITY && ImmunityStats[5]==STAT_ITEM_PIERCE_MAGIC_IMMUNITY);
    Values current{}, reductions{}, desired{};
    int writes = 0;
    auto refresh = [&] {
        Sunder::Refresh(reductions,desired,99,
            [&](uint32_t stat){return current[std::find(ResistanceStats.begin(),ResistanceStats.end(),stat)-ResistanceStats.begin()];},
            [&](uint32_t stat,int value){++writes;current[std::find(ResistanceStats.begin(),ResistanceStats.end(),stat)-ResistanceStats.begin()]=value;});
    };
    current.fill(180); desired[1]=60;
    for (int i=0;i<100;++i) {refresh(); assert(current[1]==120);}
    assert(writes==1); // Repeat damage, hover, and UMod refreshes do not stack.
    desired[1]=80;refresh();assert(current[1]==100);
    desired[1]=85;refresh();assert(current[1]==99);
    desired[1]=60;refresh();assert(current[1]==120);
    desired[1]=0;refresh();assert(current[1]==180);
    desired[1]=100;refresh();assert(current[1]==99);
    current[1]+=80;refresh();assert(current[1]==160); // 180+80-100
    refresh();assert(current[1]==160);
    current[1]-=80;refresh();assert(current[1]==99);
    current.fill(160);reductions={};desired={0,100,0,0,0,0};refresh();assert(current[1]==99);
    current[1]+=80;refresh();assert(current[1]==140); // Package UMod regression.
    current[1]-=80;refresh();assert(current[1]==99);
    assert(Calculate(99,0,200).value==99 && Calculate(-100,0,200).value==-100);
    assert(Calculate(100,0,1).value==99 && Calculate(100,0,0).value==100);
    assert(Calculate(180,0,-1).value==180);
    assert(Calculate(180,0,200,50).value==50); // Keep the HUD's configurable floor.
    assert(Calculate(180,0,200,100).value==100);
    assert(!Calculate(INT_MAX,1,0).valid);
    assert(!Calculate(100,-1,20).valid);
    assert(Calculate(INT_MAX,0,INT_MAX).value==99);
    assert(Calculate(INT_MIN,0,INT_MAX).value==INT_MIN);
    Values bases{},bonus{},appliedBase{},piercing{};bases.fill(70);bonus[1]=40;piercing[1]=50;
    int baseWrites=0;
    auto refreshBase=[&](){RefreshBase(appliedBase,piercing,99,
        [&](uint32_t stat){const auto n=std::find(ResistanceStats.begin(),ResistanceStats.end(),stat)-ResistanceStats.begin();return bases[n]+bonus[n];},
        [&](uint32_t stat){return bases[std::find(ResistanceStats.begin(),ResistanceStats.end(),stat)-ResistanceStats.begin()];},
        [&](uint32_t stat,int value){++baseWrites;bases[std::find(ResistanceStats.begin(),ResistanceStats.end(),stat)-ResistanceStats.begin()]=value;return true;});};
    for(int n=0;n<1000;++n)refreshBase();
    assert(bases[1]==59 && bases[1]+bonus[1]==99 && baseWrites==1);
    bonus[1]=0;refreshBase();assert(bases[1]==70 && appliedBase[1]==0);
    bonus[1]=80;refreshBase();assert(bases[1]==20 && bases[1]+bonus[1]==100);
    for(int n=0;n<1000;++n)refreshBase();assert(bases[1]+bonus[1]==100);
    piercing[1]=0;refreshBase();assert(bases[1]==70 && bases[1]+bonus[1]==150);
    Values players{}; MergeMaximum(players,{10,31,0,0,0,0}); MergeMaximum(players,{15,10,0,5,0,0});
    assert((players==Values{15,31,0,5,0,0}));
    std::mt19937 random(13012099);
    int randomized = 0;
    for (int sequence=0;sequence<30;++sequence) {
        Values baseline{}; for (int& value:baseline) value=static_cast<int>(random()%701)-100;
        current=baseline;reductions={};
        for (int change=0;change<30;++change) {
            for (int& value:desired) value=static_cast<int>(random()%301);
            for(int repeat=0;repeat<3;++repeat) {
                refresh();
                for(size_t element=0;element<6;++element)
                    assert(current[element]==(baseline[element]>=100?(std::max)(99,baseline[element]-desired[element]):baseline[element]));
                ++randomized;
            }
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
    originalRead=GetStat;refreshSunder=Refresh;labelReturn=0x8395d;
    UNITS_GetServerUnitByTypeAndId=Resolve;
    for(auto id:Sunder::ResistanceStats) stats[&clientStats][id]=stats[&serverStats][id]=180;
    auto label = [&](uint32_t stat){return ReadForCaller(&clientStats,stat<<16,nullptr,labelReturn);};
    applied={};pierce={0,85,0,0,0,0};
    for(int repeat=0;repeat<100;++repeat) {
        assert(label(36)==180);assert(label(39)==99);
        for(auto stat:Sunder::ResistanceStats) assert(stats[&clientStats][stat]==180);
    }
    assert(refreshCount==100);
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
        assert(UberSupported({at(uberImmunityRva),at(uberModifiersRva)}));
        std::cout<<"Native immunity loop, stat-reader and both Uber display exclusion signatures match the installed executable.\n";
    }
    std::cout<<randomized<<" mixed-element refresh checks plus threshold, equipment, UMod, label selection and build-guard regressions passed.\n";
}
