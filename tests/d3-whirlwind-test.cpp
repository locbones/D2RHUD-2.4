#include "../d2rhud/plugin/D3WhirlwindData.h"
#include "../d2rhud/plugin/D3WhirlwindNative.h"
#include "../d2rhud/plugin/D3WhirlwindPickup.h"
#include <cassert>
#include <iostream>
#include <map>
#include <vector>

namespace {
std::map<uint32_t, uintptr_t> bindings;
}
DWORD64 Pattern::Address(uint32_t rva) { return bindings[rva]; }
template<class T> void Bind(uint32_t rva, T pointer) { bindings[rva] = reinterpret_cast<uintptr_t>(pointer); }
template<size_t N> using Bytes = std::array<uint8_t, N>;
template<class T> void Put(void* pointer, size_t offset, T value) {
    std::memcpy(static_cast<uint8_t*>(pointer) + offset, &value, sizeof(value));
}
template<class T> T Get(const void* pointer, size_t offset) {
    T value; std::memcpy(&value, static_cast<const uint8_t*>(pointer) + offset, sizeof(value)); return value;
}

void ProtocolAndLease() {
    using namespace D3Whirlwind::Data;
    Command command;
    command.held = 1; command.recipient = 12; command.session = 99;
    command.skill = 151; command.x = 100; command.y = 200; command.sequence = 4;
    auto bytes = std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&command), sizeof(command));
    assert(Valid(command) && Ours(bytes));
    assert(!Ours(bytes.first(9)) && !Ours({}));
    auto reject = [&](auto change) { auto bad = command; change(bad); assert(!Valid(bad)); };
    reject([](auto& c) { c.opcode = 4; });
    reject([](auto& c) { c.magic ^= 1; });
    reject([](auto& c) { c.operation = 3; });
    reject([](auto& c) { c.version = 2; });
    reject([](auto& c) { c.held = 2; });
    reject([](auto& c) { c.skill = -1; });
    reject([](auto& c) { c.skill = 32768; });
    reject([](auto& c) { c.x = 0; });
    reject([](auto& c) { c.x = 65536; });
    reject([](auto& c) { c.y = -1; });
    reject([](auto& c) { c.y = 65536; });
    auto wrong = command; wrong.magic ^= 1;
    assert(!Ours({reinterpret_cast<const uint8_t*>(&wrong), sizeof(wrong)}));
    wrong = command; wrong.operation = 3;
    assert(!Ours({reinterpret_cast<const uint8_t*>(&wrong), sizeof(wrong)}));
    auto stop = command; stop.held = 0; stop.x = 0; stop.y = 0;
    assert(Valid(stop)); // Releasing remains valid without a world cursor.
    command.skill = 32767; command.x = command.y = 65535; assert(Valid(command));

    Lease lease;
    assert(!lease.Active(1000));
    assert(lease.Receive(command, 1000));
    assert(lease.Active(1000) && lease.Active(1400));
    assert(!lease.Active(999) && !lease.Active(1401));
    assert(!lease.Receive(command, 1200)); // Replay never extends a lease.
    assert(lease.received == 1000 && lease.sequence == 4);
    command.sequence = 3; assert(!lease.Receive(command, 1200));
    command.sequence = 5; command.x = 125;
    assert(lease.Receive(command, 1300) && lease.Active(1700));
    assert(lease.cursor.x == 125 && lease.cursor.y == 65535);
    stop.sequence = 6;
    assert(lease.Receive(stop, 1350) && !lease.Active(1350) && lease.stopped);
    command.sequence = 7;
    assert(!lease.Receive(command, 1400)); // A released session cannot restart.

    Lease wrap;
    command.sequence = UINT32_MAX - 1; assert(wrap.Receive(command, 1));
    command.sequence = UINT32_MAX; assert(wrap.Receive(command, 2));
    command.sequence = 0; assert(wrap.Receive(command, 3));
    command.sequence = 1; assert(wrap.Receive(command, 4));
    command.sequence = UINT32_MAX; assert(!wrap.Receive(command, 5));
    command.sequence = 0x80000001; assert(!wrap.Receive(command, 5));
    assert(wrap.sequence == 1 && wrap.received == 4);

    assert(Stationary({100, 100}, {100, 100}));
    assert(Stationary({100, 100}, {101, 99}));
    assert(!Stationary({100, 100}, {102, 100}));
    assert(!Stationary({100, 100}, {100, 98}));
    auto target = Target({100, 100}, {104, 106});
    assert(target.x == 104 && target.y == 106);
    target = Target({100, 100}, {1000, 100}); assert(target.x == 112 && target.y == 100);
    target = Target({100, 100}, {100, 1}); assert(target.x == 100 && target.y == 88);
    for (Point cursor : {Point{65535, 65535}, Point{1, 65535}, Point{65535, 1}, Point{1, 1}}) {
        target = Target({100, 100}, cursor);
        const auto dx = target.x - 100, dy = target.y - 100;
        assert(dx * dx + dy * dy <= 169 && target.x >= 1 && target.x <= 65535 && target.y >= 1 && target.y <= 65535);
    }
    target = Target({1, 1}, {0, 0}); assert(target.x == 1 && target.y == 1);
    target = Target({65535, 65535}, {65536, 65536}); assert(target.x == 65535 && target.y == 65535);
    for (int action = 0; action <= 0x43; ++action) {
        assert(UsesButton(action, 1) == (action >= 0x5 && action <= 0xa));
        assert(UsesButton(action, 2) == (action >= 0xc && action <= 0x11));
        assert(!UsesButton(action, 0) && !UsesButton(action, 3));
    }
    for (int action : {INT_MIN, INT_MAX, 0xb, 0x12, 0x2d})
        assert(!UsesButton(action, 1) && !UsesButton(action, 2));
    assert(!ControlDue({100, 200}, {100, 200}, 100, 100, false));
    assert(!ControlDue({100, 200}, {100, 200}, 139, 100, false));
    assert(ControlDue({100, 200}, {100, 200}, 140, 100, false));
    assert(ControlDue({101, 200}, {100, 200}, 101, 100, false));
    assert(ControlDue({100, 199}, {100, 200}, 101, 100, false));
    assert(ControlDue({100, 200}, {100, 200}, 100, 100, true));
    std::cout << "Whirlwind protocol, malformed commands, release, lease timeout, replay/wrap ordering and bounded cursor targets passed.\n";
}

