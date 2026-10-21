// Executes original storage routines in a private PE image. Never opens a save,
// injects into D2R, or modifies D2R.exe. This is NOT an end-to-end Act VI test.
#include <Windows.h>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include "../d2rhud/plugin/ExtendedActsPatchPlan.h"

struct Buffer {
    uint8_t* data;
    uint64_t bits;
    uint64_t reserved[3]{};
};
uintptr_t fixtureBase=0;
const char* fixturePhase="storage";
LONG WINAPI Fault(EXCEPTION_POINTERS* e){std::cerr<<"Native fixture fault ("<<fixturePhase<<"): RVA 0x"<<std::hex<<e->ContextRecord->Rip-fixtureBase<<" code "<<e->ExceptionRecord->ExceptionCode<<" address "<<e->ExceptionRecord->ExceptionInformation[1]<<'\n';uintptr_t stack[32]{};SIZE_T n=0;ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(e->ContextRecord->Rsp),stack,sizeof(stack),&n);for(auto v:stack)if(v>=fixtureBase && v<fixtureBase+0x262a000)std::cerr<<" stack RVA "<<v-fixtureBase<<'\n';return EXCEPTION_EXECUTE_HANDLER;}
static_assert(sizeof(Buffer) == 0x28);
struct Guarded {
    std::array<uint8_t, 16> before;
    std::array<uint8_t, 96> bytes;
    std::array<uint8_t, 16> after;
    Guarded() { before.fill(0x6d); bytes.fill(0); after.fill(0xb7); }
    void Check() const {
        for (auto v : before) assert(v == 0x6d);
        for (auto v : after) assert(v == 0xb7);
    }
};

const char* __fastcall Localized(uint32_t id, void*) {
    if(id>=64010 && id<64028)return "Act VI fixture text";
    return "Native quest text";
}
const char* __fastcall LocalizedKey(void*) { return "Quest completed"; }
void* __fastcall CopyText(void* to,const void* from,size_t bytes) { return memcpy(to,from,bytes); }
void Jump(uint8_t* to,uintptr_t target) {
    const uint8_t indirect[]{0xff,0x25,0,0,0,0};
    memcpy(to,indirect,sizeof(indirect));memcpy(to+6,&target,8);
}

void TownPredicatesContract(uint8_t* image) {
    using namespace ExtendedActs;
    auto wrapper=static_cast<uint8_t*>(VirtualAlloc(nullptr,64,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));assert(wrapper);
    for(const auto& town:Contracts::TownTests) {
        assert(town.yes>town.no?town.yes-town.no>=3:town.no-town.yes>=3);
        std::array<uint8_t,3> yes,no;
        memcpy(yes.data(),image+town.yes,3);memcpy(no.data(),image+town.no,3);
        const uint8_t accept[]{0xb0,1,0xc3},reject[]{0x31,0xc0,0xc3};
        memcpy(image+town.yes,accept,3);memcpy(image+town.no,reject,3);
        // Enter the actual native classifier with its original input register.
        size_t at=0;wrapper[at++]=0x31;wrapper[at++]=0xc0;
        if(town.reg>=8)wrapper[at++]=0x41;
        wrapper[at++]=0x89;wrapper[at++]=uint8_t(0xc8+(town.reg&7));
        Jump(wrapper+at,reinterpret_cast<uintptr_t>(image)+town.start);
        auto classify=reinterpret_cast<int(__fastcall*)(int)>(wrapper);
        std::vector<uint8_t> patched(image+town.rva,image+town.rva+town.size);
        memcpy(image+town.rva,town.before.data(),town.size);
        for(int level=-1;level<=512;++level)
            assert(bool(classify(level))==(level==1 || level==40 || level==75 || level==103 || level==109));
        memcpy(image+town.rva,patched.data(),town.size);
        for(int level=-1;level<=512;++level)
            assert(bool(classify(level))==(level==1 || level==40 || level==75 || level==103 || level==109 || level==240));
        memcpy(image+town.yes,yes.data(),3);memcpy(image+town.no,no.data(),3);
    }
    assert(VirtualFree(wrapper,0,MEM_RELEASE));
    std::cout<<Contracts::TownTests.size()<<" native town predicates preserve all original classifications and recognize Refuge; 165508 before/after cases.\n";
}

