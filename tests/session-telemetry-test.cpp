#include <Windows.h>
#include <atomic>
#include <thread>
#include <vector>
#include <cassert>
#include <iostream>
std::atomic<uint64_t> clockReads{0},ticks{100};
BOOL ControlledClock(LARGE_INTEGER* value){++clockReads;value->QuadPart=ticks.fetch_add(100);return TRUE;}
#define QueryPerformanceCounter ControlledClock
#include "../d2rhud/RuntimeTelemetry.h"
#undef QueryPerformanceCounter
#include "../d2rhud/SessionEpoch.h"
#include "../d2rhud/plugin/ItemFilter/ItemSessionState.h"
int main(){
    using namespace HudTelemetry;
    for(int i=0;i<100000;++i){Scope disabled(Feature::Frame);assert(activeFeature==Feature::Frame);}
    assert(!clockReads && activeFeature==Feature::None);
    SetEnabled(true);State().frequency=1000000000;
    {Scope outer(Feature::Frame);{Scope inner(Feature::Combat);assert(activeFeature==Feature::Combat);}assert(activeFeature==Feature::Frame);}
    auto snapshot=Snapshot();assert(snapshot[size_t(Feature::Combat)].calls==1 && snapshot[size_t(Feature::Combat)].totalNs==100);
    assert(snapshot[size_t(Feature::Frame)].totalNs==300 && activeFeature==Feature::None);
    {Scope old(Feature::AssetRead);Reset();}assert(!Snapshot()[size_t(Feature::AssetRead)].calls);
    std::vector<std::thread> workers;for(int i=0;i<4;++i)workers.emplace_back([]{for(int j=0;j<10000;++j){Scope work(Feature::BackgroundWork);assert(activeFeature==Feature::BackgroundWork);}});
    for(auto& thread:workers)thread.join();snapshot=Snapshot();assert(snapshot[size_t(Feature::BackgroundWork)].calls==40000);
    const auto before=clockReads.load();SetEnabled(false);{Scope off(Feature::AssetRead);}assert(before==clockReads);
    HudSession::Tracker tracker;const auto first=tracker.Observe(true,0,false,111);
    assert(first && tracker.Observe(true,0,false,111)==first);
    assert(tracker.Observe(true,0,true,0)==first); // Temporarily unavailable fields.
    assert(tracker.Observe(true,0,false,0)==first); // Temporarily missing name.
    const auto second=tracker.Observe(true,0,false,222);assert(second!=first);
    assert(!tracker.Observe(false,999));assert(tracker.Observe(true,0,false,222)!=second);
    const auto loading=tracker.Observe(true,333,true,222);assert(loading!=second);
    assert(tracker.Observe(true,333,true,444)==loading && tracker.Observe(true,333,false,444)==loading);
    HudItems::SessionState items;items.Synchronize(first);items.names[42]="old character";items.transmogs[42]=19;items.showFiltered=true;items.removed.push_back({{1,2,3},false});items.readded[42]={{4,5},true};
    assert(!items.Synchronize(first) && items.names.size()==1 && items.removed.size()==1);
    assert(items.Synchronize(second) && items.names.empty() && items.transmogs.empty() && items.removed.empty() && items.readded.empty() && !items.showFiltered);
    items.inventoryApplications[42]=2;items.inventoryPaths[42]="old path";
    items.Synchronize(second+1);assert(items.inventoryApplications.empty() && items.inventoryPaths.empty());
    items.names[42]="new character";assert(items.names.at(42)=="new character");
    assert(items.Synchronize(0) && items.names.empty());
    std::cout<<"PASS: 100000 disabled scopes make zero clock queries; nested scopes restore fault attribution; reset rejects old windows; 40000 threaded samples; joiner/character/load/reentry boundaries and real item-session replay/cache reset.\n";
}
