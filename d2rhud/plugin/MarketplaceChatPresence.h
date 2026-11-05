#pragma once
#include "MarketplaceStore.h"
#include "../RuntimeTelemetry.h"
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

namespace Marketplace::Chat {
// Ephemeral presence must never flush storage on the native window thread.
// One in-flight write plus one latest request bounds both threads and backlog.
// Tasks own values/state only; destroying a Bridge never joins a disk worker.
class PresenceWriter {
public:
    struct Request {std::filesystem::path path;std::string owner;int64_t at=0;};
    using Task=std::function<void()>;
    using Launch=std::function<void(Task)>;
    using Write=std::function<void(const Request&)>;
private:
    struct State {
        std::mutex mutex;bool busy=false,closed=false;std::optional<Request> next;Write write;
    };
    std::shared_ptr<State> state_=std::make_shared<State>();Launch launch_;
    static void Save(const Request& r){
        // This expiring, non-authoritative heartbeat is not a transaction or
        // save receipt. Atomic cached replacement is enough; forcing a drive
        // flush every two seconds can stall unrelated game asset I/O too.
        if(r.path.extension()!=L".json"||r.owner.empty()||r.owner.size()>64)return;
        const auto bytes=Json{{"schema",1},{"owner",r.owner},{"at",r.at}}.dump();
        auto lockPath=r.path;lockPath+=L".lock";
        HANDLE lease=CreateFileW(lockPath.c_str(),GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(lease==INVALID_HANDLE_VALUE)return;
        struct Close{HANDLE h;~Close(){CloseHandle(h);}} closeLease{lease};
        static std::atomic<unsigned> serial{0};auto temporary=r.path;
        temporary+=L".presence."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(++serial)+L".tmp";
        HANDLE file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE)return;
        DWORD written=0;const bool complete=WriteFile(file,bytes.data(),DWORD(bytes.size()),&written,nullptr)&&written==bytes.size();CloseHandle(file);
        if(!complete||!MoveFileExW(temporary.c_str(),r.path.c_str(),MOVEFILE_REPLACE_EXISTING))DeleteFileW(temporary.c_str());
    }
public:
    explicit PresenceWriter(Launch launch={},Write write={}):launch_(std::move(launch)){
        if(!launch_)launch_=[](Task task){std::thread(std::move(task)).detach();};
        state_->write=write?std::move(write):Write(Save);
    }
    ~PresenceWriter(){std::lock_guard lock(state_->mutex);state_->closed=true;state_->next.reset();}
    PresenceWriter(const PresenceWriter&)=delete;
    void Reset(){std::lock_guard lock(state_->mutex);state_->next.reset();}
    void Schedule(Request request) noexcept {
        try{
            const auto state=state_;
            {std::lock_guard lock(state->mutex);if(state->closed)return;state->next=std::move(request);if(state->busy)return;state->busy=true;}
            try{launch_([state]{
                for(;;){std::optional<Request> request;
                    {std::lock_guard lock(state->mutex);if(state->closed||!state->next){state->busy=false;return;}request=std::move(state->next);state->next.reset();}
                    try{HudTelemetry::Scope timing(HudTelemetry::Feature::MarketplacePresence);state->write(*request);}catch(...){} // Retry on the next heartbeat; presence never gates gameplay.
                }
            });}
            catch(...){std::lock_guard lock(state->mutex);state->busy=false;}
        }catch(...){} // Even allocation/thread-launch failure leaves gameplay available.
    }
};
}
