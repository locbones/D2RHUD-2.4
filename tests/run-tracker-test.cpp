#define NOMINMAX
#include "../d2rhud/plugin/RunTracker.cpp"
#include "../d2rhud/plugin/D2RHUD/MenuWorkspace.h"
#include <imgui_internal.h>
#include <cassert>
#include <iostream>
#include <thread>

namespace RunTracker {
static int samples=0;
Frame Sample(const HudFrame::Context&,uint64_t epoch,const std::string& mod){++samples;Frame f;f.epoch=epoch;f.now=GetTickCount64();f.mod=mod;f.utc=1793600000;f.character="TestHero";return f;}
}

namespace RunTracker::Analysis {
Sources Load(const std::filesystem::path&,const std::string& mod,const std::filesystem::path&,bool hc,int64_t now){
    Sources out;out.mod=mod;out.hardcore=hc;out.loadedUtc=now;out.catalogStatus="Fixture item types";out.marketStatus="Fixture completed sales";
    out.bases["7gd"]={"Weapons","swor"};out.bases["r30"]={"Runes","rune"};out.currencies["gold/gld/2/-1/-1/"]="Gold";
    out.treasures["Fixture root"]={1,1,{{"7gd",1},{"r30",1}}};
    Item item;item.code="7gd";item.quality=7;item.fileIndex=222;item.sockets=0;
    for(int n=0;n<4;++n)out.sales.push_back({Identity(item),"gold/gld/2/-1/-1/","Gold",100.+n*20,1,now-60});
    return out;
}
}

