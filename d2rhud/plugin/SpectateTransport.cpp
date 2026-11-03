#define WIN32_LEAN_AND_MEAN
// Winsock must precede IP Helper. Keep these dependent SDK headers ordered.
// clang-format off
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
// clang-format on
#include "SpectateTransport.h"
#include "../ProcessLifetime.h"
#include "../RuntimeTelemetry.h"
#include "SpectateDiagnostics.h"
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")

namespace Spectate::Stream {
namespace {
constexpr uint64_t Magic = 0x3444495644554844ULL;
constexpr uint16_t Version = 2;
enum Kind : uint16_t {
    Auth = 1,
    Options,
    Subscription,
    Capture,
    Video,
    Heartbeat,
    Authenticated,
    WatchStatus,
    CaptureError
};
#pragma pack(push, 1)
struct Header {
    uint64_t magic = Magic;
    uint16_t version = Version, kind = 0;
    uint32_t size = 0;
};
struct AuthData {
    Credential credential;
    uint32_t bandwidth = 16;
};
struct OptionsData {
    uint32_t ready = 0, sharing = 0, bandwidth = 16;
};
struct WatchData {
    uint32_t target = 0, request = 0;
    int32_t fps = 0, width = 0, quality = 0;
};
struct DemandData {
    uint64_t stream = 0;
    int32_t fps = 0, width = 0, quality = 0;
    uint32_t bitrate = 0;
};
struct FrameData {
    uint32_t target = 0, request = 0;
    uint64_t stream = 0, sequence = 0, time = 0;
    uint32_t width = 0, height = 0, key = 0;
};
struct StatusData {
    uint32_t target = 0, request = 0;
    char message[112]{};
};
struct ErrorData {
    char message[112]{};
};
#pragma pack(pop)
static_assert(sizeof(Header) == 16 && sizeof(Credential) == 44 && sizeof(FrameData) == 44);
using Bytes = std::shared_ptr<const std::vector<uint8_t>>;
uint64_t Now() { return GetTickCount64(); }
bool Winsock() {
    static const bool initialized = [] {
        WSADATA data{};
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    return initialized;
}
template <size_t N> void Text(char (&out)[N], std::string_view text) {
    const auto length = (std::min)(text.size(), N - 1);
    for (size_t n = 0; n < length; ++n)
        out[n] = text[n] >= 32 && text[n] <= 126 ? text[n] : '?';
    out[length] = 0;
}
template <size_t N> bool ValidText(const char (&text)[N]) {
    const auto end = std::find(std::begin(text), std::end(text), '\0');
    return end != std::end(text) &&
           std::all_of(std::begin(text), end, [](unsigned char c) { return c >= 32 && c <= 126; });
}
void Close(SOCKET& socket) {
    if (socket != INVALID_SOCKET)
        closesocket(socket);
    socket = INVALID_SOCKET;
}
bool Setup(SOCKET socket) {
    u_long nonblocking = 1;
    int yes = 1, buffer = 256 * 1024;
    setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<char*>(&yes), sizeof(yes));
    setsockopt(socket, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<char*>(&buffer), sizeof(buffer));
    setsockopt(socket, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<char*>(&buffer), sizeof(buffer));
    return ioctlsocket(socket, FIONBIO, &nonblocking) == 0;
}
template <class T> Bytes Message(Kind kind, const T& payload) {
    Header h;
    h.kind = kind;
    h.size = sizeof(payload);
    auto bytes = std::make_shared<std::vector<uint8_t>>(sizeof(h) + sizeof(payload));
    memcpy(bytes->data(), &h, sizeof(h));
    memcpy(bytes->data() + sizeof(h), &payload, sizeof(payload));
    return bytes;
}
Bytes Picture(const Frame& f, uint32_t target = 0, uint32_t request = 0) {
    Header h;
    h.kind = Video;
    h.size = uint32_t(sizeof(FrameData) + f.bytes.size());
    FrameData d{target, request, f.stream, f.sequence, f.time, f.width, f.height, f.key ? 1u : 0u};
    auto bytes = std::make_shared<std::vector<uint8_t>>(sizeof(h) + h.size);
    memcpy(bytes->data(), &h, sizeof(h));
    memcpy(bytes->data() + sizeof(h), &d, sizeof(d));
    memcpy(bytes->data() + sizeof(h) + sizeof(d), f.bytes.data(), f.bytes.size());
    return bytes;
}
template <class T> bool Read(std::span<const uint8_t> bytes, T& value) {
    if (bytes.size() != sizeof(T))
        return false;
    memcpy(&value, bytes.data(), sizeof(T));
    return true;
}
std::optional<Received> Picture(std::span<const uint8_t> bytes) {
    if (bytes.size() < sizeof(FrameData) || bytes.size() > sizeof(FrameData) + MaxFrameBytes)
        return {};
    FrameData d;
    memcpy(&d, bytes.data(), sizeof(d));
    if (d.key > 1)
        return {};
    auto f = std::make_shared<Frame>();
    f->stream = d.stream;
    f->sequence = d.sequence;
    f->time = d.time;
    f->width = d.width;
    f->height = d.height;
    f->key = d.key != 0;
    f->bytes.assign(bytes.begin() + sizeof(d), bytes.end());
    if (!ValidFrame(*f))
        return {};
    return Received{d.target, d.request, std::move(f)};
}
struct Socket {
    SOCKET handle = INVALID_SOCKET;
    std::vector<uint8_t> input;
    std::deque<Bytes> output;
    size_t sent = 0;
    uint64_t started = Now(), active = Now(), sending = 0, notified = 0;
    uint32_t id = 0;
    Credential credential;
    bool authenticated = false;
    bool requireAuth = false;
    std::optional<Watch> watch;
    ~Socket() { Close(handle); }
    bool Queue(Bytes bytes) {
        // At most one partially sent frame and one pending frame. Never drop a
        // predictive frame and then continue its dependent stream.
        size_t frames = 0;
        for (const auto& b : output) {
            Header h;
            memcpy(&h, b->data(), sizeof(h));
            frames += h.kind == Video;
        }
        Header h;
        memcpy(&h, bytes->data(), sizeof(h));
        if (output.size() >= 16 || (h.kind == Video && frames >= 2))
            return false;
        output.push_back(std::move(bytes));
        return true;
    }
    template <class Callback> bool Receive(Callback callback, uint64_t& count) {
        std::array<uint8_t, 65536> buffer;
        // Bound work per socket so a publisher cannot monopolize the relay.
        for (unsigned n = 0; n < 64; ++n) {
            const int got = recv(handle, reinterpret_cast<char*>(buffer.data()), int(buffer.size()), 0);
            if (!got)
                return false;
            if (got < 0)
                return WSAGetLastError() == WSAEWOULDBLOCK;
            count += got;
            active = Now();
            input.insert(input.end(), buffer.begin(), buffer.begin() + got);
            size_t used = 0;
            while (input.size() - used >= sizeof(Header)) {
                Header h;
                memcpy(&h, input.data() + used, sizeof(h));
                if (h.magic != Magic || h.version != Version || h.kind < Auth || h.kind > CaptureError ||
                    h.size > MaxFrameBytes + sizeof(FrameData) || (h.kind != Video && h.size > 128))
                    return false;
                if (requireAuth && !authenticated && (h.kind != Auth || h.size != sizeof(AuthData)))
                    return false;
                const size_t total = sizeof(h) + h.size;
                if (input.size() - used < total)
                    break;
                if (!callback(Kind(h.kind), std::span(input.data() + used + sizeof(h), h.size)))
                    return false;
                used += total;
            }
            if (used)
                input.erase(input.begin(), input.begin() + used);
            if (input.size() > MaxFrameBytes + sizeof(FrameData) + sizeof(Header))
                return false;
        }
        return true;
    }
    bool Send(uint64_t& count, size_t& credit) {
        while (!output.empty() && credit) {
            const auto& b = output.front();
            if (!sending)
                sending = Now();
            const int wrote = send(handle, reinterpret_cast<const char*>(b->data() + sent),
                                   int((std::min)({b->size() - sent, size_t(262144), credit})), 0);
            if (wrote <= 0)
                return wrote < 0 && WSAGetLastError() == WSAEWOULDBLOCK && Now() - sending < 2000;
            sent += wrote;
            credit -= wrote;
            count += wrote;
            if (sent == b->size()) {
                output.pop_front();
                sent = 0;
                sending = 0;
            }
        }
        return !sending || Now() - sending < 2000;
    }
};
void Pause() { std::this_thread::sleep_for(std::chrono::milliseconds(3)); }
} // namespace

struct Server::State {
    mutable std::mutex mutex;
    std::mutex lifecycle;
    Authority authority;
    std::string error;
    std::atomic<bool> stop{false};
    std::thread thread;
    void Run() {
        Socket listening;
        auto& listener = listening.handle;
        uint16_t port = 0;
        uint32_t epoch = 0, seed = 0;
        Relay relay;
        std::vector<std::unique_ptr<Socket>> sockets;
        uint64_t budgetAt = Now(), sent = 0, received = 0;
        size_t credit = 0;
        while (!stop) {
            Authority a;
            {
                std::lock_guard lock(mutex);
                a = authority;
            }
            const bool active =
                a.epoch && a.port && Now() - a.refreshed < 6500 &&
                std::any_of(a.members.begin(), a.members.end(), [](const Member& m) { return m.enabled; });
            if (!active || port != a.port || epoch != a.epoch || seed != a.seed) {
                sockets.clear();
                Close(listener);
                relay.Reset();
                port = a.port;
                epoch = a.epoch;
                seed = a.seed;
            }
            if (!active) {
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
                continue;
            }
            relay.Update(a);
            std::erase_if(sockets, [&](const auto& socket) {
                if (!socket->authenticated)
                    return Now() - socket->started > 2000;
                if (relay.Authorized(socket->credential))
                    return false;
                relay.Disconnected(socket->id);
                return true;
            });
            if (listener == INVALID_SOCKET) {
                listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
                sockaddr_in address{};
                address.sin_family = AF_INET;
                address.sin_port = htons(port);
                int exclusive = 1;
                setsockopt(listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<char*>(&exclusive),
                           sizeof(exclusive));
                if (listener == INVALID_SOCKET || !Setup(listener) ||
                    bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) ||
                    listen(listener, 8)) {
                    const auto code = WSAGetLastError();
                    Close(listener);
                    {
                        std::lock_guard lock(mutex);
                        error = "Spectate video port " + std::to_string(port) + " could not open (Windows " +
                                std::to_string(code) + ").";
                    }
                    for (int n = 0; n < 100 && !stop; ++n)
                        Pause();
                    continue;
                }
                {
                    std::lock_guard lock(mutex);
                    error.clear();
                }
                Diagnostics::Log("relay", "Video listener started on TCP port " + std::to_string(port));
            }
            for (unsigned n = 0; n < 16; ++n) {
                SOCKET incoming = accept(listener, nullptr, nullptr);
                if (incoming == INVALID_SOCKET)
                    break;
                if (sockets.size() >= 16 || !Setup(incoming)) {
                    Close(incoming);
                    continue;
                }
                auto s = std::make_unique<Socket>();
                s->handle = incoming;
                s->requireAuth = true;
                sockets.push_back(std::move(s));
            }
            for (auto& socket : sockets) {
                if (socket->handle == INVALID_SOCKET)
                    continue;
                const bool good = socket->Receive(
                    [&](Kind kind, std::span<const uint8_t> b) {
                        if (!socket->authenticated) {
                            AuthData auth;
                            if (kind != Auth || !Read(b, auth) ||
                                !relay.Connected(auth.credential, auth.bandwidth))
                                return false;
                            for (auto& other : sockets)
                                if (other.get() != socket.get() && other->authenticated &&
                                    other->id == auth.credential.id)
                                    Close(other->handle);
                            socket->id = auth.credential.id;
                            socket->credential = auth.credential;
                            socket->authenticated = true;
                            Diagnostics::Log("relay", "Authenticated player " + std::to_string(socket->id));
                            Header ack;
                            ack.kind = Authenticated;
                            auto bytes = std::make_shared<std::vector<uint8_t>>(sizeof(ack));
                            memcpy(bytes->data(), &ack, sizeof(ack));
                            return socket->Queue(std::move(bytes));
                        }
                        if (kind == Options) {
                            OptionsData options;
                            if (!Read(b, options) || options.ready > 1 || options.sharing > 1)
                                return false;
                            relay.Options(socket->id, options.ready != 0, options.sharing != 0,
                                          options.bandwidth);
                            return true;
                        }
                        if (kind == Subscription) {
                            WatchData watch;
                            if (!Read(b, watch))
                                return false;
                            const auto wanted =
                                watch.request
                                    ? std::optional<Watch>{{watch.target,
                                                            watch.request,
                                                            {watch.fps, watch.width, watch.quality}}}
                                    : std::nullopt;
                            if (!relay.Subscribe(socket->id, wanted))
                                return false;
                            if (socket->watch != wanted) {
                                socket->watch = wanted;
                                socket->notified = 0;
                                if (wanted)
                                    Diagnostics::Log("relay", "Player " + std::to_string(socket->id) +
                                                                  " watching " +
                                                                  std::to_string(wanted->target));
                            }
                            return true;
                        }
                        if (kind == CaptureError) {
                            ErrorData report;
                            if (!Read(b, report) || !ValidText(report.message))
                                return false;
                            relay.CaptureError(socket->id, report.message);
                            return true;
                        }
                        if (kind == Heartbeat)
                            return b.empty();
                        if (kind != Video)
                            return false;
                        auto picture = Picture(b);
                        if (!picture || picture->target || picture->request)
                            return false;
                        for (const auto recipient : relay.Publish(socket->id, *picture->frame)) {
                            const auto peer =
                                std::find_if(sockets.begin(), sockets.end(), [&](const auto& p) {
                                    return p->authenticated && p->id == recipient.id &&
                                           p->handle != INVALID_SOCKET;
                                });
                            if (peer != sockets.end() &&
                                !(*peer)->Queue(Picture(*picture->frame, socket->id, recipient.request))) {
                                relay.Congested(socket->id);
                                Close((*peer)->handle);
                            }
                        }
                        return true;
                    },
                    received);
                if (!good || Now() - socket->active > 5000)
                    Close(socket->handle);
            }
            // Remove old connections before producing capture demand. Replaced
            // sockets must not disconnect the new socket using the same ID.
            std::erase_if(sockets, [&](const auto& s) {
                if (s->handle != INVALID_SOCKET)
                    return false;
                if (s->authenticated && std::none_of(sockets.begin(), sockets.end(), [&](const auto& p) {
                        return p && p.get() != s.get() && p->handle != INVALID_SOCKET && p->authenticated &&
                               p->id == s->id;
                    }))
                    relay.Disconnected(s->id);
                return true;
            });
            for (const auto& control : relay.Refresh()) {
                const auto peer = std::find_if(sockets.begin(), sockets.end(), [&](const auto& p) {
                    return p->authenticated && p->id == control.recipient;
                });
                if (peer != sockets.end()) {
                    const auto& d = control.demand;
                    if (!(*peer)->Queue(Message(Capture, DemandData{d.stream, d.profile.fps, d.profile.width,
                                                                    d.profile.quality, d.bitrate})))
                        Close((*peer)->handle);
                }
            }
            // Acknowledge the actual subscription state even before any video
            // exists, and keep idle authenticated channels alive in both directions.
            for (auto& socket : sockets) {
                if (!socket->authenticated || socket->handle == INVALID_SOCKET ||
                    Now() - socket->notified < 1000)
                    continue;
                bool queued;
                if (socket->watch) {
                    StatusData report;
                    report.target = socket->watch->target;
                    report.request = socket->watch->request;
                    Text(report.message, relay.Notice(socket->id));
                    queued = socket->Queue(Message(WatchStatus, report));
                } else {
                    Header heartbeat;
                    heartbeat.kind = Heartbeat;
                    auto bytes = std::make_shared<std::vector<uint8_t>>(sizeof(heartbeat));
                    memcpy(bytes->data(), &heartbeat, sizeof(heartbeat));
                    queued = socket->Queue(std::move(bytes));
                }
                socket->notified = Now();
                if (!queued)
                    Close(socket->handle);
            }
            const auto now = Now(), bandwidth = Data::Bandwidth(a.bandwidthMbps);
            credit = (std::min)(bandwidth / 4, credit + size_t((std::min)(now - budgetAt, uint64_t(1000)) *
                                                               bandwidth / 1000));
            budgetAt = now;
            for (auto& socket : sockets)
                if (socket->handle != INVALID_SOCKET && !socket->Send(sent, credit))
                    Close(socket->handle);
            Pause();
        }
        Close(listener);
    }
    void SafeRun() noexcept {
        HudTelemetry::FeatureScope feature(HudTelemetry::Feature::SpectateTransport);
        while (!stop) {
            try {
                Run();
            } catch (...) {
                Diagnostics::Log("relay", "Background relay failed; restarting the video service.");
                std::lock_guard lock(mutex);
                error = "Spectate relay encountered a background error; reconnecting.";
            }
            for (int n = 0; n < 100 && !stop; ++n)
                Pause();
        }
    }
};
Server::Server() : state_(std::make_unique<State>()) {}
Server::~Server() { Stop(); }
void Server::Update(Authority a) {
    if (!Winsock())
        return;
    auto& s = *state_;
    std::lock_guard lifecycle(s.lifecycle);
    const bool active =
        a.epoch && std::any_of(a.members.begin(), a.members.end(), [](const Member& m) { return m.enabled; });
    {
        std::lock_guard lock(s.mutex);
        for (const auto& member : a.members) {
            const auto previous = std::find_if(s.authority.members.begin(), s.authority.members.end(),
                                               [&](const auto& old) { return old.id == member.id; });
            if (a.epoch != s.authority.epoch || previous == s.authority.members.end() ||
                previous->enabled != member.enabled || previous->sharing != member.sharing ||
                previous->ready != member.ready)
                Diagnostics::Log("authority", "Player " + std::to_string(member.id) +
                                                  " enabled=" + std::to_string(member.enabled) +
                                                  " sharing=" + std::to_string(member.sharing) +
                                                  " ready=" + std::to_string(member.ready));
        }
        s.authority = std::move(a);
        if (!s.authority.epoch)
            s.error.clear();
    }
    if (!s.thread.joinable() && active) {
        s.stop = false;
        try {
            s.thread = std::thread([&s] { s.SafeRun(); });
        } catch (...) {
            std::lock_guard lock(s.mutex);
            s.error = "Could not start the Spectate relay worker.";
        }
    }
}
void Server::Stop() {
    auto& s = *state_;
    std::lock_guard lifecycle(s.lifecycle);
    s.stop = true;
    if (s.thread.joinable())
        s.thread.join();
}
std::string Server::Error() const {
    std::lock_guard lock(state_->mutex);
    return state_->error;
}

