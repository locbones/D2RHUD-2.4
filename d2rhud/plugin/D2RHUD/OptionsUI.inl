void ShowD2RHUDMenu()
{
    if (!showD2RHUDMenu) return;

    static std::string saveStatusMessage = "";
    static ImVec4 saveStatusColor = ImVec4(0, 1, 0, 1); // default green
    float menuScale = GetMenuScaleFactor();
    ImVec2 windowSize = ImVec2(850.0f * menuScale, 520.0f * menuScale);
    HudMenu::Prepare(windowSize);

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags &= ~ImGuiConfigFlags_NoKeyboard;

    int fontIndex = 3;
    ImFont* fontSize = (fontIndex >= 0 && fontIndex < io.Fonts->Fonts.Size) ? io.Fonts->Fonts[fontIndex] : nullptr;
    if (fontSize) ImGui::PushFont(fontSize);

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ShouldDrawWindowBackgroundImage(kWindowBg_D2RHUDOptions) ? ImVec4(0, 0, 0, 0) : s_WindowBgColors[kWindowBg_D2RHUDOptions]);
    ImGui::SetNextWindowScroll({0,0});
    if (HudMenu::Begin("D2RHUD Options", &showD2RHUDMenu, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
    {
        if (ShouldDrawWindowBackgroundImage(kWindowBg_D2RHUDOptions))
            DrawWindowBackgroundImage(kWindowBg_D2RHUDOptions);
        DrawWindowTitleAndClose("Options", &showD2RHUDMenu);
        if (fontSize) ImGui::PopFont();
        ImGui::Separator();

        static HudOptions::Filter optionFilter;
        bool filterChanged=false;
        ImGui::SetNextItemWidth((std::max)(180.f,ImGui::GetContentRegionAvail().x-95*menuScale));
        filterChanged=HudText::InputTextWithHint("##OptionSearch","Search all options...",optionFilter.query,sizeof(optionFilter.query));
        ImGui::SameLine();if(ThemeButton("Clear")){optionFilter.query[0]=0;filterChanged=true;}
        if(ImGui::BeginTabBar("OptionCategories",ImGuiTabBarFlags_FittingPolicyScroll)){
            for(int i=0;i<int(std::size(HudOptions::Categories));++i)if(HudText::BeginTabItem(HudOptions::Categories[i])){
                if(optionFilter.category!=HudOptions::Category(i)){optionFilter.category=HudOptions::Category(i);filterChanged=true;}
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        optionFilter.matches=0;
        const auto optionPanels=HudOptions::BeginPanels(menuScale);
        HudMenu::Panel();
        static std::string descriptionTitle,descriptionText,descriptionNote;
        if(filterChanged||descriptionTitle.empty()){
            descriptionTitle=optionFilter.query[0]?"Search results":HudOptions::Categories[int(optionFilter.category)];
            descriptionText=optionFilter.query[0]?"Search covers every category. Hover over an option to read its details.":HudOptions::CategoryHelp[int(optionFilter.category)];
            descriptionNote="Changes apply and save automatically.";
            ImGui::SetScrollY(0);
        }
        auto values=[](){return std::make_tuple(
            d2rHUDConfig.MonsterStatsDisplay,d2rHUDConfig.SunderedMonUMods,d2rHUDConfig.MinionEquality,
            d2rHUDConfig.GambleCostControl,d2rHUDConfig.CombatLog,d2rHUDConfig.TransmogVisuals,
            d2rHUDConfig.NoWidescreenBars,d2rHUDConfig.DisableRightClickItemPickup,d2rHUDConfig.QuantityRightClicks,
            d2rHUDConfig.MinimalistQuantityPopup,d2rHUDConfig.PersistentSummons,d2rHUDConfig.D3StyleWhirlwind,
            d2rHUDConfig.ExtendedItemcodes,d2rHUDConfig.ChatSounds,d2rHUDConfig.ChatSoundPath,d2rHUDConfig.FloatingDamage,
            d2rHUDConfig.HPRolloverMods,d2rHUDConfig.HPRolloverDifficulty,d2rHUDConfig.HPRolloverPercent,
            d2rHUDConfig.SurvivorArena,d2rHUDConfig.ActVI);};
        const auto before=values();
        auto applyAndSave=[&](){
            cachedSettings.HPRollover=d2rHUDConfig.HPRolloverMods;
            cachedSettings.HPRolloverDiff=d2rHUDConfig.HPRolloverDifficulty;
            cachedSettings.HPRolloverAmt=d2rHUDConfig.HPRolloverPercent;
            cachedSettings.SunderValue=99;cachedSettings.sunderedMonUMods=d2rHUDConfig.SunderedMonUMods;
            cachedSettings.monsterStatsDisplay=d2rHUDConfig.MonsterStatsDisplay;
            cachedSettings.minionEquality=d2rHUDConfig.MinionEquality;cachedSettings.gambleForce=d2rHUDConfig.GambleCostControl;
            cachedSettings.CombatLog=d2rHUDConfig.CombatLog;cachedSettings.TransmogVisuals=d2rHUDConfig.TransmogVisuals;
            cachedSettings.ExtendedItemcodes=d2rHUDConfig.ExtendedItemcodes;cachedSettings.FloatingDamage=d2rHUDConfig.FloatingDamage;
            settings.sunderedMonUMods=d2rHUDConfig.SunderedMonUMods;settings.SunderValue=99;
            settings.HPRollover=cachedSettings.HPRollover;
            settings.HPRolloverDiff=cachedSettings.HPRolloverDiff;
            settings.HPRolloverAmt=cachedSettings.HPRolloverAmt;
            settings.monsterStatsDisplay=cachedSettings.monsterStatsDisplay;
            settings.minionEquality=cachedSettings.minionEquality;
            settings.gambleForce=cachedSettings.gambleForce;
            settings.CombatLog=cachedSettings.CombatLog;
            settings.TransmogVisuals=cachedSettings.TransmogVisuals;
            settings.ExtendedItemcodes=cachedSettings.ExtendedItemcodes;
            settings.FloatingDamage=cachedSettings.FloatingDamage;

            const bool saved=SaveFullGrailConfig(configFilePath,false);
            saveStatusMessage=saved?"Settings saved":"Could not save settings. Try again.";
            saveStatusColor=saved?QuestJournalSkin::Gold:ImVec4(.9f,.35f,.25f,1);
        };

        auto drawCheckbox = [&](const char* label, bool* value, const char* title, const char* desc, const char* descnote, const LockedValueInfo* lockInfo)
            {
                // Disable checkbox if locked
                if (lockInfo && lockInfo->locked)
                    ImGui::BeginDisabled();

                const bool changed = ThemeCheckbox(label, value);

                if (lockInfo && lockInfo->locked)
                    ImGui::EndDisabled();

                // Update main description variables (existing behavior) even if disabled
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                {
                    descriptionTitle = title;
                    descriptionText = desc;
                    descriptionNote = descnote;
                }

                if (lockInfo && lockInfo->locked && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                {
                    ImVec2 mousePos = ImGui::GetIO().MousePos;
                    ImGui::SetNextWindowPos(ImVec2(mousePos.x + 70, mousePos.y), ImGuiCond_Always);
                    ImGui::BeginTooltip();
                    float tooltipWidth = 600.0f;
                    ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + tooltipWidth);
                    HudText::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "Locked by Mod Author(s)"); // red
                    HudText::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "This setting cannot be changed while this mod is active"); // gray
                    HudText::TextWrapped("");
                    HudText::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Reason:"); // orange
                    ImGui::SameLine();
                    HudText::TextWrapped("%s", lockInfo->reason.c_str());
                    ImGui::PopTextWrapPos();
                    ImGui::EndTooltip();
                }
                return changed;
            };

        auto drawLabeledInput = [&](const char* label, auto widgetFunc, const char* title, const char* desc, const char* descnote, bool sameLine = true, float slValue = 35, const LockedValueInfo* lockInfo = nullptr)
            {
                ImGui::BeginGroup();

                if (sameLine) ImGui::AlignTextToFramePadding();
                HudText::Text("%s", label);

                if (sameLine)
                    ImGui::SameLine(0.0f, slValue * menuScale);

                // Disable input if locked
                if (lockInfo && lockInfo->locked)
                    ImGui::BeginDisabled();

                widgetFunc();

                if (lockInfo && lockInfo->locked)
                    ImGui::EndDisabled();

                ImGui::EndGroup();

                // Right Panel Description
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                {
                    descriptionTitle = title;
                    descriptionText = desc;
                    descriptionNote = descnote;
                }

                // Lock Reason Tooltip
                if (lockInfo && lockInfo->locked &&
                    ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                {
                    ImVec2 mousePos = ImGui::GetIO().MousePos;
                    ImGui::SetNextWindowPos(ImVec2(mousePos.x + 70, mousePos.y), ImGuiCond_Always);

                    ImGui::BeginTooltip();
                    ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + 600.0f);

                    HudText::TextColored(
                        ImVec4(1.0f, 0.2f, 0.2f, 1.0f),
                        "Locked by Mod Author(s)"
                    );

                    HudText::TextColored(
                        ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                        "This value cannot be changed while this mod is active\n"
                    );

                    HudText::TextWrapped("");

                    HudText::TextColored(
                        ImVec4(1.0f, 0.5f, 0.0f, 1.0f),
                        "Reason:"
                    );
                    ImGui::SameLine();
                    HudText::TextWrapped("%s", lockInfo->reason.c_str());

                    ImGui::PopTextWrapPos();
                    ImGui::EndTooltip();
                }
            };

        if (optionFilter.Visible(HudOptions::Category::Display,"Monster Stats Display","health resistance")) {
        drawCheckbox("Monster Stats Display", &d2rHUDConfig.MonsterStatsDisplay, "Monster Stats Display", "- Displays HP and Resistances of Monsters in real-time\n- Uses data retrieved directly from the game for accuracy\n- Values for -Enemy Resistance% (when applied by items) are not shown, they don't actually affect monsters\n\n", "This setting is best displayed using either of the Advanced Display Modes in D2RLAN", GetLockInfo(modName, &ModOverrideSettings::MonsterStatsDisplay));
        }
        if (optionFilter.Visible(HudOptions::Category::Gameplay,"Immunity Sunder","immune resistance pierce")) {
        if (drawCheckbox("Immunity Sunder", &d2rHUDConfig.SunderedMonUMods, "Immunity Sunder", "- Applies immunity pierce only to resistances of 100 or more\n- Includes unique modifiers and positive resistance bonuses\n- Stops immunity pierce at 99; auras and curses apply afterwards\n- TCP/IP uses the highest value per element among players in this game\n", "Requires mod files with immunity-pierce stats", GetLockInfo(modName, &ModOverrideSettings::SunderedMonUMods))) {
            SunderSystem::SetEnabled(d2rHUDConfig.SunderedMonUMods);
            settings.sunderedMonUMods = cachedSettings.sunderedMonUMods = d2rHUDConfig.SunderedMonUMods;

        }
        }
        if (optionFilter.Visible(HudOptions::Category::Items,"Minion Equality","drops treasure class")) {
        drawCheckbox("Minion Equality", &d2rHUDConfig.MinionEquality, "Minion Equality", "- Champ/Uniques minions assume their master's drops\n(Determined by your TreasureClassEx.txt entries)\n\n", "Uses TXT or BIN tables. Custom terror drop columns in BIN-only releases use the exported HUD table metadata.", GetLockInfo(modName, &ModOverrideSettings::MinionEquality));
        }
        if (optionFilter.Visible(HudOptions::Category::Items,"Gamble Cost Control","gold gambling")) {
        drawCheckbox("Gamble Cost Control", &d2rHUDConfig.GambleCostControl, "Gamble Cost Control", "- Allows the gamble cost column to be used by AMW.txt\n- If a value of -1 is specified, it will use retail cost logic\n- If Disabled, only ring and amulet costs may be changed\n\n", "The maximum gamble cost will be determined by your in-game gold limits (defined in Memory Edits)", GetLockInfo(modName, &ModOverrideSettings::GambleCostControl));
        }
        if (optionFilter.Visible(HudOptions::Category::Advanced,"Combat Log","diagnostics damage hit block dodge")) {
        drawCheckbox("Combat Log", &d2rHUDConfig.CombatLog, "Combat Log", "- Displays real-time combat logs in system chat\n    - RNG rolls for Hit, Block and Dodge results\n    - Elemental Type, +DMG%, Length and Final Damage\n    - Remaining Monster Health\n- Uses data retrieved directly from the game for accuracy\n\n", "This feature is also utilized by the 'attackinfo 1' cheat\nExpect formatting and usage to adapt over time", GetLockInfo(modName, &ModOverrideSettings::CombatLog));
        }
        if (optionFilter.Visible(HudOptions::Category::Items,"Transmog Visuals","appearance equipment")) {
        drawCheckbox("Transmog Visuals", &d2rHUDConfig.TransmogVisuals, "Transmog Visuals", "- Allows you to transform visuals of applicable items\n(Make your Hand Axe look like a Short Sword, etc)\n- Uses the mod's transmog stat and transmog TXT/JSON mapping\n- Mod Author decides all transmog possibilities\n- Applies both in-game and at the main menu\n\n", "Four-character item codes work independently when native HD support is active", GetLockInfo(modName, &ModOverrideSettings::TransmogVisuals));
        }
        if (optionFilter.Visible(HudOptions::Category::Display,"No Widescreen Bars","ultrawide black bars resolution")) {
        ImGui::BeginDisabled(!WidescreenBars::Available());
        if (ThemeCheckbox("No Widescreen Bars", &d2rHUDConfig.NoWidescreenBars))
        {
            WidescreenBars::SetEnabled(d2rHUDConfig.NoWidescreenBars);

        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        {
            descriptionTitle = "No Widescreen Bars";
            descriptionText = "- Removes the black HD side bars and faded edges at ultrawide resolutions\n"
                "- Allows mouse targeting across the full display width\n"
                "- Applies immediately and remembers your preference\n"
                "- Defaults off\n"
                "- Monster awareness keeps its normal range; distant enemies may remain inactive\n\n";
            descriptionText += "Active only in-game; main menu bars stay enabled.\n";
            descriptionNote = WidescreenBars::Status();
            const ImVec2 mousePos = ImGui::GetIO().MousePos;
            ImGui::SetNextWindowPos(ImVec2(mousePos.x + 70.0f, mousePos.y), ImGuiCond_Always);
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + 440.0f * menuScale);
            HudText::TextUnformatted("Removes the black HD side bars and faded edges at ultrawide resolutions while in-game. Main menu bars stay enabled. Mouse targeting uses the full width in-game. Applies immediately; defaults off. Monster awareness keeps its normal range; distant enemies may remain inactive.");
            ImGui::Separator();
            HudText::TextWrapped("%s", WidescreenBars::Status());
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
        }
        if (optionFilter.Visible(HudOptions::Category::Items,"Disable Right-Click Item Pickup","mouse ground items")) {
        const float pickupLabelWidth = HudText::CalcTextSize("Disable Right-Click Item Pickup").x + ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x;
        const char* pickupLabel = pickupLabelWidth <= ImGui::GetContentRegionAvail().x
            ? "Disable Right-Click Item Pickup###DisableRightClickItemPickup"
            : "Disable Right-Click\nItem Pickup###DisableRightClickItemPickup";
        if (ThemeCheckbox(pickupLabel, &d2rHUDConfig.DisableRightClickItemPickup))
        {
            D3Whirlwind::SetRightClickPickupDisabled(d2rHUDConfig.DisableRightClickItemPickup);

        }
        if (ImGui::IsItemHovered())
        {
            descriptionTitle = "Disable Right-Click Item Pickup";
            descriptionText = "- Prevents right-clicks from picking up ground items\n"
                "- Use left-click to pick up items instead\n"
                "- Also applies while using D3 Style Whirlwind\n"
                "- Inventory right-click actions and automatic gold pickup are unchanged\n\n";
            descriptionNote = "Applies immediately. Defaults off.";
        }
        }
        if (optionFilter.Visible(HudOptions::Category::Items,"Quantity Right-Clicks","Minimalist Quantity Popup split stacks")) {
        if (ThemeCheckbox("Quantity Right-Clicks", &d2rHUDConfig.QuantityRightClicks))
        {
            LegacyTransmog::SetQuantityRightClicks(d2rHUDConfig.QuantityRightClicks);

        }
        if (ImGui::IsItemHovered())
        {
            descriptionTitle = "Quantity Right-Clicks";
            descriptionText = "- Right-click a non-transmog stack to separate one item\n- Keep shows the original stack; New stack receives the chosen amount\n- Choose the final quantity and click Split\n- Example: choose 20 from 88 to keep stacks of 68 and 20\n- Uses free inventory, cube or personal stash slots\n- Close or click elsewhere to keep the initial one-item split\n- Full storage leaves the original stack unchanged\n\n";
            descriptionNote = LegacyTransmog::StackSplitStatus();
        }
        ImGui::Indent();
        if (ThemeCheckbox("Minimalist Quantity Popup", &d2rHUDConfig.MinimalistQuantityPopup))
        {
            LegacyTransmog::SetMinimalistQuantityPopup(d2rHUDConfig.MinimalistQuantityPopup);

        }
        if (ImGui::IsItemHovered())
        {
            descriptionTitle = "Minimalist Quantity Popup";
            descriptionText = "- Uses the compact quantity-entry layout\n- Hides item images and the two-stack preview\n- Applies immediately and remembers your preference\n\n";
            descriptionNote = "Quantity Right-Clicks must be enabled.";
        }
        ImGui::Unindent();
        }
        if (optionFilter.Visible(HudOptions::Category::Gameplay,"Persistent Summons","minions next game")) {
        if (ThemeCheckbox("Persistent Summons", &d2rHUDConfig.PersistentSummons))
        {
            SummonPersistence::SetEnabled(d2rHUDConfig.PersistentSummons);

        }
        if (ImGui::IsItemHovered())
        {
            descriptionTitle = "Persistent Summons";
            descriptionText = "- Restores surviving permanent minions in your next game\n"
                "- Supports all classes and custom permanent summons\n"
                "- Timed summons are excluded\n"
                "- Iron Golem retains its native save behavior\n\n";
            descriptionNote = "Requires offline play or the local TCP/IP host's D2RHUD; the host controls persistence.";
            const ImVec2 mousePos = ImGui::GetIO().MousePos;
            ImGui::SetNextWindowPos(ImVec2(mousePos.x + 70.0f, mousePos.y), ImGuiCond_Always);
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + 440.0f * menuScale);
            HudText::TextUnformatted("Restore surviving permanent minions next game. Timed summons are excluded. Iron Golem keeps its native save behavior. Requires offline play or a local TCP/IP host running D2RHUD.");
            ImGui::Separator();
            HudText::TextWrapped("%s", SummonPersistence::Status());
            const auto summonStorageStatus = SummonPersistence::StorageStatus();
            HudText::TextWrapped("%s", summonStorageStatus.c_str());
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
        }
        if (optionFilter.Visible(HudOptions::Category::Gameplay,"D3 Style Whirlwind","hold mouse steer drink belt potions")) {
        if (ThemeCheckbox("D3 Style Whirlwind", &d2rHUDConfig.D3StyleWhirlwind))
        {
            D3Whirlwind::SetEnabled(d2rHUDConfig.D3StyleWhirlwind);

        }
        if (ImGui::IsItemHovered())
        {
            descriptionTitle = "D3 Style Whirlwind";
            descriptionText = "- Hold the assigned mouse button to keep Whirlwind active\n"
                "- Steer continuously by moving the cursor\n"
                "- Click nearby items with the other mouse button to pick them up\n"
                "- Drink belt potions while spinning\n"
                "- Pays the native cast cost once per second\n\n";
            descriptionNote = "Available for your local/offline or TCP/IP host character. Charged item skills retain native behavior.";
            const ImVec2 mousePos = ImGui::GetIO().MousePos;
            ImGui::SetNextWindowPos(ImVec2(mousePos.x + 70.0f, mousePos.y), ImGuiCond_Always);
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + 440.0f * menuScale);
            HudText::TextUnformatted("Hold Whirlwind's assigned mouse button and move the cursor to steer. Click nearby ground items with the other mouse button to pick them up, and drink belt potions while spinning. Uses one native cast cost per second. Available for your local/offline or TCP/IP host character.");
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
        }
        if (!ItemCodes::Installed() && optionFilter.Visible(HudOptions::Category::Advanced,"Extended Itemcodes","legacy four character codes")) {
        if (!ItemCodes::Installed()) {
            drawCheckbox("Extended Itemcodes (legacy)", &d2rHUDConfig.ExtendedItemcodes, "Extended Itemcodes", "- Legacy visual fallback for four-character item codes\n- Uses transmog TXT/JSON mapping when native HD support is unavailable\n\n", "Used when native four-character HD support is unavailable", GetLockInfo(modName, &ModOverrideSettings::ExtendedItemcodes));
        }
        }
        if (optionFilter.Visible(HudOptions::Category::Sound,"Chat Sounds","Chat Sound notifications audio sound path")) {
        drawCheckbox("Chat Sounds", &d2rHUDConfig.ChatSounds, "Chat Sounds", "- Plays a notification sound when other players send chat\n- Does not play for your own messages\n- Controlled by D2RHUD options (not chat panel widget)\n\n", "Toggle this to enable or disable chat notification sounds", nullptr);


        bool chatSoundSelectionChanged = false;
        drawLabeledInput(
            "Chat Sound",
            [&]() {
                chatSoundSelectionChanged = DrawChatSoundSelector(menuScale);
            },
            "Chat Sound",
            "- Sound played when another player sends chat (if Chat Sounds is enabled)\n"
            "- Auto uses the first file in My Filters/sounds, Pandemonium/Sounds, or D2RLAN/Sounds\n"
            "- Pick a listed file or choose Custom path for any .wav, .mp3, or .flac\n\n",
            "Add sounds in D2RLoot Settings > Sounds, or paste a full path for custom",
            false,
            35.0f,
            nullptr
        );
        if (chatSoundSelectionChanged)
        {
            InvalidateChatSoundPathCache();

        }

        }
        if (optionFilter.Visible(HudOptions::Category::Display,"Floating Damage","DPS meter damage numbers")) {
        if (drawCheckbox("Floating Damage", &d2rHUDConfig.FloatingDamage, "Floating Damage", "- Displays floating damage values above monsters\n- Coalesces rapid ticks (poison, fire, etc.)\n- Optional DPS meter with rolling window\n- Customize appearance in the Floating Damage menu\n\n", "Applies immediately and saves automatically. Appearance previews remain available while disabled.", GetLockInfo(modName, &ModOverrideSettings::FloatingDamage)))
        {
            FloatingDamage::SetEnabled(d2rHUDConfig.FloatingDamage);
            settings.FloatingDamage = d2rHUDConfig.FloatingDamage;
            cachedSettings.FloatingDamage = d2rHUDConfig.FloatingDamage;

        }
        }
        if (optionFilter.Visible(HudOptions::Category::Advanced,"HP Rollover Mods","HP Rollover Difficulty HP Rollover Percent health high player counts")) {
        drawCheckbox("HP Rollover Mods", &d2rHUDConfig.HPRolloverMods, "HP Rollover Mods", "- Prevents HP rollovers on high player counts by:\n- Capping the maximum health bonus % applied to monsters\n- Applying damage reduction logic to the Player\n- Scales Reduction % by the calculated rollover amount\n\n", "This feature cannot guarantee no rollovers\nHowever, it should work for mods with retail-ish values", GetLockInfo(modName, &ModOverrideSettings::HPRolloverMods));

        // --- HPRolloverDifficulty ---
        const char* difficultyItems[] = { "Normal or Higher", "Nightmare or Higher", "Hell Only" };
        int difficultyIndex = d2rHUDConfig.HPRolloverDifficulty + 1;

        // label hover detection:
        drawLabeledInput(
            "HP Rollover Difficulty",
            [&]() {
                ImGui::PushItemWidth(250.0f * menuScale);
                BeginFrameBgImageRegion();
                if (HudText::Combo("##HPRolloverDifficulty", &difficultyIndex,
                    difficultyItems, IM_ARRAYSIZE(difficultyItems)))
                {
                    d2rHUDConfig.HPRolloverDifficulty = difficultyIndex - 1;
                }
                EndFrameBgImageRegion();
                ImGui::PopItemWidth();
            },
            "HP Rollover Difficulty", "- Control the applied Difficulty of HP Rollover Mods\n\n", "Only valid if HP Rollover Mods are enabled", false, 35.0f, GetLockInfo(modName, &ModOverrideSettings::HPRolloverDifficulty)
        );

        ImGui::Dummy(ImVec2(0.0f, 3.0f));

        drawLabeledInput(
            "HP Rollover %",
            [&]() {
                ImGui::PushItemWidth(100.0f * menuScale);
                BeginFrameBgImageRegion();
                if (HudText::InputInt("##HPRolloverPercent", &d2rHUDConfig.HPRolloverPercent, 1, 10))
                {
                    d2rHUDConfig.HPRolloverPercent =
                        std::clamp(d2rHUDConfig.HPRolloverPercent, 0, 100);
                }
                EndFrameBgImageRegion();
                ImGui::PopItemWidth();
            },
            "HP Rollover %", "- Controls the maximum amount of Damage reduction\n\n", "Only valid if HP Rollover Mods are enabled", true, 35.0f, GetLockInfo(modName, &ModOverrideSettings::HPRolloverPercent)
        );

        }
        if (optionFilter.Visible(HudOptions::Category::Gameplay,"Sunder Floor","resistance immunity 99")) {
        drawLabeledInput(
            "Sunder Floor",
            [&]() {
                ImGui::PushItemWidth(100.0f * menuScale);
                BeginFrameBgImageRegion();
                HudText::TextUnformatted("99");
                EndFrameBgImageRegion();
                ImGui::PopItemWidth();
            },
            "Sunder Floor", "- Immunity pierce stops at 99 resistance\n- Auras and curses can reduce resistance further\n- Immunity checks use the result before aura/curse reductions\n- TCP/IP shares the highest immunity pierce per element\n\n", "Requires compatible sunder-edited mod files to use\nMore info available at D2RModding Discord", true, 42.0f, GetLockInfo(modName, &ModOverrideSettings::SunderValue)
        );

        }
        if (optionFilter.Visible(HudOptions::Category::Workshop,"Workshop","Survivor Arena Act VI experimental prototypes")) {
        ImGui::Separator();
        const bool workshopOpen = true;
        HudText::TextWrapped("Unfinished prototypes. Use a separate test character.");
        if (ImGui::IsItemHovered() || ImGui::IsItemToggledOpen()) {
            descriptionTitle = "Workshop";
            descriptionText = "Unfinished features for exploring the engine's limits and planning future modding work.\n\n"
                "These prototypes are experiments and may change as development continues.\n\n"
                "- Survivor Arena: an automatic-combat survival experiment\n"
                "- Act VI: a sixth-act campaign and engine expansion prototype\n\n";
            descriptionNote = "Both features default to off. Enable the ones you want to explore; use a separate test character.";
        }
        if (workshopOpen) {
            ImGui::Indent();
            if (ThemeCheckbox("Survivor Arena", &d2rHUDConfig.SurvivorArena)) {
                CubeMap::SetSurvivorsEnabled(d2rHUDConfig.SurvivorArena);

            }
            if (ImGui::IsItemHovered()) {
                descriptionTitle = "Survivor Arena";
                descriptionText = "Unfinished prototype for exploring engine limits and planning future mods.\n\n"
                    "- Approach Charsi in Act I to enter Sanctuary Survivors\n"
                    "- Survive ten minutes with automatic class attacks\n"
                    "- Each class has separate saved arena progress\n"
                    "- Your normal hero returns with their original equipment and skills\n\n";
                descriptionNote = "Defaults off. The host must enable it for entry. Disabling blocks new entries; an active run can still finish or return to town.";
                const auto mouse = ImGui::GetIO().MousePos;
                ImGui::SetNextWindowPos(ImVec2(mouse.x + 70.0f, mouse.y), ImGuiCond_Always);
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + 440.0f * menuScale);
                HudText::TextUnformatted("Unfinished Workshop prototype. Enable to approach Charsi in Act I and explore the Survivor Arena. The host must also enable it. Existing runs can finish after disabling.");
                ImGui::Separator();
                HudText::TextWrapped("%s", CubeMap::Status());
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
            if (!_stricmp(modName.c_str(),"RMD-MP")) {
                if (ThemeCheckbox("Act VI", &d2rHUDConfig.ActVI)) {
                    ExtendedActs::SetRequested(d2rHUDConfig.ActVI);

                }
                if (ImGui::IsItemHovered()) {
                    descriptionTitle="Act VI";
                    descriptionText="Unfinished prototype for exploring engine limits and planning future mods.\n\n- Baal unlocks Act VI in the same difficulty\n- Return to Act VI town after the credits\n- Act VI's final quest unlocks the next difficulty\n\n";
                    descriptionNote="Requires a full D2R restart. Prototype content; use a separate test character.";
                    const auto mouse=ImGui::GetIO().MousePos;
                    ImGui::SetNextWindowPos(ImVec2(mouse.x+70.0f,mouse.y),ImGuiCond_Always);
                    ImGui::BeginTooltip();
                    ImGui::PushTextWrapPos(ImGui::GetCursorPos().x+440.0f*menuScale);
                    HudText::TextUnformatted("Unfinished Workshop prototype. Baal unlocks Act VI; its final quest unlocks the next difficulty. Requires a matching compiled Act VI data pack (or -txt when compiling source tables). Full D2R restart required. Prototype content; use a separate test character.");
                    if(ExtendedActs::Requested()!=ExtendedActs::Active())HudText::TextUnformatted("Restart D2R to apply the selected option.");
                    ImGui::Separator();HudText::TextWrapped("%s",ExtendedActs::Status());
                    ImGui::PopTextWrapPos();ImGui::EndTooltip();
                }
            }
            ImGui::Unindent();
        }

        }
        if(!optionFilter.matches)HudText::TextWrapped("No matching options. Try another search or clear the search field.");
        if(before!=values())applyAndSave();

        static std::string lastDescriptionTitle,lastDescriptionText,lastDescriptionNote;
        const bool descriptionChanged=descriptionTitle!=lastDescriptionTitle || descriptionText!=lastDescriptionText || descriptionNote!=lastDescriptionNote;
        lastDescriptionTitle=descriptionTitle;lastDescriptionText=descriptionText;lastDescriptionNote=descriptionNote;
        HudOptions::BeginDescription(optionPanels,descriptionChanged);
        HudMenu::Panel();
        const float descriptionStart=ImGui::GetCursorPosX();
        const float descriptionWidth=ImGui::GetContentRegionAvail().x;
        fontIndex=3;
        fontSize=(fontIndex>=0 && fontIndex<io.Fonts->Fonts.Size) ? io.Fonts->Fonts[fontIndex] : nullptr;
        if(fontSize)ImGui::PushFont(fontSize);
        if(!descriptionTitle.empty()) {
            const float titleWidth=HudText::CalcTextSize(descriptionTitle.c_str()).x;
            ImGui::SetCursorPosX(descriptionStart+(std::max)(0.0f,(descriptionWidth-titleWidth)*0.5f));
            ImGui::PushTextWrapPos(descriptionStart+descriptionWidth);
            HudText::TextColored(ImGui::GetStyle().Colors[ImGuiCol_CheckMark],"%s",descriptionTitle.c_str());
            ImGui::PopTextWrapPos();
        }
        if(fontSize)ImGui::PopFont();
        ImGui::SetCursorPosX(descriptionStart);
        ImGui::PushTextWrapPos(descriptionStart+descriptionWidth);
        if(!descriptionText.empty())HudText::TextWrapped("%s",descriptionText.c_str());
        if(!descriptionNote.empty())HudText::TextColored(ImVec4(1.0f,0.65f,0.0f,1.0f),"%s",descriptionNote.c_str());
        ImGui::PopTextWrapPos();
        HudOptions::EndPanels();
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0.0f, 3.0f));

        HudText::TextDisabled("Changes apply and save automatically.");
        if(!saveStatusMessage.empty())HudText::TextColored(saveStatusColor,"%s",saveStatusMessage.c_str());
        if(saveStatusMessage=="Could not save settings. Try again."&&ThemeButton("Retry save"))applyAndSave();
        if(ExtendedActs::Requested()!=ExtendedActs::Active())HudText::TextWrapped("Restart D2R to apply the Act VI selection.");

        ImGui::PopStyleColor();
        HudMenu::End();
    }
    else {
        ImGui::PopStyleColor();
        HudMenu::End();
        PopFontSafe(3);
    }
}
