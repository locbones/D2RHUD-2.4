#include "../d2rhud/CrashDiagnostics.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

LONG WINAPI Capture(EXCEPTION_POINTERS* fault){return CrashDiagnostics::Handle(fault);}
__declspec(noinline) void NativeFault(volatile int* address){*address=42;}
int wmain(int argc,wchar_t** argv){
    assert(argc>=2);const std::filesystem::path directory=argv[1];
    if(argc==3){
        SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
        CrashDiagnostics::Initialize(argv[1]);SetUnhandledExceptionFilter(Capture);
        CrashDiagnostics::Event("spawn-begin",17,4,1234,156,23);
        NativeFault(nullptr);return 1;
    }
    std::filesystem::create_directories(directory);
    wchar_t self[MAX_PATH]{};assert(GetModuleFileNameW(nullptr,self,MAX_PATH));
    auto command=L"\""+std::wstring(self)+L"\" \""+directory.wstring()+L"\" --fault";
    STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION process{};
    assert(CreateProcessW(self,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process));
    assert(WaitForSingleObject(process.hProcess,15000)==WAIT_OBJECT_0);DWORD code=0;
    assert(GetExitCodeProcess(process.hProcess,&code) && code==EXCEPTION_ACCESS_VIOLATION);
    CloseHandle(process.hThread);CloseHandle(process.hProcess);
    auto read=[&](const wchar_t* name){std::ifstream input(directory/name,std::ios::binary);assert(input);return std::string(std::istreambuf_iterator<char>(input),{});};
    auto log=read(L"crash.log"),events=read(L"map-events.log");
    assert(log.find("exception=C0000005")!=std::string::npos && log.find("parameter[1]=0000000000000000")!=std::string::npos);
    assert(log.find("RIP=")!=std::string::npos && log.find("crash-diagnostics-test.exe+")!=std::string::npos && log.find("stack+000=")!=std::string::npos);
    assert(events.find("run=17 event=4 frame=1234 spawn-begin monster=156 unique=23")!=std::string::npos);
    auto dump=read(L"crash.dmp");assert(dump.size()>sizeof(MINIDUMP_HEADER));
    const auto header=reinterpret_cast<const MINIDUMP_HEADER*>(dump.data());assert(header->Signature==MINIDUMP_SIGNATURE);
    const auto streams=reinterpret_cast<const MINIDUMP_DIRECTORY*>(dump.data()+header->StreamDirectoryRva);bool exception=false;
    for(unsigned n=0;n<header->NumberOfStreams;++n)if(streams[n].StreamType==ExceptionStream){
        const auto stream=reinterpret_cast<const MINIDUMP_EXCEPTION_STREAM*>(dump.data()+streams[n].Location.Rva);
        const auto context=reinterpret_cast<const CONTEXT*>(dump.data()+stream->ThreadContext.Rva);
        assert(stream->ExceptionRecord.ExceptionCode==EXCEPTION_ACCESS_VIOLATION && stream->ExceptionRecord.ExceptionAddress==context->Rip);
        exception=true;
    }
    assert(exception);std::cout<<"Real child-process access violation records the original exception, module/RVA, registers, stack, event checkpoint and minidump context.\n";
}
