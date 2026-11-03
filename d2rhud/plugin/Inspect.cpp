#include "../ProcessLifetime.h"
#include "InspectNative.h"
#include <Windows.h>
#include <atomic>
#include <mutex>

namespace Inspect {
namespace {
struct State {
    std::mutex mutex;
    View view;
    Data::Assembly assembly;
    uint64_t session = 0, started = 0, lastRequest = 0;
    uint32_t recipient = 0, seed = 0, epoch = 0, serial = 0, request = 0, type = 0, target = 0;
    bool ready = false, pending = false, waiting = false;
};
State& Client() { return HudLifetime::State<State>(); }
std::atomic<bool> enabled{true};
std::atomic<uint64_t> appearance{96ULL | (22ULL << 16)};
void Reset(State& s) {
    s.view = {};
    s.assembly.Reset();
    s.recipient = s.seed = s.epoch = s.request = 0;
    s.ready = s.pending = s.waiting = false;
    s.type = s.target = 0;
    s.started = s.lastRequest = 0;
}
void Begin(State& s, uint32_t type, uint32_t target) {
    const auto previous = s.type == type && s.target == target ? s.view.snapshot : nullptr;
    const auto revision = previous ? s.view.revision : 0;
    s.view = {};
    s.view.snapshot = previous;
    s.view.revision = revision;
    s.view.open = true;
    s.type = type;
    s.target = target;
    s.waiting = false;
    s.assembly.Reset();
    if (!s.ready) {
        s.waiting = true;
        s.view.busy = true;
        s.started = GetTickCount64();
        s.view.status = "Inspect is waiting for a compatible HUD on the game host.";
        return;
    }
    s.pending = true;
    s.view.busy = true;
    s.view.status = "Reading equipment and inventory from the host...";
    if (!++s.serial)
        ++s.serial;
    s.request = s.serial;
    s.started = GetTickCount64();
}
} // namespace
void SetEnabled(bool value) {
    enabled.store(value);
    if (!value)
        Close();
}
bool Enabled() { return enabled.load(); }
void SetAppearance(Appearance value) {
    const auto lift = uint64_t(std::clamp(value.lift, 0, 240));
    const auto size = uint64_t(std::clamp(value.textSize, 16, 40));
    appearance.store(lift | (size << 16) | (uint64_t(value.labels) << 32));
}
Appearance GetAppearance() {
    const auto value = appearance.load();
    return {bool(value >> 32), int(value & 0xffff), int((value >> 16) & 0xffff)};
}
bool IsOpen() {
    auto& s = Client();
    std::lock_guard lock(s.mutex);
    return s.view.open;
}
void Close() {
    auto& s = Client();
    std::lock_guard lock(s.mutex);
    s.view = {};
    s.pending = s.waiting = false;
    s.request = 0;
    s.type = s.target = 0;
    s.assembly.Reset();
}
View Current() {
    auto& s = Client();
    std::lock_guard lock(s.mutex);
    return s.view;
}
bool Open(uint32_t type, uint32_t id) {
    // Native reads take the inventory lifetime guard before our client mutex.
    if (!Enabled() || !Native::CanInspect(type, id))
        return false;
    auto& s = Client();
    std::lock_guard lock(s.mutex);
    if (!s.session || !Enabled())
        return false;
    Begin(s, type, id);
    return true;
}
void Refresh() {
    if (!Enabled() || !Native::InTown()) {
        Close();
        return;
    }
    auto& s = Client();
    std::lock_guard lock(s.mutex);
    if (s.view.open && s.ready)
        Begin(s, s.type, s.target);
}
void ClientTick(const HudFrame::Context& frame, uint64_t session) {
    auto& s = Client();
    // Do not hold the client mutex while entering the native read guard.
    const bool town = Enabled() && frame.inGame && !frame.loading && session && Native::InTown();
    std::optional<Data::CommandPacket> command;
    {
        std::lock_guard lock(s.mutex);
        if (s.session != session || !frame.inGame || frame.loading || !session) {
            Reset(s);
            s.session = session;
            return;
        }
        if (!town) {
            s.view = {};
            s.pending = s.waiting = false;
            s.type = s.target = s.request = 0;
            s.assembly.Reset();
            return;
        }
        if (s.view.busy && GetTickCount64() - s.started > 15000) {
            s.view.busy = false;
            s.request = 0;
            s.pending = false;
            s.waiting = false;
            s.assembly.Reset();
            s.view.status = "The host did not finish the inspection. Refresh to retry.";
        }
        if (s.pending && s.ready && (!s.lastRequest || GetTickCount64() - s.lastRequest >= 1000)) {
            s.pending = false;
            s.lastRequest = GetTickCount64();
            Data::CommandPacket c;
            auto& p = c.packet;
            p.opcode = 3;
            p.action = Data::Request;
            p.recipient = s.recipient;
            p.seed = s.seed;
            p.epoch = s.epoch;
            p.request = s.request;
            p.type = s.type;
            p.target = s.target;
            command = c;
        }
    }
    if (command)
        Native::Send(*command);
}
void Receive(const uint8_t* bytes, size_t size) {
    if (!bytes || size != sizeof(Data::Packet))
        return;
    Data::Packet p;
    memcpy(&p, bytes, size);
    if (p.opcode != 0x2b || p.magic != Data::Magic || p.operation != Data::Operation ||
        p.version != Data::Version || p.type > 1 || p.length > Data::Payload)
        return;
    if (!std::all_of(p.bytes.begin() + p.length, p.bytes.end(), [](uint8_t b) { return b == 0; }))
        return;
    // Bind the handshake to this client's live player, not a cached unit from
    // the last game. Requests cannot be emitted before this host advertises.
    const auto local = Native::LocalId();
    if (!local || p.recipient != *local)
        return;
    auto& s = Client();
    std::lock_guard lock(s.mutex);
    if (p.action == Data::Hello) {
        if (!p.epoch || p.request || p.total || p.offset || p.length)
            return;
        if (s.ready && (s.epoch != p.epoch || s.seed != p.seed))
            Reset(s);
        s.recipient = p.recipient;
        s.seed = p.seed;
        s.epoch = p.epoch;
        s.ready = true;
        if (s.waiting && s.view.open && Enabled())
            Begin(s, s.type, s.target);
        return;
    }
    if (!Enabled() || !s.view.open || !s.view.busy || !s.ready || p.seed != s.seed || p.epoch != s.epoch ||
        p.request != s.request || p.type != s.type || p.target != s.target)
        return;
    if (p.action == Data::Failure) {
        std::string message(reinterpret_cast<const char*>(p.bytes.data()), p.length);
        if (!Data::Text(message, Data::Payload))
            return;
        s.view.status = std::move(message);
        s.view.busy = false;
        s.request = 0;
        s.assembly.Reset();
        return;
    }
    if (!s.assembly.Add(p)) {
        s.view.status = "The inspection response was incomplete. Refresh to retry.";
        s.view.busy = false;
        s.request = 0;
        s.assembly.Reset();
        return;
    }
    if (s.assembly.Complete()) {
        const auto snapshot = Data::Decode(s.assembly.bytes);
        if (snapshot && snapshot->type == s.type && snapshot->id == s.target) {
            s.view.snapshot = std::make_shared<const Data::Snapshot>(*snapshot);
            s.view.revision = uint64_t(s.epoch) << 32 | s.request;
            s.view.status = "Read-only equipment and backpack snapshot. Refresh to update.";
        } else
            s.view.status = "The host returned an unsupported inspection. Refresh to retry.";
        s.view.busy = false;
        s.request = 0;
        s.assembly.Reset();
    }
}
} // namespace Inspect
