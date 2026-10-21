#pragma once
#include "BuffIconsData.h"
#include <array>
#include <memory>
#include <span>

struct ImFont;
namespace LegacyTransmog::Skin {
enum Part {Frame,Input,Up,Down,Confirm,Close,JournalBackground,JournalTab,JournalButton,Count};
struct Sprite {BuffIcons::SpriteAtlas atlas;int pitch=0;};
using Collection=std::array<Sprite,Count>;
struct Image {uint64_t texture=0;int width=0,height=0,frameWidth=0,frames=0,pitch=0;};
using Artwork=Image(*)(Part);
using Font=ImFont*(*)(float);
using ItemArtwork=Image(*)(const std::string&);
void SetProviders(Artwork artwork,Font font,ItemArtwork item=nullptr);
// Stable, shared panel atlas; safe for other HUD editors to reuse.
Image PanelArtwork();
Image ArtworkImage(Part part);
// Loaded once by the existing DLL startup worker; no CASC/filesystem I/O in Draw.
void Load();
std::shared_ptr<const Collection> Assets();
// Polled only by the render thread. File reads/decoding run on a worker.
std::shared_ptr<const Sprite> ItemSprite(const std::string& code);
inline std::vector<std::string> IconPaths(std::string asset){
    std::replace(asset.begin(),asset.end(),'\\','/');
    while(asset.find("//")!=asset.npos)asset.replace(asset.find("//"),2,"/");
    if(asset.empty() || asset.size()>256 || asset.front()=='/' || asset.find(':')!=asset.npos)return {};
    if(asset.starts_with("../custom/"))asset=asset.substr(3);
    if(asset.find("..")!=asset.npos)return {};
    if(asset.starts_with("items/"))asset=asset.substr(6);
    if(asset.ends_with(".sprite"))asset.resize(asset.size()-7);
    const std::string root="hd/global/ui/items/";
    if(asset.starts_with("custom/") || asset.starts_with("misc/") || asset.starts_with("weapon/") || asset.starts_with("armor/"))return {root+asset+".sprite"};
    return {root+"misc/"+asset+".sprite",root+"weapon/"+asset+".sprite",root+"armor/"+asset+".sprite"};
}
inline bool Decode(std::span<const uint8_t> bytes,Sprite& out){
    out={};if(bytes.size()<40 || memcmp(bytes.data(),"SpA1",4))return false;
    auto word=[&](size_t offset){uint16_t value;memcpy(&value,bytes.data()+offset,2);return value;};
    auto dword=[&](size_t offset){uint32_t value;memcpy(&value,bytes.data()+offset,4);return value;};
    const auto format=word(4),fw=word(6);const auto w=dword(8),h=dword(12),n=dword(20);
    if((format!=31 && format!=61) || !fw || !w || !h || !n || n>64 || w>8192 || h>8192 || uint64_t(fw)*n>w)return false;
    // UI sprites often have one separator pixel between frames: 4*531+3.
    const int pitch=n==1?fw:(w-fw)/(n-1);
    if(pitch<fw || (n>1 && uint64_t(pitch)*(n-1)+fw>w))return false;
    const size_t size=format==31?size_t(w)*h*4:size_t((w+3)/4)*((h+3)/4)*16;
    if(size>32*1024*1024 || size>bytes.size()-40)return false;
    out.atlas={int(w),int(h),fw,int(n),format==61,{bytes.begin()+40,bytes.begin()+40+size}};out.pitch=pitch;return true;
}
}
