#define NOMINMAX
#include "../d2rhud/plugin/D2RHUD/LootFilterBuilderCatalog.h"
#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <new>
#include <stdexcept>
#include <thread>

using namespace HudBuilderCatalog;
using namespace std::chrono_literals;
namespace AllocationFixture {
thread_local bool enabled = false;
thread_local size_t count = 0;
}
void* operator new(size_t bytes) {
    if (void* memory = std::malloc(bytes ? bytes : 1)) {
        if (AllocationFixture::enabled) ++AllocationFixture::count;
        return memory;
    }
    throw std::bad_alloc();
}
void* operator new[](size_t bytes) { return ::operator new(bytes); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, size_t) noexcept { std::free(memory); }
namespace {
int checks = 0;
void Check(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
Catalog Data(const std::string& name) {
    Catalog result; result.loaded = true;
    result.items.push_back({name, name});
    result.bases[name].name = name;
    return result;
}
struct Job { Task task; std::promise<Result> promise; };
struct Fixture {
    uint64_t now = 100;
    unsigned launches = 0, reads = 0;
    bool launchFails = false, loadFails = false, invalidFuture = false, empty = false;
    std::vector<std::shared_ptr<Job>> jobs;
    std::vector<std::pair<std::filesystem::path, std::string>> inputs;
    Operations Ops() {
        return {
            [this](const auto& root, const auto& mod) {
                ++reads; inputs.emplace_back(root, mod);
                if (loadFails) throw std::runtime_error("unavailable asset");
                if (empty) return Catalog{};
                return Data(mod);
            },
            [this] { return now; },
            [this](Task task) {
                ++launches;
                if (launchFails) { now += 500; throw std::runtime_error("thread unavailable"); }
                if (invalidFuture) return std::future<Result>{};
                auto job = std::make_shared<Job>(); job->task = std::move(task);
                jobs.push_back(job); return job->promise.get_future();
            }
        };
    }
    void Complete(size_t index) { jobs.at(index)->promise.set_value(jobs.at(index)->task()); }
};
void Lifecycle() {
    Fixture f; Loader loader(f.Ops());
    const auto& initial = loader.Get(L"C:\\fixture", "first");
    Check(!initial.loaded && initial.status.find("Loading") != std::string::npos, "Initial view describes loading");
    for (int i = 0; i < 10000; ++i) loader.Get(L"C:\\fixture", "first");
    Check(f.launches == 1 && f.reads == 0, "10000 pending polls never execute or duplicate asset reading");
    f.now = 5000; f.Complete(0);
    Check(loader.Get(L"C:\\fixture", "first").items[0].value == "first", "Completed catalog published");
    Check(f.reads == 1 && f.inputs[0].first == L"C:\\fixture", "Worker captured source values");
    for (int i = 0; i < 10000; ++i) { ++f.now; loader.Get(L"C:\\fixture", "first"); }
    Check(f.launches == 1, "Successful catalog is reused indefinitely without repeated disk work");
    loader.Invalidate();
    Check(loader.Get(L"C:\\fixture", "first").loaded && f.launches == 2, "Reload retains known-good data");
    f.loadFails = true; f.now = 30000; f.Complete(1);
    auto& failed = loader.Get(L"C:\\fixture", "first");
    Check(failed.loaded && failed.items[0].value == "first" && failed.status.find("Retrying") != std::string::npos, "Reload error preserves data and reports retry");
    for (int i = 0; i < 1000; ++i) { f.now = 30000 + i; loader.Get(L"C:\\fixture", "first"); }
    Check(f.launches == 2, "Failure backoff uses completion time, including retained successful data");
    f.now = 31000; loader.Get(L"C:\\fixture", "first");
    Check(f.launches == 3, "Failed reload retries at one second");
    f.loadFails = false; f.Complete(2);
    Check(loader.Get(L"C:\\fixture", "first").status.empty(), "Recovery clears failure status");

    loader.Invalidate(); loader.Get(L"C:\\fixture", "first");
    Check(!loader.Get(L"D:\\other", "second").loaded, "Source change clears previous source data");
    Check(f.launches == 4, "Changing source never replaces or waits on a running future");
    f.Complete(3);
    auto& switched = loader.Get(L"D:\\other", "second");
    Check(!switched.loaded && f.launches == 5, "Stale result rejected and latest source starts");
    f.Complete(4);
    Check(loader.Get(L"D:\\other", "second").items[0].value == "second", "New source publishes independently");
    Check(f.inputs.back() == std::make_pair(std::filesystem::path(L"D:\\other"), std::string("second")), "New root and mod captured correctly");

    loader.Invalidate(); loader.Get(L"D:\\other", "second");
    loader.Invalidate(); loader.Invalidate(); f.Complete(5);
    loader.Get(L"D:\\other", "second");
    Check(f.launches == 7, "Repeated invalidations coalesce into one replacement load");
    f.Complete(6); loader.Get(L"D:\\other", "second");
    Check(f.reads == 7, "Every completed task read only once");
}
void Failures() {
    Fixture f; f.launchFails = true; Loader loader(f.Ops());
    Check(loader.Get(L"root", "mod").status.find("Retrying") != std::string::npos && f.now == 600, "Worker launch exception stays inside loader");
    for (int i = 0; i < 1000; ++i) { f.now = 600 + i; loader.Get(L"root", "mod"); }
    Check(f.launches == 1, "Launch failure backoff starts after failed API returns");
    f.now = 1600; f.launchFails = false; loader.Get(L"root", "mod");
    Check(f.launches == 2, "Launch failure recovers");
    f.empty = true; f.now = 9000; f.Complete(0);
    Check(!loader.Get(L"root", "mod").loaded, "Loader rejects an explicitly incomplete catalog");
    f.now = 9999; loader.Get(L"root", "mod"); Check(f.launches == 2, "Incomplete catalog also backs off");
    f.now = 10000; loader.Get(L"root", "mod"); Check(f.launches == 3, "Incomplete catalog retries");
    f.jobs[1]->promise.set_exception(std::make_exception_ptr(std::runtime_error("future error")));
    Check(loader.Get(L"root", "mod").status.find("Retrying") != std::string::npos, "Future exception contained");
    f.now = 1; loader.Get(L"root", "mod"); Check(f.launches == 4, "Clock rollback permits recovery");
    f.empty = false; f.Complete(2); Check(loader.Get(L"root", "mod").loaded, "Recovered worker publishes");
    loader.Invalidate(); f.invalidFuture = true; loader.Get(L"root", "mod");
    Check(f.launches == 5, "Invalid future is treated as a launch failure");
    for (int i = 0; i < 10000; ++i) loader.Get(L"root", "mod");
    Check(f.launches == 5, "Invalid future does not cause a render-frame retry loop");
    f.now = 1001; f.invalidFuture = false; loader.Get(L"root", "mod"); f.Complete(3);
    Check(loader.Get(L"root", "mod").status.empty(), "Invalid future recovers while retaining catalog");
}
void StaleException() {
    Fixture f; Loader loader(f.Ops()); loader.Get(L"first", "mod");
    loader.Get(L"second", "mod");
    f.jobs[0]->promise.set_exception(std::make_exception_ptr(std::runtime_error("old future failed")));
    Check(loader.Get(L"second", "mod").status.find("Loading") != std::string::npos && f.launches == 2, "Old future failure cannot delay or mark new source failed");
    f.Complete(1); Check(loader.Get(L"second", "mod").loaded, "Source switch recovers after old future exception");
}
void RealWorker() {
    std::mutex mutex; std::condition_variable cv;
    bool entered = false, release = false;
    std::atomic<unsigned> calls = 0;
    std::thread::id worker;
    Loader loader({[&](const auto&, const auto& mod) {
        ++calls;
        std::unique_lock lock(mutex); worker = std::this_thread::get_id(); entered = true; cv.notify_one();
        cv.wait(lock, [&] { return release; }); return Data(mod);
    }});
    loader.Get(L"isolated", "async");
    { std::unique_lock lock(mutex); if (!cv.wait_for(lock, 4s, [&] { return entered; })) throw std::runtime_error("Worker never entered"); }
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 10000; ++i) loader.Get(L"isolated", "async");
    const auto elapsed = std::chrono::steady_clock::now() - start;
    { std::lock_guard lock(mutex); release = true; } cv.notify_one();
    const auto deadline = std::chrono::steady_clock::now() + 4s;
    while (!loader.Get(L"isolated", "async").loaded) {
        if (std::chrono::steady_clock::now() >= deadline) throw std::runtime_error("Worker did not complete");
        std::this_thread::sleep_for(1ms);
    }
    Check(calls == 1 && worker != std::this_thread::get_id(), "Default launch runs asset loading on one real worker");
    Check(elapsed < 1s, "10000 Get calls finish while asset worker remains blocked");
    std::cout << "10000 polls while worker blocked: " << std::chrono::duration<double, std::milli>(elapsed).count() << " ms; one worker.\n";
}
void BorrowedOptions() {
    using namespace LootFilterConfig::Builder;
    Catalog catalog; for (int i = 0; i < 5000; ++i) catalog.items.push_back({"Long catalog item label " + std::to_string(i), std::to_string(i)});
    catalog.types = {{"Category", "12"}}; catalog.areas = {{"Area", "101"}};
    Check(&Options("codes", catalog) == &catalog.items, "Item list borrowed");
    Check(&Options("itype", catalog) == &catalog.types, "Category list borrowed");
    Check(&Options("area", catalog) == &catalog.areas, "Area list borrowed");
    for (const char* key : {"quality", "rarity", "difficulty", "location", "name_style", "background_style"})
        Check(!Options(key, catalog).empty() && &Options(key, catalog) == &Options(key, Catalog{}), "Fixed choices reused across catalogs");
    Check(Options("unknown", catalog).empty(), "Unknown choice key remains empty");
    const auto extra = SelectionExtras("codes", {"allitems", "0", "custom", "custom", "missing"}, catalog.items);
    Check(extra.size() == 3 && extra[0].value == "missing" && extra[1].value == "custom" && extra[2].value == "allitems", "Custom choices and All items retain original order without duplicates");
    Check(catalog.items.size() == 5000 && catalog.items[0].value == "0", "Extras do not modify source catalog");
    const auto quality = SelectionExtras("quality", {"Unique", "Custom Quality"}, Options("quality", catalog));
    Check(quality.size() == 1 && quality[0].value == "Custom Quality", "Unknown non-item choices remain removable");
    Check(Options("quality", catalog).size() == 9 && Options("location", catalog)[1].value == "onplayer", "Fixed choice contents unchanged");
    size_t borrowedItems = 0;
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 10000; ++i) { const auto& list = Options("codes", catalog); borrowedItems += list.size(); }
    const auto duration = std::chrono::steady_clock::now() - start;
    Check(borrowedItems == 50000000, "10000 borrows preserve all 5000 entries");
    std::cout << "10000 item-option reads: 50000000 entries borrowed, zero catalog-entry copies; " << std::chrono::duration<double, std::milli>(duration).count() << " ms (component fixture).\n";
    AllocationFixture::count = 0; AllocationFixture::enabled = true;
    size_t copied = 0;
    for (int i = 0; i < 100; ++i) { auto oldList = catalog.items; copied += oldList.size(); }
    AllocationFixture::enabled = false; const auto oldAllocations = AllocationFixture::count;
    AllocationFixture::count = 0; AllocationFixture::enabled = true;
    size_t borrowed = 0;
    for (int i = 0; i < 100; ++i) { const auto& list = Options("codes", catalog); borrowed += list.size(); }
    AllocationFixture::enabled = false; const auto newAllocations = AllocationFixture::count;
    Check(copied == borrowed && copied == 500000 && oldAllocations >= 500100, "Old catalog-copy fixture exercises actual vector/string allocations");
    Check(newAllocations == 0, "Borrowed item options require zero vector/string allocations");
    std::cout << "100 reads of 5000-item catalog: " << oldAllocations << " old-copy allocations -> " << newAllocations << " borrowed-list allocations.\n";
}
}
int main() { try { Lifecycle(); Failures(); StaleException(); RealWorker(); BorrowedOptions();
    std::cout << checks << " builder catalog checks passed.\n"; return 0;
} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; } }
