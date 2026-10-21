#pragma once
#include <array>
#include <cstdint>
#include <cstring>

namespace ExtendedActs::Transition {
enum class Phase {Idle,Armed,Credits,Returning};
struct Flow {
    std::array<char,16> hero{};
    uintptr_t game=0;
    uint32_t seed=0,frame=0;
    unsigned difficulty=3,attempts=0;
    bool baal=false,ending=false;
    Phase phase=Phase::Idle;
    uint64_t expires=0;
    bool Matches(const char* name,unsigned diff)const{return name && difficulty==diff && !strncmp(hero.data(),name,16);}
    void Observe(const char* name,unsigned diff,uintptr_t session,uint32_t key,uint32_t nowFrame,bool completed) {
        if(!name || !memchr(name,0,16) || !name[0] || diff>=3)return;
        if(!Matches(name,diff) || game!=session || seed!=key || frame>nowFrame) {
            const bool returning=Matches(name,diff) && phase==Phase::Returning;
            const auto deadline=expires;const auto tries=attempts;
            *this={};memcpy(hero.data(),name,16);difficulty=diff;game=session;seed=key;
            baal=completed;if(returning){phase=Phase::Returning;expires=deadline;attempts=tries;}
        } else if(!baal && completed && phase==Phase::Idle)phase=Phase::Armed;
        baal=completed;frame=nowFrame;
    }
    void Ending(){if(difficulty<3 && (phase==Phase::Idle || phase==Phase::Armed)){ending=true;phase=Phase::Armed;}}
    bool Open(){if(phase!=Phase::Armed || !ending)return false;phase=Phase::Credits;return true;}
    bool Close(uint64_t now){if(phase!=Phase::Credits)return false;phase=Phase::Returning;expires=now+120000;return true;}
    bool Due(const char* name,unsigned diff,uint64_t now) {
        if(phase!=Phase::Returning)return false;
        if(!Matches(name,diff) || now>=expires || attempts>=3){phase=Phase::Idle;return false;}
        return true;
    }
    void Finish(){phase=Phase::Idle;attempts=0;}
};
}
