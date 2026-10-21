// Included by D2RHUD.cpp and the headless editor regression harness.
static LootFilterConfig::Document g_LootConfigDocument;
static std::unordered_map<std::string, std::string> g_LootConfigOriginalValues;
static std::vector<std::string> g_LootConfigOriginalRules;
static std::string g_LootConfigStatus;
static unsigned g_LootConfigRevision = 0;

void LoadLootFilterConfig(const std::string& path)
{
    g_LuaVariables.clear();
    g_LuaVariableComments.clear();
    g_LootFilterHeader = {};
    g_LootFilterRules.clear();
    g_LootConfigOriginalValues.clear();
    g_LootConfigOriginalRules.clear();
    ++g_LootConfigRevision;
    std::string source;
    if (!LootFilterConfig::Read(path, source)) {
        g_LootConfigDocument = {};
        g_LootConfigStatus = "Unable to read " + path;
        return;
    }
    std::string syntaxError;
    if (!LootFilterConfig::ValidateSyntax(source, syntaxError)) {
        g_LootConfigDocument.source = source;
        g_LootConfigDocument.error = syntaxError;
        g_LootConfigStatus = syntaxError;
        return;
    }
    const bool parsed = g_LootConfigDocument.Parse(source);
    std::istringstream header(source);
    std::string line;
    while (std::getline(header, line)) {
        auto marker = line.find("--- Filter Title:");
        if (marker != std::string::npos) {
            g_LootFilterHeader.Title = line.substr(marker + 17);
            break;
        }
    }
    if (!parsed) { g_LootConfigStatus = g_LootConfigDocument.error; return; }
    for (const auto& field : g_LootConfigDocument.fields) {
        if (field.key == "rules") continue;
        const std::string key = field.key == "Debug" ? "debug" : field.key;
        if (field.key == "Debug" && g_LootConfigDocument.Find("debug")) continue;
        g_LuaVariables[key] = LootFilterConfig::DecodeString(g_LootConfigDocument.Value(field));
    }
    g_LootConfigOriginalValues = g_LuaVariables;
    g_LootConfigOriginalRules = g_LootConfigDocument.RuleSources();
    for (size_t i = 0; i < g_LootConfigOriginalRules.size(); ++i) {
        LootFilterRule rule;
        rule.rawLua = g_LootConfigOriginalRules[i];
        rule.fields = g_LootConfigDocument.RuleFields(i);
        for (const auto& field : rule.fields)
            if (field.first == "label") rule.comment = LootFilterConfig::DecodeString(field.second);
        g_LootFilterRules.push_back(std::move(rule));
    }
    g_LootConfigStatus = "Loaded " + std::to_string(g_LootFilterRules.size()) + " authored rules.";
    if (g_LootConfigDocument.generated)
        g_LootConfigStatus += " This config also uses Lua code; generated rules stay in its source.";
}

static std::string FormatLuaValue(const std::string& key, const std::string& value)
{
    if (key == "reload" || key == "language") return LootFilterConfig::Quote(value);
    const auto* field = g_LootConfigDocument.Find(key);
    if (field && g_LootConfigDocument.IsString(g_LootConfigDocument.Value(*field))) return LootFilterConfig::Quote(value);
    return value; // All other controls edit a Lua expression; preserve tables, booleans and references.
}

bool SaveLootFilterConfig(const std::string& path)
{
    try {
        std::map<std::string, std::string> edits;
        for (const auto& pair : g_LuaVariables) {
            auto old = g_LootConfigOriginalValues.find(pair.first);
            if (old != g_LootConfigOriginalValues.end() && old->second == pair.second) continue;
            std::string key = pair.first;
            if (key == "debug" && !g_LootConfigDocument.Find("debug") && g_LootConfigDocument.Find("Debug")) key = "Debug";
            edits[key] = FormatLuaValue(pair.first, pair.second);
        }
        std::vector<std::string> rules;
        for (const auto& rule : g_LootFilterRules) rules.push_back(rule.rawLua);
        const bool changedRules = rules != g_LootConfigOriginalRules;
        const std::string source = g_LootConfigDocument.Update(edits, rules, changedRules);
        const bool changed = source != g_LootConfigDocument.source;
        std::string error;
        if (!LootFilterConfig::Save(path, g_LootConfigDocument.source, source, error)) {
            g_LootConfigStatus = error; return false;
        }
        g_LootConfigDocument.Parse(source);
        g_LootConfigOriginalValues = g_LuaVariables;
        g_LootConfigOriginalRules = rules;
        g_LootConfigStatus = changed ? "Saved. Previous config is in lootfilter_config.lua.bak." : "No changes to save.";
        return true;
    } catch (const std::exception& error) {
        g_LootConfigStatus = error.what(); return false;
    }
}

