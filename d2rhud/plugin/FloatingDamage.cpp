#include "FloatingDamage.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cfloat>
#include <deque>
#include <filesystem>
#include <mutex>
#include <random>
#include <unordered_map>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../D3D12Hook.h"

using ordered_json = nlohmann::ordered_json;

namespace FloatingDamage {
namespace {

constexpr int kFontPresetCount = D3D12::kFloatingDamageFontCount;

const char* kFontPresetLabels[] = {
    "Exocet",
    "Akaya Telivagala",
    "ReggaeOne",
    "SansitaSwashed",
    "DM Mono",
    "Girassol",
    "Turret Road",
    "Literata",
    "Zilla Slab",
    "Aref Ruqaa",
    "Formal 436",
    "PoE"
};

int ParseLegacyCustomFontIndex(const std::string& filename)
{
    const std::string stem = std::filesystem::path(filename).stem().string();
    if (stem.empty() || !std::all_of(stem.begin(), stem.end(), [](unsigned char c) { return std::isdigit(c); }))
        return 0;
    try {
        return std::clamp(std::stoi(stem), 0, kFontPresetCount - 1);
    }
    catch (...) {
        return 0;
    }
}

struct TickPopup {
    std::string text;
    float age = 0.0f;
    float duration = 0.0f;
    float startOffsetX = 0.0f;
    float startOffsetY = 0.0f;
    float endOffsetX = 0.0f;
    float endOffsetY = 0.0f;
};

struct DamageEvent {
    int amount = 0;
    float screenX = 0.0f;
    float screenY = 0.0f;
    Kind kind = Kind::Normal;
    Element element = Element::Physical;
    float durationSeconds = 0.0f;
    uint32_t targetUnitType = UINT32_MAX;
    uint32_t targetUnitId = UINT32_MAX;
};

struct DamageNumber {
    std::string text;
    float x = 0.0f;
    float y = 0.0f;
    float anchorX = 0.0f;
    float anchorY = 0.0f;
    float localOffsetX = 0.0f;
    float localOffsetY = 0.0f;
    float animatedOffsetX = 0.0f;
    float animatedOffsetY = 0.0f;
    float drawX = 0.0f;
    float drawY = 0.0f;
    float combineIdleSeconds = 0.0f;
    float hitPulseAge = 0.0f;
    float vx = 0.0f;
    float vy = 0.0f;
    float age = 0.0f;
    float lifetime = 0.0f;
    Kind kind = Kind::Normal;
    Element element = Element::Physical;
    bool isProminent = false;
    bool canCombineHits = false;
    int combinedAmount = 0;
    int spreadColumn = 0;
    float prominenceScale = 1.0f;
    std::vector<TickPopup> tickPopups;
    uint32_t targetUnitType = UINT32_MAX;
    uint32_t targetUnitId = UINT32_MAX;
};

struct DpsSample {
    int amount = 0;
    float ageSeconds = 0.0f;
};

Config g_config;
std::vector<DamageNumber> g_numbers;
std::vector<DamageEvent> g_pendingEvents;
std::deque<DpsSample> g_dpsSamples;
long long g_rollingDamage = 0;
std::mt19937 g_rng{ std::random_device{}() };
std::mutex g_queueMutex;

int ClampInt(int value, int minValue, int maxValue)
{
    return std::clamp(value, minValue, maxValue);
}

float ClampFloat(float value, float minValue, float maxValue)
{
    return std::clamp(value, minValue, maxValue);
}

float RandomFloat(float minValue, float maxValue)
{
    std::uniform_real_distribution<float> dist(minValue, maxValue);
    return dist(g_rng);
}

int RandomInt(int minValue, int maxValue)
{
    std::uniform_int_distribution<int> dist(minValue, maxValue);
    return dist(g_rng);
}

ImU32 ToImColor(const ImVec4& color, float alpha)
{
    return ImGui::ColorConvertFloat4ToU32(ImVec4(color.x, color.y, color.z, color.w * alpha));
}

bool IsProminent(Kind kind)
{
    return kind != Kind::Normal;
}

float GetProminenceScale(Kind kind)
{
    return kind == Kind::Critical ? 1.06f : 1.0f;
}

ImVec4 GetDamageColor(Kind kind)
{
    return kind == Kind::Critical ? g_config.criticalColor : g_config.normalColor;
}

ImVec4 GetElementColor(Element element)
{
    switch (element)
    {
    case Element::Fire: return g_config.fireColor;
    case Element::Lightning: return g_config.lightningColor;
    case Element::Cold: return g_config.coldColor;
    case Element::Poison: return g_config.poisonColor;
    case Element::Magic: return g_config.magicColor;
    case Element::Physical:
    default: return g_config.physicalColor;
    }
}

ImVec4 ResolveEventColor(const DamageEvent& event)
{
    if (g_config.colorByDamageType)
        return GetElementColor(event.element);
    return GetDamageColor(event.kind);
}

ImVec4 ResolveNumberColor(Kind kind, Element element)
{
    DamageEvent event{};
    event.kind = kind;
    event.element = element;
    return ResolveEventColor(event);
}

std::string FormatWholeWithThousands(int amount)
{
    std::string raw = std::to_string(std::max(0, amount));
    std::string result;
    int digitCount = 0;
    for (auto it = raw.rbegin(); it != raw.rend(); ++it)
    {
        if (digitCount > 0 && digitCount % 3 == 0)
            result.insert(result.begin(), ',');
        result.insert(result.begin(), *it);
        ++digitCount;
    }
    return result;
}

std::string FormatDamageAmount(int amount)
{
    static const char* suffixes[] = { "", "k", "m", "b", "t" };

    const int clampedAmount = std::max(0, amount);
    double value = static_cast<double>(clampedAmount);
    int suffixIndex = 0;

    while (value >= 10000.0 && suffixIndex < 4)
    {
        value /= 1000.0;
        ++suffixIndex;
    }

    if (suffixIndex == 0)
        return FormatWholeWithThousands(clampedAmount);

    int roundedTenths = static_cast<int>((value * 10.0) + 0.5);
    if (roundedTenths >= 10000 && suffixIndex < 4)
    {
        value /= 1000.0;
        ++suffixIndex;
        roundedTenths = static_cast<int>((value * 10.0) + 0.5);
    }

    char text[32]{};
    if (roundedTenths < 1000 && roundedTenths % 10 != 0)
        std::snprintf(text, sizeof(text), "%d,%d%s", roundedTenths / 10, roundedTenths % 10, suffixes[suffixIndex]);
    else
        std::snprintf(text, sizeof(text), "%d%s", (roundedTenths + 5) / 10, suffixes[suffixIndex]);

    return text;
}

std::string FormatDpsAmount(long long amount)
{
    return FormatDamageAmount(static_cast<int>(std::min<long long>(amount, INT32_MAX)));
}

bool HasDamageTarget(uint32_t unitType, uint32_t unitId)
{
    return unitType != UINT32_MAX && unitId != UINT32_MAX;
}

bool IsHitCombiningEligible(const DamageEvent& event)
{
    return g_config.enableHitCombining &&
        event.kind == Kind::Normal &&
        event.amount > 0 &&
        event.amount <= g_config.maxCombinedHitSize &&
        HasDamageTarget(event.targetUnitType, event.targetUnitId);
}

bool IsSameTarget(const DamageNumber& number, const DamageEvent& event)
{
    return number.targetUnitType == event.targetUnitType &&
        number.targetUnitId == event.targetUnitId;
}

int GetNumberOfColumns()
{
    return ClampInt(g_config.numberOfColumns, 1, 9);
}

float GetColumnOffset(int column)
{
    if (column <= 0)
        return 0.0f;

    const int distance = (column + 1) / 2;
    const float direction = (column % 2) == 1 ? -1.0f : 1.0f;
    return direction * static_cast<float>(distance) * g_config.columnSpacing;
}

int GetNearestColumn(float sideOffset, int columnCount)
{
    int bestColumn = 0;
    float bestDistance = std::abs(sideOffset - GetColumnOffset(0));
    for (int column = 1; column < columnCount; ++column)
    {
        const float distance = std::abs(sideOffset - GetColumnOffset(column));
        if (distance < bestDistance)
        {
            bestColumn = column;
            bestDistance = distance;
        }
    }
    return bestColumn;
}

float GetScreenDistance(float x1, float y1, float x2, float y2)
{
    const float dx = x2 - x1;
    const float dy = y2 - y1;
    return std::sqrt(dx * dx + dy * dy);
}

bool IsSpreadRelatedNumber(const DamageNumber& number, const DamageEvent& event)
{
    const bool eventHasTarget = HasDamageTarget(event.targetUnitType, event.targetUnitId);
    const bool numberHasTarget = HasDamageTarget(number.targetUnitType, number.targetUnitId);

    if (eventHasTarget && numberHasTarget)
        return IsSameTarget(number, event);

    if (!eventHasTarget && !numberHasTarget)
        return GetScreenDistance(number.anchorX, number.anchorY, event.screenX, event.screenY) <= 72.0f;

    return false;
}

struct SpreadPlacement {
    int column = 0;
    float sideOffset = 0.0f;
    float verticalOffset = 0.0f;
};

SpreadPlacement CalculateSpreadPlacement(const DamageEvent& event)
{
    SpreadPlacement placement{};
    if (!g_config.spreadNumbersHorizontally || IsHitCombiningEligible(event))
        return placement;

    const int columnCount = GetNumberOfColumns();
    const float reuseSeconds = std::max(0.05f, g_config.columnReuseTimeSeconds);

    std::vector<int> columnCounts(static_cast<size_t>(columnCount), 0);
    std::vector<float> columnOldestAge(static_cast<size_t>(columnCount), -1.0f);
    int relatedCount = 0;

    for (const auto& number : g_numbers)
    {
        if (number.age >= number.lifetime)
            continue;
        if (number.age > reuseSeconds)
            continue;
        if (!IsSpreadRelatedNumber(number, event))
            continue;

        ++relatedCount;
        int column = number.spreadColumn >= 0 && number.spreadColumn < columnCount
            ? number.spreadColumn
            : GetNearestColumn(number.localOffsetX, columnCount);
        columnCounts[static_cast<size_t>(column)] += 1;
        columnOldestAge[static_cast<size_t>(column)] = std::max(columnOldestAge[static_cast<size_t>(column)], number.age);
    }

    int bestColumn = 0;
    for (int column = 0; column < columnCount; ++column)
    {
        if (columnCounts[static_cast<size_t>(column)] == 0)
        {
            bestColumn = column;
            break;
        }

        const int bestCount = columnCounts[static_cast<size_t>(bestColumn)];
        const int columnCountValue = columnCounts[static_cast<size_t>(column)];
        const float bestAge = columnOldestAge[static_cast<size_t>(bestColumn)];
        const float columnAge = columnOldestAge[static_cast<size_t>(column)];
        if (columnCountValue < bestCount || (columnCountValue == bestCount && columnAge > bestAge))
            bestColumn = column;
    }

    const int verticalStepCount = columnCount > 0 ? relatedCount / columnCount : 0;
    placement.column = bestColumn;
    placement.sideOffset = GetColumnOffset(bestColumn);
    placement.verticalOffset = std::min(
        std::max(0.0f, g_config.maxStackHeight),
        static_cast<float>(verticalStepCount) * g_config.stackHeightStep
    );
    return placement;
}

void AddTickPopup(DamageNumber& number, const DamageEvent& event)
{
    if (!g_config.showTickPopups || event.amount <= 0)
        return;

    TickPopup tick{};
    tick.text = "+" + FormatDamageAmount(event.amount);
    tick.duration = std::max(0.05f, g_config.tickPopupTimeSeconds);

    const float distance = std::max(0.0f, g_config.tickPopupTravel);
    const float sideSpread = std::min(14.0f, distance * 0.22f);
    tick.startOffsetX = RandomFloat(-sideSpread, sideSpread);
    tick.startOffsetY = -RandomFloat(distance * 0.65f, distance * 1.15f);
    tick.endOffsetX = tick.startOffsetX * 0.25f;
    tick.endOffsetY = g_config.tickPopupHeightOffset;

    constexpr size_t maxVisibleTickPops = 8;
    if (number.tickPopups.size() >= maxVisibleTickPops)
        number.tickPopups.erase(number.tickPopups.begin());

    number.tickPopups.push_back(tick);
}

void RefreshCombinedNumber(DamageNumber& number, const DamageEvent& event)
{
    number.combinedAmount += event.amount;
    number.text = FormatDamageAmount(number.combinedAmount);
    number.combineIdleSeconds = 0.0f;
    number.hitPulseAge = 0.0f;
    if (event.element != Element::Physical)
        number.element = event.element;
    number.kind = event.kind;
    AddTickPopup(number, event);

    number.targetUnitType = event.targetUnitType;
    number.targetUnitId = event.targetUnitId;

    const float refreshLifetime = std::max(0.05f, g_config.extendDisplayOnHitSeconds);
    const float fadeStart = ClampFloat(g_config.fadeOutStart, 0.05f, 0.95f);
    const float visibleLifetime = (number.age + std::min(0.12f, refreshLifetime)) / fadeStart;
    number.lifetime = std::max(number.lifetime, std::max(number.age + refreshLifetime, visibleLifetime));

    number.vx *= 0.35f;
    number.animatedOffsetY = std::max(number.animatedOffsetY, -28.0f);
}

bool TryCombineRapidHit(const DamageEvent& event)
{
    if (!IsHitCombiningEligible(event))
        return false;

    const float windowSeconds = static_cast<float>(std::max(50, g_config.combineWindowMs)) / 1000.0f;
    for (auto it = g_numbers.rbegin(); it != g_numbers.rend(); ++it)
    {
        DamageNumber& number = *it;
        if (!number.canCombineHits)
            continue;
        if (number.age >= number.lifetime)
            continue;
        if (number.combineIdleSeconds > windowSeconds)
            continue;
        if (!IsSameTarget(number, event))
            continue;
        if (number.element != event.element)
            continue;

        RefreshCombinedNumber(number, event);
        return true;
    }

    return false;
}

void SpawnNumberFromEvent(const DamageEvent& event)
{
    const SpreadPlacement placement = CalculateSpreadPlacement(event);

    DamageNumber number{};
    number.text = FormatDamageAmount(event.amount);
    number.anchorX = event.screenX;
    number.anchorY = event.screenY;
    number.localOffsetX = placement.sideOffset;
    number.localOffsetY = g_config.spawnHeightOffset - placement.verticalOffset;
    number.hitPulseAge = std::max(0.01f, g_config.hitPulseTimeSeconds);
    number.x = number.anchorX + number.localOffsetX;
    number.y = number.anchorY + number.localOffsetY;
    number.drawX = number.x;
    number.drawY = number.y;
    number.vx = RandomFloat(-g_config.sidewaysSpread, g_config.sidewaysSpread);
    number.vy = -g_config.upwardDriftSpeed * (IsProminent(event.kind) ? 1.05f : 1.0f);
    number.lifetime = std::max(0.10f, event.durationSeconds);
    number.kind = event.kind;
    number.element = event.element;
    number.isProminent = IsProminent(event.kind);
    number.canCombineHits = IsHitCombiningEligible(event);
    number.combinedAmount = event.amount;
    number.spreadColumn = placement.column;
    number.prominenceScale = GetProminenceScale(event.kind);
    number.targetUnitType = event.targetUnitType;
    number.targetUnitId = event.targetUnitId;

    g_numbers.push_back(number);
}

void TrimActiveNumbers()
{
    const size_t maxActive = static_cast<size_t>(std::max(1, g_config.maxNumbersOnScreen));
    if (g_numbers.size() <= maxActive)
        return;
    g_numbers.erase(g_numbers.begin(), g_numbers.begin() + (g_numbers.size() - maxActive));
}

void RecordDpsEvent(const DamageEvent& event)
{
    if (event.amount <= 0)
        return;

    g_dpsSamples.push_back({ event.amount, 0.0f });
    g_rollingDamage += event.amount;
}

void UpdateDps(float dt)
{
    const float window = ClampFloat(g_config.dpsSampleTimeSeconds, 0.25f, 60.0f);
    const float elapsed = std::max(0.0f, dt);

    for (auto& sample : g_dpsSamples)
        sample.ageSeconds += elapsed;

    while (!g_dpsSamples.empty() && g_dpsSamples.front().ageSeconds > window)
    {
        g_rollingDamage -= g_dpsSamples.front().amount;
        g_dpsSamples.pop_front();
    }

    if (g_rollingDamage < 0)
        g_rollingDamage = 0;
}

long long CurrentDps()
{
    const float window = ClampFloat(g_config.dpsSampleTimeSeconds, 0.25f, 60.0f);
    return static_cast<long long>(static_cast<double>(g_rollingDamage) / static_cast<double>(window) + 0.5);
}

float CalculateFade(float age, float lifetime)
{
    if (lifetime <= 0.0f)
        return 0.0f;

    const float t = ClampFloat(age / lifetime, 0.0f, 1.0f);
    if (t <= g_config.fadeOutStart)
        return 1.0f;

    return ClampFloat((1.0f - t) / std::max(0.05f, 1.0f - g_config.fadeOutStart), 0.0f, 1.0f);
}

float CalculatePopScale(float age)
{
    const float popInSeconds = std::max(0.01f, g_config.popInTimeSeconds);
    const float popSettleSeconds = std::max(0.01f, g_config.settleTimeSeconds);

    if (age < popInSeconds)
    {
        const float t = age / popInSeconds;
        return g_config.spawnSize + t * (g_config.popBounceSize - g_config.spawnSize);
    }

    if (age < popInSeconds + popSettleSeconds)
    {
        const float t = (age - popInSeconds) / popSettleSeconds;
        return g_config.popBounceSize + t * (1.0f - g_config.popBounceSize);
    }

    return 1.0f;
}

float CalculateHitPulseScale(const DamageNumber& number)
{
    if (!number.canCombineHits)
        return 1.0f;

    const float pulseSeconds = std::max(0.01f, g_config.hitPulseTimeSeconds);
    if (number.hitPulseAge >= pulseSeconds)
        return 1.0f;

    const float t = ClampFloat(number.hitPulseAge / pulseSeconds, 0.0f, 1.0f);
    const float eased = (1.0f - t) * (1.0f - t);
    return 1.0f + ((std::max(1.0f, g_config.hitPulseSize) - 1.0f) * eased);
}

ImFont* GetRenderFont()
{
    const int index = std::clamp(g_config.fontIndex, 0, kFontPresetCount - 1);
    if (ImFont* font = D3D12::GetFloatingDamageFont(index))
        return font;
    if (ImFont* exocet = D3D12::GetFloatingDamageFont(0))
        return exocet;

    ImGuiIO& io = ImGui::GetIO();
    if (io.Fonts->Fonts.Size > 3)
        return io.Fonts->Fonts[3];
    return ImGui::GetFont();
}

void DrawStyledText(
    ImDrawList* drawList,
    ImFont* font,
    float fontSize,
    ImVec2 pos,
    const char* text,
    ImVec4 color,
    float fade)
{
    if (!text || !text[0])
        return;

    const int outlineThickness = std::max(0, g_config.textOutlineWidth);
    const ImU32 mainColor = ToImColor(color, fade);
    const ImU32 outlineCol = ToImColor(g_config.outlineColor, fade);
    const ImU32 shadowCol = ToImColor(g_config.shadowColor, fade);

    const ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text);
    pos.x -= textSize.x * 0.5f;
    pos.y -= textSize.y * 0.5f;

