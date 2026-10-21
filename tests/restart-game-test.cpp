#include "../d2rhud/plugin/RestartGame.cpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <vector>

using namespace RestartGame;
std::map<uint32_t, uintptr_t> bindings;
uintptr_t testImage = 0;
DWORD64 Pattern::Address(uint32_t rva) { return testImage ? testImage + rva : bindings[rva]; }
void FlowTests() {
    using namespace Data;
    Flow f;
    assert(f.Tick(0, false, true, true, true) == Action::None);
    assert(f.Begin(100) && !f.Begin(200));
    for (int n = 0; n < 10; ++n) assert(f.Tick(300 + n, true, false, true, true) == Action::None);
    assert(f.Tick(400, false, false, true, true) == Action::None); // old server still saving
    assert(f.Tick(500, false, true, false, true) == Action::None); // UI is not ready
    assert(f.Tick(600, false, true, true, true) == Action::None);
    assert(f.Tick(650, false, true, false, true) == Action::None); // readiness must remain stable
    assert(f.Tick(700, false, true, true, true) == Action::None);
    assert(f.Tick(750, false, true, true, true) == Action::None);
    assert(f.Tick(800, false, true, true, true) == Action::Start);
    for (int n = 0; n < 20; ++n) assert(f.Tick(900 + n, false, true, true, true) == Action::None); // one start only
    assert(f.Tick(1000, true, false, false, true) == Action::Complete && f.phase == Phase::Idle);
    assert(f.Begin(2000));
    assert(f.Tick(2100, false, true, true, false) == Action::Cancel); // never play a different hero
    assert(f.Begin(3000));
    assert(f.Tick(63000, true, false, false, false) == Action::Cancel); // save/exit stalls safely
    assert(f.Begin(64000));
    f.Reset(); assert(f.phase == Phase::Idle);
}
void SelectionTests() {
    int mode = 0; uint64_t count = 1, index = 0; uint8_t selected = 1;
    std::array<uint8_t, 0x130> record{};
    strcpy_s(reinterpret_cast<char*>(record.data() + 0x10), 16, "MyHero");
    uintptr_t entry = reinterpret_cast<uintptr_t>(record.data()), entries = reinterpret_cast<uintptr_t>(&entry);
    bindings[0x1bfa31c] = reinterpret_cast<uintptr_t>(&mode);
    bindings[0x1ca10a8] = reinterpret_cast<uintptr_t>(&selected);
    bindings[0x1ca10a0] = reinterpret_cast<uintptr_t>(&index);
    bindings[0x1ca0fe0] = reinterpret_cast<uintptr_t>(&count);
    bindings[0x1ca0fd8] = reinterpret_cast<uintptr_t>(&entries);
    std::array<char, 16> name{};
    assert(Selected(name) && !strcmp(name.data(), "MyHero"));
    mode = 1; assert(!Selected(name)); mode = 2; assert(!Selected(name)); mode = 3; assert(!Selected(name)); mode = 0;
    selected = 0; assert(!Selected(name)); selected = 1;
    index = 1; assert(!Selected(name)); index = 0;
    count = 100001; assert(!Selected(name)); count = 1;
    memset(record.data() + 0x10, 'x', 16); assert(!Selected(name));
    bindings.clear();
}
D2UnitStrc hero{};
std::array<uint8_t,0x60> restartButton{},restartRow{},ordinaryButton{};
void* __fastcall FindRestart(void*,const char* name){
    if(!strcmp(name,"RestartGame"))return restartButton.data();
    if(!strcmp(name,"Row Restart Game"))return restartRow.data();return nullptr;
}
void __fastcall SetWidgetEnabled(void* widget,bool enabled){static_cast<uint8_t*>(widget)[0x50]=enabled;}
void __fastcall SetWidgetVisible(void* widget,bool visible){static_cast<uint8_t*>(widget)[0x51]=visible;}
D2UnitStrc* __fastcall Hero(uint32_t selector) { assert(selector == 0); return &hero; }
void SessionTests() {
    int mode = 0; uint8_t state = 1,lanHost=0; uint32_t selector = 0;
    D2GameStrc game{}; game.nClients = 1; game.nDifficulty = 2;
    D2ClientStrc client{}; client.dwClientState = 4; client.pGame = &game; client.dwUnitGUID = 77;
    strcpy_s(client.szName, "MyHero");
    uintptr_t firstClient = reinterpret_cast<uintptr_t>(&client);
    D2PlayerDataStrc player{}; strcpy_s(player.szName, "MyHero");
    hero = {}; hero.dwUnitId = 77; hero.pPlayerData = &player;
    bindings[0x1bfa31c] = reinterpret_cast<uintptr_t>(&mode);
    bindings[0x1dc76f8] = reinterpret_cast<uintptr_t>(&state);
    bindings[0x18882e8] = reinterpret_cast<uintptr_t>(&lanHost);
    bindings[0x1d637f0] = reinterpret_cast<uintptr_t>(&firstClient);
    bindings[0x1d442d8] = reinterpret_cast<uintptr_t>(&selector);
    bindings[0x6b050] = reinterpret_cast<uintptr_t>(Hero);
    bindings[0x576070]=reinterpret_cast<uintptr_t>(FindRestart);
    bindings[0x10f9c0]=reinterpret_cast<uintptr_t>(SetWidgetEnabled);
    bindings[0x10f9d0]=reinterpret_cast<uintptr_t>(SetWidgetVisible);
    Session snapshot;
    // Closed frontend records are deliberately absent while the hero is playing.
    assert(LiveSession(snapshot) && snapshot.difficulty == 2 && !strcmp(snapshot.name.data(), "MyHero"));
    UpdateRestartButton(nullptr);assert(restartButton[0x50] && restartButton[0x51] && restartRow[0x51]);
    ordinaryButton[0x50]=ordinaryButton[0x51]=1;
    lanHost=2;assert(!LiveSession(snapshot));UpdateRestartButton(nullptr);
    assert(!restartButton[0x50] && !restartButton[0x51] && !restartRow[0x51] && ordinaryButton[0x51]);lanHost=0;
    UpdateRestartButton(nullptr);assert(restartButton[0x50] && restartButton[0x51] && restartRow[0x51]); // back to offline
    mode = 1; assert(!LiveSession(snapshot)); mode = 2; assert(!LiveSession(snapshot)); mode = 0;
    game.nClients = 2; assert(!LiveSession(snapshot)); game.nClients = 1;
    client.dwUnitGUID = 99; assert(!LiveSession(snapshot)); client.dwUnitGUID = 77;
    strcpy_s(player.szName, "OtherHero"); assert(!LiveSession(snapshot)); strcpy_s(player.szName, "MyHero");
    selector = 8; assert(!LiveSession(snapshot)); selector = 0;
    state = 0; assert(!LiveSession(snapshot));
    UpdateRestartButton(nullptr);assert(!restartButton[0x51]);
    bindings.clear();
}
int forwardedPanels = 0;
void __fastcall ForwardPanel(void*, const PanelMessage*) { ++forwardedPanels; }
void PanelTests() {
    originalPanelHandler = ForwardPanel;
    constexpr char command[] = "D2RHUDRestartGame";
    constexpr char other[] = "SettingsPanel";
    PanelMessage message{0x7127a406e0b6452cULL, 0x723d40e52f74e229ULL,
        reinterpret_cast<uintptr_t>(command), 1};
    // Recognize the registered PanelManager/OpenPanel route before trying to
    // instantiate a nonexistent panel. A missing save bridge must not exit.
    saveAndExit.store(nullptr);
    PanelHook(nullptr, &message); assert(forwardedPanels == 0);
    message.data = reinterpret_cast<uintptr_t>(other);
    PanelHook(nullptr, &message); assert(forwardedPanels == 1);
    message.data = reinterpret_cast<uintptr_t>(command); message.hasData = 0;
    PanelHook(nullptr, &message); assert(forwardedPanels == 2);
    message.hasData = 1; message.action = 0;
    PanelHook(nullptr, &message); assert(forwardedPanels == 3);
    originalPanelHandler = nullptr;
}
int nativeCalls = 0;
struct InlineString { const char* text; uint64_t length, capacity; };
InlineString* __fastcall SavePath(InlineString* result, uint64_t id) {
    assert(id == 123); *result = {"", 0, 0x8000000000000000ULL}; return result;
}
std::array<uint8_t, 0x4300> profile{};
void* __fastcall Profile() { return profile.data(); }
void* __fastcall PathObject(void* result, void*) {
    *reinterpret_cast<InlineString*>(result) = {"", 0, 0x8000000000000000ULL}; return result;
}
void __fastcall CreateOffline(uintptr_t, const char* name, uint16_t flags, uint8_t type, uint8_t level) {
    assert(!strcmp(name, "FixtureHero") && flags == 0x28 && type == 3 && level == 42); ++nativeCalls;
}
void __fastcall Noop() {}
std::vector<int> exitCalls;
void __fastcall ClosePause() { exitCalls.push_back(1); }
void __fastcall ExitSession() { exitCalls.push_back(2); }
int nativePanels = 0;
void* __fastcall NativeOpenPanel(void*, const char* name) {
    assert(!strcmp(name, "D2RHUDRestartGame")); ++nativePanels; return nullptr;
}
void NativeTest(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate); assert(input);
    std::vector<uint8_t> file(size_t(input.tellg())); input.seekg(0);
    input.read(reinterpret_cast<char*>(file.data()), file.size());
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(file.data());
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(file.data() + dos->e_lfanew);
    auto image = static_cast<uint8_t*>(VirtualAlloc(nullptr, nt->OptionalHeader.SizeOfImage, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE)); assert(image);
    auto sections = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i)
        memcpy(image + sections[i].VirtualAddress, file.data() + sections[i].PointerToRawData, sections[i].SizeOfRawData);
    testImage = reinterpret_cast<uintptr_t>(image);
    Install(); assert(installed.load()); Uninstall();
    image[0x418990] ^= 1; Install(); assert(!installed.load()); image[0x418990] ^= 1;
    auto stub = [&](uint32_t rva, auto function) {
        const uint8_t jump[]{0xff, 0x25, 0, 0, 0, 0}; memcpy(image + rva, jump, 6);
        auto address = reinterpret_cast<uintptr_t>(function); memcpy(image + rva + 6, &address, 8);
    };
    stub(0x1434c50, Noop); // private-image security cookie
    stub(0xae1600, SavePath); // profile/path preference boundaries; no disk writes
    stub(0xb88c30, Profile);
    stub(0x192b60, PathObject);
    stub(0x419d30, Noop);
    stub(0x418bd0, CreateOffline); // observe the real local loader's game-creation boundary
    stub(0xbc8b50, ClosePause);
    stub(0x6e2e0, ExitSession);
    stub(0x596e10, NativeOpenPanel);
    FlushInstructionCache(GetCurrentProcess(), image, nt->OptionalHeader.SizeOfImage);
    struct ExitMessage { uintptr_t type; uint64_t action; } message{0, 0x621d53c05fca5a67ULL};
    reinterpret_cast<void(__fastcall*)(void*, ExitMessage*)>(image + 0xbc88c0)(nullptr, &message);
    assert((exitCalls == std::vector<int>{1, 2})); // real native ExitGame dispatch, no retained UI pointer
    const char command[] = "D2RHUDRestartGame";
    PanelMessage open{0x7127a406e0b6452cULL, 0x723d40e52f74e229ULL,
        reinterpret_cast<uintptr_t>(command), 1};
    reinterpret_cast<PanelHandler>(image + 0x5959e0)(nullptr, &open);
    assert(nativePanels == 1); // actual registered OpenPanel dispatch/argument ABI
    // Execute the real pause-open entry and widget state setters. Unrelated
    // pause-layout and game-render services are fixture boundaries.
    stub(0xbc86f0,Noop);stub(0xbf87c0,Noop);stub(0x576070,FindRestart);stub(0x6b050,Hero);
    D2GameStrc game{};game.nClients=1;
    D2ClientStrc client{};client.pGame=&game;client.dwClientState=4;client.dwUnitGUID=77;strcpy_s(client.szName,"MyHero");
    D2PlayerDataStrc player{};strcpy_s(player.szName,"MyHero");hero={};hero.dwUnitId=77;hero.pPlayerData=&player;
    auto clientAddress=reinterpret_cast<uintptr_t>(&client);memcpy(image+0x1d637f0,&clientAddress,8);
    image[0x1dc76f8]=1;image[0x18882e8]=0;memset(image+0x1bfa31c,0,4);memset(image+0x1d442d8,0,4);
    FlushInstructionCache(GetCurrentProcess(),image,nt->OptionalHeader.SizeOfImage);
    originalPauseOpen=reinterpret_cast<PauseOpen>(image+0xbc8530);
    PauseOpenHook(nullptr);assert(restartButton[0x50] && restartButton[0x51] && restartRow[0x51]);
    image[0x18882e8]=2;PauseOpenHook(nullptr);assert(!restartButton[0x50] && !restartButton[0x51] && !restartRow[0x51]);
    image[0x18882e8]=0;PauseOpenHook(nullptr);assert(restartButton[0x51] && restartRow[0x51]);
    game.nClients=2;PauseOpenHook(nullptr);assert(!restartButton[0x51]);game.nClients=1;
    memset(image+0x1d637f0,0,8);PauseOpenHook(nullptr);assert(!restartButton[0x51]); // joiner has no local server
    originalPauseOpen=nullptr;
    std::array<uint8_t, 0x130> character{};
    uint64_t id = 123; memcpy(character.data() + 8, &id, 8);
    strcpy_s(reinterpret_cast<char*>(character.data() + 0x10), 16, "FixtureHero");
    uint16_t flags = 0x28; memcpy(character.data() + 0x122, &flags, 2);
    character[0x120] = 3; character[0x121] = 42;
    uintptr_t record = reinterpret_cast<uintptr_t>(character.data()), records = reinterpret_cast<uintptr_t>(&record);
    uint64_t count = 1;
    memcpy(image + 0x1ca0fd8, &records, 8); memcpy(image + 0x1ca0fe0, &count, 8);
    // Online list remains empty: the native selector must use the real offline list.
    memset(image + 0x1ca0c78, 0, 16);
    const auto start = reinterpret_cast<Start>(image + 0x418990);
    Selection selection;
    for (int difficulty = 0; difficulty < 3; ++difficulty) {
        start(difficulty, &selection);
        assert(image[0x1bf086e] == difficulty);
    }
    assert(nativeCalls == 3);
    selection.index = 1; start(0, &selection); assert(nativeCalls == 3);
    selection.index = 0; selection.list = 0; start(0, &selection); assert(nativeCalls == 3);
    VirtualFree(image, 0, MEM_RELEASE); testImage = 0;
    std::cout << "Native registered panel route, exit dispatch, actual offline character loader/difficulty/identity, signature rejection and hook lifecycle passed.\n";
}
int main(int argc, char** argv) {
    FlowTests(); SelectionTests(); SessionTests(); PanelTests();
    if (argc > 1) NativeTest(argv[1]);
    std::cout << "Restart sequencing, duplicate clicks, teardown readiness, identity changes and timeouts passed.\n";
}
