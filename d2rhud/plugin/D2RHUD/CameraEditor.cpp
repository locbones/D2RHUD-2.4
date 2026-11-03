#include "CameraEditor.h"
#include "MenuWorkspace.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace HudCameraEditor {
void Draw(State& state,bool& showCameraMenu,Settings settings,HudCamera::Controller& controller,const Host& host)
{
    if (!showCameraMenu)
    {
        state.wasOpen = false;
        return;
    }
    if (!state.wasOpen) host.refresh();
    const bool justOpened = !state.wasOpen || state.lastSessionRevision != controller.SessionRevision();
    state.wasOpen = true;
    state.lastSessionRevision = controller.SessionRevision();
    if (justOpened)
    {
        state.autoloadDone = false;
        state.lastPresetIndex = -1;
        state.selectedPreset = settings.autoload
            ? std::clamp(settings.lastPreset, 0, 3) : 0;
    }
    auto& camera = controller.View();
    if (state.lastCaptureRevision != controller.CaptureRevision()) {
        state.lastCaptureRevision = controller.CaptureRevision();
        state.autoloadDone = false;
    }
    controller.Request(justOpened);
    const auto ApplyAngleValues = [&] { host.applyAngles(); };
    const auto ApplyZoomValue = [&] { host.applyZoom(); };
    const bool angleScanning = controller.Busy() && !camera.valuesFound;
    const bool zoomScanning = controller.Busy() && !camera.zoomFound;

    // Only autoload after both scans have completed and default values are captured
    if (!state.autoloadDone
        && camera.available
        && settings.autoload
        && settings.lastPreset >= 1
        && settings.lastPreset <= 3
        && !angleScanning && !zoomScanning
        && camera.valuesFound && camera.zoomFound)
    {
        const HudPreferences::CameraPreset& p = settings.presets[settings.lastPreset - 1];
        if (p.HasValues)
        {
            camera.pitch = p.Pitch;
            camera.height = p.Height;
            camera.pan = p.Pan;
            camera.roll = p.Roll;
            camera.zoom = p.Zoom;

            ApplyAngleValues();
            ApplyZoomValue();
        }

        // Either we applied a preset or there was nothing to apply;
        state.autoloadDone = true;
    }

    host.enableInput();
    float menuScale = host.scale;
    ImVec2 windowSize = ImVec2(680.0f, 380.0f);
    host.prepare(windowSize);

    ImGui::PushStyleColor(ImGuiCol_WindowBg, host.backgroundColor);
    host.pushFont(3);
    if (HudMenu::Begin("Camera Controls", &showCameraMenu, ImGuiWindowFlags_NoTitleBar))
    {
        if (host.background) host.background();
        host.title("Camera Controls", &showCameraMenu);
        host.popFont(3);

        ImGui::Separator();
        ImGui::Dummy(ImVec2(0.0f, 4.0f * menuScale));

        // Top info line
        host.pushFont(1);
        host.centeredText("Adjust in-game camera pitch, height, pan, roll, and zoom.");
        host.centeredText("Camera values are scanned automatically when you open this window.");
        host.popFont(1);

        ImGui::Dummy(ImVec2(0.0f, 4.0f * menuScale));

        // Status text (auto-scan runs on open)
        ImVec4 okColor = ImVec4(0.4f, 0.9f, 0.4f, 1.0f);
        ImVec4 warnColor = ImVec4(0.9f, 0.7f, 0.2f, 1.0f);
        ImVec4 errColor = ImVec4(0.9f, 0.3f, 0.3f, 1.0f);

        ImVec4 angleColor = camera.valuesFound ? okColor : (camera.didScanAngles ? errColor : warnColor);
        ImVec4 zoomColor = camera.zoomFound ? okColor : (camera.didScanZoom ? errColor : warnColor);

        const char* angleStatus = camera.loading ? "loading..." : !camera.available ? "enter a game" : angleScanning ? "scanning..."
            : (camera.valuesFound ? "found" : (camera.didScanAngles ? "unavailable (scan/read failed)" : "not scanned"));
        const char* zoomStatus = camera.loading ? "loading..." : !camera.available ? "enter a game" : zoomScanning ? "scanning..."
            : (camera.zoomFound ? "found" : (camera.didScanZoom ? "unavailable (scan/read failed)" : "not scanned"));

        char statusBuf[96];
        snprintf(statusBuf, sizeof(statusBuf), "Angles: %s  Zoom: %s", angleStatus, zoomStatus);
        float statusWidth = HudText::CalcTextSize(statusBuf).x;
        float avail = ImGui::GetContentRegionAvail().x;
        const float statusOffset = 8.0f * menuScale;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - statusWidth) * 0.5f + statusOffset);
        HudText::TextColored(angleColor, "Angles: %s", angleStatus);
        ImGui::SameLine();
        HudText::TextColored(zoomColor, "Zoom: %s", zoomStatus);

        ImGui::Dummy(ImVec2(0.0f, 6.0f * menuScale));

        const float cameraLabelWidth = 90.0f * menuScale;
        const float cameraInputWidth = 200.0f * menuScale;
        const float presetColWidth = 350.0f * menuScale;

        // Left: Presets | Right: All values + reset buttons
        HudWindowLayout::PrepareLeftPane(menuScale);
        ImGui::BeginChild("CameraPresets", ImVec2(presetColWidth, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
        HudText::TextColored(ImVec4(0.8f, 0.8f, 0.3f, 1.0f), "Presets");
        ImGui::Separator();

        static char presetNameBuf[64] = {};
        if (state.lastPresetIndex != state.selectedPreset)
        {
            HudPreferences::CameraPreset p{};
            if (state.selectedPreset > 0 && state.selectedPreset <= static_cast<int>(settings.presets.size()))
            {
                p = settings.presets[state.selectedPreset - 1];

                // When switching to a saved preset, update the input values to match it
                if (p.HasValues)
                {
                    camera.pitch = p.Pitch;
                    camera.height = p.Height;
                    camera.pan = p.Pan;
                    camera.roll = p.Roll;
                    camera.zoom = p.Zoom;
                }
            }
            else if (state.selectedPreset == 0 && camera.valuesFound && camera.zoomFound)
            {
                // Default slot: show the captured in-game defaults in the inputs
                camera.pitch = camera.defaultPitch;
                camera.height = camera.defaultHeight;
                camera.pan = camera.defaultPan;
                camera.roll = camera.defaultRoll;
                camera.zoom = camera.defaultZoom;
            }

            strncpy(presetNameBuf, p.Name.c_str(), sizeof(presetNameBuf));
            presetNameBuf[sizeof(presetNameBuf) - 1] = '\0';
            state.lastPresetIndex = state.selectedPreset;
        }

        static char presetLabel0[64] = "Default";
        static char presetLabel1[64] = "Preset 1";
        static char presetLabel2[64] = "Preset 2";
        static char presetLabel3[64] = "Preset 3";
        for (int i = 0; i < 3; ++i)
        {
            char* buf = (i == 0) ? presetLabel1 : (i == 1) ? presetLabel2 : presetLabel3;
            const HudPreferences::CameraPreset& p = settings.presets[i];
            if (!p.Name.empty())
            {
                strncpy(buf, p.Name.c_str(), sizeof(presetLabel1) - 1);
                buf[sizeof(presetLabel1) - 1] = '\0';
            }
            else
            {
                snprintf(buf, sizeof(presetLabel1), "Preset %d", i + 1);
            }
        }
        const char* presetLabels[4] = { presetLabel0, presetLabel1, presetLabel2, presetLabel3 };
        ImGui::AlignTextToFramePadding();
        HudText::Text("Slot");
        ImGui::SameLine(cameraLabelWidth);
        ImGui::SetNextItemWidth(-1);
        host.beginFrame();
        HudText::Combo("##PresetSlot", &state.selectedPreset, presetLabels, IM_ARRAYSIZE(presetLabels));
        host.endFrame();

        ImGui::AlignTextToFramePadding();
        HudText::Text("Name");
        ImGui::SameLine(cameraLabelWidth);
        ImGui::SetNextItemWidth(-1);
        host.beginFrame();
        HudText::InputText("##PresetName", presetNameBuf, sizeof(presetNameBuf));
        host.endFrame();

        ImGui::BeginDisabled(!camera.available || !camera.valuesFound || !camera.zoomFound);
        if (host.button("Save Current to Preset", ImVec2(-1, 0)))
        {
            if (state.selectedPreset > 0 && state.selectedPreset <= static_cast<int>(settings.presets.size()))
            {
                HudPreferences::CameraPreset& p = settings.presets[state.selectedPreset - 1];
                p.Name = presetNameBuf;
                p.Pitch = camera.pitch;
                p.Height = camera.height;
                p.Pan = camera.pan;
                p.Roll = camera.roll;
                p.Zoom = camera.zoom;
                p.HasValues = true;
                host.save();
            }
        }
        ImGui::EndDisabled();
        ImGui::BeginDisabled(!camera.available || (!camera.valuesFound && !camera.zoomFound));
        if (host.button("Apply Preset", ImVec2(-1, 0)))
        {
            if (state.selectedPreset == 0)
            {
                if (camera.valuesFound) {
                    camera.pitch = camera.defaultPitch;
                    camera.height = camera.defaultHeight;
                    camera.pan = camera.defaultPan;
                    camera.roll = camera.defaultRoll;
                    ApplyAngleValues();
                }
                if (camera.zoomFound) {
                    camera.zoom = camera.defaultZoom;
                    ApplyZoomValue();
                }
            }
            else if (state.selectedPreset > 0 && state.selectedPreset <= static_cast<int>(settings.presets.size()))
            {
                const HudPreferences::CameraPreset& p = settings.presets[state.selectedPreset - 1];
                if (p.HasValues)
                {
                    camera.pitch = p.Pitch;
                    camera.height = p.Height;
                    camera.pan = p.Pan;
                    camera.roll = p.Roll;
                    camera.zoom = p.Zoom;
                    if (camera.valuesFound) ApplyAngleValues();
                    if (camera.zoomFound) ApplyZoomValue();
                }
            }
            settings.lastPreset = state.selectedPreset;
            host.save();
        }
        ImGui::EndDisabled();
        if (host.checkbox("Autoload last preset", &settings.autoload))
            host.save();
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("CameraValues", ImVec2(0, 0), true);
        HudText::TextColored(ImVec4(0.8f, 0.8f, 0.3f, 1.0f), "Camera values");
        ImGui::Separator();

        ImGui::BeginDisabled(!camera.available || !camera.programEnabled || !camera.valuesFound);
        ImGui::AlignTextToFramePadding();
        HudText::Text("Pitch");
        ImGui::SameLine(cameraLabelWidth);
        ImGui::SetNextItemWidth(cameraInputWidth);
        if (HudText::DragFloat("##Pitch", &camera.pitch, 0.01f, -10.0f, 10.0f, "%.3f"))
            ApplyAngleValues();
        ImGui::AlignTextToFramePadding();
        HudText::Text("Yaw");
        ImGui::SameLine(cameraLabelWidth);
        ImGui::SetNextItemWidth(cameraInputWidth);
        if (HudText::DragFloat("##Pan", &camera.pan, 0.01f, -10.0f, 10.0f, "%.3f"))
            ApplyAngleValues();
        ImGui::AlignTextToFramePadding();
        HudText::Text("Roll");
        ImGui::SameLine(cameraLabelWidth);
        ImGui::SetNextItemWidth(cameraInputWidth);
        if (HudText::DragFloat("##Roll", &camera.roll, 0.01f, -10.0f, 10.0f, "%.3f"))
            ApplyAngleValues();
        ImGui::AlignTextToFramePadding();
        HudText::Text("Height");
        ImGui::SameLine(cameraLabelWidth);
        ImGui::SetNextItemWidth(cameraInputWidth);
        if (HudText::DragFloat("##Height", &camera.height, 0.01f, -10.0f, 10.0f, "%.3f"))
            ApplyAngleValues();
        ImGui::EndDisabled();

        ImGui::BeginDisabled(!camera.available || !camera.programEnabled || !camera.zoomFound);
        ImGui::AlignTextToFramePadding();
        HudText::Text("Zoom");
        ImGui::SameLine(cameraLabelWidth);
        ImGui::SetNextItemWidth(cameraInputWidth);
        if (HudText::DragFloat("##Zoom", &camera.zoom, 0.01f, -5.0f, 5.0f, "%.3f"))
            ApplyZoomValue();
        ImGui::EndDisabled();

        ImGui::Dummy(ImVec2(0.0f, 4.0f * menuScale));
        ImGui::BeginDisabled(!camera.available || !camera.valuesFound);
        if (host.button("Reset Angles",{}))
        {
            camera.pitch = camera.defaultPitch;
            camera.height = camera.defaultHeight;
            camera.pan = camera.defaultPan;
            camera.roll = camera.defaultRoll;
            ApplyAngleValues();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!camera.available || !camera.zoomFound);
        if (host.button("Reset Zoom",{}))
        {
            camera.zoom = camera.defaultZoom;
            ApplyZoomValue();
        }
        ImGui::EndDisabled();

        ImGui::EndChild();

        ImGui::PopStyleColor();
        HudMenu::End();
    }
    else {
        ImGui::PopStyleColor();
        HudMenu::End();
        host.popFont(3);
    }
}

}