    if (g_config.shadowLeftRightOffset != 0.0f || g_config.shadowUpDownOffset != 0.0f)
    {
        drawList->AddText(
            font,
            fontSize,
            ImVec2(pos.x + g_config.shadowLeftRightOffset, pos.y + g_config.shadowUpDownOffset),
            shadowCol,
            text
        );
    }

    if (outlineThickness > 0)
    {
        for (int y = -outlineThickness; y <= outlineThickness; ++y)
        {
            for (int x = -outlineThickness; x <= outlineThickness; ++x)
            {
                if (x == 0 && y == 0)
                    continue;
                if ((x * x) + (y * y) > outlineThickness * outlineThickness + outlineThickness)
                    continue;

                drawList->AddText(font, fontSize, ImVec2(pos.x + x, pos.y + y), outlineCol, text);
            }
        }
    }

    drawList->AddText(font, fontSize, pos, mainColor, text);
}

void RenderTickPopup(
    ImDrawList* drawList,
    ImFont* font,
    const DamageNumber& number,
    const TickPopup& tick,
    float baseFontSize)
{
    if (!g_config.showTickPopups)
        return;

    const float duration = std::max(0.05f, tick.duration);
    if (tick.age >= duration)
        return;

    const float t = ClampFloat(tick.age / duration, 0.0f, 1.0f);
    const float moveT = 1.0f - std::pow(1.0f - t, 3.0f);
    const float drawX = number.drawX + tick.startOffsetX + ((tick.endOffsetX - tick.startOffsetX) * moveT);
    const float drawY = number.drawY + tick.startOffsetY + ((tick.endOffsetY - tick.startOffsetY) * moveT);

    const float popT = t < 0.30f ? t / 0.30f : ClampFloat(1.0f - ((t - 0.30f) / 0.70f), 0.0f, 1.0f);
    const float tickScale = std::max(0.25f, g_config.tickPopupSize) * (1.0f + (0.20f * popT));
    const float fontSize = std::max(10.0f, baseFontSize * number.prominenceScale * tickScale);

    DrawStyledText(
        drawList,
        font,
        fontSize,
        ImVec2(drawX, drawY),
        tick.text.c_str(),
        ResolveNumberColor(number.kind, number.element),
        CalculateFade(tick.age, duration)
    );
}

void RenderDpsNumber(ImDrawList* drawList, ImFont* font, const ImVec2& displaySize)
{
    if (!g_config.showDpsCounter)
        return;

    const long long dps = CurrentDps();
    const std::string text = "DPS " + FormatDpsAmount(dps);
    const float fontSize = ClampFloat(g_config.textSize - 6.0f, 20.0f, 40.0f);

    const float xPercent = ClampFloat(g_config.horizontalPositionPercent, 0.0f, 100.0f);
    const float yPercent = ClampFloat(g_config.verticalPositionPercent, 0.0f, 100.0f);
    const float anchorX = displaySize.x * xPercent / 100.0f;
    const float anchorY = displaySize.y * yPercent / 100.0f;

    ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text.c_str());
    ImVec2 pos = ImVec2(anchorX, anchorY);

