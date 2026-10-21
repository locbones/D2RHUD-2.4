#define NOMINMAX
#include "../d2rhud/plugin/ItemFilter/LuaConfigIO.h"
#include "../d2rhud/plugin/ItemFilter/LootFilterFields.h"
#include "../d2rhud/plugin/ItemFilter/LootFilterEditorText.h"
#include "../d2rhud/plugin/ItemFilter/LootFilterRuleBuilder.h"
#include "../d2rhud/plugin/ItemFilter/LootFilterPreview.h"
#include <imgui_internal.h>
#include <unordered_map>
#include <iostream>

struct LootFilterHeader { std::string Title, Version; } g_LootFilterHeader;
struct LootFilterRule { std::string comment; std::vector<std::pair<std::string,std::string>> fields; std::string rawLua; };
std::vector<LootFilterRule> g_LootFilterRules;
std::unordered_map<std::string,std::string> g_LuaVariables, g_LuaVariableComments;
struct ItemFilter {
    void ReloadGameFilter() {}
    static const std::string& GetDiagnosticsText() { static std::string text = "Synthetic test runtime"; return text; }
} testFilter;
ItemFilter* itemFilter = &testFilter;
static std::string testPath;
static std::string previewEngine;
static const std::string& GetLootFilterPreviewEngine(){return previewEngine;}
static std::string GetLootFilterConfigPath() { return testPath; }
static bool ThemeButton(const char* label, const ImVec2& size = ImVec2(0,0)) { return ImGui::Button(label,size); }
#include "../d2rhud/plugin/ItemFilter/LootFilterEditorIO.inl"
static const LootFilterConfig::Builder::Catalog& GetLootFilterBuilderCatalog() {
    static LootFilterConfig::Builder::Catalog catalog{{{"Ring","rin"},{"Custom item","Z01"}},{{"Ring","10"}},{{"Fire resistance","39"}},{{"Blood Moor","2"}},true,{}};
    return catalog;
}
#include "../d2rhud/plugin/ItemFilter/LootFilterRuleBuilderUI.inl"
#include "../d2rhud/plugin/ItemFilter/LootFilterPreviewUI.inl"
#include "../d2rhud/plugin/ItemFilter/LootFilterEditorUI.inl"

