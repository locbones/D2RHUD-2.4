// Audited D2R 1.2.69270 avatar, save and skill-action entry windows.
// Reference executable SHA-256: E4E13570E60C0A65CB0469D6659BE8A6008F9433AC50DB1C4F94501D7D9016F8
#pragma once
#include <array>
#include <cstdint>
namespace CubeMap::SurvivorsNative {
struct Signature {uint32_t rva,size;uint64_t hash;};
inline constexpr std::array<Signature,37> Signatures{{
    {0x2934b0,0x20,0x9d5f7b18d2521f34ULL},
    {0x318c80,0x20,0xff97a5ba130cb03cULL},
    {0x28f2b0,0x20,0xc44e0e3eaad36f24ULL},
    {0x29f350,0x20,0x280267da64d5d598ULL},
    {0x29f090,0x20,0xbc59ca8ad840071dULL},
    {0x31a280,0x20,0xd3c9026618bc233bULL},
    {0x29d890,0x20,0x39d2926ae8dcc0ceULL},
    {0x2c52e0,0x20,0x638364fdbebe4ce0ULL},
    {0x29d8f0,0x20,0xedc5ab0be658ada7ULL},
    {0x29de20,0x20,0x12b289075de3c56fULL},
    {0x341670,0x20,0xba7badcdf03fc135ULL},
    {0x2192d0,0x20,0x52751a148815ff19ULL},
    {0x218ce0,0x20,0xb2ed836e0e2755c6ULL},
    {0x22e8a0,0x20,0x7b97f5d8fb1a8511ULL},
    {0x1f73e0,0x20,0xa4a533696d24d4b9ULL},
    {0x2c7a90,0x20,0x19de8b2881461125ULL},
    {0x2aaa00,0x20,0xaacd1bf24230cf8aULL},
    {0x29a2a0,0x20,0x63dfa5bcaad5735dULL},
    {0x2cce40,0x20,0x6739d6ad50d15c36ULL},
    {0x298550,0x20,0xd8bb698b903b5990ULL},
    {0x298810,0x20,0xba2d045978e6e249ULL},
    {0x2095a0,0x20,0x1a7daab6cce414f8ULL},
    {0x2ce6c0,0x20,0x941c5fe39c0f9b5aULL},
    {0x28c740,0x20,0x906d408cab3300ffULL},
    {0x28c520,0x20,0x9f817d315951b20aULL},
    {0x6d01a0,0x20,0x7f76cedfc9544adcULL},
    {0x2d5890,0x20,0xacbd86c82d2ae114ULL},
    {0x2f70a0,0x20,0x090114e63fc9693bULL},
    {0x6ff60,0x20,0x5cecb990aa7517e5ULL},
    {0x70007,0x20,0x47d320523d2ddb60ULL},
    {0x70065,0x20,0x09baf3b5313b46bbULL},
    {0x6d0470,0x20,0x5eeab4767635bae3ULL},
    {0x1d4e15,0x20,0x101f82614a3d50b7ULL},
    {0x2cb670,0x20,0x84907d989cb0352aULL},
    {0x320fa0,0x20,0x230e5b456163526aULL},
    {0x3208b0,0x20,0x983f687dce160d04ULL},
    {0x320a10,0x20,0xf62a77726246d46aULL},
}};
// Full save-dispatch variants: original 8 KiB and the complete launcher 32-KiB edit.
inline constexpr Signature SaveDispatchOriginal{0x28f2b0,0x257,0x44b331bdceb666ecULL};
inline constexpr Signature SaveDispatch32K{0x28f2b0,0x257,0x3a92c6f8e4ec192cULL};
inline constexpr std::array<uint32_t,8> SaveLimitPatchSites{{0x28f2b7,0x28f2d1,0x28f2f9,0x28f324,0x28f454,0x28f47f,0x28f487,0x28f496}};
// Character persistence needs only these shared loader/writer hooks.
inline constexpr std::array<Signature,2> SaveLoadSignatures{{Signatures[0],Signatures[1]}};
static_assert(SaveLoadSignatures[0].rva==0x2934b0 && SaveLoadSignatures[1].rva==0x318c80);
}
