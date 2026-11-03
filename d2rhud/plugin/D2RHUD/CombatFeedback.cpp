#include "../../RuntimeTelemetry.h"
#include "../../SessionEpoch.h"
#include "CombatFeedback.h"
#include "HookTransaction.h"
#include "../../d2/D2Ptrs.h"
#include "../../Pattern.h"
#include "../FloatingDamage.h"
#include "../ItemFilter/ItemFilter.h"
#include <detours/detours.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <limits>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace HudCombat {
namespace {
std::recursive_mutex trackingMutex;
thread_local uint64_t feedbackEpoch=UINT64_MAX;
struct FeedbackEpoch {uint64_t previous=feedbackEpoch;FeedbackEpoch(){feedbackEpoch=HudSession::Current();}~FeedbackEpoch(){feedbackEpoch=previous;}};

void (*ensureResistance)()=nullptr;
void (*ensureChat)()=nullptr;
constexpr uint32_t unitDataOffset=0x1d442e0;
using GetUnitNameFptr=const char*(__fastcall*)(uint64_t,char*);
const auto GetUnitName=reinterpret_cast<GetUnitNameFptr>(Pattern::Address(0x27a920));
using BroadcastChatMessageFptr=void(__fastcall*)(uint64_t,const char*,uint8_t);
BroadcastChatMessageFptr oBroadcastChatMessage=reinterpret_cast<BroadcastChatMessageFptr>(Pattern::Address(0x2ada90));
static bool IsFloatingDamageActive();
static void TryQueuePoisonCombatMessage(const char*,D2UnitStrc* =nullptr);
static void QueuePoisonDamageEvent(int,float,float,uint32_t,uint32_t,FloatingDamage::Element);
static void SyncPoisonWatchHp(uint32_t,uint32_t);
static void EnsureBroadcastChatHook();
static const char* SkipD2ColorCodesForChat(const char* p){
    while(p && (unsigned char)p[0]==0xff && p[1]=='c' && p[2])p+=3;
    return p;
}
#pragma region Floating Damage

static ImVec2 g_LastDisplaySize = ImVec2(1920.0f, 1080.0f);
static bool g_DamageInfoHookInstalled = false;

typedef void(__fastcall* DamageInfo_t)(void* param1, D2UnitStrc* attacker, D2UnitStrc* target, int damage, int param5, void* param6, int param7, void* param8, char param9, void* param10);
DamageInfo_t oDamageInfo = nullptr;

static bool IsFloatingDamageActive()
{
    return FloatingDamage::IsEnabled();
}

void __fastcall Hooked_DamageInfo(void* param1, D2UnitStrc* attacker, D2UnitStrc* target, int baseDamage, int param5, void* param6, int finalDamage, void* param8, char param9, void* param10);

static bool LooksLikeGamePointer(const void* ptr)
{
    const uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    return addr >= 0x10000;
}

static bool IsCommittedReadableMemory(const void* address, size_t size)
{
    if (!LooksLikeGamePointer(address) || size == 0)
        return false;

    uintptr_t cursor = reinterpret_cast<uintptr_t>(address);
    if(size>UINTPTR_MAX-cursor)return false;
    const uintptr_t end = cursor + size;

    while (cursor < end)
    {
        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery(reinterpret_cast<void*>(cursor), &mbi, sizeof(mbi)) == 0)
            return false;
        if (mbi.State != MEM_COMMIT)
            return false;
        if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))
            return false;

        const auto regionBase=reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        if(mbi.RegionSize>UINTPTR_MAX-regionBase)return false;
        const uintptr_t regionEnd = regionBase + mbi.RegionSize;
        if (regionEnd <= cursor)
            return false;

        cursor = regionEnd;
    }

    return true;
}

static bool SafeReadShortString(const char* src, char* dst, size_t dstSize)
{
    if (!src || !dst || dstSize == 0 || !LooksLikeGamePointer(src))
        return false;

    __try
    {
        for (size_t i = 0; i + 1 < dstSize; ++i)
        {
            const char c = src[i];
            dst[i] = c;
            if (c == '\0')
                return true;
        }
        dst[dstSize - 1] = '\0';
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        if (dstSize > 0)
            dst[0] = '\0';
        return false;
    }
}

static bool NameEqualsI(const char* value, const char* token)
{
    return value && token && _stricmp(value, token) == 0;
}

static bool NameContainsI(const char* value, const char* token)
{
    if (!value || !token || !token[0])
        return false;

    const size_t tokenLen = strlen(token);
    if (tokenLen == 0)
        return false;

    for (const char* cursor = value; *cursor; ++cursor)
    {
        if (_strnicmp(cursor, token, tokenLen) == 0)
            return true;
    }
    return false;
}

static FloatingDamage::Element ElementFromDamageTypeName(const char* name)
{
    if (!name || !name[0])
        return FloatingDamage::Element::Physical;

    if (NameEqualsI(name, "Fire") || NameEqualsI(name, "Burn"))
        return FloatingDamage::Element::Fire;
    if (NameEqualsI(name, "Lightning") || NameEqualsI(name, "Ltng") || NameEqualsI(name, "Light") || NameEqualsI(name, "ligt"))
        return FloatingDamage::Element::Lightning;
    if (NameEqualsI(name, "Cold"))
        return FloatingDamage::Element::Cold;
    if (NameEqualsI(name, "Poison") || NameEqualsI(name, "Pois") || NameEqualsI(name, "pois"))
        return FloatingDamage::Element::Poison;
    if (NameEqualsI(name, "Magic") || NameEqualsI(name, "Mag") || NameEqualsI(name, "magc"))
        return FloatingDamage::Element::Magic;
    if (NameEqualsI(name, "Physical") || NameEqualsI(name, "Phys") || NameEqualsI(name, "Attack"))
        return FloatingDamage::Element::Physical;

    if (NameContainsI(name, "POISON:") || NameContainsI(name, " poison damage"))
        return FloatingDamage::Element::Poison;

    if (NameContainsI(name, "ligt") || NameContainsI(name, "Lightning") || NameContainsI(name, "Ltng"))
        return FloatingDamage::Element::Lightning;
    if (NameContainsI(name, "Fire"))
        return FloatingDamage::Element::Fire;
    if (NameContainsI(name, "Cold"))
        return FloatingDamage::Element::Cold;
    if (NameContainsI(name, "pois") || NameContainsI(name, "Poison"))
        return FloatingDamage::Element::Poison;
    if (NameContainsI(name, "magc") || NameContainsI(name, "Magic"))
        return FloatingDamage::Element::Magic;

    return FloatingDamage::Element::Physical;
}

