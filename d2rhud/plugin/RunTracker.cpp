#include "RunTracker.h"
#include "RunTrackerStore.h"
#include "RunTrackerAnalysis.h"
#include "../BackgroundJob.h"
#include "../ProcessLifetime.h"
#include "../RuntimeTelemetry.h"
#include "../SessionEpoch.h"
#include "HudText.h"
#include "SettingsUI.h"
#include <Windows.h>
#include <atomic>
#include <deque>
#include <fstream>
#include <mutex>
#include <set>

namespace RunTracker {
namespace {
std::atomic<bool> tracking{false};
struct IOResult {Library library;bool loaded=false,cleared=false;ClearResult clear;std::string error;std::vector<Run> failed;};
struct Runtime {
    Recorder recorder{std::to_string(GetTickCount64())+"-"+std::to_string(GetCurrentProcessId())};
    bool initialized=false,loaded=false,settingsValid=true,historyRequested=false;uint64_t lastTick=0,lastSave=0,retryAt=0;
    std::filesystem::path folder,preferencesRoot;std::vector<Run> history;std::map<std::string,Run> pending;
    std::map<int,std::string> areas;HudAsync::Job<IOResult> io;
    std::vector<Run> inFlight;
    std::string status;size_t unreadable=0,older=0;uint64_t revision=0;
    bool clearing=false,clearJob=false,clearExports=false;
    std::function<std::filesystem::path()> marketFolder;
    HudAsync::Job<Analysis::Sources> analysisLoad;HudAsync::Job<Analysis::Report> analysisWork;
    std::shared_ptr<const Analysis::Sources> analysisSource;
    Analysis::Report analysisReport;std::string analysisSourceKey,analysisReportKey,analysisError;
    uint64_t sourceGeneration=0,reportGeneration=0,sourceRevision=0;
    uint64_t sourceAttempted=0,reportAttempted=0;
    bool sourceReady=false,reportReady=false;
};
Runtime& State(){return HudLifetime::State<Runtime>();}
void Initialize(){
    auto& s=State();if(s.initialized)return;s.initialized=true;
    wchar_t local[32768]{};const auto length=GetEnvironmentVariableW(L"LOCALAPPDATA",local,DWORD(std::size(local)));
    if(!length||length>=std::size(local)){s.status="Run Tracker data folder is unavailable.";s.settingsValid=false;return;}
    s.folder=std::filesystem::path(local)/L"D2RHUD/RunTracker";
    s.preferencesRoot=s.folder.parent_path();HudConfig::Json prefs;
    const auto read=HudUserConfig::Read("RunTracker",prefs,s.preferencesRoot);
    if(!read){s.settingsValid=false;s.status="Run Tracker preferences could not be read; existing files kept. "+read.error;}
    else {s.recorder.settings=DecodeSettings(prefs);if(!read.error.empty())s.status=read.error;}
    tracking.store(s.recorder.settings.enabled,std::memory_order_release);
}
void SaveSettings(){
    auto& s=State();if(!s.settingsValid){s.status="Repair the shared HUD preferences before saving. Existing files kept.";return;}
    const auto encoded=EncodeSettings(s.recorder.settings);
    const auto result=HudUserConfig::Update("RunTracker",[&](HudConfig::Json& j){for(const auto& [key,value]:encoded.items())j[key]=value;},s.preferencesRoot);
    if(!result)s.status=result.error;else s.status="Run Tracker preferences saved.";
    tracking.store(s.recorder.settings.enabled,std::memory_order_release);
}
void Queue(const Run& run){auto& s=State();if(s.clearing)return;s.pending[run.id]=run;++s.revision;}
void RequestClearHistory(bool exports){
    auto& s=State();if(s.clearing||s.folder.empty())return;
    s.clearing=true;s.clearExports=exports;s.retryAt=0;
    s.pending.clear();s.history.clear();s.recorder.ClearRecordedData();
    s.unreadable=s.older=0;++s.revision;++s.reportGeneration;
    s.analysisReport={};s.analysisReportKey.clear();s.reportReady=false;s.analysisError.clear();
    s.status="Clearing Run Tracker data...";
}
void Collect(){
    auto& s=State();for(auto& run:s.recorder.completed){Queue(run);s.history.insert(s.history.begin(),std::move(run));}s.recorder.completed.clear();
    if(s.history.size()>size_t(s.recorder.settings.historyLimit))s.history.resize(s.recorder.settings.historyLimit);
}
void PumpIO(uint64_t now){
    auto& s=State();
    if(auto completed=s.io.Poll()){
        if(s.clearing){
            // An earlier load/save must finish before deletion starts, but its
            // completion (including retry snapshots) must never repopulate data.
            if(s.clearJob){
                s.clearJob=s.clearing=false;s.loaded=false;
                if(completed->value&&completed->value->cleared){const auto& result=*completed->value;
                    s.history=result.library.runs;s.loaded=true;s.unreadable=result.library.unreadable;s.older=result.library.older;
                    s.status=result.clear.failed?"Some Run Tracker files could not be cleared ("+std::to_string(result.clear.failed)+"). Close other programs using these files and try again. "+result.clear.error:"Run Tracker history cleared. Timer and display settings kept.";
                    if(!result.library.error.empty())s.status+=" Remaining history could not be read: "+result.library.error;
                }else s.status="Run Tracker clear task failed. Remaining history will be reloaded; try clearing again.";
                ++s.revision;
            }
        }else if(completed->value){auto& result=*completed->value;
            if(result.loaded){s.loaded=true;s.unreadable=result.library.unreadable;s.older=result.library.older;
                std::set<std::string> known;for(const auto& r:s.history)known.insert(r.id);if(s.recorder.current)known.insert(s.recorder.current->id);
                for(auto& run:result.library.runs)if(known.insert(run.id).second)s.history.push_back(std::move(run));
                std::sort(s.history.begin(),s.history.end(),[](const Run& a,const Run& b){return a.startedUtc>b.startedUtc;});++s.revision;
                if(!result.library.error.empty())s.status=result.library.error;
            }
            if(!result.error.empty()){s.status="Run history could not be saved: "+result.error;s.retryAt=now+10000;}
            for(auto& run:result.failed)if(!s.pending.contains(run.id))s.pending.emplace(run.id,std::move(run));
        }else{s.status="Run history background task failed; pending snapshots retained.";s.retryAt=now+10000;for(auto& run:s.inFlight)if(!s.pending.contains(run.id))s.pending.emplace(run.id,std::move(run));}
        s.inFlight.clear();
    }
    if(s.clearing){
        if(!s.io.Busy()){
            const auto root=s.folder;const auto exports=s.clearExports;const auto limit=size_t(s.recorder.settings.historyLimit);
            s.clearJob=s.io.Start(0,[root,exports,limit]{IOResult r;r.cleared=true;r.clear=ClearHistoryFiles(root,exports);r.library=LoadLibrary(root/L"runs",limit);return r;});
            if(!s.clearJob){s.clearing=false;s.loaded=false;s.status="The clear task could not start. Remaining history will be reloaded; try clearing again.";}
        }return;
    }
    if(s.folder.empty()||s.io.Busy()||now<s.retryAt)return;
    // The history browser can load while tracking is off. Native callbacks still
    // take the disabled fast path and never scan inventory or write files.
    if(!s.loaded){if(!Enabled()&&!s.historyRequested)return;const auto root=s.folder/L"runs";const auto limit=size_t(s.recorder.settings.historyLimit);if(!s.io.Start(0,[root,limit]{IOResult r;r.loaded=true;r.library=LoadLibrary(root,limit);return r;})){s.status="Run history worker could not start; retrying shortly.";s.retryAt=now+10000;}return;}
    if(s.pending.empty())return;
    std::vector<Run> batch;for(const auto& [id,run]:s.pending)batch.push_back(run);
    const auto root=s.folder/L"runs";
    if(s.io.Start(0,[root,batch]()mutable{IOResult r;for(auto& run:batch){const auto saved=SaveRun(root,run);if(!saved){r.error=saved.error;r.failed.push_back(std::move(run));}}return r;})){s.inFlight=std::move(batch);s.pending.clear();}
}
void RefreshNow(){auto& s=State();auto f=s.recorder.Context();f.now=GetTickCount64();f.utc=std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();s.recorder.Tick(std::move(f));}
void Export(const std::vector<const Run*>& selected,bool items){
    auto& s=State();try{
        const auto root=s.folder/L"exports";std::filesystem::create_directories(root);const auto path=root/(std::string(items?"items-":"runs-")+std::to_string(GetTickCount64())+".csv");
        std::string text;
        if(!items){std::vector<Run> runs;for(auto* r:selected)runs.push_back(*r);text=ExportRuns(runs);}
        else {text="run_id,character,item,code,quality,quantity,first_observed_area,seconds\r\n";for(auto* r:selected)for(const auto& f:r->items)text+=CsvCell(r->id)+","+CsvCell(r->character)+","+CsvCell(f.item.name)+","+CsvCell(f.item.code)+","+CsvCell(Quality(f.item.quality))+","+std::to_string(f.item.quantity)+","+std::to_string(f.area)+","+std::to_string(double(f.atMs)/1000)+"\r\n";}
        std::ofstream out(path,std::ios::binary);out<<"\xef\xbb\xbf"<<text;out.flush();if(!out)throw std::runtime_error("Could not write CSV export");
        s.status="Exported: "+path.string();
    }catch(const std::exception& e){s.status=e.what();}
}
}
bool Enabled(){return tracking.load(std::memory_order_acquire);}
void ConfigureAnalysisFolder(std::function<std::filesystem::path()> folder){State().marketFolder=std::move(folder);}
void SetAreaNames(const std::map<int,std::string>& names){State().areas=names;}
void Tick(const HudFrame::Context& context,uint64_t epoch,const std::string& mod,bool hardcore){
    Initialize();auto& s=State();const auto now=GetTickCount64();PumpIO(now);
    if(s.clearing)return;
    if(!Enabled()&&!s.recorder.current){Frame f;f.now=now;f.mod=mod;f.hardcore=hardcore;f.utc=std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();s.recorder.Tick(std::move(f));return;}
    if(now-s.lastTick<100&&epoch==s.recorder.Epoch())return;s.lastTick=now;
    HudTelemetry::Scope measured(HudTelemetry::Feature::RunTracker);
    auto f=Sample(context,epoch,mod);
    f.hardcore=hardcore;f.modeKnown=true;
    if(epoch!=HudSession::Current())return;
    if(const auto found=s.areas.find(f.area);found!=s.areas.end())f.areaName=found->second;else f.areaName="Area "+std::to_string(f.area);
    // Missing transient native details during loading do not erase identity.
    const auto& previous=s.recorder.Context();if(epoch==previous.epoch){if(f.character.empty())f.character=previous.character;if(!f.area){f.area=previous.area;f.areaName=previous.areaName;f.town=previous.town;}if(f.difficulty<0)f.difficulty=previous.difficulty;}
    s.recorder.Tick(std::move(f));
    Collect();if(s.recorder.current&&now-s.lastSave>=5000){Queue(*s.recorder.current);s.lastSave=now;}PumpIO(now);
}

#include "RunTrackerUI.inl"
}
