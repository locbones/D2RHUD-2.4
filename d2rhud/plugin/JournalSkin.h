#pragma once
#include "StackSplitSkin.h"
#include <imgui.h>
#include <algorithm>

// Native RMD quest-log panels and button atlases, with ImGui handling input.
namespace QuestJournalSkin {
using Image=LegacyTransmog::Skin::Image;
inline constexpr ImVec4 Gold{.84f,.71f,.43f,1},Paper{.84f,.80f,.70f,1},Muted{.53f,.50f,.44f,1};
inline Image background,tab,button,close,modal;
inline void Load(){
    using namespace LegacyTransmog::Skin;
    background=ArtworkImage(JournalBackground);tab=ArtworkImage(JournalTab);button=ArtworkImage(JournalButton);close=ArtworkImage(Close);modal=PanelArtwork();
}
inline void Slice(const Image& art,ImVec4 source,ImVec2 a,ImVec2 b,float sourceX,float sourceY,float edgeX,float edgeY,int frame=0){
    if(!art.texture||art.width<=0||art.height<=0||b.x<=a.x||b.y<=a.y)return;
    frame=std::clamp(frame,0,(std::max)(0,art.frames-1));const float offset=float(frame*art.pitch);
    edgeX=(std::min)(edgeX,(b.x-a.x)*.4f);edgeY=(std::min)(edgeY,(b.y-a.y)*.4f);
    const float x[]{a.x,a.x+edgeX,b.x-edgeX,b.x},y[]{a.y,a.y+edgeY,b.y-edgeY,b.y};
    const float u[]{(offset+source.x)/art.width,(offset+source.x+sourceX)/art.width,(offset+source.z-sourceX)/art.width,(offset+source.z)/art.width};
    const float v[]{source.y/art.height,(source.y+sourceY)/art.height,(source.w-sourceY)/art.height,source.w/art.height};
    auto draw=ImGui::GetWindowDrawList();
    for(int row=0;row<3;++row)for(int col=0;col<3;++col)draw->AddImage(ImTextureID(art.texture),{x[col],y[row]},{x[col+1],y[row+1]},{u[col],v[row]},{u[col+1],v[row+1]});
}
inline void Panel(ImVec2 a,ImVec2 b,float scale){
    if(background.texture)Slice(background,{89,963,1034,1376},a,b,24,24,12*scale,12*scale);
    else if(modal.texture)Slice(modal,{0,0,float(modal.frameWidth),float(modal.height)},a,b,60,50,12*scale,12*scale);
    else {auto draw=ImGui::GetWindowDrawList();draw->AddRectFilled(a,b,IM_COL32(15,12,10,255));draw->AddRect(a,b,IM_COL32(92,67,35,255));}
}
inline void Plate(const char* label,float scale,float height=38){
    const auto p=ImGui::GetCursorScreenPos();const float width=ImGui::GetContentRegionAvail().x,h=height*scale;
    if(background.texture)Slice(background,{89,788,1034,925},p,{p.x+width,p.y+h},155,20,(std::min)(70*scale,width*.18f),5*scale);
    const auto extent=ImGui::CalcTextSize(label);auto draw=ImGui::GetWindowDrawList();
    draw->PushClipRect(p,{p.x+width,p.y+h},true);draw->AddText({p.x+(width-extent.x)*.5f+1,p.y+(h-extent.y)*.5f+1},IM_COL32_BLACK,label);
    draw->AddText({p.x+(width-extent.x)*.5f,p.y+(h-extent.y)*.5f},ImGui::ColorConvertFloat4ToU32(Gold),label);draw->PopClipRect();ImGui::Dummy({width,h});
}
inline bool Button(const char* label,ImVec2 size={},float scale=1){
    if(size.x==0)size.x=(std::min)(ImGui::GetContentRegionAvail().x,ImGui::CalcTextSize(label).x+48*scale);
    if(size.x<0)size.x=ImGui::GetContentRegionAvail().x;
    if(size.y==0)size.y=ImGui::GetTextLineHeight()+18*scale;
    auto draw=ImGui::GetWindowDrawList();draw->ChannelsSplit(2);draw->ChannelsSetCurrent(1);
    ImGui::PushStyleColor(ImGuiCol_Button,{0,0,0,0});ImGui::PushStyleColor(ImGuiCol_ButtonHovered,{0,0,0,0});ImGui::PushStyleColor(ImGuiCol_ButtonActive,{0,0,0,0});ImGui::PushStyleColor(ImGuiCol_Text,Gold);
    const bool pressed=ImGui::Button(label,size);const auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();const int frame=ImGui::GetStyle().Alpha<.8f?2:ImGui::IsItemActive()?1:ImGui::IsItemHovered()?3:0;
    draw->ChannelsSetCurrent(0);
    if(button.texture)Slice(button,{0,0,float(button.frameWidth),float(button.height)},a,b,40,16,18*scale,6*scale,frame);
    else Panel(a,b,scale);
    draw->ChannelsMerge();ImGui::PopStyleColor(4);return pressed;
}
inline void Meter(float fraction,const char* label,float scale){
    if(label&&*label)ImGui::TextColored(Gold,"%s",label);
    const auto p=ImGui::GetCursorScreenPos();const float width=ImGui::GetContentRegionAvail().x,h=8*scale;auto draw=ImGui::GetWindowDrawList();
    draw->AddRectFilled(p,{p.x+width,p.y+h},IM_COL32(8,6,5,255));
    draw->AddRectFilled({p.x+2*scale,p.y+2*scale},{p.x+2*scale+(width-4*scale)*std::clamp(fraction,0.f,1.f),p.y+h-2*scale},IM_COL32(141,100,42,255));
    draw->AddRect(p,{p.x+width,p.y+h},IM_COL32(85,66,37,255));ImGui::Dummy({width,h});
}
inline void CloseButton(bool* open,float scale){
    const auto saved=ImGui::GetCursorScreenPos();const auto window=ImGui::GetWindowPos();const auto size=ImGui::GetWindowSize();
    const ImVec2 p{window.x+size.x-47*scale,window.y+22*scale};ImGui::SetCursorScreenPos(p);
    if(ImGui::InvisibleButton("Close journal",{25*scale,25*scale}))*open=false;
    const int frame=ImGui::IsItemActive()?1:ImGui::IsItemHovered()?3:0;
    if(close.texture){const float offset=float(frame*close.pitch);ImGui::GetWindowDrawList()->AddImage(ImTextureID(close.texture),p,{p.x+25*scale,p.y+25*scale},{offset/close.width,0},{(offset+close.frameWidth)/close.width,1});}
    else ImGui::GetWindowDrawList()->AddText(p,IM_COL32(200,170,110,255),"X");
    ImGui::SetCursorScreenPos(saved);
}
}
