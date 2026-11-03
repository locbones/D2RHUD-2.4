#include "RunTrackerAnalysis.h"
#include "GameAssets.h"
#include "ItemFilter/CatalogTables.h"
#include <fstream>

namespace RunTracker::Analysis {
namespace {
std::string Fold(std::string text){for(auto& c:text)c=char(std::tolower(static_cast<unsigned char>(c)));return text;}
std::string Category(const LootFilterConfig::Builder::Catalog& catalog,const LootFilterConfig::Builder::BaseItem& item){
    std::set<std::string> types;std::vector<std::string> todo=item.types;
    while(!todo.empty()&&types.size()<128){auto code=todo.back();todo.pop_back();if(!types.insert(code).second)continue;if(const auto at=catalog.typeParents.find(code);at!=catalog.typeParents.end())todo.insert(todo.end(),at->second.begin(),at->second.end());}
    auto has=[&](const char* code){return types.contains(code);};
    if(has("rune"))return "Runes";if(has("gem"))return "Gems";for(const auto& type:types)if(type.starts_with("gem"))return "Gems";
    if(has("jewl"))return "Jewels";if(has("char")||has("scha")||has("mcha")||has("lcha"))return "Charms";
    if(has("ring"))return "Rings";if(has("amul"))return "Amulets";
    if(has("poti")||has("hpot")||has("mpot")||has("rpot"))return "Potions";
    if(has("weap"))return "Weapons";if(has("armo"))return "Armor";
    if(has("key"))return "Keys";if(has("book")||has("scro"))return "Books and scrolls";
    if(has("ques"))return "Quest items";
    if(!item.types.empty()){
        const auto type=item.types.front();const auto id=catalog.typeIds.find(type);
        if(id!=catalog.typeIds.end())for(const auto& label:catalog.types)if(label.value==std::to_string(id->second))return label.label;
        return "Other: "+type;
    }return "Other";
}
void TreasureSource(Sources& out,const std::string& text){
    std::istringstream input(text);std::string line;if(!std::getline(input,line))return;
    auto header=LootFilterConfig::Builder::Columns(line);std::map<std::string,size_t> columns;
    for(size_t i=0;i<header.size();++i){if(i==0&&header[i].starts_with("\xef\xbb\xbf"))header[i].erase(0,3);columns[Fold(header[i])]=i;}
    if(!columns.contains("treasure class")||!columns.contains("picks"))return;
    while(std::getline(input,line)){
        const auto cells=LootFilterConfig::Builder::Columns(line);auto get=[&](const std::string& key){const auto at=columns.find(Fold(key));return at!=columns.end()&&at->second<cells.size()?cells[at->second]:std::string{};};
        const auto name=get("Treasure Class");if(name.empty()||name=="Expansion"||out.treasures.size()>=20000)continue;
        try{Treasure tc;tc.picks=get("Picks").empty()?1:std::stoi(get("Picks"));tc.noDrop=get("NoDrop").empty()?0:std::stod(get("NoDrop"));
            if(!std::isfinite(tc.noDrop)||tc.noDrop<0)continue;
            for(int n=1;n<=10;++n){auto item=get("Item"+std::to_string(n));const auto number=get("Prob"+std::to_string(n));if(item.empty()||number.empty())continue;const auto weight=std::stod(number);if(!std::isfinite(weight)||weight<=0)continue;
                if(item.size()>1&&item.front()=='"'&&item.back()=='"')item=item.substr(1,item.size()-2);
                tc.edges.push_back({item,weight});
            }out.treasures.emplace(name,std::move(tc));
        }catch(...){/* A malformed optional table row cannot affect gameplay. */}
    }
}
}
Sources Load(const std::filesystem::path& install,const std::string& mod,const std::filesystem::path& marketFolder,bool hardcore,int64_t now){
    Sources out;out.mod=mod;out.hardcore=hardcore;out.loadedUtc=now;
    try{GameAssets assets(install,mod);const auto catalog=LootFilterConfig::Builder::Tables::Load(assets);
        for(const auto& [code,item]:catalog.bases)out.bases[code]={Category(catalog,item),item.types.empty()?"":item.types.front()};
        for(const auto& item:catalog.namedItems)out.namedCodes[item.label]=item.code;
        out.catalogStatus=out.bases.empty()?"Item type tables are unavailable.":"Item types loaded from the selected mod's TXT/BIN tables.";
        TreasureSource(out,assets.ReadTableSource("treasureclassex"));
        out.treasureStatus=out.treasures.empty()?"Treasure-class source/compatible metadata is unavailable. Rarity comparison is disabled.":"One weighted selection per treasure-class node; unsupported paths are withheld.";
    }catch(const std::exception& e){out.catalogStatus=e.what();out.treasureStatus="Treasure-class data is unavailable.";}
    try{
        const auto path=marketFolder/(hardcore?L"MarketplaceActivity_2.4_HC.json":L"MarketplaceActivity_2.4_SC.json");
        if(marketFolder.empty()||!std::filesystem::exists(path)){out.marketStatus="No completed-sale cache. Sync the marketplace companion to populate comparisons.";return out;}
        std::ifstream input(path,std::ios::binary|std::ios::ate);const auto size=input.tellg();if(size<0||size>16*1024*1024)throw std::runtime_error("Marketplace history cache exceeds its size limit.");
        std::string text(size_t(size),'\0');input.seekg(0);if(!input.read(text.data(),size))throw std::runtime_error("Marketplace history cache could not be read.");
        ReadMarket(out,Json::parse(text),mod,hardcore,now);
    }catch(const std::exception& e){out.sales.clear();out.currencies.clear();out.marketStatus=e.what();}return out;
}
}
