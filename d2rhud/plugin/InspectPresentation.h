#pragma once
#include <algorithm>
#include <string>
#include <string_view>
#include <vector>
namespace Inspect::Presentation {
struct Run {
    std::string text;
    unsigned color = 0;
};
using Line = std::vector<Run>;
inline std::vector<Line> TooltipLines(std::string_view text) {
    std::vector<Line> lines(1);
    unsigned color = 0;
    std::string pending;
    auto flush = [&] {
        if (!pending.empty()) {
            lines.back().push_back({std::move(pending), color});
            pending.clear();
        }
    };
    for (size_t n = 0; n < text.size();) {
        size_t prefix = 0;
        if (n + 3 < text.size() && uint8_t(text[n]) == 0xc3 && uint8_t(text[n + 1]) == 0xbf &&
            text[n + 2] == 'c')
            prefix = 3;
        else if (n + 2 < text.size() && uint8_t(text[n]) == 0xff && text[n + 1] == 'c')
            prefix = 2;
        if (prefix) {
            flush();
            const auto code = uint8_t(text[n + prefix]);
            color = code >= '0' && code <= '?' ? unsigned(code - '0') : 0;
            n += prefix + 1;
        } else if (text[n] == '\n') {
            flush();
            lines.emplace_back();
            ++n;
        } else if (text[n] == '\r')
            ++n;
        else {
            pending += text[n++];
        }
    }
    flush();
    while (!lines.empty() && lines.back().empty())
        lines.pop_back();
    // Native inventory tooltips use bottom-up layout. Preserve each run's
    // inherited color before reversing, including colors carried across rows.
    std::reverse(lines.begin(), lines.end());
    return lines;
}
struct Panel {
    float x, y, width, height, scale;
};
inline Panel Placement(float displayWidth, float displayHeight, float width, float height) {
    const float scale = (std::min)(displayHeight / 2160.f, displayWidth * .48f / width);
    const float margin = (std::max)(0.f, (displayWidth - 4560.f * scale) * .5f);
    return {margin, 0, width * scale, height * scale, scale};
}
} // namespace Inspect::Presentation
