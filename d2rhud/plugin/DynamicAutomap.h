#pragma once
#include <cstdint>

namespace DynamicAutomap {
using DrawFunction = void(__fastcall*)(int, void*, uint8_t*, void*);
bool Install();
void Uninstall();
bool Available();
void Configure(bool enabled, int intervalMilliseconds);
const char* Status();
// Called by the existing automap hook owner, on the native drawing thread.
void Draw(int style, void* tree, uint8_t* transform, void* opacity, DrawFunction original);
void Reset();
} // namespace DynamicAutomap
