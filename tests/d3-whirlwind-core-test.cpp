#include <Windows.h>
namespace InputMock {
bool left = false, right = false, swapped = false, focused = true;
HWND Foreground() { return focused ? reinterpret_cast<HWND>(uintptr_t(0x1234)) : nullptr; }
DWORD WindowProcess(HWND window, DWORD* process) { if (!window) return 0; *process = GetCurrentProcessId(); return 1; }
SHORT KeyState(int key) { return ((key == VK_LBUTTON && left) || (key == VK_RBUTTON && right)) ? SHORT(-32768) : 0; }
int Metrics(int id) { return id == SM_SWAPBUTTON && swapped; }
}
#define GetForegroundWindow InputMock::Foreground
#define GetWindowThreadProcessId InputMock::WindowProcess
#define GetAsyncKeyState InputMock::KeyState
#define GetSystemMetrics InputMock::Metrics
#include "../d2rhud/plugin/D3Whirlwind.cpp"
#undef GetForegroundWindow
#undef GetWindowThreadProcessId
#undef GetAsyncKeyState
#undef GetSystemMetrics
#include <cassert>
#include <iostream>
#include <vector>
#include <filesystem>
#include <fstream>

namespace {
std::map<uint32_t, uintptr_t> testBindings;
}
DWORD64 Pattern::Address(uint32_t rva) { return testBindings[rva]; }
using namespace D3Whirlwind;
template<size_t N> using Bytes = std::array<uint8_t, N>;
template<class T> void Put(void* pointer, size_t offset, T value) {
    std::memcpy(static_cast<uint8_t*>(pointer) + offset, &value, sizeof(value));
}
template<class T> void Bind(uint32_t rva, T value) { testBindings[rva] = reinterpret_cast<uintptr_t>(value); }

struct Fixture {
    Game game{};
    Unit owner{};
    D2PlayerDataStrc data{};
    Client connection{};
    D2MouseHoverStruct hover{};
    Bytes<0x400> path{};
    Bytes<0x50> skill{}, skills{};
    Bytes<0x284> row{};
    Bytes<0x100> room{};
    Bytes<0xad8> stats{};
    std::array<uint8_t, 31> panels{};
    Client* clientHead = nullptr;
    uint32_t selector = 0;
    int cursorX = 105, cursorY = 206;
    int startCalls = 0, doCalls = 0, moveCalls = 0, dispatchCalls = 0, pickupCalls = 0, chargeCalls = 0;
    int clientStarts = 0, clientMoves = 0, clientActions = 0;
    int serverComputes = 0, clientComputes = 0, clears = 0, reserves = 0;
    int cachedRouteUses = 0;
    bool startSuccess = true, chargeSuccess = true, computeSuccess = true;
    bool nestedAction = false;
    bool endOnAction = false, routeControls = false;
    std::vector<int> inputEvents;
    std::vector<Data::Command> controls;
    Unit gold{};Bytes<0x30> goldPath{};Bytes<0x1b4> goldRow{};
    uint8_t* goldTable=nullptr;uint64_t goldRows=1;Unit* autoGoldTarget=nullptr;
    uint32_t goldDistance=2;bool goldFound=true;
    int interactions=0;std::array<int,3> interactionArguments{};uint16_t interactionModifiers=0;
    std::vector<Pickup::Command> goldPackets;
    std::array<uint32_t, 8> localIds{};
    Unit* selectedItem = nullptr;
    int selectionCalls = 0;
    void Reset();
    Data::Command Command(uint32_t sequence = 1, bool held = true) {
        Data::Command command;
        command.held = held; command.recipient = owner.dwUnitId; command.session = game.dwInitSeed;
        command.skill = 151; command.x = 105; command.y = 206; command.sequence = sequence;
        return command;
    }
    void Arm() { assert(ServerStartHook(&game, &owner, 151, 20) == 1); assert(channels.size() == 1); }
    ServerChannel& Current() { return channels.at({&game, owner.dwUnitId}); }
    void Receive(const Data::Command& command) { assert(DispatcherHook(&game, &owner, &command, sizeof(command)) == 1); }
} fixture;

