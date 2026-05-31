#pragma once

#include "../d2/json.hpp"
#include <imgui.h>
#include <cstdint>
#include <string>

namespace FloatingDamage {

enum class Kind {
    Normal,
    Critical
};

enum class Element {
    Physical,
    Fire,
    Lightning,
    Cold,
    Poison,
    Magic
};

struct Config {
    bool enabled = true;

    float textSize = 38.0f;
    float criticalHitSize = 48.0f;
    int textOutlineWidth = 1;
    float shadowLeftRightOffset = 0.0f;
    float shadowUpDownOffset = 0.0f;
    int maxNumbersOnScreen = 160;

    float displayTimeSeconds = 0.85f;
    float criticalDisplayTimeSeconds = 0.95f;
    float fadeOutStart = 0.75f;
    float spawnSize = 0.01f;
    float popBounceSize = 1.75f;
    float popInTimeSeconds = 0.08f;
    float settleTimeSeconds = 0.12f;
    float upwardDriftSpeed = 45.0f;
    float sidewaysSpread = 0.0f;
    float spawnHeightOffset = 0.0f;

    bool enableHitCombining = true;
    int maxCombinedHitSize = 999999;
    int combineWindowMs = 500;
    float extendDisplayOnHitSeconds = 0.52f;
    float hitPulseSize = 1.24f;
    float hitPulseTimeSeconds = 0.13f;
    bool showTickPopups = true;
    float tickPopupTimeSeconds = 0.70f;
    float tickPopupSize = 0.60f;
    float tickPopupTravel = 64.0f;
    float tickPopupHeightOffset = -28.0f;

    bool spreadNumbersHorizontally = true;
    int numberOfColumns = 7;
    float columnSpacing = 40.0f;
    float stackHeightStep = 24.0f;
    float columnReuseTimeSeconds = 0.60f;
    float maxStackHeight = 96.0f;

    bool showDpsCounter = true;
    float horizontalPositionPercent = 2.0f;
    float verticalPositionPercent = 98.0f;
    float dpsSampleTimeSeconds = 5.0f;

    int previewNumberCount = 8;
    float previewSpread = 32.0f;

    int fontIndex = 0;

    bool colorByDamageType = false;
    ImVec4 normalColor = ImVec4(0.92f, 0.92f, 0.88f, 1.0f);
    ImVec4 criticalColor = ImVec4(1.0f, 0.84f, 0.27f, 1.0f);
    ImVec4 physicalColor = ImVec4(0.92f, 0.92f, 0.88f, 1.0f);
    ImVec4 fireColor = ImVec4(1.0f, 0.45f, 0.12f, 1.0f);
    ImVec4 lightningColor = ImVec4(1.0f, 0.95f, 0.35f, 1.0f);
    ImVec4 coldColor = ImVec4(0.45f, 0.78f, 1.0f, 1.0f);
    ImVec4 poisonColor = ImVec4(0.35f, 0.90f, 0.30f, 1.0f);
    ImVec4 magicColor = ImVec4(0.72f, 0.45f, 1.0f, 1.0f);
    ImVec4 outlineColor = ImVec4(0.16f, 0.11f, 0.03f, 1.0f);
    ImVec4 shadowColor = ImVec4(0.16f, 0.11f, 0.02f, 1.0f);
};

Config& GetConfig();
void ResetToDefaults();
void LoadFromJson(const nlohmann::json& root);
void SaveToJson(nlohmann::ordered_json& root);

void QueueDamage(int amount, float screenX, float screenY, uint32_t unitType, uint32_t unitId, Kind kind, Element element = Element::Physical);
void QueueGameDamage(int amount, float screenX, float screenY, uint32_t unitType, uint32_t unitId, Kind kind, Element element = Element::Physical);
void QueuePreviewAt(float screenX, float screenY, Kind kind);
void QueuePreviewBurstAt(float screenX, float screenY);

void Update(float dt);
void Render(ImDrawList* drawList, const ImVec2& displaySize);

void DrawSettingsPanel(float menuScale);
void SpawnPreviewBurstAtScreenCenter(const ImVec2& displaySize);

size_t ActiveCount();
size_t PendingCount();
bool HasDisplayActivity();

} // namespace FloatingDamage
