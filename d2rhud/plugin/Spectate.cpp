#include "Spectate.h"
#include "../ProcessLifetime.h"
#include "SpectateDiagnostics.h"
#include "SpectateTransport.h"
#include <mutex>

namespace Spectate {
namespace {
struct State {
    std::mutex mutex;
    Settings settings;
    View view;
    uint64_t session = 0, generation = 1, viewGeneration = 1, hello = 0, advertised = 0;
    uint32_t recipient = 0, seed = 0, epoch = 0, serial = 0, capture = 0;
    bool advertise = true;
    bool captureBlocked = false;
    std::optional<Stream::Credential> credential;
    uint16_t port = 0;
    std::vector<uint32_t> addresses;
    uint64_t addressAt = 0, captureStream = 0, openedAt = 0;
    double nextCapture = 0;
    std::vector<std::array<float, 4>> regions;
    uint64_t regionsAt = 0;
    uint8_t heldMouse = 0;
};
State& Client() { return HudLifetime::State<State>(); }
double Clock() {
    LARGE_INTEGER value, frequency;
    QueryPerformanceCounter(&value);
    QueryPerformanceFrequency(&frequency);
    return double(value.QuadPart) / double(frequency.QuadPart);
}
void ClearView(State& s) {
    s.view.open = s.view.fullscreen = false;
    s.view.receivedAt = 0;
    s.view.receivedFps = 0;
    s.view.request = 0;
    s.view.name.clear();
    s.view.status.clear();
    s.view.videoError.clear();
    s.regions.clear();
    s.view.generation = ++s.viewGeneration;
}
void Reset(State& s) {
    ClearView(s);
    s.view = {};
    s.view.generation = s.viewGeneration;
    ++s.generation;
    s.epoch = 0;
    s.hello = s.advertised = 0;
    s.credential.reset();
    s.addresses.clear();
    s.addressAt = 0;
    s.advertise = true;
    s.captureStream = 0;
    s.nextCapture = 0;
}
} // namespace
void Configure(Settings settings) {
    settings.fps = std::clamp(settings.fps, Data::MinFps, Data::MaxFps);
    settings.width = std::clamp(settings.width, Data::MinWidth, Data::MaxWidth);
    settings.quality = std::clamp(settings.quality, Data::MinQuality, Data::MaxQuality);
    settings.bandwidthMbps = std::clamp(settings.bandwidthMbps, Data::MinBandwidth, Data::MaxBandwidth);
    settings.videoPort = std::clamp(settings.videoPort, 1024, 65535);
    auto& s = Client();
    std::lock_guard lock(s.mutex);
    const bool changed = settings.enabled != s.settings.enabled || settings.sharing != s.settings.sharing;
    s.settings = settings;
    s.advertise |= changed;
    if (!settings.enabled)
        ClearView(s);
    if (changed)
        ++s.generation;
}
Settings Configuration() {
    auto& s = Client();
    std::lock_guard lock(s.mutex);
    return s.settings;
}
View Current() {
    auto& s = Client();
    std::lock_guard lock(s.mutex);
    return s.view;
}
bool Open(uint32_t player) {
    auto& s = Client();
    std::lock_guard lock(s.mutex);
    const auto now = GetTickCount64();
    const auto at =
        std::find_if(s.view.peers.begin(), s.view.peers.end(), [&](const Peer& p) { return p.id == player; });
    if (!s.settings.enabled || !s.view.inGame || !s.view.ready || at == s.view.peers.end() || !at->sharing ||
        now - at->seen > 6000 || player == s.recipient)
        return false;
    const auto peer = *at;
    ClearView(s);
    if (!++s.serial)
        ++s.serial;
    s.view.open = true;
    Diagnostics::Log("watch", "Opened viewer for player " + std::to_string(player));
    s.openedAt = now;
    s.view.target = player;
    s.view.request = s.serial;
    s.view.name = peer.name;
    s.view.status =
        peer.ready ? "Connecting to live gameplay..." : "Waiting for the player to finish loading...";
    return true;
}
void Close() {
    auto& s = Client();
    std::lock_guard lock(s.mutex);
    ClearView(s);
}
void Fullscreen(bool value) {
    auto& s = Client();
    std::lock_guard lock(s.mutex);
    s.view.fullscreen = value && s.view.open;
}
bool BlocksGameInput() {
    auto& s = Client();
    std::lock_guard lock(s.mutex);
    return s.view.open && s.view.fullscreen && s.view.inGame;
}
bool VideoDemand() {
    const auto network = Stream::Local().Current();
    auto& s = Client();
    std::lock_guard lock(s.mutex);
    return s.settings.enabled && s.view.inGame &&
           (s.view.open || (s.settings.sharing && network.demand.stream));
}
void ClientTick(const HudFrame::Context& frame, uint64_t session) {
    std::vector<Data::CommandPacket> commands;
    const bool active = frame.inGame && Native::TcpGame(), inGame = active && !frame.loading;
    const auto network = Stream::Local().Current();
    std::optional<Stream::Connection> connection;
    auto& s = Client();
    {
        std::lock_guard lock(s.mutex);
        if (session != s.session || (!active && s.epoch)) {
            Reset(s);
            s.session = session;
        }
        if (s.view.inGame != inGame) {
            s.advertise = true;
            ++s.generation;
        }
        s.view.inGame = inGame;
        const auto now = GetTickCount64();
        std::erase_if(s.view.peers, [&](const Peer& p) { return now - p.seen > 6500; });
        if (s.epoch && now - s.hello > 6500) {
            Reset(s);
            s.session = session;
            s.view.inGame = inGame;
        }
        if (s.epoch && (s.advertise || !s.advertised || now - s.advertised >= 2000)) {
            Data::CommandPacket c;
            c.packet.opcode = 3;
            c.packet.action = Data::Advertise;
            c.packet.recipient = s.recipient;
            c.packet.seed = s.seed;
            c.packet.epoch = s.epoch;
            c.packet.total = (s.settings.enabled ? Data::Enabled : 0) |
                             (s.settings.enabled && s.settings.sharing ? Data::Sharing : 0) |
                             (inGame ? Data::Ready : 0);
            commands.push_back(c);
            s.advertise = false;
            s.advertised = now;
        }
        if (active && s.settings.enabled && s.credential) {
            if (!s.addressAt || ((!network.connected || s.addresses.empty()) && now - s.addressAt >= 1000)) {
                s.addresses = Stream::GameAddresses(Native::Hosting(), Native::GamePort());
                s.addressAt = now;
            }
            Stream::Connection c;
            c.credential = *s.credential;
            c.addresses = s.addresses;
            c.port = s.port;
            c.ready = inGame;
            c.sharing = s.settings.sharing;
            c.bandwidthMbps = unsigned(s.settings.bandwidthMbps);
            if (s.view.open)
                c.watch = Stream::Watch{
                    s.view.target, s.view.request, {s.settings.fps, s.settings.width, s.settings.quality}};
            connection = std::move(c);
        }
        if (s.view.open) {
            if (!s.view.videoError.empty())
                s.view.status = s.view.videoError;
            else if (Native::Hosting() && !Stream::Host().Error().empty())
                s.view.status = Stream::Host().Error();
            else if (!s.credential)
                s.view.status =
                    now - s.openedAt > 6000
                        ? "No video credentials received. Update the HUD on the host and both players."
                        : "Waiting for the host to authorize video...";
            else if (s.addresses.empty())
                s.view.status = "Spectate could not find the host's established game connection.";
            else if (!network.error.empty())
                s.view.status = network.error;
            else if (!network.notice.empty())
                s.view.status = network.notice;
            else if (!network.connected)
                s.view.status = "Connecting to the host's video TCP port " + std::to_string(s.port) + "...";
            else if (!s.view.receivedAt || now - s.view.receivedAt > 2500)
                s.view.status = "Video channel authenticated; waiting for gameplay frames...";
        }
    }
    Stream::Local().Update(std::move(connection));
    for (const auto& c : commands)
        Native::Send(c);
}
void Receive(const uint8_t* bytes, size_t size) {
    if (!bytes || size != sizeof(Data::Packet))
        return;
    Data::Packet p;
    memcpy(&p, bytes, sizeof(p));
    if (!Data::Valid(p) || p.opcode != 0x2b || !Native::TcpGame())
        return;
    const auto local = Native::LocalId();
    if (!local || *local != p.recipient)
        return;
    auto& s = Client();
    std::lock_guard lock(s.mutex);
    const auto now = GetTickCount64();
    if (p.action == Data::Hello && p.epoch && !p.request && !p.frame && !p.total && !p.offset && !p.length) {
        if (s.epoch != p.epoch || s.seed != p.seed)
            Reset(s);
        s.epoch = p.epoch;
        s.seed = p.seed;
        s.recipient = p.recipient;
        s.hello = now;
        s.view.ready = true;
        return;
    }
    if (!s.epoch || p.epoch != s.epoch || p.seed != s.seed || p.recipient != s.recipient ||
        !s.settings.enabled)
        return;
    if (p.action == Data::ChannelGrant && p.length == 34 && !p.request && !p.frame && !p.total && !p.offset) {
        const auto port = uint16_t(p.bytes[0] | (unsigned(p.bytes[1]) << 8));
        if (port < 1024)
            return;
        Stream::Credential c{p.recipient, p.seed, p.epoch};
        memcpy(c.token.data(), p.bytes.data() + 2, c.token.size());
        if (s.credential != c || s.port != port)
            Diagnostics::Log("native",
                             "Received video channel credentials for TCP port " + std::to_string(port));
        s.credential = c;
        s.port = port;
        return;
    }
    if (p.action == Data::Peer && !p.request && !p.frame && !p.offset && p.total <= 7 &&
        p.target != s.recipient) {
        std::string name(reinterpret_cast<const char*>(p.bytes.data()), p.length);
        if (!Data::Name(name))
            return;
        const Peer peer{p.target, name, (p.total & 3) == 3, bool(p.total & Data::Ready), now};
        auto at = std::find_if(s.view.peers.begin(), s.view.peers.end(),
                               [&](const Peer& x) { return x.id == peer.id; });
        if (at != s.view.peers.end())
            *at = peer;
        else if (s.view.peers.size() < Data::MaxPlayers - 1)
            s.view.peers.push_back(peer);
        if (s.view.open && s.view.target == peer.id && !peer.sharing) {
            ClearView(s);
        }
    }
}
std::optional<CaptureRequest> TakeCapture() {
    const auto network = Stream::Local().Current();
    auto& s = Client();
    std::lock_guard lock(s.mutex);
    const bool blocked = network.connected && network.demand.stream && s.view.inGame && s.settings.enabled &&
                         s.settings.sharing && !Native::CaptureReady();
    if (s.captureBlocked != blocked) {
        s.captureBlocked = blocked;
        Diagnostics::Log("capture", blocked ? "Native game state is blocking capture (loading or not ready)."
                                            : "Native capture readiness recovered.");
    }
    if (!network.connected || !network.demand.stream || !s.view.inGame || !s.settings.enabled ||
        !s.settings.sharing || blocked)
        return {};
    const auto now = Clock();
    if (s.captureStream != network.demand.stream) {
        s.captureStream = network.demand.stream;
        s.nextCapture = now;
        s.capture = 0;
    }
    if (now < s.nextCapture)
        return {};
    const double interval = 1.0 / network.demand.profile.fps;
    s.nextCapture = (std::max)(s.nextCapture + interval, now - interval * .25);
    auto settings = s.settings;
    settings.fps = network.demand.profile.fps;
    settings.width = network.demand.profile.width;
    settings.quality = network.demand.profile.quality;
    return CaptureRequest{++s.capture, s.generation, settings, network.demand.stream, network.demand.bitrate};
}
void Submit(const CaptureRequest& request, Stream::Frame frame) {
    auto& s = Client();
    std::lock_guard lock(s.mutex);
    if (request.generation != s.generation || !s.view.inGame || !s.settings.enabled || !s.settings.sharing)
        return;
    s.view.videoError.clear();
    Stream::Local().ReportCaptureError({});
    Stream::Local().Publish(std::make_shared<const Stream::Frame>(std::move(frame)));
}
void CaptureFailed(const CaptureRequest& request) {
    auto& s = Client();
    std::lock_guard lock(s.mutex);
    if (request.generation == s.generation)
        Stream::Local().Resync();
}
void Displayed(uint64_t generation, uint64_t revision) {
    auto& s = Client();
    std::lock_guard lock(s.mutex);
    if (!s.view.open || generation != s.view.generation)
        return;
    const auto now = GetTickCount64();
    if (s.view.receivedAt && now > s.view.receivedAt) {
        const float rate = 1000.f / float(now - s.view.receivedAt);
        s.view.receivedFps = s.view.receivedFps > 0 ? s.view.receivedFps * .85f + rate * .15f : rate;
    }
    s.view.receivedAt = now;
    s.view.revision = revision;
    s.view.status.clear();
    s.view.videoError.clear();
}
void VideoError(std::string error) {
    auto& s = Client();
    std::lock_guard lock(s.mutex);
    s.view.videoError = error;
    if (s.view.open)
        s.view.status = std::move(error);
}
void SetInputRegion(float x, float y, float width, float height) {
    auto& s = Client();
    std::lock_guard lock(s.mutex);
    s.regions.clear();
    s.regionsAt = GetTickCount64();
    if (width > 0 && height > 0)
        s.regions.push_back({x, y, width, height});
}
void AddInputRegion(float x, float y, float width, float height) {
    auto& s = Client();
    std::lock_guard lock(s.mutex);
    if (s.regions.size() < 16 && width > 0 && height > 0)
        s.regions.push_back({x, y, width, height});
}
bool WindowInput(HWND window, UINT message, WPARAM key, LPARAM parameter) {
    if (message == WM_KILLFOCUS || (message == WM_ACTIVATEAPP && !key)) {
        auto& s = Client();
        std::lock_guard lock(s.mutex);
        s.heldMouse = 0;
        return false;
    }
    if (message == WM_KEYDOWN && key == VK_ESCAPE && BlocksGameInput()) {
        Close();
        return true;
    }
    auto& s = Client();
    std::lock_guard lock(s.mutex);
    const bool fullscreen = s.view.open && s.view.fullscreen && s.view.inGame;
    if (!fullscreen && s.regions.empty() && !s.heldMouse)
        return false;
    if ((message == WM_SYSKEYDOWN || message == WM_SYSKEYUP) && key == VK_F4 &&
        (parameter & (LPARAM(1) << 29)))
        return false;
    // Releases pass through to clear actions held before entering full screen.
    if (message == WM_KEYUP || message == WM_SYSKEYUP)
        return false;
    if (fullscreen &&
        (message == WM_KEYDOWN || message == WM_SYSKEYDOWN || message == WM_CHAR || message == WM_SYSCHAR ||
         message == WM_UNICHAR || message == WM_DEADCHAR || message == WM_SYSDEADCHAR))
        return true;
    bool down = message == WM_LBUTTONDOWN || message == WM_LBUTTONDBLCLK || message == WM_RBUTTONDOWN ||
                message == WM_RBUTTONDBLCLK || message == WM_MBUTTONDOWN || message == WM_MBUTTONDBLCLK ||
                message == WM_XBUTTONDOWN || message == WM_XBUTTONDBLCLK;
    bool up = message == WM_LBUTTONUP || message == WM_RBUTTONUP || message == WM_MBUTTONUP ||
              message == WM_XBUTTONUP;
    unsigned button = message == WM_RBUTTONDOWN || message == WM_RBUTTONDBLCLK || message == WM_RBUTTONUP ? 2
                      : message == WM_MBUTTONDOWN || message == WM_MBUTTONDBLCLK || message == WM_MBUTTONUP
                          ? 4
                          : 1;
    if (message == WM_XBUTTONDOWN || message == WM_XBUTTONDBLCLK || message == WM_XBUTTONUP)
        button = GET_XBUTTON_WPARAM(key) == XBUTTON2 ? 16 : 8;
    if (message == WM_INPUT) {
        RAWINPUT raw{};
        UINT size = sizeof(raw);
        if (GetRawInputData(reinterpret_cast<HRAWINPUT>(parameter), RID_INPUT, &raw, &size,
                            sizeof(RAWINPUTHEADER)) == UINT(-1))
            return false;
        if (raw.header.dwType == RIM_TYPEKEYBOARD)
            return fullscreen && !(raw.data.keyboard.Flags & RI_KEY_BREAK);
        if (raw.header.dwType != RIM_TYPEMOUSE)
            return false;
        const auto flags = raw.data.mouse.usButtonFlags;
        down = flags & (RI_MOUSE_LEFT_BUTTON_DOWN | RI_MOUSE_RIGHT_BUTTON_DOWN | RI_MOUSE_MIDDLE_BUTTON_DOWN |
                        RI_MOUSE_BUTTON_4_DOWN | RI_MOUSE_BUTTON_5_DOWN);
        up = flags & (RI_MOUSE_LEFT_BUTTON_UP | RI_MOUSE_RIGHT_BUTTON_UP | RI_MOUSE_MIDDLE_BUTTON_UP |
                      RI_MOUSE_BUTTON_4_UP | RI_MOUSE_BUTTON_5_UP);
        button = flags & (RI_MOUSE_RIGHT_BUTTON_DOWN | RI_MOUSE_RIGHT_BUTTON_UP)     ? 2
                 : flags & (RI_MOUSE_MIDDLE_BUTTON_DOWN | RI_MOUSE_MIDDLE_BUTTON_UP) ? 4
                                                                                     : 1;
        if (flags & (RI_MOUSE_BUTTON_4_DOWN | RI_MOUSE_BUTTON_4_UP))
            button = 8;
        if (flags & (RI_MOUSE_BUTTON_5_DOWN | RI_MOUSE_BUTTON_5_UP))
            button = 16;
    }
    if (up && (s.heldMouse & button)) {
        s.heldMouse &= ~button;
        return true;
    }
    POINT point{};
    bool inside = false;
    if (GetTickCount64() - s.regionsAt < 250 && GetCursorPos(&point) && ScreenToClient(window, &point))
        for (const auto& r : s.regions)
            inside |= point.x >= r[0] && point.y >= r[1] && point.x < r[0] + r[2] && point.y < r[1] + r[3];
    const bool mouse = message == WM_INPUT || (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST);
    if (down && (inside || fullscreen))
        s.heldMouse |= button;
    return mouse && (fullscreen ? !up : inside);
}
} // namespace Spectate
