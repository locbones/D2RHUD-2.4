#include "../d2rhud/plugin/SelectedAuras.cpp"
#include <cassert>
#include <iostream>
#include <map>
#include <vector>
#include <filesystem>
#include <fstream>

namespace {
std::map<uint32_t, uintptr_t> auraBindings;
uint8_t* auraImage = nullptr;
}
DWORD64 Pattern::Address(uint32_t rva) { return auraImage ? reinterpret_cast<uintptr_t>(auraImage) + rva : auraBindings[rva]; }
template<size_t N> using Bytes = std::array<uint8_t, N>;
template<class T> void Put(void* pointer, size_t offset, T value) {
    std::memcpy(static_cast<uint8_t*>(pointer) + offset, &value, sizeof(value));
}
template<class T> T Get(const void* pointer, size_t offset) {
    T value; std::memcpy(&value, static_cast<const uint8_t*>(pointer) + offset, sizeof(value)); return value;
}
template<class T> void Bind(uint32_t rva, T value) { auraBindings[rva] = reinterpret_cast<uintptr_t>(value); }
using Byte = uint8_t;
using namespace SelectedAuras;

struct Fixture {
    Bytes<0x300> unit{};
    Bytes<0x40> list{};
    Bytes<0xb00> widget{};
    Bytes<0xb00> itemAura{};
    std::array<Bytes<0x284>, 6> rows{};
    std::array<Bytes<0x50>, 6> nodes{};
    std::array<int, 6> ids{0, 10, 99, 102, 122, 123};
    int bonus = 4, bonusCalls = 0, pickerState = 6, hand = 1, handCalls = 0;
    int leftSets = 0, rightSets = 0, prayerClears = 0;
    struct Assignment { int hand, id, owner; };
    std::vector<Assignment> assignments;
    Byte* Node(int id) {
        for (size_t i = 0; i < ids.size(); ++i) if (ids[i] == id) return nodes[i].data();
        return nullptr;
    }
    void Slot(int left, int id) { Put(list.data(), left ? 8 : 0x10, Node(id)); }
    void Reset();
} fixture;

Byte* __fastcall MockGetSkill(Byte* unit, int id, int owner) {
    assert(unit == fixture.unit.data());
    auto node = fixture.Node(id);
    return node && Get<int>(node, 0x44) == owner ? node : nullptr;
}
void __fastcall SetLeft(Byte* unit, int id, int owner) { assert(MockGetSkill(unit, id, owner)); ++fixture.leftSets; fixture.Slot(1, id); }
void __fastcall SetRight(Byte* unit, int id, int owner) { assert(MockGetSkill(unit, id, owner)); ++fixture.rightSets; fixture.Slot(0, id); }
int __fastcall Bonus(Byte* unit, Byte* skill, int include) {
    assert(unit == fixture.unit.data() && skill && include == 0); ++fixture.bonusCalls; return fixture.bonus;
}
void __fastcall Toggle(Byte* unit, int state, int active) {
    assert(unit == fixture.unit.data() && state == 85 && active == 0); ++fixture.prayerClears;
}
int __fastcall Picker(Byte* unit, Byte* skill) { assert(unit == fixture.unit.data() && skill); return fixture.pickerState; }
int __fastcall Hand(Byte* widget, Byte*, int zero) { assert(widget == fixture.widget.data() && zero == 0); ++fixture.handCalls; return fixture.hand; }
void __fastcall Assign(Byte* unit, int hand, int id, int owner) {
    assert(unit == fixture.unit.data());
    fixture.assignments.push_back({hand, id, owner});
    // The corrective native assignment must pass the same guard without
    // recursion and retain the exact authoritative skill owner.
    auto skill = GuardGetSkill(unit, id, owner, hand);
    assert(skill == fixture.Node(id));
}
void Fixture::Reset() {
    auraBindings.clear(); unit = {}; list = {}; widget = {}; itemAura = {}; rows = {}; nodes = {}; assignments.clear();
    bonus = 4; bonusCalls = 0; pickerState = 6; hand = 1; handCalls = leftSets = rightSets = prayerClears = 0;
    Put(unit.data(), 0, uint32_t(0)); Put(unit.data(), 0x100, list.data());
    // Unassigned item effects exist independently of the chosen skill nodes.
    Put(unit.data(), 0x88, itemAura.data());
    for (size_t i = 0; i < ids.size(); ++i) {
        Put(rows[i].data(), 0, int16_t(ids[i]));
        Put(rows[i].data(), 0x24, uint32_t(i >= 2 ? 0x20 : 0));
        Put(rows[i].data(), 0xa0, int16_t(i >= 2 ? 40 + i : -1));
        Put(nodes[i].data(), 0, rows[i].data());
        Put(nodes[i].data(), 0x38, int(10)); Put(nodes[i].data(), 0x44, int(-1));
    }
    Slot(1, 0); Slot(0, 0);
    Bind(0x218c70, MockGetSkill); Bind(0x219690, SetLeft); Bind(0x219750, SetRight);
    Bind(0x21a990, Bonus); Bind(0x222ea0, Toggle); Bind(0x169da0, Picker);
    Bind(0x177bc0, Hand); Bind(0x29a2a0, Assign);
}

