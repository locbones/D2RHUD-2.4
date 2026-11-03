#pragma once
#include "FrameContext.h"
#include <cstdint>
#include <cstddef>
#include <atomic>
#include <functional>
#include <future>
#include <memory>

namespace HudCamera {
struct State {
    bool programEnabled = true, available = false, loading = false;
    bool valuesFound = false, zoomFound = false;
    bool didScanAngles = false, didScanZoom = false;
    float pitch = 0, height = 0, pan = 0, roll = 0, zoom = 0;
    float defaultPitch = 0, defaultHeight = 0, defaultPan = 0, defaultRoll = 0, defaultZoom = 0;
};
struct Operations {
    std::function<uintptr_t(const char*)> scan;
    std::function<bool(uintptr_t, void*, size_t)> read;
    std::function<bool(uintptr_t, const void*, size_t)> write;
};
// Render-thread owner. The worker receives only an immutable scan function and
// returns addresses; it never reads UI state or writes native camera memory.
class Controller {
public:
    explicit Controller(Operations operations = {});
    Controller(const Controller&) = delete;
    Controller& operator=(const Controller&) = delete;
    void Tick(uint64_t session, bool inGame, bool loading);
    void Request(bool retry = false);
    bool ApplyAngles();
    bool ApplyZoom();
    State& View() { return state_; }
    const State& View() const { return state_; }
    bool Busy() const { return pending_.valid(); }
    bool NeedsTick() const { return Busy() || angles_ || zoom_; }
    uint64_t Generation() const { return generation_; }
    uint64_t SessionRevision() const { return sessionRevision_; }
    uint64_t CaptureRevision() const { return captureRevision_; }
private:
    struct Result { uint64_t generation; uintptr_t angles = 0, zoom = 0; };
    void Invalidate();
    void Publish(const Result& result);
    void Revalidate();
    State state_;
    Operations operations_;
    std::future<Result> pending_;
    std::shared_ptr<std::atomic<uint64_t>> epoch_ = std::make_shared<std::atomic<uint64_t>>(0);
    uint64_t session_ = 0, generation_ = 0;
    uint64_t sessionRevision_ = 0, captureRevision_ = 0;
    uintptr_t angles_ = 0, zoom_ = 0;
    bool inGame_ = false, loading_ = false, attempted_ = false;
};
Controller& Runtime();
void Tick(bool visible = true);
void Tick(const HudFrame::Context& context, bool visible = true);
// Refresh the native game/loading boundary immediately before each write.
bool ApplyAngles();
bool ApplyZoom();
}
