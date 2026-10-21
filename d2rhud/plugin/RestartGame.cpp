#include "RestartGame.h"
#include "RestartGameData.h"
#include "../Pattern.h"
#include "../d2/Reclass.h"
#include <MinHook.h>
#include <array>
#include <atomic>
#include <cstring>

namespace RestartGame {
namespace {
template<class T> T Native(uint32_t rva) { return reinterpret_cast<T>(Pattern::Address(rva)); }
template<class T> bool Read(uintptr_t p, T& value) {
    SIZE_T size = 0;
    return p && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(p), &value, sizeof(value), &size) && size == sizeof(value);
}
// The local difficulty callback c1f60a calls this loader. The c1e9c0
// CharacterSelectPanel request wrapper belongs to the online path.
struct Selection { uint32_t list = 1, padding = 0; uint64_t index = 0; };
using Start = void(__fastcall*)(int, const Selection*);
struct PanelMessage { uint64_t type, action; uintptr_t data; uint8_t hasData; };
using PanelHandler = void(__fastcall*)(void*, const PanelMessage*);
using PauseOpen = void(__fastcall*)(void*);
PanelHandler originalPanelHandler = nullptr;
PauseOpen originalPauseOpen = nullptr;
std::atomic<void(*)()> saveAndExit{nullptr};
std::atomic<bool> installed{false};
std::atomic<const char*> status{"Restart Game is not installed."};
constexpr UINT_PTR TimerId = 0xd2a1;
HWND timerWindow = nullptr;
Data::Flow flow;
void(*continuation)(bool)=nullptr;
void Finished(bool success){auto callback=continuation;continuation=nullptr;if(callback)callback(success);}
struct Session {
    std::array<char, 16> name{};
    int difficulty = 0;
    bool valid = false;
} restarting;

bool Offline() {
    // Live offline mode is zero; mode 1 is the online character array. This
    // field is distinct from the adjacent game-type setting at 1bfa318.
    int mode = 0;
    return Read(Pattern::Address(0x1bfa31c), mode) && mode == 0;
}
bool Selected(std::array<char, 16>& name, uint16_t* flags = nullptr) {
    uint64_t index = 0, count = 0; uintptr_t entries = 0, entry = 0;
    uint8_t selected = 0;
    if (!Offline() || !Read(Pattern::Address(0x1ca10a8), selected) || !selected ||
        !Read(Pattern::Address(0x1ca10a0), index) ||
        !Read(Pattern::Address(0x1ca0fe0), count) || !count || count > 100000 || index >= count ||
        !Read(Pattern::Address(0x1ca0fd8), entries) || !entries ||
        !Read(entries + index * sizeof(uintptr_t), entry) || !entry ||
        !Read(entry + 0x10, name) || !name[0]) return false;
    if (flags && !Read(entry + 0x122, *flags)) return false;
    return memchr(name.data(), 0, name.size()) != nullptr;
}
bool SameName(const std::array<char, 16>& a, const std::array<char, 16>& b) {
    return _stricmp(a.data(), b.data()) == 0;
}
void* Frontend() {
    uintptr_t manager = 0;
    if (!Read(Pattern::Address(0x1d7c4e8), manager) || !manager) return nullptr;
    // The HD frontend's instance is MainMenuPanel, even though its entry
    // handler is implemented by the CharacterSelectPanel base class.
    auto find = Native<void*(__fastcall*)(void*, const char*)>(0x597320);
    auto panel = find(reinterpret_cast<void*>(manager), "MainMenuPanel");
    if (!panel) panel = find(reinterpret_cast<void*>(manager), "CharacterSelectPanel");
    uint8_t visible = 0;
    return panel && Read(reinterpret_cast<uintptr_t>(panel) + 0x51, visible) && visible ? panel : nullptr;
}
bool LiveSession(Session& session) {
    uint8_t clientState = 0,lanHost=0; uintptr_t client = 0;
    if (!Read(Pattern::Address(0x1dc76f8), clientState) || clientState != 1 || !Offline() ||
        // Same TCP/IP hosting marker used by the existing LAN lobby reader.
        // A one-player TCP/IP host must still be excluded.
        !Read(Pattern::Address(0x18882e8),lanHost) || lanHost==2 ||
        !Read(Pattern::Address(0x1d637f0), client) || !client) {
        status.store("Restart unavailable: no active offline server session."); return false;
    }
    D2ClientStrc c{}; D2GameStrc game{};
    if (!Read(client, c) || !c.pGame || c.dwClientState != 4 ||
        !Read(reinterpret_cast<uintptr_t>(c.pGame), game) || game.nClients != 1 || game.nDifficulty > 2) {
        status.store("Restart unavailable: the local server or difficulty is not ready."); return false;
    }
    // Read the playing hero directly. Frontend records are allowed to be
    // released while playing and are checked again after the native refresh.
    uint32_t selector = 8;
    if (!Read(Pattern::Address(0x1d442d8), selector) || selector >= 8) {
        status.store("Restart unavailable: the local player selector is not ready."); return false;
    }
    auto hero = Native<D2UnitStrc*(__fastcall*)(uint32_t)>(0x6b050)(selector);
    D2UnitStrc u{}; D2PlayerDataStrc player{};
    if (!Read(reinterpret_cast<uintptr_t>(hero), u) || u.dwUnitType || u.dwUnitId != c.dwUnitGUID || !u.pPlayerData ||
        !Read(reinterpret_cast<uintptr_t>(u.pPlayerData), player) ||
        memchr(player.szName, 0, sizeof(player.szName)) == nullptr ||
        memchr(c.szName, 0, sizeof(c.szName)) == nullptr || _stricmp(player.szName, c.szName)) {
        status.store("Restart unavailable: the local client and playing hero do not match."); return false;
    }
    memcpy(session.name.data(), player.szName, session.name.size());
    session.difficulty = game.nDifficulty;
    session.valid = true;
    return true;
}
void UpdateRestartButton(void* pause){
    Session session;const bool visible=LiveSession(session);
    auto find=Native<void*(__fastcall*)(void*,const char*)>(0x576070);
    // Desktop layouts wrap the button in a table row; controller layouts may
    // use only the button. Hide the row too so it cannot retain a hit target.
    for(const char* name:{"Row Restart Game","RestartGame"})if(auto widget=find(pause,name)){
        Native<void(__fastcall*)(void*,bool)>(0x10f9c0)(widget,visible);
        Native<void(__fastcall*)(void*,bool)>(0x10f9d0)(widget,visible);
    }
}
void __fastcall PauseOpenHook(void* pause){originalPauseOpen(pause);UpdateRestartButton(pause);}
void __fastcall PanelHook(void* panel, const PanelMessage* message) {
    PanelMessage copy{};
    constexpr char command[] = "D2RHUDRestartGame";
    std::array<char, sizeof(command)> name{};
    if (Read(reinterpret_cast<uintptr_t>(message), copy) &&
        copy.type == 0x7127a406e0b6452cULL && copy.action == 0x723d40e52f74e229ULL &&
        copy.hasData && Read(copy.data, name) && !memcmp(name.data(), command, sizeof(command))) {
        // Use a registered native message with a string argument. Unknown
        // PausePanelMessage actions are discarded by the layout message parser.
        if (auto handler = saveAndExit.load()) { if (Request()) handler(); }
        else status.store("Restart unavailable: the HUD save/exit handler is not ready.");
        OutputDebugStringA(Status());
        return;
    }
    originalPanelHandler(panel, message);
}
BOOL CALLBACK FindWindow(HWND hwnd, LPARAM parameter) {
    DWORD pid = 0; GetWindowThreadProcessId(hwnd, &pid);
    if (pid != GetCurrentProcessId() || GetWindow(hwnd, GW_OWNER) || !IsWindowVisible(hwnd)) return TRUE;
    *reinterpret_cast<HWND*>(parameter) = hwnd; return FALSE;
}
void StopTimer() {
    if (timerWindow) KillTimer(timerWindow, TimerId);
    timerWindow = nullptr;
}
VOID CALLBACK Timer(HWND hwnd, UINT, UINT_PTR id, DWORD) {
    if (id != TimerId || hwnd != timerWindow) return;
    uint8_t state = 0; uintptr_t server = 0;
    if (!Read(Pattern::Address(0x1dc76f8), state) || !Read(Pattern::Address(0x1d637f0), server)) {
        flow.Reset(); StopTimer(); Finished(false); status.store("Restart cancelled: game state is unavailable."); return;
    }
    void* panel = nullptr; std::array<char, 16> selected{}; uint16_t flags = 0;
    const bool ready = state != 1 && !server && (panel = Frontend()) && Selected(selected, &flags);
    if (ready && (flags & 0x0c) == 0x0c) {
        flow.Reset(); StopTimer(); Finished(false); status.store("Restart cancelled: this hardcore character has died."); return;
    }
    const auto action = flow.Tick(GetTickCount64(), state == 1, !server, ready, ready && SameName(selected, restarting.name));
    if (action == Data::Action::Start) {
        Selection choice;
        if (!Read(Pattern::Address(0x1ca10a0), choice.index)) {
            flow.Reset(); StopTimer(); Finished(false); status.store("Restart cancelled: offline selection is unavailable."); return;
        }
        status.store("Starting a fresh game with the saved character.");
        // Same loader and selection descriptor as the local difficulty callback.
        // It reads the saved character and starts normal offline initialization.
        Native<Start>(0x418990)(restarting.difficulty, &choice);
    } else if (action == Data::Action::Complete) {
        StopTimer(); Finished(true); status.store("Restart Game is available for offline sessions.");
    } else if (action == Data::Action::Cancel) {
        StopTimer(); Finished(false); status.store("Restart cancelled: character selection changed or the new game did not become ready.");
    }
}
uint64_t Hash(const std::array<uint8_t, 32>& bytes) {
    uint64_t hash = 14695981039346656037ULL;
    for (auto byte : bytes) hash = (hash ^ byte) * 1099511628211ULL;
    return hash;
}
}