namespace Mock {
using Unit = D2UnitStrc;
using Game = D2GameStrc;
Unit owner{}, item{};
Game game{};
Bytes<0x400> path{};
Bytes<0x50> skill{};
Bytes<0x88> staticPath{};
Bytes<0x100> room{}, itemTable{};
Bytes<0xad8> stats{};
uint32_t distance = 4;
bool found = true;
int sends = 0, callbacks = 0;
std::vector<uint8_t> wire;
const void* forwarded = nullptr;
int callbackResult = 4;
Unit* __fastcall Lookup(void* table, int bucket, uint32_t id, int type) {
    assert(table == itemTable.data() && bucket == int(id & 0x7f) && type == 4);
    return found ? &item : nullptr;
}
uint32_t __fastcall Distance(Unit* player, Unit* target) { assert(player == &owner && target == &item); return distance; }
void __fastcall Send(const void* bytes, int count) {
    ++sends; const auto first = static_cast<const uint8_t*>(bytes); wire.assign(first, first + count);
}
int __fastcall Callback(Game* currentGame, Unit* player, const void* bytes, int count) {
    assert(currentGame == &game && player == &owner && count == 17);
    ++callbacks; forwarded = bytes; return callbackResult;
}
int clears = 0, reserves = 0, serverComputes = 0, clientComputes = 0;
int reservedX = 0, reservedY = 0;
bool computeSuccess = true;
int computedCount = 1;
bool modelRouteCache = false;
int cachedRouteUses = 0;
D2ActiveRoomStrc* __fastcall Room(Unit* player) { assert(player == &owner); return reinterpret_cast<D2ActiveRoomStrc*>(room.data()); }
void __fastcall Clear(D2ActiveRoomStrc* currentRoom, int x, int y, int pattern, int mask) {
    assert(currentRoom == reinterpret_cast<D2ActiveRoomStrc*>(room.data()) && x == 110 && y == 210 && pattern == 3 && mask == 0x80); ++clears;
}
void __fastcall Reserve(D2ActiveRoomStrc* currentRoom, int x, int y, int pattern, int mask) {
    assert(currentRoom == reinterpret_cast<D2ActiveRoomStrc*>(room.data()) && pattern == 3 && mask == 0x80);
    ++reserves; reservedX = x; reservedY = y;
}
bool Compute(void* pointer, Unit* player) {
    assert(pointer == path.data() && player == &owner);
    assert(Get<uint32_t>(pointer, 0x64) == 0xc01 && !Get<void*>(pointer, 0x70)
        && Get<uint32_t>(pointer, 0x78) == 6 && Get<uint32_t>(pointer, 0x7c) == UINT32_MAX);
    // Type-7's builder consults the cached route BEFORE generic compute
    // clears its valid byte. A stale route can prepend its old waypoint
    // even though the final waypoint already equals the requested target.
    const bool cached = modelRouteCache && Get<uint8_t>(pointer, 0x381) && Get<uint8_t>(pointer, 0x380);
    const int count = modelRouteCache ? (cached ? 2 : 1) : computedCount;
    Put(pointer, 0x34, count);
    if (cached) {
        ++cachedRouteUses;
        Put(pointer, 0xc8, Get<uint16_t>(pointer, 0x2b8));
        Put(pointer, 0xca, Get<uint16_t>(pointer, 0x2ba));
    }
    if (count >= 1 && count <= 78) {
        Put(pointer, 0xc8 + size_t(count - 1) * 4, modelRouteCache ? Get<uint16_t>(pointer, 0x10) : uint16_t(107));
        Put(pointer, 0xca + size_t(count - 1) * 4, modelRouteCache ? Get<uint16_t>(pointer, 0x12) : uint16_t(208));
    }
    Put(pointer, 0x380, uint8_t(0xbe));
    Put(pointer, 0x381, uint8_t(0)); // Native clears this only after the builder.
    return computeSuccess;
}
int __fastcall ComputeServer(void* pointer, Unit* player, int zero, int one) {
    assert(zero == 0 && one == 1); ++serverComputes; return Compute(pointer, player);
}
bool __fastcall ComputeClient(Unit* player, void* pointer) { ++clientComputes; return Compute(pointer, player); }
int cost = 10, chargeCalls = 0, chargeResult = 1, life = 100;
int __fastcall Cost(int id, int level) { assert(id == 151 && level == 20); return cost; }
int __fastcall Charge(Unit* player, int amount) { assert(player == &owner && amount == cost); ++chargeCalls; return chargeResult; }
int __fastcall Stat(Unit* player, int id, uint16_t layer) { assert(player == &owner && id == 6 && layer == 0); return life; }
void Reset() {
    owner = {}; item = {}; game = {}; path = {}; skill = {}; staticPath = {}; stats = {};
    owner.dwUnitType = 0; owner.dwUnitId = 3; owner.dwAnimMode = 18;
    owner.pDynamicPath = reinterpret_cast<D2DynamicPathStrc*>(path.data());
    owner.pStatListEx = reinterpret_cast<D2StatListExStrc*>(stats.data());
    item.dwUnitType = 4; item.dwUnitId = 0xaabbccdd; item.dwAnimMode = 3; item.dwFlags = 2;
    item.pStaticPath = reinterpret_cast<D2StaticPathStrc*>(staticPath.data());
    Put(staticPath.data(), 0x10, int32_t(12345)); Put(staticPath.data(), 0x14, int32_t(23456));
    Put(path.data(), 2, uint16_t(100)); Put(path.data(), 6, uint16_t(200));
    Put(path.data(), 0x50, int(7)); Put(path.data(), 0x5c, int(3));
    Put(path.data(), 0x70, reinterpret_cast<void*>(uintptr_t(0x12345678)));
    Put(path.data(), 0x78, uint32_t(1)); Put(path.data(), 0x7c, uint32_t(77));
    Put(path.data(), 0x388, uint64_t(0xabcdef0123456789));
    Put(skill.data(), 0x14, uint32_t(1)); Put(skill.data(), 0x24, int(110)); Put(skill.data(), 0x28, int(210));
    Put(skill.data(), 0x2c, uint32_t(0x123)); Put(skill.data(), 0x30, int(88)); Put(skill.data(), 0x44, int(-1));
    distance = 4; found = true; sends = callbacks = 0; wire.clear(); forwarded = nullptr; callbackResult = 4;
    clears = reserves = serverComputes = clientComputes = 0;
    computeSuccess = true; computedCount = 1; modelRouteCache = false; cachedRouteUses = 0;
    cost = 10; chargeCalls = 0; chargeResult = 1; life = 100;
    Bind(D3Whirlwind::Pickup::LookupRva, Lookup); Bind(D3Whirlwind::Pickup::DistanceRva, Distance);
    Bind(D3Whirlwind::Pickup::SenderRva, Send); Bind(D3Whirlwind::Pickup::HandlerRva, Callback);
    Bind(D3Whirlwind::Pickup::ClientUnitsRva + 4 * 0x400, itemTable.data());
    Bind(0x209530, Room); Bind(0x230eb0, Clear); Bind(0x230c10, Reserve);
    Bind(0x1f7c30, ComputeServer); Bind(0x175ea0, ComputeClient);
    Bind(0x21df90, Cost); Bind(0x294e70, Charge); Bind(0x1e5620, Stat);
}
}

