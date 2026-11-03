#define NOMINMAX
#include <Windows.h>
#include "../d2rhud/Pattern.h"
#include <array>
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
std::atomic<uint64_t> now{100};
std::atomic<unsigned> registrations=0,posts=0,contextReads=0;
bool registerWorks=false,postWorks=true,delayRegistration=false;
struct Execution {std::string command;DWORD thread;};
std::vector<Execution> executed;
std::vector<UINT> keys;
unsigned checks=0;
void Check(bool condition,const char* message){++checks;if(!condition)throw std::runtime_error(message);}
uint8_t* Memory(){static auto memory=static_cast<uint8_t*>(VirtualAlloc(nullptr,0x2000000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));return memory;}
uint8_t& Status(){return Memory()[0x1dc76f8];}
uint8_t& Loading(){return Memory()[0x1cb410a];}
uint64_t& NativeToken(){return *reinterpret_cast<uint64_t*>(Memory()+0x1d617d0);}
ULONGLONG WINAPI FixtureTime(){return now.load();}
UINT WINAPI FixtureRegister(LPCWSTR name){
    ++registrations;
    if(!registerWorks){if(delayRegistration)now+=500;SetLastError(ERROR_NOT_ENOUGH_MEMORY);return 0;}
    return RegisterWindowMessageW(name);
}
BOOL WINAPI FixturePost(HWND window,UINT message,WPARAM w,LPARAM l){
    ++posts;if(!postWorks){SetLastError(ERROR_NOT_ENOUGH_QUOTA);return FALSE;}
    return PostMessageW(window,message,w,l);
}
void __fastcall Debug(const char* text);
}
DWORD64 Pattern::Address(uint32_t rva){
    if(rva==0x76bc0)return reinterpret_cast<DWORD64>(&Debug);
    if(rva==0x1dc76f8 || rva==0x1cb410a || rva==0x1d617d0 || rva==0x1d442d8 || rva==0x1c9b200)++contextReads;
    return reinterpret_cast<DWORD64>(Memory()+rva);
}
// Real production dispatcher, Win32 window/message delivery and thread ownership.
// Failure injection replaces registration/posting/time only; successful calls
// forward to real User32 and target this fixture's message-only windows.
#define GetTickCount64 FixtureTime
#define RegisterWindowMessageW FixtureRegister
#define PostMessageW FixturePost
#include "../d2rhud/plugin/D2RHUD/HudCommands.cpp"
#undef GetTickCount64
#undef RegisterWindowMessageW
#undef PostMessageW
namespace {
void __fastcall Debug(const char* text){
    executed.push_back({text,GetCurrentThreadId()});
    if(std::string_view(text)=="reentrant")HudCommands::Queue("chained");
}
void __fastcall Slash(std::string first,std::string second){
    if(first!=second)throw std::runtime_error("Slash command ABI changed");
    executed.push_back({"slash:"+first,GetCurrentThreadId()});
}
LRESULT CALLBACK WindowProc(HWND window,UINT message,WPARAM w,LPARAM l){
    if(HudCommands::HandleMessage(window,message,w,l))return 0;
    if(message==WM_KEYDOWN || message==WM_KEYUP || message==WM_SYSKEYDOWN || message==WM_SYSKEYUP){keys.push_back(message);return 0;}
    return DefWindowProcW(window,message,w,l);
}
void Pump(){MSG message;unsigned count=0;while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){
    if(++count>1000)throw std::runtime_error("Unexpected message flood");TranslateMessage(&message);DispatchMessageW(&message);
}}
void Sessions(){
    HudCommands::SessionTracker tracker;
    Check(tracker.Observe(false,99)==0,"Inactive state ignores stale server tokens");
    const auto joiner=tracker.Observe(true,0);
    Check(joiner!=0 && tracker.Observe(true,0)==joiner,"Joiner without host token has a stable session");
    Check(tracker.Observe(false,0)==0,"Observed leave clears client session");
    Check(tracker.Observe(true,0)!=joiner,"Joiner reentry receives a fresh generation");
    const auto host=tracker.Observe(true,1234);
    Check(tracker.Observe(true,1234)==host,"Host token stays stable within game");
    Check(tracker.Observe(true,5678)!=host,"Changed native game token starts fresh generation");
}
void Commands(HWND window,HWND second){
    using namespace HudCommands;
    CLIENT_playerCommand=Slash;
    contextReads=0;const auto originalRegistrations=registrations.load();
    bool ordinary=false;for(int i=0;i<100000;++i)ordinary |= HandleMessage(window,WM_MOUSEMOVE,0,0);
    Check(!ordinary && contextReads==0 && registrations==originalRegistrations,"100000 ordinary window messages read no game context and make no registration calls");
    Check(Tick(window)==0 && !Queue("outside"),"Commands rejected outside a game");
    Status()=1;NativeToken()=0;Loading()=0;
    const auto joiner=Tick(window);Check(joiner!=0,"Native zero-token joiner session accepted");
    delayRegistration=true;
    Check(Queue("first"),"Joiner command retained while registration fails");
    Check(registrations==1 && posts==0 && now==600,"Registration failure injected without posting");
    for(int frame=0;frame<10000;++frame)Tick(window);
    Check(registrations==1,"Registration backoff starts after the failed call completes");
    now=699;Tick(window);Check(registrations==1,"Registration not retried early");
    registerWorks=true;now=700;Tick(window);
    Check(registrations==2 && posts==1 && executed.empty(),"Registration retries successfully and posts to owner");
    const auto message=State().message.load();const auto token=State().token;
    std::thread wrongThread([&]{HandleMessage(window,message,token,0);});wrongThread.join();
    Check(executed.empty() && State().posted,"Direct producer-thread dispatch cannot execute native code");
    Pump();Check(executed.size()==1 && executed[0].command=="first" && executed[0].thread==GetCurrentThreadId(),"Posted native command executes once on window owner");
    const auto beforePosts=posts.load();Queue("second");Queue("third");
    Check(posts==beforePosts+1,"Multiple queued commands keep only one posted message");
    Pump();Tick(window);Pump();
    Check(executed.size()==3 && executed[1].command=="second" && executed[2].command=="third","Successive successful messages preserve FIFO without failure delay");
    postWorks=false;const auto failedPosts=posts.load();Queue("retry");
    for(unsigned frame=0;frame<10000;++frame){now=700+frame;Tick(window);}
    Check(posts-failedPosts==100,"10000 one-millisecond failed-delivery polls make only 100 post attempts");
    Check(executed.size()==3,"Posting failures execute no native code");
    postWorks=true;now=10700;Tick(window);Pump();
    Check(executed.back().command=="retry" && executed.size()==4,"Delivery recovers without dropping or duplicating retained command");
    postWorks=false;Queue("rollback");const auto prior=posts.load();now=1;Tick(window);
    Check(posts==prior+1,"Clock rollback permits retry rather than wedging queue");
    now=101;postWorks=true;Tick(window);Pump();Check(executed.back().command=="rollback","Clock rollback recovery delivers command");
    Queue("stale joiner");const auto beforeChange=executed.size();NativeToken()=33;Pump();
    Check(executed.size()==beforeChange,"Native token change cancels outstanding command before render");
    const auto host=Tick(window);Check(host!=joiner,"Host/native context change gets new session generation");
    Queue("fresh host");Pump();Check(executed.back().command=="fresh host","New host session delivers normally");
    Queue("old window");const auto oldWindowToken=State().token;
    Tick(second);HandleMessage(window,message,oldWindowToken,0);Pump();
    Check(executed.back().command=="old window","Window replacement redirects delivery and ignores old window token");
    Tick(window);
    Queue("before exit");const auto oldToken=State().token;const auto count=executed.size();
    Status()=0;Pump();Check(executed.size()==count,"Exit clears posted command backlog");
    Status()=1;NativeToken()=33;const auto reentered=Tick(window);
    Check(reentered!=host,"Same native token reused after observed exit gets fresh ownership");
    Queue("reentered");HandleMessage(window,message,oldToken,0);Pump();
    Check(executed.back().command=="reentered" && executed.size()==count+1,"Reused host token cannot revive old message");
    Loading()=1;const auto delayedPosts=posts.load();Check(Queue("after loading"),"Loading retains a command");
    now=30000;Tick(window);Pump();Check(posts==delayedPosts,"Loading does not post delivery messages");
    Loading()=0;Check(Tick(window)==reentered,"Act loading keeps the same session generation");Pump();
    Check(executed.back().command=="after loading","Deferred command resumes after loading");
    Queue("already posted");Loading()=1;Pump();const auto deferred=executed.size();Loading()=0;Tick(window);Pump();
    Check(executed.size()==deferred+1 && executed.back().command=="already posted","Loading beginning after post also preserves command");
    Queue("shutdown stale");Shutdown();const auto shutdownCount=executed.size();Tick(window);Pump();
    Check(executed.size()==shutdownCount,"Shutdown invalidates posted tokens before next session");
    Queue("configuration stale");const auto configCount=executed.size();Configure({"auto one","auto two","disabled","","",""});Pump();
    now=32999;Tick(window);Pump();Check(executed.size()==configCount,"Configuration edit cancels backlog and preserves three-second delay");
    now=33000;Tick(window);Pump();Check(executed.back().command=="auto one","First startup command works after reconfigure");
    const auto firstAuto=executed.size();Tick(window);Pump();Check(executed.size()==firstAuto,"Startup commands cannot burst in same frame");
    now=100000;Tick(window);Pump();Check(executed.size()==firstAuto+1 && executed.back().command=="auto two","Late frame takes only one remaining startup command");
    Configure({"capacity auto","","","","",""});bool accepted=true;
    for(int i=0;i<64;++i)accepted &= Queue("manual "+std::to_string(i));
    Check(accepted && !Queue("overflow"),"Bounded queue accepts 64 and rejects overflow");
    now=103000;Tick(window);const auto fullCount=executed.size();Pump();
    for(int i=0;i<65;++i){Tick(window);Pump();}
    Check(executed.size()==fullCount+65 && executed.back().command=="capacity auto","Full queue backpressures startup schedule without losing command");
    bool ordered=true;for(int i=0;i<64;++i)ordered &= executed[fullCount+i].command=="manual "+std::to_string(i);
    Check(ordered,"Backpressure preserves every manual command in order");
    Configure({});const auto producerCount=executed.size();std::atomic<unsigned> acceptedProducers=0;
    std::vector<std::thread> producers;for(int t=0;t<4;++t)producers.emplace_back([&,t]{for(int i=0;i<8;++i)if(Queue("producer "+std::to_string(t*8+i)))++acceptedProducers;});
    for(auto& producer:producers)producer.join();
    Check(acceptedProducers==32 && executed.size()==producerCount,"Concurrent producers enqueue without executing native calls");
    for(int i=0;i<32;++i){Pump();Tick(window);}Pump();
    bool owner=true;for(size_t i=producerCount;i<executed.size();++i)owner &= executed[i].thread==GetCurrentThreadId();
    Check(executed.size()==producerCount+32 && owner,"Concurrent producer commands all dispatch on window owner");
    Queue("reentrant");Pump();Check(executed.back().command=="chained","Native callback can enqueue without deadlocking queue mutex");
    Queue("/native path");Pump();Check(executed.back().command=="slash:/native path","Slash command still uses native client command entry point");
    const auto nativeBeforeKeys=executed.size();Queue("[TaB]");Pump();Queue("[ALT]");Pump();
    Check(keys==std::vector<UINT>{WM_KEYDOWN,WM_KEYUP,WM_SYSKEYDOWN,WM_SYSKEYUP} && executed.size()==nativeBeforeKeys,"Tab/Alt remain window-local key messages");
    now=200000;NativeToken()=0;Tick(window);Configure({"[tab]","","","","",""});
    const auto joinerKeys=keys.size();now=202999;Tick(window);Pump();
    Check(keys.size()==joinerKeys,"Joiner startup command keeps entry delay");
    now=203000;Tick(window);Pump();
    Check(keys.size()==joinerKeys+2,"Startup command works for zero-token joiner");
    Check(registrations==2,"Known registered message reused after sessions and reconfiguration");
    Configure({});contextReads=0;
    const auto shared=HudFrame::Capture();const auto capturedReads=contextReads.load();
    for(int i=0;i<10000;++i)Tick(window,shared);
    Check(capturedReads==5&&contextReads==capturedReads,"10000 snapshot command ticks perform no repeated native reads");
    Queue("frame snapshot stale");const auto beforeFrameExit=executed.size();Status()=0;Pump();
    Check(executed.size()==beforeFrameExit,"Dispatch rechecks exit after an earlier in-game frame snapshot");
    contextReads=0;const auto menuFrame=HudFrame::Capture();
    Check(!menuFrame.inGame&&menuFrame.nativeToken==0&&!menuFrame.loading&&contextReads==1,"Menu snapshot ignores stale game/loading globals");
    std::cout<<"10000 failed-delivery polls: 100 post attempts; zero-token joiners, queue ownership and native window-thread dispatch passed.\n";
    Shutdown();Status()=0;Pump();
}
}
int main(){try{
    Sessions();WNDCLASSW type{};type.lpfnWndProc=WindowProc;type.hInstance=GetModuleHandleW(nullptr);type.lpszClassName=L"D2RHUD command dispatch fixture";
    Check(RegisterClassW(&type)!=0,"Private window class registered");
    const auto window=CreateWindowExW(0,type.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,type.hInstance,nullptr);
    const auto second=CreateWindowExW(0,type.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,type.hInstance,nullptr);
    Check(window && second,"Private message-only windows created");Commands(window,second);
    DestroyWindow(second);DestroyWindow(window);UnregisterClassW(type.lpszClassName,type.hInstance);
    std::cout<<checks<<" command-dispatch checks passed.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
