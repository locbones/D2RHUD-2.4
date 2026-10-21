#pragma once
#include <Windows.h>
#include <DbgHelp.h>
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <cstdio>

// Keep the faulting context, rather than a stack trace of the exception handler.
// No heap dump or network upload: these files stay on the local computer.
namespace CrashDiagnostics {
inline wchar_t crashPath[MAX_PATH]{},dumpPath[MAX_PATH]{},eventPath[MAX_PATH]{};
inline HMODULE dbgHelp=nullptr;
inline std::atomic_flag writing=ATOMIC_FLAG_INIT;
inline void Initialize(const wchar_t* directory=nullptr){
    wchar_t root[MAX_PATH]{};
    if(directory){if(wcslen(directory)>=MAX_PATH-40)return;wcscpy_s(root,directory);}
    else {
        const DWORD size=GetEnvironmentVariableW(L"LOCALAPPDATA",root,MAX_PATH);
        if(!size || size>=MAX_PATH-40)return;
        wcscat_s(root,L"\\D2RHUD");
    }
    CreateDirectoryW(root,nullptr);
    swprintf_s(crashPath,L"%s\\crash.log",root);
    swprintf_s(dumpPath,L"%s\\crash.dmp",root);
    swprintf_s(eventPath,L"%s\\map-events.log",root);
    // Load before a fault; loading a DLL inside the exception filter can block.
    dbgHelp=LoadLibraryExW(L"dbghelp.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
}
inline void Line(HANDLE file,const char* format,...){
    char buffer[2048];va_list args;va_start(args,format);
    const int size=vsnprintf_s(buffer,sizeof(buffer),_TRUNCATE,format,args);va_end(args);
    DWORD written=0;WriteFile(file,buffer,DWORD(size<0 ? strlen(buffer) : size),&written,nullptr);
}
inline void Event(const char* stage,uint32_t run,unsigned event,uint32_t frame,int monster=-1,int unique=-1){
    if(!eventPath[0])return;
    auto file=CreateFileW(eventPath,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return;
    // Bound history between games/runs without deleting any user's files.
    LARGE_INTEGER size{};if(GetFileSizeEx(file,&size) && size.QuadPart>1024*1024){CloseHandle(file);file=CreateFileW(eventPath,GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);}
    if(file==INVALID_HANDLE_VALUE)return;
    SYSTEMTIME time{};GetSystemTime(&time);
    Line(file,"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ pid=%lu thread=%lu run=%u event=%u frame=%u %s monster=%d unique=%d\r\n",
        time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond,time.wMilliseconds,GetCurrentProcessId(),GetCurrentThreadId(),run,event,frame,stage,monster,unique);
    CloseHandle(file);
}
inline void Address(HANDLE file,const char* label,DWORD64 address){
    MEMORY_BASIC_INFORMATION memory{};wchar_t module[MAX_PATH]{};
    if(VirtualQuery(reinterpret_cast<void*>(address),&memory,sizeof(memory)) && memory.Type==MEM_IMAGE && GetModuleFileNameW(static_cast<HMODULE>(memory.AllocationBase),module,MAX_PATH))
        Line(file,"%s=%016llX %ls+%llX\r\n",label,address,module,address-reinterpret_cast<DWORD64>(memory.AllocationBase));
    else Line(file,"%s=%016llX\r\n",label,address);
}
inline LONG Handle(EXCEPTION_POINTERS* fault){
    if(!fault || !fault->ExceptionRecord || !fault->ContextRecord || writing.test_and_set())return EXCEPTION_CONTINUE_SEARCH;
    auto file=crashPath[0] ? CreateFileW(crashPath,GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr) : INVALID_HANDLE_VALUE;
    if(file!=INVALID_HANDLE_VALUE){
        SYSTEMTIME time{};GetSystemTime(&time);const auto& c=*fault->ContextRecord;const auto& e=*fault->ExceptionRecord;
        Line(file,"D2RHUD crash UTC %04u-%02u-%02uT%02u:%02u:%02u pid=%lu thread=%lu build=%s %s\r\nexception=%08lX flags=%08lX\r\n",
            time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond,GetCurrentProcessId(),GetCurrentThreadId(),__DATE__,__TIME__,e.ExceptionCode,e.ExceptionFlags);
        Address(file,"exception-address",reinterpret_cast<DWORD64>(e.ExceptionAddress));Address(file,"RIP",c.Rip);
        for(DWORD n=0;n<e.NumberParameters && n<EXCEPTION_MAXIMUM_PARAMETERS;++n)Line(file,"parameter[%lu]=%016llX\r\n",n,DWORD64(e.ExceptionInformation[n]));
        Line(file,"RAX=%016llX RBX=%016llX RCX=%016llX RDX=%016llX\r\nRSI=%016llX RDI=%016llX RBP=%016llX RSP=%016llX\r\nR8=%016llX R9=%016llX R10=%016llX R11=%016llX\r\nR12=%016llX R13=%016llX R14=%016llX R15=%016llX\r\n",
            c.Rax,c.Rbx,c.Rcx,c.Rdx,c.Rsi,c.Rdi,c.Rbp,c.Rsp,c.R8,c.R9,c.R10,c.R11,c.R12,c.R13,c.R14,c.R15);
        // Raw stack addresses remain useful even if third-party unwind data is bad.
        for(size_t n=0;n<128;++n){DWORD64 value=0;SIZE_T read=0;if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(c.Rsp+n*8),&value,sizeof(value),&read) || read!=sizeof(value))break;
            char label[40];sprintf_s(label,"stack+%03llX",static_cast<unsigned long long>(n*8));Address(file,label,value);
        }
        FlushFileBuffers(file);CloseHandle(file);
    }
    if(dbgHelp && dumpPath[0]){
        const auto write=reinterpret_cast<decltype(&MiniDumpWriteDump)>(GetProcAddress(dbgHelp,"MiniDumpWriteDump"));
        auto dump=CreateFileW(dumpPath,GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(write && dump!=INVALID_HANDLE_VALUE){MINIDUMP_EXCEPTION_INFORMATION info{GetCurrentThreadId(),fault,FALSE};write(GetCurrentProcess(),GetCurrentProcessId(),dump,MiniDumpNormal,&info,nullptr,nullptr);}
        if(dump!=INVALID_HANDLE_VALUE)CloseHandle(dump);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}
}
