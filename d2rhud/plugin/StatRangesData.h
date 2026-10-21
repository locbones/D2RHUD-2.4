#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

class GameAssets;
namespace StatRanges {
struct Bounds {
    int64_t min = 0, max = 0;
    bool operator==(const Bounds& other) const { return min == other.min && max == other.max; }
};
struct StatKey {
    int stat = -1, layer = 0;
    bool operator<(const StatKey& other) const { return stat < other.stat || (stat == other.stat && layer < other.layer); }
    bool operator==(const StatKey& other) const { return stat == other.stat && layer == other.layer; }
};
using Totals = std::map<StatKey, Bounds>;
inline void Add(Totals& totals, StatKey key, Bounds value, bool replace = false) {
    if (value.max < value.min) std::swap(value.min, value.max);
    if (replace) totals[key] = value;
    else { auto& total = totals[key]; total.min += value.min; total.max += value.max; }
}
struct Property { std::string code, param; int min = 0, max = 0; };
using Properties = std::vector<Property>;
struct PropertySlot { int function = 0, stat = -1, value = 0; bool replace = false; };
struct Stat {
    std::string name, positive, negative, second, groupPositive, groupNegative;
    int id = -1, function = 0, group = 0, groupFunction = 0, shift = 0;
    int op = 0, opParam = 0, opStat = -1;
};
struct Item {
    int quality = 0, index = -1, level = 1;
    std::array<uint16_t, 3> prefixes{}, suffixes{};
    uint16_t automagic = 0;
    bool runeword = false;
};
struct SetItem { Properties base; std::vector<Properties> bonuses; std::string set; };
struct BaseItem { std::string code; int gemType = 0; std::string type, type2; };
struct Runeword { std::vector<std::string> runes, include, exclude; Properties properties; };
struct Database {
    std::map<std::string, std::vector<PropertySlot>> properties;
    std::map<int, Stat> stats;
    std::map<std::string, int> statIds, skillIds;
    // Each key has all installed translations. Matching follows the actual tooltip language.
    std::map<std::string, std::vector<std::string>> strings;
    std::map<int, std::string> skillNames;
    std::map<int, std::string> classSkills, tabSkills;
    std::map<int, BaseItem> baseItems;
    std::map<std::string, std::array<Properties, 3>> gems;
    std::map<std::string, std::vector<std::string>> itemTypes;
    std::vector<Runeword> runewords;
    std::map<int, Properties> uniques;
    std::map<int, Properties> superior;
    std::map<int, SetItem> sets;
    std::map<std::string, std::vector<Properties>> setBonuses;
    // Native IDs are 1 based: suffixes, then prefixes, then automagic.
    std::vector<Properties> affixes;
    std::string error;
    std::string diagnostics;
    void Apply(Totals& totals, const Properties& source) const;
    Totals ForItem(const Item& item) const;
    const Properties* ForRuneword(int baseId, const std::vector<std::string>& socketCodes) const;
};
// Prefer the compiled tables used by the game. Source mode is retained for
// authoring validation and for tables with no same-layer BIN.
Database LoadDatabase(GameAssets& assets, bool preferBinary = true);
std::string PlainText(const std::string& text);
inline constexpr char DefaultRangeFormat[] = "[%d-%d]";
inline constexpr size_t MaxRangeFormatLength = 128;
struct Annotation { std::string text; bool found = false; std::vector<Bounds> bounds; };
bool ValidRangeFormat(const std::string& format);
std::string FormatAnnotation(const Annotation& annotation, const std::string& format);
// Only attach an unambiguous, localized property description. Unknown descriptions
// are left alone rather than guessing a range from the observed roll.
Annotation MatchLine(const Database& db, const Totals& totals, const std::string& line, bool showFixed, int characterLevel = 1);

struct Activation {
    bool latched = false, previous = false, armed = true;
    void Reset() { latched = previous = false; armed = true; }
    bool Update(bool hotkeyMode, bool hold, bool enabled, bool down, bool allowed) {
        if (!allowed) { previous = down; armed = !down; return false; }
        if (!down) armed = true;
        if (hotkeyMode && !hold && down && !previous && armed) latched = !latched;
        previous = down;
        return hotkeyMode ? (hold ? down && armed : latched) : enabled;
    }
};
}
