#define NOMINMAX
#include "../d2rhud/plugin/ItemFilter/LootFilterPreview.h"
#include <iostream>
#include <chrono>
using namespace LootFilterConfig;
using namespace LootFilterConfig::Preview;
static int checks=0;
static void Check(bool ok,const std::string& message){++checks;if(!ok)throw std::runtime_error(message);}
static std::string Plain(std::string value){for(size_t i=0;i<value.size();){if(i+3<value.size()&&static_cast<unsigned char>(value[i])==0xc3&&static_cast<unsigned char>(value[i+1])==0xbf&&value[i+2]=='c')value.erase(i,4);else ++i;}return value;}
int main(int argc,char** argv){
    try{
        Check(DataExpression("NOT {'rin','amu'}"),"NOT data rejected");
        Check(DataExpression("OR {{index=39,op='>=',value=30},{index={41,43},value=10}}"),"OR data rejected");
        Check(DataExpression("{enUS='Text',frFR=[=[Texte]=]}"),"Translations rejected");
        for(const char* value:{"Codes","function() return 'bad' end","os.execute('bad')","{index=39,value=readStat()}","'a' .. action()","(function()while true do end end)()"})Check(!DataExpression(value),std::string("Authored code accepted: ")+value);
        Builder::Catalog catalog;
        Builder::AddTable(catalog,"itemtypes","ItemType\tCode\tEquiv1\nAny\t\t\nNone\tnone\t\nWeapon\tweap\t\nSword\tswor\tweap\nCustom\tCsw\tswor\n");
        Builder::AddTable(catalog,"weapons","name\tcode\ttype\tgemsockets\tlevel\tnormcode\tubercode\tultracode\nCustom Sword\tZ01\tCsw\t6\t85\tZ01\tZ02\tZ03\nCrystal Sword\tcrs\tswor\t6\t11\tcrs\t9cr\t7cr\n");
        auto inherited=Types(catalog,{"Csw"});Check(inherited.size()==3,"Inherited categories missing");
        catalog.typeParents["weap"]={"Csw"};Check(Types(catalog,{"Csw"}).size()==3,"Cyclic categories did not terminate");
        auto focused=Samples("{codes='Z01',quality='Unique',ilvl='95+',stat={index=39,op='>=',value=30}}",catalog);
        Check(focused.size()>=18&&focused[0].code=="Z01","Focused samples missing");
        Check(std::any_of(focused.begin(),focused.end(),[](const Sample& sample){return sample.level==95;}),"Threshold example missing");
        Session session;
        Check(!session.Build("","{}",{},catalog,{}),"Missing engine was accepted");
        if(argc>1){
            std::string engine;Check(Read(argv[1],engine),"Cannot read engine");
            const auto before=std::chrono::steady_clock::now();
            const std::string rule="{codes='rin',quality='Unique',suffix=' {red}Keep {sockets}',background={10,20,30,128},border={90,80,70,255,3},notify='Found {link}',audio='Notice'}";
            Check(session.Build(engine,rule,{},catalog,{}),session.error);
            int matches=0,misses=0;
            for(size_t i=0;i<session.results.size();++i){const auto& result=session.results[i];const auto& sample=session.samples[i];
                if(sample.code=="rin"&&sample.quality==7){++matches;Check(result.matched&&!result.hidden,"Unique ring did not match");Check(Plain(result.name).find("Keep")!=std::string::npos,"Name action missing");Check(result.background[3]>0.5f&&result.background[3]<0.51f&&result.thickness==3,"Color/border action missing");Check(result.messages.find("Found")!=std::string::npos&&result.audio,"Notification preview missing");}
                else{++misses;Check(!result.matched&&!result.hidden&&result.name==sample.name,"Nonmatch was changed");}
            }
            Check(matches>0&&misses>0,"Preview did not include contrasting samples");
            Check(session.Build(engine,"{codes='rin',quality='Unique',hide=true}",{},catalog,{}),session.error);
            Check(std::any_of(session.results.begin(),session.results.end(),[](const Result& result){return result.matched&&result.hidden;}),"Hide action missing");
            Check(session.Build(engine,"{codes='rin',quality='Unique',hide_mismatches=true}",{},catalog,{}),session.error);
            Check(std::any_of(session.results.begin(),session.results.end(),[](const Result& result){return !result.matched&&result.hidden;}),"Mismatch hiding missing");
            Context inventory;inventory.location=0;
            Check(session.Build(engine,"{codes='rin',quality='Unique',hide_mismatches=true}",{},catalog,inventory),session.error);
            Check(std::none_of(session.results.begin(),session.results.end(),[](const Result& result){return result.hidden;}),"Ground-only rule hid inventory samples");
            Check(session.Build(engine,"{codes='rin',enabled=false,suffix=' WRONG'}",{},catalog,{}),session.error);
            Check(std::none_of(session.results.begin(),session.results.end(),[](const Result& result){return result.matched;}),"Disabled rule matched");
            Check(session.Build(engine,"{codes='rin',style=nil,audio=nil,suffix=' Updated'}",{},catalog,{}),session.error);
            Check(std::any_of(session.results.begin(),session.results.end(),[](const Result& result){return result.matched&&result.name.find("Updated")!=std::string::npos&&!result.audio;}),"Draft changes or cleared actions were not refreshed");
            Check(session.Build(engine,"{codes='rin',style='good'}",{{"styles","{good={suffix=' Styled'}}"}},catalog,{}),session.error);
            Check(std::any_of(session.results.begin(),session.results.end(),[](const Result& result){return result.name.find("Styled")!=std::string::npos;}),"Reusable style was lost");
            Check(session.Build(engine,"{codes='rin',suffix={enUS=' English',frFR=' French'}}",{{"language","frFR"}},catalog,{}),session.error);
            Check(session.results[0].name.find("French")!=std::string::npos,"Translation preview wrong");
            Check(session.Build(engine,"{codes='Z01',itype=2,ilvl='95+',stat={index=39,op='>=',value=30},suffix=' Custom'}",{},catalog,{}),session.error);
            Check(std::any_of(session.results.begin(),session.results.end(),[](const Result& result){return result.matched;}),"Custom code/category/stat sample never matched");
            for(const char* key:{"name_style","background_style"})for(const auto& option:Builder::Options(key,catalog)){
                Check(session.Build(engine,"{codes='rin',"+std::string(key)+"="+Quote(option.value)+"}",{},catalog,{}),session.error);
                Check(session.Animate(12345),session.error);Check(session.Animate(12999),session.error);
            }
            const std::string safeEngine="assert(io.open('unwanted-preview-file','w')==nil);assert(os.execute==nil and require==nil and debug==nil and dofile==nil and load==nil);\n"+engine;
            Check(session.Build(safeEngine,"{codes='allitems'}",{},catalog,{}),session.error);
            Check(!session.Build(engine,"{suffix=doSomething()}",{},catalog,{})&&!session.error.empty(),"Custom function executed");
            Check(!session.Build(engine,"makeRule()",{},catalog,{}),"Generated rule executed");
            Check(!session.Build(engine,"{sockets='typo'}",{},catalog,{}),"Invalid rule preview accepted");
            Check(!session.Build("while true do end","{}",{},catalog,{}),"Instruction budget failed");
            Check(!session.Build("local huge=string.rep('x',100000000)","{}",{},catalog,{}),"Memory budget failed");
            std::cout<<"Preview suite took "<<std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-before).count()<<" ms\n";
        }
        std::cout<<"PASS: "<<checks<<" live preview, matching, appearance, effects and isolation checks\n";return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
