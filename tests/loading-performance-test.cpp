#define NOMINMAX
#include <Windows.h>
#include "../d2rhud/plugin/LoadingPerformanceNative.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <span>
#include <vector>

namespace N = LoadingPerformance::Native;
namespace {
unsigned checks = 0;
void Check(bool condition, const char* message) {
    ++checks;
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::abort(); }
}
struct Allocation {
    uint8_t* bytes;
    explicit Allocation(size_t size) : bytes(static_cast<uint8_t*>(VirtualAlloc(nullptr,size,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE))) { Check(bytes != nullptr,"native fixture allocation"); }
    ~Allocation() { VirtualFree(bytes,0,MEM_RELEASE); }
};
struct Captured { uint64_t rax,rdx,rsp,r9,r10,r11,cap,available,dispatched; };
constexpr uint64_t Marker10=0x1213141516171819, Marker11=0x8182838485868788;
void RelayExecution() {
    Allocation memory(0x1000);
    auto entry=memory.bytes, relay=memory.bytes+0x100, resume=memory.bytes+0x200, flag=memory.bytes+0x300;
    // Win64 arguments: RCX=inFlight (high bits seeded), RDX=queued, R8=capture.
    // The wrapper has no stack frame and jumps through the production relay.
    const uint8_t prefix[]{0x48,0x89,0xc8,0x49,0x89,0xe1,0x49,0xba}; // mov rax,rcx; mov r9,rsp; mov r10,imm64
    std::memcpy(entry,prefix,sizeof(prefix));std::memcpy(entry+sizeof(prefix),&Marker10,8);
    entry[16]=0x49;entry[17]=0xbb;std::memcpy(entry+18,&Marker11,8);
    entry[26]=0xe9;const auto into=int32_t(relay-(entry+31));std::memcpy(entry+27,&into,4);
    auto generated=N::Relay(uintptr_t(relay),uintptr_t(flag),uintptr_t(resume));
    Check(generated.has_value(),"production relay emits for nearby flag/resume");
    std::memcpy(relay,generated->data(),generated->size());
    const uint8_t suffix[]{
        0x49,0x89,0x00,                 // mov [r8],rax
        0x49,0x89,0x50,0x08,            // mov [r8+8],rdx
        0x49,0x89,0x60,0x10,            // mov [r8+16],rsp
        0x4d,0x89,0x48,0x18,            // mov [r8+24],r9
        0x4d,0x89,0x50,0x20,            // mov [r8+32],r10
        0x4d,0x89,0x58,0x28,            // mov [r8+40],r11
        0x49,0x89,0x48,0x30,            // mov [r8+48],rcx (selected cap)
        0x2b,0xc8,                      // sub ecx,eax: original native next instruction
        0x49,0x89,0x48,0x38,            // mov [r8+56],rcx (nonnegative capacity)
        0x48,0x39,0xd1,                 // cmp rcx,rdx
        0x48,0x0f,0x47,0xca,            // cmova rcx,rdx: min(capacity,queued)
        0x49,0x89,0x48,0x40,            // mov [r8+64],rcx
        0xc3
    };
    std::memcpy(resume,suffix,sizeof(suffix));
    Check(FlushInstructionCache(GetCurrentProcess(),memory.bytes,0x1000)!=FALSE,"flush native relay fixture");
    using Run=void(__fastcall*)(uint64_t,uint64_t,Captured*);
    const auto run=reinterpret_cast<Run>(entry);
    const std::array<uint32_t,11> usedValues{0,15,16,17,63,64,65,127,4096,65535,0x7fffffff};
    const std::array<uint64_t,7> queues{0,1,5,16,64,4096,uint64_t(1)<<40};
    for(uint8_t loading:std::array<uint8_t,4>{0,1,2,255})for(auto used:usedValues)for(auto queued:queues){
        *flag=loading;
        const uint64_t input=0xa1b2c3d400000000ULL|used;
        Captured out{};run(input,queued,&out);
        const auto cap=std::max(loading?N::LoadingLimit:N::NormalLimit,used);
        const auto capacity=cap-used;
        Check(out.cap==cap,"selected cap matches loading flag and in-flight clamp");
        Check(out.available==capacity && out.available<=N::LoadingLimit,"native subtraction stays nonnegative/bounded");
        Check(out.dispatched==std::min(uint64_t(capacity),queued),"native-style queue clamp handles empty/small/large queue");
        Check(out.rax==input && out.rdx==queued,"relay preserves full RAX/EAX and RDX");
        Check(out.rsp==out.r9 && (out.rsp&15)==8,"relay preserves stack pointer and Win64 entry alignment");
        Check(out.r10==Marker10 && out.r11==Marker11,"relay preserves other volatile register markers");
    }
    // Loading can finish before the outstanding upload cohort drains back to16.
    for(uint32_t used=64;;--used){
        *flag=0;Captured out{};run(used,1000,&out);
        Check(out.available==(used>=16?0:16-used),"loading-end drain cannot produce negative availability or admit excess work");
        if(!used)break;
    }
    std::cout<<"Native relay: loading on/off, in-flight boundaries, queue clamps and register/stack preservation passed.\n";
}
void RelativeBounds() {
    const uintptr_t start=0x180000000ULL;
    const auto length=N::Relay(start,start,start)->size();
    Check(!N::Relay(start,start+12+0x80000000ULL,start+100),"flag rel32 upper overflow rejected");
    Check(!N::Relay(start,start+12-0x80000001ULL,start+100),"flag rel32 lower overflow rejected");
    Check(!N::Relay(start,start+100,start+length+0x80000000ULL),"resume rel32 upper overflow rejected");
    Check(!N::Relay(start,start+100,start+length-0x80000001ULL),"resume rel32 lower overflow rejected");
    auto low=N::Relay(start,start+12-0x80000000ULL,start+length-0x80000000ULL);
    auto high=N::Relay(start,start+12+0x7fffffffULL,start+length+0x7fffffffULL);
    Check(low.has_value()&&high.has_value(),"inclusive signed rel32 limits accepted");
    int32_t displacement=0;std::memcpy(&displacement,low->data()+7,4);Check(displacement==INT32_MIN,"negative flag displacement encoded exactly");
    std::memcpy(&displacement,high->data()+length-4,4);Check(displacement==INT32_MAX,"positive resume displacement encoded exactly");
}

