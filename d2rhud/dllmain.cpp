#include <Windows.h>
#include "D3D12Hook.h"
#include <utility>
#include <fstream>
#include <iostream>
#include <format>
#include <sstream>
#include <stacktrace>
#include "Logging.h"
#include "d2/D2Ptrs.h"

LONG WINAPI ExceptionHandler(PEXCEPTION_POINTERS pExceptionInfo);

DWORD WINAPI CreateConsole(LPVOID lParam) {
#ifndef NDEBUG
    AllocConsole();
    AttachConsole(GetCurrentProcessId());
    freopen_s(reinterpret_cast<FILE**>(stdin), "CONIN$", "r", stdin);
    freopen_s(reinterpret_cast<FILE**>(stdout), "CONOUT$", "w", stdout);
    freopen_s(reinterpret_cast<FILE**>(stderr), "CONOUT$", "w", stderr);
#endif
    return 0;
}

DWORD WINAPI AttachThread(LPVOID lParam) {
    D2Ptrs::Initialize();
    if (D3D12::Init() == D3D12::Status::Success) {
        D3D12::InstallHooks();
    }
    return 0;
}

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH: {
        CreateConsole(hModule);
        DisableThreadLibraryCalls(hModule);
        SetUnhandledExceptionFilter(ExceptionHandler);
        CreateThread(nullptr, 0, &AttachThread, static_cast<LPVOID>(hModule), 0, nullptr);
        break;
    }
    case DLL_PROCESS_DETACH: {
        D3D12::RemoveHooks();
        FreeConsole();
        break;
    }
    }
    return TRUE;
}

LONG WINAPI ExceptionHandler(PEXCEPTION_POINTERS pExceptionInfo) {
    std::stringstream ss;
    ss << std::endl;
    auto trace = std::stacktrace::current();
    for (const auto& frame : trace) {
        ss << frame << std::endl;
    }
    const auto message = ss.str();
    std::cout << message << std::endl;
#ifndef NDEBUG
    system("pause");
#endif
    exit(EXIT_FAILURE);
    return EXCEPTION_EXECUTE_HANDLER;
}

