#include "BuffIcons.h"
#include "BuffIconsData.h"
#include "GameAssets.h"
#include "../d2/D2Ptrs.h"
#include "../D3D12Hook.h"
#include <imgui.h>
#include <windows.h>
#include <array>
#include <cctype>
#include <cfloat>
#include <cstddef>
#include <chrono>
#include <future>
#include <map>
#include <set>
#include <sstream>
#include <unordered_map>

namespace BuffIcons {
namespace {
struct Config {
    bool enabled = false, timers = true;
    // Timer position: below, above, top inside, bottom inside.
    int timerPosition = 0, timerFormat = 0;
    float timerSize = 17.0f;
    ImVec4 timerColor = ImVec4(1, 1, 1, 1);
    bool highlightExpiring = true;
    float expiringSeconds = 5.0f;
    ImVec4 expiringColor = ImVec4(1.0f, 120.0f / 255, 80.0f / 255, 1);
    bool labels = false, labelBottom = true;
    float labelSize = 14.0f;
    ImVec4 labelColor = ImVec4(1, 1, 1, 1);
    float x = 40.0f, y = 12.0f, scale = 1.0f;
    int columns = 10;
    std::map<std::string, std::set<int>> characters;
} config;
constexpr const char* timerPositions[] = {"Below", "Above", "Top", "Bottom"};
constexpr const char* timerFormats[] = {"Automatic", "Seconds", "DecimalSeconds", "MinutesSeconds", "HoursMinutesSeconds"};
struct Skill {
    int id = -1, frame = 0;
    std::string name, sprite;
    std::vector<int> states;
};
struct Database {
    std::map<int, Skill> skills;
    std::map<std::string, SpriteAtlas> atlases;
    std::string error;
};
struct LiveSkill { int id, level; float seconds = -1; bool active = false; };
Database database;
std::future<Database> loading;
bool loadStarted = false, loaded = false, previewEnabled = true;
std::vector<LiveSkill> available, selectedStates;
std::string character;
std::string runtimeStatus;
char search[96]{};

// Keep undocumented memory views private and bounded. These offsets are verified
// against D2R 1.4.71510, not the original 32-bit Diablo II structures.
template<typename T> bool Read(uintptr_t address, T& out) {
    SIZE_T count = 0;
    return address && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address), &out, sizeof(T), &count) && count == sizeof(T);
}
#pragma pack(push, 1)
struct SkillMemory {
    uintptr_t txt, next;                 // 00, 08
    uint8_t unused[0x28];
    int32_t level, bonus, quantity, owner, charges; // 38..48
};
struct StateMemory {
    uintptr_t unit;
    uint32_t ownerType, ownerId;
    uint8_t unused[12];
    uint32_t flags, state;               // 1c, 20
    float timeout;                      // 24: absolute on server, relative on client
    uint8_t padding[0x20];
    uintptr_t previous;                 // 48
};
#pragma pack(pop)
static_assert(offsetof(SkillMemory, level) == 0x38 && offsetof(SkillMemory, charges) == 0x48);
static_assert(offsetof(StateMemory, timeout) == 0x24 && offsetof(StateMemory, previous) == 0x48);

// Tiny SEH adapters contain no C++ objects and tolerate teardown during a read.
uintptr_t LocalPlayer() {
    __try {
        const auto getPlayer = reinterpret_cast<uintptr_t(__fastcall*)(uint32_t)>(Pattern::Address(0x6b050));
        const uint8_t expected[] = { 0x83, 0xf9, 0x08, 0x73 };
        if (memcmp(reinterpret_cast<void*>(getPlayer), expected, sizeof(expected))) return 0;
        const uint32_t selector = *reinterpret_cast<uint32_t*>(Pattern::Address(0x1d442d8));
        return selector < 8 ? getPlayer(selector) : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int SkillLevel(uintptr_t player, uintptr_t skill, int base, int cachedBonus) {
    const int fallback = CachedSkillLevel(base, cachedBonus);
    __try {
        const auto getBonus = reinterpret_cast<int(__fastcall*)(void*, void*, bool)>(Pattern::Address(0x21a990));
        std::array<uint8_t, 10> code{};
        if (!Read(reinterpret_cast<uintptr_t>(getBonus), code) || !IsSkillBonusFunction(code.data(), code.size())) return fallback;
        return CachedSkillLevel(base, getBonus(reinterpret_cast<void*>(player), reinterpret_cast<void*>(skill), false));
    } __except (EXCEPTION_EXECUTE_HANDLER) { return fallback; }
}

template<typename StatusFor>
std::vector<LiveSkill> ReadAvailableSkills(uintptr_t player, StatusFor&& statusFor) {
    uintptr_t list = 0, node = 0;
    if (!Read(player + 0x100, list) || !Read(list, node)) return {};
    std::set<uintptr_t> visited;
    std::map<int, LiveSkill> unique;
    while (node && visited.size() < 1024 && visited.insert(node).second) {
        SkillMemory skill{}; int16_t skillId = -1;
        if (!Read(node, skill)) break;
        if (Read(skill.txt, skillId) && database.skills.count(skillId)) {
            const int level = SkillLevel(player, node, skill.level, skill.bonus);
            if (level > 0 && (skill.owner == -1 || skill.charges > 0)) {
                auto live = statusFor(skillId, level);
                auto found = unique.find(skillId);
                if (found == unique.end() || level > found->second.level) unique[skillId] = live;
            }
        }
        node = skill.next;
    }
    std::vector<LiveSkill> result;
    for (const auto& entry : unique) result.push_back(entry.second);
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return database.skills.at(a.id).name < database.skills.at(b.id).name; });
    return result;
}

