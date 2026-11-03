#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace HudCommands {
// Clock-driven policy, owned by the render/settings thread under the command
// module's mutex. No detached workers, native pointers or wall-clock sleeps.
class StartupSchedule {
public:
    using Commands=std::array<std::string,6>;
    bool Configure(const Commands& commands,uint64_t now){
        if(commands_==commands)return false;
        commands_=commands;index_=0;due_=now+3000;
        return true;
    }
    bool SetSession(uint64_t session,uint64_t now){
        if(session_==session)return false;
        session_=session;index_=0;due_=now+3000;return true;
    }
    std::optional<std::string> TakeDue(uint64_t now){
        if(!session_ || index_>=commands_.size() || now<due_)return std::nullopt;
        const auto& command=commands_[index_++];
        due_=now+1000; // Never burst six commands after a slow/loading frame.
        if(command.empty() || command=="disabled")return std::nullopt;
        return command;
    }
    uint64_t Session()const{return session_;}
private:
    Commands commands_{};
    uint64_t session_=0,due_=0;
    size_t index_=0;
};
}
