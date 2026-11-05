#define NOMINMAX
#include "../d2rhud/plugin/MarketplacePanel.h"
#include <fstream>
#include <iostream>
#include <cstring>
#include <stdexcept>
#define CHECK(v) do{if(!(v))throw std::runtime_error("Page highlight check "+std::to_string(__LINE__)+": " #v);}while(false)
std::array<uint8_t,0x100> bank{},content{};
std::array<uint8_t,0x1800> pages{};
std::array<uint8_t,0x200> popup{},list{};
std::array<std::array<uint8_t,0x100>,64> row{};
std::array<void*,64> rows{};
void* active=nullptr;void* manager=reinterpret_cast<void*>(1);void** gpPanelManager=&manager;
bool choiceSupported=true;unsigned getterCalls=0;
std::map<void*,std::array<int32_t,4>> rectangles;
std::map<uint32_t,std::array<uint8_t,32>> code;
void* WidgetFindChild(void* parent,const char* name){
 if(parent==bank.data()&&!strcmp(name,"BankPages"))return pages.data();
 if(parent==manager&&!strcmp(name,"DropdownListContents"))return popup.data();
 if(parent==popup.data()&&!strcmp(name,"Items"))return list.data();
 return nullptr;
}
void* __fastcall ResolvePopup(void* widget){CHECK(widget==pages.data());return popup[0x51]?popup.data():nullptr;}
uint64_t __fastcall Position(void* widget,uint64_t* out){++getterCalls;auto r=rectangles.at(widget);*out=uint32_t(r[0])|(uint64_t(uint32_t(r[1]))<<32);return *out;}
uint64_t __fastcall Size(void* widget,uint64_t* out){++getterCalls;auto r=rectangles.at(widget);*out=uint32_t(r[2])|(uint64_t(uint32_t(r[3]))<<32);return *out;}
namespace Pattern {uintptr_t Address(uint32_t rva){
 if(rva==0x1d7c988)return reinterpret_cast<uintptr_t>(&active);
 if(rva==0x5cbad0)return reinterpret_cast<uintptr_t>(ResolvePopup);
 if(rva==0x576f00)return reinterpret_cast<uintptr_t>(Position);
 if(rva==0x158120)return reinterpret_cast<uintptr_t>(Size);
 return reinterpret_cast<uintptr_t>(code.at(rva).data());
}}
namespace Marketplace::NativePanel {
template<class T>bool Read(const void* p,T& value){if(!p)return false;if constexpr(sizeof(T)==32){if(p==reinterpret_cast<void*>(ResolvePopup)){memcpy(&value,code.at(0x5cbad0).data(),32);return true;}if(p==reinterpret_cast<void*>(Size)){memcpy(&value,code.at(0x158120).data(),32);return true;}}memcpy(&value,p,sizeof(T));return true;}
template<class T>T Field(const void* p,size_t offset=0){T v{};if(p)Read(static_cast<const uint8_t*>(p)+offset,v);return v;}
bool WidgetLayoutSupported(){return true;}
bool ChoiceSupported(void* p){return p==pages.data()&&choiceSupported;}
}
#include "../d2rhud/plugin/MarketplaceNativePageGlow.inl"
template<class T>void Put(void* p,size_t offset,T value){memcpy(static_cast<uint8_t*>(p)+offset,&value,sizeof(value));}
uint32_t U32(const std::vector<uint8_t>& b,size_t p){CHECK(p+4<=b.size());uint32_t v;memcpy(&v,b.data()+p,4);return v;}
uint16_t U16(const std::vector<uint8_t>& b,size_t p){CHECK(p+2<=b.size());uint16_t v;memcpy(&v,b.data()+p,2);return v;}
int main(int argc,char** argv)try{
 CHECK(argc==2||argc==3);std::ifstream f(argv[1],std::ios::binary);std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),{});
 CHECK(U16(bytes,0)==0x5a4d);const auto pe=U32(bytes,0x3c);CHECK(U32(bytes,pe)==0x4550);
 auto at=[&](uint32_t rva){const auto section=pe+24+U16(bytes,pe+20);for(unsigned n=0;n<U16(bytes,pe+6);++n){const auto s=section+n*40,va=U32(bytes,s+12),size=U32(bytes,s+16);if(rva>=va&&rva+32<=va+size)return size_t(U32(bytes,s+20)+rva-va);}throw std::runtime_error("Missing native PE range");};
 for(auto rva:{0x5cbad0u,0x5cbff4u,0x158120u})memcpy(code[rva].data(),bytes.data()+at(rva),32);
 // Native popup resolver compares the same active-widget global; row lookup
 // uses list +d8, its children +58 and the matching zero-based state index.
 CHECK(U32(bytes,at(0x5cbb3e+3))+0x5cbb45==0x1d7c988);
 if(argc==3)code[0x5cbff4][0]^=1;
 Put(popup.data(),0x1a0,static_cast<void*>(list.data()));Put(list.data(),0xd8,static_cast<void*>(content.data()));
 Put(content.data(),0x58,rows.data());Put(content.data(),0x60,uint64_t(64));Put(pages.data(),0x1788,int(64));Put(pages.data(),0x178c,int(1));
 popup[0x51]=list[0x51]=1;
 for(size_t i=0;i<row.size();++i){rows[i]=row[i].data();row[i][0x51]=1;Put(row[i].data(),0x30,static_cast<void*>(content.data()));rectangles[row[i].data()]={100,100+int32_t(i)*40,400,40};}
 rectangles[list.data()]={100,100,400,600};active=pages.data();
 const auto before=pages;
 using namespace Marketplace;using namespace NativePanel;Panel::marketPage=11;
 auto highlight=MarketPageHighlight(bank.data(),1000,1000);
 if(argc==3){CHECK(!highlight.page&&!getterCalls);std::cout<<"Changed native popup code safely suppresses highlighting.\n";return 0;}
 CHECK(highlight.page==11&&highlight.row.top==.5f&&highlight.row.bottom==.54f&&highlight.clip.bottom==.7f);
 CHECK(pages==before); // No selection, caption, popup or page changes.
 // Scroll the native content; only coordinates change, never the market index.
 rectangles[row[10].data()][1]=120;highlight=MarketPageHighlight(bank.data(),1000,1000);CHECK(highlight.page==11&&highlight.row.top==.12f);
 rectangles[row[10].data()][1]=-100;CHECK(!MarketPageHighlight(bank.data(),1000,1000).page);
 rectangles[row[10].data()][1]=80;highlight=MarketPageHighlight(bank.data(),1000,1000);CHECK(highlight.page==11&&highlight.row.top==.08f&&highlight.clip.top==.1f);
 active=nullptr;getterCalls=0;CHECK(!MarketPageHighlight(bank.data(),1000,1000).page&&!getterCalls);
 active=list.data();CHECK(!MarketPageHighlight(bank.data(),1000,1000).page&&!getterCalls);active=pages.data();
 Panel::marketPage=0;CHECK(!MarketPageHighlight(bank.data(),1000,1000).page);Panel::marketPage=65;CHECK(!MarketPageHighlight(bank.data(),1000,1000).page);Panel::marketPage=11;
 popup[0x51]=0;CHECK(!MarketPageHighlight(bank.data(),1000,1000).page);popup[0x51]=1;
 choiceSupported=false;CHECK(!MarketPageHighlight(bank.data(),1000,1000).page);choiceSupported=true;
 Panel::standaloneMarket=true;CHECK(!MarketPageHighlight(bank.data(),1000,1000).page);Panel::standaloneMarket=false;
 Panel::NativeView view;view.open=true;Panel::Present(view);CHECK(!MarketPageHighlight(bank.data(),1000,1000).page);view.open=false;Panel::Present(view);
 Put(content.data(),0x60,uint64_t(1000));CHECK(!MarketPageHighlight(bank.data(),1000,1000).page);Put(content.data(),0x60,uint64_t(64));
 Put(pages.data(),0x1788,int(-1));CHECK(!MarketPageHighlight(bank.data(),1000,1000).page);Put(pages.data(),0x1788,int(64));
 CHECK(!MarketPageHighlight(bank.data(),0,1000).page&&pages==before);
 std::cout<<"Native market-page highlight: installed-PE signatures/global, matching state index, scrolled/clipped bounds, hidden/foreign popups, disabled/changed page and read-only selection passed.\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
