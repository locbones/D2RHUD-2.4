// This helper runs as a Windows process, including inside Wine/Proton.
// It creates its own D2R.exe and never attaches to existing game processes.
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <tlhelp32.h>
#include <string>
#include <iostream>
#include "MemoryEdits.h"
#include "StatesStartupGate.h"

static std::wstring Quote(const std::wstring& value) {
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (wchar_t c : value) {
        if (c == L'\\') { ++slashes; continue; }
        result.append(slashes * (c == L'"' ? 2 : 1), L'\\');
        slashes = 0;
        if (c == L'"') result += L'\\';
        result += c;
    }
    result.append(slashes * 2, L'\\');
    return result + L"\"";
}

static uintptr_t ModuleBase(DWORD pid, const wchar_t* name, DWORD* imageSize = nullptr) {
    HANDLE snapshot = StatesStartup::ModuleSnapshot(pid);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;
    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    uintptr_t address = 0;
    if (Module32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szModule, name) == 0) {
                address = reinterpret_cast<uintptr_t>(entry.modBaseAddr);
                if (imageSize) *imageSize = entry.modBaseSize;
                break;
            }
        } while (Module32NextW(snapshot, &entry));
    }
    const DWORD error = address ? ERROR_SUCCESS : ERROR_MOD_NOT_FOUND;
    CloseHandle(snapshot);
    SetLastError(error);
    return address;
}

