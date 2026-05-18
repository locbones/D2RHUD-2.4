#pragma once

#include "D2RHUD/D2RHUD.h"
#include "GrailStatus.h"
#include <string>
#include <unordered_set>
#include <mutex>

extern std::unordered_set<std::string> g_ExcludedGrailItems;
extern bool autoBackups;
extern bool backupWithTimestamps;
extern bool overwriteOldBackup;
extern int backupIntervalMinutes;
extern bool triggerBackupNow;
extern std::mutex backupMutex;
extern char backupPath[260];

void SaveGrailProgress(const std::string& userPath, bool isAutoBackup);
void LoadGrailProgress(const std::string& filepath);
bool LoadUniqueItems(const std::string& filepath);
bool LoadSetItems(const std::string& filepath);
void LoadExcludedGrailItems(const std::string& filepath);
void LoadAllItemData();
bool GenerateStaticArrays(const std::string& filepath, int itemsPerLine = 5);
void SortItemLists();
void WriteResultsToFile(const std::string& output);
std::string EscapeString(const std::string& input);

