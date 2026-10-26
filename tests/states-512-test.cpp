#include "../d2rhud/plugin/States512.cpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>

using namespace States512;
using namespace States512::Data;
uintptr_t testImage=0;
DWORD64 Pattern::Address(uint32_t rva){return testImage+rva;}
LONG WINAPI Fault(EXCEPTION_POINTERS* error){
    if(error->ExceptionRecord->ExceptionCode==EXCEPTION_ACCESS_VIOLATION){const auto& c=*error->ContextRecord;std::cerr<<"Native fixture fault RVA "<<std::hex<<(c.Rip-testImage)<<" rcx="<<c.Rcx<<" rdx="<<c.Rdx<<" r8="<<c.R8<<std::dec<<'\n';}
    return EXCEPTION_CONTINUE_SEARCH;
}
Unit unit{};std::array<uint8_t,0xb58> statList{};
std::vector<unsigned> began,finished,removed;
unsigned removalCallbacks=0;
void __fastcall RemovedCallback(Unit* u,int state){assert(u==&unit && state==511);++removalCallbacks;}
std::vector<std::vector<uint8_t>> sent;
int detached=0,freed=0,legacySends=0;
struct SentView {const uint8_t* begin;const uint8_t* end;};
void __fastcall Transport(void*,uint32_t client,const SentView* view){assert(client==44);sent.emplace_back(view->begin,view->end);}
void __fastcall LegacySend(Unit*,Client*){++legacySends;}
void __fastcall Detach(void*){++detached;uintptr_t empty=0;memcpy(statList.data()+0x68,&empty,8);}
void __fastcall Free(void*){++freed;}
struct Applied {int state,stat,value;uint16_t layer;};std::vector<Applied> applied;
Unit* __fastcall Lookup(uintptr_t,uint32_t,uint32_t id,uint32_t type){assert(id==unit.dwUnitId && type==unit.dwUnitType);return &unit;}
void __fastcall Begin(Unit* u,int state){assert(u==&unit);began.push_back(state);}
void __fastcall Finish(Unit* u,int state){assert(u==&unit);finished.push_back(state);}
void __fastcall End(Unit* u,int state){assert(u==&unit);removed.push_back(state);}
void* __fastcall Apply(Unit* u,void*,int state,int stat,int value,uint16_t layer){assert(u==&unit);applied.push_back({state,stat,value,layer});return reinterpret_cast<void*>(1);}
void __fastcall Nothing(){}
void Codec(){
    auto describe=[](uint16_t){return Schema{16,8,true};};
    for(unsigned state:{0,31,32,254,255,256,257,511})for(bool active:{false,true}){
        const auto packet=Packet(1,1234,state,active,{},describe);
        assert(packet.size()==7 && packet[0]==(active ? 0xa7 : 0xa9) && packet[6]==uint8_t(state));
        assert(bool(packet[1]&ExtendedType)==(state>=256));
        if(state>=256){const auto view=NativeView(packet,512);uint16_t decoded=0;memcpy(&decoded,view.data()+6,2);assert(decoded==state && view.size()==8 && view[1]==1);assert(NativeView(packet,state).empty());}
    }
    std::array<Stat,16> stats{};for(auto& s:stats)s={7,1,-321};
    const auto packet=Packet(1,1234,511,true,stats,describe);
    assert(packet[0]==0xa8 && packet[6]==packet.size() && packet.size()<255);
    const auto view=NativeView(packet,512);assert(view[6]==view.size() && view[7]==255 && view[8]==1);
    auto invalid=packet;invalid[1]=0x86;assert(NativeView(invalid,512).empty());
    invalid=packet;invalid[6]--;assert(NativeView(invalid,512).empty());
    assert(Packet(6,0,1,true,{},describe).empty() && Packet(1,0,512,true,{},describe).empty());
    assert(Packet(1,0,511,true,stats,[](uint16_t){return Schema{33,0,false};}).empty());
    std::cout<<"State packet boundaries 254/255/256/511, framing, payload bounds and malformed inputs passed.\n";
}
void Executable(const std::filesystem::path& path){
    std::ifstream input(path,std::ios::binary|std::ios::ate);assert(input);
    std::vector<uint8_t> file(size_t(input.tellg()));input.seekg(0);input.read(reinterpret_cast<char*>(file.data()),file.size());assert(input);
    auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(file.data());auto nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(file.data()+dos->e_lfanew);
    auto image=static_cast<uint8_t*>(VirtualAlloc(nullptr,nt->OptionalHeader.SizeOfImage,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));assert(image);
    const auto sections=IMAGE_FIRST_SECTION(nt);for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i)memcpy(image+sections[i].VirtualAddress,file.data()+sections[i].PointerToRawData,sections[i].SizeOfRawData);
    testImage=reinterpret_cast<uintptr_t>(image);assert(Supported());
    auto belowLimit=[&](uint64_t rows){
        // Execute the loader's actual unsigned comparison in an owned thunk.
        auto code=image+0x1ff0000;const uint8_t enter[]{0x53,0x48,0x89,0xcb};memcpy(code,enter,4);
        memcpy(code+4,image+0x1dc277,7);const uint8_t leave[]{0x0f,0x92,0xc0,0x5b,0xc3};memcpy(code+11,leave,5);
        FlushInstructionCache(GetCurrentProcess(),code,16);return reinterpret_cast<uint8_t(__fastcall*)(uint64_t)>(code)(rows)!=0;
    };
    assert(belowLimit(255) && !belowLimit(256));
    for(const uint64_t rows:{1,255}){
        memcpy(image+0x1c9ebf8,&rows,8);D2RHUD_States512Startup=0;
        assert(Install()&&!installed.load()&&!receiverCode&&D2RHUD_States512Startup==1);
        assert(*reinterpret_cast<uint32_t*>(image+0x1dc27a)==256&&Supported());
    }
    const uint64_t empty=0;memcpy(image+0x1c9ebf8,&empty,8);D2RHUD_States512Startup=0;
    const auto signature=Signatures.front();image[signature.rva]^=1;
    assert(!Install(false) && D2RHUD_States512Startup==0); // Disk report owns final readiness publication.
    FinishStartup(false,StartupDiagnostics::States,Status());
    assert(D2RHUD_States512Startup==2 && D2RHUD_StartupDiagnostics.failedFeatures==StartupDiagnostics::States && strstr(D2RHUD_StartupDiagnostics.detail,"signature mismatch"));
    FinishStartup(true);assert(D2RHUD_States512Startup==1 && !D2RHUD_StartupDiagnostics.failedFeatures && !D2RHUD_StartupDiagnostics.detail[0]);
    image[signature.rva]^=1;D2RHUD_States512Startup=0;
    for(const auto& s:Signatures){image[s.rva]^=1;assert(!Supported());image[s.rva]^=1;}
    assert(MH_Initialize()==MH_OK);
    void* conflict=nullptr;assert(MH_CreateHook(image+0xdd7d0,reinterpret_cast<void*>(Nothing),&conflict)==MH_OK);
    assert(!Install() && *reinterpret_cast<uint32_t*>(image+0x1dc27a)==256 && Supported());
    assert(MH_RemoveHook(image+0xdd7d0)==MH_OK);
    assert(Install() && *reinterpret_cast<uint32_t*>(image+0x1dc27a)==513);
    assert(belowLimit(256) && belowLimit(512) && !belowLimit(513));Uninstall();assert(Supported());
    auto stub=[&](uint32_t rva,auto function){const uint8_t jump[]{0xff,0x25,0,0,0,0};memcpy(image+rva,jump,6);auto target=reinterpret_cast<uintptr_t>(function);memcpy(image+rva+6,&target,8);};
    auto pointer=[&](uint32_t rva,uintptr_t value){memcpy(image+rva,&value,8);};
    std::vector<std::array<uint8_t,64>> states(512);for(auto& state:states){int16_t none=-1;memcpy(state.data()+0x1c,&none,2);memcpy(state.data()+0x30,&none,2);}
    pointer(0x1c9ebf0,reinterpret_cast<uintptr_t>(states.data()));pointer(0x1c9ebf8,512);
    std::array<std::array<uint8_t,0x144>,2> costs{};costs[1][4]=2;costs[1][8]=16;costs[1][9]=8;
    pointer(0x1c9fb78,reinterpret_cast<uintptr_t>(costs.data()));pointer(0x1c9fb80,costs.size());
    unit.dwUnitType=1;unit.dwUnitId=1234;unit.pStatListEx=reinterpret_cast<D2StatListExStrc*>(statList.data());
    std::array<uint8_t,14> nativeBegin{};memcpy(nativeBegin.data(),image+0x1693f0,nativeBegin.size());
    uint32_t extended=0x80000000;memcpy(statList.data()+0x1c,&extended,4);
    stub(0x66510,Lookup);stub(0x1693f0,Begin);stub(0x169bc0,Finish);stub(0x169780,End);stub(0x169a30,Apply);stub(0x1434c50,Nothing);stub(0x22e750,Nothing);
    FlushInstructionCache(GetCurrentProcess(),image,nt->OptionalHeader.SizeOfImage);
    // Cloned native handlers retain the game's decoder, stat application calls
    // and removal callbacks, but consume a local WORD-state view.
    originalOn=Native<Receiver>(0xdd7d0);originalStats=Native<Receiver>(0xdd830);originalOff=Native<Receiver>(0xdda30);
    for(unsigned state:{254,255,256,511}){
        auto on=Packet(1,1234,state,true,{},Describe);On(on.data());assert(began.back()==state && finished.back()==state);
        std::array<Stat,1> values{{{7,1,-321}}};auto stats=Packet(1,1234,state,true,values,Describe);Stats(stats.data());
        assert(applied.back().state==state && applied.back().stat==1 && applied.back().value==-321 && applied.back().layer==7);
        auto off=Packet(1,1234,state,false,{},Describe);Off(off.data());assert(removed.back()==state);
    }
    int16_t callback=0;memcpy(states[511].data()+0x1c,&callback,2);pointer(0x14b9910,reinterpret_cast<uintptr_t>(RemovedCallback));
    auto callbackOff=Packet(1,1234,511,false,{},Describe);Off(callbackOff.data());assert(removalCallbacks==1);
    DWORD64 unwindBase=0;assert(RtlLookupFunctionEntry(reinterpret_cast<uintptr_t>(receiverCode+0x170),&unwindBase,nullptr) && unwindBase==reinterpret_cast<uintptr_t>(receiverCode));
    // The existing native storage holds 16 active words and 16 graphics words.
    // Exercise the native clear operation at 511 without touching callbacks.
    auto flags=reinterpret_cast<uint32_t*>(statList.data()+0xac8);flags[15]=0x80000000;
    uint64_t sentinel=0x123456789abcdef0ULL;memcpy(statList.data()+0xb48,&sentinel,8);
    Native<void(__fastcall*)(Unit*,int)>(0x9fe30)(&unit,511);
    assert(flags[15]==0 && flags[31]==0x80000000 && *reinterpret_cast<uint64_t*>(statList.data()+0xb48)==sentinel);
    Client client{};client.dwClientId=44;std::array<uintptr_t,6> transport{};transport[5]=reinterpret_cast<uintptr_t>(Transport);
    pointer(0x18682b0,reinterpret_cast<uintptr_t>(transport.data()));stub(0x2d0130,Nothing);
    originalAll=originalUpdates=LegacySend;
    std::fill(flags,flags+16,0xffffffffu);std::fill(flags+16,flags+32,0);
    FlushInstructionCache(GetCurrentProcess(),image,nt->OptionalHeader.SizeOfImage);
    SendAll(&unit,&client);
    assert(sent.size()==512);
    for(unsigned i=0;i<512;++i){assert(sent[i][0]==0xa7 && sent[i][6]==uint8_t(i) && bool(sent[i][1]&ExtendedType)==(i>=256));}
    // Full and delta snapshots must not overwrite the adjacent callback fields.
    assert(*reinterpret_cast<uint64_t*>(statList.data()+0xb48)==sentinel);
    std::array<uint8_t,0x68> modifier{};uint32_t state=511;Stat modifierStat{7,1,-321};uint64_t one=1;
    auto listAddress=reinterpret_cast<uintptr_t>(modifier.data()),valueAddress=reinterpret_cast<uintptr_t>(&modifierStat);
    memcpy(statList.data()+0x68,&listAddress,8);memcpy(modifier.data()+0x20,&state,4);memcpy(modifier.data()+0x30,&valueAddress,8);memcpy(modifier.data()+0x38,&one,8);
    sent.clear();flags[16+15]=0x80000000;SendUpdates(&unit,&client);
    assert(sent.size()==1 && sent[0][0]==0xa8);Stats(sent[0].data());assert(applied.back().state==511 && applied.back().value==-321);
    flags[15]&=~0x80000000u;sent.clear();SendUpdates(&unit,&client);assert(sent.size()==1 && sent[0][0]==0xa9);Off(sent[0].data());assert(removed.back()==511);
    flags[15]|=0x80000000u;std::array<uint32_t,16> removable{};removable[15]=0x80000000;
    pointer(0x1c9ec60,reinterpret_cast<uintptr_t>(removable.data()));stub(0x1e4300,Detach);stub(0x1e45b0,Free);
    FlushInstructionCache(GetCurrentProcess(),image,nt->OptionalHeader.SizeOfImage);
    RemoveCurable(&unit);assert(detached==1 && freed==1 && !(flags[15]&0x80000000) && flags[0]==0xffffffff);
    assert(*reinterpret_cast<uint64_t*>(statList.data()+0xb48)==sentinel);
    pointer(0x1c9ebf8,255);SendAll(&unit,&client);SendUpdates(&unit,&client);assert(legacySends==2);
    pointer(0x1c9ebf8,512);memcpy(image+0x1693f0,nativeBegin.data(),nativeBegin.size());
    states[511][0x15]=1;unit.dwAnimMode=12;flags[15]=0;flags[31]=0;
    FlushInstructionCache(GetCurrentProcess(),image,nt->OptionalHeader.SizeOfImage);
    auto realOn=Packet(1,1234,511,true,{},Describe);On(realOn.data());
    assert(flags[15]==0x80000000 && flags[31]==0x80000000 && *reinterpret_cast<uint64_t*>(statList.data()+0xb48)==sentinel);
    std::cout<<"Native high-state receivers, signed stat decoding, graphics cleanup dispatch, unwind metadata and state-511 storage boundary passed.\n";
    std::cout<<"All 512 active states synchronize individually; high-state deltas, stat payloads, removal, curse removal and legacy delegation passed.\n";
    // Receiver copies and their registered unwind metadata intentionally live
    // for the process lifetime, like the installed extension.
    testImage=0;
}
int main(int argc,char** argv){std::cout<<std::unitbuf;AddVectoredExceptionHandler(1,Fault);Codec();if(argc>1)Executable(argv[1]);}
