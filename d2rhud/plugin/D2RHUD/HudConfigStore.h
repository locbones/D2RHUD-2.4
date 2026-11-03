#pragma once
#include "../../d2/json.hpp"
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

namespace HudConfig {
using Json = nlohmann::ordered_json;
struct Result {
    bool ok = false;
    std::string error;
    bool changed = false;
    explicit operator bool() const { return ok; }
};

// Comments and trailing commas are accepted outside JSON strings only.
Result Parse(std::string_view source, Json& document);
Result Read(const std::filesystem::path& path, Json& document);
std::string Serialize(const Json& document);

// Read/modify/replace is serialized within the HUD. Unknown fields survive edits.
// Invalid or unreadable existing files are never replaced with defaults.
Result Update(const std::filesystem::path& path, const std::function<void(Json&)>& edit);
}

// Global user preferences share one document; mod gameplay preferences remain
// in HUDConfig_<mod>.json. Each edit touches only its feature's section.
namespace HudUserConfig {
std::filesystem::path Directory();
std::filesystem::path Path(const std::filesystem::path& directory = {});
// A successful legacy fallback can carry a warning in error while migration is
// deferred. Its settings remain usable; Update refuses until migration succeeds.
HudConfig::Result Read(const char* section,HudConfig::Json& value,const std::filesystem::path& directory = {});
HudConfig::Result Update(const char* section,const std::function<void(HudConfig::Json&)>& edit,const std::filesystem::path& directory = {});
}
