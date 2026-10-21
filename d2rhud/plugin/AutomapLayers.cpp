#include "AutomapLayers.h"
#include "AutomapLayersData.h"
#include "CubeMap.h"
#include "../Pattern.h"
#include <windows.h>
#include <tlhelp32.h>
#include <detours/detours.h>
#include <atomic>
#include <exception>
#include <map>
#include <mutex>

namespace AutomapLayers {
namespace {
// Audited against D2R 1.4.71510. The cache owns its allocation and disk writes;
// retain its SRW lock, allocator, dirty state and native seed/difficulty resolver.
struct CachedFile {
    SRWLOCK lock;
    uint8_t* data;
    uint64_t size, capacity;
    uint8_t reserved[0x38];
    uint32_t state;
};
struct NativeVector { const uint8_t* data; uint64_t size, capacity; };
struct NativeRecord {
    uint32_t layer, special, metadata;
    std::array<uint32_t, 4> counts;
    uint32_t padding;
    std::array<NativeVector, 4> groups;
};
struct Layer { int32_t id; uint32_t special; uint8_t trees[0xa0]; };
static_assert(offsetof(CachedFile, data) == 8 && offsetof(CachedFile, size) == 0x10 && offsetof(CachedFile, state) == 0x58);
static_assert(offsetof(NativeRecord, groups) == 0x20 && sizeof(NativeRecord) == 0x80);
static_assert(sizeof(Layer) == 0xa8);
using Save = void(__fastcall*)(const NativeRecord*, void*);
using Load = void(__fastcall*)(void*);
struct Bindings {
    CachedFile* (__fastcall* resolve)(void*) = nullptr;
    void (__fastcall* resize)(void*, size_t) = nullptr;
    void (__fastcall* insert)(uint32_t, uint64_t, void*, int) = nullptr;
    Layer** current = nullptr;
    int32_t* imageCount = nullptr;
    uint8_t* specialSets = nullptr;
} engine;
Save originalSave = nullptr;
Load originalLoad = nullptr;
bool installed = false, attempted = false;
std::atomic<bool> reported{false};
struct TransientLayer {uint64_t seed=0;std::vector<Data::Record> records;};
std::map<uint32_t,TransientLayer> transientLayers;
std::mutex transientMutex;
void Report() {
    if (!reported.exchange(true)) OutputDebugStringA("D2RHUD: extended automap data unavailable or invalid; original map data was preserved.\n");
}
struct SharedLock {
    SRWLOCK* lock;
    explicit SharedLock(SRWLOCK* value) : lock(value) { AcquireSRWLockShared(lock); }
    ~SharedLock() { ReleaseSRWLockShared(lock); }
};
struct ExclusiveLock {
    SRWLOCK* lock;
    explicit ExclusiveLock(SRWLOCK* value) : lock(value) { AcquireSRWLockExclusive(lock); }
    ~ExclusiveLock() { ReleaseSRWLockExclusive(lock); }
};
bool CacheView(const CachedFile& file, std::span<const uint8_t>& bytes) {
    if (file.size > UINT32_MAX || (file.size && !file.data)) return false;
    bytes = {file.data, static_cast<size_t>(file.size)};
    return true;
}
void __fastcall HookSave(const NativeRecord* record, void* map) {
    if (!record) return;
    if(const auto seed=CubeMap::ClientLayerSeed(record->layer)){
        try{
        if(record->special>3)return;
        Data::Record saved;saved.special=record->special;
        for(size_t group=0;group<4;++group){
            const auto count=record->counts[group];const auto& input=record->groups[group];
            if(count%sizeof(Data::Cell) || count>64*1024*1024 || (count && (!input.data || input.size<count/2)))return;
            saved.cells[group].resize(count/sizeof(Data::Cell));if(count)memcpy(saved.cells[group].data(),input.data,count);
        }
        std::lock_guard lock(transientMutex);auto& cached=transientLayers[record->layer];
        if(cached.seed!=seed){cached.records.clear();cached.seed=seed;}
        cached.records.push_back(std::move(saved));
        }catch(const std::exception&){Report();}
        return;
    }
    if (record->layer < Data::NativeLayers) { originalSave(record, map); return; }
    // Never send an extended or negative layer to the native 100-entry stack
    // table, even after a malformed file or allocation failure.
    try {
        if (record->layer > Data::MaxLayer || !map) { Report(); return; }
        Data::RecordView view; view.layer = record->layer; view.special = record->special; view.metadata = record->metadata;
        for (size_t group = 0; group < view.cells.size(); ++group) {
            const auto count = record->counts[group];
            const auto& native = record->groups[group];
            if (count && (!native.data || count % sizeof(Data::Cell) || native.size < count / 2)) { Report(); return; }
            view.cells[group] = {native.data, count};
        }
        auto file = engine.resolve(map);
        if (!file) { Report(); return; }
        ExclusiveLock lock(&file->lock);
        std::span<const uint8_t> bytes;
        std::vector<uint8_t> updated;
        if (!CacheView(*file, bytes) || !Data::Append(bytes, view, updated)) { Report(); return; }
        engine.resize(&file->data, updated.size());
        std::memcpy(file->data, updated.data(), updated.size());
        file->state = 4; // Native writable/dirty state, flushed by the game.
    } catch (const std::exception&) { Report(); }
}
void __fastcall HookLoad(void* map) {
    auto layer = engine.current ? *engine.current : nullptr;
    if (!layer || layer->id < 0) return;
    const auto transientSeed=CubeMap::ClientLayerSeed(uint32_t(layer->id));
    if (!transientSeed && static_cast<uint32_t>(layer->id) < Data::NativeLayers) { originalLoad(map); return; }
    try {
        if (!map) return;
        std::vector<Data::Record> records;
        if(transientSeed){
            std::lock_guard lock(transientMutex);const auto found=transientLayers.find(uint32_t(layer->id));
            if(found!=transientLayers.end() && found->second.seed==transientSeed)records=found->second.records;
        }else{
            auto file = engine.resolve(map);if(!file){Report();return;}
            SharedLock lock(&file->lock);
            std::span<const uint8_t> bytes;
            if (!CacheView(*file, bytes) || !Data::Load(bytes, static_cast<uint32_t>(layer->id), records)) { Report(); return; }
        }
        // The native layer creation/switch code still owns all four cell trees.
        // Mark restored cells as saved so the native serializer emits only new
        // exploration on the next switch or exit, just as for vanilla layers.
        for (const auto& record : records) for (size_t group = 0; group < record.cells.size(); ++group) {
            int32_t limit = *engine.imageCount;
            if (group == 3) {
                if (record.special > 3) continue;
                std::memcpy(&limit, engine.specialSets + record.special * 0x58, sizeof(limit));
            }
            for (const auto& cell : record.cells[group]) {
                // A mod may have changed sprite tables since this map was saved.
                if (limit > 0 && cell.image >= static_cast<uint32_t>(limit)) continue;
                const uint64_t point = uint64_t(uint32_t(int32_t(cell.x))) | (uint64_t(uint32_t(int32_t(cell.y))) << 32);
                engine.insert(cell.image, point, reinterpret_cast<uint8_t*>(layer) + 8 + group * 0x28, 1);
            }
        }
    } catch (const std::exception&) { Report(); }
}
struct Signature { uint32_t rva, size; uint64_t hash; };
// Full code hashes cover both replaced functions and the native helpers/layouts
// they depend on. No instruction in D2R.exe is patched on disk.
constexpr std::array<Signature, 6> signatures{{
    {0x94a40, 0x661, 0x27391d6b1ff38cb8ull}, // save record / native stack table
    {0x95640, 0xa1d, 0xe8903e59a8d21e14ull}, // load record / native stack table
    {0x946a0, 0x39b, 0x93901e88f3de066aull}, // map seed history and .ma file resolver
    {0x94310, 0xec, 0x12391aec7bf889daull},  // insert cell (loaded flag)
    {0x10ed40, 0xcf, 0xb5a4c8de5dc9ed62ull}, // native byte-vector resize
    {0x96060, 0x156, 0x1efdbba9e8ca341full} // dynamic layer list / current pointer
}};
uint64_t Hash(std::span<const uint8_t> bytes) {
    uint64_t hash = 14695981039346656037ull;
    for (auto byte : bytes) hash = (hash ^ byte) * 1099511628211ull;
    return hash;
}
template<class Address> bool Supported(Address&& address) {
    std::array<uint8_t, 0xa1d> bytes;
    for (const auto& signature : signatures) {
        SIZE_T count = 0;
        if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address(signature.rva)), bytes.data(), signature.size, &count) ||
            count != signature.size || Hash({bytes.data(), signature.size}) != signature.hash) return false;
    }
    return true;
}
// Detours must relocate instruction pointers for every other process thread,
// including the simulation thread, when committing either hook transaction.
struct Threads {
    std::vector<HANDLE> handles;
    ~Threads() { for (auto handle : handles) CloseHandle(handle); }
    bool Open() {
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snapshot == INVALID_HANDLE_VALUE) return false;
        THREADENTRY32 entry{}; entry.dwSize = sizeof(entry);
        BOOL more = Thread32First(snapshot, &entry); bool ok = more != FALSE;
        while (more) {
            if (entry.th32OwnerProcessID == GetCurrentProcessId() && entry.th32ThreadID != GetCurrentThreadId()) {
                auto handle = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT, FALSE, entry.th32ThreadID);
                if (handle) handles.push_back(handle);
                else if (GetLastError() != ERROR_INVALID_PARAMETER) { ok = false; break; }
            }
            more = Thread32Next(snapshot, &entry);
        }
        CloseHandle(snapshot); return ok;
    }
    bool Update() {
        for (auto handle : handles) if (DetourUpdateThread(handle) != NO_ERROR) return false;
        return DetourUpdateThread(GetCurrentThread()) == NO_ERROR;
    }
};
}
bool Install() {
    if (attempted) return installed;
    attempted = true;
    if (!Supported([](uint32_t rva) { return Pattern::Address(rva); })) {
        OutputDebugStringA("D2RHUD: extended automap layers disabled; executable signature mismatch.\n"); return false;
    }
    engine = {
        reinterpret_cast<decltype(engine.resolve)>(Pattern::Address(0x946a0)),
        reinterpret_cast<decltype(engine.resize)>(Pattern::Address(0x10ed40)),
        reinterpret_cast<decltype(engine.insert)>(Pattern::Address(0x94310)),
        reinterpret_cast<Layer**>(Pattern::Address(0x1d56150)),
        reinterpret_cast<int32_t*>(Pattern::Address(0x1c9b89c)),
        reinterpret_cast<uint8_t*>(Pattern::Address(0x1c9b898))
    };
    originalSave = reinterpret_cast<Save>(Pattern::Address(0x94a40));
    originalLoad = reinterpret_cast<Load>(Pattern::Address(0x95640));
    Threads threads;
    if (!threads.Open() || DetourTransactionBegin() != NO_ERROR) return false;
    if (!threads.Update() || DetourAttach(reinterpret_cast<PVOID*>(&originalSave), HookSave) != NO_ERROR ||
        DetourAttach(reinterpret_cast<PVOID*>(&originalLoad), HookLoad) != NO_ERROR) { DetourTransactionAbort(); return false; }
    installed = DetourTransactionCommit() == NO_ERROR;
    return installed;
}
void Uninstall() {
    if (!installed) return;
    Threads threads;
    if (!threads.Open() || DetourTransactionBegin() != NO_ERROR) return;
    if (!threads.Update() || DetourDetach(reinterpret_cast<PVOID*>(&originalSave), HookSave) != NO_ERROR ||
        DetourDetach(reinterpret_cast<PVOID*>(&originalLoad), HookLoad) != NO_ERROR) { DetourTransactionAbort(); return; }
    if (DetourTransactionCommit() == NO_ERROR) installed = false;
}
}
