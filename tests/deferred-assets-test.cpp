#include "../d2rhud/DeferredAssets.h"
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
int checks=0;
void Check(bool value,const char* text){++checks;if(!value)throw std::runtime_error(text);}
using Assets=HudAssets::Deferred<std::string>;
struct Job {HudAsync::Job<Assets::Result>::Task task;std::promise<Assets::Result> promise;};
struct Fixture {
    uint64_t now=100;unsigned launches=0,reads=0;bool failLaunch=false,failRead=false;
    std::vector<std::shared_ptr<Job>> jobs;
    Assets::Operations Ops(){return {
        [this](const std::string& key){++reads;if(failRead)throw std::runtime_error("read");return key;},
        [this]{return now;},[this](auto task){++launches;if(failLaunch){now+=500;throw std::runtime_error("launch");}
            auto job=std::make_shared<Job>();job->task=std::move(task);jobs.push_back(job);return job->promise.get_future();}
    };}
    void Complete(size_t n){try{jobs[n]->promise.set_value(jobs[n]->task());}catch(...){jobs[n]->promise.set_exception(std::current_exception());}}
};
void Tests(){
    Fixture f;Assets assets(f.Ops());
    for(int i=0;i<100000;++i)assets.Tick("first",false);
    Check(f.launches==0&&f.reads==0&&!assets.Value()&&!assets.Busy(),"100000 disabled ticks never launch or read assets");
    assets.Tick("first",true);Check(f.launches==1&&assets.Busy(),"Demand starts one worker");
    for(int i=0;i<10000;++i)assets.Tick("first",true);
    Check(f.launches==1,"Repeated demand never duplicates pending work");
    f.Complete(0);assets.Tick("first",false);
    Check(assets.Value()&&*assets.Value()=="first"&&!assets.Busy(),"Earlier request may publish while feature is inactive");
    const auto old=assets.Value();const auto revision=assets.Revision();
    for(int i=0;i<10000;++i){++f.now;assets.Tick("first",true);}
    Check(f.launches==1&&assets.Value()==old&&assets.Revision()==revision,"Loaded immutable data reused indefinitely");
    assets.Tick("second",false);Check(!assets.Value()&&f.launches==1,"Source change clears stale data without inactive work");
    assets.Tick("second",true);assets.Tick("third",true);
    Check(f.launches==2,"Changing source while pending never waits or replaces active future");
    f.Complete(1);assets.Tick("third",true);
    Check(!assets.Value()&&f.launches==3,"Old result rejected and replacement starts");
    f.Complete(2);assets.Tick("third",true);
    Check(*assets.Value()=="third"&&*old=="first","Replacement and retained prior snapshot remain independent");
    assets.Tick("error",true);f.failRead=true;f.now=20000;f.Complete(3);assets.Tick("error",true);
    Check(assets.Failed()&&!assets.Busy(),"Worker error contained");
    for(int i=0;i<1000;++i){f.now=20000+i;assets.Tick("error",true);}
    Check(f.launches==4,"Read error uses bounded retry delay");
    f.now=21000;f.failRead=false;assets.Tick("error",true);f.Complete(4);assets.Tick("error",true);
    Check(*assets.Value()=="error"&&!assets.Failed(),"Error recovers");
    f.failLaunch=true;assets.Tick("launch",true);Check(f.now==21500&&assets.Failed(),"Launch failure contained with completion timestamp");
    f.now=22499;assets.Tick("launch",true);Check(f.launches==6,"Slow failed launch starts backoff after return");
    f.now=22500;f.failLaunch=false;assets.Tick("launch",true);f.Complete(5);assets.Tick("launch",true);
    Check(assets.Value()&&f.launches==7,"Launch failure retries successfully");
    assets.Tick("last",true);f.jobs[6]->promise.set_exception(std::make_exception_ptr(std::runtime_error("future")));
    assets.Tick("last",true);f.now=1;assets.Tick("last",true);
    Check(f.launches==9,"Clock rollback permits retry");f.Complete(7);assets.Tick("last",true);
    Check(*assets.Value()=="last","Future failure recovers with original source key");
    assets.Invalidate();assets.Tick("last",true);
    Check(!assets.Value()&&f.launches==10,"Manual retry invalidates cached data");
    f.Complete(8);assets.Tick("last",true);
    Check(*assets.Value()=="last","Manual retry publishes replacement data");
    std::cout<<"100000 disabled asset ticks: zero workers and zero asset reads.\n";
}
}
int main(){try{Tests();std::cout<<checks<<" deferred asset checks passed.\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
