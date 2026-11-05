#pragma once

namespace MultipleAuras {
bool Install();
void Uninstall();
void SetEnabled(bool enabled);
bool Available();
const char* Status();
}