void SelectionPolicy() {
    auto& f = fixture; f.Reset();
    assert(!Data::Selected(f.unit.data()));
    assert(!Data::IsAura(f.Node(0)) && Data::IsAura(f.Node(102)));
    assert(!Data::Conflict(f.unit.data(), f.Node(102), 1));
    f.Slot(1, 102);
    assert(Data::Selected(f.unit.data()) == f.Node(102));
    assert(!Data::Conflict(f.unit.data(), f.Node(123), 1)); // Same-slot replacement remains available.
    assert(Data::Conflict(f.unit.data(), f.Node(123), 0));
    assert(Data::Conflict(f.unit.data(), f.Node(102), 0)); // Same aura cannot occupy a new second slot either.
    assert(!Data::Conflict(f.unit.data(), f.Node(10), 0));
    assert(!Data::Conflict(f.unit.data(), f.Node(102), 1));
    f.Slot(0, 122); // A legacy dual selection uses the established right-hand priority.
    assert(Data::Selected(f.unit.data()) == f.Node(122));
    assert(!Data::Conflict(f.unit.data(), f.Node(102), 1));
    assert(!Data::Conflict(f.unit.data(), f.Node(122), 0));
    f.Slot(0, 10); assert(Data::Selected(f.unit.data()) == f.Node(102));
    Put(f.unit.data(), 0, uint32_t(1));
    assert(Data::Selected(f.unit.data()) == f.Node(10));
    assert(!Data::Conflict(f.unit.data(), f.Node(123), 0) && !Data::Conflict(f.unit.data(), f.Node(123), 1));
    f.Reset(); Put(f.itemAura.data(), 0x20, uint32_t(42));
    assert(!Data::Selected(f.unit.data()) && !Data::Conflict(f.unit.data(), f.Node(102), 0)
        && !Data::Conflict(f.unit.data(), f.Node(102), 1));
    auto candidate = f.Node(102); auto row = Get<Byte*>(candidate, 0);
    Put(row, 0x24, uint32_t(0x30)); assert(!Data::IsAura(candidate));
    Put(row, 0x24, uint32_t(0x20)); Put(row, 0xa0, int16_t(-1)); assert(!Data::IsAura(candidate));
    assert(!Data::IsAura(nullptr) && !Data::Conflict(nullptr, candidate, 0));
    std::cout << "Selected auras support either slot, retain right priority for legacy dual selections, permit same-slot replacement and exclude unassigned item effects/nonplayers from slot conflicts.\n";
}

