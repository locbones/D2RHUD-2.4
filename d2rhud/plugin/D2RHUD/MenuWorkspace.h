#pragma once
#include "../JournalSkin.h"
#include "../SettingsUI.h"
#include "../WindowLayout.h"
#include <array>
#include <cctype>
#include <string>
#include <string_view>

namespace HudMenu {
enum class Page { Home,Options,Damage,Buffs,Ranges,Camera,Loot,Grail,Journal,Hotkeys,Appearance,Count };
struct Entry { Page page;const char* group;const char* title;const char* description; };
inline constexpr Entry Entries[]{
    {Page::Home,"","Overview","Your interface, your adventure. Choose a section to get started."},
    {Page::Options,"PREFERENCES","Options","Search and browse display, gameplay, item, sound and advanced preferences."},
    {Page::Damage,"ON SCREEN","Damage & DPS","Damage numbers, hit animations and the DPS meter."},
    {Page::Buffs,"ON SCREEN","Buff Icons","Active skill icons, timers and placement."},
    {Page::Ranges,"ON SCREEN","Item Stat Ranges","Possible rolls and item tooltip appearance."},
    {Page::Camera,"ON SCREEN","Camera","Camera angles, zoom and saved presets."},
    {Page::Loot,"YOUR ADVENTURE","Loot Filter","Manage your filter, build rules and preview their results."},
    {Page::Grail,"YOUR ADVENTURE","Grail Tracker","Browse and track your set and unique collection."},
    {Page::Journal,"YOUR ADVENTURE","Quest Journal","Your objectives, progress and rewards."},
    {Page::Hotkeys,"SETUP","Hotkeys & Commands","Key bindings, custom commands and startup actions."},
    {Page::Appearance,"SETUP","Appearance","Themes, colors and panel artwork."}
};
inline const Entry& Info(Page page){return Entries[(std::clamp)(int(page),0,int(Page::Count)-1)];}
inline Page ValidPage(int page){return page>=0&&page<int(Page::Count)?Page(page):Page::Home;}
inline void RouteJournal(Page& page,bool& open,bool visible,bool previousVisible){
    if(visible&&!previousVisible){page=Page::Journal;open=true;}
    else if(!visible&&previousVisible&&page==Page::Journal)open=false;
}
inline bool nativeTheme=false,embedded=false;
inline void (*workspaceBackground)()=nullptr;
inline const char* DefaultTheme(std::string_view mod){
    std::string lower(mod);for(auto& c:lower)c=char(std::tolower(static_cast<unsigned char>(c)));
    return lower=="rmd-mp"?"Sanctuary":"Default";
}
inline std::string ResolveTheme(std::string_view mod,std::string saved,int version){
    if(saved.empty()||(saved=="Default"&&version<1))return DefaultTheme(mod);
    return saved;
}
inline void Prepare(ImVec2 size){if(!embedded)HudWindowLayout::Prepare(size);}
inline void ApplySanctuaryPalette(){
    ImGui::StyleColorsDark();
    auto& style=ImGui::GetStyle();auto colors=style.Colors;
    colors[ImGuiCol_Text]={.90f,.90f,.87f,1};colors[ImGuiCol_TextDisabled]={.64f,.65f,.67f,1};
    colors[ImGuiCol_WindowBg]={.045f,.047f,.052f,1};colors[ImGuiCol_ChildBg]={.035f,.039f,.045f,.97f};
    colors[ImGuiCol_PopupBg]={.055f,.06f,.07f,.99f};colors[ImGuiCol_Border]={.36f,.38f,.41f,1};
    colors[ImGuiCol_FrameBg]={.12f,.135f,.155f,1};colors[ImGuiCol_FrameBgHovered]={.19f,.21f,.24f,1};colors[ImGuiCol_FrameBgActive]={.25f,.27f,.31f,1};
    colors[ImGuiCol_CheckMark]={.94f,.94f,.92f,1};colors[ImGuiCol_SliderGrab]={.66f,.70f,.76f,1};colors[ImGuiCol_SliderGrabActive]={.92f,.25f,.30f,1};
    colors[ImGuiCol_Button]={.22f,.075f,.09f,1};colors[ImGuiCol_ButtonHovered]={.40f,.10f,.14f,1};colors[ImGuiCol_ButtonActive]={.53f,.12f,.17f,1};
    colors[ImGuiCol_Header]={.29f,.075f,.095f,.9f};colors[ImGuiCol_HeaderHovered]={.39f,.10f,.14f,1};colors[ImGuiCol_HeaderActive]={.49f,.12f,.17f,1};
    colors[ImGuiCol_Tab]={.10f,.115f,.135f,1};colors[ImGuiCol_TabHovered]={.34f,.095f,.13f,1};colors[ImGuiCol_TabSelected]={.29f,.075f,.105f,1};
    colors[ImGuiCol_Separator]={.31f,.33f,.36f,1};colors[ImGuiCol_SeparatorHovered]={.72f,.26f,.31f,1};colors[ImGuiCol_SeparatorActive]={.9f,.3f,.35f,1};
    colors[ImGuiCol_ScrollbarBg]={.025f,.029f,.035f,1};colors[ImGuiCol_ScrollbarGrab]={.31f,.34f,.39f,1};colors[ImGuiCol_ScrollbarGrabHovered]={.44f,.47f,.53f,1};colors[ImGuiCol_ScrollbarGrabActive]={.65f,.68f,.73f,1};
    colors[ImGuiCol_ResizeGrip]={.58f,.62f,.69f,.8f};colors[ImGuiCol_ResizeGripHovered]={.85f,.34f,.39f,1};colors[ImGuiCol_ResizeGripActive]={.95f,.40f,.45f,1};
}

inline void Panel(){
    if(!nativeTheme)return;
    QuestJournalSkin::Load();const auto p=ImGui::GetWindowPos(),s=ImGui::GetWindowSize();
    auto draw=ImGui::GetWindowDrawList();draw->PushClipRect(p,{p.x+s.x,p.y+s.y},false);
    QuestJournalSkin::Panel(p,{p.x+s.x,p.y+s.y},ImGui::GetIO().FontGlobalScale);
    draw->PopClipRect();
}
inline bool Begin(const char* name,bool* open,ImGuiWindowFlags flags){
    if(embedded){ImGui::PushStyleColor(ImGuiCol_ChildBg,{0,0,0,0});ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{16*ImGui::GetIO().FontGlobalScale,16*ImGui::GetIO().FontGlobalScale});}
    const bool visible=embedded?ImGui::BeginChild(name,{0,0},ImGuiChildFlags_AlwaysUseWindowPadding,flags|ImGuiWindowFlags_NoSavedSettings):ImGui::Begin(name,open,flags);
    if(embedded){ImGui::PopStyleVar();ImGui::PopStyleColor();}
    if(visible&&!embedded)Panel();return visible;
}
inline void End(){if(embedded)ImGui::EndChild();else ImGui::End();}
inline bool Button(const char* label,ImVec2 size={}){
    return nativeTheme?QuestJournalSkin::Button(label,size,ImGui::GetIO().FontGlobalScale):ImGui::Button(label,size);
}
inline void Heading(const char* text,float scale){
    if(HudSettings::headingFont)ImGui::PushFont(HudSettings::headingFont);
    if(nativeTheme)QuestJournalSkin::Plate(text,scale);
    else {ImGui::TextUnformatted(text);ImGui::Separator();}
    if(HudSettings::headingFont)ImGui::PopFont();
}
inline void ResizeGrip(float scale){
    if(!nativeTheme)return;
    const auto p=ImGui::GetWindowPos(),size=ImGui::GetWindowSize();
    const ImVec2 corner{p.x+size.x-4*scale,p.y+size.y-4*scale};
    auto draw=ImGui::GetWindowDrawList();draw->PushClipRect(p,{p.x+size.x,p.y+size.y},false);
    for(int i=1;i<=3;++i)draw->AddLine({corner.x-4*i*scale,corner.y},{corner.x,corner.y-4*i*scale},ImGui::GetColorU32(ImGuiCol_ResizeGrip),scale);
    draw->PopClipRect();
}
inline bool Navigation(const Entry& entry,bool selected,float scale){
    const ImVec2 size{ImGui::GetContentRegionAvail().x,(std::max)(30*scale,ImGui::GetTextLineHeight()+10*scale)};
    const auto p=ImGui::GetCursorScreenPos();
    const auto textColor=ImGui::GetColorU32(ImGuiCol_Text);
    // Preserve the full clickable row and stable title ID while padding its
    // label away from the active marker and centering it vertically.
    ImGui::PushStyleColor(ImGuiCol_Text,{0,0,0,0});
    const bool clicked=ImGui::Selectable(entry.title,selected,0,size);
    ImGui::PopStyleColor();
    auto draw=ImGui::GetWindowDrawList();
    draw->PushClipRect({p.x+12*scale,p.y},{p.x+size.x-4*scale,p.y+size.y},true);
    draw->AddText({p.x+12*scale,p.y+(size.y-ImGui::GetTextLineHeight())*.5f},textColor,entry.title);
    draw->PopClipRect();
    if(nativeTheme&&selected){
        draw->AddRect(p,{p.x+size.x,p.y+size.y},ImGui::GetColorU32(ImGuiCol_Border));
        draw->AddRectFilled(p,{p.x+3*scale,p.y+size.y},ImGui::GetColorU32(ImGuiCol_CheckMark));
    }
    if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",entry.description);
    return clicked;
}
inline Page Home(float scale){
    HudSettings::Style form(scale);
    Page target=Page::Home;
    if(ImGui::BeginTable("OverviewTools",2,ImGuiTableFlags_SizingStretchSame)){
        std::string_view group;
        for(const auto& entry:Entries){
            if(!*entry.group||group==entry.group)continue;
            group=entry.group;
            ImGui::TableNextColumn();ImGui::PushID(entry.group);
            ImGui::BeginChild("Card",{0,0},ImGuiChildFlags_Borders|ImGuiChildFlags_AutoResizeY|ImGuiChildFlags_AlwaysUseWindowPadding);
            Panel();Heading(entry.group,scale);
            ImGui::Spacing();if(Button("Open",{-1,0}))target=entry.page;
            ImGui::EndChild();ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::Spacing();ImGui::TextDisabled("Resize corners and drag dividers to arrange your workspace.");
    return target;
}

// All editors are children of this one window. A page change never spawns a
// second menu, and native ImGui settings retain the workspace and divider size.
template<class DrawPage> bool Workspace(Page& page,bool& open,float scale,const char* context,DrawPage drawPage){
    const auto previous=page;
    HudWindowLayout::Prepare({1240*scale,860*scale});
    const auto display=ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowSizeConstraints({(std::min)(920*scale,display.x-16),(std::min)(600*scale,display.y-16)},{display.x-16,display.y-16});
    if(ImGui::Begin("D2RHUD###HUDWorkspaceV2",&open,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse)){
        if(nativeTheme)Panel();else if(workspaceBackground)workspaceBackground();
        Heading("D2RHUD",scale);
        if(nativeTheme)QuestJournalSkin::CloseButton(&open,scale);
        else {
            const auto cursor=ImGui::GetCursorScreenPos(),position=ImGui::GetWindowPos();
            ImGui::SetCursorScreenPos({position.x+ImGui::GetWindowWidth()-32*scale,position.y+10*scale});
            if(ImGui::SmallButton("X"))open=false;
            ImGui::SetCursorScreenPos(cursor);
        }
        ImGui::TextDisabled("%s",context);
        const float footer=ImGui::GetTextLineHeightWithSpacing()+4*scale;
        const float height=(std::max)(1.f,ImGui::GetContentRegionAvail().y-footer);
        const float maxNav=(std::max)(180*scale,ImGui::GetContentRegionAvail().x-580*scale);
        ImGui::SetNextWindowSizeConstraints({180*scale,0},{maxNav,FLT_MAX});
        ImGui::BeginChild("WorkspaceNavigation",{230*scale,height},ImGuiChildFlags_ResizeX|ImGuiChildFlags_AlwaysUseWindowPadding);
        Panel();std::string_view group;
        for(const auto& entry:Entries){
            if(group!=entry.group){group=entry.group;ImGui::Spacing();ImGui::TextColored(QuestJournalSkin::Gold,"%s",entry.group);}
            if(Navigation(entry,page==entry.page,scale))page=entry.page;
        }
        ImGui::EndChild();ImGui::SameLine();
        ImGui::BeginChild("WorkspacePage",{0,height},ImGuiChildFlags_AlwaysUseWindowPadding,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
        Panel();Heading(Info(page).title,scale);ImGui::Dummy({0,8*scale});
        if(page==Page::Home){
            ImGui::BeginChild("OverviewContents",{0,0},0,ImGuiWindowFlags_NoBackground);
            const auto target=Home(scale);
            ImGui::EndChild();
            if(target!=Page::Home)page=target;
            else {embedded=true;HudSettings::Style editor(scale);drawPage(Page::Home);embedded=false;}
        }
        else {embedded=true;HudSettings::Style editor(scale);drawPage(page);embedded=false;}
        ImGui::EndChild();ImGui::TextDisabled("Esc: close     |     Window sizes and pane widths save automatically");
        ResizeGrip(scale);
    }
    ImGui::End();return previous!=page;
}
}
