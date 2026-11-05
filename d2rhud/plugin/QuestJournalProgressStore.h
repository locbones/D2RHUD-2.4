#pragma once
#include "QuestJournalStorage.h"
#include "../RuntimeTelemetry.h"
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace QuestJournal::Data {
// Counter-only updates stay in memory between ten-second checkpoints. Reward boundaries
// still wait for the same atomic, flushed Store::Commit before sending an ACK.
// The worker owns the store/lease, never game pointers or native callbacks.
class ProgressStore {
public:
    enum class Result { Stored, Pending, Failed };
    using Task=std::function<void()>;
    using Launch=std::function<void(Task)>;
    using Persist=std::function<bool(Store&,State,uint64_t,std::string&)>;
    using Clock=std::function<uint64_t()>;
    static constexpr uint64_t CounterIntervalMs=10000;
    History history; // Accepted snapshot: accessed only by the native receiver.
    std::filesystem::path path;
private:
    struct Request {State value;uint64_t revision;};
    struct Worker {
        Store store;std::mutex mutex;std::condition_variable changed;
        std::optional<Request> next;bool busy=false;
        State durable;uint64_t revision=0;std::string error;Persist persist;
    };
    std::shared_ptr<Worker> worker_=std::make_shared<Worker>();
    Launch launch_;Clock clock_;uint64_t lastCounterAttempt_=0;
    static bool CountersOnly(State old,State next){old.kills=next.kills;return old==next;}
    static void Run(const std::shared_ptr<Worker>& worker) noexcept {
        for(;;){std::optional<Request> request;
            {std::lock_guard lock(worker->mutex);
                if(!worker->next){worker->busy=false;worker->changed.notify_all();return;}
                request=std::move(worker->next);worker->next.reset();}
            std::string error;bool stored=false;
            try{HudTelemetry::Scope timing(HudTelemetry::Feature::QuestJournalStore);
                stored=worker->persist(worker->store,request->value,request->revision,error);
            }catch(...){error="The quest journal worker could not save progress.";}
            {std::lock_guard lock(worker->mutex);
                if(stored){worker->durable=Current(worker->store.history);worker->revision=worker->store.history.revision;worker->error.clear();}
                else worker->error=error.empty()?"The quest journal could not be saved.":std::move(error);
                worker->changed.notify_all();}
        }
    }
    void Start(std::unique_lock<std::mutex>& lock){
        const auto worker=worker_;if(worker->busy)return;
        worker->busy=true;lock.unlock();
        try{launch_([worker]{Run(worker);});}
        catch(...){std::lock_guard failed(worker->mutex);worker->busy=false;worker->next.reset();worker->error="The quest journal worker could not start.";worker->changed.notify_all();}
        lock.lock();
    }
public:
    explicit ProgressStore(Launch launch={},Persist persist={},Clock clock={}):launch_(std::move(launch)),clock_(std::move(clock)){
        if(!launch_)launch_=[](Task task){std::thread(std::move(task)).detach();};
        if(!clock_)clock_=[] {return GetTickCount64();};
        worker_->persist=persist?std::move(persist):Persist([](Store& store,State value,uint64_t revision,std::string& error){return store.Commit(value,revision,error);});
    }
    ProgressStore(const ProgressStore&)=delete;
    ~ProgressStore(){
        // A session change flushes the last accepted counters without joining
        // a storage thread. Native save packets force a barrier separately.
        try{if(history.checkpoints.empty())return;const auto value=Current(history);
            std::unique_lock lock(worker_->mutex);
            if(worker_->revision>=history.revision)return;
            worker_->next=Request{value,history.revision};Start(lock);
        }catch(...){}
    }
    bool Open(const std::filesystem::path& root,const Identity& identity,std::string& error){
        if(!worker_->store.Open(root,identity,error))return false;
        history=worker_->store.history;path=worker_->store.path;
        worker_->durable=Current(history);worker_->revision=history.revision;lastCounterAttempt_=clock_();return true;
    }
    Result Update(State value,uint64_t revision,std::string& error,bool force=false){
        const auto old=Current(history);
        if(revision<history.revision||value.completed<old.completed){error="An older quest update was ignored; the current journal was retained.";return Result::Failed;}
        if(revision==history.revision&&value!=old){error="Conflicting quest update; waiting for the host's current state.";return Result::Failed;}
        const auto worker=worker_;std::unique_lock lock(worker->mutex);
        if(worker->revision==revision&&worker->durable==value)return Result::Stored;
        // Compare with durable progress, so a failed/pending objective or claim
        // cannot become an asynchronous counter update on its next retry.
        const bool wait=force||!CountersOnly(worker->durable,value);
        if(!wait){history.revision=revision;Append(history,{0,0,value});}
        const auto now=clock_();if(now<lastCounterAttempt_)lastCounterAttempt_=now;
        if(!wait&&now-lastCounterAttempt_<CounterIntervalMs){
            if(worker->next)worker->next=Request{value,revision};
            if(!worker->error.empty()){error=worker->error;return Result::Failed;}
            return Result::Pending;
        }
        lastCounterAttempt_=now;
        worker->next=Request{value,revision}; // Coalesce only unstarted work.
        Start(lock);
        if(wait)worker->changed.wait(lock,[&]{return worker->revision>=revision||!worker->busy;});
        if(worker->revision>=revision){history.revision=revision;Append(history,{0,0,value});return Result::Stored;}
        if(!worker->error.empty()){error=worker->error;return Result::Failed;}
        return Result::Pending;
    }
    bool Commit(State value,uint64_t revision,std::string& error){return Update(value,revision,error,true)==Result::Stored;}
};
}
