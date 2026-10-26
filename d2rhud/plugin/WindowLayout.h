#pragma once
#include <imgui.h>
#include <algorithm>
#include <cfloat>
#include <filesystem>

namespace HudWindowLayout {
// Defaults apply only before a window has a saved layout. Keep restored windows
// usable when the player switches to a smaller display resolution.
inline ImVec2 Prepare(ImVec2 size) {
    const auto display=ImGui::GetIO().DisplaySize;
    const ImVec2 maximum{(std::max)(1.f,display.x-16),(std::max)(1.f,display.y-16)};
    size={(std::min)(size.x,maximum.x),(std::min)(size.y,maximum.y)};
    const ImVec2 minimum{(std::min)(size.x,400.f),(std::min)(size.y,280.f)};
    const ImVec2 position{(display.x-size.x)*.5f,(display.y-size.y)*.5f};
    ImGui::SetNextWindowPos(position,ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(size,ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(minimum,maximum);
    return position;
}

// Constrain a draggable left pane so both sides remain reachable, even after
// shrinking its parent. ResizeX stores the pane width in ImGui's layout data.
inline void PrepareLeftPane(float scale) {
    const auto available=ImGui::GetContentRegionAvail();
    const float maximum=(std::max)(1.f,available.x-140*scale-ImGui::GetStyle().ItemSpacing.x);
    ImGui::SetNextWindowSizeConstraints({(std::min)(140*scale,maximum),0},{maximum,FLT_MAX});
}

void Initialize(const std::filesystem::path& path = {});
void Tick();
void Shutdown();
}
