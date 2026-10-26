#pragma once
#include "CandyCornSave.h"
#include "SettingsUI.h"
#include <imgui.h>
#include <algorithm>

namespace CandyCornSave {
inline void DrawNotification(){
    static Notification current;
    static double until=0;
    Notification next;
    // A quick conversion can finish between frames. Show its final result
    // directly instead of leaving a stale "converting" message queued.
    while(TakeNotification(next)){current=std::move(next);until=ImGui::GetTime()+(current.error?15:9);}
    if(current.message.empty() || ImGui::GetTime()>=until)return;
    auto& io=ImGui::GetIO();
    const float scale=(std::max)(.65f,io.DisplaySize.y/1080.f);
    const float width=(std::min)(460*scale,io.DisplaySize.x-32*scale);
    ImGui::SetNextWindowPos({io.DisplaySize.x*.5f,io.DisplaySize.y*.14f},ImGuiCond_Always,{.5f,0});
    ImGui::SetNextWindowSize({width,0},ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{18*scale,14*scale});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize,2*scale);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,{8*scale,10*scale});
    ImGui::PushStyleColor(ImGuiCol_WindowBg,{.035f,.039f,.045f,.98f});
    ImGui::PushStyleColor(ImGuiCol_Border,current.error?ImVec4(.8f,.18f,.18f,1):ImVec4(.55f,.18f,.18f,1));
    ImGui::PushStyleColor(ImGuiCol_Text,{.88f,.88f,.86f,1});
    if(HudSettings::bodyFont)ImGui::PushFont(HudSettings::bodyFont);
    ImGui::Begin("Candy Corn update###CandyCornConversion",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_AlwaysAutoResize|
        ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoInputs|ImGuiWindowFlags_NoFocusOnAppearing|ImGuiWindowFlags_NoNav);
    ImGui::TextColored({.9f,.76f,.45f,1},"%s",current.title.c_str());
    ImGui::Separator();ImGui::PushTextWrapPos(0);ImGui::TextUnformatted(current.message.c_str());ImGui::PopTextWrapPos();
    ImGui::End();
    if(HudSettings::bodyFont)ImGui::PopFont();
    ImGui::PopStyleColor(3);ImGui::PopStyleVar(3);
}
}
