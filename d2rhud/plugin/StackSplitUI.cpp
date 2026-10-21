#include "LegacyTransmog.h"
#include "StackSplitSkin.h"
#include <imgui.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cfloat>

namespace LegacyTransmog {
namespace Skin {
namespace {std::atomic<Artwork> artworkProvider{nullptr};std::atomic<Font> fontProvider{nullptr};std::atomic<ItemArtwork> itemProvider{nullptr};}
void SetProviders(Artwork artwork,Font font,ItemArtwork item){artworkProvider.store(artwork);fontProvider.store(font);itemProvider.store(item);}
Image ArtworkImage(Part part){if(part>=0&&part<Count)if(auto provider=artworkProvider.load())return provider(part);return {};}
Image PanelArtwork(){return ArtworkImage(Frame);}
}
namespace {
std::atomic<bool> minimalist{false};
void Picture(const Skin::Image& image,int frame,ImVec2 a,ImVec2 b){
    if(!image.texture || image.width<=0 || image.height<=0 || image.frames<=0)return;
    frame=std::clamp(frame,0,image.frames-1);
    const float start=float(frame*image.pitch)/image.width,end=float(frame*image.pitch+image.frameWidth)/image.width;
    ImGui::GetWindowDrawList()->AddImage(ImTextureID(image.texture),a,b,{start,0},{end,1});
}
void Sliced(const Skin::Image& image,ImVec2 a,ImVec2 b,float left,float top,float right,float bottom,float dl,float dt,float dr,float db,int frame=0,float sourceLeft=0){
    if(!image.texture || image.width<=0 || image.height<=0)return;
    frame=std::clamp(frame,0,image.frames-1);
    const float x[]{a.x,a.x+dl,b.x-dr,b.x},y[]{a.y,a.y+dt,b.y-db,b.y};
    const float u[]{float(frame*image.pitch+sourceLeft)/image.width,float(frame*image.pitch+sourceLeft+left)/image.width,float(frame*image.pitch+image.frameWidth-right)/image.width,float(frame*image.pitch+image.frameWidth)/image.width};
    const float v[]{0,top/image.height,1-bottom/image.height,1};
    for(int row=0;row<3;++row)for(int col=0;col<3;++col)
        ImGui::GetWindowDrawList()->AddImage(ImTextureID(image.texture),{x[col],y[row]},{x[col+1],y[row+1]},{u[col],v[row]},{u[col+1],v[row+1]});
}
ImFont* Face(float size){if(auto provider=Skin::fontProvider.load())if(auto font=provider(size))return font;return ImGui::GetFont();}
void Caption(const char* text,ImVec2 position,float size,ImU32 color,float alignment=0){
    auto font=Face(size);const auto extent=font->CalcTextSizeA(size,FLT_MAX,0,text);
    ImGui::GetWindowDrawList()->AddText(font,size,{position.x-extent.x*alignment,position.y},color,text);
}
bool ArtButton(const char* id,const Skin::Image& image,ImVec2 p,ImVec2 size,int hover,bool enabled=true,const char* label=nullptr,float scale=1){
    ImGui::SetCursorScreenPos(p);ImGui::BeginDisabled(!enabled);
    const bool clicked=ImGui::InvisibleButton(id,size);const bool hot=ImGui::IsItemHovered(),pressed=ImGui::IsItemActive();
    ImGui::EndDisabled();const int frame=!enabled?2:pressed?1:hot?hover:0;
    if(label)Sliced(image,p,{p.x+size.x,p.y+size.y},70,20,70,20,12*scale,5*scale,12*scale,5*scale,frame);
    else Picture(image,frame,p,{p.x+size.x,p.y+size.y});
    if(label)Caption(label,{p.x+size.x*.5f,p.y+(size.y-13*scale)*.5f},13*scale,enabled?IM_COL32(244,228,194,255):IM_COL32(120,116,107,255),.5f);
    return clicked;
}
}
void SetMinimalistQuantityPopup(bool enabled){minimalist.store(enabled);}
void DrawStackSplit(){
    const auto view=GetSplitView();static uint32_t lastToken=0;static int amount=1;
    if(view.phase==SplitView::Closed){lastToken=0;return;}
    if(view.token!=lastToken){lastToken=view.token;amount=1;}
    auto& io=ImGui::GetIO();if(io.DisplaySize.x<64 || io.DisplaySize.y<64)return;
    const float scale=1.3f*std::clamp(io.DisplaySize.y/1080.0f,0.65f,2.0f);
    const bool compact=minimalist.load();
    const float panelWidth=compact?216.0f:236.0f,panelHeight=compact?88.0f:164.0f;
    const float width=(std::min)(panelWidth*scale,io.DisplaySize.x-8),height=panelHeight*scale,gap=10*scale;
    const ImVec2 anchor{view.anchorX*io.DisplaySize.x,view.anchorY*io.DisplaySize.y};
    const float x=std::clamp(anchor.x-width*.5f,4.0f,(std::max)(4.0f,io.DisplaySize.x-width-4));
    float y=anchor.y-height-gap;if(y<4)y=anchor.y+gap;y=std::clamp(y,4.0f,(std::max)(4.0f,io.DisplaySize.y-height-4));
    std::array<Skin::Image,Skin::Count> images{};if(auto provider=Skin::artworkProvider.load())for(int i=0;i<Skin::Count;++i)images[i]=provider(Skin::Part(i));
    ImGui::SetNextWindowPos({x,y},ImGuiCond_Always);ImGui::SetNextWindowSize({width,height},ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{0,0});ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize,0);
    constexpr auto flags=ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove|
        ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoFocusOnAppearing|ImGuiWindowFlags_NoNavFocus|ImGuiWindowFlags_NoBackground;
    ImGui::Begin("##StackQuantity",nullptr,flags);
    auto point=[&](float px,float py){return ImVec2{x+px*scale,y+py*scale};};
    Sliced(images[Skin::Frame],{x,y},{x+width,y+height},60,96,60,50,10*scale,23*scale,10*scale,9*scale);
    Caption(compact?"Quantity":"Split stack",point(panelWidth*.5f,8),12*scale,IM_COL32(211,186,136,255),.5f);
    if(images[Skin::Close].texture){if(ArtButton("##Close",images[Skin::Close],point(panelWidth-28,6),{18*scale,18*scale},3))CloseSplit(view.token);}
    else if(ArtButton("##Close",images[Skin::Confirm],point(panelWidth-42,6),{32*scale,18*scale},3,true,"Back",scale*.8f))CloseSplit(view.token);
    bool editing=false;
    if(view.phase==SplitView::Choosing){
        if(!compact){
            Caption("Keep",point(53,30),10*scale,IM_COL32(187,172,143,255),.5f);
            Caption("New stack",point(183,30),10*scale,IM_COL32(221,197,151,255),.5f);
            Skin::Image item;if(auto provider=Skin::itemProvider.load())item=provider(view.iconCode);
            for(float center:{53.0f,183.0f}){
                Sliced(images[Skin::Input],point(center-38,46),point(center+38,94),14,22,42,22,5*scale,6*scale,8*scale,6*scale,0,100);
                if(item.texture && item.frameWidth>0 && item.height>0){
                    const float fit=(std::min)(58.0f/item.frameWidth,38.0f/item.height);
                    const float w=item.frameWidth*fit,h=item.height*fit;
                    Picture(item,0,point(center-w*.5f,70-h*.5f),point(center+w*.5f,70+h*.5f));
                }else Caption("Item",point(center,64),11*scale,IM_COL32(161,151,130,255),.5f);
            }
            auto draw=ImGui::GetWindowDrawList();const auto color=IM_COL32(213,184,116,255);
            draw->AddLine(point(103,70),point(129,70),color,2*scale);
            draw->AddTriangleFilled(point(133,70),point(125,65),point(125,75),color);
        }
        const float inputX=compact?15.0f:115.0f,inputY=compact?29.0f:98.0f;
        Sliced(images[Skin::Input],point(inputX,inputY),point(inputX+106,inputY+34),100,22,42,22,27*scale,6*scale,8*scale,6*scale);
        if(ArtButton("##Increase",images[Skin::Up],point(inputX+7,inputY+4),{13*scale,12*scale},2) && amount<int(view.total)-1)amount=(std::max)(1,amount+1);
        if(ArtButton("##Decrease",images[Skin::Down],point(inputX+7,inputY+18),{13*scale,12*scale},2))amount=amount>1?amount-1:1;
        ImGui::SetCursorScreenPos(point(inputX+30,inputY+5));ImGui::SetNextItemWidth(65*scale);
        ImGui::PushFont(Face(17*scale));ImGui::SetWindowFontScale(17*scale/(ImGui::GetFont()->FontSize*io.FontGlobalScale));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,{2*scale,2*scale});ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize,0);
        ImGui::PushStyleColor(ImGuiCol_FrameBg,{0,0,0,0});ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,{0,0,0,0});ImGui::PushStyleColor(ImGuiCol_FrameBgActive,{0,0,0,0});
        ImGui::PushStyleColor(ImGuiCol_Text,{0.86f,0.78f,0.61f,1});
        ImGui::InputInt("##Amount",&amount,0,0,ImGuiInputTextFlags_AutoSelectAll);
        editing=ImGui::IsItemActive();
        const bool enter=(editing || ImGui::IsItemDeactivated()) && ImGui::IsKeyPressed(ImGuiKey_Enter,false);
        ImGui::PopStyleColor(4);ImGui::PopStyleVar(2);ImGui::PopFont();ImGui::SetWindowFontScale(1);
        const bool valid=amount>=1 && uint32_t(amount)<view.total;
        if(ArtButton("##Apply",images[Skin::Confirm],point(compact?131:145,compact?32:135),{(compact?69:76)*scale,(compact?29:22)*scale},3,valid,"Split",scale) || (enter && valid))SubmitSplit(view.token,amount);
        char remaining[48]{},total[48]{};
        if(valid)std::snprintf(remaining,sizeof(remaining),compact?"%u left":"%u",view.total-uint32_t(amount));else std::snprintf(remaining,sizeof(remaining),compact?"1 - %u":"--",view.total-1);
        std::snprintf(total,sizeof(total),"Total %u",view.total);
        if(compact){Caption(remaining,point(18,68),11*scale,IM_COL32(161,151,130,255));Caption(total,point(198,68),11*scale,IM_COL32(161,151,130,255),1);}
        else{
            Caption(remaining,point(53,105),17*scale,IM_COL32(219,203,171,255),.5f);
            Caption(total,point(16,140),10*scale,IM_COL32(161,151,130,255));
        }
    }else{
        const char* text=view.phase==SplitView::Error?(view.message.find("No free")!=std::string::npos?"No free slot":"Unable to split"):view.phase==SplitView::Submitting?"Applying...":"Splitting...";
        Caption(text,point(panelWidth*.5f,panelHeight*.5f-4),13*scale,IM_COL32(211,186,136,255),.5f);
    }
    SetSplitInputRegion(view.token,{LONG(x),LONG(y),LONG(x+width),LONG(y+height)},int(io.DisplaySize.x),int(io.DisplaySize.y),editing);
    ImGui::End();ImGui::PopStyleVar(2);
}
}