void AssignmentAndPeriodic() {
    auto& f = fixture; f.Reset();
    auto old = BeforeAssign(f.unit.data(), 1, 102, -1);
    assert(!old && f.leftSets == 1 && AfterAssign(f.unit.data(), 1, 102, old) == 102 && !f.prayerClears);
    auto result = Periodic(f.unit.data());
    assert(uint32_t(result >> 32) == 102 && uint32_t(result) == 14 && f.bonusCalls == 1);
    f.bonus = 7; result = Periodic(f.unit.data()); assert(uint32_t(result) == 17);
    Put(f.Node(102), 0x44, int(777)); f.bonus = 100;
    result = Periodic(f.unit.data()); assert(uint32_t(result) == 10 && f.bonusCalls == 2);
    Put(f.Node(102), 0x38, int(7)); result = Periodic(f.unit.data()); assert(uint32_t(result) == 7);
    Put(f.Node(102), 0x44, int(-1));
    old = BeforeAssign(f.unit.data(), 0, 10, -1);
    assert(old == f.Node(102) && AfterAssign(f.unit.data(), 0, 10, old) == 102); // Native right prologue requires reactivation.
    old = BeforeAssign(f.unit.data(), 1, 102, -1);
    assert(old == f.Node(102) && AfterAssign(f.unit.data(), 1, 102, old) == -2 && !f.prayerClears);
    old = BeforeAssign(f.unit.data(), 1, 123, -1);
    assert(old == f.Node(102) && AfterAssign(f.unit.data(), 1, 123, old) == 123 && f.prayerClears == 1);
    old = BeforeAssign(f.unit.data(), 1, 0, -1);
    assert(old == f.Node(123) && AfterAssign(f.unit.data(), 1, 0, old) == 0 && f.prayerClears == 2);
    assert(Periodic(f.unit.data()) == 0xffffffff00000000ULL);
    f.Slot(1, 102); f.Slot(0, 122);
    old = BeforeAssign(f.unit.data(), 0, 0, -1);
    assert(old == f.Node(122) && AfterAssign(f.unit.data(), 0, 0, old) == 102 && f.prayerClears == 2);
    Put(f.unit.data(), 0, uint32_t(1));
    old = BeforeAssign(f.unit.data(), 1, 123, -1);
    assert(!old && AfterAssign(f.unit.data(), 1, 123, old) == -2);
    old = BeforeAssign(f.unit.data(), 0, 122, -1);
    assert(old == f.Node(0) && AfterAssign(f.unit.data(), 0, 122, old) == 122);
    f.Reset(); f.Slot(0, 102);
    old = BeforeAssign(f.unit.data(), 0, 102, -1);
    assert(old == f.Node(102) && AfterAssign(f.unit.data(), 0, 102, old) == 102);
    f.Slot(0, 0); f.Slot(1, 99);
    old = BeforeAssign(f.unit.data(), 0, 10, -1);
    assert(old == f.Node(99) && AfterAssign(f.unit.data(), 0, 10, old) == 99); // Restore Prayer after native right-side reset.
    std::cout << "Assignment helpers preserve unchanged left auras, retain native right-side reactivation, clear left-slot Prayer state and reevaluate periodic levels/ownership.\n";
}

void PickerAndHotkey() {
    auto& f = fixture;
    for (int activeHand : {0, 1}) {
        f.Reset(); f.Slot(activeHand, 102); f.hand = !activeHand;
        assert(PickerState(f.unit.data(), f.Node(123), f.widget.data()) == 3);
        assert(!PickerGetSkill(f.unit.data(), 123, -1, f.widget.data()));
        assert(!GuardGetSkill(f.unit.data(), 123, -1, !activeHand));
        assert(f.assignments.size() == 1 && f.assignments[0].hand == !activeHand
            && f.assignments[0].id == 0 && f.assignments[0].owner == -1);
        assert(Data::Selected(f.unit.data()) == f.Node(102));
        f.hand = activeHand;
        assert(PickerState(f.unit.data(), f.Node(123), f.widget.data()) == 6);
        assert(PickerGetSkill(f.unit.data(), 123, -1, f.widget.data()) == f.Node(123));
        assert(GuardGetSkill(f.unit.data(), 123, -1, activeHand) == f.Node(123));
        f.hand = !activeHand;
        for (int original : {0, 1, 2, 3, 4, 7, 8, 9}) {
            f.pickerState = original;
            assert(PickerState(f.unit.data(), f.Node(10), f.widget.data()) == original);
        }
        Put(f.widget.data(), 0x9f0, int(1)); f.pickerState = 6;
        assert(PickerState(f.unit.data(), f.Node(123), f.widget.data()) == 6);
        assert(PickerGetSkill(f.unit.data(), 123, -1, f.widget.data()) == f.Node(123));
        assert(PickerState(f.unit.data(), f.Node(123), nullptr) == 6);
        assert(PickerGetSkill(f.unit.data(), 123, -1, nullptr) == f.Node(123));
        assert(!PickerGetSkill(f.unit.data(), 999, -1, nullptr));
        Put(f.widget.data(), 0x9f0, int(0));
        const auto handLookups = f.handCalls;
        assert(!PickerGetSkill(f.unit.data(), 999, -1, f.widget.data()) && f.handCalls == handLookups);
    }
    f.Reset(); f.Slot(1, 102); f.Slot(0, 10); Put(f.Node(10), 0x44, int(777));
    assert(!GuardGetSkill(f.unit.data(), 123, -1, 0));
    assert(f.assignments.size() == 1 && f.assignments[0].id == 10 && f.assignments[0].owner == 777);
    std::cout << "Picker status/click guards prevent new dual auras, preserve normal/native icon states, and resynchronize rejected hotkeys once with the exact current owner.\n";
}

