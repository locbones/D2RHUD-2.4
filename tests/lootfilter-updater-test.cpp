#define NOMINMAX
#include <winsock2.h>
#include "../d2rhud/plugin/D2RHUD/LootFilterUpdater.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#pragma comment(lib, "ws2_32.lib")

namespace {
namespace fs = std::filesystem;
using namespace HudFilterUpdate;
int checks = 0;
void Check(bool condition, const char* message) { ++checks; if (!condition) throw std::runtime_error(message); }
std::string Engine(const std::string& version) { return "local version = \"" + version + "\"\nfunction ApplyFilter(Me, Item, Result, level) end\n"; }
void Write(const fs::path& path, const std::string& bytes) { std::ofstream file(path, std::ios::binary); file.write(bytes.data(), std::streamsize(bytes.size())); if (!file) throw std::runtime_error("fixture write"); }
std::string Read(const fs::path& path) { std::ifstream file(path, std::ios::binary); return std::string(std::istreambuf_iterator<char>(file), {}); }
struct Directory {
    fs::path path;
    Directory() : path(fs::temp_directory_path() / (L"HUD-updater-翻訳-" + std::to_wstring(GetCurrentProcessId()))) {
        if (!fs::create_directory(path)) throw std::runtime_error("private fixture directory already exists");
    }
    ~Directory() {
        std::error_code error;
        for (fs::directory_iterator it(path, error), end; !error && it != end; it.increment(error)) fs::remove(it->path(), error);
        fs::remove(path, error);
    }
};
void NoTemporary(const fs::path& root) {
    bool clean = true;
    for (const auto& entry : fs::directory_iterator(root)) if (entry.path().extension() == L".tmp") clean = false;
    Check(clean, "staging files cleaned on every completed path");
}
Operations Downloading(std::string content) {
    Operations operations;
    operations.download = [content = std::move(content)](const std::wstring&) { return Download{true, content}; };
    return operations;
}
BOOL WINAPI FailWrite(HANDLE, LPCVOID, DWORD, LPDWORD count, LPOVERLAPPED) { *count = 0; SetLastError(ERROR_DISK_FULL); return FALSE; }
BOOL WINAPI ZeroWrite(HANDLE, LPCVOID, DWORD, LPDWORD count, LPOVERLAPPED) { *count = 0; return TRUE; }
BOOL WINAPI PartialFailure(HANDLE file, LPCVOID bytes, DWORD size, LPDWORD count, LPOVERLAPPED overlapped) {
    WriteFile(file, bytes, size / 2, count, overlapped); SetLastError(ERROR_DISK_FULL); return FALSE;
}
BOOL WINAPI ShortWrite(HANDLE file, LPCVOID bytes, DWORD size, LPDWORD count, LPOVERLAPPED overlapped) { return WriteFile(file, bytes, (std::min)(size, DWORD(7)), count, overlapped); }
BOOL WINAPI FailFlush(HANDLE) { SetLastError(ERROR_DISK_FULL); return FALSE; }
BOOL WINAPI FailReplace(LPCWSTR, LPCWSTR, LPCWSTR, DWORD, LPVOID, LPVOID) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
BOOL WINAPI FailMove(LPCWSTR, LPCWSTR, DWORD) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
void MetadataTests() {
    Check(Version(Engine("1.2.3")) == "1.2.3", "ordinary version");
    Check(Version("-- local version = \"99\"\n" + Engine("1.2.3")) == "1.2.3", "line comment cannot impersonate version");
    Check(Version("--[=[\nlocal version=\"99\"\n]=]\n" + Engine("1.2.3")) == "1.2.3", "long comment skipped");
    Check(Version("local text=[=[\nlocal version=\"99\"\n]=]\nlocal version='1.2.3'") == "1.2.3", "long string skipped and single quotes supported");
    Check(Version("local text='local version=\"99\"'\n" + Engine("1.2.3")) == "1.2.3", "quoted metadata-looking text skipped");
    Check(Version("local version = 'not-a-version'").empty(), "version requires numeric component");
    Check(IsNewer("1.10", "1.9"), "numeric version comparison");
    Check(!IsNewer("1.2", "1.2.0") && !IsNewer("v01.002.0", "1.2"), "zero components and prefixes preserved");
    Check(IsNewer("1.2.1", "1.2") && !IsNewer("1.2.0", "1.2.1"), "missing component ordering");
    Check(IsNewer("1." + std::string(5000, '9'), "1.2147483648"), "large components cannot overflow int");
    Check(!IsNewer("1." + std::string(5000, '0'), "1.0"), "long zero component");
    Check(!IsNewer("", "") && IsNewer("1", ""), "empty installed version");
}
void FileTests(const fs::path& root) {
    const auto target = root / L"lootfilter-翻訳.lua";
    const auto old = Engine("1.0"), newer = Engine("1.1");
    Write(target, old); Write(root / "unrelated-config.lua", "authored rules");
    const auto result = Run({target}, Downloading(newer));
    Check(result.outcome == Outcome::Updated && result.version == "1.1", "update success typed result");
    Check(Read(target) == newer && !result.backup.empty() && Read(result.backup) == old, "atomic replacement with original-byte backup on Unicode path");
    Check(Read(root / "unrelated-config.lua") == "authored rules", "author config untouched");
    NoTemporary(root);
    const auto before = fs::last_write_time(target);
    Check(Run({target}, Downloading(newer)).outcome == Outcome::Current && fs::last_write_time(target) == before, "equal version causes no file write");
    Check(Run({target}, Downloading(old)).outcome == Outcome::Current && Read(target) == newer, "downgrade refused");
    for (const auto& body : {std::string{}, std::string("<html>error</html>"), std::string("local version='9'\nfunction ApplyFilter("),
            std::string("function ApplyFilter() end"), std::string("local version='9'"),
            std::string("local version='9'\n-- function ApplyFilter() end"),
            std::string("local version='9'\nlocal function ApplyFilter() end"),
            std::string(MaxBytes + 1, 'x')}) {
        const auto rejected = Run({target}, Downloading(body));
        Check(rejected.outcome == Outcome::Failed && Read(target) == newer, "invalid/empty/oversized/non-engine download preserves existing engine");
        NoTemporary(root);
    }
    auto failure = Downloading(newer);
    failure.download = [](const std::wstring&) { return Download{false, {}, "offline fixture"}; };
    Check(Run({target}, failure).outcome == Outcome::Failed && Read(target) == newer, "download failure preserves file");
    failure.download = [](const std::wstring&) -> Download { throw std::runtime_error("network fixture"); };
    Check(Run({target}, failure).outcome == Outcome::Failed && Read(target) == newer, "download exception contained");
    Write(target, old);
    for (int fault = 0; fault < 5; ++fault) {
        auto operations = Downloading(newer);
        if (fault == 0) operations.write = FailWrite;
        if (fault == 1) operations.write = ZeroWrite;
        if (fault == 2) operations.write = PartialFailure;
        if (fault == 3) operations.flush = FailFlush;
        if (fault == 4) operations.replace = FailReplace;
        const auto rejected = Run({target}, operations);
        Check(rejected.outcome == Outcome::Failed && Read(target) == old, "write/partial/zero/flush/replace failure keeps original");
        Check(Read(result.backup) == old, "older successful backup preserved after failures");
        NoTemporary(root);
    }
    auto shortWrites = Downloading(newer); shortWrites.write = ShortWrite;
    Check(Run({target}, shortWrites).outcome == Outcome::Updated && Read(target) == newer, "short successful writes completed in loop");
    Write(target, old);
    const auto locked = CreateFileW(target.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    Check(locked != INVALID_HANDLE_VALUE, "real target lock");
    const auto lockedResult = Run({target}, Downloading(newer)); CloseHandle(locked);
    Check(lockedResult.outcome == Outcome::Failed && Read(target) == old, "real ReplaceFile sharing failure keeps old engine");
    NoTemporary(root);
    auto edit = Downloading(newer);
    edit.download = [&](const std::wstring&) { Write(target, "external edit"); return Download{true, newer}; };
    Check(Run({target}, edit).outcome == Outcome::Conflict && Read(target) == "external edit", "external edit during download preserved");
    Write(target, old);
    edit.download = [&](const std::wstring&) { fs::remove(target); return Download{true, newer}; };
    Check(Run({target}, edit).outcome == Outcome::Conflict && !fs::exists(target), "external removal is not silently undone");
    auto missing = Downloading(newer); missing.move = FailMove;
    Check(Run({target}, missing).outcome == Outcome::Failed && !fs::exists(target), "new-file move failure leaves target absent");
    const auto created = Run({target}, Downloading(newer));
    Check(created.outcome == Outcome::Updated && created.backup.empty() && Read(target) == newer, "missing engine can be installed without bogus backup");
    fs::remove(target);
    edit.download = [&](const std::wstring&) { Write(target, "created elsewhere"); return Download{true, newer}; };
    Check(Run({target}, edit).outcome == Outcome::Conflict && Read(target) == "created elsewhere", "late external creation preserved");
    NoTemporary(root);
}
void TaskTests(const fs::path& root) {
    const auto target = root / "async.lua"; Write(target, Engine("1"));
    Task task;
    std::atomic<int> calls{0};
    std::promise<void> entered, release; auto enteredFuture = entered.get_future(); auto gate = release.get_future().share();
    Operations operations;
    operations.download = [&, gate](const std::wstring&) { ++calls; entered.set_value(); gate.wait_for(std::chrono::seconds(5)); return Download{true, Engine("2")}; };
    Request request{target};
    Check(task.Start(request, operations), "start owned task");
    Check(enteredFuture.wait_for(std::chrono::seconds(5)) == std::future_status::ready, "worker reaches blocked download");
    request.target = root / "wrong-file.lua";
    for (int i = 0; i < 10000; ++i) if (task.Start(request, operations) || task.Poll()) throw std::runtime_error("overlapping task/premature result");
    Check(task.Busy() && calls == 1, "repeated starts/polls create one worker and no shared result writes");
    release.set_value();
    std::optional<Result> result;
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!result && std::chrono::steady_clock::now() < until) { result = task.Poll(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    Check(result && result->outcome == Outcome::Updated && result->target == target, "immutable target snapshot and typed completion");
    Check(Read(target) == Engine("2") && !fs::exists(request.target), "caller changes cannot redirect worker file writes");
    Check(!task.Busy() && !task.Poll(), "completed result consumed exactly once");
    const auto begin = std::chrono::steady_clock::now();
    for (int i = 0; i < 100000; ++i) if (task.Poll()) throw std::runtime_error("unexpected idle result");
    Check(calls == 1, "100000 idle polls perform no downloads or tasks");
    std::cout << "100000 idle update polls: " << std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count() << " ms; not an FPS benchmark.\n";
}
struct Server {
    SOCKET listener = INVALID_SOCKET;
    std::thread worker;
    uint16_t port = 0;
    std::shared_ptr<std::atomic<bool>> stop = std::make_shared<std::atomic<bool>>(false);
    Server(std::string response, int delayAfterHeaders = 0) {
        listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in address{}; address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (listener == INVALID_SOCKET || bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) || listen(listener, 1)) throw std::runtime_error("loopback listener");
        int length = sizeof(address); getsockname(listener, reinterpret_cast<sockaddr*>(&address), &length); port = ntohs(address.sin_port);
        worker = std::thread([socket = listener, response = std::move(response), delayAfterHeaders, stopping = stop] {
            fd_set set; FD_ZERO(&set); FD_SET(socket, &set); timeval timeout{3, 0};
            if (select(0, &set, nullptr, nullptr, &timeout) <= 0) return;
            const auto client = accept(socket, nullptr, nullptr); if (client == INVALID_SOCKET) return;
            DWORD receiveTimeout = 1000; setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&receiveTimeout), sizeof(receiveTimeout));
            char request[4096]; recv(client, request, sizeof(request), 0);
            size_t at = 0;
            while (at < response.size()) { const auto sent = send(client, response.data() + at, int(response.size() - at), 0); if (sent <= 0) break; at += size_t(sent); }
            for (int elapsed = 0; elapsed < delayAfterHeaders && !stopping->load(); elapsed += 10)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            shutdown(client, SD_BOTH); closesocket(client);
        });
    }
    ~Server() { *stop = true; closesocket(listener); if (worker.joinable()) worker.join(); }
    std::wstring Url() const { return L"http://127.0.0.1:" + std::to_wstring(port) + L"/engine.lua?fixture=1"; }
};
void NetworkTests() {
    WSADATA data{}; Check(WSAStartup(MAKEWORD(2, 2), &data) == 0, "Winsock initialized");
    {
        Server server("HTTP/1.1 200 OK\r\nContent-Length: 5\r\nConnection: close\r\n\r\nhello");
        const auto result = DownloadUrl(server.Url(), {1000, 5000});
        Check(result.ok && result.body == "hello", "real WinHTTP loopback download");
    }
    {
        Server server("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        const auto result = DownloadUrl(server.Url(), {1000, 5000});
        Check(!result.ok && result.error.find("404") != std::string::npos, "HTTP failure identifies status");
    }
    {
        Server server("HTTP/1.1 200 OK\r\nContent-Length: 9000000\r\nConnection: close\r\n\r\n");
        Check(!DownloadUrl(server.Url(), {1000, 5000}).ok, "oversized advertised body rejected before allocation");
    }
    {
        Server server("HTTP/1.1 200 OK\r\nContent-Length: 100\r\nConnection: close\r\n\r\nshort");
        Check(!DownloadUrl(server.Url(), {1000, 5000}).ok, "truncated response rejected");
    }
    {
        Server server("HTTP/1.1 200 OK\r\nContent-Length: 1\r\nConnection: close\r\n\r\n", 10000);
        const auto begin = std::chrono::steady_clock::now();
        const auto stalled = DownloadUrl(server.Url(), {100, 500});
        std::cout << "Stalled fixture: " << stalled.error << "; " << std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count() << " ms\n";
        Check(!stalled.ok, "stalled response terminates");
        Check(stalled.error.find("12002") != stalled.error.npos, "native timeout, rather than server disconnect, stops stalled read");
        Check(std::chrono::steady_clock::now()-begin < std::chrono::seconds(8), "timeout tolerates WinHTTP timer granularity but precedes server disconnect");
    }
    Check(!DownloadUrl(L"file:///bad.lua").ok && !DownloadUrl(L"invalid").ok, "non-HTTP and malformed URL rejected");
    Check(!DownloadUrl(L"http://127.0.0.1/", {0, 0}).ok, "infinite timeout settings rejected");
    WSACleanup();
}
}
int wmain(int argc, wchar_t** argv) {
    try {
        Directory root; MetadataTests(); FileTests(root.path); TaskTests(root.path); NetworkTests();
        if (argc > 1) { const auto source = Read(argv[1]); Check(!source.empty() && !Version(source).empty(), "installed engine metadata read-only compatibility"); }
        std::cout << "PASS: " << checks << " loot-filter updater checks; private files and loopback HTTP only.\n"; return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
