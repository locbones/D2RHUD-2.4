#pragma once
#include <cstdint>

// Value-only shortcut policy. Native widgets stay on the render/game UI path;
// window callbacks only post a request. No save files or item records are edited.
namespace Marketplace::Hotkey {
struct Context {
    uint64_t now=0,paintTick=0;uint32_t owner=0;
    bool playing=false,allowed=false,available=false,stashOpen=false,inventoryOpen=false;
    bool paintReady=false,marketOpen=false,syncing=false;
};
struct Action {bool openStash=false,openMarket=false,closeStash=false,closeInventory=false;const char* notice=nullptr;};
class Session {
    enum class Phase {Idle,Waiting,Opening,Active,Closing};
    Phase phase=Phase::Idle;uint32_t owner=0;uint64_t deadline=0,started=0;
    bool ownStash=false,ownInventory=false;
    Action Finish(const Context& c,const char* message=nullptr){
        if(ownStash&&c.stashOpen&&c.syncing){phase=Phase::Closing;return {};}
        Action a;a.closeStash=ownStash&&c.stashOpen;a.closeInventory=a.closeStash&&ownInventory&&c.inventoryOpen;a.notice=message;Reset();return a;
    }
public:
    bool Busy()const{return phase!=Phase::Idle;}
    void Reset(){phase=Phase::Idle;owner=0;deadline=started=0;ownStash=ownInventory=false;}
    Action Tick(const Context& c,bool request){
        if(!c.playing||!c.owner){Reset();return {};}
        if(Busy()&&owner!=c.owner){Reset();return {};}
        if(request&&phase==Phase::Idle){
            if(c.marketOpen||!c.allowed||c.syncing)return {};
            if(!c.available)return {false,false,false,false,"Marketplace unavailable: this mod needs the native marketplace layout and a supported 2.4 client."};
            owner=c.owner;started=c.now;deadline=c.now+5000;phase=Phase::Waiting;ownStash=!c.stashOpen;ownInventory=ownStash&&!c.inventoryOpen;
            if(ownStash)return {true};
        }
        if(phase==Phase::Idle)return {};
        if(phase==Phase::Closing)return Finish(c);
        if(phase==Phase::Active)return c.marketOpen?Action{}:Finish(c);
        if(!c.allowed)return Finish(c);
        if(c.now>=deadline)return Finish(c,"The marketplace could not open. Try the shortcut again when the stash is ready.");
        if(phase==Phase::Opening){if(c.marketOpen)phase=Phase::Active;return {};}
        if(c.stashOpen&&c.paintReady&&(!ownStash||c.paintTick>started)&&!c.syncing){phase=Phase::Opening;return {false,true};}
        return {};
    }
};
}
