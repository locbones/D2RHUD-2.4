#define NOMINMAX
#include "../d2rhud/plugin/ItemFilter/LootFilterRuleBuilder.h"
#include <iostream>
using namespace LootFilterConfig;
using namespace LootFilterConfig::Builder;
static int checks=0;
static void Check(bool value,const std::string& message){++checks;if(!value)throw std::runtime_error(message);}
static std::string RuleValue(const std::string& raw,const std::string& key){for(const auto& field:RuleFields(raw))if(Canonical(field.first)==key)return field.second;return {};}
static void Lua(lua_State* state,const std::string& source){if(luaL_dostring(state,source.c_str()))throw std::runtime_error(lua_tostring(state,-1));}
int main(int argc,char** argv){
    try{
        std::string raw="-- original note\n{ code = Codes, stat = OR {{index={39,43},op='>=',value=30}}, suffix = 'old', custom = function() return {1,2} end } -- trailing note",error;
        Check(SetField(raw,"quality","{\"Unique\", \"Set\"}",error),error);
        Check(SetField(raw,"suffix",Quote("\"quoted\" \\ text\n% {name}"),error),error);
        Check(raw.find("code = Codes")!=std::string::npos&&raw.find("stat = OR {{index={39,43},op='>=',value=30}}")!=std::string::npos,"Unrelated expressions were changed");
        Check(raw.find("-- original note")!=std::string::npos&&raw.find("-- trailing note")!=std::string::npos&&raw.find("custom = function() return {1,2} end")!=std::string::npos,"Author comments/functions were lost");
        Check(SetField(raw,"codes","{\"Z01\",\"rin\"}",error)&&raw.find("code = {")!=std::string::npos&&raw.find("codes =")==std::string::npos,"Legacy alias was duplicated");
        Check(SetField(raw,"quality","nil",error)&&RuleValue(raw,"quality")=="nil","Condition removal failed");
        for(const auto& pair:std::map<std::string,std::string>{{"locations","location"},{"stats","stat"},{"pstats","pstat"},{"item_levels","ilvl"},{"ilvls","ilvl"},{"filter_level","filter_levels"},{"levels","filter_levels"}}){
            std::string aliased="{"+pair.first+"=Previous}";
            Check(SetField(aliased,pair.second,"nil",error)&&aliased=="{"+pair.first+"=nil}","Alias duplicated: "+pair.first);
        }
        Check(ValidateSyntax("return {rules={"+raw+"\n}}",error),error);
        std::string computed="makeRule()";Check(!SetField(computed,"suffix","'bad'",error)&&computed=="makeRule()","Computed rule changed");
        for(const auto& spec:Specs){
            if(spec.kind==Kind::Text){Value value;value.text="quote\" slash\\ tab\t newline\n\1";Check(Decode(spec,Encode(spec,value)).text==value.text,spec.key);}
            if(spec.kind==Kind::Boolean){Value value;value.flag=false;Check(!Decode(spec,Encode(spec,value)).flag,spec.key);}
            if(spec.kind==Kind::Number)for(int op=0;op<6;++op){Value value;value.number=80;value.operation=op;auto decoded=Decode(spec,Encode(spec,value));Check(decoded.editable&&decoded.number==80&&decoded.operation==op,spec.key);}
            if(spec.kind==Kind::Color){Value value;value.color={0.1f,0.4f,0.8f,0.9f};auto decoded=Decode(spec,Encode(spec,value));Check(decoded.editable&&decoded.color[3]>0.89f,spec.key);}
            if(spec.kind==Kind::Stat)for(int op=0;op<7;++op){Value value;value.stat=39;value.parameter=2;value.number=20;value.upper=40;value.operation=op;auto decoded=Decode(spec,Encode(spec,value));Check(decoded.editable&&decoded.stat==39&&decoded.parameter==2&&decoded.operation==op&&decoded.number==20,spec.key);}
        }
        for(const auto& key:{"quality","codes","rarity","area","difficulty","location","itype"}){Value value;value.choices=(std::string(key)=="itype"||std::string(key)=="area")?std::vector<std::string>{"10","12"}:std::vector<std::string>{"A","B"};value.exclude=true;auto decoded=Decode(*FindSpec(key),Encode(*FindSpec(key),value));Check(decoded.editable&&decoded.exclude&&decoded.choices==value.choices,key);}
        Check(!Decode(*FindSpec("codes"),"ItemCodes").editable,"Variable reference flattened");
        Check(Decode(*FindSpec("quality"),"'Unique+'").editable,"Quality literal should stay a literal selection");
        Check(!Decode(*FindSpec("suffix"),"{enUS='English',frFR='French'}").editable,"Translation table flattened");
        Check(!Decode(*FindSpec("stat"),"OR {{index=39,value=20}}").editable,"Compound stat flattened");
        Check(!Decode(*FindSpec("maxsock"),"true").editable,"Legacy boolean maxsock flattened");
        Catalog catalog;
        AddTable(catalog,"misc","name\tcode\nStorage Bag\tZ01\nRing\trin\nExpansion\t\n");
        AddTable(catalog,"itemtypes","ItemType\tCode\nAny\t\nNone\tnone\nShield\tshie\nCustom swords\tCswd\n");
        AddTable(catalog,"itemstatcost","Stat\t*ID\nfire_resist\t39\ncustom_points\t650\n");
        AddTable(catalog,"levels","Name\tId\nRogue Encampment\t1\nCustom Arena\t239\n");SortCatalog(catalog);
        AddTable(catalog,"uniqueitems","index\t*ID\tenabled\tcode\nCustom Unique\t612\t1\tZ01\nDisabled\t613\t0\trin\n");
        AddTable(catalog,"setitems","index\t*ID\titem\nCustom Set\t55\tamu\n");
        Check(catalog.items.size()==2&&catalog.types.size()==2&&catalog.stats.size()==2&&catalog.areas.size()==2,"Custom catalogs lost rows");
        Check(catalog.types[0].value=="3"&&catalog.stats[0].label=="custom points","Catalog names/IDs wrong");
        Check(catalog.namedItems.size()==2&&catalog.namedItems[0].index==612&&catalog.namedItems[1].quality=="Set","Named item identity lost");
        Check(Encode(*FindSpec("name_style"),Decode(*FindSpec("name_style"),"'Rainbow'"))=="\"Rainbow\"","Effect must be a scalar");
        if(argc>2){
            Catalog actual;
            for(const char* table:{"weapons","armor","misc","itemtypes","itemstatcost","levels","uniqueitems","setitems"}){
                std::string text;Check(Read((std::filesystem::u8path(argv[2])/(std::string(table)+".txt")).string(),text),std::string("Read catalog ")+table);AddTable(actual,table,text);
            }
            SortCatalog(actual);
            Check(!actual.items.empty()&&!actual.types.empty()&&!actual.stats.empty()&&!actual.areas.empty()&&!actual.namedItems.empty(),"A real mod catalog is empty");
            std::cout<<actual.items.size()<<" base items, "<<actual.types.size()<<" categories, "<<actual.stats.size()<<" stats, "<<actual.areas.size()<<" areas, "<<actual.namedItems.size()<<" named items\n";
        }
        if(argc>1){
            auto previous=std::filesystem::current_path();
            auto folder=std::filesystem::temp_directory_path()/("d2rhud-builder-test-"+std::to_string(GetCurrentProcessId()));std::filesystem::create_directories(folder);
            std::filesystem::copy_file(std::filesystem::u8path(argv[1]),folder/"lootfilter.lua",std::filesystem::copy_options::overwrite_existing);
            std::filesystem::current_path(folder);
            std::string rule="{label='Built without code',codes='rin',location='onground',suffix=' Keep'}";
            for(const auto& key:{"quality","itype","ilvl","stat","background","border","name_style","background_style"}){
                const auto& spec=*FindSpec(key);Value value;
                if(std::string(key)=="quality")value.choices={"Unique","Set"};
                if(std::string(key)=="itype")value.choices={"10"};
                if(std::string(key)=="ilvl"){value.number=80;value.operation=1;}
                if(std::string(key)=="stat"){value.stat=39;value.number=30;value.operation=1;}
                if(std::string(key)=="name_style")value.choices={"Rainbow"};
                if(std::string(key)=="background_style")value.choices={"Ocean Drift"};
                Check(SetField(rule,key,Encode(spec,value),error),error);
            }
            {std::ofstream file("lootfilter_config.lua");file<<"return {strict=true,rules={"<<rule<<"}}";}
            lua_State* state=luaL_newstate();luaL_openlibs(state);
            Lua(state,"print=function()end; assert(loadfile('lootfilter.lua'))(); local d=GetFilterDiagnostics(); assert(d.rules==1 and d.disabled==0,'Generated rule did not compile')");
            Lua(state,R"(
                local item={Txt={Code='rin'},Data={Quality=7,FileIndex=42,ItemLevel=85,Flags=0x10,IsEthereal=false,Owner=1},Mode=3,Area=2,Rarity=2,Name='Ring',Stat=function(_,i)return i==39 and 30 or 0 end,MaxSockets=function()return 4 end,IsType=function(_,i)return i==10 end}
                local function apply() local r={Name='Ring',Description='Base',Background={},Border={},Grail={isGrail=false,collected=false}};ApplyFilter({ID=1,Difficulty=2},item,r,1);return r end
                assert(apply().Name=='Ring Keep','Matching selection failed');item.Data.Quality=4;assert(apply().Name=='Ring','Failed condition still matched')
            )");
            // Every advertised preset must compile with the actual engine.
            for(const char* key:{"name_style","background_style"})for(const auto& option:Options(key,catalog)){
                Lua(state,"package.loaded.lootfilter_config=nil");
                {std::ofstream file("lootfilter_config.lua");file<<"return {strict=true,rules={{codes='rin',"<<key<<"="<<Quote(option.value)<<"}}}";}
                Lua(state,"assert(loadfile('lootfilter.lua'))()");++checks;
            }
            lua_close(state);std::filesystem::current_path(previous);std::filesystem::remove_all(folder);
        }
        std::cout<<"PASS: "<<checks<<" builder preservation, custom catalog, encoding and native Lua engine checks\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
