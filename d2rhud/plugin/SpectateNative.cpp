#include "../ProcessLifetime.h"
#include "../d2/D2Ptrs.h"
#include "Spectate.h"
#include "SpectatePartyLayout.h"
#include "SpectateTransport.h"
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#include <atomic>
#include <cmath>
#include <mutex>
#include <set>

namespace Spectate {
namespace {
template <class T> T Fn(uint32_t rva) { return reinterpret_cast<T>(Pattern::Address(rva)); }
template <class T> bool Read(const void* p, T& value) {
    SIZE_T n = 0;
    return p && ReadProcessMemory(GetCurrentProcess(), p, &value, sizeof(value), &n) && n == sizeof(value);
}
template <class T> T Field(const void* p, size_t offset = 0) {
    T value{};
    if (p)
        Read(static_cast<const uint8_t*>(p) + offset, value);
    return value;
}
bool CodeSupported() {
    constexpr std::pair<uint32_t, uint64_t> signatures[]{{0x6b050, 0x90869c3b9cbd1b2fULL},
                                                         {0x422ac0, 0xfd9ed239098cb330ULL},
                                                         {0x597320, 0xb095715269b79a39ULL},
                                                         {0x576f00, 0x1bc415ed9dfba407ULL}};
    auto hash = [](const auto& bytes) {
        uint64_t value = 14695981039346656037ULL;
        for (auto b : bytes)
            value = (value ^ b) * 1099511628211ULL;
        return value;
    };
    for (const auto& [rva, expected] : signatures) {
        std::array<uint8_t, 32> bytes{};
        if (!Read(Fn<void*>(rva), bytes) || hash(bytes) != expected)
            return false;
    }
    // Marketplace chat detours the first five bytes of this same queue. Check
    // its unchanged body as well as the original prologue / Detours jump, so
    // startup order cannot silently disable Spectate in a supported game.
    std::array<uint8_t, 32> queue{};
    constexpr std::array<uint8_t, 5> prologue{0x48, 0x89, 0x5c, 0x24, 0x18};
    if (!Read(Fn<void*>(0x10dce0), queue) ||
        (queue[0] != 0xe9 && !std::equal(prologue.begin(), prologue.end(), queue.begin())))
        return false;
    return hash(std::span(queue).subspan(5)) == 0xc7568e3151587dc8ULL;
}
bool Supported() {
    static const bool supported = CodeSupported();
    return supported;
}
std::atomic<bool> available{false};
struct Pending {
    uint32_t sender;
    Data::Packet packet;
};
struct Server {
    Data::Host host;
    std::vector<Pending> pending;
    uint64_t lastTick = 0;
    uint32_t seed, epoch;
    std::map<uint32_t, Stream::Token> tokens;
    Server(uint32_t seedValue, uint32_t epochValue)
        : host(seedValue, epochValue), seed(seedValue), epoch(epochValue) {}
};
struct State {
    std::mutex mutex;
    std::map<D2GameStrc*, Server> servers;
    uint32_t serial = uint32_t(GetTickCount64());
};
State& Runtime() { return HudLifetime::State<State>(); }
bool Client(D2ClientStrc* pointer, D2ClientStrc& value) {
    constexpr size_t size = offsetof(D2ClientStrc, pNext) + sizeof(D2ClientStrc*);
    SIZE_T n = 0;
    return pointer && ReadProcessMemory(GetCurrentProcess(), pointer, &value, size, &n) && n == size;
}
D2ClientStrc* Owner(D2GameStrc* game, D2UnitStrc* player) {
    D2UnitStrc u{};
    D2ClientStrc c{};
    if (!Read(player, u) || u.dwUnitType || !u.pPlayerData)
        return nullptr;
    const auto pointer = Field<D2ClientStrc*>(u.pPlayerData, 0xf0);
    return Client(pointer, c) && c.pGame == game && c.pPlayer == player && c.dwUnitGUID == u.dwUnitId &&
                   c.dwClientState == 4
               ? pointer
               : nullptr;
}
std::map<uint32_t, D2ClientStrc*> Players(D2GameStrc* game, std::vector<Data::Player>& roster) {
    std::map<uint32_t, D2ClientStrc*> result;
    std::set<D2ClientStrc*> seen;
    auto pointer = Field<D2ClientStrc*>(game, 0x150);
    while (pointer && seen.size() < 128 && result.size() < Data::MaxPlayers) {
        D2ClientStrc c{};
        if (!seen.insert(pointer).second || !Client(pointer, c))
            break;
        if (c.pGame == game && c.dwClientState == 4 && c.pPlayer && Owner(game, c.pPlayer) == pointer) {
            std::string name(c.szName, strnlen_s(c.szName, 16));
            if (Data::Name(name)) {
                result[c.dwUnitGUID] = pointer;
                roster.push_back({c.dwUnitGUID, std::move(name)});
            }
        }
        pointer = c.pNext;
    }
    return result;
}
void Send(D2ClientStrc* client, const Data::Packet& packet) {
    const auto id = Field<uint32_t>(client);
    void* range[]{const_cast<Data::Packet*>(&packet),
                  const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(&packet)) + sizeof(packet)};
    Fn<void(__fastcall*)(void*, uint32_t, void**)>(0x422ac0)(Fn<void*>(0x18682b0), id, range);
}
std::string WidgetName(void* widget) {
    const auto p = Field<const char*>(widget, 8);
    std::string result;
    for (size_t i = 0; p && i < 64; ++i) {
        char c = 0;
        if (!Read(p + i, c) || !c)
            break;
        result += c;
    }
    return result;
}
float Scale(void* widget) {
    float scale = 1;
    for (unsigned n = 0; widget && n < 16; ++n, widget = Field<void*>(widget, 0x30)) {
        const auto next = Field<float>(widget, 0x80);
        if (!std::isfinite(next) || next < .001f || next > 10)
            return 0;
        scale *= next;
    }
    return std::isfinite(scale) ? scale : 0;
}
std::string PlayerLabel(void* widget) {
    // Ordinary native TextBoxWidget's owned blz::string. Pointer/length are
    // copied, never retained by the overlay or used as another player's ID.
    const auto table = Field<uintptr_t>(widget);
    if (table != Pattern::Address(0x15acb98))
        return {};
    const auto size = Field<uint64_t>(widget, 0x90);
    const auto p = Field<const char*>(widget, 0x88);
    if (!p || !size || size > 16)
        return {};
    std::array<char, 16> name{};
    SIZE_T n = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), p, name.data(), size, &n) || n != size)
        return {};
    std::string value(name.data(), size);
    return Data::Name(value) ? value : std::string{};
}
using WidgetRect = std::array<int32_t, 4>;
std::map<std::string, void*> RowControls(void* row) {
    std::map<std::string, void*> controls;
    const auto children = Field<void**>(row, 0x58);
    const auto count = Field<uint64_t>(row, 0x60);
    if (!children || count > 128)
        return controls;
    for (size_t i = 0; i < count; ++i) {
        auto child = Field<void*>(children, i * sizeof(void*));
        if (child && Field<void*>(child, 0x30) == row)
            controls[WidgetName(child)] = child;
    }
    return controls;
}
} // namespace
void SetAvailable(bool value) {
    available = value;
    if (!value)
        Stream::Host().Update({});
}
bool Command(D2GameStrc* game, D2UnitStrc* player, const uint8_t* bytes, size_t size) {
    if (!bytes || size < 10 || bytes[0] != 3 || Field<uint64_t>(bytes, 1) != Data::Magic ||
        bytes[9] != Data::Operation)
        return false;
    if (!available || size != sizeof(Data::CommandPacket) || !Supported() || !Owner(game, player) ||
        !Native::TcpGame())
        return true;
    Data::CommandPacket c;
    memcpy(&c, bytes, sizeof(c));
    if (!Data::Valid(c.packet) || c.packet.action != Data::Advertise ||
        std::any_of(c.padding.begin(), c.padding.end(), [](uint8_t b) { return b != 0; }))
        return true;
    auto& s = Runtime();
    std::lock_guard lock(s.mutex);
    const auto at = s.servers.find(game);
    if (at == s.servers.end() || at->second.pending.size() >= Data::PendingPackets)
        return true;
    at->second.pending.push_back({player->dwUnitId, c.packet});
    return true;
}
void Tick(D2GameStrc* game) {
    if (!available || !game || !Supported() || !Native::TcpGame())
        return;
    // No offline video handshake. TCP/IP's one-player host is supported too.
    if (Field<uint32_t>(game, offsetof(D2GameStrc, nClients)) < 2 &&
        Field<uint8_t>(Fn<void*>(0x18882e8)) != 2)
        return;
    {
        auto& s = Runtime();
        std::lock_guard lock(s.mutex);
        const auto at = s.servers.find(game);
        if (at != s.servers.end() && at->second.pending.empty() &&
            GetTickCount64() - at->second.lastTick < 250)
            return;
    }
    const auto settings = Configuration();
    std::vector<Data::Player> roster;
    const auto clients = Players(game, roster);
    std::vector<Data::Delivery> deliveries;
    {
        auto& s = Runtime();
        std::lock_guard lock(s.mutex);
        auto at = s.servers.find(game);
        if (at == s.servers.end()) {
            if (!++s.serial)
                ++s.serial;
            at = s.servers.try_emplace(game, uint32_t(game->dwInitSeed), s.serial).first;
        }
        auto& server = at->second;
        server.host.Players(roster);
        const auto now = GetTickCount64();
        server.lastTick = now;
        for (const auto& c : server.pending)
            server.host.Receive(c.sender, c.packet, now);
        server.pending.clear();
        deliveries = server.host.Tick(now);
        Stream::Authority authority;
        authority.seed = server.seed;
        authority.epoch = server.epoch;
        authority.port = uint16_t(settings.videoPort);
        authority.bandwidthMbps = settings.bandwidthMbps;
        authority.refreshed = now;
        std::erase_if(server.tokens, [&](const auto& p) { return !clients.contains(p.first); });
        for (const auto& player : roster) {
            auto token = server.tokens.find(player.id);
            if (token == server.tokens.end()) {
                Stream::Token random{};
                if (BCryptGenRandom(nullptr, random.data(), ULONG(random.size()),
                                    BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
                    continue;
                token = server.tokens.emplace(player.id, random).first;
            }
            const auto flags = server.host.Flags(player.id, now);
            authority.members.push_back({player.id, token->second, bool(flags & Data::Enabled),
                                         bool(flags & Data::Sharing), bool(flags & Data::Ready)});
        }
        // Grants travel only to the owning native client, never to its peers.
        const auto helloDeliveries = deliveries.size();
        for (size_t n = 0; n < helloDeliveries; ++n) {
            const auto& hello = deliveries[n];
            const auto token = server.tokens.find(hello.recipient);
            if (hello.packet.action != Data::Hello || token == server.tokens.end() ||
                !(server.host.Flags(hello.recipient, now) & Data::Enabled))
                continue;
            auto grant = hello.packet;
            grant.action = Data::ChannelGrant;
            grant.length = 34;
            grant.bytes[0] = uint8_t(settings.videoPort);
            grant.bytes[1] = uint8_t(settings.videoPort >> 8);
            memcpy(grant.bytes.data() + 2, token->second.data(), token->second.size());
            deliveries.push_back({hello.recipient, grant});
        }
        Stream::Host().Update(std::move(authority));
    }
    for (const auto& delivery : deliveries)
        if (const auto at = clients.find(delivery.recipient); at != clients.end())
            Send(at->second, delivery.packet);
}
void Destroyed(D2GameStrc* game) {
    auto& s = Runtime();
    std::lock_guard lock(s.mutex);
    s.servers.erase(game);
    Stream::Host().Update({});
}
namespace Native {
bool Hosting() { return Field<uint8_t>(Fn<void*>(0x18882e8)) == 2; }
uint16_t GamePort() {
    // 2.4 TCP/IP setup at C342A0 / C18D4F initializes this port to 4000.
    const auto port = Field<uint16_t>(Fn<void*>(0x1bf0bf6));
    return port ? port : 4000;
}
bool TcpGame() {
    // Native session kind: 0 = single player, 1 = online, 2 = TCP/IP.
    int mode = -1;
    return Read(Fn<void*>(0x1bfa31c), mode) && mode == 2;
}
const char* CompatibilityError() {
    if (!Supported())
        return "Spectate is unavailable: the native game functions did not match this HUD build.";
    if (!available)
        return "Spectate is unavailable: the client/server message hooks are not installed.";
    return nullptr;
}
bool CaptureReady() {
    uint8_t status = 0, loading = 1;
    return Read(Fn<void*>(0x1dc76f8), status) && status == 1 && Read(Fn<void*>(0x1cb410a), loading) &&
           !loading && TcpGame();
}
std::optional<uint32_t> LocalId() {
    if (!Supported())
        return {};
    const auto selector = Field<uint32_t>(Fn<void*>(0x1d442d8));
    if (selector >= 8)
        return {};
    D2UnitStrc unit{};
    if (!Read(Fn<D2UnitStrc*(__fastcall*)(uint32_t)>(0x6b050)(selector), unit) || unit.dwUnitType)
        return {};
    return unit.dwUnitId;
}
void Send(const Data::CommandPacket& packet) {
    if (Supported())
        Fn<void(__fastcall*)(void*, int)>(0x10dce0)(const_cast<Data::CommandPacket*>(&packet),
                                                    sizeof(packet));
}
std::vector<Eye> PartyEyes(float width, float height, const std::vector<Peer>& peers) {
    std::vector<Eye> result;
    if (!Supported() || width < 160 || height < 180 || peers.empty())
        return result;
    const auto root = Field<void*>(Fn<void*>(0x1d7c4e8));
    if (!root || !Field<uint8_t>(root, 0xb9))
        return result; // Native desktop HD panel.
    void* panel = nullptr;
    const auto panels = Field<void**>(root, 0x58);
    const auto panelCount = Field<uint64_t>(root, 0x60);
    if (!panels || panelCount > 512)
        return result;
    for (size_t i = 0; i < panelCount; ++i) {
        const auto child = Field<void*>(panels, i * sizeof(void*));
        if (WidgetName(child) == "PartyPanel") {
            panel = child;
            break;
        }
    }
    if (!panel || !Field<uint8_t>(panel, 0x51) || !Field<uint8_t>(panel, 0x50))
        return result;
    std::vector<void*> todo{panel};
    std::set<void*> seen;
    while (!todo.empty() && seen.size() < 1024) {
        auto widget = todo.back();
        todo.pop_back();
        if (!widget || !seen.insert(widget).second || !Field<uint8_t>(widget, 0x51))
            continue;
        if (WidgetName(widget) == "PlayerName") {
            const auto controls = RowControls(Field<void*>(widget, 0x30));
            const auto name = PlayerLabel(widget);
            const auto peer =
                std::find_if(peers.begin(), peers.end(), [&](const Peer& p) { return p.name == name; });
            if (peer != peers.end() && std::count_if(peers.begin(), peers.end(),
                                                     [&](const Peer& p) { return p.name == name; }) == 1) {
                const auto squelch = controls.find("SquelchPlayerButton");
                if (squelch == controls.end() || !Field<uint8_t>(squelch->second, 0x51))
                    continue;
                const auto button = squelch->second;
                const auto scale = Scale(button);
                const auto rect = Field<WidgetRect>(button, 0x70);
                if (!(scale > 0))
                    continue;
                uint64_t xy = 0;
                Fn<uint64_t*(__fastcall*)(void*, uint64_t*)>(0x576f00)(button, &xy);
                const float x = float(int32_t(xy)), y = float(int32_t(xy >> 32));
                float right = width, bottom = height;
                const auto panelRect = Field<WidgetRect>(panel, 0x70);
                const float panelScale = Scale(panel);
                if (panelRect[2] > 0 && panelRect[2] < 8192 && panelRect[3] > 0 && panelRect[3] < 8192 &&
                    panelScale > 0) {
                    uint64_t panelXY = 0;
                    Fn<uint64_t*(__fastcall*)(void*, uint64_t*)>(0x576f00)(panel, &panelXY);
                    right = (std::min)(right, float(int32_t(panelXY)) + panelRect[2] * panelScale);
                    bottom = (std::min)(bottom, float(int32_t(panelXY >> 32)) + panelRect[3] * panelScale);
                }
                if (const auto eye = PartyLayout::Place(x, y, rect[2], rect[3], scale, right, bottom))
                    result.push_back({peer->id, eye->x, eye->y, eye->size, peer->sharing, peer->name});
            }
        }
        const auto children = Field<void**>(widget, 0x58);
        const auto count = Field<uint64_t>(widget, 0x60);
        if (children && count <= 128)
            for (size_t i = 0; i < count; ++i)
                todo.push_back(Field<void*>(children, i * sizeof(void*)));
    }
    return result;
}
} // namespace Native
} // namespace Spectate
