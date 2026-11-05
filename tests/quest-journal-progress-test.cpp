#define NOMINMAX
#include "../d2rhud/plugin/QuestJournalProgressStore.h"
#include <future>
#include <iostream>
#include <deque>

using namespace QuestJournal::Data;
using namespace std::chrono_literals;
int checks=0;
void Check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
int main(){try{
    const auto root=std::filesystem::temp_directory_path()/("d2rhud-quest-progress-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
    std::string error;const Identity identity{"CombatTest",0,4,0x20};
    // A held launcher makes the pending state deterministic: no write and no
    // durable ACK until a real Store::Commit has completed on the worker.
    std::deque<ProgressStore::Task> tasks;std::mutex tasksMutex;
    auto launch=[&](ProgressStore::Task task){std::lock_guard lock(tasksMutex);tasks.push_back(std::move(task));};
    auto run=[&]{ProgressStore::Task task;{std::lock_guard lock(tasksMutex);Check(tasks.size()==1,"more than one worker queued");task=std::move(tasks.front());tasks.pop_front();}task();};
    uint64_t now=0;ProgressStore store(launch,{},[&]{return now;});Check(store.Open(root/"coalescing",identity,error),"open failed");
    auto value=Current(store.history);const auto base=store.history.revision;
    now=10000;value.kills=1;Check(store.Update(value,base+1,error)==ProgressStore::Result::Pending,"counter waited for disk or acknowledged early");
    now=20000;value.kills=100;Check(store.Update(value,base+100,error)==ProgressStore::Result::Pending,"latest counters did not queue");
    Check(Store::Read(store.path)->revision==base,"pending counter changed disk on caller");run();
    Check(Current(*Store::Read(store.path)).kills==100,"coalesced latest counters missing");
    Check(store.Update(value,base+100,error)==ProgressStore::Result::Stored,"completed checkpoint not acknowledged");
    Check(store.Update({},base,error)==ProgressStore::Result::Failed,"old revision accepted");
    auto conflict=value;++conflict.kills;Check(store.Update(conflict,base+100,error)==ProgressStore::Result::Failed,"same-revision conflict accepted");

    // Continuous activity never pushes the deadline out: thousands of updates
    // remain in memory, with exactly one checkpoint per ten-second period.
    {
        uint64_t clock=0;ProgressStore cadence(launch,{},[&]{return clock;});
        Check(cadence.Open(root/"cadence",identity,error),"cadence open failed");
        auto counters=Current(cadence.history);const auto revision=cadence.history.revision;
        for(unsigned n=1;n<1000;++n){clock=n*10;counters.kills=n;Check(cadence.Update(counters,revision+n,error)==ProgressStore::Result::Pending,"early counter checkpoint");}
        Check(tasks.empty()&&Store::Read(cadence.path)->revision==revision,"activity wrote before ten-second deadline");
        clock=10000;counters.kills=1000;cadence.Update(counters,revision+1000,error);run();
        Check(Current(*Store::Read(cadence.path)).kills==1000,"deadline snapshot was not latest");
        clock=19999;counters.kills=1999;cadence.Update(counters,revision+1999,error);Check(tasks.empty(),"second checkpoint too early");
        clock=20000;counters.kills=2000;cadence.Update(counters,revision+2000,error);run();
        for(unsigned n=0;n<100;++n){clock+=10000;Check(cadence.Update(counters,revision+2000,error)==ProgressStore::Result::Stored,"unchanged snapshot lost durable status");}
        Check(tasks.empty(),"idle periods spawned redundant disk jobs");
        // Clock rollback does not trigger repeated saves or postpone a forced save.
        clock=1;counters.kills=2001;cadence.Update(counters,revision+2001,error);Check(tasks.empty(),"clock rollback caused a write burst");
        // A synchronous launcher makes the forced checkpoint deterministic.
        auto force=std::async(std::launch::async,[&]{return cadence.Commit(counters,revision+2001,error);});
        for(unsigned n=0;n<200;++n){{std::lock_guard lock(tasksMutex);if(!tasks.empty())break;}std::this_thread::sleep_for(1ms);}run();
        Check(force.get()&&Current(*Store::Read(cadence.path)).kills==2001,"forced checkpoint waited for periodic deadline");
    }

    // Each claim-affecting transition remains a synchronous durable barrier.
    auto barrier=[&](State next,uint64_t revision){
        auto done=std::async(std::launch::async,[&]{return store.Update(next,revision,error);});
        for(unsigned n=0;n<200;++n){{std::lock_guard lock(tasksMutex);if(!tasks.empty())break;}std::this_thread::sleep_for(1ms);}
        Check(done.wait_for(10ms)==std::future_status::timeout,"claim returned before durable commit");run();
        Check(done.get()==ProgressStore::Result::Stored,"claim barrier failed");Check(Current(*Store::Read(store.path))==next,"claim ACK preceded actual durable state");
    };
    value.legacyClosed=true;barrier(value,base+101);
    value.objectiveReady=true;barrier(value,base+102);
    value.objectiveReady=false;value.completed=1;barrier(value,base+103);
    value.uncertain=true;barrier(value,base+104);
    value.uncertain=false;value.evidence=128;barrier(value,base+105);
    value.menus=3;barrier(value,base+106);
    value.countess=true;value.thul=true;barrier(value,base+107);
    Check(!store.Commit({},base+108,error),"completed reward rolled back");

    // Failed reward boundaries cannot leak through a reconnect snapshot or
    // become counter-only writes on retry. Real backup/atomic writes retained.
    bool fail=true;ProgressStore failures(launch,[&](Store& disk,State state,uint64_t revision,std::string& message){if(fail){message="injected disk error";return false;}return disk.Commit(state,revision,message);});
    Check(failures.Open(root/"failure",identity,error),"failure fixture open failed");
    const auto initial=failures.history;auto critical=Current(initial);critical.legacyClosed=true;
    auto failed=std::async(std::launch::async,[&]{return failures.Update(critical,initial.revision+1,error);});
    for(unsigned n=0;n<200;++n){{std::lock_guard lock(tasksMutex);if(!tasks.empty())break;}std::this_thread::sleep_for(1ms);}run();
    Check(failed.get()==ProgressStore::Result::Failed&&error=="injected disk error","disk failure was acknowledged");
    Check(Current(failures.history)==Current(initial)&&failures.history.revision==initial.revision,"failed reward published as accepted progress");
    fail=false;
    auto retry=std::async(std::launch::async,[&]{return failures.Update(critical,initial.revision+1,error);});
    for(unsigned n=0;n<200;++n){{std::lock_guard lock(tasksMutex);if(!tasks.empty())break;}std::this_thread::sleep_for(1ms);}
    Check(retry.wait_for(10ms)==std::future_status::timeout,"retry bypassed reward barrier");run();Check(retry.get()==ProgressStore::Result::Stored,"disk retry did not recover");

    // Simulate a 150+ ms storage stall while native input keeps scheduling.
    // Destruction does not join; the in-flight worker keeps the writer lease.
    std::promise<void> entered,release,finished;auto released=release.get_future().share();std::atomic<unsigned> writes{0};uint64_t blockedNow=0;
    auto blocked=std::make_unique<ProgressStore>([&](ProgressStore::Task task){std::thread([&,task=std::move(task)]{task();finished.set_value();}).detach();},
        [&](Store& disk,State state,uint64_t revision,std::string& message){if(writes.fetch_add(1)==0){entered.set_value();released.wait();}return disk.Commit(state,revision,message);},[&]{return blockedNow;});
    Check(blocked->Open(root/"blocked",identity,error),"blocked fixture open failed");
    blockedNow=10000;const auto blockedPath=blocked->path;const auto startRevision=blocked->history.revision;State counters=Current(blocked->history);counters.kills=1;
    Check(blocked->Update(counters,startRevision+1,error)==ProgressStore::Result::Pending,"blocked storage stalled native caller");entered.get_future().wait();
    auto input=std::async(std::launch::async,[&]{for(unsigned n=2;n<=1000;++n){counters.kills=n;if(blocked->Update(counters,startRevision+n,error)!=ProgressStore::Result::Pending)return false;}blocked.reset();return true;});
    const auto responsive=input.wait_for(150ms)==std::future_status::ready;
    bool leaseHeld=false;if(responsive){Store other;std::string message;leaseHeld=!other.Open(root/"blocked",identity,message);}
    release.set_value();Check(input.get()&&responsive,"counter scheduling/destruction blocked behind disk");Check(leaseHeld,"destruction released writer lease before pending write completed");
    Check(finished.get_future().wait_for(5s)==std::future_status::ready,"owned worker did not finish");
    Check(writes==2&&Current(*Store::Read(blockedPath)).kills==1000,"unbounded backlog or final counters lost");

    ProgressStore launchFailure([](ProgressStore::Task){throw std::runtime_error("launch failed");},{},[&]{return now;});
    Check(launchFailure.Open(root/"launch-failure",identity,error),"launch failure open failed");auto kills=Current(launchFailure.history);kills.kills=1;now+=10000;
    Check(launchFailure.Update(kills,launchFailure.history.revision+1,error)==ProgressStore::Result::Failed,"thread failure silently acknowledged");
    std::cout<<"PASS: "<<checks<<" quest progress checks; nonblocking counters, bounded coalescing, durable reward barriers, retry, writer lifetime and failures.\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}}
