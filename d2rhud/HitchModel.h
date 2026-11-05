#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace HudHitches {
// Preallocated, chronological rings. Recording never grows a buffer.
template<class T> class Ring {
    std::vector<T> rows_;uint64_t written_=0;
public:
    explicit Ring(size_t capacity):rows_(capacity){}
    void Add(const T& value){if(!rows_.empty())rows_[written_++%rows_.size()]=value;}
    size_t Size()const{return (std::min)(size_t(written_),rows_.size());}
    uint64_t Overwritten()const{return written_>rows_.size()?written_-rows_.size():0;}
    const T& At(size_t i)const{return rows_[(written_-Size()+i)%rows_.size()];}
};
struct Frame {
    int64_t at=0;double intervalMs=0,hudMs=0,presentMs=0,threadCpuMs=0;
    uint64_t context=0,session=0;bool eligible=false,sampleGap=false;
};
struct Summary {
    uint64_t frames=0,hitches=0,excluded=0;double totalMs=0,maximumMs=0;
    uint64_t observed=0,allHitches=0;double allMaximumMs=0;
    std::array<uint64_t,12> buckets{};
    inline static constexpr std::array<double,12> Limits{8.334,16.667,25,33.334,50,75,100,150,250,500,1000,1e12};
    void Add(const Frame& f,double threshold){
        ++observed;if(!f.sampleGap){allMaximumMs=(std::max)(allMaximumMs,f.intervalMs);if(f.intervalMs>=threshold)++allHitches;}
        if(!f.eligible){++excluded;return;}++frames;totalMs+=f.intervalMs;maximumMs=(std::max)(maximumMs,f.intervalMs);
        if(f.intervalMs>=threshold)++hitches;
        for(size_t i=0;i<Limits.size();++i)if(f.intervalMs<=Limits[i]){++buckets[i];break;}
    }
    double Mean()const{return frames?totalMs/double(frames):0;}
    double P95Upper()const{uint64_t sum=0,target=frames-frames/20;for(size_t i=0;frames&&i<buckets.size();++i){sum+=buckets[i];if(sum>=target)return (std::min)(Limits[i],maximumMs);}return 0;}
};
inline bool Eligible(uint64_t before,uint64_t after,uint64_t oldSession,uint64_t session,bool oldFocused,bool focused){
    // Bits: status[0..7], loading[8], HUD menu[9], marketplace[10], stash page[16..23].
    return oldFocused&&focused&&oldSession==session&&(before&255)==1&&(after&255)==1&&!(before&0x300)&&!(after&0x300);
}
}
