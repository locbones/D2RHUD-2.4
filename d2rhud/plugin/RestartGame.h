#pragma once
#include <cstdint>

namespace RestartGame {
constexpr uint64_t MessageHash = 0xfe55c0b14d39b446ULL; // PausePanelMessage:RestartGame
void Install();
void SetSaveAndExitHandler(void(*handler)());
void Uninstall();
// Called on the native window/UI thread. The caller queues the normal save/exit
// only after this succeeds; the timer waits for that session to be destroyed.
bool Request();
// UI-thread continuation after the native ending has saved/exited. Does not
// issue another save/exit or change difficulty; preserves the same identity.
bool AfterCredits(const char* name,int difficulty,void(*completed)(bool));
void Cancel();
const char* Status();
}