using namespace RunTracker;
Item Sword(int quantity=1){Item item;item.id=9;item.seed=12345;item.name="The Grandfather";item.code="7gd";item.quality=7;item.fileIndex=222;item.quantity=quantity;item.sockets=0;item.stackableKnown=true;return item;}
Item Rune(int quantity=1){auto item=Sword(quantity);item.id=10;item.seed=54321;item.name="Ber Rune";item.code="r30";item.quality=2;item.fileIndex=-1;item.stackable=true;return item;}
Frame Context(uint64_t now,int area=1){Frame f;f.epoch=1;f.now=now;f.utc=1793600000+int64_t(now/1000);f.area=area;f.areaName=area==1?"Rogue Encampment":"Chaos Sanctuary";f.town=Town(area);f.mod="RMD-MP";f.character="TestHero";f.level=99;return f;}
void TimerTests(){
    Recorder r("fixture");auto f=Context(1000);r.Tick(f);assert(!r.current);r.settings.enabled=true;r.Tick(f);assert(r.current);
    f=Context(6000);r.Tick(f);assert(r.current->wallMs==5000&&r.current->activeMs==0&&r.current->townMs==5000);
    f=Context(7000,108);r.Tick(f);f=Context(17000,108);r.Tick(f);assert(r.current->activeMs==10000&&r.current->wallMs==16000);
    f=Context(18000,108);f.loading=true;r.Tick(f);f.now=23000;r.Tick(f);assert(r.current->activeMs==11000&&r.current->loadingMs==5000);
    f=Context(24000,108);r.Tick(f);r.paused=true;f.now=27000;r.Tick(f);assert(r.current->pausedMs==3000&&r.current->activeMs==11000);
    r.paused=false;r.Split();assert(r.completed.size()==1&&r.completed[0].endReason=="Manual split"&&r.current->wallMs==0);
    f=Context(28000,108);f.dead=true;r.Tick(f);f.now=29000;r.Tick(f);assert(r.current->deaths==1);
    f=Context(30000,108);f.epoch=2;f.character="OtherHero";r.Tick(f);assert(r.completed.size()==2&&r.current->character=="OtherHero");
    f.now=31000;f.epoch=0;r.Tick(f);assert(!r.current&&r.completed.size()==3);assert(Duration(123456,r.settings)=="2:03");r.settings.milliseconds=true;assert(Duration(123456,r.settings)=="2:03.456");r.settings.timeFormat=1;assert(Duration(123456,r.settings)=="0:02:03.456");r.settings.timeFormat=2;assert(Duration(123456,r.settings)=="123.456 s");
    Recorder town("town");town.settings.enabled=true;town.settings.freezeTown=false;town.settings.freezeLoading=false;f=Context(1000);town.Tick(f);f.now=6000;f.loading=true;town.Tick(f);f.now=11000;town.Tick(f);assert(town.current->activeMs==10000);
    Recorder manual("manual");manual.settings.enabled=true;manual.settings.autoStart=false;manual.Tick(Context(1000));assert(!manual.current);manual.Start();assert(manual.current);manual.Finish("Manual");manual.Tick(Context(2000));assert(!manual.current);
}
void RetentionTests(){
    Recorder r("loot");r.settings.enabled=true;auto f=Context(1000);f.storage["Character"]={Rune(10)};r.Tick(f);assert(r.current->items.empty());
    f.now=2000;f.area=108;f.town=false;f.storage["Character"]={Rune(15),Sword()};r.Tick(f);assert(r.current->kept==6&&r.current->items.size()==2);
    f.now=3000;r.Tick(f);assert(r.current->kept==6); // repeated snapshots are inert
    f.now=4000;f.storage["Character"]={};f.storage["Shared page 1 / tab 1"]={Rune(15),Sword()};r.Tick(f);assert(r.current->kept==6); // late first-seen stash + stack transfer
    f.now=5000;f.storage["Shared page 1 / tab 1"]={Rune(12)};r.Tick(f);assert(r.current->kept==2&&r.current->items.size()==1); // sold unique + consumed stack
    f.now=6000;f.storage.clear();r.Tick(f);assert(r.current->kept==2); // unloaded container never means empty
    f.now=7000;f.storage["Shared page 1 / tab 1"]={};r.Tick(f);assert(r.current->kept==0);
    f.now=8000;f.storage["Character"]={Sword()};r.Tick(f);assert(r.current->kept==1);r.Split();assert(r.current->kept==0);f.now=9000;r.Tick(f);assert(r.current->kept==0); // manual run's new baseline
    // A transfer through an unavailable destination keeps its acquisition
    // provenance, so opening the destination later restores the retained count.
    Recorder delayed("delayed");delayed.settings.enabled=true;f=Context(1000);f.storage["Character"]={};delayed.Tick(f);f.now=2000;f.storage["Character"]={Sword()};delayed.Tick(f);f.now=3000;f.storage["Character"]={};delayed.Tick(f);assert(!delayed.current->kept);f.now=4000;f.storage["Shared page 2 / tab 3"]={Sword()};delayed.Tick(f);assert(delayed.current->kept==1);
    auto encoded=Encode(*r.current);Run decoded;assert(Decode(encoded,decoded)&&Encode(decoded)==encoded);encoded["schema"]=999;assert(!Decode(encoded,decoded));encoded=Encode(*r.current);encoded["id"]="../escape";assert(!Decode(encoded,decoded));
    auto a=*r.current,b=a;a.activeMs=1000;a.kept=1;b.activeMs=3000;b.kept=3;const auto stats=Summarize({&a,&b});assert(stats.count==2&&stats.mean==2&&stats.median==2&&stats.fastest==1&&stats.slowest==3&&stats.stddev==1&&stats.kept==4&&stats.perHour==3600);
    assert(CsvCell("=cmd").find("'=cmd")!=std::string::npos);assert(CsvCell("a,\"b")=="\"a,\"\"b\"");
}
void PersistenceTests(const std::filesystem::path& root){
    Recorder r("persist");r.settings.enabled=true;r.Tick(Context(1000));r.Tick(Context(2000));r.Finish("Fixture");const auto run=r.completed[0];const auto directory=root/"runs";
    assert(SaveRun(directory,run));auto library=LoadLibrary(directory,100);assert(library.runs.size()==1&&library.runs[0].id==run.id);
    const auto path=directory/(run.id+".json");std::ofstream(path)<<"{broken";assert(!SaveRun(directory,run));std::ifstream input(path);std::string text((std::istreambuf_iterator<char>(input)),{});assert(text=="{broken");input.close();assert(LoadLibrary(directory,100).unreadable==1);
    std::ofstream(path)<<"{\"schema\":999}";assert(!SaveRun(directory,run));
    std::ofstream(path)<<"{\"schema\":1,\"native-note\":\"preserve\"}";assert(SaveRun(directory,run));HudConfig::Json j;assert(HudConfig::Read(path,j)&&j["native-note"]=="preserve");
    auto unfinished=run;unfinished.id+="-unfinished";unfinished.finished=false;assert(SaveRun(directory,unfinished));library=LoadLibrary(directory,100);assert(library.runs.size()==2);bool recovered=false;for(auto& entry:library.runs)recovered|=entry.endReason=="Interrupted / recovered";assert(recovered);
}
void ClearTests(const std::filesystem::path& root){
    const auto folder=root/("clear-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
    std::filesystem::create_directories(folder/"runs/nested");std::filesystem::create_directories(folder/"exports");
    const std::string preferences="{\"freezeTown\":false,\"overlayScale\":1.4}";
    std::ofstream(folder/"settings.json")<<preferences;std::ofstream(folder/"runs/broken.json")<<"{broken";std::ofstream(folder/"runs/future.json")<<"{\"schema\":999}";
    std::ofstream(folder/"runs/old.json.tmp-123-1")<<"snapshot";std::ofstream(folder/"runs/keep.txt")<<"unrelated";std::ofstream(folder/"runs/nested/keep.json")<<"unrelated nested";std::ofstream(folder/"exports/items.csv")<<"private export";
    auto cleared=ClearHistoryFiles(folder,false);assert(cleared.removed==3&&!cleared.failed&&std::filesystem::exists(folder/"exports/items.csv")&&std::filesystem::exists(folder/"runs/keep.txt")&&std::filesystem::exists(folder/"runs/nested/keep.json"));
    cleared=ClearHistoryFiles(folder,true);assert(cleared.removed==1&&!cleared.failed&&!std::filesystem::exists(folder/"exports/items.csv"));
    std::ifstream prefs(folder/"settings.json");assert(std::string((std::istreambuf_iterator<char>(prefs)),{})==preferences);prefs.close();
    std::ofstream(folder/"runs/locked.json")<<"locked";const auto locked=CreateFileW((folder/"runs/locked.json").c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);assert(locked!=INVALID_HANDLE_VALUE);
    cleared=ClearHistoryFiles(folder,false);assert(cleared.failed==1&&std::filesystem::exists(folder/"runs/locked.json"));CloseHandle(locked);assert(ClearHistoryFiles(folder,false).removed==1);

    // A save/load finishing after the button was clicked must never resurrect
    // the old run, even if it returns a retry snapshot or a loaded library.
    auto& s=State();assert(!s.io.Busy());s.folder=folder;s.loaded=true;s.recorder=Recorder("clear-fixture");s.recorder.settings.enabled=true;s.recorder.settings.overlayScale=1.4f;tracking.store(true);
    auto f=Context(1000,108);f.storage["Character"]={};s.recorder.Tick(f);f.now=2000;f.storage["Character"]={Sword()};s.recorder.Tick(f);const auto old=*s.recorder.current;assert(old.kept==1);s.history={old};s.pending[old.id]=old;s.analysisReport.entries=20;s.reportReady=true;s.inFlight={old};
    std::promise<void> release;auto gate=release.get_future().share();assert(s.io.Start(0,[gate,folder,old]{gate.wait();assert(SaveRun(folder/"runs",old));IOResult result;result.loaded=true;result.library.runs={old};result.failed={old};return result;}));
    RequestClearHistory(false);assert(s.clearing&&s.history.empty()&&s.pending.empty()&&!s.reportReady&&!s.analysisReport.entries&&s.recorder.current&&s.recorder.current->kept==0&&s.recorder.current->id!=old.id&&s.recorder.settings.overlayScale==1.4f);
    PumpIO(2000);assert(s.io.Busy()&&!s.clearJob);release.set_value();
    for(int n=0;n<2000&&(s.clearing||s.io.Busy());++n){PumpIO(3000+n);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    assert(!s.clearing&&!s.io.Busy()&&s.loaded&&s.history.empty()&&s.pending.empty()&&s.inFlight.empty()&&!std::filesystem::exists(folder/"runs"/(old.id+".json")));
    f.now=5000;s.recorder.Tick(f);assert(s.recorder.current->kept==0&&s.recorder.current->activeMs==3000); // existing sword is baseline, not a new find
    // A stopped or disabled tracker does not start a new run when cleared.
    s.recorder.Finish("manual");s.recorder.ClearRecordedData();s.recorder.Tick(f);assert(!s.recorder.current&&s.recorder.completed.empty());
    assert(SaveRun(folder/"runs",old));const auto lockedRun=CreateFileW((folder/"runs"/(old.id+".json")).c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);assert(lockedRun!=INVALID_HANDLE_VALUE);
    RequestClearHistory(false);for(int n=0;n<2000&&(s.clearing||s.io.Busy());++n){PumpIO(10000+n);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    assert(!s.clearing&&s.history.size()==1&&s.status.find("could not be cleared")!=std::string::npos);CloseHandle(lockedRun);
    RequestClearHistory(false);for(int n=0;n<2000&&(s.clearing||s.io.Busy());++n){PumpIO(13000+n);std::this_thread::sleep_for(std::chrono::milliseconds(1));}assert(!s.clearing&&s.history.empty());
    s.recorder.settings.enabled=false;tracking.store(false);s.pending.clear();s.history.clear();
}
void Capture(const std::filesystem::path& directory,const std::string& name,unsigned char* atlas,int width,int height){
    std::ofstream texture(directory/"atlas.rgba",std::ios::binary);texture.write(reinterpret_cast<const char*>(atlas),size_t(width)*height*4);
    const auto* data=ImGui::GetDrawData();std::ofstream out(directory/(name+".bin"),std::ios::binary);uint32_t commands=0;for(auto* list:data->CmdLists)commands+=list->CmdBuffer.Size;
    const uint32_t header[]{uint32_t(data->DisplaySize.x),uint32_t(data->DisplaySize.y),uint32_t(width),uint32_t(height),uint32_t(data->TotalVtxCount),uint32_t(data->TotalIdxCount),commands};out.write(reinterpret_cast<const char*>(header),sizeof(header));
    for(auto* list:data->CmdLists)out.write(reinterpret_cast<const char*>(list->VtxBuffer.Data),size_t(list->VtxBuffer.Size)*sizeof(ImDrawVert));
    uint32_t vertexOffset=0;for(auto* list:data->CmdLists){for(auto index:list->IdxBuffer){const uint32_t value=uint32_t(index)+vertexOffset;out.write(reinterpret_cast<const char*>(&value),4);}vertexOffset+=list->VtxBuffer.Size;}
    uint32_t indexOffset=0;for(auto* list:data->CmdLists){for(const auto& cmd:list->CmdBuffer){const uint32_t values[]{indexOffset+cmd.IdxOffset,cmd.ElemCount};out.write(reinterpret_cast<const char*>(values),sizeof(values));out.write(reinterpret_cast<const char*>(&cmd.ClipRect),sizeof(cmd.ClipRect));const uint64_t textureId=cmd.GetTexID();out.write(reinterpret_cast<const char*>(&textureId),sizeof(textureId));}indexOffset+=list->IdxBuffer.Size;}
}
void UITests(const std::filesystem::path& root){
    Initialize();auto& s=State();s.folder=root;s.initialized=true;s.loaded=true;s.recorder.settings.enabled=true;tracking.store(true);s.recorder.Tick(Context(1000));s.recorder.settings.analysisCurrency="gold/gld/2/-1/-1/";s.recorder.settings.analysisTreasure="Fixture root";
    auto f=Context(5000,108);f.storage["Character"]={};s.recorder.Tick(f);f.now=15000;f.storage["Character"]={Sword(),Rune(3)};s.recorder.Tick(f);
    for(int n=0;n<30;++n){Run run=*s.recorder.current;run.id="fixture-"+std::to_string(n);run.activeMs=uint64_t(45000+n*1500);run.wallMs=run.activeMs+5000;run.startedUtc-=n*300;run.finished=true;run.label=n%2?"Chaos Sanctuary":"Cow Level";run.launch=s.recorder.launch;s.history.push_back(std::move(run));}
    ImGui::CreateContext();HudMenu::ApplySanctuaryPalette();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.Fonts->AddFontDefault();unsigned char* pixels=nullptr;int width=0,height=0;io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);io.Fonts->SetTexID(1);
    for(auto display:{ImVec2{1280,720},ImVec2{1920,1080},ImVec2{3840,2160}}){io.DisplaySize=display;io.FontGlobalScale=display.x>=3840?2.f:1.f;
        for(int tab=0;tab<8;++tab){for(int repeat=0;repeat<2;++repeat){ImGui::NewFrame();ImGui::SetNextWindowPos({240,20});ImGui::SetNextWindowSize({display.x>=3840?1800.f:display.x>=1920?960.f:680.f,display.y-40});ImGui::Begin("Run Tracker fixture");if(tab==0)Live();else if(tab==1){View().selected=s.history[0].id;History();}else if(tab==2)Charts();else if(tab==3){ItemAnalysis();}else if(tab==4)Preferences();else if(tab==5)AnalysisAreas(s.analysisReport,"Gold");else if(tab==6)AnalysisValuables(s.analysisReport,"Gold");else AnalysisRarity(s.analysisReport);ImGui::End();DrawOverlay();ImGui::Render();assert(ImGui::GetDrawData()->TotalVtxCount>0);}
            if(tab==3)for(int retry=0;retry<1000&&(!s.sourceReady||!s.reportReady);++retry){PollAnalysisSources(false);PollAnalysisReport();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
            if(tab==3){ImGui::NewFrame();ImGui::SetNextWindowPos({240,20});ImGui::SetNextWindowSize({display.x>=3840?1800.f:display.x>=1920?960.f:680.f,display.y-40});ImGui::Begin("Run Tracker fixture");ItemAnalysis();ImGui::End();DrawOverlay();ImGui::Render();assert(s.reportReady&&s.analysisReport.valuable.size()==10);}
            Capture(root,"run-tracker-"+std::to_string(tab)+"-"+std::to_string(int(display.x)),pixels,width,height);
        }
    }
    io.DisplaySize={1920,1080};io.FontGlobalScale=1.f;
    auto overlayFrame=[&]{ImGui::NewFrame();DrawOverlay();ImGui::Render();};
    View().resetOverlay=true;overlayFrame();overlayFrame();auto* window=ImGui::FindWindowByName("Run Tracker###RunTrackerOverlay");assert(window);const auto start=window->Pos;
    io.AddMousePosEvent(start.x+20,start.y+12);overlayFrame();io.AddMouseButtonEvent(0,true);overlayFrame();io.AddMousePosEvent(start.x-80,start.y+92);overlayFrame();io.AddMouseButtonEvent(0,false);overlayFrame();assert(window->Pos.x<start.x-20&&window->Pos.y>start.y+20);
    const auto moved=window->Pos;const auto ini=std::string(ImGui::SaveIniSettingsToMemory());assert(ini.find("RunTrackerOverlay")!=std::string::npos);
    s.recorder.settings.overlayLocked=true;io.AddMousePosEvent(moved.x+20,moved.y+12);overlayFrame();io.AddMouseButtonEvent(0,true);overlayFrame();io.AddMousePosEvent(moved.x+120,moved.y+112);overlayFrame();io.AddMouseButtonEvent(0,false);overlayFrame();assert(window->Pos.x==moved.x&&window->Pos.y==moved.y);
    io.DisplaySize={1280,720};
    for(int acknowledged=0;acknowledged<2;++acknowledged){View().clearAcknowledged=acknowledged!=0;
        for(int repeat=0;repeat<3;++repeat){ImGui::NewFrame();ImGui::SetNextWindowPos({240,20});ImGui::SetNextWindowSize({680,680});ImGui::Begin("Run Tracker fixture");
            if(acknowledged==0&&repeat==0)HudText::OpenPopup("Clear Run Tracker data?###ClearRunTrackerData");ClearDataControls();ImGui::End();ImGui::Render();assert(!s.clearing&&!s.history.empty());
        }Capture(root,"run-tracker-clear-confirm-"+std::to_string(acknowledged),pixels,width,height);
    }
    ImGui::DestroyContext();
    assert(!s.io.Busy()&&!s.analysisLoad.Busy()&&!s.analysisWork.Busy());tracking.store(false);s.recorder.current.reset();s.recorder.settings.enabled=false;
    for(int i=0;i<10000;++i)Tick({},0,"Fixture");assert(samples==0); // no native scans while disabled
}
int main(int argc,char** argv){try{assert(argc==2);const auto root=std::filesystem::path(argv[1]);std::filesystem::create_directories(root);SetEnvironmentVariableW(L"LOCALAPPDATA",(root/L"profile").c_str());TimerTests();RetentionTests();PersistenceTests(root/("store-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64())));UITests(root);ClearTests(root);std::cout<<"PASS: timers/town/loading/pause/splits/deaths/session changes; kept-only net quantities, stash transfers, later-loaded pages and discarded/consumed items; round trips, atomic preservation, recovery, stats and CSV; UI at 720p/1080p/4K; drag/lock/saved timer placement; 10000 disabled ticks make zero native samples; clear history/optional exports preserves settings and unrelated files, reports locked files, discards pending save/load/retry results and restarts a fresh baseline.\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
