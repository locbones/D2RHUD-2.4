#pragma once
#include "SpectateH264.h"
#include <optional>
#include <set>

namespace Spectate::Stream {
using Token = std::array<uint8_t, 32>;
struct Member {
    uint32_t id = 0;
    Token token{};
    bool enabled = false, sharing = false, ready = false;
};
struct Authority {
    uint32_t seed = 0, epoch = 0;
    uint16_t port = 6113;
    int bandwidthMbps = 16;
    uint64_t refreshed = 0;
    std::vector<Member> members;
};
struct Credential {
    uint32_t id = 0, seed = 0, epoch = 0;
    Token token{};
    bool operator==(const Credential&) const = default;
};
struct Watch {
    uint32_t target = 0, request = 0;
    Data::Profile profile;
    bool operator==(const Watch&) const = default;
};
struct Demand {
    uint64_t stream = 0;
    Data::Profile profile;
    uint32_t bitrate = 0;
    bool operator==(const Demand&) const = default;
};
struct Control {
    uint32_t recipient = 0;
    Demand demand;
};
struct Recipient {
    uint32_t id = 0, request = 0;
};
inline bool ValidProfile(Data::Profile p) {
    return p.fps >= Data::MinFps && p.fps <= Data::MaxFps && p.width >= Data::MinWidth &&
           p.width <= Data::MaxWidth && p.quality >= Data::MinQuality && p.quality <= Data::MaxQuality;
}
inline bool ValidFrame(const Frame& f) {
    return f.stream && f.sequence && f.width >= 48 && f.width <= Data::MaxWidth && f.height >= 48 &&
           f.height <= Data::MaxHeight && !(f.width & 1) && !(f.height & 1) && !f.bytes.empty() &&
           f.bytes.size() <= MaxFrameBytes;
}

// Game-thread authority is a value snapshot. The video service has no unit
// pointers, inventory access, native packet calls, or ability to move a camera.
class Relay {
    struct Peer {
        Credential credential;
        bool sharing = true, ready = true;
        unsigned bandwidthMbps = 16;
        std::optional<Watch> watch;
        std::string captureError;
    };
    struct Source {
        Demand demand;
        std::vector<uint32_t> viewers;
        uint64_t sequence = 0;
        bool key = false;
        uint32_t ceiling = 128000000;
        size_t frameBytes = 0;
    };
    Authority authority_;
    std::map<uint32_t, Peer> peers_;
    std::map<uint32_t, Source> sources_;
    uint64_t serial_ = 0;
    const Member* Find(uint32_t id) const {
        const auto at = std::find_if(authority_.members.begin(), authority_.members.end(),
                                     [&](const Member& member) { return member.id == id; });
        return at == authority_.members.end() ? nullptr : &*at;
    }