void Install() {
    if (installed.load()) return;
    for (auto [rva, hash] : std::array<std::pair<uint32_t, uint64_t>, 9>{{
        {0xbc88c0, 0xdd280ec18b6abcb2ULL}, {0x418990, 0x4fc93f485f4dcecdULL},
        {0x597320, 0xb095715269b79a39ULL}, {0x6b050, 0x90869c3b9cbd1b2fULL},
        {0x5959e0, 0x89968e8b494450eeULL},
        {0xbc8530,0x06af427e9be2800dULL},{0x576070,0x06e32db29337f320ULL},
        {0x10f9c0,0xca7eed56bfef7c3cULL},{0x10f9d0,0xa1e1ee6b83822874ULL}}}) {
        std::array<uint8_t, 32> bytes{};
        if (!Read(Pattern::Address(rva), bytes) || Hash(bytes) != hash) {
            status.store("Restart Game is unavailable: native game-entry signatures do not match."); return;
        }
    }
    auto result = MH_Initialize();
    if (result != MH_OK && result != MH_ERROR_ALREADY_INITIALIZED) return;
    auto panelTarget = Native<void*>(0x5959e0);
    auto pauseTarget = Native<void*>(0xbc8530);
    if (MH_CreateHook(panelTarget, reinterpret_cast<void*>(PanelHook), reinterpret_cast<void**>(&originalPanelHandler)) != MH_OK) return;
    if(MH_CreateHook(pauseTarget,reinterpret_cast<void*>(PauseOpenHook),reinterpret_cast<void**>(&originalPauseOpen))!=MH_OK){
        MH_RemoveHook(panelTarget);originalPanelHandler=nullptr;return;
    }
    if (MH_EnableHook(panelTarget) != MH_OK || MH_EnableHook(pauseTarget)!=MH_OK) {
        MH_DisableHook(panelTarget);MH_DisableHook(pauseTarget);MH_RemoveHook(panelTarget);MH_RemoveHook(pauseTarget);
        originalPanelHandler = nullptr;originalPauseOpen=nullptr;return;
    }
    installed.store(true); status.store("Restart Game is available for offline sessions.");
}
void SetSaveAndExitHandler(void(*handler)()) { saveAndExit.store(handler); }
bool Request() {
    if (!installed.load() || flow.phase != Data::Phase::Idle) return false;
    Session session;
    if (!LiveSession(session)) return false;
    HWND window = nullptr; EnumWindows(FindWindow, reinterpret_cast<LPARAM>(&window));
    if (!window || !flow.Begin(GetTickCount64())) return false;
    timerWindow = window;
    if (!SetTimer(window, TimerId, 50, Timer)) { flow.Reset(); timerWindow = nullptr; return false; }
    restarting = session; status.store("Saving and closing the current game before restarting."); return true;
}
bool AfterCredits(const char* name,int difficulty,void(*completed)(bool)) {
    uint8_t lanHost=0;
    if(!installed.load() || flow.phase!=Data::Phase::Idle || !Offline() || !name || !name[0] ||
       strnlen_s(name,16)>=16 || difficulty<0 || difficulty>2 ||
       !Read(Pattern::Address(0x18882e8),lanHost) || lanHost==2)return false;
    HWND window=nullptr;EnumWindows(FindWindow,reinterpret_cast<LPARAM>(&window));
    if(!window || !flow.Begin(GetTickCount64()))return false;
    timerWindow=window;
    if(!SetTimer(window,TimerId,50,Timer)){flow.Reset();timerWindow=nullptr;return false;}
    restarting={};strcpy_s(restarting.name.data(),restarting.name.size(),name);
    restarting.difficulty=difficulty;restarting.valid=true;continuation=completed;
    status="Returning to the same character and difficulty after the credits.";return true;
}
void Cancel() { flow.Reset(); StopTimer(); Finished(false); }
void Uninstall() {
    Cancel();
    if (installed.exchange(false))for(uint32_t rva:{0x5959e0u,0xbc8530u}){MH_DisableHook(Native<void*>(rva));MH_RemoveHook(Native<void*>(rva));}
    originalPauseOpen=nullptr;
    originalPanelHandler = nullptr; saveAndExit.store(nullptr);
}
const char* Status() { return status.load(); }
}
