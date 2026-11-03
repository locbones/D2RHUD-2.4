#define NOMINMAX
#include "../d2rhud/plugin/Spectate.h"
#include "../d2rhud/plugin/SpectateTransport.h"
#include <Windows.h>
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>

namespace Spectate::Video {
std::vector<uint8_t> Encode(const uint8_t*, unsigned, unsigned, unsigned, unsigned, Settings);
}
using namespace Spectate::Stream;
static void SleepBriefly() { std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
template <class F> void Until(F condition, const char* message) {
    const auto deadline = GetTickCount64() + 6000;
    while (!condition() && GetTickCount64() < deadline)
        SleepBriefly();
    if (!condition()) {
        std::cerr << message << '\n';
        std::abort();
    }
}
static Frame Example(uint64_t stream, uint64_t sequence = 1) {
    Frame f;
    f.stream = stream;
    f.sequence = sequence;
    f.width = 1920;
    f.height = 1080;
    f.key = sequence == 1;
    f.bytes.resize(1000, 42);
    return f;
}
int main() {
    Authority a;
    a.seed = 33;
    a.epoch = 44;
    a.port = uint16_t(20000 + GetCurrentProcessId() % 20000);
    a.bandwidthMbps = 128;
    a.refreshed = GetTickCount64();
    for (uint32_t n = 1; n <= 3; ++n) {
        Member m{n};
        m.token.fill(uint8_t(n * 17));
        m.enabled = m.sharing = m.ready = true;
        a.members.push_back(m);
    }
    auto credential = [&](unsigned n) { return Credential{n, a.seed, a.epoch, a.members[n - 1].token}; };
    Relay relay;
    relay.Update(a);
    auto bad = credential(1);
    bad.token[2] ^= 1;
    assert(!relay.Authorized(bad));
    bad = credential(1);
    ++bad.epoch;
    assert(!relay.Authorized(bad));
    assert(relay.Connected(credential(1), 128) && relay.Connected(credential(2), 128));
    assert(!relay.Subscribe(2, Watch{2, 1, {30, 1920, 80}}));
    assert(relay.Subscribe(2, Watch{1, 7, {30, 1920, 80}}));
    auto controls = relay.Refresh();
    const auto control =
        std::find_if(controls.begin(), controls.end(), [](const auto& c) { return c.recipient == 1; });
    assert(control != controls.end() && control->demand.stream);
    auto f = Example(control->demand.stream);
    f.key = false;
    assert(relay.Publish(1, f).empty());
    f.key = true;
    assert(relay.Publish(1, f).size() == 1 && relay.Publish(1, f).empty());
    relay.Options(1, true, false, 128);
    relay.Refresh();
    assert(relay.Publish(1, Example(f.stream, 2)).empty());
    relay.Options(1, true, true, 128);
    assert(relay.Connected(credential(3), 128) && relay.Subscribe(3, Watch{1, 8, {60, 3840, 100}}));
    controls = relay.Refresh();
    auto maximum =
        std::find_if(controls.begin(), controls.end(), [](const auto& c) { return c.recipient == 1; });
    assert(maximum != controls.end() && maximum->demand.profile.width == 3840 &&
           maximum->demand.profile.fps == 60);
    auto shared = Example(maximum->demand.stream);
    shared.bytes.resize(32000);
    assert(relay.Publish(1, shared).size() == 2);
    const auto rate = maximum->demand.bitrate;
    relay.Congested(1);
    controls = relay.Refresh();
    maximum = std::find_if(controls.begin(), controls.end(), [](const auto& c) { return c.recipient == 1; });
    assert(maximum != controls.end() && maximum->demand.bitrate < rate &&
           maximum->demand.stream != shared.stream);
    // Actual independent clients and an authenticated host relay on loopback.
    Server server;
    server.Update(a);
    Client source, viewer, intruder;
    Connection sc;
    sc.credential = credential(1);
    sc.port = a.port;
    sc.addresses = {0x0100007f};
    sc.ready = sc.sharing = true;
    sc.bandwidthMbps = 128;
    auto vc = sc;
    vc.credential = credential(2);
    vc.watch = Watch{1, 1, {30, 1920, 80}};
    auto ic = vc;
    ic.credential = credential(3);
    ic.credential.token[0] ^= 1;
    source.Update(sc);
    viewer.Update(vc);
    intruder.Update(ic);
    Until([&] { return source.Current().demand.stream != 0; }, "No authenticated capture demand");
    Until([&] { return !intruder.Current().error.empty(); }, "Rejected credentials had no diagnostic");
    assert(!intruder.Current().connected && intruder.Current().demand.stream == 0 &&
           intruder.Drain().empty());
    assert(source.Current().connected && viewer.Current().connected);
    // Source codec failures must reach a remote viewer before any image exists.
    source.ReportCaptureError("H.264 encoder creation failed (0x80040154).");
    Until([&] { return viewer.Current().notice.find("0x80040154") != std::string::npos; },
          "Source encoder failure left the viewer loading");
    source.ReportCaptureError({});
    Until([&] { return viewer.Current().notice.empty(); }, "Source recovery did not clear the diagnostic");
    // Native presence does not mean that this player's separate video channel connected.
    source.Update(std::nullopt);
    Until([&] { return viewer.Current().notice.find("not connected") != std::string::npos; },
          "Missing publisher left the viewer loading");
    source.Update(sc);
    Until([&] { return source.Current().demand.stream != 0 && viewer.Current().notice.empty(); },
          "Publisher did not recover after connecting");
    a.members[0].ready = false;
    a.refreshed = GetTickCount64();
    server.Update(a);
    Until([&] { return viewer.Current().notice.find("finish loading") != std::string::npos; },
          "Loading state was not communicated to the viewer");
    a.members[0].ready = true;
    a.refreshed = GetTickCount64();
    server.Update(a);
    Until([&] { return source.Current().demand.stream != 0 && viewer.Current().notice.empty(); },
          "Leaving the loading screen did not resume capture");
    const auto frameDeadline = GetTickCount64() + 12000;
    while (GetTickCount64() < frameDeadline && viewer.Current().notice.find("10 seconds") == std::string::npos) {
        a.refreshed = GetTickCount64();
        server.Update(a);
        SleepBriefly();
    }
    assert(viewer.Current().notice.find("10 seconds") != std::string::npos);
    Encoder encoder;
    Decoder decoder;
    constexpr unsigned width = 1920, height = 1080, frames = 120;
    std::vector<uint8_t> rgba(size_t(width) * height * 4);
    // Textured static scenery plus moving objects, rather than a solid image.
    uint32_t noise = 12345;
    for (unsigned y = 0; y < height; ++y)
        for (unsigned x = 0; x < width; ++x) {
            noise ^= noise << 13;
            noise ^= noise >> 17;
            noise ^= noise << 5;
            const auto at = (size_t(y) * width + x) * 4;
            rgba[at] = uint8_t(50 + (noise & 63));
            rgba[at + 1] = uint8_t(30 + ((x / 16 + y / 16) % 10) * 8);
            rgba[at + 2] = uint8_t(25 + ((noise >> 8) & 31));
            rgba[at + 3] = 255;
        }
    const auto background = rgba;
    uint64_t stream = source.Current().demand.stream;
    size_t decoded = 0, h264Bytes = 0, jpegBytes = 0;
    std::vector<double> latency;
    const auto start = std::chrono::steady_clock::now();
    auto milliseconds = [&] {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    };
    auto receive = [&] {
        for (const auto& received : viewer.Drain()) {
            assert(received.target == 1 && received.request == 1 && received.frame->stream == stream);
            auto pixels = decoder.Decode(*received.frame);
            if (!pixels)
                std::cerr << decoder.Error() << '\n';
            assert(pixels && pixels->width == width && pixels->height == height);
            latency.push_back(milliseconds() - double(received.frame->time) / 1000.0);
            ++decoded;
        }
    };
    for (unsigned n = 0; n < frames; ++n) {
        while (milliseconds() < n * 1000.0 / 30) {
            receive();
            SleepBriefly();
        }
        a.refreshed = GetTickCount64();
        server.Update(a);
        rgba = background;
        for (unsigned y = 250; y < 430; ++y)
            for (unsigned x = n * 11 % 1600; x < n * 11 % 1600 + 200; ++x) {
                const auto at = (size_t(y) * width + x) * 4;
                rgba[at] = 210;
                rgba[at + 1] = 130;
                rgba[at + 2] = 45;
            }
        const auto captured = uint64_t(milliseconds() * 1000);
        auto frame =
            encoder.Encode(rgba.data(), width, height, width * 4, 28, {30, 1920, 80}, 7600000, stream, n + 1);
        assert(frame);
        frame->time = captured;
        h264Bytes += frame->bytes.size();
        if (n % 30 == 0)
            jpegBytes += Spectate::Video::Encode(rgba.data(), width, height, width * 4, 28,
                                                 {true, true, 30, 1920, 80, 128})
                             .size();
        if (!source.Publish(std::make_shared<const Frame>(std::move(*frame)))) {
            std::cerr << source.Current().error << '\n';
            std::abort();
        }
        receive();
    }
    Until(
        [&] {
            receive();
            return decoded == frames;
        },
        "Frames missing from complete video path");
    const auto elapsed = milliseconds();
    std::sort(latency.begin(), latency.end());
    std::cout << "BENCHMARK: " << decoded << " 1080p frames at " << decoded * 1000 / elapsed << " fps; H264 "
              << h264Bytes * 8.0 * 30 / frames / 1000000 << " Mbps; JPEG baseline "
              << jpegBytes * 8.0 * 30 / 4 / 1000000 << " Mbps; median latency " << latency[latency.size() / 2]
              << " ms, p95 " << latency[latency.size() * 95 / 100] << " ms\n";
    assert(decoded == frames && h264Bytes < jpegBytes * 30 && latency[latency.size() * 95 / 100] < 500);
    const auto oldStream = stream;
    vc.watch->profile.width = 1280;
    viewer.Update(vc);
    Until(
        [&] {
            const auto d = source.Current().demand;
            return d.stream && d.stream != oldStream && d.profile.width == 1280;
        },
        "Live settings were not applied");
    // Host authority overrides a publisher that still advertises sharing.
    a.members[0].sharing = false;
    a.refreshed = GetTickCount64();
    server.Update(a);
    Until([&] { return !source.Current().demand.stream; }, "Host consent revocation was ignored");
    a.members[0].sharing = true;
    a.refreshed = GetTickCount64();
    server.Update(a);
    Until([&] { return source.Current().demand.stream != 0; }, "Host consent did not resume");
    sc.sharing = false;
    source.Update(sc);
    Until([&] { return !source.Current().demand.stream; }, "Sharing off did not stop capture");
    assert(!source.Publish(std::make_shared<const Frame>(Example(oldStream, frames + 1))));
    sc.sharing = true;
    source.Update(sc);
    Until([&] { return source.Current().demand.stream != 0; }, "Sharing did not resume");
    const auto reconnectStream = source.Current().demand.stream;
    viewer.Resync();
    Until([&] { return source.Current().demand.stream && source.Current().demand.stream != reconnectStream; },
          "Reconnect did not request a new keyframe");
    viewer.Update(std::nullopt);
    Until([&] { return !source.Current().demand.stream; }, "Disconnected viewer left capture running");
    source.Stop();
    viewer.Stop();
    intruder.Stop();
    server.Stop();
    // Concurrent updates used to inspect/assign the same std::thread without a
    // lock. Exercise start/stop serialization without touching a game process.
    Server concurrentServer;
    Client concurrentClient;
    std::vector<std::thread> callers;
    for (int n = 0; n < 8; ++n)
        callers.emplace_back([&] {
            for (int i = 0; i < 30; ++i) {
                concurrentServer.Update(a);
                concurrentClient.Update(sc);
                if (i % 10 == 0) {
                    concurrentClient.Stop();
                    concurrentServer.Stop();
                }
            }
        });
    for (auto& t : callers)
        t.join();
    concurrentClient.Stop();
    concurrentServer.Stop();
    std::cout << "PASS: consent, forged credentials, stale sessions, replays, real H264 over host relay, "
                 "live profile changes, privacy, reconnect, disconnect, authentication ACK, remote capture "
                 "errors, absent source, loading recovery and concurrent worker lifecycle\n";
}
