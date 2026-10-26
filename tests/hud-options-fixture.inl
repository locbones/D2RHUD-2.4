#include "../d2rhud/plugin/D2RHUD/OptionsLayout.h"
#include "../d2rhud/plugin/D2RHUD/OptionsCatalog.h"
#include <tuple>

static bool showD2RHUDMenu=true;
static std::string modName="RMD-MP",configFilePath="fixture.json";
struct OptionSettings {
    bool MonsterStatsDisplay=true,SunderedMonUMods=true,MinionEquality=false,GambleCostControl=false,CombatLog=false,TransmogVisuals=true;
    bool NoWidescreenBars=false,DisableRightClickItemPickup=false,QuantityRightClicks=true,MinimalistQuantityPopup=false;
    bool PersistentSummons=false,D3StyleWhirlwind=false,ExtendedItemcodes=false,ChatSounds=false,FloatingDamage=false;
    bool HPRolloverMods=false,SurvivorArena=false,ActVI=false;
    int HPRolloverDifficulty=0,HPRolloverPercent=0,SunderValue=99;
    std::string ChatSoundPath;
} d2rHUDConfig;
struct Cached {
    bool HPRollover=false,sunderedMonUMods=false,monsterStatsDisplay=false,minionEquality=false,gambleForce=false;
    bool CombatLog=false,TransmogVisuals=false,ExtendedItemcodes=false,FloatingDamage=false;
    int HPRolloverDiff=0,HPRolloverAmt=0,SunderValue=99;
} cachedSettings,settings;
struct ModOverrideSettings {
    bool MonsterStatsDisplay,SunderedMonUMods,MinionEquality,GambleCostControl,CombatLog,TransmogVisuals;
    bool ExtendedItemcodes,FloatingDamage,HPRolloverMods,HPRolloverDifficulty,HPRolloverPercent,SunderValue;
};
struct LockedValueInfo {bool locked=true;std::string reason="Required by this mod";};
static bool lockMonster=false;
static const LockedValueInfo* GetLockInfo(const std::string&,bool ModOverrideSettings::*member){
    static LockedValueInfo info;return lockMonster&&member==&ModOverrideSettings::MonsterStatsDisplay?&info:nullptr;
}
static int saves=0;
static bool SaveFullGrailConfig(const std::string&,bool){++saves;return true;}
static std::unordered_map<std::string,ImRect> optionRects;
static bool ThemeCheckbox(const char* label,bool* value){const bool changed=ImGui::Checkbox(label,value);optionRects[label]={ImGui::GetItemRectMin(),ImGui::GetItemRectMax()};return changed;}
static bool ShouldDrawWindowBackgroundImage(int){return false;}
static void DrawWindowBackgroundImage(int){}
static void DrawWindowTitleAndClose(const char*,bool*){}
static void BeginFrameBgImageRegion(){}
static void EndFrameBgImageRegion(){}
static bool DrawChatSoundSelector(float){ImGui::Button("Automatic");return false;}
static void InvalidateChatSoundPathCache(){}
constexpr int kWindowBg_D2RHUDOptions=0;
static ImVec4 s_WindowBgColors[1]{{.05f,.04f,.03f,1}};
namespace SunderSystem {void SetEnabled(bool){}}
namespace WidescreenBars {bool enabled=false;bool Available(){return true;}void SetEnabled(bool value){enabled=value;}const char* Status(){return "Ready";}}
namespace FloatingDamage {bool enabled=false;void SetEnabled(bool value){enabled=value;}}
namespace SummonPersistence {void SetEnabled(bool){}const char* Status(){return "Ready";}std::string StorageStatus(){return "Saved per character";}}
namespace LegacyTransmog {void SetQuantityRightClicks(bool){}void SetMinimalistQuantityPopup(bool){}const char* StackSplitStatus(){return "Ready";}}
namespace D3Whirlwind {void SetEnabled(bool){}void SetRightClickPickupDisabled(bool){}}
namespace ItemCodes {bool Installed(){return false;}}
namespace ExtendedActs {bool enabled=false;void SetRequested(bool value){enabled=value;}bool Requested(){return enabled;}bool Active(){return false;}const char* Status(){return "Ready";}}
namespace CubeMap {void SetSurvivorsEnabled(bool){}const char* Status(){return "Ready";}}
#include "../d2rhud/plugin/D2RHUD/OptionsUI.inl"
