#define NOMINMAX
#include "../d2rhud/plugin/Spectate.cpp"
#include <cassert>
#include <iostream>
namespace {
std::optional<uint32_t> local = 0;
bool tcp = true;
std::vector<Spectate::Data::CommandPacket> sent;
} // namespace
bool Spectate::Native::TcpGame() { return tcp; }
bool Spectate::Native::Hosting() { return true; }
uint16_t Spectate::Native::GamePort() { return 4000; }
bool Spectate::Native::CaptureReady() { return tcp; }
std::optional<uint32_t> Spectate::Native::LocalId() { return local; }
void Spectate::Native::Send(const Data::CommandPacket& p) { sent.push_back(p); }
int main() {
    using namespace Spectate;
    Configure({true, true, 30, 1920, 80, 128});
    HudFrame::Context context;
    context.inGame = true;
    ClientTick(context, 1);
    Data::Packet hello;
    hello.seed = 77;
    hello.epoch = 5;
    Receive(reinterpret_cast<const uint8_t*>(&hello), sizeof(hello));
    ClientTick(context, 1);
    assert(Current().ready && sent.size() == 1 && sent[0].packet.action == Data::Advertise &&
           sent[0].packet.total == 7);
    Data::Host host(77, 5);
    host.Players({{0, "Viewer"}, {1, "Source"}});
    host.Receive(0, sent[0].packet, GetTickCount64());
    assert(host.Flags(0, GetTickCount64()) == 7);
    auto forged = sent[0].packet;
    forged.recipient = 1;
    forged.total = 0;
    host.Receive(0, forged, GetTickCount64());
    assert(host.Flags(0, GetTickCount64()) == 7);
    assert(host.Flags(0, GetTickCount64() + 6001) == 0);
    auto old = hello;
    old.version = 3;
    assert(!Data::Valid(old));
    auto peer = hello;
    peer.action = Data::Peer;
    peer.target = 1;
    peer.total = 7;
    peer.length = 6;
    memcpy(peer.bytes.data(), "Source", 6);
    Receive(reinterpret_cast<const uint8_t*>(&peer), sizeof(peer));
    assert(Open(1) && Current().open);
    Fullscreen(true);
    assert(BlocksGameInput());
    assert(WindowInput(nullptr, WM_KEYDOWN, 'W', 0));
    assert(!WindowInput(nullptr, WM_KEYUP, 'W', 0));
    assert(!WindowInput(nullptr, WM_SYSKEYDOWN, VK_F4, LPARAM(1) << 29));
    assert(WindowInput(nullptr, WM_KEYDOWN, VK_ESCAPE, 0));
    assert(!Current().open);
    assert(Open(1));
    const auto generation = Current().generation;
    Displayed(generation - 1, 1);
    assert(!Current().receivedAt);
    Displayed(generation, 1);
    assert(Current().receivedAt && Current().revision == 1);
    auto disabled = Configuration();
    disabled.enabled = false;
    Configure(disabled);
    assert(!Current().open && !BlocksGameInput() && !VideoDemand());
    ClientTick(context, 1);
    assert(sent.back().packet.total == Data::Ready);
    for (const auto& packet : sent)
        assert(packet.packet.action == Data::Advertise);
    context.inGame = false;
    ClientTick(context, 1);
    assert(!Current().ready);
    Stream::Local().Stop();
    std::cout << "PASS: native presence only, protocol revision, authoritative consent, stale sessions, "
                 "viewer/input/privacy state and generation guards\n";
}
