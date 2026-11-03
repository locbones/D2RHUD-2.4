#pragma once
#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace HudLocale {
template<class Catalog> struct CatalogHash {
    size_t operator()(const Catalog& catalog) const {
        size_t result = catalog.rejected;
        const auto combine = [&](size_t value) { result ^= value + size_t{0x9e3779b9} + (result << 6) + (result >> 2); };
        for (const auto& [key, value] : catalog.strings) {
            combine(std::hash<std::string>{}(key));
            combine(std::hash<std::string>{}(value));
        }
        return result;
    }
};
// Catalogs contain a strings map and rejected-entry count. Published copies are
// immutable and remain alive until the store dies: returned text pointers may
// outlive any number of reloads. The runtime store lives for the whole process.
template<class Catalog, class Hash = CatalogHash<Catalog>> class CatalogStore {
public:
    const Catalog* Read() const noexcept { return active_.load(std::memory_order_acquire); }
    bool Publish(const std::shared_ptr<const Catalog>& next) {
        std::lock_guard lock(mutex_);
        if (!next) {
            const bool changed = Read() != nullptr;
            active_.store(nullptr, std::memory_order_release);
            return changed;
        }
        const auto hash = Hash{}(*next);
        const auto [bucketBegin, bucketEnd] = index_.equal_range(hash);
        for (auto entry = bucketBegin; entry != bucketEnd; ++entry) {
            const auto existing = entry->second;
            // Fingerprints narrow the search; collisions never decide equality.
            if (existing->rejected == next->rejected && existing->strings == next->strings) {
                if (Read() == existing) return false;
                active_.store(existing, std::memory_order_release);
                return true;
            }
        }
        auto snapshot = std::make_unique<const Catalog>(*next);
        const auto pointer = snapshot.get();
        retained_.push_back(std::move(snapshot));
        try { index_.emplace(hash, pointer); }
        catch (...) { retained_.pop_back(); throw; }
        active_.store(pointer, std::memory_order_release);
        return true;
    }
    size_t RetainedCatalogs() const {
        std::lock_guard lock(mutex_);
        return retained_.size();
    }
private:
    mutable std::mutex mutex_;
    std::atomic<const Catalog*> active_{nullptr};
    std::vector<std::unique_ptr<const Catalog>> retained_;
    std::unordered_multimap<size_t, const Catalog*> index_;
};
}
