#pragma once
#include "HookTransaction.h"
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace HudHooks {
struct Hook {
    PVOID* original;
    PVOID target;
    PVOID replacement;
    std::string name;
};
template<class Function, class Detour>
Hook Bind(Function& original, Detour replacement, uintptr_t target, const char* name) {
    return {reinterpret_cast<PVOID*>(&original), reinterpret_cast<PVOID>(target),
            reinterpret_cast<PVOID>(replacement), name};
}
enum class Outcome { Waiting, Installed, AlreadyInstalled, Failed };
struct Report {
    Outcome outcome;
    LONG error = NO_ERROR;
    std::string_view group, phase, hook;
    bool diagnosticChanged = false;
};
// Owned by the render thread. Native callbacks only use the published original
// function pointers; they never touch this coordinator or its retry state.
class Installer {
public:
    Installer(std::string name, std::initializer_list<Hook> hooks, Operations operations = {});
    Installer(const Installer&) = delete;
    Installer& operator=(const Installer&) = delete;
    Report Tick(uint64_t now);
    bool Ready() const { return ready_; }
    uint64_t Attempts() const { return attempts_; }
private:
    Report Failure(LONG error, std::string_view phase, std::string_view hook = {});
    std::string name_;
    std::vector<Hook> hooks_;
    std::vector<PVOID> previous_;
    Operations operations_;
    uint64_t attempts_ = 0, lastAttempt_ = 0;
    bool ready_ = false;
    LONG lastError_ = NO_ERROR;
    std::string_view lastPhase_, lastHook_;
};
}
