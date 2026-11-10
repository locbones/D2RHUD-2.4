#pragma once
#include "HudConfigStore.h"
#include "ChatAppearance.h"
#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace HudPreferences {
struct CameraPreset
{
    std::string Name;
    float Pitch = 0.0f;
    float Height = 0.0f;
    float Pan = 0.0f;
    float Roll = 0.0f;
    float Zoom = 0.0f;
    bool HasValues = false;
};

struct Config
{
    bool MonsterStatsDisplay = true;
    bool Inspect = true;
    bool InspectLabels = false;
    int InspectIconHeight = 96;
    int InspectLabelSize = 22;
    std::string ChannelColor = "ÿc5";
    std::string PlayerNameColor = "ÿc8";
    std::string MessageColor = "ÿc0";
    bool HPRolloverMods = true;
    int HPRolloverPercent = 99;
    int HPRolloverDifficulty = -1;
    bool SunderedMonUMods = true;
    int SunderValue = 99;
    bool MinionEquality = true;
    bool PersistentSummons = false;
    bool MercSellerUnlock = true;
    bool MercIconsFix = true;
    bool MultipleAurasPerItem = true;
    bool MultipleUseitemOutputs = false;
    bool MultipleUseitemInputs = false;
    bool ExtendedCubeConditions = false;
    bool BulkCubeOutputs = false;
    bool ExtendedCubeOutputs = false;
    bool SurvivorArena = false;
    bool ActVI = false;
    bool D3StyleWhirlwind = false;
    bool NoWidescreenBars = false;
    bool DynamicAutomap = true;
    int DynamicAutomapInterval = 100;
    bool QuantityRightClicks = true;
    bool DisableRightClickItemPickup = false;
    bool MinimalistQuantityPopup = false;
    bool GambleCostControl = true;
    bool CombatLog = true;
    bool TransmogVisuals = true;
    bool ExtendedItemcodes = true;
    bool FloatingDamage = true;
    bool ChatSounds = true;
    HudChatAppearance::Settings ChatAppearance;
    int ChatNotificationVolume = 30;
    std::string ChatSoundPath;
    std::vector<std::string> DLLsToLoad = { "D2RHUD.dll" };
    std::array<CameraPreset, 3> CameraPresets{};
    bool CameraAutoloadLastPreset = false;
    int CameraLastPresetIndex = 0;  // 0 = Default, 1-3 = preset slot
};

using Hotkeys = std::unordered_map<std::string, std::pair<std::string, std::string>>;
struct Command { std::string key, command; };
struct Bindings {
    Hotkeys hotkeys;
    std::string startup;
    std::vector<Command> commands;
};
// Input callbacks keep one immutable view while the render thread edits bindings.
class BindingState {
public:
    BindingState();
    void Publish(Bindings bindings);
    std::shared_ptr<const Bindings> Read() const;
private:
    std::atomic<std::shared_ptr<const Bindings>> current_;
};
const std::pair<std::string, std::string>* FindBinding(const Bindings& bindings, const std::string& name);
void Load(Config& config, const HudConfig::Json& document, bool rmdDefaults);
void Save(const Config& config, HudConfig::Json& document);
Bindings LoadBindings(const HudConfig::Json& document);
void SaveBindings(const Bindings& bindings, HudConfig::Json& document);
}
