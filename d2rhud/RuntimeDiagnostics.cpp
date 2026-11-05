#include "RuntimeDiagnostics.h"
#include "RuntimeTelemetry.h"
#include "HitchDiagnostics.h"
#include "SessionEpoch.h"
#include "plugin/HudText.h"
#include "plugin/Inspect.h"
#include "plugin/D2RHUD/HudConfigStore.h"
#include "json.hpp"
#include <bcrypt.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <vector>
#pragma comment(lib,"bcrypt.lib")
#pragma comment(lib,"version.lib")
#pragma comment(lib,"shell32.lib")

namespace RuntimeDiagnostics {
namespace {
std::atomic<const char*> identity{"Identity collection has not completed.\n"},stage{"HUD initialization"};
std::filesystem::path folder;
std::string status;
std::string Utf8(const std::filesystem::path& path){const auto text=path.u8string();return {reinterpret_cast<const char*>(text.data()),text.size()};}
std::string Sha256(const std::filesystem::path& path){
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
    struct Cleanup {BCRYPT_ALG_HANDLE& a;BCRYPT_HASH_HANDLE& h;~Cleanup(){if(h)BCryptDestroyHash(h);if(a)BCryptCloseAlgorithmProvider(a,0);}} cleanup{algorithm,hash};
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0 || BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)<0)return "unavailable";
    std::ifstream input(path,std::ios::binary);if(!input)return "unavailable";
    std::array<char,65536> bytes{};
    while(input){input.read(bytes.data(),bytes.size());const auto count=input.gcount();if(count && BCryptHashData(hash,reinterpret_cast<PUCHAR>(bytes.data()),ULONG(count),0)<0)return "unavailable";}
    if(!input.eof())return "unavailable";
    std::array<unsigned char,32> digest{};if(BCryptFinishHash(hash,digest.data(),ULONG(digest.size()),0)<0)return "unavailable";
    std::ostringstream out;for(auto value:digest)out<<std::hex<<std::setfill('0')<<std::setw(2)<<unsigned(value);return out.str();
}
std::string FileVersion(const std::filesystem::path& path){
    DWORD ignored=0;const auto size=GetFileVersionInfoSizeW(path.c_str(),&ignored);if(!size || size>1024*1024)return "unavailable";
    std::vector<uint8_t> bytes(size);VS_FIXEDFILEINFO* value=nullptr;UINT length=0;
    if(!GetFileVersionInfoW(path.c_str(),0,size,bytes.data()) || !VerQueryValueW(bytes.data(),L"\\",reinterpret_cast<void**>(&value),&length) || length<sizeof(*value))return "unavailable";
    return std::to_string(HIWORD(value->dwFileVersionMS))+"."+std::to_string(LOWORD(value->dwFileVersionMS))+"."+std::to_string(HIWORD(value->dwFileVersionLS))+"."+std::to_string(LOWORD(value->dwFileVersionLS));
}
void AppendFile(std::ostream& out,const std::filesystem::path& path){
    out<<"\n--- "<<Utf8(path.filename())<<" ---\n";
    std::ifstream file(path,std::ios::binary|std::ios::ate);if(!file){out<<"Not available.\n";return;}
    const auto length=file.tellg();if(length<0){out<<"Could not read file.\n";return;}
    const auto size=static_cast<size_t>((std::min)(length,std::streampos(128*1024)));
    file.seekg(length-std::streamoff(size));std::string bytes(size,'\0');file.read(bytes.data(),size);out.write(bytes.data(),file.gcount());
    if(length>std::streampos(size))out<<"\n[Only the final 128 KiB included.]\n";
}
}
void SetStage(const char* value){stage.store(value?value:"Unknown stage",std::memory_order_release);}
const char* IdentityText(){return identity.load(std::memory_order_acquire);}
void Initialize(HMODULE hud,const char* mod,const char* version,const std::filesystem::path& root){
    try {
        wchar_t game[32768]{},dll[32768]{},local[32768]{},locale[32]{};
        GetModuleFileNameW(nullptr,game,DWORD(std::size(game)));GetModuleFileNameW(hud,dll,DWORD(std::size(dll)));
        const auto localLength=GetEnvironmentVariableW(L"LOCALAPPDATA",local,DWORD(std::size(local)));
        if(!root.empty())folder=root;else if(localLength && localLength<std::size(local))folder=std::filesystem::path(local)/L"D2RHUD";
        DWORD localeBytes=sizeof(locale);RegGetValueW(HKEY_CURRENT_USER,L"Software\\Blizzard Entertainment\\Battle.net\\Launch Options\\OSI",L"LOCALE",RRF_RT_REG_SZ,nullptr,locale,&localeBytes);
        std::ostringstream out;SYSTEMTIME time{};GetSystemTime(&time);
        out<<"HUD version: "<<(version?version:"unknown")<<"\nHUD build: "<<__DATE__<<' '<<__TIME__<<"\nProcess: "<<GetCurrentProcessId()<<" (x64)\n";
        out<<"Game: "<<Utf8(game)<<"\nGame version: "<<FileVersion(game)<<"\nGame SHA256: "<<Sha256(game)<<"\nHUD: "<<Utf8(dll)<<"\nHUD SHA256: "<<Sha256(dll)<<'\n';
        out<<"Mod: "<<(mod?mod:"")<<"\nGame text locale (registry): "<<Utf8(locale)<<'\n';
        if(mod && *mod){const auto directory=std::filesystem::path(game).parent_path()/L"Mods"/mod/(std::string(mod)+".mpq");
            out<<"Mod directory: "<<Utf8(directory)<<"\nMod descriptor SHA256: "<<Sha256(directory/L"modinfo.json")<<"\nMemory overrides SHA256: "<<Sha256(directory/L"data/Pandemonium/memory_overrides.json")<<"\nThese selected hashes do not fingerprint every mod asset.\n";}
        auto text=new std::string(out.str());identity.store(text->c_str(),std::memory_order_release); // Immutable for fault-handler lifetime.
        if(!folder.empty()){
            HudConfig::Json prefs;
            if(HudUserConfig::Read("Diagnostics",prefs,folder) && prefs.is_object() && prefs.contains("PerformanceCounters") && prefs["PerformanceCounters"].is_boolean())HudTelemetry::SetEnabled(prefs["PerformanceCounters"].get<bool>());
        }
    }catch(...){status="Diagnostic identity could not be fully collected.";}
}
void WriteFaultContext(HANDLE file){
    DWORD written=0;const auto text=IdentityText();WriteFile(file,text,DWORD(strlen(text)),&written,nullptr);
    const auto current=stage.load(std::memory_order_acquire);WriteFile(file,"Startup stage: ",15,&written,nullptr);WriteFile(file,current,DWORD(strlen(current)),&written,nullptr);
    const auto feature=HudTelemetry::ActiveFeature();constexpr char label[]="\r\nActive feature on faulting thread: ";WriteFile(file,label,sizeof(label)-1,&written,nullptr);WriteFile(file,feature,DWORD(strlen(feature)),&written,nullptr);WriteFile(file,"\r\n",2,&written,nullptr);
}
bool SetPerformanceEnabled(bool value){
    if(folder.empty())return false;
    if(!HudUserConfig::Update("Diagnostics",[&](HudConfig::Json& prefs){prefs["PerformanceCounters"]=value;},folder)){status="Diagnostics preference could not be saved; existing file kept.";return false;}
    HudTelemetry::SetEnabled(value);status.clear();return true;
}
std::string PerformanceText(){
    std::ostringstream out;out<<"Counters: "<<(HudTelemetry::Enabled()?"enabled":"disabled")<<"\nInclusive wall time; nested rows overlap. Percentile is a histogram upper bound.\n";
    const auto samples=HudTelemetry::Snapshot();
    for(size_t i=1;i<samples.size();++i){const auto& s=samples[i];if(!s.calls)continue;
        out<<HudTelemetry::Names[i]<<": calls="<<s.calls<<std::fixed<<std::setprecision(3)<<" mean_ms="<<double(s.totalNs)/s.calls/1e6<<" max_ms="<<double(s.maximumNs)/1e6<<" p95_le_ms="<<double(s.p95UpperNs)/1e6<<" total_ms="<<double(s.totalNs)/1e6<<'\n';}
    return out.str();
}
std::filesystem::path SaveSupportReport(){
    if(folder.empty())throw std::runtime_error("Diagnostics folder is unavailable.");
    const auto directory=folder/L"Support";std::filesystem::create_directories(directory);
    SYSTEMTIME t{};GetSystemTime(&t);static std::atomic<unsigned> serial{0};wchar_t name[128]{};
    swprintf_s(name,L"report-%04u%02u%02u-%02u%02u%02u-%lu-%u.txt",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,GetCurrentProcessId(),++serial);
    const auto path=directory/name;std::ofstream out(path,std::ios::binary);
    out<<"D2RHUD local support report (UTC)\n"<<IdentityText()<<"Startup stage: "<<stage.load()<<"\nSession generation: "<<HudSession::Current()<<"\n\n"<<PerformanceText()<<"\nInspect native hover: "<<Inspect::SelectionStatus()<<"\nInspect last world scan: "<<Inspect::WorldStatus()<<'\n';
    const auto hitch=HudHitches::Snapshot();out<<"\nHitch recorder: "<<hitch.status<<"\nLast hitch report: "<<Utf8(hitch.report)<<'\n';
    for(const wchar_t* file:{L"feature-status.txt",L"launch-diagnostics.json",L"crash.log",L"map-events.log",L"spectate.log"})AppendFile(out,folder/file);
    out.flush();if(!out)throw std::runtime_error("Could not write support report.");return path;
}
void DrawPanel(){
    HudText::SeparatorText("Hitch recorder");
    HudText::TextWrapped("1. Start a capture.  2. Close the HUD menu and play until it hitches.  3. Send the saved JSON report. Captures stop and save automatically.");
    auto capture=HudHitches::Snapshot();
    static int threshold=1,duration=1;
    if(!capture.recording&&!capture.saving){
        HudText::TextUnformatted("Hitch threshold");ImGui::SameLine();ImGui::SetNextItemWidth(130);ImGui::Combo("##hitch-threshold",&threshold,"33 ms\0 50 ms\0 100 ms\0 200 ms\0");
        ImGui::SameLine();HudText::TextUnformatted("Capture length");ImGui::SameLine();ImGui::SetNextItemWidth(130);ImGui::Combo("##hitch-duration",&duration,"2 minutes\0 5 minutes\0 10 minutes\0");
        if(HudText::Button("Start hitch capture")){
            constexpr double limits[]{33,50,100,200};constexpr unsigned seconds[]{120,300,600};
            if(!HudHitches::Start({limits[threshold],seconds[duration]},folder/L"Support",IdentityText()))status="Could not start hitch capture.";
            capture=HudHitches::Snapshot();
        }
    }else if(capture.recording){
        HudText::Text("Recording | %.0f seconds remaining",capture.remaining);
        if(HudText::Button("Stop and save capture"))HudHitches::Stop();
    }else HudText::TextUnformatted("Saving capture in the background...");
    if(!capture.status.empty())HudText::TextWrapped("%s",capture.status.c_str());
    if(capture.graphCount){
        const auto& summary=capture.summary;
        if(summary.frames)HudText::Text("%llu gameplay hitches | worst %.1f ms | average %.1f ms | p95 <= %.1f ms",summary.hitches,summary.maximumMs,summary.Mean(),summary.P95Upper());
        else HudText::TextWrapped("No gameplay-only samples yet. Close the HUD menu and play to include gameplay in this capture.");
        HudText::Text("All contexts: %llu pauses over %.0f ms | worst %.1f ms",summary.allHitches,capture.options.thresholdMs,summary.allMaximumMs);
        if(capture.graphCount){ImGui::PlotLines("Frame time (ms)",capture.graph.data(),capture.graphCount,0,nullptr,0,FLT_MAX,ImVec2(0,80));}
        HudText::TextWrapped("Loading, HUD-menu, unfocused and session-transition intervals are excluded from the hitch count. The graph also includes those intervals.");
        if(!capture.worst.empty()&&ImGui::BeginTable("hitch-worst",5,ImGuiTableFlags_RowBg|ImGuiTableFlags_BordersInnerH)){
            for(const char* name:{"Worst frame","HUD overlay","Present wait","Render CPU","At (seconds)"})ImGui::TableSetupColumn(name);ImGui::TableHeadersRow();
            for(size_t i=0;i<(std::min)(size_t(6),capture.worst.size());++i){const auto& f=capture.worst[i];ImGui::TableNextRow();ImGui::TableNextColumn();HudText::Text("%.1f ms",f.intervalMs);ImGui::TableNextColumn();HudText::Text("%.1f ms",f.hudMs);ImGui::TableNextColumn();HudText::Text("%.1f ms",f.presentMs);ImGui::TableNextColumn();HudText::Text("%.1f ms",f.threadCpuMs);ImGui::TableNextColumn();HudText::Text("%.1f",double(f.at-capture.start)/double(capture.frequency));}
            ImGui::EndTable();
        }
    }
    if(capture.dropped||capture.overwritten)HudText::TextWrapped("Some trace history was dropped or overwritten. The report records its limits; session hitch totals are retained.");
    if(!capture.report.empty()){
        HudText::TextWrapped("Report: %s",Utf8(capture.report.filename()).c_str());
        if(HudText::Button("Open report folder"))ShellExecuteW(nullptr,L"open",capture.report.parent_path().c_str(),nullptr,nullptr,SW_SHOWNORMAL);
        ImGui::SameLine();if(HudText::Button("Copy report path"))ImGui::SetClipboardText(Utf8(capture.report).c_str());
    }
    HudText::TextWrapped("Captures frame cadence, selected HUD timings, render-thread CPU, process CPU/memory/I/O and system CPU. Present wall time is not a GPU timer. No saves, chat or credentials are collected or uploaded.");
    ImGui::Spacing();
    if(!ImGui::CollapsingHeader("Support report and advanced counters"))return;
    HudText::TextWrapped("Inspect native hover: %s",Inspect::SelectionStatus());
    HudText::TextWrapped("Inspect last world scan: %s",Inspect::WorldStatus());
    HudText::TextWrapped("Save a local report with versions, installation hashes, feature status and recent failures. Character saves are not included.");
    if(HudText::Button("Save support report"))try{status="Saved: "+Utf8(SaveSupportReport());}catch(const std::exception& error){status=error.what();}
    if(!status.empty()){HudText::TextWrapped("%s",status.c_str());if(HudText::Button("Copy report status"))ImGui::SetClipboardText(status.c_str());}
    ImGui::Spacing();ImGui::Separator();ImGui::Spacing();
    bool enabled=HudTelemetry::Enabled();if(HudText::Checkbox("Enable performance counters",&enabled))SetPerformanceEnabled(enabled);
    HudText::TextWrapped("Measures selected HUD functions, hooks and background work. Leave disabled for normal play. Timings include time spent waiting; nested rows overlap.");
    if(HudText::Button("Reset counters"))HudTelemetry::Reset();
    static uint64_t refreshed=0;static std::string text;
    const auto now=GetTickCount64();if(text.empty() || now-refreshed>=1000){text=PerformanceText();refreshed=now;}
    ImGui::Spacing();HudText::TextWrapped("%s",text.c_str());
}
}
