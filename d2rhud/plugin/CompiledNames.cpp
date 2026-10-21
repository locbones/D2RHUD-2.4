#include "CompiledNames.h"
#include "GameAssets.h"

namespace CompiledNames {
void Load(const std::filesystem::path& install,const std::string& mod) {
    auto result=std::make_shared<Database>();
    GameAssets assets(install,mod);
    for(const auto& [global,name]:std::map<uint32_t,std::string>{{0x1c9fb90,"itemstatcost"},{0x1c9ebd8,"properties"},{0x1c9ec08,"states"},{0x1c9f890,"monstats"},{0x1c9f6a8,"treasureclassex"},{0x1c9fc68,"uniqueitems"},{0x1c9fc48,"setitems"}}){
        auto names=assets.ReadTableNames(name);auto& target=(*result)[global];
        for(size_t i=0;i<names.size();++i)if(!names[i].empty())target.emplace(Fold(std::move(names[i])),int(i));
    }
    size_t count=0;for(const auto& [global,names]:*result)count+=names.size();
    status=count?"Table symbols cached for TXT/BIN features ("+std::to_string(count)+" names). BIN-only custom settings use data/D2RHUD/table-metadata.json.":
        "No source symbols cached. BIN-only custom settings require matching data/D2RHUD/table-metadata.json exported from the mod sources.";
    database.store(std::move(result));
}
}
