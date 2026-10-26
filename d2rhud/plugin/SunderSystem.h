#pragma once
#include <cstdint>
class D2GameStrc;
class D2UnitStrc;
namespace SunderSystem {
bool Install();
int EffectiveResistance(D2GameStrc* game, D2UnitStrc* monster, uint32_t stat, int nativeTotal);
void Uninstall();
void SetEnabled(bool enabled);
const char* Status();
}
