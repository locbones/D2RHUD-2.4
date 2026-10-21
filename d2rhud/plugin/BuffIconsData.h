#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace BuffIcons {
// A redundant REX prefix (40) is present in the actual 1.4.71510 function.
// Disassemblers can omit it when displaying the first PUSH instruction.
inline bool IsSkillBonusFunction(const uint8_t* code, size_t size) {
    if (!code || !size) return false;
    if (code[0] == 0x40) { ++code; --size; }
    constexpr uint8_t expected[] = {0x56, 0x41, 0x54, 0x41, 0x56, 0x48, 0x83, 0xec, 0x50};
    return size >= sizeof(expected) && !std::memcmp(code, expected, sizeof(expected));
}

inline int CachedSkillLevel(int base, int bonus) {
    return static_cast<int>(std::clamp<int64_t>(int64_t(base) + bonus, 0, 32767));
}

// D2R 2.4 stores the timeout as a float, in simulation frames (25 Hz).
// Server lists contain an absolute frame; client lists contain frames remaining.
inline float RemainingSeconds(float timeout, bool timed, bool server, uint32_t frame) {
    if (!timed) return -1.0f;
    if (!std::isfinite(timeout)) return 0.0f;
    return (std::max)(0.0f, timeout - (server ? static_cast<float>(frame) : 0.0f)) / 25.0f;
}

// Automatic, whole seconds, decimal seconds, m:ss, h:mm:ss.
inline std::string TimerText(float seconds, int format = 0) {
    if (seconds < 0) return "";
    if (!std::isfinite(seconds) || seconds > 2147483000.0f) return "?";
    const int whole = static_cast<int>(std::ceil(seconds));
    if (format == 1) return std::to_string(whole) + "s";
    if (format == 2) {
        const int64_t tenths = static_cast<int64_t>(std::ceil(seconds * 10.0f));
        return std::to_string(tenths / 10) + "." + std::to_string(tenths % 10) + "s";
    }
    auto twoDigits = [](int value) { return (value < 10 ? "0" : "") + std::to_string(value); };
    if (format == 4) return std::to_string(whole / 3600) + ":" + twoDigits((whole / 60) % 60) + ":" + twoDigits(whole % 60);
    if (format != 3 && whole < 60) return std::to_string(whole);
    const int remainder = whole % 60;
    return std::to_string(whole / 60) + ":" + (remainder < 10 ? "0" : "") + std::to_string(remainder);
}

struct SpriteAtlas {
    int width = 0, height = 0, frameWidth = 0, frames = 0;
    // Preserve BC3 on the GPU instead of expanding large skill atlases to RGBA.
    bool bc3 = false;
    std::vector<uint8_t> pixels;
};

inline bool ReadSprite(const std::vector<uint8_t>& bytes, SpriteAtlas& out) {
    out = {};
    if (bytes.size() < 40 || std::memcmp(bytes.data(), "SpA1", 4)) return false;
    auto u16 = [&](size_t p) { return uint32_t(bytes[p]) | (uint32_t(bytes[p + 1]) << 8); };
    auto u32 = [&](size_t p) { return u16(p) | (u16(p + 2) << 16); };
    const uint32_t format = u16(4), width = u32(8), height = u32(12), frames = u32(20), frameWidth = u16(6);
    if ((format != 31 && format != 61) || !width || !height || width > 16384 || height > 16384 ||
        !frames || frames > width || width % frames || !frameWidth || frameWidth > width / frames) return false;
    const uint64_t size = format == 31 ? uint64_t(width) * height * 4 : uint64_t((width + 3) / 4) * ((height + 3) / 4) * 16;
    if (size > 64 * 1024 * 1024 || size > bytes.size() - 40) return false;
    out.width = width; out.height = height; out.frameWidth = frameWidth; out.frames = frames; out.bc3 = format == 61;
    out.pixels.assign(bytes.begin() + 40, bytes.begin() + 40 + static_cast<size_t>(size));
    return true;
}
}
