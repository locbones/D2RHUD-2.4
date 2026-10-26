#pragma once

namespace WidescreenBars {
bool Install();
void Uninstall();
bool Available();
void SetEnabled(bool enabled);
void SetInGame(bool inGame);
const char* Status();
}
