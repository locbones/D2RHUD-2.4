#define NOMINMAX
#include "../d2rhud/d2/Reclass.h"
#include "../d2rhud/plugin/D2RHUD/HudPreferences.cpp"
#include "../d2rhud/plugin/MercSeller.cpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <malloc.h>

uintptr_t testBase = 0;
std::map<uint32_t, uintptr_t> bindings;
DWORD64 Pattern::Address(uint32_t rva) { return testBase ? testBase + rva : bindings[rva]; }
namespace {
using namespace MercSeller;
using namespace MercSeller::Data;
using Unit = D2UnitStrc;
struct Fixture {
    std::vector<uint8_t> monsters = std::vector<uint8_t>(4100 * 0x1dc);
    std::vector<uint8_t> rows;
    std::array<uint8_t, NativeNpcCount * MetadataSize> metadata{};
    std::array<uint8_t, 0x6000> game{};
    Fixture() {
        for (int id : {150, 198, 252, 515, 367, 4000})
            monsters[size_t(id) * 0x1dc + 0x3d] = 2;
    }
    void Row(int seller, int version, int difficulty, int act, int names = 1000, int last = 1009) {
        const auto offset = rows.size();
        rows.resize(offset + HirelingSize);
        auto p = rows.data() + offset;
        Put<uint16_t>(p, 0, uint16_t(version));
        Put<int32_t>(p, 4, 1);
        Put<int32_t>(p, 8, 271);
        Put<int32_t>(p, 0xc, act);
        Put<int32_t>(p, 0x10, difficulty);
        Put<int32_t>(p, 0x14, seller);
        Put<int32_t>(p, 0x1c, 3);
        Put<uint16_t>(p, 0x14c, uint16_t(names));
        Put<uint16_t>(p, 0x14e, uint16_t(last));
    }
    Catalog BuildCatalog() { return Build(rows, monsters, metadata); }
    void Bind() {
        static uintptr_t rowPointer, monsterPointer;
        static uint64_t count, monsterCount;
        rowPointer = uintptr_t(rows.data());
        monsterPointer = uintptr_t(monsters.data());
        count = rows.size() / HirelingSize;
        monsterCount = monsters.size() / 0x1dc;
        bindings[0x1c9ee58] = uintptr_t(&rowPointer);
        bindings[0x1c9ee60] = uintptr_t(&count);
        bindings[0x1c9f878] = uintptr_t(&monsterPointer);
        bindings[0x1c9f880] = uintptr_t(&monsterCount);
        bindings[0x1852470] = uintptr_t(metadata.data());
        catalog.reset();
        retiredCatalogs.clear();
        catalogSource = {};
    }
};
void Policy() {
    Fixture f;
    Put<int16_t>(f.metadata.data(), 0, 150);
    f.metadata[7] = 1;
    f.metadata[0x10] = 3;
    f.metadata[0x11] = 8;
    Put<int16_t>(f.metadata.data() + MetadataSize, 0, 367);
    f.metadata[MetadataSize + 0x10] = 42;
    f.Row(150, 0, 1, 1);
    f.Row(367, 0, 1, 4);
    f.Row(4000, 100, 2, 4);
    f.Row(4000, 100, 3, 4);
    const auto data = f.BuildCatalog();
    assert(data.Find(150, false, 0) && (*data.Find(150, false, 0))[0x10] == 3 &&
           (*data.Find(150, false, 0))[0x11] == 8);
    assert(data.Find(367, false, 0) && (*data.Find(367, false, 0))[7] == 1 &&
           (*data.Find(367, false, 0))[0xa] == 3 && !(*data.Find(367, false, 0))[0x10]);
    assert(data.Find(4000, true, 1) && data.Find(4000, true, 2));
    assert(!data.Find(4000, false, 1) && !data.Find(4000, true, 0) && !data.Find(198, false, 0));
    f.Row(4001, 100, 1, 4);
    assert(!f.BuildCatalog().Find(4001, true, 0)); // not an NPC
    f.Row(4000, 0, 3, 4, 1010, 1000);
    f.Row(4000, 0, 3, 4);
    assert(!f.BuildCatalog().Find(4000, false, 2)); // matches the native first-row policy
    f.Row(367, 100, 1, 4, 1000, 1069);
    assert(!f.BuildCatalog().Find(367, true, 0)); // fixed native roster capacity
    for (bool rmd : {false, true}) {
        HudPreferences::Config config;
        HudPreferences::Load(config, HudConfig::Json::object(), rmd);
        assert(config.MercSellerUnlock);
        config.MercSellerUnlock = true;
        HudConfig::Json json;
        HudPreferences::Save(config, json);
        HudPreferences::Config restored;
        HudPreferences::Load(restored, json, rmd);
        assert(restored.MercSellerUnlock);
        HudPreferences::Load(restored, {{"MercSellerUnlock", false}}, rmd);
        assert(!restored.MercSellerUnlock);
        HudPreferences::Save(restored, json);
        HudPreferences::Config disabled;
        HudPreferences::Load(disabled, json, rmd);
        assert(!disabled.MercSellerUnlock);
    }
}
void Dialogs() {
    std::array<uint8_t, 0x5d0 + 32> bytes{};
    std::fill(bytes.end() - 32, bytes.end(), uint8_t(0xa5));
    auto dialog = bytes.data();
    Put<int32_t>(dialog, 0x1c, 3);
    Put<int32_t>(dialog, 0x18, 1);
    auto entries = reinterpret_cast<DialogEntry*>(dialog + 0x30);
    entries[0].color = 4;
    entries[1].callback = 123;
    entries[2].callback = 456;
    assert(AddHire(dialog, "Hire", 789));
    assert(Get<int32_t>(dialog, 0x1c) == 4 && Get<int32_t>(dialog, 0x18) == 2);
    assert(entries[1].callback == 123 && entries[2].callback == 789 && entries[3].callback == 456);
    assert(!AddHire(dialog, "Hire", 789));
    Put<int32_t>(dialog, 0x1c, 10);
    assert(!AddHire(dialog, "Hire", 999));
    assert(std::all_of(bytes.end() - 32, bytes.end(), [](auto b) { return b == 0xa5; }));
}
std::array<uintptr_t, 5> allocatorVtable{};
struct Allocator {
    uintptr_t* vtable = allocatorVtable.data();
} allocator;
void* __fastcall AllocatorGet() { return &allocator; }
void* __fastcall AllocateMock(void*, size_t size, size_t align) { return _aligned_malloc(size, align); }
void __fastcall FreeMock(void*, void* pointer) { _aligned_free(pointer); }
std::array<uint8_t, 0x5d0> menuDialog{};
RosterRequest sentRequest{};
int sentRequests = 0;
void __fastcall BuildMenuMock() {
    menuDialog = {};
    Put<int32_t>(menuDialog.data(), 0x1c, 3);
    Put<int32_t>(menuDialog.data(), 0x18, 1);
    auto entries = reinterpret_cast<DialogEntry*>(menuDialog.data() + 0x30);
    entries[1].callback = 123;
    entries[2].callback = 456;
}
const char* __fastcall HireLabelMock(int id, void*) {
    assert(id == 3397);
    return "Localized Hire";
}
void __fastcall SendMock(const void* bytes, int size) {
    assert(size == 9);
    memcpy(&sentRequest, bytes, 9);
    ++sentRequests;
}
void Menus() {
    testBase = 0;
    Fixture f;
    f.Row(367, 100, 1, 4);
    f.Row(4000, 100, 1, 4);
    f.Bind();
    auto data = Current();
    assert(data);
    allocatorVtable[1] = uintptr_t(AllocateMock);
    allocatorVtable[4] = uintptr_t(FreeMock);
    bindings[0xacdd80] = uintptr_t(AllocatorGet);
    std::array<Menu, 2> original{};
    Put<int16_t>(original[0].data(), 0, 367);
    Put<int32_t>(original[0].data(), 4, 3);
    Put<int16_t>(original[1].data(), 0, 150);
    Put<int32_t>(original[1].data(), 4, 4);
    NativeVector vector{original.data(), 2, (1ULL << 63) | 2};
    bindings[0x1c9e608] = uintptr_t(&vector);
    requested = true;
    assert(EnsureMenus(*data));
    assert(vector.size == 3 && vector.data != original.data());
    assert(!memcmp(vector.data, original.data(), sizeof(original)) &&
           Field<int16_t>(vector.data, 2 * MenuSize) == 4000);
    auto pointer = vector.data;
    assert(EnsureMenus(*data) && vector.data == pointer && vector.size == 3);
    requested = false;
    assert(EnsureMenus(*data) && vector.size == 2 && vector.data == pointer);
    requested = true;
    assert(EnsureMenus(*data) && vector.size == 3 && vector.data == pointer);
    uint32_t selected = 1, guid = 42;
    uint16_t seller = 4000;
    uint8_t difficulty = 0;
    std::array<uint8_t, 0x60> configuration{};
    Put<uint32_t>(configuration.data(), 0x5c, 1);
    void* configPointer = configuration.data();
    void* dialogPointer = menuDialog.data();
    void* strings = nullptr;
    bindings[0x1d60c10] = uintptr_t(&selected);
    bindings[0x1d60c0c] = uintptr_t(&guid);
    bindings[0x1d60c14] = uintptr_t(&seller);
    bindings[0x1c9b3f0] = uintptr_t(&configPointer);
    bindings[0x1bf1ecc] = uintptr_t(&difficulty);
    bindings[0x1d60c60] = uintptr_t(&dialogPointer);
    bindings[0x1856688] = uintptr_t(&strings);
    bindings[0x413860] = uintptr_t(HireLabelMock);
    bindings[0x10e710] = uintptr_t(SendMock);
    bindings[0x13b910] = 789;
    originalMenu = BuildMenuMock;
    ready = true;
    MenuHook();
    assert(sentRequests == 1 && sentRequest.opcode == 0x38 && sentRequest.action == 3 &&
           sentRequest.npc == 42);
    auto entries = reinterpret_cast<DialogEntry*>(menuDialog.data() + 0x30);
    assert(Get<int32_t>(menuDialog.data(), 0x1c) == 4 && !strcmp(entries[2].text, "Localized Hire") &&
           entries[3].callback == 456);
    difficulty = 1;
    MenuHook();
    assert(sentRequests == 1 && Get<int32_t>(menuDialog.data(), 0x1c) == 3);
    difficulty = 0;
    requested = false;
    MenuHook();
    assert(sentRequests == 1 && vector.size == 2);
    ready = false;
    originalMenu = nullptr;
    Free(pointer);
    extendedMenus = {};
}
uintptr_t captured = 0;
void UnitProfiles() {
    testBase = 0;
    Fixture f;
    for (int version : {0, 100})
        for (int difficulty : {1, 2, 3}) {
            f.Row(367, version, difficulty, 4, 10835, 10901);
            Put<int32_t>(f.rows.data() + f.rows.size() - HirelingSize, 8, 944);
            // Another type from the same seller must not disappear from the index.
            f.Row(367, version, difficulty, 4, 10835, 10901);
            Put<int32_t>(f.rows.data() + f.rows.size() - HirelingSize, 8, 945);
        }
    f.Bind();
    uint8_t clientDifficulty = 0;
    bindings[0x1bf1ecc] = uintptr_t(&clientDifficulty);
    ready = requested = true;
    Unit unit{};
    unit.dwUnitType = 1;
    for (int version : {0, 100})
        for (int difficulty : {0, 1, 2})
            for (uint32_t type : {944u, 945u}) {
                unit.dwClassId = type;
                clientDifficulty = uint8_t(difficulty);
                Put<uint32_t>(f.game.data(), 0x108, version == 100);
                Put<uint8_t>(f.game.data(), 0x104, uint8_t(difficulty));
                const auto row = ResolveUnitName(f.game.data(), &unit, 10835, version);
                assert(row && Field<uint32_t>(row, 8) == type &&
                       Field<int32_t>(row, 0x10) == difficulty + 1 && Field<uint16_t>(row) == version);
                assert(ResolveUnitName(nullptr, &unit, 10901, version) == row);
                assert(!ResolveUnitName(f.game.data(), &unit, 10902, version));
                assert(!ResolveUnitName(f.game.data(), &unit, 10834, version));
                requested = false;
                assert(!ResolveUnitName(f.game.data(), &unit, 10835, version));
                requested = true;
            }
    unit.dwUnitType = 0;
    assert(!ResolveUnitName(f.game.data(), &unit, 10835, 100));
    assert(!ResolveUnitName(f.game.data(), nullptr, 10835, 100));
    unit.dwUnitType = 1;
    assert(!ResolveUnitName(f.game.data(), &unit, 10835, 99));
    assert(!ResolveUnitName(f.game.data(), &unit, 10835, 0)); // expansion mismatch
    Put<uint8_t>(f.game.data(), 0x104, 3);
    assert(!ResolveUnitName(f.game.data(), &unit, 10835, 100));
    ready = requested = false;
}
std::array<uint8_t, 32> purchaseContinuation{};
int traceChecks = 0;
void TraceRelay() {
    CONTEXT context{};
    RtlCaptureContext(&context);
    bool crossed = false;
    const auto exe = uintptr_t(GetModuleHandleW(nullptr));
    const auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(exe);
    const auto nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(exe + dos->e_lfanew);
    for (int i = 0; i < 12 && context.Rip; ++i) {
        const bool inRelay =
            context.Rip >= uintptr_t(relay) && context.Rip < uintptr_t(relay) + hooks.size() * 0x200;
        DWORD64 base = 0;
        auto function = RtlLookupFunctionEntry(context.Rip, &base, nullptr);
        assert(function);
        const auto stack = context.Rsp;
        void* handler = nullptr;
        DWORD64 frame = 0;
        RtlVirtualUnwind(0, base, context.Rip, function, &context, &handler, &frame, nullptr);
        if (inRelay) {
            assert(context.Rsp >= stack + 0xc0);
            crossed = true;
        }
        if (crossed && context.Rip >= exe && context.Rip < exe + nt->OptionalHeader.SizeOfImage)
            break;
    }
    assert(crossed);
    ++traceChecks;
}
const Metadata* __fastcall ProbeResolve(void* game, uint32_t seller, uint32_t action) {
    TraceRelay();
    return Resolve(game, seller, action);
}
const uint8_t* __fastcall ProbeClientName(uint32_t name, uint32_t version) {
    TraceRelay();
    return ResolveClientName(name, version);
}
const uint8_t* __fastcall ProbeUnitName(void* game, void* unit, uint32_t name, uint32_t version) {
    TraceRelay();
    return ResolveUnitName(game, unit, name, version);
}
void __fastcall Nothing() {}
void* __fastcall NullPlayer(void*) { return nullptr; }
Unit* requestNpc = nullptr;
std::array<uint8_t, 0x100> requestPlayerData{};
std::vector<std::vector<uint8_t>> replies;
Unit* __fastcall RequestNpcMock(void*, uint32_t id) {
    return requestNpc && requestNpc->dwUnitId == id ? requestNpc : nullptr;
}
void* __fastcall RequestPlayerDataMock(void*) { return requestPlayerData.data(); }
void __fastcall ReplyMock(void*, uint32_t, const uintptr_t* span) {
    assert(span && span[1] >= span[0] && span[1] - span[0] <= 16);
    const auto bytes = reinterpret_cast<const uint8_t*>(span[0]);
    replies.emplace_back(bytes, bytes + span[1] - span[0]);
}
template <class T> void Stub(uint8_t* image, uint32_t rva, T function) {
    const uint8_t jump[]{0xff, 0x25, 0, 0, 0, 0};
    memcpy(image + rva, jump, 6);
    Put(image + rva, 6, uintptr_t(function));
}
void CaptureAt(uint8_t* image, uint32_t rva, uint32_t exit, uint8_t source) {
    std::vector<uint8_t> bytes{0x48, 0xb8};
    auto address = uintptr_t(&captured);
    auto p = reinterpret_cast<uint8_t*>(&address);
    bytes.insert(bytes.end(), p, p + 8);
    bytes.insert(bytes.end(), {0x48, 0x89, uint8_t(source == 3 ? 0x18 : 0x38), 0xe9});
    const auto delta = int32_t(exit - rva - bytes.size() - 4);
    p = reinterpret_cast<uint8_t*>(const_cast<int32_t*>(&delta));
    bytes.insert(bytes.end(), p, p + 4);
    memcpy(image + rva, bytes.data(), bytes.size());
}
void PurchaseChecks(uint8_t* image, Fixture& f, Unit& player, Unit& npc, std::array<uint8_t, 0x78>& record,
                    std::array<uint8_t, 0x1088>& client, const std::filesystem::path& capture);
void NativeChecks(const std::filesystem::path& path, const std::filesystem::path& capture = {}) {
    std::ifstream input(path, std::ios::binary);
    assert(input);
    std::vector<uint8_t> file{std::istreambuf_iterator<char>(input), {}};
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(file.data());
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(file.data() + dos->e_lfanew);
    auto image = static_cast<uint8_t*>(
        VirtualAlloc(nullptr, nt->OptionalHeader.SizeOfImage, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    assert(image);
    for (auto section = IMAGE_FIRST_SECTION(nt);
         section < IMAGE_FIRST_SECTION(nt) + nt->FileHeader.NumberOfSections; ++section)
        memcpy(image + section->VirtualAddress, file.data() + section->PointerToRawData,
               section->SizeOfRawData);
    testBase = uintptr_t(image);
    assert(Supported());
    image[0x344e9e] ^= 1;
    assert(!Supported());
    image[0x344e9e] ^= 1;
    auto directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    auto runtime = reinterpret_cast<RUNTIME_FUNCTION*>(image + directory.VirtualAddress);
    assert(RtlAddFunctionTable(runtime, directory.Size / sizeof(RUNTIME_FUNCTION), testBase));
    DWORD protection = 0;
    assert(VirtualProtect(image, nt->OptionalHeader.SizeOfImage, PAGE_EXECUTE_READWRITE, &protection));
    Fixture f;
    memcpy(f.metadata.data(), image + 0x1852470, f.metadata.size());
    for (int version : {0, 100})
        for (int difficulty : {1, 2, 3}) {
            // Captured Crusader definition: Tyrael2, Class 944, IDs 39..41,
            // and the temporary 67-name range used during the report.
            f.Row(367, version, difficulty, 4, 10835, 10901);
            Put<int32_t>(f.rows.data() + f.rows.size() - HirelingSize, 4, 38 + difficulty);
            Put<int32_t>(f.rows.data() + f.rows.size() - HirelingSize, 8, 944);
            f.Row(4000, version, difficulty, 4, 2000, 2014);
            f.Row(150, version, difficulty, 1);
        }
    Put(image, 0x1c9ee58, f.rows.data());
    Put<uint64_t>(image, 0x1c9ee60, f.rows.size() / HirelingSize);
    Put(image, 0x1c9f878, f.monsters.data());
    Put<uint64_t>(image, 0x1c9f880, f.monsters.size() / 0x1dc);
    catalog.reset();
    catalogSource = {};
    const auto installed = Install();
    if (!installed)
        std::cerr << "Install failed: " << Status() << "; relay=" << static_cast<void*>(relay)
                  << "; registered=" << registered << "; Win32=" << GetLastError() << std::endl;
    assert(installed && Available());
    assert(VirtualProtect(relay, Allocation, PAGE_EXECUTE_READWRITE, &protection));
    for (size_t i = 0; i < hooks.size(); ++i) {
        const auto& site = NativeData::Sites[i];
        const auto callback = site.unitLookup     ? uintptr_t(ProbeUnitName)
                              : site.clientLookup ? uintptr_t(ProbeClientName)
                                                  : uintptr_t(ProbeResolve);
        const auto code =
            NativeData::Relay(site, callback, uintptr_t(hooks[i].trampoline), testBase + site.resume);
        memcpy(relay + i * 0x200, code.data(), code.size());
    }
    assert(VirtualProtect(relay, Allocation, PAGE_EXECUTE_READ, &protection));
    FlushInstructionCache(GetCurrentProcess(), relay, Allocation);
    // Capture the real native capability lookup's result, then execute its
    // original epilogue. No UI, gold, saves, units or live game are mutated.
    std::array<uint8_t, 32> nativeRosterContinuation{};
    memcpy(nativeRosterContinuation.data(), image + 0x344eda, nativeRosterContinuation.size());
    CaptureAt(image, 0x344eda, 0x3451f3, 3);
    memcpy(purchaseContinuation.data(), image + 0x34a07d, purchaseContinuation.size());
    CaptureAt(image, 0x34a07d, 0x34a48b, 7);
    CaptureAt(image, 0x34c5ec, 0x34ca18, 3);
    Stub(image, 0x20c230, NullPlayer);
    Stub(image, 0x2c2020, Nothing);
    Unit player{}, npc{};
    player.dwUnitType = 0;
    player.dwAnimMode = 1;
    npc.dwUnitType = 1;
    std::array<uint8_t, 0x78> record{};
    Put(f.game.data(), 0x3840, record.data());
    Put<uint64_t>(f.game.data(), 0x3848, 1);
    FlushInstructionCache(GetCurrentProcess(), image, nt->OptionalHeader.SizeOfImage);
    for (bool enabled : {false, true})
        for (int seller : {367, 4000, 198}) {
            SetEnabled(enabled);
            npc.dwClassId = seller;
            Put<int16_t>(record.data(), 0, int16_t(seller));
            for (int expansion : {0, 1})
                for (int difficulty : {0, 1, 2}) {
                    Put<uint32_t>(f.game.data(), 0x108, expansion);
                    Put<uint8_t>(f.game.data(), 0x104, uint8_t(difficulty));
                    captured = 0;
                    Native<void(__fastcall*)(void*, void*, Unit*)>(0x344e70)(f.game.data(), nullptr, &npc);
                    if (enabled && seller != 198) {
                        assert(captured && reinterpret_cast<Metadata*>(captured)->at(7));
                        assert(reinterpret_cast<Metadata*>(captured)->at(0xa) == 3);
                    } else if (seller == 4000)
                        assert(!captured);
                    captured = 0;
                    Native<void(__fastcall*)(void*, Unit*, Unit*, int, int, int)>(0x349f00)(
                        f.game.data(), &player, &npc, 1000, 20, -1);
                    if (enabled && seller != 198)
                        assert(captured && reinterpret_cast<Metadata*>(captured)->at(0xa) == 3);
                    else if (seller == 4000)
                        assert(!captured);
                    captured = 0;
                    Native<void(__fastcall*)(void*, Unit*, int)>(0x34c530)(f.game.data(), &player, seller);
                    if (enabled && seller != 198)
                        assert(captured && reinterpret_cast<Metadata*>(captured)->at(7));
                    else if (seller == 4000)
                        assert(!captured);
                }
        }
    assert(traceChecks == 2 * 3 * 2 * 3 * 3);
    // The real native roster initializer selects rows by Seller, version and
    // difficulty, and chooses at most ten available mercenaries from its names.
    allocatorVtable[1] = uintptr_t(AllocateMock);
    allocatorVtable[4] = uintptr_t(FreeMock);
    Stub(image, 0xacdd80, AllocatorGet);
    Stub(image, 0x14383a0, &memmove);
    Stub(image, 0x1438100, &memset);
    FlushInstructionCache(GetCurrentProcess(), image, nt->OptionalHeader.SizeOfImage);
    for (int seller : {367, 4000})
        for (int expansion : {0, 1})
            for (int difficulty : {0, 1, 2}) {
                Put<uint32_t>(f.game.data(), 0x108, expansion);
                Put<uint8_t>(f.game.data(), 0x104, uint8_t(difficulty));
                std::array<uint8_t, 0x78> roster{};
                Native<void(__fastcall*)(void*, int, void*)>(0x3444a0)(f.game.data(), seller, roster.data());
                const auto entries = Field<uint8_t*>(roster.data(), 0x18);
                const auto size = Field<uint64_t>(roster.data(), 0x20);
                const int first = seller == 367 ? 10835 : 2000;
                const size_t names = seller == 367 ? 67 : 15;
                assert(entries && size == RosterCapacity);
                int active = 0;
                for (size_t i = 0; i < size; ++i) {
                    if (i < names)
                        assert(Field<uint16_t>(entries, i * 16) == first + i);
                    else
                        assert(Field<uint16_t>(entries, i * 16) == 0);
                    active += Field<int32_t>(entries, i * 16 + 12) != 0;
                }
                assert(active == 10);
                FreeMock(nullptr, entries);
            }
    // Exercise the complete real action-3 request dispatcher, not just the
    // downstream roster function. The previous build rejected Tyrael here.
    memcpy(image + 0x344eda, nativeRosterContinuation.data(), nativeRosterContinuation.size());
    Stub(image, 0x34cd00, RequestNpcMock);
    Stub(image, 0x20c230, RequestPlayerDataMock);
    Stub(image, 0x2ec3b0, Nothing);
    Stub(image, 0x2d0130, Nothing);
    Stub(image, 0x1434c50, Nothing);
    std::array<uintptr_t, 6> packetVtable{};
    packetVtable[5] = uintptr_t(ReplyMock);
    Put(image, 0x18682b0, packetVtable.data());
    std::array<uint8_t, 0x1088> client{};
    Put(client.data(), 0x230, f.game.data());
    Put(requestPlayerData.data(), 0xf0, client.data());
    FlushInstructionCache(GetCurrentProcess(), image, nt->OptionalHeader.SizeOfImage);
    const auto request = Native<int(__fastcall*)(void*, Unit*, const void*, int)>(0x2bb7e0);
    npc.dwUnitId = 42;
    requestNpc = &npc;
    for (bool enabled : {false, true})
        for (int seller : {367, 4000})
            for (int expansion : {0, 1})
                for (int difficulty : {0, 1, 2}) {
                    SetEnabled(enabled);
                    npc.dwClassId = seller;
                    record = {};
                    Put<int16_t>(record.data(), 0, int16_t(seller));
                    Put<uint32_t>(f.game.data(), 0x108, expansion);
                    Put<uint8_t>(f.game.data(), 0x104, uint8_t(difficulty));
                    RosterRequest packet;
                    packet.npc = npc.dwUnitId;
                    replies.clear();
                    const int result = request(f.game.data(), &player, &packet, sizeof(packet));
                    if (enabled) {
                        assert(result == 0 && replies.size() == 11 &&
                               replies[0] == std::vector<uint8_t>{0x4f});
                        memset(image + 0x1d60b6c, 0, 10 * 16);
                        for (size_t i = 1; i < replies.size(); ++i) {
                            assert(replies[i].size() == 7 && replies[i][0] == 0x4e);
                            Native<void(__fastcall*)(const void*)>(0xdb0d0)(replies[i].data());
                        }
                        const int first = seller == 367 ? 10835 : 2000,
                                  last = first + (seller == 367 ? 66 : 14);
                        for (size_t i = 0; i < 10; ++i) {
                            const auto row = image + 0x1d60b6c + i * 16;
                            const auto name = Field<uint16_t>(row);
                            assert(name >= first && name <= last && Field<int32_t>(row, 8) == 1);
                        }
                        assert(Field<uint64_t>(record.data(), 0x20) == RosterCapacity);
                        FreeMock(nullptr, Field<void*>(record.data(), 0x18));
                    } else
                        assert(result == 1 && replies.empty() && Field<uint64_t>(record.data(), 0x20) == 0);
                }
    // This extension authorizes Hire only; other NPC services stay native.
    SetEnabled(true);
    npc.dwClassId = 4000;
    for (uint32_t action : {0u, 1u, 2u, 4u}) {
        RosterRequest packet;
        packet.action = action;
        packet.npc = npc.dwUnitId;
        replies.clear();
        assert(request(f.game.data(), &player, &packet, sizeof(packet)) == 1 && replies.empty());
        assert(!Resolve(f.game.data(), 4000, action));
    }
    RosterRequest missing;
    missing.npc = 99;
    assert(request(f.game.data(), &player, &missing, sizeof(missing)) == 1);
    PurchaseChecks(image, f, player, npc, record, client, capture);
    Uninstall();
    assert(!Available());
    // The private native image stays alive because registered relays chain its
    // unwind records. Process exit reclaims this owned test allocation.
    std::cout << "Actual client/host hook installation and native list/purchase/refresh capability gates "
                 "passed for Act IV/custom sellers, Classic/Expansion and all difficulties.\n";
    std::cout << "Native Hire request -> roster creation -> 4F/4E replies -> native client offer slots "
                 "passed; disabled/unrelated actions and invalid interactions remain rejected.\n";
}
int wallet = 2000000, charges = 0, spawns = 0, attachments = 0, lastClass = -1, lastCharge = 0;
std::array<uint8_t, 0x30> attachedProfile{}, clientProfile{};
int __fastcall CaptureAttachment(void*, Unit*, void* list, Unit* merc, const void* profile) {
    assert(list && merc && profile);
    // These are the exact seed/name/profile fields committed to the owner's pet
    // record, whose profile ID the hireling AI later reads at record+8.
    attachedProfile = {};
    Put<int32_t>(attachedProfile.data(), 0, Get<int32_t>(profile, 8));
    Put<uint32_t>(attachedProfile.data(), 4, Get<uint32_t>(profile, 0));
    Put<uint32_t>(attachedProfile.data(), 8, Get<uint32_t>(profile, 4));
    ++attachments;
    return 0;
}
void __fastcall CaptureInitialization(const void* roll) {
    memcpy(clientProfile.data(), roll, clientProfile.size());
}
Unit spawnedMerc{};
int __fastcall QuoteStatMock(void*, uint32_t stat, void*) {
    return stat == 0xc0000 ? 99 : stat == 0xe0000 ? wallet : 0;
}
Unit* __fastcall SpawnMock(void*, void*, int type) {
    ++spawns;
    lastClass = type;
    spawnedMerc = {};
    spawnedMerc.dwUnitType = 1;
    spawnedMerc.dwClassId = type;
    spawnedMerc.dwUnitId = 9000;
    return &spawnedMerc;
}
int __fastcall ChargeMock(void*, void*, int price) {
    ++charges;
    lastCharge = price;
    wallet -= price;
    return 1;
}
void ReturnPointer(uint8_t* image, uint32_t rva, void* pointer) {
    image[rva] = 0x48;
    image[rva + 1] = 0xb8;
    Put(image + rva, 2, uintptr_t(pointer));
    image[rva + 10] = 0xc3;
}
void CaptureRow(uint8_t* image, uint32_t rva, uint32_t exit, uint8_t reg) {
    // R11 is volatile and unused by these return-only fixture continuations.
    std::vector<uint8_t> bytes{0x49, 0x89, uint8_t(0xc3 + reg * 8), 0x48, 0xb8};
    const auto address = uintptr_t(&captured);
    const auto p = reinterpret_cast<const uint8_t*>(&address);
    bytes.insert(bytes.end(), p, p + 8);
    bytes.insert(bytes.end(), {0x4c, 0x89, 0x18, 0xe9});
    const auto delta = int32_t(exit - rva - bytes.size() - 4);
    const auto q = reinterpret_cast<const uint8_t*>(&delta);
    bytes.insert(bytes.end(), q, q + 4);
    memcpy(image + rva, bytes.data(), bytes.size());
}
void CaptureProfile(uint8_t* image, uint32_t rva, uint32_t exit, uintptr_t callback, bool server) {
    // Call after the native profile roll, then use the function's own epilogue.
    // Server roll is [RBP-51h]; client roll is [RSP+30h]. No live effects follow.
    std::vector<uint8_t> code = server ? std::vector<uint8_t>{0x48, 0x8d, 0x4d, 0xaf}
                                       : std::vector<uint8_t>{0x48, 0x8d, 0x4c, 0x24, 0x30};
    code.insert(code.end(), {0x48, 0xb8});
    auto bytes = reinterpret_cast<const uint8_t*>(&callback);
    code.insert(code.end(), bytes, bytes + 8);
    code.insert(code.end(), {0xff, 0xd0, 0xe9});
    const auto delta = int32_t(exit - rva - code.size() - 4);
    bytes = reinterpret_cast<const uint8_t*>(&delta);
    code.insert(code.end(), bytes, bytes + 4);
    memcpy(image + rva, code.data(), code.size());
}
void PurchaseChecks(uint8_t* image, Fixture& f, Unit& player, Unit& npc, std::array<uint8_t, 0x78>& record,
                    std::array<uint8_t, 0x1088>& client, const std::filesystem::path& capture) {
    if (!capture.empty()) {
        std::ifstream input(capture, std::ios::binary);
        assert(input);
        std::vector<uint8_t> raw{std::istreambuf_iterator<char>(input), {}};
        assert(raw.size() >= 4 && raw.size() == 4 + uint64_t(Get<uint32_t>(raw.data(), 0)) * HirelingSize);
        f.rows.assign(raw.begin() + 4, raw.end());
    } else {
        f.rows.clear();
        for (int difficulty : {1, 2, 3})
            for (int seller : {515, 367}) {
                f.Row(seller, 100, difficulty, seller == 515 ? 5 : 4, 10835, 10901);
                auto row = f.rows.data() + f.rows.size() - HirelingSize;
                Put<int32_t>(row, 4, seller == 515 ? 24 : 38 + difficulty);
                Put<int32_t>(row, 8, seller == 515 ? 561 : 944);
                Put<int32_t>(row, 0x18, 2500);
                Put<int32_t>(row, 0x1c, seller == 515 ? 28 : 3);
                Put<int32_t>(row, 0x7c, 1);
                const std::array<int, 6> skills = seller == 515
                                                      ? std::array<int, 6>{631, 139, 246, 82, 152, 549}
                                                      : std::array<int, 6>{630, 151, 246, 82, 152, 549};
                for (size_t slot = 0; slot < skills.size(); ++slot) {
                    Put<int32_t>(row, 0x90 + slot * 4, skills[slot]);
                    row[0xd8 + slot] = uint8_t(slot == 0 ? 1 : seller == 515 ? 5 : slot == 1 ? 14 : 4);
                }
            }
    }
    Put(image, 0x1c9ee58, f.rows.data());
    Put<uint64_t>(image, 0x1c9ee60, f.rows.size() / HirelingSize);
    catalog.reset();
    catalogSource = {};
    std::array<uint8_t, 0x60> configuration{};
    Put<uint32_t>(configuration.data(), 0x5c, 1);
    Put(image, 0x1c9b3f0, configuration.data());
    Put<uint32_t>(image, 0x1d60c10, 1);
    Put<uint16_t>(image, 0x1d60c14, 367);
    Put<uint32_t>(image, 0x1d60b00, 0);
    // Run each real client function's prologue and name lookup, then capture its
    // selected row and use its own epilogue instead of entering desktop UI.
    std::vector<uint8_t> prelude{0x41, 0x89, 0xd0, 0x48, 0xb8};
    const auto append = [&](uintptr_t value) {
        const auto p = reinterpret_cast<const uint8_t*>(&value);
        prelude.insert(prelude.end(), p, p + 8);
    };
    append(uintptr_t(f.rows.data()));
    prelude.insert(prelude.end(), {0x48, 0xba});
    append(uintptr_t(f.rows.data() + f.rows.size()));
    prelude.push_back(0xe9);
    const auto relative = int32_t(0xbe2151 - 0xbe1ea1 - prelude.size() - 4);
    const auto p = reinterpret_cast<const uint8_t*>(&relative);
    prelude.insert(prelude.end(), p, p + 4);
    memcpy(image + 0xbe1ea1, prelude.data(), prelude.size());
    CaptureRow(image, 0xbe224e, 0xbe231e, 0);
    image[0xbe217d] = 0x31;
    image[0xbe217e] = 0xc0;
    CaptureRow(image, 0xbe217f, 0xbe231e, 0);
    CaptureRow(image, 0x13b3f4, 0x13b6b0, 5);
    ReturnPointer(image, 0x66510, &npc);
    ReturnPointer(image, 0x6b050, &player);
    std::array<uint8_t, 16 * 0x144> stats{};
    Put(image, 0x1c9fb78, stats.data());
    Put<uint64_t>(image, 0x1c9fb80, 16);
    player.pStatListEx = reinterpret_cast<D2StatListExStrc*>(stats.data());
    Stub(image, 0x1e2e70, QuoteStatMock);
    memcpy(image + 0x34a07d, purchaseContinuation.data(), purchaseContinuation.size());
    Stub(image, 0x304d90, SpawnMock);
    Stub(image, 0x345ed0, ChargeMock);
    // Run attachment through its real lookup, roll and pet-record argument
    // construction. Isolate the final pet write and skip network/save effects.
    Stub(image, 0x2c74a0, Nothing);
    Stub(image, 0x3c5410, Nothing);
    Stub(image, 0x20c230, RequestPlayerDataMock);
    std::array<uint8_t, 0x80> petBuckets{};
    void* petPointer = petBuckets.data();
    Put(requestPlayerData.data(), 0x98, &petPointer);
    Put<int32_t>(image, 0x1c9fbd0, 8);
    Stub(image, 0x2df310, CaptureAttachment);
    image[0x33d791] = 0xe9;
    Put<int32_t>(image, 0x33d792, 0x33d98b - 0x33d796);
    CaptureProfile(image, 0x0ae1d8, 0x0ae2fc, uintptr_t(CaptureInitialization), false);
    Stub(image, 0x2cef50, Nothing);
    ReturnPointer(image, 0x2c5350, client.data());
    Stub(image, 0x344e70, Nothing);
    FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
    const auto preview = Native<void(__fastcall*)(uint32_t, uint32_t)>(0xbe1e70);
    const auto hire = Native<void(__fastcall*)()>(0x13b2e0);
    const auto roll = Native<int(__fastcall*)(int, Unit*, uint32_t, int, int, void*)>(0x24c740);
    const auto purchase = Native<int(__fastcall*)(void*, Unit*, const void*, int)>(0x2ba290);
    const auto attach = Native<void(__fastcall*)(void*, Unit*, Unit*, uint16_t, void*, int)>(0x33d4b0);
    const auto initialize = Native<void(__fastcall*)(Unit*, void*, int)>(0x0ae0f0);
#pragma pack(push, 1)
    struct Buy {
        uint8_t opcode = 0x36;
        uint32_t npc = 42, name = 0, price = 0, merc = UINT32_MAX;
    };
#pragma pack(pop)
    static_assert(sizeof(Buy) == 17);
    npc.dwClassId = 367;
    npc.dwUnitId = 42;
    requestNpc = &npc;
    Put<uint32_t>(f.game.data(), 0x108, 1);
    for (bool sharedNames : {true, false}) {
        if (!sharedNames) {
            for (size_t offset = 0; offset < f.rows.size(); offset += HirelingSize)
                if (Get<int32_t>(f.rows.data() + offset, 0x14) == 367) {
                    Put<uint16_t>(f.rows.data() + offset, 0x14c, 22000);
                    Put<uint16_t>(f.rows.data() + offset, 0x14e, 22066);
                }
            catalog.reset();
            catalogSource = {};
        }
        for (int difficulty : {0, 1, 2}) {
            Put<uint8_t>(f.game.data(), 0x104, uint8_t(difficulty));
            Put<uint8_t>(image, 0x1bf1ecc, uint8_t(difficulty));
            record = {};
            Put<int16_t>(record.data(), 0, 367);
            Native<void(__fastcall*)(void*, int, void*)>(0x3444a0)(f.game.data(), 367, record.data());
            const auto entries = Field<uint8_t*>(record.data(), 0x18);
            assert(entries);
            uint8_t* offer = nullptr;
            for (size_t i = 0; i < RosterCapacity; ++i)
                if (Field<int32_t>(entries + i * 16, 12)) {
                    offer = entries + i * 16;
                    break;
                }
            assert(offer);
            const auto name = Field<uint16_t>(offer);
            const auto seed = Field<uint32_t>(offer, 4);
            Put<uint16_t>(image, 0x1d60b6c, name);
            Put<uint32_t>(image, 0x1d60b70, seed);
            SetEnabled(false);
            captured = 0;
            preview(name, 100);
            const auto old = reinterpret_cast<uint8_t*>(captured);
            assert(old && Get<int32_t>(old, 0xc) == (sharedNames ? 5 : 4));
            captured = 0;
            hire();
            assert(captured == uintptr_t(old));
            std::array<uint8_t, 0x30> wrongQuote{}, correctQuote{};
            assert(roll(1, &player, seed, Get<int32_t>(old, 0xc) - 1, difficulty, wrongQuote.data()));
            assert(roll(1, &player, seed, 3, difficulty, correctQuote.data()));
            const auto wrongPrice = Get<uint32_t>(wrongQuote.data(), 0x14),
                       price = Get<uint32_t>(correctQuote.data(), 0x14);
            assert((wrongPrice != price) == sharedNames);
            SetEnabled(true);
            captured = 0;
            preview(name, 100);
            const auto selected = reinterpret_cast<uint8_t*>(captured);
            assert(selected && Get<int32_t>(selected, 0x14) == 367 && Get<int32_t>(selected, 0xc) == 4 &&
                   Get<int32_t>(selected, 0x10) == difficulty + 1);
            captured = 0;
            hire();
            assert(captured == uintptr_t(selected));
            std::array<uint8_t, 0x30> clientQuote{};
            assert(roll(1, &player, seed, Get<int32_t>(selected, 0xc) - 1, difficulty, clientQuote.data()));
            assert(clientQuote == correctQuote);
            // Reproduce the exact live failure: Class 944 with shared names is
            // assigned a Barbarian ID/profile after a successful purchase.
            std::array<uint8_t, 16> savedOffer{};
            Put<uint16_t>(savedOffer.data(), 0, name);
            Put<uint32_t>(savedOffer.data(), 4, seed);
            spawnedMerc = {};
            spawnedMerc.dwUnitType = 1;
            spawnedMerc.dwClassId = 944;
            for (bool enabled : {false, true}) {
                SetEnabled(enabled);
                attachments = 0;
                attachedProfile = clientProfile = {};
                attach(f.game.data(), &player, &spawnedMerc, UINT16_MAX, savedOffer.data(), 0);
                initialize(&spawnedMerc, savedOffer.data(), 39 + difficulty);
                const auto expectedId =
                    enabled || !sharedNames ? 39 + difficulty : Get<int32_t>(wrongQuote.data(), 0);
                assert(attachments == 1 && Get<int32_t>(attachedProfile.data(), 0) == expectedId &&
                       Get<int32_t>(clientProfile.data(), 0) == expectedId);
                assert(Get<uint32_t>(attachedProfile.data(), 4) == seed &&
                       Get<uint32_t>(attachedProfile.data(), 8) == name);
                const auto row = Native<uint8_t*(__fastcall*)(int, int, int)>(0x2391f0)(1, expectedId, 99);
                assert(row && Get<int32_t>(row, 8) == (enabled || !sharedNames ? 944 : 561));
                assert(row[0xda] == uint8_t(enabled || !sharedNames ? 4 : 5)); // Landslide mode
                if (enabled)
                    assert(row[0xd9] == 14 && Get<int32_t>(row, 0x94) == 151);
            }
            SetEnabled(true);
            Buy packet;
            packet.name = name;
            packet.price = sharedNames ? wrongPrice : price + 1;
            wallet = 2000000;
            charges = spawns = attachments = 0;
            assert(purchase(f.game.data(), &player, &packet, sizeof(packet)) == 1 && !charges && !spawns &&
                   wallet == 2000000);
            packet.price = price;
            wallet = 0;
            assert(purchase(f.game.data(), &player, &packet, sizeof(packet)) == 1 && !charges && !spawns);
            wallet = 2000000;
            packet.name = UINT16_MAX;
            assert(purchase(f.game.data(), &player, &packet, sizeof(packet)) == 1 && !charges && !spawns);
            packet.name = name;
            const int result = purchase(f.game.data(), &player, &packet, sizeof(packet));
            if (result != 0)
                std::cerr << "Purchase failed diff=" << difficulty << " quote=" << price
                          << " old=" << wrongPrice << " spawns=" << spawns << " charges=" << charges
                          << " attach=" << attachments << std::endl;
            assert(result == 0 && spawns == 1 && lastClass == 944 && charges == 1 &&
                   lastCharge == int(price) && attachments == 1 && wallet == 2000000 - int(price));
            assert(Get<int32_t>(attachedProfile.data(), 0) == 39 + difficulty);
            assert(purchase(f.game.data(), &player, &packet, sizeof(packet)) == 1 && spawns == 1 &&
                   charges == 1);
            FreeMock(nullptr, entries);
            std::cout << "Native Crusader purchase (" << (sharedNames ? "shared" : "unique")
                      << " names) diff " << difficulty << ": old client " << wrongPrice
                      << ", server/corrected client " << price
                      << "; Class 944 spawned and charged once; client/owner AI profile "
                      << Get<int32_t>(attachedProfile.data(), 0) << "; Landslide A1, Whirlwind SQ.\n";
        }
    }
    assert(!ResolveClientName(UINT16_MAX, 100));
    Put<uint32_t>(image, 0x1d60c10, 0);
    assert(!ResolveClientName(10835, 100));
}
} // namespace
int main(int argc, char** argv) {
    Policy();
    Dialogs();
    Menus();
    UnitProfiles();
    if (argc > 1)
        NativeChecks(argv[1], argc > 2 ? argv[2] : "");
    std::cout << "Merc Seller Unlock policy, settings, native menu capacity/preservation and seller "
                 "isolation passed.\n";
}
