#include "LootFilterCatalog.h"
#include "../ItemFilter/LootFilterLibrary.h"
#include "../../ProcessLifetime.h"
#include <Windows.h>
#include <algorithm>
#include <stdexcept>

namespace HudFilterCatalog {
namespace {
constexpr uint64_t RefreshMs = 2000;
Names Discover(const Directories& directories)
{
    std::error_code error;
    auto names = LootFilterLibrary::Names(directories, &error);
    if (error) throw std::filesystem::filesystem_error("Reading installed filters", error);
    Names ordered;
    bool overrides = false, blank = false;
    for (auto& name : names) {
        if (name == "override_rules") overrides = true;
        else if (name == "lootfilter_config_blank") blank = true;
        else ordered.push_back(std::move(name));
    }
    std::sort(ordered.begin(), ordered.end());
    if (overrides) ordered.emplace_back("override_rules");
    if (blank) ordered.emplace_back("lootfilter_config_blank");
    return ordered;
}
}
Catalog::Catalog(Operations operations) : operations_(std::move(operations))
{
    if (!operations_.scan) operations_.scan = Discover;
    if (!operations_.clock) operations_.clock = [] { return GetTickCount64(); };
}
void Catalog::Configure(Directories directories)
{
    if (configured_ && directories_ == directories) return;
    directories_ = std::move(directories);
    configured_ = true;
    names_ = std::make_shared<const Names>();
    error_.clear();
    Invalidate();
}
void Catalog::Invalidate()
{
    ++generation_;
    dirty_ = true;
}
View Catalog::Get()
{
    if (auto completion = pending_.Poll(); completion && completion->ticket == generation_) {
        if (completion->value) {
            auto& result = *completion->value;
            completed_ = result.completed;
            attempted_ = true;
            error_ = std::move(result.error);
            if (result.names) names_ = std::move(result.names);
        } else {
            completed_ = operations_.clock(); attempted_ = true;
            error_ = "Could not refresh installed filters. Retrying...";
        }
    }
    const auto now = operations_.clock();
    if (configured_ && !pending_.Busy() &&
        (dirty_ || !attempted_ || now < completed_ || now - completed_ >= RefreshMs)) {
        dirty_ = false;
        try {
            const bool started = pending_.Start(generation_,
                [directories = directories_, operations = operations_] {
                    Result result;
                    try { result.names = std::make_shared<const Names>(operations.scan(directories)); }
                    catch (const std::exception&) { result.error = "Could not refresh installed filters. Retrying..."; }
                    catch (...) { result.error = "Could not refresh installed filters. Retrying..."; }
                    result.completed = operations.clock();
                    return result;
                });
            if (!started) {
                error_ = "Could not start filter discovery. Retrying...";
                completed_ = operations_.clock(); attempted_ = true;
            }
        }
        catch (...) {
            error_ = "Could not start filter discovery. Retrying...";
            completed_ = operations_.clock();
            attempted_ = true;
        }
    }
    return { names_, pending_.Busy(), error_ };
}
Catalog& Runtime() { return HudLifetime::State<Catalog>(); }
}
