#pragma once
#include "../Pattern.h"
#include "../d2/Reclass.h"
#include <array>
#include <cstdint>
#include <cstring>

// Audited against D2R.exe SHA-256
// 2c2ddb60a40ede3aaff51afb64e932621a03e32487687b6c64d88205fa652360.
namespace D3Whirlwind::NativeData {
inline constexpr uint32_t ServerStart = 0x3741d0, ServerDo = 0x374ca0,
    ServerMove = 0x2c5e30, ServerCleanup = 0x3749d0,
    ClientStart = 0x17bf80, ClientDo = 0x17c310, ClientCleanup = 0x17c3e0,
    ClientMove = 0xa4f40;
struct Signature { uint32_t rva; uint64_t hash; };
// BEGIN GENERATED SIGNATURES
inline constexpr std::array<Signature, 18> Signatures{{
    {0x3741d0, 0x9913d130ae0d33bfULL},
    {0x374ca0, 0x4e674b128a5c5465ULL},
    {0x2c5e30, 0x9a9f4240e106989eULL},
    {0x3749d0, 0x649cc4621014b427ULL},
    {0x17bf80, 0x1a2a18d0e549d3eeULL},
    {0x17c310, 0x527390ec10ef8b9fULL},
    {0x17c3e0, 0x75f8c4aab7976700ULL},
    {0x175ea0, 0x62f3372530195636ULL},
    {0x1f7c30, 0x82292e1afcb46302ULL},
    {0x230eb0, 0xd0c51a32927e366fULL},
    {0x230c10, 0x31a54f0272470416ULL},
    {0x209530, 0x98a8ca85970a8f50ULL},
    {0x21df90, 0xc5543936c523a524ULL},
    {0x294e90, 0x57175bda9de0bbdeULL},
    {0x1e5620, 0x1bfb159b91b26fc1ULL},
    {0xa4f40, 0x39832ca3af272be3ULL},
    {0x22bbdd, 0xfae334802be9aaa6ULL},
    {0x215dbe, 0x8ddf9a34c64fd529ULL},
}};
inline constexpr std::array<Signature, 7> InputSignatures{{
    {0xa6120, 0x681d95586e1f25bbULL},
    {0x6b050, 0x90869c3b9cbd1b2fULL},
    {0x10dce0, 0x9a494746d193b041ULL},
    {0x6e5e0, 0x3bbcef3e842b9034ULL},
    {0x86030, 0x2225ea27c1edcda3ULL},
    {0x865e0, 0xdc45f8215f71078cULL},
    {0x86860, 0x8773941eeb5f8596ULL},
}};
// END GENERATED SIGNATURES

template<class T> inline T At(uint32_t rva) { return reinterpret_cast<T>(Pattern::Address(rva)); }
template<class T> inline T Read(const void* pointer, size_t offset = 0) {
    T value{}; SIZE_T count = 0;
    if (pointer) ReadProcessMemory(GetCurrentProcess(), static_cast<const uint8_t*>(pointer) + offset, &value, sizeof(value), &count);
    return value;
}
template<class T> inline void Write(void* pointer, size_t offset, T value) {
    memcpy(static_cast<uint8_t*>(pointer) + offset, &value, sizeof(value));
}
inline bool Alive(D2UnitStrc* unit) {
    return unit && unit->dwUnitType == 0 && unit->dwAnimMode != 0 && unit->dwAnimMode != 17 && !(unit->dwFlags & 0x10000);
}
inline bool Active(void* skill) { return skill && (Read<uint32_t>(skill, 0x14) & 1) != 0; }
inline void Finish(void* skill) {
    if (Active(skill)) Write(skill, 0x14, Read<uint32_t>(skill, 0x14) | 2u);
}

inline bool HoldPosition(D2UnitStrc* unit, void* skill) {
    if (!Alive(unit) || !Active(skill)) return false;
    const auto path = Read<void*>(unit, 0x38);
    if (!path || Read<int>(path, 0x50) != 7) return false;
    const int x = Read<uint16_t>(path, 2), y = Read<uint16_t>(path, 6);
    if (!x || !y) return false;
    const int oldX = Read<int>(skill, 0x24), oldY = Read<int>(skill, 0x28);
    if (oldX == x && oldY == y) return true;
    const auto room = At<D2ActiveRoomStrc*(__fastcall*)(D2UnitStrc*)>(0x209530)(unit);
    if (!room) return false;
    using Collision = void(__fastcall*)(D2ActiveRoomStrc*, int, int, int, int);
    const int pattern = Read<int>(path, 0x5c);
    if (oldX && oldY) At<Collision>(0x230eb0)(room, oldX, oldY, pattern, 0x80);
    At<Collision>(0x230c10)(room, x, y, pattern, 0x80);
    Write(skill, 0x24, x); Write(skill, 0x28, y);
    // Leave native path memory intact. Resuming must force Retarget, while
    // native cleanup releases the reservation at the current position.
    return true;
}

// Continue the current native channel; do not invoke the start function or reset
// skill+0x2c (next hit frame), +0x30 (previous target), animation, or velocity.
// Both native client and server starts reserve endpoint collision. The final
// argument exists for callers which explicitly manage that reservation elsewhere.
inline bool Retarget(D2UnitStrc* unit, void* skill, int x, int y, bool server = true, bool collisionReservation = true) {
    if (!Alive(unit) || !Active(skill) || x <= 0 || y <= 0 || x > 65535 || y > 65535) return false;
    auto path = Read<uint8_t*>(unit, 0x38);
    if (!path || Read<int>(path, 0x50) != 7 || (Read<uint32_t>(path, 0x48) & 0x40000)) return false;
    const int hereX = Read<uint16_t>(path, 2), hereY = Read<uint16_t>(path, 6);
    if ((hereX == x && hereY == y) || x - hereX > 100 || hereX - x > 100 || y - hereY > 100 || hereY - y > 100) return false;
    const auto room = At<D2ActiveRoomStrc*(__fastcall*)(D2UnitStrc*)>(0x209530)(unit);
    if (!room) return false;
    // Native path computation writes through byte+0x381. Its client render
    // container starts at+0x388 and must retain native ownership during rollback.
    std::array<uint8_t, 0x382> previous{}; SIZE_T copied = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), path, previous.data(), previous.size(), &copied) || copied != previous.size()) return false;
    const int oldX = Read<int>(skill, 0x24), oldY = Read<int>(skill, 0x28);
    const int pattern = Read<int>(path, 0x5c);
    using Collision = void(__fastcall*)(D2ActiveRoomStrc*, int, int, int, int);
    const auto clear = At<Collision>(0x230eb0), reserve = At<Collision>(0x230c10);
    if (collisionReservation && oldX && oldY) clear(room, oldX, oldY, pattern, 0x80);
    Write(path, 0x10, uint16_t(x)); Write(path, 0x12, uint16_t(y));
    Write<void*>(path, 0x70, nullptr); Write(path, 0x78, uint32_t(6)); Write(path, 0x7c, uint32_t(-1));
    Write(path, 0x64, uint32_t(0xc01));
    // Movement saves the remaining old route at +0x2b4/+0x2b8 and sets
    // +0x381. Type-7's builder (22cec0 -> 22bb80) can prepend a waypoint
    // from that route before the new goal. Invalidate it BEFORE computing;
    // 1f7c30's normal invalidation happens after the builder has consumed it.
    // The snapshot above restores the cache too if this new path fails.
    Write(path, 0x381, uint8_t(0));
    const bool computed = server ?
        At<int(__fastcall*)(void*, D2UnitStrc*, int, int)>(0x1f7c30)(path, unit, 0, 1) != 0 :
        At<bool(__fastcall*)(D2UnitStrc*, void*)>(0x175ea0)(unit, path);
    const int count = Read<int>(path, 0x34);
    // Straight-path builder 0x270400 checks output index against 0x4e;
    // 0x27044f computes remaining capacity using the same 78-point limit.
    if (!computed || count < 1 || count > 78) {
        memcpy(path, previous.data(), previous.size());
        if (collisionReservation && oldX && oldY) reserve(room, oldX, oldY, pattern, 0x80);
        return false;
    }
    const int finalX = Read<uint16_t>(path, 0xc8 + size_t(count - 1) * 4);
    const int finalY = Read<uint16_t>(path, 0xca + size_t(count - 1) * 4);
    Write(path, 0x64, uint32_t(0x401));
    Write(skill, 0x24, finalX); Write(skill, 0x28, finalY);
    if (collisionReservation) reserve(room, finalX, finalY, pattern, 0x80);
    return true;
}

// Call at the channel policy's interval (25 game frames), in addition to the
// native first-cast cost. Charged item skills retain the native cast path until
// their charge/replenishment semantics are separately audited.
inline bool Charge(D2UnitStrc* unit, void* skill, int id, int level) {
    if (!Alive(unit) || !skill || Read<int>(skill, 0x44) != -1 || level < 1) return false;
    const int cost = At<int(__fastcall*)(int, int)>(0x21df90)(id, level);
    if (cost <= 0) return true;
    const auto lists = Read<void*>(unit, 0x88);
    const bool bloodMana = lists && Read<int>(lists, 0x1c) < 0 && (Read<uint32_t>(lists, 0xad4) & 0x40000);
    if (bloodMana && At<int(__fastcall*)(D2UnitStrc*, int, uint16_t)>(0x1e5620)(unit, 6, 0) <= cost) return false;
    // This native helper handles insufficient mana, Blood Mana, and existing
    // CubeMap mana hooks; do not directly write mana or hitpoint statistics.
    return At<int(__fastcall*)(D2UnitStrc*, int)>(0x294e70)(unit, cost) != 0;
}
}
