#include "StatRangesData.h"
#include "GameAssets.h"
#include "StatRangesBinTables.h"
#include "../json.hpp"
#include <cctype>
#include <regex>
#include <limits>
#include <set>
#include <sstream>

namespace StatRanges {
namespace {
using Row = std::map<std::string, std::string>;
std::string Lower(std::string s) { for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; }
std::string Field(const Row& row, const std::string& key) { auto i = row.find(key); return i == row.end() ? "" : i->second; }
int Number(const std::string& s, int fallback = 0) {
    try { size_t used; int n = std::stoi(s, &used); return used == s.size() ? n : fallback; } catch (...) { return fallback; }
}
void PropertyFrom(Properties& to, const Row& row, const std::string& code, const std::string& param, const std::string& min, const std::string& max) {
    auto c = Field(row, code);
    if (!c.empty()) to.push_back({ Lower(c), Field(row, param), Number(Field(row, min)), Number(Field(row, max)) });
}
Properties ItemProperties(const Row& row, int count) {
    Properties result;
    for (int i = 1; i <= count; ++i) { auto n = std::to_string(i); PropertyFrom(result, row, "prop" + n, "par" + n, "min" + n, "max" + n); }
    return result;
}
std::string RangeText(Bounds b) { return std::to_string(b.min) + "-" + std::to_string(b.max); }
bool ContainsRoll(const std::string& line, Bounds b, size_t which = 0) {
    // A minus between two digits separates damage endpoints; it is not a sign.
    for (size_t i = 0, number = 0; i < line.size(); ++i) if (std::isdigit(static_cast<unsigned char>(line[i]))) {
        size_t end = i;
        while (end < line.size() && std::isdigit(static_cast<unsigned char>(line[end]))) ++end;
        if (number++ == which) {
            int64_t value = 0;
            try { value = std::stoll(line.substr(i, end - i)); } catch (...) { return false; }
            if (i && line[i - 1] == '-' && (i < 2 || !std::isdigit(static_cast<unsigned char>(line[i - 2])))) value = -value;
            return value >= b.min && value <= b.max;
        }
        i = end - 1;
    }
    return false;
}
// printf placeholders in the game's localization files, not English substrings.
std::string Expression(const std::string& source) {
    std::string result;
    auto text = PlainText(source);
    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (c == '%' && i + 1 < text.size()) {
            if (text[i + 1] == '%') { result += '%'; ++i; continue; }
            size_t end = i + 1;
            while (end < text.size() && std::string("+-0123456789.$hl").find(text[end]) != std::string::npos) ++end;
            if (end < text.size() && std::string("diufgs").find(text[end]) != std::string::npos) {
                result += text[end] == 's' ? ".*?" : "[+-]?[0-9]+(?:[.,][0-9]+)?";
                i = end; continue;
            }
        }
        if (std::isspace(static_cast<unsigned char>(c))) { result += "\\s*"; continue; }
        if (std::string(".^$|()[]{}*+?\\").find(c) != std::string::npos) result += '\\';
        result += c;
    }
    return result;
}
bool Matches(const std::string& line, const std::string& format, const std::vector<std::string>& seconds) {
    if (format.size() < 3) return false;
    const auto body = Expression(format);
    // Old tables keep the number outside the localized string; newer ones use printf.
    const std::string edge = "[+0-9%., \\-]*";
    auto match = [&](const std::string& suffix) {
        static thread_local std::map<std::string, std::regex> expressions;
        const auto expression = "^" + edge + body + edge + suffix + "(?:\\s*\\([0-9]+[^)]*\\))?$";
        try {
            auto found = expressions.find(expression);
            if (found == expressions.end()) {
                if (expressions.size() >= 8192) expressions.clear();
                found = expressions.emplace(expression, std::regex(expression, std::regex::icase)).first;
            }
            return std::regex_match(line, found->second);
        }
        catch (const std::regex_error&) { return false; }
    };
    if (match("")) return true;
    for (const auto& s : seconds) if (match("\\s*" + Expression(s))) return true;
    return false;
}
const std::vector<std::string>& Strings(const Database& db, const std::string& key) {
    static const std::vector<std::string> empty;
    auto it = db.strings.find(key); return it == db.strings.end() ? empty : it->second;
}
bool FixedMinus(const std::string& format) {
    const auto text = PlainText(format);
    for (size_t i = 0; i + 1 < text.size(); ++i) if (text[i] == '%') {
        if (text[i + 1] == '%') { ++i; continue; }
        size_t end = i + 1;
        while (end < text.size() && std::string("+-0123456789.$hl").find(text[end]) != std::string::npos) ++end;
        if (end < text.size() && std::string("diufg").find(text[end]) != std::string::npos) return i && text[i - 1] == '-';
    }
    return false;
}
bool NamedMatch(const Database& db, const std::string& key, const std::string& line, const std::string& second = "", const std::string& skill = "", bool* fixedMinus = nullptr) {
    for (auto format : Strings(db, key)) {
        if (!skill.empty()) {
            auto p = format.find("%s");
            if (p != std::string::npos) format.replace(p, 2, skill);
        }
        if (Matches(line, format, Strings(db, second))) {
            if (fixedMinus) *fixedMinus = FixedMinus(format);
            return true;
        }
    }
    return false;
}
}

