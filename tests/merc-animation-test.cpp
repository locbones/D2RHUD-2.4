#define NOMINMAX
#include "../d2rhud/plugin/GameAssets.h"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <vector>

// Runs only in this owned test process. No attachment, injection or game launch.
namespace {
using Bytes = std::vector<uint8_t>;
using Row = std::map<std::string, std::string>;
Bytes Read(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    assert(file);
    return {std::istreambuf_iterator<char>(file), {}};
}
template <class T> T Get(const void* p, size_t offset) {
    T value{};
    memcpy(&value, static_cast<const uint8_t*>(p) + offset, sizeof(value));
    return value;
}
template <class T> void Put(void* p, size_t offset, T value) {
    memcpy(static_cast<uint8_t*>(p) + offset, &value, sizeof(value));
}
std::vector<Row> Table(const std::filesystem::path& path) {
    std::ifstream file(path);
    assert(file);
    auto cells = [](std::string line) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        std::vector<std::string> result;
        size_t start = 0;
        for (size_t i = 0; i <= line.size(); ++i)
            if (i == line.size() || line[i] == '\t') {
                result.push_back(line.substr(start, i - start));
                start = i + 1;
            }
        return result;
    };
    std::string line;
    assert(std::getline(file, line));
    if (line.starts_with("\xef\xbb\xbf"))
        line.erase(0, 3);
    auto header = cells(line);
    std::vector<Row> rows;
    while (std::getline(file, line)) {
        auto values = cells(line);
        if (values[0].empty() && path.stem() != "monseq")
            continue;
        Row row;
        for (size_t i = 0; i < header.size(); ++i)
            row[header[i]] = i < values.size() ? values[i] : "";
        rows.push_back(std::move(row));
    }
    return rows;
}
struct Seq {
    const uint8_t* frames;
    uint32_t count, duration;
};
static_assert(sizeof(Seq) == 16);
void Check(const std::filesystem::path& exe, const std::filesystem::path& install, const std::string& mod) {
    const auto data = install / "Mods" / mod / (mod + ".mpq") / "data";
    const auto excel = data / "global/excel";
    auto monsters = Table(excel / "monstats.txt");
    auto hires = Table(excel / "hireling.txt");
    auto skills = Table(excel / "skills.txt");
    auto sequenceRows = Table(excel / "monseq.txt");
    std::map<std::string, int> skillIds, modes, sequenceIds;
    std::map<std::string, Row> skillRows;
    const std::array<std::string, 16> modeNames{"DT", "NU", "WL", "GH", "A1", "A2", "BL", "SC",
                                                "S1", "S2", "S3", "S4", "DD", "KB", "SQ", "RN"};
    for (int i = 0; i < 16; ++i)
        modes[modeNames[i]] = i;
    for (const auto& row : skills)
        if (!row.at("*Id").empty()) {
            skillIds[row.at("skill")] = std::stoi(row.at("*Id"));
            skillRows[row.at("skill")] = row;
        }
    std::vector<Bytes> frames;
    for (const auto& row : sequenceRows) {
        const auto& name = row.at("sequence");
        if (!sequenceIds.contains(name)) {
            sequenceIds[name] = int(frames.size());
            frames.emplace_back();
        }
        auto& bytes = frames[sequenceIds.at(name)];
        const auto start = bytes.size();
        bytes.resize(start + 6);
        Put<uint16_t>(bytes.data(), start, uint16_t(sequenceIds.at(name)));
        bytes[start + 2] = uint8_t(modes.at(row.at("mode")));
        bytes[start + 3] = uint8_t(std::stoi(row.at("frame")));
        bytes[start + 4] = row.at("dir").empty() ? 0 : uint8_t(std::stoi(row.at("dir")));
        bytes[start + 5] = row.at("event").empty() ? 0 : uint8_t(std::stoi(row.at("event")));
    }
    // Existing sequence BIN agrees with the TXT; no new sequence or numeric ID is introduced.
    auto sequenceBin = Read(excel / "monseq.bin");
    Bytes joined;
    for (const auto& bytes : frames)
        joined.insert(joined.end(), bytes.begin(), bytes.end());
    assert(joined.size() + 4 == sequenceBin.size() &&
           !memcmp(joined.data(), sequenceBin.data() + 4, joined.size()));
    std::vector<Seq> sequences;
    for (const auto& bytes : frames)
        sequences.push_back({bytes.data(), uint32_t(bytes.size() / 6), uint32_t(bytes.size() / 6)});
    auto originalMonsters = Read(excel / "monstats.bin");
    auto monster2 = Read(excel / "monstats2.bin");
    auto count = Get<uint32_t>(originalMonsters.data(), 0);
    assert(originalMonsters.size() == 4 + size_t(count) * 0x1dc);
    Bytes compiled(945 * 0x1dc);
    memcpy(compiled.data(), originalMonsters.data() + 4,
           (std::min)(compiled.size(), originalMonsters.size() - 4));
    auto crusader = compiled.data() + 944 * 0x1dc;
    memcpy(crusader, compiled.data() + 338 * 0x1dc, 0x1dc);
    Put<uint16_t>(crusader, 0, 944);
    const auto found =
        std::find_if(monsters.begin(), monsters.end(), [](const Row& r) { return r.at("Id") == "act4hire"; });
    assert(found != monsters.end() && found->at("Code") == "GU" && found->at("MonStatsEx") == "act2hire");
    // Compile only the relevant native fields, at offsets verified in the 2.4 reader.
    for (int slot = 0; slot < 8; ++slot) {
        const auto suffix = std::to_string(slot + 1);
        const auto& skill = found->at("Skill" + suffix);
        const auto& mode = found->at("Sk" + suffix + "mode");
        Put<int16_t>(crusader, 0x1a4 + slot * 2, skill.empty() ? -1 : int16_t(skillIds.at(skill)));
        crusader[0x1b4 + slot] = uint8_t(modes.contains(mode) ? modes.at(mode) : 14);
        Put<int16_t>(crusader, 0x1bc + slot * 2,
                     sequenceIds.contains(mode) ? int16_t(sequenceIds.at(mode)) : -1);
    }
    auto file = Read(exe);
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(file.data());
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(file.data() + dos->e_lfanew);
    auto image = static_cast<uint8_t*>(
        VirtualAlloc(nullptr, nt->OptionalHeader.SizeOfImage, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    assert(image);
    for (auto s = IMAGE_FIRST_SECTION(nt); s < IMAGE_FIRST_SECTION(nt) + nt->FileHeader.NumberOfSections; ++s)
        memcpy(image + s->VirtualAddress, file.data() + s->PointerToRawData, s->SizeOfRawData);
    // Fail closed if these are not the native functions audited for 1.4.71510.
    auto verify = [&](uint32_t offset, uint32_t size, uint64_t expected) {
        uint64_t hash = 14695981039346656037ULL;
        for (uint32_t i = 0; i < size; ++i)
            hash = (hash ^ image[offset + i]) * 1099511628211ULL;
        assert(hash == expected);
    };
    verify(0xa4440, 0x6c, 0x199fbbe8ec2bd3d8ULL);
    verify(0x24c4d0, 0x264, 0xfe7ca52eb620cc90ULL);
    verify(0x20a040, 0x99, 0x6b01557645b3dca3ULL);
    verify(0x209f30, 0xe1, 0xdb582a36e5babe9eULL);
    Put(image, 0x1c9f878, compiled.data());
    Put<uint64_t>(image, 0x1c9f880, compiled.size() / 0x1dc);
    Put(image, 0x1c9f8b8, monster2.data() + 4);
    Put<uint64_t>(image, 0x1c9f8c0, Get<uint32_t>(monster2.data(), 0));
    Put(image, 0x1c9fa98, sequences.data());
    Put<uint64_t>(image, 0x1c9faa0, sequences.size());
    DWORD old = 0;
    assert(VirtualProtect(image, nt->OptionalHeader.SizeOfImage, PAGE_EXECUTE_READWRITE, &old));
    auto supported = reinterpret_cast<int (*)(int, int)>(image + 0xa4440);
    auto resolve = reinterpret_cast<const Seq* (*)(void*)>(image + 0x24c4d0);
    auto initialize = reinterpret_cast<void (*)(void*)>(image + 0x20a040);
    auto advance = reinterpret_cast<int (*)(void*, int)>(image + 0x209f30);
    std::array<uint8_t, 0x1b8> unit{};
    std::array<uint8_t, 0x30> skillList{}, usedSkill{}, skill{};
    Put<int>(unit.data(), 0, 1);
    Put<int>(unit.data(), 4, 944);
    Put(unit.data(), 0x100, skillList.data());
    Put(skillList.data(), 0x18, usedSkill.data());
    Put(usedSkill.data(), 0, skill.data());
    Put<uint16_t>(skill.data(), 0, uint16_t(skillIds.at("Whirlwind")));
    // Reproduce the report: missing A2 and absent Whirlwind -> no native sequence.
    assert(!supported(944, 5) && supported(944, 4));
    auto before = Get<int16_t>(crusader, 0x1a6);
    Put<int16_t>(crusader, 0x1a6, -1);
    assert(!resolve(unit.data()));
    Put<int16_t>(crusader, 0x1a6, before);
    auto selected = resolve(unit.data());
    assert(selected == &sequences.at(sequenceIds.at("seq_ancientwhirlwind")) && selected->count == 8);
    initialize(unit.data());
    assert(Get<const uint8_t*>(unit.data(), 0x40) == selected->frames &&
           Get<uint32_t>(unit.data(), 0x48) == 8 * 256);
    auto animation = Read(data / "global/animdata.d2");
    std::map<std::string, uint32_t> animationFrames;
    for (size_t offset = 0; offset < animation.size();) {
        auto entries = Get<uint32_t>(animation.data(), offset);
        offset += 4;
        for (uint32_t i = 0; i < entries; ++i, offset += 160) {
            assert(offset + 160 <= animation.size());
            std::string name(reinterpret_cast<char*>(animation.data() + offset), 8);
            name.resize(name.find('\0') == std::string::npos ? name.size() : name.find('\0'));
            animationFrames[name] = Get<uint32_t>(animation.data(), offset + 8);
        }
    }
    GameAssets assets(install, mod);
    auto cof = assets.Read("global/monsters/gu/cof/gua1hth.cof");
    assert(cof.size() >= 28 && cof[1] == animationFrames.at("GUA1HTH"));
    unsigned events = 0;
    for (uint32_t frame = 0; frame < selected->count; ++frame) {
        Put(unit.data(), 0x4c, frame * 256);
        advance(unit.data(), int(frame ? (frame - 1) * 256 : 0));
        assert(Get<uint32_t>(unit.data(), 0x58) == 4);
        assert(Get<uint32_t>(unit.data(), 0x5c) / 256 < animationFrames.at("GUA1HTH"));
        events += Get<uint8_t>(unit.data(), 0x6a) == 1;
    }
    assert(events == 2);
    unsigned checked = 0;
    for (const auto& row : hires)
        if (row.at("Class") == "944") {
            for (int i = 1; i <= 6; ++i) {
                const auto suffix = std::to_string(i);
                const auto& name = row.at("Skill" + suffix);
                if (name.empty() || skillRows.at(name).at("aura") == "1" ||
                    skillRows.at(name).at("passive") == "1")
                    continue;
                auto mode = std::stoi(row.at("Mode" + suffix));
                assert(supported(944, mode));
                if (mode == 14)
                    assert(name == "Whirlwind");
                else
                    assert(animationFrames.contains("GU" + modeNames.at(mode) + "HTH"));
                ++checked;
            }
        }
    assert(checked == 15);
    assert(VirtualFree(image, 0, MEM_RELEASE));
    std::cout << "PASS: all 3 Crusader difficulties; native A2 failure reproduced; Whirlwind lookup/init/8 "
                 "frames/2 attack events; 15 active skill modes; packed GU COF\n";
}
} // namespace
int wmain(int argc, wchar_t** argv) {
    assert(argc == 4);
    const auto length = WideCharToMultiByte(CP_UTF8, 0, argv[3], -1, nullptr, 0, nullptr, nullptr);
    assert(length > 0);
    std::string mod(size_t(length), '\0');
    assert(WideCharToMultiByte(CP_UTF8, 0, argv[3], -1, mod.data(), length, nullptr, nullptr));
    mod.pop_back();
    Check(argv[1], argv[2], mod);
}
