#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <array>
#include <cassert>
#include <cstring>
#include <string>
#include <iostream>
namespace Pattern {inline uintptr_t Address(uint32_t rva){return rva;}}
namespace Marketplace::NativePanel {
template<class T>bool Read(const void* p,T& value){SIZE_T count=0;return p&&ReadProcessMemory(GetCurrentProcess(),p,&value,sizeof(value),&count)&&count==sizeof(value);}
template<class T>T Field(const void* p,size_t at=0){T result{};if(p)Read(static_cast<const uint8_t*>(p)+at,result);return result;}
}
#include "../d2rhud/plugin/MarketplaceNativeImages.inl"
template<class T>void Put(void* p,size_t at,T value){memcpy(static_cast<uint8_t*>(p)+at,&value,sizeof(value));}
static unsigned loads=0;static std::string lastPath;
static void __fastcall Load(void* resource,const char* path,bool special){
    assert(!special);++loads;lastPath=path;
    Put(resource,0,uintptr_t(lastPath.empty()||lastPath=="items/missing"?0:123));Put(resource,8,uintptr_t(0));
}
static uint64_t* __fastcall Size(void* resource,uint64_t* out,int frame){assert(frame==0);*out=Marketplace::NativePanel::Field<uintptr_t>(resource)?uint64_t(80)|(uint64_t(320)<<32):0;return out;}
int main(){
    using namespace Marketplace::NativePanel;
    std::array<uint8_t,0x120> widget{};std::array<uintptr_t,8> vtable{};
    vtable[1]=0x5790f0;vtable[3]=0x578d10;
    Put(widget.data(),0,vtable.data());Put(widget.data(),8,"MarketItemIcon");
    Put(widget.data(),0x88,widget.data()+0x110);Put(widget.data(),0x70,int32_t(10));Put(widget.data(),0x74,int32_t(20));
    auto set=[&](const std::string& path,int w=160,int h=160){return Images::SetWith(widget.data(),path,w,h,Load,Size);};
    assert(set("items/weapon/sword/crystal_sword"));assert(loads==1);
    assert(Field<int32_t>(widget.data(),0x70)==70&&Field<int32_t>(widget.data(),0x74)==20);
    assert(Field<int32_t>(widget.data(),0x78)==80&&Field<int32_t>(widget.data(),0x7c)==320);
    assert(Field<float>(widget.data(),0x80)==.5f);
    assert(set(lastPath)&&loads==1); // Native resources are not reloaded each frame.
    assert(set(lastPath,320,320)&&loads==1&&Field<float>(widget.data(),0x80)==1);
    assert(Field<int32_t>(widget.data(),0x70)==130); // Center stays relative to JSON origin.
    Put(widget.data(),0x110,uintptr_t(0)); // Game reloaded/cleared the native resource.
    assert(set(lastPath)&&loads==2);
    assert(!set("items/missing")&&loads==3);assert(!set("items/missing")&&loads==3);
    assert(!set("../../arbitrary")&&loads==3);assert(!set("items/../arbitrary")&&loads==3);
    assert(!set("items/weapon/sword/crystal_sword",0)&&loads==3);
    assert(!set("")&&loads==4); // Clearing goes through the game's release path too.
    assert(Field<uintptr_t>(widget.data(),0x88)==reinterpret_cast<uintptr_t>(widget.data()+0x110));
    vtable[1]=0;assert(!set("items/weapon/sword/crystal_sword")&&loads==4);
    Images::Forget();std::cout<<"Native item image resource, layout and cache tests passed.\n";
}
