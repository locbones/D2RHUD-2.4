#pragma once
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace LootFilterLibrary {
namespace fs=std::filesystem;
inline std::string Lower(std::string text){for(auto& c:text)c=char(std::tolower(static_cast<unsigned char>(c)));return text;}
inline bool File(const fs::path& path){std::error_code error;return fs::is_regular_file(path,error);}
inline std::vector<fs::path> Directories(const fs::path& game,const std::string& mod){
    if(game.empty())return {};
    std::vector<fs::path> result{game/"My Filters"};
    if(!mod.empty())for(const auto& folder:{"Pandemonium/Filters","pandemonium/filters","D2RLAN/filters","d2rlan/filters"}){
        result.push_back(game/"Mods"/mod/(mod+".mpq")/"data"/folder);
        result.push_back(game/"Mods"/mod/"data"/folder);
    }
    return result;
}
inline fs::path Resolve(const std::vector<fs::path>& directories,const std::string& name){
    if(name.empty() || name=="." || name==".." || name.find_first_of("/\\")!=name.npos)return {};
    for(const auto& directory:directories){
        for(const auto& candidate:{directory/name/"lootfilter_config.lua",directory/(name+"_config.lua"),directory/(name+".lua")})
            if(File(candidate))return candidate;
        // Preserve the actual spelling of case-insensitive Lua extensions.
        std::error_code error;fs::directory_iterator it(directory,fs::directory_options::skip_permission_denied,error),end;
        while(!error && it!=end){const auto& entry=*it;
            if(entry.is_regular_file(error) && Lower(entry.path().extension().string())==".lua" && Lower(entry.path().stem().string())==Lower(name))return entry.path();
            it.increment(error);
        }
    }
    return {};
}
inline std::vector<std::string> Names(const std::vector<fs::path>& directories){
    std::map<std::string,std::string> names;
    for(const auto& directory:directories){
        std::error_code error;fs::directory_iterator it(directory,fs::directory_options::skip_permission_denied,error),end;
        while(!error && it!=end){const auto& entry=*it;const auto filename=entry.path().filename().string();
            if(!filename.empty() && filename.front()!='.'){
                std::string name;
                if(entry.is_directory(error) && Lower(filename)!="sounds" && File(entry.path()/"lootfilter_config.lua"))name=filename;
                else if(entry.is_regular_file(error) && Lower(entry.path().extension().string())==".lua")name=entry.path().stem().string();
                if(!name.empty() && Lower(name)!="last_active_lootfilter")names.try_emplace(Lower(name),name);
            }
            it.increment(error);
        }
    }
    std::vector<std::string> result;for(const auto& [key,name]:names)result.push_back(name);
    return result;
}
}
