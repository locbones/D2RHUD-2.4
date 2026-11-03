#include <Windows.h>
#include <cassert>
#include <iostream>
#include <map>
#include "../d2rhud/plugin/MarketplacePanel.h"
namespace Pattern {inline uintptr_t Address(uint32_t rva);}
static std::map<uint32_t,uintptr_t> addresses;
uintptr_t Pattern::Address(uint32_t rva){return addresses[rva];}
namespace Marketplace::NativePanel {
template<class T>bool Read(const void* source,T& result){SIZE_T count=0;return source&&ReadProcessMemory(GetCurrentProcess(),source,&result,sizeof(T),&count)&&count==sizeof(T);}
template<class T>T Field(const void* source,size_t offset=0){T result{};if(source)Read(static_cast<const uint8_t*>(source)+offset,result);return result;}
}
#include "../d2rhud/plugin/MarketplaceNativeLinks.inl"
template<class T>void Put(void* target,size_t offset,T value){memcpy(static_cast<uint8_t*>(target)+offset,&value,sizeof(value));}
static std::string caption;static int assigns=0;
static void* __fastcall AssignCaption(void* target,const char* text,uint64_t length){assert(target);caption.assign(text,length);++assigns;return target;}
int main(){
    using namespace Marketplace;using namespace NativePanel;using namespace Links;
    Record record;assert(Build(record,"[Zod]","00Aa12"));assert(record.length==5&&record.size==3&&record.bytes[1]==0xaa&&record.item&&record.url==record.urlInline);
    assert(record.urlCapacity>>63&&record.urlLength==0);assert(Build(record,"[\xc3\xa9]","ab")&&record.length==3);
    assert(!Build(record,"Bad","a"));assert(!Build(record,"Bad","zz"));assert(!Build(record,"Bad",std::string(2050,'a')));
    assert(Build(record,"Max",std::string(2048,'f'))&&record.size==1024);assert(!Build(record,"", "aa"));
    std::array<uint8_t,0x750> widget{};std::array<uintptr_t,32> vtable{};
    addresses={{0x15b1d60,reinterpret_cast<uintptr_t>(vtable.data())},{0x5b0f30,123},{0x5b0fb0,456}};
    vtable[3]=123;vtable[4]=456;Put(widget.data(),0,vtable.data());
    Panel::PreviewUpdate value{"ItemName","Zod Rune","ab00ff"};assert(BindWith(widget.data(),value,&AssignCaption));
    assert(caption=="[Zod Rune]"&&assigns==1&&Field<uint64_t>(widget.data(),0x528)==1&&Field<uint64_t>(widget.data(),0x530)>>63);
    auto pointer=Field<Record*>(widget.data(),0x520);assert(pointer&&pointer->size==3&&pointer->item);
    assert(BindWith(widget.data(),value,&AssignCaption)&&assigns==1); // Native caption ownership is not rewritten each frame.
    pointer->item=false;assert(BindWith(widget.data(),value,&AssignCaption)&&pointer->item); // After native destructor/reconstruction.
    value.itemHex=std::string(2050,'a');assert(!BindWith(widget.data(),value,&AssignCaption)&&Field<uint64_t>(widget.data(),0x528)==0);
    value.itemHex.clear();value.label="Agreement draft";assert(!BindWith(widget.data(),value,&AssignCaption)&&caption=="Agreement draft");
    auto foreign=widget;Put(foreign.data(),0x520,reinterpret_cast<void*>(1234));assert(!BindWith(foreign.data(),value,&AssignCaption));
    vtable[4]=0;assert(!BindWith(widget.data(),value,&AssignCaption));
    std::cout<<"Native item-link bounds, exact bytes, UTF-8 hit length, external ownership, caption caching and foreign-widget guards passed.\n";
}
