#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
namespace TooltipText {
constexpr size_t LegacyCapacity = 1024;
constexpr size_t NativeCapacity = 16384;
// Extra headroom also makes intermediate instruction-limit updates safe.
constexpr size_t StorageCapacity = 32768;
bool InstallLimits(); // Call only after the description detour has committed.
bool LimitsExtended();
void Remember(uint32_t unitId, const std::string& full);
std::string Expand(uint32_t unitId, const std::string& original);
void SetViewport(float width, float height, float resolutionScale);
void BeginPanel(uint32_t unitId, void* panel);
float LayoutScale();
// Grow the native background without changing the rectangle used to lay out text.
void FitBounds(float left, float top, float right, float bottom);
const int* ContentRect(const int* nativeRect);
void EndPanel();
}
