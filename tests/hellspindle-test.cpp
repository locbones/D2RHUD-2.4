// Real MinHook detours and native entry prefixes in a private executable image.
// Native allocation, unit lookup, rendering and cleanup are mocked boundaries.
#include "../d2rhud/plugin/HellSpindle.cpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <vector>

namespace {
uint8_t* image = nullptr;
uint64_t creationCalls = 0, serverUpdates = 0, clientUpdates = 0, cleanupCalls = 0;
float lastDelta = 0;
void* nextCreated = nullptr;
std::map<uint64_t, void*> units;
uint64_t Key(uint32_t type, uint32_t id) { return (uint64_t(type) << 32) | id; }
void* __fastcall CreateS(void*, void*) { ++creationCalls; return nextCreated; }
void* __fastcall CreateC(void*, int mode) { assert(mode == 7); ++creationCalls; return nextCreated; }
int __fastcall UpdateS(void*, void*) { ++serverUpdates; return 1; }
void __fastcall UpdateC(void*, float delta) { ++clientUpdates; lastDelta = delta; }
void __fastcall Cleanup(void*) { ++cleanupCalls; }
void* __fastcall LookupS(void*, uint32_t type, uint32_t id) {
    auto found = units.find(Key(type, id)); return found == units.end() ? nullptr : found->second;
}
void* __fastcall Competitor(void*, int) { return nullptr; }
}
DWORD64 Pattern::Address(uint32_t rva) { return reinterpret_cast<uintptr_t>(image) + rva; }

using namespace HellSpindle;
struct Entity {
    std::array<uint8_t, 512> unit{}, data{}, list{}, skill{}, row{};
};
void Bind(Entity& entity, uint32_t type, uint32_t id) {
    Put(entity.unit.data(), 0, type); Put(entity.unit.data(), 8, id);
    units[Key(type, id)] = entity.unit.data();
    const auto table = type == 3 ? 0x1d466e0 : 0x1d442e0 + type * 0x400;
    auto head = image + table + (id & 127) * 8;
    Put(entity.unit.data(), 0x150, Field<void*>(head)); Put(head, 0, entity.unit.data());
}
void Remove(uint32_t id) {
    auto key = Key(3, id); auto found = units.find(key); if (found == units.end()) return;
    auto link = image + 0x1d466e0 + (id & 127) * 8;
    while (auto current = Field<uint8_t*>(link)) {
        if (current == found->second) { Put(link, 0, Field<void*>(current, 0x150)); break; }
        link = current + 0x150;
    }
    units.erase(found);
}
struct Host {
    std::array<uint8_t, 1024> game{};
    std::array<uint8_t, 1536> client{};
    std::vector<std::unique_ptr<Entity>> entities;
    int side;
    explicit Host(int side, int hard = 10) : side(side) {
        sides = {}; units.clear();
        std::memset(image + 0x1d442e0, 0, 0x2800);
        Put(game.data(), 0x101, uint8_t(3)); Put(game.data(), 0x144, uint32_t(111));
        Put(client.data(), 0x230, game.data()); Put(image, 0x1d637f0, client.data());
        Put(image, 0x1d617d0, uint64_t(123));
        Player(1, hard);
    }
    Entity& Allocate() { entities.push_back(std::make_unique<Entity>()); return *entities.back(); }
    Entity& Player(uint32_t id, int hard) {
        auto& p = Allocate();
        Put(p.unit.data(), 0x100, p.list.data()); Put(p.list.data(), 0, p.skill.data());
        Put(p.skill.data(), 0, p.row.data()); Put(p.row.data(), 0, uint16_t(Skill));
        Put(p.skill.data(), 0x38, hard); Put(p.skill.data(), 0x44, -1);
        Bind(p, 0, id); return p;
    }
    Entity& Missile(uint32_t id, uint32_t owner = 1, uint32_t code = Emitter, uint16_t skill = Skill, uint32_t type = 0) {
        auto& m = Allocate();
        Put(m.unit.data(), 4, code); Put(m.unit.data(), 0x10, m.data.data());
        Put(m.data.data(), 0xc, skill); Put(m.data.data(), 0x10, uint16_t(90));
        Put(m.data.data(), 0x14, 3600.0f); Put(m.data.data(), 0x30, uint32_t(44));
        Put(m.unit.data(), 0xe8, type); Put(m.unit.data(), 0xec, owner);
        Put(m.unit.data(), 0x128, uint32_t(0x400)); Bind(m, 3, id); return m;
    }
    void* Create(Entity* m, uint32_t code = Emitter) {
        std::array<uint8_t, 128> parameters{};
        Put(parameters.data(), 0x20, code); nextCreated = m ? m->unit.data() : nullptr;
        const auto calls = creationCalls;
        auto result = side ? Native<CreateClient>(ClientCreate)(parameters.data(), 7) :
            Native<CreateServer>(ServerCreate)(game.data(), parameters.data());
        assert(result == nextCreated && creationCalls == calls + 1); return result;
    }
    int Tick(Entity& m, float delta = 40.0f) {
        auto oldServer = serverUpdates, oldClient = clientUpdates, oldCleanup = cleanupCalls;
        if (side) {
            Native<UpdateClient>(ClientUpdate)(m.unit.data(), delta);
            if (cleanupCalls != oldCleanup) { assert(clientUpdates == oldClient); return 2; }
            assert(clientUpdates == oldClient + 1 && lastDelta == delta); return 1;
        }
        const auto result = Native<UpdateServer>(ServerUpdate)(game.data(), m.unit.data());
        assert(serverUpdates == oldServer + (result == 1)); return result;
    }
};

