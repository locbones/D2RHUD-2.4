#include "ItemCodes.h"
#include "ItemCodesData.h"
#include "../Pattern.h"
#include <windows.h>
#include <tlhelp32.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>

namespace ItemCodes {
namespace {
std::atomic<bool> installed{false};
bool attempted = false;
const char* status = "Native four-character HD item codes are not installed.";
bool Read(uintptr_t address, void* out, size_t size) {
    SIZE_T count = 0;
    return address && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address), out, size, &count) && count == size;
}
template<class Address> bool Supported(Address address) {
    std::array<uint8_t, 0x56e> code{};
    for (const auto& signature : Data::Signatures) {
        if (!Read(address(signature.rva), code.data(), signature.size) || Data::Hash({code.data(), signature.size}) != signature.hash) return false;
    }
    return true;
}
// Prepare handles before suspension. No heap allocations or native game calls
// occur while paused, and a thread inside the modified instructions is retried.
struct Threads {
    std::array<HANDLE, 512> handles{};
    std::array<bool, 512> paused{};
    size_t count = 0;
    ~Threads() {
        for (size_t i = 0; i < count; ++i) if (paused[i]) ResumeThread(handles[i]);
        for (size_t i = 0; i < count; ++i) CloseHandle(handles[i]);
    }
    bool Pause(uintptr_t begin, uintptr_t end) {
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snapshot == INVALID_HANDLE_VALUE) return false;
        THREADENTRY32 entry{}; entry.dwSize = sizeof(entry);
        BOOL more = Thread32First(snapshot, &entry); bool ok = more != FALSE;
        while (more) {
            if (entry.th32OwnerProcessID == GetCurrentProcessId() && entry.th32ThreadID != GetCurrentThreadId()) {
                if (count == handles.size()) { ok = false; break; }
                HANDLE handle = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, entry.th32ThreadID);
                if (handle) handles[count++] = handle;
                else if (GetLastError() != ERROR_INVALID_PARAMETER) { ok = false; break; }
            }
            more = Thread32Next(snapshot, &entry);
        }
        CloseHandle(snapshot);
        if (!ok) return false;
        for (size_t i = 0; i < count; ++i) {
            if (SuspendThread(handles[i]) == static_cast<DWORD>(-1)) {
                DWORD code = STILL_ACTIVE;
                if (!GetExitCodeThread(handles[i], &code) || code == STILL_ACTIVE) return false;
            } else paused[i] = true;
        }
        for (size_t i = 0; i < count; ++i) if (paused[i]) {
            CONTEXT context{}; context.ContextFlags = CONTEXT_CONTROL;
            if (!GetThreadContext(handles[i], &context) || (context.Rip >= begin && context.Rip < end)) return false;
        }
        return true;
    }
};
bool Write(uintptr_t address, std::span<const uint8_t> expected, std::span<const uint8_t> replacement) {
    std::array<uint8_t, Data::PackSize> bytes{};
    if (expected.size() != bytes.size() || replacement.size() != bytes.size() || !Read(address, bytes.data(), bytes.size()) ||
        !std::equal(bytes.begin(), bytes.end(), expected.begin())) return false;
    DWORD old = 0;
    auto target = reinterpret_cast<void*>(address);
    if (!VirtualProtect(target, bytes.size(), PAGE_EXECUTE_READWRITE, &old)) return false;
    memcpy(target, replacement.data(), replacement.size());
    FlushInstructionCache(GetCurrentProcess(), target, replacement.size());
    DWORD ignored = 0; VirtualProtect(target, bytes.size(), old, &ignored);
    return true;
}
enum class Result { Retry, Unsupported, Late, Success };
Result Apply() {
    const uintptr_t address = Pattern::Address(Data::PackRva);
    Threads threads;
    if (!threads.Pause(address, address + Data::PackSize)) return Result::Retry;
    if (!Supported([](uint32_t rva) { return Pattern::Address(rva); })) return Result::Unsupported;
    // Already-loaded entries have lost their fourth byte. Never mutate/reload
    // a live native asset vector from the injector thread; restart to load it.
    uint64_t count = 0;
    if (!Read(Pattern::Address(Data::ManagerRva) + 0xe0, &count, sizeof(count))) return Result::Unsupported;
    if (count) return Result::Late;
    return Write(address, Data::Original, Data::Replacement) ? Result::Success : Result::Retry;
}
}
bool Install() {
    if (attempted) return installed.load();
    attempted = true;
    Result result = Result::Retry;
    for (int retry = 0; retry < 3 && result == Result::Retry; ++retry) result = Apply();
    if (result == Result::Success) { installed.store(true); status = "Native four-character HD item codes are active."; }
    else if (result == Result::Late) status = "HD item mappings already loaded; restart with HUD loaded at startup for native four-character codes.";
    else if (result == Result::Unsupported) status = "Native four-character HD item codes unavailable: executable signature mismatch.";
    else status = "Native four-character HD item codes could not be installed safely.";
    OutputDebugStringA(status);
    return installed.load();
}
bool Installed() { return installed.load(); }
const char* Status() { return status; }
void Uninstall() {
    if (!installed.load()) return;
    const uintptr_t address = Pattern::Address(Data::PackRva);
    Threads threads;
    if (threads.Pause(address, address + Data::PackSize) && Write(address, Data::Replacement, Data::Original)) installed.store(false);
    // The replacement itself contains no HUD addresses, calls or allocations;
    // leaving it in place is safe even if an explicit unload cannot restore it.
}
}
