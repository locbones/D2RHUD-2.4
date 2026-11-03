#define NOMINMAX
#include "../d2rhud/plugin/D2RHUD/HudPreferences.cpp"
#include "../d2rhud/plugin/MercIcons.cpp"
#include "../d2rhud/plugin/TableMetadataExport.h"
#include <cassert>
#include <fstream>
#include <iostream>

uintptr_t testBase = 0;
DWORD64 Pattern::Address(uint32_t rva) { return testBase + rva; }
namespace {
using namespace MercIcons;
namespace fs = std::filesystem;
template <class T> void Put(void* data, size_t offset, T value) {
    std::memcpy(static_cast<uint8_t*>(data) + offset, &value, sizeof(value));
}
std::string Text(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), {}};
}
void Write(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(text.data(), text.size());
    out.close();
    assert(out);
}
void Policy() {
    const auto frames =
        Data::Parse("\xef\xbb\xbfSkillDesc\tIconCel\tHireableIconCel\r\nAlpha\t9\t0\r\nBeta\t3\t32\r\nBlank\t"
                    "5\t\r\nBad\t9\t-2\r\nBig\t1\t65536\r\nAlpha\t1\t8\r\nExpansion\t\t\r\n");
    assert(frames.size() == 5 && frames.at("alpha") == 0 && frames.at("beta") == 32);
    assert(frames.at("blank") == -1 && frames.at("bad") == -1 && frames.at("big") == -1);
    assert(Data::Frame(" 17 ") == 17 && Data::Frame("1x") == -1 && Data::Frame("2147483648") == -1);
    assert(Data::Parse("skilldesc\tIconCel\nalpha\t4\n").empty());
    for (bool rmd : {false, true}) {
        HudPreferences::Config config;
        HudPreferences::Load(config, HudConfig::Json::object(), rmd);
        assert(config.MercIconsFix);
        config.MercIconsFix = false;
        HudConfig::Json json;
        HudPreferences::Save(config, json);
        HudPreferences::Config restored;
        HudPreferences::Load(restored, json, rmd);
        assert(!restored.MercIconsFix);
        config.MercIconsFix = true;
        HudPreferences::Save(config, json);
        HudPreferences::Load(restored, json, rmd);
        assert(restored.MercIconsFix);
    }
}
fs::path Tables() {
    const auto root =
        fs::temp_directory_path() / ("d2rhud-merc-icons-" + std::to_string(GetCurrentProcessId()));
    const auto data = root / "Mods/OnlyBin/OnlyBin.mpq/data", excel = data / "global/excel";
    const std::string text = "skilldesc\tIconCel\tHireableIconCel\nalpha\t8\t0\nbeta\t9\t32\ngamma\t4\t17\nde"
                             "lta\t2\t3\nepsilon\t6\t24\nzeta\t1\t28\n";
    std::string binary(4 + 6 * 0x128, 0);
    Put<uint32_t>(binary.data(), 0, 6);
    for (uint16_t i = 0; i < 6; ++i)
        Put(binary.data(), 4 + size_t(i) * 0x128, i);
    Write(excel / "skilldesc.txt", text);
    Write(excel / "skilldesc.bin", binary);
    Load(root, "OnlyBin");
    const auto fromText = source.load();
    assert(fromText && fromText->at("alpha") == 0 && fromText->at("zeta") == 28);
    const auto output = TableMetadataExport::Build(data);
    const auto metadata = data / "D2RHUD/table-metadata.json";
    assert(output.metadata["tables"]["skilldesc"]["overlay"].get<std::string>().starts_with(
        "skilldesc\tHireableIconCel\n"));
    Write(metadata, output.metadata.dump());
    fs::remove(excel / "skilldesc.txt");
    Load(root, "OnlyBin");
    assert(*source.load() == *fromText);
    // A higher-priority mod BIN cannot borrow a retail TXT's icon mappings.
    Write(root / "data/global/excel/skilldesc.txt", "skilldesc\tHireableIconCel\nwrong\t12\n");
    auto stale = output.metadata;
    stale["tables"]["skilldesc"]["bin_hash"] = "bad";
    Write(metadata, stale.dump());
    Load(root, "OnlyBin");
    assert(source.load()->empty());
    Write(metadata, "{ malformed");
    Load(root, "OnlyBin");
    assert(source.load()->empty());
    Write(metadata, output.metadata.dump());
    Load(root, "OnlyBin");
    assert(*source.load() == *fromText);
    return root;
}
constexpr std::array<int, 6> skills{7, 11, 99, 209, 600, 601}, frames{0, 32, 17, 3, 24, 28},
    descriptions{4, 2, 0, 5, 1, 3};
