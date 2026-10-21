#pragma once
#include <cstdint>
#include <cstring>

namespace SelectedAuras::Data {
using Byte = uint8_t;
template<class T> T Read(const void* address, size_t offset = 0) {
    T result{};
    if (address) std::memcpy(&result, static_cast<const Byte*>(address) + offset, sizeof(result));
    return result;
}
constexpr uint32_t AuraFlag = 0x20, PassiveFlag = 0x10, LeftFlag = 0x40000000;
constexpr size_t SkillStride = 0x284;
inline bool Eligible(uint32_t flags, int16_t state) {
    return (flags & AuraFlag) && !(flags & PassiveFlag) && state >= 0;
}
inline Byte* Slot(Byte* unit, size_t offset) {
    return Read<Byte*>(Read<Byte*>(unit, 0x100), offset);
}
inline bool IsAura(Byte* skill) {
    auto row = Read<Byte*>(skill);
    return row && Eligible(Read<uint32_t>(row, 0x24), Read<int16_t>(row, 0xa0));
}
inline Byte* Selected(Byte* unit) {
    auto right = Slot(unit, 0x10);
    if (!unit || Read<uint32_t>(unit) != 0) return right;
    if (IsAura(right)) return right;
    auto left = Slot(unit, 8);
    return IsAura(left) ? left : nullptr;
}
inline bool Conflict(Byte* unit, Byte* candidate, int left) {
    return unit && Read<uint32_t>(unit) == 0 && IsAura(candidate) &&
        candidate != Slot(unit, left ? 8 : 0x10) && IsAura(Slot(unit, left ? 0x10 : 8));
}
}
