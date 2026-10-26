#pragma once
#include <imgui.h>
#include <array>
#include <climits>
#include <cstddef>
#include <string>

namespace CubeMap::Text {
// Keep the existing HUD font indices intact. These faces are appended after
// the other fonts and uploaded with the same atlas, before rendering starts.
inline constexpr std::array<float,7> RasterSizes{16,24,32,48,64,96,128};
struct FontBank {
    std::array<ImFont*,RasterSizes.size()> fonts{};
    void Load(ImFontAtlas& atlas,const std::string& path,const unsigned char* fallback=nullptr,size_t fallbackSize=0,const ImWchar* ranges=nullptr,bool compact=false){
        fonts.fill(nullptr);
        for(size_t i=0;i<fonts.size();++i){
            if(compact && i>3){fonts[i]=fonts[3];continue;}
            ImFontConfig config;config.OversampleH=2;config.OversampleV=1;config.PixelSnapH=true;
            if(!path.empty())fonts[i]=atlas.AddFontFromFileTTF(path.c_str(),RasterSizes[i],&config,ranges);
            else if(fallback && fallbackSize && fallbackSize<=INT_MAX){
                config.FontDataOwnedByAtlas=false;
                fonts[i]=atlas.AddFontFromMemoryTTF(const_cast<unsigned char*>(fallback),int(fallbackSize),RasterSizes[i],&config,ranges);
            }
        }
    }
    ImFont* ForSize(float requested)const{
        ImFont* largest=nullptr;
        for(auto font:fonts)if(font){if(font->FontSize>=requested)return font;largest=font;}
        return largest;
    }
};
}
