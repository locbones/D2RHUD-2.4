#include "ChatNotifications.h"
#include "HudSoundLibrary.h"
#include "../../ProcessLifetime.h"
#include "../../Pattern.h"
#include "../../d2/D2Enums.h"
#include "../../d2/Reclass.h"
#include <Windows.h>
#include <algorithm>
#include <cctype>
#include <cstring>

D2UnitStrc* GetClientPlayerUnit();

namespace HudChat {
namespace {
std::string_view WithoutColors(std::string_view text) {
    while (!text.empty()) {
        if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xff && text[1] == 'c') text.remove_prefix(3);
        else if (text.size() >= 4 && static_cast<unsigned char>(text[0]) == 0xc3 &&
                 static_cast<unsigned char>(text[1]) == 0xbf && text[2] == 'c') text.remove_prefix(4);
        else break;
    }
    return text;
}
bool Equal(std::string_view left, std::string_view right) {
    if (left.size() != right.size()) return false;
    for (size_t i = 0; i < left.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(left[i])) != std::tolower(static_cast<unsigned char>(right[i]))) return false;
    return true;
}
bool Phrase(std::string_view text, std::string_view phrase) {
    if (text.size() < phrase.size() || !Equal(text.substr(0, phrase.size()), phrase)) return false;
    if (text.size() == phrase.size()) return true;
    const auto next = text[phrase.size()];
    return next == ' ' || next == '.' || next == '!' || next == '?';
}
template<size_t N> void Copy(std::array<char, N>& output, std::string_view input) {
    const auto length = (std::min)(input.size(), N - 1);
    std::memcpy(output.data(), input.data(), length);
    output[length] = '\0';
}
bool Read(const void* source, void* output, size_t size) {
    const auto saved = GetLastError();
    SIZE_T count = 0;
    const bool success = source && ReadProcessMemory(GetCurrentProcess(), source, output, size, &count) && count == size;
    SetLastError(saved);
    return success;
}
Identity LocalIdentity() {
    Identity identity;
    D2UnitStrc unit{};
    if (Read(GetClientPlayerUnit(), &unit, sizeof(unit)) && unit.dwUnitType == UNIT_PLAYER) {
        identity.type = unit.dwUnitType; identity.id = unit.dwUnitId;
        char name[sizeof(D2PlayerDataStrc::szName)]{};
        if (unit.pPlayerData && Read(unit.pPlayerData->szName, name, sizeof(name)))
            identity.name.assign(name, strnlen_s(name, sizeof(name)));
    }
    if (identity.name.empty()) {
        int32_t index = -1;
        const auto slot = reinterpret_cast<const void*>(Pattern::Address(0x1d442d8));
        if (!Read(slot, &index, sizeof(index)) || index < 0 || index >= 8) return identity;
        const auto clients = reinterpret_cast<D2ClientStrc* const*>(Pattern::Address(0x1d637f0));
        D2ClientStrc* client = nullptr;
        char name[sizeof(D2ClientStrc::szName)]{};
        if (Read(clients + index, &client, sizeof(client)) && client && Read(client->szName, name, sizeof(name)))
            identity.name.assign(name, strnlen_s(name, sizeof(name)));
    }
    return identity;
}
struct Runtime {
    Inbox inbox;
    Identity identity;
    uint64_t session = 0, refreshed = 0;
    bool enabled = false, hasRefresh = false;
};
Runtime& State() { return HudLifetime::State<Runtime>(); }
}