int lookups = 0, tooltips = 0, unwinds = 0;
int tooltipSkill = -1, tooltipLevel = -1, tooltipRequirement = -1;
void* tooltipContext = nullptr;
int __fastcall LookupMock(void*, const char* name) {
    ++lookups;
    const std::array<const char*, 6> names{"alpha", "beta", "gamma", "delta", "epsilon", "zeta"};
    for (size_t i = 0; i < names.size(); ++i)
        if (!strcmp(name, names[i]))
            return descriptions[i];
    return -1;
}
void* __fastcall TooltipMock(void* widget, void* text, void* context) {
    ++tooltips;
    tooltipSkill = Field<int32_t>(widget, 0x8f8);
    tooltipLevel = Field<int32_t>(widget, 0x8fc);
    tooltipRequirement = Field<int32_t>(widget, 0x904);
    tooltipContext = context;
    Put<uintptr_t>(text, 0, uintptr_t(static_cast<uint8_t*>(text) + 0x18));
    Put<uint64_t>(text, 8, 0);
    Put<uint64_t>(text, 0x10, 0x800000000000000fULL);
    *(static_cast<uint8_t*>(text) + 0x18) = 0;
    return text;
}
void __fastcall Nothing() {}
void __fastcall AssignMock(void*, void*) {}
int __fastcall ProbeResolve(int skill) {
    CONTEXT context{};
    RtlCaptureContext(&context);
    bool crossed = false;
    for (int i = 0; i < 6 && context.Rip; ++i) {
        const bool inRelay = context.Rip >= uintptr_t(relay) && context.Rip < uintptr_t(relay) + 0x800;
        DWORD64 base = 0;
        const auto entry = RtlLookupFunctionEntry(context.Rip, &base, nullptr);
        assert(entry);
        const auto stack = context.Rsp;
        void* handler = nullptr;
        DWORD64 frame = 0;
        RtlVirtualUnwind(0, base, context.Rip, entry, &context, &handler, &frame, nullptr);
        if (inRelay) {
            const auto exe = uintptr_t(GetModuleHandleW(nullptr));
            const auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(exe);
            const auto nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(exe + dos->e_lfanew);
            crossed = context.Rip >= exe && context.Rip < exe + nt->OptionalHeader.SizeOfImage &&
                      context.Rsp >= stack + 0xc0;
            break;
        }
    }
    assert(crossed);
    ++unwinds;
    return Resolve(skill);
}
template <class T> void Stub(uint8_t* image, uint32_t rva, T function) {
    const uint8_t jump[]{0xff, 0x25, 0, 0, 0, 0};
    std::memcpy(image + rva, jump, 6);
    Put(image + rva, 6, uintptr_t(function));
}
void NativeChecks(const fs::path& path, const fs::path& root) {
    std::ifstream input(path, std::ios::binary);
    assert(input);
    std::vector<uint8_t> file{std::istreambuf_iterator<char>(input), {}};
    const auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(file.data());
    const auto nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(file.data() + dos->e_lfanew);
    auto image = static_cast<uint8_t*>(VirtualAlloc(nullptr, nt->OptionalHeader.SizeOfImage,
                                                    MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    assert(image);
    for (auto section = IMAGE_FIRST_SECTION(nt);
         section < IMAGE_FIRST_SECTION(nt) + nt->FileHeader.NumberOfSections; ++section)
        std::memcpy(image + section->VirtualAddress, file.data() + section->PointerToRawData,
                    section->SizeOfRawData);
    testBase = uintptr_t(image);
    assert(Supported());
    image[NativeData::Site] ^= 1;
    assert(!Supported());
    image[NativeData::Site] ^= 1;
    const auto directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    assert(RtlAddFunctionTable(reinterpret_cast<RUNTIME_FUNCTION*>(image + directory.VirtualAddress),
                               directory.Size / sizeof(RUNTIME_FUNCTION), testBase));
    Stub(image, 0xbb9120, TooltipMock);
    Stub(image, 0x7ba00, AssignMock);
    Stub(image, 0x1434c50, Nothing);
    Stub(image, 0x6d0470, LookupMock);
    std::vector<uint8_t> skillRows(602 * 0x284), descriptionRows(6 * 0x128);
    Put(image, 0x1c9fad0, skillRows.data());
    Put<uint64_t>(image, 0x1c9fad8, 602);
    Put(image, 0x1c9e9e0, descriptionRows.data());
    Put<uint64_t>(image, 0x1c9e9e8, 6);
    Put(image, 0x1c9e9f8, descriptionRows.data());
    for (size_t i = 0; i < 6; ++i) {
        Put<int16_t>(skillRows.data(), size_t(skills[i]) * 0x284 + 0x1d4, int16_t(descriptions[i]));
        Put<int16_t>(skillRows.data(), size_t(skills[i]) * 0x284 + 0x1ac, int16_t(10 + i));
    }
    const auto originalSkills = skillRows, originalDescriptions = descriptionRows;
    FlushInstructionCache(GetCurrentProcess(), image, nt->OptionalHeader.SizeOfImage);
    using Widget = std::array<uint8_t, 0x908 + 32>;
    auto initialize = [] {
        Widget result;
        result.fill(0xa5);
        for (size_t i = 0; i < 16; ++i) {
            result[0x8b0 + i] = uint8_t(i + 1);
            result[0x8c0 + i] = uint8_t(i + 21);
        }
        return result;
    };
    const auto setter = Native<void(__fastcall*)(void*, void*, int, int, int)>(0xbb8dd0);
    std::array<Widget, 6> retail;
    for (size_t i = 0; i < 6; ++i) {
        retail[i] = initialize();
        setter(retail[i].data(), reinterpret_cast<void*>(0x1234), skills[i], 23, 9);
    }
    Load(root, "OnlyBin");
    assert(Install() && Available());
    DWORD protection = 0;
    assert(VirtualProtect(relay, Allocation, PAGE_EXECUTE_READWRITE, &protection));
    const auto code = NativeData::Relay(uintptr_t(ProbeResolve), uintptr_t(trampoline),
                                        Pattern::Address(NativeData::Resume));
    std::memcpy(relay, code.data(), code.size());
    assert(VirtualProtect(relay, Allocation, PAGE_EXECUTE_READ, &protection));
    FlushInstructionCache(GetCurrentProcess(), relay, Allocation);
    const int before = tooltips;
    for (int panel = 0; panel < 2; ++panel)
        for (size_t slot = 0; slot < 6; ++slot) {
            auto widget = initialize();
            SetEnabled(true);
            setter(widget.data(), reinterpret_cast<void*>(0x1234), skills[slot], 23, 9);
            assert(Field<int32_t>(widget.data(), 0x5b0) == frames[slot]);
            assert(tooltipSkill == skills[slot] && tooltipLevel == 23 && tooltipRequirement == 10 + slot &&
                   tooltipContext == reinterpret_cast<void*>(0x1234));
            assert(Field<int32_t>(widget.data(), 0x900) == 9);
            assert(!std::memcmp(widget.data() + 0x5bc, widget.data() + 0x8c0, 16));
            assert(std::all_of(widget.end() - 32, widget.end(), [](auto value) { return value == 0xa5; }));
            // Only skill/frame/tooltip/color fields differ from the untouched widget.
            assert(std::all_of(widget.begin(), widget.begin() + 0x5b0,
                               [](auto value) { return value == 0xa5; }));
        }
    assert(tooltips - before == 12 && lookups == 6);
    for (size_t i = 0; i < 6; ++i) {
        auto widget = initialize();
        SetEnabled(false);
        setter(widget.data(), reinterpret_cast<void*>(0x1234), skills[i], 23, 9);
        assert(widget == retail[i]);
    }
    SetEnabled(true);
    assert(Resolve(-1) == -1 && Resolve(602) == -1);
    Put<int16_t>(skillRows.data(), size_t(skills[0]) * 0x284 + 0x1d4, -1);
    assert(Resolve(skills[0]) == -1);
    Put<int16_t>(skillRows.data(), size_t(skills[0]) * 0x284 + 0x1d4, 99);
    assert(Resolve(skills[0]) == -1);
    Put<int16_t>(skillRows.data(), size_t(skills[0]) * 0x284 + 0x1d4, int16_t(descriptions[0]));
    auto replacement = std::make_shared<Data::Frames>(*source.load());
    (*replacement)["alpha"] = 48;
    source.store(replacement);
    assert(Resolve(skills[0]) == 48 && lookups == 12);
    source.store(std::make_shared<const Data::Frames>());
    for (size_t i = 0; i < 6; ++i) {
        auto widget = initialize();
        setter(widget.data(), reinterpret_cast<void*>(0x1234), skills[i], 23, 9);
        assert(widget == retail[i]);
    }
    assert(skillRows == originalSkills && descriptionRows == originalDescriptions);
    assert(unwinds == 24);
    Uninstall();
    assert(!Available());
    std::cout << "Actual native widget: two panels/six slots, custom skill IDs, frame zero, linked "
                 "descriptions, tooltip identity, required levels/colors, exact disabled/missing-data "
                 "fallback, cache refresh and relay unwinding passed.\n";
}
} // namespace
int main(int argc, char** argv) {
    Policy();
    const auto root = Tables();
    if (argc > 1)
        NativeChecks(argv[1], root);
    if (argc > 2) {
        Load(argv[2], argc > 3 ? argv[3] : "");
        const auto data = source.load();
        assert(data && !data->empty());
        size_t count = 0;
        for (const auto& [name, frame] : *data)
            count += frame >= 0;
        GameAssets assets(argv[2], argc > 3 ? argv[3] : "");
        const auto files = assets.ReadTable("skilldesc", true);
        assert(!files.binary.empty() && !files.layer.empty());
        const auto published = root / "Mods/Published/Published.mpq/data";
        Write(published / "global/excel/skilldesc.bin", {files.binary.begin(), files.binary.end()});
        if (!files.text.empty()) {
            // Build the BIN release companion from the real source pair in a
            // private folder; do not rewrite the live marketplace catalog.
            Write(published / "global/excel/skilldesc.txt", {files.text.begin(), files.text.end()});
            Write(published / "D2RHUD/table-metadata.json", TableMetadataExport::Build(published).metadata.dump());
            fs::remove(published / "global/excel/skilldesc.txt");
        } else {
            Write(published / "D2RHUD/table-metadata.json", Text(files.layer / "D2RHUD/table-metadata.json"));
        }
        Load(root, "Published");
        assert(*source.load() == *data);
        std::cout << "Installed mod: " << count << " explicit HireableIconCel mappings.\n";
        std::cout << "Published BIN-only copy resolves identical icon mappings without TXT.\n";
    }
    std::cout << "Merc Icons Fix parser, TXT/BIN metadata, layer isolation and default-on/saved-opt-out "
                 "settings passed.\n";
}
