// Included after MarketplaceNative.inl's Read/Field helpers, on the bank paint
// thread. The existing ImageWidget owns its sprite; this bridge never creates
// an inventory unit, decodes an image, or draws through the HUD renderer.
#include <map>
#include <string>
#include <array>
#include <cmath>
#include <algorithm>
namespace Marketplace::NativePanel::Images {
using Load = void(__fastcall*)(void*,const char*,bool);
using Size = uint64_t*(__fastcall*)(void*,uint64_t*,int);
struct State {
    void* parent=nullptr;const char* name=nullptr;std::string path;
    int32_t x=0,y=0,width=0,height=0;bool initialized=false,loaded=false;
    std::array<uintptr_t,2> resources{};
};
inline std::map<void*,State>& States(){static std::map<void*,State> states;return states;}
inline void Forget(){States().clear();} // Resources remain owned by the game.
inline bool ValidPath(const std::string& path){
    if(path.empty())return true;
    if(path.size()>230||path.rfind("items/",0)!=0||path.find("..")!=std::string::npos)return false;
    for(unsigned char c:path)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='/'||c=='_'||c=='-'||c==' '||c=='\''))return false;
    return path.back()!='/';
}
inline bool WidgetSupported(void* widget){
    const auto table=Field<void*>(widget);
    return table&&Field<uintptr_t>(table,8)==Pattern::Address(0x5790f0)&&
        Field<uintptr_t>(table,0x18)==Pattern::Address(0x578d10)&&
        Field<void*>(widget,0x88)==static_cast<uint8_t*>(widget)+0x110;
}
inline bool Supported(){
    static const bool supported=[](){
        const std::array<std::pair<uint32_t,uint64_t>,3> signatures{{
            {0x573670,0x3b343f20fda7d0e6ULL},{0x573930,0xbe52d594cf07a476ULL},
            {0x5790f0,0xf67e0d40bfeffb52ULL}}};
        for(const auto& [rva,expected]:signatures){
            std::array<uint8_t,32> code{};if(!Read(reinterpret_cast<void*>(Pattern::Address(rva)),code))return false;
            uint64_t hash=14695981039346656037ULL;for(auto byte:code){hash^=byte;hash*=1099511628211ULL;}
            if(hash!=expected)return false;
        }return true;
    }();return supported;
}
// The icon's initial JSON x/y is the containing box's top-left. ImageWidget's
// OnLoad replaces JSON width/height with the sprite dimensions, so the caller
// supplies box dimensions explicitly. Native scale preserves aspect ratio.
inline bool SetWith(void* widget,const std::string& path,int boxWidth,int boxHeight,Load load,Size size){
    if(!load||!size||boxWidth<1||boxHeight<1||boxWidth>4096||boxHeight>4096||!ValidPath(path)||!WidgetSupported(widget))return false;
    auto& states=States();if(states.size()>128)states.clear();auto& state=states[widget];
    const auto parent=Field<void*>(widget,0x30);const auto name=Field<const char*>(widget,8);
    if(!state.initialized||state.parent!=parent||state.name!=name){
        state={};state.initialized=true;state.parent=parent;state.name=name;
        state.x=Field<int32_t>(widget,0x70);state.y=Field<int32_t>(widget,0x74);
    }
    auto resource=static_cast<uint8_t*>(widget)+0x110;
    const std::array<uintptr_t,2> current{Field<uintptr_t>(resource),Field<uintptr_t>(resource,8)};
    if(state.path!=path||state.resources!=current){
        // This is the same release/load routine called by ImageWidget::OnLoad.
        // Its destructor later releases +0x110, including our replacement.
        load(resource,path.c_str(),false);state.path=path;
        state.resources={Field<uintptr_t>(resource),Field<uintptr_t>(resource,8)};
        uint64_t dimensions=0;size(resource,&dimensions,0);
        state.width=int32_t(dimensions);state.height=int32_t(dimensions>>32);
        state.loaded=!path.empty()&&(state.resources[0]||state.resources[1])&&
            state.width>0&&state.height>0&&state.width<=16384&&state.height<=16384;
    }
    if(!state.loaded)return false;
    const float scale=(std::min)(float(boxWidth)/state.width,float(boxHeight)/state.height);
    const std::array<int32_t,4> rect{state.x+int32_t(std::lround((boxWidth-state.width*scale)*.5f)),
        state.y+int32_t(std::lround((boxHeight-state.height*scale)*.5f)),state.width,state.height};
    SIZE_T written=0;
    return WriteProcessMemory(GetCurrentProcess(),static_cast<uint8_t*>(widget)+0x70,rect.data(),sizeof(rect),&written)&&written==sizeof(rect)&&
        WriteProcessMemory(GetCurrentProcess(),static_cast<uint8_t*>(widget)+0x80,&scale,sizeof(scale),&written)&&written==sizeof(scale);
}
inline bool Set(void* widget,const std::string& path,int boxWidth,int boxHeight){
    return Supported()&&SetWith(widget,path,boxWidth,boxHeight,
        reinterpret_cast<Load>(Pattern::Address(0x573670)),reinterpret_cast<Size>(Pattern::Address(0x573930)));
}
}