void QuestPresentationContract(uint8_t* image,uint32_t extraRva,uintptr_t preferredBase) {
    using namespace ExtendedActs::Native;
    auto original=reinterpret_cast<QuestRecord*>(image+QuestTableRva);
    auto table=reinterpret_cast<QuestRecord*>(image+extraRva);
    std::array<QuestLabels,6> labels{};
    for(unsigned q=0;q<6;++q)labels[q]={uint16_t(64010+q*3),uint16_t(64011+q*3),uint16_t(64012+q*3)};
    for(const auto& source:ExtendedActs::Contracts::Patches) {
        image[source.rva]^=1;
        assert(!BuildPlan(image,image+extraRva,labels));image[source.rva]^=1;
    }
    for(const auto& source:ExtendedActs::Contracts::Relocations) {
        image[source.rva]^=1;
        assert(!BuildPlan(image,image+extraRva,labels));image[source.rva]^=1;
    }
    auto edits=BuildPlan(image,image+extraRva,labels);assert(edits);
    for(const auto& edit:*edits)memcpy(image+edit.rva,edit.after.data(),edit.after.size());
    TownPredicatesContract(image);
    assert(!memcmp(original,table,41*sizeof(QuestRecord)));
    assert(!table[41].visible && table[41].flags==41);
    // Original text pointers are absolute VAs; account for this owned mapping.
    for(unsigned q=0;q<41;++q)if(table[q].text)
        table[q].text=reinterpret_cast<uint16_t*>(uintptr_t(image)+uintptr_t(table[q].text)-preferredBase);
    Jump(image+0x413860,reinterpret_cast<uintptr_t>(&Localized));
    Jump(image+0x413960,reinterpret_cast<uintptr_t>(&LocalizedKey));
    Jump(image+0x14383a0,reinterpret_cast<uintptr_t>(&CopyText));
    Guarded personal,world;
    Buffer p{personal.bytes.data(),768},w{world.bytes.data(),768};
    // Bind the bitbuffer objects, not raw bytes.
    *reinterpret_cast<Buffer**>(image+0x1d60c28)=&p;
    *reinterpret_cast<Buffer**>(image+0x1d60c30)=&w;
    auto describe=reinterpret_cast<void(__fastcall*)(int,void*)>(image+0x147a70);
    struct alignas(8) Description {
        uint16_t title;char message[300];uint16_t speech;
        uint8_t order,button,notify,state;uint32_t display,record;
    };
    static_assert(sizeof(Description)==0x140 && offsetof(Description,record)==0x138);
    for(unsigned q=0;q<6;++q)for(unsigned phase=0;phase<3;++phase) {
        personal.bytes.fill(0);world.bytes.fill(0);
        const unsigned record=42+q;
        if(phase==1)personal.bytes[record*2+1]=0x80;
        if(phase==2)personal.bytes[record*2]=1;
        image[0x1d60d10+record]=phase==1?1:0;
        std::array<uint8_t,0x160> guarded;guarded.fill(0x91);
        auto result=reinterpret_cast<Description*>(guarded.data()+16);
        describe(record,result);
        assert(result->record==record && result->order==q && result->button==27+q);
        if(phase)assert(result->title==labels[q].title && result->message[0]);
        if(phase==2)assert(result->state==11);
        for(unsigned i=0;i<16;++i)assert(guarded[i]==0x91 && guarded[0x14c+i]==0x91);
        personal.Check();world.Check();
    }
    std::cout << "Original native quest description resolver reads six independent Act VI records, button IDs, and objective/completed states.\n"
                 "Localization and text copying are explicit fixture boundaries; the actual quest window is not instantiated.\n";
}

