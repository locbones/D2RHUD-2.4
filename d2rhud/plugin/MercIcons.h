#pragma once
#include <filesystem>
#include <string>

namespace MercIcons {
void Load(const std::filesystem::path& install, const std::string& mod);
bool Install();
void Uninstall();
void SetEnabled(bool enabled);
bool Available();
const char* Status();
} // namespace MercIcons