    if (xPercent <= 10.0f)
        pos.x += textSize.x * 0.5f + 10.0f;
    else if (xPercent >= 90.0f)
        pos.x -= textSize.x * 0.5f + 10.0f;

    if (yPercent <= 10.0f)
        pos.y += textSize.y * 0.5f + 6.0f;
    else if (yPercent >= 90.0f)
        pos.y -= textSize.y * 0.5f + 6.0f;

    DrawStyledText(drawList, font, fontSize, pos, text.c_str(), g_config.normalColor, 1.0f);
}

ImVec4 JsonToColor(const nlohmann::json& j, const ImVec4& fallback)
{
    if (j.is_array() && j.size() >= 3)
        return ImVec4(j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j.size() >= 4 ? j[3].get<float>() : 1.0f);
    return fallback;
}

void ColorToJson(nlohmann::ordered_json& j, const ImVec4& color)
{
    j = ordered_json::array({ color.x, color.y, color.z, color.w });
}

int GeneratePreviewAmount(Kind kind)
{
    int amount = RandomInt(4, 1500);
    return kind == Kind::Critical ? amount * 2 : amount;
}

Kind RandomPreviewKind()
{
    return RandomInt(1, 100) <= 70 ? Kind::Normal : Kind::Critical;
}

Element RandomPreviewElement()
{
    switch (RandomInt(0, 5))
    {
    case 0: return Element::Fire;
    case 1: return Element::Lightning;
    case 2: return Element::Cold;
    case 3: return Element::Poison;
    case 4: return Element::Magic;
    default: return Element::Physical;
    }
}

