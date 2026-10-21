#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <span>

namespace D3Whirlwind::Data {
constexpr uint64_t Magic = 0xDEADBEEFDEADBEEFULL;
constexpr uint8_t Operation = 4;
constexpr uint64_t LeaseMilliseconds = 400;
constexpr uint32_t ManaInterval = 25; // One native cast cost per second at 25 Hz.
constexpr uint64_t HeartbeatMilliseconds = 40;
#pragma pack(push, 1)
struct Command {
    uint8_t opcode = 3;
    uint64_t magic = Magic;
    uint8_t operation = Operation, version = 1, held = 0;
    uint32_t recipient = 0, session = 0;
    int32_t skill = -1, x = 0, y = 0;
    uint32_t sequence = 0;
};
#pragma pack(pop)
static_assert(sizeof(Command) == 36);
inline bool Ours(std::span<const uint8_t> bytes) {
    if (bytes.size() < 10 || bytes[0] != 3 || bytes[9] != Operation) return false;
    uint64_t magic = 0; std::memcpy(&magic, bytes.data() + 1, 8);
    return magic == Magic;
}
inline bool Valid(const Command& command) {
    return command.opcode == 3 && command.magic == Magic && command.operation == Operation &&
        command.version == 1 && command.held <= 1 && command.skill >= 0 && command.skill <= 32767 &&
        (!command.held || (command.x > 0 && command.x <= 65535 && command.y > 0 && command.y <= 65535));
}
struct Point { int x = 0, y = 0; };
inline bool ControlDue(Point cursor, Point sentCursor, uint64_t now, uint64_t sent, bool pending) {
    return pending || cursor.x != sentCursor.x || cursor.y != sentCursor.y ||
        (now >= sent && now - sent >= HeartbeatMilliseconds);
}
inline bool UsesButton(int action, int button) {
    // Native selected-skill actions. Forced attacks (0xb, 0x12, 0x2d) use
    // their own skill and must remain available while the channel is active.
    return (button == 1 && action >= 0x5 && action <= 0xa) ||
        (button == 2 && action >= 0xc && action <= 0x11);
}
inline bool Stationary(Point source, Point cursor) {
    return std::abs(cursor.x - source.x) <= 1 && std::abs(cursor.y - source.y) <= 1;
}
inline Point Target(Point source, Point cursor) {
    // Short native paths let the channel turn promptly and keep requests local.
    const double dx = double(cursor.x) - source.x, dy = double(cursor.y) - source.y;
    const double distance = std::sqrt(dx * dx + dy * dy);
    const double factor = distance > 12 ? 12 / distance : 1;
    return {std::clamp(source.x + int(std::lround(dx * factor)), 1, 65535),
        std::clamp(source.y + int(std::lround(dy * factor)), 1, 65535)};
}
struct Lease {
    uint64_t received = 0;
    uint32_t sequence = 0;
    bool accepted = false, stopped = false;
    Point cursor;
    bool Receive(const Command& command, uint64_t now) {
        if (stopped || (accepted && int32_t(command.sequence - sequence) <= 0)) return false;
        accepted = true; sequence = command.sequence; received = now;
        cursor = {command.x, command.y};
        if (!command.held) stopped = true;
        return true;
    }
    bool Active(uint64_t now) const {
        return accepted && !stopped && now >= received && now - received <= LeaseMilliseconds;
    }
};
}
