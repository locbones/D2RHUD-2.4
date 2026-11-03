#define NOMINMAX
#include "../d2rhud/plugin/SpectateTransport.cpp"
#include <cassert>
#include <iostream>
using namespace Spectate::Stream;
int main() {
    assert(Winsock());
    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    assert(listener != INVALID_SOCKET);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(!bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) && !listen(listener, 1));
    int length = sizeof(address);
    assert(!getsockname(listener, reinterpret_cast<sockaddr*>(&address), &length));
    auto pair = [&] {
        auto writer = std::make_unique<Socket>(), reader = std::make_unique<Socket>();
        writer->handle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        assert(!connect(writer->handle, reinterpret_cast<sockaddr*>(&address), sizeof(address)));
        reader->handle = accept(listener, nullptr, nullptr);
        assert(reader->handle != INVALID_SOCKET);
        assert(Setup(writer->handle) && Setup(reader->handle));
        return std::pair{std::move(writer), std::move(reader)};
    };
    auto rejected = [&](Header h, bool authentication) {
        auto [writer, reader] = pair();
        reader->requireAuth = authentication;
        assert(send(writer->handle, reinterpret_cast<char*>(&h), sizeof(h), 0) == sizeof(h));
        uint64_t bytes = 0;
        bool good = true;
        for (int n = 0; n < 200 && good; ++n) {
            good = reader->Receive([](Kind, auto) { return false; }, bytes);
            if (good)
                Sleep(1);
        }
        assert(!good);
    };
    Header h;
    h.kind = Video;
    h.size = MaxFrameBytes + 45;
    rejected(h, false);
    h.size = 100;
    rejected(h, true); // Reject a video header before allocating its announced payload.
    h.kind = Auth;
    h.size = sizeof(AuthData);
    ++h.version;
    rejected(h, true);
    h.version = Version;
    h.magic ^= 1;
    rejected(h, true);
    auto [writer, reader] = pair();
    const auto endpoints = GameAddresses(false, ntohs(address.sin_port));
    assert(endpoints.size() == 1 && endpoints[0] == htonl(INADDR_LOOPBACK));
    const auto message = Message(Options, OptionsData{1, 1, 16});
    // A split header/payload is reassembled exactly once, without losing bytes.
    assert(send(writer->handle, reinterpret_cast<const char*>(message->data()), 7, 0) == 7);
    uint64_t bytes = 0;
    unsigned callbacks = 0;
    assert(reader->Receive(
        [&](Kind, auto) {
            ++callbacks;
            return true;
        },
        bytes));
    assert(!callbacks);
    assert(send(writer->handle, reinterpret_cast<const char*>(message->data() + 7), int(message->size() - 7),
                0) == int(message->size() - 7));
    for (int n = 0; n < 200 && !callbacks; ++n) {
        assert(reader->Receive(
            [&](Kind kind, auto payload) {
                OptionsData d;
                assert(kind == Options && Read(payload, d) && d.ready == 1);
                ++callbacks;
                return true;
            },
            bytes));
        Sleep(1);
    }
    assert(callbacks == 1 && bytes == message->size());
    StatusData notice;
    Text(notice.message, "Capture failed\nwith a control character");
    assert(ValidText(notice.message) && std::string(notice.message).find('\n') == std::string::npos);
    memset(notice.message, 'x', sizeof(notice.message));
    assert(!ValidText(notice.message));
    notice.message[sizeof(notice.message) - 1] = 0;
    assert(ValidText(notice.message));
    Frame frame;
    frame.stream = frame.sequence = 1;
    frame.width = 1920;
    frame.height = 1080;
    frame.key = true;
    frame.bytes.resize(1024);
    assert(writer->Queue(Picture(frame)) && writer->Queue(Picture(frame)) && !writer->Queue(Picture(frame)));
    assert(writer->output.size() == 2); // Predictive frames cannot silently disappear from a live stream.
    assert(Picture(std::span<const uint8_t>(Picture(frame)->data() + sizeof(Header),
                                            sizeof(FrameData) + frame.bytes.size())));
    frame.width = 99999;
    assert(!ValidFrame(frame));
    auto [slowWriter, slowReader] = pair();
    frame.width = 1920;
    frame.bytes.resize(MaxFrameBytes);
    assert(slowWriter->Queue(Picture(frame)) && slowWriter->Queue(Picture(frame)));
    size_t credit = MaxFrameBytes * 2;
    uint64_t sent = 0;
    assert(slowWriter->Send(sent, credit) && !slowWriter->output.empty());
    slowWriter->sending = Now() - 2001;
    credit = MaxFrameBytes * 2;
    assert(!slowWriter->Send(sent, credit)); // A blocked socket cannot retain a partial frame indefinitely.
    Client unacknowledged;
    Connection connection;
    connection.credential = {1, 2, 3};
    connection.port = ntohs(address.sin_port);
    connection.addresses = {htonl(INADDR_LOOPBACK)};
    connection.ready = true;
    unacknowledged.Update(connection);
    Socket silentHost;
    silentHost.handle = accept(listener, nullptr, nullptr);
    assert(silentHost.handle != INVALID_SOCKET);
    const auto deadline = Now() + 4000;
    while (Now() < deadline && unacknowledged.Current().error.empty()) {
        assert(!unacknowledged.Current().connected);
        Sleep(1);
    }
    assert(!unacknowledged.Current().error.empty() && !unacknowledged.Current().connected);
    unacknowledged.Stop();
    Close(listener);
    std::cout << "PASS: malformed/oversized/unauthenticated wire headers, split TCP messages, video bounds "
                 "and bounded congestion queues\n";
}
