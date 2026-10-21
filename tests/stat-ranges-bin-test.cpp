// Exercise the stat-range reader without a running game or archive access.
#include "../d2rhud/plugin/StatRangesData.cpp"
#include <cassert>
#include <fstream>
#include <iostream>
#include <filesystem>

static std::filesystem::path fixture;
static std::map<std::string,bool> binary;
static bool metadata=false;
static std::string damaged;
static bool missingStrings=false;
static bool staleStats=false;
struct GameAssets::MetadataCache {};
GameAssets::GameAssets(const std::filesystem::path&,const std::string&) {}
GameAssets::~GameAssets() {}
static std::vector<uint8_t> File(const std::filesystem::path& path){std::ifstream f(path,std::ios::binary|std::ios::ate);if(!f)return {};std::vector<uint8_t> out(size_t(f.tellg()));f.seekg(0);f.read(reinterpret_cast<char*>(out.data()),out.size());return out;}
std::vector<uint8_t> GameAssets::Read(const std::string& relative){return missingStrings?std::vector<uint8_t>{}:File(fixture.parent_path().parent_path()/relative);}
GameAssets::TableFiles GameAssets::ReadTable(const std::string& name,bool companion){TableFiles out;if(binary[name]){out.binary=File(fixture/(name+".bin"));if(name==damaged&&!out.binary.empty())out.binary.pop_back();}else{out.text=File(fixture/(name+".txt"));if(companion)out.binary=File(fixture/(name+".bin"));}if(staleStats && name=="itemstatcost"){const std::string stale="Stat\t*ID\nwrong_0\t0\nwrong_1\t1\nwrong_2\t2\n";out.text.assign(stale.begin(),stale.end());}return out;}
std::vector<std::string> GameAssets::ReadTableNames(const std::string& name,bool){
    if(!metadata)return {};const auto rows=StatRanges::Binary::Text(File(fixture/(name+".txt")));std::vector<std::string> out;
    const auto key=name=="itemstatcost"?"stat":name=="properties"||name=="itemtypes"?"code":name=="skilldesc"?"skilldesc":name=="skills"?"skill":"index";
    for(const auto& row:rows){const auto it=row.find(key);out.push_back(it==row.end()?std::string{}:it->second);}return out;
}
int main(int argc,char** argv){
    using namespace StatRanges;assert(argc==2);fixture=argv[1];
    const std::vector<std::string> tables{"itemstatcost","properties","magicsuffix","magicprefix","automagic","qualityitems","charstats","weapons","armor","misc","itemtypes","runes","gems","uniqueitems","setitems","sets","skilldesc","skills"};
    GameAssets assets({},{});auto text=LoadDatabase(assets,false);assert(text.error.empty());
    for(const auto& name:tables)binary[name]=true;
    auto bins=LoadDatabase(assets);if(!bins.error.empty())std::cerr<<bins.error<<'\n';assert(bins.error.empty());
    auto records=[&](const char* table){const auto bytes=File(fixture/(std::string(table)+".bin"));assert(bytes.size()>=4);uint32_t n=0;memcpy(&n,bytes.data(),4);return n;};
    assert(bins.stats.size()==records("itemstatcost") && bins.uniques.size()==records("uniqueitems") && bins.sets.size()==records("setitems"));
    assert(bins.affixes.size()==records("magicsuffix")+records("magicprefix")+records("automagic"));
    assert(bins.diagnostics.find("itemstatcost.bin")!=std::string::npos && bins.diagnostics.find("Localized stat descriptions:")!=std::string::npos);
    staleStats=true;auto recovered=LoadDatabase(assets);staleStats=false;
    assert(recovered.error.empty() && recovered.stats.size()==bins.stats.size());
    assert(recovered.diagnostics.find("Using compiled BIN")!=std::string::npos);
    for(const auto& name:tables)binary[name]=false;
    const auto paired=LoadDatabase(assets);assert(paired.error.empty() && paired.stats.size()==bins.stats.size());
    for(const auto& name:tables)binary[name]=true;
    missingStrings=true;auto noStrings=LoadDatabase(assets);missingStrings=false;
    assert(noStrings.error.find("No matching localized stat descriptions")!=std::string::npos);
    assert(noStrings.diagnostics.find("missing or invalid JSON array")!=std::string::npos);
    Item axe;axe.quality=7;axe.index=10;auto values=bins.ForItem(axe);
    assert(values==text.ForItem(axe));assert(MatchLine(bins,values,"+100% Enhanced Damage",true).text=="[75-150]");
    assert(MatchLine(bins,values,"+3% to Experience Gained",true).text=="[2-5]");
    assert(MatchLine(bins,values,"+38 poison damage over 3 seconds",true).text=="[38-38]");
    Item bow;bow.quality=7;bow.index=157;values=bins.ForItem(bow);assert(values==text.ForItem(bow));
    assert(MatchLine(bins,values,"+202% Enhanced Damage",true).text=="[180-220]");
    assert(MatchLine(bins,values,"Cold Resist +35%",true).text=="[35-35]");
    assert(MatchLine(bins,values,"-13% to Enemy Cold Resistance",true).text=="[10-20]");
    Totals gem;bins.Apply(gem,bins.gems.at("gcv")[1]);assert((gem.at({0,0})==Bounds{3,3}));
    // Compare every native-ID item range present in the paired source data.
    size_t matches=0,differences=0;
    for(int quality:{3,5,7})for(const auto& [id,props]:bins.uniques){
        if(quality==3&&id>=int(bins.superior.size()))continue;if(quality==5&&id>=int(bins.sets.size()))continue;
        Item item;item.quality=quality;item.index=id;
        if(bins.ForItem(item)==text.ForItem(item))++matches;else{if(differences<8)std::cerr<<"Paired source differs at quality "<<quality<<" ID "<<id<<'\n';++differences;}
    }
    assert(matches>800);std::cout<<"BIN-only item ranges: "<<matches<<" paired-source matches; "<<differences<<" source/BIN differences.\n";
    // Every single-table fallback and a mixed set use checksum-bound names.
    metadata=true;
    for(const auto& table:tables){for(const auto& name:tables)binary[name]=false;binary[table]=true;auto mixed=LoadDatabase(assets,false);if(!mixed.error.empty())std::cerr<<table<<": "<<mixed.error<<'\n';assert(mixed.error.empty());assert(mixed.ForItem(axe)==text.ForItem(axe));assert(mixed.ForItem(bow)==text.ForItem(bow));}
    for(size_t n=0;n<tables.size();++n)binary[tables[n]]=n%2==0;auto mixed=LoadDatabase(assets,false);assert(mixed.error.empty()&&mixed.ForItem(axe)==text.ForItem(axe));
    for(const auto& name:tables)binary[name]=true;
    for(const auto& table:tables){damaged=table;const auto broken=LoadDatabase(assets);assert(!broken.error.empty());}damaged.clear();
    std::cout<<"Stat-range BIN decoders, localized ranges, unique/set/superior records, gem properties and mixed TXT/BIN references passed.\n";
}