static void Check(bool condition, const std::string& message) { if (!condition) throw std::runtime_error(message); }
static void Capture(const std::filesystem::path& directory,const std::string& name,unsigned char* atlas,int width,int height) {
    std::filesystem::create_directories(directory);
    std::ofstream texture(directory/"atlas.rgba",std::ios::binary);texture.write(reinterpret_cast<const char*>(atlas),size_t(width)*height*4);
    const auto* data=ImGui::GetDrawData();std::ofstream out(directory/(name+".bin"),std::ios::binary);
    uint32_t commands=0;for(auto* list:data->CmdLists)commands+=list->CmdBuffer.Size;
    const uint32_t header[]{uint32_t(data->DisplaySize.x),uint32_t(data->DisplaySize.y),uint32_t(width),uint32_t(height),uint32_t(data->TotalVtxCount),uint32_t(data->TotalIdxCount),commands};out.write(reinterpret_cast<const char*>(header),sizeof(header));
    for(auto* list:data->CmdLists)out.write(reinterpret_cast<const char*>(list->VtxBuffer.Data),size_t(list->VtxBuffer.Size)*sizeof(ImDrawVert));
    uint32_t base=0;for(auto* list:data->CmdLists){for(auto index:list->IdxBuffer){uint32_t value=base+index;out.write(reinterpret_cast<const char*>(&value),4);}base+=list->VtxBuffer.Size;}
    uint32_t first=0;for(auto* list:data->CmdLists){for(const auto& command:list->CmdBuffer){const uint32_t span[]{first+command.IdxOffset,command.ElemCount};out.write(reinterpret_cast<const char*>(span),sizeof(span));out.write(reinterpret_cast<const char*>(&command.ClipRect),sizeof(ImVec4));const uint64_t id=command.GetTexID();out.write(reinterpret_cast<const char*>(&id),sizeof(id));}first+=list->IdxBuffer.Size;}
}
static void Frame(bool longText = false, std::string* text = nullptr) {
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(10,10));
    const float scale=ImGui::GetIO().DisplaySize.x>=3840?2.f:1.f;
    ImGui::SetNextWindowSize(ImVec2((std::min)(1200.f*scale,ImGui::GetIO().DisplaySize.x-20),720.f*scale-20));
    ImGui::Begin("Filter test",nullptr,ImGuiWindowFlags_NoSavedSettings);
    if (longText) LootFilterConfig::InputTextMultiline("Long rule",text,ImVec2(-1,250));
    else if(text) LootFilterConfig::Builder::UI::Render(*text,g_LootConfigStatus,GetLootFilterBuilderCatalog());
    else {
        ImGui::TextUnformatted("D2RLoot Settings");ImGui::Separator();
        ImGui::AlignTextToFramePadding();ImGui::TextUnformatted("Filter");ImGui::SameLine();
        ImGui::SetNextItemWidth(360*scale);ImGui::Button("My loot filter",{360*scale,0});
        ImGui::SameLine();ImGui::Button("Files, sounds & updates...");ImGui::Spacing();
        RenderLootFilterEditor();
    }
    ImGui::End(); ImGui::Render();
    Check(ImGui::GetDrawData()->TotalVtxCount > 0,"Editor produced no drawing");
}
int main(int argc,char** argv) {
    try {
        if(argc>2)Check(LootFilterConfig::Read(argv[2],previewEngine),"Unable to read preview engine");
        const auto directory = std::filesystem::temp_directory_path() / ("d2rhud-editor-test-" + std::to_string(GetCurrentProcessId()));
        std::filesystem::create_directories(directory);
        testPath = (directory / "lootfilter_config.lua").string();
        const std::string source = "-- author\nlocal T=true; local filterConfig={allowOverrides=T,Debug=false,filter_level=1,filter_titles={'Early','End'},styles={good={suffix=' OK'}},rules={{codes='rin',suffix=[=[" + std::string(20000,'x') + "]=]}}}; for i=1,3 do table.insert(filterConfig.rules,{code='amu'}) end; return filterConfig";
        { std::ofstream file(testPath,std::ios::binary); file << source; }
        LoadLootFilterConfig(testPath);
        Check(g_LootFilterRules.size()==1 && g_LootFilterRules[0].rawLua.size()>20000,"Long authored rule missing");
        Check(g_LuaVariables.at("allowOverrides")=="T" && g_LuaVariables.at("debug")=="false","Expressions/Debug alias changed");
        Check(SaveLootFilterConfig(testPath),g_LootConfigStatus);
        std::string disk; LootFilterConfig::Read(testPath,disk);
        Check(disk==source,"Unchanged production editor save changed source");
        g_LuaVariables["debug"]="{mode='all',limit=2}";
        Check(SaveLootFilterConfig(testPath),g_LootConfigStatus);
        Check(g_LootConfigDocument.Find("Debug") && !g_LootConfigDocument.Find("debug"),"Legacy Debug created conflicting option");
        Check(g_LootConfigDocument.source.find("for i=1,3")!=std::string::npos,"Generated code was lost");
        g_LootFilterRules.clear();
        Check(SaveLootFilterConfig(testPath),g_LootConfigStatus);
        Check(g_LootConfigDocument.rules.empty(),"Last rule was not deleted");
        LoadLootFilterConfig(testPath);
        ImGui::CreateContext();
        auto& io = ImGui::GetIO(); io.IniFilename=nullptr; io.DeltaTime=1.0f/60;
        ImFontConfig font;font.SizePixels=18;
        if(argc>4)Check(io.Fonts->AddFontFromFileTTF(argv[4],18,&font)!=nullptr,"Unable to load native UI font");
        else io.Fonts->AddFontDefault(&font);
        unsigned char* pixels; int width,height;
        io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
        io.Fonts->SetTexID(1);
        // Optional native panel RGBA fixture: uint32 width/height, then pixels.
        // The capture keeps texture IDs so visual QA can render native artwork.
        if(argc>3){std::ifstream fixture(argv[3],std::ios::binary);uint32_t dimensions[2]{};fixture.read(reinterpret_cast<char*>(dimensions),8);Check(fixture&&dimensions[0]>120&&dimensions[0]<=8192&&dimensions[1]>146&&dimensions[1]<=8192,"Invalid native panel fixture");LootFilterConfig::Builder::UI::panelArt={2,float(dimensions[0]),float(dimensions[1]),float(dimensions[0])};}
        for (const auto& size : {ImVec2(960,720),ImVec2(1200,720),ImVec2(1920,1080),ImVec2(3840,2160)}) {
            io.DisplaySize=size;
            io.FontGlobalScale=size.x>=3840?2.f:1.f;
            Frame(); Frame();
            auto& context = *GImGui;
            Check(context.TabBars.GetAliveCount()>0,"Editor tab bar missing");
            auto* bar = context.TabBars.GetByIndex(0);
            Check(bar->Tabs.Size==5,"Expected Settings/Rules/Styles/Diagnostics/Source");
            for (int tab=0;tab<bar->Tabs.Size;++tab) {
                bar->NextSelectedTabId=bar->Tabs[tab].ID;
                Frame(); Frame();
            }
            std::string built="{label='Valuable rings',codes={'rin','Z01'},quality={'Unique','Set'},itype={10},ilvl='>=80',suffix=' {gold}Keep',background={20,30,40,220},border={255,128,40,255,2},name_style='Rainbow'}";
            const auto original=built;
            Frame(false,&built);Frame(false,&built);
            Check(built==original,"Rendering builder changed the draft");
            // A selected production rule must render the guided controls too.
            LootFilterRule rule;rule.comment="Valuable rings";rule.rawLua=built;g_LootFilterRules={rule};
            bar->NextSelectedTabId=bar->Tabs[1].ID;Frame();Frame();
            ImGuiWindow* list=nullptr;
            for(auto* window:context.Windows)if(std::string(window->Name).find("RuleList")!=std::string::npos&&window->WasActive)list=window;
            Check(list!=nullptr,"Rule list is missing");
            list->Scroll.y=0;Frame();
            io.AddMousePosEvent(list->DC.CursorStartPos.x+15,list->DC.CursorStartPos.y+6);Frame();
            io.AddMouseButtonEvent(0,true);Frame();
            io.AddMouseButtonEvent(0,false);Frame();
            Frame();
            bool details=false;for(auto* window:context.Windows)if((std::string(window->Name).find("RuleDetails")!=std::string::npos||std::string(window->Name).find("RulePreviewPane")!=std::string::npos)&&window->WasActive)details=true;
            Check(details,"Selecting a rule did not open the builder");
            io.AddMousePosEvent(io.DisplaySize.x-2,2);Frame();Frame();
            for(auto* window:context.Windows)if(window->WasActive&&(std::string(window->Name).substr(std::string(window->Name).rfind('/')+1).find("RuleLibrary")==0||std::string(window->Name).substr(std::string(window->Name).rfind('/')+1).find("RuleWorkspace")==0)){
                Check(window->Pos.y+window->Size.y<=720.f*(size.x>=3840?2.f:1.f),"Workspace exceeds the real menu height");
                Check(window->Size.y>200*(size.x>=3840?2.f:1.f),"Workspace has too little usable height: "+std::string(window->Name)+" height="+std::to_string(window->Size.y));
            }
            if(argc>1&&*argv[1])Capture(std::filesystem::path(argv[1]),"editor-"+std::to_string(int(size.x)),pixels,width,height);
            ImGuiTabBar* views=nullptr;
            for(int i=0;i<context.TabBars.GetMapSize();++i)if(auto* candidate=context.TabBars.TryGetMapData(i))if(candidate->Tabs.Size==4)views=candidate;
            Check(views!=nullptr,"Conditions / Actions / Preview / Rule code views are missing");
            for(int view=0;view<4;++view){
                views->NextSelectedTabId=views->Tabs[view].ID;Frame();Frame();Frame();
                if(argc>1&&*argv[1])Capture(std::filesystem::path(argv[1]),"editor-"+std::to_string(int(size.x))+"-view-"+std::to_string(view),pixels,width,height);
            }
            if(size.x==1200){
                io.FontGlobalScale=22.f/18.f;
                for(int view=0;view<3;++view){views->NextSelectedTabId=views->Tabs[view].ID;Frame();Frame();Frame();if(argc>1&&*argv[1])Capture(std::filesystem::path(argv[1]),"editor-1200-large-type-view-"+std::to_string(view),pixels,width,height);}
                io.FontGlobalScale=1;
            }
        }
        // Exercise the real ImGui resize callback through keyboard text events.
        io.DisplaySize=ImVec2(960,720);
        io.FontGlobalScale=1;
        std::string longDraft="short";
        Frame(true,&longDraft);
        io.AddMousePosEvent(100,100); io.AddMouseButtonEvent(0,true); Frame(true,&longDraft);
        io.AddMouseButtonEvent(0,false); Frame(true,&longDraft);
        io.AddKeyEvent(ImGuiMod_Ctrl,true); io.AddKeyEvent(ImGuiKey_A,true); Frame(true,&longDraft);
        io.AddKeyEvent(ImGuiKey_A,false); io.AddKeyEvent(ImGuiMod_Ctrl,false); Frame(true,&longDraft);
        io.AddInputCharactersUTF8(std::string(24000,'z').c_str()); Frame(true,&longDraft);
        Check(longDraft==std::string(24000,'z'),"Long rule input was truncated or resize callback failed");
        ImGui::DestroyContext();
        std::filesystem::remove(testPath); std::filesystem::remove(testPath+".bak"); std::filesystem::remove(directory);
        std::cout << "PASS: production editor load/save; four workspace views in real 1200x720 menu at 18px and 4K scale; 24 KB text input\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}


