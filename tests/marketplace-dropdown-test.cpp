#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>

static std::map<uint32_t,uintptr_t> addresses;
namespace Pattern {inline uintptr_t Address(uint32_t rva){return addresses[rva];}}
namespace Marketplace::NativePanel {
template<class T>bool Read(const void* source,T& value){SIZE_T size=0;return source&&ReadProcessMemory(GetCurrentProcess(),source,&value,sizeof(value),&size)&&size==sizeof(value);}
template<class T>T Field(const void* source,size_t offset=0){T value{};if(source)Read(static_cast<const uint8_t*>(source)+offset,value);return value;}
}
#include "../d2rhud/plugin/MarketplaceNativeDropdowns.inl"

template<class T>void Put(void* target,size_t offset,T value){memcpy(static_cast<uint8_t*>(target)+offset,&value,sizeof(value));}
static int calls=0,caption=-1;
static void __fastcall Select(void* widget,int selection){++calls;caption=selection;Put(widget,0x178c,selection);}

int main(){
    using namespace Marketplace::NativePanel;
    std::array<uint8_t,0x1790> widget{};std::array<uintptr_t,8> table{};
    std::array<uint8_t,20> states{0x48,0x8d,0x0c,0xb6,0x48,0x8b,0x94,0xcf,0x88,0x0d,0,0,0x4c,0x8b,0x84,0xcf,0x90,0x0d,0,0};
    std::array<uint8_t,6> selected{0x89,0xb7,0x8c,0x17,0,0},count{0x3b,0xb1,0x88,0x17,0,0};
    addresses={{0x15b4038,reinterpret_cast<uintptr_t>(table.data())},{0x5cb410,0x12345678},
        {0x5cba36,reinterpret_cast<uintptr_t>(states.data())},{0x5cbab3,reinterpret_cast<uintptr_t>(selected.data())},
        {0x5cb9f3,reinterpret_cast<uintptr_t>(count.data())},{0x5cb9d0,reinterpret_cast<uintptr_t>(&Select)}};
    table[4]=0x12345678;Put(widget.data(),0,table.data());Put(widget.data(),0x1788,8);Put(widget.data(),0x178c,-1);
    auto bound=BindChoice(widget.data(),7,10);assert(bound&&bound->selection==7&&bound->revision==10&&caption==7&&calls==1);
    // Native mouse/keyboard edits survive presentation frames using the prior
    // model revision; they must not snap back to the model's stale selection.
    Put(widget.data(),0x178c,2);bound=BindChoice(widget.data(),7,10);assert(bound&&bound->selection==2&&calls==1);
    bound=BindChoice(widget.data(),0,11);assert(bound&&bound->selection==0&&caption==0&&calls==2);
    for(int n=0;n<8;++n){bound=BindChoice(widget.data(),n,20+n);assert(bound&&bound->selection==n&&caption==n);}
    const auto before=calls;
    assert(!BindChoice(nullptr,0,50));assert(!BindChoice(widget.data(),-1,50));assert(!BindChoice(widget.data(),8,50));
    Put(widget.data(),0x1788,65);assert(!BindChoice(widget.data(),0,50));Put(widget.data(),0x1788,0);assert(!BindChoice(widget.data(),0,50));Put(widget.data(),0x1788,8);
    table[4]=0x87654321;assert(!BindChoice(widget.data(),0,50));table[4]=0x12345678;
    Put(widget.data(),0,reinterpret_cast<void*>(1));assert(!BindChoice(widget.data(),0,50));Put(widget.data(),0,table.data());
    states[0]^=1;assert(!BindChoice(widget.data(),0,50));states[0]^=1;
    selected[0]^=1;assert(!BindChoice(widget.data(),0,50));selected[0]^=1;
    count[0]^=1;assert(!BindChoice(widget.data(),0,50));count[0]^=1;
    Put(widget.data(),0x178c,8);assert(!BindChoice(widget.data(),7,27));assert(calls==before);
    // Rebuilding the state list changes its count and resets the binding even
    // if the caller still has the same revision.
    Put(widget.data(),0x1788,3);bound=BindChoice(widget.data(),1,27);assert(bound&&bound->selection==1&&caption==1&&calls==before+1);
    std::cout<<"Marketplace native dropdown bindings passed.\n";
}
