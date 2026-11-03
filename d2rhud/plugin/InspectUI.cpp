#include "HudText.h"
#include "Inspect.h"
#include "InspectNative.h"
#include "InspectPresentation.h"
#include <atomic>
#include <cfloat>
#include <cmath>
#include <imgui.h>

namespace Inspect {
namespace {
std::atomic<Artwork> artwork{nullptr};
std::atomic<LegacyTransmog::Skin::Font> fonts{nullptr};
ImU32 Color(unsigned index) {
    constexpr ImU32 colors[]{
        IM_COL32(255, 255, 255, 255), IM_COL32(255, 70, 70, 255),   IM_COL32(0, 255, 0, 255),
        IM_COL32(105, 105, 255, 255), IM_COL32(199, 179, 119, 255), IM_COL32(105, 105, 105, 255),
        IM_COL32(0, 0, 0, 255),       IM_COL32(208, 192, 159, 255), IM_COL32(255, 168, 0, 255),
        IM_COL32(255, 255, 0, 255),   IM_COL32(0, 100, 0, 255),     IM_COL32(175, 80, 255, 255),
        IM_COL32(0, 200, 0, 255),     IM_COL32(255, 255, 255, 255), IM_COL32(255, 155, 0, 255),
        IM_COL32(255, 100, 175, 255)};
    return colors[index < std::size(colors) ? index : 0];
}
ImFont* Font(float size) {
    if (auto provider = fonts.load())
        if (auto font = provider(size))
            return font;
    return ImGui::GetFont();
}
void Text(ImVec2 a, ImVec2 b, const std::string& text, float size, ImU32 color = Color(4)) {
    auto font = Font(size);
    const auto measure = font->CalcTextSizeA(size, b.x - a.x, b.x - a.x, text.c_str());
    auto* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(a, b, true);
    draw->AddText(font, size, {a.x + (b.x - a.x - measure.x) * .5f, a.y + (b.y - a.y - measure.y) * .5f},
                  color, text.c_str(), nullptr, b.x - a.x);
    draw->PopClipRect();
}
void Image(const LegacyTransmog::Skin::Image& image, ImVec2 a, ImVec2 b, int frame = 0) {
    if (image.texture && image.width > 0 && image.height > 0) {
        frame = std::clamp(frame, 0, (std::max)(0, image.frames - 1));
        const float u = float(frame * image.pitch) / image.width;
        ImGui::GetWindowDrawList()->AddImage(ImTextureID(image.texture), a, b, {u, 0},
                                             {u + float(image.frameWidth) / image.width, 1});
    }
}
void Tooltip(const Data::Item& item) {
    auto lines = Presentation::TooltipLines(item.tooltip);
    if (lines.empty())
        lines = {{{item.name, item.quality == 7   ? 4u
                              : item.quality == 5 ? 2u
                              : item.quality == 4 ? 3u
                                                  : 0u}},
                 {{HudLocale::Tr("Item description is unavailable on the host."), 5}}};
    const auto display = ImGui::GetIO().DisplaySize;
    float size = std::clamp(22.f * display.y / 1080.f, 14.f, 44.f);
    auto font = Font(size);
    float width = 0;
    for (const auto& line : lines) {
        float w = 0;
        for (const auto& run : line)
            w += font->CalcTextSizeA(size, FLT_MAX, 0, run.text.c_str()).x;
        width = (std::max)(width, w);
    }
    // Match the game's centered, colored rows without a HUD settings frame.
    const float fit = (std::min)(
        {1.f, (display.x - 32) / (width + 24), (display.y - 32) / (lines.size() * size * 1.25f + 24)});
    size *= fit;
    font = Font(size);
    width *= fit;
    const float pad = 10.f * display.y / 1080.f, step = size * 1.25f;
    ImVec2 extent{width + 2 * pad, lines.size() * step + 2 * pad};
    const auto mouse = ImGui::GetIO().MousePos;
    ImVec2 pos{std::clamp(mouse.x - extent.x * .5f, 0.f, (std::max)(0.f, display.x - extent.x)),
               mouse.y + 24};
    if (pos.y + extent.y > display.y)
        pos.y = (std::max)(0.f, mouse.y - extent.y - 10);
    ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(extent, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(0, 0, 0, 245));
    ImGui::BeginTooltip();
    auto* draw = ImGui::GetWindowDrawList();
    float y = pos.y + pad;
    for (const auto& line : lines) {
        float w = 0;
        for (const auto& run : line)
            w += font->CalcTextSizeA(size, FLT_MAX, 0, run.text.c_str()).x;
        float x = pos.x + (extent.x - w) * .5f;
        for (const auto& run : line) {
            draw->AddText(font, size, {x, y}, Color(run.color), run.text.c_str());
            x += font->CalcTextSizeA(size, FLT_MAX, 0, run.text.c_str()).x;
        }
        y += step;
    }
    ImGui::EndTooltip();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
}
} // namespace
void SetArtwork(Artwork value) { artwork.store(value); }
void SetFont(LegacyTransmog::Skin::Font value) { fonts.store(value); }
void DrawEyes() {
    if (!Enabled() || ImGui::GetIO().WantTextInput)
        return;
    const auto display = ImGui::GetIO().DisplaySize;
    if (display.x < 160 || display.y < 180)
        return;
    const auto current = Current();
    const auto appearance = GetAppearance();
    const auto panel = Presentation::Placement(display.x, display.y, 1132, 1730);
    const float size = std::clamp(display.y / 1080.f * 32.f, 26.f, 56.f);
    const float textSize = std::clamp(appearance.textSize * display.y / 1080.f, 16.f, 80.f);
    auto font = Font(textSize);
    const float gap = (std::max)(6.f, 8.f * display.y / 1080.f);
    auto eyes = Native::Eyes(display.x, display.y);
    std::sort(eyes.begin(), eyes.end(),
              [](const auto& a, const auto& b) { return std::pair{a.type, a.id} < std::pair{b.type, b.id}; });
    std::vector<ImVec2> placed;
    placed.reserve(eyes.size());
    for (const auto& eye : eyes) {
        const auto caption = HudLocale::Tr(eye.type ? "Inspect companion" : "Inspect player");
        const auto textExtent =
            appearance.labels ? font->CalcTextSizeA(textSize, FLT_MAX, 0, caption) : ImVec2{};
        const float labelWidth = appearance.labels ? textExtent.x + gap * 2 : 0;
        const float buttonWidth = size + labelWidth;
        const float buttonHeight = (std::max)(size, textExtent.y + gap);
        ImVec2 center{eye.x, eye.y};
        const float stackStep = buttonHeight + 2;
        const bool stackDown = center.y < size + stackStep * 8;
        // Separate clustered companions without losing their world anchors.
        for (unsigned pass = 0; pass < 8; ++pass) {
            const bool overlap = std::any_of(placed.begin(), placed.end(), [&](ImVec2 other) {
                return std::abs(center.x - other.x) < buttonWidth &&
                       std::abs(center.y - other.y) < buttonHeight;
            });
            if (!overlap)
                break;
            center.y += stackDown ? stackStep : -stackStep;
        }
        if (center.x < size || center.x > display.x - size || center.y < size || center.y > display.y - size)
            continue;
        if (current.open && center.x - size < panel.x + panel.width && center.x + size > panel.x &&
            center.y - size < panel.height + size)
            continue;
        if (std::any_of(placed.begin(), placed.end(), [&](ImVec2 other) {
                return std::abs(center.x - other.x) < buttonWidth &&
                       std::abs(center.y - other.y) < buttonHeight;
            }))
            continue;
        placed.push_back(center);
        const bool labelLeft = appearance.labels && center.x + size * .5f + labelWidth > display.x - gap;
        const ImVec2 origin{center.x - size * .5f - (labelLeft ? labelWidth : 0),
                            center.y - buttonHeight * .5f};
        const auto label = "##InspectEye" + std::to_string(eye.type) + "_" + std::to_string(eye.id);
        ImGui::SetNextWindowPos(origin, ImGuiCond_Always);
        ImGui::SetNextWindowSize({buttonWidth, buttonHeight}, ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
        ImGui::Begin(label.c_str(), nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground |
                         ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing);
        if (ImGui::InvisibleButton("eye", {buttonWidth, buttonHeight}))
            Open(eye.type, eye.id);
        const bool hover = ImGui::IsItemHovered();
        auto* draw = ImGui::GetWindowDrawList();
        const auto gold = hover ? IM_COL32(255, 225, 135, 255) : Color(4);
        if (appearance.labels) {
            const float x = labelLeft ? origin.x + gap : center.x + size * .5f + gap;
            const ImVec2 textPos{x, center.y - textExtent.y * .5f};
            draw->AddRectFilled({x - gap * .5f, origin.y},
                                {x + textExtent.x + gap * .5f, origin.y + buttonHeight},
                                IM_COL32(10, 6, 5, 230), 3.f);
            draw->AddText(font, textSize, textPos, gold, caption);
        }
        draw->AddCircleFilled(center, size * .47f, IM_COL32(15, 8, 7, 230));
        draw->AddCircle(center, size * .47f, hover ? gold : IM_COL32(110, 40, 35, 255), 24, 1.5f);
        // An almond-shaped eye stays legible at small sizes without a font glyph.
        draw->PathLineTo({center.x - size * .32f, center.y});
        draw->PathBezierCubicCurveTo({center.x - size * .13f, center.y - size * .29f},
                                     {center.x + size * .13f, center.y - size * .29f},
                                     {center.x + size * .32f, center.y});
        draw->PathBezierCubicCurveTo({center.x + size * .13f, center.y + size * .29f},
                                     {center.x - size * .13f, center.y + size * .29f},
                                     {center.x - size * .32f, center.y});
        draw->PathStroke(gold, ImDrawFlags_Closed, 1.7f);
        draw->AddCircleFilled(center, size * .10f, gold, 12);
        if (hover) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        }
        ImGui::End();
        ImGui::PopStyleVar(2);
    }
}
void Draw() {
    const auto view = Current();
    if (!view.open || !Enabled())
        return;
    const auto assets = Art(view);
    const bool artReady = assets && assets->revision == view.revision;
    const auto display = ImGui::GetIO().DisplaySize;
    if (display.x < 160 || display.y < 180)
        return;
    const Layout layout = assets ? assets->layout : Layout{};
    const auto panel = Presentation::Placement(display.x, display.y, layout.width, layout.height);
    const float scale = panel.scale, w = panel.width, h = panel.height;
    const float textSize = std::clamp(22.f * display.y / 1080.f, 14.f, 44.f);
    const ImVec2 origin{panel.x, panel.y};
    auto point = [&](float x, float y) { return ImVec2{origin.x + x * scale, origin.y + y * scale}; };
    static uint64_t last = 0;
    static bool alternate = false;
    if (last != view.revision) {
        last = view.revision;
        alternate = false;
    }
    ImGui::SetNextWindowPos(origin, ImGuiCond_Always);
    ImGui::SetNextWindowSize({w, h + textSize * 3.5f}, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    constexpr auto flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                           ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse |
                           ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoNavInputs |
                           ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBackground;
    if (ImGui::Begin("##InspectInventory", nullptr, flags)) {
        auto* draw = ImGui::GetWindowDrawList();
        auto provider = artwork.load();
        if (assets && provider)
            Image(provider(assets, 0), origin, point(layout.width, layout.height));
        else
            draw->AddRectFilled(origin, point(layout.width, layout.height), IM_COL32(12, 12, 12, 245));
        auto chrome = [&](const char* key, ImVec2 a, ImVec2 b, int frame = 0) {
            if (assets && provider)
                if (const auto it = assets->chrome.find(key); it != assets->chrome.end())
                    Image(provider(assets, it->second), a, b, frame);
        };
        auto button = [&](const char* id, Rect rect) {
            ImGui::SetCursorScreenPos(point(rect.x, rect.y));
            return ImGui::InvisibleButton(id, {rect.width * scale, rect.height * scale});
        };
        const auto& close = layout.close;
        if (button("##CloseInspect", close))
            Close();
        const bool closeHover = ImGui::IsItemHovered();
        chrome("close", point(close.x, close.y), point(close.x + close.width, close.y + close.height),
               closeHover ? 3 : 0);
        if (!assets || !assets->chrome.contains("close"))
            Text(point(close.x, close.y), point(close.x + close.width, close.y + close.height), "X",
                 textSize);
        size_t visibleItems = 0, renderedItems = 0;
        if (view.snapshot && artReady)
            for (const auto& item : view.snapshot->items) {
                if (item.slot && (item.slot == 4 || item.slot == 5 || item.slot == 11 || item.slot == 12) &&
                    ((item.slot >= 11) != alternate))
                    continue;
                Rect rect;
                if (item.slot)
                    rect = layout.slots[item.slot];
                else {
                    if (int(item.x) + item.width > layout.columns || int(item.y) + item.height > layout.rows)
                        continue;
                    rect = {layout.grid.x + item.x * layout.cell, layout.grid.y + item.y * layout.cell,
                            item.width * layout.cell, item.height * layout.cell};
                }
                if (rect.width <= 0 || rect.height <= 0)
                    continue;
                ++visibleItems;
                const auto a = point(rect.x, rect.y), b = point(rect.x + rect.width, rect.y + rect.height);
                // Retail uses a uniform inventory cell scale for every item, with
                // centered equipment. Never enlarge a one-cell icon to fill a slot.
                draw->AddRectFilled(a, b, item.slot ? IM_COL32(0, 24, 0, 80) : IM_COL32(0, 0, 65, 180));
                bool pictured = false;
                if (assets && provider)
                    if (const auto at = assets->icons.find(item.id); at != assets->icons.end()) {
                        const auto image = provider(assets, at->second);
                        if (image.texture && image.frameWidth > 0 && image.height > 0) {
                            const auto density = assets->iconScales.find(item.id);
                            const float itemScale =
                                scale * (density == assets->iconScales.end() ? 1.f : density->second);
                            const float fit = (std::min)({1.f, (b.x - a.x) / (image.frameWidth * itemScale),
                                                          (b.y - a.y) / (image.height * itemScale)});
                            const float iw = image.frameWidth * itemScale * fit,
                                        ih = image.height * itemScale * fit;
                            Image(image, {(a.x + b.x - iw) * .5f, (a.y + b.y - ih) * .5f},
                                  {(a.x + b.x + iw) * .5f, (a.y + b.y + ih) * .5f});
                            pictured = true;
                        }
                    }
                renderedItems += pictured ? 1 : 0;
                if (!pictured)
                    Text(a, b, item.code, textSize, Color(5));
                if (item.quantity > 1)
                    draw->AddText(Font(textSize * .8f), textSize * .8f, {a.x + 2, b.y - textSize}, Color(0),
                                  std::to_string(item.quantity).c_str());
                ImGui::PushID(int(item.id));
                button("##InspectItem", rect);
                if (ImGui::IsItemHovered()) {
                    draw->AddRectFilled(a, b, IM_COL32(0, 180, 0, 35));
                    Tooltip(item);
                }
                ImGui::PopID();
            }
        // Original weapon-swap tabs remain in their authored location.
        for (int arm : {4, 5}) {
            const auto& slot = layout.slots[arm];
            Rect tab{slot.x, 35, slot.width, 48};
            chrome("tabs", point(tab.x, tab.y), point(tab.x + tab.width, tab.y + tab.height),
                   alternate ? 1 : 0);
            if (button(arm == 4 ? "##InspectWeaponsLeft" : "##InspectWeaponsRight", tab))
                alternate = !alternate;
            Text(point(tab.x, tab.y), point(tab.x + tab.width * .5f, tab.y + tab.height), "I", textSize,
                 Color(alternate ? 5 : 4));
            Text(point(tab.x + tab.width * .5f, tab.y), point(tab.x + tab.width, tab.y + tab.height), "II",
                 textSize, Color(alternate ? 4 : 5));
        }
        // Reuse the native gold plate for the inspection designation. Labels
        // and loading state sit below the art, leaving every native slot fixed.
        Text(point(layout.width * .32f, layout.height - 140), point(layout.width * .68f, layout.height - 38),
             HudLocale::Tr("Inspect"), textSize);
        const ImVec2 footer{origin.x, origin.y + h + 8};
        Text(footer, {footer.x + w, footer.y + textSize * 1.5f},
             view.snapshot ? view.snapshot->name : HudLocale::Tr("Inspect"), textSize);
        if (view.snapshot) {
            const auto& s = *view.snapshot;
            const std::string designation =
                s.type ? std::string(HudLocale::Tr(s.designation.c_str())) + " — " + s.owner
                       : HudLocale::Tr("Equipment and backpack");
            Text({footer.x, footer.y + textSize * 1.4f}, {footer.x + w, footer.y + textSize * 2.5f},
                 designation, textSize * .8f, Color(7));
        }
        if (view.busy || !view.snapshot || !artReady) {
            const std::string state = view.busy       ? HudLocale::Tr("Reading equipment and inventory...")
                                      : view.snapshot ? HudLocale::Tr("Loading inventory artwork...")
                                                      : view.status;
            draw->AddRectFilled(point(30, layout.height * .43f),
                                point(layout.width - 30, layout.height * .57f), IM_COL32(0, 0, 0, 235));
            Text(point(42, layout.height * .43f), point(layout.width - 42, layout.height * .57f), state,
                 textSize);
        }
        if (assets && artReady && view.snapshot &&
            (assets->icons.size() < view.snapshot->items.size() || renderedItems < visibleItems)) {
            const Rect report{30, layout.height - 116, 280, 60};
            Text(point(report.x, report.y), point(report.x + report.width, report.y + report.height),
                 HudLocale::Tr("Artwork report"), textSize * .8f);
            if (button("##InspectArtworkReport", report))
                ImGui::SetClipboardText((assets->artworkReport + "\nVisible item images: " +
                                         std::to_string(renderedItems) + "/" + std::to_string(visibleItems))
                                            .c_str());
            if (ImGui::IsItemHovered())
                HudText::SetTooltip("Copy missing item artwork details to share for troubleshooting");
        }
        if (!view.busy) {
            const Rect refresh{layout.width * .32f, layout.height - 140, layout.width * .36f, 102};
            if (button("##RefreshInspect", refresh))
                Refresh();
            if (ImGui::IsItemHovered())
                HudText::SetTooltip("Refresh equipment and inventory");
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
}
} // namespace Inspect
