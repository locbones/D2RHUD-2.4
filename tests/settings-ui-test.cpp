#define main ExistingStatRangeTestsMain
#include "stat-ranges-test.cpp"
#undef main
#include "../d2rhud/plugin/D2RHUD/MenuWorkspace.h"

namespace LegacyTransmog::Skin {
std::array<Image,Count> fixtureImages;
Image ArtworkImage(Part part){return fixtureImages[part];}
Image PanelArtwork(){return {};}
}
void CaptureSettings(const std::filesystem::path& root,const std::string& name,unsigned char* atlas,int tw,int th){
    std::filesystem::create_directories(root);
    std::ofstream texture(root/"atlas.rgba",std::ios::binary);texture.write(reinterpret_cast<char*>(atlas),size_t(tw)*th*4);
    const auto data=ImGui::GetDrawData();uint32_t commands=0;for(auto list:data->CmdLists)commands+=list->CmdBuffer.Size;
    std::ofstream out(root/(name+".bin"),std::ios::binary);
    uint32_t header[]{uint32_t(data->DisplaySize.x),uint32_t(data->DisplaySize.y),uint32_t(tw),uint32_t(th),uint32_t(data->TotalVtxCount),uint32_t(data->TotalIdxCount),commands};
    out.write(reinterpret_cast<char*>(header),sizeof(header));
    for(auto list:data->CmdLists)out.write(reinterpret_cast<char*>(list->VtxBuffer.Data),list->VtxBuffer.Size*sizeof(ImDrawVert));
    uint32_t base=0;for(auto list:data->CmdLists){for(auto i:list->IdxBuffer){const uint32_t value=base+i;out.write(reinterpret_cast<const char*>(&value),4);}base+=list->VtxBuffer.Size;}
    uint32_t first=0;for(auto list:data->CmdLists){for(const auto& command:list->CmdBuffer){const uint32_t values[]{first+command.IdxOffset,command.ElemCount};const uint64_t id=command.GetTexID();out.write(reinterpret_cast<const char*>(values),8);out.write(reinterpret_cast<const char*>(&command.ClipRect),16);out.write(reinterpret_cast<const char*>(&id),8);}first+=list->IdxBuffer.Size;}
}
ImGuiWindow* ActiveSettingsWindow(const char* prefix){
    for(auto window:ImGui::GetCurrentContext()->Windows){const auto last=strrchr(window->Name,'/');if(window->Active&&last&&std::string_view(last+1).starts_with(prefix))return window;}return nullptr;
}
float Luminance(ImVec4 color){
    auto linear=[](float v){return v<=.04045f?v/12.92f:powf((v+.055f)/1.055f,2.4f);};
    return .2126f*linear(color.x)+.7152f*linear(color.y)+.0722f*linear(color.z);
}
float Contrast(ImVec4 a,ImVec4 b){const float x=Luminance(a),y=Luminance(b);return ((std::max)(x,y)+.05f)/((std::min)(x,y)+.05f);}
int main(int argc,char** argv){
    assert(argc==5);const std::filesystem::path data=argv[2];
    using namespace LegacyTransmog::Skin;
    struct Asset {Part part;const char* file;uint64_t texture;};
    for(const auto& asset:std::array<Asset,3>{{{JournalBackground,"ReMoDDeD/Panels/Quest/QuestBG_R.sprite",3},{JournalButton,"ReMoDDeD/Panels/Buttons/ButtonMed_R.sprite",5},{Close,"ReMoDDeD/Panels/Buttons/Exit.sprite",6}}}){
        std::ifstream input(data/asset.file,std::ios::binary|std::ios::ate);assert(input);
        std::vector<uint8_t> bytes(size_t(input.tellg()));input.seekg(0);input.read(reinterpret_cast<char*>(bytes.data()),bytes.size());
        Sprite sprite;assert(Decode(bytes,sprite));const auto& a=sprite.atlas;fixtureImages[asset.part]={asset.texture,a.width,a.height,a.frameWidth,a.frames,sprite.pitch};
    }
    StatRanges::config.hotkey=true;StatRanges::config.hold=false;StatRanges::config.fixed=true;
    StatRanges::config.size=22;StatRanges::config.color={188/255.f,231/255.f,10/255.f,1};strcpy_s(StatRanges::config.format,"[Min: %d, Max: %d]");
    for(auto display:{ImVec2{960,720},ImVec2{1280,720},ImVec2{1920,1080},ImVec2{3840,2160}}){
        ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize=display;io.DeltaTime=1.f/60;
        for(float size:{12.f,15.f,18.f,24.f})assert(io.Fonts->AddFontFromFileTTF(argv[3],size));
        HudSettings::bodyFont=io.Fonts->AddFontFromFileTTF(argv[4],36);assert(HudSettings::bodyFont);HudSettings::bodyFont->Scale=.5f;
        HudSettings::noteFont=io.Fonts->AddFontFromFileTTF(argv[4],32);assert(HudSettings::noteFont);HudSettings::noteFont->Scale=.5f;
        HudSettings::headingFont=io.Fonts->Fonts[3];
        unsigned char* pixels;int tw,th;io.Fonts->GetTexDataAsRGBA32(&pixels,&tw,&th);io.Fonts->SetTexID(1);
        const float scale=display.x>=3840?2:1;io.FontGlobalScale=scale;HudMenu::nativeTheme=true;HudMenu::ApplySanctuaryPalette();
        const auto& colors=ImGui::GetStyle().Colors;
        assert(Contrast(colors[ImGuiCol_Text],colors[ImGuiCol_FrameBg])>7);
        assert(Contrast(colors[ImGuiCol_SliderGrab],colors[ImGuiCol_FrameBg])>3);
        auto page=HudMenu::Page::Ranges;bool open=true;
        auto frame=[&]{
            ImGui::NewFrame();ImGui::PushFont(io.Fonts->Fonts[1]);
            HudMenu::Workspace(page,open,scale,"RMD-MP / D2RHUD",[&](HudMenu::Page){
                HudMenu::Begin("RangeEditor",&open,ImGuiWindowFlags_NoTitleBar);
                const float footer=ImGui::GetTextLineHeightWithSpacing()+6*scale;
                ImGui::BeginChild("PreferenceFields",{0,-footer},0,ImGuiWindowFlags_NoBackground);
                StatRanges::DrawSettingsPanel(scale);ImGui::EndChild();HudSettings::Note("Changes apply and save automatically.");HudMenu::End();
            });ImGui::PopFont();ImGui::Render();
        };
        for(int settle=0;settle<6;++settle)frame();
        assert(ActiveSettingsWindow("RangePreview_")&&ActiveSettingsWindow("RangeActivation_")&&ActiveSettingsWindow("RangeAppearance_"));
        assert(!ActiveSettingsWindow("RangeDiagnostics_")&&!ActiveSettingsWindow("RangeFormat_"));
        for(const auto prefix:{"RangePreview_","RangeActivation_","RangeAppearance_"}){
            auto window=ActiveSettingsWindow(prefix);if(window->ScrollMax.x>0)std::cerr<<"Overflow "<<display.x<<" "<<prefix<<" scroll="<<window->ScrollMax.x<<" content="<<window->ContentSize.x<<" inner="<<window->InnerRect.GetWidth()<<" size="<<window->Size.x<<"\n";assert(window->ScrollMax.x==0);assert(window->ContentSize.x<=window->InnerRect.GetWidth()+1);
        }
        CaptureSettings(argv[1],"settings-display-"+std::to_string(int(display.x)),pixels,tw,th);
        auto fields=ActiveSettingsWindow("PreferenceFields_");assert(fields);
        auto tabs=ImGui::GetCurrentContext()->TabBars.GetByKey(fields->GetID("StatRangeSections"));assert(tabs&&tabs->Tabs.Size==3);
        auto clickTab=[&](int index){
            const auto tab=tabs->Tabs[index];io.AddMousePosEvent(tabs->BarRect.Min.x+tab.Offset+tab.Width*.5f,tabs->BarRect.GetCenter().y);frame();
            io.AddMouseButtonEvent(0,true);frame();io.AddMouseButtonEvent(0,false);frame();frame();
        };
        clickTab(1);assert(ActiveSettingsWindow("RangeFormat_")&&!ActiveSettingsWindow("RangeDiagnostics_"));
        CaptureSettings(argv[1],"settings-format-"+std::to_string(int(display.x)),pixels,tw,th);
        clickTab(2);assert(ActiveSettingsWindow("RangeDiagnostics_")&&!ActiveSettingsWindow("RangeAppearance_"));
        CaptureSettings(argv[1],"settings-help-"+std::to_string(int(display.x)),pixels,tw,th);
        ImGui::DestroyContext();HudSettings::bodyFont=HudSettings::noteFont=HudSettings::headingFont=nullptr;
    }
    std::cout<<"Settings UI: actual Stat Ranges tabs, separate help/diagnostics, no horizontal overflow at 960/1280/1920/3840, native panels and Formal 436, text/slider contrast passed.\n";
}
