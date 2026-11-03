#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <imgui.h>
class D2UnitStrc;
class D2DamageInfoStrc;
class D2DamageStatTableStrc;
struct blz_string;

namespace HudCombat {
// Render-thread setup; callbacks install companion hooks owned by the HUD.
void ConfigureHooks(void (*resistance)(),void (*chat)());
void Tick(const ImVec2& displaySize,bool inGame,uint64_t session=0);
void ObserveCombatMessage(const char* text,D2UnitStrc* target=nullptr);
void ObserveDamageTable(D2DamageInfoStrc* info,D2DamageStatTableStrc* table);
bool ReadChatMessage(const blz_string* message,char* text,size_t capacity);
bool ParseChatLine(const char* text,std::string& sender,std::string& body);
}
