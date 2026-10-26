#pragma once
#include <imgui_internal.h>
#include <cassert>

template<class Draw> void ExerciseSettingsTabs(const char* windowName,const char* tabName,int count,Draw draw){
    auto& io=ImGui::GetIO();
    auto frame=[&]{
        ImGui::NewFrame();ImGui::SetNextWindowPos({20,20});ImGui::SetNextWindowSize({900,800});
        ImGui::Begin(windowName);
        auto& g=*ImGui::GetCurrentContext();const auto colors=g.ColorStack.Size,styles=g.StyleVarStack.Size,fonts=g.FontStack.Size;
        draw();
        assert(colors==g.ColorStack.Size&&styles==g.StyleVarStack.Size&&fonts==g.FontStack.Size);
        ImGui::End();ImGui::Render();assert(ImGui::GetDrawData()->TotalVtxCount>0);
    };
    frame();frame();auto window=ImGui::FindWindowByName(windowName);assert(window);
    auto tabs=ImGui::GetCurrentContext()->TabBars.GetByKey(window->GetID(tabName));assert(tabs&&tabs->Tabs.Size==count);
    for(int i=0;i<count;++i){
        auto tab=tabs->Tabs[i];io.AddMousePosEvent(tabs->BarRect.Min.x+tab.Offset+tab.Width*.5f,tabs->BarRect.GetCenter().y);frame();
        io.AddMouseButtonEvent(0,true);frame();io.AddMouseButtonEvent(0,false);frame();frame();
        assert(tabs->SelectedTabId==tab.ID);
    }
}
