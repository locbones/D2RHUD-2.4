#include "ImmunityLabels.h"
#include "Sunder.h"
#include "../d2/D2Ptrs.h"
#include <windows.h>
#include <detours/detours.h>
#include <intrin.h>
#include <array>
#include <algorithm>

namespace ImmunityLabels {
namespace {
using ReadStat = int(__fastcall*)(D2StatListExStrc*, uint32_t, const void*);
ReadStat originalRead = nullptr;
RefreshSunder refreshSunder = nullptr;
uintptr_t labelReturn = 0;
bool attempted = false, installed = false;

// These class-ID guards suppress only native Uber display. Keep the native
// localized resistance and modifier builders, including their normal filters.
constexpr uint32_t uberImmunityRva = 0x83794, uberModifiersRva = 0xb27d8;
constexpr std::array<uint8_t, 20> uberImmunityInstructions{
    0xb8,0xc0,0x02,0,0,0x66,0x44,0x2b,0xf8,0x66,
    0x41,0x83,0xff,0x05,0x0f,0x86,0x69,0x03,0,0
};
constexpr std::array<uint8_t, 23> uberModifiersInstructions{
    0x41,0x0f,0xb7,0x4f,0x04,0xba,0xc0,0x02,0,0,
    0x66,0x2b,0xca,0x66,0x83,0xf9,0x05,0x0f,0x86,0xc4,0x02,0,0
};
template<class T> bool Read(uintptr_t address, T& out) {
    SIZE_T count = 0;
    return address && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address), &out, sizeof(out), &count) && count == sizeof(out);
}
template<size_t N> bool UberSiteSupported(uintptr_t address, const std::array<uint8_t, N>& expected, bool patched) {
    auto wanted = expected;
    if (patched) std::fill(wanted.end() - 6, wanted.end(), uint8_t(0x90));
    std::array<uint8_t, N> actual{};
    return Read(address, actual) && actual == wanted;
}
bool UberSupported(const std::array<uintptr_t, 2>& sites, bool patched = false) {
    return UberSiteSupported(sites[0], uberImmunityInstructions, patched) &&
        UberSiteSupported(sites[1], uberModifiersInstructions, patched);
}
bool ChangeUberDisplay(const std::array<uintptr_t, 2>& sites, bool enable) {
    // Validate both complete comparisons before touching either conditional
    // branch. A mismatch or protection failure must never patch only one site.
    if (!UberSupported(sites, !enable)) return false;
    const std::array<uintptr_t, 2> branches{
        sites[0] + uberImmunityInstructions.size() - 6,
        sites[1] + uberModifiersInstructions.size() - 6
    };
    std::array<DWORD, 2> protection{};
    size_t writable = 0;
    for (; writable < branches.size(); ++writable) {
        if (!VirtualProtect(reinterpret_cast<void*>(branches[writable]), 6, PAGE_EXECUTE_READWRITE, &protection[writable])) break;
    }
    if (writable != branches.size()) {
        for (size_t i = 0; i < writable; ++i) {
            DWORD ignored = 0;
            VirtualProtect(reinterpret_cast<void*>(branches[i]), 6, protection[i], &ignored);
        }
        return false;
    }
    constexpr std::array<uint8_t, 6> nops{0x90,0x90,0x90,0x90,0x90,0x90};
    const std::array<const uint8_t*, 2> original{
        uberImmunityInstructions.data() + uberImmunityInstructions.size() - 6,
        uberModifiersInstructions.data() + uberModifiersInstructions.size() - 6
    };
    for (size_t i = 0; i < branches.size(); ++i) {
        memcpy(reinterpret_cast<void*>(branches[i]), enable ? nops.data() : original[i], 6);
        FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(branches[i]), 6);
    }
    for (size_t i = 0; i < branches.size(); ++i) {
        DWORD ignored = 0;
        VirtualProtect(reinterpret_cast<void*>(branches[i]), 6, protection[i], &ignored);
    }
    return true;
}
constexpr std::array<uint8_t, 65> labelInstructions{
    0x48,0x63,0x56,0xf8,0x4d,0x85,0xf6,0x74,0x36,0x49,0x8b,0x8e,0x88,0,0,0,
    0x48,0x85,0xc9,0x74,0x2a,0x85,0xd2,0x78,0x26,0x48,0x3b,0x15,0x40,0xc2,0xc1,0x01,
    0x48,0x8b,0xc2,0x73,0x1a,0x4c,0x69,0xc0,0x44,0x01,0,0,0x4c,0x03,0x05,0x25,
    0xc2,0xc1,0x01,0x74,0x0a,0xc1,0xe2,0x10,0xe8,0x13,0xf5,0x15,0,0xeb,0x02,0x33,0xc0
};
bool Supported(uintptr_t site, uintptr_t reader) {
    std::array<uint8_t, 65> bytes{};
    std::array<uint8_t, 14> entry{};
    // Full function detour, scoped to the audited native label call site, avoids
    // transplanting a mid-function assembly stub or old DLL-relative addresses.
    constexpr std::array<uint8_t, 14> expected{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x63,0x41,0x1c};
    return Read(site, bytes) && bytes == labelInstructions && Read(reader, entry) && entry == expected;
}