int __fastcall NativeStart(Game* game, Unit* player, int id, int level) {
    assert(game == &fixture.game && player == &fixture.owner && id == 151 && level == 20);
    ++fixture.startCalls; return fixture.startSuccess;
}
int __fastcall NativeDo(Game* game, Unit* player, int id, int level) {
    assert(game == &fixture.game && player == &fixture.owner && id == 151 && level == 20);
    ++fixture.doCalls; return 77;
}
int __fastcall NativeMove(Game* game, Unit* player) { assert(game == &fixture.game && player == &fixture.owner); ++fixture.moveCalls; return 88; }
int __fastcall NativeDispatch(Game* game, Unit* player, const void*, int) {
    assert(game == &fixture.game && player == &fixture.owner); ++fixture.dispatchCalls;
    // Reproduce the native generic dispatcher gate for STATE_SKILL_MOVE (54).
    return NativeData::Read<uint32_t>(fixture.stats.data(), 0xacc) & 0x400000 ? 0 : 66;
}
int __fastcall NativePickup(Game* game, Unit* player, const void* bytes, int size) {
    assert(game == &fixture.game && player == &fixture.owner && size == 17 && *static_cast<const uint8_t*>(bytes) == 0x16);
    ++fixture.pickupCalls; return 44;
}
int __fastcall NativeClientStart(Unit* player, int id) {
    assert(player == &fixture.owner && id == 151); ++fixture.clientStarts;
    Put(fixture.skill.data(), 0x14, uint32_t(1));
    Put(fixture.skill.data(), 0x2c, uint32_t(1234)); Put(fixture.skill.data(), 0x30, int(99));
    return 1;
}
int __fastcall NativeClientMove(Unit* player, bool) { assert(player == &fixture.owner); ++fixture.clientMoves; return 55; }
Unit* __fastcall NativeLocalPlayer(uint32_t selector) { assert(selector == fixture.selector); return &fixture.owner; }
bool __fastcall NativeCursor(int*, int*) { return true; }
void __fastcall NativeClientAction(int action, Unit* player, int type, int id) {
    ++fixture.clientActions;
    if (fixture.endOnAction) {
        assert(player == &fixture.owner && clientActionDepth > 0);
        // Native d0230 prediction marks an active Whirlwind for completion
        // before attempting the repeated selected-skill cast.
        NativeData::Finish(fixture.skill.data()); fixture.inputEvents.push_back(1); return;
    }
    assert(action == 0xc && player == &fixture.owner && type == 0 && id == 0 && clientActionDepth > 0);
    // Model ClientStart occurring during native prediction, before the native
    // action routine has completed queuing the initial Whirlwind cast packet.
    client.active = true; client.pending = true; client.player = player; client.skill = fixture.skill.data();
    client.id = 151; client.seed = fixture.game.dwInitSeed; client.cursor = {105, 206};
    if (fixture.nestedAction) { fixture.nestedAction = false; ClientActionHook(action, player, type, id); }
    fixture.inputEvents.push_back(1); // Native cast packet has now been queued.
}
void __fastcall SendControlPacket(const void* bytes, int size) {
    assert(clientActionDepth == 0 && size == sizeof(Data::Command));
    Data::Command command; std::memcpy(&command, bytes, sizeof(command));
    assert(Data::Valid(command) && command.recipient == fixture.owner.dwUnitId
        && command.skill == 151 && command.session == uint32_t(fixture.game.dwInitSeed));
    fixture.controls.push_back(command);
    if (fixture.routeControls) assert(DispatcherHook(&fixture.game, &fixture.owner, &command, sizeof(command)) == 1);
    fixture.inputEvents.push_back(2);
}
int __fastcall NativeInteraction(Unit*,int action,int x,int y,uint16_t modifiers){
    ++fixture.interactions;fixture.interactionArguments={action,x,y};fixture.interactionModifiers=modifiers;return 99;
}
Unit* __fastcall GoldLookup(void*,int bucket,uint32_t id,int type){
    assert(type==4 && bucket==int(id&0x7f));
    return fixture.goldFound && id==fixture.gold.dwUnitId ? &fixture.gold : nullptr;
}
Unit* __fastcall SelectedGroundItem(uint32_t index) {
    assert(index < fixture.localIds.size() && fixture.localIds[index] == fixture.owner.dwUnitId);
    ++fixture.selectionCalls;
    return fixture.selectedItem;
}
uint32_t __fastcall GoldDistance(Unit* player,Unit* gold){
    assert(player==&fixture.owner && gold==&fixture.gold);return fixture.goldDistance;
}
void __fastcall GoldSend(const void* bytes,int size){
    assert(size==sizeof(Pickup::Command));Pickup::Command packet;memcpy(&packet,bytes,sizeof(packet));
    fixture.goldPackets.push_back(packet);
    assert(DispatcherHook(&fixture.game,&fixture.owner,bytes,size)==44);
}
int __fastcall Cost(int id, int level) { assert(id == 151 && level == 20); return 10; }
int __fastcall ChargeMana(Unit* player, int cost) { assert(player == &fixture.owner && cost == 10); ++fixture.chargeCalls; return fixture.chargeSuccess; }
D2ActiveRoomStrc* __fastcall CurrentRoom(Unit* player) { assert(player == &fixture.owner); return reinterpret_cast<D2ActiveRoomStrc*>(fixture.room.data()); }
void __fastcall Clear(D2ActiveRoomStrc*, int, int, int pattern, int mask) { assert(pattern == 3 && mask == 0x80); ++fixture.clears; }
void __fastcall Reserve(D2ActiveRoomStrc*, int, int, int pattern, int mask) { assert(pattern == 3 && mask == 0x80); ++fixture.reserves; }
bool Compute(void* path, Unit* player) {
    assert(path == fixture.path.data() && player == &fixture.owner);
    // Reproduce the order in native type-7 compute: a valid cached route
    // prepends its old first waypoint before the cache flag is cleared.
    const bool cached = NativeData::Read<uint8_t>(path, 0x381) && NativeData::Read<uint8_t>(path, 0x380);
    Put(path, 0x34, int(cached ? 2 : 1));
    if (cached) {
        ++fixture.cachedRouteUses;
        Put(path, 0xc8, NativeData::Read<uint16_t>(path, 0x2b8));
        Put(path, 0xca, NativeData::Read<uint16_t>(path, 0x2ba));
    }
    Put(path, cached ? 0xcc : 0xc8, NativeData::Read<uint16_t>(path, 0x10));
    Put(path, cached ? 0xce : 0xca, NativeData::Read<uint16_t>(path, 0x12));
    Put(path, 0x381, uint8_t(0));
    return fixture.computeSuccess;
}
int __fastcall ServerCompute(void* path, Unit* player, int zero, int one) { assert(zero == 0 && one == 1); ++fixture.serverComputes; return Compute(path, player); }
bool __fastcall ClientCompute(Unit* player, void* path) { ++fixture.clientComputes; return Compute(path, player); }

