#define NOMINMAX
#include "../d2rhud/plugin/D2RHUD/CameraEditor.h"
#include "../d2rhud/plugin/D2RHUD/HudPresentation.h"
#include "../d2rhud/plugin/D2RHUD/MenuWorkspace.h"
#include <imgui_internal.h>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
int checks=0,saves=0,angleWrites=0,zoomWrites=0,refreshes=0;
std::array<float,4> nativeAngles{1,2,3,4};float nativeZoom=5;
HudCamera::Controller* current=nullptr;
std::string action;
bool saveWorks=true,editPreference=false,preferenceValue=false;
void Push(int);void Pop(int);bool Button(const char*,ImVec2);
void Check(bool value,const char* text){++checks;if(!value)throw std::runtime_error(text);}
bool Read(uintptr_t at,void* out,size_t size){
    if(at==reinterpret_cast<uintptr_t>(nativeAngles.data())&&size==sizeof(nativeAngles)){memcpy(out,nativeAngles.data(),size);return true;}
    if(at==reinterpret_cast<uintptr_t>(&nativeZoom)&&size==sizeof(nativeZoom)){memcpy(out,&nativeZoom,size);return true;}return false;
}
void PresentationTests(){
    ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={1280,720};io.DeltaTime=1.f/60;
    io.Fonts->AddFontDefault();unsigned char* bytes;int w,h;io.Fonts->GetTexDataAsRGBA32(&bytes,&w,&h);
    HudPresentation::PreferenceState state;bool open=true;
    HudPresentation::PreferenceHost host{1,Push,Pop,[](const char* text,bool*){ImGui::TextUnformatted(text);return true;},
        []{++saves;return saveWorks;},[](const char* text){return Button(text,{});}};
    auto draw=[](float){if(editPreference){preferenceValue=!preferenceValue;editPreference=false;}ImGui::Checkbox("Setting",&preferenceValue);};
    auto snapshot=[]()->nlohmann::ordered_json{return {{"enabled",preferenceValue}};};
    auto frame=[&]{ImGui::NewFrame();HudPresentation::Preferences("Preference lifecycle",open,{640,480},state,draw,snapshot,host);ImGui::Render();};
    const auto baseline=saves;frame();frame();Check(saves==baseline,"Unchanged preference rendering never saves");
    editPreference=true;frame();Check(saves==baseline+1&&!state.dirty&&!state.failed,"Edited preference saves once");
    saveWorks=false;editPreference=true;frame();Check(state.failed&&state.dirty,"Failed save keeps changed data dirty");
    for(int i=0;i<100;++i)frame();Check(saves==baseline+2,"Failed save does not retry on every rendered frame");
    saveWorks=true;action="Retry saving";frame();Check(action.empty()&&!state.failed&&!state.dirty&&saves==baseline+3,"Explicit retry saves and clears failure state");
    state.dirty=true;open=false;frame();Check(saves==baseline+4&&!state.wasOpen&&!state.dirty,"Closing flushes pending changes once");
    frame();Check(saves==baseline+4,"Closed preference stays idle");
    using Clock=std::chrono::steady_clock;const Clock::time_point began{};
    bool visible=true;
    auto toast=[&](const char* text,int milliseconds){ImGui::NewFrame();const auto fonts=GImGui->FontStack.Size;HudPresentation::Toast(text,visible,began,began+std::chrono::milliseconds(milliseconds));Check(fonts==GImGui->FontStack.Size,"Toast leaves font stack balanced");ImGui::Render();return ImGui::GetDrawData()->TotalVtxCount;};
    Check(toast("Filter changed",2999)>0&&visible,"Ordinary status remains visible before three seconds");
    Check(toast("Filter changed",3000)==0&&!visible,"Ordinary status expires at three seconds");
    visible=true;Check(toast("D2RHUD loaded",4000)>0&&visible,"HUD status keeps five-second duration");
    Check(toast("D2RHUD loaded",5000)==0&&!visible,"HUD status expires at five seconds");
    ImGui::DestroyContext();
}
bool Write(uintptr_t at,const void* value,size_t size){
    if(at==reinterpret_cast<uintptr_t>(nativeAngles.data())&&size==sizeof(nativeAngles)){memcpy(nativeAngles.data(),value,size);++angleWrites;return true;}
    if(at==reinterpret_cast<uintptr_t>(&nativeZoom)&&size==sizeof(nativeZoom)){memcpy(&nativeZoom,value,size);++zoomWrites;return true;}return false;
}
void Push(int){ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);}
void Pop(int){ImGui::PopFont();}
bool Button(const char* label,ImVec2 size){
    const bool clicked=ImGui::Button(label,size);
    if(action==label&&!(GImGui->CurrentItemFlags&ImGuiItemFlags_Disabled)){action.clear();return true;}
    return clicked;
}
HudCameraEditor::Host Host(){return {
    1,ImVec4(0,0,0,1),[]{},[](ImVec2 size){ImGui::SetNextWindowPos({10,10});ImGui::SetNextWindowSize(size);},Push,Pop,
    [](const char* title,bool*){ImGui::TextUnformatted(title);return true;},[](const char* text){ImGui::TextUnformatted(text);},nullptr,
    Button,[](const char* label,bool* value){return ImGui::Checkbox(label,value);},[]{},[]{},[]{++saves;return true;},
    []{++refreshes;current->Tick(1,true,false);},[]{return current->ApplyAngles();},[]{return current->ApplyZoom();}
};}
void Tests(){
    ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DeltaTime=1.f/60;io.DisplaySize={1920,1080};
    io.Fonts->AddFontDefault();unsigned char* bytes;int w,h;io.Fonts->GetTexDataAsRGBA32(&bytes,&w,&h);
    HudCamera::Controller controller({[](const char* pattern){return pattern[0]=='4'?reinterpret_cast<uintptr_t>(&nativeAngles[1]):reinterpret_cast<uintptr_t>(&nativeZoom)-0x32;},Read,Write});
    current=&controller;
    HudCameraEditor::State state;bool open=false,autoload=false;int last=0;
    std::array<HudPreferences::CameraPreset,3> presets{};
    auto frame=[&]{ImGui::NewFrame();HudCameraEditor::Draw(state,open,{presets,last,autoload},controller,Host());ImGui::Render();};
    frame();Check(!state.wasOpen&&refreshes==0&&saves==0,"Closed camera page has no side effects");
    open=true;frame();
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(4);
    while(controller.Busy()||!controller.View().valuesFound){controller.Tick(1,true,false);frame();if(std::chrono::steady_clock::now()>deadline)throw std::runtime_error("Scan never completed");std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    Check(controller.View().defaultPitch==2&&controller.View().defaultZoom==5,"Editor keeps native default capture");
    Check(refreshes==1&&saves==0&&angleWrites==0&&zoomWrites==0,"Opening/rendering alone does not save or write camera memory");
    state.selectedPreset=1;frame();
    auto& camera=controller.View();camera.pitch=6;camera.zoom=7;
    action="Save Current to Preset";frame();
    Check(action.empty()&&presets[0].HasValues&&presets[0].Pitch==6&&presets[0].Zoom==7&&saves==1,"Save preset preserves current values and calls host persistence once");
    camera.pitch=2;camera.zoom=5;action="Apply Preset";frame();
    Check(action.empty()&&nativeAngles[1]==6&&nativeZoom==7&&last==1&&saves==2,"Apply preset routes writes through host callbacks and saves last slot");
    action="Reset Angles";frame();Check(action.empty()&&nativeAngles[1]==2,"Reset angles uses captured defaults");
    action="Reset Zoom";frame();Check(action.empty()&&nativeZoom==5,"Reset zoom uses captured default");
    for(const auto display:{ImVec2(1280,720),ImVec2(1920,1080),ImVec2(3840,2160)}){
        io.DisplaySize=display;frame();frame();Check(ImGui::GetDrawData()->TotalVtxCount>0,"Relocated camera page renders at supported viewport sizes");
    }
    open=false;frame();autoload=true;last=1;open=true;frame();
    Check(nativeAngles[1]==6&&nativeZoom==7&&state.autoloadDone,"Reopen autoload keeps saved preset behavior");
    const auto writes=angleWrites+zoomWrites;frame();frame();
    Check(angleWrites+zoomWrites==writes,"Autoload writes once per open/capture boundary");
    open=false;frame();Check(!state.wasOpen,"Close resets editor lifecycle");
    ImGui::DestroyContext();
}
}
DWORD64 Pattern::Address(uint32_t){return 0;}
DWORD64 Pattern::ScanProcess(const char*){return 0;}
namespace LegacyTransmog::Skin {Image PanelArtwork(){return {};}Image ArtworkImage(Part){return {};}}
int main(){try{Tests();PresentationTests();std::cout<<checks<<" editor/presentation checks passed; real ImGui and private camera memory.\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
