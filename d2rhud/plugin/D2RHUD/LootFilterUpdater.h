#pragma once
#include <Windows.h>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <future>
#include <optional>
#include <string>
#include <string_view>

namespace HudFilterUpdate {
inline constexpr size_t MaxBytes = 8 * 1024 * 1024;
inline constexpr const wchar_t* DefaultUrl = L"https://github.com/locbones/D2RLAN-Filters/raw/refs/heads/main/lootfilter.lua";
enum class Outcome { Updated, Current, Failed, Conflict };
struct Result {
    Outcome outcome = Outcome::Failed;
    std::string message, version;
    std::filesystem::path target, backup;
};
struct Download { bool ok = false; std::string body, error; };
struct NetworkLimits { int ioMs = 10000; uint64_t totalMs = 60000; };
Download DownloadUrl(const std::wstring& url, NetworkLimits limits = {});
struct Request { std::filesystem::path target; std::wstring url = DefaultUrl; };
struct Operations {
    std::function<Download(const std::wstring&)> download;
    decltype(&WriteFile) write = &WriteFile;
    decltype(&FlushFileBuffers) flush = &FlushFileBuffers;
    decltype(&ReplaceFileW) replace = &ReplaceFileW;
    decltype(&MoveFileExW) move = &MoveFileExW;
};
std::string Version(std::string_view source);
bool IsNewer(std::string_view candidate, std::string_view installed);
Result Run(const Request& request, Operations operations = {});
// Render-thread owner; only immutable requests/results cross the worker boundary.
class Task {
public:
    bool Start(Request request, Operations operations = {});
    bool Busy() const { return pending_.valid() || ready_.has_value(); }
    std::optional<Result> Poll();
private:
    std::future<Result> pending_;
    std::optional<Result> ready_;
    std::filesystem::path target_;
};
Task& Runtime();
}