void WaypointContract(uint8_t* image) {
    auto has=reinterpret_cast<int(__fastcall*)(void*,uint16_t)>(image+0x2591d0);
    auto add=reinterpret_cast<void(__fastcall*)(void*,uint16_t)>(image+0x259250);
    struct Waypoints {std::array<uint8_t,16> before;uint16_t words[8];std::array<uint8_t,16> after;};
    Waypoints data{};data.before.fill(0x6d);data.after.fill(0xb7);data.words[0]=0x102;
    for(uint16_t w=0;w<39;++w)if(w%3==1)add(data.words,w);
    for(uint16_t w=39;w<48;++w) {
        assert(!has(data.words,w));add(data.words,w);assert(has(data.words,w));
        for(uint16_t other=0;other<39;++other)assert(bool(has(data.words,other))==(other%3==1));
        assert(data.words[0]==0x102);
        for(auto byte:data.before)assert(byte==0x6d);
        for(auto byte:data.after)assert(byte==0xb7);
    }
    add(data.words,111);assert(has(data.words,111));
    std::cout << "Original native waypoint getter/setter supports Act VI bits 39..47 inside its existing 112-bit capacity. Original waypoints and header are preserved.\n";
}

namespace ExitFixture {
template<class T>void Put(void* p,size_t offset,T value){memcpy(static_cast<uint8_t*>(p)+offset,&value,sizeof(value));}
std::array<std::array<uint8_t,0x9c>,249> definitions;
std::array<std::array<uint8_t,0x60>,2> warps;
std::array<uint8_t,0x200> area;
std::array<uint8_t,0x120> drlg;
std::array<uint8_t,0xa0> room;
int calls=0,lastId=-1;
void* __fastcall Place(void* source,int type,int id,int mode,int x,int y){
    assert(source==room.data() && type==5 && mode==0 && x==17 && y==28);
    ++calls;lastId=id;return source;
}
}

void ExitTileContract(uint8_t* image) {
    using namespace ExitFixture;
    fixturePhase="cloned exit tile metadata";
    auto defs=definitions.data();auto records=warps.data();uint64_t count=249,warpCount=2;
    memcpy(image+0x1c9fd28,&defs,8);memcpy(image+0x1c9fd10,&count,8);
    memcpy(image+0x1ca08e0,&records,8);memcpy(image+0x1ca08e8,&warpCount,8);
    auto d=drlg.data();auto level=area.data();Put(area.data(),0x1c8,d);Put(room.data(),0x90,level);
    Put(room.data(),0x60,10);Put(room.data(),0x64,20);Put(room.data(),0x68,50);Put(room.data(),0x6c,50);
    auto mappings=reinterpret_cast<int*(__fastcall*)(void*,int)>(image+0x25a370);
    auto lookup=reinterpret_cast<void*(__fastcall*)(int,char)>(image+0x2268f0);
    auto create=reinterpret_cast<int(__fastcall*)(void*,int,int,uint64_t,int)>(image+0x26df20);
    Jump(image+0x2435f0,reinterpret_cast<uintptr_t>(&Place));
    for(unsigned i=0;i<2;++i){Put(warps[i].data(),0x2c,int(73+i));Put(warps[i].data(),0x48,2);Put(warps[i].data(),0x4c,3);warps[i][0x5c]='b';}
    for(unsigned levelId=241;levelId<249;++levelId){
        Put(area.data(),0x1f8,int(levelId));
        for(unsigned index=0;index<8;++index){
            // The old pack erased these even though the DS1 still had exits.
            Put(definitions[levelId].data(),0x68+4*index,-1);
            assert(mappings(drlg.data(),levelId)[index]==-1);
            // The fixed pack retains source Warp metadata, independently of
            // disabled Vis links. Exercise the ORIGINAL tile creator and
            // fatal-on-missing native lookup, not a replacement guard.
            const int warp=73+index%2;Put(definitions[levelId].data(),0x68+4*index,warp);
            assert(lookup(warp,'l')==warps[index%2].data());
            assert(create(room.data(),13,25,uint64_t(index)<<20,10)==1);
            assert(lastId==warp);
        }
    }
    assert(calls==64);
    std::cout << "Original DRLG exit-tile creator resolves all eight cloned areas' retained Warp metadata; disabled destinations do not remove required tile definitions.\n"
                 "Preset-unit allocation is an explicit boundary; full procedural layout generation remains a runtime check.\n";
}

