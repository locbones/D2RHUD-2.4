#include "HUDControls.h"
#include "HUDControlsStore.h"
#include "KeyMappings.h"
#include "../Pattern.h"
#include <MinHook.h>
#include <array>
#include <atomic>
#include <algorithm>
#include <cstring>
#include <cctype>
#include <cstdlib>
#include <mutex>
#include <optional>
#include <string_view>

namespace HUDControls {
namespace {
constexpr int Action=61, NoAction=60;
constexpr uint32_t CurrentBindings=0x184d5f0, BackupBindings=0x184cba0;
#pragma pack(push,1)
struct Binding {uint32_t action;uint16_t key;uint32_t primary;};
#pragma pack(pop)
static_assert(sizeof(Binding)==10);
using Bindings=std::array<Binding,120>;
template<class T>T Native(uint32_t rva){return reinterpret_cast<T>(Pattern::Address(rva));}
template<class T>T Field(const void* p,size_t offset=0){T value{};SIZE_T n=0;if(p)ReadProcessMemory(GetCurrentProcess(),static_cast<const uint8_t*>(p)+offset,&value,sizeof(value),&n);return n==sizeof(value)?value:T{};}
template<class T>void Put(void* p,size_t offset,const T& value){memcpy(static_cast<uint8_t*>(p)+offset,&value,sizeof(value));}
constexpr uint64_t Hash(std::string_view text){uint64_t h=14695981039346656037ULL;for(auto c:text)h=(h^static_cast<uint8_t>(c))*1099511628211ULL;return h;}
using Panel=void(__fastcall*)(void*);
using Iterate=void*(__fastcall*)(void*,const int*,const int*,void*);
using Name=const char*(__fastcall*)(int);
using KeyName=const char*(__fastcall*)(int,int,bool);
using Color=void*(__fastcall*)(void*,bool);
using Assign=bool(__fastcall*)(int,int,uint16_t,const char**);
using Message=void(__fastcall*)(void*,const void*);
using Defaults=Binding*(__fastcall*)(int);
using Load=void(__fastcall*)();
Panel originalPopulate=nullptr,originalShow=nullptr,originalHide=nullptr,originalCancel=nullptr;
Iterate originalIterate=nullptr;
Name originalName=nullptr;
KeyName originalKeyName=nullptr;
Color originalColor=nullptr;
Assign originalAssign=nullptr;
Message originalMessage=nullptr;
Defaults originalDefaults=nullptr;
Load originalLoad=nullptr;
std::recursive_mutex mutex;
Store store;
Keys fallback;
std::filesystem::path storage;
std::string scope;
void* controls=nullptr;
bool visible=false,configured=false,pendingSave=false;
uint16_t held=Unbound;
std::atomic<bool> installed=false,toggle=false;
std::atomic<const char*> status="Native Controls integration is not installed.";

struct Signature {uint32_t rva;uint64_t hash;};
// Guard every hooked entry, including the native key validator used below.
constexpr std::array<Signature,12> signatures{{
    // Filled from the supported 2.4 executable; private regression checks all entries.
    {0xbec7a0,0xa584c86661e33644ULL},{0xbede00,0x20a8d68f025c60e2ULL},{0xbf06f0,0x2db8ae3837036489ULL},{0x11e190,0x33fe212d07376938ULL},
    {0xbf1290,0x43bb09e61ad714f0ULL},{0x11c7a0,0x96371dbff9a2a644ULL},{0xbeccb0,0x4c7fe5b2075bac68ULL},{0xbec970,0x0be64eaae6d2bf3eULL},
    {0xbeca50,0x3d12f7cc4406ee08ULL},{0xbedc30,0x2d538eef3275df73ULL},{0x11b320,0xa68cad3ff3ae0749ULL},{0x11bf90,0xd29ebea14bedfdd9ULL}
}};
bool Supported(){
    for(auto s:signatures){const auto bytes=Field<std::array<uint8_t,32>>(Native<void*>(s.rva));uint64_t h=14695981039346656037ULL;for(auto c:bytes)h=(h^c)*1099511628211ULL;if(h!=s.hash)return false;}return true;
}
Bindings& Current(){return *Native<Bindings*>(CurrentBindings);}
std::string Scope(){
    const auto mod=Field<std::array<char,128>>(Native<void*>(0x1bf084f));
    const auto selector=Field<uint32_t>(Native<void*>(0x1d442d8));
    const auto name=selector<8?Field<std::array<char,62>>(Native<void*>(0x1c9b200+selector*62)):std::array<char,62>{};
    return std::string(mod.data(),strnlen_s(mod.data(),mod.size()))+"|"+std::string(name.data(),strnlen_s(name.data(),name.size()));
}
bool ClearConflicts(Bindings& bindings,Keys keys){
    bool changed=false;for(auto& b:bindings)if(b.key!=Unbound&&(b.key==keys.primary||b.key==keys.secondary)){b.key=Unbound;changed=true;}return changed;
}
void Sync(bool force=false){
    if(storage.empty())return;const auto next=Scope();if(!force&&next==scope)return;
    scope=next;
    if(!store.Load(storage,scope,fallback))status="Open HUD Menu applies, but its saved Controls file could not be read.";
    ClearConflicts(Current(),store.saved);
    Native<void(__fastcall*)()>(0x11b260)();
}
void Flush(){
    if(!pendingSave)return;
    if(store.Save()){pendingSave=false;status="Open HUD Menu is configured in the game's Options > Controls.";}
    else status="Open HUD Menu applies, but its Controls file could not be saved.";
}
std::optional<uint16_t> Legacy(std::string text){
    if(text.empty())return uint16_t(Unbound);
    uint16_t modifiers=0,key=0;size_t start=0;
    while(start<text.size()){
        const auto end=text.find('+',start);auto part=text.substr(start,end-start);
        part.erase(std::remove_if(part.begin(),part.end(),[](unsigned char c){return std::isspace(c)!=0;}),part.end());
        std::transform(part.begin(),part.end(),part.begin(),[](unsigned char c){return char(std::toupper(c));});
        if(part.starts_with("VK_"))part.erase(0,3);
        if(part=="CTRL"||part=="CONTROL")modifiers|=0x2000;
        else if(part=="SHIFT")modifiers|=0x1000;
        else if(part=="ALT"||part=="MENU")modifiers|=0x4000;
        else {
            if(key)return std::nullopt;
            if(part.size()==1)key=uint8_t(part[0]);
            else if(part.size()>1&&part[0]=='F'&&std::all_of(part.begin()+1,part.end(),[](unsigned char c){return std::isdigit(c)!=0;})){
                const int f=std::atoi(part.c_str()+1);if(f<1||f>24)return std::nullopt;key=uint16_t(VK_F1+f-1);
            }else{
                auto found=keyMap.find(part);if(found==keyMap.end())found=keyMap.find("VK_"+part);
                if(found==keyMap.end())return std::nullopt;key=found->second;
                if(key==VK_LBUTTON)key=0x100;else if(key==VK_RBUTTON)key=0x101;else if(key==VK_MBUTTON)key=0x102;
            }
        }
        if(end==std::string::npos)break;start=end+1;
    }
    return key?std::optional<uint16_t>(uint16_t(key|modifiers)):std::nullopt;
}
const char* __fastcall NameHook(int action){return action==Action?"Open HUD Menu":originalName(action);}
const char* __fastcall KeyNameHook(int action,int primary,bool shorthand){
    if(action!=Action)return originalKeyName(action,primary,shorthand);
    std::lock_guard lock(mutex);
    // Use the game's own localized key names and modifier formatting. Only the
    // two descriptor slots are borrowed during this synchronous formatting call.
    auto& records=Current();const auto first=records[0],second=records[1];
    records[0]={Action,store.editing.primary,1};records[1]={Action,store.editing.secondary,0};
    const char* result=originalKeyName(Action,primary,shorthand);
    records[0]=first;records[1]=second;return result;
}
void* Selected(void* panel){
    const auto list=Field<void*>(panel,0x188);const int index=Field<int>(list,0x148);if(index<0)return nullptr;
    const auto data=Field<void*>(list,0xd8);if(size_t(index)>=Field<size_t>(data,0x60))return nullptr;
    const auto items=Field<void*>(data,0x58);const auto item=Field<void*>(items,size_t(index)*8);
    return item?Native<void*(__fastcall*)(void*)>(0xbedd50)(item):nullptr;
}
void* __fastcall ColorHook(void* widget,bool primary){
    if(Field<int>(widget,0x568)!=Action)return originalColor(widget,primary);
    std::lock_guard lock(mutex);const auto panel=Field<void*>(widget,0x570);
    if((Selected(panel)==widget&&Field<bool>(panel,0x190)==primary)||Field<int>(panel,0x194)==Action)return static_cast<uint8_t*>(widget)+0x520;
    const auto keys=store.editing;const auto key=primary?keys.primary:keys.secondary,other=primary?keys.secondary:keys.primary;
    return static_cast<uint8_t*>(widget)+(key!=Unbound?0x530:other!=Unbound?0x540:0x550);
}
bool __fastcall AssignHook(int action,int primary,uint16_t key,const char** error){
    std::lock_guard lock(mutex);
    if(action!=Action){
        const bool accepted=originalAssign(action,primary,key,error);
        if(accepted&&key!=Unbound){if(store.editing.primary==key)store.editing.primary=Unbound;if(store.editing.secondary==key)store.editing.secondary=Unbound;}
        return accepted;
    }
    auto& records=Current();const auto before=records;
    // Run the native validator/conflict resolver against a temporary descriptor
    // for an ordinary action. Its real bindings are restored before returning;
    // successful conflict removals for other actions are retained.
    Binding* slots[2]{};for(auto& b:records)if(b.action==0&&b.primary<2)slots[b.primary]=&b;
    if(!slots[0]||!slots[1])return false;
    slots[1]->key=store.editing.primary;slots[0]->key=store.editing.secondary;
    const auto modifiers=Field<std::array<uint8_t,9>>(Native<void*>(0x184d050));
    const bool accepted=originalAssign(0,primary,key,error);
    const Keys result{slots[1]->key,slots[0]->key};
    if(!accepted){records=before;Put(Native<void*>(0x184d050),0,modifiers);return false;}
    for(size_t n=0;n<records.size();++n)if(before[n].action==0){records[n]=before[n];if(key!=Unbound&&records[n].key==key)records[n].key=Unbound;}
    store.editing=result;return true;
}
void* __fastcall IterateHook(void* out,const int* begin,const int* end,void* capture){
    const auto result=originalIterate(out,begin,end,capture);
    // Both classic and expansion lists end with the two Party bindings.
    if(begin==Native<const int*>(0x1509cf0)&&end==begin+2){const int extra[]{NoAction,Action};originalIterate(out,extra,extra+2,out);}
    return result;
}
void __fastcall PopulateHook(void* panel){std::lock_guard lock(mutex);controls=panel;originalPopulate(panel);}
void __fastcall ShowHook(void* panel){
    std::lock_guard lock(mutex);controls=panel;Sync();store.Revert();visible=true;held=Unbound;originalShow(panel);
}
void __fastcall HideHook(void* panel){std::lock_guard lock(mutex);originalHide(panel);visible=false;held=Unbound;}
void __fastcall CancelHook(void* panel){std::lock_guard lock(mutex);store.Revert();originalCancel(panel);}
Keys CustomPreset(bool write=false,Keys keys={}){
    const auto saved=store.saved,editing=store.editing;
    // The game's Custom.key defaults belong to the mod, rather than one hero.
    store.Load(storage,scope.substr(0,scope.find('|'))+"|@custom-defaults",Keys{});
    const auto result=store.saved;
    if(write){store.editing=keys;store.Apply();pendingSave=true;Flush();}
    store.Load(storage,scope,fallback);store.saved=saved;store.editing=editing;
    return result;
}
Binding* __fastcall DefaultsHook(int preset){
    std::lock_guard lock(mutex);auto result=originalDefaults(preset);
    if(!visible||!result)return result;
    if(preset==2)store.editing=CustomPreset();else store.Defaults();
    // Keep the built-in defaults intact while removing any duplicate F12 (or
    // saved custom shortcut) from the copy consumed by the Controls panel.
    static Bindings defaults;defaults=Field<Bindings>(result);ClearConflicts(defaults,store.editing);return defaults.data();
}
void __fastcall LoadHook(){std::lock_guard lock(mutex);originalLoad();Sync(true);}
void __fastcall MessageHook(void* panel,const void* message){
    std::lock_guard lock(mutex);const auto type=Field<uint64_t>(message),verb=Field<uint64_t>(message,8);
    const bool accept=type==Hash("OptionsPanelMessage")&&verb==Hash("AcceptChanges");
    const bool custom=type==Hash("KeyBindingPanelMessage");
    const auto customKeys=store.editing;
    if(type==Hash("InputMessage")&&verb==Hash("KeyDown")&&Field<int>(panel,0x194)==NoAction){
        const auto payload=Field<void*>(message,16);
        if(Field<int>(payload,16)==VK_DELETE){
            const auto row=Selected(panel);
            if(Field<int>(row,0x568)==Action){
                auto& key=Field<bool>(panel,0x190)?store.editing.primary:store.editing.secondary;
                if(key!=Unbound){key=Unbound;Put(panel,0x1aa,true);Native<void(__fastcall*)(int,int,int,int,bool)>(0xe49e0)(Field<int>(panel,0x178),0,0,0,false);}
                return;
            }
        }
    }
    originalMessage(panel,message);
    if(accept){store.Apply();pendingSave=true;Flush();}
    if(custom&&verb==Hash("SaveCustomDefaultsConfirm"))CustomPreset(true,customKeys);
    if(custom&&verb==Hash("DeleteCustomDefaultsConfirm"))CustomPreset(true,Keys{});
}
}
bool Installed(){return installed.load();}
const char* Status(){return status.load();}
void Configure(const char* legacyBinding){
    std::lock_guard lock(mutex);if(configured)return;configured=true;
    if(legacyBinding)if(const auto key=Legacy(legacyBinding))fallback.primary=*key;
    if(installed.load())Sync(true);
}
void Tick(){std::lock_guard lock(mutex);if(!installed.load())return;Sync();if(pendingSave){static ULONGLONG last=0;const auto now=GetTickCount64();if(now-last>=1000){last=now;Flush();}}}
bool TakeToggle(){return toggle.exchange(false);}
bool HandleInput(UINT message,WPARAM key,LPARAM detail,bool textInput){
    if(!installed.load())return false;std::lock_guard lock(mutex);
    uint16_t code=Unbound;bool release=false,repeated=false;
    switch(message){
    case WM_KEYDOWN:case WM_SYSKEYDOWN:code=uint16_t(key);repeated=(detail&(LPARAM(1)<<30))!=0;break;
    case WM_KEYUP:case WM_SYSKEYUP:code=uint16_t(key);release=true;break;
    case WM_LBUTTONDOWN:code=0x100;break;case WM_LBUTTONUP:code=0x100;release=true;break;
    case WM_RBUTTONDOWN:code=0x101;break;case WM_RBUTTONUP:code=0x101;release=true;break;
    case WM_MBUTTONDOWN:code=0x102;break;case WM_MBUTTONUP:code=0x102;release=true;break;
    case WM_MOUSEWHEEL:code=GET_WHEEL_DELTA_WPARAM(key)>0?0x103:0x104;break;
    default:return false;
    }
    if(release){if(held!=Unbound&&(held&0xfff)==code){held=Unbound;return true;}return false;}
    if(repeated&&held!=Unbound&&(held&0xfff)==code)return true;
    if(visible||textInput||Field<uint8_t>(Native<void*>(0x1d53fb8+5)))return false;
    uint16_t modifiers=0;if(GetKeyState(VK_SHIFT)&0x8000)modifiers|=0x1000;if(GetKeyState(VK_CONTROL)&0x8000)modifiers|=0x2000;if(GetKeyState(VK_MENU)&0x8000)modifiers|=0x4000;
    const auto value=uint16_t(code|modifiers);const auto keys=store.saved;
    if(value!=keys.primary&&value!=keys.secondary)return false;
    held=value;if(!repeated)toggle=true;return true;
}
bool Install(){
    std::lock_guard lock(mutex);if(installed.load())return true;
    if(!Supported()){status="Native Controls unavailable: executable signatures differ. The HUD shortcut remains in Hotkeys & Commands.";return false;}
    wchar_t path[32768]{};const auto length=GetEnvironmentVariableW(L"LOCALAPPDATA",path,32768);if(!length||length>=32768)return false;
    storage=std::filesystem::path(path)/L"D2RHUD"/L"controls.json";
    const auto init=MH_Initialize();if(init!=MH_OK&&init!=MH_ERROR_ALREADY_INITIALIZED)return false;
    void* hooks[]{reinterpret_cast<void*>(PopulateHook),reinterpret_cast<void*>(IterateHook),reinterpret_cast<void*>(NameHook),reinterpret_cast<void*>(KeyNameHook),reinterpret_cast<void*>(ColorHook),reinterpret_cast<void*>(AssignHook),reinterpret_cast<void*>(MessageHook),reinterpret_cast<void*>(ShowHook),reinterpret_cast<void*>(HideHook),reinterpret_cast<void*>(CancelHook),reinterpret_cast<void*>(DefaultsHook),reinterpret_cast<void*>(LoadHook)};
    void** originals[]{reinterpret_cast<void**>(&originalPopulate),reinterpret_cast<void**>(&originalIterate),reinterpret_cast<void**>(&originalName),reinterpret_cast<void**>(&originalKeyName),reinterpret_cast<void**>(&originalColor),reinterpret_cast<void**>(&originalAssign),reinterpret_cast<void**>(&originalMessage),reinterpret_cast<void**>(&originalShow),reinterpret_cast<void**>(&originalHide),reinterpret_cast<void**>(&originalCancel),reinterpret_cast<void**>(&originalDefaults),reinterpret_cast<void**>(&originalLoad)};
    size_t created=0;for(;created<signatures.size();++created)if(MH_CreateHook(Native<void*>(signatures[created].rva),hooks[created],originals[created])!=MH_OK)break;
    bool ok=created==signatures.size();if(ok){for(auto s:signatures)if(MH_QueueEnableHook(Native<void*>(s.rva))!=MH_OK)ok=false;ok=ok&&MH_ApplyQueued()==MH_OK;}
    if(!ok){for(size_t n=0;n<created;++n){MH_DisableHook(Native<void*>(signatures[n].rva));MH_RemoveHook(Native<void*>(signatures[n].rva));}status="Native Controls hooks could not be installed. The HUD shortcut remains in Hotkeys & Commands.";return false;}
    installed=true;status="Open HUD Menu is configured in the game's Options > Controls.";return true;
}
void Uninstall(){
    std::lock_guard lock(mutex);Flush();if(installed.exchange(false)){for(auto s:signatures)MH_QueueDisableHook(Native<void*>(s.rva));if(MH_ApplyQueued()==MH_OK)for(auto s:signatures)MH_RemoveHook(Native<void*>(s.rva));}
    visible=false;controls=nullptr;toggle=false;held=Unbound;
}
}
