#pragma once
#include "FrameContext.h"
#include <Windows.h>
#include "StartupSchedule.h"
#include <string>

namespace HudCommands {
void Configure(const StartupSchedule::Commands& commands);
// Returns an opaque client session generation, or zero outside a game.
uint64_t Tick(HWND window);
uint64_t Tick(HWND window, const HudFrame::Context& context);
bool Queue(const std::string& command);
bool HandleMessage(HWND window,UINT message,WPARAM wParam,LPARAM lParam);
void Shutdown();
void PostKeyTap(HWND window,WORD key,bool systemKey);
std::string TrimToken(const std::string& value);
}
