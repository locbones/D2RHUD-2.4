//plugin/autosort/autosort.h
#pragma once
#include "../Plugin.h"

class AutoSort : public Plugin {
public:
    bool OnKeyPressed(short key) override;
};