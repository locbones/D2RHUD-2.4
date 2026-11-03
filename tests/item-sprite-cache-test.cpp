#define NOMINMAX
#include "../d2rhud/plugin/ItemSpriteCache.h"
#include "../d2rhud/plugin/GameAssets.h"
#include "../d2rhud/Pattern.h"
#include "../d2rhud/ProcessLifetime.h"
#include <Windows.h>
#include <atomic>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>

using namespace LegacyTransmog::Skin;
using namespace std::chrono_literals;
namespace {
int checks=0;
void Check(bool value,const char* text){++checks;if(!value)throw std::runtime_error(text);}
std::shared_ptr<const Sprite> Pixels(size_t count=4,uint8_t mark=7){
    auto sprite=std::make_shared<Sprite>();sprite->atlas.pixels.assign(count,mark);return sprite;
}
std::shared_ptr<const ItemSource> Source(const char* mod="fixture"){
    auto source=std::make_shared<ItemSource>();source->install=L"C:\\fixture";source->mod=mod;
    for(int i=0;i<20;++i)source->mappings["c"+std::to_string(i)]="custom/icon"+std::to_string(i);
    source->mappings["bad"]="../../outside";return source;
}
struct Job {ItemTask task;std::promise<ItemResult> promise;};
struct Fixture {
    uint64_t now=100;unsigned launches=0,reads=0;
    bool throwLaunch=false,throwRead=false,missing=false,invalid=false;
    size_t bytes=4;std::vector<std::shared_ptr<Job>> jobs;std::vector<std::string> mods,paths;
    ItemOperations Ops(){return {
        [this](const ItemSource& source,const std::vector<std::string>& files){
            ++reads;mods.push_back(source.mod);paths=files;
            if(throwRead)throw std::runtime_error("read failed");
            return missing?std::shared_ptr<const Sprite>{}:Pixels(bytes);
        },[this]{return now;},[this](ItemTask task){
            ++launches;if(throwLaunch){now+=500;throw std::runtime_error("launch failed");}
            if(invalid)return std::future<ItemResult>{};
            auto job=std::make_shared<Job>();job->task=std::move(task);jobs.push_back(job);return job->promise.get_future();
        }};}
    void Complete(size_t n){auto& job=jobs.at(n);job->promise.set_value(job->task());}
};
void Jobs(){
    HudAsync::Job<int> job;Check(!job.Busy()&&!job.Poll(),"Empty job is idle");
    std::promise<int> promise;
    HudAsync::Job<int> controlled([&](auto){return promise.get_future();});
    Check(controlled.Start(41,[]{return 0;}),"Controlled job starts");
    Check(!controlled.Start(42,[]{return 1;}),"Busy job cannot be replaced");
    for(int i=0;i<10000;++i)if(controlled.Poll())throw std::runtime_error("Premature completion");
    Check(controlled.Busy(),"10000 polls leave pending work owned");
    promise.set_value(9);auto result=controlled.Poll();
    Check(result&&result->ticket==41&&result->value==9&&!controlled.Busy(),"Successful completion retains original ticket");
    Check(!controlled.Poll(),"Completion consumed exactly once");
    std::promise<int> failure;
    HudAsync::Job<int> failed([&](auto){return failure.get_future();});
    failed.Start(7,[]{return 0;});failure.set_exception(std::make_exception_ptr(std::runtime_error("worker failed")));
    result=failed.Poll();Check(result&&result->ticket==7&&!result->value&&!failed.Busy(),"Worker exception returns failure with original ticket");
    HudAsync::Job<int> throwing([](auto)->std::future<int>{throw std::runtime_error("no thread");});
    Check(!throwing.Start(1,[]{return 0;})&&!throwing.Busy(),"Launch exceptions contained");
    HudAsync::Job<int> invalid([](auto){return std::future<int>{};});
    Check(!invalid.Start(1,[]{return 0;})&&!invalid.Busy(),"Invalid future refused");
    int calls=0;
    HudAsync::Job<int> deferred([](auto task){return std::async(std::launch::deferred,std::move(task));});
    Check(!deferred.Start(1,[&]{++calls;return 4;})&&!deferred.Poll()&&calls==0,"Deferred work cannot run file I/O on polling thread");
}
void Lifetime(){
    wchar_t path[32768]{};Check(GetModuleFileNameW(nullptr,path,DWORD(std::size(path)))!=0,"Lifetime executable path available");
    auto run=[&](const wchar_t* mode){
        std::wstring command=L"\""+std::wstring(path)+L"\" "+mode;
        STARTUPINFOW start{};start.cb=sizeof(start);PROCESS_INFORMATION process{};
        const auto before=GetTickCount64();
        Check(CreateProcessW(path,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&start,&process)!=FALSE,"Lifetime child starts");
        const auto wait=WaitForSingleObject(process.hProcess,8000);DWORD exit=1;GetExitCodeProcess(process.hProcess,&exit);
        CloseHandle(process.hThread);CloseHandle(process.hProcess);
        Check(wait==WAIT_OBJECT_0&&exit==0,"Lifetime child exits successfully");return GetTickCount64()-before;
    };
    const auto retained=run(L"--retained-job"),ordinary=run(L"--static-job");
    Check(retained<1500,"Process-lifetime job does not join worker during CRT teardown");
    Check(ordinary>=1750,"Positive control static future waits for worker at shutdown");
    std::cout<<"Background-job exit: retained "<<retained<<" ms; static-future positive control "<<ordinary<<" ms.\n";
}
void Cache(){
    Fixture f;ItemCache cache(f.Ops());auto source=Source();
    Check(!cache.Get(source,"c0")&&f.launches==1,"First icon starts one load");
    for(int i=0;i<10000;++i)cache.Get(source,"c0");
    Check(f.launches==1&&f.reads==0,"10000 polls perform no I/O and start no duplicate work");
    f.Complete(0);auto first=cache.Get(source,"c0");
    Check(first&&f.paths==std::vector<std::string>{"hd/global/ui/items/custom/icon0.sprite"},"Native path normalization retained");
    cache.Get(source,"c1");Check(cache.Get(source,"c0")==first,"Cached icon remains available while another loads");
    f.Complete(1);auto second=cache.Get(source,"c1");Check(second&&second!=first,"Second icon publishes independently");
    bool stable=true;for(int i=0;i<10000;++i)stable &= cache.Get(source,i%2?"c0":"c1")== (i%2?first:second);
    Check(stable,"Alternating requests retain exact shared icon");
    Check(f.reads==2&&f.launches==2,"10000 alternating requests require only two decodes");
    std::cout<<"10000 alternating icon requests: 2 reads/decodes; retained pointers reused.\n";
    for(int i=2;i<8;++i){cache.Get(source,"c"+std::to_string(i));f.Complete(f.jobs.size()-1);cache.Get(source,"c"+std::to_string(i));}
    Check(cache.Entries()==8&&cache.PixelBytes()==32,"Entry/pixel accounting includes eight icons");
    cache.Get(source,"c0");cache.Get(source,"c8");f.Complete(f.jobs.size()-1);cache.Get(source,"c8");
    const auto launches=f.launches;
    Check(cache.Get(source,"c0")==first&&f.launches==launches,"Recently used icon survives eviction");
    cache.Get(source,"c1");Check(f.launches==launches+1,"Least recently used icon reloads after eviction");
    f.Complete(f.jobs.size()-1);cache.Get(source,"c1");
    Check(second->atlas.pixels[0]==7,"External owner keeps evicted icon valid");
    cache.Get(source,"c9");const auto pending=f.jobs.size()-1;
    auto other=Source("other");Check(!cache.Get(other,"c9")&&cache.Entries()==0,"Source change clears cache without waiting for worker");
    f.Complete(pending);Check(!cache.Get(other,"c9")&&f.mods.back()=="fixture","Old source completion rejected");
    f.Complete(f.jobs.size()-1);Check(cache.Get(other,"c9")&&f.mods.back()=="other","Replacement work captured new source");
    cache.Get(other,"c10");const auto old=f.jobs.size()-1;cache.Get(source,"c10");
    f.jobs[old]->promise.set_exception(std::make_exception_ptr(std::runtime_error("old failed")));
    const auto before=f.launches;cache.Get(source,"c10");
    Check(f.launches==before+1,"Old worker exception cannot poison new source negative cache");
    f.Complete(f.jobs.size()-1);Check(bool(cache.Get(source,"c10")),"Replacement recovers after stale exception");
    Check(!cache.Get({},"c10")&&cache.Entries()==0&&cache.PixelBytes()==0,"Null source resets cache");
}
void FailureAndBounds(){
    Fixture f;ItemCache cache(f.Ops());auto source=Source();f.throwLaunch=true;
    Check(!cache.Get(source,"c0")&&f.now==600,"Launch error stays inside cache");
    for(int i=0;i<2000;++i){f.now=600+i;cache.Get(source,"c0");}
    Check(f.launches==1,"Retry delay starts after failed launch returns");
    f.throwLaunch=false;f.now=2600;cache.Get(source,"c0");Check(f.launches==2,"Failed launch retries after two seconds");
    f.throwRead=true;f.now=10000;f.Complete(0);cache.Get(source,"c0");
    f.now=11999;cache.Get(source,"c0");Check(f.launches==2,"Read error uses completion-based retry delay");
    f.now=12000;cache.Get(source,"c0");Check(f.launches==3,"Reader exception recovers");
    f.throwRead=false;f.Complete(1);Check(bool(cache.Get(source,"c0")),"Recovery publishes icon");
    f.invalid=true;cache.Get(source,"c1");const auto invalid=f.launches;
    for(int i=0;i<10000;++i)cache.Get(source,"c1");
    Check(f.launches==invalid,"Invalid future does not cause frame-by-frame launch attempts");
    f.invalid=false;f.now=1;cache.Get(source,"c1");Check(f.launches==invalid+1,"Clock rollback permits retry");
    f.Complete(f.jobs.size()-1);cache.Get(source,"c1");
    const auto before=f.launches;cache.Get(source,"");cache.Get(source,"unknown");cache.Get(source,"bad");
    Check(f.launches==before,"Empty, absent and unsafe paths never launch readers");
    f.missing=true;cache.Get(source,"c2");f.Complete(f.jobs.size()-1);Check(!cache.Get(source,"c2"),"Missing decode produces no art");
    f.now=2001;f.missing=false;cache.Get(source,"c2");f.Complete(f.jobs.size()-1);Check(bool(cache.Get(source,"c2")),"Previously missing icon can recover");
    auto bounded=Source("bounded");f.bytes=12*1024*1024;
    std::shared_ptr<const Sprite> retained;
    for(int i=0;i<3;++i){auto code="c"+std::to_string(i);cache.Get(bounded,code);f.Complete(f.jobs.size()-1);auto icon=cache.Get(bounded,code);if(i==0)retained=icon;}
    Check(cache.Entries()==2&&cache.PixelBytes()==24*1024*1024,"Pixel budget evicts before exceeding 32 MiB");
    Check(retained&&retained->atlas.pixels[0]==7,"Eviction preserves caller-owned pixels");
    f.bytes=ItemCache::MaxPixelBytes+1;auto over=Source("oversized");cache.Get(over,"c0");f.Complete(f.jobs.size()-1);
    Check(!cache.Get(over,"c0")&&cache.PixelBytes()==0,"Oversized result refused");
    f.bytes=0;cache.Get(over,"c1");f.Complete(f.jobs.size()-1);
    Check(!cache.Get(over,"c1"),"Empty pixel result refused");
}

std::mutex ioMutex;std::condition_variable ioCv;
std::map<std::string,std::vector<uint8_t>> files;
bool blockPanel=false,panelEntered=false,releasePanel=false,ioFails=false;
std::atomic<unsigned> iconReads{0};
std::array<char,128> modBytes{'R','M','D','-','M','P'};
std::vector<uint8_t> SpriteFile(uint8_t value){
    std::vector<uint8_t> bytes(44,value);memcpy(bytes.data(),"SpA1",4);
    auto put=[&](size_t at,auto number){memcpy(bytes.data()+at,&number,sizeof(number));};
    put(4,uint16_t(31));put(6,uint16_t(1));put(8,uint32_t(1));put(12,uint32_t(1));put(20,uint32_t(1));return bytes;
}
void Mapping(const char* asset,uint8_t value){
    std::lock_guard lock(ioMutex);
    const auto text=std::string("[{\"Y20\":{\"asset\":\"custom/")+asset+"\"}}]";
    files["hd/items/items.json"]={text.begin(),text.end()};
    files[std::string("hd/global/ui/items/custom/")+asset+".sprite"]=SpriteFile(value);
    files["hd/global/ui/panel/modals/modal_bg.sprite"]=SpriteFile(value);
    blockPanel=true;panelEntered=releasePanel=false;
}
void WaitPanel(){std::unique_lock lock(ioMutex);if(!ioCv.wait_for(lock,4s,[]{return panelEntered;}))throw std::runtime_error("Startup reader never entered");}
void ReleasePanel(){std::lock_guard lock(ioMutex);releasePanel=true;ioCv.notify_all();}
std::shared_ptr<const Sprite> ReadyIcon(){
    auto deadline=std::chrono::steady_clock::now()+4s;
    for(;;){if(auto result=ItemSprite("Y20"))return result;if(std::chrono::steady_clock::now()>deadline)throw std::runtime_error("Icon not ready");std::this_thread::sleep_for(1ms);}
}
void Publication(){
    Check(!Assets()&&!ItemSprite("Y20"),"No partially initialized runtime source");
    Mapping("first",7);std::thread load([]{Load();});WaitPanel();
    bool hidden=true;for(int i=0;i<10000;++i)hidden &= !Assets()&&!ItemSprite("Y20");
    ReleasePanel();load.join();Check(hidden&&iconReads==0,"Startup construction remains private during render polling");
    auto panels=Assets();Check(panels&&(*panels)[Frame].atlas.pixels[0]==7,"Complete panel snapshot published");
    auto first=ReadyIcon();Check(first->atlas.pixels[0]==7&&iconReads==1,"Real production wrapper loads and decodes on worker");
    for(int i=0;i<10000;++i)if(ItemSprite("Y20")!=first)throw std::runtime_error("Cached runtime icon changed");
    Check(iconReads==1,"Production repeated requests issue no asset reads");
    Mapping("second",9);std::thread replacement([]{Load();});WaitPanel();
    Check(Assets()==panels&&ItemSprite("Y20")==first,"Existing snapshot remains usable during replacement load");
    ReleasePanel();replacement.join();auto second=ReadyIcon();
    Check(second!=first&&second->atlas.pixels[0]==9&&Assets()!=panels,"Replacement mapping invalidates icon cache");
    Check(first->atlas.pixels[0]==7&&(*panels)[Frame].atlas.pixels[0]==7,"Old shared snapshots remain valid");
    {std::lock_guard lock(ioMutex);ioFails=true;}Load();
    Check(ItemSprite("Y20")==second,"Failed pack reload retains prior complete source");
    {std::lock_guard lock(ioMutex);ioFails=false;}
}
}
DWORD64 Pattern::Address(uint32_t rva){return rva==0x1bf084f?reinterpret_cast<DWORD64>(modBytes.data()):0;}
GameAssets::GameAssets(const std::filesystem::path& install,const std::string& mod):install_(install),mod_(mod){}
GameAssets::~GameAssets()=default;
std::vector<uint8_t> GameAssets::Read(const std::string& name){
    std::unique_lock lock(ioMutex);if(ioFails)throw std::runtime_error("fixture source failed");
    if(name.find("hd/global/ui/items/")==0)++iconReads;
    if(blockPanel&&name=="hd/global/ui/panel/modals/modal_bg.sprite"){
        panelEntered=true;ioCv.notify_all();ioCv.wait(lock,[]{return releasePanel;});blockPanel=false;
    }
    auto found=files.find(name);return found==files.end()?std::vector<uint8_t>{}:found->second;
}
int main(int argc,char** argv){try{
    if(argc>1){
        auto start=[](auto& job){job.Start(1,[]{Sleep(2000);return 7;});};
        if(std::string(argv[1])=="--retained-job")start(HudLifetime::State<HudAsync::Job<int>>());
        else if(std::string(argv[1])=="--static-job"){static HudAsync::Job<int> job;start(job);}
        else return 1;
        return 0;
    }
    Jobs();Cache();FailureAndBounds();Publication();Lifetime();std::cout<<checks<<" item cache/background job checks passed.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
