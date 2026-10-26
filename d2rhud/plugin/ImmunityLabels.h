#pragma once
#include <cstdint>
class D2GameStrc;
class D2UnitStrc;
class D2StatListExStrc;
namespace ImmunityLabels {
using ProjectResistance = int(*)(D2GameStrc*, D2UnitStrc*, uint32_t, int);
// Preserve native localization/layout and the native >=100 visibility test.
// The same projection runs before combat tests immunity and ordinary pierce.
bool Install(ProjectResistance project);
// Read unprojected stats through the trampoline, including from the projector.
int ReadNative(D2StatListExStrc* stats, uint32_t encodedStat, const void* cost);
// Restores the guarded Uber display branches and removes the reader hook.
void Uninstall();
}