void Fixture::Reset() {
    channels.clear(); client = {}; clientActionDepth = 0; testBindings.clear();
    for (auto& click : itemClicks) click.store(0);
    clickSequence.store(0);
    ready.store(true); enabled.store(true); SetRightClickPickupDisabled(false);
    localIds={};selectedItem=nullptr;selectionCalls=0;
    gold={};goldPath={};goldRow={};goldTable=goldRow.data();goldRows=1;autoGoldTarget=nullptr;
    gold.dwUnitType=4;gold.dwUnitId=42;gold.dwAnimMode=3;gold.dwFlags=2;
    gold.pStaticPath=reinterpret_cast<D2StaticPathStrc*>(goldPath.data());
    Put(goldPath.data(),0x10,int(100));Put(goldPath.data(),0x14,int(200));Put(goldRow.data(),0x126,int16_t(4));
    goldDistance=2;goldFound=true;interactions=0;interactionArguments={};interactionModifiers=0;goldPackets.clear();
    originalClientInteraction=NativeInteraction;
    Bind(Pickup::AutoGoldTargetRva,&autoGoldTarget);Bind(0x1ca0390,&goldTable);Bind(0x1ca0398,&goldRows);
    Bind(Pickup::LookupRva,GoldLookup);Bind(Pickup::DistanceRva,GoldDistance);Bind(Pickup::SenderRva,GoldSend);
    Bind(Pickup::ClientUnitsRva+4*0x400,goldPath.data());
    inputBlocked.store(false); inputStamp.store(GetTickCount64());
    InputMock::left = InputMock::right = InputMock::swapped = false; InputMock::focused = true;
    game = {}; owner = {}; data = {}; connection = {}; hover = {}; path = {}; skill = {}; skills = {}; row = {}; stats = {};
    gMouseHover = &hover;
    startCalls = doCalls = moveCalls = dispatchCalls = pickupCalls = chargeCalls = serverComputes = clientComputes = clears = reserves = 0;
    cachedRouteUses = 0;
    startSuccess = chargeSuccess = computeSuccess = true;
    // Captured from ordinary live gameplay: the bottom HUD MiniMenuPanel
    // (20) stays visible alongside the other noninteractive HUD panels.
    panels = {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0};
    clientHead = &connection; selector = 0; cursorX = 105; cursorY = 206;
    clientStarts = clientMoves = clientActions = 0;
    nestedAction = endOnAction = routeControls = false; inputEvents.clear(); controls.clear();
    game.dwInitSeed = 123456; game.dwGameFrame = 100;
    owner.dwUnitType = 0; owner.dwUnitId = 7; owner.dwAnimMode = 18;
    owner.pDynamicPath = reinterpret_cast<D2DynamicPathStrc*>(path.data()); owner.pPlayerData = &data;
    Put(&owner, 0x88, stats.data());
    data.pClient = &connection; connection.pGame = &game; connection.pPlayer = &owner;
    connection.dwClientState = 4; connection.dwUnitGUID = owner.dwUnitId;
    Put(&owner, 0x100, skills.data()); Put(skills.data(), 0x18, skill.data());
    Put(skills.data(), 8, skill.data()); Put(skills.data(), 0x10, skill.data());
    Put(skill.data(), 0, row.data()); Put(skill.data(), 0x14, uint32_t(1)); Put(skill.data(), 0x44, int(-1));
    Put(skill.data(), 0x24, int(110)); Put(skill.data(), 0x28, int(210));
    Put(skill.data(), 0x2c, uint32_t(1234)); Put(skill.data(), 0x30, int(99));
    Put(row.data(), 0, int16_t(151)); Put(row.data(), 0x4c, int16_t(38)); Put(row.data(), 0x4e, int16_t(76));
    Put(path.data(), 2, uint16_t(100)); Put(path.data(), 6, uint16_t(200));
    Put(path.data(), 0x50, int(7)); Put(path.data(), 0x5c, int(3));
    originalServerStart = NativeStart; originalServerDo = NativeDo; originalServerMove = NativeMove; originalDispatcher = NativeDispatch;
    originalClientAction = NativeClientAction;
    originalClientStart = NativeClientStart; originalClientMove = NativeClientMove;
    Bind(Pickup::HandlerRva, NativePickup); Bind(0x21df90, Cost); Bind(0x294e70, ChargeMana);
    Bind(0x209530, CurrentRoom); Bind(0x230eb0, Clear); Bind(0x230c10, Reserve);
    Bind(0x1f7c30, ServerCompute); Bind(0x175ea0, ClientCompute);
    Bind(0x10dce0, SendControlPacket);
    Bind(0x1d442d8, &selector); Bind(0x6b050, NativeLocalPlayer); Bind(0x1d637f0, &clientHead);
    Bind(0x1d53fb8, panels.data()); Bind(0x1d60a38, &cursorX); Bind(0x1d60a3c, &cursorY); Bind(0x6e5e0, NativeCursor);
    localIds[selector] = owner.dwUnitId;
    Bind(Pickup::LocalPlayerIdsRva, localIds.data()); Bind(Pickup::SelectedUnitRva, SelectedGroundItem);
}

