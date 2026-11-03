#define NOMINMAX
#include "../d2rhud/plugin/D2RHUD/HudConfigStore.h"
#include "../d2rhud/plugin/HUDControlsStore.h"
#include "../d2rhud/plugin/WindowLayout.h"
#include <Windows.h>
#include <imgui_internal.h>
#include <cassert>
#include <fstream>
#include <iostream>
#include <thread>
#include <cwctype>
using Json=HudConfig::Json;
std::string Bytes(const std::filesystem::path& path){std::ifstream f(path,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
void Write(const std::filesystem::path& path,const std::string& bytes){std::filesystem::create_directories(path.parent_path());std::ofstream f(path,std::ios::binary);f<<bytes;assert(f.good());}
int main(int argc,char** argv){
    if(argc==3&&std::string_view(argv[1])=="--migrate"){
        for(const char* section:{"RunTracker","Language","Controls","Diagnostics","WindowLayout"}){Json value;const auto result=HudUserConfig::Read(section,value,std::filesystem::path(argv[2]));if(!result||!result.error.empty()){std::cerr<<result.error<<'\n';return 1;}}
        std::cout<<"User preferences migrated and readable.\n";return 0;
    }
    assert(argc==2);const auto root=std::filesystem::path(argv[1])/("preferences-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
    const auto profile=root/L"profile",directory=profile/L"D2RHUD";SetEnvironmentVariableW(L"LOCALAPPDATA",profile.c_str());
    assert(HudUserConfig::Directory()==directory&&HudUserConfig::Path()==directory/L"preferences.json");
    const std::string layout="[Window][Test Window]\nPos=83,91\nSize=540,360\nCollapsed=0\n\n";
    const std::vector<std::pair<std::filesystem::path,std::string>> old{
        {L"language.json","{\"language\":\"ruRU\",\"future\":9}"},
        {L"diagnostics.json","{\"PerformanceCounters\":true}"},
        {L"RunTracker/settings.json","{\"freezeTown\":false,\"overlayScale\":1.4,\"enabled\":true}"},
        {L"controls.json","{\"version\":1,\"profiles\":{\"RMD-MP|Hero\":{\"primary\":8257,\"secondary\":65535}}}"},
        {L"ui-layout.ini",layout}
    };
    for(const auto& [file,bytes]:old)Write(directory/file,bytes);
    Write(directory/L"RunTracker/runs/keep.json","run record");Write(directory/L"Quests/keep.json","quest data");
    Json section;assert(HudUserConfig::Read("RunTracker",section)&&section["freezeTown"]==false&&section["enabled"]==true);
    Json archive;assert(HudConfig::Read(directory/L"preferences-migration-backup.json",archive));
    for(const auto& [file,bytes]:old){assert(!std::filesystem::exists(directory/file));bool found=false;for(const auto& entry:archive["legacy"])found|=entry["bytes"]==bytes;assert(found);}
    assert(Bytes(directory/L"RunTracker/runs/keep.json")=="run record"&&Bytes(directory/L"Quests/keep.json")=="quest data");
    assert(HudUserConfig::Read("Language",section)&&section["language"]=="ruRU"&&section["future"]==9);
    const auto shared=HudUserConfig::Path();assert(HudUserConfig::Update("Language",[](Json& j){j["language"]="frFR";}));
    assert(HudUserConfig::Update("Diagnostics",[](Json& j){j["PerformanceCounters"]=false;}));
    assert(HudUserConfig::Read("RunTracker",section)&&section["enabled"]==true);
    assert(HudUserConfig::Read("Language",section)&&section["future"]==9&&section["language"]=="frFR");
    // A leftover old copy cannot override a section that is already authoritative.
    Write(directory/L"language.json",old[0].second);assert(HudUserConfig::Read("Language",section)&&section["language"]=="frFR"&&!std::filesystem::exists(directory/L"language.json"));

    HUDControls::Store controls;assert(controls.Load(directory/L"controls.json","RMD-MP|Hero")&&controls.saved.primary==8257);
    controls.editing={0x107b,HUDControls::Unbound};controls.Apply();assert(controls.Save());
    assert(HudUserConfig::Read("Language",section)&&section["language"]=="frFR");
    HUDControls::Store concurrent;assert(concurrent.Load(directory/L"controls.json","Other|Hero"));concurrent.editing={0x41,0x42};concurrent.Apply();assert(concurrent.Save());
    controls.editing={0x107c,HUDControls::Unbound};controls.Apply();assert(controls.Save());assert(concurrent.Load(directory/L"controls.json","Other|Hero")&&concurrent.saved.primary==0x41);

    ImGui::CreateContext();auto& io=ImGui::GetIO();io.DisplaySize={1280,720};io.Fonts->AddFontDefault();assert(io.Fonts->Build());
    HudWindowLayout::Initialize();assert(std::string(ImGui::SaveIniSettingsToMemory()).find("Pos=83,91")!=std::string::npos);
    for(int n=0;n<2;++n){ImGui::NewFrame();ImGui::SetNextWindowPos({120,140},ImGuiCond_Always);ImGui::SetNextWindowSize({560,380});ImGui::Begin("Test Window");ImGui::End();ImGui::Render();}
    HudWindowLayout::Shutdown();ImGui::DestroyContext();assert(HudUserConfig::Read("WindowLayout",section)&&section["ini"].get<std::string>().find("Pos=120,140")!=std::string::npos);
    assert(HudUserConfig::Read("Controls",section)&&section["profiles"]["RMD-MP|Hero"]["primary"]==0x107c);

    const auto valid=Bytes(shared);const auto held=CreateFileW(shared.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);assert(held!=INVALID_HANDLE_VALUE);
    assert(!HudUserConfig::Update("Language",[](Json& j){j["language"]="deDE";})&&Bytes(shared)==valid);CloseHandle(held);
    Write(shared,"{broken");assert(!HudUserConfig::Read("Language",section)&&!HudUserConfig::Update("Language",[](Json& j){j["language"]="deDE";})&&Bytes(shared)=="{broken");
    Write(shared,"{\"schema\":999,\"sections\":{}}");assert(!HudUserConfig::Update("Language",[](Json&){})&&Bytes(shared).find("999")!=std::string::npos);Write(shared,valid);
    assert(HudConfig::Update(shared,[](Json& j){j["sections"]["DamagedFeature"]=false;j["unknownRoot"]="keep";}));
    assert(!HudUserConfig::Read("DamagedFeature",section)&&HudUserConfig::Update("Language",[](Json& j){j["language"]="jaJP";}));
    Json doc;assert(HudConfig::Read(shared,doc)&&doc["unknownRoot"]=="keep"&&doc["sections"]["DamagedFeature"]==false);
    // An invalid legacy file affects only its own missing section and is kept.
    const auto broken=root/L"broken-migration";Write(broken/L"language.json","{broken");Write(broken/L"diagnostics.json","{\"PerformanceCounters\":true}");
    assert(HudUserConfig::Read("Diagnostics",section,broken)&&section["PerformanceCounters"]==true);assert(!HudUserConfig::Update("Language",[](Json&){},broken)&&Bytes(broken/L"language.json")=="{broken");
    // An interrupted/blocked migration never removes its only source copy.
    const auto blocked=root/L"blocked-migration";Write(blocked/L"language.json",old[0].second);Write(blocked/L"preferences-migration-backup.json","{broken");
    const auto deferred=HudUserConfig::Read("Language",section,blocked);assert(deferred&&!deferred.error.empty()&&section["language"]=="ruRU"&&Bytes(blocked/L"language.json")==old[0].second);
    assert(!HudUserConfig::Update("Language",[](Json& j){j["language"]="frFR";},blocked)&&!std::filesystem::exists(blocked/L"preferences.json"));
    const auto heldLegacy=root/L"held-legacy";Write(heldLegacy/L"language.json",old[0].second);
    const auto legacyHandle=CreateFileW((heldLegacy/L"language.json").c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);assert(legacyHandle!=INVALID_HANDLE_VALUE);
    assert(HudUserConfig::Read("Language",section,heldLegacy)&&section["language"]=="ruRU"&&std::filesystem::exists(heldLegacy/L"language.json"));CloseHandle(legacyHandle);
    assert(HudUserConfig::Read("Language",section,heldLegacy)&&!std::filesystem::exists(heldLegacy/L"language.json"));
    const auto encoded=root/L"bad-encoding";Write(encoded/L"ui-layout.ini",std::string(1,char(255)));Write(encoded/L"diagnostics.json","{\"PerformanceCounters\":true}");assert(HudUserConfig::Read("Diagnostics",section,encoded)&&section["PerformanceCounters"]==true&&!HudUserConfig::Read("WindowLayout",section,encoded));
    // A different process can hold this named writer lease without leaving a
    // lock file. The attempted edit fails without waiting on the game thread.
    uint64_t lockKey=14695981039346656037ULL;for(auto c:std::filesystem::absolute(shared).lexically_normal().wstring())lockKey=(lockKey^uint16_t(std::towlower(c)))*1099511628211ULL;
    const auto lease=CreateMutexW(nullptr,TRUE,(L"Local\\D2RHUD-Preferences-"+std::to_wstring(lockKey)).c_str());assert(lease);bool blockedWriter=false;
    std::thread writer([&]{blockedWriter=!HudUserConfig::Update("Language",[](Json& j){j["language"]="enUS";});});writer.join();assert(blockedWriter);ReleaseMutex(lease);CloseHandle(lease);
    // Independent sections are read/modified/replaced under the same writer lock.
    std::thread a([&]{for(int n=0;n<30;++n)assert(HudUserConfig::Update("A",[&](Json& j){j["value"]=n;}));});
    std::thread b([&]{for(int n=0;n<30;++n)assert(HudUserConfig::Update("B",[&](Json& j){j["value"]=n;}));});a.join();b.join();
    assert(HudUserConfig::Read("A",section)&&section["value"]==29&&HudUserConfig::Read("B",section)&&section["value"]==29);
    std::cout<<"PASS: five-file migration and exact recovery archive; settings isolation; Unicode-safe shared storage; Controls profiles/apply; native layout restore/save; malformed/future/locked-file refusal; damaged-section isolation; migration interruption; concurrent section writes. "<<directory<<'\n';
}
