#define NOMINMAX
#include "../d2rhud/plugin/InspectAssets.cpp"
#include <cassert>
#include <iostream>
#include <thread>
namespace {
using Bytes = std::vector<uint8_t>;
std::map<std::string, Bytes> tables, files;
Bytes Table(size_t stride, uint32_t count) {
    Bytes b(4 + stride * count);
    memcpy(b.data(), &count, 4);
    return b;
}
Bytes Sprite(unsigned w, unsigned h) {
    Bytes b(40 + w * h * 4, 255);
    memcpy(b.data(), "SpA1", 4);
    uint16_t format = 31, fw = uint16_t(w);
    uint32_t frames = 1;
    memcpy(b.data() + 4, &format, 2);
    memcpy(b.data() + 6, &fw, 2);
    memcpy(b.data() + 8, &w, 4);
    memcpy(b.data() + 12, &h, 4);
    memcpy(b.data() + 20, &frames, 4);
    return b;
}
void JsonFile(const char* key, const char* json) { files[key] = Bytes(json, json + strlen(json)); }
} // namespace
GameAssets::GameAssets(const std::filesystem::path&, const std::string&) {}
GameAssets::~GameAssets() = default;
std::vector<uint8_t> GameAssets::Read(const std::string& key) {
    readError_.clear();
    auto it = files.find(key);
    if (it == files.end()) {
        readError_ = "Fixture packed storage unavailable (Windows error 2).";
        return {};
    }
    return it->second;
}
GameAssets::TableFiles GameAssets::ReadTable(const std::string& key, bool) {
    return {{}, tables.at(key), {}};
}
std::vector<std::string> GameAssets::ReadTableNames(const std::string&, bool) { return {}; }
int main() {
    tables["itemtypes"] = Table(0xe4, 1);
    tables["weapons"] = Table(0x1b4, 1);
    memcpy(tables["weapons"].data() + 4 + 0x80, "crs", 3);
    tables["weapons"][4 + 0x116] = 2;
    tables["weapons"][4 + 0x117] = 4;
    tables["armor"] = Table(0x1b4, 0);
    tables["misc"] = Table(0x1b4, 0);
    tables["uniqueitems"] = Table(0x150, 0);
    tables["setitems"] = Table(0x1bc, 0);
    JsonFile("hd/items/items.json", R"([{"crs":{"asset":"sword/crystal_sword"}}])");
    JsonFile("hd/items/uniques.json", "[]");
    JsonFile("hd/items/sets.json", "[]");
    const std::string full = "hd/global/ui/items/weapon/sword/crystal_sword.sprite";
    const std::string low = "hd/global/ui/items/weapon/sword/crystal_sword.lowend.sprite";
    auto snapshot = std::make_shared<Inspect::Data::Snapshot>();
    Inspect::Data::Item item;
    item.id = 7;
    item.code = "crs";
    item.width = 2;
    item.height = 4;
    snapshot->items.push_back(item);
    item.id = 8;
    snapshot->items.push_back(item);
    Inspect::ConfigureAssets(L"C:\\fixture", "RMD-MP");
    Inspect::View view;
    view.open = true;
    view.snapshot = snapshot;
    auto load = [&] {
        ++view.revision;
        for (int n = 0; n < 10000; ++n) {
            const auto art = Inspect::Art(view);
            if (art && art->revision == view.revision)
                return art;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        assert(false);
        return std::shared_ptr<const Inspect::Assets>{};
    };
    files[low] = Sprite(98, 196);
    auto art = load();
    assert(art->icons.size() == 2 && art->icons.at(7) == art->icons.at(8));
    assert(art->iconScales.at(7) == 2 && art->sprites[art->icons.at(7)]->atlas.frameWidth == 98);
    files[full] = Sprite(196, 392);
    art = load();
    assert(art->iconScales.at(7) == 1);
    files[full] = Bytes(4, 0); // Bad full-resolution data must still try lowend.
    art = load();
    assert(art->icons.size() == 2 && art->iconScales.at(7) == 2);
    files.erase(full);
    files.erase(low);
    art = load();
    assert(art->icons.empty());
    const auto report = HudConfig::Json::parse(art->artworkReport);
    assert(report["items"] == 2 && report["decodedItems"] == 0 && report["problems"].size() == 2);
    assert(report["problems"][0]["path"] == full);
    assert(report["problems"][0]["reason"].get<std::string>().find("Windows error 2") != std::string::npos);
    std::cout
        << "PASS: missing full-resolution sprites use lowend at native inventory size; full art preferred; "
           "invalid sprites retry lowend; duplicate art shared; missing images report exact path/read "
           "error\n";
}