template<typename T>
T JsonValueOrLegacy(const nlohmann::json& j, const char* key, const char* legacyKey, T fallback)
{
    if (j.contains(key))
        return j.value(key, fallback);
    return j.value(legacyKey, fallback);
}

template<typename T>
T JsonValueFirstMatch(const nlohmann::json& j, std::initializer_list<const char*> keys, T fallback)
{
    for (const char* key : keys)
    {
        if (j.contains(key))
            return j.value(key, fallback);
    }
    return fallback;
}

} // namespace

Config& GetConfig()
{
    return g_config;
}

void ResetToDefaults()
{
    const bool wasEnabled = g_config.enabled;
    g_config = Config{};
    g_config.enabled = wasEnabled;
}

static void LoadSettingsFromJsonObject(const nlohmann::json& j)
{
    g_config.enabled = j.value("Enabled", g_config.enabled);
    g_config.textSize = JsonValueOrLegacy(j, "TextSize", "FontSize", g_config.textSize);
    g_config.criticalHitSize = JsonValueOrLegacy(j, "CriticalHitSize", "CritFontSize", g_config.criticalHitSize);
    g_config.textOutlineWidth = JsonValueOrLegacy(j, "TextOutlineWidth", "OutlineThickness", g_config.textOutlineWidth);
    g_config.shadowLeftRightOffset = JsonValueOrLegacy(j, "ShadowLeftRightOffset", "ShadowOffsetX", g_config.shadowLeftRightOffset);
    g_config.shadowUpDownOffset = JsonValueOrLegacy(j, "ShadowUpDownOffset", "ShadowOffsetY", g_config.shadowUpDownOffset);
    g_config.maxNumbersOnScreen = JsonValueOrLegacy(j, "MaxNumbersOnScreen", "MaxActiveNumbers", g_config.maxNumbersOnScreen);
    g_config.displayTimeSeconds = JsonValueOrLegacy(j, "DisplayTimeSeconds", "LifetimeSeconds", g_config.displayTimeSeconds);
    g_config.criticalDisplayTimeSeconds = JsonValueOrLegacy(j, "CriticalDisplayTimeSeconds", "CritLifetimeSeconds", g_config.criticalDisplayTimeSeconds);
    g_config.fadeOutStart = JsonValueOrLegacy(j, "FadeOutStart", "FadeStart", g_config.fadeOutStart);
    g_config.spawnSize = JsonValueOrLegacy(j, "SpawnSize", "PopStartScale", g_config.spawnSize);
    g_config.popBounceSize = JsonValueOrLegacy(j, "PopBounceSize", "PopOvershootScale", g_config.popBounceSize);
    g_config.popInTimeSeconds = JsonValueOrLegacy(j, "PopInTimeSeconds", "PopInSeconds", g_config.popInTimeSeconds);
    g_config.settleTimeSeconds = JsonValueOrLegacy(j, "SettleTimeSeconds", "PopSettleSeconds", g_config.settleTimeSeconds);
    g_config.upwardDriftSpeed = JsonValueFirstMatch(j, { "UpwardDriftSpeed", "RiseSpeed", "FloatSpeed" }, g_config.upwardDriftSpeed);
    g_config.sidewaysSpread = JsonValueOrLegacy(j, "SidewaysSpread", "HorizontalDrift", g_config.sidewaysSpread);
    g_config.spawnHeightOffset = JsonValueOrLegacy(j, "SpawnHeightOffset", "SpawnYOffset", g_config.spawnHeightOffset);
    g_config.enableHitCombining = JsonValueOrLegacy(j, "EnableHitCombining", "CoalesceSmallDamage", g_config.enableHitCombining);
    g_config.maxCombinedHitSize = JsonValueOrLegacy(j, "MaxCombinedHitSize", "CoalesceMaxDamage", g_config.maxCombinedHitSize);
    g_config.combineWindowMs = JsonValueOrLegacy(j, "CombineWindowMs", "CoalesceWindowMs", g_config.combineWindowMs);
    g_config.extendDisplayOnHitSeconds = JsonValueOrLegacy(j, "ExtendDisplayOnHitSeconds", "CoalesceRefreshLifetime", g_config.extendDisplayOnHitSeconds);
    g_config.hitPulseSize = JsonValueOrLegacy(j, "HitPulseSize", "CoalescePulseScale", g_config.hitPulseSize);
    g_config.hitPulseTimeSeconds = JsonValueOrLegacy(j, "HitPulseTimeSeconds", "CoalescePulseSeconds", g_config.hitPulseTimeSeconds);
    g_config.showTickPopups = JsonValueOrLegacy(j, "ShowTickPopups", "CoalesceTickPop", g_config.showTickPopups);
    g_config.tickPopupTimeSeconds = JsonValueOrLegacy(j, "TickPopupTimeSeconds", "CoalesceTickPopSeconds", g_config.tickPopupTimeSeconds);
    g_config.tickPopupSize = JsonValueOrLegacy(j, "TickPopupSize", "CoalesceTickPopScale", g_config.tickPopupSize);
    g_config.tickPopupTravel = JsonValueOrLegacy(j, "TickPopupTravel", "CoalesceTickPopDistance", g_config.tickPopupTravel);
    g_config.tickPopupHeightOffset = JsonValueOrLegacy(j, "TickPopupHeightOffset", "CoalesceTickPopMergeOffsetY", g_config.tickPopupHeightOffset);
    g_config.spreadNumbersHorizontally = JsonValueOrLegacy(j, "SpreadNumbersHorizontally", "UseStackLanes", g_config.spreadNumbersHorizontally);
    g_config.numberOfColumns = JsonValueFirstMatch(j, { "NumberOfColumns", "NumberOfLanes", "StackLanes" }, g_config.numberOfColumns);
    g_config.columnSpacing = JsonValueFirstMatch(j, { "ColumnSpacing", "LaneSpacing", "StackLaneWidth" }, g_config.columnSpacing);
    g_config.stackHeightStep = JsonValueOrLegacy(j, "StackHeightStep", "StackVerticalStep", g_config.stackHeightStep);
    g_config.columnReuseTimeSeconds = JsonValueFirstMatch(j, { "ColumnReuseTimeSeconds", "LaneReuseTimeSeconds", "StackReuseSeconds" }, g_config.columnReuseTimeSeconds);
    g_config.maxStackHeight = JsonValueOrLegacy(j, "MaxStackHeight", "StackMaxYOffset", g_config.maxStackHeight);
    g_config.showDpsCounter = JsonValueOrLegacy(j, "ShowDpsCounter", "ShowDpsNumber", g_config.showDpsCounter);
    g_config.horizontalPositionPercent = JsonValueOrLegacy(j, "HorizontalPositionPercent", "DpsNumberXPercent", g_config.horizontalPositionPercent);
    g_config.verticalPositionPercent = JsonValueOrLegacy(j, "VerticalPositionPercent", "DpsNumberYPercent", g_config.verticalPositionPercent);
    g_config.dpsSampleTimeSeconds = JsonValueOrLegacy(j, "DpsSampleTimeSeconds", "DpsRollingWindowSeconds", g_config.dpsSampleTimeSeconds);
    g_config.previewNumberCount = JsonValueOrLegacy(j, "PreviewNumberCount", "BurstCount", g_config.previewNumberCount);
    g_config.previewSpread = JsonValueOrLegacy(j, "PreviewSpread", "BurstRadius", g_config.previewSpread);

    if (j.contains("FontIndex"))
    {
        g_config.fontIndex = std::clamp(j.value("FontIndex", g_config.fontIndex), 0, kFontPresetCount - 1);
    }
    else
    {
        const int legacyPreset = j.value("FontPreset", g_config.fontIndex);
        if (j.contains("CustomFontFile"))
        {
            const std::string customFile = j.value("CustomFontFile", std::string{});
            g_config.fontIndex = customFile.empty() ? 0 : ParseLegacyCustomFontIndex(customFile);
        }
        else if (legacyPreset <= 5)
        {
            g_config.fontIndex = 0;
        }
        else
        {
            g_config.fontIndex = std::clamp(legacyPreset, 0, kFontPresetCount - 1);
        }
    }

    g_config.colorByDamageType = JsonValueOrLegacy(j, "ColorByDamageType", "ColorByElement", g_config.colorByDamageType);

    if (j.contains("NormalColor"))
        g_config.normalColor = JsonToColor(j["NormalColor"], g_config.normalColor);
    if (j.contains("CriticalColor"))
        g_config.criticalColor = JsonToColor(j["CriticalColor"], g_config.criticalColor);
    if (j.contains("PhysicalColor"))
        g_config.physicalColor = JsonToColor(j["PhysicalColor"], g_config.physicalColor);
    if (j.contains("FireColor"))
        g_config.fireColor = JsonToColor(j["FireColor"], g_config.fireColor);
    if (j.contains("LightningColor"))
        g_config.lightningColor = JsonToColor(j["LightningColor"], g_config.lightningColor);
    if (j.contains("ColdColor"))
        g_config.coldColor = JsonToColor(j["ColdColor"], g_config.coldColor);
    if (j.contains("PoisonColor"))
        g_config.poisonColor = JsonToColor(j["PoisonColor"], g_config.poisonColor);
    if (j.contains("MagicColor"))
        g_config.magicColor = JsonToColor(j["MagicColor"], g_config.magicColor);
    if (j.contains("OutlineColor"))
        g_config.outlineColor = JsonToColor(j["OutlineColor"], g_config.outlineColor);
    if (j.contains("ShadowColor"))
        g_config.shadowColor = JsonToColor(j["ShadowColor"], g_config.shadowColor);
}

