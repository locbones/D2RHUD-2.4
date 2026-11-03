// Read-only JSON bridge for Pandemonium. Never edits saves or claims custody.
#define NOMINMAX
#include "../../d2rhud/plugin/MarketplaceStash.h"
#include "../../d2rhud/plugin/MarketplaceStore.h"
#include <iostream>
#include <chrono>

int wmain(int argc,wchar_t** argv) {
    try {
        if(argc!=4)throw std::runtime_error("Usage: stash-catalogue.exe <compiled-excel-directory> <stash.d2i> <settings.json>");
        using namespace Marketplace;
        const auto excel=std::filesystem::path(argv[1]),stash=std::filesystem::path(argv[2]),settingsPath=std::filesystem::path(argv[3]);
        const auto settingsBytes=ReadFile(settingsPath,16*1024*1024);
        const auto document=Json::parse(settingsBytes.begin(),settingsBytes.end());
        const auto mod=document.at("mod").get<std::string>();const auto hardcore=document.at("hardcore").get<bool>();
        const auto settings=ReadSettings(document,mod,hardcore);
        const auto expected=(hardcore?L"Stash_HC_Page":L"Stash_SC_Page")+std::to_wstring(settings.page)+L".d2i";
        if(stash.filename()!=expected)throw std::runtime_error("The stash filename does not match the configured trade page and character mode.");
        if(!settings.enabled)throw std::runtime_error("The trade page is not enabled.");
        const auto tables=LoadSaveTables([&](const std::string& name){return ReadFile(excel/(name+".bin"),64*1024*1024);});
        const auto before=ReadFile(stash);const auto items=ReadStash(before,tables);
        if(ReadFile(stash)!=before || ReadFile(settingsPath,16*1024*1024)!=settingsBytes)throw std::runtime_error("Inputs changed while scanning; retry after saving finishes.");
        Json output={{"schema",Schema},{"game_mode","2.4"},{"mod",mod},{"hardcore",hardcore},{"page",settings.page},
            {"captured_at",std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count()},
            {"custody","local_save"},{"purchasable",false},{"items",Json::array()},{"unmatched_drafts",settings.drafts.size()}};
        size_t matched=0;
        for(const auto& item:items){
            auto row=item.ToJson();row["price"]=nullptr;
            for(const auto& draft:settings.drafts)if(draft.tab==item.tab&&draft.itemHex==item.itemHex){row["price"]=PriceJson(draft.price);++matched;break;}
            output["items"].push_back(std::move(row));
        }
        output["unmatched_drafts"]=settings.drafts.size()-std::min(settings.drafts.size(),matched);
        std::cout<<output.dump(2)<<'\n';return 0;
    }catch(const std::exception& e){std::cerr<<"Marketplace catalogue: "<<e.what()<<'\n';return 1;}
}
