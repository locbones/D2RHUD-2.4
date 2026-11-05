#include "HudPreferences.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <limits>

namespace HudPreferences {
BindingState::BindingState() : current_(std::make_shared<const Bindings>()) {}
void BindingState::Publish(Bindings bindings) {
    current_.store(std::make_shared<const Bindings>(std::move(bindings)));
}
std::shared_ptr<const Bindings> BindingState::Read() const { return current_.load(); }
const std::pair<std::string, std::string>* FindBinding(const Bindings& bindings, const std::string& name) {
    const auto found = bindings.hotkeys.find(name);
    return found == bindings.hotkeys.end() ? nullptr : &found->second;
}
namespace {
using Json = HudConfig::Json;
const Json* Field(const Json& document, const char* key) {
    if (!document.is_object()) return nullptr;
    const auto value = document.find(key);
    return value == document.end() ? nullptr : &*value;
}
bool Boolean(const Json& document, const char* key, bool fallback) {
    const auto value = Field(document, key);
    return value && value->is_boolean() ? value->get<bool>() : fallback;
}
std::string Text(const Json& document, const char* key, std::string fallback = {}) {
    const auto value = Field(document, key);
    return value && value->is_string() ? value->get<std::string>() : fallback;
}
int Integer(const Json& document, const char* key, int fallback) {
    const auto value = Field(document, key);
    if (!value || !value->is_number_integer()) return fallback;
    if (value->is_number_unsigned()) {
        const auto number = value->get<uint64_t>();
        return number <= uint64_t(INT_MAX) ? int(number) : fallback;
    }
    const auto number = value->get<int64_t>();
    return number >= INT_MIN && number <= INT_MAX ? int(number) : fallback;
}
float Number(const Json& document, const char* key, float fallback = 0) {
    const auto value = Field(document, key);
    if (!value || !value->is_number()) return fallback;
    const auto number = value->get<double>();
    return std::isfinite(number) && std::abs(number) <= (std::numeric_limits<float>::max)()
        ? float(number) : fallback;
}
struct BoolField { const char* name; bool Config::* member; };
constexpr BoolField BooleanFields[]{
    {"Inspect", &Config::Inspect},
    {"InspectLabels", &Config::InspectLabels},
    {"MonsterStatsDisplay", &Config::MonsterStatsDisplay},
    {"HPRolloverMods", &Config::HPRolloverMods},
    {"SunderedMonUMods", &Config::SunderedMonUMods},
    {"MinionEquality", &Config::MinionEquality},
    {"PersistentSummons", &Config::PersistentSummons},
    {"MercSellerUnlock", &Config::MercSellerUnlock},
    {"MercIconsFix", &Config::MercIconsFix},
    {"MultipleAurasPerItem", &Config::MultipleAurasPerItem},
    {"ActVI", &Config::ActVI},
    {"SurvivorArena", &Config::SurvivorArena},
    {"D3StyleWhirlwind", &Config::D3StyleWhirlwind},
    {"NoWidescreenBars", &Config::NoWidescreenBars},
    {"DynamicAutomap", &Config::DynamicAutomap},
    {"QuantityRightClicks", &Config::QuantityRightClicks},
    {"DisableRightClickItemPickup", &Config::DisableRightClickItemPickup},
    {"MinimalistQuantityPopup", &Config::MinimalistQuantityPopup},
    {"GambleCostControl", &Config::GambleCostControl},
    {"CombatLog", &Config::CombatLog},
    {"TransmogVisuals", &Config::TransmogVisuals},
    {"ExtendedItemcodes", &Config::ExtendedItemcodes},
    {"FloatingDamage", &Config::FloatingDamage},
    {"ChatSounds", &Config::ChatSounds},
    {"CameraAutoloadLastPreset", &Config::CameraAutoloadLastPreset}
};
constexpr BoolField CubeFields[]{
    {"MultipleUseitemOutputs", &Config::MultipleUseitemOutputs},
    {"MultipleUseitemInputs", &Config::MultipleUseitemInputs},
    {"ExtendedCubeConditions", &Config::ExtendedCubeConditions},
    {"BulkCubeOutputs", &Config::BulkCubeOutputs},
    {"ExtendedCubeOutputs", &Config::ExtendedCubeOutputs}
};
Json& Object(Json& document, const char* key) {
    auto& value = document[key];
    if (!value.is_object()) value = Json::object();
    return value;
}
}

void Load(Config& config, const HudConfig::Json& document, bool rmdDefaults) {
    for (const auto& field : BooleanFields)
        config.*field.member = Boolean(document, field.name, config.*field.member);
    for (const auto& field : CubeFields)
        config.*field.member = Boolean(document, field.name, rmdDefaults);
    config.HPRolloverPercent = Integer(document, "HPRollover%", config.HPRolloverPercent);
    config.HPRolloverDifficulty = Integer(document, "HPRolloverDifficulty", config.HPRolloverDifficulty);
    config.SunderValue = 99;
    config.InspectIconHeight = std::clamp(Integer(document, "InspectIconHeight", config.InspectIconHeight), 0, 240);
    config.InspectLabelSize = std::clamp(Integer(document, "InspectLabelSize", config.InspectLabelSize), 16, 40);
    config.DynamicAutomapInterval = std::clamp(Integer(document, "DynamicAutomapInterval", config.DynamicAutomapInterval), 50, 5000);
    config.ChannelColor = Text(document, "Channel Color", config.ChannelColor);
    config.PlayerNameColor = Text(document, "Player Name Color", config.PlayerNameColor);
    config.MessageColor = Text(document, "Message Color", config.MessageColor);
    config.ChatSoundPath = Text(document, "ChatSoundPath", config.ChatSoundPath);
    config.ChatNotificationVolume = std::clamp(Integer(document, "ChatNotificationVolume", config.ChatNotificationVolume), 0, 100);
    config.CameraLastPresetIndex = Integer(document, "CameraLastPresetIndex", config.CameraLastPresetIndex);
    if (config.CameraLastPresetIndex < 0 || config.CameraLastPresetIndex > 3)
        config.CameraLastPresetIndex = 0;
    if (const auto libraries = Field(document, "DLLsToLoad"); libraries && libraries->is_array()) {
        std::vector<std::string> names;
        for (const auto& name : *libraries) if (name.is_string()) names.push_back(name.get<std::string>());
        config.DLLsToLoad = std::move(names);
    }
    if (const auto presets = Field(document, "CameraPresets"); presets && presets->is_array()) {
        for (size_t i = 0; i < config.CameraPresets.size() && i < presets->size(); ++i) {
            const auto& source = (*presets)[i];
            if (!source.is_object()) continue;
            auto& preset = config.CameraPresets[i];
            preset.Name = Text(source, "Name");
            preset.Pitch = Number(source, "Pitch");
            preset.Height = Number(source, "Height");
            preset.Pan = Number(source, "Pan");
            preset.Roll = Number(source, "Roll");
            preset.Zoom = Number(source, "Zoom");
            preset.HasValues = Boolean(source, "HasValues", true);
        }
    }
}

void Save(const Config& config, HudConfig::Json& document) {
    for (const auto& field : BooleanFields) document[field.name] = config.*field.member;
    for (const auto& field : CubeFields) document[field.name] = config.*field.member;
    document["HPRollover%"] = config.HPRolloverPercent;
    document["HPRolloverDifficulty"] = config.HPRolloverDifficulty;
    document["SunderValue"] = 99;
    document["InspectIconHeight"] = config.InspectIconHeight;
    document["InspectLabelSize"] = config.InspectLabelSize;
    document["DynamicAutomapInterval"] = std::clamp(config.DynamicAutomapInterval, 50, 5000);
    document["Channel Color"] = config.ChannelColor;
    document["Player Name Color"] = config.PlayerNameColor;
    document["Message Color"] = config.MessageColor;
    document["ChatSoundPath"] = config.ChatSoundPath;
    document["ChatNotificationVolume"] = std::clamp(config.ChatNotificationVolume, 0, 100);
    document["DLLsToLoad"] = config.DLLsToLoad;
    document["CameraLastPresetIndex"] = config.CameraLastPresetIndex;
    auto& presets = document["CameraPresets"];
    if (!presets.is_array()) presets = Json::array();
    // Always retain all three slots, including unnamed empty slots.
    for (size_t i = 0; i < config.CameraPresets.size(); ++i) {
        if (presets.size() <= i) presets.push_back(Json::object());
        auto& target = presets[i];
        if (!target.is_object()) target = Json::object();
        const auto& preset = config.CameraPresets[i];
        target["Name"] = preset.Name;
        target["Pitch"] = preset.Pitch;
        target["Height"] = preset.Height;
        target["Pan"] = preset.Pan;
        target["Roll"] = preset.Roll;
        target["Zoom"] = preset.Zoom;
        target["HasValues"] = preset.HasValues;
    }
}

Bindings LoadBindings(const HudConfig::Json& document) {
    Bindings bindings;
    if (const auto keys = Field(document, "Keybinds"); keys && keys->is_object()) {
        for (const auto& [name, entry] : keys->items()) {
            if (!entry.is_object()) continue;
            std::string extra = Text(entry, "Extra");
            if (extra.empty()) if (const auto enabled = Field(entry, "Enabled"); enabled && enabled->is_boolean())
                extra = enabled->get<bool>() ? "Enabled" : "Disabled";
            bindings.hotkeys[name] = {Text(entry, "Key"), std::move(extra)};
        }
    }
    if (const auto commands = Field(document, "Commands"); commands && commands->is_object()) {
        bindings.startup = Text(*commands, "Startup Commands");
        if (const auto custom = Field(*commands, "Custom Commands"); custom && custom->is_array()) {
            // Invalid slots remain empty rather than shifting subsequent commands.
            for (size_t i = 0; i < custom->size() && i < 6; ++i)
                bindings.commands.push_back({Text((*custom)[i], "Key"), Text((*custom)[i], "Command")});
        }
    }
    bindings.commands.resize(6);
    bindings.hotkeys.erase("Inspect"); // Inspection now uses town eye icons.
    return bindings;
}

void SaveBindings(const Bindings& bindings, HudConfig::Json& document) {
    auto& keys = Object(document, "Keybinds");
    keys.erase("Inspect");
    for (const auto& [name, binding] : bindings.hotkeys) {
        if (name == "Inspect") continue;
        auto& entry = Object(keys, name.c_str());
        entry["Key"] = binding.first;
        if (binding.second == "Enabled" || binding.second == "Disabled") {
            entry["Enabled"] = binding.second == "Enabled";
            entry.erase("Extra");
        } else entry["Extra"] = binding.second;
    }
    auto& commands = Object(document, "Commands");
    commands["Startup Commands"] = bindings.startup;
    auto& custom = commands["Custom Commands"];
    if (!custom.is_array()) custom = Json::array();
    for (size_t i = 0; i < 6; ++i) {
        if (custom.size() <= i) custom.push_back(Json::object());
        auto& entry = custom[i];
        if (!entry.is_object()) entry = Json::object();
        entry["Key"] = i < bindings.commands.size() ? bindings.commands[i].key : "";
        entry["Command"] = i < bindings.commands.size() ? bindings.commands[i].command : "";
    }
}
}
