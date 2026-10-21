#include "SelectedAuras.h"
#include "SelectedAurasData.h"
#include "SelectedAurasNative.h"
#ifndef SELECTED_AURAS_TEST
#include "../Pattern.h"
#endif
#include <Windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <limits>
#include <mutex>
#include <vector>

namespace SelectedAuras {
namespace {
using Data::Byte;
template<class T> T Native(uint32_t rva) { return reinterpret_cast<T>(Pattern::Address(rva)); }
using Setter = void(__fastcall*)(Byte*, int, int);
using GetSkill = Byte*(__fastcall*)(Byte*, int, int);

// These helpers execute in the original assignment/event frames. The native
// blocks after the patched spans still remove stat lists, expire callbacks,
// update state graphics and start the newly selected aura's event.
Byte* BeforeAssign(Byte* unit, int hand, int id, int owner) {
    auto old = unit && Data::Read<uint32_t>(unit) == 0 ? Data::Selected(unit) :
        (hand ? nullptr : Data::Slot(unit, 0x10));
    Native<Setter>(hand ? 0x219690 : 0x219750)(unit, id, owner);
    return old;
}
int AfterAssign(Byte* unit, int hand, int id, Byte* old) {
    if (!unit || Data::Read<uint32_t>(unit) != 0) return hand ? -2 : id;
    auto selected = Data::Selected(unit);
    // Right assignment has already cleared state 85 in its native prologue.
    // Keep its native cleanup/reactivation even for the same selection, so
    // Prayer's mana-regeneration suppression is not skipped by this shortcut.
    if (selected == old && hand) return -2;
    if (hand && old) Native<void(__fastcall*)(Byte*, int, int)>(0x222ea0)(unit, 85, 0);
    return selected ? Data::Read<int16_t>(Data::Read<Byte*>(selected)) : 0;
}
uint64_t Periodic(Byte* unit) {
    auto selected = Data::Selected(unit);
    if (!selected) return 0xffffffff00000000ULL;
    int level = Data::Read<int>(selected, 0x38);
    if (Data::Read<int>(selected, 0x44) == -1)
        level += Native<int(__fastcall*)(Byte*, Byte*, int)>(0x21a990)(unit, selected, 0);
    return (uint64_t(uint32_t(Data::Read<int16_t>(Data::Read<Byte*>(selected)))) << 32) |
        uint32_t(level);
}
int PickerState(Byte* unit, Byte* skill, Byte* widget) {
    const int original = Native<int(__fastcall*)(Byte*, Byte*)>(0x169da0)(unit, skill);
    if (!widget || Data::Read<int>(widget, 0x9f0) != 0 || !skill) return original;
    const int left = Native<int(__fastcall*)(Byte*, Byte*, int)>(0x177bc0)(widget, skill, 0);
    return Data::Conflict(unit, skill, left) ? 3 : original;
}
Byte* PickerGetSkill(Byte* unit, int id, int owner, Byte* widget) {
    auto skill = Native<GetSkill>(0x218c70)(unit, id, owner);
    if (!skill || !widget || Data::Read<int>(widget, 0x9f0) != 0) return skill;
    const int left = Native<int(__fastcall*)(Byte*, Byte*, int)>(0x177bc0)(widget, skill, 0);
    return Data::Conflict(unit, skill, left) ? nullptr : skill;
}
Byte* GuardGetSkill(Byte* unit, int id, int owner, int left) {
    auto skill = Native<GetSkill>(0x218c70)(unit, id, owner);
    if (!Data::Conflict(unit, skill, left)) return skill;
    // Re-send the authoritative slot after an optimistic client hotkey change.
    // The recursive lookup passes because its requested node is already selected.
    auto current = Data::Slot(unit, left ? 8 : 0x10);
    auto row = Data::Read<Byte*>(current);
    if (row) Native<void(__fastcall*)(Byte*, int, int, int)>(0x29a2a0)(unit, left,
        Data::Read<int16_t>(row), Data::Read<int>(current, 0x44));
    return nullptr;
}

struct Patch {
    uint32_t rva;
    std::vector<Byte> original, replacement;
};
std::array<Patch, 6> patches;
Byte* relay = nullptr;
bool retainedRelay = false;
std::atomic<bool> installed{false};
std::atomic<const char*> status{"Selected auras are not installed."};
std::mutex lifecycle;
Byte* changedRows = nullptr;
uint64_t changedCount = 0;
bool initializedRows = false;
std::vector<uint32_t> changedFlags;

template<class T> bool ReadMemory(uintptr_t address, T& value) {
    SIZE_T count = 0;
    return address && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address),
        &value, sizeof(value), &count) && count == sizeof(value);
}
bool Equal(uintptr_t address, const std::vector<Byte>& expected) {
    std::array<Byte, 128> bytes{};
    SIZE_T count = 0;
    return expected.size() <= bytes.size() && ReadProcessMemory(GetCurrentProcess(),
        reinterpret_cast<void*>(address), bytes.data(), expected.size(), &count) &&
        count == expected.size() && std::equal(expected.begin(), expected.end(), bytes.begin());
}
bool Supported() {
    for (const auto& signature : NativeData::Signatures) {
        std::array<Byte, 32> bytes{};
        if (!ReadMemory(Pattern::Address(signature.rva), bytes)) return false;
        uint32_t hash = 2166136261u;
        for (auto value : bytes) hash = (hash ^ value) * 16777619u;
        if (hash != signature.hash) return false;
    }
    return true;
}
struct Code {
    Byte* data;
    size_t capacity, size = 0;
    uintptr_t address;
    bool valid = true;
    void ByteValue(Byte value) {
        if (size >= capacity) { valid = false; return; }
        data[size++] = value;
    }
    void Bytes(std::initializer_list<Byte> values) { for (auto value : values) ByteValue(value); }
    template<class T> void Value(T value) {
        if (size + sizeof(value) > capacity) { valid = false; return; }
        std::memcpy(data + size, &value, sizeof(value)); size += sizeof(value);
    }
    void Rax(uintptr_t value) { Bytes({0x48,0xb8}); Value(uint64_t(value)); }
    void Relative(uintptr_t target) {
        const int64_t distance = int64_t(target) - int64_t(address + size + 4);
        if (distance < INT32_MIN || distance > INT32_MAX) { valid = false; return; }
        Value(int32_t(distance));
    }
};
template<size_t N> Patch MakePatch(uint32_t rva, const std::array<Byte, N>& original) {
    return {rva, {original.begin(), original.end()}, std::vector<Byte>(N, 0x90)};
}
void DescribePatches() {
    patches[0] = MakePatch(0x29a3ba, NativeData::BeforeBytes);
    patches[1] = MakePatch(0x29a48c, NativeData::AfterBytes);
    patches[2] = MakePatch(0x299683, NativeData::PeriodicBytes);
    constexpr uint32_t calls[] = {0x178651, 0x177ed7, 0x29a39c};
    for (size_t i = 0; i < 3; ++i) patches[i + 3] = MakePatch(calls[i], NativeData::CallBytes[i]);
}
bool BuildPatches() {
    auto emitter = [](Patch& patch) {
        return Code{patch.replacement.data(), patch.replacement.size(), 0, Pattern::Address(patch.rva)};
    };
    auto a = emitter(patches[0]);
    a.Bytes({0x48,0x8b,0xcf,0x8b,0xd5,0x44,0x8b,0xc6,0x44,0x8b,0xcb});
    a.Rax(reinterpret_cast<uintptr_t>(&BeforeAssign)); a.Bytes({0xff,0xd0,0x4c,0x8b,0xf8});
    auto b = emitter(patches[1]);
    b.Bytes({0x48,0x8b,0xcf,0x8b,0xd5,0x44,0x8b,0xc6,0x4d,0x8b,0xcf});
    b.Rax(reinterpret_cast<uintptr_t>(&AfterAssign)); b.Bytes({0xff,0xd0,0x83,0xf8,0xfe,0x0f,0x84});
    b.Relative(Pattern::Address(0x29a66b));
    b.Bytes({0x8b,0xf0,0x4d,0x85,0xff,0x0f,0x84}); b.Relative(Pattern::Address(0x29a62f));
    b.Bytes({0x49,0x8b,0x1f});
    auto c = emitter(patches[2]);
    c.Bytes({0x48,0x8b,0xcf}); c.Rax(reinterpret_cast<uintptr_t>(&Periodic));
    c.Bytes({0xff,0xd0,0x8b,0xd8,0x48,0xc1,0xe8,0x20,0x4c,0x8b,0xf0});
    // Leaf relays tail-call compiled helpers. The original CALL supplies their
    // return address, shadow space and alignment. No HUD DLL IAT offsets remain.
    Code r0{relay, 64, 0, reinterpret_cast<uintptr_t>(relay)};
    r0.Bytes({0x4c,0x8b,0xc7}); r0.Rax(reinterpret_cast<uintptr_t>(&PickerState)); r0.Bytes({0xff,0xe0});
    Code r1{relay + 64, 64, 0, reinterpret_cast<uintptr_t>(relay + 64)};
    r1.Bytes({0x4c,0x8b,0xcf}); r1.Rax(reinterpret_cast<uintptr_t>(&PickerGetSkill)); r1.Bytes({0xff,0xe0});
    Code r2{relay + 128, 64, 0, reinterpret_cast<uintptr_t>(relay + 128)};
    r2.Bytes({0x44,0x8b,0xcd}); r2.Rax(reinterpret_cast<uintptr_t>(&GuardGetSkill)); r2.Bytes({0xff,0xe0});
    bool valid = a.valid && b.valid && c.valid && r0.valid && r1.valid && r2.valid;
    for (size_t i = 0; i < 3; ++i) {
        auto call = emitter(patches[i + 3]);
        call.ByteValue(0xe8); call.Relative(reinterpret_cast<uintptr_t>(relay + i * 64));
        valid &= call.valid;
    }
    return valid;
}
bool AllocateRelay() {
    const uintptr_t base = Pattern::Address(0);
    SYSTEM_INFO system{}; GetSystemInfo(&system);
    const uintptr_t step = system.dwAllocationGranularity;
    if (!step) return false;
    for (uintptr_t delta = 0x4000000; delta < 0x14000000 && !relay; delta += step) {
        const uintptr_t address = (base + delta) & ~(step - 1);
        relay = static_cast<Byte*>(VirtualAlloc(reinterpret_cast<void*>(address), 0x1000,
            MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    }
    return relay != nullptr;
}
bool ApplyPatches() {
    std::array<DWORD, 6> previous{};
    size_t protectedCount = 0;
    for (; protectedCount < patches.size(); ++protectedCount) {
        const auto& patch = patches[protectedCount];
        if (!VirtualProtect(Native<void*>(patch.rva), patch.original.size(), PAGE_EXECUTE_READWRITE,
            &previous[protectedCount])) break;
    }
    const bool success = protectedCount == patches.size();
    if (success) {
        for (const auto& patch : patches) {
            std::memcpy(Native<void*>(patch.rva), patch.replacement.data(), patch.replacement.size());
            FlushInstructionCache(GetCurrentProcess(), Native<void*>(patch.rva), patch.replacement.size());
        }
    }
    // Several spans share a page. Reverse restoration preserves its first,
    // original protection rather than leaving it writable after nested calls.
    while (protectedCount) {
        const size_t i = --protectedCount; DWORD unused = 0;
        VirtualProtect(Native<void*>(patches[i].rva), patches[i].original.size(), previous[i], &unused);
    }
    return success;
}
void UpdateFlags() {
    Byte* rows = nullptr; uint64_t count = 0;
    if (!ReadMemory(Pattern::Address(NativeData::SkillRows), rows) ||
        !ReadMemory(Pattern::Address(NativeData::SkillCount), count) || !rows || !count || count > 65536) return;
    if (rows != changedRows || count != changedCount) {
        changedRows = rows; changedCount = count; changedFlags.clear(); initializedRows = false;
    }
    if (initializedRows) return;
    for (uint32_t i = 0; i < count; ++i) {
        auto row = rows + size_t(i) * Data::SkillStride;
        uint32_t flags = 0; int16_t state = -1;
        if (!ReadMemory(reinterpret_cast<uintptr_t>(row + 0x24), flags) ||
            !ReadMemory(reinterpret_cast<uintptr_t>(row + 0xa0), state)) return;
        if (Data::Eligible(flags, state) && !(flags & Data::LeftFlag)) {
            // Skill tables are native writable runtime data, shared by picker,
            // client and server. Remember only bits this module introduced.
            flags |= Data::LeftFlag;
            std::memcpy(row + 0x24, &flags, sizeof(flags));
            if (std::find(changedFlags.begin(), changedFlags.end(), i) == changedFlags.end()) changedFlags.push_back(i);
        }
    }
    initializedRows = true;
}
void RestoreFlags() {
    Byte* rows = nullptr; uint64_t count = 0;
    if (ReadMemory(Pattern::Address(NativeData::SkillRows), rows) &&
        ReadMemory(Pattern::Address(NativeData::SkillCount), count) && rows == changedRows && count == changedCount) {
        for (auto index : changedFlags) {
            auto row = rows + size_t(index) * Data::SkillStride;
            uint32_t flags = 0;
            if (ReadMemory(reinterpret_cast<uintptr_t>(row + 0x24), flags)) {
                flags &= ~Data::LeftFlag;
                std::memcpy(row + 0x24, &flags, sizeof(flags));
            }
        }
    }
    changedRows = nullptr; changedCount = 0; changedFlags.clear(); initializedRows = false;
}
}

bool Install() {
    std::lock_guard<std::mutex> guard(lifecycle);
    if (installed.load()) return true;
    if (retainedRelay) { status = "Selected aura hooks changed after installation; restart required."; return false; }
    DescribePatches();
    if (!Supported()) { status = "Selected auras: unsupported native functions."; return false; }
    for (const auto& patch : patches) if (!Equal(Pattern::Address(patch.rva), patch.original)) {
        status = "Selected auras: native patch site changed."; return false;
    }
    auto fail = []() {
        if (relay) VirtualFree(relay, 0, MEM_RELEASE);
        relay = nullptr; status = "Selected aura hook installation failed."; return false;
    };
    if (!AllocateRelay() || !BuildPatches()) return fail();
    DWORD previous = 0;
    if (!VirtualProtect(relay, 0x1000, PAGE_EXECUTE_READ, &previous)) return fail();
    FlushInstructionCache(GetCurrentProcess(), relay, 192);
    // A later hook may chain through our relay even after a partial uninstall.
    // Keep its compiled destinations valid for the game's entire lifetime.
    HMODULE containingModule = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&BeforeAssign), &containingModule)) return fail();
    if (!ApplyPatches()) return fail();
    installed = true; status = "Left-slot auras enabled; one selected aura per player.";
    UpdateFlags();
    return true;
}
void Uninstall() {
    std::lock_guard<std::mutex> guard(lifecycle);
    if (!installed.exchange(false)) return;
    bool complete = true;
    for (const auto& patch : patches) {
        if (!Equal(Pattern::Address(patch.rva), patch.replacement)) { complete = false; continue; }
        DWORD previous = 0, unused = 0;
        if (!VirtualProtect(Native<void*>(patch.rva), patch.original.size(), PAGE_EXECUTE_READWRITE, &previous)) {
            complete = false; continue;
        }
        std::memcpy(Native<void*>(patch.rva), patch.original.data(), patch.original.size());
        FlushInstructionCache(GetCurrentProcess(), Native<void*>(patch.rva), patch.original.size());
        VirtualProtect(Native<void*>(patch.rva), patch.original.size(), previous, &unused);
    }
    RestoreFlags();
    if (complete) {
        VirtualFree(relay, 0, MEM_RELEASE); relay = nullptr;
        status = "Selected auras are not installed.";
    } else {
        // Another hook can chain to our relay. Preserve its executable storage
        // rather than freeing a destination still reachable by foreign code.
        retainedRelay = true; status = "Selected aura hooks changed; relay retained until exit.";
    }
}
void Update() { if (installed.load()) { std::lock_guard<std::mutex> guard(lifecycle); if (installed.load()) UpdateFlags(); } }
bool Installed() { return installed.load(); }
const char* Status() { return status.load(); }
}