using Row = std::unordered_map<std::string, std::string>;
std::string Lower(std::string value) {
    for (char& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}
std::vector<Row> Table(GameAssets& assets, const std::string& name) {
    const auto bytes = assets.Read("global/excel/" + name + ".txt");
    if (bytes.empty()) return {};
    std::string text(bytes.begin(), bytes.end());
    if (text.compare(0, 3, "\xef\xbb\xbf") == 0) text.erase(0, 3);
    std::istringstream input(text);
    std::string line, value;
    std::vector<std::string> headers;
    std::vector<Row> rows;
    if (!std::getline(input, line)) return rows;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    std::istringstream header(line);
    while (std::getline(header, value, '\t')) headers.push_back(Lower(value));
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        Row row;
        std::istringstream fields(line);
        size_t column = 0;
        while (column < headers.size() && std::getline(fields, value, '\t')) row[headers[column++]] = value;
        rows.push_back(std::move(row));
    }
    return rows;
}
std::string Field(const Row& row, const std::string& key) {
    const auto it = row.find(key);
    return it == row.end() ? "" : it->second;
}
int Number(const std::string& text, int fallback = -1) {
    if (text.empty()) return fallback;
    try { size_t used = 0; int n = std::stoi(text, &used); return used == text.size() ? n : fallback; }
    catch (...) { return fallback; }
}

