// Included after the HUD theme/config helpers are declared.
static void RenderLootFilterEditor()
{
    static unsigned revision = ~0u;
    static int selected = -1;
    static std::string draft, fullSource, query;
    static bool draftDirty = false, sourceDirty = false, selectRules = true;
    if (revision != g_LootConfigRevision) {
        revision = g_LootConfigRevision;
        selected = -1; draft.clear(); draftDirty = sourceDirty = false;
        fullSource = g_LootConfigDocument.source;
        selectRules = true;
    }
    auto commitDraft = [&]() {
        if (draftDirty && selected >= 0 && selected < int(g_LootFilterRules.size())) {
            auto& rule = g_LootFilterRules[selected]; rule.rawLua = draft;
            rule.fields = LootFilterConfig::Builder::RuleFields(draft);
            rule.comment.clear();
            for (const auto& field : rule.fields) if (field.first == "label") rule.comment = LootFilterConfig::DecodeString(field.second);
        }
        draftDirty = false;
    };
    if (ThemeButton("Save and apply")) {
        commitDraft();
        if (sourceDirty) {
            g_LootConfigStatus = "Save or discard the complete source draft in the Source tab first.";
            ImGui::OpenPopup("FilterSaveStatus");
        } else if (SaveLootFilterConfig(GetLootFilterConfigPath())) {
            itemFilter->ReloadGameFilter();
            LoadLootFilterConfig(GetLootFilterConfigPath());
        } else ImGui::OpenPopup("FilterSaveStatus");
    }
    ImGui::SameLine();
    if (ThemeButton("Reload from disk")) {
        LoadLootFilterConfig(GetLootFilterConfigPath());
        return;
    }
    // Keep routine file/status detail available without taking another row.
    ImGui::SameLine();
    if(ThemeButton("Status"))ImGui::OpenPopup("FilterSaveStatus");
    if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",g_LootConfigStatus.c_str());
    if(ImGui::BeginPopup("FilterSaveStatus")){ImGui::PushTextWrapPos(ImGui::GetFontSize()*35);ImGui::TextWrapped("%s",g_LootConfigStatus.c_str());ImGui::PopTextWrapPos();ImGui::EndPopup();}
    if(!g_LootConfigDocument.error.empty())ImGui::TextWrapped("%s",g_LootConfigDocument.error.c_str());
    bool rulesDirty = g_LootConfigOriginalRules.size() != g_LootFilterRules.size();
    if (!rulesDirty) for (size_t i = 0; i < g_LootFilterRules.size(); ++i)
        if (g_LootFilterRules[i].rawLua != g_LootConfigOriginalRules[i]) { rulesDirty = true; break; }
    if (draftDirty || sourceDirty || rulesDirty || g_LuaVariables != g_LootConfigOriginalValues)
        {ImGui::SameLine();ImGui::TextColored({.86f,.72f,.4f,1},"Unsaved changes");}
    if (!ImGui::BeginChild("FilterEditorBody",ImVec2(0,0))) { ImGui::EndChild(); return; }
    if (!ImGui::BeginTabBar("LootFilterEditorTabs")) { ImGui::EndChild(); return; }
    if (ImGui::BeginTabItem("Settings")) {
        ImGui::TextWrapped("Choose filter options here. Custom expressions remain available in the Source tab.");
        ImGui::BeginDisabled(!g_LootConfigDocument.error.empty() || sourceDirty);
        struct Setting { const char* group; const char* key; const char* initial; const char* help; };
        static const Setting settings[] = {
            {"Filter", "reload", "", "Message printed when the filter reloads; supports color tags."},
            {"Filter", "language", "enUS", "Text language; translations fall back to enUS."},
            {"Filter", "filter_level", "1", "Selected numeric level."},
            {"Filter", "filter_titles", "{\"Early Game\", \"End Game\"}", "Lua list of level titles; single/double quotes and multiline tables work."},
            {"Filter", "allowOverrides", "false", "Load override_rules.lua once per reload, with overrides.lua as a legacy fallback."},
            {"Filter", "hide_mismatches", "false", "Hide unmatched ground items only after every rule is checked."},
            {"Audio", "audioPlayback", "false", "Enable text to speech or audio files through the D2RLAN/Pandemonium TTS service."},
            {"Audio", "audioVoice", "1", "Numeric installed Windows voice ID used by the TTS service."},
            {"Diagnostics", "debug", "false", "false, true (matches), or {mode=\"all\", code=\"rin\", rule=1, limit=100}. Modes: matches, failures, all."},
            {"Diagnostics", "strict", "false", "Reject a reload if any rule fails validation; the previous working filter stays active."},
        };
        std::string group;
        for (const auto& setting : settings) {
            if (group != setting.group) { group = setting.group; ImGui::SeparatorText(setting.group); }
            ImGui::PushID(setting.key);
            auto it = g_LuaVariables.find(setting.key);
            bool present = it != g_LuaVariables.end();
            static const std::map<std::string,std::string> labels = {{"reload","Reload message"},{"language","Language"},{"filter_level","Active filter level"},{"filter_titles","Filter level names"},{"allowOverrides","Include the mod's override rules"},{"hide_mismatches","Hide unmatched ground items"},{"audioPlayback","Enable speech and sounds"},{"audioVoice","Windows voice number"},{"debug","Diagnostic messages"},{"strict","Keep the previous filter if a rule is invalid"}};
            ImGui::TextUnformatted(labels.at(setting.key).c_str());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", setting.help);
            if (!present) {
                ImGui::SameLine();
                if (ThemeButton("Add option")) g_LuaVariables[setting.key] = setting.initial;
            } else {
                const std::string key = setting.key;
                auto& value = it->second;
                bool custom = false;
                ImGui::SetNextItemWidth(-1);
                if (key == "reload") LootFilterConfig::InputText("##setting", &value);
                else if (key == "language") {
                    std::vector<LootFilterConfig::Builder::Option> languages = {{"English","enUS"},{"French","frFR"},{"German","deDE"},{"Spanish","esES"},{"Italian","itIT"},{"Polish","plPL"},{"Russian","ruRU"},{"Korean","koKR"},{"Japanese","jaJP"},{"Chinese (traditional)","zhTW"},{"Chinese (simplified)","zhCN"},{"Portuguese","ptBR"},{"Spanish (Latin America)","esMX"}};
                    LootFilterConfig::Builder::UI::Choose("##setting",value,languages);
                } else if (key == "filter_level" || key == "audioVoice") {
                    int number = 0;
                    if (LootFilterConfig::Builder::Integer(value,number)) { if (ImGui::InputInt("##setting",&number)) value=std::to_string((std::max)(key=="filter_level"?1:0,number)); }
                    else custom=true;
                } else if (key == "filter_titles") {
                    std::vector<std::string> names;
                    if (LootFilterConfig::Builder::List(value,names)) {
                        bool edited=false;
                        for(size_t i=0;i<names.size();++i){ImGui::PushID(int(i));ImGui::SetNextItemWidth(-1);edited|=LootFilterConfig::InputText("##name",&names[i]);ImGui::PopID();}
                        if(ThemeButton("Add filter level")){names.push_back("New level");edited=true;}
                        if(edited){value="{";for(size_t i=0;i<names.size();++i){if(i)value+=", ";value+=LootFilterConfig::Quote(names[i]);}value+="}";}
                    } else custom=true;
                } else if (key == "debug") {
                    int mode=value=="false"?0:value=="true"?1:-1;
                    if(mode<0)custom=true;else if(ImGui::Combo("##setting",&mode,"Off\0Show matching rules\0"))value=mode?"true":"false";
                } else if (value == "true" || value == "false") {
                    bool checked=value=="true";if(ImGui::Checkbox("##setting",&checked))value=checked?"true":"false";
                } else custom=true;
                if(custom){ImGui::TextDisabled("Custom value preserved.");ImGui::SameLine();if(ThemeButton("Use standard option"))value=setting.initial;}
            }
            ImGui::PopID();
        }
        ImGui::EndDisabled();
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Rules", nullptr, selectRules ? ImGuiTabItemFlags_SetSelected : 0)) {
        selectRules = false;
        ImGui::BeginDisabled(!g_LootConfigDocument.error.empty() || sourceDirty);
        const float paneHeight=(std::max)(100.f,ImGui::GetContentRegionAvail().y-5.f);
        const bool columns=ImGui::BeginTable("RuleBuilderLayout",2,ImGuiTableFlags_BordersInnerV);
        if(columns){ImGui::TableSetupColumn("Rule list",ImGuiTableColumnFlags_WidthFixed,(std::min)(280.f*ImGui::GetFontSize()/18.f,ImGui::GetContentRegionAvail().x*.26f));ImGui::TableSetupColumn("Rule builder",ImGuiTableColumnFlags_WidthStretch);ImGui::TableNextColumn();}
        ImGui::BeginChild("RuleLibrary",{0,paneHeight});
        LootFilterConfig::Builder::UI::Banner(LootFilterConfig::Builder::UI::Symbol::Rules,"Your rules");
        if (ThemeButton("+ Add rule",{ImGui::GetContentRegionAvail().x,0})) {
            commitDraft();
            LootFilterRule rule;
            rule.comment = "New rule";
            rule.rawLua = "\n        {\n            label = \"New rule\",\n            codes = \"allitems\",\n            location = \"onground\",\n            suffix = \" Keep\",\n        }";
            g_LootFilterRules.push_back(rule);
            selected = int(g_LootFilterRules.size()) - 1; draft = rule.rawLua;
        }
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##named_rule", "Named unique / set...")) {
            static std::string namedQuery;
            LootFilterConfig::InputTextWithHint("##search_named", "Search item names...", &namedQuery);
            ImGui::BeginChild("named_items",ImVec2(0,210));
            const auto& catalog=GetLootFilterBuilderCatalog();
            for(const auto& item:catalog.namedItems){
                std::string haystack=item.label,needle=namedQuery;
                std::transform(haystack.begin(),haystack.end(),haystack.begin(),[](unsigned char c){return char(std::tolower(c));});
                std::transform(needle.begin(),needle.end(),needle.begin(),[](unsigned char c){return char(std::tolower(c));});
                if(!needle.empty()&&haystack.find(needle)==std::string::npos)continue;
                ImGui::PushID((item.quality+std::to_string(item.index)).c_str());
                if(ImGui::Selectable((item.label+" ("+item.quality+")").c_str())){
                    commitDraft();LootFilterRule rule;rule.comment=item.label;
                    rule.rawLua="\n        {label="+LootFilterConfig::Quote(item.label)+",codes="+LootFilterConfig::Quote(item.code)+",quality="+LootFilterConfig::Quote(item.quality)+",index="+std::to_string(item.index)+",location=\"onground\",suffix=\" Keep\"}";
                    g_LootFilterRules.push_back(rule);selected=int(g_LootFilterRules.size())-1;draft=rule.rawLua;ImGui::CloseCurrentPopup();
                }
                ImGui::PopID();
            }
            if(catalog.namedItems.empty())ImGui::TextWrapped("%s",catalog.loaded?"No named item list is available.":"Loading item names...");
            ImGui::EndChild();ImGui::EndCombo();
        }
        ImGui::BeginDisabled(selected<0);
        ImGui::SetNextItemWidth(-1);
        if(ImGui::BeginCombo("##rule_tools","Rule actions...")){
            if (ImGui::Selectable("Duplicate")) {
                commitDraft(); auto copy = g_LootFilterRules[selected];
                g_LootFilterRules.insert(g_LootFilterRules.begin() + selected + 1, copy);
                ++selected; draft = copy.rawLua;
            }
            if (ImGui::Selectable("Remove")) {
                g_LootFilterRules.erase(g_LootFilterRules.begin() + selected);
                selected = -1; draft.clear(); draftDirty = false;
            }
            if (selected >= 0) {
                if (ImGui::Selectable("Move up",false,selected>0?0:ImGuiSelectableFlags_Disabled) && selected > 0) {
                    commitDraft(); std::swap(g_LootFilterRules[selected], g_LootFilterRules[selected - 1]); --selected;
                }
                if (ImGui::Selectable("Move down",false,selected+1<int(g_LootFilterRules.size())?0:ImGuiSelectableFlags_Disabled) && selected + 1 < int(g_LootFilterRules.size())) {
                    commitDraft(); std::swap(g_LootFilterRules[selected], g_LootFilterRules[selected + 1]); ++selected;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::EndDisabled();
        ImGui::SetNextItemWidth(-1);
        LootFilterConfig::InputTextWithHint("##rule_search", "Search rules...", &query);
        if(ImGui::IsItemHovered())ImGui::SetTooltip("Rules run from top to bottom. Hide actions stop evaluation.");
        if(ImGui::BeginChild("RuleList",{0,0},true))for(size_t i=0;i<g_LootFilterRules.size();++i){
            const auto& rule=g_LootFilterRules[i];
            if(!query.empty()&&rule.rawLua.find(query)==std::string::npos&&rule.comment.find(query)==std::string::npos)continue;
            std::string name=rule.comment;
            if(selected==int(i))for(const auto& field:LootFilterConfig::Builder::RuleFields(draft))if(field.first=="label")name=LootFilterConfig::DecodeString(field.second);
            if(name.empty())for(const auto& field:rule.fields)if(field.first=="codes"||field.first=="code"){name=field.second;break;}
            if(name.empty())name="Untitled rule";
            const std::string label=std::to_string(i+1)+". "+name+"###loot_rule_"+std::to_string(i);
            if(ImGui::Selectable(label.c_str(),selected==int(i),0,{0,ImGui::GetTextLineHeight()+10})){
                commitDraft();selected=int(i);draft=rule.rawLua;
            }
            if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",name.c_str());
        }
        ImGui::EndChild();ImGui::EndChild();
        if(columns)ImGui::TableNextColumn();
        ImGui::BeginChild("RuleWorkspace",{0,paneHeight});
        if(selected>=0&&selected<int(g_LootFilterRules.size())){
            if(ImGui::BeginTabBar("RuleWorkspaceViews")){
                for(int view=0;view<4;++view){
                    const char* names[]{"Conditions","Actions","Preview","Rule code"};
                    if(ImGui::BeginTabItem(names[view])){
                        ImGui::BeginChild(view==2?"RulePreviewPane":"RuleDetails",{0,0});
                        if(view<2){if(LootFilterConfig::Builder::UI::Render(draft,g_LootConfigStatus,GetLootFilterBuilderCatalog(),view))draftDirty=true;}
                        else if(view==2)LootFilterConfig::Preview::UI::Render(draft,g_LuaVariables,GetLootFilterBuilderCatalog(),GetLootFilterPreviewEngine());
                        else {
                            LootFilterConfig::Builder::UI::Banner(LootFilterConfig::Builder::UI::Symbol::Code,"Advanced rule code","Changes here are reflected in the builder.");
                            if(ThemeButton("Copy code"))ImGui::SetClipboardText(draft.c_str());
                            if(LootFilterConfig::InputTextMultiline("##rule_draft",&draft,{-1,(std::max)(100.f,ImGui::GetContentRegionAvail().y-45)},ImGuiInputTextFlags_AllowTabInput))draftDirty=true;
                            if(ImGui::CollapsingHeader("Field reference"))for(const auto& field:LootFilterConfig::Fields){ImGui::TextWrapped("%s = %s",field.name,field.example);ImGui::TextWrapped("%s",field.help);}
                        }
                        ImGui::EndChild();ImGui::EndTabItem();
                    }
                }
                ImGui::EndTabBar();
            }
        }else{
            LootFilterConfig::Builder::UI::Banner(LootFilterConfig::Builder::UI::Symbol::Conditions,"Build your filter","Select a rule or create one to get started.");
            ImGui::TextWrapped("1. Choose which items should match.\n\n2. Add colors, labels or other actions.\n\n3. Check sample items in Preview, then Save and apply.");
        }
        ImGui::EndChild();
        if(columns)ImGui::EndTable();
        ImGui::EndDisabled();
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Styles")) {
        ImGui::BeginDisabled(!g_LootConfigDocument.error.empty() || sourceDirty);
        ImGui::TextWrapped("Reusable styles supply actions. Rule values (including false) override a style. Use style = \"valuable\" in a rule.");
        auto it = g_LuaVariables.find("styles");
        if (it == g_LuaVariables.end()) {
            if (ThemeButton("Add styles table")) g_LuaVariables["styles"] = "{\n    valuable = { border = {240,160,40,230,2}, suffix = \" {gold}Keep\" },\n}";
        } else LootFilterConfig::InputTextMultiline("##styles", &it->second, ImVec2(-1, 250), ImGuiInputTextFlags_AllowTabInput);
        ImGui::TextWrapped("Name presets: Rainbow, Rainbow Group, Cotton Candy, Frost Wave, Toxic Fog, Open Flames, Christmas; static variants also exist for all except Christmas.");
        ImGui::TextWrapped("Background presets: Rainbow, Ocean Drift, Fading Ember, Midnight Memory, Verdant Bloom, Sapphire Sonnet, Tamarillo Glow.");
        ImGui::EndDisabled();
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Diagnostics")) {
        if (ThemeButton("Validate Lua syntax")) {
            commitDraft();
            try {
                std::map<std::string,std::string> edits;
                for (const auto& pair : g_LuaVariables) {
                    auto it = g_LootConfigOriginalValues.find(pair.first);
                    if (it == g_LootConfigOriginalValues.end() || it->second != pair.second) edits[pair.first] = FormatLuaValue(pair.first, pair.second);
                }
                std::vector<std::string> rules;
                for (const auto& rule : g_LootFilterRules) rules.push_back(rule.rawLua);
                std::string candidate = sourceDirty ? fullSource : g_LootConfigDocument.Update(edits, rules, rules != g_LootConfigOriginalRules);
                std::string error;
                g_LootConfigStatus = LootFilterConfig::ValidateSyntax(candidate, error) ? "Lua syntax is valid. Save and apply to compile conditions and refresh runtime diagnostics." : error;
            } catch (const std::exception& e) { g_LootConfigStatus = e.what(); }
        }
        ImGui::TextWrapped("%s", ItemFilter::GetDiagnosticsText().c_str());
        ImGui::TextWrapped("Debug output is capped per reload. Use debug = {mode=\"failures\", code=\"rin\", rule=1, limit=30}. Invalid rules are disabled and reported; strict=true rejects an invalid reload. Missing optional TTS service retries at most every five seconds.");
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Source")) {
        ImGui::TextWrapped("Complete author source, including local variables, loops and generated rules. Saving replaces the file after syntax validation and keeps a .bak. Reload from disk discards drafts.");
        if (LootFilterConfig::InputTextMultiline("##complete_filter_source", &fullSource, ImVec2(-1, 360), ImGuiInputTextFlags_AllowTabInput)) sourceDirty = true;
        if (ThemeButton("Save source and apply")) {
            std::string error;
            if (LootFilterConfig::Save(GetLootFilterConfigPath(), g_LootConfigDocument.source, fullSource, error)) {
                LoadLootFilterConfig(GetLootFilterConfigPath());
                itemFilter->ReloadGameFilter();
            } else g_LootConfigStatus = error;
        }
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
    ImGui::EndChild();
}

