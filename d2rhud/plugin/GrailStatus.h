#pragma once

extern std::atomic<uint32_t> g_GrailRevision;
void ReloadGameFilterForGrail();

struct GrailStatus {
    bool isGrail = false;
    bool collected = false;
    int located = 0;
};

GrailStatus GetGrailStatus(uint32_t fileIndex);
