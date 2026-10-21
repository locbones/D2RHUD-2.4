#include "TooltipText.h"
#include "../Pattern.h"
#include <windows.h>
#include <tlhelp32.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <deque>
#include <map>
#include <mutex>
#include <vector>

namespace TooltipText {
namespace {
struct Expansion { std::string prefix, full; };
std::mutex textMutex;
std::map<uint32_t, std::vector<Expansion>> descriptions;
std::deque<uint32_t> order;
std::atomic<bool> extended{ false };
std::atomic<float> viewportWidth{1920}, viewportHeight{1080}, resolutionScale{1};
struct Panel {
    uintptr_t address = 0, text = 0;
    uint64_t length = 0;
    std::array<int, 4> rect{};
    std::array<int, 4> content{}, secondary{};
    bool movedSecondary = false;
    std::string expanded;
} ;
thread_local Panel current;
template<class T> bool Read(uintptr_t address, T& out) {
    SIZE_T count = 0;
    return address && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address), &out, sizeof(out), &count) && count == sizeof(out);
}
bool Supported(uintptr_t address, const std::vector<uint8_t>& signature) {
    std::vector<uint8_t> bytes(signature.size()); SIZE_T count = 0;
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address), bytes.data(), bytes.size(), &count) && count == bytes.size() && bytes == signature;
}
float NativeScale() {
    // Same scale used by TooltipsPanel::DrawTooltip in D2R 1.4.71510.
    uintptr_t ui = 0, controller = 0; float multiplier = 1;
    if (!Read(Pattern::Address(0x1d7c4e8), ui) || !ui) return 1;
    if (!Read(ui + 0x80, multiplier) || !std::isfinite(multiplier) || multiplier <= 0) return 1;
    float scale = 1;
    if (Read(ui + 0x30, controller) && controller) {
        const auto getScale = reinterpret_cast<float(__fastcall*)(void*)>(Pattern::Address(0x155700));
        scale = getScale(reinterpret_cast<void*>(controller));
    }
    return std::isfinite(scale) && scale > 0 ? scale * multiplier : multiplier;
}
bool HighDefinition() {
    uintptr_t ui = 0; uint8_t hd = 0;
    if (Read(Pattern::Address(0x1d7c4e8), ui) && ui && Read(ui + 0xb9, hd)) return hd != 0;
    float fallback = 0;
    return Read(Pattern::Address(0x1d64170), fallback) && fallback > 0;
}
int MeasureHeight(const char* text, const void* style, float scale, const int* limits) {
    // The native layout owns spilled allocations. Use its destructor, never the
    // HUD's allocator, to release them. Inline capacity is 128 glyphs/rows.
    alignas(16) std::array<uint8_t, 0x2638> layout{};
    const uintptr_t chars = reinterpret_cast<uintptr_t>(layout.data() + 0x18);
    const uintptr_t rows = reinterpret_cast<uintptr_t>(layout.data() + 0x1e30);
    constexpr uint64_t capacity = 0x8000000000000080ull;
    memcpy(layout.data(), &chars, 8); memcpy(layout.data() + 0x10, &capacity, 8);
    memcpy(layout.data() + 0x1e18, &rows, 8); memcpy(layout.data() + 0x1e28, &capacity, 8);
    auto build = reinterpret_cast<void(__fastcall*)(const char*, const void*, void*, float, const int*)>(Pattern::Address(0x5d97d0));
    auto destroy = reinterpret_cast<void(__fastcall*)(void*)>(Pattern::Address(0x5d9940));
    build(text, style, layout.data(), scale, limits);
    int height = 0; memcpy(&height, layout.data() + 0x2634, 4);
    destroy(layout.data());
    return height;
}
// Do not raise limits while an earlier, unredirected call could still be using
// a legacy caller's stack buffer. Open handles before suspending any thread and
// avoid heap allocations until every thread has resumed.
struct PausedThreads {
    std::array<HANDLE, 512> handles{};
    std::array<bool, 512> suspended{};
    size_t count = 0;
    ~PausedThreads() {
        for (size_t i = 0; i < count; ++i) if (suspended[i]) ResumeThread(handles[i]);
        for (size_t i = 0; i < count; ++i) CloseHandle(handles[i]);
    }
    bool PauseOutsideDescription() {
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snapshot == INVALID_HANDLE_VALUE) return false;
        THREADENTRY32 thread{}; thread.dwSize = sizeof(thread);
        BOOL more = Thread32First(snapshot, &thread); bool success = more != FALSE;
        while (more) {
            if (thread.th32OwnerProcessID == GetCurrentProcessId() && thread.th32ThreadID != GetCurrentThreadId()) {
                if (count == handles.size()) { success = false; break; }
                HANDLE handle = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, thread.th32ThreadID);
                if (handle) handles[count++] = handle;
                else if (GetLastError() != ERROR_INVALID_PARAMETER) { success = false; break; }
            }
            more = Thread32Next(snapshot, &thread);
        }
        CloseHandle(snapshot);
        if (!success) return false;
        for (size_t i = 0; i < count; ++i) {
            if (SuspendThread(handles[i]) == static_cast<DWORD>(-1)) {
                DWORD exit = STILL_ACTIVE;
                if (!GetExitCodeThread(handles[i], &exit) || exit == STILL_ACTIVE) return false;
            } else suspended[i] = true;
        }
        const uintptr_t begin = Pattern::Address(0x1c72d0), end = Pattern::Address(0x1c7c45);
        std::array<uintptr_t, 8192> stack{};
        for (size_t i = 0; i < count; ++i) if (suspended[i]) {
            CONTEXT context{}; context.ContextFlags = CONTEXT_CONTROL;
            if (!GetThreadContext(handles[i], &context)) return false;
            if (context.Rip >= begin && context.Rip < end) return false;
            MEMORY_BASIC_INFORMATION memory{};
            if (!VirtualQuery(reinterpret_cast<void*>(context.Rsp), &memory, sizeof(memory)) || memory.State != MEM_COMMIT || (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
            const auto available = reinterpret_cast<uintptr_t>(memory.BaseAddress) + memory.RegionSize - context.Rsp;
            const size_t bytes = (std::min)(available, sizeof(stack)); SIZE_T read = 0;
            if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(context.Rsp), stack.data(), bytes, &read) || read != bytes) return false;
            for (size_t word = 0; word < bytes / sizeof(uintptr_t); ++word) if (stack[word] >= begin && stack[word] < end) return false;
        }
        return true;
    }
};
}

