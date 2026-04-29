#include <Windows.h>
#include <MinHook.h>
#include <cstdio>

bool InstallBankPanelHook();
void UninstallBankPanelHook();
bool InstallWndProcHook();
void UninstallWndProcHook();

static DWORD WINAPI AttachThread(LPVOID /*lParam*/) {
    // Give D2R time to fully initialize the UI subsystem before we hook.
    Sleep(2000);

    // MH_Initialize may already have been called by another DLL (e.g.
    // D2RHUD). MH_ERROR_ALREADY_INITIALIZED is fine; we proceed regardless.
    MH_Initialize();

    InstallBankPanelHook();
    InstallWndProcHook();
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID /*reserved*/) {
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, &AttachThread, hModule, 0, nullptr);
        break;
    case DLL_PROCESS_DETACH:
        // Don't call MH_Uninitialize: another loaded DLL may still be using
        // MinHook hooks. Just disable our specific hooks.
        UninstallWndProcHook();
        UninstallBankPanelHook();
        break;
    }
    return TRUE;
}