Database LoadDatabase(const std::filesystem::path& install, const std::string& mod) {
    Database result;
    try {
        GameAssets assets(install, mod);
        const auto skills = Table(assets, "skills"), descriptions = Table(assets, "skilldesc"), states = Table(assets, "states");
        if (skills.empty() || descriptions.empty() || states.empty()) {
            result.error = "Could not read skills.txt, skilldesc.txt or states.txt from this mod/game installation.";
            return result;
        }
        std::map<std::string, int> stateIds;
        for (size_t i = 0; i < states.size(); ++i) {
            const auto& row = states[i];
            if (Field(row, "state") == "Expansion") continue;
            // Self buffs and beneficial target states; curses are not buff choices.
            if (Number(Field(row, "curse"), 0) == 0)
                stateIds[Lower(Field(row, "state"))] = Number(Field(row, "*id"), Number(Field(row, "id"), static_cast<int>(i)));
        }
        std::map<std::string, Row> desc;
        for (const auto& row : descriptions) desc[Lower(Field(row, "skilldesc"))] = row;
        std::map<std::string, std::string> names;
        const auto strings = assets.Read("local/lng/strings/skills.json");
        if (!strings.empty()) {
            auto json = nlohmann::json::parse(strings.begin(), strings.end(), nullptr, false);
            if (json.is_array()) for (const auto& row : json) {
                if (row.is_object() && row.contains("Key") && row["Key"].is_string() && row.contains("enUS") && row["enUS"].is_string())
                    names[row["Key"].get<std::string>()] = row["enUS"].get<std::string>();
            }
        }
        // The HUD uses this array for both native skills and item-granted skills.
        // Resolve its paths from hd/global/ui, including mod-specific ../ paths.
        std::array<std::string, 8> iconPaths = {
            "spells/amazon/amskillicon", "spells/sorceress/soskillicon", "spells/necromancer/neskillicon",
            "spells/paladin/paskillicon", "spells/barbarian/baskillicon", "spells/druid/drskillicon",
            "spells/assassin/asskillicon", "spells/common/skillicon"
        };
        const std::map<std::string, size_t> classes = {{"ama", 0}, {"sor", 1}, {"nec", 2}, {"pal", 3}, {"bar", 4}, {"dru", 5}, {"ass", 6}};
        const auto profileBytes = assets.Read("global/ui/layouts/_profilehd.json");
        auto profile = nlohmann::json::parse(profileBytes.begin(), profileBytes.end(), nullptr, false, true);
        if (profile.is_object() && profile.contains("SkillIconFilenames") && profile["SkillIconFilenames"].is_array()) {
            const auto& files = profile["SkillIconFilenames"];
            for (size_t i = 0; i < iconPaths.size() && i < files.size(); ++i)
                if (files[i].is_string()) iconPaths[i] = files[i].get<std::string>();
        }
        std::set<std::string> failedAtlases;
        for (const auto& row : skills) {
            Skill skill;
            skill.id = Number(Field(row, "*id"), Number(Field(row, "id")));
            if (skill.id < 0) continue;
            for (const char* field : { "aurastate", "auratargetstate", "passivestate" }) {
                auto state = stateIds.find(Lower(Field(row, field)));
                if (state != stateIds.end() && state->second > 0 &&
                    std::find(skill.states.begin(), skill.states.end(), state->second) == skill.states.end()) skill.states.push_back(state->second);
            }
            if (skill.states.empty()) continue;
            skill.name = Field(row, "skill");
            auto description = desc.find(Lower(Field(row, "skilldesc")));
            if (description != desc.end()) {
                auto name = names.find(Field(description->second, "str name"));
                if (name != names.end()) skill.name = name->second;
                skill.frame = Number(Field(description->second, "iconcel"));
            }
            auto cls = classes.find(Lower(Field(row, "charclass")));
            if (skill.frame >= 0) {
                std::string filename = iconPaths[cls != classes.end() ? cls->second : 7];
                std::replace(filename.begin(), filename.end(), '\\', '/');
                auto path = (std::filesystem::path("hd/global/ui") / filename).lexically_normal();
                path.replace_extension(".sprite");
                skill.sprite = path.generic_string();
                if (!result.atlases.count(skill.sprite) && !failedAtlases.count(skill.sprite)) {
                    SpriteAtlas atlas;
                    if (ReadSprite(assets.Read(skill.sprite), atlas)) result.atlases.emplace(skill.sprite, std::move(atlas));
                    else failedAtlases.insert(skill.sprite);
                }
            }
            result.skills.emplace(skill.id, std::move(skill));
        }
    } catch (const std::exception& e) { result.error = e.what(); }
    return result;
}

struct ActiveState { float timeout; bool timed; };
std::map<int, ActiveState> ReadStates(uintptr_t player, bool server, uint32_t frame) {
    std::map<int, ActiveState> result;
    uintptr_t root = 0;
    if (!Read(player + 0x88, root)) return result;
    uint32_t rootFlags = 0;
    if (!Read(root + 0x1c, rootFlags) || !(rootFlags & 0x80000000)) return result;
    for (size_t offset : { size_t(0x68), size_t(0x70) }) {
        uintptr_t node = 0;
        if (!Read(root + offset, node)) continue;
        std::set<uintptr_t> visited;
        while (node && visited.size() < 1024 && visited.insert(node).second) {
            StateMemory state{};
            if (!Read(node, state)) break;
            if (state.state > 0 && state.state < 512) {
                uint32_t bits = 0;
                const bool active = Read(root + 0xac8 + (state.state / 32) * 4, bits) && (bits & (1u << (state.state % 32)));
                const bool timed = (state.flags & 2) != 0;
                const float remaining = RemainingSeconds(state.timeout, timed, server, frame);
                if (active && (!timed || remaining > 0)) {
                    auto it = result.find(state.state);
                    if (it == result.end() || remaining > RemainingSeconds(it->second.timeout, it->second.timed, server, frame))
                        result[state.state] = {state.timeout, timed};
                }
            }
            node = state.previous;
        }
    }
    return result;
}