void LoadFromJson(const nlohmann::json& root)
{
    if (root.contains("FloatingDamage") && root["FloatingDamage"].is_boolean())
        g_config.enabled = root["FloatingDamage"].get<bool>();

    if (root.contains("FloatingDamageSettings") && root["FloatingDamageSettings"].is_object())
    {
        LoadSettingsFromJsonObject(root["FloatingDamageSettings"]);
        return;
    }

    if (root.contains("DamageNumbers") && root["DamageNumbers"].is_object())
        LoadSettingsFromJsonObject(root["DamageNumbers"]);
}

void SaveToJson(nlohmann::ordered_json& root)
{
    root["FloatingDamage"] = g_config.enabled;

    ordered_json j;
    j["Enabled"] = g_config.enabled;
    j["TextSize"] = g_config.textSize;
    j["CriticalHitSize"] = g_config.criticalHitSize;
    j["TextOutlineWidth"] = g_config.textOutlineWidth;
    j["ShadowLeftRightOffset"] = g_config.shadowLeftRightOffset;
    j["ShadowUpDownOffset"] = g_config.shadowUpDownOffset;
    j["MaxNumbersOnScreen"] = g_config.maxNumbersOnScreen;
    j["DisplayTimeSeconds"] = g_config.displayTimeSeconds;
    j["CriticalDisplayTimeSeconds"] = g_config.criticalDisplayTimeSeconds;
    j["FadeOutStart"] = g_config.fadeOutStart;
    j["SpawnSize"] = g_config.spawnSize;
    j["PopBounceSize"] = g_config.popBounceSize;
    j["PopInTimeSeconds"] = g_config.popInTimeSeconds;
    j["SettleTimeSeconds"] = g_config.settleTimeSeconds;
    j["UpwardDriftSpeed"] = g_config.upwardDriftSpeed;
    j["SidewaysSpread"] = g_config.sidewaysSpread;
    j["SpawnHeightOffset"] = g_config.spawnHeightOffset;
    j["EnableHitCombining"] = g_config.enableHitCombining;
    j["MaxCombinedHitSize"] = g_config.maxCombinedHitSize;
    j["CombineWindowMs"] = g_config.combineWindowMs;
    j["ExtendDisplayOnHitSeconds"] = g_config.extendDisplayOnHitSeconds;
    j["HitPulseSize"] = g_config.hitPulseSize;
    j["HitPulseTimeSeconds"] = g_config.hitPulseTimeSeconds;
    j["ShowTickPopups"] = g_config.showTickPopups;
    j["TickPopupTimeSeconds"] = g_config.tickPopupTimeSeconds;
    j["TickPopupSize"] = g_config.tickPopupSize;
    j["TickPopupTravel"] = g_config.tickPopupTravel;
    j["TickPopupHeightOffset"] = g_config.tickPopupHeightOffset;
    j["SpreadNumbersHorizontally"] = g_config.spreadNumbersHorizontally;
    j["NumberOfColumns"] = g_config.numberOfColumns;
    j["ColumnSpacing"] = g_config.columnSpacing;
    j["StackHeightStep"] = g_config.stackHeightStep;
    j["ColumnReuseTimeSeconds"] = g_config.columnReuseTimeSeconds;
    j["MaxStackHeight"] = g_config.maxStackHeight;
    j["ShowDpsCounter"] = g_config.showDpsCounter;
    j["HorizontalPositionPercent"] = g_config.horizontalPositionPercent;
    j["VerticalPositionPercent"] = g_config.verticalPositionPercent;
    j["DpsSampleTimeSeconds"] = g_config.dpsSampleTimeSeconds;
    j["PreviewNumberCount"] = g_config.previewNumberCount;
    j["PreviewSpread"] = g_config.previewSpread;
    j["FontIndex"] = g_config.fontIndex;
    j["ColorByDamageType"] = g_config.colorByDamageType;

    ColorToJson(j["NormalColor"], g_config.normalColor);
    ColorToJson(j["CriticalColor"], g_config.criticalColor);
    ColorToJson(j["PhysicalColor"], g_config.physicalColor);
    ColorToJson(j["FireColor"], g_config.fireColor);
    ColorToJson(j["LightningColor"], g_config.lightningColor);
    ColorToJson(j["ColdColor"], g_config.coldColor);
    ColorToJson(j["PoisonColor"], g_config.poisonColor);
    ColorToJson(j["MagicColor"], g_config.magicColor);
    ColorToJson(j["OutlineColor"], g_config.outlineColor);
    ColorToJson(j["ShadowColor"], g_config.shadowColor);

    root["FloatingDamageSettings"] = j;
}

