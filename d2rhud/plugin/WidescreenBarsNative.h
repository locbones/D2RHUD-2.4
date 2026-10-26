#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <span>

namespace WidescreenBars::Native {
// D2R 1.4.71510: vignette resource-binding callback. This callback uploads
// {renderWidth, renderHeight, 19/9, ...} to shader constant slot 0x15.
inline constexpr uint32_t Callback = 0x823540;
inline constexpr size_t CallbackSize = 0x175;
inline constexpr uint64_t CallbackHash = 0x6487dd834bf21653ull;
inline constexpr uint32_t Store = 0x823675;
inline constexpr uint32_t Bars = 0x565b54, BarsResume = 0x565bcc;
inline constexpr uint32_t Cursor = 0x5e817, CursorResume = 0x5e868;
inline constexpr uint32_t Refresh = 0x7e55ee, DirtyOffset = 0x133848;
struct Signature { uint32_t rva; size_t size; uint64_t hash; };
inline constexpr std::array<Signature, 4> Signatures{{
    {Callback, CallbackSize, CallbackHash},
    {0x565b2d, 0xac, 0x225c9408bb9cc2b6ull},
    {0x5e7ea, 0x7e, 0xc2dcece34c1af9bdull},
    {0x7e55eb, 0x40, 0xb15b0d85a17cdc25ull},
}};
inline constexpr uint32_t NativeAspect = 0x40071c72; // 19/9, as emitted by the game
inline constexpr uint32_t UnmaskedAspect = 0x46800000; // 16384:1, beyond practical viewports
inline constexpr std::array<uint8_t, 8> StoreBytes{0x41, 0xc7, 0x40, 0x08, 0x72, 0x1c, 0x07, 0x40};
inline uint64_t Hash(std::span<const uint8_t> bytes) {
    uint64_t result = 14695981039346656037ull;
    for (auto value : bytes) result = (result ^ value) * 1099511628211ull;
    return result;
}
// Replace only the store's immediate with a live, aligned data load. Preserve
// RAX, every other register, flags and the stack; there is no C++ call here.
inline std::array<uint8_t, 32> Relay(uintptr_t aspect, uintptr_t continuation) {
    std::array<uint8_t, 32> code{
        0x50,                                           // push rax
        0x48, 0xb8, 0,0,0,0,0,0,0,0,                    // mov rax, aspect
        0x8b, 0x00,                                     // mov eax, [rax]
        0x41, 0x89, 0x40, 0x08,                         // mov [r8+8], eax
        0x58,                                           // pop rax
        0xff, 0x25, 0,0,0,0, 0,0,0,0,0,0,0,0           // jmp [rip+0]
    };
    std::memcpy(code.data() + 3, &aspect, sizeof(aspect));
    std::memcpy(code.data() + 24, &continuation, sizeof(continuation));
    return code;
}
// Enabled skips only the HD bars/clamp body; disabled executes MinHook's
// relocated native instructions. Both paths preserve every GPR and flags.
inline std::array<uint8_t, 49> BranchRelay(uintptr_t enabled, uintptr_t original, uintptr_t bypass) {
    std::array<uint8_t, 49> code{
        0x9c, 0x50,                                  // pushfq; push rax
        0x48,0xb8, 0,0,0,0,0,0,0,0,                  // mov rax,enabled
        0x80,0x38,0, 0x74,0x10,                      // cmp byte [rax],0; je native
        0x58,0x9d, 0xff,0x25,0,0,0,0, 0,0,0,0,0,0,0,0,
        0x58,0x9d, 0xff,0x25,0,0,0,0, 0,0,0,0,0,0,0,0,
    };
    std::memcpy(code.data()+4,&enabled,8);
    std::memcpy(code.data()+25,&bypass,8);
    std::memcpy(code.data()+41,&original,8);
    return code;
}
// Consume a refresh request on the native render thread, where RCX is the
// current render state. Never retain this pointer across scene/device changes.
// The trampoline performs the original CMP before the game's existing Jcc.
inline std::array<uint8_t, 45> RefreshRelay(uintptr_t pending, uintptr_t original) {
    std::array<uint8_t, 45> code{
        0x9c,0x50,0x52,
        0x48,0xb8, 0,0,0,0,0,0,0,0,
        0x31,0xd2, 0x86,0x10,                       // xor edx,edx; xchg byte [rax],dl
        0x84,0xd2, 0x74,0x07,                       // test dl,dl; je unchanged
        0xc6,0x81,0x48,0x38,0x13,0,1,               // mov byte [rcx+133848h],1
        0x5a,0x58,0x9d,
        0xff,0x25,0,0,0,0, 0,0,0,0,0,0,0,0,
    };
    std::memcpy(code.data()+5,&pending,8);
    std::memcpy(code.data()+37,&original,8);
    return code;
}
}