  public:
    void Reset() {
        authority_ = {};
        peers_.clear();
        sources_.clear();
        // Keep stream IDs monotonic across port/session changes in this host.
    }
    bool Authorized(const Credential& c) const {
        const auto member = Find(c.id);
        if (!authority_.epoch || c.seed != authority_.seed || c.epoch != authority_.epoch || !member ||
            !member->enabled)
            return false;
        unsigned difference = 0;
        for (size_t n = 0; n < c.token.size(); ++n)
            difference |= c.token[n] ^ member->token[n];
        return difference == 0;
    }
    void Update(const Authority& value) {
        authority_ = value;
        std::erase_if(peers_, [&](const auto& p) { return !Authorized(p.second.credential); });
    }
    bool Connected(Credential c, unsigned bandwidthMbps) {
        if (!Authorized(c) || (peers_.size() >= Data::MaxPlayers && !peers_.contains(c.id)))
            return false;
        Peer p;
        p.credential = c;
        p.bandwidthMbps = unsigned(std::clamp(int(bandwidthMbps), Data::MinBandwidth, Data::MaxBandwidth));
        peers_[c.id] = std::move(p);
        for (auto& [id, source] : sources_)
            if (id == c.id ||
                std::find(source.viewers.begin(), source.viewers.end(), c.id) != source.viewers.end())
                source.demand.stream = 0;
        if (auto at = sources_.find(c.id); at != sources_.end())
            at->second.demand.stream = 0;
        return true;
    }
    void Disconnected(uint32_t id) { peers_.erase(id); }
    void CaptureError(uint32_t id, std::string error) {
        if (auto at = peers_.find(id); at != peers_.end())
            at->second.captureError = std::move(error);
    }
    std::string Notice(uint32_t viewer) const {
        const auto p = peers_.find(viewer);
        if (p == peers_.end() || !p->second.watch)
            return {};
        const auto target = p->second.watch->target;
        const auto member = Find(target);
        if (!member || !member->enabled)
            return "The player has left or disabled Spectate.";
        if (!member->sharing)
            return "The player is not allowing spectators.";
        if (!member->ready)
            return "Waiting for the player to finish loading...";
        const auto source = peers_.find(target);
        if (source == peers_.end())
            return "The player's video channel is not connected. Check their HUD version and host video "
                   "port.";
        if (!source->second.sharing)
            return "The player is not allowing spectators.";
        if (!source->second.ready)
            return "Waiting for the player to finish loading...";
        if (!source->second.captureError.empty())
            return "Player capture failed: " + source->second.captureError;
        return {};
    }
    void Options(uint32_t id, bool ready, bool sharing, unsigned bandwidthMbps) {
        if (auto p = peers_.find(id); p != peers_.end()) {
            p->second.ready = ready;
            p->second.sharing = sharing;
            p->second.bandwidthMbps =
                unsigned(std::clamp(int(bandwidthMbps), Data::MinBandwidth, Data::MaxBandwidth));
        }
    }
    bool Subscribe(uint32_t id, std::optional<Watch> watch) {
        const auto p = peers_.find(id);
        if (p == peers_.end())
            return false;
        if (watch && (!watch->request || watch->target == id || !ValidProfile(watch->profile)))
            return false;
        if (p->second.watch != watch && watch)
            if (auto source = sources_.find(watch->target); source != sources_.end())
                source->second.demand.stream = 0;
        p->second.watch = watch;
        return true;
    }
    void Congested(uint32_t source) {
        if (auto at = sources_.find(source); at != sources_.end()) {
            const auto measured = at->second.frameBytes * size_t(at->second.demand.profile.fps) * 8;
            const auto previous = measured ? (std::min)(size_t(at->second.demand.bitrate), measured)
                                           : size_t(at->second.demand.bitrate);
            at->second.ceiling = uint32_t((std::max)(size_t(128000), previous * 3 / 4));
            at->second.demand.stream = 0;
        }
    }
    std::vector<Control> Refresh() {
        std::vector<Control> controls;
        size_t subscriptions = 0;
        for (const auto& [id, p] : peers_)
            subscriptions += p.watch.has_value() && p.ready;
        for (const auto& member : authority_.members) {
            const auto peer = peers_.find(member.id);
            if (peer == peers_.end())
                continue;
            auto& source = sources_[member.id];
            std::vector<uint32_t> viewers;
            size_t viewerLimit = Data::Bandwidth(Data::MaxBandwidth) * 8;
            Data::Profile requested{Data::MinFps, Data::MinWidth, Data::MinQuality};
            if (member.enabled && member.sharing && member.ready && peer->second.ready &&
                peer->second.sharing)
                for (const auto& [viewerId, viewer] : peers_) {
                    const auto permission = Find(viewerId);
                    if (!viewer.watch || viewer.watch->target != member.id || !viewer.ready || !permission ||
                        !permission->ready)
                        continue;
                    viewers.push_back(viewerId);
                    viewerLimit =
                        (std::min)(viewerLimit, Data::Bandwidth(int(viewer.bandwidthMbps)) * 8 * 85 / 100);
                    requested.fps = (std::max)(requested.fps, viewer.watch->profile.fps);
                    requested.width = (std::max)(requested.width, viewer.watch->profile.width);
                    requested.quality = (std::max)(requested.quality, viewer.watch->profile.quality);
                }
            Demand desired;
            if (!viewers.empty()) {
                desired.profile = requested;
                const auto sourceLimit = Data::Bandwidth(int(peer->second.bandwidthMbps)) * 8 * 85 / 100;
                const auto relayLimit = Data::Bandwidth(authority_.bandwidthMbps) * 8 * 85 /
                                        (100 * (std::max)(size_t(1), subscriptions));
                desired.bitrate = uint32_t(
                    (std::max)(size_t(128000),
                               (std::min)({sourceLimit, relayLimit, viewerLimit, size_t(source.ceiling)})));
                desired.stream = source.demand.stream;
                if (!desired.stream || desired.profile != source.demand.profile ||
                    desired.bitrate != source.demand.bitrate || viewers != source.viewers) {
                    if (!++serial_)
                        ++serial_;
                    desired.stream = serial_;
                }
            }
            if (desired != source.demand) {
                source.demand = desired;
                source.sequence = 0;
                source.key = false;
                controls.push_back({member.id, desired});
            }
            source.viewers = std::move(viewers);
        }
        std::erase_if(sources_, [&](const auto& p) { return !peers_.contains(p.first); });
        return controls;
    }
    std::vector<Recipient> Publish(uint32_t id, const Frame& frame) {
        std::vector<Recipient> result;
        const auto source = sources_.find(id);
        const auto member = Find(id);
        const auto peer = peers_.find(id);
        if (!member || !member->enabled || !member->sharing || !member->ready || peer == peers_.end() ||
            !peer->second.sharing || !peer->second.ready || source == sources_.end() || !ValidFrame(frame))
            return result;
        auto& s = source->second;
        if (!s.demand.stream || frame.stream != s.demand.stream || frame.sequence <= s.sequence ||
            frame.width > unsigned(s.demand.profile.width) || (!s.key && !frame.key))
            return result;
        s.sequence = frame.sequence;
        s.key |= frame.key;
        s.frameBytes = s.frameBytes ? (s.frameBytes * 7 + frame.bytes.size()) / 8 : frame.bytes.size();
        for (auto viewer : s.viewers) {
            const auto p = peers_.find(viewer);
            if (p != peers_.end() && p->second.watch && p->second.watch->target == id)
                result.push_back({viewer, p->second.watch->request});
        }
        return result;
    }
};
} // namespace Spectate::Stream
