#pragma once
#include "../Pattern.h"
#include "../d2/Reclass.h"
#include <array>
#include <cstdint>
#include <cstring>

namespace SummonPersistence::NativeData {
inline constexpr size_t DoOffset = 0x4e;
struct Signature { uint32_t rva; uint64_t hash; };
// BEGIN GENERATED SIGNATURES
inline constexpr std::array<Signature, 26> Signatures{{
    {0x29b4c0, 0x2c15b86d143b43f0ULL},
    {0x367f60, 0xa623eb46917c4b9dULL},
    {0x368a90, 0xa6a9410b34841984ULL},
    {0x367d40, 0x59597e622128b0baULL},
    {0x367af0, 0x39b03ad9a427877cULL},
    {0x367910, 0x5d62a917850da79bULL},
    {0x3c5410, 0x321410aa2cf42e33ULL},
    {0x21e3a0, 0xe359a09ff5235d69ULL},
    {0x218ce0, 0xb2ed836e0e2755c6ULL},
    {0x21a990, 0x740491c3e2abe224ULL},
    {0x2095a0, 0x1a7daab6cce414f8ULL},
    {0x1e5620, 0x1bfb159b91b26fc1ULL},
    {0x2c49c0, 0x04d772b7be349296ULL},
    {0x209530, 0x98a8ca85970a8f50ULL},
    {0x6d08c0, 0x69f920949e3996d0ULL},
    {0x2185e0, 0x51e24a7c9aaffb3eULL},
    {0x1e2e00, 0x4d06bb9e31b434acULL},
    {0x29bf70, 0xabe7302a5be11ebcULL},
    {0x371690, 0x10676c4aef194015ULL},
    {0x368cb0, 0x984b1a80b6e9a6afULL},
    {0x359950, 0x3afe824d54285c79ULL},
    {0x369e90, 0xeee71b400213be12ULL},
    {0x3608d0, 0xd409d190536f5aaeULL},
    {0x360c20, 0x92e93b45bc7b53dcULL},
    {0x361cb0, 0x06e70218fc69dbfeULL},
    {0x382370, 0x38d010189ac67ffbULL},
}};
// END GENERATED SIGNATURES

template<class T> inline T Read(const void* pointer, size_t offset = 0) {
    T value{}; SIZE_T size = 0;
    if (pointer) ReadProcessMemory(GetCurrentProcess(), static_cast<const uint8_t*>(pointer) + offset, &value, sizeof(value), &size);
    return value;
}
template<class T> inline T At(uint32_t rva) { return reinterpret_cast<T>(Pattern::Address(rva)); }

// The summon implementation is shared by custom skill rows. Its lifetime
// implementation alone does not determine whether a custom pet is permanent.
template<class Unit, class Evaluate>
inline bool Limited(Unit*, int, int, const uint8_t* row, Evaluate, int summonedMonster = -1) {
    // An arbitrary calc is not a lifetime: a custom monster can ignore the AI
    // parameter written by the reused server-do routine entirely.
    const int monster = summonedMonster >= 0 ? summonedMonster : Read<int16_t>(row, 0xdc);
    const auto count = Read<uint64_t>(At<void*>(0x1c9f880));
    const auto table = Read<uint8_t*>(At<void*>(0x1c9f878));
    if (monster < 0 || uint64_t(monster) >= count || count > 65536 || !table) return true;
    // These native AIs terminate after a time or a finite number of attacks,
    // including Raven's native fallback of three attacks when no skill exists.
    switch (Read<int16_t>(table + size_t(monster) * 0x1dc, 0x4e)) {
    case 86: case 101: case 102: case 104: case 107: return true;
    default: return false;
    }
}

inline int FullSetLevel(D2UnitStrc* owner) {
    const auto extended = Read<void*>(owner, 0x88);
    for (size_t head : {size_t(0x68), size_t(0x70)}) {
        auto list = Read<void*>(extended, head);
        for (size_t n = 0; list && n < 1024; ++n, list = Read<void*>(list, 0x48)) {
            if (Read<int>(list, 0x20) != 97) continue;
            const auto query = At<int(__fastcall*)(void*, uint32_t, void*)>(0x1e2e00);
            return query ? query(list, 351u << 16, nullptr) : 1;
        }
    }
    return 1;
}
inline void SkeletonComponents(D2UnitStrc* owner, D2UnitStrc* pet, int skill, int level,
    const std::array<uint8_t, 16>& components) {
    if (pet->dwClassId == 363) {
        // Native physical skeleton wrapper computes shield chance/full-set level.
        At<void(__fastcall*)(D2UnitStrc*, D2UnitStrc*, int, int, int)>(0x367d40)(owner, pet, 0, skill, level);
    } else if (pet->dwClassId == 364) {
        // Special=1 initializes the mage AI in addition to its components.
        At<void(__fastcall*)(D2UnitStrc*, D2UnitStrc*, int, int, int, int)>(0x367af0)(owner, pet, 0, FullSetLevel(owner), 0, 1);
    }
    if (pet->pMonsterData) memcpy(pet->pMonsterData->nComponent, components.data(), components.size());
}
}
