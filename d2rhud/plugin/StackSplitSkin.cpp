#include "StackSplitSkin.h"
#include "ItemSpriteCache.h"
#include "GameAssets.h"
#include "QuestJournal.h"
#include "Inspect.h"
#include "../Pattern.h"
#include "../ProcessLifetime.h"
#include <Windows.h>
#include <atomic>
#include "../D2/json.hpp"

namespace LegacyTransmog::Skin {
namespace {
struct Pack { Collection panels; ItemSource items; };
struct Runtime {
    std::atomic<std::shared_ptr<const Pack>> pack;
    ItemCache items{{[](const ItemSource& source, const std::vector<std::string>& paths)->std::shared_ptr<const Sprite> {
        GameAssets assets(source.install, source.mod);
        for (const auto& path : paths) {
            auto sprite = std::make_shared<Sprite>();
            if (Decode(assets.Read(path), *sprite)) return sprite;
        }
        return {};
    }}};
};
Runtime& State() { return HudLifetime::State<Runtime>(); }
}
std::shared_ptr<const Collection> Assets(){
    const auto pack=State().pack.load();
    return pack?std::shared_ptr<const Collection>(pack,&pack->panels):nullptr;
}
std::shared_ptr<const Sprite> ItemSprite(const std::string& code){
    auto& state=State();const auto pack=state.pack.load();
    return state.items.Get(pack?std::shared_ptr<const ItemSource>(pack,&pack->items):nullptr,code);
}
void Load(){
    try{
        wchar_t exe[32768]{};char mod[128]{};SIZE_T read=0;
        if(!GetModuleFileNameW(nullptr,exe,DWORD(std::size(exe))))return;
        if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(Pattern::Address(0x1bf084f)),mod,sizeof(mod),&read) || read!=sizeof(mod))mod[0]=0;
        mod[sizeof(mod)-1]=0;std::string name=mod;
        if(name.find_first_of("/\\:")!=name.npos || name=="." || name=="..")return;
        GameAssets source(std::filesystem::path(exe).parent_path(),name);auto result=std::make_shared<Pack>();
        Inspect::ConfigureAssets(std::filesystem::path(exe).parent_path(),name);
        result->items.install=std::filesystem::path(exe).parent_path();result->items.mod=name;
        const auto bytes=source.Read("hd/items/items.json");
        const auto mappings=nlohmann::json::parse(bytes.begin(),bytes.end(),nullptr,false,true);
        if(mappings.is_array())for(const auto& entry:mappings)if(entry.is_object())for(auto it=entry.begin();it!=entry.end();++it){
            if(it.value().is_object() && it.value().contains("asset") && it.value()["asset"].is_string())result->items.mappings[it.key()]=it.value()["asset"].get<std::string>();
        }
        constexpr const char* paths[]{"hd/global/ui/panel/modals/modal_bg.sprite","hd/global/ui/panel/modals/gold_drop_bar.sprite",
            "hd/global/ui/panel/modals/modal_up_arrow.sprite","hd/global/ui/panel/modals/modal_down_arrow.sprite",
            "hd/global/ui/panel/modals/modalbutton.sprite","ReMoDDeD/Panels/Buttons/Exit.sprite",
            "ReMoDDeD/Panels/Quest/QuestBG_R.sprite","ReMoDDeD/Panels/Quest/QuestTab_R.sprite","ReMoDDeD/Panels/Buttons/ButtonMed_R.sprite"};
        for(int part=0;part<Count;++part)if(part<JournalBackground||QuestJournal::SupportsMod(name))Decode(source.Read(paths[part]),result->panels[part]);
        // Publish panels and mappings together only after all construction ends.
        State().pack.store(std::move(result));
    }catch(...){OutputDebugStringA("D2RHUD: quantity UI artwork could not be loaded.\n");}
}
}