void RuntimeSkillFlags() {
    auto& f = fixture; f.Reset();
    Byte* rowTable = nullptr; uint64_t rowCount = f.rows.size();
    Bind(NativeData::SkillRows, &rowTable); Bind(NativeData::SkillCount, &rowCount);
    Put(f.rows[3].data(), 0xa0, int16_t(-1));
    Put(f.rows[4].data(), 0x24, uint32_t(Data::AuraFlag | Data::LeftFlag)); // Already supplied by the mod.
    Put(f.rows[5].data(), 0x24, uint32_t(Data::AuraFlag | Data::PassiveFlag));
    installed.store(true); Update(); assert(changedFlags.empty());
    rowTable = f.rows[0].data(); Update(); // Skill data can arrive after module installation.
    assert(!(Get<uint32_t>(f.rows[0].data(), 0x24) & Data::LeftFlag)
        && !(Get<uint32_t>(f.rows[1].data(), 0x24) & Data::LeftFlag));
    assert(Get<uint32_t>(f.rows[2].data(), 0x24) & Data::LeftFlag);
    assert(!(Get<uint32_t>(f.rows[3].data(), 0x24) & Data::LeftFlag)
        && !(Get<uint32_t>(f.rows[5].data(), 0x24) & Data::LeftFlag));
    assert(changedFlags.size() == 1);
    Update(); assert(changedFlags.size() == 1);
    Put(f.rows[3].data(), 0xa0, int16_t(43)); Update();
    assert(changedFlags.size() == 1 && !(Get<uint32_t>(f.rows[3].data(), 0x24) & Data::LeftFlag));
    Put(f.rows[2].data(), 0x24, uint32_t(Data::AuraFlag)); Update();
    assert(!(Get<uint32_t>(f.rows[2].data(), 0x24) & Data::LeftFlag)); // Do not overwrite a later runtime edit.
    const auto rowsBeforeInvalidCount = f.rows;
    rowCount = 65537; Update(); assert(f.rows == rowsBeforeInvalidCount); rowCount = f.rows.size();
    Put(f.rows[2].data(), 0x24, Get<uint32_t>(f.rows[2].data(), 0x24) | 0x8000u); // An unrelated later edit.
    RestoreFlags(); installed.store(false);
    assert(Get<uint32_t>(f.rows[2].data(), 0x24) == (Data::AuraFlag | 0x8000u));
    assert(!(Get<uint32_t>(f.rows[3].data(), 0x24) & Data::LeftFlag));
    assert(Get<uint32_t>(f.rows[4].data(), 0x24) == (Data::AuraFlag | Data::LeftFlag));
    assert(changedFlags.empty() && !changedRows && !changedCount);
    std::cout << "Runtime left-slot flags initialize lazily once per table, reject invalid tables and preserve mod and later edits during updates/restoration.\n";
}

