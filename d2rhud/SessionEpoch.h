#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include "ProcessLifetime.h"

namespace HudSession {
class Tracker {
    uint64_t token_=0,character_=0,generation_=0;
    bool inGame_=false,settling_=false;
public:
    uint64_t Observe(bool inGame,uint64_t token,bool loading=false,uint64_t character=0){
        // Zero tokens/names during an act load are temporarily unavailable,
        // rather than a new game. Joiners legitimately have a zero host token.
        if(inGame && inGame_ && loading){
            if(!token)token=token_;
            if(!character)character=character_;
            if(settling_){token_=token;character_=character;return generation_;}
        }
        if(inGame && inGame_ && settling_ && !loading){
            token_=token;if(character)character_=character;settling_=false;return generation_;
        }
        if(inGame && inGame_ && !character)character=character_;
        if(!inGame){token=character=0;}
        if(inGame!=inGame_ || token!=token_ || (character && character_ && character!=character_)){
            if(++generation_==0)++generation_;
            settling_=inGame && loading;
        }
        if(!inGame)settling_=false;
        inGame_=inGame;token_=token;character_=character;
        return inGame?generation_:0;
    }
};
struct Runtime {std::mutex mutex;Tracker tracker;std::atomic<uint64_t> epoch{0};};
inline Runtime& State(){return HudLifetime::State<Runtime>();}
inline uint64_t Observe(bool inGame,uint64_t token,bool loading=false,uint64_t character=0){
    auto& runtime=State();std::lock_guard lock(runtime.mutex);
    const auto value=runtime.tracker.Observe(inGame,token,loading,character);runtime.epoch.store(value,std::memory_order_release);return value;
}
inline uint64_t Current(){return State().epoch.load(std::memory_order_acquire);}
}
