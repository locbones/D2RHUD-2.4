// Intercepts D2R's window messages.
// - When user has clicked the search widget, we directly mutate the widget's
//   std::string for typing and backspace, because D2R's native WM_CHAR
//   pipeline drops ~70% of keystrokes (likely a per-frame gate). Direct
//   write is reliable.
// - For non-typing operations (arrows, Ctrl+A/C/X/V, selection), we still
//   pass through to D2R's native widget input via CallWindowProcW — those
//   are infrequent enough that the per-frame drop doesn't matter much, and
//   re-implementing them was the source of the previous bug pile.
// - When widget is "focused" (+0x551 = 1) but user hasn't clicked, block
//   stray WM_CHAR. Stand down for DropGoldModal so digits reach the popup.
// - Capped at 15 chars total: D2R's std::string SSO transition to heap at
//   16+ chars makes direct mutation unsafe. JSON maxStringLength must
//   match.
#include <Windows.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include "../../Pattern.h"

bool  IsSearchWidgetActive();
void* GetSearchWidget();
void* GetSearchWidgetIfExists();
bool  IsDropGoldModalOpen();

namespace {
    HWND    g_hWnd = nullptr;
    WNDPROC g_origWndProc = nullptr;

    constexpr size_t OFF_WidgetStringPtr = 0x520;
    constexpr size_t OFF_WidgetStringSize = 0x528;
    constexpr size_t OFF_WidgetStringCap = 0x530;
    constexpr size_t OFF_WidgetStringInline = 0x538;
    constexpr uint64_t SSO_FLAG = 0x8000000000000000ULL;

    constexpr size_t OFF_WidgetFocused = 0x551;
    constexpr size_t OFF_WidgetCaret = 0x548;
    constexpr size_t OFF_WidgetSelStart = 0x670;
    constexpr size_t OFF_WidgetSelEnd = 0x674;

    constexpr size_t MAX_SEARCH_LEN = 15;

    void SetCaret(void* pWidget, int32_t pos) {
        auto base = reinterpret_cast<uint8_t*>(pWidget);
        *reinterpret_cast<int32_t*>(base + OFF_WidgetCaret) = pos;
        *reinterpret_cast<int32_t*>(base + OFF_WidgetSelStart) = pos;
        *reinterpret_cast<int32_t*>(base + OFF_WidgetSelEnd) = pos;
    }

    // Delete selection [s, e) from buf of size 'size'. Updates buf, returns
    // new size and writes the post-delete caret position via *outCaret.
    size_t DeleteRange(char* buf, size_t size, int32_t s, int32_t e, int32_t* outCaret) {
        if (s < 0) s = 0;
        if (e > static_cast<int32_t>(size)) e = static_cast<int32_t>(size);
        if (s >= e) {
            *outCaret = s;
            return size;
        }
        for (size_t i = static_cast<size_t>(s); i + (e - s) < size; i++) {
            buf[i] = buf[i + (e - s)];
        }
        size_t newSize = size - static_cast<size_t>(e - s);
        buf[newSize] = '\0';
        *outCaret = s;
        return newSize;
    }

    void AppendCharToWidget(void* pWidget, char c) {
        auto base = reinterpret_cast<uint8_t*>(pWidget);
        size_t size = *reinterpret_cast<size_t*>(base + OFF_WidgetStringSize);
        uint64_t cap = *reinterpret_cast<uint64_t*>(base + OFF_WidgetStringCap);
        bool isInline = (cap & SSO_FLAG) != 0;
        if (!isInline) return;

        char* buf = reinterpret_cast<char*>(base + OFF_WidgetStringInline);

        int32_t selStart = *reinterpret_cast<int32_t*>(base + OFF_WidgetSelStart);
        int32_t selEnd = *reinterpret_cast<int32_t*>(base + OFF_WidgetSelEnd);
        if (selStart > selEnd) { int32_t t = selStart; selStart = selEnd; selEnd = t; }

        int32_t caret;
        if (selStart != selEnd) {
            size = DeleteRange(buf, size, selStart, selEnd, &caret);
        }
        else {
            caret = *reinterpret_cast<int32_t*>(base + OFF_WidgetCaret);
            if (caret < 0) caret = 0;
            if (static_cast<size_t>(caret) > size) caret = static_cast<int32_t>(size);
        }

        if (size >= MAX_SEARCH_LEN) {
            *reinterpret_cast<size_t*>(base + OFF_WidgetStringSize) = size;
            SetCaret(pWidget, caret);
            return;
        }

        for (size_t i = size; i > static_cast<size_t>(caret); i--) {
            buf[i] = buf[i - 1];
        }
        buf[caret] = c;
        buf[size + 1] = '\0';

        *reinterpret_cast<size_t*>(base + OFF_WidgetStringSize) = size + 1;
        SetCaret(pWidget, caret + 1);
    }

