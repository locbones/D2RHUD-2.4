//plugin/autosort/autosort.h
#pragma once
#include "../Plugin.h"

class Transmute : public Plugin {
public:
    bool OnKeyPressed(short key) override;
};