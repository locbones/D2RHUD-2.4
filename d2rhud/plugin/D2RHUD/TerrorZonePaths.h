#pragma once
#include <filesystem>
#include <string>

// Match the selected data root. A -direct/-txt launch without -mod reads the
// install's loose data, while named mods retain their own configuration.
inline std::filesystem::path TerrorZoneDataPath(const std::filesystem::path& install, const std::string& mod,
    const std::filesystem::path& asset)
{
    const auto relative = std::filesystem::path("data") / asset;
    if (mod.empty()) return install / relative;
    const auto root = install / "Mods" / mod;
    const auto mpq = root / (mod + ".mpq") / relative;
    const auto extracted = root / relative;
    std::error_code error;
    if (!std::filesystem::is_regular_file(mpq, error) && std::filesystem::is_regular_file(extracted, error)) return extracted;
    return mpq;
}

inline std::filesystem::path TerrorZoneConfigPath(const std::filesystem::path& install, const std::string& mod)
{
    return TerrorZoneDataPath(install, mod, "hd/global/excel/desecratedzones.json");
}