struct Client::State {
    mutable std::mutex mutex;
    std::mutex lifecycle;
    std::optional<Connection> connection;
    Status status;
    std::deque<std::shared_ptr<const Frame>> outgoing;
    std::vector<Received> incoming;
    std::atomic<bool> stop{false};
    uint64_t reset = 0;
    std::string captureError;
    std::thread thread;
    void Run() {
        std::unique_ptr<Socket> socket;
        std::optional<Connection> used;
        uint64_t resetSeen = 0, lastOptions = 0, retry = 0, watchingAt = 0, videoAt = 0;
        std::string reportedError;
        size_t addressIndex = 0;
        bool connecting = false;
        while (!stop) {
            std::optional<Connection> wanted;
            uint64_t revision;
            {
                std::lock_guard lock(mutex);
                wanted = connection;
                revision = reset;
            }
            if (!wanted || !used || wanted->credential != used->credential || wanted->port != used->port ||
                wanted->addresses != used->addresses || revision != resetSeen) {
                socket.reset();
                used = wanted;
                resetSeen = revision;
                retry = 0;
                lastOptions = 0;
                std::lock_guard lock(mutex);
                status.connected = false;
                status.demand = {};
                status.error.clear();
                status.notice.clear();
                outgoing.clear();
                incoming.clear();
            }
            if (!wanted || wanted->addresses.empty()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
                continue;
            }
            auto fail = [&](std::string error) {
                socket.reset();
                retry = Now() + 350;
                connecting = false;
                std::lock_guard lock(mutex);
                if (status.error != error)
                    Diagnostics::Log("client", error);
                status.connected = false;
                status.demand = {};
                status.error = std::move(error);
                status.notice.clear();
                outgoing.clear();
                incoming.clear();
            };
            if (!socket) {
                if (Now() < retry) {
                    Pause();
                    continue;
                }
                socket = std::make_unique<Socket>();
                socket->handle = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
                if (socket->handle == INVALID_SOCKET || !Setup(socket->handle)) {
                    fail("Could not create Spectate video socket.");
                    continue;
                }
                sockaddr_in address{};
                address.sin_family = AF_INET;
                address.sin_port = htons(wanted->port);
                address.sin_addr.s_addr = wanted->addresses[addressIndex++ % wanted->addresses.size()];
                const int result =
                    connect(socket->handle, reinterpret_cast<sockaddr*>(&address), sizeof(address));
                if (result && WSAGetLastError() != WSAEWOULDBLOCK) {
                    fail("Spectate video connection failed. The host's TCP port " +
                         std::to_string(wanted->port) + " must be reachable.");
                    continue;
                }
                connecting = true;
            }
            if (connecting) {
                fd_set writes, errors;
                FD_ZERO(&writes);
                FD_ZERO(&errors);
                FD_SET(socket->handle, &writes);
                FD_SET(socket->handle, &errors);
                timeval timeout{};
                const int ready = select(0, nullptr, &writes, &errors, &timeout);
                if (ready < 0 || FD_ISSET(socket->handle, &errors) || Now() - socket->started > 1500) {
                    fail("Spectate video connection failed. Check the host's TCP port " +
                         std::to_string(wanted->port) + ".");
                    continue;
                }
                if (!FD_ISSET(socket->handle, &writes)) {
                    Pause();
                    continue;
                }
                int error = 0, size = sizeof(error);
                if (getsockopt(socket->handle, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error),
                               &size) ||
                    error) {
                    fail("Spectate video connection was refused on TCP port " + std::to_string(wanted->port) +
                         ".");
                    continue;
                }
                connecting = false;
                socket->Queue(Message(Auth, AuthData{wanted->credential, wanted->bandwidthMbps}));
                {
                    std::lock_guard lock(mutex);
                    status.notice = "Authenticating the video channel...";
                }
                reportedError.clear();
            }
            if (!lastOptions || *wanted != *used || Now() - lastOptions > 1000) {
                if (!lastOptions || used->watch != wanted->watch) {
                    std::lock_guard lock(mutex);
                    incoming.clear();
                    status.notice.clear();
                    watchingAt = Now();
                    videoAt = 0;
                }
                socket->Queue(Message(Options, OptionsData{wanted->ready ? 1u : 0u, wanted->sharing ? 1u : 0u,
                                                           wanted->bandwidthMbps}));
                WatchData watch;
                if (wanted->watch) {
                    const auto& w = *wanted->watch;
                    watch = {w.target, w.request, w.profile.fps, w.profile.width, w.profile.quality};
                }
                socket->Queue(Message(Subscription, watch));
                used = wanted;
                lastOptions = Now();
            }
            {
                std::lock_guard lock(mutex);
                if (captureError != reportedError) {
                    ErrorData report;
                    Text(report.message, captureError);
                    socket->Queue(Message(CaptureError, report));
                    reportedError = captureError;
                }
            }
            uint64_t received = 0, sent = 0;
            bool good = socket->Receive(
                [&](Kind kind, std::span<const uint8_t> b) {
                    if (kind == Authenticated) {
                        if (!b.empty() || socket->authenticated)
                            return false;
                        socket->authenticated = true;
                        Diagnostics::Log("client", "Host authenticated the video channel.");
                        std::lock_guard lock(mutex);
                        status.connected = true;
                        status.error.clear();
                        status.notice.clear();
                        return true;
                    }
                    if (!socket->authenticated)
                        return false;
                    if (kind == Heartbeat)
                        return b.empty();
                    if (kind == WatchStatus) {
                        StatusData report;
                        if (!Read(b, report) || !ValidText(report.message))
                            return false;
                        if (wanted->watch && report.target == wanted->watch->target &&
                            report.request == wanted->watch->request) {
                            std::lock_guard lock(mutex);
                            if (status.notice != report.message && report.message[0])
                                Diagnostics::Log("watch", report.message);
                            status.notice = report.message;
                        }
                        return true;
                    }
                    if (kind == Capture) {
                        DemandData d;
                        if (!Read(b, d) || (d.stream && (!ValidProfile({d.fps, d.width, d.quality}) ||
                                                         d.bitrate < 128000 || d.bitrate > 128000000)))
                            return false;
                        std::lock_guard lock(mutex);
                        status.demand = {d.stream, {d.fps, d.width, d.quality}, d.bitrate};
                        Diagnostics::Log("capture", d.stream ? "Host requested video capture."
                                                             : "Host stopped video capture.");
                        outgoing.clear();
                        return true;
                    }
                    if (kind != Video)
                        return false;
                    auto picture = Picture(b);
                    if (!picture)
                        return false;
                    if (!wanted->watch || picture->target != wanted->watch->target ||
                        picture->request != wanted->watch->request)
                        return true;
                    std::lock_guard lock(mutex);
                    if (incoming.size() >= 8)
                        return false;
                    videoAt = Now();
                    incoming.push_back(std::move(*picture));
                    return true;
                },
                received);
            {
                std::lock_guard lock(mutex);
                status.received += received;
                while (good && !outgoing.empty()) {
                    good = socket->Queue(Picture(*outgoing.front()));
                    outgoing.pop_front();
                }
            }
            size_t unlimited = MaxFrameBytes * 4;
            if (good)
                good = socket->Send(sent, unlimited);
            {
                std::lock_guard lock(mutex);
                status.sent += sent;
            }
            if (!good) {
                fail(socket->authenticated
                         ? "Spectate video is reconnecting after congestion or a closed connection."
                         : "Video authentication failed. The host and both players need the latest HUD DLL.");
                continue;
            }
            if ((!socket->authenticated && Now() - socket->started > 3000) || Now() - socket->active > 5000) {
                fail("The host video channel stopped responding. Check its TCP port and update the HUD on "
                     "all players.");
                continue;
            }
            {
                std::lock_guard lock(mutex);
                if (status.connected && wanted->watch && status.notice.empty() &&
                    Now() - (videoAt ? videoAt : watchingAt) > 10000)
                    status.notice = "No video frames arrived for 10 seconds. The player's capture may have "
                                    "stopped; check their Spectate diagnostics.";
            }
            Pause();
        }
    }
    void SafeRun() noexcept {
        HudTelemetry::FeatureScope feature(HudTelemetry::Feature::SpectateTransport);
        while (!stop) {
            try {
                Run();
            } catch (...) {
                Diagnostics::Log("client", "Background transport failed; reconnecting the video channel.");
                std::lock_guard lock(mutex);
                status.connected = false;
                status.demand = {};
                status.error = "Spectate video encountered a background error; reconnecting.";
                outgoing.clear();
                incoming.clear();
            }
            for (int n = 0; n < 100 && !stop; ++n)
                Pause();
        }
    }
};
Client::Client() : state_(std::make_unique<State>()) {}
Client::~Client() { Stop(); }
void Client::Update(std::optional<Connection> c) {
    if (!Winsock())
        return;
    auto& s = *state_;
    std::lock_guard lifecycle(s.lifecycle);
    const bool active = c.has_value();
    {
        std::lock_guard lock(s.mutex);
        s.connection = std::move(c);
    }
    if (!s.thread.joinable() && active) {
        s.stop = false;
        try {
            s.thread = std::thread([&s] { s.SafeRun(); });
        } catch (...) {
            std::lock_guard lock(s.mutex);
            s.status.error = "Could not start the Spectate video worker.";
        }
    }
}
void Client::Stop() {
    auto& s = *state_;
    std::lock_guard lifecycle(s.lifecycle);
    s.stop = true;
    if (s.thread.joinable())
        s.thread.join();
    std::lock_guard lock(s.mutex);
    s.status.connected = false;
    s.status.demand = {};
    s.status.error.clear();
    s.status.notice.clear();
    s.outgoing.clear();
    s.incoming.clear();
}
Status Client::Current() const {
    std::lock_guard lock(state_->mutex);
    return state_->status;
}
bool Client::Publish(std::shared_ptr<const Frame> f) {
    if (!f || !ValidFrame(*f))
        return false;
    auto& s = *state_;
    std::lock_guard lock(s.mutex);
    if (!s.connection || !s.connection->ready || !s.connection->sharing || !s.status.connected ||
        s.status.demand.stream != f->stream)
        return false;
    if (s.outgoing.size() >= 2) {
        ++s.reset;
        return false;
    }
    s.outgoing.push_back(std::move(f));
    return true;
}
std::vector<Received> Client::Drain() {
    std::lock_guard lock(state_->mutex);
    auto result = std::move(state_->incoming);
    state_->incoming.clear();
    return result;
}
void Client::Resync() {
    std::lock_guard lock(state_->mutex);
    ++state_->reset;
}
void Client::ReportCaptureError(std::string error) {
    std::lock_guard lock(state_->mutex);
    if (state_->captureError != error) {
        if (!error.empty())
            Diagnostics::Log("capture", error);
        state_->captureError = std::move(error);
    }
}
Server& Host() { return HudLifetime::State<Server>(); }
Client& Local() { return HudLifetime::State<Client>(); }
std::vector<uint32_t> GameAddresses(bool hosting, uint16_t gamePort) {
    auto fallback = [&] {
        return hosting ? std::vector<uint32_t>{htonl(INADDR_LOOPBACK)} : std::vector<uint32_t>{};
    };
    std::vector<uint32_t> result;
    ULONG size = 0;
    if (GetExtendedTcpTable(nullptr, &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_CONNECTIONS, 0) !=
            ERROR_INSUFFICIENT_BUFFER ||
        size > 4 * 1024 * 1024)
        return fallback();
    std::vector<uint8_t> storage(size);
    if (GetExtendedTcpTable(storage.data(), &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_CONNECTIONS, 0) !=
        NO_ERROR)
        return fallback();
    const auto table = reinterpret_cast<const MIB_TCPTABLE_OWNER_PID*>(storage.data());
    for (DWORD n = 0; n < table->dwNumEntries && result.size() < 4; ++n) {
        const auto& row = table->table[n];
        // Use the actual native game port (4000 by default). Never probe
        // arbitrary peers, the LAN, or addresses supplied by another player.
        if (row.dwOwningPid == GetCurrentProcessId() && row.dwState == MIB_TCP_STATE_ESTAB &&
            ntohs(u_short(row.dwRemotePort)) == gamePort && row.dwRemoteAddr &&
            std::find(result.begin(), result.end(), row.dwRemoteAddr) == result.end())
            result.push_back(row.dwRemoteAddr);
    }
    // Prefer the actual connected peer even if a transitional native host flag
    // has not caught up yet. A local host can use its in-process game server.
    return result.empty() ? fallback() : result;
}
} // namespace Spectate::Stream
