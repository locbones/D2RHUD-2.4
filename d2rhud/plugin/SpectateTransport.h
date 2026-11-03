#pragma once
#include "SpectateRelay.h"
#include <deque>

namespace Spectate::Stream {
struct Connection {
    Credential credential;
    std::vector<uint32_t>
        addresses; // IPv4 addresses from this process's game connection, network byte order.
    uint16_t port = 6113;
    bool ready = false, sharing = false;
    unsigned bandwidthMbps = 16;
    std::optional<Watch> watch;
    bool operator==(const Connection&) const = default;
};
struct Received {
    uint32_t target = 0, request = 0;
    std::shared_ptr<const Frame> frame;
};
struct Status {
    bool connected = false;
    Demand demand;
    std::string error;
    std::string notice;
    uint64_t sent = 0, received = 0;
};
class Server {
    struct State;
    std::unique_ptr<State> state_;

  public:
    Server();
    ~Server();
    void Update(Authority);
    void Stop();
    std::string Error() const;
};
class Client {
    struct State;
    std::unique_ptr<State> state_;

  public:
    Client();
    ~Client();
    void Update(std::optional<Connection>);
    void Stop();
    Status Current() const;
    bool Publish(std::shared_ptr<const Frame>);
    std::vector<Received> Drain();
    void Resync();
    void ReportCaptureError(std::string error);
};
Server& Host();
Client& Local();
std::vector<uint32_t> GameAddresses(bool hosting, uint16_t gamePort = 4000);
} // namespace Spectate::Stream
