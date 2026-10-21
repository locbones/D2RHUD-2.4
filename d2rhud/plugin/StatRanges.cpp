#include "StatRanges.h"
#include "StatRangesData.h"
#include "TooltipText.h"
#include "GameAssets.h"
#include "ItemFilter/ItemFilter.h"
#include "../d2/D2Ptrs.h"
#include <windows.h>
#include <detours/detours.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <atomic>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <deque>
#include <future>
#include <memory>
#include <mutex>
#include <set>

namespace StatRanges {
namespace {
struct Config {
    bool enabled = false, hotkey = false, hold = true, fixed = true;
    float size = 16, spacing = 10;
    ImVec4 color{ 0.6f, 0.85f, 1, 1 };
    char format[MaxRangeFormatLength + 1] = "[%d-%d]";
} config;
Activation activation;
std::atomic<bool> active{ false };
std::atomic<std::shared_ptr<const Database>> database;
std::atomic<bool> showFixed{ true };
// Publish immutable font measurements on the ImGui thread. Native tooltip
// hooks can then measure annotations without accessing ImGui or its font atlas.
struct RangeMetrics {
    float size = 16, gap = 10, padding = 4;
    std::vector<float> advance;
    float fallback = 16;
    const ImFont* sourceFont = nullptr;
    std::string format = DefaultRangeFormat;
    float Width(const std::string& text) const {
        float width = 0;
        for (const char* p = text.c_str(); *p;) {
            unsigned int c = 0;
            const int bytes = ImTextCharFromUtf8(&c, p, nullptr);
            if (!bytes) break;
            p += bytes;
            width += c < advance.size() ? advance[c] : fallback;
        }
        return std::ceil(width);
    }
};
std::atomic<std::shared_ptr<const RangeMetrics>> rangeMetrics;
void PublishMetrics(float resolution) {
    const auto& io = ImGui::GetIO();
    auto font = io.Fonts->Fonts.Size > 2 ? io.Fonts->Fonts[2] : ImGui::GetFont();
    const float size = config.size * resolution, gap = config.spacing * resolution, padding = 4 * resolution;
    const std::string format = ValidRangeFormat(config.format) ? config.format : DefaultRangeFormat;
    auto previous = rangeMetrics.load();
    if (previous && previous->sourceFont == font && previous->size == size && previous->gap == gap && previous->padding == padding && previous->format == format) return;
    auto metrics = std::make_shared<RangeMetrics>();
    metrics->size = size; metrics->gap = gap; metrics->padding = padding; metrics->format = format; metrics->sourceFont = font;
    const float scale = size / font->FontSize;
    metrics->advance.reserve(font->IndexAdvanceX.Size);
    for (float advance : font->IndexAdvanceX) metrics->advance.push_back(advance * scale);
    metrics->fallback = font->FallbackAdvanceX * scale;
    rangeMetrics.store(std::move(metrics));
}
std::future<Database> loading;
bool loadStarted = false, installed = false, attempted = false;
std::string status = "Property tables load when this panel is opened.";
template<class T> bool Read(uintptr_t address, T& value) {
    SIZE_T count = 0;
    return address && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address), &value, sizeof(value), &count) && count == sizeof(value);
}
bool ReadBytes(uintptr_t address, void* out, size_t size) {
    SIZE_T count = 0;
    return address && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address), out, size, &count) && count == size;
}
bool Snapshot(uintptr_t unit, Item& item, uint32_t& seed, uint32_t& flags) {
    uint32_t type = 0; uintptr_t data = 0;
    if (!Read(unit, type) || type != 4 || !Read(unit + 0x10, data)) return false;
    std::array<uint8_t, 0x54> bytes{};
    if (!Read(data, bytes)) return false;
    auto value = [&](size_t offset, auto& out) { memcpy(&out, bytes.data() + offset, sizeof(out)); };
    value(0, item.quality); value(4, seed); value(0x18, flags);
    value(0x34, item.index); value(0x38, item.level); value(0x46, item.automagic);
    item.runeword = (flags & 0x04000000) != 0;
    value(0x48, item.prefixes); value(0x4e, item.suffixes);
    return true;
}
struct TooltipContext {
    const char* text = nullptr;
    std::shared_ptr<const Database> db;
    Totals totals;
    std::vector<Totals> conditional;
    int level = 1;
    uint32_t unitId = 0;
    int index = -1, quality = 0;
    std::shared_ptr<const RangeMetrics> metrics;
};
thread_local TooltipContext tooltip;
struct TextContext { const char* text = nullptr; const int* rect = nullptr; const void* style = nullptr; };
thread_local TextContext textContext;
struct DrawLine { float right, middle; std::string text; };
std::mutex drawMutex;
std::vector<DrawLine> drawLines;
std::shared_ptr<const RangeMetrics> drawMetrics;
ULONGLONG capturedAt = 0;
// Retain the item used to build the native tooltip. The generic panel draw hook
// cannot recover inventory/equipment/vendor item identity from world hover.
using DrawText = void(__fastcall*)(const char*, const int*, const void*, float);
using LayoutText = void(__fastcall*)(const char*, const void*, void*, float, const int*);
using BuildItemText = void* (__fastcall*)(void*, D2UnitStrc*, D2UnitStrc*, D2UnitStrc*, int32_t, bool);
using DestroyLayout = void(__fastcall*)(void*);
DrawText originalDraw = nullptr;
LayoutText originalLayout = nullptr;
BuildItemText originalBuild = nullptr;
DestroyLayout destroyLayout = nullptr;
struct BuiltItem { D2UnitStrc* unit; uint32_t id, seed, base; Item item; };
std::mutex builtMutex;
std::map<std::string, std::vector<BuiltItem>> builtItems;
std::deque<std::string> builtOrder;

