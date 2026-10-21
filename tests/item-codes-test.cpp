// Run only the HUD's replacement instructions in an owned test buffer.
// The optional D2R executable and mod JSON files are read, never executed/edited.
#include "../d2rhud/plugin/ItemCodes.cpp"
#include "../d2rhud/json.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

uintptr_t testExecutable = 0;
DWORD64 Pattern::Address(uint32_t rva) { return testExecutable ? testExecutable + rva : 0; }
using Pack = uint32_t(__fastcall*)(const char*);
struct Encoder {
    uint8_t* code = nullptr;
    Pack call = nullptr;
    explicit Encoder(std::span<const uint8_t> block) {
        const uint8_t prefix[]{0x53,0x48,0x83,0xec,0x30,0x48,0x8b,0xd9,0xc7,0x44,0x24,0x20,0x20,0x20,0x20,0x20};
        const uint8_t suffix[]{0x8b,0x44,0x24,0x20,0x48,0x83,0xc4,0x30,0x5b,0xc3};
        code = static_cast<uint8_t*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE)); assert(code);
        memcpy(code,prefix,sizeof(prefix)); memcpy(code+sizeof(prefix),block.data(),block.size());
        memcpy(code+sizeof(prefix)+block.size(),suffix,sizeof(suffix));
        DWORD old=0; assert(VirtualProtect(code,4096,PAGE_EXECUTE_READ,&old)); FlushInstructionCache(GetCurrentProcess(),code,4096);
        call = reinterpret_cast<Pack>(code);
    }
    ~Encoder() { VirtualFree(code,0,MEM_RELEASE); }
};
uint32_t Expected(const std::string& text) {
    uint32_t result=0x20202020;
    for(size_t i=0;i<4 && i<text.size() && text[i];++i) result=(result&~(0xffu<<(i*8)))|(uint32_t(uint8_t(text[i]))<<(i*8));
    return result;
}
struct Executable {
    std::vector<uint8_t> bytes;
    const IMAGE_NT_HEADERS64* nt=nullptr;
    explicit Executable(const std::filesystem::path& path) {
        std::ifstream input(path,std::ios::binary|std::ios::ate); assert(input);
        bytes.resize(static_cast<size_t>(input.tellg())); input.seekg(0); input.read(reinterpret_cast<char*>(bytes.data()),bytes.size()); assert(input);
        const auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes.data()); assert(dos->e_magic==IMAGE_DOS_SIGNATURE);
        nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(bytes.data()+dos->e_lfanew); assert(nt->Signature==IMAGE_NT_SIGNATURE);
    }
    uintptr_t At(uint32_t rva) const {
        auto sections=IMAGE_FIRST_SECTION(nt);
        for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i) if(rva>=sections[i].VirtualAddress && rva-sections[i].VirtualAddress<sections[i].SizeOfRawData)
            return reinterpret_cast<uintptr_t>(bytes.data()+sections[i].PointerToRawData+rva-sections[i].VirtualAddress);
        return 0;
    }
};
int main(int argc,char** argv) {
    using namespace ItemCodes;
    Encoder original(Data::Original), extended(Data::Replacement);
    for(const std::string text:{"","a","ab","abc","cap","hax","r01"}) {
        assert(original.call(text.c_str())==Expected(text));
        assert(extended.call(text.c_str())==original.call(text.c_str()));
    }
    assert(original.call("abc1")==original.call("abc2"));
    assert(extended.call("abc1")!=extended.call("abc2"));
    assert(extended.call("abc1")!=extended.call("abc"));
    for(const std::string text:{"Tm00","Tm01","Ev10","Ev11","1337","twrk","abcd","abcdef"}) assert(extended.call(text.c_str())==Expected(text));
    for(size_t position=0;position<4;++position) for(unsigned byte=1;byte<=255;++byte) {
        std::string text="aBc1";text[position]=char(byte); assert(extended.call(text.c_str())==Expected(text));
    }
    auto pages=static_cast<char*>(VirtualAlloc(nullptr,8192,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));assert(pages);
    DWORD old=0;assert(VirtualProtect(pages+4096,4096,PAGE_NOACCESS,&old));
    for(const std::string text:{"","a","ab","abc","abcd"}) {
        auto end=pages+4096-text.size()-1;memcpy(end,text.c_str(),text.size()+1);
        assert(extended.call(end)==Expected(text)); // No read past a short key's NUL.
    }
    memcpy(pages+4092,"abcd",4);assert(extended.call(pages+4092)==Expected("abcd")); // No fifth-byte read.
    VirtualFree(pages,0,MEM_RELEASE);
    assert(!Supported([](uint32_t){return uintptr_t(0);}));
    std::cout<<"Native replacement instructions preserve short codes, distinguish all four bytes and respect guard pages.\n";
    if(argc>1) {
        Executable exe(std::filesystem::path(argv[1])/"D2R.exe");
        assert(Supported([&](uint32_t rva){return exe.At(rva);}));
        assert(memcmp(reinterpret_cast<const void*>(exe.At(Data::PackRva)),Data::Original.data(),Data::PackSize)==0);
        constexpr size_t imageSize=0x2000000;
        auto image=static_cast<uint8_t*>(VirtualAlloc(nullptr,imageSize,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));assert(image);
        testExecutable=reinterpret_cast<uintptr_t>(image);
        for(const auto& signature:Data::Signatures) memcpy(image+signature.rva,reinterpret_cast<const void*>(exe.At(signature.rva)),signature.size);
        uint64_t count=1;memcpy(image+Data::ManagerRva+0xe0,&count,8);
        assert(Apply()==Result::Late && !Installed());
        assert(memcmp(image+Data::PackRva,Data::Original.data(),Data::PackSize)==0);
        count=0;memcpy(image+Data::ManagerRva+0xe0,&count,8);
        for(const auto& signature:Data::Signatures) {
            image[signature.rva]^=1; assert(Apply()==Result::Unsupported); image[signature.rva]^=1;
            assert(memcmp(image+Data::PackRva,Data::Original.data(),Data::PackSize)==0);
        }
        assert(Install() && Installed() && Install());
        assert(memcmp(image+Data::PackRva,Data::Replacement.data(),Data::PackSize)==0);
        for(const auto& signature:Data::Signatures) for(size_t i=0;i<signature.size;++i)
            if(signature.rva+i<Data::PackRva || signature.rva+i>=Data::PackRva+Data::PackSize)
                assert(image[signature.rva+i]==*reinterpret_cast<const uint8_t*>(exe.At(signature.rva+i)));
        image[Data::PackRva]^=1;Uninstall();assert(Installed());image[Data::PackRva]^=1;
        Uninstall();assert(!Installed() && Supported([](uint32_t rva){return Pattern::Address(rva);}));
        testExecutable=0;VirtualFree(image,0,MEM_RELEASE);
        std::cout<<"Full native loader/base/tier hashes verified; altered builds, late installation, apply and restore checks passed.\n";
        if(argc>2) {
            const std::string mod=argv[2];auto root=std::filesystem::path(argv[1])/"mods"/mod/(mod+".mpq")/"data/hd/items";
            size_t four=0,collisions=0;
            std::map<uint32_t,std::string> full,legacy;
            std::map<std::string,std::string> expected;
            for(const char* file:{"items.json","sets.json","uniques.json"}) {
                std::ifstream input(root/file);assert(input);auto json=nlohmann::json::parse(input);assert(json.is_array());
                std::cout<<file<<": "<<json.size()<<" entries read without modifying mod files.\n";
                if(std::string(file)!="items.json") continue;
                for(const auto& entry:json) if(entry.is_object() && entry.size()==1) {
                    auto it=entry.begin();if(it.key().empty() || it.key().size()>4 || !it.value().contains("asset")) continue;
                    auto asset=it.value()["asset"].get<std::string>();
                    if(it.key().size()==4) ++four;
                    auto legacyKey=original.call(it.key().c_str());if(legacy.count(legacyKey)) ++collisions;
                    legacy[legacyKey]=asset;full[extended.call(it.key().c_str())]=asset;expected[it.key()]=asset;
                }
            }
            for(const auto& [code,asset]:expected) assert(full.at(extended.call(code.c_str()))==asset);
            assert(four>0);std::cout<<four<<" four-character base mappings verified; "<<collisions<<" former three-character collisions remain distinct.\n";
        }
    }
    std::cout<<"Four-character HD item-code checks passed.\n";
}
