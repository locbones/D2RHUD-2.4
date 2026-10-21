#pragma once
#include "../d2/json.hpp"
#include <string>
class D2UnitStrc;
namespace StatRanges {
void LoadFromJson(const nlohmann::json& root);
void SaveToJson(nlohmann::ordered_json& root);
// Install only after verifying the native text layout functions for this build.
void Install();
void BeginTooltip(D2UnitStrc* item, const void* panel);
void EndTooltip();
void Update(const std::string& mod, bool inGame, bool menusOpen, bool bindingDown, bool previewOpen = false);
void Render(float resolutionScale);
void DrawSettingsPanel(float scale);
bool UsesHotkey();
}
