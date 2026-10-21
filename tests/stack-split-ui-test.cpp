#include "../d2rhud/plugin/LegacyTransmog.h"
#include "../d2rhud/plugin/StackSplitSkin.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <cassert>
#include <cmath>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <vector>
namespace LegacyTransmog {
SplitView fixture;RECT region{};bool editing=false;int submissions=0,lastAmount=0;
SplitView GetSplitView(){return fixture;}
void SetSplitInputRegion(uint32_t token,RECT r,int,int,bool active){assert(token==fixture.token);region=r;editing=active;}
void SubmitSplit(uint32_t token,int amount){assert(token==fixture.token);++submissions;lastAmount=amount;}
void CloseSplit(uint32_t token){assert(token==fixture.token);fixture.phase=SplitView::Closed;}
}
using namespace LegacyTransmog;
Skin::Collection artwork;
Skin::Sprite itemArtwork;int itemCalls=0;
Skin::Image ItemArtwork(const std::string& code){++itemCalls;assert(code=="Y20");const auto& a=itemArtwork.atlas;return {8,a.width,a.height,a.frameWidth,a.frames,itemArtwork.pitch};}
std::vector<ImFont*> faces;
Skin::Image Artwork(Skin::Part part){const auto& sprite=artwork[part];const auto& a=sprite.atlas;return {uint64_t(2+part),a.width,a.height,a.frameWidth,a.frames,sprite.pitch};}
ImFont* Font(float size){for(auto font:faces)if(font->FontSize>=size)return font;return faces.empty()?nullptr:faces.back();}
void SaveFrame(const std::filesystem::path& folder,const std::string& name,int tw,int th){
    const auto data=ImGui::GetDrawData();std::ofstream out(folder/(name+".bin"),std::ios::binary);
    const uint32_t header[]{uint32_t(data->DisplaySize.x),uint32_t(data->DisplaySize.y),uint32_t(tw),uint32_t(th),uint32_t(data->TotalVtxCount),uint32_t(data->TotalIdxCount)};out.write(reinterpret_cast<const char*>(header),sizeof(header));
    for(int l=0;l<data->CmdListsCount;++l)out.write(reinterpret_cast<const char*>(data->CmdLists[l]->VtxBuffer.Data),size_t(data->CmdLists[l]->VtxBuffer.Size)*sizeof(ImDrawVert));
    uint32_t offset=0;for(int l=0;l<data->CmdListsCount;++l){auto list=data->CmdLists[l];for(auto i:list->IdxBuffer){uint32_t index=i+offset;out.write(reinterpret_cast<const char*>(&index),4);}offset+=list->VtxBuffer.Size;}
    uint32_t commands=0;for(int l=0;l<data->CmdListsCount;++l)commands+=data->CmdLists[l]->CmdBuffer.Size;out.write(reinterpret_cast<const char*>(&commands),4);
    uint32_t first=0;for(int l=0;l<data->CmdListsCount;++l){auto list=data->CmdLists[l];for(const auto& cmd:list->CmdBuffer){const uint32_t start=first+cmd.IdxOffset,n=cmd.ElemCount;const uint64_t texture=cmd.GetTexID();out.write(reinterpret_cast<const char*>(&start),4);out.write(reinterpret_cast<const char*>(&n),4);out.write(reinterpret_cast<const char*>(&texture),8);}first+=list->IdxBuffer.Size;}
}
int main(int argc,char** argv){
    assert(argc>2);const std::filesystem::path source=argv[2];
    const char* files[]{"modal_bg.sprite","gold_drop_bar.sprite","modal_up_arrow.sprite","modal_down_arrow.sprite","modalbutton.sprite","Exit.sprite"};
    for(int i=0;i<=Skin::Close;++i){std::ifstream in(source/files[i],std::ios::binary|std::ios::ate);assert(in);std::vector<uint8_t> bytes(size_t(in.tellg()));in.seekg(0);in.read(reinterpret_cast<char*>(bytes.data()),bytes.size());assert(Skin::Decode(bytes,artwork[i]));
        Skin::Sprite bad;assert(!Skin::Decode({bytes.data(),39},bad) && !Skin::Decode({bytes.data(),bytes.size()-1},bad));}
    assert(artwork[Skin::Confirm].atlas.frames==4 && artwork[Skin::Confirm].pitch==532);
    {std::ifstream in(source/"goldbar.sprite",std::ios::binary|std::ios::ate);assert(in);std::vector<uint8_t> bytes(size_t(in.tellg()));in.seekg(0);in.read(reinterpret_cast<char*>(bytes.data()),bytes.size());assert(Skin::Decode(bytes,itemArtwork));}
    assert(Skin::IconPaths("..//custom//goldbar")==std::vector<std::string>{"hd/global/ui/items/custom/goldbar.sprite"});
    assert(Skin::IconPaths("quiver/arrows").front()=="hd/global/ui/items/misc/quiver/arrows.sprite");
    assert(Skin::IconPaths("items/weapon/sword/crystal_sword").front()=="hd/global/ui/items/weapon/sword/crystal_sword.sprite");
    for(const auto path:{"../../outside","C:/outside","/absolute"})assert(Skin::IconPaths(path).empty());
    Skin::SetProviders(Artwork,Font,ItemArtwork);
    ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DeltaTime=1.0f/60;io.ConfigInputTrickleEventQueue=false;
    io.Fonts->AddFontDefault();if(argc>3 && argv[3][0])for(float size:{16.0f,24.0f,32.0f,48.0f,64.0f,96.0f,128.0f}){
        ImFontConfig config;config.OversampleH=2;config.OversampleV=1;config.PixelSnapH=true;
        faces.push_back(io.Fonts->AddFontFromFileTTF(argv[3],size,&config));
    }
    unsigned char* pixels;int tw,th;io.Fonts->GetTexDataAsRGBA32(&pixels,&tw,&th);io.Fonts->SetTexID(1);
    if(argc>1){std::filesystem::create_directories(argv[1]);std::ofstream out(std::filesystem::path(argv[1])/"atlas.rgba",std::ios::binary);out.write(reinterpret_cast<char*>(pixels),size_t(tw)*th*4);}
    auto frame=[](){ImGui::NewFrame();DrawStackSplit();ImGui::Render();};
    uint32_t token=0;
    for(bool compact:{false,true}){
    SetMinimalistQuantityPopup(compact);itemCalls=0;
    const std::string prefix=compact?"minimal-":"visual-";
    for(const auto display:{ImVec2{1280,720},ImVec2{1920,1080},ImVec2{3840,2160}}){
        io.DisplaySize=display;
        for(auto anchor:{ImVec2{0.75f,0.75f},ImVec2{0.01f,0.01f},ImVec2{0.99f,0.99f}}){
            fixture={SplitView::Choosing,++token,77,100,88,0,"Gold bars",""};fixture.anchorX=anchor.x;fixture.anchorY=anchor.y;fixture.iconCode="Y20";
            io.AddMousePosEvent(1,display.y-1);frame();frame();
            auto window=ImGui::FindWindowByName("##StackQuantity");assert(window && window->Active);
            assert(!(window->Flags&(ImGuiWindowFlags_Modal|ImGuiWindowFlags_Popup)) && ImGui::GetCurrentContext()->OpenPopupStack.empty());
            assert(window->Size.x<=display.x*0.17f && window->Size.y<=display.y*(compact?0.12f:0.21f));
            assert(window->Pos.x>=0 && window->Pos.y>=0 && window->Pos.x+window->Size.x<=display.x && window->Pos.y+window->Size.y<=display.y);
            assert(!editing && !io.WantCaptureKeyboard && !io.WantCaptureMouse);
            for(int l=0;l<ImGui::GetDrawData()->CmdListsCount;++l)for(const auto& v:ImGui::GetDrawData()->CmdLists[l]->VtxBuffer){
                assert(v.pos.x>=window->Pos.x-2 && v.pos.x<=window->Pos.x+window->Size.x+2 && v.pos.y>=window->Pos.y-2 && v.pos.y<=window->Pos.y+window->Size.y+2);
            }
            if(anchor.x==0.75f){assert(window->Pos.y+window->Size.y<anchor.y*display.y);if(argc>1)SaveFrame(argv[1],prefix+"quantity-"+std::to_string(int(display.x)),tw,th);}
        }
    }
    assert(compact?itemCalls==0:itemCalls>0);
    submissions=0;
    io.DisplaySize={1920,1080};fixture={SplitView::Choosing,++token,77,100,88,0,"Gold bars",""};fixture.anchorX=.75f;fixture.anchorY=.75f;fixture.iconCode="Y20";frame();frame();
    const auto window=ImGui::FindWindowByName("##StackQuantity");const auto pos=window->Pos;
    io.AddMousePosEvent(pos.x+(compact?80:180)*1.3f,pos.y+(compact?46:115)*1.3f);frame();io.AddMouseButtonEvent(0,true);frame();io.AddMouseButtonEvent(0,false);frame();assert(editing);
    io.AddKeyEvent(ImGuiKey_ModCtrl,true);io.AddKeyEvent(ImGuiKey_A,true);frame();io.AddKeyEvent(ImGuiKey_A,false);io.AddKeyEvent(ImGuiKey_ModCtrl,false);frame();
    io.AddInputCharactersUTF8("20");frame();frame();if(argc>1)SaveFrame(argv[1],prefix+"quantity-twenty",tw,th);
    io.AddKeyEvent(ImGuiKey_Enter,true);frame();io.AddKeyEvent(ImGuiKey_Enter,false);frame();assert(submissions==1 && lastAmount==20);
    fixture.total=10000;io.AddMouseButtonEvent(0,true);frame();io.AddMouseButtonEvent(0,false);frame();
    io.AddKeyEvent(ImGuiKey_ModCtrl,true);io.AddKeyEvent(ImGuiKey_A,true);frame();io.AddKeyEvent(ImGuiKey_A,false);io.AddKeyEvent(ImGuiKey_ModCtrl,false);frame();io.AddInputCharactersUTF8("9999");frame();
    frame();if(argc>1)SaveFrame(argv[1],prefix+"quantity-four-digits",tw,th);
    io.AddKeyEvent(ImGuiKey_Enter,true);frame();io.AddKeyEvent(ImGuiKey_Enter,false);frame();assert(submissions==2 && lastAmount==9999);
    io.AddMouseButtonEvent(0,true);frame();io.AddMouseButtonEvent(0,false);frame();
    io.AddKeyEvent(ImGuiKey_ModCtrl,true);io.AddKeyEvent(ImGuiKey_A,true);frame();io.AddKeyEvent(ImGuiKey_A,false);io.AddKeyEvent(ImGuiKey_ModCtrl,false);frame();
    io.AddInputCharactersUTF8("1234");frame();io.AddMousePosEvent(pos.x+(compact?165:183)*1.3f,pos.y+(compact?46:146)*1.3f);frame();
    io.AddMouseButtonEvent(0,true);frame();io.AddMouseButtonEvent(0,false);frame();assert(submissions==3 && lastAmount==1234);
    fixture.phase=SplitView::Closed;frame();assert(!ImGui::FindWindowByName("##StackQuantity")->Active);
    }
    faces.clear();ImGui::DestroyContext();std::cout<<"Visual/minimal quantity layouts at 720p/1080p/4K, native item art, safe custom paths, four-digit entry, Enter/click submission, no dim layer/automatic focus passed.\n";
}