std::vector<void*> freedPresetBuffers;
void __fastcall FreePresetBuffer(void*,void* buffer){freedPresetBuffers.push_back(buffer);}
void PresetCacheContract(uint8_t* image,uint32_t extraRva) {
    using namespace ExtendedActs::Native;
    fixturePhase="six-entry native preset cache cleanup";
    auto caches=reinterpret_cast<PresetCache*>(image+extraRva+PresetCacheOffset);
    auto paths=reinterpret_cast<PresetPath*>(image+extraRva+PresetPathsOffset);
    assert(paths[5].size==23 && !strcmp(reinterpret_cast<const char*>(paths[5].text),"data/global/tiles/act6/"));
    std::array<uintptr_t,5> vtable{};vtable[4]=reinterpret_cast<uintptr_t>(&FreePresetBuffer);
    auto allocator=vtable.data();
    for(unsigned i=0;i<6;++i)caches[i]={reinterpret_cast<uintptr_t>(&allocator),uintptr_t(0x1000+16*i),0,8};
    // FreeAct clears a cache using this original routine; the sixth allocator
    // must be valid even if no combined_ds1.bin exists for the new act.
    reinterpret_cast<void(__fastcall*)(void*)>(image+0xc44c0)(&caches[5]);
    assert(freedPresetBuffers.size()==1 && freedPresetBuffers[0]==reinterpret_cast<void*>(0x1050));
    assert(!caches[5].buffer && !caches[5].capacity);
    caches[5].buffer=0x1050;freedPresetBuffers.clear();
    // Execute the real reverse destructor, with both its count and end
    // iterator relocated. Guard the following bytes and adjacent old global.
    std::array<uint8_t,16> oldNext{};memcpy(oldNext.data(),image+0x1ca0ba8,16);
    auto guard=reinterpret_cast<uint8_t*>(caches+6);memset(guard,0x93,16);
    reinterpret_cast<void(__fastcall*)()>(image+0x1478820)();
    assert(freedPresetBuffers.size()==6);
    for(unsigned i=0;i<6;++i)assert(freedPresetBuffers[i]==reinterpret_cast<void*>(uintptr_t(0x1050-16*i)));
    for(unsigned i=0;i<16;++i)assert(guard[i]==0x93);
    assert(!memcmp(oldNext.data(),image+0x1ca0ba8,16));
    std::cout << "Native preset cache clear and reverse destructor handle six owned arrays without touching the adjacent original global.\n";
}

void QuestPacketContract(uint8_t* image) {
    Guarded personal,world;Buffer p{personal.bytes.data(),768},w{world.bytes.data(),768};
    *reinterpret_cast<Buffer**>(image+0x1d60c28)=&p;
    *reinterpret_cast<Buffer**>(image+0x1d60c30)=&w;
    std::array<uint8_t,103> packet{};packet[0]=0x28;packet[1]=6;
    for(unsigned i=0;i<96;++i)packet[i+7]=uint8_t(i*31);
    reinterpret_cast<void(__fastcall*)(const uint8_t*)>(image+0xd8020)(packet.data());
    assert(!memcmp(personal.bytes.data(),packet.data()+7,96));personal.Check();world.Check();
    std::array<uint8_t,97> secondary{};secondary[0]=0x29;
    for(unsigned i=0;i<96;++i)secondary[i+1]=uint8_t(i*17);
    reinterpret_cast<void(__fastcall*)(const uint8_t*)>(image+0xd8140)(secondary.data());
    assert(!memcmp(world.bytes.data(),secondary.data()+1,96));personal.Check();world.Check();
    std::cout << "Original 0x28/type-6 and 0x29 quest packet receivers copy all 48 records without widening a packet.\n";
}

