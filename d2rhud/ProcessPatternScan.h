#pragma once
#include <Windows.h>
#include <cstdint>
#include <string_view>

namespace ProcessPatternScan {
inline constexpr size_t ChunkSize = 256 * 1024;
inline constexpr size_t MaxRegionSize = 64 * 1024 * 1024;
struct Operations {
    decltype(&VirtualQuery) query = &VirtualQuery;
    decltype(&ReadProcessMemory) read = &ReadProcessMemory;
};
// Scans [begin, end). Unreadable, changing, and oversized regions are skipped.
// Calls are serialized so one scanner cannot find another scanner's scratch data.
uintptr_t Find(std::string_view signature, uintptr_t begin, uintptr_t end, Operations operations = {});
uintptr_t FindProcess(std::string_view signature);
}