bool IsItem(D2UnitStrc* unit) {
    uint32_t type = 0;
    return Read(reinterpret_cast<uintptr_t>(unit), type) && type == UNIT_ITEM;
}
bool SameProperties(const BuiltItem& a, const BuiltItem& b) {
    return a.base == b.base && a.item.quality == b.item.quality && a.item.index == b.item.index &&
        a.item.level == b.item.level && a.item.automagic == b.item.automagic && a.item.prefixes == b.item.prefixes &&
        a.item.suffixes == b.item.suffixes && a.item.runeword == b.item.runeword;
}
bool ItemIdentity(D2UnitStrc* unit, BuiltItem& out) {
    uint32_t flags = 0;
    out.unit = unit;
    const auto address = reinterpret_cast<uintptr_t>(unit);
    return Snapshot(address, out.item, out.seed, flags) && Read(address + 8, out.id) && Read(address + 4, out.base);
}
bool PanelText(const void* panel, std::string& text) {
    uintptr_t pointer = 0; uint64_t size = 0;
    const auto address = reinterpret_cast<uintptr_t>(panel);
    if (!Read(address, pointer) || !pointer || !Read(address + 8, size) || !size || size > 65535) return false;
    text.resize(static_cast<size_t>(size));
    if (!ReadBytes(pointer, text.data(), text.size())) return false;
    if (!text.empty() && text.back() == '\0') text.pop_back();
    return !text.empty();
}
void RememberBuiltItem(D2UnitStrc* unit, const void* result) {
    BuiltItem entry{}; std::string text;
    // The native builder returns a blz::string (pointer, length, capacity),
    // which is later copied into TooltipsPanel's first string.
    if (!ItemIdentity(unit, entry) || !PanelText(result, text)) return;
    std::lock_guard<std::mutex> lock(builtMutex);
    if (!builtItems.count(text)) {
        if (builtOrder.size() >= 128) { builtItems.erase(builtOrder.front()); builtOrder.pop_front(); }
        builtOrder.push_back(text);
    }
    auto& candidates = builtItems[text];
    std::erase_if(candidates, [&](const BuiltItem& old) { return old.unit == unit || old.id == entry.id; });
    if (candidates.size() >= 4) candidates.erase(candidates.begin());
    candidates.push_back(std::move(entry));
}
void* __fastcall HookBuild(void* output, D2UnitStrc* viewer, D2UnitStrc* owner, D2UnitStrc* item, int32_t context, bool details) {
    auto result = originalBuild(output, viewer, owner, item, context, details);
    try { RememberBuiltItem(item, result); } catch (const std::exception&) { /* Native tooltip remains usable. */ }
    return result;
}
D2UnitStrc* TooltipItem(D2UnitStrc* worldHover, const void* panel) {
    std::string text;
    if (PanelText(panel, text)) {
        std::lock_guard<std::mutex> lock(builtMutex);
        auto found = builtItems.find(text);
        if (found != builtItems.end()) {
            BuiltItem selected{};
            bool ambiguous = false;
            for (const auto& candidate : found->second) {
                BuiltItem live{};
                if (!ItemIdentity(candidate.unit, live) || live.id != candidate.id || live.seed != candidate.seed || !SameProperties(live, candidate)) continue;
                if (live.unit == worldHover) return live.unit;
                if (selected.unit && selected.unit != live.unit) {
                    // Identical displayed text can have different affix bounds.
                    // Do not guess which item a generic comparison panel owns.
                    uintptr_t a = 0, b = 0;
                    Read(reinterpret_cast<uintptr_t>(selected.unit) + 0x90, a);
                    Read(reinterpret_cast<uintptr_t>(live.unit) + 0x90, b);
                    if (!SameProperties(selected, live) || a || b) ambiguous = true;
                }
                selected = std::move(live);
            }
            // A stale saved pointer must not turn into an unrelated world item.
            return ambiguous ? nullptr : selected.unit;
        }
    }
    return IsItem(worldHover) ? worldHover : nullptr;
}
template<class Address> bool SupportedBuilder(Address&& address) {
    std::array<uint8_t, 0x127a> bytes{};
    if (!ReadBytes(address(0x1bb940), bytes.data(), bytes.size())) return false;
    uint64_t hash = 14695981039346656037ull;
    for (auto byte : bytes) hash = (hash ^ byte) * 1099511628211ull;
    return hash == 0xa10c4b19a969c597ull;
}

