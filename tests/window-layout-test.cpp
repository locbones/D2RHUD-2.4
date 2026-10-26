#include "../d2rhud/plugin/D2RHUD/OptionsLayout.h"
#include <imgui_internal.h>
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>

struct Layout {
    ImVec2 position,size,divider;
    float leftWidth,controlsY,controlsX,descriptionY,descriptionX;
    ImVec2 tableDivider;
    float tableWidth;
};

void Context(const std::filesystem::path& path) {
    ImGui::CreateContext();
    auto& io=ImGui::GetIO();io.DisplaySize={1920,1080};io.DeltaTime=1.f/60;
    io.BackendFlags|=ImGuiBackendFlags_HasMouseCursors;
    io.Fonts->AddFontDefault();io.Fonts->Build();
    HudWindowLayout::Initialize(path);
}

Layout Frame() {
    ImGui::NewFrame();HudWindowLayout::Prepare({850,520});
    ImGui::SetNextWindowScroll({0,0});
    ImGui::Begin("D2RHUD Options",nullptr,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
    Layout result{};result.position=ImGui::GetWindowPos();result.size=ImGui::GetWindowSize();
    ImGui::TextUnformatted("Options");
    const auto panels=HudOptions::BeginPanels(1);
    const auto left=ImGui::GetCurrentWindow();
    result.divider={left->Pos.x+left->Size.x-1,left->Pos.y+left->Size.y*.5f};
    result.leftWidth=left->Size.x;result.controlsY=left->Scroll.y;result.controlsX=left->Scroll.x;
    for(int i=0;i<90;++i)ImGui::Text("Option %d with a deliberately very long unwrapped label to exercise horizontal overflow",i);
    HudOptions::BeginDescription(panels,false);
    result.descriptionY=ImGui::GetScrollY();result.descriptionX=ImGui::GetScrollX();
    for(int i=0;i<90;++i)ImGui::Text("Description %d",i);
    HudOptions::EndPanels();ImGui::Button("Save Config");ImGui::End();
    HudWindowLayout::Prepare({900,260});
    ImGui::SetNextWindowPos({50,720},ImGuiCond_FirstUseEver);
    ImGui::Begin("Table fixture",nullptr,ImGuiWindowFlags_NoTitleBar);
    if(ImGui::BeginTable("SavedPanes",2,ImGuiTableFlags_Resizable|ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("List",ImGuiTableColumnFlags_WidthFixed,200);
        ImGui::TableSetupColumn("Details",ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextColumn();ImGui::TextUnformatted("List");ImGui::Dummy({0,170});
        auto table=ImGui::GetCurrentTable();
        result.tableDivider={table->Columns[0].MaxX,table->OuterRect.Min.y+80};
        result.tableWidth=table->Columns[0].WidthGiven;
        ImGui::TableNextColumn();ImGui::TextUnformatted("Details");ImGui::EndTable();
    }
    ImGui::End();ImGui::Render();HudWindowLayout::Tick();return result;
}

Layout Drag(ImVec2 from,ImVec2 to) {
    auto& io=ImGui::GetIO();
    io.AddMousePosEvent(from.x,from.y);Frame();Frame();
    io.AddMouseButtonEvent(0,true);Frame();
    io.AddMousePosEvent(to.x,to.y);Frame();Frame();
    io.AddMouseButtonEvent(0,false);return Frame();
}

int main(int argc,char** argv) {
    assert(argc==2);
    const auto path=std::filesystem::path(argv[1])/L"layout-\u6d4b\u8bd5"/L"ui-layout.ini";
    std::filesystem::remove(path);
    Context(path);Frame();auto initial=Frame();
    auto& io=ImGui::GetIO();
    io.AddMousePosEvent(initial.position.x+60,initial.position.y+100);Frame();
    io.AddMouseWheelEvent(0,-1);Frame();auto scrolled=Frame();
    assert(scrolled.controlsY>0&&scrolled.controlsX==0&&scrolled.descriptionY==0);
    io.AddKeyEvent(ImGuiMod_Shift,true);Frame();
    io.AddMouseWheelEvent(0,-1);Frame();auto shifted=Frame();
    assert(shifted.controlsY>scrolled.controlsY&&shifted.controlsX==0);
    io.AddKeyEvent(ImGuiMod_Shift,false);Frame();
    io.AddMousePosEvent(initial.position.x+initial.size.x-60,initial.position.y+100);Frame();
    io.AddMouseWheelEvent(0,-1);Frame();auto description=Frame();
    assert(description.descriptionY>0&&description.descriptionX==0&&description.controlsY==shifted.controlsY);

    auto resized=Drag({initial.position.x+initial.size.x-3,initial.position.y+initial.size.y-3},
        {initial.position.x+initial.size.x+117,initial.position.y+initial.size.y+77});
    assert(resized.size.x>initial.size.x+100&&resized.size.y>initial.size.y+60);
    auto moved=Drag({resized.position.x+resized.size.x*.5f,resized.position.y+10},
        {resized.position.x+resized.size.x*.5f+40,resized.position.y+35});
    assert(moved.position.x>resized.position.x+30&&moved.position.y>resized.position.y+15);
    auto split=Drag(moved.divider,{moved.divider.x+80,moved.divider.y});
    assert(split.leftWidth>initial.leftWidth+60);
    auto tables=Drag(split.tableDivider,{split.tableDivider.x+100,split.tableDivider.y});
    assert(tables.tableWidth>initial.tableWidth+80);
    // A release must persist immediately, before the normal five-second delay.
    assert(std::filesystem::exists(path));
    std::ifstream input(path,std::ios::binary);std::string saved{std::istreambuf_iterator<char>(input),{}};
    assert(saved.find("[Window][D2RHUD Options]")!=std::string::npos);
    assert(saved.find("##OptionsControls")!=std::string::npos);
    // Simulate a new game process, without calling Shutdown to mask save-on-release failures.
    ImGui::DestroyContext();Context(path);Frame();auto restored=Frame();
    assert(restored.size.x==split.size.x&&restored.size.y==split.size.y);
    assert(restored.position.x==split.position.x&&restored.position.y==split.position.y);
    assert(restored.leftWidth==split.leftWidth);
    assert(restored.tableWidth==tables.tableWidth);

    ImGui::GetIO().DisplaySize={640,480};auto smaller=Frame();
    assert(smaller.size.x<=624&&smaller.size.y<=464);
    assert(smaller.leftWidth<smaller.size.x-140);
    HudWindowLayout::Shutdown();ImGui::DestroyContext();
    std::cout<<"Window layout: vertical wheel and Shift+wheel, independent panes, corner drag, divider drag, immediate Unicode-path save, fresh-context restore, and smaller-screen constraints passed.\n";
}
