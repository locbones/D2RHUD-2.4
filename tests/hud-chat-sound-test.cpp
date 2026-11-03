#define NOMINMAX
#include <Windows.h>
#include <mmsystem.h>
#include <shellapi.h>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>
#include "../d2rhud/Pattern.h"
#include "../d2rhud/d2/D2Enums.h"
#include "../d2rhud/d2/Reclass.h"

namespace {
std::atomic<uint64_t> clockNow{10000};
std::vector<std::wstring> sounds, media;
int beeps = 0, shells = 0, identityReads = 0;
bool wavWorks = true, openWorks = true, playWorks = true;
ULONGLONG WINAPI FixtureTime() { return clockNow.load(); }
BOOL WINAPI FixturePlay(LPCWSTR path, HMODULE, DWORD flags) {
    if (path) sounds.emplace_back(path);
    return wavWorks && (flags & SND_ASYNC);
}
MCIERROR WINAPI FixtureMci(LPCWSTR command, LPWSTR, UINT, HWND) {
    media.emplace_back(command);
    if (media.back().starts_with(L"open") && !openWorks) return 1;
    if (media.back().starts_with(L"play") && !playWorks) return 1;
    return 0;
}
HINSTANCE WINAPI FixtureShell(HWND, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, INT) {
    ++shells; return reinterpret_cast<HINSTANCE>(33);
}
BOOL WINAPI FixtureBeep(UINT) { ++beeps; return TRUE; }
int checks = 0;
void Check(bool value, const char* description) {
    ++checks;
    if (!value) { std::cerr << "FAILED: " << description << '\n'; std::exit(1); }
}
uint8_t* Memory() {
    static auto memory = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x2000000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    return memory;
}
D2UnitStrc* localUnit = nullptr;
}
DWORD64 Pattern::Address(uint32_t rva) { return reinterpret_cast<DWORD64>(Memory() + rva); }
D2UnitStrc* GetClientPlayerUnit() { ++identityReads; return localUnit; }

// Real modules and Win32 filesystem/memory reads; audio APIs and time alone are stubbed.
#define GetTickCount64 FixtureTime
#define PlaySoundW FixturePlay
#define mciSendStringW FixtureMci
#define ShellExecuteW FixtureShell
#define MessageBeep FixtureBeep
#include "../d2rhud/plugin/D2RHUD/HudSoundLibrary.cpp"
#include "../d2rhud/plugin/D2RHUD/ChatNotifications.cpp"
#undef GetTickCount64
#undef PlaySoundW
#undef mciSendStringW
#undef ShellExecuteW
#undef MessageBeep