struct TimerMetrics {
    std::string text;
    ImVec2 size{};
};
TimerMetrics MeasureTimer(float seconds, float scale) {
    if (!config.timers || seconds < 0) return {};
    TimerMetrics timer;
    timer.text = TimerText(seconds, config.timerFormat);
    timer.size = ImGui::GetFont()->CalcTextSizeA(config.timerSize * scale, FLT_MAX, 0, timer.text.c_str());
    return timer;
}
void DrawTimer(ImDrawList* draw, ImVec2 pos, float size, float seconds) {
    const float scale = size / 48.0f;
    const auto timer = MeasureTimer(seconds, scale);
    if (timer.text.empty()) return;
    const bool inside = config.timerPosition >= 2;
    const float inset = 2 * scale;
    float y = pos.y + size + inset;
    if (config.timerPosition == 1) y = pos.y - inset - timer.size.y - scale;
    else if (config.timerPosition == 2) y = pos.y + inset;
    else if (config.timerPosition == 3) y = pos.y + size - inset - timer.size.y - scale;
    const ImVec2 at(pos.x + (size - timer.size.x) / 2, y);
    const ImVec4 clip(pos.x + inset, pos.y + inset, pos.x + size - inset, pos.y + size - inset);
    const auto& color = config.highlightExpiring && seconds <= config.expiringSeconds ? config.expiringColor : config.timerColor;
    ImFont* font = ImGui::GetFont();
    draw->AddText(font, config.timerSize * scale, ImVec2(at.x + scale, at.y + scale), IM_COL32(0, 0, 0, 255), timer.text.c_str(), nullptr, 0, inside ? &clip : nullptr);
    draw->AddText(font, config.timerSize * scale, at, ImGui::ColorConvertFloat4ToU32(color), timer.text.c_str(), nullptr, 0, inside ? &clip : nullptr);
}

void DrawIcon(ImDrawList* draw, const Skill& skill, ImVec2 pos, float size, float seconds, bool active, bool showTimer = true) {
    const ImVec2 end(pos.x + size, pos.y + size);
    auto atlas = database.atlases.find(skill.sprite);
    uint64_t texture = 0;
    if (atlas != database.atlases.end() && skill.frame >= 0 && skill.frame < atlas->second.frames)
        texture = D3D12::GetBuffIconAtlas(skill.sprite, atlas->second);
    if (texture) {
        const auto& data = atlas->second;
        const float textureWidth = static_cast<float>(data.bc3 ? (data.width + 3) & ~3 : data.width);
        const float textureHeight = static_cast<float>(data.bc3 ? (data.height + 3) & ~3 : data.height);
        const float left = float(skill.frame * (data.width / data.frames)) / textureWidth;
        draw->AddImage((ImTextureID)texture, pos, end, ImVec2(left, 0), ImVec2(left + float(data.frameWidth) / textureWidth, float(data.height) / textureHeight), active ? IM_COL32_WHITE : IM_COL32(255, 255, 255, 120));
    } else {
        draw->AddRectFilled(pos, end, IM_COL32(30, 26, 20, 220), 3);
        const auto label = std::to_string(skill.id);
        draw->AddText(ImGui::GetFont(), 17 * (size / 48.0f), ImVec2(pos.x + size / 12, pos.y + size / 12), IM_COL32_WHITE, label.c_str());
    }
    draw->AddRect(pos, end, active ? IM_COL32(199, 179, 119, 230) : IM_COL32(150, 150, 150, 160), 2);
    if (showTimer) DrawTimer(draw, pos, size, seconds);
}

void DrawLabel(ImDrawList* draw, const Skill& skill, ImVec2 pos, float size, float seconds) {
    if (!config.labels || skill.name.empty()) return;
    const float scale = size / 48.0f, inset = 2 * scale;
    const float fontSize = config.labelSize * scale, width = size - 2 * inset;
    ImFont* font = ImGui::GetFont();
    const auto textSize = font->CalcTextSizeA(fontSize, FLT_MAX, width, skill.name.c_str());
    float top = pos.y + inset, bottom = pos.y + size - inset;
    const auto timer = MeasureTimer(seconds, scale);
    if (!timer.text.empty()) {
        if (config.timerPosition == 2 && !config.labelBottom) top += timer.size.y + 2 * scale;
        if (config.timerPosition == 3 && config.labelBottom) bottom -= timer.size.y + 2 * scale;
    }
    if (bottom <= top) return;
    const float height = (std::min)(textSize.y, bottom - top);
    const ImVec2 at(pos.x + inset + (width - textSize.x) / 2, config.labelBottom ? bottom - height : top);
    const ImVec4 clip(pos.x + inset, top, pos.x + size - inset, bottom);
    draw->AddRectFilled(ImVec2(pos.x + inset, at.y), ImVec2(pos.x + size - inset, at.y + height), IM_COL32(0, 0, 0, 150));
    draw->AddText(font, fontSize, at, ImGui::ColorConvertFloat4ToU32(config.labelColor), skill.name.c_str(), nullptr, width, &clip);
}

