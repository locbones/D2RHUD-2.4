#pragma once

#include "../Plugin.h"
#include "../../D2/D2Ptrs.h"
#include "../D2RHUD/D2RHUD.h"

class ItemFilter : public Plugin {
public:
	bool bInstalled = false;
	bool Install(MonsterStatsDisplaySettings settings);
	void ReloadGameFilter();
	void ReloadGameFilterForGrail();
	void CycleFilter();
	void ClearInvOverrideCache();

	static bool GetShowFilteredItems();
	static void SetShowFilteredItems(bool show);
};

extern bool IsPlayerInGame();
/** Local human's player unit from the client-side unit list (TCP/IP clients; not always unit id 1 on the game server). */
D2UnitStrc* GetClientPlayerUnit();
