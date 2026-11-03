namespace LootFilterConfig::Simulator::UI {
struct StatRow {int stat=0,param=0,value=0;};
inline bool Stats(const char* id,std::vector<StatRow>& rows,const Builder::Catalog& catalog){
    ImGui::PushID(id);bool changed=false;
    for(size_t n=0;n<rows.size();++n){
        ImGui::PushID(int(n));auto& row=rows[n];
        std::string selected=std::to_string(row.stat);
        if(Builder::UI::Choose("##stat",selected,catalog.stats)){Builder::Integer(selected,row.stat);changed=true;}
        const float width=(std::max)(60.f,(ImGui::GetContentRegionAvail().x-85)*.5f);
        ImGui::AlignTextToFramePadding();HudText::TextUnformatted("Value");ImGui::SameLine();ImGui::SetNextItemWidth(width);changed|=HudText::InputInt("##value",&row.value,0,0);
        ImGui::SameLine();if(HudText::Button("Remove")){rows.erase(rows.begin()+n);changed=true;ImGui::PopID();break;}
        HudText::TextUnformatted("Parameter");ImGui::SameLine();ImGui::SetNextItemWidth(width);changed|=HudText::InputInt("##param",&row.param,0,0);row.param=std::clamp(row.param,0,65535);
        ImGui::Spacing();ImGui::PopID();
    }
    if(HudText::Button("Add stat")){rows.push_back({});changed=true;}ImGui::PopID();return changed;
}
inline void Render(const std::string& source,const Builder::Catalog& catalog,const std::string& engine,const std::map<std::string,std::string>& overrides){
    static Preview::Session session;
    static Preview::Context context=[](){Preview::Context value;value.filterLevel=0;value.playerStats.clear();return value;}();
    static Sample item;static std::string selection,observed;static std::vector<StatRow> itemStats,playerStats;
    static bool showAll=false;static double lastAnimation=-1;
    bool changed=false;
    static const Builder::Catalog* catalogSeen=nullptr;static size_t itemCount=0,namedCount=0;
    static std::vector<Builder::Option> options;
    if(catalogSeen!=&catalog||itemCount!=catalog.items.size()||namedCount!=catalog.namedItems.size()){
        catalogSeen=&catalog;itemCount=catalog.items.size();namedCount=catalog.namedItems.size();options=Items(catalog);changed=true;
        if(!selection.empty())Select(catalog,selection,item);
    }
    if(selection.empty()&&!options.empty()){
        auto ring=std::find_if(options.begin(),options.end(),[](const auto& option){return option.value=="base:rin";});
        selection=ring==options.end()?options.front().value:ring->value;
        changed=Select(catalog,selection,item);
    }
    const float available=ImGui::GetContentRegionAvail().x;
    const bool columns=available>=ImGui::GetFontSize()*43&&ImGui::BeginTable("SimulatorColumns",2,ImGuiTableFlags_Resizable|ImGuiTableFlags_SizingStretchProp);
    if(columns){ImGui::TableSetupColumn("Item",ImGuiTableColumnFlags_WidthFixed,available*.38f);ImGui::TableSetupColumn("Result",ImGuiTableColumnFlags_WidthStretch);ImGui::TableNextColumn();}
    HudText::SeparatorText("Choose an item");
    if(!catalog.loaded)HudText::TextDisabled("Loading the mod's item list...");
    if(!catalog.status.empty())HudText::TextWrapped("%s",catalog.status.c_str());
    HudText::TextUnformatted("Search base, unique and set items");
    if(Builder::UI::Choose("##simulation_item",selection,options)){changed=Select(catalog,selection,item);itemStats.clear();}
    if(item.code.empty()){
        HudText::TextWrapped("An item catalog is needed to run the simulator.");if(columns)ImGui::EndTable();return;
    }
    auto number=[&](const char* label,const char* id,int& value,int minimum,int maximum){HudText::TextUnformatted(label);ImGui::SetNextItemWidth(-1);changed|=HudText::InputInt(id,&value);value=std::clamp(value,minimum,maximum);};
    ImGui::Spacing();
    if(HudText::CollapsingHeader("Item properties",columns?ImGuiTreeNodeFlags_DefaultOpen:0)){
    HudText::TextUnformatted("Quality");ImGui::SetNextItemWidth(-1);
    if(HudText::Combo("##sim_quality",&item.quality,Preview::Qualities,10)){
        changed=true;
        selection="base:"+item.code;item.index=-1;const auto base=catalog.bases.find(item.code);if(base!=catalog.bases.end())item.name=base->second.name;
    }

    number("Item level","##sim_ilvl",item.level,1,999);
    number("Sockets","##sim_sockets",item.sockets,0,255);
    changed|=HudText::Checkbox("Identified",&item.identified);
    changed|=HudText::Checkbox("Ethereal",&item.ethereal);changed|=HudText::Checkbox("Runeword",&item.runeword);
    if(item.quality==5||item.quality==7)changed|=HudText::Checkbox("Already collected for grail",&item.collected);
    if(HudText::CollapsingHeader("Item stats"))changed|=Stats("ItemStats",itemStats,catalog);
    if((item.quality==5||item.quality==7)&&item.index<0)HudText::TextWrapped("Choose a named unique or set item above to check its exact identity.");
    }
    ImGui::Spacing();if(HudText::CollapsingHeader("Drop context")){
    int location=context.location==-1?3:context.location==3?0:context.location==0?1:2;
    HudText::TextUnformatted("Location");ImGui::SetNextItemWidth(-1);
    if(HudText::Combo("##sim_location",&location,"Ground\0Inventory / storage\0Equipped\0Vendor\0")){context.location=location==0?3:location==1?0:location==2?1:-1;changed=true;}
    HudText::TextUnformatted("Difficulty");ImGui::SetNextItemWidth(-1);changed|=HudText::Combo("##sim_difficulty",&context.difficulty,"Normal\0Nightmare\0Hell\0");
    bool active=context.filterLevel==0;
    if(HudText::Checkbox("Use filter's active level",&active)){context.filterLevel=active?0:1;changed=true;}
    if(!active)number("Filter level","##sim_filterlevel",context.filterLevel,1,999);
    if(HudText::CollapsingHeader("Area and player")){
        number("Player level","##sim_playerlevel",context.playerLevel,1,999);
        HudText::TextUnformatted("Area");std::string area=std::to_string(context.area);
        if(Builder::UI::Choose("##sim_area",area,catalog.areas)){Builder::Integer(area,context.area);changed=true;}
        if(HudText::TreeNode("Player stats")){changed|=Stats("PlayerStats",playerStats,catalog);ImGui::TreePop();}
    }
    }
    item.stats.clear();for(const auto& stat:itemStats)item.stats[{stat.stat,stat.param}]=stat.value;
    context.playerStats.clear();for(const auto& stat:playerStats)context.playerStats[{stat.stat,stat.param}]=stat.value;
    auto sample=item;
    if(!sample.identified){auto base=catalog.bases.find(sample.code);if(base!=catalog.bases.end())sample.name=base->second.name;}
    std::string key=source+'\n'+selection+'\n'+engine;
    for(const auto& file:overrides)key+='\n'+file.first+'='+file.second;
    key+='\n'+std::to_string(catalog.bases.size())+":"+std::to_string(catalog.namedItems.size());
    if(HudText::Button("Run simulation"))changed=true;
    HudText::TextWrapped("Uses the current editor draft. Results apply to these properties; unspecified item and player stats use 0.");
    if(changed||observed!=key){observed=key;session.BuildFilter(engine,source,sample,context,overrides);lastAnimation=-1;}
    if(columns)ImGui::TableNextColumn();else{ImGui::Spacing();ImGui::Separator();ImGui::Spacing();}
    HudText::SeparatorText("Filter result");
    if(!session.error.empty()){
        HudText::TextColored({1,.55f,.35f,1},"Simulation could not complete");
        HudText::TextWrapped("%s",session.error.c_str());HudText::TextWrapped("Visibility is unknown until the filter can be evaluated.");
    }else if(!session.results.empty()){
        const double now=ImGui::GetTime();if(lastAnimation<0||now-lastAnimation>=.1){session.Animate(unsigned(now*1000));lastAnimation=now;}
        if(!session.error.empty())HudText::TextWrapped("Simulation effect failed: %s",session.error.c_str());
        else{
            const auto& result=session.results[0];
            HudText::TextColored(result.hidden?ImVec4(1,.4f,.35f,1):ImVec4(.4f,.9f,.5f,1),result.hidden?"HIDDEN by this filter":"VISIBLE with these properties");
            HudText::TextWrapped("%s",result.explanation.c_str());
            HudText::TextDisabled("Filter level %d",result.filterLevel);ImGui::Spacing();
            Preview::UI::Card(0,session.samples[0],result,ImGui::GetContentRegionAvail().x,catalog,true);
            ImGui::Spacing();HudText::SeparatorText("Rules affecting this item");
            HudText::Checkbox("Show skipped and non-matching rules",&showAll);
            size_t shown=0;
            for(size_t i=0;i<result.rules.size();++i){
                const auto& rule=result.rules[i];const bool affects=rule.status=="matched"||rule.status=="hidden";
                if(!showAll&&!affects)continue;++shown;ImGui::PushID(int(i));
                const std::string title="#"+std::to_string(rule.number)+"  "+(rule.label.empty()?"Untitled rule":rule.label);
                HudText::TextColored(rule.status=="hidden"?ImVec4(1,.5f,.35f,1):affects?ImVec4(.8f,.72f,.45f,1):ImVec4(.65f,.65f,.65f,1),"%s",title.c_str());
                HudText::TextWrapped("%s / %s",rule.source=="config"?"Filter":rule.source.c_str(),rule.status.c_str());
                if(!rule.reason.empty())HudText::TextWrapped("%s",Condition(rule.reason).c_str());
                if(!rule.conditions.empty()&&HudText::TreeNode("Conditions")){
                    for(const auto& condition:rule.conditions){auto spec=Builder::FindSpec(condition.first);HudText::TextWrapped("%s: %s",spec?spec->label:condition.first.c_str(),condition.second.c_str());}
                    ImGui::TreePop();
                }
                if(affects&&!rule.actions.empty()){
                    std::string actions;for(const auto& action:rule.actions){if(!actions.empty())actions+=", ";actions+=Action(action);}
                    HudText::TextWrapped("%s",actions.c_str());
                }
                ImGui::Spacing();ImGui::Separator();ImGui::Spacing();ImGui::PopID();
            }
            if(!shown)HudText::TextWrapped("No individual rule matched. The filter's unmatched-item setting determines visibility.");
            if(!result.messages.empty()){HudText::SeparatorText("Chat output");HudText::TextWrapped("%s",Preview::UI::Plain(result.messages).c_str());}
            if(result.audio)HudText::TextWrapped("A sound or speech action would run. Simulation does not play audio.");
            if(!result.warnings.empty()){HudText::SeparatorText("Filter diagnostics");for(const auto& warning:result.warnings)HudText::TextWrapped("%s",warning.c_str());}
        }
    }
    if(columns)ImGui::EndTable();
}
}
