#pragma once
#include <cstdint>

namespace RestartGame::Data {
enum class Phase { Idle, Leaving, Frontend, Starting };
enum class Action { None, Start, Complete, Cancel };
struct Flow {
    Phase phase = Phase::Idle;
    uint64_t deadline = 0;
    unsigned readyFrames = 0;
    bool Begin(uint64_t now) {
        if (phase != Phase::Idle) return false;
        phase = Phase::Leaving; deadline = now + 60000; readyFrames = 0;
        return true;
    }
    void Reset() { *this = {}; }
    Action Tick(uint64_t now, bool inGame, bool serverGone, bool frontendReady, bool identityMatches) {
        if (phase == Phase::Idle) return Action::None;
        if (now >= deadline) { Reset(); return Action::Cancel; }
        if (phase == Phase::Starting) {
            if (inGame) { Reset(); return Action::Complete; }
            return Action::None;
        }
        if (inGame || !serverGone) { readyFrames = 0; return Action::None; }
        phase = Phase::Frontend;
        if (!frontendReady) { readyFrames = 0; return Action::None; }
        if (!identityMatches) { Reset(); return Action::Cancel; }
        // Let the frontend finish its open/refresh messages before requesting
        // another game. This is elapsed UI ticks, never a render-thread call.
        if (++readyFrames < 3) return Action::None;
        phase = Phase::Starting; deadline = now + 30000;
        return Action::Start;
    }
};
}
