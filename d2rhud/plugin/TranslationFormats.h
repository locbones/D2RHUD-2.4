#pragma once
#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace HudLocale::Formats {
inline bool Digit(char c) { return c >= '0' && c <= '9'; }
// The existing printf grammar, scanned without regex recursion, allocations or
// backtracking. Invalid candidates advance to the next percent sign.
inline std::string_view Next(std::string_view text, size_t& position)
{
    while (position < text.size()) {
        const auto start = text.find('%', position);
        if (start == text.npos) { position = text.size(); return {}; }
        auto cursor = start + 1;
        while (cursor < text.size() && std::string_view("-+ #0").find(text[cursor]) != text.npos) ++cursor;
        if (cursor < text.size() && text[cursor] == '*') ++cursor;
        else while (cursor < text.size() && Digit(text[cursor])) ++cursor;
        if (cursor < text.size() && text[cursor] == '.') {
            const auto precision = ++cursor;
            if (cursor < text.size() && text[cursor] == '*') ++cursor;
            else while (cursor < text.size() && Digit(text[cursor])) ++cursor;
            if (precision == cursor) { position = start + 1; continue; }
        }
        const auto rest = text.substr(cursor);
        if (rest.substr(0, 2) == "hh" || rest.substr(0, 2) == "ll") cursor += 2;
        else if (rest.substr(0, 3) == "I64" || rest.substr(0, 3) == "I32") cursor += 3;
        else if (cursor < text.size() && std::string_view("hljztL").find(text[cursor]) != text.npos) ++cursor;
        if (cursor < text.size() && std::string_view("diuoxXfFeEgGaAcsnp%").find(text[cursor]) != text.npos) {
            position = cursor + 1;
            return text.substr(start, position - start);
        }
        position = start + 1;
    }
    return {};
}
inline bool Match(std::string_view source, std::string_view translation)
{
    if (std::count(source.begin(), source.end(), '%') != std::count(translation.begin(), translation.end(), '%')) return false;
    size_t left = 0, right = 0;
    for (;;) {
        const auto expected = Next(source, left), actual = Next(translation, right);
        if (expected != actual) return false;
        if (expected.empty()) return true;
    }
}
}
namespace HudLocale {
// Retain the existing utility API; pack validation uses the allocation-free Match.
inline std::vector<std::string> Placeholders(const std::string& text)
{
    std::vector<std::string> result;
    size_t position = 0;
    for (;;) {
        const auto placeholder = Formats::Next(text, position);
        if (placeholder.empty()) return result;
        result.emplace_back(placeholder);
    }
}
}
