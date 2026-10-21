#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <random>
#include <span>
#include <vector>

namespace CubeMap::JungleData {
// Native Act III templates use two kinds of edges: river paths and clearing
// paths. These are the preset IDs/masks from the supported game's tables.
struct Tile {int id=0,river=0,clearing=0;};
inline constexpr std::array<int,60> Junctions{
    0,545,546,547,548,0,549,550,0,0,551,552,553,554,0,555,
    0,556,0,557,558,0,0,559,0,0,0,560,561,562,563,0,
    0,564,565,0,566,0,567,0,0,0,568,0,569,570,0,0,
    0,571,0,0,572,0,0,0,0,0,0,0
};
inline constexpr std::array<int,16> Clearings{0,575,576,577,578,579,580,0,581,582,583,0,584,0,0,0};
inline Tile Decode(int id){
    if(!id)return {};
    if(id>=530 && id<=544)return {id,id-529,0};
    for(size_t n=0;n<Junctions.size();++n)if(Junctions[n]==id)return {id,int(n/4)+1,1<<int(n%4)};
    for(int n=1;n<16;++n)if(Clearings[n]==id)return {id,0,n};
    return {-1,0,0};
}
inline int Edge(Tile tile,int bit){return (tile.river&bit)?1:(tile.clearing&bit)?2:0;}
inline bool Connected(std::span<const Tile> tiles,int width,bool riverOnly){
    std::vector<size_t> queue;std::vector<bool> seen(tiles.size());size_t total=0;
    for(size_t n=0;n<tiles.size();++n)if(riverOnly?tiles[n].river!=0:tiles[n].id!=0){if(queue.empty()){queue.push_back(n);seen[n]=true;}++total;}
    for(size_t cursor=0;cursor<queue.size();++cursor){
        const auto n=queue[cursor];const int x=int(n)%width,y=int(n)/width;
        for(int d=0;d<4;++d){
            const int xx=x+(d==1)-(d==0),yy=y+(d==2)-(d==3);
            if(xx<0 || xx>=width || yy<0 || size_t(yy*width+xx)>=tiles.size())continue;
            const auto m=size_t(yy*width+xx);const auto edge=Edge(tiles[n],1<<d);
            if(!edge || (riverOnly && edge!=1) || seen[m])continue;
            const int opposite=d^1; // W/E are 0/1, S/N are 2/3.
            if(Edge(tiles[m],1<<opposite)!=edge)continue;
            seen[m]=true;queue.push_back(m);
        }
    }
    return total && queue.size()==total;
}
inline bool Valid(std::span<const Tile> tiles,int width){
    if(width!=2 || tiles.size()%width)return false;
    for(size_t n=0;n<tiles.size();++n){
        if(tiles[n].id<0)return false;
        if(n%width && Edge(tiles[n],1)!=Edge(tiles[n-1],2))return false;
        if(n>=size_t(width) && Edge(tiles[n],8)!=Edge(tiles[n-width],4))return false;
    }
    return Connected(tiles,width,true) && Connected(tiles,width,false);
}
// Enumerate only native-compatible tiles, retaining all boundary ports and
// the head/tail preset row. Bounded deterministic search is identical on host
// and client and does not touch the act-wide RNG or neighboring templates.
inline std::vector<int> Roll(std::span<const int> original,int width,int level,uint32_t seed){
    std::vector<int> unchanged(original.begin(),original.end());
    if(width!=2 || original.size()<8 || original.size()>32 || original.size()%width || level<76 || level>78)return unchanged;
    const int height=int(original.size())/width;std::vector<Tile> base;
    for(auto id:original)base.push_back(Decode(id));
    if(!Valid(base,width))return unchanged;
    int rivers=0,clearings=0;for(auto tile:base){rivers+=tile.river!=0;clearings+=tile.id>574;}
    if(clearings<2 || clearings>3)return unchanged;
    std::vector<Tile> choices{{}};for(int id=530;id<=584;++id)if(auto tile=Decode(id);tile.id>=0)choices.push_back(tile);
    std::mt19937 random(seed); // Explicit shuffle avoids library-specific uniform distributions.
    for(size_t n=choices.size();n>1;--n)std::swap(choices[n-1],choices[random()%n]);
    std::vector<Tile> result(base.size());size_t budget=200000;
    const auto solve=[&](auto&& self,size_t n,int remainingRivers,int remainingClearings)->bool{
        if(!budget)return false;--budget;
        if(n==base.size()){
            if(remainingRivers || remainingClearings || !Valid(result,width))return false;
            return true;
        }
        const int x=int(n)%width,y=int(n)/width;
        const bool fixed=(level==76 && y==height-1) || (level==78 && y==0);
        const size_t count=fixed?1:choices.size(),start=fixed?0:random()%choices.size();
        for(size_t j=0;j<count && budget;++j){
            const auto tile=fixed?base[n]:choices[(start+j)%choices.size()];
            const int r=remainingRivers-(tile.river!=0),c=remainingClearings-(tile.id>574);
            if(r<0 || c<0 || r+c>int(base.size()-n-1))continue;
            if(Edge(tile,1)!=(x?Edge(result[n-1],2):Edge(base[n],1)))continue;
            if(Edge(tile,8)!=(y?Edge(result[n-width],4):Edge(base[n],8)))continue;
            if(x==width-1 && Edge(tile,2)!=Edge(base[n],2))continue;
            if(y==height-1 && Edge(tile,4)!=Edge(base[n],4))continue;
            result[n]=tile;if(self(self,n+1,r,c))return true;
        }
        return false;
    };
    if(!solve(solve,0,rivers,clearings))return unchanged;
    std::vector<int> output;for(auto tile:result)output.push_back(tile.id);return output;
}
}
