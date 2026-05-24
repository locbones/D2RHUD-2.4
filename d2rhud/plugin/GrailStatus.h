#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

extern std::atomic<uint32_t> g_GrailRevision;
void ReloadGameFilterForGrail();

struct GrailStatus {
    bool isGrail = false;
    bool collected = false;
    int located = 0;
};

GrailStatus GetGrailStatus(uint32_t fileIndex, bool isSetItem);

struct StashParsedItemDebug {
    int page = 0;
    int tab = 0;
    int x = 0;
    int y = 0;
    std::string code;
    uint8_t quality = 0;
    uint16_t setId = 0;
    uint16_t uniqueId = 0;
    bool identified = false;
    bool grailMatched = false;
    std::string grailName;
    std::string note;
};

extern bool showStashParseDebug;
extern bool g_ForceStashRescan;
extern bool g_StashScanInProgress;
extern double g_DeferStashScanUntil;
extern int g_StashScanPageFilter;
extern std::vector<int> g_AvailableStashPages;
extern std::vector<StashParsedItemDebug> g_StashDebugEntries;

void ClearStashDebugEntries();
const char* GetStashItemQualityName(uint8_t quality);
const char* GetQualityName(uint32_t quality);
void ScanStashPages();