namespace PanelFixture {
template<class T>void Put(void* at,size_t offset,T value){memcpy(static_cast<uint8_t*>(at)+offset,&value,sizeof(value));}
template<class T>T Get(const void* at,size_t offset){T value;memcpy(&value,static_cast<const uint8_t*>(at)+offset,sizeof(value));return value;}
alignas(16) std::array<uint8_t,0xa08+32> allocation;
alignas(16) std::array<uint8_t,0x530> bar;
alignas(16) std::array<std::array<uint8_t,0xa40>,6> tabs;
alignas(16) std::array<std::array<std::array<uint8_t,0xa40>,6>,6> buttons;
std::array<void*,6> tabPointers;
std::array<std::array<void*,6>,6> buttonPointers;
constexpr const char* names[]{"Tab0","Tab1","Tab2","Tab3","Tab4","Tab5"};
std::array<uintptr_t,12> vtable;
std::array<uint8_t,24> metadata;
std::array<bool,6> enabled;
int selected=-1;
void* __fastcall Allocate(void*,size_t bytes,size_t align){assert(bytes==0xa08 && align==4);allocation.fill(0x91);return allocation.data()+16;}
void* __fastcall Construct(void* object,void*,void*) {
    memset(object,0,0x168);Put(object,0x58,tabPointers.data());Put(object,0x60,uint64_t(6));
    return object;
}
void* __fastcall Type(){return metadata.data();}
void __fastcall Active(void* widget,uint8_t on){Put(widget,0x90,on);}
void __fastcall Visible(void* widget,uint8_t on){Put(widget,0x91,on);}
void* __fastcall Lookup(void*,const char*){return nullptr;}
void* __fastcall Tabs(void*,const char*){return bar.data();}
void __fastcall Nothing(){}
int __fastcall Format(char* out,const char* format,int value){return snprintf(out,32,format,value);}
void* __fastcall Clear(void* at,int value,size_t size){return memset(at,value,size);}
void __fastcall Enable(void* widget,int tab,uint8_t on){assert(widget==bar.data() && tab>=0 && tab<6);enabled[tab]=bool(on);}
void __fastcall Select(void* widget,int tab,uint8_t){assert(widget==bar.data() && tab>=0 && tab<6);selected=tab;}
void Check(){for(unsigned i=0;i<16;++i)assert(allocation[i]==0x91 && allocation[0xa18+i]==0x91);}
}