bool IsSystemFeedback(std::string_view message) {
    const auto text = WithoutColors(message);
    if (text.empty()) return true;
    constexpr std::string_view phrases[]{
        "Out of mana", "Not enough mana", "Not enough gold", "Not here", "Not in town",
        "I can't carry", "I can't get there", "I can't do that", "I cannot carry", "I cannot get there", "I cannot do that",
        "You can't carry", "You can't do that", "You cannot carry", "You cannot do that", "Cannot carry", "Cannot equip",
        "Need more gold", "Need more mana", "Requires level", "Locked", "No room", "No space", "Inventory is full",
        "Imbued unbalanced", "That is not yours", "That is owned", "This is not yours", "Must identify"
    };
    return std::any_of(std::begin(phrases), std::end(phrases), [&](auto phrase) { return Phrase(text, phrase); });
}
void Inbox::SetContext(uint64_t session, bool enabled, Identity identity) {
    std::lock_guard lock(mutex_);
    enabled = enabled && session != 0;
    if (session != session_.load() || enabled != enabled_.load()) {
        first_ = count_ = 0; pending_.store(0); alerted_ = false;
    }
    identity_ = std::move(identity);
    session_.store(session, std::memory_order_release);
    enabled_.store(enabled, std::memory_order_release);
}
bool Inbox::Submit(std::string_view sender, std::string_view message, uint32_t type, uint32_t id, bool hasUnit) {
    // Disabled callbacks do not inspect the native message buffers at all.
    if (!Enabled()) return false;
    const auto session = session_.load(std::memory_order_acquire);
    if (!session || sender.empty() || message.empty() || IsSystemFeedback(message)) return false;
    Message value;
    Copy(value.sender, WithoutColors(sender)); Copy(value.text, message);
    value.type = type; value.id = id; value.hasUnit = hasUnit;
    std::lock_guard lock(mutex_);
    if (!enabled_.load() || session != session_.load()) return false;
    if (count_ == messages_.size()) { first_ = (first_ + 1) % messages_.size(); --count_; }
    messages_[(first_ + count_) % messages_.size()] = value;
    ++count_; pending_.store(count_, std::memory_order_release);
    return true;
}
bool Inbox::TakeAlert(uint64_t now) {
    if (!pending_.load(std::memory_order_acquire)) return false;
    std::lock_guard lock(mutex_);
    bool eligible = false;
    for (size_t i = 0; i < count_; ++i) {
        const auto& value = messages_[(first_ + i) % messages_.size()];
        const std::string_view sender(value.sender.data());
        if (sender.empty() || IsSystemFeedback(value.text.data())) continue;
        if (value.hasUnit) {
            if (value.type != UNIT_PLAYER || value.id == UINT32_MAX) continue;
            if (identity_.id != UINT32_MAX && value.type == identity_.type && value.id == identity_.id) continue;
        }
        if (!identity_.name.empty()) {
            if (Equal(sender, WithoutColors(identity_.name))) continue;
            eligible = true;
        } else if (value.hasUnit && identity_.id != UINT32_MAX) eligible = true;
    }
    first_ = count_ = 0; pending_.store(0, std::memory_order_release);
    if (!eligible || !enabled_.load()) return false;
    if (alerted_ && now >= lastAlert_ && now - lastAlert_ < 350) return false;
    lastAlert_ = now; alerted_ = true;
    return true;
}
bool Enabled() { return State().inbox.Enabled(); }
void Observe(std::string_view sender, std::string_view message, uint32_t type, uint32_t id, bool hasUnit) {
    State().inbox.Submit(sender, message, type, id, hasUnit);
}
void Tick(uint64_t session, bool inGame, bool enabled, const std::string& soundPath, int volumePercent) {
    auto& state = State();
    // Joiners must not depend on a host/session token being populated. Native
    // in-game status supplies the enter/leave boundary even when that token is zero.
    session = inGame ? (session ? session : UINT64_MAX) : 0;
    const auto now = GetTickCount64();
    const bool changed = session != state.session || enabled != state.enabled;
    if (changed) { state.identity = {}; state.hasRefresh = false; state.session = session; state.enabled = enabled; }
    const bool refresh = session && enabled && (!state.hasRefresh || now < state.refreshed || now - state.refreshed >= 500);
    if (refresh) { state.identity = LocalIdentity(); state.refreshed = now; state.hasRefresh = true; }
    if (changed || refresh) state.inbox.SetContext(session, enabled, state.identity);
    if (state.inbox.TakeAlert(now)) {
        const auto path = HudSounds::Resolve(soundPath);
        HudSounds::PlayNotification(path, volumePercent);
    }
}
}
