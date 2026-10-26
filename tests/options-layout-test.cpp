#include "../d2rhud/plugin/D2RHUD/OptionsLayout.h"
#include <imgui_internal.h>
#include <cassert>
#include <cmath>
#include <iostream>
#include <string>

void Check(float scale,ImVec2 display) {
    ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize=display;
    ImFontConfig font;font.SizePixels=18*scale;io.Fonts->AddFontDefault(&font);io.Fonts->Build();
    ImVec2 first{},last{};float descriptionTop=0,footerTop=0;int hovered=-1;
    bool value=false;
    for(int frame=0;frame<12;++frame) {
        if(frame==2)io.AddMousePosEvent(first.x,first.y);
        if(frame==6)io.AddMousePosEvent(last.x,last.y);
        ImGui::NewFrame();ImGui::SetNextWindowPos({20,20});ImGui::SetNextWindowSize({850*scale,520*scale});
        ImGui::SetNextWindowScroll({0,0});
        ImGui::Begin("D2RHUD Options",nullptr,ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
        auto parent=ImGui::GetCurrentWindow();
        ImGui::TextUnformatted("D2RHUD Options");ImGui::Separator();
        const auto panels=HudOptions::BeginPanels(scale);auto controls=ImGui::GetCurrentWindow();
        ImGui::TextUnformatted("Current Options");ImGui::Separator();
        for(int i=0;i<35;++i) {
            ImGui::PushID(i);ImGui::Checkbox(("Option "+std::to_string(i)).c_str(),&value);
            const auto min=ImGui::GetItemRectMin(),max=ImGui::GetItemRectMax();
            if(!i)first={(min.x+max.x)*0.5f,(min.y+max.y)*0.5f};
            if(i==34)last={(min.x+max.x)*0.5f,(min.y+max.y)*0.5f};
            if(ImGui::IsItemHovered())hovered=i;ImGui::PopID();
        }
        ImGui::Separator();ImGui::CollapsingHeader("Workshop");
        if(frame==4)ImGui::SetScrollY(ImGui::GetScrollMaxY());
        if(frame>=5)assert(controls->Scroll.y>0);
        HudOptions::BeginDescription(panels,frame==10);
        auto description=ImGui::GetCurrentWindow();const float top=description->Pos.y;
        if(!frame)descriptionTop=top;
        assert(std::abs(top-descriptionTop)<1 && std::abs(top-controls->Pos.y)<1);
        if(frame==10)assert(description->Scroll.y==0);
        ImGui::TextUnformatted("Hovered option description");
        for(int line=0;line<70;++line)ImGui::Text("Description line %d",line);
        if(frame==8)ImGui::SetScrollY(ImGui::GetScrollMaxY());
        if(frame==9)assert(description->Scroll.y>0);
        HudOptions::EndPanels();ImGui::Separator();ImGui::Dummy({0,3});
        const auto footer=ImGui::GetCursorScreenPos();if(!frame)footerTop=footer.y;
        assert(std::abs(footer.y-footerTop)<1 && footer.y>descriptionTop+panels.height);
        ImGui::Button("Test Settings");ImGui::SameLine();ImGui::Button("Save Config");
        ImGui::TextUnformatted("New Settings Applied!");
        if(frame>=3)assert(parent->Scroll.y==0 && parent->ScrollMax.y==0);
        assert(ImGui::GetCursorScreenPos().y<parent->Pos.y+parent->Size.y);
        ImGui::End();ImGui::Render();
        if(frame==3)assert(hovered==0);
        if(frame==7)assert(hovered==34);
    }
    ImGui::DestroyContext();
}
int main() {
    Check(1,{1280,720});Check(1,{1920,1080});Check(2,{3840,2160});
    std::cout<<"Options layout: scrolling controls keep hover descriptions aligned at the top, independent description scrolling resets on selection, and footer buttons remain visible at 720p/1080p/4K.\n";
}
