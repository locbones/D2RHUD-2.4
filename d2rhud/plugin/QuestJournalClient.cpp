#include "QuestJournalClient.h"
#include "../Pattern.h"
#include "../d2/Reclass.h"
#include <MinHook.h>
#include <atomic>
#include <array>
#include <cstring>
#include <utility>

namespace QuestJournal::ClientUI {
namespace {
template<class T>T Native(uint32_t rva){return reinterpret_cast<T>(Pattern::Address(rva));}
template<class T>T Field(const void* p,size_t offset=0){T value{};SIZE_T n=0;if(p)ReadProcessMemory(GetCurrentProcess(),static_cast<const uint8_t*>(p)+offset,&value,sizeof(value),&n);return n==sizeof(value)?value:T{};}
using Availability=bool(__fastcall*)(int);
using Click=void(__fastcall*)(void*);
using Panel=bool(__fastcall*)(int,bool);
Availability originalAvailability=nullptr;
Click originalClick=nullptr;
Panel originalOpen=nullptr,originalToggle=nullptr;
constexpr int QuestPanel=14,QuestAlert=16,QuestCategory=2;
constexpr uint32_t UIFlags=0x1d53fb8;
// D2R's LevelUpButtonWidget category 2 and native UI indices differ from LoD.
// First 32 bytes, also checked by the private executable-image regression.
struct Signature {uint32_t rva;uint64_t hash;};
constexpr std::array<Signature,5> signatures{{
    {0xbcfaa0,0xb8a6e3732f62e4aeULL},{0xbd0550,0x709367658cd6254dULL},
    {0x865e0,0xdc45f8215f71078cULL},{0x86a50,0x156ad3b7c0708202ULL},
    {0x86860,0x8773941eeb5f8596ULL}
}};
constexpr std::array<uint32_t,4> hooks{0xbcfaa0,0xbd0550,0x865e0,0x86a50};
std::atomic<bool> installed=false,notification=false;
std::atomic<bool> journalAlert=false;
enum class Request:uint8_t {None,Open,Toggle};
std::atomic<Request> request=Request::None;
std::atomic<const char*> message="Journal shortcuts are not installed.";
Notice notice;
thread_local bool repeatedInput=false,consumedInput=false;
D2UnitStrc* Player(){
    const auto selector=Field<uint32_t>(Native<void*>(0x1d442d8));
    const auto getter=Native<D2UnitStrc*(__fastcall*)(uint32_t)>(0x6b050);
    return selector<8&&getter?getter(selector):nullptr;
}
bool CanRoute(){
    if(!Bindings())return false;const auto player=Player();
    return player&&player->dwUnitType==0&&!(player->dwFlags&0x10000)&&player->dwAnimMode!=0&&player->dwAnimMode!=17;
}
bool Supported(){
    for(const auto s:signatures){
        std::array<uint8_t,32> bytes{};SIZE_T n=0;
        if(!ReadProcessMemory(GetCurrentProcess(),Native<void*>(s.rva),bytes.data(),bytes.size(),&n)||n!=bytes.size())return false;
        uint64_t hash=14695981039346656037ULL;for(auto byte:bytes)hash=(hash^byte)*1099511628211ULL;
        if(hash!=s.hash)return false;
    }return true;
}
bool __fastcall AvailabilityHook(int category){
    const bool native=originalAvailability(category);
    return native||(category==QuestCategory&&notification.load()&&CanRoute());
}
void __fastcall ClickHook(void* widget){
    const auto count=Field<int>(widget,0x9fc);const auto mask=Field<uint32_t>(widget,0xbc4);
    uint32_t quests=0;
    if(widget&&count>0&&count<=3&&CanRoute())for(int n=0;n<count;++n)
        if(Field<int>(widget,0x9f0+size_t(n)*4)==QuestCategory)quests|=1u<<n;
    quests&=mask;
    if(!quests){originalClick(widget);return;}
    RequestOpen();
    // Controller widgets can combine stat, skill and quest actions. Run the
    // native handler for the remaining actions and retain its housekeeping.
    const auto remaining=mask&~quests;memcpy(static_cast<uint8_t*>(widget)+0xbc4,&remaining,4);
    originalClick(widget);
    const auto restored=Field<uint32_t>(widget,0xbc4)|quests;memcpy(static_cast<uint8_t*>(widget)+0xbc4,&restored,4);
}
bool __fastcall OpenHook(int panel,bool sound){
    if(panel==QuestPanel&&CanRoute()){RequestOpen();return true;}
    if(panel==QuestAlert)journalAlert=false; // An engine-owned alert takes ownership.
    return originalOpen(panel,sound);
}
bool __fastcall ToggleHook(int panel,bool sound){
    if(panel==QuestPanel&&CanRoute()){RequestToggle();return true;}
    return originalToggle(panel,sound);
}
}
void RequestOpen(){request.store(Request::Open);}
void RequestToggle(){request.store(Request::Toggle);}
void BeginInput(bool repeated){repeatedInput=repeated;consumedInput=false;}
void Hotkey(){consumedInput=true;if(!repeatedInput)RequestToggle();}
bool ConsumeInput(){return std::exchange(consumedInput,false);}
void Update(bool& open,bool playing){
    const auto rules=Bindings();const auto view=playing&&rules?CurrentView():View{};
    if(notice.Changed(view))open=false;
    const auto action=request.exchange(Request::None);
    if(playing&&CanRoute()){
        const bool opening=action==Request::Open||(action==Request::Toggle&&!open);
        if(action==Request::Open)open=true;else if(action==Request::Toggle)open=!open;
        if(opening&&installed.load()&&Field<uint8_t>(Native<void*>(UIFlags+QuestAlert)))
            Native<Panel>(0x86860)(QuestAlert,false);
    }
    notice.Observe(view,open);
    notification.store(playing&&rules&&rules->error.empty()&&notice.pending&&!open&&CanRoute());
    if(installed.load()&&playing&&CanRoute()){
        // Availability alone cannot show a widget inside a closed native panel.
        // Open the quest-alert container as well as supplying its button state.
        if(notification.load()&&!Field<uint8_t>(Native<void*>(UIFlags+QuestAlert)))
            journalAlert=originalOpen(QuestAlert,false);
        else if(!notification.load()&&journalAlert.exchange(false)&&Field<uint8_t>(Native<void*>(UIFlags+QuestAlert)))
            Native<Panel>(0x86860)(QuestAlert,false);
    }else journalAlert=false;
}
const char* Status(){return message.load();}
bool Install(){
    if(installed.load())return true;
    if(!Bindings()){message="Journal shortcuts are restricted to RMD-MP.";return false;}
    if(!Supported()){message="Native journal shortcuts unavailable: executable signatures differ. The journal menu/hotkey remains available.";return false;}
    const auto initialized=MH_Initialize();if(initialized!=MH_OK&&initialized!=MH_ERROR_ALREADY_INITIALIZED)return false;
    void* replacements[]{reinterpret_cast<void*>(AvailabilityHook),reinterpret_cast<void*>(ClickHook),reinterpret_cast<void*>(OpenHook),reinterpret_cast<void*>(ToggleHook)};
    void** originals[]{reinterpret_cast<void**>(&originalAvailability),reinterpret_cast<void**>(&originalClick),reinterpret_cast<void**>(&originalOpen),reinterpret_cast<void**>(&originalToggle)};
    size_t created=0;for(;created<hooks.size();++created)if(MH_CreateHook(Native<void*>(hooks[created]),replacements[created],originals[created])!=MH_OK)break;
    bool ok=created==hooks.size();if(ok){for(auto rva:hooks)if(MH_QueueEnableHook(Native<void*>(rva))!=MH_OK)ok=false;ok=ok&&MH_ApplyQueued()==MH_OK;}
    if(!ok){for(size_t n=0;n<created;++n){MH_DisableHook(Native<void*>(hooks[n]));MH_RemoveHook(Native<void*>(hooks[n]));}message="Native journal shortcut hooks could not be installed.";return false;}
    installed=true;message="RMD-MP quest alerts and quest-panel shortcuts open the journal.";return true;
}
void Uninstall(){
    if(installed.exchange(false)){
        for(auto rva:hooks)MH_QueueDisableHook(Native<void*>(rva));
        if(MH_ApplyQueued()==MH_OK)for(auto rva:hooks)MH_RemoveHook(Native<void*>(rva));
    }
    notification=false;request=Request::None;notice={};journalAlert=false;
}
}
