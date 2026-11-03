#include "InspectNative.h"
#include "InspectEquipment.h"
#include "../Pattern.h"
#include "../ProcessLifetime.h"
#include "../d2/D2Ptrs.h"
#include "CubeMapData.h"
#include "ItemFilter/ItemText.h"
#include <Windows.h>
#include <atomic>
#include <map>
#include <mutex>
#include <set>

namespace Inspect {
namespace {
using Unit = D2UnitStrc;
using Game = D2GameStrc;
using Client = D2ClientStrc;
template <class T> T Fn(uint32_t rva) { return reinterpret_cast<T>(Pattern::Address(rva)); }
template <class T> bool Read(const T* p, T& v) {
    SIZE_T n = 0;
    return p && ReadProcessMemory(GetCurrentProcess(), p, &v, sizeof(v), &n) && n == sizeof(v);
}
template <class T> T Field(const void* p, size_t at = 0) {
    T v{};
    if (p)
        Read(reinterpret_cast<const T*>(reinterpret_cast<uintptr_t>(p) + at), v);
    return v;
}
std::atomic<bool> available{false};
using Name = void (*)(Unit*, char*);
std::atomic<Name> itemName{nullptr};
std::atomic<TextProvider> itemText{nullptr};
struct Pending {
    Data::Packet request;
    uint64_t at = 0;
    bool capture = false;
    std::vector<uint8_t> bytes;
    size_t offset = 0;
};
struct Server {
    uint32_t epoch = 0;
    uint64_t hello = 0;
    std::map<uint32_t, Pending> clients;
};
struct Servers {
    std::mutex mutex;
    std::map<Game*, Server> games;
    std::atomic<uint32_t> serial{uint32_t(GetTickCount64())};
};
Servers& State() { return HudLifetime::State<Servers>(); }
Unit* Lookup(Game* game, uint32_t type, uint32_t id) {
    return Fn<Unit*(__fastcall*)(Game*, int, uint32_t)>(0x2c49c0)(game, int(type), id);
}
bool ReadClient(Client* pointer, Client& value) {
    // This engine allocates a shorter client than Reclass's historical tail.
    // Do not make a valid client depend on unrelated pages beyond pNext.
    constexpr size_t prefix = offsetof(Client, pNext) + sizeof(Client*);
    SIZE_T count = 0;
    return pointer && ReadProcessMemory(GetCurrentProcess(), pointer, &value, prefix, &count) &&
           count == prefix;
}
Client* Owner(Game* game, Unit* player) {
    Unit u{};
    if (!Read(player, u) || u.dwUnitType || !u.pPlayerData)
        return nullptr;
    const auto client = Field<Client*>(u.pPlayerData, 0xf0);
    Client c{};
    return ReadClient(client, c) && c.pGame == game && c.dwUnitGUID == u.dwUnitId && c.pPlayer == player &&
                   c.dwClientState == 4
               ? client
               : nullptr;
}
std::vector<Unit*> Players(Game* game) {
    std::vector<Unit*> result;
    std::set<Client*> seen;
    for (auto client = Field<Client*>(game, 0x150); client && seen.size() < 128;) {
        Client c{};
        if (!seen.insert(client).second || !ReadClient(client, c))
            break;
        if (c.pPlayer && Owner(game, c.pPlayer) == client)
            result.push_back(c.pPlayer);
        client = c.pNext;
    }
    return result;
}
int Area(Unit* unit) {
    const auto path = Field<D2DynamicPathStrc*>(unit, 0x38);
    const auto room = Field<D2ActiveRoomStrc*>(path, 0x20);
    const auto drlg = Field<D2DrlgRoomStrc*>(room, 0x18);
    const auto level = Field<D2DrlgLevelStrc*>(drlg, offsetof(D2DrlgRoomStrc, pLevel));
    return level ? int(Field<uint32_t>(&level->nLevelId)) : 0;
}
bool Nearby(Unit* a, Unit* b) {
    // Same loaded area and bounded range; no stash, unloaded unit or world scan.
    const auto area = Area(a);
    if (!area || area != Area(b))
        return false;
    D2DynamicPathStrc x{}, y{};
    if (!Read(a->pDynamicPath, x) || !Read(b->pDynamicPath, y))
        return false;
    const int dx = int(x.wPosX) - int(y.wPosX), dy = int(x.wPosY) - int(y.wPosY);
    return int64_t(dx) * dx + int64_t(dy) * dy <= 128 * 128;
}
bool Registered(Unit* owner, uint32_t id) {
    const int types = Field<int>(Fn<void*>(0x1c9fbd0));
    if (!owner || !owner->pPlayerData || types < 1 || types > 1024)
        return false;
    const auto lists = Field<void*>(owner->pPlayerData, 0x98);
    const auto entries = Field<uint8_t*>(lists);
    if (!entries)
        return false;
    size_t visited = 0;
    for (int type = 1; type < types; ++type) {
        std::set<void*> seen;
        auto node = Field<void*>(entries, size_t(type) * 0x10);
        while (node && visited++ < 4096 && seen.size() < 256) {
            if (!seen.insert(node).second)
                break;
            if (Field<uint32_t>(node, 4) == id && !(Field<uint32_t>(node) & 1))
                return true;
            node = Field<void*>(node, 0x18);
        }
    }
    return false;
}
std::string Clean(std::string text) {
    std::string out;
    for (size_t i = 0; i < text.size();) {
        if (i + 3 < text.size() && uint8_t(text[i]) == 0xc3 && uint8_t(text[i + 1]) == 0xbf &&
            text[i + 2] == 'c') {
            i += 4;
            continue;
        }
        if (i + 2 < text.size() && uint8_t(text[i]) == 0xff && text[i + 1] == 'c') {
            i += 3;
            continue;
        }
        out += uint8_t(text[i]) < 32 || uint8_t(text[i]) == 127 ? ' ' : text[i];
        ++i;
    }
    if (!Data::Text(out, 32768))
        return {};
    char bounded[513]{};
    ItemText::Copy(bounded, sizeof(bounded), out);
    return bounded;
}
std::string UnitName(Unit* unit) {
    if (!unit->dwUnitType && unit->pPlayerData) {
        D2PlayerDataStrc player{};
        if (Read(unit->pPlayerData, player))
            return Clean(std::string(player.szName, strnlen_s(player.szName, 16)));
    }
    char buffer[32768]{};
    const auto result =
        Fn<const char*(__fastcall*)(uint64_t, char*)>(0x27a920)(reinterpret_cast<uint64_t>(unit), buffer);
    if (result && result != buffer) {
        std::string name;
        for (size_t n = 0; n < 512; ++n) {
            char c = 0;
            if (!Read(result + n, c) || !c)
                break;
            name += c;
        }
        if (auto text = Clean(name); !text.empty())
            return text;
    }
    return Clean(std::string(buffer, strnlen_s(buffer, sizeof(buffer))));
}
bool Item(Unit* pointer, Data::Item& item) {
    Unit u{};
    D2ItemDataStrc d{};
    if (!Read(pointer, u) || u.dwUnitType != 4 || !Read(u.pItemData, d))
        return false;
    const auto tables = Fn<D2DataTablesStrc*>(0x1c9e980);
    const auto rows = Field<D2ItemsTxt*>(&tables->pItemsTxt);
    const auto count = Field<int64_t>(&tables->nItemsTxtRecordCount);
    D2ItemsTxt row{};
    if (!rows || count <= 0 || count > 1000000 || u.dwClassId >= uint64_t(count) ||
        !Read(rows + u.dwClassId, row))
        return false;
    item.id = u.dwUnitId;
    item.flags = d.dwItemFlags;
    item.index = d.dwFileIndex;
    item.level = uint16_t((std::min)(d.dwItemLevel, 65535u));
    item.quality = uint8_t(d.dwQualityNo);
    item.picture = d.nInvGfxIdx;
    item.slot = u.dwAnimMode == IMODE_EQUIP ? d.nBodyLoc : 0;
    item.code.assign(row.szCode, strnlen_s(row.szCode, 4));
    while (!item.code.empty() && item.code.back() == ' ')
        item.code.pop_back();
    item.width = Field<uint8_t>(&row, 0x116);
    item.height = Field<uint8_t>(&row, 0x117);
    if (!item.slot) {
        D2StaticPathStrc path{};
        if (!Read(u.pStaticPath, path) || path.tGameCoord.X > 31 || path.tGameCoord.Y > 31)
            return false;
        item.x = uint8_t(path.tGameCoord.X);
        item.y = uint8_t(path.tGameCoord.Y);
    }
    char name[32768]{};
    if (auto provider = itemName.load())
        provider(pointer, name);
    item.name = Clean(std::string(name, strnlen_s(name, sizeof(name))));
    if (item.name.empty())
        item.name = item.code;
    const auto quantity =
        Fn<int(__fastcall*)(Unit*, uint32_t, uint16_t)>(0x1e5620)(pointer, STAT_QUANTITY, 0);
    item.quantity = (std::max)(1, quantity);
    if (u.pStatListEx) {
        const auto flags = Field<uint32_t>(u.pStatListEx, 0x1c);
        const size_t at = flags & 0x80000000u ? 0x80 : 0x30;
        struct Entry {
            uint32_t key;
            int32_t value;
        };
        const auto entries = Field<Entry*>(u.pStatListEx, at);
        const auto n = Field<uint64_t>(u.pStatListEx, at + 8);
        if (n > 4096 || (n && !entries))
            return false;
        item.statsTruncated = n > Data::MaxStats;
        for (size_t j = 0; j < (std::min)(size_t(n), Data::MaxStats); ++j) {
            Entry e{};
            if (!Read(entries + j, e) || (e.key >> 16) >= 4096)
                return false;
            item.stats.push_back({uint16_t(e.key >> 16), uint16_t(e.key), e.value});
        }
    }
    return true;
}
std::optional<Data::Snapshot> Capture(Game* game, Unit* requester, const Data::Packet& request,
                                      std::string& error) {
    if (!CubeMap::Data::Town(Area(requester))) {
        error = "Inspection is only available in town.";
        return {};
    }
    auto target = Lookup(game, request.type, request.target);
    Unit u{};
    if (!Read(target, u) || u.dwUnitType != request.type || u.dwUnitId != request.target ||
        !Nearby(requester, target) || (u.dwFlags & 0x10000) || !u.dwAnimMode ||
        u.dwAnimMode == (u.dwUnitType ? 12u : 17u)) {
        error = "The unit is no longer nearby. Click its eye icon in town to inspect again.";
        return {};
    }
    if (!Equipment::Capable(u, [](auto* pointer, auto& value) { return Read(pointer, value); })) {
        error = "That unit does not support equipment inventory.";
        return {};
    }
    Unit* owner = nullptr;
    if (!u.dwUnitType) {
        if (!Owner(game, target)) {
            error = "That player is no longer connected.";
            return {};
        }
        owner = target;
    } else
        for (auto player : Players(game))
            if (Registered(player, u.dwUnitId)) {
                owner = player;
                break;
            }
    if (!owner) {
        error = "Inspect supports players and their allied summons or mercenaries.";
        return {};
    }
    Data::Snapshot s;
    s.type = u.dwUnitType;
    s.id = u.dwUnitId;
    s.name = UnitName(target);
    s.owner = UnitName(owner);
    s.designation = u.dwUnitType ? "Companion" : "Player";
    if (s.name.empty())
        s.name = s.designation + " " + std::to_string(s.id);
    // Actual server inventory includes rolled monequip gear. Never synthesize
    // items from the monster's nominal row, or trust client appearance packets.
    if (!u.pInventory)
        return s;
    D2InventoryStrc inventory{};
    if (!Read(u.pInventory, inventory) || inventory.pOwner != target) {
        error = "The unit's inventory is unavailable.";
        return {};
    }
    std::set<Unit*> seen;
    Unit* tail = nullptr;
    for (auto pointer = inventory.pFirstItem; pointer;) {
        Unit item{};
        D2ItemDataStrc d{};
        if (seen.size() >= 4096 || !seen.insert(pointer).second || !Read(pointer, item) ||
            item.dwUnitType != 4 || !Read(item.pItemData, d) || d.pExtraData.pParentInv != u.pInventory) {
            error = "The unit's inventory changed while reading. Refresh to retry.";
            return {};
        }
        // Carried backpack and equipped gear only. Excludes personal/shared
        // stash, trade, cursor, cube contents and belt storage containers.
        if ((item.dwAnimMode == IMODE_EQUIP && d.nBodyLoc >= 1 && d.nBodyLoc <= 12) ||
            (item.dwAnimMode == IMODE_STORED && d.nPage == INVPAGE_INVENTORY)) {
            Data::Item out;
            if (s.items.size() >= Data::MaxItems || !Item(pointer, out)) {
                error = "An equipped or carried item could not be read safely.";
                return {};
            }
            if (auto text = itemText.load()) {
                out.tooltip = text(requester, target, pointer);
                char bounded[Data::MaxTooltip + 1]{};
                ItemText::Copy(bounded, sizeof(bounded), out.tooltip);
                out.tooltip = bounded;
                if (!Data::Tooltip(out.tooltip))
                    out.tooltip.clear();
                // The native text already includes these properties. Avoid
                // sending a second raw copy for every charm in the backpack.
                if (!out.tooltip.empty()) {
                    out.stats.clear();
                    out.statsTruncated = false;
                }
            }
            s.items.push_back(std::move(out));
        }
        tail = pointer;
        pointer = d.pExtraData.pNextItem;
    }
    if (tail != inventory.pLastItem) {
        error = "The unit's inventory changed while reading. Refresh to retry.";
        return {};
    }
    return s;
}
void Send(Client* client, const Data::Packet& packet) {
    void* range[]{const_cast<Data::Packet*>(&packet),
                  const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(&packet)) + sizeof(packet)};
    Fn<void(__fastcall*)(void*, uint32_t, void**)>(0x422ac0)(Fn<void*>(0x18682b0), client->dwClientId, range);
}
} // namespace
namespace Native {
std::optional<uint32_t> LocalId() {
    const auto index = Field<uint32_t>(Fn<void*>(0x1d442d8));
    if (index >= 8)
        return {};
    const auto player = Fn<Unit*(__fastcall*)(uint32_t)>(0x6b050)(index);
    Unit u{};
    if (Read(player, u) && !u.dwUnitType)
        return u.dwUnitId;
    return {};
}
void Send(const Data::CommandPacket& command) {
    Fn<void(__fastcall*)(void*, int)>(0x10dce0)(const_cast<Data::CommandPacket*>(&command), sizeof(command));
}
} // namespace Native
void SetNameProvider(void (*provider)(Unit*, char*)) { itemName.store(provider); }
void SetTextProvider(TextProvider provider) { itemText.store(provider); }
void SetAvailable(bool value) { available.store(value); }
bool Command(Game* game, Unit* player, const uint8_t* bytes, size_t size) {
    if (!bytes || size < 10 || bytes[0] != 3 || Field<uint64_t>(bytes, 1) != Data::Magic ||
        bytes[9] != Data::Operation)
        return false;
    if (!available || size != sizeof(Data::CommandPacket) || !Owner(game, player) ||
        !CubeMap::Data::Town(Area(player)))
        return true;
    Data::CommandPacket c;
    memcpy(&c, bytes, size);
    const auto& p = c.packet;
    if (p.version != Data::Version || p.action != Data::Request || p.type > 1 || !p.request ||
        p.recipient != player->dwUnitId || p.seed != uint32_t(game->dwInitSeed) || p.total || p.offset ||
        p.length || std::any_of(p.bytes.begin(), p.bytes.end(), [](uint8_t b) { return b != 0; }) ||
        std::any_of(c.padding.begin(), c.padding.end(), [](uint8_t b) { return b != 0; }))
        return true;
    auto& state = State();
    std::lock_guard lock(state.mutex);
    auto at = state.games.find(game);
    if (at == state.games.end() || p.epoch != at->second.epoch)
        return true;
    auto& pending = at->second.clients[p.recipient];
    const auto now = GetTickCount64();
    if (pending.at && now - pending.at < 750)
        return true;
    pending = {};
    pending.request = p;
    pending.at = now;
    pending.capture = true;
    return true;
}
void Tick(Game* game) {
    if (!available || !game)
        return;
    auto& state = State();
    std::lock_guard lock(state.mutex);
    auto& server = state.games[game];
    if (!server.epoch) {
        server.epoch = ++state.serial;
        if (!server.epoch)
            server.epoch = ++state.serial;
    }
    const auto now = GetTickCount64();
    const bool hello = !server.hello || now - server.hello >= 2000;
    if (!hello && std::none_of(server.clients.begin(), server.clients.end(), [](const auto& entry) {
            return entry.second.capture || entry.second.offset < entry.second.bytes.size();
        }))
        return;
    const auto players = Players(game);
    if (hello)
        server.hello = now;
    std::set<uint32_t> connected;
    for (auto player : players) {
        const auto client = Owner(game, player);
        if (!client)
            continue;
        connected.insert(player->dwUnitId);
        if (hello) {
            Data::Packet p;
            p.recipient = player->dwUnitId;
            p.seed = uint32_t(game->dwInitSeed);
            p.epoch = server.epoch;
            Send(client, p);
        }
        auto at = server.clients.find(player->dwUnitId);
        if (at == server.clients.end())
            continue;
        auto& pending = at->second;
        if (!CubeMap::Data::Town(Area(player))) {
            pending.capture = false;
            pending.bytes.clear();
            pending.offset = 0;
            continue;
        }
        if (pending.capture) {
            pending.capture = false;
            std::string error;
            try {
                if (auto snapshot = Capture(game, player, pending.request, error))
                    pending.bytes = Data::Encode(*snapshot);
            } catch (...) {
                error = "The inspection could not be completed.";
            }
            if (pending.bytes.empty()) {
                if (error.empty())
                    error = "The inspection exceeded its supported size or item limits.";
                auto reply = pending.request;
                reply.opcode = 0x2b;
                reply.action = Data::Failure;
                reply.length = uint16_t((std::min)(error.size(), Data::Payload));
                memcpy(reply.bytes.data(), error.data(), reply.length);
                Send(client, reply);
            }
        }
        // Bounded work per server frame, including full charm inventories.
        for (unsigned n = 0; n < 16 && pending.offset < pending.bytes.size(); ++n) {
            auto reply = pending.request;
            reply.opcode = 0x2b;
            reply.action = Data::Chunk;
            reply.total = uint32_t(pending.bytes.size());
            reply.offset = uint32_t(pending.offset);
            reply.length = uint16_t((std::min)(Data::Payload, pending.bytes.size() - pending.offset));
            memcpy(reply.bytes.data(), pending.bytes.data() + pending.offset, reply.length);
            Send(client, reply);
            pending.offset += reply.length;
        }
        if (!pending.bytes.empty() && pending.offset == pending.bytes.size()) {
            pending.bytes.clear();
            pending.bytes.shrink_to_fit();
            pending.offset = 0;
        }
    }
    std::erase_if(server.clients, [&](const auto& entry) { return !connected.contains(entry.first); });
}
void Destroyed(Game* game) {
    auto& s = State();
    std::lock_guard lock(s.mutex);
    s.games.erase(game);
}
} // namespace Inspect
