#pragma once
#include <windows.h>
#include <tlhelp32.h>
#include <cstdint>
#include <cstring>
#include <array>
#include <string>
#include "../d2rhud/StartupDiagnostics.h"

// Shared with Pandemonium's native HUD loader. This gate is armed only in a
// newly created, suspended process. It does not attach to an existing game.
namespace StatesStartup {
// Module loading on another thread can invalidate a Toolhelp snapshot.
// ERROR_BAD_LENGTH is transient; do not report a missing HUD or stop the game.
using SnapshotFunction=HANDLE(WINAPI*)(DWORD,DWORD);
inline HANDLE ModuleSnapshot(DWORD pid,DWORD timeout=5000,SnapshotFunction snapshot=CreateToolhelp32Snapshot) {
    const auto started=GetTickCount64();
    for(;;) {
        const auto result=snapshot(TH32CS_SNAPMODULE,pid);
        if(result!=INVALID_HANDLE_VALUE)return result;
        const DWORD error=GetLastError();
        if(error!=ERROR_BAD_LENGTH || GetTickCount64()-started>=timeout){SetLastError(error);return INVALID_HANDLE_VALUE;}
        Sleep(10);
    }
}
inline constexpr DWORD CheckRva=0x1dc277, LoaderRva=0x1db3b0, LoaderSize=0x13c3;
inline constexpr uint64_t LoaderHash=0x002bbd10e9d3a737ULL;
inline constexpr std::array<uint8_t,7> Original{{0x48,0x81,0xfb,0,1,0,0}};

template<class T> bool Read(HANDLE process,uintptr_t address,T& value) {
    SIZE_T n=0;
    return ReadProcessMemory(process,reinterpret_cast<void*>(address),&value,sizeof(value),&n) && n==sizeof(value);
}
inline uintptr_t ImageBase(HANDLE process) {
    using Query=LONG(NTAPI*)(HANDLE,ULONG,void*,ULONG,ULONG*);
    auto query=reinterpret_cast<Query>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"NtQueryInformationProcess"));
    std::array<uintptr_t,6> information{};uintptr_t base=0;
    if(!query || query(process,0,information.data(),ULONG(sizeof(information)),nullptr)<0 ||
       !Read(process,information[1]+0x10,base))return 0;
    return base;
}
inline bool Write(HANDLE process,uintptr_t address,const void* bytes,SIZE_T size) {
    DWORD previous=0;
    if(!VirtualProtectEx(process,reinterpret_cast<void*>(address),size,PAGE_EXECUTE_READWRITE,&previous))return false;
    SIZE_T n=0;
    const bool ok=WriteProcessMemory(process,reinterpret_cast<void*>(address),bytes,size,&n) && n==size &&
        FlushInstructionCache(process,reinterpret_cast<void*>(address),size);
    DWORD unused=0;const bool protectedAgain=VirtualProtectEx(process,reinterpret_cast<void*>(address),size,previous,&unused)!=FALSE;
    return ok && protectedAgain;
}

