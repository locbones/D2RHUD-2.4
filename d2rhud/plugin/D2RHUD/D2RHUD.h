#pragma once
#include "../Plugin.h"
#include "../../D2/D2Ptrs.h"
#include <chrono>

struct MonsterStatsDisplaySettings {
    bool monsterStatsDisplay;
    std::string channelColor;
    std::string playerNameColor;
    std::string messageColor;
    bool HPRollover;
    std::int32_t HPRolloverAmt;
    std::int32_t HPRolloverDiff;
    bool sunderedMonUMods;
    bool minionEquality;
    bool gambleForce;
    std::int32_t SunderValue;
    bool CombatLog;
    bool TransmogVisuals;
    bool ExtendedItemcodes;
    bool FloatingDamage;
};

class D2RHUD : public Plugin {
public:
	void OnDraw() override;
	bool OnKeyPressed(short key) override;
    bool TryCloseMenuOnEscape() override;
    static bool IsAnyMenuOpen();
    

private:
	void ShowVersionMessage();
	bool ctrlPressed = false;
	bool altPressed = false;
	bool vPressed = false;
};

struct FoundLocation {
    int page;
    int tab;
    int x;
    int y;
};

struct SetItemEntry {
    std::string name;
    int id;
    std::string setName;
    std::string itemName;
    std::string code;
    bool enabled = false;
    bool collected = false;
    std::vector<FoundLocation> locations;
};

struct UniqueItemEntry {
    int index;
    int id;
    std::string name;
    std::string code;
    std::string itemName;
    bool enabled = false;
    bool collected = false;
    std::vector<FoundLocation> locations;
};

extern std::string g_ItemFilterStatusMessage;
extern bool g_ShouldShowItemFilterMessage;
extern std::chrono::steady_clock::time_point g_ItemFilterMessageStartTime;
extern std::string modName;
extern std::vector<SetItemEntry> g_SetItems;
extern std::vector<UniqueItemEntry> g_UniqueItems;