CONTEXT Unwind(DWORD64 base,PRUNTIME_FUNCTION function,DWORD64 pc,CONTEXT context,DWORD64& frame) {
    PVOID handler=nullptr;frame=0;
    RtlVirtualUnwind(UNW_FLAG_NHANDLER,base,pc,function,&context,&handler,&frame,nullptr);
    return context;
}
void CompareUnwind(uint8_t* image,RUNTIME_FUNCTION original,RUNTIME_FUNCTION relay,uintptr_t resume) {
    alignas(16) std::array<uint64_t,2048> stack{};
    for(size_t i=0;i<stack.size();++i)stack[i]=uintptr_t(stack.data()+1024)+(i%64)*8;
    CONTEXT initial{};initial.ContextFlags=CONTEXT_FULL;initial.Rsp=uintptr_t(stack.data()+512);
    initial.Rbp=uintptr_t(stack.data()+768);initial.Rbx=initial.Rsi=initial.Rdi=initial.R12=initial.R13=initial.R14=initial.R15=initial.Rbp;
    DWORD64 originalFrame=0;
    initial.Rip=resume;
    const auto direct=Unwind(uintptr_t(image),&original,resume,initial,originalFrame);
    for(auto offset:std::array<unsigned,8>{0,5,12,14,19,21,24,26}){
        auto trial=initial;trial.Rip=uintptr_t(image)+relay.BeginAddress+offset;DWORD64 relayFrame=0;
        trial=Unwind(uintptr_t(image),&relay,trial.Rip,trial,relayFrame);
        if(trial.Rip!=direct.Rip || trial.Rsp!=direct.Rsp || relayFrame!=originalFrame)
            std::cerr<<"Unwind mismatch at relay+"<<offset<<" originalRF "<<std::hex<<original.BeginAddress<<".."<<original.EndAddress<<" unwind "<<original.UnwindData<<" header "<<unsigned(image[original.UnwindData])<<","<<unsigned(image[original.UnwindData+1])<<","<<unsigned(image[original.UnwindData+2])<<","<<unsigned(image[original.UnwindData+3])<<" direct RIP/SP/frame "<<direct.Rip<<"/"<<direct.Rsp<<"/"<<originalFrame<<" relay "<<trial.Rip<<"/"<<trial.Rsp<<"/"<<relayFrame<<std::dec<<'\n';
        Check(trial.Rip==direct.Rip && trial.Rsp==direct.Rsp && relayFrame==originalFrame,"chained relay unwind restores same caller PC/SP/frame as original body");
        Check(trial.Rbx==direct.Rbx && trial.Rbp==direct.Rbp && trial.Rsi==direct.Rsi && trial.Rdi==direct.Rdi && trial.R12==direct.R12 && trial.R13==direct.R13 && trial.R14==direct.R14 && trial.R15==direct.R15,"chained relay unwind restores original nonvolatile registers");
        Check(!std::memcmp(&trial.Xmm6,&direct.Xmm6,10*sizeof(M128A)),"chained relay unwind restores original nonvolatile SIMD registers");
    }
}
void SyntheticUnwind() {
    Allocation memory(0x2000);auto image=memory.bytes;
    const uint8_t originalCode[]{0x53,0x48,0x83,0xec,0x20,0x90,0x90,0x90,0x90,0x90,0x48,0x83,0xc4,0x20,0x5b,0xc3};
    std::memcpy(image+0x100,originalCode,sizeof(originalCode));
    // push rbx; sub rsp,32. Original full-frame unwind must be retained at every relay instruction.
    const uint8_t unwind[]{1,5,2,0,5,0x32,1,0x30};std::memcpy(image+0x380,unwind,sizeof(unwind));
    RUNTIME_FUNCTION original{0x100,0x110,0x380}, relay{0x200,0x220,0x400};
    auto code=N::Relay(uintptr_t(image+relay.BeginAddress),uintptr_t(image+0x300),uintptr_t(image+0x105));Check(code.has_value(),"synthetic chained relay build");
    std::memcpy(image+relay.BeginAddress,code->data(),code->size());
    const uint8_t chained[]{uint8_t(1|(UNW_FLAG_CHAININFO<<3)),0,0,0};std::memcpy(image+0x400,chained,sizeof(chained));std::memcpy(image+0x404,&original,sizeof(original));
    std::array<RUNTIME_FUNCTION,2> functions{original,relay};
    Check(RtlAddFunctionTable(functions.data(),DWORD(functions.size()),uintptr_t(image))!=FALSE,"register original and chained relay unwind entries");
    DWORD64 base=0;auto found=RtlLookupFunctionEntry(uintptr_t(image)+relay.BeginAddress,&base,nullptr);
    Check(found && found->BeginAddress==relay.BeginAddress && base==uintptr_t(image),"runtime lookup finds chained relay metadata");
    CompareUnwind(image,original,relay,uintptr_t(image)+0x105);
    Check(RtlDeleteFunctionTable(functions.data())!=FALSE,"remove synthetic runtime function table");
    std::cout<<"Synthetic chained unwind matches the original frame at every relay instruction, including jump edge.\n";
}