void Utf8(std::string& out, uint32_t c) {
    if (c < 0x80) out += static_cast<char>(c);
    else if (c < 0x800) { out += static_cast<char>(0xc0 | (c >> 6)); out += static_cast<char>(0x80 | (c & 63)); }
    else if (c < 0x10000) { out += static_cast<char>(0xe0 | (c >> 12)); out += static_cast<char>(0x80 | ((c >> 6) & 63)); out += static_cast<char>(0x80 | (c & 63)); }
    else if (c <= 0x10ffff) { out += static_cast<char>(0xf0 | (c >> 18)); out += static_cast<char>(0x80 | ((c >> 12) & 63)); out += static_cast<char>(0x80 | ((c >> 6) & 63)); out += static_cast<char>(0x80 | (c & 63)); }
}
#pragma pack(push, 1)
struct Glyph {
    uint32_t character, color, extra;
    int32_t x, y;
    uint32_t rgba[4];
    int32_t penX, advance;
    float background[4];
};
struct Row { uint64_t end; int32_t x, unused; };
#pragma pack(pop)
static_assert(sizeof(Glyph) == 0x3c && offsetof(Glyph, penX) == 0x24);
static_assert(sizeof(Row) == 16);

std::vector<DrawLine> CollectLines(const void* layout, const TextContext& context) {
    if (!context.text || context.text != tooltip.text || !tooltip.db) return {};
    uintptr_t glyphs = 0, rows = 0; uint64_t glyphCount = 0, rowCount = 0;
    const auto address = reinterpret_cast<uintptr_t>(layout);
    int32_t totalHeight = 0; std::array<int, 4> rect{};
    if (!Read(address, glyphs) || !Read(address + 8, glyphCount) ||
        !Read(address + 0x1e18, rows) || !Read(address + 0x1e20, rowCount) ||
        !Read(address + 0x2634, totalHeight) || !Read(reinterpret_cast<uintptr_t>(context.rect), rect) ||
        !rowCount || rowCount > 1024 || !glyphCount || glyphCount > 65536 || totalHeight <= 0) return {};
    std::vector<Glyph> chars(glyphCount); std::vector<Row> lines(rowCount);
    if (!ReadBytes(glyphs, chars.data(), chars.size() * sizeof(Glyph)) || !ReadBytes(rows, lines.data(), lines.size() * sizeof(Row))) return {};
    const auto style = reinterpret_cast<uintptr_t>(context.style);
    int align = 0, direction = 0; float lineFactor = 1;
    if (!Read(style + 0x2c, align) || !Read(style + 0x34, lineFactor) || !Read(style + 0x38, direction) || !std::isfinite(lineFactor) || lineFactor <= 0) return {};
    // Mirror the native HD renderer's 26.6 layout, including bottom-up tooltips.
    const int rowStep = totalHeight / static_cast<int>(rowCount);
    const float cell = rowStep / (64.0f * lineFactor);
    int shift = 0;
    if (align) {
        shift = rect[3] * 64 - static_cast<int>(cell * 64) + rowStep - totalHeight;
        if (align != 2) shift /= 2;
    }
    if (direction == 2) shift += totalHeight - rowStep;
    const int step = direction == 2 ? -rowStep : rowStep;
    std::vector<DrawLine> result;
    size_t begin = 0;
    // Cache only within this capture: localized regex matching is done once per
    // distinct displayed line and item, not for every glyph.
    static thread_local std::map<std::string, Annotation> matched;
    static thread_local Totals lastTotals;
    static thread_local const Database* lastDatabase = nullptr;
    static thread_local int lastLevel = 0;
    static thread_local bool lastFixed = false;
    static thread_local int lastIndex = -1, lastQuality = 0;
    const bool fixed = showFixed.load(std::memory_order_relaxed);
    if (lastDatabase != tooltip.db.get() || lastTotals != tooltip.totals || lastLevel != tooltip.level || lastFixed != fixed || lastIndex != tooltip.index || lastQuality != tooltip.quality) {
        matched.clear(); lastTotals = tooltip.totals; lastDatabase = tooltip.db.get(); lastLevel = tooltip.level; lastFixed = fixed; lastIndex = tooltip.index; lastQuality = tooltip.quality;
    }
    for (size_t row = 0; row < lines.size(); ++row) {
        const auto end = lines[row].end;
        if (end < begin || end > chars.size()) return {};
        std::string text; int right = 0;
        bool green = false;
        for (size_t c = begin; c < end; ++c) {
            Utf8(text, chars[c].character);
            right = (std::max)(right, chars[c].penX + chars[c].advance);
            if (chars[c].rgba[1] > chars[c].rgba[0] * 1.3f && chars[c].rgba[1] > chars[c].rgba[2] * 1.3f) green = true;
        }
        begin = end;
        if (text.empty()) continue;
        const auto cacheKey = (green ? "set:" : "base:") + text;
        auto found = matched.find(cacheKey);
        if (found == matched.end()) {
            auto annotation = green ? Annotation{} : MatchLine(*tooltip.db, tooltip.totals, text, fixed, tooltip.level);
            // Set bonuses are separate tooltip lines. Never add inactive bonus
            // properties to the item's permanent stats.
            if (!annotation.found) {
                std::map<std::string, Annotation> candidates;
                for (const auto& bonus : tooltip.conditional) {
                    auto a = MatchLine(*tooltip.db, bonus, text, fixed, tooltip.level);
                    if (a.found) { auto key = a.text; candidates.emplace(std::move(key), std::move(a)); }
                }
                if (candidates.size() == 1) annotation = candidates.begin()->second;
            }
            if (green && !annotation.found) annotation = MatchLine(*tooltip.db, tooltip.totals, text, fixed, tooltip.level);
            found = matched.emplace(cacheKey, std::move(annotation)).first;
        }
        if (found->second.found) result.push_back({ rect[0] + (lines[row].x + right) / 64.0f,
            rect[1] + (shift + step * static_cast<int>(row)) / 64.0f + cell * 0.5f,
            FormatAnnotation(found->second, tooltip.metrics ? tooltip.metrics->format : DefaultRangeFormat) });
    }
    return result;
}
void CaptureLayout(const void* layout) {
    auto result = CollectLines(layout, textContext);
    std::lock_guard<std::mutex> lock(drawMutex);
    drawLines = std::move(result); drawMetrics = tooltip.metrics; capturedAt = GetTickCount64();
}
void FitTooltip(const void* panel) {
    if (!tooltip.metrics || !originalLayout || !destroyLayout) return;
    const auto rect = TooltipText::ContentRect(reinterpret_cast<const int*>(static_cast<const uint8_t*>(panel) + 0x60));
    const auto style = static_cast<const uint8_t*>(panel) + 0x80;
    // Match the native layout's inline vectors; only the native destructor may
    // release spilled glyph/row allocations (including long descriptions).
    struct Layout {
        alignas(16) std::array<uint8_t, 0x2638> bytes{};
        ~Layout() { destroyLayout(bytes.data()); }
    } layout;
    const uintptr_t chars = reinterpret_cast<uintptr_t>(layout.bytes.data() + 0x18);
    const uintptr_t rows = reinterpret_cast<uintptr_t>(layout.bytes.data() + 0x1e30);
    constexpr uint64_t capacity = 0x8000000000000080ull;
    memcpy(layout.bytes.data(), &chars, 8); memcpy(layout.bytes.data() + 0x10, &capacity, 8);
    memcpy(layout.bytes.data() + 0x1e18, &rows, 8); memcpy(layout.bytes.data() + 0x1e28, &capacity, 8);
    originalLayout(tooltip.text, style, layout.bytes.data(), TooltipText::LayoutScale(), rect + 2);
    const auto lines = CollectLines(layout.bytes.data(), {tooltip.text, rect, style});
    if (lines.empty()) return;
    float left = float(rect[0]), top = float(rect[1]), right = float(rect[0] + rect[2]), bottom = float(rect[1] + rect[3]);
    const auto& metrics = *tooltip.metrics;
    for (const auto& line : lines) {
        right = (std::max)(right, line.right + metrics.gap + metrics.Width(line.text) + metrics.padding);
        top = (std::min)(top, line.middle - metrics.size * 0.5f - metrics.padding);
        bottom = (std::max)(bottom, line.middle + metrics.size * 0.5f + metrics.padding);
    }
    TooltipText::FitBounds(left, top, right, bottom);
}
void __fastcall HookLayout(const char* text, const void* style, void* result, float scale, const int* limits) {
    originalLayout(text, style, result, scale, limits);
    if (active.load(std::memory_order_relaxed) && text == textContext.text) CaptureLayout(result);
}
void __fastcall HookDraw(const char* text, const int* rect, const void* style, float scale) {
    rect = TooltipText::ContentRect(rect);
    const auto previous = textContext;
    textContext = text == tooltip.text ? TextContext{ text, rect, style } : TextContext{};
    originalDraw(text, rect, style, scale);
    textContext = previous;
}
bool Supported(uintptr_t address, const std::vector<uint8_t>& signature) {
    std::vector<uint8_t> bytes(signature.size());
    return ReadBytes(address, bytes.data(), bytes.size()) && bytes == signature;
}
void ResetActivation() { activation.Reset(); active.store(false); std::lock_guard<std::mutex> lock(drawMutex); drawLines.clear(); }
bool UpdateActivation(bool inGame, bool foreground, bool menusOpen, bool textInput, bool bindingDown, bool previewOpen = false) {
    // A saved always-on setting does not consume keyboard input. HUD menus and
    // text fields only suppress hotkey handling, not that display preference.
    const bool allowed = inGame && foreground && (!config.hotkey || (!menusOpen && !textInput));
    const bool activated = activation.Update(config.hotkey, config.hold, config.enabled, bindingDown, allowed);
    // Editing preview is transient: never latch a hotkey or change saved state.
    // An already-latched toggle also remains visible during menu interaction.
    return activated || (inGame && foreground && config.hotkey && (previewOpen || (!config.hold && activation.latched)));
}
}

