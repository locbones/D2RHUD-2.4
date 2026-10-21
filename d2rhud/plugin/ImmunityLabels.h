#pragma once
class D2GameStrc;
class D2UnitStrc;
namespace ImmunityLabels {
using RefreshSunder = void(*)(D2GameStrc*, D2UnitStrc*);
// Preserve native localization/layout and the native >=100 visibility test.
bool Install(RefreshSunder refresh);
// Restores the guarded Uber display branches and removes the label reader hook.
void Uninstall();
}
