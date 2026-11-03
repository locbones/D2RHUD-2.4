#define NOMINMAX
#include "../d2rhud/plugin/D2RHUD/MenuWorkspace.h"
#include "../d2rhud/plugin/GameAssets.h"
#include "../d2rhud/plugin/InspectUI.cpp"
#include <cassert>
#include <fstream>
#include <imgui_internal.h>
#include <iostream>
#include <thread>
namespace {
Inspect::View fixture;
Inspect::Appearance appearance;
std::vector<Inspect::Native::Eye> eyeFixtures;
std::pair<uint32_t, uint32_t> opened{UINT32_MAX, UINT32_MAX};
} // namespace
Inspect::Appearance Inspect::GetAppearance() { return appearance; }
Inspect::View Inspect::Current() { return fixture; }
bool Inspect::Enabled() { return true; }
void Inspect::Close() { fixture.open = false; }
void Inspect::Refresh() {}
bool Inspect::Open(uint32_t type, uint32_t id) {
    opened = {type, id};
    return true;
}
std::vector<Inspect::Native::Eye> Inspect::Native::Eyes(float, float) { return eyeFixtures; }
void Capture(const std::filesystem::path& directory, const std::string& name, unsigned char* atlas, int width,
             int height) {
    std::ofstream texture(directory / "atlas.rgba", std::ios::binary);
    texture.write(reinterpret_cast<const char*>(atlas), size_t(width) * height * 4);
    const auto* data = ImGui::GetDrawData();
    std::ofstream out(directory / (name + ".bin"), std::ios::binary);
    uint32_t commands = 0;
    for (auto* list : data->CmdLists)
        commands += list->CmdBuffer.Size;
    const uint32_t header[]{
        uint32_t(data->DisplaySize.x), uint32_t(data->DisplaySize.y), uint32_t(width), uint32_t(height),
        uint32_t(data->TotalVtxCount), uint32_t(data->TotalIdxCount), commands};
    out.write(reinterpret_cast<const char*>(header), sizeof(header));
    for (auto* list : data->CmdLists)
        out.write(reinterpret_cast<const char*>(list->VtxBuffer.Data),
                  size_t(list->VtxBuffer.Size) * sizeof(ImDrawVert));
    uint32_t vertexOffset = 0;
    for (auto* list : data->CmdLists) {
        for (auto index : list->IdxBuffer) {
            const uint32_t value = uint32_t(index) + vertexOffset;
            out.write(reinterpret_cast<const char*>(&value), 4);
        }
        vertexOffset += list->VtxBuffer.Size;
    }
    uint32_t indexOffset = 0;
    for (auto* list : data->CmdLists) {
        for (const auto& cmd : list->CmdBuffer) {
            const uint32_t values[]{indexOffset + cmd.IdxOffset, cmd.ElemCount};
            out.write(reinterpret_cast<const char*>(values), sizeof(values));
            out.write(reinterpret_cast<const char*>(&cmd.ClipRect), sizeof(cmd.ClipRect));
            const uint64_t textureId = cmd.GetTexID();
            out.write(reinterpret_cast<const char*>(&textureId), sizeof(textureId));
        }
        indexOffset += list->IdxBuffer.Size;
    }
}
int main(int argc, char** argv) {
    assert(argc == 3);
    const auto root = std::filesystem::path(argv[1]);
    std::filesystem::create_directories(root);
    Inspect::ConfigureAssets(std::filesystem::path(argv[2]), "RMD-MP");
    auto snapshot = std::make_shared<Inspect::Data::Snapshot>();
    snapshot->id = 1;
    snapshot->name = "PvP Barbarian";
    snapshot->owner = snapshot->name;
    snapshot->designation = "Player";
    GameAssets files(std::filesystem::path(argv[2]), "RMD-MP");
    const auto uniques = files.ReadTable("uniqueitems", true).binary;
    int grandfather = -1;
    for (size_t n = 0; 4 + (n + 1) * 0x150 <= uniques.size(); ++n)
        if (memcmp(uniques.data() + 4 + n * 0x150 + 2, "The Grandfather", 15) == 0)
            grandfather = int(n);
    assert(grandfather >= 0);
    auto add = [&](uint32_t id, const char* code, unsigned slot, unsigned x, unsigned y, unsigned width,
                   unsigned height) {
        Inspect::Data::Item item;
        item.id = id;
        item.code = code;
        item.name = slot ? "The Grandfather" : "Grand Charm of Fortune";
        item.slot = uint8_t(slot);
        item.x = uint8_t(x);
        item.y = uint8_t(y);
        item.width = uint8_t(width);
        item.height = uint8_t(height);
        item.flags = 0x10;
        item.level = 115;
        item.quality = slot ? 7 : 4;
        item.index = slot ? grandfather : -1;
        item.stats = {{0, 0, 20}, {80, 0, 7}};
        item.tooltip =
            "ÿc3+7% Better Chance of Getting Magic Items\n+20 to Strength\nÿc0Required Level: 62\nÿc4" +
            item.name;
        snapshot->items.push_back(item);
    };
    add(1, "7gd", 4, 0, 0, 2, 4);
    add(2, "7gd", 5, 0, 0, 2, 4);
    add(3, "7gd", 11, 0, 0, 2, 4);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 10; ++x)
            add(10 + y * 10 + x, "cm3", 0, x, y * 3, 1, 3);
    // Exact artwork diagnostic supplied by the user: upgraded Barrier Charm
    // keeps Barrier1's unique index while using the B03 base-item code.
    add(99,"B03",0,8,6,2,2);
    snapshot->items.back().quality=7; snapshot->items.back().index=670;
    snapshot->items.back().name="Barrier Charm 3";
    fixture = {true, false, 1, "Read-only equipment and backpack snapshot. Refresh to update.", snapshot};
    std::shared_ptr<const Inspect::Assets> assets;
    for (int n = 0; n < 30000 && !assets; ++n) {
        const auto candidate = Inspect::Art(fixture);
        if (candidate && candidate->revision == fixture.revision)
            assets = candidate;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(assets && assets->layout.columns == 10 && assets->layout.rows == 8 &&
           assets->layout.slots[1].x == 470 && assets->sprites[0]);
    if (assets->icons.size() != snapshot->items.size())
        std::cerr << "Artwork: " << assets->icons.size() << " / " << snapshot->items.size() << "; "
                  << assets->warning << "\n";
    assert(assets->icons.size() == snapshot->items.size());
    std::ofstream metadata(root / "textures.json");
    metadata << "{";
    bool first = true;
    for (size_t i = 0; i < assets->sprites.size(); ++i)
        if (auto sprite = assets->sprites[i]) {
            const auto& a = sprite->atlas;
            const auto filename = "texture-" + std::to_string(i) + ".pixels";
            std::ofstream pixels(root / filename, std::ios::binary);
            pixels.write(reinterpret_cast<const char*>(a.pixels.data()), a.pixels.size());
            if (!first)
                metadata << ",";
            first = false;
            metadata << "\"" << 100 + i << "\":{\"file\":\"" << filename << "\",\"width\":" << a.width
                     << ",\"height\":" << a.height << ",\"bc3\":" << (a.bc3 ? "true" : "false") << "}";
        }
    metadata << "}";
    metadata.close();
    Inspect::SetArtwork([](const std::shared_ptr<const Inspect::Assets>& a, size_t index) {
        LegacyTransmog::Skin::Image image;
        if (index < a->sprites.size())
            if (auto sprite = a->sprites[index]) {
                const auto& atlas = sprite->atlas;
                image = {100 + index,      atlas.width,  atlas.height,
                         atlas.frameWidth, atlas.frames, sprite->pitch};
            }
        return image;
    });
    assert(assets->icons.contains(99));
    ImGui::CreateContext();
    HudMenu::ApplySanctuaryPalette();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    const auto font =
        std::filesystem::path(__FILE__).parent_path().parent_path() / "d2rhud" / "fonts" / "0.otf";
    assert(io.Fonts->AddFontFromFileTTF(font.string().c_str(), 22));
    unsigned char* pixels = nullptr;
    int width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    io.Fonts->SetTexID(1);
    for (auto display : {ImVec2{1280, 720}, ImVec2{1920, 1080}, ImVec2{3440, 1440}, ImVec2{3840, 2160}}) {
        io.DisplaySize = display;
        for (int n = 0; n < 2; ++n) {
            ImGui::NewFrame();
            Inspect::Draw();
            ImGui::Render();
        }
        const auto window = ImGui::FindWindowByName("##InspectInventory");
        const auto placement = Inspect::Presentation::Placement(display.x, display.y, assets->layout.width,
                                                                assets->layout.height);
        assert(window && window->Pos.x == placement.x && window->Pos.y == 0 &&
               window->Pos.y + window->Size.y < display.y &&
               window->Pos.x + window->Size.x < display.x * .55f);
        Capture(root, "inspect-" + std::to_string(int(display.x)), pixels, width, height);
    }
    // Hover a charm, then exercise the read-only tooltip and alternate weapon set.
    io.DisplaySize = {1920, 1080};
    const float scale = .5f;
    io.AddMousePosEvent((assets->layout.grid.x + assets->layout.cell * .5f) * scale,
                        (assets->layout.grid.y + assets->layout.cell) * scale);
    for (int n = 0; n < 3; ++n) {
        ImGui::NewFrame();
        Inspect::Draw();
        ImGui::Render();
    }
    Capture(root, "inspect-charm-tooltip", pixels, width, height);
    snapshot->name = std::string(200, 'W');
    for (int n = 0; n < 2; ++n) {
        ImGui::NewFrame();
        Inspect::Draw();
        ImGui::Render();
    }
    Capture(root, "inspect-long-name", pixels, width, height);
    fixture.open = false;
    eyeFixtures = {{1, 15, 960, 350}, {1, 1, 960, 350}}; // Clustered custom summon and mercenary.
    opened = {UINT32_MAX, UINT32_MAX};
    io.AddMousePosEvent(960, 316);
    auto frame = [&] {
        ImGui::NewFrame();
        Inspect::DrawEyes();
        ImGui::Render();
    };
    for (int n = 0; n < 3; ++n)
        frame();
    auto mercEye = ImGui::FindWindowByName("##InspectEye1_1");
    auto soulEye = ImGui::FindWindowByName("##InspectEye1_15");
    assert(mercEye && soulEye && mercEye->Pos.y - soulEye->Pos.y >= 32);
    assert(io.WantCaptureMouse);
    Capture(root, "inspect-eye-icons", pixels, width, height);
    io.AddMouseButtonEvent(0, true);
    frame();
    io.AddMouseButtonEvent(0, false);
    frame();
    assert((opened == std::pair{1u, 15u})); // Real ImGui hit testing opens the selected unit.
    io.AddMousePosEvent(400, 350);
    for (int n = 0; n < 2; ++n)
        frame();
    assert(!io.WantCaptureMouse); // World movement is not captured outside an icon.
    appearance.labels = true;
    appearance.textSize = 28;
    for (int n = 0; n < 3; ++n)
        frame();
    soulEye = ImGui::FindWindowByName("##InspectEye1_15");
    assert(soulEye && soulEye->Size.x > 200 && soulEye->Size.y >= 32);
    Capture(root, "inspect-eye-labels", pixels, width, height);
    io.AddMousePosEvent(soulEye->Pos.x + soulEye->Size.x - 10, soulEye->Pos.y + soulEye->Size.y * .5f);
    for (int n = 0; n < 2; ++n)
        frame();
    opened = {UINT32_MAX, UINT32_MAX};
    io.AddMouseButtonEvent(0, true);
    frame();
    io.AddMouseButtonEvent(0, false);
    frame();
    assert((opened == std::pair{1u, 15u})); // The readable label is clickable too.
    appearance.textSize = 40;
    eyeFixtures = {{1, 15, 1880, 350}};
    for (int n = 0; n < 3; ++n)
        frame();
    soulEye = ImGui::FindWindowByName("##InspectEye1_15");
    assert(soulEye->Pos.x > 0 && soulEye->Pos.x + soulEye->Size.x <= 1920);
    Capture(root, "inspect-eye-labels-large", pixels, width, height);
    appearance.labels = false;
    for (int n = 0; n < 2; ++n)
        frame();
    assert(ImGui::FindWindowByName("##InspectEye1_15")->Size.x == 32);
    eyeFixtures={{1,1,960,33},{1,15,960,33}};
    for(int n=0;n<3;++n)frame();
    mercEye=ImGui::FindWindowByName("##InspectEye1_1");
    soulEye=ImGui::FindWindowByName("##InspectEye1_15");
    assert(mercEye->Active && soulEye->Active && mercEye->Pos.y>=0 && soulEye->Pos.y>mercEye->Pos.y);
    eyeFixtures.clear();
    frame();
    frame();
    assert(!ImGui::FindWindowByName("##InspectEye1_15")->Active);
    ImGui::DestroyContext();
    std::cout << "PASS: actual RMD-MP inventory layout/background, 8-row backpack and HD unique/charm "
                 "assets; shared icons; left panel bounds at 720p/1080p/ultrawide/4K; read-only charm "
                 "tooltip and long-name clipping; eye icon overlap, click selection and world mouse "
                 "pass-through. No game files changed.\n";
}
