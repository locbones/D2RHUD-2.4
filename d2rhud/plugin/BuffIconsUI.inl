void DrawSettingsPanel(float scale) {
    namespace UI=HudSettings;
    UI::Style form(scale);
    HudText::Checkbox("Enable buff icons",&config.enabled);
    ImGui::Dummy({0,6*scale});
    auto skills=[&]{
        if(!loaded){UI::Note("Loading skill data and original icon sprites...");return;}
        if(!database.error.empty()){
            HudText::TextWrapped("%s",database.error.c_str());
            if(HudText::Button("Retry loading game data")){loaded=false;loadStarted=false;}
            return;
        }
        if(character.empty()){UI::Note("Enter a game to choose your character's available buff skills.");return;}
        UI::Input("Find a skill",search,sizeof(search),scale);
        auto& selection=config.characters[character];
        if(HudText::Button("Select available"))for(const auto& skill:available)selection.insert(skill.id);
        ImGui::SameLine();if(HudText::Button("Clear selection"))selection.clear();
        ImGui::BeginChild("Buff skill choices",{0,260*scale},ImGuiChildFlags_Borders);
    const auto filter = Lower(search);
    if (available.empty()) HudText::TextWrapped("No available skills with a buff state were found.");
    for (const auto& live : available) {
        const auto& skill = database.skills.at(live.id);
        if (!filter.empty() && Lower(skill.name).find(filter) == std::string::npos) continue;
        ImGui::PushID(live.id);
        bool selected = selection.count(live.id) != 0;
        if (HudText::Checkbox("##selected", &selected)) {
            if (selected) selection.insert(live.id); else selection.erase(live.id);
        }
        ImGui::SameLine();
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        DrawIcon(ImGui::GetWindowDrawList(), skill, pos, 28 * scale, -1, true, false);
        ImGui::Dummy(ImVec2(28 * scale, 28 * scale)); ImGui::SameLine();
        HudText::Text("%s (level %d)%s", skill.name.c_str(), live.level, live.active ? " - active" : "");
        if (database.atlases.find(skill.sprite) == database.atlases.end() && ImGui::IsItemHovered())
            HudText::SetTooltip("Icon sprite unavailable; the skill ID is shown as a fallback.");
        ImGui::PopID();
    }

        ImGui::EndChild();UI::Note(runtimeStatus.c_str());
    };
    if(ImGui::BeginTabBar("BuffSettingsSections")){
        if(HudText::BeginTabItem("Skills")){
            ImGui::Dummy({0,10*scale});
            UI::BeginSection("BuffSkills","Choose your buffs","Select the skills to show while their effects are active. Choices are saved for this character.",scale);
            skills();UI::EndSection(scale);ImGui::EndTabItem();
        }
        if(HudText::BeginTabItem("Placement")){
            ImGui::Dummy({0,10*scale});
            UI::BeginSection("BuffLayout","Icon placement","Arrange the icon area on your screen.",scale);
            HudText::Checkbox("Show placement preview",&previewEnabled);
            UI::Note("Drag the preview to reposition the icons, or use the controls below.");
            UI::Slider("Horizontal position",&config.x,0,100,"%.1f%%",scale);
            UI::Slider("Vertical position",&config.y,0,100,"%.1f%%",scale);
            UI::Slider("Icon scale",&config.scale,.5f,3,"%.2fx",scale);
            UI::SliderInt("Icons per row",&config.columns,1,20,scale);
            if(HudText::Button("Reset placement")){config.x=40;config.y=12;config.scale=1;config.columns=10;}
            UI::EndSection(scale);ImGui::EndTabItem();
        }
        if(HudText::BeginTabItem("Labels")){
            ImGui::Dummy({0,10*scale});
            UI::BeginSection("BuffLabels","Skill labels","Display a skill's name beside its icon.",scale);
            HudText::Checkbox("Show skill labels",&config.labels);
            if(config.labels){
                int position=config.labelBottom?1:0;
                if(UI::Combo("Label position",&position,"Above icon\0Below icon\0",scale))config.labelBottom=position==1;
                UI::Slider("Label size",&config.labelSize,8,32,"%.0f px",scale);
                UI::Color("Label color",&config.labelColor.x,scale);
            }
            UI::EndSection(scale);ImGui::EndTabItem();
        }
        if(HudText::BeginTabItem("Timers")){
            ImGui::Dummy({0,10*scale});
            UI::BeginSection("BuffTimers","Remaining time","Show a countdown for timed effects.",scale);
            HudText::Checkbox("Show remaining time",&config.timers);
            if(config.timers){
                UI::Combo("Timer position",&config.timerPosition,"Below icon\0Above icon\0Top inside icon\0Bottom inside icon\0",scale);
                UI::Combo("Time format",&config.timerFormat,"Automatic\0Seconds (125s)\0Decimal seconds (1.2s)\0Minutes:seconds (2:05)\0Hours:minutes:seconds (0:02:05)\0",scale);
                UI::Slider("Timer size",&config.timerSize,8,32,"%.0f px",scale);
                UI::Color("Timer color",&config.timerColor.x,scale);
            }
            UI::EndSection(scale);
            if(config.timers){
                UI::BeginSection("BuffWarnings","Expiring effects","Make buffs that are about to expire easier to spot.",scale);
                HudText::Checkbox("Highlight expiring buffs",&config.highlightExpiring);
                if(config.highlightExpiring){
                    UI::Slider("Warn when time falls below",&config.expiringSeconds,0,60,"%.0f seconds",scale);
                    UI::Color("Warning color",&config.expiringColor.x,scale);
                }
                UI::Note("Effects without an expiration show an icon without a countdown.");
                UI::EndSection(scale);
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}
