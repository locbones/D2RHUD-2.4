#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include "ProcessLifetime.h"

namespace HudTelemetry {
enum class Feature : uint8_t { None,Frame,Menu,Combat,MonsterStats,Buffs,StatRanges,MapTick,LootFilter,AssetRead,BackgroundWork,Camera,Commands,Whirlwind,RunTracker,SpectateTransport,SpectateEncode,SpectateDecode,Count };
inline constexpr const char* Names[]{"Unattributed","HUD frame","HUD menus","Combat feedback","Monster overlay","Buff icons","Item stat ranges","Map/event tick","Loot filter","Asset file access","Background work","Camera","Queued commands","D3 Whirlwind","Run Tracker","Spectate transport","Spectate encode","Spectate decode"};
inline thread_local Feature activeFeature=Feature::None;
class FeatureScope {
    Feature previous_;
public:
    explicit FeatureScope(Feature value):previous_(activeFeature){activeFeature=value;}
    ~FeatureScope(){activeFeature=previous_;}
    FeatureScope(const FeatureScope&)=delete;
};
inline const char* ActiveFeature(){const auto index=size_t(activeFeature);return index<size_t(Feature::Count)?Names[index]:"Unknown feature";}
inline constexpr std::array<uint64_t,8> BucketNanoseconds{16000,64000,256000,1000000,4000000,16000000,64000000,UINT64_MAX};
struct Counter {
    std::atomic<uint64_t> calls{0},total{0},maximum{0};
    std::array<std::atomic<uint64_t>,8> buckets{};
    void Add(uint64_t ns){
        calls.fetch_add(1,std::memory_order_relaxed);total.fetch_add(ns,std::memory_order_relaxed);
        auto old=maximum.load(std::memory_order_relaxed);
        while(old<ns && !maximum.compare_exchange_weak(old,ns,std::memory_order_relaxed)){}
        for(size_t i=0;i<buckets.size();++i)if(ns<=BucketNanoseconds[i]){buckets[i].fetch_add(1,std::memory_order_relaxed);break;}
    }
};
struct Bank {std::array<Counter,size_t(Feature::Count)> counters;};
struct Runtime {
    std::atomic<std::shared_ptr<Bank>> bank{std::make_shared<Bank>()};
    int64_t frequency=[]{LARGE_INTEGER f{};QueryPerformanceFrequency(&f);return f.QuadPart;}();
};
inline Runtime& State(){return HudLifetime::State<Runtime>();}
inline std::atomic<bool> enabled{false};
inline bool Enabled(){return enabled.load(std::memory_order_relaxed);}
inline void SetEnabled(bool value){if(value)(void)State();enabled.store(value,std::memory_order_relaxed);}
inline void Reset(){State().bank.store(std::make_shared<Bank>());}
// Inclusive wall time: nested counters intentionally overlap. Disabled scopes
// perform no clock query, allocation, lock, file access or counter update.
class Scope {
    FeatureScope feature_;
    Feature id_;
    std::shared_ptr<Bank> bank_;
    int64_t started_=0,frequency_=0;
public:
    explicit Scope(Feature id):feature_(id),id_(id){
        if(!Enabled())return;
        auto& runtime=State();bank_=runtime.bank.load();frequency_=runtime.frequency;
        LARGE_INTEGER now{};if(frequency_>0 && QueryPerformanceCounter(&now))started_=now.QuadPart;else bank_.reset();
    }
    ~Scope(){if(bank_){LARGE_INTEGER now{};if(QueryPerformanceCounter(&now) && now.QuadPart>=started_)
        bank_->counters[size_t(id_)].Add(uint64_t((static_cast<long double>(now.QuadPart-started_)*1000000000.L)/frequency_));}}
    Scope(const Scope&)=delete;
};
struct Sample {uint64_t calls=0,totalNs=0,maximumNs=0,p95UpperNs=0;};
inline std::array<Sample,size_t(Feature::Count)> Snapshot(){
    const auto bank=State().bank.load();std::array<Sample,size_t(Feature::Count)> result{};
    for(size_t i=0;i<result.size();++i){auto& out=result[i];auto& counter=bank->counters[i];
        out.calls=counter.calls.load();out.totalNs=counter.total.load();out.maximumNs=counter.maximum.load();
        const auto target=out.calls-out.calls/20;uint64_t counted=0;
        for(size_t b=0;out.calls && b<counter.buckets.size();++b){counted+=counter.buckets[b].load();if(counted>=target){out.p95UpperNs=BucketNanoseconds[b]==UINT64_MAX?out.maximumNs:BucketNanoseconds[b];break;}}
    }return result;
}
}