std::string PlainText(const std::string& text) {
    std::string out;
    for (size_t i = 0; i < text.size();) {
        // D2R strings exist both as UTF-8 U+00FF and as the legacy FF byte.
        size_t mark = static_cast<uint8_t>(text[i]) == 0xff ? 1 :
            (text.compare(i, 2, "\xc3\xbf") == 0 ? 2 : 0);
        // Some mod JSONs decode invalid FF as U+FFFD.
        if (!mark && text.compare(i, 3, "\xef\xbf\xbd") == 0) mark = 3;
        if (mark && i + mark + 1 < text.size() && text[i + mark] == 'c') { i += mark + 2; continue; }
        if (text[i] != '\r') out += text[i];
        ++i;
    }
    while (!out.empty() && std::isspace(static_cast<unsigned char>(out.back()))) out.pop_back();
    size_t start = out.find_first_not_of(" \t");
    return start == std::string::npos ? "" : out.substr(start);
}

Database LoadDatabase(GameAssets& assets) {
    Database db;
    Binary::Tables tables(assets);
    try {
        auto costs = tables.Get("itemstatcost"), properties = tables.Get("properties");
        if (costs.empty() || properties.empty()) { db.error = "Could not load properties/itemstatcost tables (TXT or D2R 2.4 BIN) from the game/mod."; return db; }
        for (const auto& row : costs) {
            int id = Number(Field(row, "*id"), Number(Field(row, "id"), -1));
            if (id >= 0) db.statIds[Lower(Field(row, "stat"))] = id;
        }
        for (const auto& row : costs) {
            auto found = db.statIds.find(Lower(Field(row, "stat"))); if (found == db.statIds.end()) continue;
            Stat s; s.id = found->second; s.name = Lower(Field(row, "stat"));
            s.positive = Field(row, "descstrpos"); s.negative = Field(row, "descstrneg"); s.second = Field(row, "descstr2");
            s.function = Number(Field(row, "descfunc")); s.group = Number(Field(row, "dgrp")); s.groupFunction = Number(Field(row, "dgrpfunc"));
            s.groupPositive = Field(row, "dgrpstrpos"); s.groupNegative = Field(row, "dgrpstrneg"); s.shift = Number(Field(row, "valshift"));
            s.op = Number(Field(row, "op")); s.opParam = Number(Field(row, "op param"));
            auto target = db.statIds.find(Lower(Field(row, "op stat1"))); if (target != db.statIds.end()) s.opStat = target->second;
            db.stats[s.id] = s;
        }
        for (const auto& row : properties) {
            auto& slots = db.properties[Lower(Field(row, "code"))];
            for (int i = 1; i <= 7; ++i) {
                auto n = std::to_string(i); int f = Number(Field(row, "func" + n)); if (!f) break;
                auto stat = db.statIds.find(Lower(Field(row, "stat" + n)));
                slots.push_back({ f, stat == db.statIds.end() ? -1 : stat->second, Number(Field(row, "val" + n)), Number(Field(row, "set" + n)) != 0 });
            }
        }
        // The native loader concatenates these three tables in this exact order.
        for (const char* table : { "magicsuffix", "magicprefix", "automagic" }) for (const auto& row : tables.Get(table)) {
            Properties props;
            for (int i = 1; i <= 3; ++i) { auto n = std::to_string(i); PropertyFrom(props, row, "mod" + n + "code", "mod" + n + "param", "mod" + n + "min", "mod" + n + "max"); }
            db.affixes.push_back(std::move(props));
        }
        int ordinal = 0;
        for (const auto& row : tables.Get("qualityitems")) {
            Properties props;
            for (int i = 1; i <= 2; ++i) { auto n = std::to_string(i); PropertyFrom(props, row, "mod" + n + "code", "mod" + n + "param", "mod" + n + "min", "mod" + n + "max"); }
            db.superior[ordinal++] = std::move(props);
        }
        ordinal = 0;
        for (const auto& row : tables.Get("charstats")) {
            if (Field(row, "class") == "Expansion") continue;
            db.classSkills[ordinal] = Field(row, "strallskills");
            for (int tab = 0; tab < 3; ++tab) db.tabSkills[ordinal * 8 + tab] = Field(row, "strskilltab" + std::to_string(tab + 1));
            ++ordinal;
        }
        ordinal = 0;
        for (const char* name : { "weapons", "armor", "misc" }) for (const auto& row : tables.Get(name)) {
            if (Field(row, "name") == "Expansion") continue;
            db.baseItems[ordinal++] = { Lower(Field(row, "code")), Number(Field(row, "gemapplytype")), Lower(Field(row, "type")), Lower(Field(row, "type2")) };
        }
        for (const auto& row : tables.Get("itemtypes")) {
            auto& parents = db.itemTypes[Lower(Field(row, "code"))];
            for (const char* key : { "equiv1", "equiv2" }) { auto s = Lower(Field(row, key)); if (!s.empty()) parents.push_back(s); }
        }
        for (const auto& row : tables.Get("runes")) if (Number(Field(row, "complete"))) {
            Runeword word;
            for (int i = 1; i <= 6; ++i) { auto code = Lower(Field(row, "rune" + std::to_string(i))); if (!code.empty()) word.runes.push_back(code); }
            for (int i = 1; i <= 6; ++i) { auto type = Lower(Field(row, "itype" + std::to_string(i))); if (!type.empty()) word.include.push_back(type); }
            for (int i = 1; i <= 3; ++i) { auto type = Lower(Field(row, "etype" + std::to_string(i))); if (!type.empty()) word.exclude.push_back(type); }
            for (int i = 1; i <= 7; ++i) { auto n = std::to_string(i); PropertyFrom(word.properties, row, "t1code" + n, "t1param" + n, "t1min" + n, "t1max" + n); }
            if (!word.runes.empty()) db.runewords.push_back(std::move(word));
        }
        for (const auto& row : tables.Get("gems")) {
            auto& variants = db.gems[Lower(Field(row, "code"))];
            int type = 0;
            for (const char* context : { "weapon", "helm", "shield" }) {
                for (int i = 1; i <= 3; ++i) {
                    auto prefix = std::string(context) + "mod" + std::to_string(i);
                    PropertyFrom(variants[type], row, prefix + "code", prefix + "param", prefix + "min", prefix + "max");
                }
                ++type;
            }
        }
        ordinal = 0;
        for (const auto& row : tables.Get("uniqueitems")) {
            if (Field(row, "index") == "Expansion") continue;
            db.uniques[Number(Field(row, "*id"), ordinal++)] = ItemProperties(row, 12);
        }
        ordinal = 0;
        for (const auto& row : tables.Get("setitems")) {
            if (Field(row, "index") == "Expansion") continue;
            SetItem item; item.base = ItemProperties(row, 9); item.set = Lower(Field(row, "set"));
            for (int i = 1; i <= 5; ++i) {
                Properties bonus;
                for (const char* suffix : { "a", "b" }) { auto n = std::to_string(i) + suffix; PropertyFrom(bonus, row, "aprop" + n, "apar" + n, "amin" + n, "amax" + n); }
                if (!bonus.empty()) item.bonuses.push_back(std::move(bonus));
            }
            db.sets[Number(Field(row, "*id"), ordinal++)] = std::move(item);
        }
        for (const auto& row : tables.Get("sets")) {
            auto& bonuses = db.setBonuses[Lower(Field(row, "index"))];
            for (int i = 2; i <= 5; ++i) {
                Properties bonus;
                for (const char* suffix : { "a", "b" }) { auto n = std::to_string(i) + suffix; PropertyFrom(bonus, row, "pcode" + n, "pparam" + n, "pmin" + n, "pmax" + n); }
                if (!bonus.empty()) bonuses.push_back(std::move(bonus));
            }
            Properties full;
            for (int i = 1; i <= 8; ++i) { auto n = std::to_string(i); PropertyFrom(full, row, "fcode" + n, "fparam" + n, "fmin" + n, "fmax" + n); }
            if (!full.empty()) bonuses.push_back(std::move(full));
        }
        // Native tooltips may switch language without changing item data.
        for (const char* file : { "item-modifiers", "skills", "item-nameaffixes", "ui", "classes" }) {
            auto bytes = assets.Read(std::string("local/lng/strings/") + file + ".json");
            auto json = nlohmann::json::parse(bytes.begin(), bytes.end(), nullptr, false);
            if (!json.is_array()) continue;
            for (const auto& row : json) if (row.is_object() && row.contains("Key") && row["Key"].is_string()) {
                auto& translations = db.strings[row["Key"].get<std::string>()];
                for (auto it = row.begin(); it != row.end(); ++it) if (it.key() != "Key" && it.value().is_string()) {
                    auto s = it.value().get<std::string>();
                    if (!s.empty() && std::find(translations.begin(), translations.end(), s) == translations.end()) translations.push_back(std::move(s));
                }
                if (row.contains("id") && row["id"].is_number_integer())
                    db.strings[Binary::Alias("string",row["id"].get<int>())] = translations;
            }
        }
        std::map<std::string, std::string> desc;
        for (const auto& row : tables.Get("skilldesc")) desc[Lower(Field(row, "skilldesc"))] = Field(row, "str name");
        for (const auto& row : tables.Get("skills")) {
            int id = Number(Field(row, "*id"), Number(Field(row, "id"), -1)); if (id < 0) continue;
            db.skillIds[Lower(Field(row, "skill"))] = id;
            db.skillNames[id] = desc[Lower(Field(row, "skilldesc"))];
        }
        if (db.affixes.empty() || db.uniques.empty() || db.sets.empty()) db.error = "Some item property tables could not be loaded.";
    } catch (const std::exception& e) { db.error = e.what(); }
    return db;
}