    void BackspaceWidget(void* pWidget) {
        auto base = reinterpret_cast<uint8_t*>(pWidget);
        size_t size = *reinterpret_cast<size_t*>(base + OFF_WidgetStringSize);
        uint64_t cap = *reinterpret_cast<uint64_t*>(base + OFF_WidgetStringCap);
        bool isInline = (cap & SSO_FLAG) != 0;
        if (!isInline || size == 0) return;

        char* buf = reinterpret_cast<char*>(base + OFF_WidgetStringInline);

        int32_t selStart = *reinterpret_cast<int32_t*>(base + OFF_WidgetSelStart);
        int32_t selEnd = *reinterpret_cast<int32_t*>(base + OFF_WidgetSelEnd);
        if (selStart > selEnd) { int32_t t = selStart; selStart = selEnd; selEnd = t; }

        int32_t caret;
        if (selStart != selEnd) {
            size = DeleteRange(buf, size, selStart, selEnd, &caret);
        }
        else {
            caret = *reinterpret_cast<int32_t*>(base + OFF_WidgetCaret);
            if (caret <= 0) {
                SetCaret(pWidget, 0);
                return;
            }
            if (static_cast<size_t>(caret) > size) caret = static_cast<int32_t>(size);
            for (size_t i = static_cast<size_t>(caret) - 1; i + 1 < size; i++) {
                buf[i] = buf[i + 1];
            }
            buf[size - 1] = '\0';
            size -= 1;
            caret -= 1;
        }

        *reinterpret_cast<size_t*>(base + OFF_WidgetStringSize) = size;
        SetCaret(pWidget, caret);
    }

    bool IsWidgetCurrentlyFocused(void* pWidget) {
        if (!pWidget) return false;
        return *reinterpret_cast<uint8_t*>(reinterpret_cast<uint8_t*>(pWidget) + OFF_WidgetFocused) != 0;
    }

    LRESULT CALLBACK HookedWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        void* pWidget = GetSearchWidget();           // user clicked search
        void* pVisible = GetSearchWidgetIfExists();  // search widget exists

        // Stray-typing guard: widget focused but user hasn't clicked.
        if (pVisible && !pWidget && IsWidgetCurrentlyFocused(pVisible)
            && msg == WM_CHAR && !IsDropGoldModalOpen()) {
            char c = static_cast<char>(wParam & 0xFF);
            if ((unsigned char)c >= 0x20 && (unsigned char)c <= 0x7E) return 0;
            if (wParam == '\b') return 0;
        }

