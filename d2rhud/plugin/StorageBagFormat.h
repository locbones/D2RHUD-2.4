#pragma once
#include <cstdint>

namespace StorageBagFormat {
// Current-format D2R items store this in the three bits after item flags.
// Zero selects the legacy property-function table, which cannot apply RMD's
// rune-point properties. Native item data retains the value at offset 0x40.
inline constexpr uint16_t Current = 5;
}
