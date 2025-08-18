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
};

class D2RHUD : public Plugin {
public:
	void OnDraw() override;
	bool OnKeyPressed(short key) override;

private:
	void ShowVersionMessage();
	bool ctrlPressed = false;
	bool altPressed = false;
	bool vPressed = false;
};

extern std::string g_ItemFilterStatusMessage;
extern bool g_ShouldShowItemFilterMessage;
extern std::chrono::steady_clock::time_point g_ItemFilterMessageStartTime;

