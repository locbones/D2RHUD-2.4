#include "LootFilterUpdater.h"
#include "../ItemFilter/LuaConfigIO.h"
#include "../../ProcessLifetime.h"
#include <winhttp.h>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <utility>
#pragma comment(lib, "winhttp.lib")

namespace HudFilterUpdate {
namespace {
struct Internet { HINTERNET value = nullptr; ~Internet() { if (value) WinHttpCloseHandle(value); } };
struct Handle { HANDLE value = INVALID_HANDLE_VALUE; ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); } };
struct Temporary { std::filesystem::path path; ~Temporary() { if (!path.empty()) DeleteFileW(path.c_str()); } };
std::string Error(const char* operation) {
    const auto error = GetLastError();
    return std::string(operation) + " (Windows error " + std::to_string(error) + ")";
}
bool Read(const std::filesystem::path& path, std::string& text, bool& missing, std::string& error) {
    Handle file{CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
    missing = false; text.clear();
    if (file.value == INVALID_HANDLE_VALUE) {
        const auto code = GetLastError(); missing = code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND;
        if (!missing) error = Error("Could not open engine");
        return missing;
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file.value, &size)) { error = Error("Could not read engine size"); return false; }
    if (size.QuadPart < 0 || size.QuadPart > MaxBytes) { error = "Current engine exceeds the 8 MiB limit"; return false; }
    std::array<char, 65536> bytes{};
    DWORD count = 0;
    do {
        if (!ReadFile(file.value, bytes.data(), DWORD(bytes.size()), &count, nullptr)) { error = Error("Could not read engine"); return false; }
        if (count > MaxBytes - text.size()) { error = "Current engine grew beyond the 8 MiB limit"; return false; }
        text.append(bytes.data(), count);
    } while (count);
    return true;
}
// Tokenize only enough Lua to find metadata. Comments and string contents cannot
// impersonate a version declaration; downloaded code is never executed.
struct Lexer {
    std::string_view source;
    size_t at = 0;
    size_t LongEnd(size_t start) const {
        if (start >= source.size() || source[start] != '[') return start;
        size_t bracket = start + 1;
        while (bracket < source.size() && source[bracket] == '=') ++bracket;
        if (bracket == source.size() || source[bracket] != '[') return start;
        const auto closing = "]" + std::string(source.substr(start + 1, bracket - start - 1)) + "]";
        const auto end = source.find(closing, bracket + 1);
        return end == source.npos ? source.size() : end + closing.size();
    }
    std::string_view Next() {
        while (at < source.size()) {
            if (std::isspace(static_cast<unsigned char>(source[at]))) { ++at; continue; }
            if (source.substr(at, 2) == "--") {
                const auto end = LongEnd(at + 2);
                if (end != at + 2) at = end;
                else { at = source.find('\n', at); if (at == source.npos) at = source.size(); }
                continue;
            }
            const auto first = at;
            const auto longEnd = LongEnd(at);
            if (longEnd != at) at = longEnd;
            else if (source[at] == '\'' || source[at] == '"') {
                const auto quote = source[at++];
                while (at < source.size()) {
                    if (source[at] == '\\') { at = (std::min)(source.size(), at + 2); continue; }
                    if (source[at++] == quote) break;
                }
            } else if (std::isalnum(static_cast<unsigned char>(source[at])) || source[at] == '_') {
                do { ++at; } while (at < source.size() && (std::isalnum(static_cast<unsigned char>(source[at])) || source[at] == '_'));
            } else ++at;
            return source.substr(first, at - first);
        }
        return {};
    }
};
struct Metadata { std::string version; bool entrypoint = false; };
Metadata Inspect(std::string_view source, bool needEntrypoint = true) {
    Lexer lexer{source}; Metadata result;
    std::array<std::string_view, 5> tokens{};
    for (auto token = lexer.Next(); !token.empty(); token = lexer.Next()) {
        for (size_t i = 0; i + 1 < tokens.size(); ++i) tokens[i] = tokens[i + 1];
        tokens.back() = token;
        if (tokens[1] == "local" && tokens[2] == "version" && tokens[3] == "=" && token.size() >= 3 &&
            (token.front() == '\'' || token.front() == '"') && token.back() == token.front() && result.version.empty()) {
            const auto value = token.substr(1, token.size() - 2);
            if (value.size() <= 256 && value.find('\\') == value.npos &&
                value.find_first_of("0123456789") != value.npos) result.version = std::string(value);
        }
        if ((tokens[1] != "local" && tokens[2] == "function" && tokens[3] == "ApplyFilter" && token == "(") ||
            (tokens[1] != "local" && tokens[2] == "ApplyFilter" && tokens[3] == "=" && token == "function")) result.entrypoint = true;
        if (!result.version.empty() && (!needEntrypoint || result.entrypoint)) break;
    }
    return result;
}
std::string_view Number(std::string_view text, size_t& at) {
    while (at < text.size() && !std::isdigit(static_cast<unsigned char>(text[at]))) ++at;
    const auto first = at;
    while (at < text.size() && std::isdigit(static_cast<unsigned char>(text[at]))) ++at;
    auto number = text.substr(first, at - first);
    while (number.size() > 1 && number.front() == '0') number.remove_prefix(1);
    if (number == "0") return {};
    return number;
}
Result Replace(const Request& request, const std::string& expected, bool existed, const std::string& content, Operations& operations) {
    Result result{Outcome::Failed, {}, {}, request.target};
    static std::atomic<uint64_t> serial{0};
    Temporary temporary; Handle file;
    for (int attempt = 0; attempt < 16; ++attempt) {
        auto suffix = L".hud-update-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(++serial);
        temporary.path = request.target; temporary.path += suffix + L".tmp";
        result.backup = request.target; result.backup += suffix + L".bak";
        if (GetFileAttributesW(result.backup.c_str()) != INVALID_FILE_ATTRIBUTES) { temporary.path.clear(); continue; }
        file.value = CreateFileW(temporary.path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file.value != INVALID_HANDLE_VALUE) break;
        const auto error = GetLastError(); temporary.path.clear();
        if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS) break;
    }
    if (file.value == INVALID_HANDLE_VALUE) { result.message = Error("Could not stage engine update"); return result; }
    size_t offset = 0;
    while (offset < content.size()) {
        DWORD written = 0;
        if (!operations.write(file.value, content.data() + offset, DWORD(content.size() - offset), &written, nullptr)) {
            result.message = Error("Could not write staged engine update"); return result;
        }
        if (!written || written > content.size() - offset) { result.message = "Staged engine write made no progress or returned an invalid length"; return result; }
        offset += written;
    }
    if (!operations.flush(file.value)) { result.message = Error("Could not flush staged engine update"); return result; }
    const auto handle = file.value; file.value = INVALID_HANDLE_VALUE;
    if (!CloseHandle(handle)) { result.message = Error("Could not close staged engine update"); return result; }
    std::string current, error; bool missing = false;
    if (!Read(request.target, current, missing, error)) { result.message = error + "; existing file kept"; return result; }
    const bool currentExists = !missing;
    if (currentExists != existed || (existed && current != expected)) {
        result.outcome = Outcome::Conflict; result.message = "Engine changed during download; existing file kept. Retry the update."; return result;
    }
    const bool replaced = existed
        ? operations.replace(request.target.c_str(), temporary.path.c_str(), result.backup.c_str(), 0, nullptr, nullptr) != FALSE
        : operations.move(temporary.path.c_str(), request.target.c_str(), MOVEFILE_WRITE_THROUGH) != FALSE;
    if (!replaced) { result.message = Error("Could not replace engine; existing file kept"); return result; }
    temporary.path.clear();
    if (!existed) result.backup.clear();
    result.outcome = Outcome::Updated;
    return result;
}
}
Download DownloadUrl(const std::wstring& url, NetworkLimits limits) {
    if (limits.ioMs <= 0 || !limits.totalMs) return {false, {}, "Invalid download time limits"};
    const auto started = GetTickCount64();
    const auto failed = [](const char* operation) { return Download{false, {}, Error(operation)}; };
    URL_COMPONENTS parts{}; parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = DWORD(-1);
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts) || !parts.dwHostNameLength ||
        (parts.nScheme != INTERNET_SCHEME_HTTP && parts.nScheme != INTERNET_SCHEME_HTTPS)) return {false, {}, "Invalid update URL"};
    Internet session{WinHttpOpen(L"D2RHUD engine updater", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session.value) return failed("Could not open download session");
    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    std::wstring path;
    if (parts.dwUrlPathLength) path.assign(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.dwExtraInfoLength) path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    if (path.empty()) path = L"/";
    Internet connection{WinHttpConnect(session.value, host.c_str(), parts.nPort, 0)};
    if (!connection.value) return failed("Could not connect to update host");
    Internet request{WinHttpOpenRequest(connection.value, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)};
    if (!request.value) return failed("Could not create update request");
    if (!WinHttpSetTimeouts(request.value, limits.ioMs, limits.ioMs, limits.ioMs, limits.ioMs)) return failed("Could not set request timeouts");
    DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    if (!WinHttpSetOption(request.value, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy))) return failed("Could not configure update redirects");
    if (!WinHttpSendRequest(request.value, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.value, nullptr)) return failed("Engine download failed");
    DWORD status = 0, length = sizeof(status);
    if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &length, WINHTTP_NO_HEADER_INDEX))
        return failed("Could not read download status");
    if (status != 200) return {false, {}, "Engine download returned HTTP " + std::to_string(status)};
    DWORD expected = 0; length = sizeof(expected);
    const bool hasLength = WinHttpQueryHeaders(request.value, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &expected, &length, WINHTTP_NO_HEADER_INDEX) != FALSE;
    if ((!hasLength && GetLastError() != ERROR_WINHTTP_HEADER_NOT_FOUND) || (hasLength && expected > MaxBytes))
        return {false, {}, "Engine download has an invalid or excessive Content-Length"};
    Download result{true}; std::array<char, 65536> bytes{};
    for (;;) {
        if (GetTickCount64() - started >= limits.totalMs) return {false, {}, "Engine download exceeded its time limit"};
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.value, &available)) return failed("Could not receive engine data");
        if (!available) break;
        DWORD received = 0;
        if (!WinHttpReadData(request.value, bytes.data(), (std::min)(available, DWORD(bytes.size())), &received)) return failed("Could not read engine download");
        if (!received) break;
        if (received > MaxBytes - result.body.size()) return {false, {}, "Engine download exceeds the 8 MiB limit"};
        result.body.append(bytes.data(), received);
    }
    if (GetTickCount64() - started >= limits.totalMs) return {false, {}, "Engine download exceeded its time limit"};
    if (hasLength && result.body.size() != expected) return {false, {}, "Engine download was incomplete"};
    return result;
}
std::string Version(std::string_view source) { return Inspect(source, false).version; }
bool IsNewer(std::string_view candidate, std::string_view installed) {
    size_t left = 0, right = 0;
    while (left < candidate.size() || right < installed.size()) {
        const auto a = Number(candidate, left), b = Number(installed, right);
        if (a.size() != b.size()) return a.size() > b.size();
        if (a != b) return a > b;
    }
    return false;
}
Result Run(const Request& request, Operations operations) {
    try {
        if (request.target.empty() || !operations.write || !operations.flush || !operations.replace || !operations.move)
            return {Outcome::Failed, "Invalid engine update request"};
        std::string expected, error; bool missing = false;
        if (!Read(request.target, expected, missing, error)) return {Outcome::Failed, error + "; existing file kept", {}, request.target};
        auto download = operations.download ? operations.download(request.url) : DownloadUrl(request.url);
        if (!download.ok) return {Outcome::Failed, download.error.empty() ? "Engine download failed" : download.error, {}, request.target};
        if (download.body.empty() || download.body.size() > MaxBytes) return {Outcome::Failed, "Downloaded engine is empty or too large", {}, request.target};
        std::string syntax;
        if (!LootFilterConfig::ValidateSyntax(download.body, syntax)) return {Outcome::Failed, "Downloaded engine has invalid Lua syntax; existing file kept", {}, request.target};
        auto metadata = Inspect(download.body);
        if (metadata.version.empty() || !metadata.entrypoint) return {Outcome::Failed, "Downloaded file lacks engine version or ApplyFilter; existing file kept", {}, request.target};
        const auto version = Version(expected);
        if (!version.empty() && !IsNewer(metadata.version, version)) return {Outcome::Current, "No update available", version, request.target};
        auto message = "Updated to " + metadata.version;
        auto result = Replace(request, expected, !missing, download.body, operations);
        if (result.outcome == Outcome::Updated) { result.version = std::move(metadata.version); result.message = std::move(message); }
        return result;
    } catch (const std::exception&) { return {Outcome::Failed, "Engine update failed; existing file kept", {}, request.target}; }
    catch (...) { return {Outcome::Failed, "Engine update failed; existing file kept", {}, request.target}; }
}
bool Task::Start(Request request, Operations operations) {
    if (Busy()) return false;
    try {
        if (request.target.empty()) { ready_ = Result{Outcome::Failed, "Invalid engine update target"}; return false; }
        request.target = std::filesystem::absolute(request.target);
        target_ = request.target;
        pending_ = std::async(std::launch::async, [request = std::move(request), operations = std::move(operations)] { return Run(request, operations); });
        return true;
    } catch (...) { ready_ = Result{Outcome::Failed, "Could not start engine update", {}, target_}; return false; }
}
std::optional<Result> Task::Poll() {
    if (ready_) { auto result = std::move(ready_); ready_.reset(); return result; }
    if (!pending_.valid() || pending_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return {};
    try { return pending_.get(); }
    catch (...) { return Result{Outcome::Failed, "Engine update task failed", {}, target_}; }
}
Task& Runtime() { return HudLifetime::State<Task>(); }
}
