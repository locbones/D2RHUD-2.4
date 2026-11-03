#include "../d2rhud/plugin/MarketplaceItemVisuals.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
using namespace Marketplace::ItemVisuals;
namespace {
using Bytes=std::vector<uint8_t>;
Bytes Table(size_t stride,unsigned count){Bytes result(4+stride*count);std::memcpy(result.data(),&count,4);return result;}
void Text(Bytes& bytes,size_t at,const std::string& value){std::copy(value.begin(),value.end(),bytes.begin()+at);}
template<class F>bool Throws(F action){try{action();return false;}catch(const std::runtime_error&){return true;}}
Bytes ReadFile(const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);return {std::istreambuf_iterator<char>(file),{}};}
}
int main(int argc,char** argv){
    assert(Detail::Key("Buriza-Do Kyanon")=="buriza_do_kyanon");
    assert(Detail::Key("Trang-Oul's Guise")=="trang_ouls_guise");
    assert(Detail::Key("KhalimFlail")=="khalim_flail");
    assert(Detail::Key("Magefist1")=="magefist_1");
    assert(Detail::Key("Sword (2)  ")=="sword__2");
    std::map<std::string,Bytes> tables,assets;
    tables["itemtypes"]=Table(0xe4,2);tables["itemtypes"][4+0xe4+0x22]=3;
    tables["weapons"]=Table(0x1b4,3);tables["armor"]=Table(0x1b4,0);tables["misc"]=Table(0x1b4,1);
    for(unsigned i=0;i<3;++i){const size_t at=4+i*0x1b4;const std::array<std::string,3> codes{"swd","9wd","7wd"};
        Text(tables["weapons"],at+0x80,codes[i]);for(unsigned n=0;n<3;++n)Text(tables["weapons"],at+0x84+n*4,codes[n]);
        tables["weapons"][at+0x116]=1;tables["weapons"][at+0x117]=3;
    }
    Text(tables["misc"],4+0x80,"rin");tables["misc"][4+0x116]=tables["misc"][4+0x117]=1;tables["misc"][4+0x126]=1;
    tables["uniqueitems"]=Table(0x150,2);Text(tables["uniqueitems"],6,"Hero's Sword");Text(tables["uniqueitems"],4+0x28,"swd");
    Text(tables["uniqueitems"],4+0x150+2,"Exceptional Sword");Text(tables["uniqueitems"],4+0x150+0x28,"9wd");
    tables["setitems"]=Table(0x1bc,1);Text(tables["setitems"],6,"Hero's Band");Text(tables["setitems"],4+0x28,"rin");
    auto json=[&](const char* file,const char* text){assets[std::string("hd/items/")+file+".json"]=Bytes(text,text+std::strlen(text));};
    json("items",R"([{"swd":{"asset":"sword/short_sword"}},{"9wd":{"asset":"sword/short_sword"}},{"7wd":{"asset":"sword/short_sword"}},{"rin":{"asset":"ring/ring"}}])");
    json("uniques",R"([{"heros_sword":{"normal":"sword/hero","uber":"sword/hero_uber","ultra":"sword/hero_ultra"}},{"exceptional_sword":{"normal":"sword/original_exceptional","ultra":"sword/upgraded_exceptional"}}])");
    json("sets",R"([{"heros_band":{"normal":"..//custom//hero_band"}}])");
    auto tableRead=[&](const std::string& key){return tables.at(key);};auto assetRead=[&](const std::string& key){return assets.at(key);};
    const auto catalog=Catalog::Load(tableRead,assetRead);assert(catalog.Size()==4);
    const auto base=catalog.Resolve("swd",2,-1,0);assert(base.filename=="items/weapon/sword/short_sword"&&base.width==1&&base.height==3&&base.exact);
    assert(catalog.Resolve("swd",7,0,0).filename=="items/weapon/sword/hero");
    assert(catalog.Resolve("9wd",7,0,0).filename=="items/weapon/sword/hero_uber");
    assert(catalog.Resolve("7wd",7,0,0).filename=="items/weapon/sword/hero_ultra");
    assert(catalog.Resolve("9wd",7,1,0).filename=="items/weapon/sword/original_exceptional");
    assert(catalog.Resolve("7wd",7,1,0).filename=="items/weapon/sword/upgraded_exceptional");
    assert(!catalog.Resolve("swd",7,1,0)); // Wrong named identity, not a base-image substitution.
    assert(catalog.ResolveInventory("swd",7,1,0).filename==base.filename && !catalog.ResolveInventory("swd",7,1,0).exact);
    assert(catalog.ResolveInventory("rin",5,9999,2).filename=="items/misc/ring/ring3");
    assert(catalog.ResolveInventory("swd",7,0,0).filename=="items/weapon/sword/hero");
    assert(!catalog.ResolveInventory("unknown",7,0,0) && !catalog.ResolveInventory("rin",7,0,8));

    assert(catalog.Resolve("rin",2,-1,0).filename=="items/misc/ring/ring1");
    assert(catalog.Resolve("rin",2,-1,2).filename=="items/misc/ring/ring3");
    assert(catalog.Resolve("rin",2,-1,7).filename=="items/misc/ring/ring3");
    assert(!catalog.Resolve("rin").exact&&catalog.Resolve("rin").filename=="items/misc/ring/ring1");
    assert(catalog.Resolve("rin",5,0,2).filename=="items/custom/hero_band"); // Named sprite never gets random suffix.
    // Generic jewellery mappings also occur in named-item HD mappings.
    json("sets",R"([{"heros_band":{"normal":"ring/ring"}}])");
    assert(Catalog::Load(tableRead,assetRead).Resolve("rin",5,0,2).filename=="items/misc/ring/ring3");
    json("sets",R"([{"heros_band":{"normal":"..//custom//hero_band"}}])");
    assert(!catalog.Resolve("unknown")&&!catalog.Resolve("rin",5,9,0)&&!catalog.Resolve("rin",2,-1,8));
    assert(Detail::Filename("../../escape","misc").empty());
    assert(Detail::Filename("C:/escape","misc").empty());
    assert(Detail::Filename("/escape","misc").empty());
    assert(Detail::Filename("foo.sprite","misc").empty());
    auto broken=tables;broken["weapons"].pop_back();assert(Throws([&]{Catalog::Load([&](const std::string& key){return broken.at(key);},assetRead);}));
    const auto saved=assets;json("items",R"([{"rin":{"asset":"ring/ring"}},{"rin":{"asset":"ring/other"}}])");assert(Throws([&]{Catalog::Load(tableRead,assetRead);}));assets=saved;
    json("items",R"([{"rin":{"asset":"ring/ring"}},{"rin":{"asset":"ring/ring"}}])");assert(Catalog::Load(tableRead,assetRead).Resolve("rin",2,-1,0).filename=="items/misc/ring/ring1");assets=saved;
    json("sets",R"([{"heros_band":{"normal":"../../../bad"}}])");assert(!Catalog::Load(tableRead,assetRead).Resolve("rin",5,0,0));assets=saved;
    tables["itemtypes"][4+0xe4+0x22]=8;assert(Catalog::Load(tableRead,assetRead).Resolve("rin",2,-1,7).filename=="items/misc/ring/ring8");tables["itemtypes"][4+0xe4+0x22]=3;
    if(argc>1)try{const std::filesystem::path data=argv[1];const auto real=Catalog::Load([&](const std::string& key){return ReadFile(data/"global"/"excel"/(key+".bin"));},[&](const std::string& key){return ReadFile(data/key);});
        std::cerr<<"Real records: "<<real.Size()<<"; paths: "<<real.Resolve("r33",2,-1,0).filename<<", "<<real.Resolve("rin",2,-1,1).filename<<", "<<real.Resolve("hax",7,6,0).filename<<", "<<real.Resolve("lrg",5,0,0).filename<<", "<<real.Resolve("Ev11",2,-1,0).filename<<"\n";
        assert(real.Size()>1000);const auto zod=real.Resolve("r33",2,-1,0);assert(zod.filename=="items/misc/rune/zod_rune"&&zod.width==1&&zod.height==1);
        const auto ring=real.Resolve("rin",2,-1,1);assert(ring.filename=="items/misc/ring/ring2");
        const auto gnasher=real.Resolve("hax",7,6,0);assert(gnasher.filename=="items/weapon/axe/the_gnasher");
        const auto ward=real.Resolve("lrg",5,0,0);assert(ward.filename=="items/armor/shield/stormguild");
        const auto custom=real.Resolve("Ev11",2,-1,0);assert(custom.filename=="items/custom/candycorn");
        assert(!real.Resolve("B03",7,670,0));
        assert(real.ResolveInventory("B03",7,670,0).filename=="items/custom/BarrierCharm3");

        std::cout<<"Real mod artwork metadata: "<<real.Size()<<" base records; Zod, ring variant, unique, set, custom paths verified.\n";
    }catch(const std::exception& error){std::cerr<<"Real mod metadata failure: "<<error.what()<<"\n";return 1;}
    std::cout<<"Marketplace item artwork metadata tests passed.\n";
}
