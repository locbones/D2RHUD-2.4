#pragma once
#include "StackSplitSkin.h"
#include "../BackgroundJob.h"
#include <filesystem>
#include <functional>
#include <future>
#include <list>
#include <map>

namespace LegacyTransmog::Skin {
struct ItemSource {
    std::filesystem::path install;
    std::string mod;
    std::map<std::string, std::string> mappings;
};
struct ItemResult {
    std::shared_ptr<const Sprite> sprite;
    std::string code;
    uint64_t completed = 0;
};
using ItemTask = std::function<ItemResult()>;
struct ItemOperations {
    std::function<std::shared_ptr<const Sprite>(const ItemSource&, const std::vector<std::string>&)> load;
    std::function<uint64_t()> clock;
    std::function<std::future<ItemResult>(ItemTask)> launch;
};
// Render-thread owned. Sources must be immutable snapshots. One background
// reader owns its captured inputs; stale generations cannot publish new art.
class ItemCache {
public:
    static constexpr size_t MaxEntries = 8, MaxPixelBytes = 32 * 1024 * 1024;
    explicit ItemCache(ItemOperations operations);
    std::shared_ptr<const Sprite> Get(std::shared_ptr<const ItemSource> source, const std::string& code);
    size_t Entries() const { return entries_.size(); }
    size_t PixelBytes() const { return pixelBytes_; }
private:
    struct Entry { std::string code; std::shared_ptr<const Sprite> sprite; uint64_t completed; };
    void Remember(std::string code, std::shared_ptr<const Sprite> sprite, uint64_t completed);
    ItemOperations operations_;
    std::shared_ptr<const ItemSource> source_;
    std::list<Entry> entries_;
    size_t pixelBytes_ = 0;
    uint64_t generation_ = 0;
    std::string pendingCode_;
    HudAsync::Job<ItemResult> pending_;
};
}
