#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <span>

namespace LoadingPerformance::Native {
// Inspected D2R 1.4.71510. TextureCache::Tick selects min(queued,16-inFlight)
// under its upload queue lock. Completed workers decrement inFlight once.
inline constexpr uint32_t Budget = 0x7adbe1, Resume = Budget + 5;
inline constexpr uint32_t Loading = 0x1cb410a;
inline constexpr uint32_t NormalLimit = 16, LoadingLimit = 64;
inline constexpr size_t RelaySize = 32;
inline constexpr std::array<uint8_t,5> Original{0xb9,16,0,0,0};
struct Signature { uint32_t rva, size; uint64_t hash; };
inline constexpr std::array<Signature,3> Signatures{{
    {0x7adbd4,0x40,0x1bbd5aaaae39a11cULL},
    {0x7b2220,0x70,0xa5453e90b90ad559ULL},
    {0x6e130,0x1a,0x03e953e9ab61bcddULL}
}};
inline uint64_t Hash(std::span<const uint8_t> bytes) {
    uint64_t value=14695981039346656037ULL;
    for(auto byte:bytes)value=(value^byte)*1099511628211ULL;
    return value;
}
// No calls, stack changes or retained native objects. EAX (inFlight), RDX
// (queued) and every other register survive. The original SUB replaces flags.
// Loading can end with >16 uploads still running: clamp the cap to inFlight
// so the native subtraction cannot become negative during that transition.
inline std::optional<std::array<uint8_t,RelaySize>> Relay(uintptr_t address,uintptr_t loading,uintptr_t resume) {
    std::array<uint8_t,RelaySize> code{
        0xb9,16,0,0,0,                     // mov ecx,16
        0x80,0x3d,0,0,0,0,0,               // cmp byte [rip+loading],0
        0x74,0x05,                         // je selected
        0xb9,64,0,0,0,                     // mov ecx,64
        0x3b,0xc8,                         // cmp ecx,eax
        0x0f,0x4c,0xc8,                    // cmovl ecx,eax
        0x85,0xc9,                         // test ecx,ecx (cap is always >=16)
        0x0f,0x85,0,0,0,0                  // jnz original SUB
    };
    const auto relative=[](uintptr_t target,uintptr_t next,int32_t& value){
        const auto delta=int64_t(target)-int64_t(next);
        if(delta<(std::numeric_limits<int32_t>::min)() || delta>(std::numeric_limits<int32_t>::max)())return false;
        value=int32_t(delta);return true;
    };
    int32_t flag=0,jump=0;
    if(!relative(loading,address+12,flag) || !relative(resume,address+code.size(),jump))return std::nullopt;
    // A conditional branch deliberately avoids x64's tailcall-epilogue
    // recognition: the interrupted native frame still needs its full unwind.
    std::memcpy(code.data()+7,&flag,4);std::memcpy(code.data()+28,&jump,4);
    return code;
}
}
