#pragma once
#include "../../BackgroundJob.h"
#include <cstdint>
#include <filesystem>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <vector>

namespace HudFilterCatalog {
using Names = std::vector<std::string>;
using Directories = std::vector<std::filesystem::path>;
struct Operations {
    std::function<Names(const Directories&)> scan;
    // Called by both owner and worker; injected clocks must be thread-safe.
    std::function<uint64_t()> clock;
};
struct View {
    std::shared_ptr<const Names> names;
    bool loading = false;
    std::string error;
};
// One render-thread owner. Workers receive copies and never touch UI/game state.
// Get never waits for directory I/O; callers may retain an immutable snapshot.
class Catalog {
public:
    explicit Catalog(Operations operations = {});
    void Configure(Directories directories);
    void Invalidate();
    View Get();
private:
    struct Result {
        uint64_t completed = 0;
        std::shared_ptr<const Names> names;
        std::string error;
    };
    Operations operations_;
    Directories directories_;
    std::shared_ptr<const Names> names_ = std::make_shared<const Names>();
    HudAsync::Job<Result> pending_;
    uint64_t generation_ = 0, completed_ = 0;
    bool configured_ = false, dirty_ = true, attempted_ = false;
    std::string error_;
};
Catalog& Runtime();
}
