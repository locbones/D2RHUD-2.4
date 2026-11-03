#pragma once

// Preserve the per-load random starting group and advance once per full
// terror+break period. Both the displayed zone and monster logic must use this.
inline int TerrorZoneRotationIndex(long long nowUtc, long long startUtc,
    int terrorMinutes, int breakMinutes, int groupCount, int randomOffset)
{
    const long long periodMinutes = static_cast<long long>(terrorMinutes) + breakMinutes;
    if (groupCount <= 0 || periodMinutes <= 0) return 0;
    const unsigned long long elapsed = nowUtc > startUtc
        ? static_cast<unsigned long long>(nowUtc) - static_cast<unsigned long long>(startUtc) : 0;
    const unsigned long long cycle = elapsed / 60 / static_cast<unsigned long long>(periodMinutes);
    int offset = randomOffset % groupCount;
    if (offset < 0) offset += groupCount;
    return static_cast<int>((cycle % groupCount + offset) % groupCount);
}

// Manual cycling changes the schedule's offset, never pins one group forever.
inline int TerrorZoneSelectedIndex(long long nowUtc, long long startUtc,
    int terrorMinutes, int breakMinutes, int groupCount, int randomOffset, int manualOffset)
{
    if (groupCount <= 0) return 0;
    const int automatic = TerrorZoneRotationIndex(nowUtc,startUtc,terrorMinutes,breakMinutes,groupCount,randomOffset);
    long long selected = (static_cast<long long>(automatic) + manualOffset) % groupCount;
    return static_cast<int>(selected < 0 ? selected + groupCount : selected);
}
struct TerrorZonePhase { bool active = false; unsigned long long remaining = 0; };
inline TerrorZonePhase TerrorZoneCurrentPhase(long long nowUtc,long long startUtc,int terrorMinutes,int breakMinutes) {
    const long long minutes = static_cast<long long>(terrorMinutes) + breakMinutes;
    if (terrorMinutes < 0 || breakMinutes < 0 || minutes <= 0 || nowUtc < startUtc) return {};
    const auto elapsed = static_cast<unsigned long long>(nowUtc) - static_cast<unsigned long long>(startUtc);
    const auto period = static_cast<unsigned long long>(minutes) * 60;
    const auto position = elapsed % period;
    const auto active = static_cast<unsigned long long>(terrorMinutes) * 60;
    return {position < active, (position < active ? active : period) - position};
}
