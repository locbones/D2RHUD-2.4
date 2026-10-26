#define NOMINMAX
#include "../d2rhud/plugin/QuestJournalClient.cpp"
#include <cassert>
#include <fstream>
#include <iostream>
#include <map>

std::map<uint32_t,uintptr_t> bindings;
uintptr_t testImage=0;
DWORD64 Pattern::Address(uint32_t rva){return testImage?testImage+rva:bindings[rva];}
std::shared_ptr<QuestJournal::Rules> rules=std::make_shared<QuestJournal::Rules>();
QuestJournal::View view;
namespace QuestJournal {
std::shared_ptr<const Rules> Bindings(){return rules;}
View CurrentView(){return view;}
}
using namespace QuestJournal;
using namespace QuestJournal::ClientUI;
template<class T>void Put(void* p,size_t offset,T value){memcpy(static_cast<uint8_t*>(p)+offset,&value,sizeof(value));}
D2UnitStrc hero{};uint32_t selector=0;
bool nativeNotice=false;int opens=0,toggles=0,clicks=0,closedAlerts=0,openedAlerts=0;uint32_t clickedMask=0;
D2UnitStrc* __fastcall Local(uint32_t){return &hero;}
bool __fastcall Available(int category){return category==0||(category==2&&nativeNotice);}
bool __fastcall Open(int panel,bool sound){
    if(panel==16){assert(testImage&&!sound);++openedAlerts;Put(reinterpret_cast<void*>(testImage),UIFlags+16,uint8_t(1));return true;}
    ++opens;return false;
}
bool __fastcall Toggle(int,bool){++toggles;return false;}
void __fastcall Clicked(void* widget){++clicks;clickedMask=Field<uint32_t>(widget,0xbc4);}
bool __fastcall Closed(int panel,bool sound){assert(panel==16&&!sound);++closedAlerts;Put(reinterpret_cast<void*>(testImage),UIFlags+16,uint8_t(0));return true;}
bool __fastcall MayOpen(int panel,int mode,bool forced){assert(panel==16&&mode==0&&forced);return true;}
void __fastcall PanelOpened(int panel){assert(panel==16&&Field<uint8_t>(reinterpret_cast<void*>(testImage),UIFlags+16));++openedAlerts;}
const char* __fastcall PanelName(int panel){assert(panel==16);return nullptr;}
void __fastcall PanelChanged(void*,int){}
void Policy(){
    View progress;progress.connected=true;progress.recipient=1;progress.session=1;progress.completed=5;
    Notice n;n.Observe(progress,false);assert(!n.pending); // Loaded history is already read.
    progress.objectiveReady=true;n.Observe(progress,false);assert(n.pending);
    n.Observe(progress,true);assert(!n.pending);n.Observe(progress,false);assert(!n.pending);
    ++progress.sequence;n.Observe(progress,false);assert(!n.pending); // Repeated packets cannot re-arm it.
    progress.objectiveReady=false;++progress.completed;n.Observe(progress,false);assert(n.pending);
    n.Observe(progress,true);progress.objectiveReady=true;n.Observe(progress,true);assert(!n.pending);
    progress.session=2;assert(n.Changed(progress));n.Observe(progress,false);assert(n.pending);
    progress.blocked=true;n.Observe(progress,false);assert(!n.pending&&!n.known);
    progress.blocked=false;progress.legacyAvailable=true;n.Observe(progress,false);assert(!n.pending);
    progress.legacyAvailable=false;progress.connected=false;n.Observe(progress,false);assert(!n.pending);
    std::cout<<"Notification acknowledgements are per character/session, survive duplicate packets, and re-arm only for new objective/completion progress.\n";
}
void Routing(){
    bindings[0x1d442d8]=uintptr_t(&selector);bindings[0x6b050]=uintptr_t(Local);hero.dwAnimMode=1;
    originalAvailability=Available;originalOpen=Open;originalToggle=Toggle;originalClick=Clicked;
    view={};view.connected=true;view.recipient=1;view.session=1;view.objectiveReady=true;
    bool shown=false;Update(shown,true);assert(notification&&AvailabilityHook(2)&&AvailabilityHook(0)&&!AvailabilityHook(1));
    std::array<uint8_t,0xbd8> widget{};Put(widget.data(),0x9f0,int(2));Put(widget.data(),0x9fc,int(1));Put(widget.data(),0xbc4,uint32_t(1));
    ClickHook(widget.data());assert(clickedMask==0&&Field<uint32_t>(widget.data(),0xbc4)==1);
    Update(shown,true);assert(shown&&!notification&&!AvailabilityHook(2)&&opens==0);
    assert(ToggleHook(14,true));Update(shown,true);assert(!shown&&!notification&&toggles==0);
    assert(OpenHook(14,false));Update(shown,true);assert(shown&&opens==0);
    assert(!OpenHook(1,true)&&opens==1);assert(!ToggleHook(4,false)&&toggles==1);
    // Shared controller widget: preserve the native stat/skill selections.
    Put(widget.data(),0x9f0,int(0));Put(widget.data(),0x9f4,int(1));Put(widget.data(),0x9f8,int(2));Put(widget.data(),0x9fc,int(3));Put(widget.data(),0xbc4,uint32_t(7));
    ClickHook(widget.data());assert(clickedMask==3&&Field<uint32_t>(widget.data(),0xbc4)==7);Update(shown,true);
    BeginInput(false);Hotkey();assert(ConsumeInput()&&!ConsumeInput());Update(shown,true);assert(!shown);
    BeginInput(true);Hotkey();assert(ConsumeInput());Update(shown,true);assert(!shown);
    BeginInput(false);Hotkey();assert(ConsumeInput());Update(shown,true);assert(shown);
    view.session=2;view.objectiveReady=false;Update(shown,true);assert(!shown&&!notification);
    nativeNotice=true;assert(AvailabilityHook(2));nativeNotice=false;
    auto saved=rules;rules.reset();Put(widget.data(),0xbc4,uint32_t(7));ClickHook(widget.data());assert(clickedMask==7);
    assert(!OpenHook(14,true)&&!ToggleHook(14,true));Update(shown,true);assert(!notification);rules=saved;
    hero.dwAnimMode=17;assert(!CanRoute());hero.dwAnimMode=1;
    notice={};request=Request::None;
    std::cout<<"RMD-only routing opens/toggles the journal, preserves other panels and native alerts, handles combined controller buttons, and consumes held/configured hotkeys once.\n";
}
void Executable(const char* path){
    std::ifstream file(path,std::ios::binary|std::ios::ate);assert(file);std::vector<uint8_t> bytes(size_t(file.tellg()));file.seekg(0);file.read(reinterpret_cast<char*>(bytes.data()),bytes.size());
    auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes.data());auto nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(bytes.data()+dos->e_lfanew);
    auto image=static_cast<uint8_t*>(VirtualAlloc(nullptr,nt->OptionalHeader.SizeOfImage,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));assert(image);
    for(auto s=IMAGE_FIRST_SECTION(nt);s<IMAGE_FIRST_SECTION(nt)+nt->FileHeader.NumberOfSections;++s)memcpy(image+s->VirtualAddress,bytes.data()+s->PointerToRawData,s->SizeOfRawData);
    testImage=uintptr_t(image);assert(Supported());
    // Native LevelUpButtonWidget's click vtable and quests category literal.
    assert(Field<uint64_t>(image,0x161b600+0xa8)==nt->OptionalHeader.ImageBase+0xbd0550);
    assert(!memcmp(image+0x161b4e8,"quests",7));
    for(const auto s:signatures){image[s.rva]^=1;assert(!Supported()&&!Install());image[s.rva]^=1;}
    auto stub=[&](uint32_t rva,auto fn){const uint8_t jump[]{0xff,0x25,0,0,0,0};memcpy(image+rva,jump,6);auto address=uintptr_t(fn);memcpy(image+rva+6,&address,8);};
    stub(0x6b050,Local);assert(Install());
    std::array<uint8_t,32> closeBytes{};memcpy(closeBytes.data(),image+0x86860,32);stub(0x86860,Closed);
    // Execute the actual OpenUI trampoline and its native flag transition.
    // Only widget creation/message delivery effects are stubbed.
    stub(0x85e10,MayOpen);stub(0x859a0,PanelOpened);stub(0x86030,PanelName);stub(0x56cf70,PanelChanged);
    std::array<uint8_t,0xe0> ui{};Put(image,0x1d7c4e8,ui.data());
    view={};view.connected=true;view.recipient=1;view.session=3;bool shown=false;Update(shown,true);
    assert(!notification&&!Field<uint8_t>(image,UIFlags+16));
    ++view.completed; // Deposit-skull auto-completion has no ready reward.
    Update(shown,true);assert(notification&&openedAlerts==1&&Field<uint8_t>(image,UIFlags+16));
    Update(shown,true);assert(openedAlerts==1); // Do not reopen every frame.
    std::array<uint8_t,0xbd8> widget{};Put(widget.data(),0x9f0,int(2));Put(widget.data(),0x9fc,int(1));Put(widget.data(),0xa00,uint8_t(1));
    auto nativeMask=Native<int(__fastcall*)(void*)>(0xbcf920);
    assert(nativeMask(widget.data())==1); // Real native visibility uses our pending objective.
    Put(widget.data(),0xbc4,uint32_t(1));Put(image,UIFlags+16,uint8_t(1));
    Native<Click>(0xbd0550)(widget.data());Update(shown,true);
    assert(shown&&!notification&&closedAlerts==1&&!Field<uint8_t>(image,UIFlags+14));
    assert(nativeMask(widget.data())==0);
    ++view.completed;Update(shown,true);assert(!notification); // Reading acknowledges progress.
    RequestToggle();Update(shown,true);assert(!shown);
    view.objectiveReady=true;Update(shown,true);assert(notification&&openedAlerts==2&&Field<uint8_t>(image,UIFlags+16));
    // A native alert arriving while ours is up owns the container afterwards.
    assert(OpenHook(16,false));assert(!journalAlert);
    view.blocked=true;Update(shown,true);assert(!notification&&Field<uint8_t>(image,UIFlags+16));
    view.blocked=false;RequestOpen();Update(shown,true);assert(shown&&!Field<uint8_t>(image,UIFlags+16));
    assert(Native<Panel>(0x86a50)(14,true));Update(shown,true);assert(!shown);
    assert(Native<Panel>(0x865e0)(14,true));Update(shown,true);assert(shown&&!Field<uint8_t>(image,UIFlags+14));
    const auto closedBefore=closedAlerts;
    Put(image,UIFlags+16,uint8_t(1));journalAlert=true;Update(shown,false);
    assert(!notification&&!journalAlert&&closedAlerts==closedBefore); // No engine UI work at character select.
    memcpy(image+0x86860,closeBytes.data(),32);Uninstall();assert(Supported());
    testImage=0;VirtualFree(image,0,MEM_RELEASE);
    std::cout<<"Private game image: real quest-alert visibility/click and panel open/toggle detours work; native quest panel remains closed, alert dismissal is scoped, signatures and uninstall restore all entries.\n";
}
int main(int argc,char** argv){Policy();Routing();if(argc>1)Executable(argv[1]);std::cout<<"PASS: quest journal shortcuts\n";}
