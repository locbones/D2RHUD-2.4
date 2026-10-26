#pragma once
#include <imgui.h>
#include <algorithm>
#include "../WindowLayout.h"

namespace HudOptions {
struct Panels { float height; };
inline void VerticalWheel() {
    ImGui::SetScrollX(0);
    const auto& io=ImGui::GetIO();
    if(ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)&&!ImGui::IsAnyItemActive()&&io.MouseWheel!=0) {
        const float step=(std::min)(5*ImGui::GetTextLineHeight(),ImGui::GetWindowHeight()*.67f);
        ImGui::SetScrollY((std::clamp)(ImGui::GetScrollY()-io.MouseWheel*step,0.f,ImGui::GetScrollMaxY()));
    }
}
inline Panels BeginPanels(float scale) {
    const auto available=ImGui::GetContentRegionAvail();
    const float footer=2*ImGui::GetFrameHeightWithSpacing()+ImGui::GetStyle().ItemSpacing.y+8*scale;
    const Panels panels{(std::max)(1.0f,available.y-footer)};
    const float left=(std::min)(280*scale,available.x*0.5f);
    // Separate child windows prevent separators/groups in the long controls
    // list from advancing the description's cursor or its scroll position.
    HudWindowLayout::PrepareLeftPane(scale);
    ImGui::BeginChild("##OptionsControls",{left,panels.height},ImGuiChildFlags_ResizeX|ImGuiChildFlags_AlwaysUseWindowPadding,ImGuiWindowFlags_NoScrollWithMouse);
    VerticalWheel();
    return panels;
}
inline void BeginDescription(const Panels& panels,bool changed) {
    ImGui::EndChild();
    ImGui::SameLine();
    const auto top=ImGui::GetCursorScreenPos();
    const float gap=ImGui::GetStyle().ItemSpacing.x*0.5f;
    ImGui::GetWindowDrawList()->AddLine({top.x-gap,top.y},{top.x-gap,top.y+panels.height},IM_COL32(180,150,80,150));
    if(changed)ImGui::SetNextWindowScroll({0,0});
    ImGui::BeginChild("##OptionsDescription",{0,panels.height},ImGuiChildFlags_AlwaysUseWindowPadding,ImGuiWindowFlags_NoScrollWithMouse);
    VerticalWheel();
}
inline void EndPanels() { ImGui::EndChild(); }
}
