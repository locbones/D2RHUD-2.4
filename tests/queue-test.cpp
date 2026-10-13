#include "../d2rhud/WineQueue.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <detours/detours.h>
#include <MinHook.h>

using namespace WineQueue;
using Function = int (*)(int);
Function detourOriginal = nullptr, minhookOriginal = nullptr;
int detourCalls = 0, minhookCalls = 0;
int DetourReplacement(int n) { assert(++detourCalls < 10); return detourOriginal(n) + 100; }
int MinHookReplacement(int n) { assert(++minhookCalls < 10); return minhookOriginal(n) + 1000; }

void ChainTest(bool minhookFirst) {
    // Representative x64 prologue for testing both hook-chain orders.
    unsigned char code[] = {0x40,0x53,0x55,0x56,0x57,0x48,0x83,0xec,0x78,
        0x8b,0xc1,0x83,0xc0,0x01,0x48,0x83,0xc4,0x78,0x5f,0x5e,0x5d,0x5b,0xc3};
    auto memory = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    assert(memory);
    std::memcpy(memory, code, sizeof(code));
    assert(FlushInstructionCache(GetCurrentProcess(), memory, sizeof(code)));
    const auto target = reinterpret_cast<Function>(memory);
    assert(target(4) == 5);
    const auto attachMinhook = [&] {
        assert(MH_CreateHook(memory, MinHookReplacement, reinterpret_cast<void**>(&minhookOriginal)) == MH_OK);
        assert(MH_EnableHook(memory) == MH_OK);
    };
    const auto attachDetour = [&] {
        detourOriginal = target;
        assert(DetourTransactionBegin() == NO_ERROR);
        assert(DetourUpdateThread(GetCurrentThread()) == NO_ERROR);
        assert(DetourAttach(reinterpret_cast<PVOID*>(&detourOriginal), DetourReplacement) == NO_ERROR);
        assert(DetourTransactionCommit() == NO_ERROR);
    };
    const auto detachDetour = [&] {
        assert(DetourTransactionBegin() == NO_ERROR);
        assert(DetourUpdateThread(GetCurrentThread()) == NO_ERROR);
        assert(DetourDetach(reinterpret_cast<PVOID*>(&detourOriginal), DetourReplacement) == NO_ERROR);
        assert(DetourTransactionCommit() == NO_ERROR);
    };
    const auto detachMinhook = [&] {
        assert(MH_DisableHook(memory) == MH_OK);
        assert(MH_RemoveHook(memory) == MH_OK);
    };
    if (minhookFirst) { attachMinhook(); attachDetour(); }
    else { attachDetour(); attachMinhook(); }
    detourCalls = minhookCalls = 0;
    assert(target(4) == 1105);
    assert(detourCalls == 1 && minhookCalls == 1);
    if (minhookFirst) { detachDetour(); detachMinhook(); }
    else { detachMinhook(); detachDetour(); }
    assert(target(4) == 5);
    assert(VirtualFree(memory, 0, MEM_RELEASE));
    std::printf("PASS: hook chain order=%s, both callbacks once, forwarding/restoration\n",
        minhookFirst ? "MinHook then Detours" : "Detours then MinHook");
}

int main() {
    assert(!IsMacOS()); // This harness runs on the Windows build host.
    Methods methods{};
    for (size_t i = 0; i < methods.size(); ++i) methods[i] = 0x123400 + i * 16;
    // These fake tables deliberately contain non-callable addresses. The reader
    // must never dispatch a COM call during calibration or validation.
    uintptr_t objectA = reinterpret_cast<uintptr_t>(methods.data());
    uintptr_t objectB = objectA;
    uintptr_t objectLive = objectA;
    std::array<uintptr_t, ScanSlots> chainA{}, chainB{}, live{};
    const auto a = reinterpret_cast<uintptr_t>(&objectA);
    const auto b = reinterpret_cast<uintptr_t>(&objectB);
    const auto c = reinterpret_cast<uintptr_t>(&objectLive);
    for (const intptr_t adjustment : {0, 16, -32}) {
        chainA.fill(0); chainB.fill(0); live.fill(0);
        chainA[153] = a - adjustment; chainB[153] = b - adjustment; live[153] = c - adjustment;
        const auto first = Capture(reinterpret_cast<uintptr_t>(chainA.data()), a);
        const auto second = Capture(reinterpret_cast<uintptr_t>(chainB.data()), b);
        const auto layout = Calibrate(first, second, methods);
        assert(layout.valid && layout.offset == 153 * sizeof(uintptr_t) && layout.adjustment == adjustment);
        assert(Resolve(reinterpret_cast<uintptr_t>(live.data()), layout) == c);
        SetLastError(12345);
        assert(Resolve(reinterpret_cast<uintptr_t>(live.data()), layout) == c && GetLastError() == 12345);
        auto mismatch = second;
        mismatch.slots[153] += 8;
        assert(!Calibrate(first, mismatch, methods).valid);
        mismatch = second;
        mismatch.readable[153] = false;
        assert(!Calibrate(first, mismatch, methods).valid);
        auto ambiguousFirst = first, ambiguousSecond = second;
        ambiguousFirst.slots[154] = ambiguousFirst.slots[153];
        ambiguousSecond.slots[154] = ambiguousSecond.slots[153];
        assert(!Calibrate(ambiguousFirst, ambiguousSecond, methods).valid);
        assert(!Calibrate(first, first, methods).valid);
        auto invalid = layout;
        invalid.methods[18] ^= 1;
        assert(Resolve(reinterpret_cast<uintptr_t>(live.data()), invalid) == 0);
        invalid = layout; invalid.valid = false;
        assert(Resolve(reinterpret_cast<uintptr_t>(live.data()), invalid) == 0);
        invalid = layout; invalid.offset = 4096;
        assert(Resolve(reinterpret_cast<uintptr_t>(live.data()), invalid) == 0);
        assert(!Resolve(0, layout) && !Resolve(1, layout) && !Resolve(UINTPTR_MAX - 4, layout));
        live[153] = UINTPTR_MAX - 7;
        assert(!Resolve(reinterpret_cast<uintptr_t>(live.data()), layout));
        live[153] = 1;
        assert(!Resolve(reinterpret_cast<uintptr_t>(live.data()), layout));
    }
    auto guard = VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_NOACCESS);
    assert(guard);
    auto unreadable = Capture(reinterpret_cast<uintptr_t>(guard), a);
    for (auto readable : unreadable.readable) assert(!readable);
    assert(VirtualFree(guard, 0, MEM_RELEASE));
    std::puts("PASS: direct/positive/negative wrapper offsets; ambiguity/mismatch/unreadable/same-sample rejection; full vtable check; null/overflow/guard page; GetLastError; no candidate COM calls");
    assert(MH_Initialize() == MH_OK);
    ChainTest(true);
    ChainTest(false);
    assert(MH_Uninitialize() == MH_OK);
    std::printf("queue-test passed pid=%lu\n", GetCurrentProcessId());
}
