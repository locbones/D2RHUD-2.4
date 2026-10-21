#pragma once

namespace SelectedAuras {
// Successful installation pins the containing HUD module until process exit.
// Uninstall restores owned native patches, but does not support FreeLibrary.
bool Install();
void Uninstall();
// Call after skill tables become available; safe to repeat across games.
void Update();
bool Installed();
const char* Status();
}