struct IconAreaLayout {
    ImVec2 extent{};
    std::vector<ImVec2> icons;
};

IconAreaLayout LayoutIcons(const std::vector<LiveSkill>& shown, float scale, float screenWidth) {
    IconAreaLayout layout;
    const float size = 48 * scale, gap = 6 * scale;
    float x = 0, y = 0, above = 0, below = 0;
    int column = 0;
    size_t rowStart = 0;
    auto finishRow = [&]() {
        for (size_t i = rowStart; i < layout.icons.size(); ++i) layout.icons[i].y = y + above;
        layout.extent.y = y + above + size + below;
        y = layout.extent.y + gap;
        rowStart = layout.icons.size();
        x = 0; column = 0; above = 0; below = 0;
    };
    for (const auto& skill : shown) {
        const auto timer = MeasureTimer(skill.seconds, scale);
        const bool external = !timer.text.empty() && config.timerPosition < 2;
        const float width = external ? (std::max)(size, timer.size.x + scale) : size;
        if (column && (column >= config.columns || x + width > screenWidth)) finishRow();
        const float timerHeight = external ? timer.size.y + 3 * scale : 0;
        if (config.timerPosition == 1) above = (std::max)(above, timerHeight);
        else if (config.timerPosition == 0) below = (std::max)(below, timerHeight);
        layout.icons.emplace_back(x + (width - size) / 2, 0.0f);
        layout.extent.x = (std::max)(layout.extent.x, x + width);
        x += width + gap;
        ++column;
    }
    if (column) finishRow();
    // ImGui rounds window sizes to pixels; round outward so fractional scales
    // cannot clip the final text/shadow pixel.
    layout.extent.x = std::ceil(layout.extent.x);
    layout.extent.y = std::ceil(layout.extent.y);
    return layout;
}

} // namespace

void LoadFromJson(const nlohmann::json& root) {
    config = {};
    const auto it = root.find("BuffIcons");
    if (it == root.end() || !it->is_object()) return;
    const auto& j = *it;
    auto flag = [&](const char* key, bool& value) { auto v = j.find(key); if (v != j.end() && v->is_boolean()) value = v->get<bool>(); };
    auto number = [&](const char* key, float& value, float low, float high) {
        auto v = j.find(key); if (v != j.end() && v->is_number()) { float n = v->get<float>(); if (std::isfinite(n)) value = std::clamp(n, low, high); }
    };
    auto color = [&](const char* key, ImVec4& value) {
        auto v = j.find(key);
        if (v == j.end() || !v->is_array() || v->size() != 4) return;
        float* channels[] = {&value.x, &value.y, &value.z, &value.w};
        for (int i = 0; i < 4; ++i) if ((*v)[i].is_number()) {
            float channel = (*v)[i].get<float>();
            if (std::isfinite(channel)) *channels[i] = std::clamp(channel, 0.0f, 1.0f);
        }
    };
    auto choice = [&](const char* key, int& value, const auto& names) {
        auto v = j.find(key);
        if (v != j.end() && v->is_string()) for (size_t i = 0; i < std::size(names); ++i)
            if (*v == names[i]) { value = static_cast<int>(i); break; }
    };
    flag("Enabled", config.enabled); flag("ShowTimers", config.timers);
    choice("TimerPosition", config.timerPosition, timerPositions);
    choice("TimerFormat", config.timerFormat, timerFormats);
    number("TimerSize", config.timerSize, 8, 32);
    color("TimerColor", config.timerColor);
    flag("HighlightExpiring", config.highlightExpiring);
    number("ExpiringSeconds", config.expiringSeconds, 0, 60);
    color("ExpiringColor", config.expiringColor);
    flag("ShowLabels", config.labels);
    number("LabelSize", config.labelSize, 8, 32);
    auto position = j.find("LabelPosition");
    if (position != j.end() && position->is_string()) config.labelBottom = *position != "Top";
    color("LabelColor", config.labelColor);
    number("XPercent", config.x, 0, 100); number("YPercent", config.y, 0, 100); number("Scale", config.scale, 0.5f, 3);
    float columns = 10; number("Columns", columns, 1, 20); config.columns = static_cast<int>(columns);
    auto chars = j.find("Characters");
    if (chars != j.end() && chars->is_object()) for (auto c = chars->begin(); c != chars->end(); ++c) {
        if (!c.value().is_array()) continue;
        for (const auto& id : c.value()) if (id.is_number_integer()) {
            const auto value = id.get<int64_t>(); if (value >= 0 && value <= 32767) config.characters[c.key()].insert(static_cast<int>(value));
        }
    }
}
void SaveToJson(nlohmann::ordered_json& root) {
    root["BuffIcons"] = {{"Enabled", config.enabled}, {"ShowTimers", config.timers}, {"XPercent", config.x}, {"YPercent", config.y},
        {"Scale", config.scale}, {"Columns", config.columns}, {"Characters", config.characters},
        {"ShowLabels", config.labels}, {"LabelPosition", config.labelBottom ? "Bottom" : "Top"}, {"LabelSize", config.labelSize},
        {"LabelColor", {config.labelColor.x, config.labelColor.y, config.labelColor.z, config.labelColor.w}},
        {"TimerPosition", timerPositions[config.timerPosition]}, {"TimerFormat", timerFormats[config.timerFormat]}, {"TimerSize", config.timerSize},
        {"TimerColor", {config.timerColor.x, config.timerColor.y, config.timerColor.z, config.timerColor.w}},
        {"HighlightExpiring", config.highlightExpiring}, {"ExpiringSeconds", config.expiringSeconds},
        {"ExpiringColor", {config.expiringColor.x, config.expiringColor.y, config.expiringColor.z, config.expiringColor.w}}};
}

