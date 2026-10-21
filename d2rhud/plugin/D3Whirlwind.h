#pragma once

namespace D3Whirlwind {
bool Install();
void Uninstall();
bool Enabled();
void SetEnabled(bool enabled);
// Independent input preference, including when D3 Whirlwind is off.
void SetRightClickPickupDisabled(bool disabled);
bool RightClickPickupDisabled();
const char* Status();
// UI thread publishes capture state; native simulation callbacks own gameplay.
void SetInputBlocked(bool blocked);
void MouseButtonDown(int button); // Logical mouse button 1/2, from WndProc.
}
