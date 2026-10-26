#include <Windows.h>
#include <TlHelp32.h>
#include "D3D12Hook.h"
#include <utility>
#include <fstream>
#include <iostream>
#include <format>
#include <filesystem>
#include "Logging.h"
#include "CrashDiagnostics.h"
#include "d2/D2Ptrs.h"
#include <MinHook.h>
#include "plugin/AutomapLayers.h"
#include "plugin/ItemCodes.h"
#include "plugin/LegacyTransmog.h"
#include "plugin/StackSplitSkin.h"
#include "plugin/CubeVisual.h"
#include "plugin/CubeMap.h"
#include "plugin/RestartGame.h"
#include "plugin/SummonPersistence.h"
#include "plugin/QuestJournal.h"
#include "plugin/QuestJournalClient.h"
#include "plugin/HUDControls.h"
#include "plugin/CandyCornSave.h"
#include "plugin/QuestKillEvents.h"
#include "plugin/D3Whirlwind.h"
#include "plugin/SelectedAuras.h"
#include "plugin/WidescreenBars.h"
#include "plugin/SunderSystem.h"
#include "plugin/HellSpindle.h"
#include "plugin/States512.h"
#include "plugin/ExtendedClasses.h"
#include "plugin/ExtendedActs.h"
#include "plugin/CompiledNames.h"
#include "plugin/TableMetadataExport.h"

LONG WINAPI ExceptionHandler(PEXCEPTION_POINTERS pExceptionInfo);

bool InstallBankPanelHook();
void UninstallBankPanelHook();
bool InstallWndProcHook();
void UninstallWndProcHook();

