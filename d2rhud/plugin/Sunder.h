#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>

namespace Sunder {
using Values = std::array<int, 6>;
inline constexpr std::array<uint32_t, 6> ResistanceStats{43, 39, 41, 45, 36, 37};
inline constexpr std::array<uint32_t, 6> ImmunityStats{187, 189, 190, 191, 192, 193};
inline constexpr int Floor = 99;
struct Projection { int native, effective, immunity, applied; bool valid; };

// Derive both views from unmodified native data. Positive attached bonuses can
// create immunity. Negative aura/curse contributions apply after sunder, so the
// effective resistance can fall below 99. The immunity view precedes them.
inline Projection Calculate(int base, int positiveAttached, int negativeAttached, int pierce) {
    if (positiveAttached < 0 || negativeAttached > 0) return {};
    const int64_t intrinsic = int64_t(base) + positiveAttached;
    const int64_t native = intrinsic + negativeAttached;
    int64_t applied = 0;
    if (intrinsic >= 100 && pierce > 0)
        applied = (std::min)(int64_t(pierce), intrinsic - Floor);
    const int64_t immunity = intrinsic - applied;
    const int64_t effective = native - applied;
    const auto fits = [](int64_t value) {
        return value >= (std::numeric_limits<int>::min)() && value <= (std::numeric_limits<int>::max)();
    };
    if (!fits(intrinsic) || !fits(native) || !fits(effective) || !fits(immunity) || !fits(applied)) return {};
    return {int(native), int(effective), int(immunity), int(applied), true};
}
inline void MergeMaximum(Values& target, const Values& source) {
    for (size_t i = 0; i < target.size(); ++i) target[i] = (std::max)(target[i], source[i]);
}
inline bool IsResistance(uint32_t stat) {
    return std::find(ResistanceStats.begin(), ResistanceStats.end(), stat) != ResistanceStats.end();
}
}
