#include "../d2rhud/plugin/D2RHUD/TerrorZoneRotation.h"
#include "../d2rhud/plugin/D2RHUD/TerrorZonePaths.h"
#include <windows.h>
#include <cassert>
#include <climits>
#include <fstream>
#include <iostream>

int main() {
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() / ("d2rhud-zone-paths-" + std::to_string(GetCurrentProcessId()));
    assert(!fs::exists(root));
    const auto relative = fs::path("data/hd/global/excel/desecratedzones.json");
    const auto loose = root / relative;
    const auto extracted = root / "Mods/RMD-MP" / relative;
    const auto mpq = root / "Mods/RMD-MP/RMD-MP.mpq" / relative;
    auto write = [](const fs::path& path, const char* text) { fs::create_directories(path.parent_path()); std::ofstream file(path); file << text; assert(file.good()); };
    auto content = [](const fs::path& path) { std::ifstream file(path); std::string text; file >> text; return text; };
    assert(TerrorZoneConfigPath(root, "") == loose);
    write(loose, "loose");
    assert(content(TerrorZoneConfigPath(root, "")) == "loose");
    // Named mods cannot accidentally use a different configuration from loose
    // data, even when that is the only file present.
    assert(TerrorZoneConfigPath(root, "RMD-MP") == mpq && !fs::exists(mpq));
    write(extracted, "extracted");
    assert(content(TerrorZoneConfigPath(root, "RMD-MP")) == "extracted");
    write(mpq, "mpq");
    assert(content(TerrorZoneConfigPath(root, "RMD-MP")) == "mpq");
    assert(content(TerrorZoneConfigPath(root, "")) == "loose");
    assert(TerrorZoneConfigPath(root, "OtherMod") == root / "Mods/OtherMod/OtherMod.mpq" / relative);
    for (const char* name : {"monstats.txt", "superuniques.txt", "treasureclassex.txt"}) {
        const auto table = fs::path("global/excel") / name;
        write(root / "data" / table, "loose-table");
        write(root / "Mods/RMD-MP/data" / table, "mod-table");
        assert(content(TerrorZoneDataPath(root, "", table)) == "loose-table");
        assert(content(TerrorZoneDataPath(root, "RMD-MP", table)) == "mod-table");
    }
    std::cout << "Direct-data, named-mod and extracted-mod terror-zone selection and configuration precedence passed.\n";
    constexpr long long start = 1700000000;
    struct Boundary { long long elapsed; int expected; };
    // An initial group of two rotates to three after exactly an hour, wraps
    // to zero at two hours and returns to two after four complete periods.
    for (const auto test : {Boundary{0, 2}, {3599, 2}, {3600, 3}, {7199, 3},
                           {7200, 0}, {14399, 1}, {14400, 2}}) {
        assert(TerrorZoneRotationIndex(start + test.elapsed, start, 60, 0, 4, 2) == test.expected);
    }
    // A ten-minute terror phase plus five-minute break advances only after
    // all fifteen minutes; the existing active/break gates decide eligibility.
    for (const auto test : {Boundary{0, 1}, {599, 1}, {600, 1}, {899, 1},
                           {900, 2}, {1799, 2}, {1800, 0}, {2700, 1}}) {
        assert(TerrorZoneRotationIndex(start + test.elapsed, start, 10, 5, 3, 1) == test.expected);
    }
    assert(TerrorZoneRotationIndex(start - 1, start, 60, 0, 4, 2) == 2);
    assert(TerrorZoneRotationIndex(start + 3600, start, 60, 0, 4, -1) == 0);
    assert(TerrorZoneRotationIndex(start, start, 60, 0, 4, -5) == 3);
    assert(TerrorZoneRotationIndex(LLONG_MAX, LLONG_MIN, 60, 0, 1, INT_MIN) == 0);
    assert(TerrorZoneRotationIndex(LLONG_MIN, LLONG_MAX, 60, 0, 4, 2) == 2);
    // 2^64-1 seconds / 60 is exactly 307445734561825860 complete minutes.
    assert(TerrorZoneRotationIndex(LLONG_MAX, LLONG_MIN, 1, 0, 7, 0) == 0);
    assert(TerrorZoneRotationIndex(start, start, INT_MAX, INT_MAX, 4, 3) == 3);
    for (int groups : {0, -1})
        assert(TerrorZoneRotationIndex(start, start, 60, 0, groups, 1) == 0);
    assert(TerrorZoneRotationIndex(start, start, 0, 0, 4, 1) == 0);
    assert(TerrorZoneRotationIndex(start, start, -10, 5, 4, 1) == 0);
    assert(TerrorZoneRotationIndex(start, start, INT_MIN, INT_MIN, 4, 1) == 0);
    std::cout << "Terror-zone hourly/break boundaries, wraparound, random offsets and bounded time arithmetic passed.\n";
}