void Update(D2Client* client, const std::string& mod, bool settingsOpen) {
    if (!config.enabled && !settingsOpen) { available.clear(); selectedStates.clear(); return; }
    if (!loadStarted) {
        wchar_t path[MAX_PATH]{}; GetModuleFileNameW(nullptr, path, MAX_PATH);
        loading = std::async(std::launch::async, LoadDatabase, std::filesystem::path(path).parent_path(), mod);
        loadStarted = true;
    }
    if (!loaded && loading.valid() && loading.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        database = loading.get(); loaded = true;
    }
    available.clear(); selectedStates.clear(); character.clear(); runtimeStatus.clear();
    uintptr_t player = LocalPlayer();
    uint32_t type = 6, id = 0, cls = 0;
    if (!Read(player, type) || type != UNIT_PLAYER || !Read(player + 8, id) || !Read(player + 4, cls)) return;
    uintptr_t playerData = 0;
    std::array<char, 16> name{};
    if (!Read(player + 0x10, playerData) || !Read(playerData, name)) return;
    name.back() = 0;
    character = std::string(name.data()) + ":" + std::to_string(cls);
    bool server = false;
    uint32_t frame = 0;
    // Match the LOCAL player's GUID, not the first player in a TCP/IP game.
    D2Client clientCopy{};
    D2GameStrc gameCopy{};
    if (Read(reinterpret_cast<uintptr_t>(client), clientCopy) && clientCopy.dwClientState == 4 &&
        Read(clientCopy.pGame, gameCopy)) {
        uintptr_t candidate = reinterpret_cast<uintptr_t>(gameCopy.pUnitList[UNIT_PLAYER][id & 127]);
        std::set<uintptr_t> visited;
        while (candidate && visited.size() < 256 && visited.insert(candidate).second) {
            D2UnitStrc unit{};
            if (!Read(candidate, unit)) break;
            if (unit.dwUnitType == UNIT_PLAYER && unit.dwUnitId == id) { player = candidate; server = true; frame = gameCopy.dwGameFrame; break; }
            candidate = reinterpret_cast<uintptr_t>(unit.pListNext);
        }
    }
    uint32_t mode = 0;
    if (!Read(player + 0xc, mode) || mode == 0 || mode == 17) { character.clear(); return; }
    const auto states = ReadStates(player, server, frame);
    auto statusFor = [&](int skillId, int level) {
        LiveSkill live{skillId, level};
        for (int state : database.skills.at(skillId).states) {
            auto active = states.find(state);
            if (active == states.end()) continue;
            const float seconds = RemainingSeconds(active->second.timeout, active->second.timed, server, frame);
            if (!live.active || seconds > live.seconds) live.seconds = seconds;
            live.active = true;
        }
        return live;
    };
    // Keep tracking selected active buffs after a weapon swap removes their skill
    // (for example Battle Orders granted by Call to Arms).
    auto selection = config.characters.find(character);
    if (selection != config.characters.end()) for (int skillId : selection->second)
        if (database.skills.count(skillId)) selectedStates.push_back(statusFor(skillId, 0));

    available = ReadAvailableSkills(player, statusFor);
    runtimeStatus = server ? "Reading server state expiration frames" : "Reading client state durations";
}