void RightClickPickupPreference() {
    auto& f = fixture; f.Reset();
    assert(!RightClickPickupDisabled());
    SetRightClickPickupDisabled(true); SetEnabled(false);
    f.selectedItem = &f.gold;
    const auto path = f.path;
    const auto skill = f.skill;
    // The shared interaction guard works without a Whirlwind channel or option.
    for (int action : {3, 4}) assert(ClientInteractionHook(&f.owner,action,100,200,0x1234)==1);
    assert(f.interactions==0 && f.selectionCalls==2 && f.path==path && f.skill==skill);
    // Left-click, release, auto-gold, forced actions and other interaction IDs
    // must bypass this guard without even consulting the selected ground unit.
    for (int action : {0,1,2,5,6,7,8,9,10,11,12,13,14,15,16,17}) {
        assert(ClientInteractionHook(&f.owner,action,123,456,0xabcd)==99);
        assert((f.interactionArguments==std::array<int,3>{action,123,456}) && f.interactionModifiers==0xabcd);
    }
    assert(f.selectionCalls==2);
    for (uint32_t mode : {0u,1u,2u,4u,5u,6u}) {
        f.gold.dwAnimMode=mode;
        assert(ClientInteractionHook(&f.owner,3,100,200,0)==99);
    }
    f.gold.dwAnimMode=3;
    for (uint32_t type : {0u,1u,2u,3u,5u}) {
        f.gold.dwUnitType=type;assert(ClientInteractionHook(&f.owner,4,100,200,0)==99);
    }
    f.gold.dwUnitType=4;f.selectedItem=nullptr;
    assert(ClientInteractionHook(&f.owner,3,100,200,0)==99);
    f.selectedItem=&f.gold;f.localIds={};
    assert(ClientInteractionHook(&f.owner,3,100,200,0)==99);
    f.localIds[6]=f.owner.dwUnitId;
    assert(ClientInteractionHook(&f.owner,4,100,200,0)==1);
    SetRightClickPickupDisabled(false);
    assert(ClientInteractionHook(&f.owner,3,100,200,0)==99);
    std::cout<<"Right-click pickup guard works with WW off, blocks down/held ground items before native scheduling, and preserves left/release/auto-gold/inventory/nonitem input.\n";
}

void StartAndAuthentication() {
    auto& f = fixture;
    for (int exclusion = 0; exclusion < 6; ++exclusion) {
        f.Reset();
        if (exclusion == 0) enabled.store(false);
        if (exclusion == 1) f.startSuccess = false;
        if (exclusion == 2) Put(f.row.data(), 0x4e, int16_t(75));
        if (exclusion == 3) Put(f.skill.data(), 0x44, int(123));
        if (exclusion == 4) Put(f.skill.data(), 0x14, uint32_t(0));
        if (exclusion == 5) f.owner.dwAnimMode = 17;
        assert(ServerStartHook(&f.game, &f.owner, 151, 20) == int(f.startSuccess));
        assert(f.startCalls == 1 && channels.empty());
    }
    f.Reset(); f.Arm();
    assert(f.Current().id == 151 && f.Current().level == 20 && f.Current().nextCharge == 125 && !f.Current().lease.accepted);
    auto command = f.Command();
    auto reject = [&](auto change) { auto bad = command; change(bad); f.Receive(bad); assert(!f.Current().lease.accepted); };
    reject([](auto& c) { ++c.recipient; });
    reject([](auto& c) { ++c.session; });
    reject([](auto& c) { ++c.skill; });
    reject([](auto& c) { c.held = 2; });
    reject([](auto& c) { c.version = 2; });
    assert(DispatcherHook(&f.game, &f.owner, &command, sizeof(command) - 1) == 1 && !f.Current().lease.accepted);
    f.connection.dwClientState = 3; f.Receive(command); assert(!f.Current().lease.accepted); f.connection.dwClientState = 4;
    ++f.connection.dwUnitGUID; f.Receive(command); assert(!f.Current().lease.accepted); --f.connection.dwUnitGUID;
    f.connection.pGame = nullptr; f.Receive(command); assert(!f.Current().lease.accepted); f.connection.pGame = &f.game;
    f.data.pClient = nullptr; f.Receive(command); assert(!f.Current().lease.accepted); f.data.pClient = &f.connection;
    f.Current().created = GetTickCount64() - 501; f.Receive(command); assert(!f.Current().lease.accepted);
    f.Current().created = GetTickCount64(); f.Receive(command);
    assert(f.Current().lease.accepted && f.Current().lease.Active(GetTickCount64()) && f.dispatchCalls == 0);
    const auto received = f.Current().lease.received;
    f.Receive(command); assert(f.Current().lease.received == received);
    command.sequence = 2; command.held = 0; f.Receive(command); assert(f.Current().lease.stopped);
    command.sequence = 3; command.held = 1; f.Receive(command); assert(f.Current().lease.stopped && f.Current().lease.sequence == 2);
    assert(f.dispatchCalls == 0);
    std::cout << "Native starts arm only eligible enabled Whirlwind; controls require character, game, skill and client authentication and cannot replay/rearm released leases.\n";
}

