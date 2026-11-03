#include "../d2rhud/plugin/MarketplacePagePackets.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <filesystem>
int wmain(int argc,wchar_t** argv){
    assert(argc==2);std::ifstream f(std::filesystem::path(argv[1]),std::ios::binary);
    std::vector<uint8_t> source{std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};assert(source.size()>=7*68);
    auto packets=Marketplace::PagePackets::Build(source);assert((packets.front()==std::vector<uint8_t>{0x6c,1,1,0}));
    std::vector<std::vector<uint8_t>> headers;
    for(const auto& p:packets){assert(p.size()>=4&&p.size()<=258&&p[0]==0x6c&&p[1]+3==p.size()&&p[2]<=1);if(p[2])headers.emplace_back();assert(!headers.empty());headers.back().insert(headers.back().end(),p.begin()+3,p.end());}
    assert(headers.size()==8&&headers[0]==std::vector<uint8_t>{0});std::vector<uint8_t> roundtrip;for(size_t i=1;i<headers.size();++i)roundtrip.insert(roundtrip.end(),headers[i].begin(),headers[i].end());assert(roundtrip==source);
    auto refused=[](const std::vector<uint8_t>& b){try{(void)Marketplace::PagePackets::Build(b);return false;}catch(...){return true;}};
    auto bad=source;bad.resize(bad.size()-1);assert(refused(bad));bad=source;bad.push_back(0);assert(refused(bad));bad=source;bad[8]=97;assert(refused(bad));bad=source;bad[16]=1;bad[17]=bad[18]=bad[19]=0;assert(refused(bad));bad=source;bad[64]=0;assert(refused(bad));
    std::cout<<"Fresh stash packets: ignored character-header slot, seven exact native tab headers, 255-byte fragments, preserved item/socket bytes and complete preflight refusal passed.\n";
}
