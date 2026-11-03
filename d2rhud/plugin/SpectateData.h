#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Spectate::Data {
inline constexpr uint64_t Magic = 0xDEADBEEFDEADBEEFULL;
inline constexpr uint8_t Operation = 10, Version = 4;
inline constexpr size_t Payload = 160, MaxBytes = 8 * 1024 * 1024, MaxPlayers = 8;
inline constexpr int MinFps = 2, MaxFps = 60, MinWidth = 320, MaxWidth = 3840, MaxHeight = 2160;
inline constexpr int MinQuality = 30, MaxQuality = 100, MinBandwidth = 1, MaxBandwidth = 128;
inline constexpr size_t PendingPackets = 64;
inline size_t Bandwidth(int megabits) {
    return size_t(std::clamp(megabits, MinBandwidth, MaxBandwidth)) * 1000000 / 8;
}
enum Action : uint8_t { Hello, Advertise, Peer, Watch, Stop, Poll, Capture, Chunk, Failure, ChannelGrant };
enum Flags : uint32_t { Enabled = 1, Sharing = 2, Ready = 4 };
#pragma pack(push, 1)
struct Packet {
    uint8_t opcode = 0x2b;
    uint64_t magic = Magic;
    uint8_t operation = Operation, version = Version, action = Hello;
    uint32_t recipient = 0, seed = 0, epoch = 0, target = 0, request = 0, frame = 0, total = 0, offset = 0;
    uint16_t length = 0;
    std::array<uint8_t, Payload> bytes{};
};
struct CommandPacket {
    Packet packet;
    std::array<uint8_t, 4> padding{};
};
#pragma pack(pop)
static_assert(sizeof(Packet) == 206 && sizeof(CommandPacket) == 210);
// Viewing preferences are negotiated on the dedicated authenticated channel.
struct Profile {
    int fps = 6, width = 640, quality = 55;
    bool operator==(const Profile&) const = default;
};
inline bool Valid(const Packet& p) {
    return p.magic == Magic && p.operation == Operation && p.version == Version && p.action <= ChannelGrant &&
           p.length <= Payload &&
           std::all_of(p.bytes.begin() + p.length, p.bytes.end(), [](uint8_t b) { return !b; });
}
inline bool Name(std::string_view name) {
    return !name.empty() && name.size() <= 16 &&
           std::none_of(name.begin(), name.end(), [](unsigned char c) { return c < 32 || c == 127; });
}
struct Player {
    uint32_t id = 0;
    std::string name;
};
struct Delivery {
    uint32_t recipient = 0;
    Packet packet;
};

// The native channel carries presence and consent only. Video never enters the
// game's small packet queue; the host grants a separate authenticated channel.
class Host {
    struct Client {
        std::string name;
        uint32_t flags = 0;
        uint64_t seen = 0;
    };
    std::map<uint32_t, Client> clients_;
    uint32_t seed_, epoch_;
    uint64_t hello_ = 0;

  public:
    Host(uint32_t seed, uint32_t epoch) : seed_(seed), epoch_(epoch) {}
    uint32_t Flags(uint32_t id, uint64_t now) const {
        const auto at = clients_.find(id);
        return at != clients_.end() && now - at->second.seen <= 6000 ? at->second.flags : 0;
    }
    void Players(const std::vector<Player>& players) {
        std::map<uint32_t, bool> live;
        for (const auto& p : players)
            if (live.size() < MaxPlayers && Name(p.name)) {
                live[p.id] = true;
                clients_[p.id].name = p.name;
            }
        std::erase_if(clients_, [&](const auto& p) { return !live.contains(p.first); });
    }
    void Receive(uint32_t sender, const Packet& p, uint64_t now) {
        const auto at = clients_.find(sender);
        if (at == clients_.end() || !Valid(p) || p.opcode != 3 || p.recipient != sender || p.seed != seed_ ||
            p.epoch != epoch_ || p.action != Advertise || p.length || p.request || p.frame || p.offset ||
            p.total > 7)
            return;
        at->second.flags = p.total;
        at->second.seen = now;
    }
    std::vector<Delivery> Tick(uint64_t now) {
        std::vector<Delivery> out;
        if (hello_ && now - hello_ < 2000)
            return out;
        hello_ = now;
        for (const auto& [id, c] : clients_) {
            Packet hello;
            hello.recipient = id;
            hello.seed = seed_;
            hello.epoch = epoch_;
            out.push_back({id, hello});
            if (!(Flags(id, now) & Enabled))
                continue;
            for (const auto& [otherId, other] : clients_)
                if (otherId != id) {
                    auto p = hello;
                    p.action = Peer;
                    p.target = otherId;
                    p.total = Flags(otherId, now);
                    p.length = uint16_t(other.name.size());
                    memcpy(p.bytes.data(), other.name.data(), p.length);
                    out.push_back({id, p});
                }
        }
        return out;
    }
};
} // namespace Spectate::Data