void Database::Apply(Totals& totals, const Properties& source) const {
    auto id = [&](const char* name) { auto i = statIds.find(name); return i == statIds.end() ? -1 : i->second; };
    for (const auto& prop : source) {
        auto found = properties.find(prop.code); if (found == properties.end()) continue;
        constexpr int invalid = (std::numeric_limits<int>::min)();
        int parameter = prop.param.empty() ? 0 : Number(prop.param, invalid);
        if (parameter == invalid) { auto skill = skillIds.find(Lower(prop.param)); if (skill != skillIds.end()) parameter = skill->second; }
        Bounds rolled{ (std::min)(prop.min, prop.max), (std::max)(prop.min, prop.max) };
        Bounds first = rolled; bool firstKnown = false; size_t slotIndex = 0;
        for (const auto& slot : found->second) {
            const bool firstSlot = slotIndex++ == 0;
            int stat = slot.stat, layer = 0; Bounds b = rolled;
            const bool reuse = slot.function == 3 || slot.function == 4 || (slot.function >= 5 && slot.function <= 10) || slot.function == 24;
            if (reuse && !firstSlot && !firstKnown) continue;
            if (reuse && firstKnown && (first.min || first.max)) b = first;
            switch (slot.function) {
            case 1: case 2: case 3: case 4: case 8: break;
            case 5: stat = id("mindamage"); break;
            case 6: stat = id("maxdamage"); break;
            case 7:
                Add(totals, { id("item_mindamage_percent"), 0 }, b, slot.replace);
                stat = id("item_maxdamage_percent"); break;
            case 9: case 22: case 24:
                if (parameter < 0) continue;
                layer = parameter; break;
            case 10:
                if (parameter < 0) continue;
                layer = parameter % 3 + 8 * (parameter / 3); break;
            case 15: b = { prop.min, prop.min }; if (stat < 0) stat = id("mindamage"); break;
            case 16: b = { prop.max, prop.max }; if (stat < 0) stat = id("maxdamage"); break;
            case 17:
                // Parameter is a value (e.g. per-level coefficient or duration), not a random roll.
                if (parameter == invalid) continue;
                if (parameter) b = { parameter, parameter };
                break;
            case 21: layer = slot.value; break;
            // Chance-to-cast, charges and packed encodings do not use min/max as
            // a numeric roll. Do not accidentally advertise those as a range.
            default: continue;
            }
            if (firstSlot) { first = b; firstKnown = true; }
            if (stat >= 0) {
                Add(totals, { stat, layer }, b, slot.replace);
                if (stat == id("poisonmaxdam")) {
                    const int count = id("poison_count");
                    if (count >= 0 && (!slot.replace || !totals.count({count,0})))
                        Add(totals, {count,0}, {b.min <= 0 && b.max >= 0 ? 0 : 1, 1});
                }
            }
        }
    }
}

