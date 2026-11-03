// Ordinary native TextBoxWidget labels own a blz::string at +0x88. Never
// replace its pointer with HUD memory: the native destructor frees heap strings.
#include <array>
#include <string>
namespace Marketplace::NativePanel::Text {
using Assign=void*(__fastcall*)(void*,const char*,uint64_t);
inline bool CodeSupported(){
    // String assignment terminates at [pointer+length], then stores the byte
    // length. TextBox destruction frees +0x88 when capacity +0x98 is positive.
    static const bool supported=[](){std::array<uint8_t,11> assign{};std::array<uint8_t,15> destroy{};
    return Read(reinterpret_cast<void*>(Pattern::Address(0x6232c)),assign)&&
        assign==std::array<uint8_t,11>{0xc6,0x04,0x03,0,0x48,0x8b,0xc7,0x48,0x89,0x5f,0x08}&&
        Read(reinterpret_cast<void*>(Pattern::Address(0x57acfa)),destroy)&&
        destroy==std::array<uint8_t,15>{0x48,0x83,0xb9,0x98,0,0,0,0,0x8b,0xf2,0x48,0x8b,0xf9,0x7c,0x23};}();return supported;
}
inline bool WidgetSupported(void* widget){
    const auto table=Field<void*>(widget);return table&&reinterpret_cast<uintptr_t>(table)==Pattern::Address(0x15acb98)&&
        Field<uintptr_t>(table,0)==Pattern::Address(0x57acf0)&&Field<uintptr_t>(table,0x18)==Pattern::Address(0x57b700);
}
inline bool BindWith(void* widget,const std::string& text,Assign assign){
    if(!WidgetSupported(widget)||!assign||text.size()>4095)return false;
    const auto length=Field<uint64_t>(widget,0x90),capacity=Field<uint64_t>(widget,0x98)&0x7fffffffffffffffULL;
    auto current=Field<const char*>(widget,0x88);
    if(!current||length>capacity)return false;
    if(length==text.size()){
        std::array<char,4096> previous{};SIZE_T count=0;
        if(ReadProcessMemory(GetCurrentProcess(),current,previous.data(),text.size()+1,&count)&&count==text.size()+1&&
            !memcmp(previous.data(),text.c_str(),text.size()+1))return true;
    }
    assign(static_cast<uint8_t*>(widget)+0x88,text.c_str(),text.size());return true;
}
inline bool Bind(void* widget,const std::string& text){
    return CodeSupported()&&BindWith(widget,text,reinterpret_cast<Assign>(Pattern::Address(0x622d0)));
}
inline bool BindInput(void* widget,const std::string& text){
    const auto table=Field<void*>(widget);
    if(!table||Field<uintptr_t>(table,0x20)!=Pattern::Address(0x58e0c0)||!CodeSupported()||text.size()>2048)return false;
    const auto length=Field<uint64_t>(widget,0x528),capacity=Field<uint64_t>(widget,0x530)&0x7fffffffffffffffULL;
    if(!Field<void*>(widget,0x520)||length>capacity||length>2048)return false;
    reinterpret_cast<Assign>(Pattern::Address(0x622d0))(static_cast<uint8_t*>(widget)+0x520,text.c_str(),text.size());return true;
}
}
