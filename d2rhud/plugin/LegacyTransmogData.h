#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace LegacyTransmog::Data {
constexpr size_t ItemRecordSize = 0x1b4;
struct Definition {
    bool enabled = false;
    uint32_t target = 0;
    uint8_t minimum = 0, maximum = 0;
    bool Valid() const { return enabled && target && target != 0x20202020 && (!minimum || !maximum || maximum >= minimum); }
};
inline Definition Decode(std::span<const uint8_t> record) {
    if (record.size() != ItemRecordSize) return {};
    Definition result;
    result.enabled = record[0x141] != 0;
    std::memcpy(&result.target, record.data() + 0xd0, 4);
    result.minimum = record[0x142]; result.maximum = record[0x143];
    return result;
}
// Legacy TMogMax is exclusive for a random interval; equal bounds are fixed.
inline int Quantity(const Definition& def, uint32_t& low, uint32_t& high) {
    if (!def.minimum || !def.maximum) return 0;
    const uint32_t limit = def.maximum - def.minimum;
    if (!limit) return def.minimum;
    const uint64_t seed = uint64_t(low) * 0x6ac690c5ULL + high;
    low = uint32_t(seed); high = uint32_t(seed >> 32);
    return def.minimum + low % limit;
}
struct Location {
    uint32_t mode = 0, page = 0;
    uint16_t x = 0, y = 0;
    uint32_t body = 0;
};
static_assert(sizeof(Location) == 16);
// Native creation descriptors grew beyond the older ReClass 0x98-byte view.
struct alignas(8) Drop {
    uintptr_t unit = 0, seed = 0, game = 0;
    int32_t level = 0, unknown = 0, id = 0, mode = 4, x = 0, y = 0;
    uintptr_t room = 0;
    uint16_t initFlags = 1, format = 0;
    int32_t force = 0, quality = 2, quantity = 0, minDurability = 0, maxDurability = 0, index = 2;
    uint32_t flags = 0, unitSeed = 0, itemSeed = 0;
    std::array<uint8_t, 0xc8 - 0x60> remainder{};
};
static_assert(sizeof(Drop) == 0xc8 && offsetof(Drop, id) == 0x20 && offsetof(Drop, itemSeed) == 0x5c);

enum class Result { Unchanged, Transformed, Restored, RecoveryCursor };
// Keep the old item alive until the new item has a confirmed destination.
// Every failed native add is a preflight failure (no inventory mutation).
template<class Inventory>
Result Replace(Inventory& inv, Location origin) {
    if (!inv.Create()) return Result::Unchanged;
    if (!inv.Remove(origin)) { inv.DestroyNew(); return Result::Unchanged; }
    if (inv.AddNew(origin)) { inv.DestroyOld(); return Result::Transformed; }
    for (uint16_t y = 0; y < 16; ++y) for (uint16_t x = 0; x < 16; ++x) {
        if (x == origin.x && y == origin.y) continue;
        if (inv.AddNew(Location{0, 0, x, y, 0})) { inv.DestroyOld(); return Result::Transformed; }
    }
    if (inv.DropNew()) { inv.DestroyOld(); return Result::Transformed; }
    if (inv.RestoreOld(origin)) { inv.DestroyNew(); return Result::Restored; }
    // A reentrant game/plugin change can invalidate the original slot. Never
    // destroy either item in that case; put the original on the empty cursor.
    if (inv.RestoreOld(Location{4, 255, 0, 0, 0})) { inv.DestroyNew(); return Result::RecoveryCursor; }
    return Result::RecoveryCursor; // Native adapter logs and retains both units.
}
struct Signature { uint32_t rva, size; uint64_t hash; };
// Audited D2R.exe SHA256 2c2ddb60a40ede3aa...f652360 (file version 1.2.69270).
// Full native routine ranges; no absolute image relocations occur within them.
constexpr std::array<Signature, 20> Signatures{{
    {0x1303a0, 0x100, 0x8c388c48d73ffb1aULL},
    {0x1304a0, 0x740, 0xf5617fedc6ccd7a6ULL},
    {0x2b7370, 0x1c9, 0xce03797ce5643a4eULL},
    {0x10eae0, 0x102, 0x6f5b7397103a92f2ULL},
    {0x2c49c0, 0x6b, 0x124f1c2c181c6dbaULL},
    {0x2f58c0, 0x980, 0x5d3da95b188dffa5ULL},
    {0x2f70a0, 0xd0, 0xec62a147c9ab56ecULL},
    {0x1e5440, 0x149, 0xe0541ca8ff237e82ULL},
    {0x213a20, 0xb8, 0x048d5b15eb24d79cULL},
    {0x2dba00, 0x670, 0x8f52382855723001ULL},
    {0x2d5e10, 0x6b0, 0x3aa57b1f7d9fffc6ULL},
    {0x209530, 0x6a, 0x55b656b3ab238f6eULL},
    {0x231510, 0x615, 0x9dd5604bec4ae7d5ULL},
    {0x2fa090, 0x360, 0x5a63d2882c534175ULL},
    {0x6b050, 0x30, 0xff360842fe63918eULL},
    {0x3161b0, 0x9d0, 0x96b33dc186e538f5ULL},
    {0x316b80, 0x752, 0x31b033f05b413172ULL},
    {0x1c0410, 0x78b, 0xcb8b65710cf872d2ULL},
    {0x413860, 0x95, 0x53fbd146afd31fa4ULL},
    {0x5c7e0, 0x8e, 0xb968ca437dc6a49eULL},
}};
inline uint64_t Hash(std::span<const uint8_t> bytes) {
    uint64_t hash = 14695981039346656037ULL;
    for (uint8_t byte : bytes) hash = (hash ^ byte) * 1099511628211ULL;
    return hash;
}
}
