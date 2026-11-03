#pragma once
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace HudSounds {
using Files = std::vector<std::pair<std::string, std::string>>; // UTF-8 path, filename
std::string ExecutableDirectory();
void Configure(const std::string& executableDirectory, const std::string& mod);
std::shared_ptr<const Files> Catalog();
void Invalidate();
std::string Resolve(const std::string& preferred);
bool IsSupported(const std::filesystem::path& path);
bool PlayFile(const std::string& path);
void Beep();
struct ImportResult { size_t copied = 0; std::string error; };
ImportResult Import(const std::string& sourceDirectory);
}