void QueueGameDamage(int amount, float screenX, float screenY, uint32_t unitType, uint32_t unitId, Kind kind, Element element)
{
    if (amount <= 0)
        return;

    DamageEvent event{};
    event.amount = amount;
    event.screenX = screenX;
    event.screenY = screenY;
    event.kind = kind;
    event.element = element;
    event.durationSeconds = IsProminent(kind) ? g_config.criticalDisplayTimeSeconds : g_config.displayTimeSeconds;
    event.targetUnitType = unitType;
    event.targetUnitId = unitId;
    {
        std::lock_guard<std::mutex> lock(g_queueMutex);
        g_pendingEvents.push_back(event);
    }
}

void QueueDamage(int amount, float screenX, float screenY, uint32_t unitType, uint32_t unitId, Kind kind, Element element)
{
    if (!g_config.enabled || amount <= 0)
        return;

    QueueGameDamage(amount, screenX, screenY, unitType, unitId, kind, element);
}

void QueuePreviewAt(float screenX, float screenY, Kind kind)
{
    DamageEvent event{};
    event.amount = GeneratePreviewAmount(kind);
    event.screenX = screenX;
    event.screenY = screenY;
    event.kind = kind;
    event.element = g_config.colorByDamageType ? RandomPreviewElement() : Element::Physical;
    event.durationSeconds = IsProminent(kind) ? g_config.criticalDisplayTimeSeconds : g_config.displayTimeSeconds;
    {
        std::lock_guard<std::mutex> lock(g_queueMutex);
        g_pendingEvents.push_back(event);
    }
}

void QueuePreviewBurstAt(float screenX, float screenY)
{
    for (int i = 0; i < g_config.previewNumberCount; ++i)
    {
        const float offsetX = RandomFloat(-g_config.previewSpread, g_config.previewSpread);
        const float offsetY = RandomFloat(-g_config.previewSpread, g_config.previewSpread);
        QueuePreviewAt(screenX + offsetX, screenY + offsetY, RandomPreviewKind());
    }
}

void SpawnPreviewBurstAtScreenCenter(const ImVec2& displaySize)
{
    QueuePreviewBurstAt(displaySize.x * 0.5f, displaySize.y * 0.5f);
}

size_t ActiveCount()
{
    return g_numbers.size();
}

size_t PendingCount()
{
    std::lock_guard<std::mutex> lock(g_queueMutex);
    return g_pendingEvents.size();
}

bool HasDisplayActivity()
{
    return ActiveCount() > 0 || PendingCount() > 0;
}

void Update(float dt)
{
    std::vector<DamageEvent> pending;
    {
        std::lock_guard<std::mutex> lock(g_queueMutex);
        if (g_pendingEvents.empty() && g_numbers.empty())
            return;
        pending.swap(g_pendingEvents);
    }

    if (!pending.empty())
    {
        for (const auto& event : pending)
        {
            RecordDpsEvent(event);
            if (!TryCombineRapidHit(event))
                SpawnNumberFromEvent(event);
        }
        TrimActiveNumbers();
    }

    UpdateDps(dt);

    for (auto& number : g_numbers)
    {
        number.age += dt;
        number.combineIdleSeconds += dt;
        number.hitPulseAge += dt;

        for (auto& tick : number.tickPopups)
            tick.age += dt;

        number.tickPopups.erase(
            std::remove_if(
                number.tickPopups.begin(),
                number.tickPopups.end(),
                [](const TickPopup& tick) {
                    return tick.age >= std::max(0.05f, tick.duration);
                }
            ),
            number.tickPopups.end()
        );

        number.animatedOffsetX += number.vx * dt;
        number.animatedOffsetY += number.vy * dt;

        number.x = number.anchorX + number.localOffsetX + number.animatedOffsetX;
        number.y = number.anchorY + number.localOffsetY + number.animatedOffsetY;

        if (number.age <= dt * 1.5f)
        {
            number.drawX = number.x;
            number.drawY = number.y;
        }
        else
        {
            const float drawAlpha = ClampFloat(1.0f - std::exp(-120.0f * std::max(0.0f, dt)), 0.0f, 1.0f);
            number.drawX += (number.x - number.drawX) * drawAlpha;
            number.drawY += (number.y - number.drawY) * drawAlpha;
        }
    }

    g_numbers.erase(
        std::remove_if(
            g_numbers.begin(),
            g_numbers.end(),
            [](const DamageNumber& number) {
                return number.age >= number.lifetime;
            }
        ),
        g_numbers.end()
    );

    TrimActiveNumbers();
}

void Render(ImDrawList* drawList, const ImVec2& displaySize)
{
    if (!drawList)
        return;

    ImFont* font = GetRenderFont();
    if (!font)
        return;

    const bool showNumbers = g_config.enabled || !g_numbers.empty();
    const bool showDps = g_config.enabled && g_config.showDpsCounter;
    if (!showNumbers && !showDps)
        return;

    if (showNumbers)
    {
    for (const auto& number : g_numbers)
    {
        const float baseFontSize = number.isProminent ? g_config.criticalHitSize : g_config.textSize;
        for (const auto& tick : number.tickPopups)
            RenderTickPopup(drawList, font, number, tick, baseFontSize);
    }

    for (const auto& number : g_numbers)
    {
        const float baseFontSize = number.isProminent ? g_config.criticalHitSize : g_config.textSize;
        const float popScale = std::max(
            CalculatePopScale(number.age),
            CalculateHitPulseScale(number)
        );
        const float fontSize = std::max(12.0f, baseFontSize * number.prominenceScale * popScale);
        const float fade = CalculateFade(number.age, number.lifetime);

        DrawStyledText(
            drawList,
            font,
            fontSize,
            ImVec2(number.drawX, number.drawY),
            number.text.c_str(),
            ResolveNumberColor(number.kind, number.element),
            fade
        );
        }
    }

    if (showDps)
    RenderDpsNumber(drawList, font, displaySize);
}

