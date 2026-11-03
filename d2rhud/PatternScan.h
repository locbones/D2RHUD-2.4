#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string_view>
#include <vector>

// Anchor on the longest literal run, then verify only the other literal runs.
// Wildcards are separate from byte values: literal 3F must remain literal.
namespace PatternScan {
inline constexpr size_t NotFound = (std::numeric_limits<size_t>::max)();
struct Run { size_t offset; std::vector<uint8_t> bytes; };
struct Signature { size_t size = 0, anchor = 0; std::vector<Run> runs; };
inline int Hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
inline bool Space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v'; }
inline Signature Compile(std::string_view text) {
    Signature result;
    for (size_t at = 0; at < text.size();) {
        if (Space(text[at])) { ++at; continue; }
        if (text[at] == '?') {
            ++at; if (at < text.size() && text[at] == '?') ++at;
            ++result.size; continue;
        }
        if (text.size() - at < 2 || Hex(text[at]) < 0 || Hex(text[at + 1]) < 0) return {};
        const auto value = uint8_t((Hex(text[at]) << 4) | Hex(text[at + 1]));
        if (result.runs.empty() || result.runs.back().offset + result.runs.back().bytes.size() != result.size)
            result.runs.push_back({result.size, {}});
        result.runs.back().bytes.push_back(value);
        ++result.size; at += 2;
    }
    for (size_t i = 1; i < result.runs.size(); ++i)
        if (result.runs[i].bytes.size() > result.runs[result.anchor].bytes.size()) result.anchor = i;
    return result;
}
inline size_t Find(const uint8_t* data, size_t size, const Signature& signature) {
    if (!data || !signature.size || signature.size > size) return NotFound;
    if (signature.runs.empty()) return 0;
    const auto& anchor = signature.runs[signature.anchor];
    const auto last = size - signature.size;
    size_t candidate = 0;
    while (candidate <= last) {
        const auto found = static_cast<const uint8_t*>(std::memchr(data + candidate + anchor.offset, anchor.bytes.front(), last - candidate + 1));
        if (!found) return NotFound;
        candidate = size_t(found - data) - anchor.offset;
        bool matches = std::memcmp(found, anchor.bytes.data(), anchor.bytes.size()) == 0;
        if (matches) for (size_t i = 0; i < signature.runs.size(); ++i) {
            if (i == signature.anchor) continue;
            const auto& run = signature.runs[i];
            if (std::memcmp(data + candidate + run.offset, run.bytes.data(), run.bytes.size())) { matches = false; break; }
        }
        if (matches) return candidate;
        ++candidate;
    }
    return NotFound;
}
inline uintptr_t ResolveReference(const uint8_t* data, size_t size, uintptr_t base, size_t match, int32_t operandOffset) {
    if (!data || match == NotFound || match > size) return 0;
    const auto operand = int64_t(match) + operandOffset;
    if (operand < 0 || uint64_t(operand) > size || size - size_t(operand) < sizeof(int32_t)) return 0;
    int32_t displacement = 0; std::memcpy(&displacement, data + size_t(operand), sizeof(displacement));
    return base + size_t(operand) + sizeof(displacement) + displacement;
}
}
