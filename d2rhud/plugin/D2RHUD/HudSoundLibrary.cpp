#include "HudSoundLibrary.h"
#include "../../ProcessLifetime.h"
#include <Windows.h>
#include <mmsystem.h>
#include <shellapi.h>
#include <algorithm>
#include <cctype>
#include <mutex>
#include <set>

namespace HudSounds {
namespace {
constexpr uint64_t RefreshMilliseconds = 2000;
std::filesystem::path Path(const std::string& value) {
    return std::filesystem::path(std::u8string(value.begin(), value.end()));
}
std::string Utf8(const std::filesystem::path& value) {
    const auto text = value.u8string();
    return {text.begin(), text.end()};
}
std::string Lower(std::string value) {
    for (auto& c : value) c = char(std::tolower(static_cast<unsigned char>(c)));
    return value;
}
bool Regular(const std::filesystem::path& value) {
    std::error_code error;
    return std::filesystem::is_regular_file(value, error);
}
struct Runtime {
    std::mutex mutex, playbackMutex;
    std::filesystem::path root;
    std::string mod;
    std::shared_ptr<const Files> files;
    uint64_t scanned = 0, resolvedAt = 0;
    bool dirty = true, resolved = false;
    std::string preferred, selected;
};
Runtime& State() { return HudLifetime::State<Runtime>(); }
std::filesystem::path UserDirectory(const Runtime& state) {
    return state.root.empty() ? std::filesystem::path{} : state.root / "My Filters" / "sounds";
}
std::filesystem::path ModDirectory(const Runtime& state, const std::string& folder) {
    if (state.root.empty() || state.mod.empty()) return {};
    for (const auto& candidate : {folder, Lower(folder)}) {
        const std::filesystem::path bases[]{
            state.root / "Mods" / Path(state.mod) / Path(state.mod + ".mpq") / "data" / candidate,
            state.root / "Mods" / Path(state.mod) / "data" / candidate
        };
        for (const auto* relative : {"Sounds", "sounds", "filters/Sounds", "filters/sounds"}) {
            for (const auto& base : bases) {
                const auto path = base / relative;
                std::error_code error;
                if (std::filesystem::is_directory(path, error)) return path;
            }
        }
        if (candidate == Lower(folder)) break;
    }
    return {};
}
std::shared_ptr<const Files> CatalogLocked(Runtime& state, uint64_t now) {
    if (!state.dirty && state.files && now >= state.scanned && now - state.scanned < RefreshMilliseconds)
        return state.files;
    auto files = std::make_shared<Files>();
    std::set<std::string> seen;
    for (const auto& directory : {UserDirectory(state), ModDirectory(state, "Pandemonium"), ModDirectory(state, "D2RLAN")}) {
        if (directory.empty()) continue;
        std::vector<std::filesystem::path> entries;
        std::error_code error;
        std::filesystem::directory_iterator iterator(directory, std::filesystem::directory_options::skip_permission_denied, error), end;
        while (!error && iterator != end) {
            if (IsSupported(iterator->path()) && Regular(iterator->path())) entries.push_back(iterator->path());
            iterator.increment(error);
        }
        std::sort(entries.begin(), entries.end());
        for (const auto& entry : entries) {
            const auto name = Utf8(entry.filename());
            if (seen.insert(Lower(name)).second) files->emplace_back(Utf8(entry), name);
        }
    }
    // Keep the existing display order and Auto selection; duplicate names alone
    // are compared without case so user imports keep their source precedence.
    std::sort(files->begin(), files->end(), [](const auto& a, const auto& b) { return a.second < b.second; });
    state.files = std::move(files);
    state.scanned = now;
    state.dirty = false;
    state.resolved = false;
    return state.files;
}
}

std::string ExecutableDirectory() {
    std::wstring buffer(260, L'\0');
    while (buffer.size() <= 32768) {
        const auto length = GetModuleFileNameW(nullptr, buffer.data(), DWORD(buffer.size()));
        if (!length) return {};
        if (length < buffer.size()) { buffer.resize(length); return Utf8(std::filesystem::path(buffer).parent_path()); }
        buffer.resize(buffer.size() * 2);
    }
    return {};
}
void Configure(const std::string& directory, const std::string& mod) {
    auto& state = State();
    std::lock_guard lock(state.mutex);
    const auto root = Path(directory);
    if (root == state.root && mod == state.mod) return;
    state.root = root;
    state.mod = mod;
    state.dirty = true;
    state.resolved = false;
}
std::shared_ptr<const Files> Catalog() {
    auto& state = State(); std::lock_guard lock(state.mutex);
    return CatalogLocked(state, GetTickCount64());
}
void Invalidate() {
    auto& state = State(); std::lock_guard lock(state.mutex);
    state.dirty = true; state.resolved = false;
}
bool IsSupported(const std::filesystem::path& path) {
    const auto extension = Lower(Utf8(path.extension()));
    return extension == ".wav" || extension == ".mp3" || extension == ".flac";
}
std::string Resolve(const std::string& preferred) {
    auto& state = State(); std::lock_guard lock(state.mutex);
    const auto now = GetTickCount64();
    if (state.resolved && state.preferred == preferred && now >= state.resolvedAt &&
        now - state.resolvedAt < RefreshMilliseconds) return state.selected;
    state.preferred = preferred;
    state.selected.clear();
    if (!preferred.empty() && IsSupported(Path(preferred)) && Regular(Path(preferred))) state.selected = preferred;
    else {
        const auto files = CatalogLocked(state, now);
        if (!files->empty()) state.selected = files->front().first;
    }
    state.resolvedAt = now; state.resolved = true;
    return state.selected;
}
ImportResult Import(const std::string& sourceDirectory) {
    ImportResult result;
    std::filesystem::path target;
    { auto& state = State(); std::lock_guard lock(state.mutex); target = UserDirectory(state); }
    if (target.empty()) return {0, "My Filters/sounds path not found."};
    try {
        const auto source = Path(sourceDirectory);
        std::error_code error;
        if (!std::filesystem::is_directory(source, error)) return {0, "Folder not found."};
        std::filesystem::create_directories(target);
        if (std::filesystem::equivalent(source, target)) return {0, "These sounds are already in My Filters/sounds."};
        std::filesystem::directory_iterator iterator(source, error), end;
        if (error) return {0, "Could not read the sounds folder: " + error.message()};
        while (!error && iterator != end) {
            const auto file = iterator->path();
            if (IsSupported(file) && Regular(file)) {
                std::error_code copyError;
                if (std::filesystem::copy_file(file, target / file.filename(), std::filesystem::copy_options::overwrite_existing, copyError))
                    ++result.copied;
                else if (result.error.empty()) result.error = "Could not import " + Utf8(file.filename()) + ": " + copyError.message();
            }
            iterator.increment(error);
        }
        if (error && result.error.empty()) result.error = "Could not read the sounds folder: " + error.message();
        if (!result.copied && result.error.empty()) result.error = "No .mp3, .flac or .wav files found in folder.";
    } catch (const std::exception& error) { result.error = error.what(); }
    // Also invalidate after partial imports, including overwriting an existing file.
    Invalidate();
    return result;
}
bool PlayFile(const std::string& path) {
    if (path.empty()) return false;
    try {
        const auto file = Path(path);
        const auto extension = Lower(Utf8(file.extension()));
        if (extension == ".wav") return PlaySoundW(file.c_str(), nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT) != FALSE;
        if (extension == ".mp3") {
            // Serialize access to this HUD-owned alias; never close another feature's audio.
            std::lock_guard lock(State().playbackMutex);
            constexpr auto close = L"close D2RHUD_SoundPreview";
            mciSendStringW(close, nullptr, 0, nullptr);
            const auto command = L"open \"" + file.wstring() + L"\" type mpegvideo alias D2RHUD_SoundPreview";
            if (mciSendStringW(command.c_str(), nullptr, 0, nullptr)) return false;
            if (!mciSendStringW(L"play D2RHUD_SoundPreview", nullptr, 0, nullptr)) return true;
            mciSendStringW(close, nullptr, 0, nullptr);
            return false;
        }
        if (extension == ".flac")
            return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", file.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
    } catch (const std::exception&) {}
    return false;
}
void Beep() { MessageBeep(MB_OK); }
}