Totals Database::ForItem(const Item& item) const {
    Totals result;
    // Crafted/tempered items do not retain an unambiguous crafting recipe ID.
    // Showing affix-only bounds would omit the recipe's permanent contributions.
    if (item.quality == 8 || item.quality == 9) return result;
    if (item.quality == 3) { auto i = superior.find(item.index); if (i != superior.end()) Apply(result, i->second); }
    if (item.quality == 7) { auto i = uniques.find(item.index); if (i != uniques.end()) Apply(result, i->second); }
    if (item.quality == 5) { auto i = sets.find(item.index); if (i != sets.end()) Apply(result, i->second.base); }
    auto affix = [&](uint16_t n) { if (n && n <= affixes.size()) Apply(result, affixes[n - 1]); };
    // Normal/superior runewords can store a name ID in these fields. They are
    // not magic affix IDs, so do not interpret them as random modifiers.
    if (!item.runeword) {
        for (auto n : item.prefixes) affix(n);
        for (auto n : item.suffixes) affix(n);
    }
    affix(item.automagic);
    return result;
}

const Properties* Database::ForRuneword(int baseId, const std::vector<std::string>& socketCodes) const {
    auto base = baseItems.find(baseId); if (base == baseItems.end()) return nullptr;
    std::set<std::string> types; std::vector<std::string> pending{base->second.type, base->second.type2};
    while (!pending.empty() && types.size() < 1024) {
        auto type = pending.back(); pending.pop_back();
        if (type.empty() || !types.insert(type).second) continue;
        auto parents = itemTypes.find(type);
        if (parents != itemTypes.end()) pending.insert(pending.end(), parents->second.begin(), parents->second.end());
    }
    for (const auto& word : runewords) if (word.runes == socketCodes) {
        bool excluded = false, included = false;
        for (const auto& type : word.exclude) if (types.count(type)) excluded = true;
        for (const auto& type : word.include) if (types.count(type)) included = true;
        if (included && !excluded) return &word.properties;
    }
    return nullptr;
}

