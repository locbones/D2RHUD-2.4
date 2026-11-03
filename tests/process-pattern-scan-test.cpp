#include "../d2rhud/Pattern.h"
#include "../d2rhud/ProcessPatternScan.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
int checks = 0;
void Check(bool condition, const char* message) { ++checks; if (!condition) throw std::runtime_error(message); }
struct Arena {
    uint8_t* data;
    size_t size;
    explicit Arena(size_t bytes) : data(static_cast<uint8_t*>(VirtualAlloc(nullptr, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE))), size(bytes) {
        if (!data) throw std::runtime_error("allocation");
    }
    ~Arena() { VirtualFree(data, 0, MEM_RELEASE); }
    uintptr_t At(size_t offset = 0) const { return reinterpret_cast<uintptr_t>(data) + offset; }
    uintptr_t Find(const char* text, ProcessPatternScan::Operations operations = {}) const {
        return ProcessPatternScan::Find(text, At(), At(size), operations);
    }
};
enum class Fault { None, Fail, Partial, ShortSuccess, Protect };
Fault fault = Fault::None;
int readCalls = 0;
uintptr_t faultAddress = 0;
BOOL WINAPI Read(HANDLE process, LPCVOID source, LPVOID destination, SIZE_T size, SIZE_T* received) {
    ++readCalls;
    if (reinterpret_cast<uintptr_t>(source) == faultAddress) {
        if (fault == Fault::Fail) { *received = 0; return FALSE; }
        if (fault == Fault::Partial || fault == Fault::ShortSuccess) {
            ReadProcessMemory(process, source, destination, size / 2, received);
            return fault == Fault::ShortSuccess ? TRUE : FALSE;
        }
        if (fault == Fault::Protect) {
            DWORD previous = 0, ignored = 0;
            VirtualProtect(const_cast<LPVOID>(source), size, PAGE_NOACCESS, &previous);
            const auto result = ReadProcessMemory(process, source, destination, size, received);
            VirtualProtect(const_cast<LPVOID>(source), size, previous, &ignored);
            return result;
        }
    }
    return ReadProcessMemory(process, source, destination, size, received);
}
SIZE_T WINAPI BrokenQuery(LPCVOID address, PMEMORY_BASIC_INFORMATION info, SIZE_T) {
    *info = {}; info->BaseAddress = const_cast<LPVOID>(address); return sizeof(*info);
}
bool scratchRead = false;
BOOL WINAPI WatchRead(HANDLE process, LPCVOID source, LPVOID destination, SIZE_T size, SIZE_T* received) {
    MEMORY_BASIC_INFORMATION buffer{}, input{};
    VirtualQuery(destination, &buffer, sizeof(buffer)); VirtualQuery(source, &input, sizeof(input));
    if (buffer.AllocationBase == input.AllocationBase) scratchRead = true;
    return ReadProcessMemory(process, source, destination, size, received);
}
void ScannerTests() {
    using namespace ProcessPatternScan;
    Arena arena(ChunkSize * 3);
    arena.data[12] = 0xaa; arena.data[13] = 0x7f; arena.data[14] = 0xbb;
    arena.data[30] = 0xaa; arena.data[31] = 0x3f; arena.data[32] = 0xbb;
    Check(arena.Find("AA 3F BB") == arena.At(30), "3F is literal, not wildcard");
    Check(arena.Find("AA ? BB") == arena.At(12), "single wildcard");
    Check(arena.Find("AA ?? BB") == arena.At(12), "double wildcard");
    Check(arena.Find("aa3fbb") == arena.At(30), "contiguous lowercase signature");
    Check(arena.Find("? ? ?") == arena.At(), "all wildcards");
    Check(arena.Find("") == 0 && arena.Find("GG") == 0 && arena.Find("A") == 0, "empty/malformed signatures");
    Check(Pattern::ScanProcess(nullptr) == 0 && Pattern::ScanProcess("") == 0, "public null/empty signature");
    Check(Find("AA", arena.At(1), arena.At()) == 0, "reversed range");
    Check(Find("AA", arena.At(), arena.At()) == 0, "empty range");
    Check(Find("AA", arena.At(), arena.At(arena.size), {nullptr, &ReadProcessMemory}) == 0, "missing query callback");
    Check(Find("AA", arena.At(), arena.At(arena.size), {&VirtualQuery, nullptr}) == 0, "missing read callback");
    Check(Find("AA", arena.At(), arena.At(arena.size), {&BrokenQuery, &ReadProcessMemory}) == 0, "query without progress stops");
    Check(Find("AA 3F BB", arena.At(31), arena.At(33)) == 0, "clipped range cannot overread");
    Check(Find("AA 3F BB", arena.At(30), arena.At(33)) == arena.At(30), "exact range length");

    constexpr std::array<uint8_t, 8> marker{0xc1,0x3f,0x52,0xe8,0x90,0xd3,0x81,0x26};
    for (size_t split = 1; split < marker.size(); ++split) {
        std::memset(arena.data, 0, arena.size);
        const auto offset = ChunkSize - split;
        std::memcpy(arena.data + offset, marker.data(), marker.size());
        Check(arena.Find("C1 3F 52 E8 90 D3 81 26") == arena.At(offset), "match spans chunk boundary");
    }
    std::memset(arena.data, 0, arena.size);
    std::memcpy(arena.data + arena.size - marker.size(), marker.data(), marker.size());
    Check(arena.Find("C1 3F 52 E8 90 D3 81 26") == arena.At(arena.size - marker.size()), "last legal match");
    Check(arena.Find("C1 3F 52 E8 90 D3 81 27") == 0, "missing match");

    // Use the exact angle signature with an earlier false candidate that the
    // old scanner accepted because its fifth byte was treated as a wildcard.
    const std::array<uint8_t,12> camera{0x44,0x74,0x64,0xbf,0x3f,0xdb,0x74,0x3e,0xf4,0x41,0xbd,0x3e};
    std::memset(arena.data, 0, arena.size);
    std::memcpy(arena.data + 10, camera.data(), camera.size()); arena.data[14] = 0x42;
    std::memcpy(arena.data + 100, camera.data(), camera.size());
    Check(arena.Find("44 74 64 BF 3F DB 74 3E F4 41 BD 3E") == arena.At(100), "real camera signature rejects wrong float byte");

    for (const auto mode : {Fault::Fail, Fault::Partial, Fault::ShortSuccess, Fault::Protect}) {
        std::memset(arena.data, 0, arena.size);
        std::memcpy(arena.data + 10, marker.data(), marker.size());
        std::memcpy(arena.data + ChunkSize * 2 + 50, marker.data(), marker.size());
        fault = mode; faultAddress = arena.At(); readCalls = 0;
        Check(arena.Find("C1 3F 52 E8 90 D3 81 26", {&VirtualQuery, &Read}) == arena.At(ChunkSize * 2 + 50), "failed/partial/racing read skipped; later chunks searched");
        Check(readCalls == 3, "failed read does not busy retry");
    }
    // An incomplete read must not leave overlap that creates a match across a gap.
    std::memset(arena.data, 0, arena.size);
    std::memcpy(arena.data + ChunkSize - 4, marker.data(), 4);
    std::memcpy(arena.data + ChunkSize * 2, marker.data() + 4, 4);
    fault = Fault::Partial; faultAddress = arena.At(ChunkSize);
    Check(arena.Find("C1 3F 52 E8 90 D3 81 26", {&VirtualQuery, &Read}) == 0, "failed chunk clears overlap");
    fault = Fault::None;

    SYSTEM_INFO info{}; GetSystemInfo(&info);
    Arena pages(info.dwPageSize * 3);
    std::memcpy(pages.data + 20, marker.data(), marker.size());
    std::memcpy(pages.data + info.dwPageSize * 2 + 20, marker.data(), marker.size());
    for (const auto protection : {PAGE_NOACCESS, PAGE_READWRITE | PAGE_GUARD, PAGE_EXECUTE}) {
        DWORD previous = 0, ignored = 0;
        Check(VirtualProtect(pages.data, info.dwPageSize, protection, &previous) != 0, "protect fixture page");
        Check(pages.Find("C1 3F 52 E8 90 D3 81 26") == pages.At(info.dwPageSize * 2 + 20), "unreadable/guard/execute-only page skipped");
        VirtualProtect(pages.data, info.dwPageSize, previous, &ignored);
    }
    DWORD previous = 0;
    Check(VirtualProtect(pages.data, info.dwPageSize, PAGE_READONLY, &previous) != 0, "readonly fixture");
    Check(pages.Find("C1 3F 52 E8 90 D3 81 26") == pages.At(20), "readonly memory is searchable");

    Arena large(MaxRegionSize + info.dwPageSize);
    std::memcpy(large.data, marker.data(), marker.size());
    Check(large.Find("C1 3F 52 E8 90 D3 81 26") == 0, "existing large-region limit preserved");

    scratchRead = false;
    // Only ASCII hex text exists before compilation, so the scanner's own
    // compiled literal and copied scratch data must not become its result.
    Check(Find("A7 39 D2 85 16 EB 4C 90 63 F8 2D B1 05 7A C6 48 9E 31 54 DF 82 0B 75 E3 29 C0 6A 97 14 BD 58 F2",
        reinterpret_cast<uintptr_t>(info.lpMinimumApplicationAddress), reinterpret_cast<uintptr_t>(info.lpMaximumApplicationAddress),
        {&VirtualQuery, &WatchRead}) == 0, "full process scan rejects its own compiled signature");
    Check(!scratchRead, "scratch allocation is never scanned");

    Arena other(ChunkSize * 2);
    std::memcpy(other.data + ChunkSize - 3, marker.data(), marker.size());
    std::memset(arena.data, 0, arena.size);
    std::memcpy(arena.data + ChunkSize - 4, camera.data(), camera.size());
    std::atomic<bool> okay{true};
    std::array<std::thread, 4> workers;
    for (auto& worker : workers) worker = std::thread([&] {
        for (int i = 0; i < 100; ++i) {
            if (arena.Find("44 74 64 BF 3F DB 74 3E F4 41 BD 3E") != arena.At(ChunkSize - 4) ||
                other.Find("C1 3F 52 E8 90 D3 81 26") != other.At(ChunkSize - 3) ||
                other.Find("44 74 64 BF 3F DB 74 3E F4 41 BD 3E") != 0) okay = false;
        }
    });
    for (auto& worker : workers) worker.join();
    Check(okay, "concurrent scans remain independent");
}
void ModuleTests() {
    const auto kernel = reinterpret_cast<uintptr_t>(GetModuleHandleW(L"kernel32.dll"));
    wchar_t reused[128] = L"kernel32.dll";
    Check(Pattern::BaseAddress(reused) == kernel, "named module base");
    wcscpy_s(reused, L"not-a-real-pattern-test-module.dll");
    Check(Pattern::BaseAddress(reused) == 0, "same name pointer with changed contents cannot return cached module");
    wcscpy_s(reused, L"kernel32.dll");
    Check(Pattern::BaseAddress(reused) == kernel, "missing module lookup does not poison later lookup");
    Check(Pattern::BaseAddress(nullptr) == reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)), "main image cache");
    Check(Pattern::Address(123) == Pattern::BaseAddress(nullptr) + 123, "main RVA unchanged");
    std::atomic<bool> okay{true}; std::array<std::thread, 8> workers;
    for (auto& worker : workers) worker = std::thread([&] {
        for (int i = 0; i < 10000; ++i)
            if (Pattern::BaseAddress(nullptr) != reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) ||
                Pattern::BaseAddress(L"kernel32.dll") != kernel) okay = false;
    });
    for (auto& worker : workers) worker.join();
    Check(okay, "concurrent module lookups");
}
__declspec(noinline) size_t LegacyFind(const uint8_t* data, size_t size, const std::array<uint8_t,12>& marker) {
    for (size_t i = 0; i + marker.size() <= size; ++i) {
        bool found = true;
        for (size_t j = 0; j < marker.size(); ++j) if (marker[j] != 0x3f && marker[j] != data[i+j]) { found = false; break; }
        if (found) return i;
    }
    return size;
}
void Benchmark() {
    Arena arena(16 * 1024 * 1024);
    const std::array<uint8_t,12> marker{0x44,0x74,0x64,0xbf,0x3f,0xdb,0x74,0x3e,0xf4,0x41,0xbd,0x3e};
    const auto at = arena.size - marker.size(); std::memcpy(arena.data + at, marker.data(), marker.size());
    std::array<double,5> oldTimes{}, newTimes{};
    for (size_t i = 0; i < oldTimes.size(); ++i) {
        const auto first = std::chrono::steady_clock::now();
        const auto old = LegacyFind(arena.data, arena.size, marker);
        const auto second = std::chrono::steady_clock::now();
        const auto current = arena.Find("44 74 64 BF 3F DB 74 3E F4 41 BD 3E");
        const auto third = std::chrono::steady_clock::now();
        Check(old == at && current == arena.At(at), "benchmark results agree");
        oldTimes[i] = std::chrono::duration<double,std::milli>(second-first).count();
        newTimes[i] = std::chrono::duration<double,std::milli>(third-second).count();
    }
    std::sort(oldTimes.begin(), oldTimes.end()); std::sort(newTimes.begin(), newTimes.end());
    std::cout << "16 MiB fixture median: legacy matcher " << oldTimes[2] << " ms; checked scanner " << newTimes[2]
        << " ms (includes native memory reads). Not a game/FPS measurement.\n";
}
}
int main() {
    try { ScannerTests(); ModuleTests(); Benchmark(); std::cout << "PASS: " << checks << " process scanner checks. Original game/files unused.\n"; return 0; }
    catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}