// Inspect an image resource without executing its entry point or constructors.
// A missing export preserves the older loader behavior for older HUD builds.
inline DWORD ExportRva(const wchar_t* dll,const char* symbol) {
    const size_t symbolSize=strlen(symbol)+1;if(symbolSize>128)return 0;
    HMODULE image=LoadLibraryExW(dll,nullptr,LOAD_LIBRARY_AS_IMAGE_RESOURCE|LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE);
    if(!image)return 0;
    auto bytes=reinterpret_cast<const uint8_t*>(reinterpret_cast<uintptr_t>(image)&~uintptr_t(3));
    DWORD result=0;
    const auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes);
    if(dos->e_magic==IMAGE_DOS_SIGNATURE && dos->e_lfanew>0 && dos->e_lfanew<0x100000) {
        const auto nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(bytes+dos->e_lfanew);
        if(nt->Signature==IMAGE_NT_SIGNATURE && nt->FileHeader.Machine==IMAGE_FILE_MACHINE_AMD64 && nt->OptionalHeader.Magic==IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
            const auto size=nt->OptionalHeader.SizeOfImage;
            const auto range=[&](DWORD rva,uint64_t count){return rva<size && count<=uint64_t(size)-rva;};
            const auto directory=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
            if(directory.VirtualAddress && range(directory.VirtualAddress,sizeof(IMAGE_EXPORT_DIRECTORY))) {
                const auto exports=reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(bytes+directory.VirtualAddress);
                if(range(exports->AddressOfNames,uint64_t(exports->NumberOfNames)*4) &&
                   range(exports->AddressOfNameOrdinals,uint64_t(exports->NumberOfNames)*2) &&
                   range(exports->AddressOfFunctions,uint64_t(exports->NumberOfFunctions)*4)) {
                    const auto names=reinterpret_cast<const DWORD*>(bytes+exports->AddressOfNames);
                    const auto ordinals=reinterpret_cast<const WORD*>(bytes+exports->AddressOfNameOrdinals);
                    const auto functions=reinterpret_cast<const DWORD*>(bytes+exports->AddressOfFunctions);
                    for(DWORD i=0;i<exports->NumberOfNames;++i)if(range(names[i],symbolSize) &&
                        !memcmp(bytes+names[i],symbol,symbolSize) && ordinals[i]<exports->NumberOfFunctions) {
                        const DWORD rva=functions[ordinals[i]];
                        if(range(rva,4) && (rva<directory.VirtualAddress || uint64_t(rva)>=uint64_t(directory.VirtualAddress)+directory.Size))result=rva;
                        break;
                    }
                }
            }
        }
    }
    FreeLibrary(image);return result;
}
inline DWORD ReadyExportRva(const wchar_t* dll){return ExportRva(dll,"D2RHUD_States512Startup");}
inline DWORD DiagnosticsExportRva(const wchar_t* dll){return ExportRva(dll,"D2RHUD_StartupDiagnostics");}

inline bool ReadDiagnostics(HANDLE process,uintptr_t hud,DWORD rva,StartupDiagnostics::Report& report){
    return rva && Read(process,hud+rva,report) && report.magic==StartupDiagnostics::Magic &&
        report.version==StartupDiagnostics::Version && report.size==sizeof(report) &&
        report.stage<=uint32_t(StartupDiagnostics::Stage::Rejected) && !(report.failedFeatures&~uint32_t(7)) &&
        !report.detail[sizeof(report.detail)-1];
}
inline std::string StartupErrorMessage(HANDLE process,uintptr_t hud,DWORD diagnosticsRva,DWORD error){
    StartupDiagnostics::Report report;const bool known=ReadDiagnostics(process,hud,diagnosticsRva,report);
    if(error==ERROR_DLL_INIT_FAILED){
        std::string features;
        if(known){
            const auto add=[&](uint32_t bit,const char* name){if(report.failedFeatures&bit){if(!features.empty())features+=", ";features+=name;}};
            add(StartupDiagnostics::States,"extended states");add(StartupDiagnostics::Classes,"extended classes / Warlock");add(StartupDiagnostics::Acts,"Act VI");
        }
        std::string message=features.empty()?"HUD startup initialization was rejected":"HUD startup failed ["+features+"]";
        if(known && report.detail[0])message+=": "+std::string(report.detail);
        else message+=". Check the current LOCALAPPDATA/D2RHUD/feature-status.txt report";
        return message+="; game initialization was stopped safely.";
    }
    const std::string stage=known?StartupDiagnostics::StageName(report.stage):"HUD initialization";
    if(error==ERROR_TIMEOUT)return "HUD startup timed out while initializing "+stage+"; game initialization was stopped safely.";
    if(error==ERROR_PROCESS_ABORTED)return "The game exited while initializing "+stage+".";
    return "Could not read HUD startup status while initializing "+stage+"; game initialization was stopped safely.";
}

// The initial thread is still suspended here, before any game code can race
// the two-byte temporary branch. The full loader is verified before arming it.
inline bool Arm(HANDLE process,uintptr_t base) {
    std::array<uint8_t,LoaderSize> bytes{};
    if(!Read(process,base+LoaderRva,bytes))return false;
    uint64_t hash=14695981039346656037ULL;
    for(auto byte:bytes)hash=(hash^byte)*1099511628211ULL;
    if(hash!=LoaderHash || memcmp(bytes.data()+CheckRva-LoaderRva,Original.data(),Original.size()))return false;
    constexpr uint8_t branch[]{0xeb,0xfe};
    return Write(process,base+CheckRva,branch,sizeof(branch));
}