bool UsesHotkey() { return config.hotkey; }
void Install() {
    if (attempted) return;
    attempted = true;
    const uintptr_t draw = Pattern::Address(0x5d6230), layout = Pattern::Address(0x5d97d0);
    if (!Supported(draw, { 0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x60 }) ||
        !Supported(layout, { 0x40, 0x53, 0x55, 0x56, 0x57, 0xb8, 0x98, 0x26, 0x00, 0x00 }) ||
        !Supported(Pattern::Address(0x5d9940), {0x40,0x53,0x48,0x83,0xec,0x20}) ||
        !SupportedBuilder([](uint32_t rva) { return Pattern::Address(rva); })) {
        status = "Stat range text layout is unavailable for this game executable."; return;
    }
    originalDraw = reinterpret_cast<DrawText>(draw); originalLayout = reinterpret_cast<LayoutText>(layout);
    originalBuild = reinterpret_cast<BuildItemText>(Pattern::Address(0x1bb940));
    destroyLayout = reinterpret_cast<DestroyLayout>(Pattern::Address(0x5d9940));
    DetourTransactionBegin(); DetourUpdateThread(GetCurrentThread());
    LONG error = DetourAttach(reinterpret_cast<PVOID*>(&originalDraw), HookDraw);
    if (!error) error = DetourAttach(reinterpret_cast<PVOID*>(&originalLayout), HookLayout);
    if (!error) error = DetourAttach(reinterpret_cast<PVOID*>(&originalBuild), HookBuild);
    if (error) { DetourTransactionAbort(); status = "Could not attach stat range text layout hooks."; return; }
    installed = DetourTransactionCommit() == NO_ERROR;
    if (!installed) status = "Could not install stat range text layout hooks.";
}

