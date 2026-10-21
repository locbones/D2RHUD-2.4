#define NOMINMAX
#include "../d2rhud/plugin/GameAssets.h"
#include "../d2rhud/plugin/TableMetadata.h"
#include "../d2rhud/plugin/QuestJournalData.h"
#include "../d2rhud/plugin/D2RHUD/TerrorZoneTables.h"
#include <Windows.h>
#include <cassert>
#include <fstream>
#include <iostream>
namespace fs=std::filesystem;
std::string Text(const fs::path& path){std::ifstream file(path,std::ios::binary);return {std::istreambuf_iterator<char>(file),{}};}
void Write(const fs::path& path,const std::string& text){fs::create_directories(path.parent_path());std::ofstream(path,std::ios::binary)<<text;}
int main(int argc,char** argv){
    assert(argc==2);const fs::path data=argv[1];const auto source=data/"global/excel";
    const auto root=fs::temp_directory_path()/("d2rhud-table-metadata-"+std::to_string(GetCurrentProcessId()));
    const auto mod=root/"Mods/OnlyBin/OnlyBin.mpq/data",excel=mod/"global/excel";
    fs::create_directories(excel);fs::create_directories(mod/"D2RHUD");
    for(const char* name:{"monstats","superuniques","treasureclassex","itemstatcost","properties","states","cubemain"})fs::copy_file(source/(std::string(name)+".bin"),excel/(std::string(name)+".bin"),fs::copy_options::overwrite_existing);
    const auto original=Text(data/"D2RHUD/table-metadata.json");Write(mod/"D2RHUD/table-metadata.json",original);
    GameAssets bins(root,"OnlyBin");
    const auto stats=bins.ReadTableNames("itemstatcost"),props=bins.ReadTableNames("properties"),tcs=bins.ReadTableNames("treasureclassex");
    assert(stats.size()==487 && stats[292]=="RB_Low" && props[264]=="RB-Low");
    assert(bins.ReadTableSource("cubemain")==Text(source/"cubemain.txt"));
    const auto questConfig=Text(data/"D2RHUD/quests.json");
    const auto textRules=QuestJournal::Data::ParseRules(Text(source/"cubemain.txt"),questConfig);
    const auto binRules=QuestJournal::Data::ParseRules(bins.ReadTableSource("cubemain",true),questConfig);
    assert(textRules->error.empty()&&binRules->error.empty()&&binRules->quests.size()==45&&binRules->digest==textRules->digest);
    for(size_t n=0;n<45;++n)assert(binRules->quests[n].recipes==textRules->quests[n].recipes);
    // Leftover source must not override the recipes actually compiled for a
    // normal (non -txt) launch, or rebind a journal to an unrelated revision.
    Write(excel/"cubemain.txt","description\tenabled\nwrong_recipe\t1\n");
    GameAssets staleCubeText(root,"OnlyBin");
    const auto compiledRules=QuestJournal::Data::ParseRules(staleCubeText.ReadTableSource("cubemain",true),questConfig);
    assert(compiledRules->error.empty()&&compiledRules->digest==binRules->digest);
    fs::remove(excel/"cubemain.txt");
    // A player's leftover source contained only three usable stat IDs. Compiled
    // consumers must use bound BIN symbols, never those unrelated source names.
    Write(excel/"itemstatcost.txt","Stat\t*ID\nwrong_0\t0\nwrong_1\t1\nwrong_2\t2\n");
    GameAssets staleText(root,"OnlyBin");
    assert(staleText.ReadTableNames("itemstatcost").size()==3);
    assert(staleText.ReadTableNames("itemstatcost",true)==stats);
    fs::remove(excel/"itemstatcost.txt");
    std::map<std::string,int> indices;for(size_t i=0;i<tcs.size();++i)if(!tcs[i].empty())indices.emplace(tcs[i],int(i));
    assert(indices.at("Act 1 H2H A")==567); // Engine-generated TC prefix is 225 rows, not a TXT header offset.
    const auto resolver=[&](const std::string& name){const auto it=indices.find(name);return it==indices.end()?-1:it->second;};
    const auto nativeCount=[&](const char* name){const auto bytes=Text(source/(std::string(name)+".bin"));assert(bytes.size()>=4);uint32_t count=0;memcpy(&count,bytes.data(),4);return count;};
    const auto reference=TerrorZoneTables::Load(bins,resolver);assert(reference.valid && reference.monsters.size()==nativeCount("monstats") && reference.superuniques.size()==nativeCount("superuniques"));
    size_t comparisons=0;
    for(unsigned mask=0;mask<4;++mask){
        for(size_t i=0;i<2;++i){const auto name=i?"superuniques":"monstats";const auto target=excel/(std::string(name)+".txt");if(mask&(1u<<i))fs::copy_file(source/(std::string(name)+".txt"),target,fs::copy_options::overwrite_existing);else fs::remove(target);}
        GameAssets assets(root,"OnlyBin");const auto loaded=TerrorZoneTables::Load(assets,resolver);assert(loaded.valid);
        for(const bool unique:{false,true})for(const auto& [id,row]:unique?reference.superuniques:reference.monsters){
            const auto& other=(unique?loaded.superuniques:loaded.monsters).at(id);
            if(row.base!=other.base || row.terror!=other.terror){std::cerr<<"Mismatch mask="<<mask<<" unique="<<unique<<" id="<<id<<"\n";for(int d=0;d<3;++d)for(int v=0;v<3;++v)std::cerr<<d<<","<<v<<" base "<<row.base[d][v]<<"/"<<other.base[d][v]<<" terror "<<row.terror[d][v]<<"/"<<other.terror[d][v]<<"\n";assert(false);}++comparisons;
        }
    }
    for(const char* name:{"monstats","superuniques"})fs::remove(excel/(std::string(name)+".txt"));
    auto changed=nlohmann::json::parse(original);changed["tables"]["monstats"]["bin_hash"]="bad";Write(mod/"D2RHUD/table-metadata.json",changed.dump());
    GameAssets stale(root,"OnlyBin");assert(stale.ReadTableOverlay("monstats").empty());const auto ordinary=TerrorZoneTables::Load(stale,resolver);
    assert(ordinary.valid && TerrorZoneTables::Adjust(ordinary,0,-1,0,0,567)==567);
    // Higher-priority mod BIN metadata can never borrow loose-install TXT.
    Write(root/"data/global/excel/itemstatcost.txt","Stat\nwrong_stat\n");
    changed["tables"]["itemstatcost"]["bin_hash"]="bad";Write(mod/"D2RHUD/table-metadata.json",changed.dump());
    GameAssets noCrossLayer(root,"OnlyBin");assert(noCrossLayer.ReadTableNames("itemstatcost").empty());
    changed=nlohmann::json::parse(original);changed["tables"]["properties"]["names"].push_back("out_of_range");Write(mod/"D2RHUD/table-metadata.json",changed.dump());
    GameAssets extraName(root,"OnlyBin");assert(extraName.ReadTableNames("properties").empty());
    Write(mod/"D2RHUD/table-metadata.json","{ malformed");GameAssets malformed(root,"OnlyBin");assert(malformed.ReadTableNames("properties").empty());
    Write(root/"data/global/excel/transmog_table.txt","wrong_install_mapping");
    Write(mod/"D2RHUD/transmog_table.json",R"({"version":1,"source":"correct_mod_mapping"})");
    GameAssets auxiliary(root,"OnlyBin");assert(auxiliary.ReadAuxiliaryTable("transmog_table")=="correct_mod_mapping");
    TerrorZoneTables::Database synthetic;synthetic.valid=true;synthetic.monsters[7].base[2][2]=10;synthetic.monsters[7].terror[2][2]=25;
    assert(TerrorZoneTables::Adjust(synthetic,7,-1,2,2,12)==27);
    assert(TerrorZoneTables::Adjust(synthetic,7,-1,2,2,0)==0);
    assert(TerrorZoneTables::Adjust(synthetic,7,-1,-1,2,12)==12);
    std::cout<<"BIN-only metadata retains exact quest source, resolves stat/property/TC names; "<<comparisons<<" monster/SU records agree across TXT/BIN combinations; stale/malformed/cross-layer metadata safely rejected.\n";
}
