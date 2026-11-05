// Maps the shipped executable privately; never attaches to or launches D2R.
#include "../d2rhud/plugin/MultipleAuras.cpp"
#include "../d2rhud/plugin/SelectedAuras.cpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <thread>

namespace {
uint8_t* image = nullptr;
std::map<uint32_t,uintptr_t> bindings;
}
DWORD64 Pattern::Address(uint32_t rva) { return image ? uintptr_t(image)+rva : bindings.at(rva); }
namespace {
using namespace MultipleAuras;
using Bytes = std::vector<uint8_t>;
template<class T> void Put(void* p,size_t offset,T value) { std::memcpy(static_cast<Byte*>(p)+offset,&value,sizeof(value)); }
struct Event {
    std::array<Byte,0x48> data{};
    Event(int skill,int key,uint8_t type=9) { data[0]=type;Put(data.data(),0x18,key);Put(data.data(),0x1c,skill); }
};
std::vector<int> deleted;
void __fastcall Delete(void*,void* event) { deleted.push_back(Field<int>(event,0x1c)); }
void Basic() {
    bindings[NativeData::DeleteEvent]=uintptr_t(&Delete);
    SetEnabled(true);Event defiance(104,42),tracker(762,42),other(104,42,8);
    FilteredDelete(nullptr,tracker.data.data(),104);assert(deleted.empty());
    FilteredDelete(nullptr,defiance.data.data(),104);assert(deleted==std::vector<int>{104});
    deleted.clear();FilteredDelete(nullptr,other.data.data(),762);assert(deleted==std::vector<int>{104});
    deleted.clear();SetEnabled(false);FilteredDelete(nullptr,tracker.data.data(),104);assert(deleted==std::vector<int>{762});
    deleted.clear();SetEnabled(true);FilteredDelete(nullptr,tracker.data.data(),-1);assert(deleted==std::vector<int>{762});
    bindings[NativeData::Deactivate]=uintptr_t(+[](void* game,void* unit,int key,int skill){
        assert(removalSkill==skill);
        if(skill==104) { RemovalScope(game,unit,key,762);assert(removalSkill==104); }
    });
    RemovalScope(nullptr,nullptr,0,104);assert(removalSkill==-1);
    std::thread a([]{removalSkill=104;std::this_thread::yield();assert(removalSkill==104);});
    std::thread b([]{removalSkill=762;std::this_thread::yield();assert(removalSkill==762);});a.join();b.join();assert(removalSkill==-1);
    std::cout << "PASS enabled/disabled matching, non-aura cleanup, invalid-ID cleanup and nested/thread-isolated removal context.\n";
}
void Map(const std::filesystem::path& path) {
    std::ifstream in(path,std::ios::binary|std::ios::ate);assert(in);Bytes bytes(size_t(in.tellg()));in.seekg(0);assert(in.read(reinterpret_cast<char*>(bytes.data()),bytes.size()));
    const auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes.data());assert(dos->e_magic==IMAGE_DOS_SIGNATURE);
    const auto nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(bytes.data()+dos->e_lfanew);assert(nt->Signature==IMAGE_NT_SIGNATURE&&nt->FileHeader.Machine==IMAGE_FILE_MACHINE_AMD64);
    image=static_cast<Byte*>(VirtualAlloc(nullptr,nt->OptionalHeader.SizeOfImage,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));assert(image);
    std::memcpy(image,bytes.data(),nt->OptionalHeader.SizeOfHeaders);
    const auto sections=IMAGE_FIRST_SECTION(nt);for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i)
        std::memcpy(image+sections[i].VirtualAddress,bytes.data()+sections[i].PointerToRawData,sections[i].SizeOfRawData);
}
template<class T> void Stub(uint32_t rva,T fn) {
    const Byte jump[]{0xff,0x25,0,0,0,0};std::memcpy(image+rva,jump,6);Put(image+rva,6,uintptr_t(fn));
    FlushInstructionCache(GetCurrentProcess(),image+rva,14);
}
void Installation() {
    std::array<std::array<Byte,5>,std::size(NativeData::Sites)> before{};
    for(size_t i=0;i<before.size();++i)std::memcpy(before[i].data(),image+NativeData::Sites[i].rva,5);
    for(const auto& site:NativeData::Sites) {
        image[site.rva]^=1;assert(!Install()&&!Available());image[site.rva]^=1;
        for(size_t i=0;i<before.size();++i)assert(!std::memcmp(image+NativeData::Sites[i].rva,before[i].data(),5));
    }
    for(const auto& signature:NativeData::Functions){image[signature.rva]^=1;assert(!Install()&&!Available());image[signature.rva]^=1;}
    assert(Install()&&Available()&&Install());
    for(size_t i=0;i<before.size();++i)assert(std::memcmp(image+NativeData::Sites[i].rva,before[i].data(),5));
    Uninstall();assert(!Available());for(size_t i=0;i<before.size();++i)assert(!std::memcmp(image+NativeData::Sites[i].rva,before[i].data(),5));
    assert(Install()&&Available());
    assert(SelectedAuras::Install()&&SelectedAuras::Installed());
    assert(Available());SelectedAuras::Uninstall();assert(!SelectedAuras::Installed());
    MEMORY_BASIC_INFORMATION memory{};assert(VirtualQuery(relay,&memory,sizeof(memory))&&memory.Protect==PAGE_EXECUTE_READ);
    std::cout << "PASS all nine call sites and four native signatures, transactional rejection, idempotent installation, restoration, reinstall and RX relays.\n";
}
// Native loops keep their original source-key comparisons and next-node loads.
// Only the event allocator/destructor and the loop continuation are bounded
// test doubles. Both production leaf relay register adapters execute here.
std::array<Byte,0x200> unit{};
std::vector<Event> events;
void __fastcall Unlink(void*,void* node) {
    auto head=Field<Byte*>(unit.data(),0x140);Byte* previous=nullptr;
    while(head&&head!=node){previous=head;head=Field<Byte*>(head,0x30);}assert(head);
    auto next=Field<Byte*>(head,0x30);if(previous)Put(previous,0x30,next);else Put(unit.data(),0x140,next);
    deleted.push_back(Field<int>(node,0x1c));
}
void ResetEvents() {
    unit={};events.clear();events.reserve(8);deleted.clear();
    events.emplace_back(104,42);events.emplace_back(762,42);events.emplace_back(99,42);
    events.emplace_back(104,77);events.emplace_back(555,42,uint8_t(8));
    for(size_t i=0;i<events.size();++i)Put(events[i].data.data(),0x30,i+1<events.size()?events[i+1].data.data():static_cast<Byte*>(nullptr));
    Put(unit.data(),0x140,events[0].data.data());
}
std::vector<int> Remaining() {
    std::vector<int> ids;auto p=Field<Byte*>(unit.data(),0x140);while(p){ids.push_back(Field<int>(p,0x1c));p=Field<Byte*>(p,0x30);}return ids;
}
struct Replay {
    Byte* code=nullptr;uint32_t end=0;std::array<Byte,14> tail{};
    Replay(uint32_t begin,uint32_t finish,bool activation):end(finish) {
        code=static_cast<Byte*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));assert(code);
        Bytes b{0x55,0x53,0x56,0x57,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x20}; // preserve all touched nonvolatiles, shadow space
        if(activation)b.insert(b.end(),{0x4c,0x8b,0xf1,0x48,0x8b,0xfa,0x45,0x8b,0xf8,0x45,0x8b,0xe9}); // game r14, unit rdi, key r15d, skill r13d
        else b.insert(b.end(),{0x48,0x8b,0xf1,0x48,0x8b,0xfa,0x41,0x8b,0xd8,0x45,0x8b,0xf1}); // game rsi, unit rdi, key ebx, skill r14d
        auto jump=[&](uintptr_t target){b.insert(b.end(),{0xff,0x25,0,0,0,0});for(size_t i=0;i<8;++i)b.push_back(Byte(target>>(8*i)));};
        jump(uintptr_t(image+begin));const auto cleanup=uintptr_t(code+b.size());
        b.insert(b.end(),{0x48,0x83,0xc4,0x20,0x41,0x5f,0x41,0x5e,0x41,0x5d,0x5f,0x5e,0x5b,0x5d,0xc3});
        std::memcpy(code,b.data(),b.size());std::memcpy(tail.data(),image+end,14);Stub(end,cleanup);
        FlushInstructionCache(GetCurrentProcess(),code,b.size());
    }
    ~Replay(){std::memcpy(image+end,tail.data(),14);FlushInstructionCache(GetCurrentProcess(),image+end,14);VirtualFree(code,0,MEM_RELEASE);}
    void Run(int key,int skill){reinterpret_cast<void(__fastcall*)(void*,void*,int,int)>(code)(nullptr,unit.data(),key,skill);}
};
void Loops() {
    Stub(NativeData::DeleteEvent,&Unlink);
    for(bool activation:{true,false}) {
        Replay replay(activation?0x2d35e1:0x299b5b,activation?0x2d3614:0x299b92,activation);
        SetEnabled(true);ResetEvents();replay.Run(42,104);
        assert(Remaining()==std::vector<int>({762,99,104,555})&&deleted==std::vector<int>{104});
        // Source key zero is the native wildcard for monster properties. The
        // additional skill comparison still preserves the other aura types.
        ResetEvents();replay.Run(0,104);assert(Remaining()==std::vector<int>({762,99,555}));
        ResetEvents();replay.Run(42,762);assert(Remaining()==std::vector<int>({104,99,104,555}));
        SetEnabled(false);ResetEvents();replay.Run(42,104);assert(Remaining()==std::vector<int>({104,555}));
        ResetEvents();replay.Run(0,104);assert(Remaining()==std::vector<int>({555}));
    }
    // Invalid periodic skill cleanup still clears all events for its source.
    ResetEvents();SetEnabled(true);
    reinterpret_cast<void(__fastcall*)(void*,void*,int,int)>(image+0x2999e0)(nullptr,unit.data(),42,-1);
    assert(Remaining()==std::vector<int>({104,555}));
    // Execute the patched removal leaf with its real native event layout.
    using RemoveLeaf=void(__fastcall*)(void*,void*);
    auto remove=reinterpret_cast<RemoveLeaf>(relay+6*RelayStride);
    SetEnabled(true);ResetEvents();removalSkill=104;remove(nullptr,events[1].data.data());assert(deleted.empty());
    remove(nullptr,events[0].data.data());assert(deleted==std::vector<int>{104});removalSkill=-1;
    static int removals=0;
    Stub(NativeData::Deactivate,+[](void* game,void* object,int key,int skill){
        assert(removalSkill==skill&&object==unit.data()&&key==42);++removals;
        if(skill==104){RemovalScope(game,object,key,762);assert(removalSkill==104);}
    });
    for(size_t index:{size_t(7),size_t(8)}){
        reinterpret_cast<void(__fastcall*)(void*,void*,int,int)>(relay+index*RelayStride)(nullptr,unit.data(),42,104);
        assert(removalSkill==-1);
    }
    assert(removals==4);
    std::cout << "PASS shipped activation/periodic loops preserve distinct same-item auras and other sources; monster wildcard, toggle-off behavior, removal relay and both scoped callers verified.\n";
}
}
int main(int argc,char** argv){assert(argc==2);Basic();Map(argv[1]);Installation();Loops();std::cout<<"All Multiple Auras Per Item checks passed.\n";}