bool ValidRangeFormat(const std::string& format) {
    if (format.empty() || format.size() > MaxRangeFormatLength) return false;
    int values = 0;
    for (size_t i = 0; i < format.size(); ++i) {
        const auto c = static_cast<unsigned char>(format[i]);
        if (c < 0x20 || c == 0x7f) return false;
        if (c != '%') continue;
        if (++i == format.size()) return false;
        if (format[i] == 'd') ++values;
        else if (format[i] != '%') return false;
    }
    return values == 2;
}
std::string FormatAnnotation(const Annotation& annotation, const std::string& format) {
    if (!annotation.found || annotation.bounds.empty() || format == DefaultRangeFormat || !ValidRangeFormat(format)) return annotation.text;
    std::string result;
    for (const auto& bounds : annotation.bounds) {
        if (!result.empty()) result += " / ";
        bool maximum = false;
        for (size_t i = 0; i < format.size(); ++i) {
            if (format[i] != '%') { result += format[i]; continue; }
            // Substitute values ourselves: never pass user input to printf.
            if (format[++i] == '%') result += '%';
            else { result += std::to_string(maximum ? bounds.max : bounds.min); maximum = true; }
        }
    }
    return result;
}

Annotation MatchLine(const Database& db, const Totals& totals, const std::string& raw, bool showFixed, int characterLevel) {
    const std::string line = PlainText(raw);
    if (line.empty()) return {};
    // Resolve ambiguity from the actual bounds, before applying a user format.
    std::map<std::string, std::vector<Bounds>> matches;
    auto add = [&](Bounds b) { if (showFixed || b.min != b.max) matches.emplace("[" + RangeText(b) + "]", std::vector<Bounds>{b}); };
    auto pair = [&](Bounds low, Bounds high) { matches.emplace("[" + RangeText(low) + " / " + RangeText(high) + "]", std::vector<Bounds>{low, high}); };
    for (const auto& entry : totals) {
        auto found = db.stats.find(entry.first.stat); if (found == db.stats.end()) continue;
        const auto& s = found->second; Bounds b = entry.second;
        std::string key = b.max < 0 ? s.negative : s.positive;
        if (s.function == 13) {
            auto cls = db.classSkills.find(entry.first.layer);
            if (cls == db.classSkills.end()) continue;
            key = cls->second;
        } else if (s.function == 14) {
            auto tab = db.tabSkills.find(entry.first.layer);
            if (tab == db.tabSkills.end()) continue;
            key = tab->second;
        }
        if (s.op == 2 || s.op == 4 || s.op == 5) {
            if (s.opParam < 0 || s.opParam > 30) continue;
            b.min = b.min * characterLevel / (int64_t(1) << s.opParam);
            b.max = b.max * characterLevel / (int64_t(1) << s.opParam);
        }
        if (s.function == 5 || s.function == 10) { b.min = b.min * 100 / 128; b.max = b.max * 100 / 128; }
        bool named = false, fixedMinus = false;
        if (s.function == 27 || s.function == 28) {
            auto skill = db.skillNames.find(entry.first.layer);
            if (skill != db.skillNames.end()) for (const auto& name : Strings(db, skill->second))
                if (NamedMatch(db, key, line, s.second, name, &fixedMinus)) named = true;
        } else named = NamedMatch(db, key, line, s.second, "", &fixedMinus);
        // Some descriptions supply the minus sign themselves (for example
        // "-%d%% to Enemy Cold Resistance") while the stored pierce roll is
        // positive. Match its displayed sign without changing the roll bounds.
        const Bounds displayed = fixedMinus && b.min >= 0 ? Bounds{-b.max,-b.min} : b;
        if (named && ContainsRoll(line, displayed)) add(b);
        if (s.group && !s.groupPositive.empty()) {
            bool complete = true;
            Bounds group = entry.second;
            for (const auto& member : db.stats) if (member.second.group == s.group) {
                auto i = totals.find({ member.first, entry.first.layer });
                if (i == totals.end()) { complete = false; break; }
                group.min = (std::max)(group.min, i->second.min);
                group.max = (std::min)(group.max, i->second.max);
            }
            if (complete && group.min <= group.max && NamedMatch(db, group.max < 0 ? s.groupNegative : s.groupPositive, line) && ContainsRoll(line, group)) add(group);
        }
    }
    // The engine combines min/max elemental damage and enhanced damage into one line.
    auto stat = [&](const std::string& name) -> const Bounds* {
        auto id = db.statIds.find(name); if (id == db.statIds.end()) return nullptr;
        auto v = totals.find({ id->second, 0 }); return v == totals.end() ? nullptr : &v->second;
    };
    auto ed = stat("item_maxdamage_percent"), emin = stat("item_mindamage_percent");
    if (ed && emin && *ed == *emin && NamedMatch(db, "strModEnhancedDamage", line) && ContainsRoll(line, *ed)) add(*ed);
    for (const auto& element : { std::pair<const char*, const char*>{ "fire", "Fire" }, { "light", "Lightning" }, { "cold", "Cold" }, { "magic", "Magic" } }) {
        auto low = stat(std::string(element.first) + "mindam"), high = stat(std::string(element.first) + "maxdam");
        if (!low || !high) continue;
        const std::string key = std::string("strMod") + element.second + "Damage";
        if (NamedMatch(db, key + "Range", line) && ContainsRoll(line, *low) && ContainsRoll(line, *high, 1) && (showFixed || low->min != low->max || high->min != high->max))
            pair(*low, *high);
        if (*low == *high && NamedMatch(db, key, line) && ContainsRoll(line, *low)) add(*low);
    }
    auto low = stat("mindamage"), high = stat("maxdamage");
    if (low && high) {
        if (NamedMatch(db, "strModMinDamageRange", line) && ContainsRoll(line, *low) && ContainsRoll(line, *high, 1) && (showFixed || low->min != low->max || high->min != high->max))
            pair(*low, *high);
        if (*low == *high && NamedMatch(db, "strModMinDamage", line) && ContainsRoll(line, *low)) add(*low);
    }
    auto pmin = stat("poisonmindam"), pmax = stat("poisonmaxdam"), length = stat("poisonlength"), count = stat("poison_count");
    if (pmin && pmax && length && count && count->min > 0 && count->min == count->max && pmin->min >= 0 && pmax->min >= 0) {
        // Native D2R averages poison length across sources, then rounds damage
        // with +128 before shifting the 8-bit damage-per-frame fraction.
        Bounds frames{length->min/count->max, length->max/count->min};
        Bounds minimum{(pmin->min*frames.min+128)/256, (pmin->max*frames.max+128)/256};
        Bounds maximum{(pmax->min*frames.min+128)/256, (pmax->max*frames.max+128)/256};
        Bounds seconds{frames.min/25,frames.max/25};
        if (NamedMatch(db,"strModPoisonDamageRange",line) && ContainsRoll(line,minimum) && ContainsRoll(line,maximum,1) && ContainsRoll(line,seconds,2) &&
            (showFixed || minimum.min != minimum.max || maximum.min != maximum.max))
            pair(minimum, maximum);
        if (minimum == maximum && NamedMatch(db,"strModPoisonDamage",line) && ContainsRoll(line,minimum) && ContainsRoll(line,seconds,1)) add(minimum);
    }
    if (matches.size() != 1) return {};
    return { matches.begin()->first, true, matches.begin()->second };
}
}
