#include <Windows.h>
#include <atomic>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>
#include "../d2rhud/plugin/D2RHUD/HudPreferences.h"

namespace {
enum class Fault { None, Write, PartialWrite, Flush, ExternalEdit };
Fault fault = Fault::None;
int writeCalls = 0;
std::filesystem::path externalPath;
BOOL WINAPI FixtureWrite(HANDLE file, LPCVOID data, DWORD count, LPDWORD written, LPOVERLAPPED overlap) {
    ++writeCalls;
    if (fault == Fault::Write || (fault == Fault::PartialWrite && writeCalls > 1)) {
        SetLastError(ERROR_DISK_FULL); return FALSE;
    }
    if (fault == Fault::PartialWrite) count = (std::min)(count, DWORD(8));
    return WriteFile(file, data, count, written, overlap);
}
BOOL WINAPI FixtureFlush(HANDLE file) {
    if (fault == Fault::Flush) { SetLastError(ERROR_WRITE_FAULT); return FALSE; }
    if (fault == Fault::ExternalEdit) {
        std::ofstream changed(externalPath, std::ios::binary);
        changed << "{\"external\":42}";
    }
    return FlushFileBuffers(file);
}
}
// Exercise the real persistence implementation, injecting only OS write faults.
#define WriteFile FixtureWrite
#define FlushFileBuffers FixtureFlush
#include "../d2rhud/plugin/D2RHUD/HudConfigStore.cpp"
#undef WriteFile
#undef FlushFileBuffers
#include "../d2rhud/plugin/D2RHUD/HudPreferences.cpp"

