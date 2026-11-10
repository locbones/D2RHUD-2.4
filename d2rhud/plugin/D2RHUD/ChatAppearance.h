#pragma once
#include <algorithm>
#include <cstdint>

namespace HudChatAppearance {
struct Settings {
    bool enabled = false;
    int size = 100;
    int background = 100;
    int height = 100;
    bool operator==(const Settings&) const = default;
};
inline Settings Normalize(Settings value) {
    value.size = std::clamp(value.size, 60, 150);
    value.background = std::clamp(value.background, 0, 100);
    value.height = std::clamp(value.height, 25, 100);
    return value;
}
inline uint32_t Pack(Settings value) {
    value = Normalize(value);
    return uint32_t(value.enabled) | (uint32_t(value.size) << 1) |
        (uint32_t(value.background) << 9) | (uint32_t(value.height) << 16);
}
inline Settings Unpack(uint32_t value) {
    return Normalize({bool(value & 1), int((value >> 1) & 255),
        int((value >> 9) & 127), int((value >> 16) & 127)});
}
// Native callbacks read one immutable snapshot. No files, font reloads or
// chat-manager calls run in a paint/update callback.
void Configure(Settings value);
const char* Status();
}
