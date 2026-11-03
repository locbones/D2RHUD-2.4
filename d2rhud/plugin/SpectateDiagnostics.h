#pragma once
#include "../ProcessLifetime.h"
#include <Windows.h>
#include <algorithm>
#include <cstdio>
#include <iterator>
#include <mutex>
#include <string_view>

namespace Spectate::Diagnostics {
struct State {
    std::mutex mutex;
};
// Transition-only diagnostics: never record credentials, IPs, images or items.
inline void Log(std::string_view stage, std::string_view message) noexcept {
    try {
        std::lock_guard lock(HudLifetime::State<State>().mutex);
        wchar_t directory[32768]{};
        const auto length = GetEnvironmentVariableW(L"LOCALAPPDATA", directory, DWORD(std::size(directory)));
        if (!length || length + 40 >= std::size(directory))
            return;
        wcscat_s(directory, L"\\D2RHUD");
        CreateDirectoryW(directory, nullptr);
        wcscat_s(directory, L"\\spectate.log");
        HANDLE file = CreateFileW(directory, FILE_APPEND_DATA | FILE_READ_ATTRIBUTES,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                  OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return;
        LARGE_INTEGER size{};
        if (GetFileSizeEx(file, &size) && size.QuadPart > 512 * 1024) {
            CloseHandle(file);
            wchar_t previous[32768]{};
            wcscpy_s(previous, directory);
            wcscat_s(previous, L".previous");
            MoveFileExW(directory, previous, MOVEFILE_REPLACE_EXISTING);
            file = CreateFileW(directory, FILE_APPEND_DATA,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE)
                return;
        }
        SYSTEMTIME time{};
        GetSystemTime(&time);
        char line[768]{};
        const int count =
            sprintf_s(line, "%04u-%02u-%02u %02u:%02u:%02u UTC pid=%lu [%.*s] %.*s (build %s %s)\r\n",
                      time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond,
                      GetCurrentProcessId(), int((std::min)(stage.size(), size_t(32))), stage.data(),
                      int((std::min)(message.size(), size_t(400))), message.data(), __DATE__, __TIME__);
        DWORD written = 0;
        if (count > 0)
            WriteFile(file, line, DWORD(count), &written, nullptr);
        CloseHandle(file);
    } catch (...) {
        // Diagnostics must never take down the game.
    }
}
} // namespace Spectate::Diagnostics
