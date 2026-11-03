#include "ItemSpriteCache.h"
#include <Windows.h>
#include <utility>

namespace LegacyTransmog::Skin {
namespace {
constexpr uint64_t RetryMs = 2000;
size_t Bytes(const std::shared_ptr<const Sprite>& sprite) {
    return sprite ? sprite->atlas.pixels.capacity() : 0;
}
}
ItemCache::ItemCache(ItemOperations operations) : operations_(std::move(operations)), pending_(operations_.launch) {
    if (!operations_.clock) operations_.clock = [] { return uint64_t(GetTickCount64()); };
}
void ItemCache::Remember(std::string code, std::shared_ptr<const Sprite> sprite, uint64_t completed) {
    if (sprite && (sprite->atlas.pixels.empty() || Bytes(sprite) > MaxPixelBytes)) sprite.reset();
    const auto bytes = Bytes(sprite);
    // Publish the node before evicting so allocation failure preserves old art.
    entries_.push_front({std::move(code), std::move(sprite), completed});
    pixelBytes_ += bytes;
    while (entries_.size() > MaxEntries || pixelBytes_ > MaxPixelBytes) {
        pixelBytes_ -= Bytes(entries_.back().sprite);
        entries_.pop_back();
    }
}
std::shared_ptr<const Sprite> ItemCache::Get(std::shared_ptr<const ItemSource> source, const std::string& code) {
    if (source != source_) {
        source_ = std::move(source); ++generation_;
        entries_.clear(); pixelBytes_ = 0;
    }
    if (auto completion = pending_.Poll(); completion && completion->ticket == generation_) {
        if (completion->value) {
            auto& result = *completion->value;
            Remember(std::move(result.code), std::move(result.sprite), result.completed);
        } else Remember(pendingCode_, {}, operations_.clock());
    }
    if (!source_ || source_->install.empty() || code.empty()) return {};
    const auto now = operations_.clock();
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
        if (it->code != code) continue;
        if (it->sprite || (now >= it->completed && now - it->completed < RetryMs)) {
            entries_.splice(entries_.begin(), entries_, it);
            return entries_.front().sprite;
        }
        entries_.erase(it); // Negative entries have no pixel storage.
        break;
    }
    if (pending_.Busy()) return {};
    const auto found = source_->mappings.find(code);
    const auto paths = found == source_->mappings.end() ? std::vector<std::string>{} : IconPaths(found->second);
    if (paths.empty()) { Remember(code, {}, now); return {}; }
    pendingCode_ = code;
    try {
        const bool started = pending_.Start(generation_, [source = source_, paths, code,
            load = operations_.load, clock = operations_.clock] {
            ItemResult result; result.code = code;
            try { result.sprite = load(*source, paths); } catch (...) {}
            result.completed = clock();
            return result;
        });
        if (!started) Remember(code, {}, operations_.clock());
    } catch (...) { Remember(code, {}, operations_.clock()); }
    return {};
}
}
