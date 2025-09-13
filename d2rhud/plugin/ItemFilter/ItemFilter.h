#pragma once

#include "../Plugin.h"
#include "../../D2/D2Ptrs.h"
#include "../D2RHUD/D2RHUD.h"

class ItemFilter : public Plugin {
public:
	bool bInstalled = false;
	bool Install(MonsterStatsDisplaySettings settings);
	void ReloadGameFilter();
	void CycleFilter();
};