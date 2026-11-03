// Included in the Run Tracker presentation namespace. File access, catalog
// parsing and report calculations run on owned background jobs.
void PollAnalysisSources(bool refresh){
    auto& s=State();auto& view=View();const auto& context=s.recorder.Context();const bool hc=view.marketMode==2||(view.marketMode==0&&context.hardcore);
    const auto key=context.mod+"/"+std::to_string(hc);
    if(key!=s.analysisSourceKey||refresh){s.analysisSourceKey=key;++s.sourceGeneration;s.sourceReady=s.reportReady=false;s.analysisError.clear();}
    if(auto completion=s.analysisLoad.Poll();completion&&completion->ticket==s.sourceGeneration){
        if(completion->value){s.analysisSource=std::make_shared<Analysis::Sources>(std::move(*completion->value));s.sourceReady=true;++s.sourceRevision;}
        else s.analysisError="Item analysis sources could not be loaded. Use Refresh analysis sources to retry.";
    }
    if(!s.sourceReady&&!s.analysisLoad.Busy()&&s.sourceAttempted!=s.sourceGeneration){
        s.sourceAttempted=s.sourceGeneration;
        try{wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,DWORD(std::size(executable)));const auto install=std::filesystem::path(executable).parent_path();const auto folder=s.marketFolder?s.marketFolder():std::filesystem::path{};const auto mod=context.mod;const auto now=context.utc;
            if(!s.analysisLoad.Start(s.sourceGeneration,[install,folder,mod,hc,now]{return Analysis::Load(install,mod,folder,hc,now);}))s.analysisError="The analysis worker could not start. Use Refresh analysis sources to retry.";
        }catch(const std::exception& e){s.analysisError=e.what();}
    }
}
void ShareChart(const char* id,const std::vector<Analysis::Share>& shares){
    const float scale=ImGui::GetIO().FontGlobalScale;ImGui::PushID(id);
    const float height=(1+float((std::min)(shares.size(),size_t(8))))*ImGui::GetFrameHeightWithSpacing()+8*scale;
    if(ImGui::BeginTable("Shares",3,ImGuiTableFlags_RowBg|ImGuiTableFlags_BordersInnerH|ImGuiTableFlags_ScrollY|ImGuiTableFlags_Resizable,{0,height})){
        HudText::TableSetupColumn("Type",0,2);HudText::TableSetupColumn("Count");HudText::TableSetupColumn("% of total",0,2);ImGui::TableSetupScrollFreeze(0,1);ImGui::TableHeadersRow();
        ImGuiListClipper clipper;clipper.Begin(int(shares.size()));while(clipper.Step())for(int i=clipper.DisplayStart;i<clipper.DisplayEnd;++i){const auto& row=shares[i];ImGui::TableNextRow();ImGui::TableNextColumn();HudText::TextUnformatted(row.name.c_str());ImGui::TableNextColumn();HudText::Text("%llu",row.count);ImGui::TableNextColumn();char label[40]{};snprintf(label,sizeof(label),"%.1f%%",row.percent);ImGui::ProgressBar(float(row.percent/100),{-1,0},label);}ImGui::EndTable();
    }ImGui::PopID();
}
void PollAnalysisReport(){
    auto& s=State();if(!s.sourceReady)return;const auto& p=s.recorder.settings;const auto& view=View();
    const auto key=std::to_string(s.sourceRevision)+"/"+std::to_string(s.revision)+"/"+std::to_string(view.period)+"/"+std::to_string(view.thisMod)+std::to_string(view.thisCharacter)+std::to_string(view.stars)+std::to_string(view.recovered)+std::to_string(view.includeLive)+"/"+view.search+"/"+s.recorder.Context().character+"/"+p.analysisCurrency+"/"+p.analysisTreasure+"/"+std::to_string(p.analysisUnits)+std::to_string(p.analysisBroad)+"/"+std::to_string(p.analysisDays)+"/"+std::to_string(p.analysisSamples)+"/"+std::to_string(s.recorder.Context().utc/60);
    if(key!=s.analysisReportKey){s.analysisReportKey=key;++s.reportGeneration;s.reportReady=false;}
    if(auto completion=s.analysisWork.Poll();completion&&completion->ticket==s.reportGeneration){if(completion->value){s.analysisReport=std::move(*completion->value);s.reportReady=true;}else s.analysisError="Item analysis could not be calculated. Change a filter or refresh sources to retry.";}
    if(!s.reportReady&&!s.analysisWork.Busy()&&s.reportAttempted!=s.reportGeneration){
        s.reportAttempted=s.reportGeneration;auto runs=Filtered();
        if(view.includeLive&&s.recorder.current&&view.period!=1){const auto& r=*s.recorder.current;if((!view.stars||r.starred)&&Contains(r.label+" "+r.character+" "+r.notes+" "+r.mod,view.search))runs.push_back(&r);}
        std::sort(runs.begin(),runs.end(),[](const Run* a,const Run* b){return a->startedUtc>b->startedUtc;});auto input=Analysis::Gather(runs);
        Analysis::Options options;options.units=p.analysisUnits;options.broadQualities=p.analysisBroad;options.days=p.analysisDays;options.minSamples=p.analysisSamples;options.currency=p.analysisCurrency;options.treasure=p.analysisTreasure;
        const auto source=s.analysisSource;const auto now=s.recorder.Context().utc;
        if(!s.analysisWork.Start(s.reportGeneration,[input=std::move(input),source,options,now]{return Analysis::Build(input,*source,options,now);}))s.analysisError="The item analysis worker could not start.";
    }
}
void AnalysisAreas(const Analysis::Report& report,const std::string& currency){
    auto& v=View();auto& p=State().recorder.settings;
    HudSettings::Combo("Rank areas by",&v.areaMetric,"Retained entries\0Retained units\0Entries / active hour\0Estimated value\0Estimated value / active hour\0",ImGui::GetIO().FontGlobalScale);
    if(HudText::Checkbox("Include town areas",&p.analysisTown))SaveSettings();
    auto rows=report.areas;std::erase_if(rows,[&](const Analysis::Area& row){return row.id<=0||(!p.analysisTown&&Town(row.id));});
    auto metric=[&](const Analysis::Area& row){switch(v.areaMetric){case 1:return double(row.units);case 2:return row.activeMs?row.entries*3600000.0/row.activeMs:-1.;case 3:return row.priced?row.estimate:-1.;case 4:return row.priced&&row.activeMs?row.estimate*3600000/row.activeMs:-1.;default:return double(row.entries);}};
    std::sort(rows.begin(),rows.end(),[&](const auto& a,const auto& b){return metric(a)>metric(b);});
    // The rank bars remain readable in narrow HUD panes; detail columns only
    // appear when the available width can hold their headings comfortably.
    const auto best=rows.empty()?0.:(std::max)(0.,metric(rows.front()));
    const float scale=ImGui::GetIO().FontGlobalScale;
    for(size_t n=0;n<(std::min)(rows.size(),size_t(5));++n){const auto& row=rows[n];const auto value=metric(row);
        ImGui::PushID(int(n));HudText::TextColored({.91f,.78f,.49f,1},"%zu. %s",n+1,AreaName(row.id).c_str());char number[64]{};if(value>=0)snprintf(number,sizeof(number),"%.2f",value);else strcpy_s(number,"No comparable data");ImGui::ProgressBar(best>0&&value>=0?float(value/best):0.f,{-1,0},number);
        HudText::TextDisabled("%llu entries / %llu units | %zu visits | %s active",row.entries,row.units,row.visits,Duration(row.activeMs,State().recorder.settings).c_str());
        if(row.priced)HudText::TextDisabled("Est. value %.2f | %.0f%% priced",row.estimate,row.entries?row.priced*100.0/row.entries:0.);ImGui::Dummy({0,5*scale});ImGui::PopID();
    }
    if(ImGui::GetContentRegionAvail().x>=850*scale&&ImGui::BeginTable("AreaReturns",6,ImGuiTableFlags_RowBg|ImGuiTableFlags_BordersInnerH|ImGuiTableFlags_Resizable|ImGuiTableFlags_ScrollY,{0,240*scale})){
        HudText::TableSetupColumn("Area",0,2);HudText::TableSetupColumn("Entries / units");HudText::TableSetupColumn("Entries / h");HudText::TableSetupColumn("Est. value");HudText::TableSetupColumn("Est. value / h");HudText::TableSetupColumn("Priced %");ImGui::TableSetupScrollFreeze(0,1);ImGui::TableHeadersRow();
        for(const auto& row:rows){ImGui::TableNextRow();ImGui::TableNextColumn();HudText::TextUnformatted(AreaName(row.id).c_str());if(ImGui::IsItemHovered())ImGui::SetTooltip("%zu visits | %s active",row.visits,Duration(row.activeMs,State().recorder.settings).c_str());
            ImGui::TableNextColumn();HudText::Text("%llu / %llu",row.entries,row.units);ImGui::TableNextColumn();if(row.activeMs)HudText::Text("%.1f",row.entries*3600000.0/row.activeMs);else HudText::TextDisabled("--");
            ImGui::TableNextColumn();if(row.priced)HudText::Text("%.2f",row.estimate);else HudText::TextDisabled("--");ImGui::TableNextColumn();if(row.priced&&row.activeMs)HudText::Text("%.2f",row.estimate*3600000/row.activeMs);else HudText::TextDisabled("--");ImGui::TableNextColumn();HudText::Text("%.0f%%",row.entries?row.priced*100.0/row.entries:0.);
        }ImGui::EndTable();
    }
    HudText::TextWrapped("Value currency: %s. Returns use first-observed acquisition areas and active area time. Unpriced items are excluded from estimated totals.",currency.c_str());
    HudSettings::Note("Items first noticed after returning to town, crafting and trades are not proven monster drops. Rates describe these recorded acquisitions; they are not expected future income.");
}
void AnalysisValuables(const Analysis::Report& report,const std::string& currency){
    HudText::Text("Currency: %s",currency.c_str());
    if(ImGui::BeginTable("TopValue",4,ImGuiTableFlags_RowBg|ImGuiTableFlags_BordersInnerH|ImGuiTableFlags_Resizable)){
        HudText::TableSetupColumn("Retained item",0,3);HudText::TableSetupColumn("Median / unit");HudText::TableSetupColumn("Est. total");HudText::TableSetupColumn("Sales");ImGui::TableHeadersRow();
        for(const auto& row:report.valuable){ImGui::TableNextRow();ImGui::TableNextColumn();HudText::Text("%s x%d",row.entry.item.name.c_str(),row.entry.item.quantity);if(ImGui::IsItemHovered())ImGui::SetTooltip("%s | %s | %s",row.entry.character.c_str(),AreaName(row.entry.area).c_str(),Date(row.entry.utc).c_str());ImGui::TableNextColumn();HudText::Text("%.2f",row.unit);ImGui::TableNextColumn();HudText::Text("%.2f",row.total);ImGui::TableNextColumn();HudText::Text("%zu",row.samples);}ImGui::EndTable();
    }
    if(report.valuable.empty())HudSettings::Note("No retained items meet the selected currency, matching criteria and minimum sale count.");
    HudSettings::Note("Comparable estimates match base code, quality, named-item index, ethereal flag and occupied socket count. Individual rolls and total socket capacity can differ. Stackable sales require an explicit sold quantity; unknown older metadata stays unpriced.");
}
void AnalysisRarity(const Analysis::Report& report){
    if(ImGui::BeginTable("Rarest",3,ImGuiTableFlags_RowBg|ImGuiTableFlags_BordersInnerH|ImGuiTableFlags_Resizable)){
        HudText::TableSetupColumn("Retained item",0,3);HudText::TableSetupColumn("Base code");HudText::TableSetupColumn("Relative weight");ImGui::TableHeadersRow();
        for(const auto& row:report.rare){ImGui::TableNextRow();ImGui::TableNextColumn();HudText::TextUnformatted(row.entry.item.name.c_str());ImGui::TableNextColumn();HudText::TextUnformatted(row.entry.item.code.c_str());ImGui::TableNextColumn();HudText::Text("%.7g",row.weight);}ImGui::EndTable();
    }
    if(!report.rarityComplete)HudSettings::Note("This tree contains unsupported, generated, cyclic or deterministic-pick branches. Incomplete weights are withheld. Choose another explicit treasure class.");
    if(report.rare.empty())HudSettings::Note("No recorded base items have a complete reachable path in this reference tree.");
    HudSettings::Note("Smaller weights indicate rarer base selections in this reference model. One weighted draw is used at each node. Picks, quality/Magic Find rolls, monster level, player-count NoDrop changes and event overrides are not simulated; this is not a per-kill chance. Unique and normal versions share the same base weight.");
}
void ItemAnalysis(){
    auto& s=State();auto& v=View();auto& p=s.recorder.settings;const float scale=ImGui::GetIO().FontGlobalScale;
    Filters();HudText::Checkbox("Include current run",&v.includeLive);
    HudSettings::Combo("Marketplace mode",&v.marketMode,"Current character\0Softcore\0Hardcore\0",scale);
    const bool refresh=HudText::Button("Refresh analysis sources");PollAnalysisSources(refresh);
    if(!s.analysisError.empty())HudText::TextWrapped("%s",s.analysisError.c_str());if(!s.sourceReady){HudSettings::Note("Loading item categories, treasure classes and cached completed sales...");return;}
    const auto& source=*s.analysisSource;bool changed=false;
    changed|=HudText::Checkbox("Count stack quantities instead of entries",&p.analysisUnits);
    if(HudSettings::Disclosure("Valuation and rarity settings")){
        HudSettings::BeginField("Payment currency",scale);
        const auto selected=source.currencies.find(p.analysisCurrency);const auto label=selected==source.currencies.end()?"Choose a payment currency":selected->second.c_str();
        if(HudText::BeginCombo("##Currency",label)){for(const auto& [key,name]:source.currencies)if(HudText::Selectable((name+"##"+key).c_str(),key==p.analysisCurrency)){p.analysisCurrency=key;changed=true;}ImGui::EndCombo();}HudSettings::EndField(scale);
        changed|=HudSettings::SliderInt("Completed-sale window (days)",&p.analysisDays,1,365,scale);
        changed|=HudSettings::SliderInt("Minimum comparable sales",&p.analysisSamples,1,100,scale);
        changed|=HudText::Checkbox("Include broad magic, rare and crafted comparisons",&p.analysisBroad);
        HudSettings::BeginField("Rarity reference treasure class",scale);
        if(HudText::BeginCombo("##ReferenceTC",p.analysisTreasure.empty()?"Choose a treasure class":p.analysisTreasure.c_str())){
            HudText::InputTextWithHint("##TCSearch","Search treasure classes",v.treasureSearch,sizeof(v.treasureSearch));
            if(HudText::Selectable("No rarity comparison",p.analysisTreasure.empty())){p.analysisTreasure.clear();changed=true;}
            for(const auto& [name,tc]:source.treasures)if(Contains(name,v.treasureSearch)&&HudText::Selectable(name.c_str(),name==p.analysisTreasure)){p.analysisTreasure=name;changed=true;}ImGui::EndCombo();
        }HudSettings::EndField(scale);
    }
    if(changed)SaveSettings();
    HudSettings::Note(source.catalogStatus.c_str());HudSettings::Note(source.marketStatus.c_str());
    if(p.analysisCurrency.empty()&&!source.currencies.empty())HudSettings::Note("Choose a payment currency in Valuation and rarity settings to enable value rankings.");
    if(source.ignoredSales)HudText::TextDisabled("%zu free, bundle, mixed-currency or unusable sale records excluded.",source.ignoredSales);
    PollAnalysisReport();if(!s.reportReady){HudSettings::Note("Calculating item comparisons...");return;}const auto& report=s.analysisReport;
    HudText::Text("%zu runs | %zu retained entries | %llu units",report.runs,report.entries,report.units);
    HudText::Text("Priced entries: %zu / %zu (%.1f%%)",report.entries-report.unpriced,report.entries,report.entries?(report.entries-report.unpriced)*100.0/report.entries:0.);
    if(report.truncated)HudSettings::Note("Analysis is limited to the newest 500 matching runs and 100,000 retained entries. Run files remain unchanged.");
    const auto currency=source.currencies.contains(p.analysisCurrency)?source.currencies.at(p.analysisCurrency):"No currency selected";
    if(ImGui::BeginTabBar("ItemAnalysisViews")){
        if(HudText::BeginTabItem("Composition")){HudText::TextColored({.91f,.78f,.49f,1},"Item types");ShareChart("Types",report.categories);ImGui::Dummy({0,10*scale});HudText::TextColored({.91f,.78f,.49f,1},"Item qualities");ShareChart("Qualities",report.qualities);ImGui::EndTabItem();}
        if(HudText::BeginTabItem("Area returns")){AnalysisAreas(report,currency);ImGui::EndTabItem();}
        if(HudText::BeginTabItem("Top 10 value")){AnalysisValuables(report,currency);ImGui::EndTabItem();}
        if(HudText::BeginTabItem("Top 10 rarity")){HudSettings::Note(source.treasureStatus.c_str());AnalysisRarity(report);ImGui::EndTabItem();}
        ImGui::EndTabBar();
    }
}
