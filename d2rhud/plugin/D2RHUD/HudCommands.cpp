#include "../../RuntimeTelemetry.h"
#include "HudCommands.h"
#include "CommandSession.h"
#include "../../Pattern.h"
#include "../../d2/D2Ptrs.h"
#include "../../ProcessLifetime.h"
#include <algorithm>
#include <atomic>
#include <deque>
#include <mutex>

namespace HudCommands {
namespace {
constexpr size_t QueueLimit=64;
struct Command {uint64_t session;std::string text;};
struct Runtime {
    std::mutex mutex;
    StartupSchedule schedule;
    std::deque<Command> pending;
    HWND window=nullptr;
    uintptr_t token=0;
    bool posted=false;
    std::atomic<UINT> message{0};
    bool postFailed=false;
    uint64_t failedAt=0;
};
Runtime& State(){return HudLifetime::State<Runtime>();}
using Context = HudFrame::Context;
Context CurrentContext(){return HudFrame::Capture();}
void Synchronize(Runtime& state,const Context& context,uint64_t now){
    if(state.schedule.SetSession(HudSession::Observe(context.inGame,context.nativeToken,context.loading,context.character),now)){
        state.pending.clear();state.posted=false;state.postFailed=false;++state.token;
    }
}
void Execute(const std::string& command,HWND window){
    std::string token=TrimToken(command);
    if(token.size()>=2 && token.front()=='[' && token.back()==']')
        token=TrimToken(token.substr(1,token.size()-2));
    std::transform(token.begin(),token.end(),token.begin(),[](unsigned char c){return char(std::tolower(c));});
    if(token=="tab" || token=="alt"){
        PostKeyTap(window,token=="tab"?VK_TAB:VK_MENU,token=="alt");return;
    }
    if(command.find('/')!=std::string::npos){
        if(CLIENT_playerCommand)CLIENT_playerCommand(command,command);
    }else{
        reinterpret_cast<void(__fastcall*)(const char*)>(Pattern::Address(0x76bc0))(command.c_str());
    }
}
void Post(Runtime& state,const Context& context,uint64_t now){
    if(state.posted || state.pending.empty() || !state.window || context.loading)return;
    if(state.postFailed && now>=state.failedAt && now-state.failedAt<100)return;
    auto message=state.message.load(std::memory_order_acquire);
    if(!message){
        // Register only when work needs delivery. A transient registration
        // failure is retried; ordinary window messages never register anything.
        message=RegisterWindowMessageW(L"D2RHUD.Commands.Dispatch.v1");
        if(message)state.message.store(message,std::memory_order_release);
    }
    ++state.token;
    state.posted=message && PostMessageW(state.window,message,WPARAM(state.token),0)!=FALSE;
    state.postFailed=!state.posted;
    if(state.postFailed)state.failedAt=GetTickCount64();
}
}
std::string TrimToken(const std::string& value){
    const auto start=value.find_first_not_of(" \t\r\n");
    if(start==std::string::npos)return {};
    return value.substr(start,value.find_last_not_of(" \t\r\n")-start+1);
}
void Configure(const StartupSchedule::Commands& commands){
    auto& state=State();std::lock_guard lock(state.mutex);
    if(state.schedule.Configure(commands,GetTickCount64())){
        state.pending.clear();state.posted=false;state.postFailed=false;++state.token;
    }
}
uint64_t Tick(HWND window){
    return Tick(window,CurrentContext());
}
uint64_t Tick(HWND window,const HudFrame::Context& context){
    auto& state=State();std::lock_guard lock(state.mutex);
    const auto now=GetTickCount64();Synchronize(state,context,now);
    if(state.window!=window){state.window=window;state.posted=false;state.postFailed=false;++state.token;}
    // Apply backpressure rather than dropping a startup command if the window
    // thread is busy. Manual commands have the same bounded queue.
    if(!context.loading && state.pending.size()<QueueLimit)
        if(auto command=state.schedule.TakeDue(now))state.pending.push_back({state.schedule.Session(),std::move(*command)});
    Post(state,context,now);
    return state.schedule.Session();
}
bool Queue(const std::string& command){
    if(command.empty() || command=="disabled")return false;
    auto& state=State();std::lock_guard lock(state.mutex);
    const auto now=GetTickCount64();const auto context=CurrentContext();Synchronize(state,context,now);
    if(!state.schedule.Session() || state.pending.size()>=QueueLimit)return false;
    state.pending.push_back({state.schedule.Session(),command});Post(state,context,now);return true;
}
bool HandleMessage(HWND window,UINT message,WPARAM wParam,LPARAM){
    HudTelemetry::Scope measured(HudTelemetry::Feature::Commands);
    const auto dispatch=State().message.load(std::memory_order_acquire);
    if(!dispatch || message!=dispatch)return false;
    // Even an accidental direct call must not execute native game code on a
    // producer thread. Leave the real posted message for the window owner.
    if(GetWindowThreadProcessId(window,nullptr)!=GetCurrentThreadId())return true;
    std::optional<Command> command;
    {
        auto& state=State();std::lock_guard lock(state.mutex);
        const auto context=CurrentContext();Synchronize(state,context,GetTickCount64());
        if(window!=state.window || !state.posted || wParam!=state.token)return true;
        state.posted=false;
        if(context.loading)return true;
        if(!state.pending.empty()){command=std::move(state.pending.front());state.pending.pop_front();}
    }
    // Native calls run on the game's window thread, outside the queue mutex.
    // Recheck the session at dispatch, including rapid restart/character changes.
    if(command){
        bool current=false;
        {
            auto& state=State();std::lock_guard lock(state.mutex);
            const auto context=CurrentContext();Synchronize(state,context,GetTickCount64());
            current=!context.loading && command->session==state.schedule.Session();
        }
        if(current)Execute(command->text,window);
    }
    return true;
}
void Shutdown(){
    auto& state=State();std::lock_guard lock(state.mutex);
    HudSession::Observe(false,0);state.schedule.SetSession(0,GetTickCount64());state.pending.clear();
    state.window=nullptr;state.posted=false;state.postFailed=false;++state.token;
}
void PostKeyTap(HWND window,WORD key,bool systemKey){
    if(!window)return;
    const LPARAM down=1|(LPARAM(MapVirtualKeyW(key,MAPVK_VK_TO_VSC))<<16);
    const LPARAM up=down|(LPARAM(1)<<30)|(LPARAM(1)<<31);
    PostMessageW(window,systemKey?WM_SYSKEYDOWN:WM_KEYDOWN,key,down);
    PostMessageW(window,systemKey?WM_SYSKEYUP:WM_KEYUP,key,up);
}
}