void BeginTooltip(D2UnitStrc* unit, const void* panel) {
    tooltip = {};
    unit = TooltipItem(unit, panel);
    uint32_t unitId = 0;
    Read(reinterpret_cast<uintptr_t>(unit) + 8, unitId);
    TooltipText::BeginPanel(unitId, const_cast<void*>(panel));
    if (!active.load(std::memory_order_relaxed)) return;
    auto db = database.load(); if (!db) return;
    Item item; uint32_t seed = 0, flags = 0;
    const auto address = reinterpret_cast<uintptr_t>(unit);
    if (!Snapshot(address, item, seed, flags) || !(flags & 0x10) || item.quality == 8 || item.quality == 9) return;
    const char* text = nullptr;
    if (!Read(reinterpret_cast<uintptr_t>(panel), text) || !text) return;
    tooltip.db = std::move(db); tooltip.text = text;
    tooltip.metrics = rangeMetrics.load();
    tooltip.totals = tooltip.db->ForItem(item);
    tooltip.index = item.index; tooltip.quality = item.quality;
    Read(address + 8, tooltip.unitId);
    // Socketed jewels retain their own affixes. Gems/runes use the parent item's
    // gemapplytype, which can differ for weapons, armor/helms, and shields.
    uint32_t baseId = 0; uintptr_t inventory = 0, socket = 0;
    Read(address + 4, baseId);
    auto base = tooltip.db->baseItems.find(baseId);
    int gemType = base == tooltip.db->baseItems.end() ? -1 : base->second.gemType;
    std::vector<std::string> socketCodes;
    if (Read(address + 0x90, inventory) && Read(inventory + 0x10, socket)) {
        std::set<uintptr_t> visited;
        while (socket && visited.size() < 12 && visited.insert(socket).second) {
            Item child; uint32_t childSeed = 0, childFlags = 0, childBaseId = 0; uintptr_t data = 0, next = 0;
            if (!Snapshot(socket, child, childSeed, childFlags) || !Read(socket + 0x10, data)) break;
            auto values = tooltip.db->ForItem(child);
            for (const auto& entry : values) Add(tooltip.totals, entry.first, entry.second);
            if (Read(socket + 4, childBaseId)) {
                auto childBase = tooltip.db->baseItems.find(childBaseId);
                socketCodes.push_back(childBase == tooltip.db->baseItems.end() ? "" : childBase->second.code);
                if (childBase != tooltip.db->baseItems.end()) {
                    auto gem = tooltip.db->gems.find(childBase->second.code);
                    if (gem != tooltip.db->gems.end() && gemType >= 0 && gemType < 3) tooltip.db->Apply(tooltip.totals, gem->second[gemType]);
                }
            }
            Read(data + 0xb0, next); socket = next;
        }
    }
    if (flags & 0x04000000) if (auto props = tooltip.db->ForRuneword(baseId, socketCodes)) tooltip.db->Apply(tooltip.totals, *props);
    if (item.quality == 5) {
        auto found = tooltip.db->sets.find(item.index);
        if (found != tooltip.db->sets.end()) {
            for (const auto& props : found->second.bonuses) { Totals t; tooltip.db->Apply(t, props); tooltip.conditional.push_back(std::move(t)); }
            auto bonuses = tooltip.db->setBonuses.find(found->second.set);
            if (bonuses != tooltip.db->setBonuses.end()) for (const auto& props : bonuses->second) {
                Totals t; tooltip.db->Apply(t, props); tooltip.conditional.push_back(std::move(t));
            }
        }
    }
    auto player = GetClientPlayerUnit();
    if (player && STATLIST_GetUnitStatSigned) tooltip.level = (std::clamp)(STATLIST_GetUnitStatSigned(player, 12, 0), 1, 999);
    FitTooltip(panel);
}
void EndTooltip() { tooltip = {}; TooltipText::EndPanel(); }

