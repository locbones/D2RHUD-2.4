#include "D3Whirlwind.h"
#include "D3WhirlwindData.h"
#include "D3WhirlwindNative.h"
#include "D3WhirlwindPickup.h"
#include "D3WhirlwindPotion.h"
#include "D3WhirlwindRestart.h"
#include "../d2/D2Ptrs.h"
#include <MinHook.h>
#include <atomic>
#include <map>
#include <mutex>

namespace D3Whirlwind {
namespace {
using Unit = D2UnitStrc;
using Game = D2GameStrc;
using Client = D2ClientStrc;
using namespace NativeData;
using ServerSkill = int(__fastcall*)(Game*, Unit*, int, int);
using ClientSkill = int(__fastcall*)(Unit*, int);
using ServerMovement = int(__fastcall*)(Game*, Unit*);
using ClientMovement = int(__fastcall*)(Unit*, bool);
using ClientAction = void(__fastcall*)(int, Unit*, int, int);
using ClientInteraction = int(__fastcall*)(Unit*, int, int, int, uint16_t);
using ItemDisabled = int(__fastcall*)(Unit*);
ServerSkill originalServerStart = nullptr, originalServerDo = nullptr;
ClientSkill originalClientStart = nullptr, originalClientDo = nullptr;
ServerMovement originalServerMove = nullptr;
ClientMovement originalClientMove = nullptr;
ClientAction originalClientAction = nullptr;
ClientInteraction originalClientInteraction = nullptr;
ItemDisabled originalItemDisabled = nullptr;
Pickup::Dispatcher originalDispatcher = nullptr;
std::atomic<bool> ready{false}, enabled{false}, inputBlocked{true};
std::atomic<bool> rightClickPickupDisabled{false};
std::atomic<uint64_t> inputStamp{0};
std::array<std::atomic<uint64_t>, 2> itemClicks{};
std::atomic<uint32_t> clickSequence{0};
std::atomic<const char*> status{"D3 style Whirlwind is not installed."};
std::recursive_mutex serverMutex;
struct ServerChannel {
    uint32_t seed = 0, nextCharge = 0;
    void* skill = nullptr;
    int id = -1, level = 0;
    Data::Lease lease;
    Data::Point lastCursor;
    uint64_t created = 0;
    bool steered = false;
};
using Key = std::pair<Game*, uint32_t>;
std::map<Key, ServerChannel> channels;
// Native client callbacks run on the client's simulation thread. The render
// thread only publishes capture state and never writes unit/path memory.
struct ClientChannel {
    Unit* player = nullptr;
    void* skill = nullptr;
    uint32_t seed = 0, sequence = 0;
    int id = -1, button = 0;
    uint64_t sent = 0, itemClick = 0;
    Data::Point cursor, lastCursor, sentCursor;
    bool active = false, steered = false, pending = false;
} client;
thread_local unsigned clientActionDepth = 0;

Unit* LocalPlayer() {
    const auto selector = Read<uint32_t>(At<void*>(0x1d442d8));
    return selector < 8 ? At<Unit*(__fastcall*)(uint32_t)>(0x6b050)(selector) : nullptr;
}
bool LocalSession(Unit* player, uint32_t& seed) {
    if (!player) return false;
    auto current = Read<Client*>(At<void*>(0x1d637f0));
    for (size_t n = 0; current && n < 64; ++n) {
        Client copy{}; SIZE_T size = 0;
        // ReClass includes a tail larger than this build's allocation. Only
        // read the verified prefix through the linked-list next pointer.
        constexpr size_t prefix = offsetof(Client, pNext) + sizeof(Client*);
        if (!ReadProcessMemory(GetCurrentProcess(), current, &copy, prefix, &size) || size != prefix) return false;
        if (copy.dwClientState == 4 && copy.pGame && copy.dwUnitGUID == player->dwUnitId) {
            seed = Read<uint32_t>(copy.pGame, offsetof(Game, dwInitSeed)); return true;
        }
        if (copy.pNext == current) break;
        current = copy.pNext;
    }
    return false;
}
void* Skill(Unit* unit) { return Read<void*>(Read<void*>(unit, 0x100), 0x18); }
int SkillId(void* skill) { return Read<int16_t>(Read<void*>(skill)); }
bool Whirlwind(void* skill) {
    const auto row = Read<void*>(skill);
    return row && Read<int16_t>(row, 0x4c) == 38 && Read<int16_t>(row, 0x4e) == 76;
}
Data::Point Position(Unit* unit) {
    const auto path = Read<void*>(unit, 0x38);
    return {Read<uint16_t>(path, 2), Read<uint16_t>(path, 6)};
}
bool Down(int button) {
    const bool swapped = GetSystemMetrics(SM_SWAPBUTTON) != 0;
    const int key = (button == 1) != swapped ? VK_LBUTTON : VK_RBUTTON;
    return (GetAsyncKeyState(key) & 0x8000) != 0;
}
bool InputAllowed() {
    DWORD process = 0;
    const auto window = GetForegroundWindow();
    if (!window || !GetWindowThreadProcessId(window, &process) || process != GetCurrentProcessId()) return false;
    // Native panel IDs differ from the legacy D2C_UIvars enum. MiniMenu (20)
    // is the ordinary bottom HUD strip, not a modal menu: it remains visible
    // throughout normal gameplay. Only actual interactive panels block here;
    // HUD hit testing is handled by the renderer's inputBlocked flag below.
    const auto panels = Read<std::array<uint8_t, 31>>(At<void*>(0x1d53fb8));
    for (const int panel : {1, 2, 3, 4, 5, 9, 11, 13, 14, 15, 19, 21, 22, 23, 24, 25, 27, 30})
        if (panels[panel]) return false;
    const auto now = GetTickCount64(), stamp = inputStamp.load();
    return stamp && now >= stamp && now - stamp < 500 && !inputBlocked.load();
}
bool Cursor(Data::Point& point) {
    point = {Read<int>(At<void*>(0x1d60a38)), Read<int>(At<void*>(0x1d60a3c))};
    return At<bool(__fastcall*)(int*, int*)>(0x6e5e0)(&point.x, &point.y) &&
        point.x > 0 && point.x <= 65535 && point.y > 0 && point.y <= 65535;
}
void SendControl(bool held) {
    if (!client.player) return;
    Data::Command command;
    command.held = held ? 1 : 0; command.recipient = client.player->dwUnitId;
    command.session = client.seed; command.skill = client.id;
    command.x = client.cursor.x; command.y = client.cursor.y; command.sequence = ++client.sequence;
    At<void(__fastcall*)(const void*, int)>(0x10dce0)(&command, sizeof(command));
    client.sent = GetTickCount64(); client.sentCursor = client.cursor; client.pending = false;
}
void StopClient(bool finish = true) {
    if (!client.active) return;
    SendControl(false);
    if (finish && client.player && Skill(client.player) == client.skill) Finish(client.skill);
    client.active = false;
}
bool Held(Unit* unit) {
    if (!client.active || client.player != unit) return false;
    uint32_t seed = 0;
    const auto skills = Read<void*>(unit, 0x100);
    const auto selected = Read<void*>(skills, client.button == 1 ? 8 : 0x10);
    if (!ready.load() || !enabled.load() || !Alive(unit) || Skill(unit) != client.skill ||
        !Active(client.skill) || !InputAllowed() || !Down(client.button) || SkillId(selected) != client.id ||
        !LocalSession(unit, seed) || seed != client.seed || !Cursor(client.cursor)) {
        StopClient(); return false;
    }
    // Cursor changes are input, not a heartbeat: publish them on the first
    // native update which sees them, including during the same 40-ms interval.
    if (!clientActionDepth && Data::ControlDue(client.cursor, client.sentCursor, GetTickCount64(), client.sent, client.pending)) SendControl(true);
    const auto click = itemClicks[client.button == 1 ? 1 : 0].load();
    if (click != client.itemClick) {
        client.itemClick = click;
        if (client.button != 1 || !rightClickPickupDisabled.load()) Pickup::ClientRequest(unit, uint32_t(click));
    }
    return true;
}
bool Steer(Unit* unit, void* skill, Data::Point cursor, Data::Point& previous, bool& steered, bool server) {
    const auto here = Position(unit);
    if (Data::Stationary(here, cursor)) return true;
    const auto path = Read<void*>(unit, 0x38);
    const int index = Read<int>(path, 0x30), count = Read<int>(path, 0x34);
    if (steered && cursor.x == previous.x && cursor.y == previous.y && index + 3 < count) return true;
    const auto goal = Data::Target(here, cursor);
    if (!Retarget(unit, skill, goal.x, goal.y, server)) return false;
    previous = cursor; steered = true;
    return true;
}
ServerChannel* Channel(Game* game, Unit* unit) {
    if (!game || !unit || unit->dwUnitType != 0) return nullptr;
    auto found = channels.find({game, unit->dwUnitId});
    if (found == channels.end()) return nullptr;
    auto& channel = found->second;
    if (channel.seed != uint32_t(game->dwInitSeed) || channel.skill != Skill(unit) || !Active(channel.skill)) {
        channels.erase(found); return nullptr;
    }
    return &channel;
}
bool Holding(Game* game, Unit* unit, ServerChannel& channel) {
    return ready.load() && enabled.load() && Alive(unit) && Whirlwind(channel.skill) &&
        channel.seed == uint32_t(game->dwInitSeed) && channel.lease.Active(GetTickCount64());
}
int __fastcall ServerStartHook(Game* game, Unit* unit, int id, int level) {
    const int result = originalServerStart(game, unit, id, level);
    if (result && ready.load() && enabled.load() && game && Alive(unit)) {
        auto skill = Skill(unit);
        if (Active(skill) && Whirlwind(skill) && SkillId(skill) == id && Read<int>(skill, 0x44) == -1) {
            std::lock_guard lock(serverMutex);
            const auto now = GetTickCount64();
            for (auto at = channels.begin(); at != channels.end();) {
                if (now - at->second.created > 60000 && !at->second.lease.Active(now)) at = channels.erase(at); else ++at;
            }
            auto& channel = channels[{game, unit->dwUnitId}];
            channel = {}; channel.seed = uint32_t(game->dwInitSeed); channel.skill = skill;
            channel.id = id; channel.level = level; channel.nextCharge = game->dwGameFrame + Data::ManaInterval;
            channel.created = now;
        }
    }
    return result;
}
int __fastcall ServerDoHook(Game* game, Unit* unit, int id, int level) {
    {
        std::lock_guard lock(serverMutex);
        if (auto channel = Channel(game, unit); channel && channel->lease.accepted) {
            if (!Holding(game, unit, *channel) || (Read<uint32_t>(channel->skill, 0x14) & 2)) { channel->lease.stopped = true; Finish(channel->skill); }
            else if (int32_t(game->dwGameFrame - channel->nextCharge) >= 0) {
                if (!Charge(unit, channel->skill, id, level)) { channel->lease.stopped = true; Finish(channel->skill); }
                channel->nextCharge = game->dwGameFrame + Data::ManaInterval;
            }
        }
    }
    return originalServerDo(game, unit, id, level);
}
int __fastcall ServerMoveHook(Game* game, Unit* unit) {
    std::lock_guard lock(serverMutex);
    if (auto channel = Channel(game, unit); channel && channel->lease.accepted) {
        if (!Holding(game, unit, *channel)) { channel->lease.stopped = true; Finish(channel->skill); }
        else if (Data::Stationary(Position(unit), channel->lease.cursor)) {
            channel->steered = false;
            if (HoldPosition(unit, channel->skill)) return 1;
            channel->lease.stopped = true; Finish(channel->skill);
        } else if (!Steer(unit, channel->skill, channel->lease.cursor, channel->lastCursor, channel->steered, true)) {
            channel->lease.stopped = true; Finish(channel->skill);
        }
    }
    return originalServerMove(game, unit);
}
int __fastcall ClientStartHook(Unit* unit, int id) {
    // Ordinary Assassin WW can arrive here with a prepared client sequence
    // while the local server is still spinning. Mask only that client's
    // state-54 start gate for this one native call, then restore it.
    const int result = WhirlwindRestart::Invoke(At<WhirlwindRestart::Byte*>(0),
        reinterpret_cast<WhirlwindRestart::Byte*>(unit), id,
        reinterpret_cast<WhirlwindRestart::NativeStart>(originalClientStart), enabled.load());
    if (!result || !ready.load() || !enabled.load() || !Alive(unit) || unit != LocalPlayer() || !InputAllowed()) return result;
    const auto skill = Skill(unit), skills = Read<void*>(unit, 0x100);
    uint32_t seed = 0;
    if (!Whirlwind(skill) || !Active(skill) || Read<int>(skill, 0x44) != -1 || !LocalSession(unit, seed)) return result;
    int button = 0;
    if (Down(2) && SkillId(Read<void*>(skills, 0x10)) == id) button = 2;
    else if (Down(1) && SkillId(Read<void*>(skills, 8)) == id) button = 1;
    if (!button) return result;
    client = {}; client.player = unit; client.skill = skill; client.seed = seed; client.id = id; client.button = button;
    client.active = true;
    client.itemClick = itemClicks[button == 1 ? 1 : 0].load();
    if (!Cursor(client.cursor)) { client.active = false; return result; }
    // A6120 runs client prediction before queuing the native cast packet. Send
    // our first heartbeat only after that packet so the server has started WW.
    client.pending = true;
    return result;
}
int __fastcall ClientDoHook(Unit* unit, int id) {
    if (client.active && client.player == unit) Held(unit);
    return originalClientDo(unit, id);
}
int __fastcall ClientMoveHook(Unit* unit, bool advance) {
    if (Held(unit)) {
        if (Data::Stationary(Position(unit), client.cursor)) {
            client.steered = false;
            if (HoldPosition(unit, client.skill)) return 0;
            StopClient();
        } else if (!Steer(unit, client.skill, client.cursor, client.lastCursor, client.steered, false)) StopClient();
    }
    return originalClientMove(unit, advance);
}
void __fastcall ClientActionHook(int action, Unit* unit, int type, int id) {
    // Native held-button repeats predict a new cast and can finish the current
    // skill before discovering Whirlwind is already running. Keep the active
    // channel and let Held() publish its current cursor instead of recasting.
    if (client.active && unit == client.player && Data::UsesButton(action, client.button) && Held(unit)) return;
    // Normal item interaction changes the local animation/path before sending
    // its packet. A manual click during our channel uses the direct pickup path.
    if (client.active && unit == client.player && type == 4 && (action == 0x13 || action == 0x16 || action == 0x2f) && Held(unit)) {
        return;
    }
    ++clientActionDepth;
    originalClientAction(action, unit, type, id);
    --clientActionDepth;
    if (!clientActionDepth && client.active && client.player == unit && client.pending) SendControl(true);
}
int __fastcall ClientInteractionHook(Unit* unit,int action,int x,int y,uint16_t modifiers) {
    // This shared input hook remains installed with Whirlwind disabled. Reject
    // only right-button ground-item interaction, before native walk/pickup
    // scheduling; release messages and auto-gold keep their ordinary paths.
    if (ready.load() && rightClickPickupDisabled.load() && Pickup::RightClickGroundItem(unit, action)) return 1;
    // Let the native manager decide whether/when a gold pile is eligible. Its
    // short-lived target distinguishes auto-gold from mouse/skill input. Route
    // only that request before native interaction can replace our spin/path.
    if(action==7 && client.active && unit==client.player && ready.load() && enabled.load()){
        auto gold=Read<Unit*>(At<void*>(Pickup::AutoGoldTargetRva));
        if(Pickup::GoldItem(gold) && Held(unit))
            return Pickup::ClientRequest(unit,gold->dwUnitId) ? 1 : 0;
    }
    return originalClientInteraction(unit,action,x,y,modifiers);
}
bool ClientPotionAllowed(Unit* item){
    if(!ready.load()||!enabled.load()||!client.active||!Alive(client.player)||LocalPlayer()!=client.player||
        Skill(client.player)!=client.skill||!Active(client.skill)||!Whirlwind(client.skill))return false;
    uint32_t seed=0;
    return LocalSession(client.player,seed)&&seed==client.seed&&Potion::BeltItem(client.player,item);
}
int __fastcall ItemDisabledHook(Unit* item){
    if(ClientPotionAllowed(item)){
        // Retain item-specific disabled states and the original belt/tooltip
        // callers; only the local channel's Whirlwind gate is bypassed.
        Potion::MoveStateScope allow(client.player);
        return originalItemDisabled(item);
    }
    return originalItemDisabled(item);
}
int __fastcall DispatcherHook(Game* game, Unit* unit, const void* bytes, int size) {
    if (bytes && size >= 10 && size <= 512 && Data::Ours({static_cast<const uint8_t*>(bytes), size_t(size)})) {
        if (size != sizeof(Data::Command)) return 1;
        Data::Command command; memcpy(&command, bytes, sizeof(command));
        if (!Data::Valid(command) || !game || !Alive(unit) || unit->dwUnitId != command.recipient ||
            uint32_t(game->dwInitSeed) != command.session) return 1;
        auto owner = Read<Client*>(unit->pPlayerData, 0xf0);
        if (!owner || owner->pGame != game || owner->dwClientState != 4 || owner->dwUnitGUID != unit->dwUnitId) return 1;
        std::lock_guard lock(serverMutex);
        auto channel = Channel(game, unit);
        if (channel && ready.load() && enabled.load() && channel->id == command.skill &&
            Whirlwind(channel->skill) && (channel->lease.accepted || GetTickCount64() - channel->created <= 500)) {
            channel->lease.Receive(command, GetTickCount64());
        }
        return 1;
    }
    if (ready.load() && enabled.load() && bytes && size == sizeof(Pickup::Command) && *static_cast<const uint8_t*>(bytes) == 0x16) {
        std::lock_guard lock(serverMutex);
        if (auto channel = Channel(game, unit); channel && Holding(game, unit, *channel)) return Pickup::ServerRequest(game, unit, bytes, size);
    }
    if(ready.load()&&enabled.load()&&bytes&&size==36&&*static_cast<const uint8_t*>(bytes)==0x26){
        std::lock_guard lock(serverMutex);
        if(auto channel=Channel(game,unit);channel&&Holding(game,unit,*channel)&&Potion::RequestItem(game,unit,bytes,size)){
            // Call through the full dispatcher, including existing transmog/map
            // hooks, owner/location checks, item effects and native consumption.
            Potion::MoveStateScope allow(unit);
            return originalDispatcher(game,unit,bytes,size);
        }
    }
    return originalDispatcher(game, unit, bytes, size);
}

constexpr std::array<uint32_t, 10> Hooks{ServerStart, ServerDo, ServerMove, ClientStart, ClientDo, ClientMove, 0xa6120, Pickup::DispatcherRva, Pickup::ClientInteractionRva,Potion::ItemDisabledRva};
uint64_t Hash(const std::array<uint8_t, 32>& bytes) {
    uint64_t value = 14695981039346656037ULL;
    for (const auto byte : bytes) value = (value ^ byte) * 1099511628211ULL;
    return value;
}
template<class Signatures> bool Matches(const Signatures& signatures) {
    for (const auto& signature : signatures) {
        const auto bytes = Read<std::array<uint8_t, 32>>(At<void*>(signature.rva));
        if (Hash(bytes) != signature.hash) return false;
    }
    return true;
}
}
bool Install() {
    if (ready.load()) return true;
    if (!Matches(NativeData::Signatures) || !Matches(NativeData::InputSignatures) || !Matches(Pickup::Signatures)||!Matches(Potion::Signatures)) {
        status.store("D3 style Whirlwind unavailable: executable signature mismatch."); return false;
    }
    const auto initialized = MH_Initialize();
    if (initialized != MH_OK && initialized != MH_ERROR_ALREADY_INITIALIZED) return false;
    void* replacements[]{reinterpret_cast<void*>(ServerStartHook), reinterpret_cast<void*>(ServerDoHook), reinterpret_cast<void*>(ServerMoveHook),
        reinterpret_cast<void*>(ClientStartHook), reinterpret_cast<void*>(ClientDoHook), reinterpret_cast<void*>(ClientMoveHook),
        reinterpret_cast<void*>(ClientActionHook), reinterpret_cast<void*>(DispatcherHook), reinterpret_cast<void*>(ClientInteractionHook),reinterpret_cast<void*>(ItemDisabledHook)};
    void** originals[]{reinterpret_cast<void**>(&originalServerStart), reinterpret_cast<void**>(&originalServerDo), reinterpret_cast<void**>(&originalServerMove),
        reinterpret_cast<void**>(&originalClientStart), reinterpret_cast<void**>(&originalClientDo), reinterpret_cast<void**>(&originalClientMove),
        reinterpret_cast<void**>(&originalClientAction), reinterpret_cast<void**>(&originalDispatcher), reinterpret_cast<void**>(&originalClientInteraction),reinterpret_cast<void**>(&originalItemDisabled)};
    size_t created = 0;
    for (; created < Hooks.size(); ++created) if (MH_CreateHook(At<void*>(Hooks[created]), replacements[created], originals[created]) != MH_OK) break;
    if (created == Hooks.size()) {
        bool queued = true;
        for (const auto hook : Hooks) if (MH_QueueEnableHook(At<void*>(hook)) != MH_OK) queued = false;
        if (queued && MH_ApplyQueued() == MH_OK) {
            ready.store(true); status.store("D3 style Whirlwind is available for the local host character."); return true;
        }
    }
    for (size_t i = 0; i < created; ++i) MH_QueueDisableHook(At<void*>(Hooks[i]));
    MH_ApplyQueued();
    for (size_t i = 0; i < created; ++i) { MH_DisableHook(At<void*>(Hooks[i])); MH_RemoveHook(At<void*>(Hooks[i])); }
    status.store("D3 style Whirlwind hooks could not be installed."); return false;
}
void Uninstall() {
    if (!ready.exchange(false)) return;
    for (const auto hook : Hooks) MH_QueueDisableHook(At<void*>(hook));
    if (MH_ApplyQueued() == MH_OK) for (const auto hook : Hooks) MH_RemoveHook(At<void*>(hook));
}
bool Enabled() { return enabled.load(); }
void SetEnabled(bool value) { enabled.store(value); }
void SetRightClickPickupDisabled(bool value) { rightClickPickupDisabled.store(value); }
bool RightClickPickupDisabled() { return rightClickPickupDisabled.load(); }
const char* Status() { return status.load(); }
void SetInputBlocked(bool value) { inputBlocked.store(value); inputStamp.store(GetTickCount64()); }
void MouseButtonDown(int button) {
    if (!ready.load() || !enabled.load() || button < 1 || button > 2) return;
    if (button == 2 && rightClickPickupDisabled.load()) return;
    const auto hover = Read<D2MouseHoverStruct>(gMouseHover);
    if (!hover.IsHovered || hover.HoveredUnitType != UNIT_ITEM) return;
    const uint64_t sequence = uint64_t(clickSequence.fetch_add(1) + 1);
    itemClicks[size_t(button - 1)].store((sequence << 32) | hover.HoveredUnitId);
}
}
