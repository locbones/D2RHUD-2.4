#include "../../RuntimeTelemetry.h"
#include "CameraController.h"
#include "../../SessionEpoch.h"
#include "../../Pattern.h"
#include "../../ProcessLifetime.h"
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <utility>

namespace HudCamera {
namespace {
constexpr const char* Angles = "44 74 64 BF 3F DB 74 3E F4 41 BD 3E";
constexpr const char* Zoom = "00 00 06 00 00 00 ? ? ? ? ? ? ? 43 ? ? ? ? ? ? ? ? 00 00 00 00 01 00 00 00 00 00 00 00 00 00 80 3F";
bool Read(uintptr_t source, void* output, size_t size) {
    const auto saved = GetLastError();
    SIZE_T received = 0;
    const bool result = source && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(source), output, size, &received) && received == size;
    SetLastError(saved);
    return result;
}
bool Write(uintptr_t destination, const void* source, size_t size) {
    if (!destination || !size || destination > (std::numeric_limits<uintptr_t>::max)() - size) return false;
    const auto saved = GetLastError();
    const auto writable = [&] {
        const auto end = destination + size;
        for (auto cursor = destination; cursor < end;) {
            MEMORY_BASIC_INFORMATION info{};
            if (!VirtualQuery(reinterpret_cast<void*>(cursor), &info, sizeof(info)) || info.State != MEM_COMMIT ||
                (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
            const auto access = info.Protect & 0xff;
            if (access != PAGE_READWRITE && access != PAGE_WRITECOPY && access != PAGE_EXECUTE_READWRITE && access != PAGE_EXECUTE_WRITECOPY) return false;
            const auto base = reinterpret_cast<uintptr_t>(info.BaseAddress);
            if (base > cursor || info.RegionSize > (std::numeric_limits<uintptr_t>::max)() - base || base + info.RegionSize <= cursor) return false;
            cursor = base + info.RegionSize;
        }
        return true;
    };
    SIZE_T written = 0;
    const bool result = writable() && WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(destination), source, size, &written) && written == size;
    SetLastError(saved);
    return result;
}
bool Finite(const std::array<float, 4>& values) {
    for (const auto value : values) if (!std::isfinite(value)) return false;
    return true;
}
}

Controller::Controller(Operations operations) : operations_(std::move(operations)) {
    if (!operations_.scan) operations_.scan = [](const char* pattern) { return uintptr_t(Pattern::ScanProcess(pattern)); };
    if (!operations_.read) operations_.read = Read;
    if (!operations_.write) operations_.write = Write;
}
void Controller::Invalidate() {
    ++generation_;
    ++sessionRevision_;
    epoch_->store(generation_, std::memory_order_release);
    const bool enabled = state_.programEnabled;
    state_ = {}; state_.programEnabled = enabled;
    angles_ = zoom_ = 0; attempted_ = false;
}
void Controller::Tick(uint64_t session, bool inGame, bool loading) {
    if (session != session_ || inGame != inGame_) Invalidate();
    else if (loading != loading_) {
        ++generation_;
        epoch_->store(generation_, std::memory_order_release);
        attempted_ = false;
        // Edited angles no longer match the default float signature. Quarantine
        // existing bindings during loading, then check them before reuse rather
        // than discarding a valid customized camera on every act change.
        if (!loading) Revalidate();
    }
    session_ = session; inGame_ = inGame; loading_ = loading;
    state_.available = inGame && !loading;
    state_.loading = loading;
    if (pending_.valid() && pending_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try {
            const auto result = pending_.get();
            if (state_.available && result.generation == generation_) Publish(result);
        } catch (...) {
            // A failed scan must not escape future::get into the render loop.
            // A later page reopen or game/loading boundary permits another try.
        }
    }
}
void Controller::Request(bool retry) {
    if (!state_.available || Busy() || (state_.valuesFound && state_.zoomFound)) return;
    if (retry) attempted_ = false;
    if (attempted_) return;
    attempted_ = true;
    const bool angles = !state_.valuesFound, zoom = !state_.zoomFound;
    state_.didScanAngles |= angles; state_.didScanZoom |= zoom;
    const auto generation = generation_;
    try {
        pending_ = std::async(std::launch::async, [scan = operations_.scan, epoch = epoch_, generation, angles, zoom] {
            Result result{generation};
            if (epoch->load(std::memory_order_acquire) != generation) return result;
            if (angles) {
                try { result.angles = scan(Angles); } catch (...) {}
            }
            if (zoom && epoch->load(std::memory_order_acquire) == generation) {
                try {
                    const auto base = scan(Zoom);
                    if (base && base <= (std::numeric_limits<uintptr_t>::max)() - 0x32) result.zoom = base + 0x32;
                } catch (...) {}
            }
            return result;
        });
    } catch (...) { /* Thread creation failure is retried on a later page reopen. */ }
}
void Controller::Publish(const Result& result) {
    bool captured = false;
    std::array<float, 4> values{}; // Native contiguous order: roll, pitch, height, pan.
    if (result.angles >= sizeof(float) && operations_.read(result.angles - sizeof(float), values.data(), sizeof(values)) && Finite(values)) {
        angles_ = result.angles;
        state_.roll = state_.defaultRoll = values[0];
        state_.pitch = state_.defaultPitch = values[1];
        state_.height = state_.defaultHeight = values[2];
        state_.pan = state_.defaultPan = values[3];
        state_.valuesFound = true;
        captured = true;
    }
    float value = 0;
    if (result.zoom && operations_.read(result.zoom, &value, sizeof(value)) && std::isfinite(value)) {
        zoom_ = result.zoom;
        state_.zoom = state_.defaultZoom = value;
        state_.zoomFound = true;
        captured = true;
    }
    if (captured) ++captureRevision_;
}
void Controller::Revalidate() {
    std::array<float, 4> values{};
    if (angles_ && (!operations_.read(angles_ - sizeof(float), values.data(), sizeof(values)) || !Finite(values))) {
        angles_ = 0; state_.valuesFound = false;
    }
    float value = 0;
    if (zoom_ && (!operations_.read(zoom_, &value, sizeof(value)) || !std::isfinite(value))) {
        zoom_ = 0; state_.zoomFound = false;
    }
}
bool Controller::ApplyAngles() {
    if (!state_.available || !state_.programEnabled || !state_.valuesFound || angles_ < sizeof(float)) return false;
    const std::array<float, 4> values{state_.roll, state_.pitch, state_.height, state_.pan};
    if (!Finite(values)) return false;
    std::array<float, 4> current{};
    if (operations_.read(angles_ - sizeof(float), current.data(), sizeof(current)) && Finite(current) &&
        operations_.write(angles_ - sizeof(float), values.data(), sizeof(values))) return true;
    angles_ = 0; state_.valuesFound = false;
    return false;
}
bool Controller::ApplyZoom() {
    if (!state_.available || !state_.programEnabled || !state_.zoomFound || !zoom_ || !std::isfinite(state_.zoom)) return false;
    float current = 0;
    if (operations_.read(zoom_, &current, sizeof(current)) && std::isfinite(current) &&
        operations_.write(zoom_, &state_.zoom, sizeof(state_.zoom))) return true;
    zoom_ = 0; state_.zoomFound = false;
    return false;
}
Controller& Runtime() { return HudLifetime::State<Controller>(); }
void Tick(bool visible) {
    if (!visible && !Runtime().NeedsTick()) return;
    Tick(HudFrame::Capture(),visible);
}
void Tick(const HudFrame::Context& context,bool visible) {
    HudTelemetry::Scope measured(HudTelemetry::Feature::Camera);
    if (!visible && !Runtime().NeedsTick()) return;
    const auto epoch = HudSession::Observe(context.inGame, context.nativeToken, context.loading, context.character);
    Runtime().Tick(epoch,context.inGame,context.loading);
}
bool ApplyAngles() { Tick(); return Runtime().ApplyAngles(); }
bool ApplyZoom() { Tick(); return Runtime().ApplyZoom(); }
}