void Render(bool preview, float resolutionScale) {
    const bool editing = preview && previewEnabled;
    if ((!config.enabled && !editing) || !loaded) return;
    std::vector<LiveSkill> shown;
    for (const auto& skill : selectedStates)
        if (skill.active || editing) shown.push_back(skill);
    // An empty bar has no area to crop or drag; choices remain in the settings.
    if (shown.empty()) return;
    const float displayScale = config.scale * resolutionScale;
    const float size = 48 * displayScale;
    const ImVec2 screen = ImGui::GetIO().DisplaySize;
    ImFontAtlas* fonts = ImGui::GetIO().Fonts;
    const bool hasBodyFont = fonts->Fonts.Size > 2;
    if (hasBodyFont) ImGui::PushFont(fonts->Fonts[2]);
    const auto layout = LayoutIcons(shown, displayScale, screen.x);
    const ImVec2 extent = layout.extent;
    const float maxX = (std::max)(0.0f, screen.x - extent.x), maxY = (std::max)(0.0f, screen.y - extent.y);
    const ImVec2 origin((std::min)(maxX, screen.x * config.x / 100), (std::min)(maxY, screen.y * config.y / 100));
    auto drawIcons = [&](ImDrawList* draw) {
        for (size_t i = 0; i < shown.size(); ++i) {
            const ImVec2 at(origin.x + layout.icons[i].x, origin.y + layout.icons[i].y);
            const auto& skill = shown[i];
            const auto& data = database.skills.at(skill.id);
            DrawIcon(draw, data, at, size, skill.seconds, skill.active, false);
            DrawLabel(draw, data, at, size, skill.seconds);
            DrawTimer(draw, at, size, skill.seconds);
        }
    };
    if (editing) {
        ImGui::SetNextWindowPos(origin, ImGuiCond_Always);
        ImGui::SetNextWindowSize(extent, ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
        ImGui::SetNextWindowBgAlpha(0.35f);
        const auto flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
            ImGuiWindowFlags_NoFocusOnAppearing;
        if (ImGui::Begin("##BuffIconsPlacement", nullptr, flags)) {
            ImGui::SetCursorScreenPos(origin);
            ImGui::InvisibleButton("Drag buff icons", extent);
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                const auto delta = ImGui::GetIO().MouseDelta;
                config.x = screen.x > 0 ? std::clamp((origin.x + delta.x) * 100 / screen.x, 0.0f, 100.0f) : 0;
                config.y = screen.y > 0 ? std::clamp((origin.y + delta.y) * 100 / screen.y, 0.0f, 100.0f) : 0;
            }
            const bool hovered = ImGui::IsItemHovered();
            auto draw = ImGui::GetWindowDrawList();
            const ImVec2 end(origin.x + extent.x, origin.y + extent.y);
            draw->PushClipRect(origin, end, false);
            drawIcons(draw);
            draw->AddRect(origin, end, ImGui::GetColorU32(ImGuiCol_Border), 0, 0, resolutionScale);
            draw->PopClipRect();
            if (hovered) ImGui::SetTooltip("Drag to move the buff icon area");
        }
        ImGui::End(); ImGui::PopStyleVar(3);
    } else {
        drawIcons(ImGui::GetBackgroundDrawList());
    }
    if (hasBodyFont) ImGui::PopFont();
}