bool InstallLimits() {
    if (extended.load()) return true;
    // All ten constants constrain the caller's description destination (RSI).
    // The per-line 256-byte formatter and unrelated name/link buffers are untouched.
    struct Limit { uint32_t rva; std::vector<uint8_t> instruction; size_t immediate; };
    const std::vector<Limit> limits = {
        {0x1c7709,{0xbb,0,4,0,0},1}, {0x1c771c,{0x48,0x3d,0,4,0,0},2},
        {0x1c7a18,{0xba,0,4,0,0},1}, {0x1c7a29,{0xba,0,4,0,0},1},
        {0x1c7a59,{0xbb,0,4,0,0},1}, {0x1c7a6c,{0x48,0x3d,0,4,0,0},2},
        {0x1c7baa,{0xba,0,4,0,0},1}, {0x1c7bbb,{0xba,0,4,0,0},1},
        {0x1c7bd0,{0xba,0,4,0,0},1}, {0x1c7be5,{0xba,0,4,0,0},1}
    };
    if (!Supported(Pattern::Address(0x5d97d0), {0x40,0x53,0x55,0x56,0x57,0xb8,0x98,0x26,0,0}) ||
        !Supported(Pattern::Address(0x5d9940), {0x40,0x53,0x48,0x83,0xec,0x20})) return false;
    for (const auto& limit : limits) if (!Supported(Pattern::Address(limit.rva), limit.instruction)) return false;
    PausedThreads paused;
    if (!paused.PauseOutsideDescription()) return false;
    DWORD old = 0; auto start = reinterpret_cast<void*>(Pattern::Address(0x1c7700));
    constexpr size_t size = 0x500;
    if (!VirtualProtect(start, size, PAGE_EXECUTE_READWRITE, &old)) return false;
    const uint32_t capacity = NativeCapacity;
    for (const auto& limit : limits) memcpy(reinterpret_cast<void*>(Pattern::Address(limit.rva) + limit.immediate), &capacity, sizeof(capacity));
    DWORD ignored = 0; VirtualProtect(start, size, old, &ignored); FlushInstructionCache(GetCurrentProcess(), start, size);
    extended.store(true); return true;
}
bool LimitsExtended() { return extended.load(); }
void Remember(uint32_t unitId, const std::string& full) {
    if (full.size() < LegacyCapacity) {
        if (full.size() == LegacyCapacity - 1) {
            std::lock_guard<std::mutex> lock(textMutex);
            auto found = descriptions.find(unitId);
            if (found != descriptions.end()) std::erase_if(found->second, [&](const Expansion& e) { return e.prefix == full; });
        }
        return;
    }
    const std::string prefix = full.substr(0, LegacyCapacity - 1);
    std::lock_guard<std::mutex> lock(textMutex);
    if (!descriptions.count(unitId)) {
        if (order.size() >= 64) { descriptions.erase(order.front()); order.pop_front(); }
        order.push_back(unitId);
    }
    auto& entries = descriptions[unitId];
    auto found = std::find_if(entries.begin(), entries.end(), [&](const Expansion& e) { return e.prefix == prefix; });
    if (found == entries.end()) {
        if (entries.size() >= 16) entries.erase(entries.begin());
        entries.push_back({prefix, full.substr(0, NativeCapacity - 1)});
    } else found->full = full.substr(0, NativeCapacity - 1);
}
std::string Expand(uint32_t unitId, const std::string& original) {
    std::lock_guard<std::mutex> lock(textMutex);
    auto found = descriptions.find(unitId); if (found == descriptions.end()) return original;
    struct Replacement { size_t position, length; std::string full; };
    std::vector<Replacement> replacements;
    for (const auto& entry : found->second) {
        size_t position = 0;
        while ((position = original.find(entry.prefix, position)) != std::string::npos) {
            replacements.push_back({position, entry.prefix.size(), entry.full});
            position += entry.prefix.size();
        }
    }
    // A caller can truncate the whole tooltip before all 1023 description bytes
    // fit (the name/header uses some of its space). Restore an exact partial tail
    // only when there is one unambiguous continuation for this item.
    size_t tail = std::string::npos; std::string continuation; bool ambiguous = false;
    for (const auto& entry : found->second) {
        size_t position = 0;
        while ((position = original.find(entry.prefix.substr(0, 128), position)) != std::string::npos) {
            const size_t remaining = original.size() - position;
            bool inside = false;
            for (const auto& replacement : replacements) if (position >= replacement.position && position < replacement.position + replacement.length) inside = true;
            if (!inside && remaining < entry.prefix.size() && remaining >= 128 && entry.prefix.compare(0, remaining, original, position, remaining) == 0) {
                if (position < tail) { tail = position; continuation = entry.full; ambiguous = false; }
                else if (position == tail && continuation != entry.full) ambiguous = true;
            }
            ++position;
        }
    }
    if (tail != std::string::npos && !ambiguous) replacements.push_back({tail, original.size() - tail, continuation});
    std::sort(replacements.begin(), replacements.end(), [](const Replacement& a, const Replacement& b) { return a.position > b.position; });
    std::string result = original; size_t last = original.size();
    for (const auto& replacement : replacements) {
        if (replacement.position + replacement.length > last || result.size() - replacement.length + replacement.full.size() > 65535) continue;
        result.replace(replacement.position, replacement.length, replacement.full); last = replacement.position;
    }
    return result;
}
void SetViewport(float width, float height, float scale) {
    if (width > 0 && height > 0) { viewportWidth.store(width); viewportHeight.store(height); }
    resolutionScale.store(scale);
}
void BeginPanel(uint32_t unitId, void* panel) {
    EndPanel();
    const auto address = reinterpret_cast<uintptr_t>(panel);
    Panel next;
    if (!Read(address, next.text) || !Read(address + 8, next.length) || !Read(address + 0x60, next.rect) || !next.text || next.length > 65535 || !next.length) return;
    current = std::move(next); current.address = address; current.content = current.rect;
    if (!extended.load() || !HighDefinition()) return;
    std::string original(static_cast<size_t>(current.length), '\0'); SIZE_T count = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(current.text), original.data(), original.size(), &count) || count != original.size()) return;
    // Native strings may include the terminator in their size field.
    if (!original.empty() && original.back() == '\0') original.pop_back();
    current.expanded = Expand(unitId, original);
    if (current.expanded == original) { current.expanded.clear(); return; }
    const int limits[] = {current.rect[2], current.rect[3]};
    const auto style = reinterpret_cast<void*>(address + 0x80);
    const float scale = NativeScale();
    const int oldHeight = MeasureHeight(original.c_str(), style, scale, limits);
    const int newHeight = MeasureHeight(current.expanded.c_str(), style, scale, limits);
    if (newHeight < oldHeight || newHeight > 64 * 65535) { current.expanded.clear(); return; }
    std::array<int,4> rect = current.rect;
    const int delta = (newHeight - oldHeight + 63) / 64;
    rect[3] += delta; rect[1] = (std::max)(0, rect[1] - delta);
    const auto text = reinterpret_cast<uintptr_t>(current.expanded.c_str());
    const uint64_t length = current.expanded.size();
    memcpy(panel, &text, 8); memcpy(reinterpret_cast<uint8_t*>(panel) + 8, &length, 8);
    memcpy(reinterpret_cast<uint8_t*>(panel) + 0x60, rect.data(), sizeof(rect));
    current.content = rect;
}
float LayoutScale() { return NativeScale(); }
const int* ContentRect(const int* nativeRect) {
    return current.address && reinterpret_cast<uintptr_t>(nativeRect) == current.address + 0x60 ? current.content.data() : nativeRect;
}
void FitBounds(float left, float top, float right, float bottom) {
    if (!current.address || !std::isfinite(left) || !std::isfinite(top) || !std::isfinite(right) || !std::isfinite(bottom)) return;
    const auto& content = current.content;
    if (content[2] <= 0 || content[3] <= 0 || right < left || bottom < top) return;
    left = (std::min)(left, float(content[0])); top = (std::min)(top, float(content[1]));
    right = (std::max)(right, float(content[0] + content[2])); bottom = (std::max)(bottom, float(content[1] + content[3]));
    // All positions are native pixel coordinates, including at 4K. Keep text
    // and background together when additional width/height meets a screen edge.
    const int x = static_cast<int>(std::floor(left)), y = static_cast<int>(std::floor(top));
    const int width = static_cast<int>(std::ceil(right)) - x, height = static_cast<int>(std::ceil(bottom)) - y;
    const int fitX = (std::max)(0, (std::min)(x, static_cast<int>(viewportWidth.load()) - width));
    const int fitY = (std::max)(0, (std::min)(y, static_cast<int>(viewportHeight.load()) - height));
    const int dx = fitX - x, dy = fitY - y;
    std::array<int, 4> rect{fitX, fitY, width, height};
    memcpy(reinterpret_cast<void*>(current.address + 0x60), rect.data(), sizeof(rect));
    current.content[0] += dx; current.content[1] += dy;
    // The optional secondary native panel retains its own dimensions.
    if ((dx || dy) && Read(current.address + 0x70, current.secondary)) {
        auto secondary = current.secondary; secondary[0] += dx; secondary[1] += dy;
        memcpy(reinterpret_cast<void*>(current.address + 0x70), secondary.data(), sizeof(secondary));
        current.movedSecondary = true;
    }
}
void EndPanel() {
    if (current.address) {
        memcpy(reinterpret_cast<void*>(current.address), &current.text, 8);
        memcpy(reinterpret_cast<void*>(current.address + 8), &current.length, 8);
        memcpy(reinterpret_cast<void*>(current.address + 0x60), current.rect.data(), sizeof(current.rect));
        if (current.movedSecondary) memcpy(reinterpret_cast<void*>(current.address + 0x70), current.secondary.data(), sizeof(current.secondary));
    }
    current = {};
}
}
