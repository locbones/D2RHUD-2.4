#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>

namespace HudChat {
struct Identity {
    std::string name;
    uint32_t type = UINT32_MAX, id = UINT32_MAX;
};
// Packet and chat-manager callbacks may both submit messages. The render thread
// consumes one consistent batch and coalesces it into at most one alert.
class Inbox {
public:
    void SetContext(uint64_t session, bool enabled, Identity identity);
    bool Submit(std::string_view sender, std::string_view message,
                uint32_t type = UINT32_MAX, uint32_t id = UINT32_MAX, bool hasUnit = false);
    bool TakeAlert(uint64_t now);
    bool Enabled() const { return enabled_.load(std::memory_order_acquire); }
    size_t Pending() const { return pending_.load(std::memory_order_acquire); }
private:
    struct Message {
        std::array<char, 64> sender{};
        std::array<char, 256> text{};
        uint32_t type = UINT32_MAX, id = UINT32_MAX;
        bool hasUnit = false;
    };
    std::mutex mutex_;
    std::array<Message, 32> messages_{};
    size_t first_ = 0, count_ = 0;
    Identity identity_;
    std::atomic<bool> enabled_{false};
    std::atomic<uint64_t> session_{0};
    std::atomic<size_t> pending_{0};
    uint64_t lastAlert_ = 0;
    bool alerted_ = false;
};
bool IsSystemFeedback(std::string_view message);
bool Enabled();
void Observe(std::string_view sender, std::string_view message,
             uint32_t type = UINT32_MAX, uint32_t id = UINT32_MAX, bool hasUnit = false);
void Tick(uint64_t session, bool inGame, bool enabled, const std::string& soundPath, int volumePercent=30);
}
