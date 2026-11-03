#pragma once
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <cstdint>
#include <map>
#include "../d2/json.hpp"
#include "D2RHUD/HudConfigStore.h"

namespace HUDControls {
inline constexpr uint16_t Unbound = 0xffff;
struct Keys {
    uint16_t primary = 0x7b;
    uint16_t secondary = Unbound;
    bool operator==(const Keys&) const = default;
};

class Store {
    using Json = nlohmann::json;
    std::filesystem::path path_;
    std::string scope_;
    Json document_;
    std::map<std::string,Keys> unsaved_;
    bool cached_ = false;

    static bool ValidKey(const Json& value) {
        if (!value.is_number_integer()) return false;
        if (value.is_number_unsigned()) {
            const auto key = value.get<uint64_t>();
            return key <= 0x7fff || key == Unbound;
        }
        const auto key = value.get<int64_t>();
        return (key >= 0 && key <= 0x7fff) || key == Unbound;
    }
    static bool ValidKeys(const Json& value) {
        return value.is_object() && value.contains("primary") && value.contains("secondary") &&
            ValidKey(value["primary"]) && ValidKey(value["secondary"]);
    }
    static bool ValidDocument(const Json& value) {
        if (!value.is_object() || !value.contains("version") || !value["version"].is_number_integer() ||
            value["version"] != 1 || !value.contains("profiles") || !value["profiles"].is_object()) return false;
        for (const auto& profile : value["profiles"].items()) if (!ValidKeys(profile.value())) return false;
        return true;
    }
public:
    Keys saved, editing;
    bool readOnly = false;

    bool Load(const std::filesystem::path& path, const std::string& scope, Keys fallback = {}) {
        saved = editing = fallback;
        scope_ = scope;
        if(path_!=path)unsaved_.clear();
        {
            path_ = path;
            cached_ = true;
            readOnly = false;
            document_ = Json{{"version", 1}, {"profiles", Json::object()}};
            if(path_.empty())readOnly=true;
            else try{HudConfig::Json section;
                if(!HudUserConfig::Read("Controls",section,path_.parent_path()))readOnly=true;
                else if(!section.empty()){Json loaded=section;if(!ValidDocument(loaded))readOnly=true;else document_=std::move(loaded);}
            }catch(...){readOnly=true;}
        }
        if (readOnly) return false;
        for(const auto& [name,keys]:unsaved_){auto& profile=document_["profiles"][name];if(profile.is_null())profile=Json::object();profile["primary"]=keys.primary;profile["secondary"]=keys.secondary;}
        const auto& profiles = document_["profiles"];
        const auto profile = profiles.find(scope_);
        if (profile != profiles.end())
            saved = editing = Keys{(*profile)["primary"].get<uint16_t>(), (*profile)["secondary"].get<uint16_t>()};
        return true;
    }

    void Apply() {
        saved = editing;
        if (!cached_ || readOnly || !ValidKey(Json(saved.primary)) || !ValidKey(Json(saved.secondary))) return;
        auto& profile = document_["profiles"][scope_];
        if (profile.is_null()) profile = Json::object();
        profile["primary"] = saved.primary;
        profile["secondary"] = saved.secondary;
        unsaved_[scope_]=saved;
    }
    void Revert() { editing = saved; }
    void Defaults() { editing = Keys{}; }

    bool Save() {
        if (!cached_ || readOnly || path_.empty() ||
            !ValidKey(Json(saved.primary)) || !ValidKey(Json(saved.secondary))) return false;
        try {
            auto updated = document_;
            auto& profile = updated["profiles"][scope_];
            if (profile.is_null()) profile = Json::object();
            profile["primary"] = saved.primary;
            profile["secondary"] = saved.secondary;
            const auto result=HudUserConfig::Update("Controls",[&](HudConfig::Json& section){
                if(section.empty())section={{"version",1},{"profiles",HudConfig::Json::object()}};
                if(!ValidDocument(Json(section)))throw std::runtime_error("Invalid native Controls preference section.");
                auto pending=unsaved_;pending[scope_]=saved;
                for(const auto& [name,keys]:pending){auto& profile=section["profiles"][name];if(profile.is_null())profile=HudConfig::Json::object();profile["primary"]=keys.primary;profile["secondary"]=keys.secondary;}updated=section;
            },path_.parent_path());
            if(!result)return false;document_=std::move(updated);unsaved_.clear();
            return true;
        } catch (...) { return false; }
    }
};
}
