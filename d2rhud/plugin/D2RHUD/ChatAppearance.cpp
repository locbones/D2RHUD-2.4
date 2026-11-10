#include "ChatAppearance.h"
#include "../../Pattern.h"
#include <Windows.h>
#include <detours/detours.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <string_view>

namespace HudChatAppearance {
namespace {
std::atomic<uint32_t> preferences{Pack({})};
std::atomic<int> installed{0};
using Callback = void(__fastcall*)(void*);
Callback originalDraw = nullptr, originalUpdate = nullptr, originalBackground = nullptr;
using InputCallback = uintptr_t(__fastcall*)(void*, void*);
InputCallback originalMessage = nullptr, originalHitTest = nullptr;
thread_local void* activeList = nullptr;

template<class T> T Read(const void* object, size_t offset) {
    T value{};
    std::memcpy(&value, static_cast<const uint8_t*>(object) + offset, sizeof(value));
    return value;
}
bool Named(const void* object, std::string_view name, bool prefix = false) {
    if (!object) return false;
    const auto length = Read<uint64_t>(object, 0x10);
    const auto text = Read<const char*>(object, 8);
    return text && length <= 64 && (prefix ? length >= name.size() : length == name.size()) &&
        std::memcmp(text, name.data(), name.size()) == 0;
}
bool InPanel(const void* object, std::string_view name, bool prefix = false) {
    for (unsigned depth = 0; object && depth < 8; ++depth) {
        if (Named(object, name, prefix)) return true;
        object = Read<const void*>(object, 0x30);
    }
    return false;
}
// Only scalar fields owned by the current native callback are borrowed.
// Restore them before returning; native layouts and other HUDs remain owners
// of their geometry/colors. No pointers survive the callback or a game exit.
class Edits {
    struct Field { void* address{}; std::array<uint8_t, 4> before{}; };
    std::array<Field, 4> fields{};
    unsigned count = 0;
public:
    template<class T> void Set(void* object, size_t offset, T value) {
        static_assert(sizeof(T) == 4);
        auto& field = fields[count++];
        field.address = static_cast<uint8_t*>(object) + offset;
        std::memcpy(field.before.data(), field.address, 4);
        std::memcpy(field.address, &value, 4);
    }
    ~Edits() {
        while (count) { const auto& field = fields[--count]; std::memcpy(field.address, field.before.data(), 4); }
    }
};
bool ChatFields(void* widget, Settings settings, Edits& edits) {
    if (!settings.enabled || !widget || Read<uintptr_t>(widget, 0) != Pattern::Address(0x15ab2f0)) return false;
    const auto source = Read<int>(widget, 0x200);
    const bool live = source == 1 && InPanel(widget, "HUDMessagePanel");
    const bool history = source == 0 && InPanel(widget, "MessageLogPanel", true);
    if (!live && !history) return false;
    const auto scale = Read<float>(widget, 0x80);
    if (!std::isfinite(scale) || scale < .01f || scale > 10.f) return false;
    const auto top = Read<int32_t>(widget, 0x74), height = Read<int32_t>(widget, 0x7c), width = Read<int32_t>(widget, 0x78);
    if (Read<uint8_t>(widget, 0x52) || width <= 0 || width >= 10000 || height <= 0 || height >= 10000 || std::abs(int64_t(top)) >= 100000) return false;
    const auto factor = settings.size / 100.f;
    const auto fraction = live ? settings.height / 100.f : 1.f;
    if (settings.size != 100) {
        edits.Set(widget, 0x80, scale * factor);
        // Keep the physical viewport inside its original panel. Native text
        // layout receives the reciprocal width so larger text can wrap.
        edits.Set(widget, 0x78, (std::max)(1, int32_t(std::lround(width / factor))));
    }
    // The live list grows upwards: reducing its viewport preserves the bottom
    // edge. History retains its full area, scrollbar and stored entries.
    if (live && settings.height != 100)
        edits.Set(widget, 0x74, top + int32_t(std::lround(height * scale * (1.f - fraction))));
    if (settings.size != 100 || (live && settings.height != 100))
        edits.Set(widget, 0x7c, (std::max)(1, int32_t(std::lround(height * fraction / factor))));
    return true;
}
class ChatScope {
    Edits edits;
    void* previous = activeList;
    bool owns = false;
public:
    ChatScope(void* widget, bool findParent = false) {
        const auto settings = Unpack(preferences.load(std::memory_order_relaxed));
        if (!settings.enabled) return;
        if (findParent) {
            unsigned depth = 0;
            while (widget && Read<uintptr_t>(widget, 0) != Pattern::Address(0x15ab2f0) && depth++ < 8)
                widget = Read<void*>(widget, 0x30);
            if (depth > 8) return;
        }
        if (widget == activeList) return;
        owns = ChatFields(widget, settings, edits);
        if (owns) activeList = widget;
    }
    ~ChatScope() { if (owns) activeList = previous; }
};
void __fastcall Draw(void* widget) {
    ChatScope scope(widget);
    originalDraw(widget);
}
void __fastcall Update(void* widget) {
    ChatScope scope(widget);
    originalUpdate(widget);
}
uintptr_t __fastcall Message(void* widget, void* message) {
    ChatScope scope(widget);
    return originalMessage(widget, message);
}
uintptr_t __fastcall HitTest(void* widget, void* point) {
    // The native input dispatcher tests links before dispatching the message.
    // Match the paint transform for the list and all its descendants. A nested
    // paint/message already owns the transform and must not apply it twice.
    ChatScope scope(widget, true);
    return originalHitTest(widget, point);
}
void __fastcall Background(void* widget) {
    Edits edits;
    const auto settings = Unpack(preferences.load(std::memory_order_relaxed));
    if (settings.enabled && widget && Read<uintptr_t>(widget, 0) == Pattern::Address(0x15ac8b0) &&
        ((Named(widget, "MessageBackground") && InPanel(widget, "HUDMessagePanel")) ||
         (Named(widget, "MessageLogBackground") && InPanel(widget, "MessageLogPanel", true)) ||
         (Named(widget, "BackgroundBox") && InPanel(widget, "ChatPanel"))))
    {
        const auto alpha = Read<float>(widget, 0x94);
        if (std::isfinite(alpha) && alpha >= 0.f && alpha <= 1.f)
            edits.Set(widget, 0x94, alpha * settings.background / 100.f);
    }
    originalBackground(widget);
}
bool Matches(uint32_t rva, uint64_t expected) {
    std::array<uint8_t, 32> bytes{}; SIZE_T count = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(Pattern::Address(rva)), bytes.data(), bytes.size(), &count) || count != bytes.size()) return false;
    uint64_t hash = 14695981039346656037ULL;
    for (const auto byte : bytes) { hash ^= byte; hash *= 1099511628211ULL; }
    return hash == expected;
}
void Install() {
    int expected = 0;
    if (!installed.compare_exchange_strong(expected, -1)) return;
    // Entry-point checks bind the field contract to the supported 2.4 UI.
    if (!Matches(0x5b2140, 0x16536698afc45558ULL) || !Matches(0x568e90, 0x6a2c603bf83e7218ULL) ||
        !Matches(0x5795f0, 0x27ae39121e0cfb29ULL) || !Matches(0x568f00, 0x07c1ade7267a3fd5ULL) ||
        !Matches(0x576dd0, 0xee5c78a6a59cead0ULL)) return;
    originalDraw = reinterpret_cast<Callback>(Pattern::Address(0x5b2140));
    originalUpdate = reinterpret_cast<Callback>(Pattern::Address(0x568e90));
    originalBackground = reinterpret_cast<Callback>(Pattern::Address(0x5795f0));
    originalMessage = reinterpret_cast<InputCallback>(Pattern::Address(0x568f00));
    originalHitTest = reinterpret_cast<InputCallback>(Pattern::Address(0x576dd0));
    if (DetourTransactionBegin() != NO_ERROR) return;
    LONG error = DetourUpdateThread(GetCurrentThread());
    if (error == NO_ERROR) error = DetourAttach(reinterpret_cast<PVOID*>(&originalDraw), Draw);
    if (error == NO_ERROR) error = DetourAttach(reinterpret_cast<PVOID*>(&originalUpdate), Update);
    if (error == NO_ERROR) error = DetourAttach(reinterpret_cast<PVOID*>(&originalBackground), Background);
    if (error == NO_ERROR) error = DetourAttach(reinterpret_cast<PVOID*>(&originalMessage), Message);
    if (error == NO_ERROR) error = DetourAttach(reinterpret_cast<PVOID*>(&originalHitTest), HitTest);
    if (error != NO_ERROR) { DetourTransactionAbort(); return; }
    if (DetourTransactionCommit() == NO_ERROR) installed.store(1);
}
}
void Configure(Settings value) {
    preferences.store(Pack(value), std::memory_order_relaxed);
    if (value.enabled) Install();
}
const char* Status() {
    return installed.load() < 0 ? "Chat appearance is unavailable for this game build; native chat is unchanged." :
        "Native chat and item links remain available.";
}
}
