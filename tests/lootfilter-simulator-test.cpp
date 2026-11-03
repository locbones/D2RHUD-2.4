#define NOMINMAX
#include "../d2rhud/plugin/ItemFilter/LootFilterSimulator.h"
#include <iostream>
using namespace LootFilterConfig;
using namespace LootFilterConfig::Preview;
static int checks=0;
static void Check(bool value,const std::string& message){++checks;if(!value)throw std::runtime_error(message);}
int main(int argc,char** argv){
    try{
        Check(argc>=2,"Pass the installed engine source");std::string engine;Check(Read(argv[1],engine),"Cannot read engine");
        Builder::Catalog catalog;
        Builder::AddTable(catalog,"itemtypes","ItemType\tCode\tEquiv1\nAny\t\t\nNone\tnone\t\nWeapon\tweap\t\nSword\tswor\tweap\nCustom\tCsw\tswor\n");
        Builder::AddTable(catalog,"misc","name\tcode\ttype\nRing\trin\tCsw\nAmulet\tamu\tCsw\n");
        catalog.namedItems={{"Unique Ring","rin","Unique",0},{"Set Ring","rin","Set",0}};
        Sample item;Check(Simulator::Select(catalog,"base:rin",item)&&item.quality==2&&item.index==-1,"Base selection failed");
        Check(item.types.size()==3,"Inherited types missing");Check(Simulator::Items(catalog).size()==4,"Named items missing from search");
        Check(Simulator::Select(catalog,"named:Unique:0",item)&&item.index==0&&item.quality==7&&item.name=="Unique Ring","Unique index zero lost");
        Check(Simulator::Select(catalog,"named:Set:0",item)&&item.quality==5,"Set identity lost");
        Check(!Simulator::Select(catalog,"named:Unique:999",item),"Unknown named selection accepted");
        Simulator::Select(catalog,"named:Unique:0",item);Context context;context.filterLevel=0;context.playerStats.clear();Session session;
        // Assert simulator/normal-evaluator parity inside the isolated Lua VM.
        const std::string parity=engine+R"(
            local simulate=SimulateFilter
            function SimulateFilter(me,item,result,level)
                local normal={Name=result.Name,Description=result.Description,Background={0,0,0,0.9},Border={0,0,0,0,1},Grail=result.Grail}
                ApplyFilter(me,item,normal,level)
                local report=simulate(me,item,result,level)
                assert(normal.Name==result.Name and normal.Description==result.Description and normal.Hide==result.Hide,'Simulation changed filter behavior')
                for i=1,4 do assert(normal.Background[i]==result.Background[i] and normal.Border[i]==result.Border[i]) end
                return report
            end
        )";
        auto run=[&](const std::string& source,const std::map<std::string,std::string>& files=std::map<std::string,std::string>{})->const Result&{
            Check(session.BuildFilter(parity,source,item,context,files),session.error);Check(session.results.size()==1,"Missing result");return session.results[0];
        };
        auto result=run(R"(return {filter_level=3,rules={
            {label='Off',enabled=false,hide=true},
            {label='Amulets',codes='amu',hide=true},
            {label='Other level',codes='rin',filter_levels=2,hide=true},
            {label='Valuable',codes='rin',quality='Unique',suffix=' Keep',border={255,128,0,255,2}},
            {label='Common',codes='rin',quality='Magic',hide=true}
        }})");
        Check(result.matched&&!result.hidden&&result.filterLevel==3&&result.name.find("Keep")!=std::string::npos,"Full-filter appearance/active level failed");
        Check(result.rules.size()==5&&result.rules[0].status=="disabled"&&result.rules[1].reason=="codes"&&result.rules[2].reason=="filter_levels","Skipped rules incorrect");
        Check(result.rules[3].label=="Valuable"&&result.rules[3].status=="matched"&&result.rules[4].reason=="quality","Rule trace order/reason failed");
        Check(std::find(result.rules[3].conditions.begin(),result.rules[3].conditions.end(),std::make_pair(std::string("quality"),std::string("Unique")))!=result.rules[3].conditions.end(),"Actual rule conditions missing");
        result=run("return {rules={{codes='rin',suffix=' First'},{label='Hide ring',codes='rin',hide=true},{codes='rin',suffix=' Never'}}}");
        Check(result.hidden&&result.rules[1].status=="hidden"&&result.rules[2].status=="not reached"&&result.name.find("Never")==std::string::npos,"Hide did not stop evaluation");
        Check(result.explanation.find("#2")!=std::string::npos,"Hiding rule not identified");
        result=run("return {rules={{codes='rin',quality='Magic',hide_mismatches=true},{codes='rin',suffix=' Never'}}}");
        Check(result.hidden&&!result.matched&&result.rules[0].reason=="quality"&&result.rules[1].status=="not reached","Mismatch hide trace failed");
        context.location=0;result=run("return {hide_mismatches=true,rules={{codes='rin',quality='Magic',hide_mismatches=true}}}");
        Check(!result.hidden&&result.rules[0].reason=="location","Ground-only hiding affected inventory");context.location=3;
        result=run("return {hide_mismatches=true,rules={{codes='amu',hide=true}}}");
        Check(result.hidden&&!result.matched&&result.explanation.find("unmatched ground")!=std::string::npos,"Global fallback explanation missing");
        result=run("return {styles={show={hide=true,suffix=' Styled'}},rules={{codes='rin',style='show',hide=false}}}");
        Check(!result.hidden&&result.name.find("Styled")!=std::string::npos,"Style override not respected");
        Check(std::find(result.rules[0].actions.begin(),result.rules[0].actions.end(),"hide = false")!=result.rules[0].actions.end(),"Resolved style actions missing");
        item.stats[std::make_pair(39,2)]=42;context.playerStats[std::make_pair(0,0)]=150;
        result=run("return {rules={{codes=NOT {'amu'},quality=OR {'Set','Unique'},itype=2,area=2,difficulty='Hell',stat={index=39,param=2,op='>=',value=42},pstat={index=0,op='>=',value=150},suffix=' Stats'}}}");
        Check(result.matched&&result.name.find("Stats")!=std::string::npos,"Item/player stats, type inheritance or context failed");
        item.stats.clear();result=run("return {rules={{codes='rin',stat={index=39,op='>',value=0},hide=true}}}");
        Check(!result.hidden&&result.rules[0].reason=="stat","Unspecified stats must use zero");
        result=run("local rules={};for i=1,3 do rules[i]={codes='rin',suffix=' '..i} end;return {rules=rules}");
        Check(result.rules.size()==3&&result.name.find(" 1 2 3")!=std::string::npos,"Generated Lua rules not simulated");
        result=run("return {allowOverrides=true,filter_level=2,rules={{codes='rin',suffix=' Base'}}}",{{"override_rules.lua","return {rules={level_1={{codes='rin',suffix=' Default'}},level_2={{label='Override',codes='rin',hide=true}}}}"}});
        Check(result.hidden&&result.rules.size()==2&&result.rules[1].source=="override_rules.lua/level_2","Override level/trace failed");
        context.filterLevel=3;result=run("return {allowOverrides=true,rules={}}",{{"overrides.lua","return {rules={level_1={{codes='rin',suffix=' Fallback'}}}}"}});
        Check(result.name.find("Fallback")!=std::string::npos&&result.rules[0].source=="overrides.lua/level_1","Legacy override fallback failed");context.filterLevel=0;
        result=run("return {allowOverrides=true,rules={}}");Check(!result.warnings.empty(),"Missing override warning suppressed");
        result=run("return {rules={{codes='rin',sockets='typo'},{codes='rin',suffix=' Valid'}}}");
        Check(result.rules[0].status=="invalid"&&!result.warnings.empty()&&result.name.find("Valid")!=std::string::npos,"Non-strict invalid rule explanation missing");
        Check(!session.BuildFilter(engine,"return {strict=true,rules={{sockets='typo'}}}",item,context)&&!session.error.empty(),"Strict failure presented as success");
        Check(!session.BuildFilter(engine,"return {",item,context)&&!session.error.empty(),"Syntax failure presented as success");
        Check(!session.BuildFilter(engine,"while true do end",item,context)&&!session.error.empty(),"Simulator instruction budget failed");
        const std::string isolated="assert(io.open('simulator-unwanted-file','w')==nil and os.execute==nil and require==nil and debug==nil);return {audioPlayback=true,rules={{codes='rin',audio='test.mp3',notify='Found {link}'}}}";
        Check(session.BuildFilter(engine,isolated,item,context),session.error);Check(session.results[0].audio&&session.results[0].messages.find("Found")!=std::string::npos,"Silent audio/chat preview missing");
        Check(!session.BuildFilter("ApplyFilter=function()end","return {}",item,context)&&!session.error.empty(),"Old engine silently accepted without tracing");
        Check(session.BuildFilter("\xEF\xBB\xBF"+engine,"\xEF\xBB\xBFreturn {allowOverrides=true,rules={}}",item,context,{{"override_rules.lua","\xEF\xBB\xBFreturn {rules={{codes='rin',hide=true}}}"}}),session.error);
        Check(session.results[0].hidden,"BOM-prefixed author/override files not simulated");
        if(argc>2){
            std::string source;Check(Read(argv[2],source),"Cannot read installed author config");std::map<std::string,std::string> files;
            const auto root=std::filesystem::path(argv[2]).parent_path();
            for(const char* name:{"override_rules.lua","overrides.lua"}){std::string text;if(Read((root/name).string(),text))files[name]=std::move(text);}
            Check(session.BuildFilter(engine,source,item,context,files),"Installed author filter: "+session.error);
            std::cout<<"Installed filter: "<<session.results[0].rules.size()<<" rule explanations, "<<session.results[0].warnings.size()<<" diagnostics\n";
        }
        std::cout<<"PASS: "<<checks<<" simulator selection, exact identities, full-filter parity/order/hiding, context/stats, draft/generated Lua, overrides, diagnostics and isolation checks\n";return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL after "<<checks<<": "<<error.what()<<'\n';return 1;}
}
