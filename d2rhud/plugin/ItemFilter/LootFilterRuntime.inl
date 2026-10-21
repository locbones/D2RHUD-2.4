// Transactional runtime loading; shared with the regression harness.
static std::string g_FilterDiagnosticsText = "No filter has been loaded.";
static std::string g_FilterRuntimeError, g_FilterCombinedDiagnostics;
static ULONGLONG g_FilterNextErrorReport = 0;
static unsigned g_FilterSuppressedErrors = 0;

const std::string& ItemFilter::GetDiagnosticsText() {
    g_FilterCombinedDiagnostics = g_FilterDiagnosticsText;
    if (!g_FilterRuntimeError.empty()) {
        g_FilterCombinedDiagnostics += "\nLast runtime error: " + g_FilterRuntimeError;
        if (g_FilterSuppressedErrors) g_FilterCombinedDiagnostics += " (" + std::to_string(g_FilterSuppressedErrors) + " repeats suppressed)";
    }
    return g_FilterCombinedDiagnostics;
}

static void ReportFilterRuntimeError(const char* message) {
    const ULONGLONG now = GetTickCount64();
    if (g_FilterRuntimeError == message && now < g_FilterNextErrorReport) {
        ++g_FilterSuppressedErrors; return;
    }
    g_FilterRuntimeError = message;
    PrintGameMessage(("[LootFilter runtime] " + g_FilterRuntimeError).c_str());
    g_FilterNextErrorReport = now + 5000;
    g_FilterSuppressedErrors = 0;
}

bool LoadScript() {
    // Publish new callbacks only after the complete engine/config has loaded.
    // A syntax error or strict validation failure keeps the last working filter.
    auto env = sol::environment(lua, sol::create, lua.globals());
    const std::string failureStatus = applyFilter.valid()
        ? "Reload failed; previous filter retained.\n" : "Filter load failed; no filter is active.\n";
    const std::string fullPath = std::filesystem::absolute("lootfilter.lua").string();
    sol::load_result loaded = lua.load_file(fullPath);
    if (!loaded.valid()) {
        sol::error error = loaded;
        g_FilterDiagnosticsText = failureStatus + error.what();
        PrintGameMessage(g_FilterDiagnosticsText.c_str());
        return false;
    }
    sol::protected_function chunk = loaded;
    sol::set_environment(env, chunk);
    sol::protected_function_result result = chunk();
    if (!result.valid()) {
        sol::error error = result;
        g_FilterDiagnosticsText = failureStatus + error.what();
        PrintGameMessage(g_FilterDiagnosticsText.c_str());
        return false;
    }
    sol::protected_function candidate = env["ApplyFilter"];
    if (!candidate.valid()) {
        g_FilterDiagnosticsText = failureStatus + "Engine did not define ApplyFilter.";
        PrintGameMessage(g_FilterDiagnosticsText.c_str());
        return false;
    }
    applyFilter = candidate;
    getWelcomeMessage = env["GetWelcomeMessage"];
    g_FilterRuntimeError.clear();
    g_FilterSuppressedErrors = 0;
    g_FilterNextErrorReport = 0;
    g_FilterDiagnosticsText = "Filter loaded.";
    sol::protected_function getDiagnostics = env["GetFilterDiagnostics"];
    if (getDiagnostics.valid()) {
        sol::protected_function_result details = getDiagnostics();
        if (details.valid()) {
            sol::table table = details;
            g_FilterDiagnosticsText = "Engine " + table.get_or<std::string>("version", "unknown") + ": "
                + std::to_string(table["rules"].get_or(0)) + " compiled rules; "
                + std::to_string(table["disabled"].get_or(0)) + " disabled rules.";
            for (const char* category : { "errors", "warnings" }) {
                sol::object object = table[category];
                if (object.is<sol::table>()) {
                    sol::table entries = object.as<sol::table>();
                    for (const auto& pair : entries) {
                        if (pair.second.is<std::string>()) g_FilterDiagnosticsText += "\n" + std::string(category) + ": " + pair.second.as<std::string>();
                    }
                }
            }
        }
    }
    return true;
}

