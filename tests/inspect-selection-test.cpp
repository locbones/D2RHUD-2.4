#define NOMINMAX
#include "../d2rhud/plugin/InspectSelection.cpp"
#include <cassert>
#include <fstream>
#include <iostream>
#include <map>
namespace {
D2UnitStrc merc{}, enemy{};
D2DynamicPathStrc path{};
bool town = true, coarse = true, bounds = true, retail = false, signatureMode = false;
int geometryCalls = 0;
std::vector<uint8_t> executable;
std::map<uint32_t, uintptr_t> bytes;
int __fastcall Original(D2UnitStrc*, int, int, int) { return retail; }
int __fastcall Coarse(D2UnitStrc* unit, int isoX, int isoY, int, int) {
    assert(unit == &merc && isoX == 1000 && isoY == 2000);
    ++geometryCalls;
    return coarse;
}
int __fastcall Bounds(D2UnitStrc* unit, int part, uint8_t combined, int frame, int* box) {
    assert(unit == &merc && part == 0 && combined == 1 && frame == 7);
    box[0] = 100;
    box[1] = 200;
    box[2] = 150;
    box[3] = 120;
    return bounds;
}
void Executable(const char* filename) {
    std::ifstream in(filename, std::ios::binary | std::ios::ate);
    assert(in);
    executable.resize(size_t(in.tellg()));
    in.seekg(0);
    in.read(reinterpret_cast<char*>(executable.data()), executable.size());
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(executable.data());
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(executable.data() + dos->e_lfanew);
    auto sections = IMAGE_FIRST_SECTION(nt);
    for (auto rva : {0x6baf0u, 0xc33f0u, 0x6b290u})
        for (unsigned n = 0; n < nt->FileHeader.NumberOfSections; ++n) {
            const auto& s = sections[n];
            if (rva >= s.VirtualAddress && rva - s.VirtualAddress < s.SizeOfRawData)
                bytes[rva] = uintptr_t(executable.data() + s.PointerToRawData + rva - s.VirtualAddress);
        }
    signatureMode = true;
    assert(Inspect::Selection::Signatures());
    signatureMode = false;
    // Verify that the allowlist entries really are native cursor predicate calls.
    for (auto ret : {0xe00cfu, 0xdfc7au, 0xdee1au, 0xdef81u}) {
        for (unsigned n = 0; n < nt->FileHeader.NumberOfSections; ++n) {
            const auto& s = sections[n];
            if (ret - 5 < s.VirtualAddress || ret - s.VirtualAddress > s.SizeOfRawData)
                continue;
            const auto call = executable.data() + s.PointerToRawData + ret - 5 - s.VirtualAddress;
            int32_t rel = 0;
            memcpy(&rel, call + 1, 4);
            assert(call[0] == 0xe8 && int64_t(ret) + rel == 0x6baf0);
        }
    }
}
} // namespace
bool Inspect::Native::CursorEligible(D2UnitStrc* unit) { return town && unit == &merc; }
DWORD64 Pattern::Address(uint32_t rva) {
    if (signatureMode)
        return bytes.at(rva);
    if (rva == 0xc33f0)
        return uintptr_t(Coarse);
    if (rva == 0x6b290)
        return uintptr_t(Bounds);
    return rva;
}
int main(int argc, char** argv) {
    assert(argc == 2);
    Executable(argv[1]);
    using namespace Inspect::Selection;
    original = Original;
    merc.dwUnitType = 1;
    merc.dwUnitId = 1;
    merc.pDynamicPath = &path;
    // Deliberately unselectable flags: no temporary or persistent flag writes.
    merc.dwFlags = 0;
    merc.dwFlagsEx = 0;
    path.tUnkCoord = {1000, 2000};
    int frame = 7 << 8;
    memcpy(reinterpret_cast<uint8_t*>(&merc) + 0x5c, &frame, 4);
    auto before = merc;
    assert(ForCaller(&merc, 120, 150, 0, Fn<void*>(0xe00cf)) == 1);
    assert(!ForCaller(&merc, 99, 150, 0, Fn<void*>(0xe00cf)));
    assert(!ForCaller(&merc, 120, 201, 0, Fn<void*>(0xe00cf)));
    bounds = false;
    assert(!ForCaller(&merc, 120, 150, 0, Fn<void*>(0xe00cf)));
    bounds = true;
    coarse = false;
    assert(!ForCaller(&merc, 120, 150, 0, Fn<void*>(0xe00cf)));
    coarse = true;
    const auto calls = geometryCalls;
    assert(ForCaller(&merc, 0, 0, -1, Fn<void*>(0xdef81)) == 1);
    assert(ForCaller(&merc, 0, 0, -1, Fn<void*>(0xdee1a)) == 1);
    assert(geometryCalls == calls);
    town = false;
    assert(!ForCaller(&merc, 120, 150, 0, Fn<void*>(0xe00cf)));
    town = true;
    assert(!ForCaller(&enemy, 120, 150, 0, Fn<void*>(0xe00cf)));
    assert(!ForCaller(&merc, 120, 150, 0, Fn<void*>(0x123456))); // Combat caller.
    assert(!ForCaller(&merc, 120, 150, 1, Fn<void*>(0xe00cf)));  // No guessed controller behavior.
    retail = true;
    assert(ForCaller(&enemy, 0, 0, 0, Fn<void*>(0x123456)) == 1);
    assert(memcmp(&before, &merc, sizeof(merc)) == 0);
    std::cout
        << "PASS: real executable cursor/helper signatures and call sites; town-only allied eligibility; "
           "native coarse and animated bounds; empty-space/corpse/combat isolation; no unit writes.\n";
}