int wmain(int argc, wchar_t** argv) {
    MemoryPlan memoryPlan;
    const bool hasMemoryPlan = argc > 1 && wcscmp(argv[1], L"--pandemonium-memory-stdin") == 0;
    if (hasMemoryPlan) {
        try { memoryPlan = ReadMemoryPlan(); }
        catch (const std::exception& error) {
            std::cout << "PANDEMONIUM_HUD_ERROR: " << error.what() << std::endl;
            return 1;
        }
    }
    wchar_t directory[32768]{};
    DWORD length = GetCurrentDirectoryW(32768, directory);
    if (!length || length >= 32768) return 1;
    const std::wstring game = std::wstring(directory) + L"\\D2R.exe";
    const std::wstring dll = std::wstring(directory) + L"\\d2rhud.dll";
    if (GetFileAttributesW(dll.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::cout << "PANDEMONIUM_HUD_ERROR: d2rhud.dll is missing." << std::endl;
        return 1;
    }
    const DWORD statesReadyRva = StatesStartup::ReadyExportRva(dll.c_str());
    std::wstring command = Quote(game);
    for (int i = hasMemoryPlan ? 2 : 1; i < argc; ++i) command += L" " + Quote(argv[i]);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(game.c_str(), command.data(), nullptr, nullptr, FALSE,
        CREATE_UNICODE_ENVIRONMENT | (statesReadyRva ? CREATE_SUSPENDED : 0), nullptr, directory, &startup, &process)) {
        std::cout << "PANDEMONIUM_HUD_ERROR: Could not start D2R.exe. Windows error " << GetLastError() << std::endl;
        return 1;
    }
    const ULONGLONG started = GetTickCount64();
    auto progress = [&](const char* message) {
        std::cout << "PANDEMONIUM_HUD_PROGRESS: " << message << " ("
            << (GetTickCount64() - started) << " ms after game creation)." << std::endl;
    };
    std::cout << "PANDEMONIUM_HUD_PROGRESS: Created game process " << process.dwProcessId << "." << std::endl;
    void* allocation = nullptr;
    HANDLE thread = nullptr;
    HANDLE stateThread = nullptr;
    auto fail = [&](const char* message) {
        DWORD error = GetLastError();
        // Only the new child belongs to us. Do not leave it running without its required HUD.
        TerminateProcess(process.hProcess, 1);
        WaitForSingleObject(process.hProcess, 5000);
        if (thread) CloseHandle(thread);
        if (stateThread) CloseHandle(stateThread);
        if (process.hThread) CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        std::cout << "PANDEMONIUM_HUD_ERROR: " << message << " Windows error " << error << std::endl;
        return 1;
    };
    // New HUD builds need their state hooks before the table limit assertion.
    // Pause at that exact point, after mod globals exist, rather than racing a
    // grace-period timer. Older builds retain their established startup path.
    uintptr_t statesBase = 0;
    uint64_t stateRows = 0;
    if (statesReadyRva) {
        statesBase = StatesStartup::ImageBase(process.hProcess);
        if (!statesBase || !StatesStartup::Arm(process.hProcess, statesBase))
            return fail("Extended-state startup does not support this executable; no game code was resumed.");
        if (ResumeThread(process.hThread) == DWORD(-1)) return fail("Cannot start the game initialization thread.");
        progress("Waiting for mod globals at the state-table loading gate");
        if (!StatesStartup::HoldAtGate(process.hProcess, process.hThread, statesBase, 120000, &stateThread))
            return fail("Cannot hold state-table loading for HUD initialization.");
        if (!StatesStartup::Read(process.hProcess, statesBase + 0x1c9ebf8, stateRows) || !stateRows || stateRows > 512){
            SetLastError(ERROR_INVALID_DATA);
            return fail("The state table exceeds 512 rows or could not be read.");
        }
        std::cout << "PANDEMONIUM_HUD_PROGRESS: Loaded state table has " << stateRows
            << " rows; " << (stateRows <= 255 ? "native compatibility" : "extended-state") << " mode selected." << std::endl;
        progress("State-table loading held until HUD hooks are ready");
    }
    // D2RHUD snapshots game globals (including the mod name) during DLL loading.
    // Input-idle can signal before those globals are initialized. Match Pandemonium's
    // unconditional 1500 ms startup grace period instead of using it as readiness.
    if (!statesReadyRva) {
        progress("Waiting for the 1500 ms Pandemonium startup grace period");
        if (WaitForSingleObject(process.hProcess, 1500) != WAIT_TIMEOUT)
            return fail("The game exited before D2RHUD startup.");
        progress("Startup grace period completed");
    }
    auto loadLibrary = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
    HMODULE owner = nullptr;
    if (!loadLibrary || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(loadLibrary), &owner))
        return fail("Cannot resolve LoadLibraryW.");
    wchar_t ownerPath[32768]{};
    if (!GetModuleFileNameW(owner, ownerPath, 32768)) return fail("Cannot resolve loader module.");
    const wchar_t* ownerName = wcsrchr(ownerPath, L'\\');
    ownerName = ownerName ? ownerName + 1 : ownerPath;
    uintptr_t remoteBase = 0;
    for (int attempt = 0; attempt < 100 && !remoteBase; ++attempt) {
        if (WaitForSingleObject(process.hProcess, 0) == WAIT_OBJECT_0) return fail("The game exited during startup.");
        remoteBase = ModuleBase(process.dwProcessId, ownerName);
        if (!remoteBase) Sleep(100);
    }
    if (!remoteBase) return fail("Cannot find the Windows loader in the game.");
    auto remoteLoad = reinterpret_cast<LPTHREAD_START_ROUTINE>(
        remoteBase + reinterpret_cast<uintptr_t>(loadLibrary) - reinterpret_cast<uintptr_t>(owner));
    SIZE_T bytes = (dll.size() + 1) * sizeof(wchar_t), written = 0;
    allocation = VirtualAllocEx(process.hProcess, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!allocation || !WriteProcessMemory(process.hProcess, allocation, dll.c_str(), bytes, &written) || written != bytes)
        return fail("Cannot prepare D2RHUD loading.");
    progress("Loading D2RHUD into the new game process");
    thread = CreateRemoteThread(process.hProcess, nullptr, 0, remoteLoad, allocation, 0, nullptr);
    if (!thread) return fail("Cannot start D2RHUD loading.");
    if (WaitForSingleObject(thread, 30000) != WAIT_OBJECT_0) return fail("D2RHUD loading timed out.");
    // Enumerate modules instead of relying on the truncated 32-bit thread return value.
    const auto hudBase = ModuleBase(process.dwProcessId, L"d2rhud.dll");
    if (!hudBase) return fail("D2RHUD failed to load. Check its runtime dependencies.");
    if (statesReadyRva) {
        if (!StatesStartup::WaitReady(process.hProcess, hudBase, statesReadyRva))
            return fail("HUD rejected the extended-state hooks or did not finish initializing; state-table loading was stopped safely.");
        uint32_t limit = 0;
        if (!StatesStartup::Read(process.hProcess, statesBase + 0x1dc27a, limit) ||
            (stateRows <= 255 ? limit != 256 && limit != 513 : limit != 513)){
            SetLastError(ERROR_INVALID_DATA);return fail("The state-table loader limit was not verified.");
        }
        if (ResumeThread(stateThread) == DWORD(-1)) return fail("Cannot resume state-table loading.");
        CloseHandle(stateThread);stateThread=nullptr;
        progress("HUD hooks and state-table limit verified; state-table loading resumed");
    }
    CloseHandle(process.hThread);
    process.hThread = nullptr;
    progress("D2RHUD DLL loaded; its graphics hooks initialize asynchronously");
    VirtualFreeEx(process.hProcess, allocation, 0, MEM_RELEASE);
    CloseHandle(thread);
    thread = nullptr;
    if (hasMemoryPlan) {
        DWORD imageSize = 0;
        auto base = ModuleBase(process.dwProcessId, L"D2R.exe", &imageSize);
        if (!ApplyMemoryPlan(process.hProcess, base, imageSize, memoryPlan))
            return fail("Memory edits failed; the newly launched game was stopped.");
    }
    CloseHandle(process.hProcess);
    std::cout << "PANDEMONIUM_HUD_READY" << std::endl;
    return 0;
}
