#include "../d2rhud/RuntimeDiagnostics.h"
#include "../d2rhud/Pattern.h"
#include "../d2rhud/RuntimeTelemetry.h"
#include "../d2rhud/HitchDiagnostics.h"
#include "../d2rhud/plugin/D2RHUD/HudConfigStore.h"
#include "../d2rhud/plugin/TooltipText.h"
#include "../d2rhud/SessionEpoch.h"
#include <imgui.h>
#include <fstream>
#include <cassert>
#include <iostream>
#include <thread>
namespace Inspect { const char* SelectionStatus(){return "Fixture hover ready";} const char* WorldStatus(){return "Fixture eyes ready";} }
DWORD64 Pattern::Address(uint32_t){return 0;}
std::string Read(const std::filesystem::path& path){std::ifstream in(path,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
int main(){
    const auto root=std::filesystem::temp_directory_path()/("hud-diagnostics-"+std::to_string(GetCurrentProcessId()));
    assert(!std::filesystem::exists(root));std::filesystem::create_directories(root);
    std::ofstream(root/"diagnostics.json")<<"{\"PerformanceCounters\":false,\"unknown\":42}";
    RuntimeDiagnostics::Initialize(GetModuleHandleW(nullptr),"FixtureMod","test-version",root);
    const auto identity=std::string(RuntimeDiagnostics::IdentityText());assert(identity.find("Game SHA256: ")!=identity.npos && identity.find("HUD version: test-version")!=identity.npos && identity.find("Mod: FixtureMod")!=identity.npos);
    assert(!HudTelemetry::Enabled());assert(RuntimeDiagnostics::SetPerformanceEnabled(true) && HudTelemetry::Enabled());
    std::ofstream(root/"preferences.json")<<"{broken";assert(!RuntimeDiagnostics::SetPerformanceEnabled(false) && HudTelemetry::Enabled() && Read(root/"preferences.json")=="{broken");
    std::ofstream(root/"preferences.json")<<"{\"schema\":1,\"sections\":{\"Diagnostics\":{\"unknown\":42}}}";assert(RuntimeDiagnostics::SetPerformanceEnabled(false) && !HudTelemetry::Enabled());
    HudConfig::Json prefs;assert(HudUserConfig::Read("Diagnostics",prefs,root) && prefs["unknown"]==42 && prefs["PerformanceCounters"]==false);
    RuntimeDiagnostics::SetStage("Fixture stage");
    const auto context=root/"fault-context.txt";auto file=CreateFileW(context.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);assert(file!=INVALID_HANDLE_VALUE);
    {HudTelemetry::FeatureScope feature(HudTelemetry::Feature::LootFilter);RuntimeDiagnostics::WriteFaultContext(file);}CloseHandle(file);
    const auto fault=Read(context);assert(fault.find("Active feature on faulting thread: Loot filter")!=fault.npos && fault.find("Fixture stage")!=fault.npos && fault.find('\0')==fault.npos);
    std::ofstream(root/"feature-status.txt")<<"Feature fixture ready\n";std::ofstream(root/"crash.log")<<std::string(256*1024,'x');
    std::ofstream(root/"spectate.log")<<"Spectate transport fixture\n";
    const auto report=RuntimeDiagnostics::SaveSupportReport(),other=RuntimeDiagnostics::SaveSupportReport();assert(report!=other);
    const auto content=Read(report);assert(content.find("Feature fixture ready")!=content.npos && content.find("final 128 KiB")!=content.npos && content.size()<160*1024);
    assert(content.find("Spectate transport fixture")!=content.npos);
    assert(content.find("Fixture hover ready")!=content.npos && content.find("Fixture eyes ready")!=content.npos);
    const auto epoch=HudSession::Observe(true,123,false,111);TooltipText::ResetSession(epoch);
    const std::string full(2000,'A'),prefix(1023,'A');TooltipText::Remember(42,full,epoch);assert(TooltipText::Expand(42,prefix)==full);
    const auto next=HudSession::Observe(true,123,false,222);TooltipText::ResetSession(next);assert(TooltipText::Expand(42,prefix)==prefix);
    TooltipText::Remember(42,full,epoch);assert(TooltipText::Expand(42,prefix)==prefix);
    TooltipText::Remember(42,full,next);assert(TooltipText::Expand(42,prefix)==full);
    assert(HudHitches::Start({50,2},root/L"Support",identity));HudHitches::SetContext(1,false,true,false,11,1);
    {HudHitches::Present frame(reinterpret_cast<void*>(1),false,GetForegroundWindow());frame.BeforeDriver();}Sleep(65);
    {HudHitches::Present frame(reinterpret_cast<void*>(1),false,GetForegroundWindow());frame.BeforeDriver();}
    const auto menuCapture=HudHitches::Snapshot();assert(menuCapture.summary.frames==0&&menuCapture.summary.allHitches==1&&menuCapture.graphCount==1);
    ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={1280,720};io.Fonts->AddFontDefault();assert(io.Fonts->Build());
    for(int i=0;i<2;++i){ImGui::NewFrame();ImGui::Begin("Diagnostics");RuntimeDiagnostics::DrawPanel();ImGui::End();ImGui::Render();}
    assert(ImGui::GetDrawData()->TotalVtxCount>0);ImGui::DestroyContext();
    HudHitches::Stop();for(int i=0;i<200&&HudHitches::Snapshot().saving;++i)Sleep(25);assert(!HudHitches::Snapshot().report.empty());
    std::cout<<"PASS: cached version/hash/mod identity; attributed fault text; bounded unique support reports; atomic preference preservation/refusal; tooltip epoch reuse/stale results; real Diagnostics panel. Reports: "<<root<<'\n';
}
