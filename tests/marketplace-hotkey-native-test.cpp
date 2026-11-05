#define NOMINMAX
#include "../d2rhud/plugin/MarketplacePanel.h"
#include <fstream>
#include <iostream>
#include <string>
#include <cstring>
#include <stdexcept>
#define CHECK(v) do{if(!(v))throw std::runtime_error("Native shortcut check "+std::to_string(__LINE__)+": " #v);}while(false)
struct D2UnitStrc {uint32_t dwUnitType=0,dwUnitId=7,dwFlags=0,dwAnimMode=1;};
D2UnitStrc hero;std::array<uint8_t,31> flags{};bool cursor=false,layout=true;
std::map<uint32_t,std::array<uint8_t,32>> code;
std::vector<int> opened,closed;std::vector<std::string> notices;
void* manager=reinterpret_cast<void*>(1);void** gpPanelManager=&manager;std::atomic<bool> gMarketplacePageSwitching=false;
D2UnitStrc* GetClientPlayerUnit(){return &hero;}
bool MarketplaceLiveCursor(){return cursor;}
void MarketplaceStashNotice(const std::string& text){notices.push_back(text);}
std::array<uint8_t,0x100> inventoryWidget{},bankWidget{},marketWidget{};
std::array<uintptr_t,8> marketVtable{};unsigned marketDraws=0;
void __fastcall DrawMarket(void* widget){CHECK(widget==marketWidget.data());++marketDraws;}
void* WidgetFindChild(void*,const char* name){
 if(!layout)return nullptr;
 if(!strcmp(name,"PlayerInventoryExpansionLayout"))return inventoryWidget.data();
 if(!strcmp(name,"PlayerInventoryOriginalLayout"))return nullptr;
 if(!strcmp(name,"MarketplaceWorkspace"))return marketWidget.data();
 return bankWidget.data();
}
bool __fastcall Open(int panel,bool sound){CHECK(panel==24&&!sound);opened.push_back(panel);flags[24]=flags[1]=1;inventoryWidget[0x51]=1;return true;}
bool __fastcall Close(int panel,bool sound){CHECK((panel==24||panel==1)&&!sound);closed.push_back(panel);flags[panel]=0;return true;}
namespace Pattern {uintptr_t Address(uint32_t rva){
 if(rva==0x865e0)return reinterpret_cast<uintptr_t>(Open);
 if(rva==0x86860)return reinterpret_cast<uintptr_t>(Close);
 if(rva>=0x1d53fb8&&rva<0x1d53fb8+flags.size())return reinterpret_cast<uintptr_t>(flags.data()+rva-0x1d53fb8);
 return reinterpret_cast<uintptr_t>(code.at(rva).data());
}}
namespace Marketplace::NativePanel {
template<class T>bool Read(const void* p,T& value){if(!p)return false;if constexpr(sizeof(T)==32){if(p==reinterpret_cast<const void*>(Close)){memcpy(&value,code.at(0x86860).data(),32);return true;}}memcpy(&value,p,sizeof(T));return true;}
template<class T>T Field(const void* p,size_t offset=0){T value{};Read(static_cast<const uint8_t*>(p)+offset,value);return value;}
bool WidgetLayoutSupported(){return true;}
void Visibility(void* p,bool show,bool messages=true){CHECK(!messages);static_cast<uint8_t*>(p)[0x51]=show?1:0;}

}
#include "../d2rhud/plugin/MarketplaceShortcutDisplay.inl"
#include "../d2rhud/plugin/MarketplaceHotkeyNative.inl"
uint32_t U32(const std::vector<uint8_t>& b,size_t p){CHECK(p+4<=b.size());uint32_t v=0;memcpy(&v,b.data()+p,4);return v;}
uint16_t U16(const std::vector<uint8_t>& b,size_t p){CHECK(p+2<=b.size());uint16_t v=0;memcpy(&v,b.data()+p,2);return v;}
int main(int argc,char** argv)try{
 CHECK(argc==2||argc==3);std::ifstream file(argv[1],std::ios::binary);std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),{});CHECK(U16(bytes,0)==0x5a4d);const auto pe=U32(bytes,0x3c);CHECK(U32(bytes,pe)==0x4550);
 auto at=[&](uint32_t rva){const auto section=pe+24+U16(bytes,pe+20);for(unsigned n=0;n<U16(bytes,pe+6);++n){const auto s=section+n*40,va=U32(bytes,s+12),size=U32(bytes,s+16);if(rva>=va&&rva+32<=va+size)return size_t(U32(bytes,s+20)+rva-va);}throw std::runtime_error("Missing PE range");};
 for(auto rva:{0x86608u,0x86860u,0x86030u,0x86149u})memcpy(code[rva].data(),bytes.data()+at(rva),32);
 // Independently bind the actual UI index to the installed bank-name branch.
 CHECK(U32(bytes,at(0x861bc+24*4))==0x86149);
 if(argc==3)code[0x86608][0]^=1;
 using namespace Marketplace;
 marketVtable[3]=reinterpret_cast<uintptr_t>(DrawMarket);auto table=marketVtable.data();memcpy(marketWidget.data(),&table,sizeof(table));
 inventoryWidget[0x50]=inventoryWidget[0x51]=1;
 CHECK(!DrawStandaloneMarketplace(bankWidget.data()));
 Hotkey::Request();MarketplaceHotkeyTick(true,false);
 if(argc==3){CHECK(opened.empty()&&!notices.empty()&&!Hotkey::session.Busy());std::cout<<"Changed executable signature refuses native panel calls.\n";return 0;}
 CHECK(opened==std::vector<int>{24}&&flags[1]&&flags[24]);
 CHECK(Panel::standaloneMarket&&inventoryWidget[0x50]&&!inventoryWidget[0x51]);
 CHECK(DrawStandaloneMarketplace(bankWidget.data())&&marketDraws==0); // Waiting: no native stash flash.
 marketWidget[0x51]=1;CHECK(DrawStandaloneMarketplace(bankWidget.data())&&marketDraws==1);
 Hotkey::Request();MarketplaceHotkeyTick(true,false);CHECK(opened.size()==1&&Panel::TakeNativeActions().empty());
 Sleep(20);Panel::Frame frame;frame.owner=hero.dwUnitId;frame.tick=GetTickCount64();Panel::Publish(frame);MarketplaceHotkeyTick(true,false);
 auto actions=Panel::TakeNativeActions();CHECK(actions==std::vector<uint64_t>{Panel::MessageHash("MarketOpen")});
 Panel::NativeView view;view.open=true;Panel::Present(view);MarketplaceHotkeyTick(true,false);
 view.open=false;Panel::Present(view);Panel::stashSyncing=true;MarketplaceHotkeyTick(true,false);CHECK(closed.empty());Panel::stashSyncing=false;MarketplaceHotkeyTick(true,false);
 CHECK((closed==std::vector<int>{24,1})&&!flags[24]&&!flags[1]);
 CHECK(!Panel::standaloneMarket&&inventoryWidget[0x51]&&inventoryWidget[0x50]);
 CHECK(!DrawStandaloneMarketplace(bankWidget.data())&&marketDraws==1);
 inventoryWidget[0x51]=0;flags[24]=flags[1]=1;frame.tick=GetTickCount64();Panel::Publish(frame);Hotkey::Request();MarketplaceHotkeyTick(true,false);CHECK(Panel::TakeNativeActions().size()==1&&opened.size()==1);
 view.open=true;Panel::Present(view);MarketplaceHotkeyTick(true,false);view.open=false;Panel::Present(view);MarketplaceHotkeyTick(true,false);CHECK(closed.size()==2&&flags[24]&&flags[1]&&!Panel::standaloneMarket&&!inventoryWidget[0x51]);
 flags[5]=1;Hotkey::Request();MarketplaceHotkeyTick(true,false);CHECK(Panel::TakeNativeActions().empty()&&opened.size()==1);flags[5]=0;
 layout=false;Hotkey::Request();MarketplaceHotkeyTick(true,false);CHECK(!notices.empty()&&opened.size()==1&&Panel::TakeNativeActions().empty());
 std::cout<<"Native shortcut adapter: installed 2.4 signatures/index, direct gameplay opening, fresh capture, one MarketOpen, restore only owned panels, sync deferral, chat/layout guards passed.\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
