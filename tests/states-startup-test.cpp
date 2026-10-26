#include "../d2rhud-injector/StatesStartupGate.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

unsigned snapshotCalls=0;DWORD snapshotError=ERROR_BAD_LENGTH;
HANDLE WINAPI TransientSnapshot(DWORD flags,DWORD pid){
    ++snapshotCalls;
    if(snapshotCalls<=3){SetLastError(snapshotError);return INVALID_HANDLE_VALUE;}
    return CreateToolhelp32Snapshot(flags,pid);
}

int wmain(int argc,wchar_t** argv) {
    assert(argc==3);
    auto snapshot=StatesStartup::ModuleSnapshot(GetCurrentProcessId(),1000,TransientSnapshot);
    assert(snapshot!=INVALID_HANDLE_VALUE && snapshotCalls==4);CloseHandle(snapshot);
    snapshotCalls=0;snapshotError=ERROR_ACCESS_DENIED;
    assert(StatesStartup::ModuleSnapshot(GetCurrentProcessId(),1000,TransientSnapshot)==INVALID_HANDLE_VALUE && snapshotCalls==1 && GetLastError()==ERROR_ACCESS_DENIED);
    snapshotCalls=0;snapshotError=ERROR_BAD_LENGTH;
    assert(StatesStartup::ModuleSnapshot(GetCurrentProcessId(),0,TransientSnapshot)==INVALID_HANDLE_VALUE && snapshotCalls==1 && GetLastError()==ERROR_BAD_LENGTH);
    assert(StatesStartup::ReadyExportRva(argv[1])==0);
    const auto exportRva=StatesStartup::ReadyExportRva(argv[2]);assert(exportRva);
    assert(!StatesStartup::DiagnosticsExportRva(argv[1]) && StatesStartup::DiagnosticsExportRva(argv[2]));
    assert(!StatesStartup::ExportRva(argv[2],"D2RHUD_NonexistentExport"));
    std::ifstream input(std::filesystem::path(argv[1]),std::ios::binary|std::ios::ate);assert(input);
    std::vector<uint8_t> file(size_t(input.tellg()));input.seekg(0);input.read(reinterpret_cast<char*>(file.data()),file.size());assert(input);
    auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(file.data());auto nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(file.data()+dos->e_lfanew);
    auto image=static_cast<uint8_t*>(VirtualAlloc(nullptr,nt->OptionalHeader.SizeOfImage,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));assert(image);
    const auto sections=IMAGE_FIRST_SECTION(nt);
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i)memcpy(image+sections[i].VirtualAddress,file.data()+sections[i].PointerToRawData,sections[i].SizeOfRawData);
    const auto process=GetCurrentProcess();const auto base=reinterpret_cast<uintptr_t>(image);
    StartupDiagnostics::Report diagnostic;
    const auto diagnosticBase=reinterpret_cast<uintptr_t>(&diagnostic)-4;
    for(uint32_t feature:{StartupDiagnostics::States,StartupDiagnostics::Classes,StartupDiagnostics::Acts}){
        diagnostic.failedFeatures=feature;diagnostic.stage=uint32_t(StartupDiagnostics::Stage::Rejected);
        const char* detail=feature==StartupDiagnostics::Acts?"Act VI target was edited: global/ui/layouts/controller/questlogpanelexpansionhd.json":"exact initialization failure";
        strcpy_s(diagnostic.detail,detail);
        const auto error=StatesStartup::StartupErrorMessage(process,diagnosticBase,4,ERROR_DLL_INIT_FAILED);
        const auto name=feature==StartupDiagnostics::Acts?"Act VI":feature==StartupDiagnostics::Classes?"Warlock":"extended states";
        assert(error.find(name)!=std::string::npos && error.find(detail)!=std::string::npos);
    }
    diagnostic.failedFeatures=StartupDiagnostics::States|StartupDiagnostics::Classes|StartupDiagnostics::Acts;
    auto error=StatesStartup::StartupErrorMessage(process,diagnosticBase,4,ERROR_DLL_INIT_FAILED);
    assert(error.find("extended states")!=std::string::npos && error.find("Warlock")!=std::string::npos && error.find("Act VI")!=std::string::npos);
    diagnostic.stage=uint32_t(StartupDiagnostics::Stage::Acts);
    assert(StatesStartup::StartupErrorMessage(process,diagnosticBase,4,ERROR_TIMEOUT).find("timed out while initializing Act VI")!=std::string::npos);
    assert(StatesStartup::StartupErrorMessage(process,diagnosticBase,4,ERROR_PROCESS_ABORTED).find("exited while initializing Act VI")!=std::string::npos);
    // Older DLLs have no diagnostics export; malformed records never leak a
    // guessed feature name or cause an unbounded remote-memory read.
    const auto fallback=StatesStartup::StartupErrorMessage(process,diagnosticBase,0,ERROR_DLL_INIT_FAILED);
    assert(fallback.find("feature-status.txt")!=std::string::npos);
    for(unsigned field=0;field<6;++field){auto saved=diagnostic;switch(field){case 0:diagnostic.magic=0;break;case 1:diagnostic.version=99;break;case 2:diagnostic.size=1;break;case 3:diagnostic.failedFeatures=128;break;case 4:diagnostic.stage=999;break;default:memset(diagnostic.detail,'x',sizeof(diagnostic.detail));}
        assert(StatesStartup::StartupErrorMessage(process,diagnosticBase,4,ERROR_DLL_INIT_FAILED)==fallback);diagnostic=saved;}
    image[StatesStartup::LoaderRva]^=1;assert(!StatesStartup::Arm(process,base));image[StatesStartup::LoaderRva]^=1;
    assert(!memcmp(image+StatesStartup::CheckRva,StatesStartup::Original.data(),7));
    const auto originalLoader=std::vector<uint8_t>(image+StatesStartup::LoaderRva,image+StatesStartup::LoaderRva+StatesStartup::LoaderSize);
    // The thread executes the real loader comparison, with a minimal owned
    // prologue/return instead of the game's surrounding compiler services.
    auto thunk=image+0x1ff0000;
    const uint8_t begin[]{0x53,0x48,0xbb,0x03,0x01,0,0,0,0,0,0,0x48,0xb8};memcpy(thunk,begin,sizeof(begin));
    const auto check=base+StatesStartup::CheckRva;memcpy(thunk+sizeof(begin),&check,8);thunk[sizeof(begin)+8]=0xff;thunk[sizeof(begin)+9]=0xe0;
    HANDLE thread=CreateThread(nullptr,0,reinterpret_cast<LPTHREAD_START_ROUTINE>(thunk),nullptr,CREATE_SUSPENDED,nullptr);assert(thread);
    assert(StatesStartup::Arm(process,base));
    assert(ResumeThread(thread)==1);
    assert(StatesStartup::HoldAtGate(process,thread,base,5000));
    assert(!memcmp(image+StatesStartup::CheckRva,StatesStartup::Original.data(),7));
    DWORD exit=0;assert(GetExitCodeThread(thread,&exit) && exit==STILL_ACTIVE);
    LONG ready=2;assert(!StatesStartup::WaitReady(process,reinterpret_cast<uintptr_t>(&ready)-4,4,10));
    ready=0;assert(!StatesStartup::WaitReady(process,reinterpret_cast<uintptr_t>(&ready)-4,4,10));
    ready=1;assert(StatesStartup::WaitReady(process,reinterpret_cast<uintptr_t>(&ready)-4,4,10));
    const uint32_t limit=513;memcpy(image+StatesStartup::CheckRva+3,&limit,4);
    const uint8_t finish[]{0x0f,0x92,0xc0,0x0f,0xb6,0xc0,0x5b,0xc3};memcpy(image+StatesStartup::CheckRva+7,finish,sizeof(finish));
    FlushInstructionCache(process,image,nt->OptionalHeader.SizeOfImage);
    assert(ResumeThread(thread)==1 && WaitForSingleObject(thread,5000)==WAIT_OBJECT_0);
    assert(GetExitCodeThread(thread,&exit) && exit==1);
    CloseHandle(thread);
    // BIN-loading workers must be found even when the initial thread is waiting.
    // Small tables retain the original limit; large tables require the extension.
    for(uint64_t rows:{1,255,256,259,512,513}){
        memcpy(image+StatesStartup::LoaderRva,originalLoader.data(),originalLoader.size());
        memcpy(thunk+3,&rows,8);assert(StatesStartup::Arm(process,base));
        HANDLE waiting=CreateEventW(nullptr,TRUE,FALSE,nullptr);assert(waiting);
        HANDLE initial=CreateThread(nullptr,0,+[](void* event)->DWORD{WaitForSingleObject(event,INFINITE);return 0;},waiting,0,nullptr);assert(initial);
        HANDLE worker=CreateThread(nullptr,0,reinterpret_cast<LPTHREAD_START_ROUTINE>(thunk),nullptr,0,nullptr);assert(worker);
        HANDLE held=nullptr;assert(StatesStartup::HoldAtGate(process,initial,base,5000,&held));
        assert(held&&GetThreadId(held)==GetThreadId(worker));
        assert(!memcmp(image+StatesStartup::CheckRva,StatesStartup::Original.data(),7));
        const uint32_t selected=rows<=255?256:513;memcpy(image+StatesStartup::CheckRva+3,&selected,4);
        memcpy(image+StatesStartup::CheckRva+7,finish,sizeof(finish));FlushInstructionCache(process,image,nt->OptionalHeader.SizeOfImage);
        assert(ResumeThread(held)==1&&WaitForSingleObject(worker,5000)==WAIT_OBJECT_0);
        assert(GetExitCodeThread(worker,&exit)&&exit==DWORD(rows<=512));
        SetEvent(waiting);assert(WaitForSingleObject(initial,5000)==WAIT_OBJECT_0);
        CloseHandle(held);CloseHandle(worker);CloseHandle(initial);CloseHandle(waiting);
    }
    memcpy(image+StatesStartup::LoaderRva,originalLoader.data(),originalLoader.size());
    HANDLE waiting=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    HANDLE idle=CreateThread(nullptr,0,+[](void* event)->DWORD{WaitForSingleObject(event,INFINITE);return 0;},waiting,0,nullptr);
    assert(idle&&StatesStartup::Arm(process,base));HANDLE held=nullptr;SetLastError(ERROR_SUCCESS);
    assert(!StatesStartup::HoldAtGate(process,idle,base,20,&held)&&!held&&GetLastError()==ERROR_TIMEOUT);
    SetEvent(waiting);assert(WaitForSingleObject(idle,5000)==WAIT_OBJECT_0);CloseHandle(idle);CloseHandle(waiting);
    SetLastError(ERROR_SUCCESS);ready=0;assert(!StatesStartup::WaitReady(process,reinterpret_cast<uintptr_t>(&ready)-4,4,10)&&GetLastError()==ERROR_TIMEOUT);
    SetLastError(ERROR_SUCCESS);ready=2;assert(!StatesStartup::WaitReady(process,reinterpret_cast<uintptr_t>(&ready)-4,4,10)&&GetLastError()==ERROR_DLL_INIT_FAILED);
    VirtualFree(image,0,MEM_RELEASE);
    std::cout<<"Startup feature/reason reporting, stage-specific timeout errors, older-DLL fallback and malformed diagnostics rejection passed.\n";
    std::cout<<"Startup export/signature validation, native and worker loading gates for 1/255/256/259/512 rows, 513-row rejection, instruction restoration and explicit timeout/readiness errors passed.\n";
}
