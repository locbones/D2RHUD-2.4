#define NOMINMAX
#include "../d2rhud/plugin/ItemFilter/CatalogTables.h"
#include <fstream>
#include <iostream>
#include <cassert>
using namespace LootFilterConfig::Builder;
using namespace LootFilterConfig::Builder::Tables;
std::vector<uint8_t> Read(const std::filesystem::path& path){std::ifstream f(path,std::ios::binary|std::ios::ate);assert(f);std::vector<uint8_t> bytes(size_t(f.tellg()));f.seekg(0);assert(f.read(reinterpret_cast<char*>(bytes.data()),bytes.size()));return bytes;}
int main(int argc,char** argv){
    assert(argc==2);const std::filesystem::path root=argv[1];Catalog bin,txt;
    for(const char* table:{"itemtypes","weapons","armor","misc","itemstatcost","levels","uniqueitems","setitems"}){
        const auto bytes=Read(root/(std::string(table)+".bin"));AddBinary(bin,table,bytes);
        const auto source=Read(root/(std::string(table)+".txt"));AddTable(txt,table,std::string(source.begin(),source.end()));
    }
    assert(bin.items.size()>1000&&bin.types.size()>100&&bin.stats.size()>350&&bin.areas.size()>100&&bin.namedItems.size()>500);
    size_t checked=0,sourceOnlyChanges=0;
    for(const auto& [code,base]:bin.bases){auto other=txt.bases.find(code);if(other==txt.bases.end()){++sourceOnlyChanges;continue;}
        assert(base.level==other->second.level&&base.sockets==other->second.sockets&&base.tier==other->second.tier&&base.types==other->second.types);++checked;
    }
    assert(checked>1000&&sourceOnlyChanges<=2);
    assert(bin.typeIds==txt.typeIds&&bin.typeParents==txt.typeParents);
    for(const auto& entry:bin.namedItems){auto other=std::find_if(txt.namedItems.begin(),txt.namedItems.end(),[&](const auto& e){return e.index==entry.index&&e.quality==entry.quality;});assert(other!=txt.namedItems.end()&&other->code==entry.code);++checked;}
    // Names stripped from native stats remain usable numeric conditions, while
    // validated author metadata restores readable custom labels.
    const auto costs=Read(root/"itemstatcost.bin");Catalog named;
    std::vector<std::string> names(Binary(costs,0x144).count);names[39]="custom_fire_resist";
    AddBinary(named,"itemstatcost",costs,names);assert(named.stats[39].label=="custom fire resist"&&named.stats[39].value=="39");
    assert(bin.stats[39].label=="Stat 39"&&bin.stats[39].value=="39");
    // A mixed TXT/BIN type/base catalog must use the same numeric ancestry.
    Catalog mixed;const auto types=Read(root/"itemtypes.txt");AddTable(mixed,"itemtypes",std::string(types.begin(),types.end()));
    for(const char* table:{"weapons","armor","misc"})AddBinary(mixed,table,Read(root/(std::string(table)+".bin")));
    assert(mixed.bases.size()==bin.bases.size());for(const auto& [code,base]:mixed.bases)assert(base.types==bin.bases.at(code).types);
    auto malformed=costs;malformed.pop_back();bool refused=false;try{Catalog bad;AddBinary(bad,"itemstatcost",malformed);}catch(const std::runtime_error&){refused=true;}assert(refused);
    std::cout<<"PASS: "<<checked<<" actual compiled base/named records agree with TXT numeric IDs, types, levels, sockets and tiers; "<<sourceOnlyChanges<<" source-only item-code edits excluded. Numeric/metadata names, mixed formats and truncated-BIN refusal passed.\n";
}


