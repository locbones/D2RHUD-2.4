#pragma once
#include "../Plugin.h"

class Sample : public Plugin {
public:
	void OnDraw() override;
	bool OnKeyPressed(short key) override;
};

