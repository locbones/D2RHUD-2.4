#define NOMINMAX
#include <Windows.h>
#include <cassert>
#include <iostream>
#include <stdexcept>
#include "../d2rhud/plugin/D2RHUD/ChatAppearance.cpp"

DWORD64 Pattern::Address(uint32_t offset) { return 0x10000000ULL + offset; }
// Hook installation is checked separately against the shipped PE. This fixture
// calls the real scalar-edit callbacks with owned native-layout objects.
extern "C" LONG WINAPI DetourTransactionBegin(){return ERROR_NOT_SUPPORTED;}
extern "C" LONG WINAPI DetourTransactionCommit(){return ERROR_NOT_SUPPORTED;}
extern "C" LONG WINAPI DetourTransactionAbort(){return NO_ERROR;}
extern "C" LONG WINAPI DetourUpdateThread(HANDLE){return NO_ERROR;}
extern "C" LONG WINAPI DetourAttach(PVOID*,PVOID){return NO_ERROR;}

namespace {
using namespace HudChatAppearance;
struct Widget {
    std::array<uint8_t, 0x208> memory{};
    template<class T> void Set(size_t offset,T value){std::memcpy(memory.data()+offset,&value,sizeof(value));}
    Widget(const char* name,uintptr_t table=0){Set(0,table);Set(8,name);Set<uint64_t>(0x10,std::strlen(name));Set<float>(0x80,1.f);}
    void* Get(){return memory.data();}
    void Parent(Widget& value){Set(0x30,value.Get());}
};
int calls=0;float seenScale=0,seenAlpha=0,inputScale=0;int seenTop=0,seenHeight=0,seenWidth=0;bool throwInside=false;
void* inputRoot=nullptr;void* nestedInput=nullptr;
uintptr_t __fastcall Input(void*,void*){inputScale=Read<float>(inputRoot,0x80);return 0x12345678;}
void __fastcall Observe(void* widget){++calls;seenScale=Read<float>(widget,0x80);seenTop=Read<int>(widget,0x74);seenHeight=Read<int>(widget,0x7c);seenWidth=Read<int>(widget,0x78);seenAlpha=Read<float>(widget,0x94);if(nestedInput)assert(HitTest(nestedInput,nullptr)==0x12345678);if(throwInside)throw std::runtime_error("fixture");}
uintptr_t __fastcall InputMessage(void* widget,void*){Observe(widget);return 0xaabb;}
}
int main(){
    using namespace HudChatAppearance;
    assert(Unpack(Pack({true,100,70,100}))==Settings(true,100,70,100));
    assert(Unpack(Pack({true,-1,500,-5}))==Settings(true,60,100,25));
    assert(Unpack(Pack({true,999,-1,999}))==Settings(true,150,0,100));
    Widget panel("HUDMessagePanel"),list("GlobalChatListContainer",Pattern::Address(0x15ab2f0));list.Parent(panel);
    list.Set<int>(0x200,1);list.Set<int>(0x74,20);list.Set<int>(0x78,600);list.Set<int>(0x7c,400);
    originalDraw=Observe;originalUpdate=Observe;originalBackground=Observe;
    originalHitTest=Input;originalMessage=InputMessage;
    preferences.store(Pack({true,80,25,50}));Draw(list.Get());
    assert(calls==1&&std::abs(seenScale-.8f)<.001&&seenTop==220&&seenHeight==250);
    assert(Read<float>(list.Get(),0x80)==1&&Read<int>(list.Get(),0x74)==20&&Read<int>(list.Get(),0x7c)==400);
    Update(list.Get());assert(calls==2&&seenHeight==250&&Read<int>(list.Get(),0x7c)==400);
    Widget link("MessageText");link.Parent(list);inputRoot=list.Get();
    assert(HitTest(link.Get(),nullptr)==0x12345678&&std::abs(inputScale-.8f)<.001f&&Read<float>(list.Get(),0x80)==1);
    nestedInput=link.Get();assert(Message(list.Get(),nullptr)==0xaabb&&std::abs(inputScale-.8f)<.001f);
    Draw(list.Get());assert(std::abs(inputScale-.8f)<.001f&&Read<float>(list.Get(),0x80)==1);nestedInput=nullptr;
    preferences.store(Pack({true,150,100,25}));list.Set<float>(0x80,1.25f);Draw(list.Get());
    assert(std::abs(seenWidth*seenScale-750.f)<1.f&&std::abs(seenHeight*seenScale-125.f)<1.f&&seenTop==395);
    assert(Read<float>(list.Get(),0x80)==1.25f&&Read<int>(list.Get(),0x78)==600);
    list.Set<float>(0x80,1.f);preferences.store(Pack({true,80,25,50}));
    Widget historyPanel("MessageLogPanel_640x480"),history("ListContainer",Pattern::Address(0x15ab2f0));history.Parent(historyPanel);history.Set<int>(0x200,0);history.Set<int>(0x78,600);history.Set<int>(0x7c,330);
    Draw(history.Get());assert(std::abs(seenScale-.8f)<.001&&std::abs(seenHeight*seenScale-330.f)<1.f);
    Widget lobby("LobbyPanel");list.Parent(lobby);Draw(list.Get());assert(seenScale==1&&seenTop==20&&seenHeight==400);
    list.Parent(panel);list.Set<int>(0x200,2);Draw(list.Get());assert(seenScale==1&&seenHeight==400);list.Set<int>(0x200,1);
    preferences.store(Pack({}));Draw(list.Get());assert(seenScale==1&&seenHeight==400);
    preferences.store(Pack({true,80,25,50}));
    Widget background("MessageBackground",Pattern::Address(0x15ac8b0));background.Parent(list);background.Set<float>(0x94,.7f);
    Background(background.Get());assert(std::abs(seenAlpha-.175f)<.001f&&Read<float>(background.Get(),0x94)==.7f);
    background.Set<float>(0x94,.35f);Background(background.Get());assert(std::abs(seenAlpha-.0875f)<.001f&&Read<float>(background.Get(),0x94)==.35f);
    Widget unrelated("BackgroundBox",Pattern::Address(0x15ac8b0));unrelated.Parent(lobby);unrelated.Set<float>(0x94,.6f);Background(unrelated.Get());assert(seenAlpha==.6f);
    Widget inputPanel("ChatPanel");unrelated.Parent(inputPanel);Background(unrelated.Get());assert(std::abs(seenAlpha-.15f)<.001f&&Read<float>(unrelated.Get(),0x94)==.6f);
    Widget historyBackground("MessageLogBackground",Pattern::Address(0x15ac8b0));historyBackground.Parent(historyPanel);historyBackground.Set<float>(0x94,.8f);Background(historyBackground.Get());assert(std::abs(seenAlpha-.2f)<.001f&&Read<float>(historyBackground.Get(),0x94)==.8f);
    throwInside=true;try{Draw(list.Get());assert(false);}catch(const std::runtime_error&){}throwInside=false;
    assert(Read<float>(list.Get(),0x80)==1&&Read<int>(list.Get(),0x74)==20&&Read<int>(list.Get(),0x7c)==400);
    assert(activeList==nullptr);
    // A different game build refuses hooks, preserving all native behavior.
    installed.store(0);Configure({true,80,25,50});assert(installed.load()==-1);
    std::cout<<"Chat appearance: native scale/viewport/opacity, live/history isolation, retained geometry, default restore, exception cleanup, limits and unsupported-build fallback passed.\n";
}
