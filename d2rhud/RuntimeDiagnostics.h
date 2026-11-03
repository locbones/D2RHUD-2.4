#pragma once
#include <Windows.h>
#include <filesystem>
#include <string>

namespace RuntimeDiagnostics {
void Initialize(HMODULE hud,const char* mod,const char* version,const std::filesystem::path& root={});
void SetStage(const char* stage); // Label must have static lifetime (fault path borrows it).
const char* IdentityText();
// The fault path writes only startup-cached identity and thread-local feature
// names. It performs no hashing, allocation or mutex acquisition.
void WriteFaultContext(HANDLE file);
bool SetPerformanceEnabled(bool enabled);
std::string PerformanceText();
std::filesystem::path SaveSupportReport();
void DrawPanel();
}