static FloatingDamage::Element ElementFromReductionType(int32_t type)
{
    switch (type)
    {
    case 1: return FloatingDamage::Element::Fire;
    case 2: return FloatingDamage::Element::Lightning;
    case 3: return FloatingDamage::Element::Cold;
    case 4: return FloatingDamage::Element::Poison;
    case 5: return FloatingDamage::Element::Magic;
    case 0:
    default: return FloatingDamage::Element::Physical;
    }
}

static FloatingDamage::Element ElementFromTypeIndex(int index)
{
    switch (index)
    {
    case 1: return FloatingDamage::Element::Fire;
    case 2: return FloatingDamage::Element::Lightning;
    case 3: return FloatingDamage::Element::Cold;
    case 4: return FloatingDamage::Element::Poison;
    case 5: return FloatingDamage::Element::Magic;
    case 0:
    default: return FloatingDamage::Element::Physical;
    }
}

static bool SafeReadShortWideStringToUtf8(const wchar_t* src, char* dst, size_t dstSize)
{
    if (!src || !dst || dstSize == 0 || !LooksLikeGamePointer(src))
        return false;

    __try
    {
        wchar_t buffer[128]{};
        for (size_t i = 0; i + 1 < 128; ++i)
        {
            buffer[i] = src[i];
            if (src[i] == L'\0')
                break;
        }

        const int written = WideCharToMultiByte(CP_UTF8, 0, buffer, -1, dst, static_cast<int>(dstSize), nullptr, nullptr);
        return written > 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        if (dstSize > 0)
            dst[0] = '\0';
        return false;
    }
}

static bool SafeReadBlzString(const blz_string* src, char* dst, size_t dstSize)
{
    if (!src || !dst || dstSize == 0 || !LooksLikeGamePointer(src))
        return false;
    if (!IsCommittedReadableMemory(src, sizeof(blz_string)))
        return false;

    __try
    {
        const char* text = src->str;
        if (!text || !LooksLikeGamePointer(text))
        {
            if (src->length > 0 && src->length < sizeof(src->data))
                text = src->data;
            else
                return false;
        }

        return SafeReadShortString(text, dst, dstSize);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        if (dstSize > 0)
            dst[0] = '\0';
        return false;
    }
}

static bool TextLooksLikeDamageType(const char* text)
{
    if (!text || !text[0])
        return false;

    return NameEqualsI(text, "Physical") ||
        NameEqualsI(text, "Phys") ||
        NameEqualsI(text, "Attack") ||
        NameEqualsI(text, "ligt") ||
        NameEqualsI(text, "magc") ||
        NameEqualsI(text, "pois") ||
        NameContainsI(text, "POISON:") ||
        NameContainsI(text, " poison damage") ||
        NameContainsI(text, "Fire") ||
        NameContainsI(text, "Cold") ||
        NameContainsI(text, "Lightning") ||
        NameContainsI(text, "Light") ||
        NameContainsI(text, "Ltng") ||
        NameContainsI(text, "ligt") ||
        NameContainsI(text, "Poison") ||
        NameContainsI(text, "pois") ||
        NameContainsI(text, "Magic") ||
        NameContainsI(text, "magc") ||
        NameContainsI(text, "Burn");
}

static FloatingDamage::Element TryElementFromReadableText(const char* text)
{
    if (!TextLooksLikeDamageType(text))
        return FloatingDamage::Element::Physical;

    return ElementFromDamageTypeName(text);
}

