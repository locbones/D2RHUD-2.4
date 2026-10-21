#pragma once
#include "CubeMapData.h"
#include <memory>
#include <tuple>
#include <cmath>
#include <cstring>

namespace CubeMap::AutomapData {
using namespace Data;
constexpr uint8_t Operation=2;
constexpr size_t EntriesPerPacket=12,MaxCells=65536,MaxMonsters=16384;
enum class Stream:uint8_t { Geometry,Monsters };
#pragma pack(push,1)
struct Entry {uint32_t id=0;int32_t x=0,y=0;uint8_t kind=0;};
struct Wire {
    uint8_t opcode=0x2b;uint64_t magic=PacketMagic;uint8_t operation=Operation,version=1;
    uint32_t run=0,session=0,recipient=0,revision=0;
    uint16_t level=39;Stream stream=Stream::Geometry;
    uint32_t offset=0,total=0;uint8_t count=0;
    std::array<Entry,EntriesPerPacket> entries{};
    std::array<uint8_t,11> padding{};
};
#pragma pack(pop)
static_assert(sizeof(Entry)==13 && sizeof(Wire)==sizeof(Packet));
inline bool Visible(const Packet& p,uint64_t age,uint32_t player,int level){
    return p.active && p.run && p.recipient==player && p.level==EventLevel(p.event) && level==p.level && age<=4000 &&
        p.event>=Event::GoldenArmada && p.event<=Event::SoulSurprise && !TimedEvent(p.event) &&
        (p.eventPhase==EventPhase::Entering || (p.eventPhase==EventPhase::Active &&
            (!Info(p.event).seconds || uint64_t(p.eventElapsed)+age<uint64_t(Info(p.event).seconds)*1000)));
}
inline bool SameEvent(const Wire& w,const Packet& p){return w.run==p.run && w.session==p.session && w.recipient==p.recipient && w.level==p.level;}
inline bool Valid(const Wire& w){
    if(w.opcode!=0x2b || w.magic!=PacketMagic || w.operation!=Operation || w.version!=1 || !w.run || !w.revision ||
       (w.level!=39 && w.level!=108 && w.level!=239) || w.stream>Stream::Monsters || w.total>(w.stream==Stream::Geometry?MaxCells:MaxMonsters) ||
       w.offset>w.total || w.offset%EntriesPerPacket || w.count!=std::min<size_t>(EntriesPerPacket,w.total-w.offset) ||
       (w.total && !w.count))return false;
    for(size_t i=0;i<w.count;++i){const auto& e=w.entries[i];
        if(e.x<-(1<<24) || e.x>(1<<24) || e.y<-(1<<24) || e.y>(1<<24) ||
           e.kind>(w.stream==Stream::Geometry?1:3) || (w.stream==Stream::Geometry && e.id>32767))return false;
    }return true;
}
struct Assembly {
    uint32_t revision=0;size_t received=0;
    std::vector<Entry> pending;
    std::vector<bool> parts;
    bool complete=false;
    std::shared_ptr<const std::vector<Entry>> ready;
    bool Accept(const Wire& w){
        if(!Valid(w))return false;
        if(revision && int32_t(w.revision-revision)<0)return false;
        if(revision!=w.revision){revision=w.revision;received=0;complete=false;pending.resize(w.total);parts.assign(std::max<size_t>(1,(w.total+EntriesPerPacket-1)/EntriesPerPacket),false);}
        if(pending.size()!=w.total || complete)return false;
        const auto part=w.offset/EntriesPerPacket;if(parts[part])return false;
        std::copy_n(w.entries.begin(),w.count,pending.begin()+w.offset);parts[part]=true;++received;
        if(received==parts.size()){ready=std::make_shared<const std::vector<Entry>>(std::move(pending));complete=true;}
        return true;
    }
};
// Native map nodes are read by the renderer only. These private trees are
// never linked into the game's layer list, serializer or exploration flags.
struct CellNode {CellNode *parent=nullptr,*left=nullptr,*right=nullptr;uint64_t color=0;uint8_t saved=1,pad=0;int16_t image=0;int32_t x=0,y=0,padding=0;};
struct CellTree {
    CellNode *root=nullptr,*first=nullptr,*last=nullptr;uint64_t color=0,count=0;
};
static_assert(sizeof(CellNode)==0x30 && offsetof(CellNode,image)==0x22 && sizeof(CellTree)==0x28);
struct Geometry {
    std::array<CellTree,2> trees{};std::array<std::vector<CellNode>,2> nodes;
    int highestImage=-1;
    explicit Geometry(const std::vector<Entry>& entries){
        std::vector<Entry> sorted=entries;
        std::sort(sorted.begin(),sorted.end(),[](const auto& a,const auto& b){return std::tie(a.kind,a.y,a.x,a.id)<std::tie(b.kind,b.y,b.x,b.id);});
        for(unsigned group=0;group<2;++group){
            auto& tree=trees[group];auto& n=nodes[group];
            for(const auto& e:sorted)if(e.kind==group){CellNode node;node.image=int16_t(e.id);node.x=e.x;node.y=e.y;n.push_back(node);highestImage=std::max(highestImage,int(e.id));}
            auto header=reinterpret_cast<CellNode*>(&tree);
            const auto link=[&](auto&& self,size_t lo,size_t hi,CellNode* parent)->CellNode*{
                if(lo==hi)return nullptr;const auto mid=lo+(hi-lo)/2;auto node=&n[mid];node->parent=parent;
                node->left=self(self,lo,mid,node);node->right=self(self,mid+1,hi,node);return node;
            };
            tree.root=link(link,0,n.size(),header);tree.first=n.empty()?header:&n.front();tree.last=n.empty()?header:&n.back();tree.count=n.size();
        }
    }
    Geometry(const Geometry&)=delete;Geometry& operator=(const Geometry&)=delete;
};
inline uint8_t MonsterKind(uint8_t flags,bool boss,bool objective){return boss || objective || (flags&0x02)?3:(flags&0x04)?1:(flags&0x08)?3:(flags&0x10)?2:0;}
inline int SpriteFrame(uint8_t kind){return kind==0?3:kind<=2?1:5;} // units.sprite: dark gray-blue, green, red
struct Point {int32_t x=0,y=0;};
inline Point Project(Point p,const uint8_t* transform){
    const auto integer=[&](size_t o){int32_t v;memcpy(&v,transform+o,4);return v;};
    const auto real=[&](size_t o){float v;memcpy(&v,transform+o,4);return v;};
    return {integer(0x28)+int32_t(std::floor(p.x*real(0x30)))-int32_t(std::floor(integer(0x10)*real(0x30))),
        integer(0x2c)+int32_t(std::floor(p.y*real(0x34)))-int32_t(std::floor(integer(0x14)*real(0x34)))};
}
}
