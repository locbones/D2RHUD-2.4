#include <Windows.h>
#include <atomic>
static std::atomic<unsigned> flushCalls{0};static std::atomic<DWORD> replaceFlags{0};
static BOOL TrackedFlush(HANDLE file){++flushCalls;return FlushFileBuffers(file);}
static BOOL TrackedMove(LPCWSTR from,LPCWSTR to,DWORD flags){replaceFlags=flags;return MoveFileExW(from,to,flags);}
#define FlushFileBuffers TrackedFlush
#define MoveFileExW TrackedMove
#include "../d2rhud/plugin/MarketplaceChat.h"
#undef FlushFileBuffers
#undef MoveFileExW
#include <cassert>
#include <future>
#include <iostream>
using namespace Marketplace;
int wmain(int argc,wchar_t** argv){
    assert(argc==2);const auto folder=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(folder);
    using Writer=Chat::PresenceWriter;std::vector<Writer::Task> tasks;std::vector<Writer::Request> written;
    auto launch=[&](Writer::Task task){tasks.push_back(std::move(task));};auto write=[&](const Writer::Request& r){written.push_back(r);};
    {
        Writer writer(launch,write);writer.Schedule({folder/L"presence.json","old",1});writer.Schedule({folder/L"presence.json","new",2});
        assert(tasks.size()==1&&written.empty());tasks.back()();assert(written.size()==1&&written.back().owner=="new"&&written.back().at==2);
        writer.Schedule({folder/L"presence.json","cancelled",3});writer.Reset();tasks.back()();assert(written.size()==1);
        writer.Schedule({folder/L"presence.json","after-reset",4});tasks.back()();assert(written.back().owner=="after-reset");
    }
    {Writer writer(launch,write);writer.Schedule({folder/L"presence.json","destroyed",5});}tasks.back()();assert(written.back().owner!="destroyed");
    int attempts=0;{Writer writer(launch,[&](const auto&){if(++attempts==1)throw std::runtime_error("Injected I/O failure");});writer.Schedule({folder/L"presence.json","retry",6});tasks.back()();writer.Schedule({folder/L"presence.json","retry",7});tasks.back()();assert(attempts==2);}
    int launches=0;{Writer writer([&](Writer::Task task){if(++launches==1)throw std::runtime_error("Injected launch failure");tasks.push_back(std::move(task));},write);writer.Schedule({folder/L"presence.json","retry-launch",8});writer.Schedule({folder/L"presence.json","retry-launch",9});tasks.back()();assert(written.back().at==9);}
    // A stalled storage write must not hold the mailbox mutex or Bridge lifetime.
    std::promise<void> entered,release,finished;auto blocked=release.get_future().share();
    auto bridge=std::make_unique<Chat::Bridge>(Writer::Launch{},[&](const Writer::Request& request){assert(request.owner=="alice");entered.set_value();blocked.wait();finished.set_value();});
    bridge->Configure(folder,"alice",false);
    auto tick=std::async(std::launch::async,[&]{bridge->Tick(3000,[](const auto&){});});
    assert(entered.get_future().wait_for(std::chrono::seconds(2))==std::future_status::ready);
    assert(tick.wait_for(std::chrono::seconds(1))==std::future_status::ready);tick.get();
    assert(bridge->Outgoing("/market")); // Writer still blocked: no mailbox lock held.
    auto destroy=std::async(std::launch::async,[&]{bridge.reset();});assert(destroy.wait_for(std::chrono::seconds(1))==std::future_status::ready);destroy.get();
    release.set_value();assert(finished.get_future().wait_for(std::chrono::seconds(2))==std::future_status::ready);
    // Default worker still writes the compatible presence document.
    {Writer writer;const auto path=folder/L"real-presence.json";writer.Schedule({path,std::string(32,'a'),1793761445});
        bool found=false;for(int i=0;i<100;++i){try{const auto value=Json::parse(ReadSettingsFile(path));found=value.at("owner")==std::string(32,'a')&&value.at("at")==1793761445;}catch(...){}if(found)break;Sleep(10);}assert(found&&ReadSettingsFile(path).size()==71);
    }
    assert(flushCalls==0&&replaceFlags==MOVEFILE_REPLACE_EXISTING); // Presence never forces disk durability; transaction writers keep their existing durable API.
    std::cout<<"Presence: asynchronous cached atomic heartbeat without forced flush, latest-only bounded queue, reset/destruction cancellation, I/O/launch retry; blocked writer cannot stall chat tick, mailbox commands or Bridge destruction passed.\n";
}
