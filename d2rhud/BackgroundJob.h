#pragma once
#include "RuntimeTelemetry.h"
#include <chrono>
#include <cstdint>
#include <functional>
#include <future>
#include <optional>
#include <utility>

namespace HudAsync {
// One owner, one in-flight operation. Workers own their inputs; the ticket lets
// callers reject stale completion even when the worker throws without a result.
// Runtime owners must outlive their workers (HudLifetime::State); destroying an
// active std::async future can wait and must never happen under the loader lock.
template<class T> class Job {
public:
    using Task = std::function<T()>;
    using Launch = std::function<std::future<T>(Task)>;
    struct Completion { uint64_t ticket; std::optional<T> value; };
    explicit Job(Launch launch = {}) : launch_(std::move(launch)) {
        if (!launch_) launch_ = [](Task task) { return std::async(std::launch::async, std::move(task)); };
    }
    Job(const Job&) = delete;
    Job& operator=(const Job&) = delete;
    bool Busy() const { return future_.valid(); }
    bool Start(uint64_t ticket, Task task) {
        if (Busy()) return false;
        ticket_ = ticket;
        try {
            future_ = launch_([task=std::move(task)]() mutable {
                HudTelemetry::Scope measured(HudTelemetry::Feature::BackgroundWork);return task();
            });
            if (!future_.valid()) return false;
            // A deferred future would run asset I/O inside Poll on the caller.
            if (future_.wait_for(std::chrono::seconds(0)) == std::future_status::deferred) {
                future_ = {}; return false;
            }
            return true;
        } catch (...) { return false; }
    }
    std::optional<Completion> Poll() {
        if (!Busy()) return {};
        try {
            if (future_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return {};
            return Completion{ticket_, future_.get()};
        } catch (...) { return Completion{ticket_, {}}; }
    }
private:
    Launch launch_;
    std::future<T> future_;
    uint64_t ticket_ = 0;
};
}
