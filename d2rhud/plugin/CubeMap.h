#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
class D2GameStrc;
class D2UnitStrc;
class D2ActiveRoomStrc;
struct ImFont;
namespace CubeVisual::Data {struct Recipe;}
namespace CubeMap {
bool Install();
void Uninstall();
const char* Status();
// Workshop preference gates new arena entries; active runs retain their cleanup.
void SetSurvivorsEnabled(bool enabled);
bool SurvivorsEnabled();
bool SummonLifecycleAvailable();
const char* SummonLifecycleStatus();
// Startup failure evidence captured before this feature installs its hooks.
const char* SurvivorsCompatibilityDetails();
// The existing authoritative game tick remains the sole owner of its hook.
void SetActTick(void(*observer)(D2GameStrc*));
void SetActTown(int level);
// Called by the existing cube hook owner, before native input consumption.
bool Prepare(D2GameStrc* game,D2UnitStrc* player,CubeVisual::Data::Recipe& recipe);
bool IsMapLevel(D2GameStrc* game,int level);
bool IsMapMonster(D2GameStrc* game,D2UnitStrc* unit);
bool InitializeNativeMonster(D2GameStrc* game,D2ActiveRoomStrc* room,D2UnitStrc* unit,int64_t* region);
bool InitializeMonster(D2GameStrc* game,D2ActiveRoomStrc* room,D2UnitStrc* unit);
bool Command(D2GameStrc* game,D2UnitStrc* player,const uint8_t* packet,size_t size);
using DropFunction=void(__fastcall*)(D2GameStrc*,D2UnitStrc*,D2UnitStrc*,int,int,int,int,D2UnitStrc**,int*,int);
bool Drop(D2GameStrc*,D2UnitStrc*,D2UnitStrc*,int,int,int,int,D2UnitStrc**,int*,int,DropFunction);
void SetArtworkProvider(uint64_t(*provider)());
void SetFontProvider(ImFont*(*provider)(float));
bool Receive(const uint8_t* packet,size_t size);
// Separate rolled-layout exploration from the character's ordinary .ma cache.
uint64_t ClientLayerSeed(uint32_t layer);
bool Display(std::string& title,std::string& modifiers);
void Draw(float scale,bool clientReady=true);
}
