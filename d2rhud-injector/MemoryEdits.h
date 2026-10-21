#pragma once
#include <windows.h>
#include <cstdint>
#include <vector>
#include <string>
#include <iostream>
#include <stdexcept>
#include <io.h>
#include <fcntl.h>

struct MemoryPatch {
    uint64_t offset;
    std::vector<unsigned char> bytes;
    std::string name;
};
struct MemoryPlan {
    std::vector<MemoryPatch> patches;
    uint32_t skipped = 0;
};
template<typename T> static T ReadPlanValue() {
    T value{};
    if (!std::cin.read(reinterpret_cast<char*>(&value), sizeof(value)))
        throw std::runtime_error("Incomplete memory edit plan.");
    return value;
}
static MemoryPlan ReadMemoryPlan() {
    _setmode(_fileno(stdin), _O_BINARY);
    if (ReadPlanValue<uint32_t>() != 0x314D3244)
        throw std::runtime_error("Unsupported memory edit plan.");
    auto count = ReadPlanValue<uint32_t>();
    MemoryPlan plan;
    plan.skipped = ReadPlanValue<uint32_t>();
    if (count > 10000) throw std::runtime_error("Memory edit plan is too large.");
    for (uint32_t i = 0; i < count; ++i) {
        MemoryPatch patch{};
        patch.offset = ReadPlanValue<uint64_t>();
        auto length = ReadPlanValue<uint32_t>();
        if (!length || length > 4096) throw std::runtime_error("Invalid memory edit size.");
        patch.bytes.resize(length);
        if (!std::cin.read(reinterpret_cast<char*>(patch.bytes.data()), length))
            throw std::runtime_error("Incomplete memory edit bytes.");
        auto nameLength = ReadPlanValue<uint32_t>();
        if (nameLength > 1024) throw std::runtime_error("Memory edit label is too long.");
        patch.name.resize(nameLength);
        if (!std::cin.read(patch.name.data(), nameLength)) throw std::runtime_error("Incomplete memory edit name.");
        for (auto& c : patch.name) if (c == '\r' || c == '\n') c = ' ';
        plan.patches.push_back(std::move(patch));
    }
    return plan;
}

// Each protection region is restored independently, including writes spanning page boundaries.
static bool WriteVerified(HANDLE process, uintptr_t address, const std::vector<unsigned char>& bytes) {
    struct Region { LPVOID address; SIZE_T length; DWORD protection; };
    std::vector<Region> changed;
    SIZE_T remaining = bytes.size();
    uintptr_t cursor = address;
    bool ok = true;
    while (remaining) {
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQueryEx(process, reinterpret_cast<LPCVOID>(cursor), &info, sizeof(info)) ||
            info.State != MEM_COMMIT) { ok = false; break; }
        auto available = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize - cursor;
        SIZE_T length = remaining < available ? remaining : available;
        if (!length) { ok = false; break; }
        DWORD previous = 0;
        if (!VirtualProtectEx(process, reinterpret_cast<LPVOID>(cursor), length, PAGE_EXECUTE_READWRITE, &previous)) {
            ok = false; break;
        }
        changed.push_back({ reinterpret_cast<LPVOID>(cursor), length, previous });
        cursor += length;
        remaining -= length;
    }
    if (ok) {
        SIZE_T written = 0;
        ok = WriteProcessMemory(process, reinterpret_cast<LPVOID>(address), bytes.data(), bytes.size(), &written) &&
            written == bytes.size();
        if (ok) ok = FlushInstructionCache(process, reinterpret_cast<LPCVOID>(address), bytes.size()) != FALSE;
        std::vector<unsigned char> verified(bytes.size());
        SIZE_T read = 0;
        if (ok) ok = ReadProcessMemory(process, reinterpret_cast<LPCVOID>(address), verified.data(), verified.size(), &read) &&
            read == verified.size() && verified == bytes;
    }
    for (auto it = changed.rbegin(); it != changed.rend(); ++it) {
        DWORD ignored = 0;
        if (!VirtualProtectEx(process, it->address, it->length, it->protection, &ignored)) ok = false;
    }
    return ok;
}

static bool ApplyMemoryPlan(HANDLE process, uintptr_t base, DWORD imageSize, const MemoryPlan& plan) {
    unsigned applied = 0, already = 0, failed = 0;
    std::cout << "PANDEMONIUM_HUD_PROGRESS: Applying memory edits to the new game process." << std::endl;
    for (const auto& patch : plan.patches) {
        const char* result = "FAILED";
        const char* reason = "outside the game module";
        if (base && patch.offset < imageSize && patch.bytes.size() <= imageSize - patch.offset) {
            auto address = base + static_cast<uintptr_t>(patch.offset);
            std::vector<unsigned char> current(patch.bytes.size());
            SIZE_T read = 0;
            reason = "target memory could not be read";
            if (ReadProcessMemory(process, reinterpret_cast<LPCVOID>(address), current.data(), current.size(), &read) &&
                read == current.size()) {
                if (current == patch.bytes) { result = "already set"; ++already; }
                else {
                    reason = "write, read-back verification, or protection restoration failed";
                    if (WriteVerified(process, address, patch.bytes)) { result = "applied and verified"; ++applied; }
                }
            }
        }
        bool failure = std::string(result) == "FAILED";
        if (failure) ++failed;
        std::cout << "PANDEMONIUM_HUD_PROGRESS: " << (failure ? "ERROR: " : "") << "Memory edit " << result
            << ": " << patch.name << " [D2R.exe+0x" << std::hex << patch.offset << std::dec
            << ", " << patch.bytes.size() << " bytes]";
        if (failure) std::cout << ": " << reason;
        std::cout << "." << std::endl;
    }
    std::cout << "PANDEMONIUM_HUD_PROGRESS: Memory edits complete: " << applied << " applied, " << already
        << " already set, " << plan.skipped << " config entries skipped, " << failed << " failed." << std::endl;
    return failed == 0;
}
