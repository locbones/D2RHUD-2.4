#include "SummonPersistence.h"
#include "SummonPersistenceData.h"
#include "SummonPersistenceNative.h"
#include "CubeMap.h"
#include "../Pattern.h"
#include "../d2/Reclass.h"
#include <MinHook.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <map>
#include <mutex>
#include <set>
#include <span>
#include <cwctype>

namespace SummonPersistence {
namespace {
using Unit = D2UnitStrc;
using Game = D2GameStrc;
using Client = D2ClientStrc;
using Room = D2ActiveRoomStrc;
template<class T> T Native(uint32_t rva) { return reinterpret_cast<T>(Pattern::Address(rva)); }
template<class T> bool Read(const void* address, T& value) {
    SIZE_T count = 0;
    return address && ReadProcessMemory(GetCurrentProcess(), address, &value, sizeof(value), &count) && count == sizeof(value);
}
template<class T> T Field(const void* address, size_t offset = 0) {
    T value{};
    if (address) Read(static_cast<const uint8_t*>(address) + offset, value);
    return value;
}
template<class T> void Set(void* address, size_t offset, T value) { memcpy(static_cast<uint8_t*>(address) + offset, &value, sizeof(value)); }
using Key = std::pair<Game*, uint32_t>;
struct SummonArg {
    uint32_t flags = 0, padding = 0;
    Unit* owner = nullptr;
    int16_t monster = -1, extra = 0;
    int ai = 0, mode = 1, x = 0, y = 0, petType = 0, maximum = 1, unused = 0;
};
static_assert(sizeof(SummonArg) == 0x30 && offsetof(SummonArg, petType) == 0x24);
using Factory = Unit*(__fastcall*)(Game*, SummonArg*);
using Passives = int(__fastcall*)(Game*, Unit*, Unit*, int, int, int);
using SkillDo = int(__fastcall*)(Game*, Unit*, int, int);
Factory originalFactory = nullptr;
Passives originalPassives = nullptr;
std::atomic<bool> ready{false}, enabled{false}, configured{false};
std::atomic<const char*> status{"Persistent summons are not installed."};
std::recursive_mutex mutex;
std::string storageStatus{"No companion file has been saved in this session."};
std::string installationDetail;
struct Tracked { Unit* unit = nullptr; uint32_t owner = 0; Data::Record record; };
std::map<Key, Tracked> pets;
struct Session {
    Unit* player = nullptr;
    Data::Snapshot source;
    bool pending = false;
    unsigned attempts = 0;
    uint32_t nextFrame = 0;
};
std::map<Key, Session> sessions;
std::map<Client*, Data::Snapshot> loading;
struct RestoreContext {
    Game* game; Unit* owner; const Data::Record* record;
    Unit* created = nullptr;
    bool attempted = false;
};
thread_local RestoreContext* restoring = nullptr;

uint64_t Hash(std::span<const uint8_t> bytes) {
    uint64_t hash = 14695981039346656037ULL;
    for (uint8_t byte : bytes) hash = (hash ^ byte) * 1099511628211ULL;
    return hash;
}
// Installation and mod namespaces prevent equal character names in different
// installations/mods from sharing a roster. The filename itself is collision-free.
std::filesystem::path Directory() {
    wchar_t local[32768]{}, executable[32768]{};
    const auto count = GetEnvironmentVariableW(L"LOCALAPPDATA", local, 32768);
    const auto length = GetModuleFileNameW(nullptr, executable, 32768);
    if (!count || count >= 32768 || !length || length >= 32768) return {};
    std::array<char, 128> mod{};
    if (!Read(Native<void*>(0x1bf084f), mod)) return {};
    mod.back() = 0;
    std::wstring installation = std::filesystem::path(executable).parent_path().wstring();
    std::transform(installation.begin(), installation.end(), installation.begin(), [](wchar_t c) { return wchar_t(towlower(c)); });
    char root[17]{};
    snprintf(root, sizeof(root), "%016llx", static_cast<unsigned long long>(Hash({reinterpret_cast<const uint8_t*>(installation.data()), installation.size() * sizeof(wchar_t)})));
    Data::Identity modIdentity{mod.data()};
    const auto modKey = mod[0] ? Data::Key(modIdentity) : std::string{};
    if (mod[0] && modKey.empty()) return {};
    return std::filesystem::path(local) / L"D2RHUD" / L"Summons" / root / (mod[0] ? "mod-" + modKey : "retail");
}
std::filesystem::path RosterPath(const Data::Identity& identity) {
    const auto directory = Directory();
    return directory.empty() ? std::filesystem::path{} : directory / (Data::Key(identity) + ".json");
}
std::string DisplayPath(const std::filesystem::path& path) {
    const auto utf8 = path.u8string();
    return {reinterpret_cast<const char*>(utf8.data()), utf8.size()};
}
void StorageMessage(std::string message) {
    std::lock_guard lock(mutex);
    storageStatus = std::move(message);
    OutputDebugStringA(("D2RHUD summons: " + storageStatus + "\n").c_str());
}
bool PrepareDirectory(const std::filesystem::path& directory) {
    if (directory.empty()) {
        StorageMessage("Cannot resolve the summon folder from LOCALAPPDATA, the game installation and mod name.");
        return false;
    }
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) {
        StorageMessage("Cannot create summon folder:\n" + DisplayPath(directory) + "\n" + error.message()
            + " (" + std::to_string(error.value()) + ")");
        return false;
    }
    return true;
}
bool Header(const uint8_t* bytes, size_t size, Data::Snapshot& snapshot) {
    // The audited native writer uses the unchanged v98 header, including the
    // extended name at 0x10b. Other save layouts require a separate audit.
    if (!bytes || size < 0x34 || Field<uint32_t>(bytes) != 0xaa55aa55) return false;
    const uint32_t version = Field<uint32_t>(bytes, 4);
    if (version != 98) return false;
    snapshot.saveSize = Field<uint32_t>(bytes, 8);
    snapshot.checksum = Field<uint32_t>(bytes, 12);
    if (snapshot.saveSize < 0x14f || snapshot.saveSize > size || size < 0x14f) return false;
    snapshot.identity.created = Field<uint32_t>(bytes, 0x2c);
    snapshot.identity.classId = Field<uint8_t>(bytes, 0x28);
    snapshot.identity.flags = Field<uint32_t>(bytes, 0x24) & 0x64; // Hardcore, expansion, ladder; not progression/death.
    std::array<char, 61> name{};
    if (!Read(bytes + 0x10b, name) || name.back() != 0) return false;
    snapshot.identity.name = name.data();
    return Data::ValidIdentity(snapshot.identity);
}
std::array<uint8_t, 0x284> SkillRow(int id) {
    std::array<uint8_t, 0x284> row{};
    const auto count = Field<uint64_t>(Native<void*>(0x1c9fad8));
    const auto table = Field<uint8_t*>(Native<void*>(0x1c9fad0));
    if (id < 0 || uint64_t(id) >= count || count > 65536 || !table || !Read(table + size_t(id) * row.size(), row)) row.fill(0xff);
    return row;
}
int Stat(Unit* unit, int stat) { return Native<int(__fastcall*)(Unit*, int, uint16_t)>(0x1e5620)(unit, stat, 0); }
bool Dead(Unit* unit) {
    return !unit || (unit->dwFlags & 0x10000) || unit->dwAnimMode == 0 ||
        unit->dwAnimMode == (unit->dwUnitType == 0 ? 17u : 12u) || (Stat(unit, 7) > 0 && Stat(unit, 6) <= 0);
}
Unit* Lookup(Game* game, uint32_t id) { return Native<Unit*(__fastcall*)(Game*, int, uint32_t)>(0x2c49c0)(game, 1, id); }
int PetTypeCount() { return Field<int>(Native<void*>(0x1c9fbd0)); }
bool Registered(Unit* owner, Unit* pet, int type) {
    const int types = PetTypeCount();
    if (!owner || !owner->pPlayerData || type <= 0 || type >= types || types > 65536) return false;
    const auto list = Field<void*>(owner->pPlayerData, 0x98);
    const auto entries = Field<uint8_t*>(list);
    auto node = Field<void*>(entries, size_t(type) * 0x10);
    for (size_t n = 0; node && n < Data::MaxPets; ++n, node = Field<void*>(node, 0x18)) {
        if (Field<uint32_t>(node, 4) == pet->dwUnitId) return !(Field<uint32_t>(node) & 1);
    }
    return false;
}
int PetCount(Unit* owner, int type) {
    const auto list = Field<void*>(owner->pPlayerData, 0x98);
    const auto entries = Field<uint8_t*>(list);
    return entries ? Field<int>(entries, size_t(type) * 0x10 + 8) : 0;
}
int Evaluate(Unit* owner, uint32_t formula, int skill, int level) {
    const auto bytes = Field<uint8_t*>(Native<void*>(0x1c9eab8));
    const auto count = Field<uint64_t>(Native<void*>(0x1c9eac0));
    if (!bytes || formula >= count || count > INT_MAX) return 0;
    struct Context { Unit* owner; int skill, level; } context{owner, skill, level};
    return Native<int(__fastcall*)(const uint8_t*, int, void*, void*, int, void*)>(0x6d08c0)(bytes + formula,
        int(count - formula), Native<void*>(0x2185e0), Native<void*>(0x1800280), 7, &context);
}
bool Routine(int function) {
    // Native summon *implementations*, independent of class, skill ID, pet type,
    // and monster ID. Custom skill rows use the same engine implementations.
    switch (function) { case 16: case 31: case 49: case 56: case 114: case 115: case 119: case 144: return true; default: return false; }
}
bool Temporary(Unit* owner, Unit* pet, const Data::Record& record, const uint8_t* row) {
    if (!pet || !pet->pMonsterData || std::find(std::begin(pet->pMonsterData->nMonUmod), std::end(pet->pMonsterData->nMonUmod), uint8_t(21)) != std::end(pet->pMonsterData->nMonUmod)) return true;
    // A trap event is finite. Periodic monster-modifier events (type 7) and
    // ordinary buff expiration (type 12) can belong to permanent custom pets.
    auto event = Field<void*>(pet, 0x140);
    for (size_t n = 0; event && n < 1024; ++n, event = Field<void*>(event, 0x30))
        if (Field<uint8_t>(event) == 4) return true;
    return NativeData::Limited(owner, record.skill, record.level, row, Evaluate, record.monster);
}
bool Eligible(Unit* owner, Unit* pet, const Data::Record& record) {
    if (!Data::ValidRecord(record) || !pet || pet->dwUnitType != 1 || pet->dwClassId == 291 || record.petType == 7 || Dead(pet) || !Registered(owner, pet, record.petType)) return false;
    const auto row = SkillRow(record.skill);
    return Routine(Field<int16_t>(row.data(), NativeData::DoOffset)) &&
        Field<int8_t>(row.data(), 0xde) == record.petType && !Temporary(owner, pet, record, row.data());
}
Unit* __fastcall FactoryHook(Game* game, SummonArg* argument) {
    auto context = restoring;
    SummonArg adjusted;
    if (context && context->game == game && argument && argument->owner == context->owner) {
        // One saved minion can create at most one replacement. Never evict an
        // existing pet if a player has already summoned to the native limit.
        if (context->attempted || argument->petType != context->record->petType ||
            argument->monster != context->record->monster || PetCount(argument->owner, argument->petType) >= (std::max)(1, argument->maximum)) return nullptr;
        context->attempted = true;
        adjusted = *argument;
        adjusted.flags |= 1;
        // These companions are returning alive, rather than being cast again.
        // HD summon timelines can hide the mesh before revealing it on a later
        // frame (Blood Golem S1 does this). Joining can interrupt that timeline
        // after the hide event, leaving the model invisible in neutral mode.
        // Start in native monster neutral mode so no spawn-only event is queued.
        adjusted.mode = 1;
        adjusted.x = Field<uint16_t>(argument->owner->pDynamicPath, 2);
        adjusted.y = Field<uint16_t>(argument->owner->pDynamicPath, 6);
        argument = &adjusted;
    }
    Unit* pet = originalFactory(game, argument);
    if (ready.load() && game && argument && argument->owner && argument->owner->dwUnitType == 0 && pet) {
        if (context && context->game == game && argument->owner == context->owner) {
            pet->pSeed.LowSeed() = int32_t(context->record->seed);
            context->created = pet;
        }
        // Capture even while disabled, so enabling mid-game can save companions
        // already alive. Persistence itself is controlled at save/restore time.
        try {
            std::lock_guard lock(mutex);
            auto& tracked = pets[{game, pet->dwUnitId}];
            tracked = {pet, argument->owner->dwUnitId, {-1, 0, int(pet->dwClassId), argument->petType, uint32_t(pet->pSeed.LowSeed())}};
        } catch (...) { OutputDebugStringA("D2RHUD: summon could not be tracked.\n"); }
    }
    return pet;
}
int __fastcall PassivesHook(Game* game, Unit* owner, Unit* pet, int skill, int level, int itemLevel) {
    const int result = originalPassives(game, owner, pet, skill, level, itemLevel);
    if (!ready.load() || !game || !owner || owner->dwUnitType != 0 || !pet) return result;
    try {
        std::lock_guard lock(mutex);
        if (auto it = pets.find({game, pet->dwUnitId}); it != pets.end() && it->second.unit == pet && it->second.owner == owner->dwUnitId) {
            it->second.record.skill = skill;
            it->second.record.level = level;
        }
    } catch (...) { OutputDebugStringA("D2RHUD: summon skill could not be recorded.\n"); }
    return result;
}
// Only path targeting/used-skill fields changed by this feature are restored;
// normal skill statistics, states, input selection, and mana are left intact.
struct TargetScope {
    Unit* owner;
    void* skills;
    void* used;
    uint32_t didEffect;
    std::array<uint8_t, 12> aim{};
    std::array<uint8_t, 16> target{};
    TargetScope(Unit* unit, void* skill) : owner(unit), skills(Field<void*>(unit, 0x100)), used(Field<void*>(skills, 0x18)), didEffect(unit->dwFlags & 0x40) {
        memcpy(aim.data(), reinterpret_cast<uint8_t*>(unit->pDynamicPath) + 0x10, aim.size());
        memcpy(target.data(), reinterpret_cast<uint8_t*>(unit->pDynamicPath) + 0x70, target.size());
        Set(skills, 0x18, skill);
        Set(unit->pDynamicPath, 0x10, Field<uint16_t>(unit->pDynamicPath, 2));
        Set(unit->pDynamicPath, 0x12, Field<uint16_t>(unit->pDynamicPath, 6));
        // Native target setter clears both the pointer and its type/ID.
        Native<void(__fastcall*)(Unit*, Unit*)>(0x2095a0)(unit, nullptr);
    }
    ~TargetScope() {
        memcpy(reinterpret_cast<uint8_t*>(owner->pDynamicPath) + 0x10, aim.data(), aim.size());
        memcpy(reinterpret_cast<uint8_t*>(owner->pDynamicPath) + 0x70, target.data(), target.size());
        Set(skills, 0x18, used);
        owner->dwFlags = (owner->dwFlags & ~0x40u) | didEffect;
    }
};
bool Restore(Game* game, Unit* owner, Data::Record record, bool* retry = nullptr) {
    if (retry) *retry = false;
    const auto row = SkillRow(record.skill);
    const int function = Field<int16_t>(row.data(), NativeData::DoOffset);
    if (!Routine(function) || record.monster == 291 || record.petType == 7 ||
        Field<int8_t>(row.data(), 0xde) != record.petType || record.petType <= 0 || record.petType >= PetTypeCount()) return false;
    // A respec or removed item must not retain a skill the character no longer has.
    auto skill = Native<void*(__fastcall*)(Unit*, int)>(0x218ce0)(owner, record.skill);
    if (!skill) return false;
    const int current = Field<int>(skill, 0x38) + Native<int(__fastcall*)(Unit*, void*, bool)>(0x21a990)(owner, skill, false);
    record.level = (std::min)(record.level, current);
    if (record.level < 1) return false;
    // A changed mod can turn an old permanent summon into a temporary one.
    if (NativeData::Limited(owner, record.skill, record.level, row.data(), Evaluate, record.monster)) return false;
    TargetScope target(owner, skill);
    RestoreContext context{game, owner, &record};
    struct Scope {
        RestoreContext* previous;
        explicit Scope(RestoreContext* context) : previous(restoring) { restoring = context; }
        ~Scope() { restoring = previous; }
    } scope(&context);
    if (function == 31 || function == 144) {
        // Reuse the native skeleton initialization after its corpse-consumption
        // stage. Logging in neither creates nor consumes a world corpse.
        SummonArg args;
        args.owner = owner; args.monster = int16_t(record.monster); args.petType = record.petType;
        int mode = 1;
        const int monster = Native<int(__fastcall*)(Unit*, int, int, int, int*, int*, int*)>(0x21e3a0)(owner, 0, record.skill, record.level, &mode, nullptr, nullptr);
        if (function == 31 ? monster != record.monster : record.monster < monster || record.monster > monster + 2) return false;
        args.mode = mode;
        args.maximum = Evaluate(owner, Field<uint32_t>(row.data(), 0xe0), record.skill, record.level);
        if (auto pet = FactoryHook(game, &args)) {
            if (function == 31) {
                Native<int(__fastcall*)(Game*, Unit*, Unit*, int, int)>(0x368a90)(game, owner, pet, 0, record.level);
                NativeData::SkeletonComponents(owner, pet, record.skill, record.level, record.components);
            } else {
                // Permanent custom companions also reuse the three-part Hydra
                // implementation. Restore each saved member exactly once and
                // bypass only that implementation's town casting restriction.
                Native<void(__fastcall*)(Unit*, int, int, int)>(0x29bf70)(pet, record.skill, record.level, 1);
            }
            PassivesHook(game, owner, pet, record.skill, record.level, 0);
            if (function == 31) {
                Native<void(__fastcall*)(Unit*, Unit*)>(0x367910)(owner, pet);
                Native<void(__fastcall*)(Game*, Unit*, int, int)>(0x3c5410)(game, pet, 0, Field<int>(owner, 0x12c));
            } else {
                if (auto ai = Field<void*>(pet->pMonsterData, 0x30)) Set(ai, 0x3c,
                    game->dwGameFrame + uint32_t(Evaluate(owner, Field<uint32_t>(row.data(), 0x158), record.skill, record.level)));
            }
        }
    } else {
        const auto functionAddress = Field<SkillDo>(Native<void*>(0x14bf500), size_t(function) * sizeof(void*));
        if (!functionAddress) return false;
        functionAddress(game, owner, record.skill, record.level);
    }
    if (!context.created) {
        if (retry) *retry = context.attempted;
        return false;
    }
    // Newly temporary pets after a mod change keep their native lifetime, and
    // will be omitted at the next save. No timer is ever removed or extended.
    return true;
}
bool Supported(uint32_t* failed = nullptr) {
    std::array<uint8_t, 32> code{};
    for (auto signature : NativeData::Signatures) {
        if (!Read(Native<void*>(signature.rva), code) || Hash(code) != signature.hash) {
            if (failed) *failed = signature.rva;
            return false;
        }
    }
    return true;
}
void ReportStorageFailure() { status.store("Persistent summons: companion file could not be saved; native character save is unchanged."); }
} // namespace

