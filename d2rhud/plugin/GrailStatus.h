#pragma once

extern std::atomic<uint32_t> g_GrailRevision;
void ReloadGameFilterForGrail();

struct GrailStatus {
    bool isGrail = false;
    bool collected = false;
    int located = 0;
};

// isSetItem: true = only check set items, false = only check unique items (so set index 6 and unique index 6 are distinct).
GrailStatus GetGrailStatus(uint32_t fileIndex, bool isSetItem);
