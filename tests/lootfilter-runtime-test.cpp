#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <Windows.h>

struct ItemFilter { static const std::string& GetDiagnosticsText(); };
static sol::state lua;
static sol::protected_function applyFilter, getWelcomeMessage;
static std::vector<std::string> messages;
static void PrintGameMessage(const char* message) { messages.push_back(message); }
#include "../d2rhud/plugin/ItemFilter/LootFilterRuntime.inl"

static void Check(bool passed, const std::string& message) { if (!passed) throw std::runtime_error(message); }
static void Write(const char* path, const std::string& source) { std::ofstream f(path,std::ios::binary); f << source; }
static std::string Welcome() { sol::protected_function_result value=getWelcomeMessage(); Check(value.valid(),"Welcome callback failed"); return value.get<std::string>(); }
int main(int argc, char** argv) {
    if (argc!=2) return 2;
    const auto previousDirectory=std::filesystem::current_path();
    const auto directory=std::filesystem::temp_directory_path()/("d2rhud-runtime-test-"+std::to_string(GetCurrentProcessId()));
    try {
        std::filesystem::create_directories(directory);
        std::filesystem::copy_file(std::filesystem::u8path(argv[1]),directory/"lootfilter.lua");
        std::filesystem::current_path(directory);
        lua.open_libraries(sol::lib::base,sol::lib::package,sol::lib::table,sol::lib::coroutine,sol::lib::string,sol::lib::math,sol::lib::io,sol::lib::os);
        lua["print"]=[](const std::string& message){messages.push_back(message);};
        Write("lootfilter_config.lua","return {");
        Check(!LoadScript() && !applyFilter.valid() && ItemFilter::GetDiagnosticsText().find("no filter is active")!=std::string::npos,"Initial failure was not reported accurately");
        Write("lootfilter_config.lua","return {reload='working',rules={{codes='rin',suffix=' good'}}}");
        Check(LoadScript(),"Initial runtime load failed");
        Check(Welcome()=="working","Wrong initial welcome");
        Check(!lua["ApplyFilter"].valid(),"Engine escaped its private environment");
        Check(ItemFilter::GetDiagnosticsText().find("1 compiled rules")!=std::string::npos,"Runtime diagnostics absent");
        const auto count = messages.size();
        ReportFilterRuntimeError("synthetic error");
        ReportFilterRuntimeError("synthetic error");
        Check(messages.size()==count+1 && ItemFilter::GetDiagnosticsText().find("1 repeats suppressed")!=std::string::npos,"Repeated runtime errors flooded chat");
        Write("lootfilter_config.lua","return {");
        Check(!LoadScript() && Welcome()=="working","Syntax failure discarded working callbacks");
        Write("lootfilter_config.lua","return {reload='bad',strict=true,rules={{codes='rin',sockets='typo'}}}");
        Check(!LoadScript() && Welcome()=="working","Strict validation failure discarded working callbacks");
        Write("lootfilter_config.lua","return {reload='updated',allowOverrides=true,rules={}}");
        Write("override_rules.lua","return {rules={{codes='rin',suffix='one'}}}");
        Check(LoadScript() && Welcome()=="updated","Recovered load failed");
        Check(ItemFilter::GetDiagnosticsText().find("1 compiled rules")!=std::string::npos,"Overrides not loaded");
        Write("override_rules.lua","return {rules={{codes='rin',suffix='two'},{codes='amu',suffix='new'}}}");
        Check(LoadScript(),"Second override load failed");
        Check(ItemFilter::GetDiagnosticsText().find("2 compiled rules")!=std::string::npos,"Stale require cache retained old overrides");
        Write("lootfilter.lua","return {}");
        Check(!LoadScript() && Welcome()=="updated","Missing ApplyFilter discarded working callbacks");
        Write("lootfilter.lua","not valid lua!");
        Check(!LoadScript() && Welcome()=="updated","Invalid engine discarded working callbacks");
        std::filesystem::current_path(previousDirectory);
        for (const char* file : {"lootfilter.lua","lootfilter_config.lua","override_rules.lua"}) std::filesystem::remove(directory/file);
        std::filesystem::remove(directory);
        std::cout << "PASS: production Lua environment, transactional reloads, diagnostics, override refresh\n";
        return 0;
    } catch (const std::exception& error) {
        std::filesystem::current_path(previousDirectory);
        std::cerr << "FAIL: " << error.what() << '\n'; return 1;
    }
}
