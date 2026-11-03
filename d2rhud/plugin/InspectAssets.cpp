#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../BackgroundJob.h"
#include "../ProcessLifetime.h"
#include "D2RHUD/HudConfigStore.h"
#include "GameAssets.h"
#include "Inspect.h"
#include "MarketplaceItemVisuals.h"
#include <atomic>
#include <cmath>
#include <set>

namespace Inspect {
namespace {
using Json = HudConfig::Json;
using Sprite = LegacyTransmog::Skin::Sprite;
struct Source {
    std::filesystem::path install;
    std::string mod, warning;
    Layout layout;
    std::shared_ptr<const Sprite> background;
    std::map<std::string, std::shared_ptr<const Sprite>> chrome;
    std::shared_ptr<const Assets> base;
    Marketplace::ItemVisuals::Catalog visuals;
    bool visualReady = false, ready = false;
};
struct State {
    std::atomic<std::shared_ptr<const Source>> source;
    HudAsync::Job<std::shared_ptr<const Source>> sourceJob;
    HudAsync::Job<std::shared_ptr<const Assets>> job;
    std::shared_ptr<const Assets> assets;
    uint64_t requested = 0;
};
State& Runtime() { return HudLifetime::State<State>(); }
void Merge(Json& base, const Json& overlay) {
    if (!base.is_object() || !overlay.is_object()) {
        base = overlay;
        return;
    }
    for (auto it = overlay.begin(); it != overlay.end(); ++it) {
        if (it.key() == "children" && it.value().is_array() && base.contains("children") &&
            base["children"].is_array()) {
            for (const auto& child : it.value()) {
                bool found = false;
                const auto name = child.is_object() ? child.value("name", std::string{}) : std::string{};
                if (!name.empty())
                    for (auto& existing : base["children"])
                        if (existing.is_object() && existing.value("name", std::string{}) == name) {
                            Merge(existing, child);
                            found = true;
                            break;
                        }
                if (!found)
                    base["children"].push_back(child);
            }
        } else if (base.contains(it.key()) && base[it.key()].is_object() && it.value().is_object())
            Merge(base[it.key()], it.value());
        else
            base[it.key()] = it.value();
    }
}
Json Document(GameAssets& assets, std::string path, unsigned depth = 0) {
    if (depth >= 8)
        return Json::object();
    const auto bytes = assets.Read(path);
    if (bytes.empty() || bytes.size() > 4 * 1024 * 1024)
        return Json::object();
    Json result;
    if (!HudConfig::Parse(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()),
                          result) ||
        !result.is_object())
        return Json::object();
    const auto parent = result.value("basedOn", std::string{});
    if (!parent.empty() && parent.size() < 128 && parent.find_first_of("/\\:") == parent.npos &&
        parent.find("..") == parent.npos) {
        auto base = Document(assets, "global/ui/layouts/" + parent, depth + 1);
        Merge(base, result);
        return base;
    }
    return result;
}
Json Value(Json value, const Json& profile) {
    for (int n = 0; n < 8 && value.is_string(); ++n) {
        auto key = value.get<std::string>();
        if (key.empty() || key[0] != '$')
            break;
        key.erase(0, 1);
        if (!profile.contains(key))
            return {};
        value = profile[key];
    }
    return value;
}
float Number(const Json& object, const char* name, float fallback) {
    if (!object.is_object() || !object.contains(name) || !object[name].is_number())
        return fallback;
    const auto value = object[name].get<float>();
    return std::isfinite(value) && value >= 0 && value <= 16384 ? value : fallback;
}
Rect Rectangle(const Json& input, const Json& profile, Rect fallback = {}) {
    const auto v = Value(input, profile);
    return {Number(v, "x", fallback.x), Number(v, "y", fallback.y), Number(v, "width", fallback.width),
            Number(v, "height", fallback.height)};
}
std::string ImagePath(std::string text) {
    std::replace(text.begin(), text.end(), '\\', '/');
    if (text.empty() || text.size() > 256 || text[0] == '/' || text.find(':') != text.npos)
        return {};
    const auto path =
        (std::filesystem::path("hd/global/ui") / std::filesystem::u8path(text)).lexically_normal();
    const auto normalized = path.generic_string();
    if (normalized.starts_with("../") || normalized == "..")
        return {};
    return normalized.ends_with(".sprite") ? normalized : normalized + ".sprite";
}
std::shared_ptr<const Sprite> LoadSprite(GameAssets& assets, const std::string& path) {
    if (path.empty())
        return {};
    auto sprite = std::make_shared<Sprite>();
    return LegacyTransmog::Skin::Decode(assets.Read(path), *sprite) ? sprite : nullptr;
}
} // namespace
static std::shared_ptr<const Source> LoadSource(const std::filesystem::path& install,
                                                const std::string& mod) {
    auto source = std::make_shared<Source>();
    source->install = install;
    source->mod = mod;
    // Retail fallback is a paper doll, not a generic table of equipment.
    source->layout.slots = {Rect{},
                            Rect{470, 40, 196, 196},
                            Rect{706, 208, 98, 98},
                            Rect{470, 283, 196, 294},
                            Rect{97, 87, 196, 392},
                            Rect{849, 87, 196, 392},
                            Rect{332, 625, 98, 98},
                            Rect{706, 625, 98, 98},
                            Rect{471, 625, 196, 98},
                            Rect{848, 525, 196, 196},
                            Rect{95, 525, 196, 196},
                            Rect{97, 87, 196, 392},
                            Rect{849, 87, 196, 392}};
    try {
        GameAssets assets(install, mod);
        const auto profile = Document(assets, "global/ui/layouts/_profilehd.json");
        auto layout = Document(assets, "global/ui/layouts/playerinventoryexpansionlayouthd.json");
        if (!layout.contains("children"))
            layout = Document(assets, "global/ui/layouts/playerinventoryoriginallayouthd.json");
        const std::map<std::string, int> slots{
            {"head", 1},       {"neck", 2},      {"torso", 3}, {"right_arm", 4}, {"left_arm", 5},
            {"right_hand", 6}, {"left_hand", 7}, {"belt", 8},  {"feet", 9},      {"gloves", 10}};
        if (layout.contains("children") && layout["children"].is_array())
            for (const auto& child : layout["children"]) {
                if (!child.is_object() || !child.contains("fields") || !child["fields"].is_object())
                    continue;
                const auto& fields = child["fields"];
                const auto name = child.value("name", std::string{});
                if (name == "background" && fields.contains("filename")) {
                    const auto file = Value(fields["filename"], profile);
                    if (file.is_string())
                        source->background = LoadSprite(assets, ImagePath(file.get<std::string>()));
                }
                if ((name == "close" || name == "background_right_arm_selected") &&
                    fields.contains("filename")) {
                    const auto file = Value(fields["filename"], profile);
                    if (file.is_string()) {
                        const auto key = name == "close" ? "close" : "tabs";
                        source->chrome[key] = LoadSprite(assets, ImagePath(file.get<std::string>()));
                        if (name == "close") {
                            if (fields.contains("rect"))
                                source->layout.close =
                                    Rectangle(fields["rect"], profile, source->layout.close);
                            if (const auto sprite = source->chrome[key]) {
                                source->layout.close.width = float(sprite->atlas.frameWidth);
                                source->layout.close.height = float(sprite->atlas.height);
                            }
                        }
                    }
                }
                if (name == "grid") {
                    if (fields.contains("rect"))
                        source->layout.grid = Rectangle(fields["rect"], profile, source->layout.grid);
                    if (fields.contains("cellSize")) {
                        const auto cell = Value(fields["cellSize"], profile);
                        source->layout.cell = Number(cell, "x", source->layout.cell);
                    }
                    if (fields.contains("cellCount")) {
                        const auto count = Value(fields["cellCount"], profile);
                        source->layout.columns = int(Number(count, "x", 10));
                        source->layout.rows = int(Number(count, "y", 4));
                    }
                }
                const auto location = fields.value("location", std::string{});
                const auto at = slots.find(location);
                if (at != slots.end() && fields.contains("rect"))
                    source->layout.slots[at->second] =
                        Rectangle(fields["rect"], profile, source->layout.slots[at->second]);
            }
        source->layout.slots[11] = source->layout.slots[4];
        source->layout.slots[12] = source->layout.slots[5];
        auto& l = source->layout;
        l.columns = std::clamp(l.columns, 1, 32);
        l.rows = std::clamp(l.rows, 1, 32);
        l.cell = std::clamp(l.cell, 20.f, 200.f);
        l.grid.width = l.columns * l.cell;
        l.grid.height = l.rows * l.cell;
        l.width = source->background ? float(source->background->atlas.frameWidth)
                                     : (std::max)(1162.f, l.grid.x + l.grid.width + 80);
        l.height =
            source->background ? float(source->background->atlas.height) : l.grid.y + l.grid.height + 80;
        l.width = (std::max)(l.width, l.grid.x + l.grid.width + 32);
        l.height = (std::max)(l.height, l.grid.y + l.grid.height + 32);
        try {
            // D2R overlays partial mod HD mappings onto retail mappings. Reading
            // only the mod JSON loses every unchanged base-game item icon.
            GameAssets retail(install, {});
            auto mappings = [&](const std::string& path) {
                Json rows = Json::object();
                for (auto* layer : {&retail, &assets}) {
                    const auto bytes = layer->Read(path);
                    const auto parsed = Json::parse(bytes.begin(), bytes.end(), nullptr, false);
                    if (parsed.is_array())
                        for (const auto& row : parsed)
                            if (row.is_object() && row.size() == 1) {
                                const auto entry = row.begin();
                                rows[entry.key()] = entry.value();
                            }
                }
                Json merged = Json::array();
                for (auto entry = rows.begin(); entry != rows.end(); ++entry)
                    merged.push_back(Json{{entry.key(), entry.value()}});
                const auto text = merged.dump();
                return std::vector<uint8_t>(text.begin(), text.end());
            };
            source->visuals = Marketplace::ItemVisuals::Catalog::Load(
                [&](const std::string& table) { return assets.ReadTable(table, true).binary; }, mappings,
                [&](const std::string& table) { return assets.ReadTableNames(table, true); });
            source->visualReady = true;
        } catch (const std::exception& error) {
            source->warning = error.what();
        }
    } catch (...) {
    }
    auto base = std::make_shared<Assets>();
    base->layout = source->layout;
    base->warning = source->warning;
    base->sprites.push_back(source->background);
    for (const auto& [key, sprite] : source->chrome)
        if (sprite) {
            base->chrome[key] = base->sprites.size();
            base->sprites.push_back(sprite);
        }
    source->base = std::move(base);
    source->ready = true;
    return source;
}
void ConfigureAssets(const std::filesystem::path& install, const std::string& mod) {
    auto source = std::make_shared<Source>();
    source->install = install;
    source->mod = mod;
    Runtime().source.store(std::move(source));
}
std::shared_ptr<const Assets> Art(const View& view) {
    auto& state = Runtime();
    if (auto complete = state.job.Poll();
        complete && complete->value && *complete->value && complete->ticket == state.requested)
        state.assets = std::move(*complete->value);
    auto source = state.source.load();
    if (!source)
        return {};
    if (auto loaded = state.sourceJob.Poll(); loaded && loaded->value && *loaded->value) {
        const auto candidate = *loaded->value;
        if (candidate->install == source->install && candidate->mod == source->mod) {
            source = candidate;
            state.source.store(source);
        }
    }
    if (!source->ready) {
        if (!state.sourceJob.Busy())
            state.sourceJob.Start(0, [source] { return LoadSource(source->install, source->mod); });
        return {};
    }
    if (!view.snapshot)
        return source->base;
    if (state.requested != view.revision && !state.job.Busy()) {
        const auto snapshot = view.snapshot;
        const auto revision = view.revision;
        state.requested = revision;
        state.job.Start(revision, [source, snapshot, revision] {
            auto out = std::make_shared<Assets>(*source->base);
            out->revision = revision;
            out->warning = source->warning;
            GameAssets assets(source->install, source->mod);
            std::map<std::string, size_t> loaded;
            std::map<std::string, float> scales;
            Json problems = Json::array();
            size_t pixelBytes = source->background ? source->background->atlas.pixels.size() : 0;
            for (const auto& item : snapshot->items) {
                const auto visual = source->visualReady ? source->visuals.ResolveInventory(item.code, item.quality,
                                                                                  item.index, item.picture)
                                                        : Marketplace::ItemVisuals::Visual{};
                if (!visual) {
                    problems.push_back({{"code", item.code},
                                        {"quality", item.quality},
                                        {"index", item.index},
                                        {"reason", "Item artwork mapping could not be resolved."}});
                    continue;
                }
                const auto path = "hd/global/ui/" + visual.filename + ".sprite";
                if (const auto cached = loaded.find(path); cached != loaded.end()) {
                    out->icons[item.id] = cached->second;
                    out->iconScales[item.id] = scales[path];
                    continue;
                }
                auto sprite = LoadSprite(assets, path);
                auto error = assets.ReadError();
                float density = 1;
                if (!sprite || pixelBytes + sprite->atlas.pixels.size() > 64 * 1024 * 1024) {
                    // Native inventory can use the half-resolution variant.
                    // Don't require a full-resolution sprite to be installed.
                    const auto lowPath = path.substr(0, path.size() - 7) + ".lowend.sprite";
                    auto low = LoadSprite(assets, lowPath);
                    if (low) {
                        sprite = std::move(low);
                        density = 2;
                    } else if (error.empty())
                        error = assets.ReadError();
                }
                if (!sprite || pixelBytes + sprite->atlas.pixels.size() > 64 * 1024 * 1024) {
                    problems.push_back({{"code", item.code},
                                        {"path", path},
                                        {"reason", sprite ? "Decoded artwork exceeds the panel memory budget."
                                                   : error.empty() ? "Unsupported or incomplete sprite data."
                                                                   : error}});
                    continue;
                }
                pixelBytes += sprite->atlas.pixels.size();
                out->icons[item.id] = out->sprites.size();
                loaded[path] = out->sprites.size();
                scales[path] = density;
                out->iconScales[item.id] = density;
                out->sprites.push_back(std::move(sprite));
            }
            const auto installText = source->install.u8string();
            out->artworkReport = Json{{"install", std::string(installText.begin(), installText.end())},
                                      {"mod", source->mod},
                                      {"catalogReady", source->visualReady},
                                      {"catalogWarning", source->warning},
                                      {"items", snapshot->items.size()},
                                      {"decodedItems", out->icons.size()},
                                      {"pixelBytes", pixelBytes},
                                      {"problems", std::move(problems)}}
                                     .dump(2);
            if (out->icons.size() < snapshot->items.size())
                out->warning +=
                    " Some item artwork is unavailable; hover the labeled slots to inspect those items.";
            return std::shared_ptr<const Assets>(std::move(out));
        });
    }
    return state.assets && state.assets->revision == view.revision ? state.assets : source->base;
}
} // namespace Inspect