void PickupAndLifecycle() {
    auto& f = fixture;
    Pickup::Command pickup{0x16, 44, 100, 200, 1};
    f.Reset(); f.Arm();
    assert(DispatcherHook(&f.game, &f.owner, &pickup, sizeof(pickup)) == 66 && f.pickupCalls == 0);
    f.Receive(f.Command());
    Put(f.stats.data(), 0xacc, uint32_t(0x400000));
    assert(DispatcherHook(&f.game, &f.owner, &pickup, sizeof(pickup)) == 44 && f.pickupCalls == 1);
    assert(NativeData::Read<uint32_t>(f.stats.data(), 0xacc) == 0x400000); // The state itself is retained.
    f.Current().lease.received = GetTickCount64() - Data::LeaseMilliseconds - 1;
    assert(DispatcherHook(&f.game, &f.owner, &pickup, sizeof(pickup)) == 0 && f.pickupCalls == 1);
    assert(ServerDoHook(&f.game, &f.owner, 151, 20) == 77);
    assert(f.Current().lease.stopped && (NativeData::Read<uint32_t>(f.skill.data(), 0x14) & 2));

    f.Reset(); f.Arm(); f.Receive(f.Command());
    f.game.dwGameFrame = 124; assert(ServerDoHook(&f.game, &f.owner, 151, 20) == 77 && !f.chargeCalls);
    f.game.dwGameFrame = 125; ServerDoHook(&f.game, &f.owner, 151, 20); assert(f.chargeCalls == 1 && f.Current().nextCharge == 150);
    ServerDoHook(&f.game, &f.owner, 151, 20); assert(f.chargeCalls == 1);
    f.game.dwGameFrame = 150; f.chargeSuccess = false; ServerDoHook(&f.game, &f.owner, 151, 20);
    assert(f.chargeCalls == 2 && f.Current().lease.stopped && (NativeData::Read<uint32_t>(f.skill.data(), 0x14) & 2));
    f.Reset(); f.Arm(); f.Receive(f.Command()); enabled.store(false);
    assert(ServerDoHook(&f.game, &f.owner, 151, 20) == 77 && f.Current().lease.stopped && (NativeData::Read<uint32_t>(f.skill.data(), 0x14) & 2));
    assert(DispatcherHook(&f.game, &f.owner, &pickup, sizeof(pickup)) == 66 && !f.pickupCalls);
    f.Reset(); f.game.dwGameFrame = UINT32_MAX - 10; f.Arm(); f.Receive(f.Command());
    assert(f.Current().nextCharge == 14);
    f.game.dwGameFrame = 13; ServerDoHook(&f.game, &f.owner, 151, 20); assert(!f.chargeCalls);
    f.game.dwGameFrame = 14; ServerDoHook(&f.game, &f.owner, 151, 20); assert(f.chargeCalls == 1);
    f.Reset(); enabled.store(false);
    assert(ServerStartHook(&f.game, &f.owner, 151, 20) == 1 && channels.empty());
    assert(ServerMoveHook(&f.game, &f.owner) == 88 && ServerDoHook(&f.game, &f.owner, 151, 20) == 77);
    assert(NativeData::Read<uint32_t>(f.skill.data(), 0x14) == 1 && !f.chargeCalls);
    std::cout << "Pickup requires an active authenticated channel; timeout, disabling and insufficient mana finish natively while disabled behavior delegates unchanged.\n";
}

void SteeringIntegration() {
    auto& f = fixture;
    f.Reset(); f.Arm(); auto command = f.Command(); command.x = 100; command.y = 200; f.Receive(command);
    assert(ServerMoveHook(&f.game, &f.owner) == 1 && !f.moveCalls && !f.serverComputes);
    assert(f.clears == 1 && f.reserves == 1 && !f.Current().steered);
    assert(NativeData::Read<int>(f.skill.data(), 0x24) == 100 && NativeData::Read<int>(f.skill.data(), 0x28) == 200);
    command.sequence = 2; command.x = 130; command.y = 240; f.Receive(command);
    assert(ServerMoveHook(&f.game, &f.owner) == 88 && f.serverComputes == 1 && f.Current().steered);
    assert(NativeData::Read<int>(f.skill.data(), 0x24) == 107 && NativeData::Read<int>(f.skill.data(), 0x28) == 210);
    assert(NativeData::Read<uint32_t>(f.skill.data(), 0x2c) == 1234 && NativeData::Read<int>(f.skill.data(), 0x30) == 99);
    Put(f.path.data(), 0x34, int(10)); Put(f.path.data(), 0x30, int(0));
    assert(ServerMoveHook(&f.game, &f.owner) == 88 && f.serverComputes == 1); // Existing path has room to progress.
    command.sequence = 3; command.x = 100; command.y = 200; f.Receive(command);
    assert(ServerMoveHook(&f.game, &f.owner) == 1 && !f.Current().steered && f.serverComputes == 1);
    command.sequence = 4; command.x = 130; command.y = 240; f.Receive(command);
    assert(ServerMoveHook(&f.game, &f.owner) == 88 && f.Current().steered && f.serverComputes == 2);
    command.sequence = 5; command.x = 90; f.Receive(command); f.computeSuccess = false;
    assert(ServerMoveHook(&f.game, &f.owner) == 88 && f.Current().lease.stopped && (NativeData::Read<uint32_t>(f.skill.data(), 0x14) & 2));

    f.Reset(); Data::Point previous{}; bool steered = false;
    const auto savedPath = f.path;
    f.computeSuccess = false;
    assert(!Steer(&f.owner, f.skill.data(), {105, 205}, previous, steered, false));
    assert(f.path == savedPath && !steered && previous.x == 0 && f.clientComputes == 1 && !f.serverComputes);
    f.computeSuccess = true;
    assert(Steer(&f.owner, f.skill.data(), {105, 205}, previous, steered, false));
    assert(steered && previous.x == 105 && previous.y == 205 && f.clientComputes == 2);
    assert(NativeData::Read<uint32_t>(f.skill.data(), 0x2c) == 1234 && NativeData::Read<int>(f.skill.data(), 0x30) == 99);
    f.Reset(); f.Arm(); ++f.game.dwInitSeed;
    assert(!Channel(&f.game, &f.owner) && channels.empty());
    f.Reset(); f.Arm(); Put(f.skills.data(), 0x18, static_cast<void*>(nullptr));
    assert(!Channel(&f.game, &f.owner) && channels.empty());
    std::cout << "Server/client steering preserves native damage timing, reuses progressing paths, rolls back failure, and rejects stale game identities.\n";
}