bool Enabled() { return enabled.load(); }
void SetEnabled(bool value) {
    enabled.store(value); configured.store(true);
    if (value) try {
        const auto directory = Directory();
        if (PrepareDirectory(directory)) StorageMessage("Companion folder:\n" + DisplayPath(directory)
            + "\nA character's JSON file is written when the game saves that character.");
    } catch (...) { StorageMessage("Could not initialize the summon folder."); }
}
const char* Status() { return status.load(); }
std::string StorageStatus() {
    std::lock_guard lock(mutex);
    return (installationDetail.empty() ? "" : installationDetail + "\n") + storageStatus;
}
void BeforeLoad(Game*, Client* client) {
    if (!ready.load() || !client) return;
    try {
        std::lock_guard lock(mutex);
        loading.erase(client);
        auto block = client->pSaveHeaders;
        auto bytes = Field<const uint8_t*>(block);
        const auto size = Field<uint64_t>(block, 8);
        Data::Snapshot header;
        if (Header(bytes, size, header)) loading.emplace(client, std::move(header));
    } catch (...) { OutputDebugStringA("D2RHUD: could not read summon companion identity.\n"); }
}
void AfterLoad(Game* game, Client* client, Unit* player, bool success) {
    if (!ready.load()) return;
    try {
        std::lock_guard lock(mutex);
        auto it = loading.find(client);
        if (it == loading.end()) return;
        auto header = std::move(it->second);
        loading.erase(it);
        if (!success || !game || !player || player->dwUnitType != 0) return;
        auto source = Data::Read(RosterPath(header.identity), header.identity, header.checksum, header.saveSize);
        Session session{player, source ? std::move(*source) : std::move(header), bool(source)};
        sessions[{game, player->dwUnitId}] = std::move(session);
    } catch (...) { OutputDebugStringA("D2RHUD: summon companion could not be loaded.\n"); }
}
void Saved(Game* game, Unit* player, const uint8_t* bytes, size_t size) {
    if (!ready.load() || !game || !player || player->dwUnitType != 0) return;
    try {
        Data::Snapshot snapshot;
        if (!Header(bytes, size, snapshot)) {
            StorageMessage("Character save was received, but its header is unsupported or incomplete (version "
                + std::to_string(size >= 8 ? Field<uint32_t>(bytes, 4) : 0) + ", bytes " + std::to_string(size) + ").");
            return;
        }
        std::vector<std::pair<Key, Tracked>> candidates;
        {
            std::lock_guard lock(mutex);
            for (const auto& entry : pets) if (entry.first.first == game && entry.second.owner == player->dwUnitId) candidates.push_back(entry);
            if (enabled.load() && !Dead(player)) {
                if (auto it = sessions.find({game, player->dwUnitId}); it != sessions.end() && it->second.pending)
                    snapshot.pets = it->second.source.pets; // Not yet placed in a crowded joining room.
            }
        }
        if (enabled.load() && !Dead(player)) {
            for (const auto& [key, pet] : candidates) {
                if (key.first == game && pet.owner == player->dwUnitId && Lookup(game, key.second) == pet.unit && Eligible(player, pet.unit, pet.record)) {
                    if (snapshot.pets.size() == Data::MaxPets) { ReportStorageFailure(); return; }
                    auto record = pet.record;
                    if (pet.unit->pMonsterData) std::copy_n(pet.unit->pMonsterData->nComponent, record.components.size(), record.components.begin());
                    snapshot.pets.push_back(record);
                }
            }
        }
        // Empty snapshots matter: saving with no pets, after death, or with the
        // option disabled must supersede the previous army instead of reviving it.
        const auto path = RosterPath(snapshot.identity);
        if (path.empty() || !PrepareDirectory(path.parent_path())) { ReportStorageFailure(); return; }
        std::error_code error;
        if (!Data::AtomicWrite(path, snapshot, &error)) {
            StorageMessage("Cannot write summon file:\n" + DisplayPath(path) + "\n" + error.message()
                + " (" + std::to_string(error.value()) + ")");
            ReportStorageFailure();
        } else {
            StorageMessage("Saved " + std::to_string(snapshot.pets.size()) + " permanent summons to:\n" + DisplayPath(path));
            status.store("Persistent summons: companion file saved.");
        }
    } catch (...) { ReportStorageFailure(); }
}
void Tick(Game* game) {
    if (!ready.load() || !game || !configured.load()) return;
    try {
        struct Work { Key key; Unit* player; std::vector<Data::Record> records; unsigned attempts; };
        std::vector<Work> work;
        {
        std::lock_guard lock(mutex);
        for (auto& [key, session] : sessions) {
            if (key.first != game || !session.pending) continue;
            if (!enabled.load()) { session.pending = false; continue; }
            if (int32_t(game->dwGameFrame - session.nextFrame) < 0) continue;
            auto player = session.player;
            auto client = player && player->pPlayerData ? player->pPlayerData->pClient : nullptr;
            if (!client || client->pGame != game || client->dwClientState != 4 || client->pPlayer != player ||
                !player->pDynamicPath || !Field<void*>(player, 0x100) || !Native<Room*(__fastcall*)(Unit*)>(0x209530)(player)) continue;
            work.push_back({key, player, session.source.pets, session.attempts + 1});
            session.pending = false; // Never restore twice, including reentrant saves.
        }
        }
        // Native summon creation may call the shared removal hook. Never hold
        // this module's mutex while entering the server's other hook owners.
        for (const auto& item : work) {
            std::vector<Data::Record> deferred;
            if (!Dead(item.player)) for (const auto& record : item.records) {
                bool retry = false;
                if (!Restore(game, item.player, record, &retry) && retry) deferred.push_back(record);
            }
            if (!deferred.empty()) {
                std::lock_guard lock(mutex);
                if (auto it = sessions.find(item.key); it != sessions.end() && it->second.player == item.player) {
                    it->second.source.pets = std::move(deferred);
                    it->second.attempts = item.attempts;
                    it->second.nextFrame = game->dwGameFrame + (item.attempts < 125 ? 5 : 25);
                    it->second.pending = true;
                    if (item.attempts == 125) status.store("Persistent summons: waiting for space near the player to restore remaining minions.");
                }
            }
        }
    } catch (...) { status.store("Persistent summons: restoration could not be completed."); }
}
void Removed(Game* game, Unit* unit) {
    if (!ready.load() || !unit) return;
    std::lock_guard lock(mutex);
    if (unit->dwUnitType == 1) pets.erase({game, unit->dwUnitId});
    if (unit->dwUnitType == 0) {
        sessions.erase({game, unit->dwUnitId});
        std::erase_if(pets, [&](const auto& entry) { return entry.first.first == game && entry.second.owner == unit->dwUnitId; });
    }
}
void Destroyed(Game* game) {
    std::lock_guard lock(mutex);
    std::erase_if(pets, [game](const auto& entry) { return entry.first.first == game; });
    std::erase_if(sessions, [game](const auto& entry) { return entry.first.first == game; });
}
bool Install() {
    if (ready.load()) return true;
    if (!CubeMap::SummonLifecycleAvailable()) {
        status.store("Persistent summons unavailable: character save/load hooks are not installed.");
        std::lock_guard lock(mutex);
        installationDetail = CubeMap::SummonLifecycleStatus();
        return false;
    }
    uint32_t failed = 0;
    if (!Supported(&failed)) {
        status.store("Persistent summons unavailable: native summon signatures do not match.");
        char address[32]{}; snprintf(address, sizeof(address), "0x%x", failed);
        std::lock_guard lock(mutex);
        installationDetail = std::string("Unsupported or modified native function at D2R.exe+") + address + ".";
        return false;
    }
    const auto initialized = MH_Initialize();
    if (initialized != MH_OK && initialized != MH_ERROR_ALREADY_INITIALIZED) return false;
    const std::array<uint32_t, 2> hooks{0x29b4c0, 0x367f60};
    void* replacements[]{reinterpret_cast<void*>(FactoryHook), reinterpret_cast<void*>(PassivesHook)};
    void** originals[]{reinterpret_cast<void**>(&originalFactory), reinterpret_cast<void**>(&originalPassives)};
    size_t created = 0;
    for (; created < hooks.size(); ++created) if (MH_CreateHook(Native<void*>(hooks[created]), replacements[created], originals[created]) != MH_OK) break;
    bool ok = created == hooks.size();
    if (ok) {
        for (auto hook : hooks) if (MH_QueueEnableHook(Native<void*>(hook)) != MH_OK) ok = false;
        ok = ok && MH_ApplyQueued() == MH_OK;
    }
    if (!ok) {
        for (size_t i = 0; i < created; ++i) MH_QueueDisableHook(Native<void*>(hooks[i]));
        MH_ApplyQueued();
        for (size_t i = 0; i < created; ++i) { MH_DisableHook(Native<void*>(hooks[i])); MH_RemoveHook(Native<void*>(hooks[i])); }
        status.store("Persistent summons unavailable: native hooks could not be installed."); return false;
    }
    ready.store(true);
    status.store("Persistent summons: available for offline games and this TCP/IP host. Rosters use separate companion files.");
    return true;
}
void Uninstall() {
    if (!ready.exchange(false)) return;
    for (auto hook : {0x29b4c0u, 0x367f60u}) MH_QueueDisableHook(Native<void*>(hook));
    if (MH_ApplyQueued() == MH_OK) for (auto hook : {0x29b4c0u, 0x367f60u}) MH_RemoveHook(Native<void*>(hook));
    std::lock_guard lock(mutex);
    pets.clear(); sessions.clear(); loading.clear();
}
} // namespace SummonPersistence
