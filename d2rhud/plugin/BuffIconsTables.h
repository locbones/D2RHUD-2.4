#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace BuffIcons::Tables {
using Row = std::unordered_map<std::string, std::string>;
inline std::string Id(int value) { return "@id:" + std::to_string(value); }

// D2R 2.4 compiled table records. A four-byte row count precedes the records.
// Only fields used by the icon/state catalogue are decoded; no formulas or
// gameplay records are edited. Reject other layouts rather than guessing.
inline std::vector<Row> Decode(const std::vector<uint8_t>& bytes, const std::string& table) {
    const size_t stride = table == "skills" ? 0x284 : table == "skilldesc" ? 0x128 : table == "states" ? 0x40 : 0;
    uint32_t count = 0;
    if (bytes.size() >= 4) std::memcpy(&count, bytes.data(), 4);
    if (!stride || !count || count > 65536 || bytes.size() != 4 + uint64_t(count) * stride)
        throw std::runtime_error("Unsupported or damaged " + table + ".bin (D2R 2.4 table layout required).");
    std::vector<Row> rows;rows.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        const auto record = std::span<const uint8_t>(bytes).subspan(4 + size_t(i) * stride, stride);
        auto u16 = [&](size_t offset) { uint16_t value;std::memcpy(&value, record.data() + offset, 2);return value; };
        auto s16 = [&](size_t offset) { int16_t value;std::memcpy(&value, record.data() + offset, 2);return value; };
        auto u32 = [&](size_t offset) { uint32_t value;std::memcpy(&value, record.data() + offset, 4);return value; };
        Row row;row["@rowid"] = std::to_string(u16(0));
        if (table == "states") {
            row["state"] = Id(u16(0));
            row["curse"] = (u32(0x10) & (1u << 12)) ? "1" : "0";
        } else if (table == "skilldesc") {
            row["skilldesc"] = Id(u16(0));
            row["iconcel"] = std::to_string(record[6]);
            row["@strid"] = std::to_string(u16(8));
        } else {
            row["id"] = std::to_string(s16(0));
            const auto name = reinterpret_cast<const char*>(record.data() + 2);
            const auto end = static_cast<const char*>(std::memchr(name, 0, 32));
            row["skill"] = std::string(name, end ? size_t(end - name) : 32);
            constexpr std::array<const char*, 7> classes{"ama", "sor", "nec", "pal", "bar", "dru", "ass"};
            const auto cls = u32(0x2c);row["charclass"] = cls < classes.size() ? classes[cls] : "";
            row["skilldesc"] = Id(s16(0x1d4));
            row["aurastate"] = Id(s16(0xa0));
            row["auratargetstate"] = Id(s16(0xa2));
            row["passivestate"] = Id(s16(0xb0));
        }
        rows.push_back(std::move(row));
    }
    return rows;
}
}
