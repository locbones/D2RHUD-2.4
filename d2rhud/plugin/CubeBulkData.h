#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>
#include <numeric>
namespace CubeVisual::BulkData {
struct Rect {int width=0,height=0,x=0,y=0;};
// Match actual item footprints, not just their total area. Largest-first search
// avoids rejecting a mixed recipe merely because small outputs were placed first.
inline bool Pack(int width,int height,std::vector<Rect>& items) {
    if(width<1||height<1||width>32||height>32||items.size()>256)return false;
    int area=0;for(const auto& item:items){if(item.width<1||item.height<1||item.width>width||item.height>height)return false;area+=item.width*item.height;}
    if(area>width*height)return false;
    std::vector<size_t> order(items.size());std::iota(order.begin(),order.end(),0);
    std::stable_sort(order.begin(),order.end(),[&](size_t a,size_t b){return items[a].width*items[a].height>items[b].width*items[b].height;});
    std::vector<uint8_t> occupied(size_t(width)*height);size_t work=0;
    auto place=[&](auto&& self,size_t index)->bool{
        if(index==order.size())return true;if(++work>250000)return false;
        auto& item=items[order[index]];
        int first=0;if(index){const auto& previous=items[order[index-1]];if(previous.width==item.width&&previous.height==item.height)first=previous.y*width+previous.x+1;}
        for(int at=first;at<width*height;++at){const int x=at%width,y=at/width;if(x+item.width>width||y+item.height>height)continue;
            bool free=true;for(int row=0;row<item.height&&free;++row)for(int col=0;col<item.width;++col)if(occupied[size_t(y+row)*width+x+col]){free=false;break;}
            if(!free)continue;
            for(int row=0;row<item.height;++row)for(int col=0;col<item.width;++col)occupied[size_t(y+row)*width+x+col]=1;
            item.x=x;item.y=y;if(self(self,index+1))return true;
            for(int row=0;row<item.height;++row)for(int col=0;col<item.width;++col)occupied[size_t(y+row)*width+x+col]=0;
        }return false;
    };return place(place,0);
}
}
