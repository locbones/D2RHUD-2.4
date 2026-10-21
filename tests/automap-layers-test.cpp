#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <random>
#include <tuple>
#include "../d2rhud/plugin/AutomapLayers.cpp"

DWORD64 Pattern::Address(uint32_t) { return 0; }
uint64_t transientSeed=0;
uint64_t CubeMap::ClientLayerSeed(uint32_t){return transientSeed;}
namespace {
using namespace AutomapLayers;
std::vector<uint8_t> backing;
CachedFile cache{};
bool available = true;
int nativeSaves = 0, nativeLoads = 0, resizes = 0;
std::vector<std::tuple<uint32_t, int32_t, int32_t, void*, int>> inserted;
CachedFile* __fastcall Resolve(void*) { return available ? &cache : nullptr; }
void __fastcall Resize(void* vector, size_t size) {
    assert(vector == &cache.data); ++resizes;
    backing.resize(size); cache.data = backing.data(); cache.size = cache.capacity = backing.size();
}
void __fastcall Insert(uint32_t image, uint64_t point, void* tree, int saved) {
    inserted.emplace_back(image, static_cast<int32_t>(point), static_cast<int32_t>(point >> 32), tree, saved);
}
void __fastcall NativeSave(const NativeRecord*, void*) { ++nativeSaves; }
void __fastcall NativeLoad(void*) { ++nativeLoads; }
void SetCache(std::vector<uint8_t> bytes) {
    backing = std::move(bytes); cache.data = backing.data(); cache.size = cache.capacity = backing.size(); cache.state = 0;
}
std::span<const uint8_t> Bytes(const std::vector<Data::Cell>& cells) {
    return {reinterpret_cast<const uint8_t*>(cells.data()), cells.size() * sizeof(Data::Cell)};
}
bool Same(const std::vector<Data::Cell>& a, const std::vector<Data::Cell>& b) {
    return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(Data::Cell)) == 0);
}
// Independent simulation of a vanilla append, including its fixed 100-entry
// table. Used to check saves interleaved by the unmodified native path.
void VanillaAppend(std::vector<uint8_t>& file, uint32_t layer, Data::Cell cell) {
    assert(layer < 100);
    if (file.empty()) file.resize(400, 0);
    const uint32_t at = static_cast<uint32_t>(file.size());
    uint32_t tail = Data::Word(file, layer * 4);
    if (tail) {
        while (Data::Word(file, tail)) tail = Data::Word(file, tail);
        Data::Put(file, tail, at);
    } else Data::Put(file, layer * 4, at);
    file.resize(file.size() + 38, 0);
    Data::Put(file, at + 4, layer); Data::Put(file, at + 16, 6);
    std::memcpy(file.data() + at + 32, &cell, 6);
}
std::vector<uint8_t> ReadFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate); assert(input);
    std::vector<uint8_t> bytes(static_cast<size_t>(input.tellg())); input.seekg(0);
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())); assert(input);
    return bytes;
}
void CodecTests() {
    const std::vector<Data::Cell> cells{{1,-32768,32767},{65535,-1,-2},{5,0,0}};
    std::vector<uint8_t> file;
    VanillaAppend(file, 0, {11,10,20}); VanillaAppend(file, 99, {12,30,40});
    const auto header = std::vector<uint8_t>(file.begin(), file.begin() + 400);
    for (uint32_t layer : {100u,101u,255u,256u,65535u,65536u,1000000u,Data::MaxLayer}) {
        Data::RecordView record; record.layer = layer; record.special = layer % 4; record.metadata = 0x12345678;
        for (auto& group : record.cells) group = Bytes(cells);
        for (int batch = 0; batch < 3; ++batch) {
            std::vector<uint8_t> output;
            assert(Data::Append(file, record, output)); file = std::move(output);
        }
        assert(std::equal(header.begin(), header.end(), file.begin()));
        std::vector<Data::Record> restored; assert(Data::Load(file, layer, restored)); assert(restored.size() == 3);
        for (const auto& saved : restored) {
            assert(saved.special == record.special);
            for (const auto& group : saved.cells) assert(Same(group, cells));
        }
    }
    VanillaAppend(file, 99, {13,50,60});
    const auto lowHead = Data::Word(file, 99 * 4);
    assert(Data::Word(file, lowHead) == file.size() - 38);
    std::vector<Data::Record> selected; assert(Data::Load(file, 100, selected) && selected.size() == 3);
    assert(Data::Load(file, 102, selected) && selected.empty());
    assert(Data::Load({}, 100, selected) && selected.empty());
    for (uint32_t invalid : {0u,99u,0x80000000u,0xffffffffu}) {
        Data::RecordView record; record.layer = invalid;
        std::vector<uint8_t> output{9};
        assert(!Data::Append(file, record, output) && output == std::vector<uint8_t>{9});
        assert(!Data::Load(file, invalid, selected));
    }
    // Every truncation of a single record is rejected, without returning partial
    // exploration or changing the caller's existing output buffer.
    Data::RecordView record; record.layer = 100; record.cells[0] = Bytes(cells);
    std::vector<uint8_t> single; assert(Data::Append({}, record, single));
    for (size_t length = 1; length < single.size(); ++length) {
        if (length == 400) continue;
        std::vector<uint8_t> output{9};
        assert(!Data::Append(std::span(single).first(length), record, output));
        assert(output == std::vector<uint8_t>{9});
        selected.resize(1); assert(!Data::Load(std::span(single).first(length), 100, selected) && selected.size() == 1);
    }
    for (uint32_t count : {1u,2u,5u,UINT32_MAX,UINT32_MAX-1}) {
        auto corrupt = single; Data::Put(corrupt, 400 + 16, count);
        std::vector<uint8_t> output{9}; assert(!Data::Append(corrupt, record, output));
        assert(!Data::Load(corrupt, 100, selected));
    }
    // High-ID lookup ignores linked-list pointers, so a corrupt/cyclic chain
    // cannot hang this scanner or make another layer's cells appear.
    Data::Put(single, 400, 400);
    assert(Data::Load(single, 100, selected) && selected.size() == 1);
    Data::Put(single, 400, UINT32_MAX);
    assert(Data::Load(single, 101, selected) && selected.empty());

    std::mt19937 random(71510);
    std::map<uint32_t, std::vector<Data::Cell>> expected;
    file.clear();
    for (int batch = 0; batch < 1000; ++batch) {
        const uint32_t layer = 100 + random() % 250;
        const Data::Cell cell{static_cast<uint16_t>(random()),static_cast<int16_t>(random()),static_cast<int16_t>(random())};
        const std::vector<Data::Cell> group{cell};
        Data::RecordView view; view.layer = layer; view.cells[0] = Bytes(group);
        std::vector<uint8_t> output; assert(Data::Append(file, view, output)); file = std::move(output);
        expected[layer].push_back(cell);
        if (batch % 11 == 0) VanillaAppend(file, 99, {1,2,3});
    }
    for (const auto& [layer, wanted] : expected) {
        assert(Data::Load(file, layer, selected)); std::vector<Data::Cell> actual;
        for (const auto& saved : selected) actual.insert(actual.end(), saved.cells[0].begin(), saved.cells[0].end());
        assert(Same(actual, wanted));
    }
}
void HookTests() {
    Layer layer{100}; Layer* current = &layer;
    int32_t imageCount = 2000;
    std::array<uint8_t, 0x160> specialSets{};
    const int32_t specialCount = 100;
    std::memcpy(specialSets.data() + 3 * 0x58, &specialCount, 4);
    engine = {Resolve, Resize, Insert, &current, &imageCount, specialSets.data()};
    originalSave = NativeSave; originalLoad = NativeLoad;
    int map = 0;
    NativeRecord record{}; record.layer = 100; record.special = 3; record.metadata = 77;
    const std::vector<Data::Cell> cells{{1,-32768,32767},{5,-1,-2}};
    for (size_t i = 0; i < 4; ++i) {
        record.counts[i] = static_cast<uint32_t>(Bytes(cells).size());
        record.groups[i] = {Bytes(cells).data(), cells.size() * 3, cells.size() * 3};
    }
    SetCache({});
    HookSave(&record, &map); assert(nativeSaves == 0 && resizes == 1 && cache.state == 4);
    const auto first = backing;
    record.layer = 101; HookSave(&record, &map); assert(resizes == 2);
    assert(std::equal(first.begin(), first.end(), backing.begin()));
    const auto beforeLoad = backing; HookLoad(&map);
    assert(inserted.size() == 8 && backing == beforeLoad && nativeLoads == 0);
    for (size_t group = 0; group < 4; ++group) for (size_t i = 0; i < 2; ++i) {
        const auto& [image,x,y,tree,saved] = inserted[group*2+i];
        assert(image == cells[i].image && x == cells[i].x && y == cells[i].y && saved == 1);
        assert(tree == reinterpret_cast<uint8_t*>(&layer) + 8 + group*0x28);
    }
    imageCount = 2; inserted.clear(); HookLoad(&map); assert(inserted.size() == 5);
    const int32_t smallSpecial = 2; std::memcpy(specialSets.data()+3*0x58, &smallSpecial, 4);
    inserted.clear(); HookLoad(&map); assert(inserted.size() == 4);
    layer.id = 102; inserted.clear(); HookLoad(&map); assert(inserted.empty());
    for (int32_t low : {0, 99}) {
        layer.id = low; HookLoad(&map); record.layer = static_cast<uint32_t>(low); HookSave(&record, &map);
    }
    assert(nativeLoads == 2 && nativeSaves == 2);
    const int count = resizes;
    layer.id = -1; HookLoad(&map); record.layer = UINT32_MAX; HookSave(&record, &map);
    assert(nativeLoads == 2 && nativeSaves == 2 && resizes == count);
    current = nullptr; HookLoad(&map); current = &layer;
    layer.id = 100; record.layer = 100; available = false;
    HookLoad(&map); HookSave(&record, &map); available = true;
    assert(inserted.empty() && nativeLoads == 2 && nativeSaves == 2 && resizes == count);
    record.groups[0].size = 1; HookSave(&record, &map); record.groups[0].size = cells.size()*3;
    record.special = 4; HookSave(&record, &map); record.special = 3;
    assert(resizes == count && backing == beforeLoad);
    auto corrupt = backing; corrupt.pop_back(); SetCache(corrupt);
    HookLoad(&map); HookSave(&record, &map); assert(inserted.empty() && backing == corrupt && resizes == count);
    // Invalid cache size is rejected before constructing a span or dereferencing
    // any data, and must never fall back to the unsafe native high-ID path.
    cache.size = uint64_t(UINT32_MAX) + 1; HookLoad(&map); HookSave(&record, &map);
    assert(nativeLoads == 2 && nativeSaves == 2 && resizes == count);
    SetCache({});
    transientSeed=123;layer.id=5;record.layer=5;record.groups[0].size=cells.size()*3;record.counts[0]=uint32_t(Bytes(cells).size());
    const auto disk=backing;const auto nativeSaveCount=nativeSaves,nativeLoadCount=nativeLoads;
    HookSave(&record,&map);inserted.clear();HookLoad(&map);assert(!inserted.empty());
    assert(backing==disk && nativeSaves==nativeSaveCount && nativeLoads==nativeLoadCount);
    transientSeed=124;inserted.clear();HookLoad(&map);assert(inserted.empty());
    transientSeed=123;HookLoad(&map);assert(!inserted.empty());
    transientSeed=0;
}
void ExecutableTest(const std::filesystem::path& path) {
    const auto exe = ReadFile(path);
    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(exe.data()); assert(dos->e_magic == IMAGE_DOS_SIGNATURE);
    const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(exe.data()+dos->e_lfanew); assert(nt->Signature == IMAGE_NT_SIGNATURE);
    const auto sections = IMAGE_FIRST_SECTION(nt);
    auto at = [&](uint32_t rva)->uintptr_t {
        for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i)
            if (rva >= sections[i].VirtualAddress && rva-sections[i].VirtualAddress < sections[i].SizeOfRawData)
                return reinterpret_cast<uintptr_t>(exe.data()+sections[i].PointerToRawData+rva-sections[i].VirtualAddress);
        return 0;
    };
    assert(Supported(at));
    for (const auto& signature : signatures) {
        auto changed = std::vector<uint8_t>(reinterpret_cast<const uint8_t*>(at(signature.rva)),reinterpret_cast<const uint8_t*>(at(signature.rva))+signature.size);
        changed[signature.size/2] ^= 1;
        assert(!Supported([&](uint32_t rva) { return rva == signature.rva ? reinterpret_cast<uintptr_t>(changed.data()) : at(rva); }));
    }
    std::cout << "All six full native code signatures match the installed executable; altered code is rejected.\n";
}
void ExistingFilesTest(const std::filesystem::path& root) {
    size_t files = 0, records = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file()) continue;
        const auto extension = entry.path().extension().string();
        if (extension.size() != 4 || extension.substr(0,3) != ".ma" || extension[3] < '0' || extension[3] > '3') continue;
        const auto original = ReadFile(entry.path());
        assert(Data::Walk(original, [&](size_t, const Data::RecordView&) { ++records; }));
        std::vector<uint8_t> output;
        Data::RecordView view; view.layer = 1000000;
        assert(Data::Append(original, view, output));
        assert(std::equal(original.begin(), original.end(), output.begin()));
        std::vector<Data::Record> restored; assert(Data::Load(output, view.layer, restored) && restored.size() == 1);
        ++files;
    }
    assert(files > 0);
    std::cout << "Read-only compatibility check: " << files << " existing map files, " << records << " native records.\n";
}
}
int main(int argc, char** argv) {
    CodecTests(); HookTests();
    if (argc > 1) ExecutableTest(argv[1]);
    if (argc > 2) ExistingFilesTest(argv[2]);
    std::cout << "Extended layer persistence, layer isolation, vanilla interleaving, signed coordinates, malformed-data and native-hook regression tests passed.\n";
}
