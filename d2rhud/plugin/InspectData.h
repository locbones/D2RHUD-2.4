#pragma once
#include <algorithm>
#include <array>
#include <compare>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Inspect transfers values, never native pointers or save/item serialization.
namespace Inspect::Data {
inline constexpr uint64_t Magic = 0xDEADBEEFDEADBEEFULL;
inline constexpr uint8_t Operation = 9, Version = 2;
inline constexpr size_t MaxBytes = 512 * 1024, MaxItems = 256, MaxStats = 128, Payload = 160;
inline constexpr size_t MaxTooltip = 16383;
enum Action : uint8_t { Hello, Request, Chunk, Failure };
#pragma pack(push, 1)
struct Packet {
    uint8_t opcode = 0x2b;
    uint64_t magic = Magic;
    uint8_t operation = Operation, version = Version, action = Hello;
    uint32_t recipient = 0, seed = 0, epoch = 0, request = 0, type = 0, target = 0, total = 0, offset = 0;
    uint16_t length = 0;
    std::array<uint8_t, Payload> bytes{};
};
struct CommandPacket {
    Packet packet;
    std::array<uint8_t, 4> padding{};
};
#pragma pack(pop)
static_assert(sizeof(Packet) == 0xce && sizeof(CommandPacket) == 0xd2);
struct Stat {
    uint16_t id = 0, parameter = 0;
    int32_t value = 0;
    auto operator<=>(const Stat&) const = default;
};
struct Item {
    uint32_t id = 0, flags = 0;
    int32_t index = -1;
    uint16_t level = 0;
    uint8_t quality = 2, picture = 0, slot = 0, x = 0, y = 0, width = 1, height = 1;
    int32_t quantity = 1;
    std::string code, name, tooltip;
    std::vector<Stat> stats;
    bool statsTruncated = false;
};
struct Snapshot {
    uint32_t type = 0, id = 0;
    std::string name, owner, designation;
    std::vector<Item> items;
};
inline bool Text(std::string_view text, size_t limit = 512) {
    if (text.size() > limit)
        return false;
    for (size_t i = 0; i < text.size();) {
        unsigned c = static_cast<uint8_t>(text[i++]);
        if (c < 32 || c == 127)
            return false;
        if (c < 128)
            continue;
        int n = c >= 0xc2 && c <= 0xdf ? 1 : c >= 0xe0 && c <= 0xef ? 2 : c >= 0xf0 && c <= 0xf4 ? 3 : -1;
        if (n < 0 || i + size_t(n) > text.size())
            return false;
        unsigned cp = c & ((1u << (6 - n)) - 1);
        while (n--) {
            const unsigned tail = uint8_t(text[i++]);
            if ((tail & 0xc0) != 0x80)
                return false;
            cp = (cp << 6) | (tail & 63);
        }
        if (cp < 128 || (cp < 2048 && c >= 0xe0) || (cp < 65536 && c >= 0xf0) || cp > 0x10ffff ||
            (cp >= 0xd800 && cp <= 0xdfff))
            return false;
    }
    return true;
}
struct Writer {
    std::vector<uint8_t> bytes;
    template <class T> void Put(T value) {
        const auto p = reinterpret_cast<const uint8_t*>(&value);
        bytes.insert(bytes.end(), p, p + sizeof(T));
    }
    void String(const std::string& text) {
        Put(uint16_t(text.size()));
        bytes.insert(bytes.end(), text.begin(), text.end());
    }
};
inline bool Tooltip(std::string_view text) {
    if (text.size() > MaxTooltip)
        return false;
    std::string flattened(text);
    for (auto& c : flattened)
        if (c == '\n' || c == '\r' || c == '\t')
            c = ' ';
    return Text(flattened, MaxTooltip);
}
struct Reader {
    std::span<const uint8_t> bytes;
    size_t offset = 0;
    bool valid = true;
    template <class T> T Get() {
        T value{};
        if (offset > bytes.size() || sizeof(T) > bytes.size() - offset) {
            valid = false;
            return value;
        }
        memcpy(&value, bytes.data() + offset, sizeof(T));
        offset += sizeof(T);
        return value;
    }
    std::string String(size_t limit = 512) {
        const size_t n = Get<uint16_t>();
        if (n > limit || offset > bytes.size() || n > bytes.size() - offset) {
            valid = false;
            return {};
        }
        std::string text(reinterpret_cast<const char*>(bytes.data() + offset), n);
        offset += n;
        if (!Text(text, limit))
            valid = false;
        return text;
    }
};
inline bool Valid(const Snapshot& s) {
    if (s.type > 1 || !Text(s.name) || s.name.empty() || !Text(s.owner) || !Text(s.designation) ||
        s.items.size() > MaxItems)
        return false;
    std::vector<uint32_t> ids;
    std::array<bool, 13> slots{};
    for (const auto& i : s.items) {
        if (i.code.empty() || !Text(i.code, 4) || !Text(i.name) || !Tooltip(i.tooltip) || i.slot > 12 ||
            i.x > 31 || i.y > 31 || !i.width || i.width > 16 || !i.height || i.height > 32 || i.quality > 9 ||
            i.picture > 7 || i.quantity < 1 || i.stats.size() > MaxStats)
            return false;
        if (std::find(ids.begin(), ids.end(), i.id) != ids.end() || (i.slot && slots[i.slot]))
            return false;
        ids.push_back(i.id);
        if (i.slot)
            slots[i.slot] = true;
        for (const auto& stat : i.stats)
            if (stat.id >= 4096)
                return false;
    }
    return true;
}
inline std::vector<uint8_t> Encode(const Snapshot& s) {
    if (!Valid(s))
        return {};
    Writer w;
    w.Put(uint32_t(0x31505349));
    w.Put(s.type);
    w.Put(s.id);
    w.String(s.name);
    w.String(s.owner);
    w.String(s.designation);
    w.Put(uint16_t(s.items.size()));
    for (const auto& i : s.items) {
        w.Put(i.id);
        w.Put(i.flags);
        w.Put(i.index);
        w.Put(i.level);
        w.Put(i.quality);
        w.Put(i.picture);
        w.Put(i.slot);
        w.Put(i.x);
        w.Put(i.y);
        w.Put(i.width);
        w.Put(i.height);
        w.Put(i.quantity);
        w.Put(uint8_t(i.statsTruncated));
        w.String(i.code);
        w.String(i.name);
        w.String(i.tooltip);
        w.Put(uint16_t(i.stats.size()));
        for (const auto& stat : i.stats) {
            w.Put(stat.id);
            w.Put(stat.parameter);
            w.Put(stat.value);
        }
    }
    return w.bytes.size() <= MaxBytes ? std::move(w.bytes) : std::vector<uint8_t>{};
}
inline std::optional<Snapshot> Decode(std::span<const uint8_t> bytes) {
    if (bytes.size() > MaxBytes)
        return {};
    Reader r{bytes};
    if (r.Get<uint32_t>() != 0x31505349)
        return {};
    Snapshot s;
    s.type = r.Get<uint32_t>();
    s.id = r.Get<uint32_t>();
    s.name = r.String();
    s.owner = r.String();
    s.designation = r.String();
    const auto count = r.Get<uint16_t>();
    if (count > MaxItems)
        return {};
    for (unsigned n = 0; n < count && r.valid; ++n) {
        Item i;
        i.id = r.Get<uint32_t>();
        i.flags = r.Get<uint32_t>();
        i.index = r.Get<int32_t>();
        i.level = r.Get<uint16_t>();
        i.quality = r.Get<uint8_t>();
        i.picture = r.Get<uint8_t>();
        i.slot = r.Get<uint8_t>();
        i.x = r.Get<uint8_t>();
        i.y = r.Get<uint8_t>();
        i.width = r.Get<uint8_t>();
        i.height = r.Get<uint8_t>();
        i.quantity = r.Get<int32_t>();
        i.statsTruncated = r.Get<uint8_t>() != 0;
        i.code = r.String(4);
        i.name = r.String();
        // Unlike identifiers, the native description contains line breaks.
        const auto tooltipLength = r.Get<uint16_t>();
        if (tooltipLength > MaxTooltip || r.offset > r.bytes.size() ||
            tooltipLength > r.bytes.size() - r.offset)
            return {};
        i.tooltip.assign(reinterpret_cast<const char*>(r.bytes.data() + r.offset), tooltipLength);
        r.offset += tooltipLength;
        if (!Tooltip(i.tooltip))
            return {};
        const auto stats = r.Get<uint16_t>();
        if (stats > MaxStats)
            return {};
        for (unsigned j = 0; j < stats && r.valid; ++j)
            i.stats.push_back({r.Get<uint16_t>(), r.Get<uint16_t>(), r.Get<int32_t>()});
        s.items.push_back(std::move(i));
    }
    if (!r.valid || r.offset != bytes.size() || !Valid(s))
        return {};
    return s;
}
// Strictly ordered native TCP/IP delivery. Duplicates are harmless; gaps and
// inconsistent headers cannot publish a partial or mixed item list.
struct Assembly {
    uint32_t request = 0, type = 0, target = 0, total = 0;
    std::vector<uint8_t> bytes;
    void Reset() { *this = {}; }
    bool Add(const Packet& p) {
        if (!p.request || p.action != Chunk || p.total > MaxBytes || !p.total || !p.length ||
            p.length > Payload || p.offset >= p.total || p.length > p.total - p.offset)
            return false;
        if (!request) {
            if (p.offset)
                return false;
            request = p.request;
            type = p.type;
            target = p.target;
            total = p.total;
            bytes.reserve(total);
        }
        if (p.request != request || p.type != type || p.target != target || p.total != total)
            return false;
        if (p.offset < bytes.size())
            return p.offset + p.length <= bytes.size() &&
                   std::equal(p.bytes.begin(), p.bytes.begin() + p.length, bytes.begin() + p.offset);
        if (p.offset != bytes.size())
            return false;
        bytes.insert(bytes.end(), p.bytes.begin(), p.bytes.begin() + p.length);
        return true;
    }
    bool Complete() const { return total && bytes.size() == total; }
};
} // namespace Inspect::Data
