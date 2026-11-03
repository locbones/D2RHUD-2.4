#include "../d2rhud/plugin/D2RHUD/LootFilterCatalog.h"
#include "../d2rhud/plugin/ItemFilter/LootFilterLibrary.h"
#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace fs = std::filesystem;
using namespace HudFilterCatalog;
using namespace std::chrono_literals;
int checks = 0;
void Check(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
template<class F> void Until(F predicate) {
    const auto deadline = std::chrono::steady_clock::now() + 4s;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline) throw std::runtime_error("Timed out waiting for worker");
        std::this_thread::sleep_for(1ms);
    }
}
View Ready(Catalog& catalog) {
    View view;
    Until([&] { view = catalog.Get(); return !view.loading; });
    return view;
}
struct Scanner {
    std::atomic<uint64_t> now{100};
    std::atomic<int> calls{0};
    std::mutex mutex;
    std::condition_variable cv;
    int permits = 0;
    Names names{"first"};
    bool fail = false;
    std::vector<Directories> inputs;
    Operations Ops() {
        return { [this](const Directories& dirs) {
            std::unique_lock lock(mutex);
            inputs.push_back(dirs);
            ++calls;
            if (!cv.wait_for(lock, 3s, [&] { return permits > 0; })) throw std::runtime_error("Unreleased test gate");
            --permits;
            if (fail) throw std::runtime_error("Injected discovery failure");
            return names;
        }, [this] { return now.load(); } };
    }
    void Release(Names value, bool failure = false) {
        std::lock_guard lock(mutex);
        names = std::move(value); fail = failure; ++permits; cv.notify_one();
    }
    void Started(int expected) { Until([&] { return calls.load() == expected; }); }
};
void Lifecycle() {
    Scanner scan; Catalog catalog(scan.Ops());
    Check(!catalog.Get().loading && scan.calls == 0, "Unconfigured catalog starts no work");
    catalog.Configure({L"old"});
    const auto begin = std::chrono::steady_clock::now();
    const auto pending = catalog.Get(); scan.Started(1);
    Check(pending.loading && pending.names->empty(), "First scan returns loading placeholder");
    Check(std::chrono::steady_clock::now() - begin < 1s, "Blocked discovery does not block Get");
    for (int i = 0; i < 10000; ++i) catalog.Get();
    Check(scan.calls == 1, "Repeated pending reads start only one task");
    scan.Release({"first"}); auto first = Ready(catalog);
    Check(*first.names == Names{"first"} && first.error.empty(), "Publishes successful result");
    bool same = true;
    for (int i = 0; i < 10000; ++i) same &= catalog.Get().names == first.names;
    Check(same && scan.calls == 1, "Ten thousand warm reads reuse snapshot without discovery");
    catalog.Configure({L"old"});
    Check(catalog.Get().names == first.names && scan.calls == 1, "Identical configuration preserves cache");
    scan.now = 2099;
    Check(!catalog.Get().loading, "Cache is valid immediately before TTL");
    scan.now = 2100;
    Check(catalog.Get().loading, "Refresh begins at TTL"); scan.Started(2);
    Check(catalog.Get().names == first.names, "Refresh retains previous snapshot");
    scan.Release({}, true); auto failed = Ready(catalog);
    Check(failed.names == first.names && !failed.error.empty(), "Failure preserves usable list and reports error");
    for (int i = 0; i < 10000; ++i) catalog.Get();
    Check(scan.calls == 2, "Failures are backed off, not retried every frame");
    scan.now = 4100; catalog.Get(); scan.Started(3);
    scan.Release({"recovered"}); auto recovered = Ready(catalog);
    Check(*recovered.names == Names{"recovered"} && recovered.error.empty(), "Recovery replaces list and clears error");
    Check(*first.names == Names{"first"}, "Readers can retain older immutable snapshot");
    catalog.Invalidate(); catalog.Get(); scan.Started(4);
    catalog.Invalidate(); catalog.Invalidate();
    for (int i = 0; i < 1000; ++i) catalog.Get();
    Check(scan.calls == 4, "Invalidations never overlap discovery tasks");
    scan.Release({"stale"});
    Until([&] { catalog.Get(); return scan.calls == 5; });
    Check(catalog.Get().names == recovered.names, "Invalidated worker cannot publish stale names");
    scan.Release({"imported"}); auto imported = Ready(catalog);
    Check(*imported.names == Names{"imported"}, "Invalidation schedules one fresh scan");
    catalog.Invalidate(); catalog.Get(); scan.Started(6);
    catalog.Configure({L"new"});
    Check(catalog.Get().names->empty(), "Root change clears names from old mod immediately");
    scan.Release({"wrong mod"});
    Until([&] { catalog.Get(); return scan.calls == 7; });
    Check(catalog.Get().names->empty(), "Old root worker cannot publish");
    scan.Release({"new mod"});
    Check(*Ready(catalog).names == Names{"new mod"}, "New roots publish normally");
    { std::lock_guard lock(scan.mutex); Check(scan.inputs.back() == Directories{L"new"}, "Worker uses copied new roots"); }
    scan.now = 1;
    Check(catalog.Get().loading, "Clock rollback triggers refresh instead of wedging cache");
    scan.Started(8); scan.Release({});
    const auto empty = Ready(catalog);
    Check(empty.names->empty(), "Empty results replace removed filters");
    for (int i = 0; i < 10000; ++i) catalog.Get();
    Check(scan.calls == 8, "Empty directory results are cached");
}
void LatePoll() {
    std::atomic<uint64_t> now{100}; std::atomic<int> calls{0}, clockReads{0};
    Catalog catalog({[&](const Directories&) { ++calls; return Names{"late"}; }, [&] {
        const auto value = now.load(); ++clockReads; return value;
    }});
    catalog.Configure({L"late"}); catalog.Get();
    Until([&] { return calls == 1 && clockReads >= 2; }); // Worker stamped completion at 100.
    now = 10000;
    Until([&] { catalog.Get(); return calls == 2; });
    Check(calls == 2, "Late polling uses worker completion time, not UI poll time");
    Check(*Ready(catalog).names == Names{"late"}, "Late refresh completes");
}
void Write(const fs::path& path) {
    fs::create_directories(path.parent_path()); std::ofstream out(path); out << "return {}";
    if (!out) throw std::runtime_error("Fixture write failed");
}
void RealFiles(const fs::path& root) {
    const std::string mod = "Mód-日本", unicode = "Filtre-é-宝";
    const auto game = root / fs::u8path("Jeu-é-日本");
    const auto user = game / "My Filters";
    const auto package = game / "Mods" / fs::u8path(mod) / fs::u8path(mod + ".mpq") / "data/Pandemonium/Filters";
    Write(user / "Duplicate.lua"); Write(package / "duplicate.lua");
    Write(package / "Package/lootfilter_config.lua");
    Write(package / "UPPER.LUA"); Write(package / "override_rules.lua");
    Write(package / "lootfilter_config_blank.lua"); Write(package / "last_active_lootfilter.lua");
    Write(package / "sounds/lootfilter_config.lua"); Write(package / ".hidden.lua");
    Write(user / fs::u8path(unicode + ".lua"));
    const auto dirs = LootFilterLibrary::Directories(game, mod);
    std::atomic<uint64_t> now{100}; Catalog catalog({{}, [&] { return now.load(); }});
    catalog.Configure(dirs);
    const auto initial = Ready(catalog);
    const Names expected{"Duplicate", unicode, "Package", "UPPER", "override_rules", "lootfilter_config_blank"};
    Check(*initial.names == expected, "Production discovery preserves sort, precedence, exclusions and special tail");
    for (const auto& name : *initial.names)
        Check(!LootFilterLibrary::Resolve(dirs, name).empty(), "Every displayed UTF-8 name resolves to an installed source");
    Check(LootFilterLibrary::Resolve(dirs, "Duplicate") == user / "Duplicate.lua", "User source wins over packaged duplicate");
    Check(LootFilterLibrary::Resolve(dirs, unicode) == user / fs::u8path(unicode + ".lua"), "Unicode file name round trips");
    Check(LootFilterLibrary::Utf8(fs::u8path(unicode)) == unicode, "Unicode helper round trips");
    Write(user / "Fresh_config.lua"); catalog.Invalidate(); auto fresh = Ready(catalog);
    Check(std::find(fresh.names->begin(), fresh.names->end(), "Fresh_config") != fresh.names->end(), "Imported source appears after invalidation");
    fs::remove(user / "Fresh_config.lua"); now = 2100;
    Check(*Ready(catalog).names == expected, "TTL detects external removals");
    Check(initial.names->size() == 6, "Old filesystem snapshot remains valid");
    std::error_code error;
    Check(LootFilterLibrary::Names({root / "absent"}, &error).empty() && !error, "Missing optional roots are normal");
    // A directory handle with no sharing simulates temporary source contention.
    struct Lock {
        HANDLE handle;
        ~Lock() { if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle); }
    } locked{CreateFileW(user.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr)};
    Check(locked.handle != INVALID_HANDLE_VALUE, "Private directory contention fixture opened");
    LootFilterLibrary::Names(dirs, &error);
    Check(bool(error), "Discovery reports real root errors");
    const auto beforeFailure = catalog.Get().names;
    catalog.Invalidate(); auto invalid = Ready(catalog);
    Check(!invalid.error.empty() && invalid.names == beforeFailure, "Production scan preserves list under real directory contention");
    CloseHandle(locked.handle); locked.handle = INVALID_HANDLE_VALUE;
    now = 4100;
    Check(Ready(catalog).error.empty(), "Production scan recovers after directory contention ends");
}
void Benchmark(const fs::path& root) {
    const auto folder = root / "benchmark";
    for (int i = 0; i < 120; ++i) Write(folder / ("Filter" + std::to_string(i) + ".lua"));
    Catalog catalog({{}, [] { return uint64_t{100}; }}); catalog.Configure({folder}); const auto view = Ready(catalog);
    std::vector<double> oldSamples, cachedSamples;
    for (int trial = 0; trial < 7; ++trial) {
        auto start = std::chrono::steady_clock::now();
        bool complete = true;
        for (int i = 0; i < 100; ++i) { auto names = LootFilterLibrary::Names({folder}); std::sort(names.begin(), names.end()); complete &= names.size() == 120; }
        oldSamples.push_back(std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count() / 100);
        start = std::chrono::steady_clock::now();
        bool same = true;
        for (int i = 0; i < 10000; ++i) same &= catalog.Get().names == view.names;
        cachedSamples.push_back(std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count() / 10000);
        Check(same, "Benchmark cache reuses snapshot");
        Check(complete, "Benchmark discovery names");
    }
    std::sort(oldSamples.begin(), oldSamples.end()); std::sort(cachedSamples.begin(), cachedSamples.end());
    std::cout << "120-filter fixture median per lookup: directory discovery+sort " << oldSamples[3]
              << " us; warm cached lookup " << cachedSamples[3] << " us.\n";
}
int main(int argc, char** argv) {
    try {
        Check(argc == 2, "Expected private output root");
        const auto root = fs::path(argv[1]) / ("catalog-" + std::to_string(GetTickCount64()));
        Lifecycle(); LatePoll(); RealFiles(root); Benchmark(root);
        std::cout << checks << " catalog checks passed.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
