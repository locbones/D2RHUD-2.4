#include <Windows.h>
#include <MinHook.h>
#include <cstdint>
#include <cstring>
#include <cctype>
#include <atomic>
#include "../../Pattern.h"

//This plugin was largely developed by CelestialRay. I only assisted with fixing/improving name resolvers and integration

namespace {
    constexpr uint32_t RVA_BankPanelDraw = 0x18EB90;
    constexpr uint32_t RVA_WidgetFindChild = 0x576070;
    constexpr uint32_t RVA_ItemsTablePtr = 0x1CA0390;
    constexpr uint32_t RVA_ItemsTableCount = 0x1CA0398;
    constexpr uint32_t RVA_UnitHashItems = 0x1D442E0 + 4 * 0x400;
    constexpr uint32_t RVA_PlayerUnitHash = 0x1D442E0;
    constexpr uint32_t RVA_PanelManager = 0x1D7C4E8;
    constexpr uint32_t RVA_DrawFilledRect = 0x439280;
    constexpr uint32_t RVA_ResolvePos = 0x576F00;
    constexpr uint32_t RVA_ItemsGetName = 0x149B60;

    constexpr size_t OFF_UnitClassId = 0x04;
    constexpr size_t OFF_UnitItemData = 0x10;
    constexpr size_t OFF_UnitStaticPath = 0x38;
    constexpr size_t OFF_UnitInventory = 0x90;
    constexpr size_t OFF_UnitNext = 0x150;

    constexpr size_t OFF_ItemDataContainerId = 0x0C;
    constexpr size_t OFF_ItemDataPage = 0xB8;
    constexpr size_t ROW_STRIDE = 0x1B4;
    constexpr size_t OFF_RowInvWidth = 0x116;
    constexpr size_t OFF_RowInvHeight = 0x117;

    constexpr size_t OFF_StaticPathX = 0x10;
    constexpr size_t OFF_StaticPathY = 0x14;

    constexpr size_t OFF_InvSharedTabsPtr = 0x68;

    constexpr size_t OFF_WidgetParent = 0x30;
    constexpr size_t OFF_WidgetRectW = 0x78;
    constexpr size_t OFF_WidgetRectH = 0x7C;
    constexpr size_t OFF_WidgetScale = 0x80;

    constexpr size_t OFF_WidgetString = 0x520;
    constexpr size_t OFF_WidgetStringSize = 0x528;
    constexpr size_t OFF_WidgetStringCap = 0x530;
    constexpr size_t OFF_WidgetStringInline = 0x538;

    constexpr size_t OFF_WidgetFocused = 0x551;
    constexpr size_t OFF_TabImageActive = 0x90;

    constexpr size_t OFF_GridCellW = 0x580;
    constexpr size_t OFF_GridCellH = 0x584;
    constexpr size_t OFF_GridCols = 0x5F0;
    constexpr size_t OFF_GridRows = 0x5F4;

    constexpr size_t OFF_NodeNamePtr = 0x08;
    constexpr size_t OFF_NodeChildren = 0x58;
    constexpr size_t OFF_NodeChildCnt = 0x60;

    constexpr uint32_t PAGE_STASH = 7;
    constexpr uint32_t PERSONAL_CONTAINER_ID = 1;

    using BankPanelDraw_t = void(__fastcall*)(void* pBankPanel);
    using WidgetFindChild_t = void* (__fastcall*)(void* pParent, const char* name);
    using DrawFilledRect_t = void(__fastcall*)(int x1, int y1, int x2, int y2, const float color[4]);
    using ResolvePos_t = uint64_t(__fastcall*)(void* pWidget, uint64_t* outXY);
    using ItemsGetName_t = void(__fastcall*)(void* pUnit, char* pBuffer);

    BankPanelDraw_t   oBankPanelDraw = nullptr;
    WidgetFindChild_t pWidgetFindChild = nullptr;
    DrawFilledRect_t  pDrawFilledRect = nullptr;

    std::atomic<void*> g_searchWidget{ nullptr };
    std::atomic<DWORD> g_lastSeenTick{ 0 };
    std::atomic<bool>  g_inputEnabled{ false };
    std::atomic<bool>  g_dropGoldModalOpen{ false };