void Update(const std::string& mod, bool inGame, bool menusOpen, bool bindingDown, bool previewOpen) {
    const auto& io = ImGui::GetIO();
    const float resolution = io.DisplaySize.x >= 3840 || io.DisplaySize.y >= 2160 ? 2.0f : 1.0f;
    TooltipText::SetViewport(io.DisplaySize.x, io.DisplaySize.y, resolution);
    PublishMetrics(resolution);
    if (!loadStarted) {
        loadStarted = true;
        wchar_t path[MAX_PATH]{}; GetModuleFileNameW(nullptr, path, MAX_PATH);
        loading = std::async(std::launch::async, [install = std::filesystem::path(path).parent_path(), mod]() { GameAssets assets(install, mod); return LoadDatabase(assets); });
        status = "Loading item properties and localized stat descriptions...";
    }
    if (loading.valid() && loading.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto result = std::make_shared<const Database>(loading.get());
        if (installed) status = result->error.empty() ? "Item property tables loaded." : result->error;
        database.store(std::move(result));
    }
    DWORD foreground = 0; GetWindowThreadProcessId(GetForegroundWindow(), &foreground);
    if (!inGame) {
        activation.latched = false;
        std::lock_guard<std::mutex> lock(builtMutex); builtItems.clear(); builtOrder.clear();
    }
    showFixed.store(config.fixed, std::memory_order_relaxed);
    active.store(installed && UpdateActivation(inGame, foreground == GetCurrentProcessId(), menusOpen, io.WantTextInput, bindingDown, previewOpen));
}

