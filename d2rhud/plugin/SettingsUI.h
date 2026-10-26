#pragma once
#include "HudText.h"
#include <imgui.h>
#include <algorithm>

namespace HudSettings {
inline ImFont* bodyFont=nullptr;
inline ImFont* noteFont=nullptr;
inline ImFont* headingFont=nullptr;

// Form spacing is local to editors; gameplay overlays keep their own metrics.
struct Style {
    bool font=false;
    explicit Style(float scale) {
        font=bodyFont!=nullptr;if(font)ImGui::PushFont(bodyFont);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,{12*scale,10*scale});
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,{10*scale,6*scale});
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize,1*scale);
        ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing,20*scale);
    }
    ~Style(){ImGui::PopStyleVar(4);if(font)ImGui::PopFont();}
};
inline void Note(const char* text) {
    if(!text||!*text)return;
    if(noteFont)ImGui::PushFont(noteFont);
    ImGui::PushStyleColor(ImGuiCol_Text,ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    ImGui::PushTextWrapPos(0);HudText::TextUnformatted(text);ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();if(noteFont)ImGui::PopFont();
}
inline void BeginSection(const char* id,const char* title,const char* description,float scale,bool compact=false) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{18*scale,16*scale});
    ImGui::PushStyleColor(ImGuiCol_ChildBg,{.035f,.039f,.045f,.97f});
    ImGui::BeginChild(id,{0,0},ImGuiChildFlags_Borders|ImGuiChildFlags_AutoResizeY|ImGuiChildFlags_AlwaysUseWindowPadding,
        ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleColor();ImGui::PopStyleVar();
    if(headingFont)ImGui::PushFont(headingFont);
    HudText::TextColored({.85f,.75f,.53f,1},"%s",title);
    if(headingFont)ImGui::PopFont();
    if(description&&*description)Note(description);
    if(!compact){ImGui::Dummy({0,4*scale});ImGui::Separator();ImGui::Dummy({0,4*scale});}
}
inline void EndSection(float scale){ImGui::EndChild();ImGui::Dummy({0,12*scale});}
inline bool Disclosure(const char* label){
    return HudText::TreeNodeEx(label,ImGuiTreeNodeFlags_NoTreePushOnOpen|ImGuiTreeNodeFlags_SpanAvailWidth);
}
inline void BeginField(const char* label,float scale) {
    ImGui::PushID(label);HudText::TextUnformatted(label);
    ImGui::SetNextItemWidth((std::min)(520*scale,ImGui::GetContentRegionAvail().x));
}
// Short rows share a baseline; narrow panes stack before the label can collide.
inline void InlineField(const char* label,float width,float scale) {
    const float gap=12*scale;
    const bool inlineRow=ImGui::GetContentRegionAvail().x>=HudText::CalcTextSize(label).x+gap+(std::min)(width,120.f)*scale;
    if(inlineRow)ImGui::AlignTextToFramePadding();
    HudText::TextUnformatted(label);
    if(inlineRow)ImGui::SameLine(0,gap);
    ImGui::SetNextItemWidth((std::min)(width*scale,ImGui::GetContentRegionAvail().x));
}
inline void EndField(float scale){ImGui::PopID();ImGui::Dummy({0,4*scale});}
inline bool Combo(const char* label,int* value,const char* items,float scale){
    BeginField(label,scale);const bool changed=HudText::Combo("##value",value,items);EndField(scale);return changed;
}
inline bool Slider(const char* label,float* value,float low,float high,const char* format,float scale){
    BeginField(label,scale);const bool changed=HudText::SliderFloat("##value",value,low,high,format);EndField(scale);return changed;
}
inline bool SliderInt(const char* label,int* value,int low,int high,float scale,const char* format="%d",ImGuiSliderFlags flags=0){
    BeginField(label,scale);const bool changed=HudText::SliderInt("##value",value,low,high,format,flags);EndField(scale);return changed;
}
inline bool Color(const char* label,float* value,float scale){
    BeginField(label,scale);
    const bool changed=HudText::ColorEdit4("##value",value,ImGuiColorEditFlags_AlphaBar|ImGuiColorEditFlags_NoInputs);
    EndField(scale);return changed;
}
inline bool Input(const char* label,char* value,size_t capacity,float scale){
    BeginField(label,scale);const bool changed=HudText::InputText("##value",value,capacity);EndField(scale);return changed;
}
}