static FloatingDamage::Element TryElementFromStringCandidate(const void* candidate)
{
    if (!candidate || !LooksLikeGamePointer(candidate))
        return FloatingDamage::Element::Physical;

    char text[256]{};

    if (SafeReadShortString(static_cast<const char*>(candidate), text, sizeof(text)))
    {
        const FloatingDamage::Element element = TryElementFromReadableText(text);
        if (element != FloatingDamage::Element::Physical)
            return element;
    }

    if (SafeReadShortWideStringToUtf8(static_cast<const wchar_t*>(candidate), text, sizeof(text)))
    {
        const FloatingDamage::Element element = TryElementFromReadableText(text);
        if (element != FloatingDamage::Element::Physical)
            return element;
    }

    if (SafeReadBlzString(static_cast<const blz_string*>(candidate), text, sizeof(text)))
    {
        const FloatingDamage::Element element = TryElementFromReadableText(text);
        if (element != FloatingDamage::Element::Physical)
            return element;
    }

    if (IsCommittedReadableMemory(candidate, sizeof(void*)))
    {
        __try
        {
            const char* indirect = *static_cast<const char* const*>(candidate);
            if (indirect && SafeReadShortString(indirect, text, sizeof(text)))
            {
                const FloatingDamage::Element element = TryElementFromReadableText(text);
                if (element != FloatingDamage::Element::Physical)
                    return element;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    return FloatingDamage::Element::Physical;
}

static FloatingDamage::Element TryElementFromStatTable(const D2DamageStatTableStrc* pTable)
{
    if (!pTable)
        return FloatingDamage::Element::Physical;

    char typeName[32]{};
    if (pTable->szName && SafeReadShortString(pTable->szName, typeName, sizeof(typeName)))
    {
        const FloatingDamage::Element fromName = ElementFromDamageTypeName(typeName);
        if (fromName != FloatingDamage::Element::Physical ||
            NameEqualsI(typeName, "Physical") ||
            NameEqualsI(typeName, "Phys") ||
            NameEqualsI(typeName, "Attack"))
        {
            return fromName;
        }
    }

    return ElementFromReductionType(pTable->nDamageReductionType);
}

static const char* AdvancePastColorCodes(const char* text)
{
    while (text && *text)
    {
        if (static_cast<unsigned char>(text[0]) == 0xFF && text[1] == 'c' && text[2])
        {
            text += 3;
            continue;
        }
        break;
    }
    return text;
}

static const char* FindCaseInsensitive(const char* haystack, const char* needle)
{
    if (!haystack || !needle || !needle[0])
        return nullptr;

    const size_t needleLen = strlen(needle);
    for (const char* cursor = haystack; *cursor; ++cursor)
    {
        if (_strnicmp(cursor, needle, needleLen) == 0)
            return cursor;
    }
    return nullptr;
}

static D2GameStrc* GetActiveGame()
{
    D2Client* pGameClient = *reinterpret_cast<D2Client**>(Pattern::Address(0x1d637f0));
    if (!pGameClient)
        return nullptr;

    return reinterpret_cast<D2GameStrc*>(pGameClient->pGame);
}

static bool TryReadTextCandidate(const void* candidate, char* dst, size_t dstSize)
{
    if (!candidate || !dst || dstSize == 0 || !LooksLikeGamePointer(candidate))
        return false;

    if (SafeReadShortString(static_cast<const char*>(candidate), dst, dstSize) && dst[0])
        return true;

    if (SafeReadShortWideStringToUtf8(static_cast<const wchar_t*>(candidate), dst, dstSize) && dst[0])
        return true;

    if (SafeReadBlzString(static_cast<const blz_string*>(candidate), dst, dstSize) && dst[0])
        return true;

    return false;
}

static void CollectHookCombatText(void* param1, void* param6, void* param8, void* param10, char* dst, size_t dstSize)
{
    if (!dst || dstSize == 0)
        return;

    dst[0] = '\0';

    const void* candidates[] = { param8, param10, param6, param1 };
    for (const void* candidate : candidates)
    {
        char text[512]{};
        if (!TryReadTextCandidate(candidate, text, sizeof(text)))
            continue;

        if (!dst[0] || strlen(text) > strlen(dst))
            strncpy_s(dst, dstSize, text, _TRUNCATE);
    }
}

static int ParseDamageDigits(const char* first,const char* last){
    int value=0;
    for(;first<last;++first){
        const int digit=*first-'0';
        if(value>(INT_MAX-digit)/10)return INT_MAX;
        value=value*10+digit;
    }
    return value;
}
static int ParseCombatLogDamageAmount(const char* text)
{
    if (!text || !text[0])
        return 0;

    const char* dmgLabel = FindCaseInsensitive(text, " poison damage");
    if (dmgLabel && dmgLabel > text)
    {
        const char* cursor = dmgLabel;
        while (cursor > text && cursor[-1] == ' ')
            --cursor;

        const char* last=cursor;
        while (cursor > text && cursor[-1] >= '0' && cursor[-1] <= '9')
            --cursor;
        const int amount=ParseDamageDigits(cursor,last);

        if (amount > 0)
            return amount;
    }

    const char* cursor = text;
    while ((cursor = FindCaseInsensitive(cursor, "takes")) != nullptr)
    {
        cursor += 5;
        for (int attempt = 0; attempt < 8 && *cursor; ++attempt)
        {
            cursor = AdvancePastColorCodes(cursor);
            while (*cursor == ' ')
                ++cursor;

            if (*cursor >= '0' && *cursor <= '9')
            {
                const char* first=cursor;
                while (*cursor >= '0' && *cursor <= '9')
                    ++cursor;
                const int amount=ParseDamageDigits(first,cursor);

                if (amount > 0)
                    return amount;
            }

            if (static_cast<unsigned char>(*cursor) == 0xFF && cursor[1] == 'c' && cursor[2])
                cursor += 3;
            else if (*cursor)
                ++cursor;
        }
    }

    return 0;
}

static bool ExtractBracketedName(const char* text, char* out, size_t outSize)
{
    if (!text || !out || outSize == 0)
        return false;

    const char* open = strchr(text, '[');
    if (!open)
        return false;

    ++open;
    const char* close = strchr(open, ']');
    if (!close || close <= open)
        return false;

    const size_t length = static_cast<size_t>(close - open);
    if (length == 0 || length >= outSize)
        return false;

    memcpy(out, open, length);
    out[length] = '\0';
    return true;
}

static const char* StripLeadingColorCodes(const char* text, char* scratch, size_t scratchSize)
{
    if (!text)
        return "";

    strncpy_s(scratch, scratchSize, text, _TRUNCATE);
    char* cursor = scratch;
    while (*cursor)
    {
        char* next = cursor;
        if (static_cast<unsigned char>(*next) == 0xFF && next[1] == 'c' && next[2])
            next += 3;
        if (next == cursor)
            break;
        cursor = next;
    }

    return cursor;
}

static bool UnitNameMatches(const char* unitName, const char* bracketName)
{
    char strippedUnit[128]{};
    char strippedBracket[128]{};
    const char* lhs = StripLeadingColorCodes(unitName, strippedUnit, sizeof(strippedUnit));
    const char* rhs = StripLeadingColorCodes(bracketName, strippedBracket, sizeof(strippedBracket));

    if (!lhs[0] || !rhs[0])
        return false;

    return _stricmp(lhs, rhs) == 0 || NameContainsI(lhs, rhs) || NameContainsI(rhs, lhs);
}

static D2UnitStrc* FindClientMonsterByDisplayName(const char* displayName)
{
    if (!displayName || !displayName[0])
        return nullptr;

    auto* ppClientUnitList = reinterpret_cast<D2UnitStrc**>(Pattern::Address(unitDataOffset));
    if (!ppClientUnitList)
        return nullptr;

    D2UnitStrc** ppMonsterList = &ppClientUnitList[UNIT_MONSTER * 0x80];
    for (int bucket = 0; bucket < 0x80; ++bucket)
    {
        for (D2UnitStrc* pUnit = ppMonsterList[bucket]; pUnit; pUnit = pUnit->pListNext)
        {
            if (!pUnit->pDynamicPath)
                continue;

            char nameBuf[128]{};
            const char* unitName = GetUnitName(reinterpret_cast<uint64_t>(pUnit), nameBuf);
            if (!unitName || !unitName[0])
                unitName = nameBuf;

            if (UnitNameMatches(unitName, displayName))
                return pUnit;
        }
    }

    return nullptr;
}

static bool ComputeScreenPositionForTarget(D2UnitStrc* attacker, D2UnitStrc* target, float& screenX, float& screenY)
{
    if (!target || !target->pDynamicPath)
        return false;

    if (!attacker || !attacker->pDynamicPath)
        attacker = target;

    D2DynamicPathStrc* a = attacker->pDynamicPath;
    D2DynamicPathStrc* t = target->pDynamicPath;

    constexpr float INV_SUBTILE = 1.0f / 65536.0f;

    const float ax = a->wPosX + a->wOffsetX * INV_SUBTILE;
    const float ay = a->wPosY + a->wOffsetY * INV_SUBTILE;
    const float tx = t->wPosX + t->wOffsetX * INV_SUBTILE;
    const float ty = t->wPosY + t->wOffsetY * INV_SUBTILE;
    const float dx = tx - ax;
    const float dy = ty - ay;

    const float isoX = dx - dy;
    const float isoY = dx + dy;
    const float pixelOffsetX = 40.0f + isoX * 30.0f;
    const float pixelOffsetY = 120.0f + isoY * -12.5f;

    screenX = g_LastDisplaySize.x * 0.5f + pixelOffsetX;
    screenY = g_LastDisplaySize.y * 0.5f - pixelOffsetY;
    return true;
}

static std::unordered_map<uint64_t, std::pair<FloatingDamage::Element, std::chrono::steady_clock::time_point>> g_targetElementCache;

static D2UnitStrc* GetClientUnitByIdAndType(uint32_t unitId, uint32_t unitType)
{
    // An unset poison target uses UINT32_MAX. Validate before computing a table
    // address; that sentinel previously indexed hundreds of GB beyond the table.
    if(unitType>UNIT_ITEM || unitId==UINT32_MAX)return nullptr;
    const uint32_t table=unitType==UNIT_MISSILE?0x1d466e0:unitDataOffset+unitType*0x400;
    auto* ppClientUnitList = reinterpret_cast<D2UnitStrc**>(Pattern::Address(table));
    if (!ppClientUnitList)
        return nullptr;

    D2UnitStrc* pUnit = ppClientUnitList[unitId & 0x7F];
    for(unsigned count=0;pUnit && count<4096;++count,pUnit=pUnit->pListNext)
        if(pUnit->dwUnitId==unitId && pUnit->dwUnitType==unitType)return pUnit;
    return nullptr;
}

static bool ReadBlzStringMessage(const blz_string* msg, char* dst, size_t dstSize)
{
    if (!msg || !dst || dstSize == 0)
        return false;

    return SafeReadBlzString(msg, dst, dstSize);
}

// "ÿc4Playerÿc1: hello" or "Player: hello" -> Player
static bool TryParsePlayerChatSender(const char* text, std::string& outSender)
{
    if (!text || !text[0])
        return false;

    const char* p = text;
    while ((unsigned char)p[0] == 0xFF && p[1] == 'c' && p[2])
        p += 3;

    const char* colon = strchr(p, ':');
    if (!colon || colon == p || colon[1] != ' ')
        return false;

    const size_t nameLen = static_cast<size_t>(colon - p);
    if (nameLen == 0 || nameLen > 60)
        return false;

    for (size_t i = 0; i < nameLen; ++i)
    {
        const unsigned char c = static_cast<unsigned char>(p[i]);
        if (c == '\n' || c == '\r' || c == 0xFF)
            return false;
    }

    outSender.assign(p, nameLen);
    while (!outSender.empty() && (outSender.back() == ' ' || outSender.back() == '\t'))
        outSender.pop_back();

    return !outSender.empty();
}

static bool TryParsePlayerChatLine(const char* text, std::string& outSender, std::string& outBody)
{
    outSender.clear();
    outBody.clear();
    if (!TryParsePlayerChatSender(text, outSender))
        return false;

    const char* p = SkipD2ColorCodesForChat(text);
    const char* colon = strchr(p, ':');
    if (!colon || colon[1] != ' ')
        return false;

    outBody = SkipD2ColorCodesForChat(colon + 2);
    return !outBody.empty();
}

static D2UnitStrc* FindCachedPoisonTarget()
{
    const auto now = std::chrono::steady_clock::now();

    for (const auto& entry : g_targetElementCache)
    {
        if (entry.second.second <= now)
            continue;
        if (entry.second.first != FloatingDamage::Element::Poison)
            continue;

        const uint32_t unitType = static_cast<uint32_t>(entry.first >> 32);
        const uint32_t unitId = static_cast<uint32_t>(entry.first & 0xFFFFFFFFu);
        D2UnitStrc* pUnit = GetClientUnitByIdAndType(unitId, unitType);
        if (pUnit && pUnit->pDynamicPath)
            return pUnit;
    }

    return nullptr;
}

struct RecentDamageQueueEntry {
    uint64_t targetKey = 0;
    std::chrono::steady_clock::time_point queuedAt{};
};

static RecentDamageQueueEntry g_recentDamageQueue{};

struct PoisonWatchEntry {
    int32_t lastHpPoints = -1;
};

static std::unordered_map<uint64_t, PoisonWatchEntry> g_poisonWatch;
static std::unordered_map<uint64_t, std::chrono::steady_clock::time_point> g_lastPoisonHookQueue;
static std::unordered_map<uint64_t, std::chrono::steady_clock::time_point> g_lastNonPoisonDamageOnTarget;

static uint64_t MakeTargetElementKey(uint32_t unitType, uint32_t unitId);
static FloatingDamage::Element ResolveTargetElement(
    FloatingDamage::Element detected,
    uint32_t unitType,
    uint32_t unitId);

static void QueueTargetDamage(
    int amount,
    float screenX,
    float screenY,
    uint32_t unitType,
    uint32_t unitId,
    FloatingDamage::Kind kind,
    FloatingDamage::Element element)
{
    if (amount <= 0)
        return;

    if (element != FloatingDamage::Element::Poison)
    {
        const uint64_t targetKey = MakeTargetElementKey(unitType, unitId);
        const auto now = std::chrono::steady_clock::now();
        if (g_recentDamageQueue.targetKey == targetKey &&
            now - g_recentDamageQueue.queuedAt < std::chrono::milliseconds(20))
        {
            return;
        }

        g_recentDamageQueue = { targetKey, now };
        g_lastNonPoisonDamageOnTarget[targetKey] = now;
        SyncPoisonWatchHp(unitType, unitId);
    }

    FloatingDamage::QueueGameDamage(amount, screenX, screenY, unitType, unitId, kind, element,feedbackEpoch);
}

static std::pair<uint32_t, uint32_t> g_lastPoisonTarget{ UINT32_MAX, UINT32_MAX };

static void TryQueuePoisonCombatMessage(const char* text, D2UnitStrc* targetOverride)
{
    if (!text || !text[0])
        return;

    if (!FindCaseInsensitive(text, "POISON:") && !FindCaseInsensitive(text, " poison damage"))
        return;

    const int amount = ParseCombatLogDamageAmount(text);
    if (amount <= 0)
        return;

    D2UnitStrc* target = targetOverride;
    if (!target)
    {
        char bracketName[128]{};
        if (ExtractBracketedName(text, bracketName, sizeof(bracketName)))
            target = FindClientMonsterByDisplayName(bracketName);
    }

    if (!target)
        target = GetClientUnitByIdAndType(g_lastPoisonTarget.second, g_lastPoisonTarget.first);

    if (!target)
        target = FindCachedPoisonTarget();

    if (!target || !target->pDynamicPath)
        return;

    g_lastPoisonTarget = { target->dwUnitType, target->dwUnitId };

    float screenX = 0.0f;
    float screenY = 0.0f;
    if (!ComputeScreenPositionForTarget(target, target, screenX, screenY))
        return;

    const FloatingDamage::Element element = ResolveTargetElement(
        FloatingDamage::Element::Poison,
        target->dwUnitType,
        target->dwUnitId);

    QueuePoisonDamageEvent(
        amount,
        screenX,
        screenY,
        target->dwUnitType,
        target->dwUnitId,
        element);
}

static FloatingDamage::Element DetectDamageElement(const char* combatText,void* param1, int param5, void* param6, void* param8, void* param10)
{
    if (combatText[0])
    {
        const FloatingDamage::Element fromCombat = ElementFromDamageTypeName(combatText);
        if (fromCombat != FloatingDamage::Element::Physical)
            return fromCombat;
    }

    const FloatingDamage::Element fromParam8 = TryElementFromStringCandidate(param8);
    if (fromParam8 != FloatingDamage::Element::Physical)
        return fromParam8;

    const FloatingDamage::Element fromParam6Text = TryElementFromStringCandidate(param6);
    if (fromParam6Text != FloatingDamage::Element::Physical)
        return fromParam6Text;

    if (param6 && LooksLikeGamePointer(param6) &&
        IsCommittedReadableMemory(param6, sizeof(D2DamageStatTableStrc)))
    {
        __try
        {
            const auto* pTable = reinterpret_cast<const D2DamageStatTableStrc*>(param6);
            const FloatingDamage::Element fromTable = TryElementFromStatTable(pTable);
            if (fromTable != FloatingDamage::Element::Physical)
                return fromTable;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }
    else if (param6 && !LooksLikeGamePointer(param6))
    {
        const int typeIndex = static_cast<int>(reinterpret_cast<intptr_t>(param6));
        if (typeIndex >= 0 && typeIndex <= 16)
        {
            const FloatingDamage::Element fromIndex = ElementFromTypeIndex(typeIndex);
            if (fromIndex != FloatingDamage::Element::Physical)
                return fromIndex;
        }
    }

    if (param5 >= 0 && param5 <= 16)
    {
        const FloatingDamage::Element fromParam5 = ElementFromTypeIndex(param5);
        if (fromParam5 != FloatingDamage::Element::Physical)
            return fromParam5;
    }

    const FloatingDamage::Element fromParam10 = TryElementFromStringCandidate(param10);
    if (fromParam10 != FloatingDamage::Element::Physical)
        return fromParam10;

    const FloatingDamage::Element fromParam1 = TryElementFromStringCandidate(param1);
    if (fromParam1 != FloatingDamage::Element::Physical)
        return fromParam1;

    return FloatingDamage::Element::Physical;
}

static uint64_t MakeTargetElementKey(uint32_t unitType, uint32_t unitId)
{
    return (static_cast<uint64_t>(unitType) << 32) | static_cast<uint64_t>(unitId);
}

static FloatingDamage::Element ResolveTargetElement(
    FloatingDamage::Element detected,
    uint32_t unitType,
    uint32_t unitId)
{
    const uint64_t key = MakeTargetElementKey(unitType, unitId);
    const auto now = std::chrono::steady_clock::now();
    constexpr auto kDoTElementCacheDuration = std::chrono::seconds(60);

    if (detected != FloatingDamage::Element::Physical)
    {
        g_targetElementCache[key] = { detected, now + kDoTElementCacheDuration };
        return detected;
    }

    const auto it = g_targetElementCache.find(key);
    if (it != g_targetElementCache.end())
    {
        if (it->second.second > now)
            return it->second.first;

        g_targetElementCache.erase(it);
    }

    return FloatingDamage::Element::Physical;
}

static int32_t ReadUnitHpPoints(D2UnitStrc* pUnit)
{
    if (!pUnit)
        return -1;

    __try
    {
        return STATLIST_GetUnitStatSigned(pUnit, STAT_HITPOINTS, 0) >> 8;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return -1;
    }
}

static int32_t ReadUnitHpPointsById(uint32_t unitType, uint32_t unitId, D2UnitStrc* pClientUnit)
{
    if (D2GameStrc* pGame = GetActiveGame())
    {
        D2UnitStrc* pServerUnit = UNITS_GetServerUnitByTypeAndId(
            pGame,
            static_cast<D2C_UnitTypes>(unitType),
            unitId);
        if (pServerUnit)
        {
            const int32_t serverHp = ReadUnitHpPoints(pServerUnit);
            if (serverHp >= 0)
                return serverHp;
        }
    }

    return ReadUnitHpPoints(pClientUnit);
}

static void SyncPoisonWatchHp(uint32_t unitType, uint32_t unitId)
{
    D2UnitStrc* pUnit = GetClientUnitByIdAndType(unitId, unitType);
    if (!pUnit)
        return;

    const uint64_t key = MakeTargetElementKey(unitType, unitId);
    g_poisonWatch[key].lastHpPoints = ReadUnitHpPointsById(unitType, unitId, pUnit);
}

static bool IsTargetPoisonActive(uint32_t unitType, uint32_t unitId)
{
    const uint64_t key = MakeTargetElementKey(unitType, unitId);
    const auto now = std::chrono::steady_clock::now();

    const auto cacheIt = g_targetElementCache.find(key);
    if (cacheIt != g_targetElementCache.end() &&
        cacheIt->second.first == FloatingDamage::Element::Poison &&
        cacheIt->second.second > now)
    {
        return true;
    }

    D2UnitStrc* pUnit = GetClientUnitByIdAndType(unitId, unitType);
    if (!pUnit)
        return g_poisonWatch.find(key) != g_poisonWatch.end();

    __try
    {
        if (STATLIST_GetUnitStatSigned(pUnit, STAT_POISON_COUNT, 0) > 0)
            return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }

    return g_poisonWatch.find(key) != g_poisonWatch.end();
}

static void QueuePoisonDamageEvent(
    int amount,
    float screenX,
    float screenY,
    uint32_t unitType,
    uint32_t unitId,
    FloatingDamage::Element element)
{
    if (!IsFloatingDamageActive() || amount <= 0)
        return;

    const uint64_t key = MakeTargetElementKey(unitType, unitId);
    const auto now = std::chrono::steady_clock::now();
    const auto recentHookIt = g_lastPoisonHookQueue.find(key);
    if (recentHookIt != g_lastPoisonHookQueue.end() &&
        now - recentHookIt->second < std::chrono::milliseconds(8))
    {
        return;
    }

    g_lastPoisonHookQueue[key] = now;
    g_lastPoisonTarget = { unitType, unitId };
    ResolveTargetElement(FloatingDamage::Element::Poison, unitType, unitId);
    SyncPoisonWatchHp(unitType, unitId);

    FloatingDamage::QueueGameDamage(
        amount,
        screenX,
        screenY,
        unitType,
        unitId,
        FloatingDamage::Kind::Normal,
        element,feedbackEpoch);
}

static void UpdatePoisonDoTWatch()
{
    static bool wasActive = false;
    const bool active = IsFloatingDamageActive();
    const bool resume = active && !wasActive;
    wasActive = active;
    if (!active)
        return;

    const auto now = std::chrono::steady_clock::now();
    constexpr auto kHookGracePeriod = std::chrono::milliseconds(120);
    constexpr auto kStaleWatchTimeout = std::chrono::seconds(3);

    // Scratch storage belongs to the serialized tracker, so it can be reused
    // rather than allocating and freeing a vector for every rendered frame.
    static std::vector<uint64_t> keys;
    keys.clear();
    keys.reserve(g_poisonWatch.size() + g_targetElementCache.size());
    for (const auto& entry : g_poisonWatch)
        keys.push_back(entry.first);
    for (const auto& entry : g_targetElementCache)
    {
        if (entry.second.first == FloatingDamage::Element::Poison && entry.second.second > now)
            keys.push_back(entry.first);
    }

    std::sort(keys.begin(), keys.end());
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());

    for (const uint64_t key : keys)
    {
        const uint32_t unitType = static_cast<uint32_t>(key >> 32);
        const uint32_t unitId = static_cast<uint32_t>(key & 0xFFFFFFFFu);

        D2UnitStrc* pUnit = GetClientUnitByIdAndType(unitId, unitType);
        if (!pUnit || !pUnit->pDynamicPath)
        {
            g_poisonWatch.erase(key);
            g_targetElementCache.erase(key);
            g_lastPoisonHookQueue.erase(key);
            g_lastNonPoisonDamageOnTarget.erase(key);
            continue;
        }

        if (!IsTargetPoisonActive(unitType, unitId))
        {
            const auto watchIt = g_poisonWatch.find(key);
            if (watchIt != g_poisonWatch.end())
            {
                const auto lastHookIt = g_lastPoisonHookQueue.find(key);
                if (lastHookIt == g_lastPoisonHookQueue.end() ||
                    now - lastHookIt->second > kStaleWatchTimeout)
                {
                    g_poisonWatch.erase(watchIt);
                }
            }
            continue;
        }

        const auto hookIt = g_lastPoisonHookQueue.find(key);
        if (hookIt != g_lastPoisonHookQueue.end() && now - hookIt->second < kHookGracePeriod)
        {
            SyncPoisonWatchHp(unitType, unitId);
            continue;
        }

        const auto nonPoisonIt = g_lastNonPoisonDamageOnTarget.find(key);
        if (nonPoisonIt != g_lastNonPoisonDamageOnTarget.end() && now - nonPoisonIt->second < kHookGracePeriod)
        {
            SyncPoisonWatchHp(unitType, unitId);
            continue;
        }

        const int32_t hp = ReadUnitHpPointsById(unitType, unitId, pUnit);
        if (hp < 0)
            continue;

        PoisonWatchEntry& watch = g_poisonWatch[key];
        if (!resume && watch.lastHpPoints >= 0 && hp < watch.lastHpPoints)
        {
            const int delta = watch.lastHpPoints - hp;
            if (delta > 0)
            {
                float screenX = 0.0f;
                float screenY = 0.0f;
                if (ComputeScreenPositionForTarget(pUnit, pUnit, screenX, screenY))
                {
                    const FloatingDamage::Element element = ResolveTargetElement(
                        FloatingDamage::Element::Poison,
                        unitType,
                        unitId);

                    g_lastPoisonHookQueue[key] = now;
                    FloatingDamage::QueueGameDamage(
                        delta,
                        screenX,
                        screenY,
                        unitType,
                        unitId,
                        FloatingDamage::Kind::Normal,
                        element);
                }
            }
        }

        watch.lastHpPoints = hp;
    }
}

static bool IsPoisonStatTableRecord(const D2DamageStatTableStrc* pTable)
{
    if (!pTable)
        return false;

    if (TryElementFromStatTable(pTable) == FloatingDamage::Element::Poison)
        return true;

    return pTable->nDamageReductionType == 4;
}

static int ReadPoisonDamageAmount(D2DamageInfoStrc* pDamageInfo, D2DamageStatTableStrc* pTable)
{
    if (pTable && pTable->pOffsetInDamageStrc &&
        LooksLikeGamePointer(pTable->pOffsetInDamageStrc) &&
        IsCommittedReadableMemory(pTable->pOffsetInDamageStrc, sizeof(int32_t)))
    {
        const int amount = *pTable->pOffsetInDamageStrc;
        if (amount > 0)
            return amount;
    }

    if (pDamageInfo && pDamageInfo->pDamage &&
        LooksLikeGamePointer(pDamageInfo->pDamage) &&
        IsCommittedReadableMemory(pDamageInfo->pDamage, sizeof(D2DamageStrc)))
    {
        return pDamageInfo->pDamage->dwPoisDamage;
    }

    return 0;
}

static void TryQueuePoisonDamageFromStatTable(D2DamageInfoStrc* pDamageInfo, D2DamageStatTableStrc* pDamageStatTableRecord)
{
    if (!IsFloatingDamageActive())
        return;
    if (!pDamageInfo || !pDamageStatTableRecord || !pDamageInfo->pDefender)
        return;
    if (!pDamageInfo->pDefender->pDynamicPath)
        return;
    if (!IsCommittedReadableMemory(pDamageStatTableRecord, sizeof(D2DamageStatTableStrc)))
        return;

    __try
    {
        if (!IsPoisonStatTableRecord(pDamageStatTableRecord))
            return;

        const int amount = ReadPoisonDamageAmount(pDamageInfo, pDamageStatTableRecord);
        if (amount <= 0)
            return;

        D2UnitStrc* target = pDamageInfo->pDefender;
        D2UnitStrc* attacker = pDamageInfo->pAttacker;

        float screenX = 0.0f;
        float screenY = 0.0f;
        if (!ComputeScreenPositionForTarget(attacker, target, screenX, screenY))
            return;

        const FloatingDamage::Element resolved = ResolveTargetElement(
            FloatingDamage::Element::Poison,
            target->dwUnitType,
            target->dwUnitId);

        QueuePoisonDamageEvent(
            amount,
            screenX,
            screenY,
            target->dwUnitType,
            target->dwUnitId,
            resolved);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
}

static void EnsureDamageInfoHook();
static bool g_BroadcastChatHookInstalled = false;
void __fastcall Hooked_BroadcastChatMessage(uint64_t pGame, const char* szMsg, uint8_t color)
{
    // Disabled for chat sounds to avoid triggering on system/status broadcasts.

    if (szMsg && IsFloatingDamageActive())
    {
        char text[512]{};
        if (SafeReadShortString(szMsg, text, sizeof(text)))
            HudCombat::ObserveCombatMessage(text);
    }

    oBroadcastChatMessage(pGame, szMsg, color);
}

static void EnsureBroadcastChatHook()
{
    if(g_BroadcastChatHookInstalled || !oBroadcastChatMessage)return;
    HudHooks::Transaction transaction;
    if(transaction.Attach(oBroadcastChatMessage,Hooked_BroadcastChatMessage) && transaction.Commit())
        g_BroadcastChatHookInstalled=true;
}

static void EnsureDamageInfoHook()
{
    static uint64_t nextAttempt=0;
    if(!g_DamageInfoHookInstalled){
        const auto now=GetTickCount64();
        if(now<nextAttempt)return;
        nextAttempt=now+1000;
        oDamageInfo=reinterpret_cast<DamageInfo_t>(Pattern::Address(0x27ab90));
        HudHooks::Transaction transaction;
        if(!transaction.Attach(oDamageInfo,Hooked_DamageInfo) || !transaction.Commit())return;
        g_DamageInfoHookInstalled=true;
    }
    EnsureBroadcastChatHook();
    if(ensureChat)ensureChat();
    if(ensureResistance)ensureResistance();
}

void __fastcall Hooked_DamageInfo(void* param1, D2UnitStrc* attacker, D2UnitStrc* target, int baseDamage, int param5, void* param6, int finalDamage, void* param8, char param9, void* param10)
{
    HudTelemetry::Scope measured(HudTelemetry::Feature::Combat);
    FeedbackEpoch epoch;
    // Disabled feedback must not inspect opaque native arguments on every hit.
    if(!IsFloatingDamageActive()){
        oDamageInfo(param1,attacker,target,baseDamage,param5,param6,finalDamage,param8,param9,param10);
        return;
    }
    char combatText[512]{};
    CollectHookCombatText(param1, param6, param8, param10, combatText, sizeof(combatText));

    const int actualDamage = finalDamage >> 8;
    const int parsedDamage = ParseCombatLogDamageAmount(combatText);
    const bool isPoisonCombat = FindCaseInsensitive(combatText, "POISON:") != nullptr ||
        FindCaseInsensitive(combatText, " poison damage") != nullptr;
    int displayDamage = actualDamage > 0 ? actualDamage : parsedDamage;
    if (isPoisonCombat && parsedDamage > 0)
        displayDamage = parsedDamage;
    const FloatingDamage::Element detectedElement = DetectDamageElement(combatText,param1, param5, param6, param8, param10);

    oDamageInfo(param1, attacker, target, baseDamage, param5, param6, finalDamage, param8, param9, param10);

    // The hook stays installed for fast re-enable; disabled hits still take
    // their normal native path but never update damage-number tracking.
    if (!IsFloatingDamageActive())
        return;
    std::lock_guard lock(trackingMutex);
    if (!target || !target->pDynamicPath)
        return;

    if (isPoisonCombat && parsedDamage > 0)
    {
        TryQueuePoisonCombatMessage(combatText, target);
        return;
    }

    if (displayDamage <= 0)
        return;

    if (!attacker || !attacker->pDynamicPath)
        attacker = target;

    const FloatingDamage::Element element = ResolveTargetElement(
        detectedElement,
        target->dwUnitType,
        target->dwUnitId);

    float screenX = 0.0f;
    float screenY = 0.0f;
    if (!ComputeScreenPositionForTarget(attacker, target, screenX, screenY))
        return;

    if (element == FloatingDamage::Element::Poison)
    {
        QueuePoisonDamageEvent(
            displayDamage,
            screenX,
            screenY,
            target->dwUnitType,
            target->dwUnitId,
            element);
        return;
    }

    QueueTargetDamage(
        displayDamage,
        screenX,
        screenY,
        target->dwUnitType,
        target->dwUnitId,
        FloatingDamage::Kind::Normal,
        element);
}


} // namespace
void ConfigureHooks(void (*resistance)(),void (*chat)()){
    ensureResistance=resistance;ensureChat=chat;
}
void Tick(const ImVec2& displaySize,bool inGame,uint64_t session){
    HudTelemetry::Scope measured(HudTelemetry::Feature::Combat);
    FeedbackEpoch epoch;
    if(FloatingDamage::IsEnabled())EnsureDamageInfoHook();
    std::lock_guard lock(trackingMutex);
    g_LastDisplaySize=displaySize;
    static uint64_t trackedSession=0;
    if(!inGame || trackedSession!=session){
        g_targetElementCache.clear();g_poisonWatch.clear();
        g_lastPoisonHookQueue.clear();g_lastNonPoisonDamageOnTarget.clear();
        g_lastPoisonTarget={UINT32_MAX,UINT32_MAX};g_recentDamageQueue={};
        trackedSession=inGame?session:0;
        if(!inGame)return;
    }
    static auto lastCleanup=std::chrono::steady_clock::time_point{};
    const auto now=std::chrono::steady_clock::now();
    if(now-lastCleanup>=std::chrono::seconds(1)){
        lastCleanup=now;
        std::erase_if(g_targetElementCache,[&](const auto& entry){return entry.second.second<=now;});
        const auto expired=[&](const auto& entry){return now-entry.second>std::chrono::seconds(60);};
        std::erase_if(g_lastPoisonHookQueue,expired);
        std::erase_if(g_lastNonPoisonDamageOnTarget,expired);
    }
    UpdatePoisonDoTWatch();
}
void ObserveCombatMessage(const char* text,D2UnitStrc* target){
    HudTelemetry::Scope measured(HudTelemetry::Feature::Combat);
    FeedbackEpoch epoch;
    if(!FloatingDamage::IsEnabled())return;
    std::lock_guard lock(trackingMutex);TryQueuePoisonCombatMessage(text,target);
}
void ObserveDamageTable(D2DamageInfoStrc* info,D2DamageStatTableStrc* table){
    HudTelemetry::Scope measured(HudTelemetry::Feature::Combat);
    FeedbackEpoch epoch;
    if(!FloatingDamage::IsEnabled())return;
    std::lock_guard lock(trackingMutex);TryQueuePoisonDamageFromStatTable(info,table);
}
bool ReadChatMessage(const blz_string* message,char* text,size_t capacity){return ReadBlzStringMessage(message,text,capacity);}
bool ParseChatLine(const char* text,std::string& sender,std::string& body){return TryParsePlayerChatLine(text,sender,body);}
} // namespace HudCombat