namespace {
std::string Utf8(const std::filesystem::path& path) {
    const auto text = path.lexically_normal().make_preferred().u8string(); return {text.begin(), text.end()};
}
void File(const std::filesystem::path& path, const char* contents = "fixture") {
    std::filesystem::create_directories(path.parent_path()); std::ofstream file(path, std::ios::binary); file << contents;
}
std::string ReadFileBytes(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary); return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
void InboxTests() {
    using namespace HudChat;
    Inbox inbox;
    const auto inaccessible = static_cast<char*>(VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_NOACCESS));
    Check(inaccessible != nullptr, "guarded input allocated");
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 100000; ++i)
        if (inbox.Submit({inaccessible, 64}, {inaccessible, 256})) std::abort();
    std::cout << "100000 disabled notification callbacks: " << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count()
              << " ms (guarded buffers; not an FPS benchmark).\n";
    Check(inbox.Pending() == 0, "disabled callbacks never read inaccessible buffers");
    VirtualFree(inaccessible, 0, MEM_RELEASE);
    for (const auto text : {"Out of mana!", "Not enough gold", "Locked.", "\xff" "c1No space", "\xc3\xbf" "c2Not here"})
        Check(IsSystemFeedback(text), "native and UTF-8 colored feedback ignored");
    Check(!IsSystemFeedback("Lockedness is a strange word") && !IsSystemFeedback("Hello world"), "feedback matching respects phrase boundaries");
    Check(!IsSystemFeedback(std::string_view("\xff", 1)), "incomplete color prefix bounded safely");
    inbox.SetContext(1, true, {"Local", UNIT_PLAYER, 7});
    Check(inbox.Submit("Remote", "Hello", UNIT_PLAYER, 42, true), "remote packet queued");
    inbox.Submit("Local", "My own message", UNIT_PLAYER, 7, true);
    Check(inbox.TakeAlert(1000), "own message cannot overwrite a queued remote message");
    Check(inbox.Pending() == 0 && !inbox.TakeAlert(2000), "consumed batch cleared completely");
    inbox.Submit("Remote", "Second", UNIT_PLAYER, 42, true);
    Check(!inbox.TakeAlert(1200), "350 ms cooldown coalesces duplicate hook deliveries");
    inbox.Submit("Remote", "Third", UNIT_PLAYER, 42, true);
    Check(inbox.TakeAlert(1350), "new remote alert after cooldown");
    inbox.Submit("Different name", "Own unit", UNIT_PLAYER, 7, true);
    Check(!inbox.TakeAlert(2000), "unit identity excludes own chat even if sender differs");
    inbox.Submit("LOCAL", "Own name", UNIT_PLAYER, 42, true);
    Check(!inbox.TakeAlert(2000), "case-insensitive name excludes own chat");
    inbox.Submit("\xff" "c1Local", "Own colored name");
    Check(!inbox.TakeAlert(2000), "sender color codes do not bypass own-name filter");
    inbox.Submit("Npc", "Speech", UNIT_MONSTER, 42, true);
    Check(!inbox.TakeAlert(2000), "non-player unit is not a player chat notification");
    for (int i = 0; i < 100; ++i) inbox.Submit("Remote", "Burst", UNIT_PLAYER, 42, true);
    Check(inbox.Pending() == 32, "inbox remains bounded during bursts");
    Check(inbox.TakeAlert(2000) && !inbox.TakeAlert(2350), "burst yields one alert without an audio backlog");
    inbox.Submit("Remote", "Old session"); inbox.SetContext(0, true, {});
    Check(!inbox.Enabled() && !inbox.TakeAlert(3000), "leaving game clears pending alerts");
    inbox.SetContext(2, true, {"Local", UNIT_PLAYER, 7});
    Check(!inbox.TakeAlert(3000), "new session never inherits old alerts");
    inbox.Submit("Remote", "Queued before disable"); inbox.SetContext(2, false, {"Local", UNIT_PLAYER, 7});
    inbox.SetContext(2, true, {"Local", UNIT_PLAYER, 7});
    Check(!inbox.TakeAlert(3000), "off/on discards notifications received before disabling");
    inbox.SetContext(2, true, {"", UNIT_PLAYER, 7});
    inbox.Submit("Remote", "Known remote ID", UNIT_PLAYER, 42, true);
    Check(inbox.TakeAlert(3000), "known local unit can distinguish remote player before name is available");
    inbox.SetContext(3, true, {}); inbox.Submit("Unknown", "No identity");
    Check(!inbox.TakeAlert(4000), "unknown identity does not risk alerting for own chat");
    inbox.SetContext(4, true, {"Local", UNIT_PLAYER, 7});
    std::atomic<int> finished{0};
    std::vector<std::thread> producers;
    for (int thread = 0; thread < 4; ++thread) producers.emplace_back([&, thread] {
        for (int i = 0; i < 3000; ++i) {
            if (thread % 2) inbox.Submit("Local", "Name suppresses me", UNIT_PLAYER, 42, true);
            else inbox.Submit("Remote", "ID suppresses me", UNIT_PLAYER, 7, true);
        }
        ++finished;
    });
    bool torn = false, bounded = true;
    while (finished < 4) { torn |= inbox.TakeAlert(5000); bounded &= inbox.Pending() <= 32; std::this_thread::yield(); }
    for (auto& producer : producers) producer.join();
    torn |= inbox.TakeAlert(5000);
    Check(!torn && bounded, "12000 concurrent messages never mix sender and unit fields into a false alert");
}
void LibraryTests(const std::filesystem::path& root) {
    const auto user = root / "My Filters/sounds";
    const auto packaged = root / "Mods/RMD-MP/RMD-MP.mpq/data/Pandemonium/Sounds";
    const auto legacy = root / "Mods/RMD-MP/data/D2RLAN/filters/sounds";
    File(user / "common.WAV", "user"); File(user / L"Müller.wav"); File(user / "Zeta.MP3"); File(user / "notes.txt");
    File(packaged / "common.wav", "mod"); File(packaged / "Signal.wav");
    File(legacy / "SIGNAL.wav", "legacy"); File(legacy / "Legacy.flac");
    std::filesystem::create_directory(user / "directory.wav");
    HudSounds::Configure(Utf8(root), "RMD-MP");
    const auto files = HudSounds::Catalog();
    Check(files->size() == 5, "supported files merged across user, modern and legacy folders");
    const auto duplicate = std::find_if(files->begin(), files->end(), [](const auto& file) { return file.second == "common.WAV"; });
    Check(duplicate != files->end() && duplicate->first == Utf8(user / "common.WAV"), "user duplicate wins case-insensitively");
    Check(files->front().second == "Legacy.flac", "existing case-sensitive display order and Auto sound retained");
    bool packagedWins = false, unicode = false;
    for (const auto& [path, name] : *files) {
        packagedWins |= path == Utf8(packaged / "Signal.wav");
        unicode |= path == Utf8(user / L"Müller.wav");
    }
    Check(packagedWins && unicode, "packaged precedence and Unicode paths retained");
    File(user / "Later.wav");
    bool same = true;
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 10000; ++i) same &= HudSounds::Catalog() == files;
    std::cout << "10000 cached sound-list lookups: " << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count()
              << " ms (no directory refresh; not an FPS benchmark).\n";
    Check(same && files->size() == 5, "warm catalog reused without rescanning newly added files");
    HudSounds::Configure(Utf8(root), "RMD-MP");
    Check(HudSounds::Catalog() == files, "unchanged configuration does not invalidate cache");
    clockNow += 2000;
    const auto refreshed = HudSounds::Catalog();
    Check(refreshed != files && refreshed->size() == 6 && files->size() == 5, "timed refresh sees additions and preserves active snapshot lifetime");
    const auto imports = root / "incoming";
    File(imports / "Fresh.wav"); File(imports / "common.WAV", "updated"); File(imports / "skip.txt");
    auto imported = HudSounds::Import(Utf8(imports));
    Check(imported.copied == 2 && imported.error.empty(), "supported files imported and overwritten");
    Check(HudSounds::Catalog()->size() == 7 && ReadFileBytes(user / "common.WAV") == "updated", "imports immediately invalidate cached list");
    Check(!HudSounds::Import(Utf8(user)).error.empty(), "importing sounds folder into itself rejected safely");
    Check(!HudSounds::Import(Utf8(root / "missing")).error.empty(), "missing import folder reported");
    const auto blocked = root / "blocked";
    File(blocked / "Blocked.wav"); File(user / "Blocked.wav", "keep");
    Check(SetFileAttributesW((user / "Blocked.wav").c_str(), FILE_ATTRIBUTE_READONLY), "readonly import target fixture ready");
    imported = HudSounds::Import(Utf8(blocked));
    Check(imported.copied == 0 && !imported.error.empty() && ReadFileBytes(user / "Blocked.wav") == "keep", "copy failure reported without replacing protected sound");
    SetFileAttributesW((user / "Blocked.wav").c_str(), FILE_ATTRIBUTE_NORMAL);
    const auto custom = root / L"custom-音.mp3";
    File(custom);
    Check(HudSounds::Resolve(Utf8(custom)) == Utf8(custom), "custom Unicode sound selected");
    std::filesystem::remove(custom);
    Check(HudSounds::Resolve(Utf8(custom)) == Utf8(custom), "selected-path cache avoids repeated file checks");
    clockNow += 2000;
    Check(HudSounds::Resolve(Utf8(custom)) == HudSounds::Catalog()->front().first, "removed custom file eventually falls back to available sound");
    Check(HudSounds::PlayFile(Utf8(user / L"Müller.wav")) && sounds.back() == (user / L"Müller.wav").lexically_normal().make_preferred().wstring(), "WAV playback receives full Unicode filename");
    Check(HudSounds::PlayFile(Utf8(root / L"custom-音.mp3")) && media.back() == L"play D2RHUD_SoundPreview", "MP3 uses a HUD-owned playback alias");
    Check(media[media.size()-2].find(L"custom-音.mp3") != std::wstring::npos, "MCI open preserves Unicode path");
    openWorks = false;
    Check(!HudSounds::PlayFile(Utf8(user / "Zeta.MP3")), "MP3 open failures reported");
    openWorks = true; playWorks = false;
    Check(!HudSounds::PlayFile(Utf8(user / "Zeta.MP3")) && media.back() == L"close D2RHUD_SoundPreview", "failed MP3 play closes newly opened device");
    playWorks = true;
    Check(HudSounds::PlayFile(Utf8(legacy / "Legacy.flac")) && shells == 1, "existing FLAC external-player fallback preserved");
    Check(!HudSounds::PlayFile(Utf8(user / "notes.txt")), "unsupported playback extension rejected");
    const auto empty = root / "empty"; std::filesystem::create_directory(empty);
    HudSounds::Configure(Utf8(empty), "Other");
    Check(HudSounds::Catalog()->empty() && HudSounds::Resolve("").empty(), "changing mod/root clears sound and selected-path caches");
    File(empty / "My Filters/sounds/New.wav");
    Check(HudSounds::Catalog()->empty(), "empty sound catalogs also cached");
    HudSounds::Invalidate();
    Check(HudSounds::Catalog()->size() == 1, "explicit refresh updates previously empty list");
    Check(!HudSounds::ExecutableDirectory().empty(), "Unicode executable-directory reader succeeds");
}
void NativeTests(const std::filesystem::path& root) {
    HudSounds::Configure(Utf8(root / "native-empty"), "Other");
    D2PlayerDataStrc data{}; strcpy_s(data.szName, "Local");
    D2UnitStrc unit{}; unit.dwUnitType = UNIT_PLAYER; unit.dwUnitId = 7; unit.pPlayerData = &data;
    localUnit = &unit;
    const auto initialReads = identityReads;
    HudChat::Tick(100, true, true, "");
    Check(HudChat::Enabled() && identityReads == initialReads + 1, "native adapter obtains local client player identity");
    const int initialBeeps = beeps;
    HudChat::Observe("Remote", "Hello", UNIT_PLAYER, 42, true);
    HudChat::Tick(100, true, true, "");
    Check(beeps == initialBeeps + 1, "eligible native alert uses system beep when no sounds exist");
    for (int i = 0; i < 100; ++i) { ++clockNow; HudChat::Tick(100, true, true, ""); }
    Check(identityReads == initialReads + 1, "identity not looked up each frame");
    HudChat::Observe("Local", "Own message", UNIT_PLAYER, 7, true);
    HudChat::Tick(100, true, true, "");
    Check(beeps == initialBeeps + 1, "own native chat suppressed");
    localUnit = nullptr;
    auto slots = reinterpret_cast<D2ClientStrc**>(Memory() + 0x1d637f0);
    D2ClientStrc host{}, client{}; strcpy_s(host.szName, "Host"); strcpy_s(client.szName, "Local");
    slots[0] = &host; slots[2] = &client; *reinterpret_cast<int32_t*>(Memory()+0x1d442d8) = 2;
    clockNow += 1000; HudChat::Tick(101, true, true, "");
    HudChat::Observe("Host", "Joiner message", UNIT_PLAYER, 1, true); HudChat::Tick(101, true, true, "");
    Check(beeps == initialBeeps + 2, "joiner fallback uses own client slot rather than host slot zero");
    HudChat::Observe("Local", "Joiner own name"); HudChat::Tick(101, true, true, "");
    Check(beeps == initialBeeps + 2, "joiner fallback suppresses own name");
    slots[2] = nullptr;
    clockNow += 1000; HudChat::Tick(102, true, true, "");
    const auto missingReads = identityReads;
    for (int i = 0; i < 100; ++i) { ++clockNow; HudChat::Tick(102, true, true, ""); }
    Check(identityReads == missingReads, "unavailable identity is also throttled instead of retried each frame");
    const auto noAccess = VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_NOACCESS);
    unit.pPlayerData = static_cast<D2PlayerDataStrc*>(noAccess); localUnit = &unit;
    clockNow += 1000; HudChat::Tick(103, true, true, "");
    HudChat::Observe("Remote", "Stale player-data pointer", UNIT_PLAYER, 42, true); HudChat::Tick(103, true, true, "");
    Check(beeps == initialBeeps + 3, "unreadable native name safely falls back to unit identity");
    const auto failedSound = root / "failed.wav"; File(failedSound);
    clockNow += 1000; wavWorks = false;
    HudChat::Observe("Remote", "Decoder failure", UNIT_PLAYER, 42, true);
    HudChat::Tick(103, true, true, Utf8(failedSound));
    Check(beeps == initialBeeps + 4, "native audio failure falls back to system beep");
    wavWorks = true;
    HudChat::Tick(103, true, false, "");
    HudChat::Observe({static_cast<char*>(noAccess),64}, {static_cast<char*>(noAccess),256});
    const auto disabledReads = identityReads;
    HudChat::Tick(0, false, false, "");
    Check(!HudChat::Enabled() && identityReads == disabledReads, "disabled/out-of-game frames perform no identity reads");
    unit.pPlayerData = &data;
    clockNow += 1000;
    HudChat::Tick(0, true, true, "");
    const auto beforeJoiner = beeps;
    HudChat::Observe("Remote", "Joiner without host token", UNIT_PLAYER, 42, true);
    HudChat::Tick(0, true, true, "");
    Check(HudChat::Enabled() && beeps == beforeJoiner + 1, "joiner notifications work with zero native session token");
    HudChat::Observe("Remote", "Leaving joiner session", UNIT_PLAYER, 42, true);
    HudChat::Tick(0, false, true, "");
    HudChat::Tick(0, true, true, "");
    Check(beeps == beforeJoiner + 1, "explicit leave boundary clears zero-token joiner messages");
    VirtualFree(noAccess, 0, MEM_RELEASE); localUnit = nullptr;
}
}
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const auto root = std::filesystem::path(argv[1]) / (L"chat-sound-ü-音-" + std::to_wstring(GetCurrentProcessId()));
    if (!std::filesystem::create_directory(root)) return 2;
    InboxTests(); LibraryTests(root); NativeTests(root);
    std::cout << "PASS: " << checks << " chat/sound regression checks. No game, audible audio, external apps or original user files were used.\n";
}
