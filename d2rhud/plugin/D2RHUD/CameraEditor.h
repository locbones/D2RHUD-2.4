#pragma once
#include "CameraController.h"
#include "HudPreferences.h"
#include <imgui.h>

namespace HudCameraEditor {
struct State {
    bool wasOpen=false,autoloadDone=false;
    int selectedPreset=0,lastPresetIndex=-1;
    uint64_t lastSessionRevision=0,lastCaptureRevision=0;
};
struct Settings {
    std::array<HudPreferences::CameraPreset,3>& presets;
    int& lastPreset;
    bool& autoload;
};
// Explicit presentation services: the editor does not read game memory, own
// config files, or reach into the main HUD's globals. Native writes remain in
// the controller's fresh-boundary callbacks supplied by the runtime host.
struct Host {
    float scale=1;
    ImVec4 backgroundColor{};
    void(*enableInput)();void(*prepare)(ImVec2);
    void(*pushFont)(int);void(*popFont)(int);
    bool(*title)(const char*,bool*);void(*centeredText)(const char*);
    void(*background)();
    bool(*button)(const char*,ImVec2);bool(*checkbox)(const char*,bool*);
    void(*beginFrame)();void(*endFrame)();bool(*save)();
    void(*refresh)();bool(*applyAngles)();bool(*applyZoom)();
};
void Draw(State& state,bool& open,Settings settings,HudCamera::Controller& controller,const Host& host);
}
