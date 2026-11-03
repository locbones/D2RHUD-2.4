#pragma once
#include "D2RHUD/FrameContext.h"
#include "InspectData.h"
#include "StackSplitSkin.h"
#include <filesystem>
#include <map>
#include <memory>
class D2GameStrc;
class D2UnitStrc;
namespace Inspect {
void SetEnabled(bool enabled);
bool Enabled();
struct Appearance {
    bool labels = false;
    int lift = 96, textSize = 22; // Pixels at 1080p, scaled with the game window.
};
void SetAppearance(Appearance appearance);
Appearance GetAppearance();
bool IsOpen();
void Close();
bool Open(uint32_t type, uint32_t id);
void Refresh();
void ClientTick(const HudFrame::Context& context, uint64_t session);
void Receive(const uint8_t* bytes, size_t size);
bool Command(D2GameStrc* game, D2UnitStrc* player, const uint8_t* bytes, size_t size);
void Tick(D2GameStrc* game);
void Destroyed(D2GameStrc* game);
void SetAvailable(bool available);
void SetNameProvider(void (*name)(D2UnitStrc*, char*));
using TextProvider = std::string (*)(D2UnitStrc*, D2UnitStrc*, D2UnitStrc*);
void SetTextProvider(TextProvider provider);
void SetReadGuard(void (*guard)(void (*)(void*), void*));
bool InstallSelection();
void UninstallSelection();
const char* SelectionStatus();
const char* WorldStatus();
struct View {
    bool open = false, busy = false;
    uint64_t revision = 0;
    std::string status;
    std::shared_ptr<const Data::Snapshot> snapshot;
};
View Current();
void Draw();
void DrawEyes();
struct Rect {
    float x = 0, y = 0, width = 0, height = 0;
};
struct Layout {
    float width = 1162, height = 1600, cell = 98;
    int columns = 10, rows = 8;
    Rect grid{86, 773, 980, 784};
    Rect close{8, 21, 48, 48};
    std::array<Rect, 13> slots{};
};
struct Assets {
    Layout layout;
    std::map<uint32_t, float> iconScales; // Low-resolution art retains native inventory size.
    std::string artworkReport;
    std::map<uint32_t, size_t> icons; // item GUID -> immutable sprite index
    std::map<std::string, size_t> chrome;
    std::vector<std::shared_ptr<const LegacyTransmog::Skin::Sprite>> sprites;
    uint64_t revision = 0;
    std::string warning;
};
using Artwork = LegacyTransmog::Skin::Image (*)(const std::shared_ptr<const Assets>&, size_t);
void SetArtwork(Artwork artwork);
void SetFont(LegacyTransmog::Skin::Font font);
void ConfigureAssets(const std::filesystem::path& install, const std::string& mod);
std::shared_ptr<const Assets> Art(const View& view);
} // namespace Inspect