// The engine has resolved its mod globals by this point, unlike DLL injection
// before entry. Hold only its initial thread, restore the original instruction
// for HUD's signature checks, then let HUD initialize on its own threads.
enum class GateProbe { Running,Held,Failed };
inline GateProbe ProbeGate(HANDLE process,HANDLE thread,uintptr_t base) {
    if(SuspendThread(thread)==DWORD(-1))return GateProbe::Failed;
    CONTEXT context{};context.ContextFlags=CONTEXT_CONTROL;
    if(!GetThreadContext(thread,&context)){
        const DWORD error=GetLastError();ResumeThread(thread);SetLastError(error);return GateProbe::Failed;
    }
    if(context.Rip==base+CheckRva) {
        std::array<uint8_t,2> branch{};
        if(!Read(process,base+CheckRva,branch)){const DWORD error=GetLastError();ResumeThread(thread);SetLastError(error);return GateProbe::Failed;}
        if(branch[0]!=0xeb || branch[1]!=0xfe){ResumeThread(thread);SetLastError(ERROR_INVALID_DATA);return GateProbe::Failed;}
        if(!Write(process,base+CheckRva,Original.data(),2)){const DWORD error=GetLastError();ResumeThread(thread);SetLastError(error);return GateProbe::Failed;}
        return GateProbe::Held;
    }
    return ResumeThread(thread)==DWORD(-1)?GateProbe::Failed:GateProbe::Running;
}
// Return an independently owned handle to the thread actually stopped at the
// state loader, including BIN-loading workers. The caller resumes that handle.
// With no output handle, retain the original initial-thread-only test API.
inline bool HoldAtGate(HANDLE process,HANDLE initialThread,uintptr_t base,DWORD timeout=120000,HANDLE* heldThread=nullptr) {
    if(heldThread)*heldThread=nullptr;
    const DWORD pid=GetProcessId(process),initialId=GetThreadId(initialThread);
    ULONGLONG nextWorkers=0;
    const ULONGLONG start=GetTickCount64();
    while(GetTickCount64()-start<timeout && WaitForSingleObject(process,0)==WAIT_TIMEOUT) {
        const auto probe=ProbeGate(process,initialThread,base);
        if(probe==GateProbe::Failed)return false;
        if(probe==GateProbe::Held){
            if(heldThread && !DuplicateHandle(GetCurrentProcess(),initialThread,GetCurrentProcess(),heldThread,0,FALSE,DUPLICATE_SAME_ACCESS)){
                const DWORD error=GetLastError();ResumeThread(initialThread);SetLastError(error);return false;
            }
            SetLastError(ERROR_SUCCESS);return true;
        }
        if(heldThread && GetTickCount64()>=nextWorkers){
            nextWorkers=GetTickCount64()+100;
            const auto snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);
            if(snapshot==INVALID_HANDLE_VALUE)return false;
            THREADENTRY32 entry{};entry.dwSize=sizeof(entry);
            if(Thread32First(snapshot,&entry))do{
                if(entry.th32OwnerProcessID!=pid || entry.th32ThreadID==initialId || entry.th32ThreadID==GetCurrentThreadId())continue;
                const auto worker=OpenThread(THREAD_SUSPEND_RESUME|THREAD_GET_CONTEXT|THREAD_QUERY_INFORMATION,FALSE,entry.th32ThreadID);
                if(!worker)continue; // Workers can finish between enumeration and OpenThread.
                const auto workerProbe=ProbeGate(process,worker,base);
                if(workerProbe==GateProbe::Held){*heldThread=worker;CloseHandle(snapshot);SetLastError(ERROR_SUCCESS);return true;}
                CloseHandle(worker);
            }while(Thread32Next(snapshot,&entry));
            CloseHandle(snapshot);
        }
        Sleep(5);
    }
    SetLastError(WaitForSingleObject(process,0)==WAIT_OBJECT_0?ERROR_PROCESS_ABORTED:ERROR_TIMEOUT);return false;
}

inline bool WaitReady(HANDLE process,uintptr_t hud,DWORD readyRva,DWORD timeout=30000) {
    const ULONGLONG start=GetTickCount64();
    while(GetTickCount64()-start<timeout && WaitForSingleObject(process,0)==WAIT_TIMEOUT) {
        LONG state=0;
        if(!Read(process,hud+readyRva,state))return false;
        if(state==2){SetLastError(ERROR_DLL_INIT_FAILED);return false;}
        if(state==1)return true;
        Sleep(5);
    }
    SetLastError(WaitForSingleObject(process,0)==WAIT_OBJECT_0?ERROR_PROCESS_ABORTED:ERROR_TIMEOUT);return false;
}
}
