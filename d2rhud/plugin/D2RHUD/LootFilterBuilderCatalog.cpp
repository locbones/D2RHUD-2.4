#include "LootFilterBuilderCatalog.h"
#include <utility>

namespace HudBuilderCatalog {
namespace {
constexpr uint64_t RetryMs = 1000;
constexpr auto Failure = "Item lists could not be loaded. Retrying...";
}
Loader::Loader(Operations operations) : operations_(std::move(operations)), pending_(operations_.launch) {
    if (!operations_.clock) operations_.clock = [] { return uint64_t(GetTickCount64()); };
}
void Loader::Invalidate() { ++generation_; dirty_ = true; ready_ = false; }
void Loader::Failed() {
    completed_ = operations_.clock();
    attempted_ = true;
    ready_ = false;
    catalog_.status = Failure;
}
const Catalog& Loader::Get(const std::filesystem::path& install, const std::string& mod) {
    if (!configured_ || install != install_ || mod != mod_) {
        install_ = install; mod_ = mod; configured_ = true;
        catalog_ = {};
        catalog_.status = "Loading item, category, area and stat lists...";
        Invalidate();
    }
    if (auto completion = pending_.Poll(); completion && completion->ticket == generation_) {
        if (completion->value) {
            auto& result = *completion->value;
            completed_ = result.completed; attempted_ = true;
            ready_ = result.success;
            if (result.success) catalog_ = std::move(result.catalog);
            else catalog_.status = Failure;
        } else Failed();
    }
    const auto now = operations_.clock();
    const bool retry = !ready_ && (!attempted_ || now < completed_ || now - completed_ >= RetryMs);
    if (!pending_.Busy() && (dirty_ || retry)) {
        dirty_ = false;
        if (!catalog_.loaded) catalog_.status = "Loading item, category, area and stat lists...";
        try {
            // Capture values, never this: old work can finish after a mod change
            // but cannot publish into that mod's catalog or touch UI/game state.
            const bool started = pending_.Start(generation_, [install = install_, mod = mod_,
                load = operations_.load, clock = operations_.clock] {
                Result result;
                try { result.catalog = load(install, mod); result.success = result.catalog.loaded; }
                catch (...) {}
                result.completed = clock();
                return result;
            });
            if (!started) Failed();
        } catch (...) { Failed(); }
    }
    return catalog_;
}
}
