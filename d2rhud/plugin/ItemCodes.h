#pragma once
namespace ItemCodes {
// Native HD item-code capacity, independent of transmog and per-mod settings.
bool Install();
bool Installed();
const char* Status();
void Uninstall();
}
