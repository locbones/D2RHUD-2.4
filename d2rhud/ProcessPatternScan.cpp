#include "ProcessPatternScan.h"
#include "PatternScan.h"
#include <algorithm>
#include <limits>
#include <mutex>

namespace ProcessPatternScan {
namespace {
uintptr_t EndOf(uintptr_t begin, size_t size) {
    return begin + (std::min)(size, (std::numeric_limits<uintptr_t>::max)() - begin);
}
bool Readable(const MEMORY_BASIC_INFORMATION& region) {
    if (region.State != MEM_COMMIT || (region.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
    const auto access = region.Protect & 0xff;
    return access == PAGE_READONLY || access == PAGE_READWRITE || access == PAGE_WRITECOPY ||
        access == PAGE_EXECUTE_READ || access == PAGE_EXECUTE_READWRITE || access == PAGE_EXECUTE_WRITECOPY;
}
struct Scratch {
    uint8_t* data;
    explicit Scratch(size_t size) : data(static_cast<uint8_t*>(VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE))) {}
    ~Scratch() { if (data) VirtualFree(data, 0, MEM_RELEASE); }
    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;
};
bool OverlapsCompiledBytes(uintptr_t begin, size_t size, const PatternScan::Signature& signature) {
    const auto end = EndOf(begin, size);
    for (const auto& run : signature.runs) {
        const auto first = reinterpret_cast<uintptr_t>(run.bytes.data());
        if (begin < EndOf(first, run.bytes.capacity()) && first < end) return true;
    }
    return false;
}
}

uintptr_t Find(std::string_view text, uintptr_t begin, uintptr_t end, Operations operations) {
    if (begin >= end || text.empty() || !operations.query || !operations.read) return 0;
    // Process lifetime: no mutex destruction while another camera worker exits.
    static auto* scanMutex = new std::mutex;
    const std::lock_guard lock(*scanMutex);
    const auto signature = PatternScan::Compile(text);
    if (!signature.size || signature.size > MaxRegionSize) return 0;
    Scratch scratch(ChunkSize + signature.size - 1);
    if (!scratch.data) return 0;
    const auto process = GetCurrentProcess();

    for (auto address = begin; address < end;) {
        MEMORY_BASIC_INFORMATION region{};
        if (operations.query(reinterpret_cast<LPCVOID>(address), &region, sizeof(region)) != sizeof(region)) break;
        const auto base = reinterpret_cast<uintptr_t>(region.BaseAddress);
        const auto next = (std::min)(end, EndOf(base, region.RegionSize));
        if (base > address || next <= address) break;
        if (Readable(region) && region.RegionSize <= MaxRegionSize &&
            region.AllocationBase != scratch.data && next - address >= signature.size) {
            size_t overlap = 0;
            for (auto cursor = address; cursor < next;) {
                const auto count = (std::min)(ChunkSize, size_t(next - cursor));
                SIZE_T received = 0;
                // A query is only a hint: a different thread can free/protect the
                // allocation before this read. Never dereference the source.
                const bool read = operations.read(process, reinterpret_cast<LPCVOID>(cursor),
                    scratch.data + overlap, count, &received) != FALSE;
                if (!read || received != count) {
                    overlap = 0;
                    cursor += count;
                    continue;
                }
                const auto valid = overlap + count;
                const auto source = cursor - overlap;
                size_t search = 0;
                while (search < valid) {
                    const auto offset = PatternScan::Find(scratch.data + search, valid - search, signature);
                    if (offset == PatternScan::NotFound) break;
                    const auto candidate = source + search + offset;
                    if (!OverlapsCompiledBytes(candidate, signature.size, signature)) return candidate;
                    search += offset + 1;
                }
                cursor += count;
                overlap = (std::min)(signature.size - 1, valid);
                if (overlap) std::memmove(scratch.data, scratch.data + valid - overlap, overlap);
            }
        }
        address = next;
    }
    return 0;
}

uintptr_t FindProcess(std::string_view signature) {
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    return Find(signature, reinterpret_cast<uintptr_t>(info.lpMinimumApplicationAddress),
        reinterpret_cast<uintptr_t>(info.lpMaximumApplicationAddress));
}
}
