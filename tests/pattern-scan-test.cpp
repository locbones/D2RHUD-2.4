#include <Windows.h>
#include "../d2rhud/Pattern.h"
#include "../d2rhud/PatternScan.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>

namespace {
int checks = 0;
void Check(bool ok, const char* reason) { ++checks; if (!ok) throw std::runtime_error(reason); }
size_t Find(const std::vector<uint8_t>& data, const char* pattern) { return PatternScan::Find(data.data(), data.size(), PatternScan::Compile(pattern)); }
void UnitTests() {
    using PatternScan::NotFound;
    const std::vector<uint8_t> bytes{0x3f,0x48,0x83,0xff,0x48,0x83,0x00};
    Check(Find(bytes,"48 83") == 1,"first occurrence");
    Check(Find(bytes,"48 ?? 00") == 4,"last legal occurrence");
    Check(Find(bytes,"3F 48 83 FF 48 83 00") == 0,"exact image size");
    Check(Find(bytes,"3F 48 83 FF 48 83 00 FF") == NotFound,"oversized pattern");
    Check(Find(bytes,"") == NotFound && Find(bytes," \t\r\n") == NotFound,"empty pattern");
    Check(Find({},"48") == NotFound,"empty image");
    Check(Find(bytes,"3F") == 0 && Find({0x7f},"3F") == NotFound,"literal wildcard byte remains literal");
    Check(Find(bytes,"? 48 ??") == 0,"both wildcard spellings");
    Check(Find(bytes,"?? ?") == 0,"all wildcard pattern");
    Check(Find(bytes,"\t3f\n48\r83 ") == 0,"case and whitespace");
    Check(Find(bytes,"3F4883") == 0,"contiguous byte pairs");
    Check(Find(bytes,"GG") == NotFound && Find(bytes,"4") == NotFound && Find(bytes,"48 X0") == NotFound,"invalid signatures rejected");
    Check(PatternScan::Find(nullptr,7,PatternScan::Compile("?")) == NotFound,"null image");
    Check(Find({0,1,2,3,4},"? 01 02 03 ?") == 0,"anchor with leading and trailing wildcards");
    Check(Find({1,2,0,1,2,3},"01 02 03") == 3,"partial anchor mismatch advances");

    std::array<uint8_t,16> relative{};int32_t displacement=-9;std::memcpy(relative.data()+5,&displacement,4);
    Check(PatternScan::ResolveReference(relative.data(),relative.size(),0x1000,4,1)==0x1000,"negative RIP displacement");
    displacement=20;std::memcpy(relative.data()+5,&displacement,4);
    Check(PatternScan::ResolveReference(relative.data(),relative.size(),0x1000,4,1)==0x101d,"positive RIP displacement");
    Check(PatternScan::ResolveReference(relative.data(),relative.size(),0x1000,6,-1)==0x101d,"negative operand offset");
    Check(PatternScan::ResolveReference(relative.data(),relative.size(),0x1000,0,-1)==0,"operand before image");
    Check(PatternScan::ResolveReference(relative.data(),relative.size(),0x1000,13,0)==0,"truncated operand");
    Check(PatternScan::ResolveReference(relative.data(),relative.size(),0x1000,NotFound,1)==0,"missing match reference");

    // Independent byte/mask oracle exercises first match, wildcard gaps and
    // repeated anchors, with deliberate injections and missing patterns.
    std::mt19937 random(0x17a8);
    for (int test=0;test<5000;++test) {
        std::vector<uint8_t> data(random()%160), values(1+random()%24);std::vector<bool> wild(values.size());std::string text;
        constexpr char digits[]="0123456789ABCDEF";
        for(auto& value:data)value=uint8_t(random()%16);
        for(size_t i=0;i<values.size();++i){values[i]=uint8_t(random()%16);wild[i]=random()%3==0;
            if(wild[i])text+=i%2?"? ":"?? ";else{text+=digits[values[i]>>4];text+=digits[values[i]&15];text+=' ';}}
        if(test%2 && data.size()>=values.size()){const auto at=random()%(data.size()-values.size()+1);for(size_t i=0;i<values.size();++i)if(!wild[i])data[at+i]=values[i];}
        size_t expected=NotFound;
        if(data.size()>=values.size())for(size_t at=0;at<=data.size()-values.size();++at){bool match=true;for(size_t i=0;i<values.size();++i)if(!wild[i]&&data[at+i]!=values[i]){match=false;break;}if(match){expected=at;break;}}
        Check(Find(data,text.c_str())==expected,"randomized oracle parity");
    }

    SYSTEM_INFO info{};GetSystemInfo(&info);
    auto page=static_cast<uint8_t*>(VirtualAlloc(nullptr,size_t(info.dwPageSize)*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    Check(page!=nullptr,"guard test allocation");DWORD old=0;
    Check(VirtualProtect(page+info.dwPageSize,info.dwPageSize,PAGE_NOACCESS,&old)!=0,"guard page protection");
    page[info.dwPageSize-2]=0x12;page[info.dwPageSize-1]=0x34;
    Check(PatternScan::Find(page,info.dwPageSize,PatternScan::Compile("12 34"))==info.dwPageSize-2,"page end match without overread");
    Check(PatternScan::Find(page,info.dwPageSize,PatternScan::Compile("12 35"))==NotFound,"page end mismatch without overread");
    VirtualFree(page,0,MEM_RELEASE);
    Check(Pattern::Scan(nullptr,nullptr)==0,"public scanner null signature");
    Check(Pattern::Scan(nullptr,"")==0,"public scanner empty signature");
    Check(Pattern::Scan(L"missing-pattern-test-module.dll","48")==0,"public scanner missing module");
    Check(Pattern::Scan(nullptr,"4D 5A")==Pattern::BaseAddress(nullptr),"public scanner first image match");
    int32_t headerValue=0;const auto moduleBase=Pattern::BaseAddress(nullptr);
    std::memcpy(&headerValue,reinterpret_cast<const void*>(moduleBase),sizeof(headerValue));
    Check(Pattern::ScanRef(nullptr,"4D 5A",0)==moduleBase+4+headerValue,"public relative scanner integration");
}
struct Startup { const char* name; const char* pattern; int operand; };
constexpr std::array<Startup,11> StartupPatterns{{
    {"STATLIST_GetUnitStatSigned","48 83 EC 28 45 0F B7 C8 48 85 C9 74 42 48 8B 89 ? ? ? ? 48 85 C9 74 2F",-1},
    {"UNITS_GetClientUnitByIdAndType","E8 ? ? ? ? 8B 4F 05",1},
    {"UNITS_GetServerUnitByTypeAndId","E8 ? ? ? ? 46 8D 04 37 ",1},
    {"ITEMS_Description","E8 ? ? ? ? 8B 8C 24 ? ? ? ? 48 FF C7",1},
    {"INVENTORY_AutoSort","40 53 56 41 55 B8 ? ? ? ?",-1},
    {"D2CLIENT_Transmute","E8 ? ? ? ? EB 41 48 8B 03",1},
    {"CLIENT_RevealAutoMapRoom","48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 30 E8 ? ? ? ? 33 FF",-1},
    {"gMouseHover","48 8D 3D ? ? ? ? BB ? ? ? ? 48 8B CF E8 ? ? ? ? 48 83 C7 10 ",3},
    {"gCurrentSinglePlayerGame","48 8B 15 ? ? ? ? 33 C9 48 8D 42 01 48 83 F8 01 48 0F 47 CA",3},
    {"CLIENT_playerCommand","E8 E6 A6 0A 00",1},
    {"CLIENT_DebugExit","E8 ?? ?? ?? ?? CC 45 33 DB",1}
}};
std::vector<char> LegacyCompile(const char* pattern) {
    std::vector<char> bytes;auto current=const_cast<char*>(pattern);const auto end=current+std::strlen(pattern);
    for(;current<end;++current)if(*current=='?'){++current;if(*current=='?')++current;bytes.push_back('?');}
        else bytes.push_back(static_cast<char>(std::strtoul(current,&current,16)));
    return bytes;
}
__declspec(noinline) size_t LegacyFind(const std::vector<uint8_t>& image,const char* signature) {
    const auto pattern=LegacyCompile(signature);
    for(size_t at=0;at<image.size()-pattern.size();++at){bool match=true;
        for(size_t i=0;i<pattern.size();++i)match &= pattern[i]=='?' || pattern[i]==static_cast<char>(image[at+i]);
        if(match)return at;}
    return PatternScan::NotFound;
}
std::vector<uint8_t> MappedImage(const wchar_t* path) {
    const auto handle=LoadLibraryExW(path,nullptr,LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if(!handle)throw std::runtime_error("Could not map executable as a non-executing image");
    struct Release{HMODULE value;~Release(){FreeLibrary(value);}} release{handle};
    const auto base=reinterpret_cast<const uint8_t*>(reinterpret_cast<uintptr_t>(handle)&~uintptr_t(3));
    const auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    Check(dos->e_magic==IMAGE_DOS_SIGNATURE,"mapped DOS header");
    const auto nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
    Check(nt->Signature==IMAGE_NT_SIGNATURE,"mapped PE header");
    std::vector<uint8_t> image(nt->OptionalHeader.SizeOfImage);
    size_t copied=0;
    for(size_t at=0;at<image.size();){MEMORY_BASIC_INFORMATION region{};if(!VirtualQuery(base+at,&region,sizeof(region)))throw std::runtime_error("Mapped image query failed");
        const auto count=(std::min)(image.size()-at,size_t(static_cast<const uint8_t*>(region.BaseAddress)+region.RegionSize-(base+at)));
        if(region.State==MEM_COMMIT && !(region.Protect&(PAGE_NOACCESS|PAGE_GUARD))){SIZE_T got=0;
            if(!ReadProcessMemory(GetCurrentProcess(),base+at,image.data()+at,count,&got)||got!=count)throw std::runtime_error("Mapped image read failed");copied+=count;}
        at+=count;}
    std::cout<<"Mapped D2R image: "<<image.size()<<" bytes; readable bytes copied: "<<copied<<". No executable code was run.\n";
    return image;
}
volatile size_t observed=0;
void Benchmark(const wchar_t* path) {
    const auto image=MappedImage(path);
    constexpr uintptr_t base=0x140000000;
    for(size_t i=0;i<StartupPatterns.size();++i){const auto& signature=StartupPatterns[i];
        const auto legacy=LegacyFind(image,signature.pattern), modern=Find(image,signature.pattern);
        Check(legacy==modern,"startup signature address parity");
        if(modern==PatternScan::NotFound){std::cout<<signature.name<<": absent in both scanners (NULL)\n";continue;}
        uintptr_t target=base+modern;
        if(signature.operand>=0){int32_t offset=0;std::memcpy(&offset,image.data()+legacy+signature.operand,4);
            const auto legacyTarget=base+legacy+signature.operand+4+offset;
            target=PatternScan::ResolveReference(image.data(),image.size(),base,modern,signature.operand);
            Check(legacyTarget==target,"startup RIP target parity");}
        std::cout<<signature.name<<": match RVA 0x"<<std::hex<<modern<<", target RVA 0x"<<target-base<<std::dec<<"\n";}
    std::array<double,9> oldMs{},newMs{};
    const auto run=[&](bool optimized){const auto begin=std::chrono::steady_clock::now();size_t sum=0;
        for(const auto& signature:StartupPatterns)sum+=optimized?Find(image,signature.pattern):LegacyFind(image,signature.pattern);
        observed=sum;return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();};
    run(false);run(true);
    for(size_t i=0;i<oldMs.size();++i){if(i%2){newMs[i]=run(true);oldMs[i]=run(false);}else{oldMs[i]=run(false);newMs[i]=run(true);}}
    std::sort(oldMs.begin(),oldMs.end());std::sort(newMs.begin(),newMs.end());
    std::cout<<std::fixed<<std::setprecision(3)<<"All 11 startup scans, nine alternating warm rounds (compile + search):\n"
        <<"Legacy median: "<<oldMs[4]<<" ms (min "<<oldMs.front()<<", max "<<oldMs.back()<<")\n"
        <<"Optimized median: "<<newMs[4]<<" ms (min "<<newMs.front()<<", max "<<newMs.back()<<")\n"
        <<"Saved: "<<oldMs[4]-newMs[4]<<" ms; speedup: "<<oldMs[4]/newMs[4]<<"x\n";
}
}
int wmain(int argc,wchar_t** argv) {try{UnitTests();if(argc>1)Benchmark(argv[1]);std::cout<<"Passed "<<checks<<" pattern scan checks.\n";return 0;}
    catch(const std::exception& e){std::cerr<<"FAILED: "<<e.what()<<" ("<<checks<<" checks)\n";return 1;}}
