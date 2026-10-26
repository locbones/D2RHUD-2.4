#pragma once
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <cstdint>
#include "../d2/json.hpp"

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
        if (!cached_ || path_ != path) {
            path_ = path;
            cached_ = true;
            readOnly = false;
            document_ = Json{{"version", 1}, {"profiles", Json::object()}};
            std::error_code error;
            const bool exists = std::filesystem::exists(path_, error);
            if (error || path_.empty()) readOnly = true;
            else if (exists) {
                try {
                    std::ifstream input(path_, std::ios::binary);
                    if (!input) readOnly = true;
                    else {
                        Json loaded;
                        input >> loaded;
                        input >> std::ws;
                        if (!input.eof() || !ValidDocument(loaded)) readOnly = true;
                        else document_ = std::move(loaded);
                    }
                } catch (...) { readOnly = true; }
            }
        }
        if (readOnly) return false;
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
            const auto contents = updated.dump(2) + "\n";
            std::error_code error;
            if (!path_.parent_path().empty()) std::filesystem::create_directories(path_.parent_path(), error);
            if (error) return false;
            auto temporary = path_;
            temporary += L"." + std::to_wstring(GetCurrentProcessId()) + L"." +
                std::to_wstring(reinterpret_cast<uintptr_t>(this)) + L".tmp";
            {
                std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
                output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
                output.flush();
                output.close();
                if (!output) { std::filesystem::remove(temporary, error); return false; }
            }
            if (!MoveFileExW(temporary.c_str(), path_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                std::filesystem::remove(temporary, error);
                return false;
            }
            document_ = std::move(updated);
            return true;
        } catch (...) { return false; }
    }
};
}
