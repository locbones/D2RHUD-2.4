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
