// Included by RunTracker.cpp; presentation never retains native game pointers.
namespace {
struct Browser {
    int period=0,sort=0,metric=0;bool thisMod=true,thisCharacter=false,stars=false,recovered=false;
    char search[128]{},itemSearch[128]{},label[257]{},notes[4097]{};
    std::string selected,edited;bool resetOverlay=false;
    bool includeLive=true;int marketMode=0,areaMetric=0;char treasureSearch[128]{};
    bool clearExports=false,clearAcknowledged=false;
};
Browser& View(){return HudLifetime::State<Browser>();}
std::string Date(int64_t utc){__time64_t time=utc;tm value{};char text[64]{};if(_localtime64_s(&value,&time)==0)strftime(text,sizeof(text),"%Y-%m-%d %H:%M",&value);return text;}
std::string AreaName(int id){const auto found=State().areas.find(id);return found==State().areas.end()?"Area "+std::to_string(id):found->second;}
bool Contains(const std::string& haystack,const char* needle){return !*needle||HudLocale::Fold(haystack).find(HudLocale::Fold(needle))!=std::string::npos;}
std::vector<const Run*> Filtered(){
    auto& s=State();const auto& v=View();std::string previousLaunch;int64_t newest=0;
    for(const auto& r:s.history)if(r.launch!=s.recorder.launch&&r.startedUtc>newest){newest=r.startedUtc;previousLaunch=r.launch;}
    std::vector<const Run*> runs;const auto& context=s.recorder.Context();
    for(const auto& r:s.history){
        if(!v.recovered&&!r.finished)continue;
        if(v.period==0&&r.launch!=s.recorder.launch)continue;
        if(v.period==1&&r.launch!=previousLaunch)continue;
        if(v.period==2&&r.startedUtc<context.utc-86400)continue;
        if(v.thisMod&&!context.mod.empty()&&r.mod!=context.mod)continue;
        if(v.thisCharacter&&!context.character.empty()&&r.character!=context.character)continue;
        if(v.stars&&!r.starred)continue;
        if(!Contains(r.label+" "+r.character+" "+r.notes+" "+r.mod,v.search))continue;
        runs.push_back(&r);
    }
    std::sort(runs.begin(),runs.end(),[&](const Run* a,const Run* b){if(v.sort==1)return a->activeMs<b->activeMs;if(v.sort==2)return a->kept>b->kept;return a->startedUtc>b->startedUtc;});return runs;
}
void Stat(const char* label,const std::string& value){HudText::TextDisabled("%s",label);HudText::TextColored({.91f,.78f,.49f,1},"%s",value.c_str());}
void Stats(const std::vector<const Run*>& runs){
    const auto stats=Summarize(runs);const auto& settings=State().recorder.settings;
    if(ImGui::BeginTable("Summary",3,ImGuiTableFlags_SizingStretchSame)){
        auto cell=[&](const char* label,std::string value){ImGui::TableNextColumn();Stat(label,value);};
        cell("Runs",std::to_string(stats.count));cell("Average",Duration(uint64_t(stats.mean*1000),settings));cell("Median",Duration(uint64_t(stats.median*1000),settings));
        cell("Best",Duration(uint64_t(stats.fastest*1000),settings));cell("Kept quantity",std::to_string(stats.kept));char number[64]{};snprintf(number,sizeof(number),"%.1f",stats.perHour);cell("Kept / active hour",number);ImGui::EndTable();
    }
}
void ItemTable(const std::vector<Find>& items,const char* id){
    auto& v=View();ImGui::PushID(id);ImGui::SetNextItemWidth(-1);HudText::InputTextWithHint("##ItemSearch","Search retained items",v.itemSearch,sizeof(v.itemSearch));
    if(ImGui::BeginTable("Items",5,ImGuiTableFlags_RowBg|ImGuiTableFlags_BordersInnerH|ImGuiTableFlags_Resizable|ImGuiTableFlags_ScrollY|ImGuiTableFlags_SizingStretchProp,{0,220*ImGui::GetIO().FontGlobalScale})){
        HudText::TableSetupColumn("Item",0,3);HudText::TableSetupColumn("Quality");HudText::TableSetupColumn("Qty",0,.5f);HudText::TableSetupColumn("First observed area",0,2);HudText::TableSetupColumn("At");ImGui::TableSetupScrollFreeze(0,1);ImGui::TableHeadersRow();
        std::vector<const Find*> rows;for(const auto& item:items)if(Contains(item.item.name+" "+item.item.code+" "+Quality(item.item.quality),v.itemSearch))rows.push_back(&item);
        ImGuiListClipper clipper;clipper.Begin(int(rows.size()));while(clipper.Step())for(int n=clipper.DisplayStart;n<clipper.DisplayEnd;++n){const auto& find=*rows[n];const auto& item=find.item;ImGui::TableNextRow();ImGui::TableNextColumn();
            HudText::TextUnformatted(item.name.c_str());if(ImGui::IsItemHovered()){ImGui::BeginTooltip();HudText::Text("Code: %s  |  Item level: %d",item.code.c_str(),item.level);HudText::Text("Stored in: %s",item.location.c_str());if(item.ethereal)HudText::TextUnformatted("Ethereal");ImGui::EndTooltip();}
            HudText::TextDisabled("%s",item.location.empty()?"Owned storage":item.location.c_str());
            ImGui::TableNextColumn();HudText::TextUnformatted(Quality(item.quality).c_str());ImGui::TableNextColumn();HudText::Text("%d",item.quantity);ImGui::TableNextColumn();HudText::TextUnformatted(AreaName(find.area).c_str());ImGui::TableNextColumn();HudText::TextUnformatted(Duration(find.atMs,State().recorder.settings).c_str());
        }ImGui::EndTable();
    }if(items.empty())HudSettings::Note("No net additions in the observed storage yet.");ImGui::PopID();
}
void AreaTable(const Run& run){
    if(ImGui::BeginTable("Areas",3,ImGuiTableFlags_RowBg|ImGuiTableFlags_BordersInnerH|ImGuiTableFlags_ScrollY,{0,180*ImGui::GetIO().FontGlobalScale})){
        HudText::TableSetupColumn("Area");HudText::TableSetupColumn("Active time");HudText::TableSetupColumn("Total time");ImGui::TableSetupScrollFreeze(0,1);ImGui::TableHeadersRow();
        for(const auto& visit:run.visits){ImGui::TableNextRow();ImGui::TableNextColumn();HudText::TextUnformatted(visit.name.c_str());ImGui::TableNextColumn();HudText::TextUnformatted(Duration(visit.activeMs,State().recorder.settings).c_str());ImGui::TableNextColumn();HudText::TextUnformatted(Duration(visit.wallMs,State().recorder.settings).c_str());}ImGui::EndTable();
    }
}
void Live(){
    auto& s=State();auto& r=s.recorder;const float scale=ImGui::GetIO().FontGlobalScale;
    HudSettings::BeginSection("CurrentRun","Current run",nullptr,scale);
    if(r.current){const auto& run=*r.current;
        Stat(r.paused?"Timer paused":r.Context().loading&&r.settings.freezeLoading?"Loading - timer frozen":r.Context().town&&r.settings.freezeTown?"Town - timer frozen":"Active time",Duration(run.activeMs,r.settings));
        HudText::Text("%s  |  %s",run.character.c_str(),r.Context().areaName.c_str());
        HudText::Text("Total: %s   Town: %s   Loading: %s",Duration(run.wallMs,r.settings).c_str(),Duration(run.townMs,r.settings).c_str(),Duration(run.loadingMs,r.settings).c_str());
        HudText::Text("Retained quantity: %llu   Item entries: %zu   Deaths: %d",run.kept,run.items.size(),run.deaths);
        if(HudText::Button(r.paused?"Resume timer":"Pause timer")){RefreshNow();r.paused=!r.paused;}
        ImGui::SameLine();if(HudText::Button("Finish and start next")){RefreshNow();r.Split();Collect();}
        ImGui::SameLine();if(HudText::Button("Finish run")){RefreshNow();r.Finish("Finished manually");Collect();}
    }else{HudSettings::Note(r.Epoch()?"No run is active. Start manually or enter another game.":"Enter a game to start tracking.");ImGui::BeginDisabled(!r.settings.enabled||!r.Epoch());if(HudText::Button("Start run")){RefreshNow();r.Start();}ImGui::EndDisabled();}
    HudSettings::EndSection(scale);
    if(r.current){
        HudSettings::BeginSection("LiveLoot","Kept items",nullptr,scale);ItemTable(r.current->items,"LiveItems");
        HudSettings::Note("Net additions to observed inventory, equipment, cube and stash. Items later sold, consumed or dropped disappear from this run's retained list. Trades and crafted items can also appear.");
        HudSettings::EndSection(scale);
        if(HudSettings::Disclosure("Areas visited"))AreaTable(*r.current);
        if(HudSettings::Disclosure("Changes since the previous game")){
            const Run* previous=nullptr;for(const auto& run:s.history)if(run.character==r.current->character&&run.mod==r.current->mod&&!run.storageEnd.empty()){previous=&run;break;}
            if(previous){Storage before,after;for(const auto& [scope,items]:r.current->storageEnd)if(previous->storageEnd.contains(scope)){before[scope]=previous->storageEnd.at(scope);after[scope]=items;}
                std::vector<Find> changes;for(auto& item:Additions(before,after))changes.push_back({std::move(item),0,0});ItemTable(changes,"SincePrevious");
                HudSettings::Note("Compares containers observed in both games. Open stash pages to include them; unseen pages and transfers from them cannot be classified as new loot.");
            }else HudSettings::Note("No previous storage snapshot for this character and mod yet.");
        }
    }
    HudSettings::BeginSection("LaunchSummary","Since opening the game",nullptr,scale);
    std::vector<const Run*> launch;for(const auto& run:s.history)if(run.launch==r.launch)launch.push_back(&run);Stats(launch);HudSettings::Note("Completed runs in this game process. The current run is shown above.");HudSettings::EndSection(scale);
}
void Filters(){
    auto& v=View();const float scale=ImGui::GetIO().FontGlobalScale;
    HudSettings::Combo("Period",&v.period,"This launch\0Previous launch\0Last 24 hours\0All loaded history\0",scale);
    ImGui::SetNextItemWidth(-1);HudText::InputTextWithHint("##RunSearch","Search character, run label, notes or mod",v.search,sizeof(v.search));
    HudText::Checkbox("This mod",&v.thisMod);ImGui::SameLine();HudText::Checkbox("This character",&v.thisCharacter);ImGui::SameLine();HudText::Checkbox("Favourites only",&v.stars);
    HudText::Checkbox("Include recovered snapshots",&v.recovered);
}
void Details(Run& run){
    auto& v=View();auto& s=State();
    if(v.edited!=run.id){v.edited=run.id;strncpy_s(v.label,run.label.c_str(),_TRUNCATE);strncpy_s(v.notes,run.notes.c_str(),_TRUNCATE);}
    HudText::Text("%s  |  %s  |  %s",run.character.c_str(),Date(run.startedUtc).c_str(),run.endReason.c_str());
    HudText::Text("Active: %s   Total: %s   Deaths: %d",Duration(run.activeMs,s.recorder.settings).c_str(),Duration(run.wallMs,s.recorder.settings).c_str(),run.deaths);
    ImGui::SetNextItemWidth(-1);HudText::InputTextWithHint("##RunLabel","Run label",v.label,sizeof(v.label));
    ImGui::SetNextItemWidth(-1);HudText::InputTextMultiline("##RunNotes",v.notes,sizeof(v.notes),{0,70*ImGui::GetIO().FontGlobalScale});
    if(HudText::Button("Save label and notes")){run.label=v.label;run.notes=v.notes;Queue(run);}
    ImGui::SameLine();if(HudText::Button(run.starred?"Remove favourite":"Add favourite")){run.starred=!run.starred;Queue(run);}
    ItemTable(run.items,"HistoryItems");if(HudSettings::Disclosure("Route and area times"))AreaTable(run);
    if(!run.finished)HudSettings::Note("Recovered from the last autosaved snapshot after an interrupted game. The final seconds may be unavailable.");
    if(run.omitted)HudText::TextWrapped("%llu observations exceeded the recording limits.",run.omitted);
}
void History(){
    auto& s=State();auto& v=View();Filters();auto runs=Filtered();Stats(runs);
    HudSettings::Combo("Order",&v.sort,"Newest first\0Fastest first\0Most retained quantity\0",ImGui::GetIO().FontGlobalScale);
    if(HudText::Button("Export runs CSV"))Export(runs,false);ImGui::SameLine();if(HudText::Button("Export items CSV"))Export(runs,true);
    if(ImGui::BeginTable("RunHistory",5,ImGuiTableFlags_RowBg|ImGuiTableFlags_BordersInnerH|ImGuiTableFlags_Resizable|ImGuiTableFlags_ScrollY,{0,240*ImGui::GetIO().FontGlobalScale})){
        HudText::TableSetupColumn("Started (local)",0,1.7f);HudText::TableSetupColumn("Character / label",0,2);HudText::TableSetupColumn("Active");HudText::TableSetupColumn("Kept qty");HudText::TableSetupColumn("State");ImGui::TableSetupScrollFreeze(0,1);ImGui::TableHeadersRow();
        ImGuiListClipper clipper;clipper.Begin(int(runs.size()));while(clipper.Step())for(int n=clipper.DisplayStart;n<clipper.DisplayEnd;++n){const auto& run=*runs[n];ImGui::PushID(run.id.c_str());ImGui::TableNextRow();ImGui::TableNextColumn();
            if(ImGui::Selectable(Date(run.startedUtc).c_str(),v.selected==run.id,ImGuiSelectableFlags_SpanAllColumns))v.selected=run.id;
            ImGui::TableNextColumn();HudText::Text("%s%s%s",run.character.c_str(),run.label.empty()?"":" / ",run.label.c_str());ImGui::TableNextColumn();HudText::TextUnformatted(Duration(run.activeMs,s.recorder.settings).c_str());ImGui::TableNextColumn();HudText::Text("%llu",run.kept);ImGui::TableNextColumn();HudText::TextUnformatted(run.starred?"Favourite":run.finished?"Complete":"Recovered");ImGui::PopID();}ImGui::EndTable();
    }
    if(runs.empty())HudSettings::Note(s.loaded?"No runs match these filters.":"Loading run history...");
    for(auto& run:s.history)if(run.id==v.selected){ImGui::Spacing();ImGui::Separator();ImGui::Spacing();Details(run);break;}
}
void Graph(const char* id,const std::vector<float>& x,const std::vector<float>& y,bool bars,const char* xLabel,const char* yLabel,const std::vector<std::string>& labels={},bool connect=true){
    ImGui::PushID(id);const float scale=ImGui::GetIO().FontGlobalScale;const ImVec2 size{(std::max)(100.f,ImGui::GetContentRegionAvail().x),210*scale};const auto top=ImGui::GetCursorScreenPos();ImGui::InvisibleButton("Graph",size);
    const ImVec2 a{top.x+44*scale,top.y+16*scale},b{top.x+size.x-14*scale,top.y+size.y-38*scale};auto* draw=ImGui::GetWindowDrawList();const auto color=ImGui::GetColorU32(ImGuiCol_PlotLines),text=ImGui::GetColorU32(ImGuiCol_TextDisabled);
    draw->AddRectFilled(top,{top.x+size.x,top.y+size.y},ImGui::GetColorU32(ImGuiCol_FrameBg),5*scale);
    const float maxY=y.empty()?1.f:(std::max)(1.f,*std::max_element(y.begin(),y.end())*1.1f);const float maxX=x.empty()?1.f:(std::max)(1.f,*std::max_element(x.begin(),x.end()));
    for(int n=0;n<=4;++n){const float yy=b.y-(b.y-a.y)*n/4;draw->AddLine({a.x,yy},{b.x,yy},ImGui::GetColorU32(ImGuiCol_Border));char t[32]{};snprintf(t,sizeof(t),maxY<10?"%.1f":"%.0f",maxY*n/4);draw->AddText({top.x+3*scale,yy},text,t);}
    ImVec2 previous{};int hovered=-1;float nearest=15*scale;
    for(size_t i=0;i<y.size()&&i<x.size();++i){const ImVec2 p{a.x+(b.x-a.x)*x[i]/maxX,b.y-(b.y-a.y)*y[i]/maxY};
        if(bars){const float w=(std::max)(1.f,(b.x-a.x)/(y.size()+1)*.65f);draw->AddRectFilled({p.x-w*.5f,p.y},{p.x+w*.5f,b.y},color);}else{if(i&&connect)draw->AddLine(previous,p,color,2*scale);draw->AddCircleFilled(p,3*scale,color);}previous=p;
        const auto mouse=ImGui::GetIO().MousePos;const auto distance=std::abs(mouse.x-p.x);if(ImGui::IsItemHovered()&&distance<nearest){nearest=distance;hovered=int(i);}
    }
    draw->AddText({a.x,b.y+12*scale},text,HudLocale::Tr(xLabel));draw->AddText({a.x,top.y},text,HudLocale::Tr(yLabel));
    if(hovered>=0){ImGui::BeginTooltip();if(size_t(hovered)<labels.size())HudText::TextUnformatted(labels[hovered].c_str());HudText::Text("%s: %.2f",yLabel,y[hovered]);HudText::Text("%s: %.2f",xLabel,x[hovered]);ImGui::EndTooltip();}ImGui::PopID();
}
void Charts(){
    Filters();auto runs=Filtered();std::reverse(runs.begin(),runs.end());if(runs.size()>200)runs.erase(runs.begin(),runs.end()-200);Stats(runs);
    if(runs.empty()){HudSettings::Note("Complete a few runs to compare them here.");return;}
    auto& v=View();HudSettings::Combo("Trend",&v.metric,"Active minutes\0Kept quantity\0Kept / active hour\0",ImGui::GetIO().FontGlobalScale);
    std::vector<float> x,y,average,times,loot;std::vector<std::string> labels;std::array<float,10> qualities{};std::map<int,uint64_t> areas;
    for(size_t i=0;i<runs.size();++i){const auto& r=*runs[i];x.push_back(float(i+1));times.push_back(float(r.activeMs)/60000);loot.push_back(float(r.kept));y.push_back(v.metric==0?times.back():v.metric==1?loot.back():r.activeMs?float(r.kept*3600000.0/r.activeMs):0.f);float mean=0;const size_t first=i>=4?i-4:0;for(size_t n=first;n<=i;++n)mean+=y[n];average.push_back(mean/float(i-first+1));labels.push_back(Date(r.startedUtc)+" / "+r.character+" / "+r.label);for(const auto& f:r.items)if(f.item.quality>=0&&f.item.quality<10)qualities[f.item.quality]+=f.item.quantity;for(const auto& a:r.visits)areas[a.area]+=a.activeMs;}
    HudText::TextUnformatted("Run trend (latest 200 matching runs)");Graph("Trend",x,y,false,"Run (oldest to newest)",v.metric==0?"Active minutes":v.metric==1?"Kept quantity":"Kept / active hour",labels);
    if(HudSettings::Disclosure("Rolling average - last five runs"))Graph("Average",x,average,false,"Run","Rolling average");
    if(HudSettings::Disclosure("Time versus retained quantity")){
        // Scatter points stay independent; connecting them suggests a trend
        // that depends on the arbitrary order of equally timed runs.
        const auto maxTime=(std::max)(1.f,*std::max_element(times.begin(),times.end()));std::vector<float> binsX(10),binsY(10);
        for(int n=0;n<10;++n)binsX[n]=maxTime*float(n+1)/10;
        for(float t:times)++binsY[(std::min)(9,int(t/maxTime*10))];
        Graph("Durations",binsX,binsY,true,"Duration bin upper bound (minutes)","Number of runs");
        Graph("TimeLoot",times,loot,false,"Active minutes","Kept quantity",labels,false);
    }
    if(HudSettings::Disclosure("Item quality comparison")){
        float maximum=1;for(float count:qualities)maximum=(std::max)(maximum,count);
        for(int q=1;q<10;++q)if(qualities[q]>0){HudText::Text("%s: %.0f",Quality(q).c_str(),qualities[q]);ImGui::ProgressBar(qualities[q]/maximum,{-1,0},"");}
    }
    if(HudSettings::Disclosure("Time spent by area")){
        std::vector<std::pair<int,uint64_t>> sorted(areas.begin(),areas.end());std::sort(sorted.begin(),sorted.end(),[](auto a,auto b){return a.second>b.second;});
        for(const auto& [area,time]:sorted)HudText::Text("%s: %s",AreaName(area).c_str(),Duration(time,State().recorder.settings).c_str());
    }
    const auto stats=Summarize(runs);HudText::Text("Slowest: %.1f s   Standard deviation: %.1f s",stats.slowest,stats.stddev);
}
#include "RunTrackerAnalysisUI.inl"
void ClearDataControls(){
    auto& s=State();auto& v=View();const float scale=ImGui::GetIO().FontGlobalScale;
    ImGui::Spacing();ImGui::Separator();ImGui::Spacing();
    HudSettings::Note("Clear recorded run history, retained-item snapshots and chart data for all characters and mods. Timer and display settings are kept.");
    ImGui::BeginDisabled(s.clearing||s.folder.empty());
    if(HudText::Button("Clear recorded data...")){v.clearExports=false;v.clearAcknowledged=false;HudText::OpenPopup("Clear Run Tracker data?###ClearRunTrackerData");}
    ImGui::EndDisabled();
    const auto display=ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowSize({(std::min)(540*scale,(std::max)(200.f,display.x-40*scale)),0},ImGuiCond_Always);
    if(HudText::BeginPopupModal("Clear Run Tracker data?###ClearRunTrackerData",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
        HudText::TextWrapped("This permanently deletes ALL saved Run Tracker history for every character and mod, including older runs outside the load limit. Labels, notes, retained-item snapshots and their charts will be cleared.");
        HudText::TextWrapped("The current run is discarded. If one is active, a fresh run starts using your existing possessions as its baseline.");
        ImGui::Spacing();HudText::Checkbox("Also delete exported Run Tracker CSV files",&v.clearExports);
        HudSettings::Note("Your timer/display settings, character and stash saves, and marketplace data are kept.");
        ImGui::Spacing();HudText::Checkbox("I understand this cannot be undone",&v.clearAcknowledged);ImGui::Spacing();
        if(HudText::Button("Cancel"))ImGui::CloseCurrentPopup();ImGui::SameLine();
        ImGui::BeginDisabled(!v.clearAcknowledged||s.clearing);
        if(HudText::Button("Clear data permanently")){RefreshNow();RequestClearHistory(v.clearExports);v.selected.clear();v.edited.clear();v.label[0]=v.notes[0]=0;ImGui::CloseCurrentPopup();}
        ImGui::EndDisabled();ImGui::EndPopup();
    }
}
void Preferences(){
    auto& s=State();auto& p=s.recorder.settings;const auto previous=p;const float scale=ImGui::GetIO().FontGlobalScale;bool changed=false;
    HudSettings::BeginSection("RunTiming","Run timing",nullptr,scale);
    changed|=HudText::Checkbox("Start a run automatically for each game",&p.autoStart);
    changed|=HudText::Checkbox("Freeze timer in town",&p.freezeTown);
    changed|=HudText::Checkbox("Freeze timer during loading screens",&p.freezeLoading);
    changed|=HudSettings::Combo("Timer format",&p.timeFormat,"Minutes:seconds\0Hours:minutes:seconds\0Seconds\0",scale);
    changed|=HudText::Checkbox("Show milliseconds",&p.milliseconds);HudSettings::EndSection(scale);
    HudSettings::BeginSection("RunOverlay","Live overlay",nullptr,scale);
    changed|=HudText::Checkbox("Show Run Tracker overlay",&p.overlay);changed|=HudText::Checkbox("Lock overlay position",&p.overlayLocked);
    changed|=HudText::Checkbox("Show retained item count",&p.overlayItems);changed|=HudText::Checkbox("Show current area",&p.overlayArea);changed|=HudText::Checkbox("Show total elapsed time",&p.overlayWall);
    changed|=HudSettings::Slider("Overlay text scale",&p.overlayScale,.65f,2.f,"%.2f",scale);changed|=HudSettings::Slider("Overlay opacity",&p.overlayOpacity,.1f,1.f,"%.2f",scale);
    if(HudText::Button("Reset overlay position"))View().resetOverlay=true;HudSettings::EndSection(scale);
    HudSettings::BeginSection("RunStorage","History and storage",nullptr,scale);
    changed|=HudSettings::SliderInt("Recent runs loaded",&p.historyLimit,100,10000,scale);
    HudSettings::Note("Older run files remain on disk. The load limit applies on the next game launch. History and snapshots save locally every five seconds and when a run finishes.");
    HudSettings::Note("Only owned inventory and loaded stash containers are compared. Open the stash pages you want included. An unavailable or partially read container never means its items were deleted. Ground drops are not recorded.");
    HudText::TextWrapped("Folder: %s",s.folder.string().c_str());if(HudText::Button("Copy history folder"))ImGui::SetClipboardText(s.folder.string().c_str());
    HudText::TextWrapped("Preferences: %s",HudUserConfig::Path(s.preferencesRoot).string().c_str());
    ClearDataControls();
    HudSettings::EndSection(scale);if(changed){const auto selected=p;p=previous;RefreshNow();p=selected;SaveSettings();}
}
}
void DrawPanel(){
    Initialize();auto& s=State();s.historyRequested=true;ImGui::BeginChild("RunTrackerContents",{0,0},0,ImGuiWindowFlags_NoBackground);
    ImGui::BeginDisabled(s.clearing);
    bool enabled=s.recorder.settings.enabled;
    if(HudText::Checkbox("Enable Run Tracker",&enabled)){RefreshNow();s.recorder.settings.enabled=enabled;SaveSettings();if(!enabled){RefreshNow();Collect();}}
    if(!s.status.empty())HudText::TextWrapped("%s",s.status.c_str());
    if(s.unreadable)HudText::TextWrapped("%zu unreadable or newer-version records were left unchanged.",s.unreadable);
    if(s.older)HudText::TextDisabled("%zu older files remain on disk outside the current load limit.",s.older);
    ImGui::Spacing();if(ImGui::BeginTabBar("RunTrackerTabs")){
        if(HudText::BeginTabItem("Live Run")){Live();ImGui::EndTabItem();}
        if(HudText::BeginTabItem("History")){History();ImGui::EndTabItem();}
        if(HudText::BeginTabItem("Charts")){Charts();ImGui::EndTabItem();}
        if(HudText::BeginTabItem("Item Analysis")){ItemAnalysis();ImGui::EndTabItem();}
        if(HudText::BeginTabItem("Settings")){Preferences();ImGui::EndTabItem();}
        ImGui::EndTabBar();
    }ImGui::EndDisabled();ImGui::EndChild();
}
void DrawOverlay(){
    if(!Enabled())return;auto& s=State();auto& p=s.recorder.settings;if(s.clearing||!p.overlay||!s.recorder.current||!s.recorder.Epoch())return;
    const auto& run=*s.recorder.current;const auto& f=s.recorder.Context();
    const auto display=ImGui::GetIO().DisplaySize;const bool reset=std::exchange(View().resetOverlay,false);
    ImGui::SetNextWindowPos({display.x-24*ImGui::GetIO().FontGlobalScale,70*ImGui::GetIO().FontGlobalScale},reset?ImGuiCond_Always:ImGuiCond_FirstUseEver,{1,0});
    ImGui::SetNextWindowBgAlpha(p.overlayOpacity);
    auto flags=ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoFocusOnAppearing|ImGuiWindowFlags_NoNav;
    if(p.overlayLocked)flags|=ImGuiWindowFlags_NoInputs|ImGuiWindowFlags_NoMove;
    if(HudText::Begin("Run Tracker###RunTrackerOverlay",nullptr,flags|ImGuiWindowFlags_NoTitleBar)){
        ImGui::SetWindowFontScale(p.overlayScale);
        const auto title=HudLocale::Tr("Run Tracker");const auto start=ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##MoveTimer",{ImGui::CalcTextSize(title).x+32*ImGui::GetIO().FontGlobalScale,ImGui::GetTextLineHeight()});
        ImGui::GetWindowDrawList()->AddText(start,ImGui::GetColorU32(ImGuiCol_TextDisabled),title);
        if(!p.overlayLocked&&ImGui::IsItemActive()&&ImGui::IsMouseDragging(ImGuiMouseButton_Left)){
            const auto pos=ImGui::GetWindowPos(),delta=ImGui::GetIO().MouseDelta,size=ImGui::GetWindowSize();
            ImGui::SetWindowPos({(std::clamp)(pos.x+delta.x,0.f,(std::max)(0.f,display.x-size.x)),(std::clamp)(pos.y+delta.y,0.f,(std::max)(0.f,display.y-size.y))});
        }
        if(!p.overlayLocked&&ImGui::IsItemHovered())ImGui::SetTooltip("%s",HudLocale::Tr("Drag to move the timer. Lock its position in Run Tracker Settings."));
        HudText::TextColored({.91f,.78f,.49f,1},"%s",Duration(run.activeMs,p).c_str());
        if(s.recorder.paused)HudText::TextDisabled("Paused");else if((p.freezeTown&&f.town)||(p.freezeLoading&&f.loading))HudText::TextDisabled(f.loading?"Loading":"In town");
        if(p.overlayItems)HudText::Text("Kept: %llu",run.kept);if(p.overlayArea)HudText::TextUnformatted(f.areaName.c_str());if(p.overlayWall)HudText::Text("Total: %s",Duration(run.wallMs,p).c_str());
    }ImGui::End();
}