void BoundedLocalSession() {
    auto& f = fixture; f.Reset();
    SYSTEM_INFO system{}; GetSystemInfo(&system);
    const auto pageBytes = size_t(system.dwPageSize);
    auto allocation = static_cast<uint8_t*>(VirtualAlloc(nullptr, pageBytes * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    assert(allocation && pageBytes >= 0x888);
    DWORD previous = 0; assert(VirtualProtect(allocation + pageBytes, pageBytes, PAGE_NOACCESS, &previous));
    // The historical Reclass declaration is 0x1088, but this build's native
    // allocation is 0x888. Place it against a guard page to catch overreads.
    auto nativeClient = reinterpret_cast<Client*>(allocation + pageBytes - 0x888);
    std::memcpy(nativeClient, &f.connection, 0x560);
    Bind(0x1d637f0, &nativeClient);
    uint32_t seed = 0;
    assert(LocalSession(&f.owner, seed) && seed == uint32_t(f.game.dwInitSeed));
    Put(nativeClient, offsetof(Client, dwUnitGUID), uint32_t(f.owner.dwUnitId + 1));
    assert(!LocalSession(&f.owner, seed));
    assert(VirtualFree(allocation, 0, MEM_RELEASE));
    std::cout << "Local session discovery reads only native client fields and remains safe at an allocation guard page.\n";
}

void InitialCastOrdering() {
    auto& f = fixture; f.Reset();
    ClientActionHook(0xc, &f.owner, 0, 0);
    assert((f.inputEvents == std::vector<int>{1, 2}));
    assert(client.sequence == 1 && !client.pending && clientActionDepth == 0);
    f.Reset(); f.nestedAction = true;
    ClientActionHook(0xc, &f.owner, 0, 0);
    assert((f.inputEvents == std::vector<int>{1, 1, 2}));
    assert(client.sequence == 1 && !client.pending && clientActionDepth == 0);
    std::cout << "Initial and nested client prediction queue the native cast before the first control heartbeat.\n";
}

void CapturedItemClicks() {
    auto& f = fixture; f.Reset();
    f.hover.IsHovered = 1; f.hover.HoveredUnitType = UNIT_ITEM; f.hover.HoveredUnitId = 0xdeadbeef;
    MouseButtonDown(1);
    const auto left = itemClicks[0].load();
    assert(uint32_t(left) == 0xdeadbeef && (left >> 32) == 1 && !itemClicks[1].load());
    // A mouse tap may finish before the 25 Hz simulation samples input. Its
    // exact target must survive a subsequent hover change or loss of hover.
    f.hover.HoveredUnitId = 123; f.hover.IsHovered = 0;
    assert(itemClicks[0].load() == left);
    MouseButtonDown(2); assert(!itemClicks[1].load() && clickSequence.load() == 1);
    f.hover.IsHovered = 1; f.hover.HoveredUnitType = UNIT_MONSTER;
    MouseButtonDown(2); assert(!itemClicks[1].load() && clickSequence.load() == 1);
    f.hover.HoveredUnitType = UNIT_ITEM; f.hover.HoveredUnitId = UINT32_MAX;
    MouseButtonDown(2);
    const auto right = itemClicks[1].load();
    assert(uint32_t(right) == UINT32_MAX && (right >> 32) == 2 && itemClicks[0].load() == left);
    MouseButtonDown(2);
    const auto repeated = itemClicks[1].load();
    assert(uint32_t(repeated) == UINT32_MAX && (repeated >> 32) == 3 && repeated != right);
    for (int invalid : {-1, 0, 3, INT_MAX}) MouseButtonDown(invalid);
    enabled.store(false); MouseButtonDown(1); enabled.store(true);
    ready.store(false); MouseButtonDown(1); ready.store(true);
    gMouseHover = nullptr; MouseButtonDown(1); gMouseHover = &f.hover;
    assert(itemClicks[0].load() == left && itemClicks[1].load() == repeated && clickSequence.load() == 3);
    std::cout << "Mouse-down snapshots preserve quick-tap item targets, isolate logical buttons, and reject nonitems, invalid buttons and disabled input.\n";
}

void BeginHeldClient(int button) {
    auto& f = fixture; f.Reset(); f.Arm(); f.routeControls = true; f.endOnAction = true;
    InputMock::left = button == 1; InputMock::right = button == 2;
    assert(ClientStartHook(&f.owner, 151) == 1 && client.active && client.pending && client.button == button);
    assert(Held(&f.owner) && f.controls.size() == 1 && f.controls.back().held == 1 && f.Current().lease.accepted);
}

void WhirlwindPickupPreference() {
    auto& f=fixture;
    BeginHeldClient(1);
    f.hover.IsHovered=1;f.hover.HoveredUnitType=UNIT_ITEM;f.hover.HoveredUnitId=f.gold.dwUnitId;
    MouseButtonDown(2); // Queue before changing the preference.
    assert(itemClicks[1].load());
    SetRightClickPickupDisabled(true);
    assert(Held(&f.owner) && f.goldPackets.empty());
    const auto oldClick=itemClicks[1].load();
    MouseButtonDown(2);assert(itemClicks[1].load()==oldClick);
    BeginHeldClient(2);
    SetRightClickPickupDisabled(true);
    f.hover.IsHovered=1;f.hover.HoveredUnitType=UNIT_ITEM;f.hover.HoveredUnitId=f.gold.dwUnitId;
    MouseButtonDown(1);
    assert(Held(&f.owner) && f.goldPackets.size()==1 && f.goldPackets[0].item==f.gold.dwUnitId);
    f.autoGoldTarget=&f.gold;
    assert(ClientInteractionHook(&f.owner,7,100,200,0)==1 && f.goldPackets.size()==2);
    std::cout<<"Disabling right-click pickups suppresses queued/new WW right clicks; left-click pickup and native auto-gold remain available during the channel.\n";
}

void LiveHudActivation() {
    auto& f = fixture; f.Reset();
    assert(f.panels[0] && f.panels[12] && f.panels[20] && f.panels[28] && f.panels[29]);
    InputMock::right = true; f.routeControls = true;
    SetEnabled(false);
    assert(ClientStartHook(&f.owner, 151) == 1 && !client.active && f.controls.empty());
    SetEnabled(true); f.Arm();
    assert(ClientStartHook(&f.owner, 151) == 1 && client.active && client.pending);
    assert(Held(&f.owner) && f.controls.size() == 1 && f.Current().lease.accepted);
    f.cursorX = 160; f.cursorY = 200;
    assert(ClientMoveHook(&f.owner, true) == 55 && f.clientComputes == 1 && client.active);
    assert(ServerMoveHook(&f.game, &f.owner) == 88 && f.serverComputes == 1 && !f.Current().lease.stopped);
    assert(NativeData::Read<int>(f.skill.data(), 0x24) == 112 && f.panels[20] == 1);
    // The D2RHUD options menu publishes capture separately from native panel
    // visibility. Opening it stops the channel; closing it permits a new cast.
    SetInputBlocked(true);
    assert(!Held(&f.owner) && !client.active && f.Current().lease.stopped && !f.controls.back().held);
    const auto stoppedControls = f.controls.size();
    assert(ClientStartHook(&f.owner, 151) == 1 && !client.active && f.controls.size() == stoppedControls);
    SetInputBlocked(false); f.Arm();
    assert(ClientStartHook(&f.owner, 151) == 1 && client.active && Held(&f.owner));
    assert(!f.Current().lease.stopped && f.controls.back().held && f.panels[20] == 1);
    for (const int panel : {1, 9}) { // Native inventory and pause menu.
        f.Reset(); f.routeControls = true; InputMock::right = true;
        f.panels[panel] = 1;
        assert(ClientStartHook(&f.owner, 151) == 1 && !client.active && f.controls.empty());
        f.panels[panel] = 0; f.Arm();
        assert(ClientStartHook(&f.owner, 151) == 1 && client.active && Held(&f.owner));
        f.panels[panel] = 1;
        assert(!Held(&f.owner) && !client.active && f.Current().lease.stopped && !f.controls.back().held);
        f.panels[panel] = 0; f.Arm();
        assert(ClientStartHook(&f.owner, 151) == 1 && client.active && Held(&f.owner) && f.panels[20]);
    }
    std::cout << "Captured live HUD permits enabled activation and client/server movement; options, inventory and pause menus block input until closed.\n";
}

void RepeatedHeldActions() {
    auto& f = fixture;
    for (int button : {1, 2}) {
        BeginHeldClient(button);
        const auto initialHit = NativeData::Read<uint32_t>(f.skill.data(), 0x2c);
        const auto initialTarget = NativeData::Read<int>(f.skill.data(), 0x30);
        const auto initialCharge = f.Current().nextCharge;
        const auto initialCreated = f.Current().created;
        const int first = button == 1 ? 0x5 : 0xc;
        for (int repeat = 0; repeat < 4; ++repeat) for (int action = first; action < first + 6; ++action) {
            client.sent = GetTickCount64();
            ClientActionHook(action, &f.owner, 0, 0);
            assert(client.active && client.sequence >= 1 && f.clientActions == 0 && f.clientStarts == 1 && f.startCalls == 1);
            assert(NativeData::Read<uint32_t>(f.skill.data(), 0x14) == 1);
            assert(NativeData::Read<uint32_t>(f.skill.data(), 0x2c) == initialHit && NativeData::Read<int>(f.skill.data(), 0x30) == initialTarget);
            assert(f.Current().nextCharge == initialCharge && f.Current().created == initialCreated && !f.Current().lease.stopped);
        }
        // Opposite-hand actions and explicit forced attacks must retain native
        // behavior; the dangerous prediction mock proves they really delegate.
        for (int action : {button == 1 ? 0xc : 0x5, 0xb, 0x12, 0x2d}) {
            BeginHeldClient(button); ClientActionHook(action, &f.owner, 0, 0);
            assert(f.clientActions == 1 && (NativeData::Read<uint32_t>(f.skill.data(), 0x14) & 2));
        }
        BeginHeldClient(button);
        Put(f.row.data(), 0, int16_t(152));
        ClientActionHook(first, &f.owner, 0, 0);
        assert(f.clientActions == 1 && !client.active && !f.controls.back().held && f.Current().lease.stopped);
        BeginHeldClient(button);
        InputMock::left = InputMock::right = false;
        ClientActionHook(first, &f.owner, 0, 0);
        assert(f.clientActions == 1 && !client.active && !f.controls.back().held && f.Current().lease.stopped);
    }
    std::cout << "Repeated held-button skill actions cannot finish/recast Whirlwind or reset sequence, mana schedule or hit timing; other-hand, forced attacks, skill changes and release retain native behavior.\n";
}

void ImmediateCursorControls() {
    auto& f = fixture; BeginHeldClient(2);
    assert(client.sentCursor.x == 105 && client.sentCursor.y == 206);
    client.sent = GetTickCount64(); f.cursorX = 160; f.cursorY = 200;
    assert(Held(&f.owner) && f.controls.size() == 2 && f.controls.back().x == 160 && f.controls.back().y == 200);
    assert(client.sentCursor.x == 160 && client.sentCursor.y == 200);
    client.sent = GetTickCount64(); assert(Held(&f.owner) && f.controls.size() == 2);
    f.cursorX = 80; clientActionDepth = 1;
    assert(Held(&f.owner) && f.controls.size() == 2); // Never overtake an in-progress native cast packet.
    clientActionDepth = 0;
    assert(Held(&f.owner) && f.controls.size() == 3 && f.controls.back().x == 80);
    assert(ServerMoveHook(&f.game, &f.owner) == 88 && NativeData::Read<int>(f.skill.data(), 0x24) == 88);
    assert(ClientMoveHook(&f.owner, true) == 55 && f.clientComputes == 1);
    for (int cursor : {160, 40, 180, 20}) {
        const auto count = f.controls.size();
        client.sent = GetTickCount64(); f.cursorX = cursor;
        assert(Held(&f.owner) && f.controls.size() == count + 1 && f.controls.back().x == cursor);
        Put(f.path.data(), 0x30, int(1)); Put(f.path.data(), 0x34, int(10)); // Reverse before reaching the old endpoint.
        auto cacheOldDirection = [&] {
            Put(f.path.data(), 0x2b4, uint16_t(100)); Put(f.path.data(), 0x2b6, uint16_t(200));
            Put(f.path.data(), 0x2b8, uint16_t(cursor > 100 ? 90 : 110)); Put(f.path.data(), 0x2ba, uint16_t(200));
            Put(f.path.data(), 0x380, uint8_t(1)); Put(f.path.data(), 0x381, uint8_t(1));
        };
        auto correctFirstStep = [&] {
            const auto firstX = NativeData::Read<uint16_t>(f.path.data(), 0xc8);
            assert(cursor > 100 ? firstX > 100 : firstX < 100);
            assert(!f.cachedRouteUses && NativeData::Read<int>(f.path.data(), 0x34) == 1);
        };
        cacheOldDirection();
        const auto serverPaths = f.serverComputes, clientPaths = f.clientComputes;
        assert(ServerMoveHook(&f.game, &f.owner) == 88 && f.serverComputes == serverPaths + 1);
        correctFirstStep();
        cacheOldDirection(); // Model the client's independent path cache too.
        assert(ClientMoveHook(&f.owner, true) == 55 && f.clientComputes == clientPaths + 1);
        correctFirstStep();
        const int endpoint = NativeData::Read<int>(f.skill.data(), 0x24);
        assert(cursor > 100 ? endpoint > 100 : endpoint < 100);
        assert(client.active && !f.Current().lease.stopped && NativeData::Read<uint32_t>(f.skill.data(), 0x14) == 1);
        assert(f.clientStarts == 1 && f.startCalls == 1 && f.Current().nextCharge == 125 && !f.chargeCalls);
        assert(NativeData::Read<uint32_t>(f.skill.data(), 0x2c) == 1234 && NativeData::Read<int>(f.skill.data(), 0x30) == 99);
    }
    for (size_t i = 0; i < f.controls.size(); ++i) assert(f.controls[i].sequence == uint32_t(i + 1));
    std::cout << "Changed cursors publish immediately; rapid client/server reversals discard cached old first waypoints without recasts, hit-timer resets or extra mana charges.\n";
}

void AutomaticGoldRequests(){
    auto& f=fixture;
    for(const int button:{1,2}){
        BeginHeldClient(button);Put(f.stats.data(),0xacc,uint32_t(0x400000));
        assert(ClientInteractionHook(&f.owner,7,100,200,0x1234)==99 && f.interactions==1 && f.goldPackets.empty());
        f.autoGoldTarget=&f.gold;const auto path=f.path;const auto skill=f.skill;const auto player=f.owner;
        assert(ClientInteractionHook(&f.owner,7,100,200,0x1234)==1);
        assert(f.interactions==1 && f.goldPackets.size()==1 && f.pickupCalls==1);
        assert(f.goldPackets[0].item==42 && f.goldPackets[0].x==100 && f.goldPackets[0].y==200 && f.goldPackets[0].autoPlace==1);
        assert(f.path==path && f.skill==skill && !memcmp(&player,&f.owner,sizeof(player)) && client.active);
        f.goldDistance=5;assert(ClientInteractionHook(&f.owner,7,100,200,0)==0);
        assert(f.interactions==1 && f.goldPackets.size()==1 && f.path==path && f.skill==skill && client.active);
        f.goldDistance=2;Put(f.goldRow.data(),0x126,int16_t(1));
        assert(ClientInteractionHook(&f.owner,7,123,456,0xabcd)==99 && f.interactions==2);
        assert((f.interactionArguments==std::array<int,3>{7,123,456}) && f.interactionModifiers==0xabcd);
        Put(f.goldRow.data(),0x126,int16_t(4));
        assert(ClientInteractionHook(&f.owner,6,100,200,0)==99 && f.interactions==3);
        enabled.store(false);assert(ClientInteractionHook(&f.owner,7,100,200,0)==99 && f.interactions==4);
        assert(f.goldPackets.size()==1);
    }
    BeginHeldClient(2);f.autoGoldTarget=&f.gold;InputMock::right=false;
    assert(ClientInteractionHook(&f.owner,7,100,200,0)==99 && f.goldPackets.empty() && !client.active);
    f.Reset();f.autoGoldTarget=&f.gold;
    assert(ClientInteractionHook(&f.owner,7,100,200,0)==99 && f.goldPackets.empty());
    std::cout<<"Native auto-gold requests use exact pickup packets during either-button channels without clicks, recasts or path changes; ordinary input/non-gold/disabled/released channels retain native handling.\n";
}
#include "d3-whirlwind-gold-native-test.inl"
#include "d3-whirlwind-restart-test.inl"
#include "d3-whirlwind-potion-test.inl"
int main(int argc,char** argv) { assert(!RightClickPickupDisabled()); RightClickPickupPreference(); StartAndAuthentication(); PickupAndLifecycle(); SteeringIntegration(); BoundedLocalSession(); InitialCastOrdering(); CapturedItemClicks(); WhirlwindPickupPreference(); LiveHudActivation(); RepeatedHeldActions(); ImmediateCursorControls(); AutomaticGoldRequests(); WhirlwindPotions(); if(argc>1){NativeAutoGold(argv[1]);NativeAssassinRestart(argv[1]);NativeWhirlwindPotions(argv[1]);} }
