#include "../d2rhud/ProcessLifetime.h"
#include <Windows.h>
#include <atlbase.h>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {
struct Markers {
    volatile LONG created=0,releases=0,destroyed=0,stateDestroyed=0;
    volatile LONG returning=0,delayed=0;
    volatile LONG64 delayBegan=0,delayEnded=0;
};
Markers* markers=nullptr; // Shared mapping remains valid during CRT destruction.

class Probe final : public IUnknown {
    volatile LONG references_=1;
public:
    Probe(){InterlockedIncrement(&markers->created);}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** result) override {
        if(!result)return E_POINTER;*result=nullptr;
        if(iid!=__uuidof(IUnknown))return E_NOINTERFACE;
        *result=static_cast<IUnknown*>(this);AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {return InterlockedIncrement(&references_);}
    ULONG STDMETHODCALLTYPE Release() override {
        InterlockedIncrement(&markers->releases);
        const auto left=InterlockedDecrement(&references_);
        if(!left){
            // Positive control simulates a driver release delaying shutdown.
            if(markers->returning&&InterlockedCompareExchange(&markers->delayed,1,0)==0){
                InterlockedExchange64(&markers->delayBegan,GetTickCount64());Sleep(2000);
                InterlockedExchange64(&markers->delayEnded,GetTickCount64());
            }
            delete this;
        }
        return left;
    }
private:
    ~Probe(){InterlockedIncrement(&markers->destroyed);}
};
void GiveProbe(IUnknown** output){assert(output&&!*output);*output=new Probe;}
struct Frame {CComPtr<IUnknown> allocator,target;};
struct Texture {CComPtr<IUnknown> resource;};
struct Resources {
    CComPtr<IUnknown> direct,array[2];
    std::vector<Frame> frames;
    std::map<std::string,Texture> atlases;
    Texture quantity;
    ~Resources(){InterlockedIncrement(&markers->stateDestroyed);}
    void Fill(){
        // Exercise ATL's native output-argument idiom, including array elements.
        GiveProbe(&direct);for(auto& resource:array)GiveProbe(&resource);
        frames.emplace_back();GiveProbe(&frames.back().allocator);GiveProbe(&frames.back().target);
        GiveProbe(&atlases["item"].resource);GiveProbe(&quantity.resource);
        const std::set<IUnknown*> distinct{direct.p,array[0].p,array[1].p,
            frames.back().allocator.p,frames.back().target.p,atlases["item"].resource.p,quantity.resource.p};
        assert(distinct.size()==7&&distinct.find(nullptr)==distinct.end());
    }
    void Reset(){
        direct.Release();for(auto& resource:array)resource.Release();
        frames.clear();atlases.clear();quantity.resource.Release();
    }
};
constexpr LONG perFill=7;
Resources& Graphics(){return HudLifetime::State<Resources>();}

int Child(const std::wstring& mode,HANDLE mapping,HANDLE ready){
    markers=static_cast<Markers*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(Markers)));
    if(!markers)return 2;
    if(mode==L"static"){
        static Resources ordinary;
        ordinary.Reset();ordinary.Reset();assert(markers->releases==0);
        ordinary.Fill();
    }else{
        auto& retained=Graphics();
        assert(&retained==&HudLifetime::State<Resources>());
        // ResetState can run before the first rendered frame owns any resources.
        Graphics().Reset();Graphics().Reset();assert(markers->releases==0);
        retained.Fill();
        if(mode==L"reset"){
            Graphics().direct.Release();assert(markers->releases==1);
            Graphics().array[0].Release();Graphics().array[1].Release();assert(markers->releases==3);
            Graphics().frames.clear();assert(markers->releases==5);
            Graphics().atlases.clear();assert(markers->releases==6);
            Graphics().quantity.resource.Release();assert(markers->releases==perFill);
            Graphics().Reset();Graphics().Reset();
            assert(markers->releases==perFill&&markers->destroyed==perFill);
            Graphics().Fill();Graphics().Reset();Graphics().Reset();
            assert(markers->created==perFill*2&&markers->releases==perFill*2&&markers->destroyed==perFill*2);
        }else if(mode!=L"retained")return 3;
    }
    InterlockedExchange(&markers->returning,1);
    if(!SetEvent(ready))return 4;
    return 0; // Ordinary static Resources now releases; retained Resources does not.
}

void Run(const std::wstring& self,const wchar_t* mode){
    SECURITY_ATTRIBUTES inherit{sizeof(inherit),nullptr,TRUE};
    const auto mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,&inherit,PAGE_READWRITE,0,sizeof(Markers),nullptr);
    const auto ready=CreateEventW(&inherit,TRUE,FALSE,nullptr);
    assert(mapping&&ready);
    auto* shared=static_cast<Markers*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(Markers)));
    assert(shared);ZeroMemory(shared,sizeof(*shared));
    auto command=L"\""+self+L"\" --child "+mode+L" "+
        std::to_wstring(reinterpret_cast<uintptr_t>(mapping))+L" "+std::to_wstring(reinterpret_cast<uintptr_t>(ready));
    STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION process{};
    assert(CreateProcessW(self.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process));
    const auto began=WaitForSingleObject(ready,6000);
    const auto tick=GetTickCount64();
    const bool control=std::wstring(mode)==L"static";
    const auto exited=began==WAIT_OBJECT_0?WaitForSingleObject(process.hProcess,control?6000:1500):WAIT_TIMEOUT;
    const auto elapsed=GetTickCount64()-tick;
    if(exited!=WAIT_OBJECT_0){
        // This handle belongs solely to the child just created by this test.
        TerminateProcess(process.hProcess,99);WaitForSingleObject(process.hProcess,1000);
    }
    DWORD code=0;const auto haveCode=GetExitCodeProcess(process.hProcess,&code);
    CloseHandle(process.hThread);CloseHandle(process.hProcess);
    assert(began==WAIT_OBJECT_0&&exited==WAIT_OBJECT_0&&haveCode&&code==0);
    if(control){
        assert(shared->created==perFill&&shared->releases==perFill&&shared->destroyed==perFill);
        assert(shared->stateDestroyed==1&&shared->delayed==1);
        assert(shared->delayEnded-shared->delayBegan>=1800);
    }else if(std::wstring(mode)==L"reset"){
        assert(shared->created==perFill*2&&shared->releases==perFill*2&&shared->destroyed==perFill*2);
        assert(shared->stateDestroyed==0&&shared->delayed==0);
    }else{
        assert(shared->created==perFill&&shared->releases==0&&shared->destroyed==0);
        assert(shared->stateDestroyed==0&&shared->delayed==0);
    }
    std::wcout<<mode<<L": "<<elapsed<<L" ms after returning from main, "<<shared->releases<<L" COM releases.\n";
    UnmapViewOfFile(shared);CloseHandle(ready);CloseHandle(mapping);
}
}

int wmain(int argc,wchar_t** argv){
    if(argc==5&&std::wstring(argv[1])==L"--child"){
        return Child(argv[2],reinterpret_cast<HANDLE>(std::wcstoull(argv[3],nullptr,10)),
            reinterpret_cast<HANDLE>(std::wcstoull(argv[4],nullptr,10)));
    }
    assert(argc==1);wchar_t self[32768]{};assert(GetModuleFileNameW(nullptr,self,32768));
    Run(self,L"retained");Run(self,L"reset");Run(self,L"static");
    std::cout<<"Process-lifetime state skips exit-time ATL COM releases; explicit runtime resets still release all resource forms.\n";
}
