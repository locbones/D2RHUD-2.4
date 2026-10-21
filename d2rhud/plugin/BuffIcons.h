#pragma once
#include "../d2/json.hpp"
#include <string>
struct D2Client;
namespace BuffIcons {
void LoadFromJson(const nlohmann::json& root);
void SaveToJson(nlohmann::ordered_json& root);
void Update(D2Client* client, const std::string& mod, bool settingsOpen);
void Render(bool preview, float resolutionScale = 1.0f);
void DrawSettingsPanel(float scale);
}