void DrawSettingsPanel(float scale) {
    ImGui::Checkbox("Enable Buff Icons", &config.enabled);
    ImGui::Checkbox("Show remaining time", &config.timers);
    ImGui::Checkbox("Preview and drag icon area", &previewEnabled);
    ImGui::Checkbox("Show skill labels", &config.labels);
    if (config.labels && ImGui::CollapsingHeader("Label appearance")) {
        int position = config.labelBottom ? 1 : 0;
        if (ImGui::Combo("Label position", &position, "Top\0Bottom\0")) config.labelBottom = position == 1;
        ImGui::SliderFloat("Label text size", &config.labelSize, 8, 32, "%.0f px");
        ImGui::ColorEdit4("Label text color", &config.labelColor.x);
    }
    if (config.timers && ImGui::CollapsingHeader("Timer appearance")) {
        ImGui::Combo("Timer position", &config.timerPosition, "Below icon\0Above icon\0Top inside icon\0Bottom inside icon\0");
        ImGui::SliderFloat("Timer text size", &config.timerSize, 8, 32, "%.0f px");
        ImGui::ColorEdit4("Timer text color", &config.timerColor.x);
        ImGui::Combo("Timer format", &config.timerFormat, "Automatic\0Seconds (125s)\0Decimal seconds (1.2s)\0Minutes:seconds (2:05)\0Hours:minutes:seconds (0:02:05)\0");
        ImGui::Checkbox("Highlight expiring buffs", &config.highlightExpiring);
        if (config.highlightExpiring) {
            ImGui::SliderFloat("Warning below", &config.expiringSeconds, 0, 60, "%.0f seconds");
            ImGui::ColorEdit4("Warning text color", &config.expiringColor.x);
        }
    }
    ImGui::SliderFloat("Horizontal position", &config.x, 0, 100, "%.1f%%");
    ImGui::SliderFloat("Vertical position", &config.y, 0, 100, "%.1f%%");
    ImGui::SliderFloat("Icon scale", &config.scale, 0.5f, 3, "%.2fx");
    ImGui::SliderInt("Icons per row", &config.columns, 1, 20);
    if (ImGui::Button("Reset placement")) { config.x = 40; config.y = 12; config.scale = 1; config.columns = 10; }
    ImGui::Separator();
    if (!loaded) { ImGui::TextWrapped("Loading skill data and original icon sprites..."); return; }
    if (!database.error.empty()) {
        ImGui::TextWrapped("%s", database.error.c_str());
        if (ImGui::Button("Retry loading game data")) { loaded = false; loadStarted = false; }
        return;
    }
    if (character.empty()) { ImGui::TextWrapped("Enter a game to choose from your character's available buff skills."); return; }
    ImGui::TextWrapped("Choose skills to show while their effects are active. Choices are saved for each character.");
    ImGui::TextDisabled("%s", runtimeStatus.c_str());
    ImGui::InputText("Filter skills", search, sizeof(search));
    auto& selection = config.characters[character];
    if (ImGui::Button("Select available")) for (const auto& skill : available) selection.insert(skill.id);
    ImGui::SameLine();
    if (ImGui::Button("Clear selection")) selection.clear();
    const char* footer = "Timers follow game simulation time and refresh on recast. Effects without an expiration show an icon without a countdown.";
    const float footerHeight = ImGui::CalcTextSize(footer, nullptr, false, ImGui::GetContentRegionAvail().x).y;
    const float reserved = footerHeight + ImGui::GetFrameHeightWithSpacing() + 2 * ImGui::GetStyle().ItemSpacing.y;
    const float choicesHeight = (std::max)(2 * ImGui::GetTextLineHeightWithSpacing(),
        (std::min)(260 * scale, ImGui::GetContentRegionAvail().y - reserved));
    ImGui::BeginChild("Buff skill choices", ImVec2(0, choicesHeight), true);
    const auto filter = Lower(search);
    if (available.empty()) ImGui::TextWrapped("No available skills with a buff state were found.");
    for (const auto& live : available) {
        const auto& skill = database.skills.at(live.id);
        if (!filter.empty() && Lower(skill.name).find(filter) == std::string::npos) continue;
        ImGui::PushID(live.id);
        bool selected = selection.count(live.id) != 0;
        if (ImGui::Checkbox("##selected", &selected)) {
            if (selected) selection.insert(live.id); else selection.erase(live.id);
        }
        ImGui::SameLine();
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        DrawIcon(ImGui::GetWindowDrawList(), skill, pos, 28 * scale, -1, true, false);
        ImGui::Dummy(ImVec2(28 * scale, 28 * scale)); ImGui::SameLine();
        ImGui::Text("%s (level %d)%s", skill.name.c_str(), live.level, live.active ? " - active" : "");
        if (database.atlases.find(skill.sprite) == database.atlases.end() && ImGui::IsItemHovered())
            ImGui::SetTooltip("Icon sprite unavailable; the skill ID is shown as a fallback.");
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::TextWrapped("%s", footer);
}
}