    bool g_prevMouseDown = false;
    bool g_clickInitialized = false;

    size_t ReadSearchText(void* pWidget, char* out, size_t outCap) {
        if (!pWidget || outCap == 0) return 0;
        auto base = reinterpret_cast<uint8_t*>(pWidget);
        size_t size = *reinterpret_cast<size_t*>(base + OFF_WidgetStringSize);
        if (size > 255) return 0;
        size_t cap = *reinterpret_cast<size_t*>(base + OFF_WidgetStringCap);
        bool isInline = (cap & 0x8000000000000000ULL) != 0;
        const char* src = isInline
            ? reinterpret_cast<const char*>(base + OFF_WidgetStringInline)
            : *reinterpret_cast<const char**>(base + OFF_WidgetString);
        if (!src) return 0;
        size_t n = (size < outCap - 1) ? size : (outCap - 1);
        memcpy(out, src, n);
        out[n] = '\0';
        return n;
    }

    void StripColorCodes(const char* src, char* out, size_t outCap) {
        if (outCap == 0) return;
        size_t w = 0;
        for (size_t i = 0; src[i] != '\0' && w + 1 < outCap; ) {
            uint8_t b = static_cast<uint8_t>(src[i]);
            if (b == 0xFF && src[i + 1] == 'c' && src[i + 2] != '\0') { i += 3; continue; }
            out[w++] = src[i++];
        }
        out[w] = '\0';
    }

    bool ContainsCI(const char* haystack, const char* needle) {
        if (!needle || !*needle) return true;
        for (size_t i = 0; haystack[i] != '\0'; i++) {
            size_t j = 0;
            while (needle[j] != '\0' && haystack[i + j] != '\0' &&
                std::tolower(static_cast<uint8_t>(haystack[i + j])) ==
                std::tolower(static_cast<uint8_t>(needle[j]))) {
                j++;
            }
            if (needle[j] == '\0') return true;
        }
        return false;
    }

    static char* GetLastTwoLines(char* str)
    {
        if (!str || !*str)
            return str;

        char* last = str;
        char* secondLast = nullptr;

        for (char* p = str; *p; ++p)
        {
            if (*p == '\n')
            {
                secondLast = last;
                last = p + 1;
            }
        }

        if (!secondLast)
            return str;

        return secondLast;
    }

    bool ItemMatches(uint8_t* pItem, const char* needle)
    {
        if (!pItem || !needle)
            return false;

        auto getName = reinterpret_cast<ItemsGetName_t>(
            Pattern::Address_SS(RVA_ItemsGetName)
            );

        char buf[0x400] = {};
        getName(pItem, buf);

        if (!buf[0])
            return false;

        char clean[0x400] = {};
        StripColorCodes(buf, clean, sizeof(clean));
        char* filtered = GetLastTwoLines(clean);

        return ContainsCI(filtered, needle);
    }

    void GetItemDimensions(uint32_t classId, uint8_t& outW, uint8_t& outH) {
        outW = 1;
        outH = 1;
        auto itemsBase = *reinterpret_cast<uint8_t**>(Pattern::Address_SS(RVA_ItemsTablePtr));
        auto maxId = *reinterpret_cast<uint32_t*>(Pattern::Address_SS(RVA_ItemsTableCount));
        if (!itemsBase || classId >= maxId) return;
        uint8_t w = *reinterpret_cast<uint8_t*>(itemsBase + classId * ROW_STRIDE + OFF_RowInvWidth);
        uint8_t h = *reinterpret_cast<uint8_t*>(itemsBase + classId * ROW_STRIDE + OFF_RowInvHeight);
        if (w >= 1 && w <= 8) outW = w;
        if (h >= 1 && h <= 8) outH = h;
    }

