#pragma once
#include <algorithm>
#include <cmath>
#include <optional>

namespace Spectate::PartyLayout {
// Horizontal space after the native squelch button, in HD layout units.
inline constexpr float Gap = 16.f;
struct Eye {
    float x = 0, y = 0, size = 0;
};
inline std::optional<Eye> Place(float squelchX, float squelchY, int width, int height, float scale,
                                float right, float bottom) {
    if (!std::isfinite(scale) || scale <= 0 || width < 12 || width > 256 || height < 12 || height > 256)
        return {};
    const Eye eye{squelchX + width * scale + (std::max)(4.f, Gap * scale), squelchY, height * scale};
    if (!std::isfinite(eye.x) || !std::isfinite(eye.y) || !std::isfinite(eye.size) || eye.x < 0 ||
        eye.y < 0 || eye.x + eye.size > right || eye.y + eye.size > bottom)
        return {};
    return eye;
}
} // namespace Spectate::PartyLayout
