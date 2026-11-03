#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
namespace Inspect::WorldData {
using Matrix = std::array<float, 16>;
using Point = std::array<float, 4>;
inline Point Transform(const Point& point, const Matrix& matrix) {
    Point out{};
    for (size_t column = 0; column < 4; ++column)
        for (size_t row = 0; row < 4; ++row)
            out[column] += point[row] * matrix[row * 4 + column];
    return out;
}
inline std::optional<std::array<float, 2>> Project(const Matrix& view, const Matrix& projection, float x,
                                                   float y, float height, float width, float displayHeight,
                                                   bool clipToScreen = true) {
    // Native mouse unprojection (0xdf2f0) converts engine x/z to subtiles
    // with 0.5. Camera matrices at +0 and +0x80 use row vectors.
    const auto clip = Transform(Transform({x * 2, height, y * 2, 1}, view), projection);
    for (auto value : clip)
        if (!std::isfinite(value))
            return {};
    if (clip[3] <= .0001f || width <= 0 || displayHeight <= 0)
        return {};
    const float sx = (1 + clip[0] / clip[3]) * width * .5f;
    const float sy = (1 - clip[1] / clip[3]) * displayHeight * .5f;
    if (clipToScreen && (sx < 18 || sx > width - 18 || sy < 18 || sy > displayHeight - 18))
        return {};
    return std::array{sx, sy};
}
inline std::optional<std::array<float, 2>> EyePosition(const Matrix& view, const Matrix& projection, float x,
                                                       float y, float width, float height, int lift = 96) {
    const auto feet = Project(view, projection, x, y, 0, width, height, false);
    auto eye = Project(view, projection, x, y, 6, width, height, false);
    if (!feet || !eye || (*feet)[0] < 0 || (*feet)[0] > width || (*feet)[1] < 0 || (*feet)[1] > height)
        return {};
    // Leave a visible gap above humanoid heads. Kept in screen pixels so the
    // gap stays readable across camera zoom, and adjustable for custom summons.
    (*eye)[1] -= std::clamp(lift, 0, 240) * height / 1080.f;
    // Keep a visible companion clickable when its head reaches the top edge.
    const float inset = std::clamp(height / 1080.f * 32.f, 26.f, 56.f) + 1;
    if (width < inset * 2 || height < inset * 2)
        return {};
    (*eye)[0] = std::clamp((*eye)[0], inset, width - inset);
    (*eye)[1] = std::clamp((*eye)[1], inset, height - inset);
    return eye;
}
} // namespace Inspect::WorldData
