#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

namespace MultipleAuras::NativeData {
// D2R.exe e4e13570e60c0a65cb0469d6659be8a6008f9433ac50db1c4f94501d7d9016f8.
// Item stat 151's callback, its periodic handler and aura removal all delete
// type-9 events by source key alone. Preserve the native source-key predicate
// and additionally match the event's skill argument at +0x1c.
inline constexpr uint32_t DeleteEvent = 0x2c83d0, Deactivate = 0x3500c0;
enum class Adapter { SkillR13, SkillR14, RemovalEvent, RemovalScope };
struct Site { uint32_t rva; Adapter adapter; uint64_t hash; };
inline constexpr Site Sites[] = {
    {0x2d3607, Adapter::SkillR13, 0xaa0e2bf8a5e94b33ULL},
    {0x2d36f8, Adapter::SkillR13, 0x07ec309760ece9e1ULL},
    {0x299b85, Adapter::SkillR14, 0x4b89bac22fcccb2eULL},
    {0x299be5, Adapter::SkillR14, 0x9b4351e54676eafaULL},
    {0x299c15, Adapter::SkillR14, 0x730860c74534d49aULL},
    {0x299c4a, Adapter::SkillR14, 0xe0b28b2db649f482ULL},
    {0x350245, Adapter::RemovalEvent, 0xf265f80c513f8eacULL},
    // Both direct callers of the removal function enter a thread-local scope.
    {0x2d38fa, Adapter::RemovalScope, 0x053f498521d3a45fULL},
    {0x2d6e10, Adapter::RemovalScope, 0x7242081c0b41031cULL}
};
struct Signature { uint32_t rva, size; uint64_t hash; };
inline constexpr Signature Functions[] = {
    {Deactivate, 32, 0x098930aa8c04ad4dULL},
    {DeleteEvent, 32, 0x7eb5a94b42b0f92eULL},
    {0x2c9090, 32, 0x8c9e69120c5acb0cULL},
    {0x2c8e00, 32, 0x867e3c04f650b7a0ULL}
};
inline uint64_t Hash(const uint8_t* bytes, size_t size) {
    uint64_t hash = 14695981039346656037ULL;
    for (size_t i = 0; i < size; ++i) hash = (hash ^ bytes[i]) * 1099511628211ULL;
    return hash;
}
inline bool KeepEvent(bool enabled, uint8_t type, int eventSkill, int skill) {
    return enabled && type == 9 && skill >= 0 && eventSkill != skill;
}
inline std::vector<uint8_t> Relay(Adapter adapter, uintptr_t filteredDelete,
                                  uintptr_t removalDelete, uintptr_t removalScope) {
    std::vector<uint8_t> bytes;
    if (adapter == Adapter::SkillR13) bytes = {0x45,0x8b,0xc5}; // mov r8d,r13d
    if (adapter == Adapter::SkillR14) bytes = {0x45,0x8b,0xc6}; // mov r8d,r14d
    const auto target = adapter == Adapter::RemovalEvent ? removalDelete :
        adapter == Adapter::RemovalScope ? removalScope : filteredDelete;
    bytes.insert(bytes.end(), {0x48,0xb8});
    for (size_t i = 0; i < 8; ++i) bytes.push_back(uint8_t(target >> (8*i)));
    bytes.insert(bytes.end(), {0xff,0xe0}); // Tail call: original CALL owns ABI/unwind frame.
    return bytes;
}
inline std::array<uint8_t,5> Call(uintptr_t site, uintptr_t target) {
    const int64_t delta = int64_t(target) - int64_t(site+5);
    if (delta < INT32_MIN || delta > INT32_MAX) return {};
    std::array<uint8_t,5> bytes{0xe8}; const auto rel = int32_t(delta);
    std::memcpy(bytes.data()+1, &rel, 4); return bytes;
}
}
