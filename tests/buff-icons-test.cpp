// Include the implementation to exercise the private memory reader and data loader
// without injecting into the game. The renderer and executable resolver are stubbed.
#include "../d2rhud/plugin/BuffIcons.cpp"
#include <imgui_internal.h>
#include <cassert>
#include <iostream>
#include <limits>

DWORD64 Pattern::Address(uint32_t) { return 0; }
uint64_t D3D12::GetBuffIconAtlas(const std::string&, const BuffIcons::SpriteAtlas&) { return 0; }

int main(int argc, char** argv) {
    using namespace BuffIcons;
    constexpr uint8_t actualBonusCode[] = {0x40, 0x56, 0x41, 0x54, 0x41, 0x56, 0x48, 0x83, 0xec, 0x50};
    assert(IsSkillBonusFunction(actualBonusCode, sizeof(actualBonusCode)));
    assert(IsSkillBonusFunction(actualBonusCode + 1, sizeof(actualBonusCode) - 1));
    assert(!IsSkillBonusFunction(actualBonusCode, sizeof(actualBonusCode) - 1));
    assert(!IsSkillBonusFunction(nullptr, 0));
    const uint8_t unrelatedCode[10]{};
    assert(!IsSkillBonusFunction(unrelatedCode, sizeof(unrelatedCode)));

    // Reproduce the live character: learned Warmth/Energy Shield, both level 20.
    // Pattern::Address returns null here, so the native bonus lookup is unavailable.
    // Learned skills must still be enumerated using their cached levels.
    {
        std::array<uint8_t, 0x1b8> characterMemory{};
        int16_t warmthId = 37, shieldId = 58;
        SkillMemory warmth{}, shield{};
        warmth.txt = reinterpret_cast<uintptr_t>(&warmthId); warmth.level = 20; warmth.owner = -1;
        shield.txt = reinterpret_cast<uintptr_t>(&shieldId); shield.level = 20; shield.owner = -1;
        warmth.next = reinterpret_cast<uintptr_t>(&shield);
        uintptr_t first = reinterpret_cast<uintptr_t>(&warmth), list = reinterpret_cast<uintptr_t>(&first);
        std::memcpy(characterMemory.data() + 0x100, &list, sizeof(list));
        database.skills[37] = {37, 0, "Warmth", "", {}};
        database.skills[58] = {58, 0, "Energy Shield", "", {}};
        auto choices = ReadAvailableSkills(reinterpret_cast<uintptr_t>(characterMemory.data()), [](int id, int level) { return LiveSkill{id, level}; });
        assert(choices.size() == 2 && choices[0].id == 58 && choices[1].id == 37);
        assert(choices[0].level == 20 && choices[1].level == 20);
        shield.level = 0; shield.bonus = 5;
        choices = ReadAvailableSkills(reinterpret_cast<uintptr_t>(characterMemory.data()), [](int id, int level) { return LiveSkill{id, level}; });
        assert(choices.size() == 2 && choices[0].level == 5);
        database = {};
    }
    assert(RemainingSeconds(1250, true, true, 1000) == 10);
    assert(RemainingSeconds(1250, true, true, 1000) == 10); // paused game
    assert(RemainingSeconds(1500, true, true, 1000) == 20); // recast extends end frame
    assert(RemainingSeconds(1000, true, true, 1000) == 0);
    assert(RemainingSeconds(900, true, true, 1000) == 0);
    assert(RemainingSeconds(250, true, false, 500000) == 10); // client is relative
    assert(RemainingSeconds(0, false, true, 1000) < 0); // untimed effect
    assert(RemainingSeconds(std::numeric_limits<float>::quiet_NaN(), true, true, 0) == 0);
    assert(TimerText(0.04f) == "1" && TimerText(59.1f) == "1:00");
    assert(TimerText(125) == "2:05" && TimerText(-1).empty());
    assert(TimerText(125, 1) == "125s");
    assert(TimerText(1.12f, 2) == "1.2s" && TimerText(0.04f, 2) == "0.1s");
    assert(TimerText(10, 3) == "0:10" && TimerText(125, 3) == "2:05");
    assert(TimerText(3661, 4) == "1:01:01" && TimerText(10, 4) == "0:00:10");

    std::array<uint8_t, 0xb58> root{};
    std::array<uint8_t, 0x1b8> player{};
    StateMemory state{};
    auto put = [](auto& buffer, size_t offset, auto value) { std::memcpy(buffer.data() + offset, &value, sizeof(value)); };
    put(player, 0x88, reinterpret_cast<uintptr_t>(root.data()));
    put(root, 0x1c, uint32_t(0x80000000));
    put(root, 0x68, reinterpret_cast<uintptr_t>(&state));
    put(root, 0xac8 + 4, uint32_t(1 << 3));
    state.state = 35; state.flags = 2; state.timeout = 1250;
    auto states = ReadStates(reinterpret_cast<uintptr_t>(player.data()), true, 1000);
    assert(states.count(35) == 1);
    state.timeout = 1500;
    states = ReadStates(reinterpret_cast<uintptr_t>(player.data()), true, 1000);
    assert(states.at(35).timeout == 1500);
    assert(ReadStates(reinterpret_cast<uintptr_t>(player.data()), true, 1500).empty());
    put(root, 0xac8 + 4, uint32_t(0)); // removed before expiration
    assert(ReadStates(reinterpret_cast<uintptr_t>(player.data()), true, 1000).empty());
    put(root, 0xac8 + 4, uint32_t(1 << 3));
    state.previous = reinterpret_cast<uintptr_t>(&state); // corrupt/cyclic chain is bounded
    assert(ReadStates(reinterpret_cast<uintptr_t>(player.data()), true, 1000).size() == 1);
    assert(ReadStates(1, true, 0).empty());

    nlohmann::json settings = {{"BuffIcons", {{"Enabled", true}, {"Scale", 100}, {"Columns", 0},
        {"ShowLabels", true}, {"LabelPosition", "Top"}, {"LabelSize", 20}, {"LabelColor", {0.2, 0.4, 0.6, 0.8}},
        {"TimerPosition", "Above"}, {"TimerFormat", "DecimalSeconds"}, {"TimerSize", 22}, {"TimerColor", {1, 0.5, 0, 1}},
        {"HighlightExpiring", false}, {"ExpiringSeconds", 12}, {"ExpiringColor", {1, 0, 0, 1}},
        {"Characters", {{"Test:4", {138, 149, -1, "bad"}}}}}}};
    LoadFromJson(settings);
    nlohmann::ordered_json saved;
    SaveToJson(saved);
    assert(saved["BuffIcons"]["Scale"] == 3 && saved["BuffIcons"]["Columns"] == 1);
    assert(saved["BuffIcons"]["Characters"]["Test:4"].size() == 2);
    assert(saved["BuffIcons"]["ShowLabels"] == true && saved["BuffIcons"]["LabelPosition"] == "Top");
    assert(saved["BuffIcons"]["LabelSize"] == 20 && std::abs(saved["BuffIcons"]["LabelColor"][2].get<float>() - 0.6f) < 0.001f);
    assert(saved["BuffIcons"]["TimerPosition"] == "Above" && saved["BuffIcons"]["TimerFormat"] == "DecimalSeconds");
    assert(saved["BuffIcons"]["TimerSize"] == 22 && saved["BuffIcons"]["TimerColor"][1] == 0.5f);
    assert(saved["BuffIcons"]["HighlightExpiring"] == false && saved["BuffIcons"]["ExpiringSeconds"] == 12);
    LoadFromJson(saved);
    nlohmann::ordered_json roundTrip; SaveToJson(roundTrip);
    assert(saved == roundTrip);
    LoadFromJson({{"BuffIcons", {{"Enabled", "invalid"}, {"Scale", "invalid"}}}});
    SaveToJson(saved); assert(saved["BuffIcons"]["Enabled"] == false && saved["BuffIcons"]["Scale"] == 1);
    assert(saved["BuffIcons"]["ShowLabels"] == false && saved["BuffIcons"]["LabelPosition"] == "Bottom");
    assert(saved["BuffIcons"]["TimerPosition"] == "Below" && saved["BuffIcons"]["TimerFormat"] == "Automatic");
    LoadFromJson({{"BuffIcons", {{"LabelSize", -10}, {"LabelPosition", 123}, {"LabelColor", {-1, 2, "bad", 0.5}}}}});
    SaveToJson(saved);
    assert(saved["BuffIcons"]["LabelSize"] == 8 && saved["BuffIcons"]["LabelPosition"] == "Bottom");
    assert(saved["BuffIcons"]["LabelColor"][0] == 0 && saved["BuffIcons"]["LabelColor"][1] == 1 && saved["BuffIcons"]["LabelColor"][2] == 1);
    LoadFromJson({{"BuffIcons", {{"TimerSize", 100}, {"TimerPosition", "invalid"}, {"TimerFormat", 42}, {"ExpiringSeconds", -1}}}});
    SaveToJson(saved);
    assert(saved["BuffIcons"]["TimerSize"] == 32 && saved["BuffIcons"]["ExpiringSeconds"] == 0);
    assert(saved["BuffIcons"]["TimerPosition"] == "Below" && saved["BuffIcons"]["TimerFormat"] == "Automatic");
    LoadFromJson({});

    std::vector<uint8_t> bytes(40 + 4 * 4 * 4);
    std::memcpy(bytes.data(), "SpA1", 4);
    auto set16 = [&](size_t p, uint16_t v) { std::memcpy(bytes.data() + p, &v, 2); };
    auto set32 = [&](size_t p, uint32_t v) { std::memcpy(bytes.data() + p, &v, 4); };
    set16(4, 31); set16(6, 2); set32(8, 4); set32(12, 4); set32(20, 2);
    SpriteAtlas atlas;
    assert(ReadSprite(bytes, atlas) && atlas.frames == 2 && !atlas.bc3);
    bytes.pop_back(); assert(!ReadSprite(bytes, atlas));
    set16(4, 61); assert(ReadSprite(bytes, atlas) && atlas.bc3 && atlas.pixels.size() == 16);
    set32(20, 3); assert(!ReadSprite(bytes, atlas));

    if (argc > 1) {
        auto db = LoadDatabase(argv[1], argc > 2 ? argv[2] : "");
        if (!db.error.empty()) { std::cerr << db.error << '\n'; return 1; }
        assert(db.skills.count(138)); // Shout
        assert(!db.skills.at(138).states.empty());
        assert(db.skills.at(138).frame == 24);
        assert(db.atlases.count(db.skills.at(138).sprite));
        std::cout << "Loaded " << db.skills.size() << " state-bearing skills and " << db.atlases.size() << " original sprite atlases\n";
        if (argc > 2 && std::string(argv[2]) == "RMD-MP")
            assert(db.skills.at(138).sprite.find("ReMoDDeD/Panels/Skills/ST_Barbarian_SK") != std::string::npos);
        database = std::move(db); loaded = true;
        available = {{138, 10, 10, true}};
        selectedStates = available;
        character = "Test:4";
        config.characters[character] = {138}; config.enabled = true;
        ImGui::CreateContext();
        auto& io = ImGui::GetIO(); io.IniFilename = nullptr; io.DeltaTime = 1.0f / 60;
        for (float size : {12.0f, 15.0f, 18.0f, 24.0f, 36.0f, 48.0f}) {
            ImFontConfig fontConfig; fontConfig.SizePixels = size;
            io.Fonts->AddFontDefault(&fontConfig);
        }
        unsigned char* fontPixels; int fontWidth, fontHeight;
        io.Fonts->GetTexDataAsRGBA32(&fontPixels, &fontWidth, &fontHeight);
        io.Fonts->TexID = (ImTextureID)1;
        for (float scale : {1.0f, 2.0f}) {
            io.DisplaySize = ImVec2(1920 * scale, 1080 * scale);
            for (int i = 0; i < 2; ++i) {
                io.FontGlobalScale = i == 1 ? scale : 1;
                ImGui::NewFrame();
                if (i == 1) {
                    ImGui::SetNextWindowSize(ImVec2(720 * scale, 760 * scale));
                    ImGui::Begin("Buff Icons");
                    ImGui::PushFont(io.Fonts->Fonts[2]);
                    DrawSettingsPanel(scale);
                    ImGui::PopFont();
                    ImGui::End();
                }
                Render(i == 1, scale); ImGui::Render();
                assert(ImGui::GetDrawData()->TotalVtxCount > 0);
            }
            // A theme's minimum window size must not enlarge a one-icon bar.
            // Untimed buffs reserve neither a timer row nor trailing spacing.
            io.FontGlobalScale = scale;
            ImGui::GetStyle().WindowMinSize = ImVec2(300 * scale, 250 * scale);
            selectedStates = {{138, 10, -1, true}};
            config.columns = 10; config.scale = 1; config.timers = true;
            for (int labelMode = 0; labelMode < 3; ++labelMode) {
                config.labels = labelMode != 0;
                config.labelBottom = labelMode == 2;
                ImGui::NewFrame();
                auto empty = LayoutIcons({}, scale, io.DisplaySize.x);
                assert(empty.extent.x == 0 && empty.extent.y == 0);
                auto single = LayoutIcons(selectedStates, scale, io.DisplaySize.x);
                assert(single.extent.x == 48 * scale && single.extent.y == 48 * scale);
                std::vector<LiveSkill> three(3, selectedStates.front());
                auto row = LayoutIcons(three, scale, io.DisplaySize.x);
                assert(row.extent.x == 156 * scale && row.extent.y == 48 * scale);
                config.columns = 2;
                auto wrapped = LayoutIcons(three, scale, io.DisplaySize.x);
                assert(wrapped.extent.x == 102 * scale && wrapped.extent.y == 102 * scale);
                config.columns = 10;
                Render(true, scale);
                auto window = ImGui::FindWindowByName("##BuffIconsPlacement");
                assert(window && window->Size.x == 48 * scale && window->Size.y == 48 * scale);
                ImGui::Render();
            }
            selectedStates.front().seconds = 10;
            ImGui::NewFrame(); Render(true, scale);
            auto timedWindow = ImGui::FindWindowByName("##BuffIconsPlacement");
            assert(timedWindow->Size.y > 48 * scale && timedWindow->Size.y < 75 * scale);
            ImGui::Render();
            const float belowHeight = timedWindow->Size.y;
            for (int position = 0; position < 4; ++position) {
                for (int format = 0; format < 5; ++format) {
                    config.timerPosition = position; config.timerFormat = format;
                    config.labels = true; config.labelBottom = position == 3;
                    ImGui::NewFrame();
                    Render(true, scale);
                    auto window = ImGui::FindWindowByName("##BuffIconsPlacement");
                    if (position >= 2) assert(window->Size.x == 48 * scale && window->Size.y == 48 * scale);
                    else assert(window->Size.y == belowHeight);
                    ImGui::Render();
                }
            }
            config.timerPosition = 0; config.timerFormat = 0;
        }
        ImGui::DestroyContext();
    }
    std::cout << "Buff icon timer, state removal/recast, sprite validation and config tests passed\n";
}
