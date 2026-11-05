#pragma once
#include <cstdint>
#include <optional>

namespace Marketplace::GoldDeposit {
struct Proof {uint64_t carriedBefore=0,carriedAfter=0,stashBefore=0,stashAfter=0;};
class Detector {
    bool known=false;uint32_t owner=0;unsigned page=0,tab=0;uint64_t at=0,changed=0;
    Proof baseline{};std::optional<Proof> pending;
public:
    void Reset(){known=false;changed=0;pending.reset();}
    void Observe(uint32_t actor,unsigned route,unsigned selected,uint64_t carried,uint64_t stash,uint64_t now){
        if(!known||owner!=actor||page!=route||now<at||now-at>2000){
            known=true;owner=actor;page=route;tab=selected;at=now;changed=0;baseline={carried,carried,stash,stash};pending.reset();return;
        }
        at=now;tab=selected;
        if(carried>baseline.carriedBefore||stash<baseline.stashBefore){
            baseline={carried,carried,stash,stash};changed=0;pending.reset();return;
        }
        const auto decrease=baseline.carriedBefore-carried,increase=stash-baseline.stashBefore;
        if(decrease&&decrease==increase){
            Proof proof{baseline.carriedBefore,carried,baseline.stashBefore,stash};
            if(pending){proof.carriedBefore=pending->carriedBefore;proof.stashBefore=pending->stashBefore;}
            pending=proof;baseline={carried,carried,stash,stash};changed=0;return;
        }
        if(decrease||increase){if(!changed)changed=now;else if(now-changed>1000){baseline={carried,carried,stash,stash};changed=0;pending.reset();}}
    }
    std::optional<Proof> Pending()const{return pending;}
    void Taken(){pending.reset();}
};
}