void Render(float resolutionScale) {
    std::vector<DrawLine> lines;
    std::shared_ptr<const RangeMetrics> metrics;
    {
        std::lock_guard<std::mutex> lock(drawMutex);
        if (active.load() && GetTickCount64() - capturedAt < 100) { lines.swap(drawLines); metrics = drawMetrics; }
        else drawLines.clear();
    }
    if (lines.empty()) return;
    auto& io = ImGui::GetIO();
    auto font = io.Fonts->Fonts.Size > 2 ? io.Fonts->Fonts[2] : ImGui::GetFont();
    const float size = metrics ? metrics->size : config.size * resolutionScale, gap = metrics ? metrics->gap : config.spacing * resolutionScale;
    auto draw = ImGui::GetBackgroundDrawList();
    for (const auto& line : lines) {
        ImVec2 pos(line.right + gap, line.middle - size * 0.5f);
        draw->AddText(font, size, pos, ImGui::ColorConvertFloat4ToU32(config.color), line.text.c_str());
    }
}

void LoadFromJson(const nlohmann::json& root) {
    config = {}; ResetActivation();
    if (!root.contains("StatRanges") || !root["StatRanges"].is_object()) return;
    const auto& j = root["StatRanges"];
    auto boolean = [&](const char* key, bool& value) { if (j.contains(key) && j[key].is_boolean()) value = j[key].get<bool>(); };
    boolean("Enabled", config.enabled); boolean("ShowFixed", config.fixed);
    if (j.contains("Format") && j["Format"].is_string()) {
        const auto format = j["Format"].get<std::string>();
        if (ValidRangeFormat(format)) strcpy_s(config.format, format.c_str());
    }
    if (j.contains("Mode") && j["Mode"].is_string()) config.hotkey = j["Mode"] == "Hotkey";
    if (j.contains("HotkeyBehavior") && j["HotkeyBehavior"].is_string()) config.hold = j["HotkeyBehavior"] != "Toggle";
    auto number = [&](const char* key, float& value, float low, float high) {
        if (j.contains(key) && j[key].is_number()) { const float n = j[key].get<float>(); if (std::isfinite(n)) value = (std::clamp)(n, low, high); }
    };
    number("TextSize", config.size, 8, 40); number("Spacing", config.spacing, 0, 100);
    if (j.contains("Color") && j["Color"].is_array() && j["Color"].size() == 4) for (int i = 0; i < 4; ++i) {
        if (j["Color"][i].is_number()) { float v = j["Color"][i].get<float>(); if (std::isfinite(v)) (&config.color.x)[i] = (std::clamp)(v, 0.0f, 1.0f); }
    }
}
void SaveToJson(nlohmann::ordered_json& root) {
    root["StatRanges"] = { { "Enabled", config.enabled }, { "Mode", config.hotkey ? "Hotkey" : "Toggle" }, { "HotkeyBehavior", config.hold ? "Hold" : "Toggle" },
        { "ShowFixed", config.fixed }, { "TextSize", config.size }, { "Spacing", config.spacing },
        { "Format", ValidRangeFormat(config.format) ? config.format : DefaultRangeFormat },
        { "Color", { config.color.x, config.color.y, config.color.z, config.color.w } } };
}
void DrawSettingsPanel(float scale) {
    int mode = config.hotkey ? 1 : 0;
    if (ImGui::Combo("Operation mode", &mode, "Toggle (always on/off)\0Hotkey\0")) { config.hotkey = mode == 1; ResetActivation(); }
    if (!config.hotkey) ImGui::Checkbox("Show stat ranges", &config.enabled);
    else {
        int behavior = config.hold ? 1 : 0;
        if (ImGui::Combo("Hotkey behavior", &behavior, "Toggle on each press\0Hold to show\0")) { config.hold = behavior == 1; ResetActivation(); }
        ImGui::TextWrapped("Assign Show Stat Ranges in Hotkey Controls. Hotkey toggle starts off when you enter a game.");
        ImGui::TextWrapped("Real item tooltips preview ranges while this panel is open. The preview does not change your hotkey toggle state.");
    }
    ImGui::Separator();
    ImGui::Checkbox("Show fixed rolls (min equals max)", &config.fixed);
    ImGui::ColorEdit4("Range text color", &config.color.x, ImGuiColorEditFlags_AlphaBar);
    ImGui::SliderFloat("Range text size", &config.size, 8, 40, "%.0f px");
    ImGui::SliderFloat("Spacing from stat", &config.spacing, 0, 100, "%.0f px");
    ImGui::InputText("Range format", config.format, sizeof(config.format));
    ImGui::TextWrapped("First %%d = minimum, second %%d = maximum. Use %%%% for a percent sign. Example: Min: %%d, Max: %%d");
    if (!ValidRangeFormat(config.format)) ImGui::TextWrapped("Use exactly two %%d placeholders on one line. Preview and saving use [%%d-%%d] until the format is valid.");
    if (ImGui::Button("Reset format")) strcpy_s(config.format, DefaultRangeFormat);
    ImGui::TextWrapped("Text size and spacing follow the HUD's resolution scaling.");
    ImGui::Separator();
    ImGui::TextUnformatted("Preview");
    ImGui::TextUnformatted("+25 to Strength"); ImGui::SameLine(0, config.spacing * scale);
    auto pos = ImGui::GetCursorScreenPos();
    auto font = ImGui::GetFont(); const float size = config.size * scale;
    const auto preview = FormatAnnotation({"[20-30]", true, {{20,30}}}, config.format);
    auto extent = font->CalcTextSizeA(size, FLT_MAX, 0, preview.c_str());
    ImGui::GetWindowDrawList()->AddText(font, size, pos, ImGui::ColorConvertFloat4ToU32(config.color), preview.c_str());
    ImGui::Dummy(extent);
    ImGui::Spacing();
    ImGui::TextWrapped("Ranges use this item's unique/set properties and its actual affix IDs. Multiple contributions to the same stat and skill are added together. Set bonuses are handled as separate lines.");
    ImGui::TextWrapped("Ranges show possible rolls for these properties. Combined damage lines format each roll range separately, separated by /.");
    ImGui::Spacing(); ImGui::TextWrapped("%s", status.c_str());
    ImGui::TextWrapped("%s", TooltipText::LimitsExtended() ? "Extended stat descriptions: 16,384 bytes." : "Native stat descriptions: 1,024 bytes (extension unavailable for this executable).");
}
}
