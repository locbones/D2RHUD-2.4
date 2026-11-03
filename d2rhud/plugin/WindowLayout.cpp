#include "WindowLayout.h"
#include "D2RHUD/HudConfigStore.h"
#include <windows.h>
#include <fstream>
#include <iterator>
#include <string>

namespace HudWindowLayout {
namespace {
std::filesystem::path layoutPath;
std::string savedLayout;
bool shared=false;

bool Save() {
    if(layoutPath.empty()||!ImGui::GetCurrentContext())return false;
    size_t size=0;
    const char* data=ImGui::SaveIniSettingsToMemory(&size);
    const std::string layout(data,size);
    if(layout==savedLayout)return true;
    if(shared){
        const auto saved=HudUserConfig::Update("WindowLayout",[&](HudConfig::Json& section){section["ini"]=layout;},layoutPath.parent_path());
        if(!saved)return false;savedLayout=layout;return true;
    }
    std::error_code error;
    std::filesystem::create_directories(layoutPath.parent_path(),error);
    if(error)return false;
    auto temporary=layoutPath;
    temporary+=L"."+std::to_wstring(GetCurrentProcessId())+L".tmp";
    {
        std::ofstream output(temporary,std::ios::binary|std::ios::trunc);
        output.write(layout.data(),static_cast<std::streamsize>(layout.size()));
        output.flush();
        if(!output)return false;
    }
    // A crash during saving must not destroy the last working layout.
    if(!MoveFileExW(temporary.c_str(),layoutPath.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))return false;
    savedLayout=layout;
    return true;
}
}

void Initialize(const std::filesystem::path& path) {
    auto& io=ImGui::GetIO();
    io.IniFilename=nullptr; // Use wide Windows paths, including Unicode user names.
    io.ConfigWindowsResizeFromEdges=true;
    layoutPath=path;shared=path.empty();
    if(layoutPath.empty()) {
        wchar_t localAppData[32768];
        const auto length=GetEnvironmentVariableW(L"LOCALAPPDATA",localAppData,32768);
        if(length&&length<32768)layoutPath=std::filesystem::path(localAppData)/L"D2RHUD"/L"preferences.json";
    }
    savedLayout.clear();
    if(!layoutPath.empty()) {
        if(shared){HudConfig::Json section;if(HudUserConfig::Read("WindowLayout",section,layoutPath.parent_path())&&section.contains("ini")&&section["ini"].is_string())savedLayout=section["ini"].get<std::string>();}
        else {std::ifstream input(layoutPath,std::ios::binary);if(input)savedLayout.assign(std::istreambuf_iterator<char>(input),{});}
        if(!savedLayout.empty())ImGui::LoadIniSettingsFromMemory(savedLayout.data(),savedLayout.size());
    }
}

void Tick() {
    auto& io=ImGui::GetIO();
    // Save at the end of a drag as well as ImGui's usual delayed save, so the
    // player's final adjustment survives closing the game immediately afterward.
    if(io.WantSaveIniSettings||ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        if(Save())io.WantSaveIniSettings=false;
    }
}

void Shutdown() {
    Save();
    layoutPath.clear();
    savedLayout.clear();
}
}
