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
    static bool IsAnyMenuOpen();

private:
	void ShowVersionMessage();
	bool ctrlPressed = false;
	bool altPressed = false;
	bool vPressed = false;
};

extern std::string g_ItemFilterStatusMessage;
extern bool g_ShouldShowItemFilterMessage;
extern std::chrono::steady_clock::time_point g_ItemFilterMessageStartTime;

