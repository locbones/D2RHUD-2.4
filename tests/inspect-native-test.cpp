#define NOMINMAX
#include <Windows.h>
static size_t nativeReads = 0;
BOOL WINAPI CountedRead(HANDLE process, LPCVOID address, LPVOID output, SIZE_T size, SIZE_T* read) {
    ++nativeReads;
    return ReadProcessMemory(process, address, output, size, read);
}
#define ReadProcessMemory CountedRead
#include "../d2rhud/plugin/InspectNative.cpp"
#undef ReadProcessMemory
#include <cassert>
#include <iostream>
using namespace Inspect;
namespace {
D2GameStrc game{};
D2UnitStrc local{}, guest{}, valk{}, enemy{}, sword{}, charm{}, stash{}, belt{}, valkSword{};
D2PlayerDataStrc localData{}, guestData{};
D2MonsterDataStrc valkData{};
D2MonStatsTxt valkRow{};
D2ClientStrc localClient{}, guestClient{};
D2InventoryStrc inventory{}, valkInventory{};
D2ItemDataStrc swordData{}, charmData{}, stashData{}, beltData{}, valkSwordData{};
D2DynamicPathStrc localPath{}, guestPath{}, valkPath{}, enemyPath{};
D2ActiveRoomStrc room{};
D2DrlgRoomStrc drlgRoom{};
D2DrlgLevelStrc level{};
D2StaticPathStrc charmPath{};
D2DataTablesStrc tables{};
D2ItemsTxt rows[2]{};
uint32_t selector = 0;
int petTypes = 8;
bool serverThread = false;
int itemReads = 0;
D2MouseHoverStruct hover{};
std::array<uint8_t, 0x100> statList{}, petNode{};
std::array<uint8_t, 8 * 0x10> petEntries{};
void* petEntriesPointer = petEntries.data();
struct Entry {
    uint32_t key;
    int32_t value;
};
std::array<Entry, 2> stats{{{0, 25}, {80u << 16, 30}}};
struct Sent {
    uint32_t client;
    Data::Packet packet;
};
std::vector<Sent> sent;
std::vector<Data::CommandPacket> commands;
template <class T> void Put(void* base, size_t at, T value) {
    memcpy(static_cast<uint8_t*>(base) + at, &value, sizeof(value));
}
D2UnitStrc* __fastcall GetLocal(uint32_t) { return &local; }
D2UnitStrc* __fastcall GetUnit(D2GameStrc* g, int type, uint32_t id) {
    assert(g == &game);
    for (auto u : {&local, &guest, &valk, &enemy})
        if (int(u->dwUnitType) == type && u->dwUnitId == id)
            return u;
    return nullptr;
}
int __fastcall GetStat(D2UnitStrc*, uint32_t stat, uint16_t) {
    assert(serverThread);
    return stat == STAT_QUANTITY ? 0 : 0;
}
const char* __fastcall GetName(uint64_t, char* out) {
    assert(serverThread);
    strcpy_s(out, 32768, "Valkyrie");
    return out;
}
void ItemName(D2UnitStrc* item, char* out) {
    assert(serverThread);
    ++itemReads;
    strcpy_s(out, 32768, item == &charm ? "Lucky Charm" : "The Grandfather");
}
std::string ItemDescription(D2UnitStrc* viewer, D2UnitStrc* owner, D2UnitStrc* item) {
    assert(serverThread && viewer == &local && owner == &guest && (item == &sword || item == &charm));
    return "ÿc3+20 to Strength\nÿc4The Grandfather";
}
void __fastcall ToClient(void*, uint32_t client, void** range) {
    assert(serverThread);
    assert(static_cast<uint8_t*>(range[1]) - static_cast<uint8_t*>(range[0]) == sizeof(Data::Packet));
    sent.push_back({client, *static_cast<Data::Packet*>(range[0])});
}
void __fastcall ToServer(void* bytes, int size) {
    assert(size == sizeof(Data::CommandPacket));
    commands.push_back(*static_cast<Data::CommandPacket*>(bytes));
}
void ServerTick() {
    serverThread = true;
    Tick(&game);
    serverThread = false;
}
void Deliver() {
    for (const auto& message : sent)
        if (message.client == 0)
            Receive(reinterpret_cast<const uint8_t*>(&message.packet), sizeof(message.packet));
    sent.clear();
}
void Connect(D2UnitStrc& unit, D2PlayerDataStrc& data, D2ClientStrc& client, D2DynamicPathStrc& path,
             uint32_t id, const char* name) {
    unit.dwUnitId = id;
    unit.dwAnimMode = 1;
    unit.pPlayerData = &data;
    unit.pDynamicPath = &path;
    strcpy_s(data.szName, name);
    data.pClient = &client;
    client.dwClientId = id;
    client.dwClientState = 4;
    client.dwUnitGUID = id;
    client.pPlayer = &unit;
    client.pGame = &game;
    path.pRoom = &room;
    path.wPosX = 10;
    path.wPosY = 10;
}
} // namespace
DWORD64 Pattern::Address(uint32_t rva) {
    switch (rva) {
    case 0x1d442d8:
        return reinterpret_cast<DWORD64>(&selector);
    case 0x6b050:
        return reinterpret_cast<DWORD64>(GetLocal);
    case 0x2c49c0:
        return reinterpret_cast<DWORD64>(GetUnit);
    case 0x1e5620:
        return reinterpret_cast<DWORD64>(GetStat);
    case 0x27a920:
        return reinterpret_cast<DWORD64>(GetName);
    case 0x422ac0:
        return reinterpret_cast<DWORD64>(ToClient);
    case 0x10dce0:
        return reinterpret_cast<DWORD64>(ToServer);
    case 0x1c9e980:
        return reinterpret_cast<DWORD64>(&tables);
    case 0x1c9fbd0:
        return reinterpret_cast<DWORD64>(&petTypes);
    default:
        return 0;
    }
}
bool Inspect::Native::InTown() { return CubeMap::Data::Town(Area(&local)); }
bool Inspect::Native::CanInspect(uint32_t type, uint32_t id) { return InTown() && type == 0 && id == 1; }
int main() {
    room.pDrlgRoom = &drlgRoom;
    drlgRoom.pLevel = &level;
    level.nLevelId = 1;
    game.dwInitSeed = 1234;
    game.pClientList = &localClient;
    game.nClients = 2;
    Connect(local, localData, localClient, localPath, 0, "Host");
    Connect(guest, guestData, guestClient, guestPath, 1, "RemotePvP");
    localClient.pNext = &guestClient;
    tables.pItemsTxt = rows;
    tables.nItemsTxtRecordCount = 2;
    memcpy(rows[0].szCode, "7gd ", 4);
    memcpy(rows[1].szCode, "cm1 ", 4);
    Put(&rows[0], 0x116, uint8_t(2));
    Put(&rows[0], 0x117, uint8_t(4));
    Put(&rows[1], 0x116, uint8_t(1));
    Put(&rows[1], 0x117, uint8_t(1));
    inventory.pOwner = &guest;
    inventory.pFirstItem = &sword;
    inventory.pLastItem = &belt;
    guest.pInventory = &inventory;
    auto item = [&](D2UnitStrc& unit, D2ItemDataStrc& data, unsigned id, unsigned mode, D2UnitStrc* next) {
        unit.dwUnitType = 4;
        unit.dwUnitId = id;
        unit.dwAnimMode = mode;
        unit.pItemData = &data;
        data.dwItemFlags = 0x10;
        data.dwQualityNo = 7;
        data.dwItemLevel = 115;
        data.nInvGfxIdx = 0;
        data.pExtraData.pParentInv = &inventory;
        data.pExtraData.pNextItem = next;
    };
    item(sword, swordData, 20, IMODE_EQUIP, &charm);
    swordData.nBodyLoc = 4;
    item(charm, charmData, 21, IMODE_STORED, &stash);
    charm.dwClassId = 1;
    charmData.nPage = INVPAGE_INVENTORY;
    charmData.dwQualityNo = 4;
    charm.pStaticPath = &charmPath;
    charmPath.tGameCoord = {3, 2};
    Put(statList.data(), 0x1c, uint32_t(0x80000000));
    Put(statList.data(), 0x80, stats.data());
    Put(statList.data(), 0x88, uint64_t(stats.size()));
    charm.pStatListEx = reinterpret_cast<D2StatListExStrc*>(statList.data());
    item(stash, stashData, 22, IMODE_STORED, &belt);
    stashData.nPage = INVPAGE_STASH;
    item(belt, beltData, 23, IMODE_INBELT, nullptr);
    SetNameProvider(ItemName);
    SetAvailable(true);
    gMouseHover = &hover;
    hover.IsHovered = 1;
    hover.HoveredUnitType = UNIT_PLAYER;
    hover.HoveredUnitId = 1;
    HudFrame::Context frame;
    frame.inGame = true;
    ClientTick(frame, 1);
    Open(0, 1);
    ClientTick(frame, 1);
    assert(commands.empty());
    Close();
    ServerTick();
    Deliver();
    Open(0, 1);
    ClientTick(frame, 1);
    assert(commands.size() == 1);
    auto command = commands.back();
    assert(Command(&game, &local, reinterpret_cast<const uint8_t*>(&command), sizeof(command)));
    assert(itemReads == 0); // callback queues IDs only
    const auto before = inventory;
    ServerTick();
    Deliver();
    const auto view = Current();
    assert(view.snapshot && view.snapshot->name == "RemotePvP" && view.snapshot->items.size() == 2 &&
           itemReads == 2);
    assert(view.snapshot->items[0].code == "7gd" && view.snapshot->items[1].code == "cm1");
    assert(view.snapshot->items[1].x == 3 && view.snapshot->items[1].y == 2 &&
           view.snapshot->items[1].stats.size() == 2);
    assert(memcmp(&before, &inventory, sizeof(before)) == 0);
    // Only the verified client prefix must be readable, not Reclass's unused
    // historical tail. Place it immediately before an inaccessible page.
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    auto memory = static_cast<uint8_t*>(
        VirtualAlloc(nullptr, info.dwPageSize * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    assert(memory);
    DWORD old = 0;
    assert(VirtualProtect(memory + info.dwPageSize, info.dwPageSize, PAGE_NOACCESS, &old));
    constexpr auto prefix = offsetof(D2ClientStrc, pNext) + sizeof(D2ClientStrc*);
    auto shortClient = reinterpret_cast<D2ClientStrc*>(memory + info.dwPageSize - prefix);
    memcpy(shortClient, &localClient, prefix);
    D2ClientStrc copy{};
    assert(ReadClient(shortClient, copy) && copy.pGame == &game && copy.dwUnitGUID == 0);
    VirtualFree(memory, 0, MEM_RELEASE);
    SetTextProvider(ItemDescription);
    std::string textError;
    serverThread = true;
    const auto formatted = Capture(&game, &local, command.packet, textError);
    serverThread = false;
    assert(formatted && formatted->items.size() == 2 &&
           formatted->items[0].tooltip == "ÿc3+20 to Strength\nÿc4The Grandfather" &&
           formatted->items[0].stats.empty() && formatted->items[1].stats.empty());
    SetTextProvider(nullptr);
    valkData.pMonstatsTxt=&valkRow;valkRow.dwMonStatsFlags=MONSTATSFLAG_INVENTORY;
    valk.pMonsterData=&valkData;
    valk.dwUnitType = 1;
    valk.dwUnitId = 2;
    valk.dwAnimMode = 1;
    valk.pDynamicPath = &valkPath;
    valkPath = guestPath;
    valk.pInventory = &valkInventory;
    valkInventory.pOwner = &valk;
    valkInventory.pFirstItem = valkInventory.pLastItem = &valkSword;
    item(valkSword, valkSwordData, 24, IMODE_EQUIP, nullptr);
    valkSwordData.nBodyLoc = 4;
    valkSwordData.pExtraData.pParentInv = &valkInventory;
    Put(guestData.pad_007C, 0x98 - 0x7c, &petEntriesPointer);
    Put(petEntries.data(), 2 * 0x10, petNode.data());
    Put(petNode.data(), 4, uint32_t(2));
    auto request = command.packet;
    request.type = 1;
    request.target = 2;
    std::string error;
    serverThread = true;
    auto pet = Capture(&game, &local, request, error);
    serverThread = false;
    assert(pet && pet->name == "Valkyrie" && pet->owner == "RemotePvP" && pet->items.size() == 1 &&
           pet->items[0].name == "The Grandfather");
    valkRow.dwMonStatsFlags=0;
    serverThread=true;
    assert(!Capture(&game,&local,request,error) && error.find("does not support equipment")!=std::string::npos);
    serverThread=false;valkRow.dwMonStatsFlags=MONSTATSFLAG_INVENTORY;
    enemy = valk;
    enemy.dwUnitId = 3;
    enemy.pInventory = nullptr;
    request.target = 3;
    serverThread = true;
    assert(!Capture(&game, &local, request, error));
    serverThread = false;
    request.type = 0;
    request.target = 1;
    guestPath.wPosX = 500;
    serverThread = true;
    assert(!Capture(&game, &local, request, error));
    serverThread = false;
    guestPath.wPosX = 10;
    swordData.pExtraData.pNextItem = &sword;
    serverThread = true;
    assert(!Capture(&game, &local, request, error));
    serverThread = false;
    swordData.pExtraData.pNextItem = &charm;
    for (int town : {1, 40, 75, 103, 109}) {
        level.nLevelId = town;
        assert(Native::InTown());
    }
    CubeMap::Data::additionalTown.store(137);
    level.nLevelId = 137;
    assert(Native::InTown());
    level.nLevelId = 108;
    serverThread = true;
    assert(!Capture(&game, &local, request, error));
    serverThread = false;
    assert(!Native::InTown() && !Open(0, 1));
    ClientTick(frame, 1);
    assert(!Current().open);
    level.nLevelId = 1;
    const auto reads = itemReads;
    command.packet.epoch++;
    Command(&game, &local, reinterpret_cast<const uint8_t*>(&command), sizeof(command));
    ServerTick();
    assert(itemReads == reads);
    const auto idleReads = nativeReads;
    for (int n = 0; n < 10000; ++n)
        ServerTick();
    assert(nativeReads == idleReads); // Idle Inspect does not poll units or inventory each frame.
    Destroyed(&game);
    assert(State().games.empty());
    selector = 8;
    assert(!Native::LocalId());
    std::cout << "PASS: real client/host request and fragmented response path with separate units; player "
                 "GUID zero; authoritative equipment and backpack positions/stat values; stash/belt "
                 "exclusion; no inventory writes; deferred native reads on server tick; monequip-generated "
                 "companion equipment; unrelated monsters, distance, cycles and stale epochs rejected; game "
                 "cleanup. Private native fixture memory only.\n";
}