void PickupRequests() {
    using namespace Mock;
    using namespace D3Whirlwind::Pickup;
    Reset();
    const auto beforeOwner = owner, beforeItem = item;
    const auto beforePath = path;
    const auto beforeStatic = staticPath;
    assert(ClientRequest(&owner, item.dwUnitId) && sends == 1 && wire.size() == 17);
    assert(wire[0] == 0x16 && Get<uint32_t>(wire.data(), 1) == item.dwUnitId);
    assert(Get<int32_t>(wire.data(), 5) == 12345 && Get<int32_t>(wire.data(), 9) == 23456 && Get<uint32_t>(wire.data(), 13) == 1);
    assert(!std::memcmp(&owner, &beforeOwner, sizeof(owner)) && !std::memcmp(&item, &beforeItem, sizeof(item)));
    assert(path == beforePath && staticPath == beforeStatic);
    assert(ClientRequest(&owner, item.dwUnitId, false) && Get<uint32_t>(wire.data(), 13) == 0);
    const auto sent = sends;
    distance = 5; assert(!ClientRequest(&owner, item.dwUnitId)); distance = 4;
    found = false; assert(!ClientRequest(&owner, item.dwUnitId)); found = true;
    item.dwFlags = 0; assert(!ClientRequest(&owner, item.dwUnitId)); item.dwFlags = 2;
    item.dwAnimMode = 4; assert(!ClientRequest(&owner, item.dwUnitId)); item.dwAnimMode = 3;
    assert(!ClientRequest(&owner, item.dwUnitId + 1));
    owner.dwAnimMode = 17; assert(!ClientRequest(&owner, item.dwUnitId)); owner.dwAnimMode = 18;
    assert(sends == sent);

    Command request{0x16, item.dwUnitId, 12345, 23456, 1};
    assert(ServerRequest(&game, &owner, &request, sizeof(request)) == callbackResult && callbacks == 1 && forwarded == &request);
    callbackResult = 0; assert(ServerRequest(&game, &owner, &request, sizeof(request)) == 0 && callbacks == 2);
    assert(ServerRequest(&game, &owner, &request, 16) == 3);
    request.header = 0x13; assert(ServerRequest(&game, &owner, &request, sizeof(request)) == 3); request.header = 0x16;
    assert(ServerRequest(&game, &owner, nullptr, sizeof(request)) == 3);
    owner.dwFlags = 0x10000; assert(ServerRequest(&game, &owner, &request, sizeof(request)) == 0); owner.dwFlags = 0;
    owner.dwAnimMode = 0; assert(ServerRequest(&game, &owner, &request, sizeof(request)) == 0); owner.dwAnimMode = 18;
    assert(callbacks == 2 && path == beforePath && staticPath == beforeStatic);
    std::cout << "Manual pickup preserves player movement/animation, emits exact 17-byte native requests, and delegates authoritative checks/results.\n";
}