void DrawSettingsPanel(float menuScale)
{
    Config& cfg = g_config;
    const float fieldWidth = 160.0f * menuScale;

    auto showTooltipOnHover = [menuScale](const char* title, const char* text)
        {
            if (!ImGui::IsItemHovered() || !text || !text[0])
                return;

            ImVec2 mousePos = ImGui::GetIO().MousePos;
            ImGui::SetNextWindowPos(ImVec2(mousePos.x + 70.0f * menuScale, mousePos.y), ImGuiCond_Always);
            ImGui::BeginTooltip();
            const float tooltipWidth = 420.0f * menuScale;
            ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + tooltipWidth);
            if (title && title[0])
            {
                ImGui::TextColored(ImVec4(0.75294f, 0.50196f, 0.94902f, 1.0f), "%s", title);
                ImGui::Spacing();
            }
            ImGui::TextUnformatted(text);
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        };

    auto settingCheckbox = [&](const char* label, bool* value, const char* tooltip, const char* tooltipTitle = nullptr)
        {
            ImGui::Checkbox(label, value);
            showTooltipOnHover(tooltipTitle ? tooltipTitle : label, tooltip);
        };

    auto settingSliderFloat = [&](const char* label, float* value, float minValue, float maxValue, const char* format, const char* tooltip, const char* tooltipTitle = nullptr)
        {
            ImGui::SliderFloat(label, value, minValue, maxValue, format);
            showTooltipOnHover(tooltipTitle ? tooltipTitle : label, tooltip);
        };

    auto settingSliderInt = [&](const char* label, int* value, int minValue, int maxValue, const char* format, ImGuiSliderFlags flags, const char* tooltip, const char* tooltipTitle = nullptr)
        {
            ImGui::SliderInt(label, value, minValue, maxValue, format, flags);
            showTooltipOnHover(tooltipTitle ? tooltipTitle : label, tooltip);
        };

    auto settingSliderIntSimple = [&](const char* label, int* value, int minValue, int maxValue, const char* tooltip, const char* tooltipTitle = nullptr)
        {
            ImGui::SliderInt(label, value, minValue, maxValue);
            showTooltipOnHover(tooltipTitle ? tooltipTitle : label, tooltip);
        };

    auto settingColor = [&](const char* label, float color[4], const char* tooltip, const char* tooltipTitle = nullptr)
        {
            ImGui::ColorEdit4(label, color);
            showTooltipOnHover(tooltipTitle ? tooltipTitle : label, tooltip);
        };

    static std::unordered_map<std::string, bool> s_sectionExpanded;
    auto drawSectionHeader = [&](const char* label) -> bool
        {
            bool& expanded = s_sectionExpanded[label];
            ImGuiTreeNodeFlags flags = expanded ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None;
            const bool open = ImGui::CollapsingHeader(label, flags);
            if (ImGui::IsItemToggledOpen())
                expanded = open;
            return open;
        };

    if (ImGui::Button("Reset to Defaults", ImVec2(fieldWidth * 1.5f, 0.0f)))
        ResetToDefaults();
    showTooltipOnHover(
        "Reset to Defaults",
        "Restores every Floating Damage setting to its built-in default.\n\nDoes not change the main Floating Damage on/off toggle in D2RHUD Options.");
    ImGui::Separator();

    const bool appearanceOpen = drawSectionHeader("Appearance");
    showTooltipOnHover("Appearance", "Controls text size, colors, and outline styling for floating damage numbers.");
    if (appearanceOpen)
    {
        int fontIndex = std::clamp(cfg.fontIndex, 0, kFontPresetCount - 1);
        ImGui::Combo("Font", &fontIndex, kFontPresetLabels, kFontPresetCount);
        cfg.fontIndex = fontIndex;
        showTooltipOnHover(
            "Font",
            "Choose the typeface used for floating damage numbers.\n\nBundled fonts are embedded in D2RHUD. Defaults to Exocet.");

        settingCheckbox(
            "Color by Damage Type",
            &cfg.colorByDamageType,
            "When enabled, each damage type uses its own color (fire, cold, poison, etc.).\n\nWhen disabled, all normal hits share one color and critical hits use the critical color.",
            "Color by Damage Type");
        settingSliderFloat(
            "Text Size", &cfg.textSize, 12.0f, 72.0f, "%.0f",
            "Base text size for normal damage numbers.\n\nIncrease for high-resolution displays or reduce for a subtler look.",
            "Text Size");
        settingSliderFloat(
            "Critical Hit Size", &cfg.criticalHitSize, 12.0f, 96.0f, "%.0f",
            "Text size used for critical damage numbers.\n\nUsually set slightly larger than Text Size so crits stand out.",
            "Critical Hit Size");
        settingSliderIntSimple(
            "Text Outline Width", &cfg.textOutlineWidth, 0, 4,
            "Width of the outline drawn around each number.\n\nHelps numbers stay readable over bright or busy backgrounds. Set to 0 to disable.",
            "Text Outline Width");
        settingSliderFloat(
            "Shadow Left/Right", &cfg.shadowLeftRightOffset, -8.0f, 8.0f, "%.0f",
            "Horizontal offset of the drop shadow behind each number.\n\nPositive values shift the shadow to the right.",
            "Shadow Left/Right Offset");
        settingSliderFloat(
            "Shadow Up/Down", &cfg.shadowUpDownOffset, -8.0f, 8.0f, "%.0f",
            "Vertical offset of the drop shadow behind each number.\n\nPositive values shift the shadow downward.",
            "Shadow Up/Down Offset");
        if (!cfg.colorByDamageType)
        {
            settingColor(
                "Normal Hit Color", &cfg.normalColor.x,
                "Text color for regular damage hits when Color by Damage Type is disabled.",
                "Normal Hit Color");
            settingColor(
                "Critical Hit Color", &cfg.criticalColor.x,
                "Text color for critical damage hits when Color by Damage Type is disabled.",
                "Critical Hit Color");
        }
        else
        {
            const bool elementColorsOpen = drawSectionHeader("Element Colors");
            showTooltipOnHover(
                "Element Colors",
                "Per-element colors used when Color By Element is enabled.\n\nEach damage type is detected automatically from the hit.");
            if (elementColorsOpen)
            {
                settingColor("Physical", &cfg.physicalColor.x, "Color for physical damage numbers.");
                settingColor("Fire", &cfg.fireColor.x, "Color for fire damage numbers.");
                settingColor("Lightning", &cfg.lightningColor.x, "Color for lightning damage numbers.");
                settingColor("Cold", &cfg.coldColor.x, "Color for cold damage numbers.");
                settingColor("Poison", &cfg.poisonColor.x, "Color for poison damage numbers, including DoT ticks.");
                settingColor("Magic", &cfg.magicColor.x, "Color for magic damage numbers.");
            }
        }
        settingColor(
            "Outline Color", &cfg.outlineColor.x,
            "Color of the outline stroke drawn around each number.");
        settingColor(
            "Shadow Color", &cfg.shadowColor.x,
            "Color of the drop shadow drawn behind each number.");
    }

    const bool animationOpen = drawSectionHeader("Animation");
    showTooltipOnHover("Animation", "Controls how numbers spawn, pop in, float upward, and fade out over time.");
    if (animationOpen)
    {
        settingSliderFloat(
            "Display Time (sec)", &cfg.displayTimeSeconds, 0.2f, 3.0f, "%.2f",
            "How long normal damage numbers remain on screen before disappearing.\n\nHigher values keep numbers visible longer.",
            "Display Time");
        settingSliderFloat(
            "Critical Display Time (sec)", &cfg.criticalDisplayTimeSeconds, 0.2f, 3.0f, "%.2f",
            "How long critical damage numbers remain on screen.\n\nCan be longer than normal display time to emphasize big hits.",
            "Critical Display Time");
        settingSliderFloat(
            "Fade-Out Start", &cfg.fadeOutStart, 0.05f, 0.95f, "%.2f",
            "Point in the number's lifetime (0 to 1) where fade-out begins.\n\nLower values start fading sooner; values near 1 keep numbers fully visible until the end.",
            "Fade-Out Start");
        settingSliderFloat(
            "Spawn Size", &cfg.spawnSize, 0.0f, 1.0f, "%.2f",
            "Initial scale when a number spawns.\n\nValues below 1.0 make numbers grow in from smaller than final size.",
            "Spawn Size");
        settingSliderFloat(
            "Pop Bounce Size", &cfg.popBounceSize, 1.0f, 3.0f, "%.2f",
            "Peak scale during the pop-in animation before settling to normal size.\n\nValues above 1.0 create a brief bounce effect.",
            "Pop Bounce Size");
        settingSliderFloat(
            "Pop-In Time (sec)", &cfg.popInTimeSeconds, 0.01f, 0.5f, "%.2f",
            "Duration of the initial grow phase from start scale to overshoot.",
            "Pop-In Time");
        settingSliderFloat(
            "Settle Time (sec)", &cfg.settleTimeSeconds, 0.01f, 0.5f, "%.2f",
            "Duration to settle from the overshoot peak back to normal size.",
            "Settle Time");
        settingSliderFloat(
            "Upward Drift Speed", &cfg.upwardDriftSpeed, 0.0f, 200.0f, "%.0f",
            "Upward drift speed applied to numbers after they spawn.\n\nHigher values make numbers rise faster.",
            "Upward Drift Speed");
        settingSliderFloat(
            "Sideways Spread", &cfg.sidewaysSpread, 0.0f, 80.0f, "%.0f",
            "Maximum random left/right drift applied to each number.\n\nAdds spread so overlapping hits are easier to read.",
            "Sideways Spread");
        settingSliderFloat(
            "Spawn Height Offset", &cfg.spawnHeightOffset, -120.0f, 120.0f, "%.0f",
            "Vertical offset from the target where numbers first appear.\n\nNegative values spawn higher above the target.",
            "Spawn Height Offset");
        settingSliderIntSimple(
            "Max Numbers On Screen", &cfg.maxNumbersOnScreen, 20, 400,
            "Maximum number of floating values allowed on screen at once.\n\nOldest numbers are removed when this limit is exceeded.",
            "Max Numbers On Screen");
    }

    const bool combineRapidHitsOpen = drawSectionHeader("Combine Rapid Hits");
    showTooltipOnHover("Combine Rapid Hits", "Combines rapid hits on the same target into a single updating number.\n\nUseful for poison DoT, fast attacks, and other tick-based damage.");
    if (combineRapidHitsOpen)
    {
        settingCheckbox(
            "Enable Hit Combining",
            &cfg.enableHitCombining,
            "Merges eligible rapid hits on the same target into one number that updates in place.\n\nDisable to show every individual hit separately.",
            "Enable Hit Combining");
        settingSliderInt(
            "Max Combined Hit Value", &cfg.maxCombinedHitSize, 1, 999999, "%d", ImGuiSliderFlags_Logarithmic,
            "Only damage at or below this amount can be combined.\n\nVery large hits can be excluded so they always display individually.",
            "Max Combined Hit Value");
        settingSliderIntSimple(
            "Combine Window (ms)", &cfg.combineWindowMs, 50, 2000,
            "Time window for combining hits on the same target.\n\nHits outside this window spawn a new number instead of updating the existing one.",
            "Combine Window");
        settingSliderFloat(
            "Extend Display on Hit (sec)", &cfg.extendDisplayOnHitSeconds, 0.05f, 2.0f, "%.2f",
            "How much visible time is added when a combined number absorbs another hit.\n\nKeeps active numbers on screen while damage continues.",
            "Extend Display on Hit");
        settingSliderFloat(
            "Hit Pulse Size", &cfg.hitPulseSize, 1.0f, 2.0f, "%.2f",
            "Brief scale increase when a combined number absorbs another hit.\n\nProvides visual feedback that damage is still ticking.",
            "Hit Pulse Size");
        settingSliderFloat(
            "Hit Pulse Time (sec)", &cfg.hitPulseTimeSeconds, 0.01f, 0.5f, "%.2f",
            "How long the hit pulse animation lasts.",
            "Hit Pulse Time");
        settingCheckbox(
            "Show Tick Popups",
            &cfg.showTickPopups,
            "Shows small pop-ups when individual ticks merge into a combined total.\n\nHelps visualize rapid DoT ticks without cluttering the screen.",
            "Show Tick Popups");
        settingSliderFloat(
            "Tick Popup Time (sec)", &cfg.tickPopupTimeSeconds, 0.05f, 2.0f, "%.2f",
            "How long each tick popup stays visible.",
            "Tick Popup Time");
        settingSliderFloat(
            "Tick Popup Size", &cfg.tickPopupSize, 0.2f, 1.5f, "%.2f",
            "Relative size of the tick popup text compared to the main number.",
            "Tick Popup Size");
        settingSliderFloat(
            "Tick Popup Travel", &cfg.tickPopupTravel, 0.0f, 160.0f, "%.0f",
            "How far tick popup text travels during its animation.",
            "Tick Popup Travel");
        settingSliderFloat(
            "Tick Popup Height Offset", &cfg.tickPopupHeightOffset, -80.0f, 40.0f, "%.0f",
            "Vertical offset applied to tick popup text relative to the combined number.",
            "Tick Popup Height Offset");
    }

    const bool spreadOverlappingNumbersOpen = drawSectionHeader("Spread Overlapping Numbers");
    showTooltipOnHover("Spread Overlapping Numbers", "Spreads simultaneous numbers on the same target across horizontal columns.\n\nReduces overlap when many hits land at once.");
    if (spreadOverlappingNumbersOpen)
    {
        settingCheckbox(
            "Spread Numbers Horizontally",
            &cfg.spreadNumbersHorizontally,
            "Enables horizontal column placement for numbers hitting the same target.\n\nDisable to keep all numbers centered on the target.",
            "Spread Numbers Horizontally");
        settingSliderIntSimple(
            "Number of Columns", &cfg.numberOfColumns, 1, 9,
            "Number of horizontal columns available for spreading.\n\nMore columns allow more simultaneous numbers before overlap.",
            "Number of Columns");
        settingSliderFloat(
            "Column Spacing", &cfg.columnSpacing, 10.0f, 120.0f, "%.0f",
            "Horizontal spacing between columns.",
            "Column Spacing");
        settingSliderFloat(
            "Stack Height Step", &cfg.stackHeightStep, 0.0f, 80.0f, "%.0f",
            "Additional vertical offset applied as numbers stack in the same column.",
            "Stack Height Step");
        settingSliderFloat(
            "Column Reuse Time (sec)", &cfg.columnReuseTimeSeconds, 0.05f, 2.0f, "%.2f",
            "How long a column remembers recent use before it can be reused for the same target.",
            "Column Reuse Time");
        settingSliderFloat(
            "Max Stack Height", &cfg.maxStackHeight, 0.0f, 200.0f, "%.0f",
            "Maximum upward offset applied by column placement.",
            "Max Stack Height");
    }

    const bool dpsMeterOpen = drawSectionHeader("DPS Meter");
    showTooltipOnHover("DPS Meter", "Shows a rolling damage-per-second readout on screen.\n\nUseful for comparing builds or checking sustained output.");
    if (dpsMeterOpen)
    {
        settingCheckbox(
            "Show DPS Counter",
            &cfg.showDpsCounter,
            "Displays a rolling DPS counter on screen.\n\nCalculated from damage dealt within the sample window.",
            "Show DPS Counter");
        settingSliderFloat(
            "Horizontal Position (%)", &cfg.horizontalPositionPercent, 0.0f, 100.0f, "%.1f",
            "Horizontal screen position of the DPS readout, as a percentage of screen width.\n\n0 = left edge, 100 = right edge.",
            "Horizontal Position");
        settingSliderFloat(
            "Vertical Position (%)", &cfg.verticalPositionPercent, 0.0f, 100.0f, "%.1f",
            "Vertical screen position of the DPS readout, as a percentage of screen height.\n\n0 = top edge, 100 = bottom edge.",
            "Vertical Position");
        settingSliderFloat(
            "DPS Sample Time (sec)", &cfg.dpsSampleTimeSeconds, 0.5f, 30.0f, "%.1f",
            "Time period used to calculate DPS.\n\nShorter windows react faster; longer windows show smoother sustained output.",
            "DPS Sample Time");
    }

    const bool previewOpen = drawSectionHeader("Preview");
    showTooltipOnHover("Preview", "Preview floating damage settings using synthetic numbers.\n\nUseful for tuning appearance and animation without combat.");
    if (previewOpen)
    {
        settingSliderIntSimple(
            "Preview Number Count", &cfg.previewNumberCount, 1, 32,
            "Number of test numbers spawned by the preview button.",
            "Preview Number Count");
        settingSliderFloat(
            "Preview Spread", &cfg.previewSpread, 0.0f, 160.0f, "%.0f",
            "Random spread radius for preview spawns.",
            "Preview Spread");
        if (ImGui::Button("Preview at Screen Center", ImVec2(fieldWidth * 1.5f, 0.0f)))
        {
            const ImGuiIO& io = ImGui::GetIO();
            SpawnPreviewBurstAtScreenCenter(io.DisplaySize);
            Update(io.DeltaTime);
            Render(ImGui::GetBackgroundDrawList(), io.DisplaySize);
        }
        showTooltipOnHover("Preview at Screen Center", "Spawns a burst of synthetic damage numbers at the center of the screen.");
    }
}

} // namespace FloatingDamage
