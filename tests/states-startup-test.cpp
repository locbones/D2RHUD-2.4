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
    std::ifstream input(std::filesystem::path(argv[1]),std::ios::binary|std::ios::ate);assert(input);
    std::vector<uint8_t> file(size_t(input.tellg()));input.seekg(0);input.read(reinterpret_cast<char*>(file.data()),file.size());assert(input);
    auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(file.data());auto nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(file.data()+dos->e_lfanew);
    auto image=static_cast<uint8_t*>(VirtualAlloc(nullptr,nt->OptionalHeader.SizeOfImage,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));assert(image);
    const auto sections=IMAGE_FIRST_SECTION(nt);
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i)memcpy(image+sections[i].VirtualAddress,file.data()+sections[i].PointerToRawData,sections[i].SizeOfRawData);
    const auto process=GetCurrentProcess();const auto base=reinterpret_cast<uintptr_t>(image);
    image[StatesStartup::LoaderRva]^=1;assert(!StatesStartup::Arm(process,base));image[StatesStartup::LoaderRva]^=1;
    assert(!memcmp(image+StatesStartup::CheckRva,StatesStartup::Original.data(),7));
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
    CloseHandle(thread);VirtualFree(image,0,MEM_RELEASE);
    std::cout<<"Startup export detection, signature rejection, native table gate, instruction restoration, readiness failure/timeout and 259-row resume passed.\n";
}
