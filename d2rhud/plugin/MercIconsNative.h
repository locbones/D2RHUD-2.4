#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace MercIcons::NativeData {
constexpr uint32_t Site = 0xbb8e0a, Resume = 0xbb8f1b;
inline uint64_t Hash(std::span<const uint8_t> bytes) {
    uint64_t value = 14695981039346656037ULL;
    for (auto byte : bytes)
        value = (value ^ byte) * 1099511628211ULL;
    return value;
}
struct Signature {
    uint32_t rva, size;
    uint64_t hash;
};
// Audited 2.4 widget setter (including its switch tables), skill-description
// loader and both native panel callers. Populated from the supported executable.
inline constexpr std::array<Signature, 4> Signatures{{
    {0xbb8dd0, 0x350, 0xbfb39401c965539bULL},
    {0x1d6120, 0x180, 0xf564aad5337bc426ULL},
    {0xbe2ae3, 0x28, 0x5918fc65e0d60025ULL},
    {0xbf4bb0, 0x19, 0x2c893a5441fb5deeULL},
}};
inline std::vector<uint8_t> Relay(uintptr_t callback, uintptr_t original, uintptr_t resume) {
    std::vector<uint8_t> code;
    auto emit = [&](std::initializer_list<uint8_t> bytes) { code.insert(code.end(), bytes); };
    auto integer = [&](auto value) {
        auto p = reinterpret_cast<const uint8_t*>(&value);
        code.insert(code.end(), p, p + sizeof(value));
    };
    // The original setter has aligned RSP here. Preserve flags, all volatile
    // integer/XMM registers, and provide the callback its own shadow space.
    emit({0x9c, 0x50, 0x51, 0x52, 0x41, 0x50, 0x41, 0x51, 0x41, 0x52, 0x41, 0x53});
    emit({0x48, 0x81, 0xec, 0x80, 0, 0, 0});
    for (uint8_t i = 0; i < 6; ++i)
        emit({0xf3, 0x0f, 0x7f, uint8_t(0x44 + i * 8), 0x24, uint8_t(0x20 + i * 16)});
    emit({0x44, 0x89, 0xc1, 0x48, 0xb8}); // ECX = original skill ID in R8D
    integer(callback);
    emit({0xff, 0xd0, 0x85, 0xc0, 0x0f, 0x88}); // negative means use original switch
    const auto branch = code.size();
    integer(int32_t(0));
    // Restore the chosen frame through the saved RAX slot. The native switch
    // also zeroes EDX before joining its tooltip/required-level/color path.
    emit({0x48, 0x89, 0x84, 0x24, 0xb0, 0, 0, 0, 0x48, 0xc7, 0x84, 0x24, 0xa0, 0, 0, 0, 0, 0, 0, 0});
    const auto finish = [&](uintptr_t destination) {
        for (uint8_t i = 0; i < 6; ++i)
            emit({0xf3, 0x0f, 0x6f, uint8_t(0x44 + i * 8), 0x24, uint8_t(0x20 + i * 16)});
        emit({0x48, 0x81, 0xc4, 0x80, 0, 0, 0, 0x41, 0x5b, 0x41, 0x5a, 0x41, 0x59, 0x41, 0x58, 0x5a, 0x59,
              0x58, 0x9d});
        emit({0xff, 0x25, 0, 0, 0, 0});
        integer(destination);
    };
    finish(resume);
    const auto delta = int32_t(code.size() - branch - 4);
    std::memcpy(code.data() + branch, &delta, 4);
    finish(original);
    return code;
}
inline constexpr std::array<uint8_t, 24> UnwindPrefix(uint8_t frame) {
    return {0x21, 19, 9, frame, 19, 0xf2, 12, 2, 10, 2, 8, 2, 6, 2, 4, 2, 3, 2, 2, 2, 1, 2, 0, 0};
}
} // namespace MercIcons::NativeData
