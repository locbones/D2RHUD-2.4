#define NOMINMAX
#include "CustodyPlan.h"
#include "CharacterProfile.h"
#include <iostream>
int wmain(int argc,wchar_t** argv){try{
    using namespace Marketplace;using namespace Marketplace::Custody;
    if(argc!=4)throw std::runtime_error("Usage: custody-save.exe inventory MOD_DATA PAGE | plan MOD_DATA REQUEST_JSON");
    const auto tables=Load(argv[2]);const std::wstring command=argv[1];Json out;
    if(command==L"identity-catalog"){
        out={{"schema",1},{"game_mode","2.4"},{"codes",CandyCornSaveData::Detail::Codes},{"items",Json::object()}};
        for(const auto& d:tables.save.items){const auto s=CandyCornSaveData::Detail::Stat(tables.save,356);
            out["items"][std::string(d.code.data(),4)]={{"gold",d.gold},{"quest_bits",d.questDifficulty?(s?s->saveBits:0):0},{"charm",d.charm},{"body",d.bodyPart},{"tome",d.tome}};}
    }else if(command==L"inventory"){
        const auto bytes=ReadFile(argv[3]);const auto page=Read(bytes,tables);Occupancy(page,tables);Json items=Json::array();
        for(const auto& rows:page.tabs)for(const auto& item:rows){auto row=item.ToJson();row["legacy_item_hex"]=LegacyItemHex(item.itemHex,item.children);row["grid_width"]=Size(item,tables).first;row["grid_height"]=Size(item,tables).second;
            auto raw=Decode(item.itemHex);CandyCornSaveData::Detail::Budget budget;CandyCornSaveData::Detail::State state;state.bits={raw,0,&budget,true};const auto prefix=Prefix(state,tables);
            row["quantity_bit"]=(prefix.definition->stackable&&item.quality==2&&item.children.empty())?int64_t(state.bits.pos):-1;
            row["balances"]=Json::array();for(const auto& balance:item.balances)row["balances"].push_back({{"stat",balance.stat},{"quantity",balance.quantity}});items.push_back(std::move(row));}
        for(auto& row:items){row["credit_capacity"]=Json::array();if(row["code"]!="Z01"||row["quality"]!=2)continue;
            const auto item=ReadItem(Decode(row.at("item_hex").get<std::string>()),tables.save,row.at("tab").get<unsigned>());
            for(const auto& [symbol,id]:tables.save.bagStats)try{const auto amount=Balance(item,symbol);(void)Bag(item,symbol,amount,tables);row["credit_capacity"].push_back({{"stat",symbol},{"quantity",BagMaximum(tables,symbol)-amount}});}catch(const std::exception&){} }
        out={{"schema",1},{"game_mode","2.4"},{"mod",tables.mod},{"items",items},{"gold",Gold(page)},{"bag_stats",tables.save.bagStats.size()}};
    }else if(command==L"character")out=CharacterProfile::Read(ReadFile(argv[3],8*1024*1024),tables);
    else if(command==L"plan")out=Plan(Json::parse(ReadFile(argv[3],32*1024*1024)),tables);
    else throw std::runtime_error("Unknown custody operation.");
    std::cout<<out.dump()<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<"Custody simulation: "<<e.what()<<'\n';return 1;}}
