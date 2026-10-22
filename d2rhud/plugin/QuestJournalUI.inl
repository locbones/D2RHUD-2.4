// Native RMD quest-log panels and button atlases, with ImGui handling input.
namespace QuestJournalSkin {
using Image=LegacyTransmog::Skin::Image;
inline constexpr ImVec4 Gold{.84f,.71f,.43f,1},Paper{.84f,.80f,.70f,1},Muted{.53f,.50f,.44f,1};
inline Image background,tab,button,close,modal;
inline void Load(){
    using namespace LegacyTransmog::Skin;
    background=ArtworkImage(JournalBackground);tab=ArtworkImage(JournalTab);button=ArtworkImage(JournalButton);close=ArtworkImage(Close);modal=PanelArtwork();
}
inline void Slice(const Image& art,ImVec4 source,ImVec2 a,ImVec2 b,float sourceX,float sourceY,float edgeX,float edgeY,int frame=0){
    if(!art.texture||art.width<=0||art.height<=0||b.x<=a.x||b.y<=a.y)return;
    frame=std::clamp(frame,0,(std::max)(0,art.frames-1));const float offset=float(frame*art.pitch);
    edgeX=(std::min)(edgeX,(b.x-a.x)*.4f);edgeY=(std::min)(edgeY,(b.y-a.y)*.4f);
    const float x[]{a.x,a.x+edgeX,b.x-edgeX,b.x},y[]{a.y,a.y+edgeY,b.y-edgeY,b.y};
    const float u[]{(offset+source.x)/art.width,(offset+source.x+sourceX)/art.width,(offset+source.z-sourceX)/art.width,(offset+source.z)/art.width};
    const float v[]{source.y/art.height,(source.y+sourceY)/art.height,(source.w-sourceY)/art.height,source.w/art.height};
    auto draw=ImGui::GetWindowDrawList();
    for(int row=0;row<3;++row)for(int col=0;col<3;++col)draw->AddImage(ImTextureID(art.texture),{x[col],y[row]},{x[col+1],y[row+1]},{u[col],v[row]},{u[col+1],v[row+1]});
}
inline void Panel(ImVec2 a,ImVec2 b,float scale){
    if(background.texture)Slice(background,{89,963,1034,1376},a,b,24,24,12*scale,12*scale);
    else if(modal.texture)Slice(modal,{0,0,float(modal.frameWidth),float(modal.height)},a,b,60,50,12*scale,12*scale);
    else {auto draw=ImGui::GetWindowDrawList();draw->AddRectFilled(a,b,IM_COL32(15,12,10,255));draw->AddRect(a,b,IM_COL32(92,67,35,255));}
}
inline void Plate(const char* label,float scale,float height=38){
    const auto p=ImGui::GetCursorScreenPos();const float width=ImGui::GetContentRegionAvail().x,h=height*scale;
    if(background.texture)Slice(background,{89,788,1034,925},p,{p.x+width,p.y+h},155,20,(std::min)(70*scale,width*.18f),5*scale);
    const auto extent=ImGui::CalcTextSize(label);auto draw=ImGui::GetWindowDrawList();
    draw->PushClipRect(p,{p.x+width,p.y+h},true);draw->AddText({p.x+(width-extent.x)*.5f+1,p.y+(h-extent.y)*.5f+1},IM_COL32_BLACK,label);
    draw->AddText({p.x+(width-extent.x)*.5f,p.y+(h-extent.y)*.5f},ImGui::ColorConvertFloat4ToU32(Gold),label);draw->PopClipRect();ImGui::Dummy({width,h});
}
inline bool Button(const char* label,ImVec2 size={},float scale=1){
    if(size.x==0)size.x=(std::min)(ImGui::GetContentRegionAvail().x,ImGui::CalcTextSize(label).x+48*scale);
    if(size.x<0)size.x=ImGui::GetContentRegionAvail().x;
    if(size.y==0)size.y=ImGui::GetTextLineHeight()+18*scale;
    auto draw=ImGui::GetWindowDrawList();draw->ChannelsSplit(2);draw->ChannelsSetCurrent(1);
    ImGui::PushStyleColor(ImGuiCol_Button,{0,0,0,0});ImGui::PushStyleColor(ImGuiCol_ButtonHovered,{0,0,0,0});ImGui::PushStyleColor(ImGuiCol_ButtonActive,{0,0,0,0});ImGui::PushStyleColor(ImGuiCol_Text,Gold);
    const bool pressed=ImGui::Button(label,size);const auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();const int frame=ImGui::GetStyle().Alpha<.8f?2:ImGui::IsItemActive()?1:ImGui::IsItemHovered()?3:0;
    draw->ChannelsSetCurrent(0);
    if(button.texture)Slice(button,{0,0,float(button.frameWidth),float(button.height)},a,b,40,16,18*scale,6*scale,frame);
    else Panel(a,b,scale);
    draw->ChannelsMerge();ImGui::PopStyleColor(4);return pressed;
}
inline void Meter(float fraction,const char* label,float scale){
    if(label&&*label)ImGui::TextColored(Gold,"%s",label);
    const auto p=ImGui::GetCursorScreenPos();const float width=ImGui::GetContentRegionAvail().x,h=8*scale;auto draw=ImGui::GetWindowDrawList();
    draw->AddRectFilled(p,{p.x+width,p.y+h},IM_COL32(8,6,5,255));
    draw->AddRectFilled({p.x+2*scale,p.y+2*scale},{p.x+2*scale+(width-4*scale)*std::clamp(fraction,0.f,1.f),p.y+h-2*scale},IM_COL32(141,100,42,255));
    draw->AddRect(p,{p.x+width,p.y+h},IM_COL32(85,66,37,255));ImGui::Dummy({width,h});
}
inline void CloseButton(bool* open,float scale){
    const auto saved=ImGui::GetCursorScreenPos();const auto window=ImGui::GetWindowPos();const auto size=ImGui::GetWindowSize();
    const ImVec2 p{window.x+size.x-47*scale,window.y+22*scale};ImGui::SetCursorScreenPos(p);
    if(ImGui::InvisibleButton("Close journal",{25*scale,25*scale}))*open=false;
    const int frame=ImGui::IsItemActive()?1:ImGui::IsItemHovered()?3:0;
    if(close.texture){const float offset=float(frame*close.pitch);ImGui::GetWindowDrawList()->AddImage(ImTextureID(close.texture),p,{p.x+25*scale,p.y+25*scale},{offset/close.width,0},{(offset+close.frameWidth)/close.width,1});}
    else ImGui::GetWindowDrawList()->AddText(p,IM_COL32(200,170,110,255),"X");
    ImGui::SetCursorScreenPos(saved);
}
}
static void ShowQuestJournalMenu(){
    if(!showQuestJournalMenu)return;const auto rules=QuestJournal::Bindings();
    if(!rules){showQuestJournalMenu=false;return;}
    namespace Skin=QuestJournalSkin;
    EnableAllInput();const float scale=GetMenuScaleFactor();auto& io=ImGui::GetIO();Skin::Load();
    const ImVec2 size((std::min)(1180.f*scale,io.DisplaySize.x-20),(std::min)(800.f*scale,io.DisplaySize.y-20));
    // Center initially, then let ImGui retain the player's dragged position.
    ImGui::SetNextWindowSize(size,ImGuiCond_Always);ImGui::SetNextWindowPos({(io.DisplaySize.x-size.x)*.5f,(io.DisplaySize.y-size.y)*.5f},ImGuiCond_Once);
    PushFontSafe(2);ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{22*scale,18*scale});ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize,0);ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize,0);ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,{8*scale,9*scale});ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize,12*scale);ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding,0);
    ImGui::PushStyleColor(ImGuiCol_Text,Skin::Paper);ImGui::PushStyleColor(ImGuiCol_TextDisabled,Skin::Muted);ImGui::PushStyleColor(ImGuiCol_ChildBg,{0,0,0,0});ImGui::PushStyleColor(ImGuiCol_ScrollbarBg,{.05f,.04f,.03f,1});ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab,{.29f,.21f,.12f,1});ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered,{.45f,.33f,.18f,1});ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive,{.6f,.45f,.24f,1});
    if(ImGui::Begin("Mod Quest Journal",&showQuestJournalMenu,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoBackground|ImGuiWindowFlags_NoScrollbar)){
        const auto p=ImGui::GetWindowPos();Skin::Panel(p,{p.x+size.x,p.y+size.y},scale);Skin::Plate("Quest Journal",scale,55);Skin::CloseButton(&showQuestJournalMenu,scale);
        const auto view=QuestJournal::CurrentView();
        if(!rules->error.empty())ImGui::TextWrapped("%s",QuestJournal::Status().c_str());
        else{
            const auto heading=view.connected?view.character+"  /  "+std::to_string(view.completed)+" of "+std::to_string(rules->quests.size())+" quests completed":"RMD-MP  /  Your character's journey";
            ImGui::TextColored(Skin::Gold,"%s",heading.c_str());
            if(view.connected){Skin::Meter(float(view.completed)/float(rules->quests.size()),nullptr,scale);ImGui::TextWrapped("%s",view.message.c_str());}
            else ImGui::TextWrapped("Enter an RMD-MP game to load this character's journal.");
            static uint32_t recipient=0,session=0;static int selected=0,completed=-1;
            const bool changed=view.connected&&(recipient!=view.recipient||session!=view.session||completed!=view.completed);
            if(changed){recipient=view.recipient;session=view.session;completed=view.completed;selected=(std::min)(view.completed,int(rules->quests.size())-1);}
            if(view.connected&&view.legacyAvailable&&!view.blocked){
                ImGui::BeginDisabled(view.legacyCompleted<1);
                if(Skin::Button("Import legacy progress...",{},scale))ImGui::OpenPopup("LegacyQuestImport");
                ImGui::EndDisabled();if(view.legacyCompleted<1&&ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Carry your old Evil Barrier charm to offer an import. Importing grants no rewards.");
                ImGui::SameLine();if(Skin::Button("Start at Quest 1",{},scale))QuestJournal::RequestFreshStart(view);
                ImGui::SetNextWindowSizeConstraints({420*scale,0},{650*scale,600*scale});
                if(ImGui::BeginPopup("LegacyQuestImport",ImGuiWindowFlags_NoBackground)){
                    const auto a=ImGui::GetWindowPos(),b=ImGui::GetWindowSize();Skin::Panel(a,{a.x+b.x,a.y+b.y},scale);Skin::Plate("Legacy progress",scale);
                    ImGui::TextWrapped("Your carried Evil Barrier suggests %d completed quests. A transferred charm cannot prove who earned that progress.",view.legacyCompleted);
                    ImGui::TextWrapped("Confirm only if this matches this character's history. Earlier rewards will be marked as already claimed. No items, stats or rewards will be granted.");
                    if(Skin::Button("Import claimed quests",{},scale)){QuestJournal::RequestLegacyImport(view);ImGui::CloseCurrentPopup();}
                    ImGui::SameLine();if(Skin::Button("Cancel",{},scale))ImGui::CloseCurrentPopup();ImGui::EndPopup();
                }
            }
            const float footer=ImGui::GetTextLineHeight()+9*scale;
            ImGui::BeginChild("JournalBody",{0,-footer-9*scale},0,ImGuiWindowFlags_NoBackground);
            if(ImGui::BeginTable("QuestJournalPanes",2,ImGuiTableFlags_None)){
                ImGui::TableSetupColumn("Quests",ImGuiTableColumnFlags_WidthFixed,(std::min)(310*scale,ImGui::GetContentRegionAvail().x*.31f));ImGui::TableSetupColumn("Objective",ImGuiTableColumnFlags_WidthStretch);ImGui::TableNextColumn();
                ImGui::BeginChild("QuestList",{0,0},ImGuiChildFlags_AlwaysUseWindowPadding);const auto lp=ImGui::GetWindowPos(),ls=ImGui::GetWindowSize();Skin::Panel(lp,{lp.x+ls.x,lp.y+ls.y},scale);
                Skin::Plate("The Journey",scale);
                for(size_t i=0;i<rules->quests.size();++i){
                    const bool done=view.connected&&int(i)<view.completed,current=view.connected&&int(i)==view.completed;ImGui::PushID(int(i));
                    const auto label=std::to_string(i+1)+". "+rules->quests[i].title;const float width=(std::max)(60.f,ImGui::GetContentRegionAvail().x-20*scale);
                    const float height=ImGui::CalcTextSize(label.c_str(),nullptr,false,width).y+16*scale;const auto pos=ImGui::GetCursorScreenPos();
                    if(ImGui::InvisibleButton("##quest",{ImGui::GetContentRegionAvail().x,height}))selected=int(i);
                    const bool hovered=ImGui::IsItemHovered();
                    if(Skin::tab.texture)Skin::Slice(Skin::tab,{0,0,float(Skin::tab.frameWidth),float(Skin::tab.height)},pos,{pos.x+ImGui::GetItemRectSize().x,pos.y+height},8,8,5*scale,5*scale,selected==int(i)||hovered?0:1);
                    const auto next=ImGui::GetCursorScreenPos();ImGui::SetCursorScreenPos({pos.x+10*scale,pos.y+8*scale});
                    ImGui::PushStyleColor(ImGuiCol_Text,done?ImVec4{.5f,.64f,.45f,1}:current?Skin::Gold:Skin::Paper);ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+width);ImGui::TextUnformatted(label.c_str());ImGui::PopTextWrapPos();ImGui::PopStyleColor();
                    if(changed&&current)ImGui::SetScrollHereY(.4f);ImGui::SetCursorScreenPos(next);ImGui::PopID();
                }
                ImGui::EndChild();ImGui::TableNextColumn();ImGui::BeginChild("QuestObjective",{0,0},ImGuiChildFlags_AlwaysUseWindowPadding);const auto rp=ImGui::GetWindowPos(),rs=ImGui::GetWindowSize();Skin::Panel(rp,{rp.x+rs.x,rp.y+rs.y},scale);
                selected=std::clamp(selected,0,int(rules->quests.size())-1);const auto& quest=rules->quests[selected];
                const auto title="Quest "+std::to_string(quest.id);Skin::Plate(title.c_str(),scale);ImGui::TextColored(Skin::Gold,"%s",quest.title.c_str());
                const bool current=view.connected&&selected==view.completed,done=view.connected&&selected<view.completed;
                if(view.connected&&view.blocked)ImGui::TextColored({.95f,.35f,.25f,1},"Journal locked / see the status message above.");
                else if(done)ImGui::TextColored({.5f,.7f,.45f,1},"Complete / reward already claimed");
                else if(current&&view.legacyAvailable)ImGui::TextColored(Skin::Gold,"Choose Start at Quest 1 or import above.");
                else if(current)ImGui::TextColored(Skin::Gold,"%s",view.objectiveReady?"Objective complete / reward ready":"Current objective");
                else ImGui::TextDisabled("%s",view.connected?"Complete earlier quests to unlock this one.":"Preview / enter a game to track progress.");
                ImGui::Spacing();ImGui::TextWrapped("%s",quest.objective.c_str());
                if(current&&quest.trigger=="kills"){
                    const auto count=(std::min)(view.kills,uint32_t(quest.target));const auto label=std::to_string(count)+" / "+std::to_string(quest.target)+" monsters";Skin::Meter(float(count)/float(quest.target),label.c_str(),scale);
                }
                if(!quest.tips.empty()){ImGui::Spacing();Skin::Plate("Guidance",scale);ImGui::TextWrapped("%s",quest.tips.c_str());}
                ImGui::Spacing();Skin::Plate("Reward",scale);ImGui::TextWrapped("%s",quest.reward.empty()?"None":quest.reward.c_str());
                if(quest.milestone){
                    ImGui::TextWrapped("%s",quest.turnIn.c_str());static bool choices=false;if(changed)choices=false;
                    if(Skin::Button(choices?"Hide charm upgrade choices":"View charm upgrade choices",{-1,0},scale))choices=!choices;
                    if(choices)for(const auto& recipe:quest.rewardRecipes){ImGui::Spacing();ImGui::TextWrapped("%s",recipe.c_str());}
                }else if(quest.bonus){
                    ImGui::TextWrapped("Claim this bonus while carrying your Evil Barrier charm in inventory.");
                    if(current&&view.objectiveReady&&!view.blocked&&Skin::Button("Claim reward",{},scale))QuestJournal::RequestReward(view);
                }else if(quest.trigger!="acknowledge")ImGui::TextDisabled("Recorded automatically when satisfied.");
                if(current&&!view.objectiveReady&&!view.blocked&&quest.trigger=="acknowledge"){
                    if(quest.id==28||quest.id==34){ImGui::TextWrapped("After reading this page, confirm below.");if(Skin::Button("I have read this page",{},scale))QuestJournal::RequestAcknowledge(view);}
                    else ImGui::TextWrapped("Open the menu named in this lesson. The journal records the visit automatically.");
                }
                ImGui::Spacing();static bool details=false;if(Skin::Button(details?"Hide journal details":"Journal details",{-1,0},scale))details=!details;
                if(details)ImGui::TextWrapped("%s",QuestJournal::Status().c_str());
                ImGui::EndChild();ImGui::EndTable();
            }
            ImGui::EndChild();
            ImGui::TextDisabled("%s",view.dirty?"Progress saves with your character.":"RMD-MP / Progress belongs to this character.");
        }
    }
    ImGui::End();ImGui::PopStyleColor(7);ImGui::PopStyleVar(6);PopFontSafe(2);
}
