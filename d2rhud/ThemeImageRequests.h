#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ThemeImages {
inline constexpr size_t SlotCount = 17; // 8 windows, 3 frames, 3 checkboxes, 3 buttons.
struct Request { size_t slot; uint64_t generation; std::string filename; };
// The renderer serializes all access. This class owns CPU request state only.
class Requests {
public:
    bool Set(size_t slot, std::string filename);
    std::vector<Request> TakeDue(uint64_t now);
    void Complete(const Request& request, bool success, uint64_t now);
    void ResetResources();
private:
    struct Slot {
        std::string filename;
        uint64_t generation = 0, lastAttempt = 0;
        bool pending = false, attempted = false, busy = false;
    };
    std::array<Slot, SlotCount> slots_{};
};
Requests& Runtime();
}
