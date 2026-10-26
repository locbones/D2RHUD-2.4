#pragma once
#include <algorithm>
#include <array>
#include <charconv>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace ExtendedActs::LevelEdits {
[[noreturn]] inline void Invalid(const std::string& reason) {
    throw std::runtime_error("Act VI levels.txt: " + reason);
}
inline std::vector<std::string> Split(std::string_view line) {
    std::vector<std::string> result;
    for (size_t begin = 0;;) {
        const auto end = line.find('\t', begin);
        result.emplace_back(line.substr(begin, end == line.npos ? end : end - begin));
        if (end == line.npos) return result;
        begin = end + 1;
    }
}
inline int Number(std::string_view value, const char* column) {
    int result = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) Invalid(std::string("invalid ") + column);
    return result;
}
struct Line {
    std::string raw, ending;
    std::vector<std::string> cells;
    std::optional<int> id;
};
struct Table {
    std::vector<Line> lines;
    std::vector<std::string> columns;
    std::map<std::string, size_t, std::less<>> indices;
    std::map<int, size_t> rows;
    size_t Column(std::string_view name) const {
        const auto found = indices.find(name);
        if (found == indices.end()) Invalid("missing column " + std::string(name));
        return found->second;
    }
    const std::string& Cell(int id, std::string_view column) const { return lines.at(rows.at(id)).cells.at(Column(column)); }
    explicit Table(std::string_view bytes) {
        if (bytes.empty() || bytes.size() > 64 * 1024 * 1024) Invalid("missing or oversized table");
        for (size_t begin = 0; begin < bytes.size();) {
            const auto end = bytes.find('\n', begin);
            auto raw = bytes.substr(begin, end == bytes.npos ? bytes.size() - begin : end - begin);
            std::string ending = end == bytes.npos ? "" : "\n";
            if (!raw.empty() && raw.back() == '\r') { raw.remove_suffix(1); ending.insert(0, 1, '\r'); }
            lines.push_back({std::string(raw), std::move(ending), {}, {}});
            if (end == bytes.npos) break;
            begin = end + 1;
        }
        auto header = std::string_view(lines.front().raw);
        if (header.starts_with("\xef\xbb\xbf")) header.remove_prefix(3);
        columns = Split(header);
        for (size_t n = 0; n < columns.size(); ++n)
            if (columns[n].empty() || !indices.emplace(columns[n], n).second) Invalid("empty or duplicate column");
        const auto idColumn = Column("Id");
        Column("Name"); Column("Act"); Column("Waypoint");
        for (size_t n = 1; n < lines.size(); ++n) {
            auto& line = lines[n];
            if (line.raw.empty()) continue;
            line.cells = Split(line.raw);
            if (line.cells.size() != columns.size()) Invalid("incomplete row " + std::to_string(n + 1));
            if (line.cells[idColumn].empty()) continue; // Expansion separator, etc.
            const int id = Number(line.cells[idColumn], "Id");
            if (id < 0 || id >= 1024 || !rows.emplace(id, n).second) Invalid("out-of-range or duplicate level ID " + std::to_string(id));
            line.id = id;
        }
    }
};
struct Selection { std::string text; std::optional<std::string> archive; };
// Template hashes are checked by the caller. Ordinary row values belong to the
// mod author; Act VI owns only its row identities, act and waypoint references.
inline Selection Select(std::string_view current, std::string_view six, std::string_view five,
    const std::array<int, 9>& areas, bool enabled, const std::optional<std::string>& archived = {}) {
    const Table live(current), on(six), off(five);
    const std::set<int> owned(areas.begin(), areas.end());
    if (owned.size() != areas.size()) Invalid("duplicate configured Act VI IDs");
    for (const auto& column : on.columns) { off.Column(column); live.Column(column); }
    if (on.rows.size() != off.rows.size() + areas.size()) Invalid("template row counts do not match");
    for (const auto& [id, row] : off.rows) if (!on.rows.contains(id) || owned.contains(id)) Invalid("conflicting template level IDs");
    for (size_t n = 0; n < areas.size(); ++n) {
        const int id = areas[n];
        if (!on.rows.contains(id) || on.Cell(id, "Act") != "5" || Number(on.Cell(id, "Waypoint"), "Waypoint") != 39 + int(n))
            Invalid("template has an invalid Act VI act/waypoint reference");
    }
    auto validate = [&](const Table& table, bool onlyOwned) {
        size_t present = 0;
        std::set<int> waypoints;
        std::vector<int> originalOrder, templateOrder, fullOrder;
        for (const auto& line : off.lines) if (line.id) templateOrder.push_back(*line.id);
        for (const auto& line : table.lines) {
            if (!line.id) {
                if (!line.cells.empty()) for (int id : areas)
                    if (line.cells[table.Column("Name")] == on.Cell(id, "Name")) Invalid("Act VI row has no level ID");
                continue;
            }
            const int id = *line.id;
            fullOrder.push_back(id);
            const int act = Number(table.Cell(id, "Act"), "Act");
            const int waypoint = Number(table.Cell(id, "Waypoint"), "Waypoint");
            if (waypoint != 255 && (waypoint < 0 || waypoint >= 48 || !waypoints.insert(waypoint).second))
                Invalid("invalid or duplicate waypoint at level " + std::to_string(id));
            if (owned.contains(id)) {
                ++present;
                for (const char* column : {"Name", "Act", "Waypoint"})
                    if (table.Cell(id, column) != on.Cell(id, column)) Invalid("changed Act VI " + std::string(column) + " at level " + std::to_string(id));
            } else {
                if (onlyOwned || !off.rows.contains(id)) Invalid("unexpected level ID " + std::to_string(id) + "; regenerate the Act VI pack for structural changes");
                if (act < 0 || act > 4 || (waypoint != 255 && waypoint >= 39)) Invalid("original level uses an Act VI act/waypoint slot");
                originalOrder.push_back(id);
            }
        }
        if (present && present != areas.size()) Invalid("incomplete Act VI level set");
        if (onlyOwned ? present != areas.size() : originalOrder != templateOrder) Invalid("missing or reordered level IDs");
        auto expectedOrder = onlyOwned ? std::vector<int>{} : templateOrder;
        if (present) expectedOrder.insert(expectedOrder.end(), areas.begin(), areas.end());
        if (fullOrder != expectedOrder) Invalid("reordered Act VI level IDs");
        return present;
    };
    const auto present = validate(live, false);
    if (enabled == bool(present)) return {std::string(current), {}};
    const std::string newline = live.lines.front().ending.empty() ? "\r\n" : live.lines.front().ending;
    Selection result;
    if (!enabled) {
        // Keep the editable Act VI rows outside global/excel while disabled,
        // so both launch modes retain their own native table dimensions.
        result.archive = live.lines.front().raw + newline;
        for (size_t n = 0; n < live.lines.size(); ++n) {
            const auto& line = live.lines[n];
            auto& destination = line.id && owned.contains(*line.id) ? *result.archive : result.text;
            destination += line.raw + line.ending;
        }
        return result;
    }
    const Table saved(archived ? std::string_view(*archived) : six);
    if (archived) validate(saved, true);
    result.text = current;
    if (!result.text.empty() && result.text.back() != '\n') result.text += newline;
    for (int id : areas) {
        for (size_t n = 0; n < live.columns.size(); ++n) {
            if (n) result.text += '\t';
            const auto& name = live.columns[n];
            if (saved.indices.contains(name)) result.text += saved.Cell(id, name);
            else if (on.indices.contains(name)) result.text += on.Cell(id, name);
        }
        result.text += newline;
    }
    return result;
}
}
