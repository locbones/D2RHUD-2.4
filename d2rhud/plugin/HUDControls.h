#pragma once
#include <windows.h>

namespace HUDControls {
bool Install();
void Uninstall();
bool Installed();
const char* Status();
// Called after the old HUD configuration has loaded, to migrate its shortcut.
void Configure(const char* legacyBinding);
void Tick();
bool HandleInput(UINT message, WPARAM key, LPARAM detail, bool textInput);
bool TakeToggle();
}