namespace {
int checks = 0;
void Check(bool condition, const char* description) {
    ++checks;
    if (!condition) { std::cerr << "FAILED: " << description << '\n'; std::exit(1); }
}
using Json = HudConfig::Json;
std::string Bytes(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
void Write(const std::filesystem::path& path, const std::string& value) {
    std::ofstream file(path, std::ios::binary); file << value;
}
void NoTemporaryFiles(const std::filesystem::path& directory) {
    for (const auto& file : std::filesystem::directory_iterator(directory))
        Check(file.path().filename().wstring().find(L".tmp-") == std::wstring::npos, "temporary file cleaned up");
}
void Parser() {
    Json document;
    Check(bool(HudConfig::Parse(R"json({
        // URLs, escaped quotes, comment markers and comma/bracket sequences are literal text.
        "url":"https://example.test/a//b", "path":"C:\\Mods\\HUD",
        "quote":"\" /* not a comment */", "format":", ] , }", /* real comment */
        "list":[1,2,],
    })json", document)), "comments and trailing commas accepted");
    Check(document["url"] == "https://example.test/a//b", "URL preserved");
    Check(document["path"] == R"(C:\Mods\HUD)", "backslashes preserved");
    Check(document["quote"] == "\" /* not a comment */", "escaped quote preserved");
    Check(document["format"] == ", ] , }", "comma inside string preserved");
    Check(document["list"] == Json::array({1,2}), "array trailing comma handled");
    const auto previous = document;
    for (const auto* invalid : {"[]", "null", "123", "{\"enabled\":True}", "{\"v\":1/*open", "{\"v\":1/**/2}", "{,}", "{\"x\":[,]}"}) {
        Check(!HudConfig::Parse(invalid, document), "malformed config rejected");
        Check(document == previous, "rejected parse leaves output intact");
    }
    Check(bool(HudConfig::Parse("\xEF\xBB\xBF{\"x\":true,}", document)), "UTF-8 BOM accepted");
    Json escaped = {{"quote\"\n", "control\x01"}, {"Keybinds", {{"quoted\"key", {{"Key", "\\\"\t"}, {"Enabled", false}}}}},
                    {"Commands", {{"Startup Commands", "say \"hi\"\n\x02"}, {"Custom Commands", Json::array({{{"Key", "A"}, {"Command", "say \"hello\""}}})}}}};
    Check(bool(HudConfig::Parse(HudConfig::Serialize(escaped), document)), "serializer escapes every JSON key and string");
    Check(document == escaped, "serializer is lossless, including Enabled=false");
    Check(HudConfig::Serialize(Json::object()) == "{\n\n}\n", "empty object serialization valid");
}
void Preferences() {
    using namespace HudPreferences;
    Config config;
    Check(config.ChatNotificationVolume==30,"notification sounds default to a quieter 30 percent");
    Load(config,{{"ChatNotificationVolume",7}},false);Json audio;Save(config,audio);Config audioRestored;Load(audioRestored,audio,false);
    Check(audioRestored.ChatNotificationVolume==7,"notification gain persists independently of game/system volume");
    Load(audioRestored,{{"ChatNotificationVolume",-50}},false);Check(audioRestored.ChatNotificationVolume==0,"negative notification volume clamps to mute");
    Load(audioRestored,{{"ChatNotificationVolume",500}},false);Check(audioRestored.ChatNotificationVolume==100,"notification volume cannot amplify beyond full scale");
    Check(config.MultipleAurasPerItem,"multiple item auras default on");
    Load(config,{{"MultipleAurasPerItem",false}},false);
    Json auraDocument=Json::object();Save(config,auraDocument);Config auraRestored;Load(auraRestored,auraDocument,false);
    Check(!auraRestored.MultipleAurasPerItem,"multiple item auras disabled preference survives roundtrip");
    Load(auraRestored,{{"MultipleAurasPerItem",true}},false);
    Check(auraRestored.MultipleAurasPerItem,"multiple item auras can be enabled independently");
    Check(config.Inspect,"Inspect is enabled by default");
    Check(!config.InspectLabels && config.InspectIconHeight==96 && config.InspectLabelSize==22,"Inspect defaults to raised icons without labels");
    Load(config,{{"InspectLabels",true},{"InspectIconHeight",128},{"InspectLabelSize",30}},false);
    Json inspectDocument=Json::object();Save(config,inspectDocument);Config inspectRestored;
    Load(inspectRestored,inspectDocument,false);
    Check(inspectRestored.InspectLabels && inspectRestored.InspectIconHeight==128 && inspectRestored.InspectLabelSize==30,"Inspect appearance survives config roundtrip");
    Load(inspectRestored,{{"InspectIconHeight",-10},{"InspectLabelSize",1000}},false);
    Check(inspectRestored.InspectIconHeight==0 && inspectRestored.InspectLabelSize==40,"Inspect appearance bounds are enforced");
    Load(config, {{"Inspect",false}}, false);
    Check(!config.Inspect,"Inspect can be disabled independently of the mod");
    Load(config, {{"Inspect",true}}, false);
    Load(config, Json::object(), true);
    Check(config.MultipleUseitemOutputs && config.MultipleUseitemInputs && config.ExtendedCubeConditions && config.BulkCubeOutputs && config.ExtendedCubeOutputs,
        "all RMD cube defaults enabled");
    Load(config, {{"BulkCubeOutputs", false}}, true);
    Check(!config.BulkCubeOutputs && config.ExtendedCubeOutputs, "explicit preference overrides RMD default");
    Load(config, Json::object(), false);
    Check(!config.MultipleUseitemOutputs && !config.ExtendedCubeOutputs, "other mods retain opt-in defaults");
    Load(config, {{"MonsterStatsDisplay", "bad"}, {"ChatSounds", false}, {"HPRollover%", UINT64_MAX},
        {"CameraLastPresetIndex", 1000}, {"DLLsToLoad", Json::array({"a.dll", 5, "b.dll"})},
        {"CameraPresets", Json::array({nullptr, {{"Name", "Second"}, {"Pitch", 1.5}, {"Zoom", "bad"}},
            {{"Name", "Third"}, {"Height", 1e200}, {"HasValues", true}}})}}, false);
    Check(config.MonsterStatsDisplay && !config.ChatSounds, "one invalid preference does not block subsequent options");
    Check(config.HPRolloverPercent == 99 && config.CameraLastPresetIndex == 0, "out-of-range integers retain safe values");
    Check(config.DLLsToLoad == std::vector<std::string>({"a.dll", "b.dll"}), "malformed DLL entries isolated");
    Check(config.CameraPresets[1].Name == "Second" && config.CameraPresets[1].Pitch == 1.5f && config.CameraPresets[1].Zoom == 0,
        "invalid camera property isolated without shifting slots");
    Check(config.CameraPresets[2].Name == "Third" && config.CameraPresets[2].Height == 0, "float overflow rejected");
    config.ChatSoundPath.clear();
    config.CameraLastPresetIndex = 3;
    Json document = {{"ChatSoundPath", "old.wav"}, {"Localization", {{"Language", "deDE"}}},
        {"Unique Items", {"The Grandfather"}}, {"CameraPresets", Json::array({{{"extension", 10}}})}};
    Save(config, document);
    Check(document["ChatSoundPath"] == "", "cleared sound path does not reappear");
    Check(document["CameraPresets"].size() == 3 && document["CameraPresets"][0]["HasValues"] == false,
        "empty leading camera slot retained");
    Check(document["CameraPresets"][0]["extension"] == 10 && document.contains("Localization") && document.contains("Unique Items"),
        "unrelated top-level and per-slot fields preserved");
    Config restored;
    Load(restored, document, false);
    Check(restored.CameraPresets[1].Name == "Second" && restored.CameraPresets[2].Name == "Third" && restored.CameraLastPresetIndex == 3,
        "sparse camera slots and selected index survive reload");
    Json resaved = document; Save(restored, resaved);
    Check(resaved == document, "preference load/save is stable");

    Json keyDocument = {{"Keybinds", {{"Transmute", {{"Key", "VK_T"}, {"Enabled", false}, {"Author", "mod"}}}, {"Bad", nullptr}}},
        {"Commands", {{"extension", 7}, {"Startup Commands", "players 8"},
            {"Custom Commands", Json::array({nullptr, {{"Key", "F2"}, {"Command", "save 1"}, {"extension", "keep"}}})}}}};
    auto bindings = LoadBindings(keyDocument);
    Check(!bindings.hotkeys.contains("Inspect"),"Inspect uses eye icons without a hotkey");
    auto disabledInspect=keyDocument;disabledInspect["Keybinds"]["Inspect"]={{"Key",""}};
    Check(!LoadBindings(disabledInspect).hotkeys.contains("Inspect"),"obsolete Inspect bindings are removed");
    Check(bindings.hotkeys["Transmute"].second == "Disabled" && !bindings.hotkeys.contains("Bad"), "legacy disabled state retained");
    Check(bindings.commands.size() == 6 && bindings.commands[0].key.empty() && bindings.commands[1].key == "F2", "invalid command slots do not shift valid entries");
    Check(!bindings.hotkeys.contains("Open Marketplace"),"marketplace shortcut starts unbound on existing configs");
    bindings.hotkeys["Open Marketplace"]={"VK_CONTROL + VK_M",""};
    bindings.hotkeys["Transmute"].first.clear();
    bindings.commands[1].command = "say \"hello\"\n\x01";
    SaveBindings(bindings, keyDocument);
    Check(keyDocument["Keybinds"]["Transmute"]["Key"] == "" && keyDocument["Keybinds"]["Transmute"]["Enabled"] == false,
        "clearing key preserves false enable flag");
    Check(keyDocument["Keybinds"]["Transmute"]["Author"] == "mod" && keyDocument["Commands"]["extension"] == 7 &&
        keyDocument["Commands"]["Custom Commands"][1]["extension"] == "keep", "nested binding extensions preserved");
    const auto reloaded = LoadBindings(Json::parse(HudConfig::Serialize(keyDocument)));
    Check(reloaded.hotkeys.at("Transmute").first.empty() && reloaded.commands[1].command == bindings.commands[1].command, "bindings round-trip correctly");

    Check(reloaded.hotkeys.at("Open Marketplace").first=="VK_CONTROL + VK_M","marketplace modifier chord survives save/reload");

    BindingState live;
    live.Publish(bindings);
    const auto retained = live.Read();
    const auto oldKey = FindBinding(*retained, "Transmute");
    bindings.hotkeys["Transmute"].first = "F4";
    live.Publish(bindings);
    Check(oldKey && oldKey->first.empty() && FindBinding(*live.Read(), "Transmute")->first == "F4",
        "new binding publishes immediately while an active callback retains its previous snapshot");
    bindings.hotkeys["Transmute"].first.clear(); live.Publish(bindings);
    Check(FindBinding(*live.Read(), "Transmute")->first.empty(), "clearing key immediately reaches input callbacks");
    std::atomic<bool> coherent{true};
    std::thread reader([&] {
        for (int i = 0; i < 2000; ++i) {
            const auto snapshot = live.Read();
            const auto key = FindBinding(*snapshot, "Transmute");
            if (!key || (key->first != "" && key->first != "F4")) coherent = false;
        }
    });
    for (int i = 0; i < 2000; ++i) {
        bindings.hotkeys["Transmute"].first = i % 2 ? "F4" : "";
        live.Publish(bindings);
    }
    reader.join();
    Check(coherent, "concurrent binding publication never exposes a mutating map");
}
void Persistence(const std::filesystem::path& root) {
    const auto target = root/L"preferences-übersetzung.json";
    auto result = HudConfig::Update(target, [](Json& data) { data["unknown"] = {{"keep", 42}}; data["count"] = 0; });
    Check(result.ok && result.changed, "new Unicode path created");
    Json document;
    Check(bool(HudConfig::Read(target, document)) && document["unknown"]["keep"] == 42, "new file readable");
    const auto unchangedBytes = Bytes(target);
    const auto unchangedTime = std::filesystem::last_write_time(target);
    result = HudConfig::Update(target, [](Json& data) { data["count"] = 0; });
    Check(result.ok && !result.changed && Bytes(target) == unchangedBytes && std::filesystem::last_write_time(target) == unchangedTime,
        "unchanged save skips physical write");
    for (const auto failure : {Fault::Write, Fault::PartialWrite, Fault::Flush}) {
        fault = failure; writeCalls = 0;
        result = HudConfig::Update(target, [](Json& data) { data["count"] = 1; });
        fault = Fault::None;
        Check(!result && !result.error.empty(), "write/partial-write/flush failure reported");
        Check(Bytes(target) == unchangedBytes, "failed write keeps original bytes intact");
        NoTemporaryFiles(root);
    }
    HANDLE locked = CreateFileW(target.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    Check(locked != INVALID_HANDLE_VALUE, "replacement lock fixture opened");
    result = HudConfig::Update(target, [](Json& data) { data["count"] = 2; });
    CloseHandle(locked);
    Check(!result && Bytes(target) == unchangedBytes, "replacement failure keeps original intact");
    NoTemporaryFiles(root);
    locked = CreateFileW(target.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    result = HudConfig::Update(target, [](Json& data) { data["count"] = 2; });
    CloseHandle(locked);
    Check(!result && Bytes(target) == unchangedBytes, "unreadable existing file never overwritten");
    fault = Fault::ExternalEdit; externalPath = target;
    result = HudConfig::Update(target, [](Json& data) { data["count"] = 3; });
    fault = Fault::None;
    Check(!result && Bytes(target) == "{\"external\":42}", "external concurrent edit detected and kept");
    NoTemporaryFiles(root);

    for (const auto* malformed : {"{\"broken\":", "[]", "{\"flag\":True}"}) {
        Write(target, malformed);
        result = HudConfig::Update(target, [](Json& data) { data["count"] = 0; });
        Check(!result && Bytes(target) == malformed, "malformed existing file preserved exactly");
    }
    Write(target, "{/*comment*/\"url\":\"https://example.test\",\"count\":0,}");
    Check(bool(HudConfig::Update(target, [](Json& data) { data["count"] = 1; })), "commented existing file can be updated");
    Check(Json::parse(Bytes(target))["url"] == "https://example.test", "URL survives actual disk save");
    const auto beforeException = Bytes(target);
    result = HudConfig::Update(target, [](Json& data) { data["count"] = 9; throw std::runtime_error("fixture"); });
    Check(!result && Bytes(target) == beforeException, "snapshot exception preserves existing file");
    result = HudConfig::Update(target, [](Json& data) { data = nullptr; });
    Check(!result && Bytes(target) == beforeException, "invalid edited root never published");

    std::atomic<int> failures{0};
    std::vector<std::thread> threads;
    for (int thread = 0; thread < 4; ++thread) threads.emplace_back([&] {
        for (int update = 0; update < 12; ++update)
            if (!HudConfig::Update(target, [](Json& data) { data["count"] = data["count"].get<int>() + 1; })) ++failures;
    });
    for (auto& thread : threads) thread.join();
    Check(failures == 0 && HudConfig::Read(target, document) && document["count"] == 49, "concurrent HUD updates serialized without lost edits");
    Check(document["url"] == "https://example.test", "concurrent updates preserve unrelated data");
    NoTemporaryFiles(root);
}
}
int main(int argc, char** argv) {
    if (argc < 2) return 2;
    // Only use a fresh child directory of the caller's test-output directory.
    const auto root = std::filesystem::path(argv[1]) / ("config-fixture-" + std::to_string(GetCurrentProcessId()));
    if (!std::filesystem::create_directory(root)) return 2;
    Parser(); Preferences(); Persistence(root);
    for (int i = 2; i < argc; ++i) {
        const auto path = std::filesystem::u8path(argv[i]);
        const auto before = Bytes(path);
        Json source;
        Check(bool(HudConfig::Read(path, source)), "installed configuration parses through shared reader");
        HudPreferences::Config config;
        HudPreferences::Load(config, source, true);
        auto edited = source;
        HudPreferences::Save(config, edited);
        HudPreferences::SaveBindings(HudPreferences::LoadBindings(source), edited);
        Json roundTrip;
        Check(bool(HudConfig::Parse(HudConfig::Serialize(edited), roundTrip)) && roundTrip == edited,
            "installed preferences serialize and parse without loss");
        for (const char* untouched : {"Unique Items", "MemoryConfigs", "Localization", "WindowLayouts"})
            if (source.contains(untouched)) Check(edited[untouched] == source[untouched], "unowned installed section preserved");
        Check(Bytes(path) == before, "real configuration validation makes no disk changes");
    }
    std::cout << "HUD configuration: " << checks << " checks passed (real Win32 file replacement, injected write failures, sparse presets and binding compatibility).\n";
}