        // When user has clicked search: handle WM_CHAR ourselves by direct
        // mutation. Block the corresponding WM_KEYDOWN to suppress D2R
        // hotkeys. Let everything else (arrows, Ctrl combos, etc.) fall
        // through to D2R's native widget pipeline.
        if (pWidget) {
            if (msg == WM_CHAR) {
                // Skip if Ctrl/Alt is held — those produce control chars
                // (Ctrl+A=0x01, etc.) we don't want to insert. Let D2R's
                // native handler process them (clipboard, select-all).
                if ((GetKeyState(VK_CONTROL) & 0x8000) || (GetKeyState(VK_MENU) & 0x8000)) {
                    return CallWindowProcW(g_origWndProc, hWnd, msg, wParam, lParam);
                }
                char c = static_cast<char>(wParam & 0xFF);
                if (wParam == '\b') {
                    BackspaceWidget(pWidget);
                    return 0;
                }
                if ((unsigned char)c >= 0x20 && (unsigned char)c <= 0x7E) {
                    AppendCharToWidget(pWidget, c);
                    return 0;
                }
                // Other control chars (Tab, Esc, etc.) — let D2R handle.
                return CallWindowProcW(g_origWndProc, hWnd, msg, wParam, lParam);
            }
            if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) {
                // Block hotkey-able keys to suppress D2R's hotkey dispatcher.
                // Modifier combos (Ctrl/Alt held) still pass through so D2R
                // native pipeline handles Ctrl+A, Ctrl+C, etc.
                if ((GetKeyState(VK_CONTROL) & 0x8000) || (GetKeyState(VK_MENU) & 0x8000)) {
                    return CallWindowProcW(g_origWndProc, hWnd, msg, wParam, lParam);
                }
                if ((wParam >= 'A' && wParam <= 'Z')
                    || (wParam >= '0' && wParam <= '9')
                    || (wParam >= VK_NUMPAD0 && wParam <= VK_NUMPAD9)
                    || wParam == VK_SPACE || wParam == VK_BACK
                    || wParam == VK_OEM_1 || wParam == VK_OEM_PLUS
                    || wParam == VK_OEM_COMMA || wParam == VK_OEM_MINUS
                    || wParam == VK_OEM_PERIOD || wParam == VK_OEM_2
                    || wParam == VK_OEM_3 || wParam == VK_OEM_4
                    || wParam == VK_OEM_5 || wParam == VK_OEM_6
                    || wParam == VK_OEM_7) {
                    return 0;
                }
            }
            if (msg == WM_KEYUP || msg == WM_SYSKEYUP) {
                if ((GetKeyState(VK_CONTROL) & 0x8000) || (GetKeyState(VK_MENU) & 0x8000)) {
                    return CallWindowProcW(g_origWndProc, hWnd, msg, wParam, lParam);
                }
                if ((wParam >= 'A' && wParam <= 'Z')
                    || (wParam >= '0' && wParam <= '9')
                    || (wParam >= VK_NUMPAD0 && wParam <= VK_NUMPAD9)
                    || wParam == VK_SPACE || wParam == VK_BACK
                    || wParam == VK_OEM_1 || wParam == VK_OEM_PLUS
                    || wParam == VK_OEM_COMMA || wParam == VK_OEM_MINUS
                    || wParam == VK_OEM_PERIOD || wParam == VK_OEM_2
                    || wParam == VK_OEM_3 || wParam == VK_OEM_4
                    || wParam == VK_OEM_5 || wParam == VK_OEM_6
                    || wParam == VK_OEM_7) {
                    return 0;
                }
            }
        }

        return CallWindowProcW(g_origWndProc, hWnd, msg, wParam, lParam);
    }

    struct EnumCtx { DWORD pid; HWND result; };

    BOOL CALLBACK EnumWindowsCb(HWND hWnd, LPARAM lParam) {
        auto* ctx = reinterpret_cast<EnumCtx*>(lParam);
        DWORD pid = 0;
        GetWindowThreadProcessId(hWnd, &pid);
        if (pid == ctx->pid && IsWindowVisible(hWnd) && GetWindow(hWnd, GW_OWNER) == nullptr) {
            wchar_t title[256] = {};
            GetWindowTextW(hWnd, title, 255);
            if (wcsstr(title, L"Diablo") != nullptr) {
                ctx->result = hWnd;
                return FALSE;
            }
        }
        return TRUE;
    }

    HWND FindD2RWindow() {
        EnumCtx ctx{ GetCurrentProcessId(), nullptr };
        EnumWindows(EnumWindowsCb, reinterpret_cast<LPARAM>(&ctx));
        return ctx.result;
    }
}

bool InstallWndProcHook() {
    for (int attempt = 0; attempt < 50 && !g_hWnd; attempt++) {
        g_hWnd = FindD2RWindow();
        if (!g_hWnd) Sleep(100);
    }
    if (!g_hWnd) {
        return false;
    }
    g_origWndProc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(g_hWnd, GWLP_WNDPROC,
            reinterpret_cast<LONG_PTR>(&HookedWndProc)));
    if (!g_origWndProc) {
        return false;
    }
    return true;
}

void UninstallWndProcHook() {
    if (g_hWnd && g_origWndProc) {
        SetWindowLongPtrW(g_hWnd, GWLP_WNDPROC,
            reinterpret_cast<LONG_PTR>(g_origWndProc));
        g_hWnd = nullptr;
        g_origWndProc = nullptr;
    }
}