void QuestPanelContract(uint8_t* image) {
    using namespace PanelFixture;
    vtable[9]=reinterpret_cast<uintptr_t>(&Active);vtable[10]=reinterpret_cast<uintptr_t>(&Visible);
    vtable[11]=reinterpret_cast<uintptr_t>(&Type);
    for(unsigned act=0;act<6;++act) {
        tabs[act].fill(0);Put(tabs[act].data(),0,vtable.data());Put(tabs[act].data(),8,names[act]);
        tabPointers[act]=tabs[act].data();Put(tabs[act].data(),0x58,buttonPointers[act].data());Put(tabs[act].data(),0x60,uint64_t(6));
        for(unsigned q=0;q<6;++q) {
            buttons[act][q].fill(0);Put(buttons[act][q].data(),0,vtable.data());
            Put(buttons[act][q].data(),0x9f0,int(q%3));Put(buttons[act][q].data(),0x9f4,int(q/3));
            buttonPointers[act][q]=buttons[act][q].data();
        }
    }
    bar.fill(0);Put(bar.data(),0x520,int(6));
    std::array<uintptr_t,2> allocator{0,reinterpret_cast<uintptr_t>(&Allocate)};
    *reinterpret_cast<uintptr_t**>(image+0x18eb658)=allocator.data();
    Jump(image+0x579850,reinterpret_cast<uintptr_t>(&Construct));
    Jump(image+0x1438100,reinterpret_cast<uintptr_t>(&Clear));
    Jump(image+0x149a00,reinterpret_cast<uintptr_t>(&Lookup));
    Jump(image+0x576070,reinterpret_cast<uintptr_t>(&Lookup));
    Jump(image+0x5761f0,reinterpret_cast<uintptr_t>(&Nothing));
    Jump(image+0x155540,reinterpret_cast<uintptr_t>(&Tabs));
    Jump(image+0xbe5fb0,reinterpret_cast<uintptr_t>(&Type));
    Jump(image+0x57e370,reinterpret_cast<uintptr_t>(&Enable));
    Jump(image+0x57e220,reinterpret_cast<uintptr_t>(&Select));
    Jump(image+0x14c4e0,reinterpret_cast<uintptr_t>(&Lookup));
    Jump(image+0x157f40,reinterpret_cast<uintptr_t>(&Lookup));
    Jump(image+0xbb87f0,reinterpret_cast<uintptr_t>(&Format));
    Jump(image+0x1555f0,reinterpret_cast<uintptr_t>(&Format));
    Jump(image+0xbb8350,reinterpret_cast<uintptr_t>(&Nothing));
    fixturePhase="quest panel factory";
    auto root=reinterpret_cast<void*(__fastcall*)(void*,void*,void*)>(image+0xbb6c10)(nullptr,nullptr,nullptr);
    assert(root==allocation.data()+16);Check();
    for(unsigned x=0;x<3;++x)Put(root,0x16c+4*x,int(100+100*x));
    for(unsigned y=0;y<2;++y)Put(root,0x178+4*y,int(30+30*y));
    fixturePhase="quest panel initialize";
    reinterpret_cast<void(__fastcall*)(void*)>(image+0xbb6f60)(root);Check();
    for(unsigned act=0;act<6;++act)for(unsigned q=0;q<6;++q) {
        assert(Get<void*>(root,0x180+48*act+16*(q%3)+8*(q/3))==buttons[act][q].data());
        assert(Get<int>(buttons[act][q].data(),0x70)==int(100+100*(q%3)));
        assert(Get<int>(buttons[act][q].data(),0x74)==int(30+30*(q/3)));
    }
    Guarded quest;Buffer bits{quest.bytes.data(),768};
    *reinterpret_cast<Buffer**>(image+0x1d60c28)=&bits;
    for(auto record:{7,15,23,26,40})quest.bytes[record*2]=1;
    fixturePhase="quest panel switch to VI";
    reinterpret_cast<void(__fastcall*)(void*,int)>(image+0xbb7330)(root,5);Check();quest.Check();
    assert(selected==5 && Get<int>(root,0x168)==5);
    for(unsigned act=0;act<6;++act) {
        assert(enabled[act]);assert(bool(Get<uint8_t>(tabs[act].data(),0x91))==(act==5));
        for(unsigned q=0;q<6;++q)assert(Get<void*>(root,0x180+48*act+16*(q%3)+8*(q/3))==buttons[act][q].data());
    }
    quest.bytes[80]=0;
    reinterpret_cast<void(__fastcall*)(void*,int)>(image+0xbb7330)(root,0);Check();quest.Check();
    assert(!enabled[5] && selected==0);
    std::cout << "Native quest panel factory, 36-cell initialization and sixth-tab switch run with guarded storage. Baal completion gates tab VI.\n"
                 "Widget lookup, type metadata, allocation and rendering are explicit fixture boundaries.\n";
}

