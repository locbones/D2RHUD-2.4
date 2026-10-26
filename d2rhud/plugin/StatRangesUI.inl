void DrawSettingsPanel(float scale) {
    namespace UI=HudSettings;
    UI::Style form(scale);
    auto preview=[&]{
        UI::BeginSection("RangePreview","Item preview",nullptr,scale,true);
        HudText::TextUnformatted("+25 to Strength");
        const auto text=FormatAnnotation({"[20-30]",true,{{20,30}}},config.format);
        const auto& fonts=ImGui::GetIO().Fonts->Fonts;
        auto font=fonts.Size>2?fonts[2]:ImGui::GetFont();
        const float size=config.size*scale;
        const auto extent=HudText::CalcTextSizeA(font,size,FLT_MAX,0,text.c_str());
        if(extent.x+config.spacing*scale<ImGui::GetContentRegionAvail().x)
            ImGui::SameLine(0,config.spacing*scale);
        const auto pos=ImGui::GetCursorScreenPos();const float width=ImGui::GetContentRegionAvail().x;
        const auto wrapped=HudText::CalcTextSizeA(font,size,FLT_MAX,width,text.c_str());
        HudText::AddText(ImGui::GetWindowDrawList(),font,size,pos,ImGui::ColorConvertFloat4ToU32(config.color),text.c_str(),nullptr,width);
        ImGui::Dummy(wrapped);
        UI::EndSection(scale);
    };
    auto activation=[&]{
        UI::BeginSection("RangeActivation","Visibility","Choose when item ranges appear.",scale);
        int mode=config.hotkey?1:0;
        if(UI::Combo("Show ranges",&mode,"Always\0With a hotkey\0",scale)){config.hotkey=mode==1;ResetActivation();}
        if(!config.hotkey)HudText::Checkbox("Enable stat ranges",&config.enabled);
        else {
            int behavior=config.hold?1:0;
            if(UI::Combo("Hotkey action",&behavior,"Toggle with each press\0Hold to show\0",scale)){config.hold=behavior==1;ResetActivation();}
            UI::Note("Assign Show Stat Ranges in Hotkeys & Commands.");
            if(UI::Disclosure("How the hotkey works")) {
                UI::Note("Toggle starts off when you enter a game. Hold shows ranges only while the key is down.");
                UI::Note("Item tooltips preview ranges while this page is open. Previewing does not change your hotkey toggle.");
            }
        }
        UI::EndSection(scale);
    };
    auto appearance=[&]{
        UI::BeginSection("RangeAppearance","Appearance","Style the range beside each item stat.",scale);
        UI::Color("Range color",&config.color.x,scale);
        UI::Slider("Text size",&config.size,8,40,"%.0f px",scale);
        UI::Slider("Gap after the stat",&config.spacing,0,100,"%.0f px",scale);
        HudText::Checkbox("Include fixed rolls (min = max)",&config.fixed);
        UI::EndSection(scale);
    };
    if(ImGui::BeginTabBar("StatRangeSections")) {
        if(HudText::BeginTabItem("Display")) {
            ImGui::Dummy({0,10*scale});preview();
            const bool columns=ImGui::GetContentRegionAvail().x>=760*scale;
            if(columns&&ImGui::BeginTable("RangeSettingsColumns",2,ImGuiTableFlags_SizingStretchSame)) {
                ImGui::TableNextColumn();activation();ImGui::TableNextColumn();appearance();ImGui::EndTable();
            }else {activation();appearance();}
            ImGui::EndTabItem();
        }
        if(HudText::BeginTabItem("Format")) {
            ImGui::Dummy({0,10*scale});preview();
            UI::BeginSection("RangeFormat","Range format","Choose how the minimum and maximum are written.",scale);
            UI::Input("Format",config.format,sizeof(config.format),scale);
            if(!ValidRangeFormat(config.format))HudText::TextWrapped("Enter exactly two %%d placeholders. The preview uses [%%d-%%d] until this is corrected.");
            if(HudText::Button("Reset format"))strcpy_s(config.format,DefaultRangeFormat);
            if(UI::Disclosure("Formatting reference")) {
                UI::Note("The first %d is the minimum; the second is the maximum. Use %% for a percent sign.");
                UI::Note("Example: [Min: %d, Max: %d]");
            }
            UI::EndSection(scale);ImGui::EndTabItem();
        }
        if(HudText::BeginTabItem("Help & diagnostics")) {
            ImGui::Dummy({0,10*scale});
            UI::BeginSection("RangeHelp","Understanding ranges",nullptr,scale);
            HudText::TextWrapped("Ranges show the possible rolls for an item's unique, set and affix properties.");
            UI::Note("Contributions to the same stat or skill are added together. Set bonuses appear separately. Combined damage lines separate each range with /.");
            UI::EndSection(scale);
            UI::BeginSection("RangeDiagnostics","Troubleshooting","Use a report when an item is missing its expected ranges.",scale);
            UI::Note(status.c_str());
            HudText::TextWrapped("Hover an identified item with ranges enabled, then export its report.");
            if(HudText::Button("Export diagnostic report"))SaveDiagnosticReport();
            if(!diagnosticFile.empty())UI::Note(diagnosticFile.c_str());
            if(UI::Disclosure("Technical details"))
                UI::Note(TooltipText::LimitsExtended()?"Extended stat descriptions: 16,384 bytes.":"Native stat descriptions: 1,024 bytes. The extension is unavailable for this executable.");
            UI::EndSection(scale);ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}
