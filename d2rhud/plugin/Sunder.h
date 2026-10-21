#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>

namespace Sunder {
// Ported from the submitted dynamic-immunity-labels package. Track how much
// this mechanic applied, not the unused piercing amount, so refreshes are stable.
using Values = std::array<int, 6>;
inline constexpr std::array<uint32_t, 6> ResistanceStats{43, 39, 41, 45, 36, 37};
inline constexpr std::array<uint32_t, 6> ImmunityStats{187, 189, 190, 191, 192, 193};
struct Change { int value, applied; bool valid; };
inline Change Calculate(int current, int previousApplied, int pierce, int floor = 99) {
    if (previousApplied < 0) return {current, previousApplied, false};
    const int64_t baseline = int64_t(current) + previousApplied;
    int64_t applied = 0;
    if (baseline >= 100 && pierce > 0)
        applied = (std::min)(int64_t(pierce), baseline - (std::clamp)(floor, 0, 100));
    const int64_t target = baseline - applied;
    if (target < (std::numeric_limits<int>::min)() || target > (std::numeric_limits<int>::max)())
        return {current, previousApplied, false};
    return {static_cast<int>(target), static_cast<int>(applied), true};
}
inline void MergeMaximum(Values& target, const Values& source) {
    for (size_t i = 0; i < target.size(); ++i) target[i] = (std::max)(target[i], source[i]);
}
template<class Get, class Set>
void Refresh(Values& applied, const Values& pierce, int floor, Get&& get, Set&& set) {
    for (size_t i = 0; i < applied.size(); ++i) {
        const int current = get(ResistanceStats[i]);
        const auto change = Calculate(current, applied[i], pierce[i], floor);
        if (!change.valid) continue;
        if (change.value != current) set(ResistanceStats[i], change.value);
        applied[i] = change.applied;
    }
}
// Native setters write base stats. Temporary map/aura bonuses are already in
// the total, so assigning the desired total to the base would add them twice.
template<class GetTotal, class GetBase, class SetBase>
void RefreshBase(Values& applied,const Values& pierce,int floor,GetTotal&& total,GetBase&& base,SetBase&& set) {
    for(size_t i=0;i<applied.size();++i) {
        const auto stat=ResistanceStats[i];const int current=total(stat);
        const auto change=Calculate(current,applied[i],pierce[i],floor);
        if(!change.valid)continue;
        if(change.value!=current) {
            const int64_t value=int64_t(base(stat))+int64_t(change.value)-current;
            if(value<(std::numeric_limits<int>::min)() || value>(std::numeric_limits<int>::max)() || !set(stat,int(value)))continue;
        }
        applied[i]=change.applied;
    }
}
inline bool IsResistance(uint32_t stat) {
    return std::find(ResistanceStats.begin(), ResistanceStats.end(), stat) != ResistanceStats.end();
}
}
