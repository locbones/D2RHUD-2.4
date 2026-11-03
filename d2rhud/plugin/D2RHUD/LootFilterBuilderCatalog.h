#pragma once
#include "../ItemFilter/LootFilterRuleBuilder.h"
#include "../../BackgroundJob.h"
#include <filesystem>
#include <functional>
#include <future>

namespace HudBuilderCatalog {
using Catalog = LootFilterConfig::Builder::Catalog;
struct Result {
    Catalog catalog;
    uint64_t completed = 0;
    bool success = false;
};
using Task = std::function<Result()>;
struct Operations {
    std::function<Catalog(const std::filesystem::path&, const std::string&)> load;
    std::function<uint64_t()> clock;
    std::function<std::future<Result>(Task)> launch;
};
// Called by the render thread only. One owned worker reads assets into its own
// catalog; Get never waits for it. Production retains the loader until exit.
class Loader {
public:
    explicit Loader(Operations operations);
    const Catalog& Get(const std::filesystem::path& install, const std::string& mod);
    void Invalidate();
private:
    void Failed();
    Operations operations_;
    Catalog catalog_;
    std::filesystem::path install_;
    std::string mod_;
    HudAsync::Job<Result> pending_;
    uint64_t generation_ = 0, completed_ = 0;
    bool configured_ = false, attempted_ = false, dirty_ = true, ready_ = false;
};
}