D2GameStrc* CurrentGame() {
    uintptr_t client = 0; D2GameStrc* game = nullptr;
    return Read(Pattern::Address(0x1d637f0), client) && Read(client + 0x230, game) ? game : nullptr;
}
int ReadForCaller(D2StatListExStrc* stats, uint32_t encodedStat, const void* cost, uintptr_t caller) {
    if (caller != labelReturn || (encodedStat & 0xffff) || !Sunder::IsResistance(encodedStat >> 16))
        return originalRead(stats, encodedStat, cost);
    const uint32_t stat = encodedStat >> 16;
    // D2R's extended stat-list owner is at 0x78 (not the list's allocation pool).
    D2UnitStrc* client = nullptr;
    if (!Read(reinterpret_cast<uintptr_t>(stats) + 0x78, client) || !client ||
        client->dwUnitType != UNIT_MONSTER || client->pStatListEx != stats)
        return originalRead(stats, encodedStat, cost);
    auto selected = client;
    if (auto game = CurrentGame(); game && UNITS_GetServerUnitByTypeAndId) {
        auto server = UNITS_GetServerUnitByTypeAndId(game, UNIT_MONSTER, client->dwUnitId);
        if (server && server->dwUnitType == UNIT_MONSTER && server->dwUnitId == client->dwUnitId && server->pStatListEx) {
            // Physical is first in the native six-element loop. Refresh once,
            // before any label is tested, including after equipment changes.
            if (stat == 36 && refreshSunder) refreshSunder(game, server);
            selected = server;
        }
    }
    // Calling the trampoline also prevents recursion through the general stat
    // reader. Ordinary resistance pierce is never folded into these labels.
    return originalRead(selected->pStatListEx, encodedStat, cost);
}
int __fastcall HookRead(D2StatListExStrc* stats, uint32_t encodedStat, const void* cost) {
    return ReadForCaller(stats, encodedStat, cost, reinterpret_cast<uintptr_t>(_ReturnAddress()));
}
}
bool Install(RefreshSunder refresh) {
    if (attempted) return installed;
    attempted = true;
    const auto site = Pattern::Address(0x83920), reader = Pattern::Address(0x1e2e70);
    const std::array<uintptr_t, 2> uberSites{Pattern::Address(uberImmunityRva), Pattern::Address(uberModifiersRva)};
    if (!Supported(site, reader) || !UberSupported(uberSites)) return false;
    labelReturn = Pattern::Address(0x8395d); refreshSunder = refresh;
    originalRead = reinterpret_cast<ReadStat>(reader);
    if (DetourTransactionBegin() != NO_ERROR) return false;
    if (DetourUpdateThread(GetCurrentThread()) != NO_ERROR) { DetourTransactionAbort(); return false; }
    if (DetourAttach(reinterpret_cast<PVOID*>(&originalRead), HookRead) != NO_ERROR) { DetourTransactionAbort(); return false; }
    if (!ChangeUberDisplay(uberSites, true)) { DetourTransactionAbort(); return false; }
    installed = DetourTransactionCommit() == NO_ERROR;
    if (!installed) ChangeUberDisplay(uberSites, false);
    return installed;
}
void Uninstall() {
    if (!installed || DetourTransactionBegin() != NO_ERROR) return;
    if (DetourUpdateThread(GetCurrentThread()) != NO_ERROR ||
        DetourDetach(reinterpret_cast<PVOID*>(&originalRead), HookRead) != NO_ERROR) {
        DetourTransactionAbort(); return;
    }
    if (DetourTransactionCommit() != NO_ERROR) return;
    // Restore only our still-matching patched pair; never overwrite a later
    // modification by another module during unload.
    ChangeUberDisplay({Pattern::Address(uberImmunityRva), Pattern::Address(uberModifiersRva)}, false);
    installed = attempted = false;
    refreshSunder = nullptr; originalRead = nullptr; labelReturn = 0;
}
}
