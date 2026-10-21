#pragma once
class D2UnitStrc;
class D2GameStrc;
namespace CubeVisual::Data {struct Recipe;}
namespace CubeVisual {
using MapPrepare=bool(*)(D2GameStrc*,D2UnitStrc*,Data::Recipe&);
void SetMapPrepare(MapPrepare handler);
bool Install();
void Uninstall();
bool Installed();
bool QuestJournalSupported();
const char* Status();
// Only the world model path uses this override. Inventory images retain the
// original base code, quality, dimensions and inventory sprite selection.
const char* WorldPath(void* manager, D2UnitStrc* item);
}
