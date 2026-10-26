#include "HellSpindle.h"
#include "../Pattern.h"
#include <MinHook.h>
#include <array>
#include <atomic>
#include <cstring>

namespace HellSpindle {
namespace {
constexpr uint32_t Emitter = 1021, Skill = 51;
constexpr uint32_t ServerCreate = 0x3151a0, ClientCreate = 0x100a80;
constexpr uint32_t ServerUpdate = 0x3c95c0, ClientUpdate = 0xedb40;
template<class T> T Native(uint32_t rva) { return reinterpret_cast<T>(Pattern::Address(rva)); }
// Called only with live native objects on their owning simulation/client thread.
template<class T> T Field(const void* p, size_t offset = 0) {
    T value{};
    if (p) std::memcpy(&value, static_cast<const uint8_t*>(p) + offset, sizeof(value));
    return value;
}
template<class T> void Put(void* p, size_t offset, T value) {
    std::memcpy(static_cast<uint8_t*>(p) + offset, &value, sizeof(value));
}
using CreateServer = void*(__fastcall*)(void*, void*);
using CreateClient = void*(__fastcall*)(void*, int);
using UpdateServer = int(__fastcall*)(void*, void*);
using UpdateClient = void(__fastcall*)(void*, float);
CreateServer originalServerCreate = nullptr;
CreateClient originalClientCreate = nullptr;
UpdateServer originalServerUpdate = nullptr;
UpdateClient originalClientUpdate = nullptr;
std::atomic<bool> installed{false};
std::atomic<const char*> status{"Hell Spindle limiter is not installed."};

struct Signature { uint32_t rva, size; uint64_t hash; };
constexpr std::array<Signature, 8> signatures{{
    {ServerCreate, 64, 0x4c8716e90138e918ULL}, {ClientCreate, 64, 0xb0dbb71e25e185a1ULL},
    {ServerUpdate, 64, 0xc39004f6318a3564ULL}, {ClientUpdate, 64, 0x27b78f2b69b00bc2ULL},
    {0xffd70, 32, 0x718db3d47d40b88eULL}, {0x2c49c0, 32, 0x04d772b7be349296ULL},
    // Creation tails establish the skill and owner before our post-create hook.
    {0x315b3e, 31, 0x7197d51edf0eb81bULL}, {0x1016df, 136, 0x9b37b0c1264bda4eULL}
}};
constexpr std::array<uint32_t, 4> hooks{ServerCreate, ClientCreate, ServerUpdate, ClientUpdate};

struct Record { uint32_t id = 0; };
struct Owner {
    bool used = false;
    uint32_t id = 0;
    size_t count = 0;
    std::array<Record, 2> emitters{}; // Oldest first; never retains unit pointers.
};
struct Counters { uint64_t created = 0, retired = 0, lookups = 0, limitReads = 0; };
struct Side {
    void* game = nullptr;
    uint64_t session = 0;
    uint32_t seed = 0;
    std::array<Owner, 8> owners{};
    Counters counters{};
};
// Independent storage: server and client must never share live unit references.
std::array<Side, 2> sides{};

bool Target(void* missile) {
    return missile && Field<uint32_t>(missile) == 3 && Field<uint32_t>(missile, 4) == Emitter &&
        Field<uint16_t>(Field<void*>(missile, 0x10), 0xc) == Skill;
}
bool Alive(void* missile) {
    auto data = Field<void*>(missile, 0x10);
    return data && Field<int16_t>(data, 0x10) > 0 && Field<float>(data, 0x14) > 0.0f;
}
void Expire(void* missile) {
    auto data = Field<void*>(missile, 0x10);
    Put<uint16_t>(data, 0x10, 0);
    Put<float>(data, 0x14, 0.0f);
    // Do not touch +0x30: Frozen Orb uses it for the rotation angle.
}
void* LocalGame() {
    return Field<void*>(Field<void*>(Native<void*>(0x1d637f0)), 0x230);
}
bool Scope(void* game) {
    return game && Field<uint8_t>(game, 0x101) == 3 && LocalGame() == game &&
        Field<uint64_t>(Native<void*>(0x1d617d0)) != 0;
}
void* Lookup(int side, Side& state, uint32_t type, uint32_t id) {
    ++state.counters.lookups;
    if (!side) return Native<void*(__fastcall*)(void*, uint32_t, uint32_t)>(0x2c49c0)(state.game, type, id);
    if (type > 4) return nullptr;
    const uint32_t table = type == 3 ? 0x1d466e0 : 0x1d442e0 + type * 0x400;
    auto unit = Field<void*>(Native<void*>(table + (id & 127) * 8));
    for (unsigned n = 0; unit && n < 4096; ++n, unit = Field<void*>(unit, 0x150))
        if (Field<uint32_t>(unit) == type && Field<uint32_t>(unit, 8) == id) return unit;
    return nullptr;
}
int Maximum(void* player) {
    auto skill = Field<void*>(Field<void*>(player, 0x100));
    for (unsigned n = 0; skill && n < 2048; ++n, skill = Field<void*>(skill, 8)) {
        auto row = Field<void*>(skill);
        if (row && Field<uint16_t>(row) == Skill && Field<int>(skill, 0x44) == -1)
            return 1 + (Field<int>(skill, 0x38) == 10);
    }
    return 1; // Current RMD-MP calc1: par1+((blvl==10)?1:0), Param1=1.
}
bool Owned(void* missile, uint32_t owner) {
    return Target(missile) && (Field<uint32_t>(missile, 0x128) & 0x400) &&
        Field<uint32_t>(missile, 0xe8) == 0 && Field<uint32_t>(missile, 0xec) == owner;
}
// Runs exactly once after successful creation of a player-owned emitter.
// A new cast examines at most two old emitter IDs, rather than scanning 512
// records (and traversing the skill list) every server/client missile update.
void Created(int side, void* game, void* missile) {
    if (!Target(missile) || !Alive(missile) || !Scope(game) ||
        !(Field<uint32_t>(missile, 0x128) & 0x400) || Field<uint32_t>(missile, 0xe8) != 0) return;
    auto& state = sides[side];
    const auto session = Field<uint64_t>(Native<void*>(0x1d617d0));
    const auto seed = Field<uint32_t>(game, 0x144);
    if (state.game != game || state.session != session || state.seed != seed) {
        state.game = game; state.session = session; state.seed = seed; state.owners = {};
    }
    const auto id = Field<uint32_t>(missile, 8), ownerId = Field<uint32_t>(missile, 0xec);
    auto player = Lookup(side, state, 0, ownerId);
    if (!player) return;
    Owner* owner = nullptr;
    Owner* free = nullptr;
    for (auto& candidate : state.owners) {
        if (candidate.used && candidate.id == ownerId) { owner = &candidate; break; }
        if (!candidate.used && !free) free = &candidate;
    }
    if (!owner) {
        // Single-player normally needs one entry. Recover departed owner slots
        // without ever dereferencing an object retained from a previous cast.
        if (!free) for (auto& candidate : state.owners)
            if (!Lookup(side, state, 0, candidate.id)) { free = &candidate; break; }
        if (!free) return;
        *free = {}; free->used = true; free->id = ownerId; owner = free;
    }
    size_t alive = 0;
    std::array<void*, 2> live{};
    for (size_t n = 0; n < owner->count; ++n) {
        const auto record = owner->emitters[n];
        if (record.id == id) continue; // A removed unit ID may have been reused.
        auto old = Lookup(side, state, 3, record.id);
        if (Owned(old, ownerId) && Alive(old)) {
            live[alive] = old; owner->emitters[alive++] = record;
        }
    }
    owner->count = alive;
    ++state.counters.created;
    ++state.counters.limitReads;
    const auto cap = size_t(Maximum(player));
    while (owner->count >= cap) {
        Expire(live[0]); ++state.counters.retired;
        for (size_t n = 1; n < owner->count; ++n) {
            owner->emitters[n - 1] = owner->emitters[n]; live[n - 1] = live[n];
        }
        --owner->count;
    }
    owner->emitters[owner->count++] = {id};
}
void* __fastcall ServerCreateHook(void* game, void* parameters) {
    const bool candidate = parameters && Field<uint32_t>(parameters, 0x20) == Emitter;
    auto missile = originalServerCreate(game, parameters);
    if (candidate) Created(0, game, missile);
    return missile;
}
void* __fastcall ClientCreateHook(void* parameters, int mode) {
    const bool candidate = parameters && Field<uint32_t>(parameters, 0x20) == Emitter;
    auto missile = originalClientCreate(parameters, mode);
    if (candidate) Created(1, LocalGame(), missile);
    return missile;
}
int __fastcall ServerUpdateHook(void* game, void* missile) {
    // Native Frozen Orb can emit before its collision/expiry handler runs.
    // This constant-time guard prevents one final emission after replacement.
    // Active updates perform no tracking, owner lookup, skill scan or cap check.
    if (Target(missile) && !Alive(missile) && Scope(game)) return 2;
    return originalServerUpdate(game, missile);
}
void __fastcall ClientUpdateHook(void* missile, float delta) {
    if (Target(missile) && !Alive(missile) && Scope(LocalGame())) {
        Native<void(__fastcall*)(void*)>(0xffd70)(missile);
        return;
    }
    originalClientUpdate(missile, delta);
}
bool Supported() {
    for (const auto& s : signatures) {
        std::array<uint8_t, 160> bytes{}; SIZE_T count = 0;
        if (s.size > bytes.size() || !ReadProcessMemory(GetCurrentProcess(), Native<void*>(s.rva), bytes.data(), s.size, &count) || count != s.size) return false;
        uint64_t hash = 14695981039346656037ULL;
        for (size_t n = 0; n < s.size; ++n) hash = (hash ^ bytes[n]) * 1099511628211ULL;
        if (hash != s.hash) return false;
    }
    return Field<uintptr_t>(Native<void*>(0x14c1478)) == Pattern::Address(ServerUpdate) &&
        Field<uintptr_t>(Native<void*>(0x14b7338)) == Pattern::Address(ClientUpdate);
}
bool Mod() {
    std::array<char, 7> name{}; SIZE_T count = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), Native<void*>(0x1bf084f), name.data(), name.size(), &count) || count != name.size()) return false;
    constexpr char expected[] = "rmd-mp";
    for (size_t n = 0; n < name.size(); ++n) {
        const auto c = name[n] >= 'A' && name[n] <= 'Z' ? char(name[n] + 32) : name[n];
        if (c != expected[n]) return false;
    }
    return true;
}
}
bool Install() {
    if (installed.load()) return true;
    if (!Mod()) { status = "Inactive: Hell Spindle limiter is restricted to RMD-MP."; return false; }
    if (!Supported()) { status = "Unavailable: Hell Spindle native compatibility check failed."; return false; }
    const auto initialized = MH_Initialize();
    if (initialized != MH_OK && initialized != MH_ERROR_ALREADY_INITIALIZED) {
        status = "Unavailable: Hell Spindle hooks could not initialize."; return false;
    }
    void* replacements[]{reinterpret_cast<void*>(ServerCreateHook), reinterpret_cast<void*>(ClientCreateHook),
        reinterpret_cast<void*>(ServerUpdateHook), reinterpret_cast<void*>(ClientUpdateHook)};
    void** originals[]{reinterpret_cast<void**>(&originalServerCreate), reinterpret_cast<void**>(&originalClientCreate),
        reinterpret_cast<void**>(&originalServerUpdate), reinterpret_cast<void**>(&originalClientUpdate)};
    size_t created = 0;
    for (; created < hooks.size(); ++created)
        if (MH_CreateHook(Native<void*>(hooks[created]), replacements[created], originals[created]) != MH_OK) break;
    bool ok = created == hooks.size();
    if (ok) {
        sides = {};
        for (auto rva : hooks) if (MH_QueueEnableHook(Native<void*>(rva)) != MH_OK) ok = false;
        if (ok) ok = MH_ApplyQueued() == MH_OK;
    }
    if (!ok) {
        for (size_t n = 0; n < created; ++n) { MH_DisableHook(Native<void*>(hooks[n])); MH_RemoveHook(Native<void*>(hooks[n])); }
        status = "Unavailable: Hell Spindle hooks could not be installed."; return false;
    }
    installed = true;
    status = "Installed: single-player; cap checked on creation; 1 spindle, 2 at 10 hard points.";
    return true;
}
void Uninstall() {
    if (!installed.exchange(false)) return;
    for (auto rva : hooks) MH_QueueDisableHook(Native<void*>(rva));
    if (MH_ApplyQueued() == MH_OK) for (auto rva : hooks) MH_RemoveHook(Native<void*>(rva));
    status = "Hell Spindle limiter is not installed.";
}
const char* Status() { return status.load(); }
}