void NativeInstallation(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    assert(input && input.tellg() >= std::streamoff(sizeof(IMAGE_DOS_HEADER)));
    std::vector<Byte> file(static_cast<size_t>(input.tellg())); input.seekg(0);
    assert(input.read(reinterpret_cast<char*>(file.data()), static_cast<std::streamsize>(file.size())));
    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(file.data());
    assert(dos->e_magic == IMAGE_DOS_SIGNATURE && dos->e_lfanew >= 0
        && size_t(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS64) <= file.size());
    const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(file.data() + dos->e_lfanew);
    assert(nt->Signature == IMAGE_NT_SIGNATURE && nt->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64);
    const auto imageSize = size_t(nt->OptionalHeader.SizeOfImage);
    assert(imageSize >= 0x1c9fae0 && nt->OptionalHeader.SizeOfHeaders <= file.size()
        && nt->OptionalHeader.SizeOfHeaders <= imageSize);
    const auto sections = IMAGE_FIRST_SECTION(nt);
    assert(reinterpret_cast<const Byte*>(sections + nt->FileHeader.NumberOfSections) <= file.data() + file.size());
    auraImage = static_cast<Byte*>(VirtualAlloc(nullptr, imageSize, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    assert(auraImage); std::memcpy(auraImage, file.data(), nt->OptionalHeader.SizeOfHeaders);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        const auto& section = sections[i];
        assert(size_t(section.PointerToRawData) + section.SizeOfRawData <= file.size()
            && size_t(section.VirtualAddress) + section.SizeOfRawData <= imageSize);
        if (section.SizeOfRawData) std::memcpy(auraImage + section.VirtualAddress,
            file.data() + section.PointerToRawData, section.SizeOfRawData);
    }
    struct Site { uint32_t rva; size_t size; };
    constexpr std::array<Site, 6> sites{{{0x29a3ba, 55}, {0x29a48c, 69}, {0x299683, 49},
        {0x178651, 5}, {0x177ed7, 5}, {0x29a39c, 5}}};
    auto snapshot = [&] {
        std::array<std::vector<Byte>, sites.size()> values;
        for (size_t i = 0; i < sites.size(); ++i) values[i].assign(auraImage + sites[i].rva, auraImage + sites[i].rva + sites[i].size);
        return values;
    };
    const auto original = snapshot();
    auraImage[NativeData::Signatures[0].rva] ^= 1;
    assert(!Install() && !Installed() && snapshot() == original);
    auraImage[NativeData::Signatures[0].rva] ^= 1;
    // A conflict at any site must prevent every write, preserving other hooks.
    for (size_t bad = 0; bad < sites.size(); ++bad) {
        auraImage[sites[bad].rva] ^= 1; const auto changed = snapshot();
        assert(!Install() && !Installed() && snapshot() == changed);
        auraImage[sites[bad].rva] ^= 1;
    }
    const bool installResult = Install();
    if (!installResult) std::cerr << Status() << '\n';
    assert(installResult && Installed());
    const auto patched = snapshot();
    for (size_t i = 0; i < sites.size(); ++i) assert(patched[i] != original[i]);
    assert(Install() && snapshot() == patched); // Idempotent without replacing owned relays.
    Uninstall(); assert(!Installed() && snapshot() == original);

    assert(Install());
    auraImage[sites[0].rva] = 0xcc; // A later foreign patch owns this site now.
    const auto foreign = snapshot()[0];
    Uninstall();
    const auto remaining = snapshot();
    assert(remaining[0] == foreign);
    for (size_t i = 1; i < sites.size(); ++i) assert(remaining[i] == original[i]);
    std::memcpy(auraImage + sites[0].rva, original[0].data(), original[0].size());
    assert(VirtualFree(auraImage, 0, MEM_RELEASE)); auraImage = nullptr;
    std::cout << "Audited native aura patch sites install/restore atomically, reject foreign bytes and preserve a later hook on uninstall. No game code executed.\n";
}

int main(int argc, char** argv) { SelectionPolicy(); AssignmentAndPeriodic(); PickerAndHotkey(); RuntimeSkillFlags(); if (argc > 1) NativeInstallation(argv[1]); }
