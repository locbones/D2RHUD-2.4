#define NOMINMAX
#include "../d2rhud/plugin/SpectateUI.cpp"
#include <cassert>
#include <fstream>
#include <imgui_internal.h>
#include <iostream>
namespace {
Spectate::View fixture;
Spectate::Settings configuration;
std::vector<Spectate::Native::Eye> eyes;
std::shared_ptr<Spectate::Image> image;
unsigned regions = 0, opened = 0;
} // namespace
const char* Spectate::Native::CompatibilityError() { return nullptr; }
Spectate::Settings Spectate::Configuration() { return configuration; }
void Spectate::Configure(Settings settings) { configuration = settings; }
Spectate::View Spectate::Current() { return fixture; }
void Spectate::SetInputRegion(float, float, float, float) { regions = 0; }
void Spectate::AddInputRegion(float, float, float, float) { ++regions; }
bool Spectate::Open(uint32_t player) {
    ++opened;
    fixture.open = true;
    fixture.target = player;
    return true;
}
void Spectate::Close() { fixture.open = fixture.fullscreen = false; }
void Spectate::Fullscreen(bool value) { fixture.fullscreen = value; }
std::vector<Spectate::Native::Eye> Spectate::Native::PartyEyes(float, float, const std::vector<Peer>&) {
    return eyes;
}
uint64_t Spectate::Video::Texture() { return 100; }
std::shared_ptr<const Spectate::Image> Spectate::Video::DisplayImage() { return image; }
void Capture(const std::filesystem::path& root, const std::string& name, unsigned char* atlas, int width,
             int height) {
    const auto* data = ImGui::GetDrawData();
    unsigned commands = 0;
    for (auto* list : data->CmdLists)
        commands += list->CmdBuffer.Size;
    const uint32_t header[]{
        uint32_t(data->DisplaySize.x), uint32_t(data->DisplaySize.y), uint32_t(width), uint32_t(height),
        uint32_t(data->TotalVtxCount), uint32_t(data->TotalIdxCount), commands};
    std::ofstream out(root / (name + ".bin"), std::ios::binary);
    out.write(reinterpret_cast<const char*>(header), sizeof(header));
    for (auto* list : data->CmdLists)
        out.write(reinterpret_cast<const char*>(list->VtxBuffer.Data),
                  size_t(list->VtxBuffer.Size) * sizeof(ImDrawVert));
    uint32_t vertexOffset = 0;
    for (auto* list : data->CmdLists) {
        for (auto index : list->IdxBuffer) {
            const uint32_t value = index + vertexOffset;
            out.write(reinterpret_cast<const char*>(&value), 4);
        }
        vertexOffset += list->VtxBuffer.Size;
    }
    uint32_t indexOffset = 0;
    for (auto* list : data->CmdLists) {
        for (const auto& cmd : list->CmdBuffer) {
            const uint32_t values[]{indexOffset + cmd.IdxOffset, cmd.ElemCount};
            out.write(reinterpret_cast<const char*>(values), 8);
            out.write(reinterpret_cast<const char*>(&cmd.ClipRect), 16);
            const uint64_t texture = cmd.GetTexID();
            out.write(reinterpret_cast<const char*>(&texture), 8);
        }
        indexOffset += list->IdxBuffer.Size;
    }
    std::ofstream pixels(root / "atlas.rgba", std::ios::binary);
    pixels.write(reinterpret_cast<const char*>(atlas), size_t(width) * height * 4);
}
int main(int argc, char** argv) {
    using namespace Spectate;
    assert(argc == 3);
    const auto root = std::filesystem::absolute(argv[1]);
    std::filesystem::create_directories(root);
    const auto userRoot = root / L"isolated-preferences";
    std::filesystem::create_directories(userRoot);
    SetEnvironmentVariableW(L"LOCALAPPDATA", userRoot.c_str());
    HudConfig::Json doc = {{"existing", "preserve me"}};
    assert(HudUserConfig::Update("Spectate", [&](auto& section) { section = HudConfig::Json::object(); }));
    LoadSettings();
    assert(configuration.enabled &&
           configuration.sharing); // New profiles allow incoming viewing immediately.
    assert(HudUserConfig::Update("Spectate", [&](auto& section) {
        section["enabled"] = false;
        section["allowSpectators"] = false;
    }));
    LoadSettings();
    assert(!configuration.enabled && !configuration.sharing); // Saved opt-outs remain respected.
    assert(HudUserConfig::Update("Spectate", [&](auto& section) {
        section = {{"enabled", true}, {"allowSpectators", false}, {"framesPerSecond", 9999999999999ULL},
                   {"width", -8},     {"quality", 100},           {"unrelated", 42}};
    }));
    LoadSettings();
    assert(configuration.enabled && !configuration.sharing && configuration.fps == 60 &&
           configuration.width == 320 && configuration.quality == 100);
    assert(configuration.bandwidthMbps == 16);
    assert(HudUserConfig::Update("Spectate", [&](auto& section) {
        section["framesPerSecond"] = 60;
        section["width"] = 3840;
        section["quality"] = 100;
        section["bandwidthMbps"] = 128;
    }));
    LoadSettings();
    assert(configuration.fps == 60 && configuration.width == 3840 && configuration.quality == 100 &&
           configuration.bandwidthMbps == 128);
    configuration = {true, true, 6, 640, 55};
    assert(SaveSettings(configuration));
    assert(HudUserConfig::Read("Spectate", doc));
    assert(doc["unrelated"] == 42);
    image = std::make_shared<Image>();
    image->width = 640;
    image->height = 360;
    image->rgba.resize(640 * 360 * 4, 255);
    for (unsigned y = 0; y < 360; ++y)
        for (unsigned x = 0; x < 640; ++x) {
            auto* p = image->rgba.data() + (y * 640 + x) * 4;
            p[0] = uint8_t(x * 255 / 640);
            p[1] = uint8_t(y * 255 / 360);
            p[2] = 33;
        }
    {
        std::ofstream pixels(root / "video.pixels", std::ios::binary);
        pixels.write(reinterpret_cast<const char*>(image->rgba.data()), image->rgba.size());
    }
    {
        std::ofstream textures(root / "textures.json");
        textures << "{\"100\":{\"file\":\"video.pixels\",\"width\":640,\"height\":360,\"bc3\":false}}";
    }
    for (auto display : {ImVec2{1280, 720}, ImVec2{1920, 1080}, ImVec2{3440, 1440}, ImVec2{3840, 2160}}) {
        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = display;
        io.DeltaTime = 1.f / 60;
        assert(io.Fonts->AddFontFromFileTTF(argv[2], 18));
        unsigned char* pixels = nullptr;
        int width = 0, height = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        io.Fonts->SetTexID(1);
        fixture = {};
        fixture.inGame = fixture.ready = true;
        fixture.open = true;
        fixture.name = "Fighting Barbarian";
        fixture.target = 1;
        fixture.request = 1;
        fixture.receivedAt = GetTickCount64();
        eyes = {{1, 80, 150, 24, true, "Fighting Barbarian"}};
        auto frame = [&] {
            ImGui::NewFrame();
            Draw();
            ImGui::Render();
        };
        frame();
        frame();
        const auto eyeWindow = ImGui::FindWindowByName("##SpectateParty1");
        assert(eyeWindow && eyeWindow->Size.x == eyes[0].size && eyeWindow->Size.y == eyes[0].size);
        auto window = ImGui::FindWindowByName("###SpectateView");
        assert(window && window->Active && regions == 2 && !(window->Flags & ImGuiWindowFlags_NoInputs));
        assert(window->Pos.x >= 0 && window->Pos.y >= 0 && window->Pos.x + window->Size.x <= display.x &&
               window->Pos.y + window->Size.y <= display.y);
        bool video = false;
        for (auto* list : ImGui::GetDrawData()->CmdLists)
            for (const auto& command : list->CmdBuffer)
                video |= command.GetTexID() == 100;
        assert(video);
        Capture(root, "spectate-pip-" + std::to_string(int(display.x)), pixels, width, height);
        // Native ImGui move/resize and the saved window identity retain layout.
        ImGui::SetWindowPos(window, {30, 50});
        ImGui::SetWindowSize(window, {500, 350});
        frame();
        assert(window->Pos.x == 30 && window->Size.x == 500);
        Fullscreen(true);
        frame();
        frame();
        auto full = ImGui::FindWindowByName("###SpectateFullscreen");
        assert(full && full->Active && full->Pos.x == 0 && full->Pos.y == 0 && full->Size.x == display.x &&
               full->Size.y == display.y && regions == 1);
        Capture(root, "spectate-full-" + std::to_string(int(display.x)), pixels, width, height);
        Fullscreen(false);
        frame();
        assert(window->Pos.x == 30 && window->Pos.y == 50 && window->Size.x == 500);
        size_t length = 0;
        const std::string ini = ImGui::SaveIniSettingsToMemory(&length);
        assert(ini.find("SpectateView]") != ini.npos && ini.find("SpectateFullscreen") == ini.npos);
        fixture.receivedAt = GetTickCount64() - 3000;
        fixture.status = "Waiting for the player to finish loading. This message should wrap inside a narrow "
                         "picture-in-picture window.";
        frame();
        video = false;
        for (auto* list : ImGui::GetDrawData()->CmdLists)
            for (const auto& command : list->CmdBuffer)
                video |= command.GetTexID() == 100;
        assert(!video);
        Close();
        frame();
        opened = 0;
        io.AddMousePosEvent(92, 162);
        frame();
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
        assert(opened == 1 && fixture.target == 1);
        Close();
        configuration.enabled = false;
        frame();
        assert(!regions);
        configuration.enabled = true;
        ImGui::NewFrame();
        ImGui::SetNextWindowSize(display);
        ImGui::SetNextWindowPos({0, 0});
        ImGui::Begin("Spectate options");
        DrawOptions();
        ImGui::End();
        ImGui::Render();
        ImGui::DestroyContext();
    }
    std::cout << "PASS: actual Spectate UI at 720p/1080p/ultrawide/4K, party clicks, full-screen/PiP, "
                 "resize/layout persistence, stale-video masking and consolidated settings\n";
}
