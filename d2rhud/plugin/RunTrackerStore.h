#pragma once
#include "RunTrackerData.h"
#include "D2RHUD/HudConfigStore.h"
#include <filesystem>
#include <fstream>
#include <Windows.h>

namespace RunTracker {
struct Library {std::vector<Run> runs;size_t unreadable=0,older=0;std::string error;};
struct ClearResult {size_t removed=0,failed=0;std::string error;};
inline ClearResult ClearHistoryFiles(const std::filesystem::path& folder,bool exports){
    ClearResult result;
    // Only immediate files owned by the tracker are eligible. Never recurse,
    // follow a junction/symlink, or remove preferences or marketplace data.
    auto reparse=[](const std::filesystem::path& path){const auto attributes=GetFileAttributesW(path.c_str());return attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_REPARSE_POINT)!=0;};
    if(folder.empty()||reparse(folder)){result.failed=1;result.error="Run Tracker folder is unavailable or redirected.";return result;}
    auto clear=[&](const char* directory,const char* extension){
        const auto root=folder/directory;
        try{
            if(!std::filesystem::exists(root))return;
            if(reparse(root)||!std::filesystem::is_directory(root))throw std::runtime_error("The data folder is not a regular directory.");
            // Snapshot the names before deletion so directory iteration cannot
            // skip older records as files disappear.
            std::vector<std::filesystem::path> files;
            for(const auto& entry:std::filesystem::directory_iterator(root)){
                const auto filename=entry.path().filename().wstring();
                const bool temporary=std::string_view(extension)==".json"&&filename.find(L".json.tmp-")!=std::wstring::npos;
                if(entry.path().extension()!=extension&&!temporary)continue;
                if(reparse(entry.path())){++result.failed;continue;}
                if(entry.is_regular_file())files.push_back(entry.path());
            }
            for(const auto& path:files){std::error_code error;if(std::filesystem::remove(path,error))++result.removed;else if(error){++result.failed;result.error=error.message();}}
        }catch(const std::exception& e){++result.failed;result.error=e.what();}
    };
    clear("runs",".json");if(exports)clear("exports",".csv");return result;
}
inline Library LoadLibrary(const std::filesystem::path& folder,size_t limit){
    Library result;
    try{
        if(!std::filesystem::exists(folder))return result;
        std::vector<std::filesystem::path> files;
        for(const auto& file:std::filesystem::directory_iterator(folder))if(file.is_regular_file()&&file.path().extension()==L".json")files.push_back(file.path());
        std::sort(files.rbegin(),files.rend());result.older=files.size()>limit?files.size()-limit:0;if(files.size()>limit)files.resize(limit);
        for(const auto& path:files){
            try{if(std::filesystem::file_size(path)>64*1024*1024){++result.unreadable;continue;}
                HudConfig::Json j;Run run;if(!HudConfig::Read(path,j)||!Decode(j,run)||path.stem().string()!=run.id){++result.unreadable;continue;}
                // An interrupted snapshot remains visible and is never
                // rewritten merely because a different game opened it.
                if(!run.finished)run.endReason="Interrupted / recovered";
                result.runs.push_back(std::move(run));
            }catch(...){++result.unreadable;}
        }
    }catch(const std::exception& e){result.error=e.what();}return result;
}
inline HudConfig::Result SaveRun(const std::filesystem::path& folder,const Run& run){
    try{
        const auto encoded=Encode(run);Run verified;if(!Decode(encoded,verified))return {false,"Run record failed validation; previous file kept"};
        std::filesystem::create_directories(folder);
        return HudConfig::Update(folder/(run.id+".json"),[&](HudConfig::Json& j){
            if(!j.empty()&&j.value("schema",0)!=1)throw std::runtime_error("Unsupported run file version; existing file kept");
            for(const auto& [key,value]:encoded.items())j[key]=value;
        });
    }catch(const std::exception& e){return {false,e.what()};}
}
inline Settings DecodeSettings(const HudConfig::Json& j){
    Settings s;
    auto boolean=[&](const char* key,bool& value){if(j.contains(key)&&j[key].is_boolean())value=j[key].get<bool>();};
    boolean("enabled",s.enabled);boolean("autoStart",s.autoStart);boolean("freezeTown",s.freezeTown);boolean("freezeLoading",s.freezeLoading);boolean("overlay",s.overlay);boolean("milliseconds",s.milliseconds);boolean("overlayItems",s.overlayItems);boolean("overlayArea",s.overlayArea);boolean("overlayWall",s.overlayWall);boolean("overlayLocked",s.overlayLocked);
    boolean("analysisUnits",s.analysisUnits);boolean("analysisBroad",s.analysisBroad);boolean("analysisTown",s.analysisTown);
    if(j.contains("analysisDays")&&j["analysisDays"].is_number_integer())s.analysisDays=(std::clamp)(j["analysisDays"].get<int>(),1,365);
    if(j.contains("analysisSamples")&&j["analysisSamples"].is_number_integer())s.analysisSamples=(std::clamp)(j["analysisSamples"].get<int>(),1,100);
    if(j.contains("analysisCurrency")&&j["analysisCurrency"].is_string()&&j["analysisCurrency"].get_ref<const std::string&>().size()<=512)s.analysisCurrency=j["analysisCurrency"].get<std::string>();
    if(j.contains("analysisTreasure")&&j["analysisTreasure"].is_string()&&j["analysisTreasure"].get_ref<const std::string&>().size()<=256)s.analysisTreasure=j["analysisTreasure"].get<std::string>();
    if(j.contains("timeFormat")&&j["timeFormat"].is_number_integer())s.timeFormat=(std::clamp)(j["timeFormat"].get<int>(),0,2);
    if(j.contains("historyLimit")&&j["historyLimit"].is_number_integer())s.historyLimit=(std::clamp)(j["historyLimit"].get<int>(),100,10000);
    if(j.contains("overlayScale")&&j["overlayScale"].is_number())s.overlayScale=(std::clamp)(j["overlayScale"].get<float>(),.65f,2.f);
    if(j.contains("overlayOpacity")&&j["overlayOpacity"].is_number())s.overlayOpacity=(std::clamp)(j["overlayOpacity"].get<float>(),.1f,1.f);return s;
}
inline Json EncodeSettings(const Settings& s){return {{"enabled",s.enabled},{"autoStart",s.autoStart},{"freezeTown",s.freezeTown},{"freezeLoading",s.freezeLoading},{"overlay",s.overlay},{"milliseconds",s.milliseconds},{"overlayItems",s.overlayItems},{"overlayArea",s.overlayArea},{"overlayWall",s.overlayWall},{"overlayLocked",s.overlayLocked},{"timeFormat",s.timeFormat},{"historyLimit",s.historyLimit},{"overlayScale",s.overlayScale},{"overlayOpacity",s.overlayOpacity},{"analysisUnits",s.analysisUnits},{"analysisBroad",s.analysisBroad},{"analysisTown",s.analysisTown},{"analysisDays",s.analysisDays},{"analysisSamples",s.analysisSamples},{"analysisCurrency",s.analysisCurrency},{"analysisTreasure",s.analysisTreasure}};}
}
