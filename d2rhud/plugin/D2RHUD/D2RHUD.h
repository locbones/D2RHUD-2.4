#pragma once
#include "../Plugin.h"
#include "../../D2/D2Ptrs.h"

struct MonsterStatsDisplaySettings {
    bool monsterStatsDisplay;
    std::string channelColor;
    std::string playerNameColor;
    std::string messageColor;
    bool socketDisplay;
    bool HPRollover;
    std::int32_t HPRolloverAmt;
    std::int32_t HPRolloverDiff;
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

