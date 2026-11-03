#include "HudPresentation.h"
#include "MenuWorkspace.h"
#include "../SettingsUI.h"

namespace HudPresentation {
void Preferences(const char* title,bool& open,ImVec2 initialSize,PreferenceState& state,
    void(*draw)(float),nlohmann::ordered_json(*snapshot)(),const PreferenceHost& host) {
    if(!open){
        if(state.wasOpen&&state.dirty){state.failed=!host.save();state.dirty=state.failed;}
        state.wasOpen=false;return;
    }
    state.wasOpen=true;
    const float scale=host.scale;
    HudMenu::Prepare({initialSize.x*scale,initialSize.y*scale});
    if(HudMenu::Begin(title,&open,ImGuiWindowFlags_NoTitleBar)){
        if(!HudMenu::embedded){host.pushFont(3);host.title(title,&open);host.popFont(3);}
        const float footer=state.failed?2*ImGui::GetFrameHeightWithSpacing()+8*scale:ImGui::GetTextLineHeightWithSpacing()+6*scale;
        ImGui::BeginChild("PreferenceFields",{0,-footer},0,ImGuiWindowFlags_NoBackground);
        const auto before=snapshot();draw(scale);
        if(before!=snapshot()){state.dirty=true;state.failed=false;}
        ImGui::EndChild();
        if(state.dirty&&!state.failed&&!ImGui::IsAnyItemActive()){
            state.failed=!host.save();state.dirty=state.failed;
        }
        HudSettings::Note(state.failed?"Changes apply now, but could not be saved.":"Changes apply and save automatically.");
        if(state.failed&&host.button("Retry saving")){state.failed=!host.save();state.dirty=state.failed;}
    }
    HudMenu::End();
}
void Toast(const std::string& text,bool& visible,std::chrono::steady_clock::time_point began,
    std::chrono::steady_clock::time_point now) {
    if(!visible)return;
    const bool loaded=text.find("D2RHUD")!=std::string::npos;
    const int timeout=loaded?5:3,index=loaded?3:4;
    if(now-began>=std::chrono::seconds(timeout)){visible=false;return;}
    auto& io=ImGui::GetIO();
    ImFont* font=index<io.Fonts->Fonts.Size?io.Fonts->Fonts[index]:nullptr;
    if(font)ImGui::PushFont(font);
    const auto size=HudText::CalcTextSize(text.c_str());
    HudText::AddText(ImGui::GetBackgroundDrawList(),{(io.DisplaySize.x-size.x)*.5f,io.DisplaySize.y*.1f},
        loaded?IM_COL32(3,110,32,255):IM_COL32(199,179,119,255),text.c_str());
    if(font)ImGui::PopFont();
}
}
