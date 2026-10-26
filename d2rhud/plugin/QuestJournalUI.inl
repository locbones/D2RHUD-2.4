static void ShowQuestJournalMenu(){
    if(!showQuestJournalMenu)return;const auto rules=QuestJournal::Bindings();
    if(!rules){showQuestJournalMenu=false;return;}
    namespace Skin=QuestJournalSkin;
    EnableAllInput();const float scale=GetMenuScaleFactor();Skin::Load();
    ImVec2 size(1180.f*scale,800.f*scale);
    HudMenu::Prepare(size);
    PushFontSafe(2);ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{22*scale,18*scale});ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize,0);ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize,0);ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,{8*scale,9*scale});ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize,12*scale);ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding,0);
    ImGui::PushStyleColor(ImGuiCol_Text,Skin::Paper);ImGui::PushStyleColor(ImGuiCol_TextDisabled,Skin::Muted);ImGui::PushStyleColor(ImGuiCol_ChildBg,{0,0,0,0});ImGui::PushStyleColor(ImGuiCol_ScrollbarBg,{.05f,.04f,.03f,1});ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab,{.29f,.21f,.12f,1});ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered,{.45f,.33f,.18f,1});ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive,{.6f,.45f,.24f,1});
    if(HudMenu::Begin("Mod Quest Journal",&showQuestJournalMenu,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoBackground|ImGuiWindowFlags_NoScrollbar)){
        size=ImGui::GetWindowSize();const auto p=ImGui::GetWindowPos();Skin::Panel(p,{p.x+size.x,p.y+size.y},scale);if(!HudMenu::embedded){Skin::Plate("Quest Journal",scale,55);Skin::CloseButton(&showQuestJournalMenu,scale);}
        const auto view=QuestJournal::CurrentView();
        if(!rules->error.empty())HudText::TextWrapped("%s",QuestJournal::Status().c_str());
        else{
            if(view.connected)HudText::TextColored(Skin::Gold,"%s  /  %d of %d quests completed",view.character.c_str(),view.completed,int(rules->quests.size()));
            else HudText::TextColored(Skin::Gold,"RMD-MP  /  Your character's journey");
            if(view.connected){Skin::Meter(float(view.completed)/float(rules->quests.size()),nullptr,scale);HudText::TextWrapped("%s",view.message.c_str());}
            else HudText::TextWrapped("Enter an RMD-MP game to load this character's journal.");
            static uint32_t recipient=0,session=0;static int selected=0,completed=-1;
            const bool changed=view.connected&&(recipient!=view.recipient||session!=view.session||completed!=view.completed);
            if(changed){recipient=view.recipient;session=view.session;completed=view.completed;selected=(std::min)(view.completed,int(rules->quests.size())-1);}
            if(view.connected&&view.legacyAvailable&&!view.blocked){
                ImGui::BeginDisabled(view.legacyCompleted<1);
                if(Skin::Button("Import legacy progress...",{},scale))HudText::OpenPopup("LegacyQuestImport");
                ImGui::EndDisabled();if(view.legacyCompleted<1&&ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))HudText::SetTooltip("Carry your old Evil Barrier charm to offer an import. Importing grants no rewards.");
                ImGui::SameLine();if(Skin::Button("Start at Quest 1",{},scale))QuestJournal::RequestFreshStart(view);
                ImGui::SetNextWindowSizeConstraints({420*scale,0},{650*scale,600*scale});
                if(HudText::BeginPopup("LegacyQuestImport",ImGuiWindowFlags_NoBackground)){
                    const auto a=ImGui::GetWindowPos(),b=ImGui::GetWindowSize();Skin::Panel(a,{a.x+b.x,a.y+b.y},scale);Skin::Plate("Legacy progress",scale);
                    HudText::TextWrapped("Your carried Evil Barrier suggests %d completed quests. A transferred charm cannot prove who earned that progress.",view.legacyCompleted);
                    HudText::TextWrapped("Confirm only if this matches this character's history. Earlier rewards will be marked as already claimed. No items, stats or rewards will be granted.");
                    if(Skin::Button("Import claimed quests",{},scale)){QuestJournal::RequestLegacyImport(view);ImGui::CloseCurrentPopup();}
                    ImGui::SameLine();if(Skin::Button("Cancel",{},scale))ImGui::CloseCurrentPopup();ImGui::EndPopup();
                }
            }
            const float footer=ImGui::GetTextLineHeight()+9*scale;
            ImGui::BeginChild("JournalBody",{0,-footer-9*scale},0,ImGuiWindowFlags_NoBackground);
            if(ImGui::BeginTable("QuestJournalPanes",2,ImGuiTableFlags_Resizable|ImGuiTableFlags_BordersInnerV)){
                HudText::TableSetupColumn("Quests",ImGuiTableColumnFlags_WidthFixed,(std::min)(310*scale,ImGui::GetContentRegionAvail().x*.31f));HudText::TableSetupColumn("Objective",ImGuiTableColumnFlags_WidthStretch);ImGui::TableNextColumn();
                ImGui::BeginChild("QuestList",{0,0},ImGuiChildFlags_AlwaysUseWindowPadding);const auto lp=ImGui::GetWindowPos(),ls=ImGui::GetWindowSize();Skin::Panel(lp,{lp.x+ls.x,lp.y+ls.y},scale);
                Skin::Plate("The Journey",scale);
                for(size_t i=0;i<rules->quests.size();++i){
                    const bool done=view.connected&&int(i)<view.completed,current=view.connected&&int(i)==view.completed;ImGui::PushID(int(i));
                    const auto label=std::to_string(i+1)+". "+HudLocale::Translate(rules->quests[i].title);const float width=(std::max)(60.f,ImGui::GetContentRegionAvail().x-20*scale);
                    const float height=HudText::CalcTextSize(label.c_str(),nullptr,false,width).y+16*scale;const auto pos=ImGui::GetCursorScreenPos();
                    if(ImGui::InvisibleButton("##quest",{ImGui::GetContentRegionAvail().x,height}))selected=int(i);
                    const bool hovered=ImGui::IsItemHovered();
                    if(Skin::tab.texture)Skin::Slice(Skin::tab,{0,0,float(Skin::tab.frameWidth),float(Skin::tab.height)},pos,{pos.x+ImGui::GetItemRectSize().x,pos.y+height},8,8,5*scale,5*scale,selected==int(i)||hovered?0:1);
                    const auto next=ImGui::GetCursorScreenPos();ImGui::SetCursorScreenPos({pos.x+10*scale,pos.y+8*scale});
                    ImGui::PushStyleColor(ImGuiCol_Text,done?ImVec4{.5f,.64f,.45f,1}:current?Skin::Gold:Skin::Paper);ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+width);HudText::TextUnformatted(label.c_str());ImGui::PopTextWrapPos();ImGui::PopStyleColor();
                    if(changed&&current)ImGui::SetScrollHereY(.4f);ImGui::SetCursorScreenPos(next);ImGui::PopID();
                }
                ImGui::EndChild();ImGui::TableNextColumn();ImGui::BeginChild("QuestObjective",{0,0},ImGuiChildFlags_AlwaysUseWindowPadding);const auto rp=ImGui::GetWindowPos(),rs=ImGui::GetWindowSize();Skin::Panel(rp,{rp.x+rs.x,rp.y+rs.y},scale);
                selected=std::clamp(selected,0,int(rules->quests.size())-1);const auto& quest=rules->quests[selected];
                char title[128]{};snprintf(title,sizeof(title),HudLocale::Tr("Quest %d"),quest.id);Skin::Plate(title,scale);HudText::TextColored(Skin::Gold,"%s",quest.title.c_str());
                const bool current=view.connected&&selected==view.completed,done=view.connected&&selected<view.completed;
                if(view.connected&&view.blocked)HudText::TextColored({.95f,.35f,.25f,1},"Journal locked / see the status message above.");
                else if(done)HudText::TextColored({.5f,.7f,.45f,1},"Complete / reward already claimed");
                else if(current&&view.legacyAvailable)HudText::TextColored(Skin::Gold,"Choose Start at Quest 1 or import above.");
                else if(current)HudText::TextColored(Skin::Gold,"%s",view.objectiveReady?"Objective complete / reward ready":"Current objective");
                else HudText::TextDisabled("%s",view.connected?"Complete earlier quests to unlock this one.":"Preview / enter a game to track progress.");
                ImGui::Spacing();HudText::TextWrapped("%s",quest.objective.c_str());
                if(current&&quest.trigger=="kills"){
                    const auto count=(std::min)(view.kills,uint32_t(quest.target));char label[128]{};snprintf(label,sizeof(label),HudLocale::Tr("%u / %d monsters"),count,quest.target);Skin::Meter(float(count)/float(quest.target),label,scale);
                }
                if(!quest.tips.empty()){ImGui::Spacing();Skin::Plate("Guidance",scale);HudText::TextWrapped("%s",quest.tips.c_str());}
                ImGui::Spacing();Skin::Plate("Reward",scale);HudText::TextWrapped("%s",quest.reward.empty()?"None":quest.reward.c_str());
                if(quest.milestone){
                    HudText::TextWrapped("%s",quest.turnIn.c_str());static bool choices=false;if(changed)choices=false;
                    if(Skin::Button(choices?"Hide charm upgrade choices":"View charm upgrade choices",{-1,0},scale))choices=!choices;
                    if(choices)for(const auto& recipe:quest.rewardRecipes){ImGui::Spacing();HudText::TextWrapped("%s",recipe.c_str());}
                }else if(quest.bonus){
                    HudText::TextWrapped("Claim this bonus while carrying your Evil Barrier charm in inventory.");
                    if(current&&view.objectiveReady&&!view.blocked&&Skin::Button("Claim reward",{},scale))QuestJournal::RequestReward(view);
                }else if(quest.trigger!="acknowledge")HudText::TextDisabled("Recorded automatically when satisfied.");
                if(current&&!view.objectiveReady&&!view.blocked&&quest.trigger=="acknowledge"){
                    if(quest.id==28||quest.id==34){HudText::TextWrapped("After reading this page, confirm below.");if(Skin::Button("I have read this page",{},scale))QuestJournal::RequestAcknowledge(view);}
                    else HudText::TextWrapped("Open the menu named in this lesson. The journal records the visit automatically.");
                }
                ImGui::Spacing();static bool details=false;if(Skin::Button(details?"Hide journal details":"Journal details",{-1,0},scale))details=!details;
                if(details)HudText::TextWrapped("%s",QuestJournal::Status().c_str());
                ImGui::EndChild();ImGui::EndTable();
            }
            ImGui::EndChild();
            HudText::TextDisabled("%s",view.dirty?"Progress saves with your character.":"RMD-MP / Progress belongs to this character.");
        }
    }
    HudMenu::End();ImGui::PopStyleColor(7);ImGui::PopStyleVar(6);PopFontSafe(2);
}