int wmain(int argc, wchar_t** argv) {
    std::cout<<std::unitbuf;SetUnhandledExceptionFilter(&Fault);
    assert(argc == 2);
    std::ifstream file(std::filesystem::path(argv[1]), std::ios::binary | std::ios::ate);
    assert(file);
    const auto length = size_t(file.tellg());
    std::vector<uint8_t> raw(length);
    file.seekg(0); file.read(reinterpret_cast<char*>(raw.data()), length); assert(file);
    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(raw.data());
    assert(dos->e_magic == IMAGE_DOS_SIGNATURE && dos->e_lfanew > 0);
    const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(raw.data()+dos->e_lfanew);
    assert(nt->Signature == IMAGE_NT_SIGNATURE && nt->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64);
    auto image = static_cast<uint8_t*>(VirtualAlloc(nullptr, nt->OptionalHeader.SizeOfImage+0x10000,
        MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)); assert(image);
    fixtureBase=reinterpret_cast<uintptr_t>(image);
    const auto sections = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        const auto& section = sections[i];
        assert(size_t(section.PointerToRawData)+section.SizeOfRawData <= raw.size());
        memcpy(image+section.VirtualAddress,raw.data()+section.PointerToRawData,section.SizeOfRawData);
    }
    const uint8_t readerEntry[]{0x48,0x83,0xec,0x28,0x48,0x8b,0xc1};
    const uint8_t loaderEntry[]{0x48,0x83,0xec,0x08,0x48,0x8b,0x01};
    assert(!memcmp(image+0x243880,readerEntry,sizeof(readerEntry)));
    assert(!memcmp(image+0x243ae0,loaderEntry,sizeof(loaderEntry)));
    const uint8_t normalizeCount[]{0x44,0x8d,0x50,0x1c};
    assert(!memcmp(image+0x243b31,normalizeCount,sizeof(normalizeCount)));
    DWORD old=0;
    assert(VirtualProtect(image,nt->OptionalHeader.SizeOfImage+0x10000,PAGE_EXECUTE_READWRITE,&old));
    auto read=reinterpret_cast<int(__fastcall*)(Buffer*,int,int)>(image+0x243880);
    auto load=reinterpret_cast<void(__fastcall*)(Buffer*,const uint8_t*,int,int)>(image+0x243ae0);
    for (int difficulty=0; difficulty<3; ++difficulty) {
        Guarded input, output;
        Buffer buffer{output.bytes.data(),768};
        // Distinct original-act patterns, including the shared introduction.
        for (int q=0;q<42;++q) {
            input.bytes[q*2]=uint8_t(q*17+difficulty);
            input.bytes[q*2+1]=uint8_t(q*31+3*difficulty);
        }
        for (int q=42;q<48;++q) {
            input.bytes[q*2]=uint8_t(1u<<((q+difficulty)%8));
            input.bytes[q*2+1]=uint8_t(1u<<((q+difficulty+3)%8));
        }
        const auto original=input.bytes;
        load(&buffer,input.bytes.data(),0,0);
        assert(output.bytes==original);
        for(int q=0;q<48;++q)for(int flag=0;flag<16;++flag)
            assert(bool(read(&buffer,q,flag))==bool(original[q*2+flag/8]&(1u<<(flag%8))));
        output.Check(); input.Check(); assert(input.bytes==original);
        load(&buffer,input.bytes.data(),0,1);
        // The original loader normalizes only records 0..41. It still copies
        // all 96 bytes. Adding flags requires an explicit lifecycle decision.
        assert(!memcmp(output.bytes.data()+84,original.data()+84,12));
        output.Check(); input.Check(); assert(input.bytes==original);
        // Verify a scoped normalization-count change on this owned copy.
        image[0x243b34]=0x22; // 14 + 34 = 48 (previously 14 + 28 = 42).
        load(&buffer,input.bytes.data(),0,1);
        for(int q=0;q<48;++q) {
            assert(!read(&buffer,q,13) && !read(&buffer,q,14));
            assert(bool(read(&buffer,q,0))==bool(original[q*2]&1));
            assert(bool(read(&buffer,q,1))==bool(original[q*2]&2));
            if(original[q*2]&2)assert(read(&buffer,q,15));
        }
        output.Check(); input.Check(); assert(input.bytes==original);
        image[0x243b34]=0x1c;
    }
    WaypointContract(image);ExitTileContract(image);QuestPacketContract(image);
    QuestPresentationContract(image,nt->OptionalHeader.SizeOfImage,nt->OptionalHeader.ImageBase);
    PresetCacheContract(image,nt->OptionalHeader.SizeOfImage);
    QuestPanelContract(image);
    assert(VirtualFree(image,0,MEM_RELEASE));
    std::cout << "Native 96-byte loader and flag reader preserve records 42..47 across three independent difficulty fixtures.\n"
                 "Scoped 48-record normalization preserves permanent flags and stays inside guarded buffers.\n"
                 "Full save writer, game reload, UI, travel, and Act VI runtime are not covered by this test.\n";
}