void RetargetAndCost() {
    using namespace Mock;
    using namespace D3Whirlwind::NativeData;
    Reset();
    const auto beforeSkill = skill;
    assert(Retarget(&owner, skill.data(), 108, 209));
    assert(clears == 1 && reserves == 1 && reservedX == 107 && reservedY == 208 && serverComputes == 1);
    assert(Get<int>(skill.data(), 0x24) == 107 && Get<int>(skill.data(), 0x28) == 208);
    assert(Get<uint32_t>(path.data(), 0x64) == 0x401 && Get<uint64_t>(path.data(), 0x388) == 0xabcdef0123456789);
    auto changed = skill; Put(changed.data(), 0x24, int(110)); Put(changed.data(), 0x28, int(210));
    assert(changed == beforeSkill); // Damage cadence and prior target survive steering.

    for (int failure : {0, 1, 2}) {
        Reset(); const auto oldPath = path; const auto oldSkill = skill;
        if (failure == 0) computeSuccess = false;
        if (failure == 1) computedCount = 0;
        if (failure == 2) computedCount = 79;
        assert(!Retarget(&owner, skill.data(), 108, 209));
        assert(path == oldPath && skill == oldSkill && clears == 1 && reserves == 1 && reservedX == 110 && reservedY == 210);
    }
    Reset(); computedCount = 78;
    assert(Retarget(&owner, skill.data(), 108, 209));
    assert(Get<int>(skill.data(), 0x24) == 107 && Get<int>(skill.data(), 0x28) == 208);
    Reset();
    assert(!Retarget(&owner, skill.data(), 100, 200));
    assert(!Retarget(&owner, skill.data(), 0, 200));
    assert(!Retarget(&owner, skill.data(), 201, 200));
    Put(path.data(), 0x48, uint32_t(0x40000)); assert(!Retarget(&owner, skill.data(), 108, 209));
    assert(!clears && !reserves && !serverComputes);
    Reset(); assert(Retarget(&owner, skill.data(), 108, 209, false)); assert(clientComputes == 1 && !serverComputes);
    Reset(); assert(Retarget(&owner, skill.data(), 108, 209, true, false)); assert(!clears && !reserves);
    Finish(skill.data()); assert(Get<uint32_t>(skill.data(), 0x14) == 3);
    Put(skill.data(), 0x14, uint32_t(0)); Finish(skill.data()); assert(!Get<uint32_t>(skill.data(), 0x14));

    Reset(); const auto stationaryPath = path; const auto stationarySkill = skill;
    assert(HoldPosition(&owner, skill.data()));
    assert(clears == 1 && reserves == 1 && reservedX == 100 && reservedY == 200 && path == stationaryPath);
    auto stationaryChanged = skill;
    assert(Get<int>(skill.data(), 0x24) == 100 && Get<int>(skill.data(), 0x28) == 200);
    Put(stationaryChanged.data(), 0x24, int(110)); Put(stationaryChanged.data(), 0x28, int(210));
    assert(stationaryChanged == stationarySkill); // Hit schedule, last target, flags and path stay intact.
    assert(HoldPosition(&owner, skill.data()) && clears == 1 && reserves == 1); // No duplicate reservation.
    Put(path.data(), 0x50, int(1)); assert(!HoldPosition(&owner, skill.data()) && clears == 1);

    Reset(); assert(Charge(&owner, skill.data(), 151, 20) && chargeCalls == 1);
    chargeResult = 0; assert(!Charge(&owner, skill.data(), 151, 20) && chargeCalls == 2);
    cost = 0; assert(Charge(&owner, skill.data(), 151, 20) && chargeCalls == 2);
    Put(skill.data(), 0x44, int(44)); assert(!Charge(&owner, skill.data(), 151, 20) && chargeCalls == 2);
    Put(skill.data(), 0x44, int(-1)); cost = 10;
    Put(stats.data(), 0x1c, int(-1)); Put(stats.data(), 0xad4, uint32_t(0x40000)); life = 10;
    assert(!Charge(&owner, skill.data(), 151, 20) && chargeCalls == 2);
    life = 11; chargeResult = 1; assert(Charge(&owner, skill.data(), 151, 20) && chargeCalls == 3);
    std::cout << "Native steering preserves attack cadence, rolls back blocked/invalid paths and endpoint reservations, and retains native mana/Blood Mana costs.\n";
}

