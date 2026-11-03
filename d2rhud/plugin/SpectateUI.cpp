#include "D2RHUD/HudConfigStore.h"
#include "HudText.h"
#include "SettingsUI.h"
#include "Spectate.h"
#include "SpectateTransport.h"
#include "WindowLayout.h"
#include <cfloat>
#include <cmath>

namespace Spectate {
namespace {
std::string settingsError;
bool SaveSettings(const Settings& settings) {
    const auto result = HudUserConfig::Update("Spectate", [&](HudConfig::Json& section) {
        section["enabled"] = settings.enabled;
        section["allowSpectators"] = settings.sharing;
        section["framesPerSecond"] = settings.fps;
        section["width"] = settings.width;
        section["quality"] = settings.quality;
        section["bandwidthMbps"] = settings.bandwidthMbps;
        section["videoPort"] = settings.videoPort;
    });
    settingsError = result ? std::string{} : result.error;
    return bool(result);
}
void Eye(ImDrawList* draw, ImVec2 center, float radius, ImU32 color) {
    constexpr unsigned count = 24;
    ImVec2 points[count];
    for (unsigned n = 0; n < count; ++n) {
        const float angle = n * 6.2831853f / count;
        points[n] = {center.x + std::cos(angle) * radius, center.y + std::sin(angle) * radius * .58f};
    }
    draw->AddPolyline(points, count, color, ImDrawFlags_Closed, 1.7f);
    draw->AddCircleFilled(center, radius * .25f, color, 16);
}
void Party(const View& view) {
    const auto display = ImGui::GetIO().DisplaySize;
    for (const auto& eye : Native::PartyEyes(display.x, display.y, view.peers)) {
        ImGui::SetNextWindowPos({eye.x, eye.y});
        ImGui::SetNextWindowSize({eye.size, eye.size});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, {0, 0});
        const auto id = "##SpectateParty" + std::to_string(eye.player);
        ImGui::Begin(id.c_str(), nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav);
        const bool clicked = ImGui::InvisibleButton("Watch", {eye.size, eye.size});
        const bool hovered = ImGui::IsItemHovered();
        const bool active = view.open && view.target == eye.player;
        auto* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled({eye.x, eye.y}, {eye.x + eye.size, eye.y + eye.size},
                            hovered ? IM_COL32(75, 18, 22, 245) : IM_COL32(15, 15, 18, 230), 3);
        const auto color = !eye.available ? IM_COL32(105, 105, 110, 255)
                           : active       ? IM_COL32(125, 225, 165, 255)
                                          : IM_COL32(225, 195, 115, 255);
        draw->AddRect({eye.x, eye.y}, {eye.x + eye.size, eye.y + eye.size}, color, 3);
        Eye(draw, {eye.x + eye.size * .5f, eye.y + eye.size * .5f}, eye.size * .34f, color);
        if (hovered) {
            ImGui::BeginTooltip();
            HudText::Text("%s: %s", HudLocale::Tr("Spectate"), eye.name.c_str());
            if (!eye.available)
                HudText::TextUnformatted("This player has not enabled gameplay sharing.");
            ImGui::EndTooltip();
        }
        if (clicked && eye.available)
            Open(eye.player);
        AddInputRegion(eye.x, eye.y, eye.size, eye.size);
        ImGui::End();
        ImGui::PopStyleVar(3);
    }
}
} // namespace
void LoadSettings() {
    HudConfig::Json section;
    const auto result = HudUserConfig::Read("Spectate", section);
    if (!result) {
        settingsError = result.error;
        return;
    }
    Settings settings;
    auto boolean = [&](const char* key, bool fallback) {
        const auto at = section.find(key);
        return at != section.end() && at->is_boolean() ? at->get<bool>() : fallback;
    };
    auto integer = [&](const char* key, int fallback, int minimum, int maximum) {
        const auto at = section.find(key);
        if (at == section.end() || !at->is_number_integer())
            return fallback;
        if (at->is_number_unsigned())
            return int((std::min)(uint64_t(maximum), (std::max)(uint64_t(minimum), at->get<uint64_t>())));
        return int(std::clamp(at->get<int64_t>(), int64_t(minimum), int64_t(maximum)));
    };
    if (section.is_object()) {
        settings.enabled = boolean("enabled", true);
        settings.sharing = boolean("allowSpectators", true);
        settings.fps = integer("framesPerSecond", 6, Data::MinFps, Data::MaxFps);
        settings.width = integer("width", 640, Data::MinWidth, Data::MaxWidth);
        settings.quality = integer("quality", 55, Data::MinQuality, Data::MaxQuality);
        settings.bandwidthMbps = integer("bandwidthMbps", 16, Data::MinBandwidth, Data::MaxBandwidth);
        settings.videoPort = integer("videoPort", 6113, 1024, 65535);
    }
    Configure(settings);
}
void DrawOptions() {
    auto settings = Configuration();
    bool changed = HudText::Checkbox("Spectate players (TCP/IP)", &settings.enabled);
    if (settings.enabled) {
        ImGui::Indent();
        changed |= HudText::Checkbox("Allow others to spectate me", &settings.sharing);
        HudSettings::Note("Click the eye beside a player in the party menu. Both players and the host need a "
                          "compatible HUD.");
        if (const auto error = Native::CompatibilityError())
            HudSettings::Note(error);
        else
            HudSettings::Note(Current().ready ? "Compatible TCP/IP host connected."
                                              : "Waiting for a compatible TCP/IP host.");
        if (const auto error = Stream::Host().Error(); !error.empty())
            HudSettings::Note(error.c_str());
        if (const auto error = Stream::Local().Current().error; !error.empty())
            HudSettings::Note(error.c_str());
        if (const auto error = Current().videoError; !error.empty())
            HudSettings::Note(error.c_str());
        ImGui::SetNextItemWidth(190);
        changed |= HudText::SliderInt("Preferred viewing frame rate", &settings.fps, Data::MinFps,
                                      Data::MaxFps, "%d fps");
        ImGui::SetNextItemWidth(190);
        changed |= HudText::SliderInt("Preferred viewing width", &settings.width, Data::MinWidth,
                                      Data::MaxWidth, "%d px");
        ImGui::SetNextItemWidth(190);
        changed |= HudText::SliderInt("Preferred viewing quality", &settings.quality, Data::MinQuality,
                                      Data::MaxQuality, "%d%%");
        ImGui::SetNextItemWidth(190);
        changed |= HudText::SliderInt("Spectate bandwidth limit", &settings.bandwidthMbps, Data::MinBandwidth,
                                      Data::MaxBandwidth, "%d Mbps");
        HudSettings::Note("Viewing preferences are sent to the player you watch, including changes made "
                          "while spectating. Frames keep their source aspect ratio and never enlarge it. "
                          "H.264 compression reuses image detail between frames.");
        HudSettings::Note(
            "Frame rate is a target, limited by capture, encoding and the video connection. "
            "Your bandwidth limit controls your uploads and, when hosting, the shared video relay. "
            "No capture runs without a spectator.");
        ImGui::SetNextItemWidth(190);
        changed |= HudText::InputInt("Host video TCP port", &settings.videoPort);
        HudSettings::Note(
            "The host's video port (default 6113) must be reachable on your LAN or VPN. "
            "Internet hosts may need to forward this TCP port. All players need this HUD version.");
        ImGui::Unindent();
    }
    if (changed) {
        Configure(settings);
        SaveSettings(Configuration());
    }
    if (!settingsError.empty()) {
        HudText::TextWrapped("%s", settingsError.c_str());
        if (HudText::Button("Retry saving Spectate settings"))
            SaveSettings(Configuration());
    }
}
void Draw(bool showParty) {
    SetInputRegion(0, 0, 0, 0);
    const auto settings = Configuration();
    auto view = Current();
    if (!settings.enabled || !view.inGame)
        return;
    if (showParty && !view.fullscreen)
        Party(view);
    if (!view.open)
        return;
    const auto display = ImGui::GetIO().DisplaySize;
    const float scale = std::clamp(display.y / 1080.f, .7f, 2.f);
    bool open = true;
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
    if (view.fullscreen) {
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize(display);
        flags |= ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
    } else
        HudWindowLayout::Prepare({640 * scale, 420 * scale});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(8, 8, 11, 255));
    ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(175, 143, 83, 255));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, IM_COL32(67, 17, 22, 255));
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(33, 32, 38, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(95, 28, 34, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(125, 38, 42, 255));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
    const auto title = std::string(HudLocale::Tr("Spectating")) + ": " + view.name +
                       (view.fullscreen ? "###SpectateFullscreen" : "###SpectateView");
    const bool visible = ImGui::Begin(title.c_str(), &open, flags);
    ImGui::SetWindowFontScale((std::max)(1.f, scale));
    if (visible) {
        if (view.fullscreen) {
            HudText::TextColored({.88f, .77f, .47f, 1}, "%s: %s", HudLocale::Tr("Spectating"),
                                 view.name.c_str());
            ImGui::SameLine();
        }
        if (HudText::SmallButton(view.fullscreen ? "Picture in picture" : "Full screen"))
            Fullscreen(!view.fullscreen);
        const float nextX = ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x;
        const float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", HudLocale::Tr("Your character remains in the game while you spectate."));
        if (nextX + HudText::CalcTextSize(HudLocale::Tr("Stop spectating")).x +
                2 * ImGui::GetStyle().FramePadding.x <=
            right)
            ImGui::SameLine();
        if (HudText::SmallButton("Stop spectating")) {
            Close();
            open = false;
        }
        if (view.fullscreen) {
            if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x +
                    HudText::CalcTextSize(HudLocale::Tr("Esc to exit")).x <=
                right)
                ImGui::SameLine();
            HudText::TextDisabled("Esc to exit");
        }
        const auto image = Video::DisplayImage();
        const auto texture = Video::Texture();
        if (image && texture)
            HudText::TextDisabled("%d x %d | %.1f fps displayed", image->width, image->height,
                                  view.receivedFps);
        ImGui::Separator();
        const auto pos = ImGui::GetCursorScreenPos(), size = ImGui::GetContentRegionAvail();
        auto* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(pos, {pos.x + size.x, pos.y + size.y}, IM_COL32(0, 0, 0, 255));
        const bool fresh = image && texture && view.receivedAt && GetTickCount64() - view.receivedAt < 2500;
        if (fresh) {
            const float fit = (std::min)(size.x / image->width, size.y / image->height);
            const ImVec2 extent{image->width * fit, image->height * fit},
                start{pos.x + (size.x - extent.x) * .5f, pos.y + (size.y - extent.y) * .5f};
            draw->AddImage(ImTextureID(texture), start, {start.x + extent.x, start.y + extent.y});
        } else {
            const auto text =
                HudLocale::Translate(view.status.empty() ? "Waiting for live gameplay..." : view.status);
            auto font = HudText::FontFor(ImGui::GetFont(), text);
            const auto fontSize = ImGui::GetFontSize();
            const auto wrap = (std::max)(16.f, size.x - 32);
            const auto textSize = font->CalcTextSizeA(fontSize, FLT_MAX, wrap, text.c_str());
            draw->AddText(font, fontSize,
                          {pos.x + (size.x - textSize.x) * .5f, pos.y + (size.y - textSize.y) * .5f},
                          IM_COL32(225, 210, 180, 255), text.c_str(), nullptr, wrap);
        }
        const auto windowPos = ImGui::GetWindowPos(), windowSize = ImGui::GetWindowSize();
        AddInputRegion(windowPos.x, windowPos.y, windowSize.x, windowSize.y);
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(6);
    if (!open)
        Close();
}
} // namespace Spectate
