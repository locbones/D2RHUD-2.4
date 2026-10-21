#include "StackSplitSkin.h"
#include "GameAssets.h"
#include "QuestJournal.h"
#include "../Pattern.h"
#include <Windows.h>
#include <atomic>
#include <future>
#include <map>
#include <chrono>
#include "../D2/json.hpp"

namespace LegacyTransmog::Skin {
namespace {
std::atomic<std::shared_ptr<const Collection>> assets;
std::filesystem::path install;std::string modName;
std::map<std::string,std::string> itemAssets;
std::future<std::shared_ptr<const Sprite>> itemLoading;
std::shared_ptr<const Sprite> itemSprite;
std::string pendingCode,loadedCode;
}
std::shared_ptr<const Collection> Assets(){return assets.load();}
std::shared_ptr<const Sprite> ItemSprite(const std::string& code){
    if(itemLoading.valid()){
        if(itemLoading.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return {};
        try{itemSprite=itemLoading.get();}catch(...){itemSprite.reset();}
        loadedCode=pendingCode;
    }
    if(code==loadedCode)return itemSprite;
    if(code.empty() || install.empty())return {};
    const auto found=itemAssets.find(code);
    if(found==itemAssets.end()){loadedCode=code;itemSprite.reset();return {};}
    pendingCode=code;const auto paths=IconPaths(found->second);
    itemLoading=std::async(std::launch::async,[paths,folder=install,mod=modName]()->std::shared_ptr<const Sprite>{
        GameAssets source(folder,mod);
        for(const auto& path:paths){auto sprite=std::make_shared<Sprite>();if(Decode(source.Read(path),*sprite))return sprite;}
        return {};
    });
    return {};
}
void Load(){
    try{
        wchar_t exe[32768]{};char mod[128]{};SIZE_T read=0;
        if(!GetModuleFileNameW(nullptr,exe,DWORD(std::size(exe))))return;
        if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(Pattern::Address(0x1bf084f)),mod,sizeof(mod),&read) || read!=sizeof(mod))mod[0]=0;
        mod[sizeof(mod)-1]=0;std::string name=mod;
        if(name.find_first_of("/\\:")!=name.npos || name=="." || name=="..")return;
        GameAssets source(std::filesystem::path(exe).parent_path(),name);auto result=std::make_shared<Collection>();
        install=std::filesystem::path(exe).parent_path();modName=name;
        const auto bytes=source.Read("hd/items/items.json");
        const auto mappings=nlohmann::json::parse(bytes.begin(),bytes.end(),nullptr,false,true);
        if(mappings.is_array())for(const auto& entry:mappings)if(entry.is_object())for(auto it=entry.begin();it!=entry.end();++it){
            if(it.value().is_object() && it.value().contains("asset") && it.value()["asset"].is_string())itemAssets[it.key()]=it.value()["asset"].get<std::string>();
        }
        constexpr const char* paths[]{"hd/global/ui/panel/modals/modal_bg.sprite","hd/global/ui/panel/modals/gold_drop_bar.sprite",
            "hd/global/ui/panel/modals/modal_up_arrow.sprite","hd/global/ui/panel/modals/modal_down_arrow.sprite",
            "hd/global/ui/panel/modals/modalbutton.sprite","ReMoDDeD/Panels/Buttons/Exit.sprite",
            "ReMoDDeD/Panels/Quest/QuestBG_R.sprite","ReMoDDeD/Panels/Quest/QuestTab_R.sprite","ReMoDDeD/Panels/Buttons/ButtonMed_R.sprite"};
        for(int part=0;part<Count;++part)if(part<JournalBackground||QuestJournal::SupportsMod(name))Decode(source.Read(paths[part]),(*result)[part]);
        assets.store(std::move(result));
    }catch(...){OutputDebugStringA("D2RHUD: quantity UI artwork could not be loaded.\n");}
}
}
