#pragma once
#include "BackgroundJob.h"
#include <memory>
#include <string>
#include <string_view>

namespace HudAssets {
// Render-thread owner. Inactive features perform no launch/read work; work
// requested earlier may finish and publish. Runtime owners are process-resident.
template<class T> class Deferred {
public:
    struct Result { std::shared_ptr<const T> value; uint64_t completed = 0; };
    struct Operations {
        std::function<T(const std::string&)> load;
        std::function<uint64_t()> clock;
        typename HudAsync::Job<Result>::Launch launch;
    };
    explicit Deferred(Operations operations) : operations_(std::move(operations)), job_(operations_.launch) {}
    void Invalidate() { ++generation_; value_.reset(); ++revision_; attempted_=failed_=false; }
    void Tick(std::string_view source, bool wanted) {
        if (source != source_) {
            source_ = source; Invalidate();
        }
        if (auto done = job_.Poll(); done && done->ticket == generation_) {
            attempted_ = true;
            if (done->value && done->value->value) {
                value_ = std::move(done->value->value); completed_ = done->value->completed;
                failed_ = false; ++revision_;
            } else { completed_ = operations_.clock(); failed_ = true; }
        }
        if (!wanted || value_ || job_.Busy()) return;
        const auto now = operations_.clock();
        if (attempted_ && now >= completed_ && now - completed_ < 1000) return;
        try {
            if (job_.Start(generation_, [source = source_, load = operations_.load, clock = operations_.clock] {
                Result result;
                result.value = std::make_shared<const T>(load(source));
                result.completed = clock();
                return result;
            })) { failed_ = false; return; }
        } catch (...) {}
        completed_ = operations_.clock(); attempted_ = failed_ = true;
    }
    const std::shared_ptr<const T>& Value() const { return value_; }
    uint64_t Revision() const { return revision_; }
    bool Busy() const { return job_.Busy(); }
    bool Failed() const { return failed_; }
private:
    Operations operations_;
    HudAsync::Job<Result> job_;
    std::string source_;
    std::shared_ptr<const T> value_;
    uint64_t generation_ = 0, revision_ = 0, completed_ = 0;
    bool attempted_ = false, failed_ = false;
};
}