void WriteJump(uint32_t rva, void* target, bool popClient = false) {
    std::vector<uint8_t> code;
    // Client update's relocated first five bytes push RBX/RSI. Balance those
    // before entering our mock; the other prefixes only store shadow space.
    if (popClient) { code.push_back(0x5e); code.push_back(0x5b); }
    code.insert(code.end(), {0x48, 0xb8});
    auto address = reinterpret_cast<uintptr_t>(target);
    for (size_t n = 0; n < 8; ++n) code.push_back(uint8_t(address >> (8 * n)));
    code.insert(code.end(), {0xff, 0xe0});
    std::memcpy(image + rva, code.data(), code.size());
    FlushInstructionCache(GetCurrentProcess(), image + rva, code.size());
}

int main(int argc, char** argv) {
    assert(argc == 2);
    std::ifstream input(std::filesystem::path(argv[1]), std::ios::binary | std::ios::ate);
    assert(input); std::vector<uint8_t> file(size_t(input.tellg())); input.seekg(0);
    assert(input.read(reinterpret_cast<char*>(file.data()), std::streamsize(file.size())));
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(file.data());
    assert(dos->e_magic == IMAGE_DOS_SIGNATURE);
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(file.data() + dos->e_lfanew);
    assert(nt->Signature == IMAGE_NT_SIGNATURE && nt->OptionalHeader.SizeOfImage == 0x261a000);
    image = static_cast<uint8_t*>(VirtualAlloc(nullptr, nt->OptionalHeader.SizeOfImage, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    assert(image);
    std::memcpy(image, file.data(), nt->OptionalHeader.SizeOfHeaders);
    auto sections = IMAGE_FIRST_SECTION(nt);
    for (unsigned n = 0; n < nt->FileHeader.NumberOfSections; ++n)
        std::memcpy(image + sections[n].VirtualAddress, file.data() + sections[n].PointerToRawData, sections[n].SizeOfRawData);
    Put(image, 0x14c1478, Pattern::Address(ServerUpdate));
    Put(image, 0x14b7338, Pattern::Address(ClientUpdate));
    std::array<std::array<uint8_t, 5>, 4> prefixes{};
    for (size_t n = 0; n < hooks.size(); ++n) std::memcpy(prefixes[n].data(), image + hooks[n], 5);
    std::memcpy(image + 0x1bf084f, "other", 6); assert(!Install());
    std::memcpy(image + 0x1bf084f, "RMD-MP", 7);
    for (const auto& signature : signatures) {
        image[signature.rva] ^= 1; assert(!Install() && !installed.load()); image[signature.rva] ^= 1;
    }
    Put(image, 0x14c1478, Pattern::Address(ServerUpdate) + 1); assert(!Install());
    Put(image, 0x14c1478, Pattern::Address(ServerUpdate));
    assert(MH_Initialize() == MH_OK);
    void* competitor = nullptr;
    assert(MH_CreateHook(image + ClientCreate, reinterpret_cast<void*>(Competitor), &competitor) == MH_OK);
    assert(!Install()); // Roll back our first hook; retain the other component's.
    assert(MH_RemoveHook(image + ClientCreate) == MH_OK);
    assert(Install() && Install());
    WriteJump(ServerCreate + 5, reinterpret_cast<void*>(CreateS));
    WriteJump(ClientCreate + 5, reinterpret_cast<void*>(CreateC));
    WriteJump(ServerUpdate + 5, reinterpret_cast<void*>(UpdateS));
    WriteJump(ClientUpdate + 5, reinterpret_cast<void*>(UpdateC), true);
    WriteJump(0x2c49c0, reinterpret_cast<void*>(LookupS));
    WriteJump(0xffd70, reinterpret_cast<void*>(Cleanup));
    std::cout << "Native signatures, mod/competitor refusal, rollback, four real detours and ABI passthrough passed.\n";

    for (int side = 0; side < 2; ++side) {
        for (int hard : {1, 9, 10, 11}) {
            Host h(side, hard); const auto cap = 1 + (hard == 10);
            std::vector<Entity*> ms;
            for (int n = 0; n <= cap; ++n) { ms.push_back(&h.Missile(100 + n)); h.Create(ms.back()); }
            assert(!Alive(ms[0]->unit.data()) && h.Tick(*ms[0]) == 2);
            assert(Field<uint32_t>(ms[0]->data.data(), 0x30) == 44);
            for (int n = 1; n <= cap; ++n) assert(Alive(ms[n]->unit.data()) && h.Tick(*ms[n], 1.25f) == 1);
        }
        {
            Host h(side); auto& a = h.Missile(100); auto& b = h.Missile(101);
            h.Create(&a); h.Create(&b);
            const auto counters = sides[side].counters;
            for (int n = 0; n < 100000; ++n) { assert(h.Tick(a, 1.25f) == 1); assert(h.Tick(b, 39.75f) == 1); }
            const auto& after = sides[side].counters;
            assert(after.created == counters.created && after.retired == counters.retired &&
                after.lookups == counters.lookups && after.limitReads == counters.limitReads);
            // Newly emitted ground fire is outside the limiter's scope.
            auto& flames = h.Missile(102, 1, 69); h.Create(&flames, 69);
            auto& c = h.Missile(103); h.Create(&c);
            assert(!Alive(a.unit.data()) && Alive(flames.unit.data()) && h.Tick(flames) == 1);
            Put(a.unit.data(), 4, uint32_t(1022)); assert(h.Tick(a) == 1); // Unrelated expired emitter passes through.
        }
        {
            Host h(side); h.Player(2, 10);
            auto& a = h.Missile(100); auto& b = h.Missile(101); auto& c = h.Missile(102, 2);
            h.Create(&a); h.Create(&b); h.Create(&c);
            assert(Alive(a.unit.data()) && Alive(b.unit.data()) && Alive(c.unit.data()));
            // Natural expiration frees a slot without evicting the newer cast.
            Put(a.data.data(), 0x14, 0.0f); auto& d = h.Missile(103); h.Create(&d);
            assert(Alive(b.unit.data()) && Alive(d.unit.data()) && h.Tick(a) == 2);
            // A vanished/reused unit ID cannot count twice or retire the new unit.
            Remove(101); auto& reused = h.Missile(101); h.Create(&reused);
            assert(Alive(d.unit.data()) && Alive(reused.unit.data()));
            // Session changes clear stale tracking even at the same game address.
            Put(image, 0x1d617d0, uint64_t(124)); auto& fresh = h.Missile(104); h.Create(&fresh);
            assert(Alive(d.unit.data()) && Alive(reused.unit.data()));
            auto& fresh2 = h.Missile(105); h.Create(&fresh2);
            Put(h.game.data(), 0x144, uint32_t(222)); auto& seeded = h.Missile(106); h.Create(&seeded);
            assert(Alive(fresh.unit.data()) && Alive(fresh2.unit.data()));
        }
        {
            Host h(side); auto& a = h.Missile(100); h.Create(&a);
            const auto before = sides[side].counters.created;
            h.Create(nullptr);
            auto& monster = h.Missile(101, 7, Emitter, Skill, 1); h.Create(&monster);
            auto& otherSkill = h.Missile(102, 1, Emitter, 112); h.Create(&otherSkill);
            auto& otherEmitter = h.Missile(103, 1, 1022); h.Create(&otherEmitter, 1022);
            auto& orphan = h.Missile(104, 999); h.Create(&orphan);
            Put(h.game.data(), 0x101, uint8_t(2)); auto& online = h.Missile(105); h.Create(&online);
            assert(sides[side].counters.created == before && Alive(a.unit.data()) && h.Tick(online) == 1);
            Put(online.data.data(), 0x10, uint16_t(0)); assert(h.Tick(online) == 1); // Multiplayer preserves even native expiry behavior.
        }
        {
            Host h(side); Entity* previous = nullptr;
            for (uint32_t n = 0; n < 10000; ++n) {
                auto& current = h.Missile(1000 + n); h.Create(&current);
                if (n >= 2) {
                    auto& old = *h.entities[h.entities.size() - 3];
                    assert(!Alive(old.unit.data()) && h.Tick(old) == 2);
                    Remove(Field<uint32_t>(old.unit.data(), 8));
                }
                assert(h.Tick(current) == 1); previous = &current;
            }
            assert(previous && sides[side].counters.created == 10000 && sides[side].counters.limitReads == 10000 &&
                sides[side].counters.retired == 9998 && sides[side].counters.lookups <= 30000);
        }
        std::cout << "Side " << side << ": caps, replacement/no final emission, natural expiry, ID reuse, owners, sessions, exclusions and 10,000 rapid creations passed.\n";
    }
    Uninstall(); assert(!installed.load());
    for (size_t n = 0; n < hooks.size(); ++n) assert(std::memcmp(image + hooks[n], prefixes[n].data(), 5) == 0);
    assert(MH_Uninitialize() == MH_OK);
    VirtualFree(image, 0, MEM_RELEASE);
    std::cout << "400,000 active updates performed zero limiter lookups, cap checks or skill-list scans.\n";
}
