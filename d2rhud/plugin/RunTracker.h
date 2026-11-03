#pragma once
#include "RunTrackerData.h"
#include "D2RHUD/FrameContext.h"
#include <filesystem>
#include <functional>
class D2UnitStrc;
namespace RunTracker {
bool Enabled();
void Tick(const HudFrame::Context& frame,uint64_t epoch,const std::string& mod,bool hardcore=false);
void ConfigureAnalysisFolder(std::function<std::filesystem::path()> folder);
void DrawPanel();
void DrawOverlay();
void SetAreaNames(const std::map<int,std::string>& names);
// Native callbacks only request a fresh owned-storage snapshot; ground drops
// are never recorded. A supplied trampoline resolves original item names.
void InventoryChanged();
void SetNameProvider(void(*name)(D2UnitStrc*,char*));
void SetStashPage(uint32_t page,bool ready);
void StashVisible();
void SetReadGuard(void(*guard)(void(*)(void*),void*));
Frame Sample(const HudFrame::Context& frame,uint64_t epoch,const std::string& mod);
}
