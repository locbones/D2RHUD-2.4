#pragma once
#include <algorithm>
#include <cctype>
#include <charconv>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace MercIcons::Data {
constexpr size_t MaxRows = 65536;
using Frames = std::map<std::string, int>;
inline std::string Fold(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    return value;
}
inline std::vector<std::string> Fields(const std::string& line) {
    std::vector<std::string> cells;
    size_t start = 0;
    for (size_t i = 0; i <= line.size(); ++i)
        if (i == line.size() || line[i] == '\t') {
            cells.push_back(line.substr(start, i - start));
            start = i + 1;
        }
    return cells;
}
inline int Frame(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
        text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
        text.remove_suffix(1);
    int value = -1;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size() && value >= 0 && value <= 65535
               ? value
               : -1;
}
inline Frames Parse(std::string text) {
    Frames result;
    if (text.starts_with("\xef\xbb\xbf"))
        text.erase(0, 3);
    std::istringstream input(text);
    std::string line;
    if (!std::getline(input, line))
        return result;
    if (!line.empty() && line.back() == '\r')
        line.pop_back();
    auto header = Fields(Fold(line));
    const auto name = std::find(header.begin(), header.end(), "skilldesc");
    const auto icon = std::find(header.begin(), header.end(), "hireableiconcel");
    if (name == header.end() || icon == header.end())
        return result;
    const size_t nameColumn = name - header.begin(), iconColumn = icon - header.begin();
    size_t count = 0;
    while (std::getline(input, line)) {
        if (++count > MaxRows)
            return {};
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        const auto cells = Fields(line);
        if (nameColumn >= cells.size() || cells[nameColumn].empty())
            continue;
        const auto key = Fold(cells[nameColumn]);
        if (key == "expansion")
            continue;
        result.emplace(key, iconColumn < cells.size() ? Frame(cells[iconColumn]) : -1);
    }
    return result;
}
} // namespace MercIcons::Data
