#define NOMINMAX
#include "../d2rhud/plugin/ItemFilter/LuaConfigIO.h"
#include <iostream>

using namespace LootFilterConfig;
static int checks = 0;
static void Check(bool passed, const std::string& message) {
    ++checks;
    if (!passed) throw std::runtime_error(message);
}
static void RoundTrip(const std::string& source) {
    Document doc;
    Check(doc.Parse(source), doc.error);
    Check(doc.Update({}, {}, false) == source, "No-op must be byte identical");
    const auto sources = doc.RuleSources();
    std::string edited = doc.Update({{"filter_level", "2"}}, sources, false), error;
    Check(ValidateSyntax(edited, error), error);
    Document updated;
    Check(updated.Parse(edited), updated.error);
    Check(updated.Value(*updated.Find("filter_level")) == "2", "Global edit missing");
    Check(updated.RuleSources() == sources, "Global edit altered rules");
    edited = doc.Update({}, {}, true);
    Check(ValidateSyntax(edited, error), error);
    Check(updated.Parse(edited) && updated.rules.empty(), "Delete all rules must persist");
    auto added = sources;
    added.push_back("\n-- inserted\n{codes='rin',suffix=[=[ {quoted} -- text ]=]},");
    edited = doc.Update({}, added, true);
    Check(ValidateSyntax(edited, error), error);
    Check(updated.Parse(edited) && updated.rules.size() == sources.size() + 1, "Append rule missing");
    Check(updated.source.substr(updated.rootEnd) == doc.source.substr(doc.rootEnd), "Generated tail was changed");
    for (size_t i = 0; i < updated.rules.size(); ++i) updated.RuleFields(i);
}
int main(int argc, char** argv) {
    try {
        RoundTrip("return { filter_level=1,rules={{codes='rin',suffix='{} -- 100%'},{code=\"amu\",stat=OR {{index=39,op=\">\",value=0}}}},styles={valuable={hide=false}}}");
        RoundTrip("\xEF\xBB\xBF--[==[ { unmatched in comment } ]==]\r\nreturn {\r\n[\"filter_level\"]=1; rules={\r\n-- author note\r\n{code='rin',prefix_desc=[=[line one\n  preserve indentation\n  -- { }]=]},\r\n-- tail note\r\n}, language='enUS'\r\n}\r\n");
        RoundTrip("local T=true; local filterConfig={allowOverrides=T,rules={{codes='allitems',location=Locations}}}; for i=1,3 do table.insert(filterConfig.rules,{code='rin'}) end; return filterConfig");
        RoundTrip("return {rules={},language='enUS'}");
        RoundTrip("return {language='enUS'}");
        RoundTrip("return {rules={{codes='rin',custom=function() local a,b=1,2; if a then return {a,b} end end}}}");
        Document doc;
        Check(!doc.Parse("return {rules={ { codes='rin' }"), "Unclosed config accepted");
        Check(!doc.Parse("return {rules={},rules={}}"), "Duplicate roots accepted");
        Check(!doc.Parse("return makeConfig()"), "Computed return should require source editing");
        Check(!doc.Parse("return {rules=generated}"), "Computed rules should require source editing");
        std::string error;
        Check(!ValidateSyntax("return {rules={{codes='rin'}}",error), "Invalid syntax accepted");
        Check(DecodeString("'a\\n\\x25'") == "a\n%", "Literal escapes not decoded");
        Check(DecodeString(Quote("a\n\t\\\"\1")) == "a\n\t\\\"\1", "Text quoting round trip");
        Check(DecodeString("os.execute('bad')") == "os.execute('bad')", "Expression should remain opaque");
        std::string large = "return {rules={{codes='rin',prefix=[=[" + std::string(20000,'x') + "]=]}}}";
        RoundTrip(large);
        if (argc > 1) {
            int files = 0, rules = 0;
            for (const auto& file : std::filesystem::recursive_directory_iterator(std::filesystem::u8path(argv[1]))) {
                if (file.path().extension() != ".lua" || file.path().filename() == "lootfilter.lua" || file.path().parent_path().filename() == "tests") continue;
                std::string source;
                Check(Read(file.path().string(),source), file.path().string());
                Check(doc.Parse(source), file.path().string() + ": " + doc.error);
                rules += int(doc.rules.size()); ++files;
                RoundTrip(source);
            }
            std::cout << files << " author/template files, " << rules << " authored rules parsed\n";
        }
        // Disk writes only touch this test's temporary directory.
        const auto dir = std::filesystem::temp_directory_path() / ("d2rhud-filter-test-" + std::to_string(GetCurrentProcessId()));
        std::filesystem::create_directories(dir);
        const auto path = dir / "lootfilter_config.lua";
        const std::string first = "return {rules={}}", second = "return {rules={{code='rin'}}}";
        { std::ofstream f(path,std::ios::binary); f << first; }
        Check(Save(path.string(),first,second,error), error);
        std::string actual;
        Check(Read(path.string()+".bak",actual) && actual==first, "Backup lost original bytes");
        Check(!Save(path.string(),first,"return {}",error), "External modification was overwritten");
        Check(!Save(path.string(),second,"return {",error), "Invalid draft was written");
        Check(Read(path.string(),actual) && actual==second, "Rejected save changed file");
        std::filesystem::remove(path); std::filesystem::remove(path.string()+".bak"); std::filesystem::remove(dir);
        std::cout << "PASS: " << checks << " config parser/save checks\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL after " << checks << ": " << e.what() << '\n'; return 1; }
}