void NativeImage(const char* path) {
    std::ifstream input(std::filesystem::path(path),std::ios::binary|std::ios::ate);Check(bool(input),"open native image fixture");
    std::vector<uint8_t> file(size_t(input.tellg()));input.seekg(0);Check(bool(input.read(reinterpret_cast<char*>(file.data()),std::streamsize(file.size()))),"read native image fixture");
    Check(file.size()>=sizeof(IMAGE_DOS_HEADER),"native DOS header bounds");auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(file.data());
    Check(dos->e_magic==IMAGE_DOS_SIGNATURE && dos->e_lfanew>0 && size_t(dos->e_lfanew)+sizeof(IMAGE_NT_HEADERS64)<=file.size(),"native PE header bounds");
    auto nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(file.data()+dos->e_lfanew);Check(nt->Signature==IMAGE_NT_SIGNATURE && nt->FileHeader.Machine==IMAGE_FILE_MACHINE_AMD64,"native x64 image");
    Allocation mapped(size_t(nt->OptionalHeader.SizeOfImage)+0x10000);auto image=mapped.bytes;std::memcpy(image,file.data(),nt->OptionalHeader.SizeOfHeaders);
    for(auto section=IMAGE_FIRST_SECTION(nt);section<IMAGE_FIRST_SECTION(nt)+nt->FileHeader.NumberOfSections;++section){
        Check(size_t(section->PointerToRawData)+section->SizeOfRawData<=file.size() && size_t(section->VirtualAddress)+section->SizeOfRawData<=nt->OptionalHeader.SizeOfImage,"native section bounds");
        std::memcpy(image+section->VirtualAddress,file.data()+section->PointerToRawData,section->SizeOfRawData);
    }
    for(auto signature:N::Signatures)Check(N::Hash(std::span(image+signature.rva,signature.size))==signature.hash,"native audited loading/upload signatures match");
    Check(!std::memcmp(image+N::Budget,N::Original.data(),N::Original.size()),"original budget MOV matches native target");
    const auto exception=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    auto functions=reinterpret_cast<const RUNTIME_FUNCTION*>(image+exception.VirtualAddress);
    auto end=functions+exception.Size/sizeof(RUNTIME_FUNCTION);
    auto original=std::find_if(functions,end,[](const auto& function){return N::Budget>=function.BeginAddress && N::Budget<function.EndAddress;});
    Check(original!=end,"target has existing native unwind descriptor");
    const auto relayRva=(nt->OptionalHeader.SizeOfImage+0xfff)&~0xfffu;
    const auto unwindRva=relayRva+0x100;
    RUNTIME_FUNCTION relay{relayRva,relayRva+32,unwindRva};
    auto code=N::Relay(uintptr_t(image+relayRva),uintptr_t(image+N::Loading),uintptr_t(image+N::Resume));Check(code.has_value(),"real-image relay RIP displacements fit");
    std::memcpy(image+relayRva,code->data(),code->size());
    const uint8_t header[]{uint8_t(1|(UNW_FLAG_CHAININFO<<3)),0,0,image[original->UnwindData+3]};
    std::memcpy(image+unwindRva,header,sizeof(header));std::memcpy(image+unwindRva+4,&*original,sizeof(*original));
    Check(RtlAddFunctionTable(&relay,1,uintptr_t(image))!=FALSE,"register relay chained to actual game unwind entry");
    DWORD64 base=0;auto found=RtlLookupFunctionEntry(uintptr_t(image)+relayRva+24,&base,nullptr);
    Check(found && found->BeginAddress==relayRva && base==uintptr_t(image),"real-image dynamic relay lookup at jump edge");
    CompareUnwind(image,*original,relay,uintptr_t(image)+N::Resume);
    Check(RtlDeleteFunctionTable(&relay)!=FALSE,"remove real-image dynamic relay metadata");
    std::cout<<"Mapped native image signatures and original-frame chained unwind passed; no game process launched or attached.\n";
}
}
int main(int argc,char** argv){
    std::cout<<std::unitbuf;
    RelativeBounds();RelayExecution();SyntheticUnwind();if(argc>1)NativeImage(argv[1]);
    std::cout<<"PASS: "<<checks<<" loading-performance checks.\n";
}
