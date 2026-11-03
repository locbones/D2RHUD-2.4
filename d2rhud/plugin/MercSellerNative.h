#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace MercSeller::NativeData {
struct Site {
    uint32_t rva, resume;
    uint8_t gameRegister, sellerRegister, outputRegister;
    uint8_t clearRegister = 0;
    bool request = false, clientLookup = false;
    uint8_t unitLookup = 0;
};
// Extend seller capability and disambiguate profile identity. Native roster
// filtering, interaction checks, purchase validation, costs and packets continue.
inline constexpr std::array<Site, 8> Sites{{
    {0x344e9e, 0x344eda, 13, 9, 3},                // host list request: game R13, seller R9D, metadata RBX
    {0x34a043, 0x34a07d, 14, 8, 7, 3},             // host hire purchase: also clear EBX
    {0x34c5bc, 0x34c5ec, 14, 5, 3},                // host list refresh: after native RBX save; seller EBP
    {0x2bb84b, 0x2bb88a, 15, 8, 3, 6, true},       // upstream action-3 dispatcher; clear ESI
    {0xbe2151, 0xbe224e, 0, 8, 0, 0, false, true}, // HD preview: name CX/version R8W -> row RAX
    {0x13b3b3, 0x13b3e9, 0, 9, 5, 0, false, true}, // Hire: name DX/version R9W -> row RBP
    {0x33d6d6, 0x33d740, 0, 0, 0, 0, false, false, 1}, // host attachment: unit RDI, name BX, Version DX
    {0x0ae16b, 0x0ae215, 0, 0, 0, 0, false, false, 2}, // client unit initialization: unit R15, name CX
}};
inline uint64_t Hash(std::span<const uint8_t> bytes) {
    uint64_t result = 14695981039346656037ULL;
    for (auto byte : bytes)
        result = (result ^ byte) * 1099511628211ULL;
    return result;
}
struct Signature {
    uint32_t rva, size;
    uint64_t hash;
};
// Filled from the supported 1.4.71510 executable by the native fixture.
inline constexpr std::array<Signature, 17> Signatures{{
    {0x344e70, 0x490, 0xd47d71095692cd0cULL},
    {0x349f00, 0x7f0, 0xbb69859bfa511729ULL},
    {0x34c530, 0x590, 0x6dfad6e66a80cadeULL},
    {0x13d5c0, 0x7b0, 0xf00206481d1f1b00ULL},
    {0x1567d0, 0x10d, 0xe7dfe20d041ae39cULL},
    {0x2381c0, 0x180, 0x1aedc55104e4f36aULL},
    {0x3444a0, 0x23a, 0x25ac21ee9e68c3beULL},
    {0x346d50, 0x11a, 0x22065f0e617d39f2ULL},
    {0x2ebcc0, 0x280, 0xfc2e0b4af6d13ce0ULL},
    {0x1852470, 0x4b4, 0x3bebb02374c7b8b0ULL},
    {0x2bb7e0, 0x219, 0xbe10e0c0e808756bULL},
    {0xbe2110, 0x140, 0xcd6575f2e07b39eeULL},
    {0x13b2e0, 0x419, 0xdb594bd00eeba388ULL},
    {0x24c740, 0x2bc, 0xc190f4c36f70809cULL},
    {0x2ba290, 0xa4, 0x936cd8d1cf028fbaULL},
    {0x33d4b0, 0x50f, 0xfa2977a11700f4eaULL},
    {0x0ae0f0, 0x230, 0xa896c5c7b43db1e3ULL},
}};
inline std::vector<uint8_t> Relay(const Site& site, uintptr_t callback, uintptr_t original,
                                  uintptr_t resume) {
    std::vector<uint8_t> code;
    auto emit = [&](std::initializer_list<uint8_t> value) { code.insert(code.end(), value); };
    auto integer = [&](auto value) {
        auto p = reinterpret_cast<const uint8_t*>(&value);
        code.insert(code.end(), p, p + sizeof(value));
    };
    // All sites have 16-byte-aligned RSP. Save volatile registers, flags
    // and XMM0..5; provide the callback's Win64 shadow space separately.
    emit({0x9c, 0x50, 0x51, 0x52, 0x41, 0x50, 0x41, 0x51, 0x41, 0x52, 0x41, 0x53});
    emit({0x48, 0x81, 0xec, 0x80, 0, 0, 0});
    for (uint8_t i = 0; i < 6; ++i)
        emit({0xf3, 0x0f, 0x7f, uint8_t(0x44 + i * 8), 0x24, uint8_t(0x20 + i * 16)});
    if (site.unitLookup == 1) {
        emit({0x44, 0x0f, 0xb7, 0xca}); // R9D = original Version DX
        emit({0x44, 0x0f, 0xb7, 0xc3}); // R8D = name BX
        emit({0x48, 0x89, 0xfa});       // RDX = unit RDI
        emit({0x4c, 0x89, 0xe9});       // RCX = game R13
    } else if (site.unitLookup == 2) {
        emit({0x45, 0x0f, 0xb7, 0xc8}); // R9D = original Version R8W
        emit({0x44, 0x0f, 0xb7, 0xc1}); // R8D = name CX
        emit({0x4c, 0x89, 0xfa});       // RDX = unit R15
        emit({0x31, 0xc9});             // RCX = null (client difficulty)
    } else if (site.clientLookup) {
        // Pass name CX (preview) or DX (Hire) and the native Version word.
        emit({0x0f, 0xb7, uint8_t(site.sellerRegister == 8 ? 0xc9 : 0xca)});
        emit({0x41, 0x0f, 0xb7, uint8_t(site.sellerRegister == 8 ? 0xd0 : 0xd1)});
    } else {
        emit({0x4c, 0x89, uint8_t(site.gameRegister == 13 ? 0xe9 : site.gameRegister == 14 ? 0xf1 : 0xf9)});
        if (site.sellerRegister == 9)
            emit({0x44, 0x89, 0xca});
        else if (site.sellerRegister == 8)
            emit({0x44, 0x89, 0xc2});
        else
            emit({0x89, 0xea});
        if (site.request)
            emit({0x44, 0x8b, 0x84, 0x24, 0xe1, 0, 0, 0}); // action at original RSP+0x21
        else
            emit({0x41, 0xb8, 3, 0, 0, 0});
    }
    emit({0x48, 0xb8});
    integer(callback);
    emit({0xff, 0xd0, 0x48, 0x85, 0xc0, 0x0f, 0x84});
    const auto branch = code.size();
    integer(int32_t(0));
    if (site.outputRegister == 0)
        emit({0x48, 0x89, 0x84, 0x24, 0xb0, 0, 0, 0}); // chosen RAX survives the volatile restore
    else
        emit({0x48, 0x89, uint8_t(0xc0 + site.outputRegister)});
    if (site.clearRegister == 3)
        emit({0x31, 0xdb});
    else if (site.clearRegister == 6)
        emit({0x31, 0xf6});
    const auto finish = [&](uintptr_t target) {
        for (uint8_t i = 0; i < 6; ++i)
            emit({0xf3, 0x0f, 0x6f, uint8_t(0x44 + i * 8), 0x24, uint8_t(0x20 + i * 16)});
        emit({0x48, 0x81, 0xc4, 0x80, 0, 0, 0, 0x41, 0x5b, 0x41, 0x5a, 0x41, 0x59, 0x41, 0x58, 0x5a, 0x59,
              0x58, 0x9d});
        emit({0xff, 0x25, 0, 0, 0, 0});
        integer(target);
    };
    finish(resume);
    const auto displacement = int32_t(code.size() - branch - 4);
    std::memcpy(code.data() + branch, &displacement, 4);
    finish(original);
    return code;
}
// Stack adjustment codes for the relay's 19-byte prologue. Chain the native
// frame at the original site, so crash reporting/unwinding can cross callbacks.
inline std::array<uint8_t, 24> UnwindPrefix(uint8_t nativeFrame) {
    return {uint8_t(1 | (4 << 3)),
            19,
            9,
            nativeFrame,
            19,
            0xf2,
            12,
            2,
            10,
            2,
            8,
            2,
            6,
            2,
            4,
            2,
            3,
            2,
            2,
            2,
            1,
            2,
            0,
            0};
}
} // namespace MercSeller::NativeData