    bool HasNamedDescendant(uint8_t* node, const char* name, int depth) {
        if (!node || depth > 8) return false;
        const char* np = *reinterpret_cast<const char**>(node + OFF_NodeNamePtr);
        if (np) {
            __try {
                if (strcmp(np, name) == 0) return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
        uint8_t** data = *reinterpret_cast<uint8_t***>(node + OFF_NodeChildren);
        uint64_t count = *reinterpret_cast<uint64_t*>(node + OFF_NodeChildCnt);
        if (!data || count == 0 || count > 256) return false;
        for (uint64_t i = 0; i < count; i++) {
            if (HasNamedDescendant(data[i], name, depth + 1)) return true;
        }
        return false;
    }

    // Resolve a widget's screen-space top-left via D2R's own anchor resolver.
    bool ResolveWidgetTopLeft(void* pWidget, int32_t& outX, int32_t& outY) {
        if (!pWidget) return false;
        auto resolvePos = reinterpret_cast<ResolvePos_t>(Pattern::Address_SS(RVA_ResolvePos));
        uint64_t xy = 0;
        resolvePos(pWidget, &xy);
        outX = static_cast<int32_t>(xy & 0xFFFFFFFF);
        outY = static_cast<int32_t>(xy >> 32);
        return true;
    }

    // Walk the widget's parent chain (via [pWidget+0x30]), multiplying each
    // node's scale at +0x80. Some mods nest the stash inside scaled parents.
    float ResolveAccumulatedScale(void* pWidget) {
        if (!pWidget) return 1.0f;
        float accum = 1.0f;
        uint8_t* node = reinterpret_cast<uint8_t*>(pWidget);
        for (int depth = 0; node && depth < 16; depth++) {
            float s = *reinterpret_cast<float*>(node + OFF_WidgetScale);
            if (s > 0.001f) accum *= s;
            node = *reinterpret_cast<uint8_t**>(node + OFF_WidgetParent);
        }
        return accum;
    }

    bool PointInResolvedWidget(void* pWidget, int cx, int cy) {
        int32_t rx, ry;
        if (!ResolveWidgetTopLeft(pWidget, rx, ry)) return false;
        auto base = reinterpret_cast<uint8_t*>(pWidget);
        int32_t declW = *reinterpret_cast<int32_t*>(base + OFF_WidgetRectW);
        int32_t declH = *reinterpret_cast<int32_t*>(base + OFF_WidgetRectH);
        float scale = ResolveAccumulatedScale(pWidget);
        int32_t rw = static_cast<int32_t>(declW * scale);
        int32_t rh = static_cast<int32_t>(declH * scale);
        return cx >= rx && cx < rx + rw && cy >= ry && cy < ry + rh;
    }

    void UpdateInputEnabledFromClick(void* pSearch) {
        bool mouseDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        if (!g_clickInitialized) {
            g_prevMouseDown = mouseDown;
            g_clickInitialized = true;
            return;
        }
        bool risingEdge = mouseDown && !g_prevMouseDown;
        g_prevMouseDown = mouseDown;
        if (!risingEdge) return;
        HWND hWnd = GetForegroundWindow();
        if (!hWnd) return;
        POINT cursor;
        if (!GetCursorPos(&cursor)) return;
        if (!ScreenToClient(hWnd, &cursor)) return;
        bool inside = PointInResolvedWidget(pSearch,
            static_cast<int>(cursor.x), static_cast<int>(cursor.y));
        g_inputEnabled.store(inside, std::memory_order_relaxed);
    }

    void SyncFocusByte(void* pSearch) {
        auto base = reinterpret_cast<uint8_t*>(pSearch);
        uint8_t target = g_inputEnabled.load(std::memory_order_relaxed) ? 1 : 0;
        *(base + OFF_WidgetFocused) = target;
    }

    uint8_t* FindPlayerUnit() {
        auto buckets = reinterpret_cast<uint8_t**>(Pattern::Address_SS(RVA_PlayerUnitHash));
        for (int i = 0; i < 128; i++) {
            auto unit = buckets[i];
            while (unit) {
                if (*reinterpret_cast<uint32_t*>(unit) == 0) return unit;
                unit = *reinterpret_cast<uint8_t**>(unit + OFF_UnitNext);
            }
        }
        return nullptr;
    }

    // Active tab = the BankTabs child Image whose +0x90 byte is 1.
    int GetActiveTabIndex(void* pBankPanel) {
        void* pTabs = pWidgetFindChild(pBankPanel, "BankTabs");
        if (!pTabs) return -1;
        auto base = reinterpret_cast<uint8_t*>(pTabs);
        uint8_t** data = *reinterpret_cast<uint8_t***>(base + OFF_NodeChildren);
        uint64_t count = *reinterpret_cast<uint64_t*>(base + OFF_NodeChildCnt);
        if (!data || count == 0 || count > 256) return -1;
        for (uint64_t i = 0; i < count; i++) {
            uint8_t* child = data[i];
            if (!child) continue;
            const char* name = *reinterpret_cast<const char**>(child + OFF_NodeNamePtr);
            if (!name) continue;
            if (name[0] == 'I' && name[1] == 'm' && name[2] == 'a' && name[3] == 'g' && name[4] == 'e') {
                if (*(child + OFF_TabImageActive) == 1) {
                    return atoi(name + 5);
                }
            }
        }
        return -1;
    }

    bool GetSharedTabBaseId(uint32_t& outBase) {
        uint8_t* player = FindPlayerUnit();
        if (!player) return false;
        uint8_t* pInv = *reinterpret_cast<uint8_t**>(player + OFF_UnitInventory);
        if (!pInv) return false;
        uint8_t* tab1 = *reinterpret_cast<uint8_t**>(pInv + OFF_InvSharedTabsPtr);
        if (!tab1) return false;
        outBase = *reinterpret_cast<uint32_t*>(tab1);
        return true;
    }

    uint32_t GetVisibleContainerId(void* pBankPanel) {
        int tabIdx = GetActiveTabIndex(pBankPanel);
        if (tabIdx < 0) return 0;
        if (tabIdx == 0) return PERSONAL_CONTAINER_ID;
        uint32_t base = 0;
        if (!GetSharedTabBaseId(base)) return 0;
        return base + static_cast<uint32_t>(tabIdx - 1);
    }

    void DarkenNonMatchingCells(const char* needle, void* pBankPanel) {
        if (!needle || !*needle || !pDrawFilledRect) return;
        uint32_t visibleCid = GetVisibleContainerId(pBankPanel);
        if (visibleCid == 0) return;

        void* pGrid = pWidgetFindChild(pBankPanel, "grid");
        if (!pGrid) return;

        auto gbase = reinterpret_cast<uint8_t*>(pGrid);
        int32_t cellW = *reinterpret_cast<int32_t*>(gbase + OFF_GridCellW);
        int32_t cellH = *reinterpret_cast<int32_t*>(gbase + OFF_GridCellH);
        int32_t gridCols = *reinterpret_cast<int32_t*>(gbase + OFF_GridCols);
        int32_t gridRows = *reinterpret_cast<int32_t*>(gbase + OFF_GridRows);
        if (cellW <= 0 || cellH <= 0 || gridCols <= 0 || gridRows <= 0) return;
        if (gridCols > 64 || gridRows > 64) return;

        float scale = ResolveAccumulatedScale(pGrid);
        cellW = static_cast<int32_t>(cellW * scale);
        cellH = static_cast<int32_t>(cellH * scale);
        if (cellW <= 0 || cellH <= 0) return;

        int32_t gridX = 0, gridY = 0;
        if (!ResolveWidgetTopLeft(pGrid, gridX, gridY)) return;

        constexpr int MAX_CELLS = 64;
        bool match[MAX_CELLS * MAX_CELLS] = {};

        auto buckets = reinterpret_cast<uint8_t**>(Pattern::Address_SS(RVA_UnitHashItems));
        for (int i = 0; i < 128; i++) {
            auto unit = buckets[i];
            while (unit) {
                auto pItemData = *reinterpret_cast<uint8_t**>(unit + OFF_UnitItemData);
                if (pItemData && pItemData[OFF_ItemDataPage] == PAGE_STASH) {
                    uint32_t cid = *reinterpret_cast<uint32_t*>(pItemData + OFF_ItemDataContainerId);
                    if (cid == visibleCid && ItemMatches(unit, needle)) {
                        auto pStaticPath = *reinterpret_cast<uint8_t**>(unit + OFF_UnitStaticPath);
                        if (pStaticPath) {
                            uint32_t cx = *reinterpret_cast<uint32_t*>(pStaticPath + OFF_StaticPathX);
                            uint32_t cy = *reinterpret_cast<uint32_t*>(pStaticPath + OFF_StaticPathY);
                            uint8_t iw = 1, ih = 1;
                            GetItemDimensions(*reinterpret_cast<uint32_t*>(unit + OFF_UnitClassId), iw, ih);
                            for (uint32_t dy = 0; dy < ih; dy++) {
                                for (uint32_t dx = 0; dx < iw; dx++) {
                                    uint32_t x = cx + dx, y = cy + dy;
                                    if (x < (uint32_t)gridCols && y < (uint32_t)gridRows) {
                                        match[y * gridCols + x] = true;
                                    }
                                }
                            }
                        }
                    }
                }
                unit = *reinterpret_cast<uint8_t**>(unit + OFF_UnitNext);
            }
        }

        static const float dim[4] = { 0.0f, 0.0f, 0.0f, 0.6f };
        for (int cy = 0; cy < gridRows; cy++) {
            for (int cx = 0; cx < gridCols; cx++) {
                if (match[cy * gridCols + cx]) continue;
                int x1 = gridX + cx * cellW;
                int y1 = gridY + cy * cellH;
                pDrawFilledRect(x1, y1, x1 + cellW, y1 + cellH, dim);
            }
        }
    }

    void __fastcall HookedBankPanelDraw(void* pBankPanel) {
        oBankPanelDraw(pBankPanel);

        auto pPanelMgr = *reinterpret_cast<uint8_t**>(Pattern::Address_SS(RVA_PanelManager));
        bool modalOpen = pPanelMgr && HasNamedDescendant(pPanelMgr, "DropGoldModal", 0);
        g_dropGoldModalOpen.store(modalOpen, std::memory_order_relaxed);

        void* pSearch = pWidgetFindChild(pBankPanel, "search_input");
        g_searchWidget.store(pSearch, std::memory_order_relaxed);
        if (!pSearch) {
            g_inputEnabled.store(false, std::memory_order_relaxed);
            g_clickInitialized = false;
            return;
        }

        g_lastSeenTick.store(GetTickCount(), std::memory_order_relaxed);

        UpdateInputEnabledFromClick(pSearch);
        SyncFocusByte(pSearch);

        char text[256] = {};
        ReadSearchText(pSearch, text, sizeof(text));

        DarkenNonMatchingCells(text, pBankPanel);
    }
}

bool IsSearchWidgetActive() {
    if (!g_inputEnabled.load(std::memory_order_relaxed)) return false;
    if (!g_searchWidget.load(std::memory_order_relaxed)) return false;
    DWORD last = g_lastSeenTick.load(std::memory_order_relaxed);
    DWORD now = GetTickCount();
    return (now - last) < 100;
}

void* GetSearchWidget() {
    if (!IsSearchWidgetActive()) return nullptr;
    return g_searchWidget.load(std::memory_order_relaxed);
}

void* GetSearchWidgetIfExists() {
    if (!g_searchWidget.load(std::memory_order_relaxed)) return nullptr;
    DWORD last = g_lastSeenTick.load(std::memory_order_relaxed);
    DWORD now = GetTickCount();
    if ((now - last) >= 100) return nullptr;
    return g_searchWidget.load(std::memory_order_relaxed);
}

bool IsDropGoldModalOpen() {
    return g_dropGoldModalOpen.load(std::memory_order_relaxed);
}

bool InstallBankPanelHook() {
    pWidgetFindChild = reinterpret_cast<WidgetFindChild_t>(Pattern::Address_SS(RVA_WidgetFindChild));
    pDrawFilledRect = reinterpret_cast<DrawFilledRect_t>(Pattern::Address_SS(RVA_DrawFilledRect));
    LPVOID target = reinterpret_cast<LPVOID>(Pattern::Address_SS(RVA_BankPanelDraw));
    if (MH_CreateHook(target,
        reinterpret_cast<LPVOID>(&HookedBankPanelDraw),
        reinterpret_cast<LPVOID*>(&oBankPanelDraw)) != MH_OK) {
        return false;
    }
    if (MH_EnableHook(target) != MH_OK) {
        return false;
    }
    return true;
}

void UninstallBankPanelHook() {
    LPVOID target = reinterpret_cast<LPVOID>(Pattern::Address_SS(RVA_BankPanelDraw));
    MH_DisableHook(target);
    MH_RemoveHook(target);
}