// Capture installation failures before play, including native patches from
// other components. This report is local and replaced on each game launch.
static void WriteFeatureStatus(HMODULE hud) {
    try {
        wchar_t local[32768]{}, gamePath[32768]{}, hudPath[32768]{};
        const auto size = GetEnvironmentVariableW(L"LOCALAPPDATA", local, 32768);
        if (!size || size >= 32768) return;
        const auto directory = std::filesystem::path(local) / L"D2RHUD";
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        if (error) return;
        GetModuleFileNameW(nullptr, gamePath, 32768);
        GetModuleFileNameW(hud, hudPath, 32768);
        auto utf8 = [](const wchar_t* path) {
            const auto value = std::filesystem::path(path).u8string();
            return std::string(reinterpret_cast<const char*>(value.data()), value.size());
        };
        std::ofstream report(directory / L"feature-status.txt", std::ios::binary | std::ios::trunc);
        report << "D2RHUD build: " << __DATE__ << ' ' << __TIME__ << '\n'
            << "Game: " << utf8(gamePath) << '\n'
            << "HUD: " << utf8(hudPath) << '\n'
            << "State-table compatibility: " << States512::Status() << '\n'
            << "TXT/BIN table metadata: " << CompiledNames::Status() << '\n'
            << "Automatic table metadata export: " << TableMetadataExport::Status() << '\n'
            << "Right-click item conversion: " << LegacyTransmog::Status() << '\n'
            << "Right-click stack splitting: " << LegacyTransmog::StackSplitStatus() << '\n'
            << "Automatic stack socketing: " << LegacyTransmog::SocketStackStatus() << '\n'
            << "Cube recipes / Rune deposits: " << CubeVisual::Status() << '\n'
            << "Cube maps / Survivors: " << CubeMap::Status() << '\n'
            << "Character save/load: " << CubeMap::SummonLifecycleStatus() << '\n'
            << "Persistent Summons: " << SummonPersistence::Status() << '\n'
            << "Native HUD Controls: " << HUDControls::Status() << '\n'
            << "Candy Corn save conversion: " << CandyCornSave::Status() << '\n'
            << "Immunity Sunder: " << SunderSystem::Status() << '\n'
            << "Hell Spindle: " << HellSpindle::Status() << '\n'
            << "No Widescreen Bars: " << WidescreenBars::Status() << '\n'
            << "Quest kills: " << QuestKillEvents::Status() << '\n'
            << "Quest Journal: " << QuestJournal::Status() << '\n'
            << "Quest Journal shortcuts: " << QuestJournal::ClientUI::Status() << '\n'
            << "Restart Game: " << RestartGame::Status() << '\n'
            << "Act VI: " << ExtendedActs::Status() << '\n';
        if(const auto details=CubeMap::SurvivorsCompatibilityDetails();*details){
            report<<"\nSurvivors startup diagnostics:\n"<<details;
            // Destination ownership alone cannot identify an installer. Include
            // the loaded paths so reports from two installations can be compared.
            const auto snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,GetCurrentProcessId());
            if(snapshot==INVALID_HANDLE_VALUE)report<<"Module snapshot failed: "<<GetLastError()<<'\n';
            else{
                MODULEENTRY32W module{};module.dwSize=sizeof(module);
                report<<"\nLoaded modules at report time (base, size, path):\n";
                unsigned count=0;
                if(Module32FirstW(snapshot,&module))do{
                    report<<std::hex<<"0x"<<reinterpret_cast<uintptr_t>(module.modBaseAddr)<<" 0x"<<module.modBaseSize<<' '<<utf8(module.szExePath)<<'\n';
                }while(++count<512 && Module32NextW(snapshot,&module));
                CloseHandle(snapshot);
            }
        }
    } catch (...) { OutputDebugStringA("D2RHUD: feature installation report could not be written.\n"); }
}

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
    CrashDiagnostics::Initialize();
    HMODULE hModule = static_cast<HMODULE>(lParam);
    D3D12::SetDllModule(hModule);
    const bool statesReady=States512::Install(false);
    D2Ptrs::Initialize();
    wchar_t exe[32768]{};char selectedMod[128]{};SIZE_T selectedRead=0;
    if(GetModuleFileNameW(nullptr,exe,32768) && ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(Pattern::Address(0x1bf084f)),selectedMod,sizeof(selectedMod),&selectedRead)){
        selectedMod[127]=0;
        CompiledNames::Load(std::filesystem::path(exe).parent_path(),selectedMod);
    }
    CandyCornSave::Initialize(selectedMod);
    ItemCodes::Install();
    RestartGame::Install();
    AutomapLayers::Install();
    LegacyTransmog::Install();
    CubeVisual::Install();
    CubeMap::Install();
    SummonPersistence::Install();
    const auto questDefinitions=QuestJournal::Bindings();
    const bool questKills=questDefinitions&&questDefinitions->events&&questDefinitions->error.empty()&&QuestKillEvents::Install();
    QuestJournal::SetAvailable(questKills&&questDefinitions&&questDefinitions->events&&questDefinitions->error.empty()&&CubeVisual::QuestJournalSupported()&&CubeMap::SummonLifecycleAvailable());
    D3Whirlwind::Install();
    SelectedAuras::Install();
    WidescreenBars::Install();
    HUDControls::Install();
    SunderSystem::Install();
    HellSpindle::Install();
    // Class storage must be widened before the suspended table loader resumes.
    // Install after features that verify the original player allocator bytes.
    const bool classesReady=ExtendedClasses::Install();
    const bool actsReady=ExtendedActs::Install();
    // Whirlwind verifies the original UI-open bytes before these UI detours.
    QuestJournal::ClientUI::Install();
    if(statesReady && classesReady && actsReady && exe[0])TableMetadataExport::Start(std::filesystem::path(exe).parent_path(),selectedMod);
    States512::FinishStartup(statesReady && classesReady && actsReady);
    WriteFeatureStatus(hModule);
    if(!statesReady || !classesReady || !actsReady)return 0;
    LegacyTransmog::Skin::Load();
    if (D3D12::Init() == D3D12::Status::Success) {
        D3D12::InstallHooks();
    }

    Sleep(2000);

    // MH_Initialize may already have been called by another DLL (e.g.
    // D2RHUD). MH_ERROR_ALREADY_INITIALIZED is fine; we proceed regardless.
    MH_Initialize();

    InstallBankPanelHook();
    InstallWndProcHook();
    // Startup injection precedes the game's own handler installation.
    SetUnhandledExceptionFilter(ExceptionHandler);
    CrashDiagnostics::Event("hud-ready",0,0,0);
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
        // At process termination another thread may have died holding a mutex.
        // Let the OS reclaim resources without waiting or attempting to unhook.
        if (lpReserved != nullptr) break;
        ExtendedActs::Uninstall();
        SelectedAuras::Uninstall();
        HUDControls::Uninstall();
        WidescreenBars::Uninstall();
        RestartGame::Uninstall();
        SunderSystem::Uninstall();
        HellSpindle::Uninstall();
        AutomapLayers::Uninstall();
        ItemCodes::Uninstall();
        LegacyTransmog::Uninstall();
        D3Whirlwind::Uninstall();
        SummonPersistence::Uninstall();
        QuestJournal::ClientUI::Uninstall();QuestJournal::SetAvailable(false);QuestKillEvents::Uninstall();
        CandyCornSave::Uninstall();
        CubeMap::Uninstall();
        CubeVisual::Uninstall();
        States512::Uninstall();
        D3D12::RemoveHooks();
        FreeConsole();
        UninstallWndProcHook();
        UninstallBankPanelHook();
        break;
    }
    }
    return TRUE;
}

LONG WINAPI ExceptionHandler(PEXCEPTION_POINTERS pExceptionInfo) {
    return CrashDiagnostics::Handle(pExceptionInfo);
}