void CachedRouteReversal() {
    using namespace Mock;
    using namespace D3Whirlwind::NativeData;
    for (bool server : {true, false}) for (bool succeeds : {true, false}) {
        Reset(); modelRouteCache = true; computeSuccess = succeeds;
        Put(path.data(), 0x2b4, uint16_t(100)); Put(path.data(), 0x2b6, uint16_t(200)); // Cache origin.
        Put(path.data(), 0x2b8, uint16_t(90)); Put(path.data(), 0x2ba, uint16_t(200)); // Old westbound route.
        Put(path.data(), 0x2bc, uint16_t(89)); Put(path.data(), 0x2be, uint16_t(200));
        Put(path.data(), 0x380, uint8_t(2)); Put(path.data(), 0x381, uint8_t(1));
        const auto oldPath = path; const auto oldSkill = skill;
        assert(Retarget(&owner, skill.data(), 108, 209, server) == succeeds);
        assert(cachedRouteUses == 0); // Invalidated before the native builder consumes it.
        assert(server ? serverComputes == 1 && !clientComputes : clientComputes == 1 && !serverComputes);
        if (succeeds) {
            assert(Get<int>(path.data(), 0x34) == 1);
            assert(Get<uint16_t>(path.data(), 0xc8) == 108 && Get<uint16_t>(path.data(), 0xca) == 209);
            assert(Get<int>(skill.data(), 0x24) == 108 && Get<int>(skill.data(), 0x28) == 209);
            auto changed = skill; Put(changed.data(), 0x24, int(110)); Put(changed.data(), 0x28, int(210));
            assert(changed == oldSkill && Get<uint64_t>(path.data(), 0x388) == Get<uint64_t>(oldPath.data(), 0x388));
        } else {
            assert(path == oldPath && skill == oldSkill); // Including cached points, count and valid flag.
            assert(clears == 1 && reserves == 1 && reservedX == 110 && reservedY == 210);
        }
    }
    std::cout << "Client/server reversals discard cached old waypoints before native compute; blocked turns restore cache, timing and collision reservations exactly.\n";
}

int main() { ProtocolAndLease(); PickupRequests(); RetargetAndCost(); CachedRouteReversal(); }
