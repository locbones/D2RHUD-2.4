#pragma once

#include "../Pattern.h"
#include "../d2/Reclass.h"
#include <array>
#include <cstdint>
#include <cstring>

namespace D3Whirlwind::Pickup {
    using Unit = D2UnitStrc;
    using Game = D2GameStrc;
    using Dispatcher = int(__fastcall*)(Game*, Unit*, const void*, int);
    using Lookup = Unit*(__fastcall*)(void*, int, uint32_t, int);
    using Distance = uint32_t(__fastcall*)(Unit*, Unit*);
    using Sender = void(__fastcall*)(const void*, int);

    inline constexpr uint32_t DispatcherRva = 0x2c1ce0;
    inline constexpr uint32_t HandlerRva = 0x2aed60;
    inline constexpr uint32_t LookupRva = 0x66510;
    inline constexpr uint32_t DistanceRva = 0x243d40;
    inline constexpr uint32_t SenderRva = 0x10e710;
    inline constexpr uint32_t ClientUnitsRva = 0x1d442e0;
    inline constexpr uint32_t NativePickupRva = 0x2d59f0;
    inline constexpr uint32_t ClientInteractionRva = 0xd3e30;
    inline constexpr uint32_t SelectedUnitRva = 0xdedb0;
    inline constexpr uint32_t LocalPlayerIdsRva = 0x1bf1e80;
    // AutoGoldManager::UpdateAutoGold owns this target only around its native
    // interaction call (102704..10272f). It is cleared immediately afterward.
    inline constexpr uint32_t AutoGoldTargetRva = 0x1dfe2b0;
    struct Signature { uint32_t rva; uint64_t hash; };
    // FNV-1a of each function's first 32 bytes, D2R 3.1.1.0 (91923),
    // SHA256 2c2ddb60a40ede3aaff51afb64e932621a03e32487687b6c64d88205fa652360.
    inline constexpr std::array<Signature, 11> Signatures{{
        {DispatcherRva, 0xa9a1f728346d8dd6ULL},
        {HandlerRva, 0x4dbd4a8bab82773fULL},
        {LookupRva, 0x269d02b3e6718045ULL},
        {DistanceRva, 0xef40fd2ea9eed4c6ULL},
        {SenderRva, 0x61fe78ba3f4ff47fULL},
        {NativePickupRva, 0xee2cc8a233bedd26ULL},
        {ClientInteractionRva, 0xda9c2c892fa64f8dULL},
        {SelectedUnitRva, 0x55c0a572dc7731cfULL},
        {0x1023c0, 0x9edf851e594529e2ULL}, // Native auto-gold update/eligibility.
        {0x1020b0, 0xdd2464749233a51fULL}, // Native gold-only selection.
        {0xb2f40, 0xe50cec77d339f8e8ULL}, // Owner of the auto-gold manager.
    }};

#pragma pack(push, 1)
    struct Command {
        uint8_t header = 0x16;
        uint32_t item = 0;
        int32_t x = 0, y = 0;
        uint32_t autoPlace = 1;
    };
#pragma pack(pop)
    static_assert(sizeof(Command) == 17 && offsetof(Command, item) == 1
        && offsetof(Command, x) == 5 && offsetof(Command, y) == 9
        && offsetof(Command, autoPlace) == 13);

    template<class T> inline T Native(uint32_t rva) {
        return reinterpret_cast<T>(Pattern::Address(rva));
    }
    template<class T> inline bool Read(const void* pointer, T& value) {
        SIZE_T count = 0;
        return pointer && ReadProcessMemory(GetCurrentProcess(), pointer, &value, sizeof(value), &count)
            && count == sizeof(value);
    }

    inline bool GoldItem(Unit* item) {
        std::array<uint32_t, 2> header{};
        uint8_t* items=nullptr;uint64_t count=0;int16_t type=-1;
        return Read(item,header) && header[0]==4
            && Read(Native<void*>(0x1ca0390),items) && items
            && Read(Native<void*>(0x1ca0398),count) && header[1]<count
            && Read(items+size_t(header[1])*0x1b4+0x126,type) && type==4;
    }

    // d3e30 actions 3/4 are mouse right-down/held. Actions 0/1 are left,
    // 5 is right-up, and 7 is native auto-gold: none of those are blocked.
    // Match d3e30's player-index lookup and use its exact selection accessor,
    // so HD labels and legacy hover resolve the same actual ground unit.
    inline bool RightClickGroundItem(Unit* player, int action) {
        if (action != 3 && action != 4) return false;
        std::array<uint32_t, 3> header{};
        std::array<uint32_t, 8> ids{};
        if (!Read(player, header) || header[0] != 0 || !Read(Native<void*>(LocalPlayerIdsRva), ids)) return false;
        const auto selected = Native<Unit*(__fastcall*)(uint32_t)>(SelectedUnitRva);
        if (!selected) return false;
        for (uint32_t index = 0; index < ids.size(); ++index) if (ids[index] == header[2]) {
            std::array<uint32_t, 4> item{};
            return Read(selected(index), item) && item[0] == 4 && item[3] == 3;
        }
        return false;
    }

    // Called for a real manual click or an eligible native auto-gold request
    // while the feature owns the channel.
    // Bypass the ordinary client interaction helper: that helper sends a walk
    // command before its pickup packet and can replace Whirlwind's movement.
    // The server remains authoritative for ownership, inventory and collision.
    inline bool ClientRequest(Unit* player, uint32_t itemId, bool autoPlace = true) {
        if (!player || player->dwUnitType != 0 || player->dwAnimMode == 0 || player->dwAnimMode == 17) return false;
        const auto lookup = Native<Lookup>(LookupRva);
        const auto distance = Native<Distance>(DistanceRva);
        const auto send = Native<Sender>(SenderRva);
        auto table = Native<void*>(ClientUnitsRva + 4 * 0x400);
        if (!lookup || !distance || !send || !table) return false;
        auto item = lookup(table, int(itemId & 0x7f), itemId, 4);
        if (!item || item->dwUnitType != 4 || item->dwUnitId != itemId || item->dwAnimMode != 3
            || !(item->dwFlags & 2) || !item->pStaticPath || distance(player, item) > 4) return false;
        std::array<int32_t, 2> coordinates{};
        if (!Read(reinterpret_cast<const uint8_t*>(item->pStaticPath) + 0x10, coordinates)) return false;
        const Command command{0x16, itemId, coordinates[0], coordinates[1], autoPlace ? 1u : 0u};
        send(&command, sizeof(command));
        return true;
    }

    // The caller must require an enabled, authenticated, active Whirlwind
    // channel before using this bypass. The standard dispatcher rejects the
    // STATE_SKILL_MOVE bit (54), which Whirlwind deliberately keeps set.
    // Calling only the original 0x16 callback preserves its full native range,
    // room-neighbor, coordinate, collision and inventory validation. No player
    // state, animation, path or native pickup restriction is cleared here.
    inline int ServerRequest(Game* game, Unit* player, const void* bytes, int size) {
        Command command;
        if (!game || !player || player->dwUnitType != 0 || size != sizeof(Command)
            || !Read(bytes, command) || command.header != 0x16) return 3;
        if (player->dwAnimMode == 0 || player->dwAnimMode == 17 || (player->dwFlags & 0x10000)) return 0;
        const auto callback = Native<Dispatcher>(HandlerRva);
        return callback ? callback(game, player, bytes, size) : 3;
    }
}
