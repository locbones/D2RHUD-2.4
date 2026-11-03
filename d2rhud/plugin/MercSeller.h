#pragma once
namespace MercSeller {
bool Install();
void Uninstall();
void SetEnabled(bool enabled);
bool Available();
const char* Status();
} // namespace MercSeller
