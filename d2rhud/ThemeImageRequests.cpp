#include "ThemeImageRequests.h"
#include "ProcessLifetime.h"
#include <utility>

namespace ThemeImages {
bool Requests::Set(size_t slot, std::string filename)
{
    if (slot >= slots_.size()) return false;
    auto& state = slots_[slot];
    state.filename = std::move(filename);
    ++state.generation;
    state.pending = true;
    state.attempted = state.busy = false;
    return true;
}
std::vector<Request> Requests::TakeDue(uint64_t now)
{
    std::vector<Request> result;
    for (size_t i = 0; i < slots_.size(); ++i) {
        auto& state = slots_[i];
        if (!state.pending || state.busy || (state.attempted && now >= state.lastAttempt && now - state.lastAttempt < 1000)) continue;
        result.push_back({i, state.generation, state.filename});
        state.busy = true;
    }
    return result;
}
void Requests::Complete(const Request& request, bool success, uint64_t now)
{
    if (request.slot >= slots_.size()) return;
    auto& state = slots_[request.slot];
    if (request.generation != state.generation || !state.busy) return;
    state.busy = false;
    state.pending = !success;
    state.attempted = true;
    state.lastAttempt = now;
}
void Requests::ResetResources()
{
    for (auto& state : slots_) {
        // Remember the chosen filename after a successful load. Resize destroys
        // textures; selected images must be recreated along with the renderer.
        ++state.generation;
        state.pending = !state.filename.empty();
        state.busy = state.attempted = false;
    }
}
Requests& Runtime() { return HudLifetime::State<Requests>(); }
}
