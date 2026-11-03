#pragma once
#include <array>
#include <cstdint>
namespace HudNativePanels {
// D2R 2.4 indices, not the legacy D2C_UIvars enum. Alerts, the bottom
// MiniMenu (20), party portraits and held-Alt are ordinary gameplay UI.
using Flags = std::array<uint8_t, 31>;
inline bool BlocksWorldInput(const Flags& flags) {
    for (const int panel : {1, 2, 3, 4, 5, 9, 11, 13, 14, 15, 19, 21, 22, 23, 24, 25, 27, 30})
        if (flags[panel])
            return true;
    return false;
}
} // namespace HudNativePanels
