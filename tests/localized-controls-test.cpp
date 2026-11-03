#define NOMINMAX
#include "../d2rhud/plugin/HudText.h"
#include <imgui_internal.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <regex>
#include <set>
#include <stdexcept>

using namespace std::chrono;
namespace fs=std::filesystem;
unsigned checks=0;size_t oracleCases=0;
void Check(bool condition,const char* message){++checks;if(!condition)throw std::runtime_error(message);}
// Unmodified previous production regex serves as an independent compatibility oracle.
std::vector<std::string> Previous(const std::string& text){
    static const std::regex pattern(R"(%[-+ #0]*(?:\d+|\*)?(?:\.(?:\d+|\*))?(?:hh|ll|I64|I32|[hljztL])?[diuoxXfFeEgGaAcsnp%])");
    std::vector<std::string> result;for(auto i=std::sregex_iterator(text.begin(),text.end(),pattern);i!=std::sregex_iterator();++i)result.push_back(i->str());return result;
}
bool PreviousMatch(const std::string& a,const std::string& b){return std::count(a.begin(),a.end(),'%')==std::count(b.begin(),b.end(),'%') && Previous(a)==Previous(b);}
void Parity(const std::string& text){
    ++oracleCases;
    if(HudLocale::Placeholders(text)!=Previous(text))throw std::runtime_error("Format parser differs from previous regex: "+text);
}
using Corpus=std::vector<std::pair<std::string,std::string>>;
Corpus FormatTests(const fs::path& repository){
    using namespace HudLocale;
    for(const std::string flags:{"","0","-","#+ ","000"," +-#0"})
    for(const std::string width:{"","*","1","12","001"})
    for(const std::string precision:{"",".0",".2",".*",".","..4"})
    for(const std::string length:{"","h","hh","l","ll","I64","I32","j","z","t","L","I","hhh"})
    for(const char conversion:std::string("diuoxXfFeEgGaAcsnp%q"))Parity("start %"+flags+width+precision+length+conversion+" end");
    std::mt19937 random(0xD2);
    const std::string alphabet="%0123456789*.-+ #IhljztLdiuoxXfFeEgGaAcsnpq_xyz";
    for(unsigned i=0;i<50000;++i){
        std::string text;for(size_t j=0,n=random()%100;j<n;++j)text+=alphabet[random()%alphabet.size()];
        Parity(text);
        auto changed=text;if(!changed.empty())changed[random()%changed.size()]=alphabet[random()%alphabet.size()];
        if(Formats::Match(text,changed)!=PreviousMatch(text,changed))throw std::runtime_error("Format compatibility decision changed");
    }
    for(int byte=0;byte<256;++byte)Parity("%"+std::string(1,char(byte))+"d");
    Corpus corpus;
    for(const auto& language:Languages){
        std::ifstream file(repository/"translations"/(std::string(language.code)+".json"),std::ios::binary);
        Check(bool(file),"Shipped language pack opened read-only");
        const auto document=nlohmann::json::parse(file);
        for(const auto& [key,value]:document.at("strings").items()){
            const auto text=value.get<std::string>();Parity(key);Parity(text);
            if(Formats::Match(key,text)!=PreviousMatch(key,text))throw std::runtime_error("Shipped translation validation changed: "+key);
            corpus.emplace_back(key,text);
        }
    }
    Check(Formats::Match("Count: %d","Nombre : %d"),"Translated prefix preserves exact format");
    Check(!Formats::Match("Count: %d","Nombre : %s"),"Changed argument type rejected");
    Check(!Formats::Match("%s %d","%d %s"),"Reordered argument types rejected");
    Check(!Formats::Match("Chance: %%","Chance"),"Escaped percent cannot disappear");
    Check(!Formats::Match("Safe","%n"),"New write-through format rejected");
    Check(!Formats::Match("%*.*I64d","%d"),"Width, precision and length cannot disappear");
    Check(Formats::Match("stray %q","another %w"),"Legacy invalid-format compatibility preserved");
    // Long authored entries must not recurse through regex quantifiers. Running
    // the old regex here could overflow its native stack, so these are direct cases.
    std::string longFormat="%"+std::string(2*1024*1024,'0')+"lld";
    auto other=longFormat;other.back()='s';
    const auto start=steady_clock::now();
    Check(Formats::Match(longFormat,longFormat),"Two MiB width/flags scanned safely");
    Check(!Formats::Match(longFormat,other),"Long entry still rejects argument changes");
    longFormat="%."+std::string(2*1024*1024,'1')+"f";
    Check(Formats::Match(longFormat,longFormat),"Two MiB precision scanned safely");
    std::cout<<oracleCases<<" legacy-regex parity cases, "<<corpus.size()<<" shipped source/translation pairs; long format checks "<<duration<double,std::milli>(steady_clock::now()-start).count()<<" ms.\n";
    return corpus;
}
void FormatBenchmark(const Corpus& corpus){
    std::vector<double> oldSamples,newSamples;size_t oldAccepted=0,newAccepted=0;
    for(unsigned trial=0;trial<7;++trial){
        auto start=steady_clock::now();size_t accepted=0;
        for(const auto& [key,value]:corpus)accepted+=PreviousMatch(key,value);
        oldAccepted=accepted;oldSamples.push_back(duration<double,std::milli>(steady_clock::now()-start).count());
        start=steady_clock::now();accepted=0;
        for(const auto& [key,value]:corpus)accepted+=HudLocale::Formats::Match(key,value);
        newAccepted=accepted;newSamples.push_back(duration<double,std::milli>(steady_clock::now()-start).count());
        Check(oldAccepted==newAccepted,"Benchmark format acceptance matches");
    }
    std::sort(oldSamples.begin(),oldSamples.end());std::sort(newSamples.begin(),newSamples.end());
    std::cout<<"All-pack format validation median: previous regex "<<oldSamples[3]<<" ms; linear parser "<<newSamples[3]<<" ms.\n";
}
struct Items {
    std::vector<std::string> names;
    unsigned calls=0;
    bool reuse=false,capture=false,legacyFails=false;
    char buffer[64]{};
    std::vector<int> indices;
    explicit Items(int count){for(int i=0;i<count;++i)names.push_back("Entry "+std::to_string(i));}
};
const char* Getter(void* data,int index){
    auto& items=*static_cast<Items*>(data);++items.calls;
    if(index<0 || size_t(index)>=items.names.size())throw std::runtime_error("Getter received out of range index");
    if(items.capture)items.indices.push_back(index);
    if(!items.reuse)return items.names[index].c_str();
    strcpy_s(items.buffer,items.names[index].c_str());return items.buffer;
}
bool LegacyGetter(void* data,int index,const char** text){
    auto& items=*static_cast<Items*>(data);
    if(items.legacyFails){++items.calls;return false;}
    *text=Getter(data,index);return true;
}
bool OldCombo(const char* label,int* selected,Items& items){
    std::vector<const char*> translated;for(int i=0;i<int(items.names.size());++i)translated.push_back(HudLocale::Tr(Getter(&items,i)));
    return ImGui::Combo(HudLocale::Label(label).c_str(),selected,translated.data(),int(translated.size()),7);
}
enum class Mode { Modern,Legacy,Previous };
ImVec2 comboMinimum,comboMaximum;ImGuiID comboID=0;
bool Frame(Items& items,int& selected,Mode mode=Mode::Modern){
    ImGui::NewFrame();ImGui::SetNextWindowPos({10,10});ImGui::SetNextWindowSize({900,650});
    ImGui::Begin("Localized controls fixture",nullptr,ImGuiWindowFlags_NoSavedSettings);
    ImGui::SetNextItemWidth(360);comboID=ImGui::GetID(HudLocale::Label("Choice").c_str());
    const bool changed=mode==Mode::Modern?HudText::Combo("Choice",&selected,Getter,&items,int(items.names.size()),7):
        mode==Mode::Legacy?HudText::Combo("Choice",&selected,LegacyGetter,&items,int(items.names.size()),7):OldCombo("Choice",&selected,items);
    if(GImGui->OpenPopupStack.empty()){comboMinimum=ImGui::GetItemRectMin();comboMaximum=ImGui::GetItemRectMax();}
    ImGui::End();ImGui::Render();return changed;
}
void Controls(){
    auto french=std::make_shared<HudLocale::Catalog>();french->Merge(R"({"strings":{"Choice":"Choix","Quest Journal":"Journal des quêtes","Cancel":"Annuler"}})");HudLocale::Publish(french);
    ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={1200,720};io.DeltaTime=1.f/60;
    io.Fonts->AddFontDefault();io.Fonts->Build();
    Items items(10000);int selected=9000;Frame(items,selected);Frame(items,selected);items.calls=0;
    bool unchanged=true;for(int frame=0;frame<1000;++frame)unchanged &= !Frame(items,selected);
    Check(unchanged,"Closed dropdown preserves selection");
    Check(items.calls==1000 && selected==9000,"Closed 10000-entry dropdown requests one preview per frame");
    items.calls=0;Frame(items,selected,Mode::Legacy);
    Check(items.calls==1,"Legacy callback is also lazy");
    items.legacyFails=true;Frame(items,selected,Mode::Legacy);
    Check(selected==9000,"Failed legacy lookup does not change selection");items.legacyFails=false;
    HudText::ComboSource::Legacy legacy{LegacyGetter,&items};items.legacyFails=true;
    Check(HudText::ComboSource::LegacyItem(&legacy,0)==nullptr,"Legacy getter failure respected without reading output");items.legacyFails=false;
    const char* array[]{"Quest Journal","Cancel"};HudText::ComboSource::Array arraySource{array};
    Check(std::string(HudText::ComboSource::ArrayItem(&arraySource,0))=="Journal des quêtes","Array entries translated lazily");
    const char zeros[]="Quest Journal\0Cancel\0";HudText::ComboSource::Zeros zeroSource{zeros,zeros};
    Check(std::string(HudText::ComboSource::ZeroItem(&zeroSource,1))=="Annuler","Zero-separated list translated");
    Check(std::string(HudText::ComboSource::ZeroItem(&zeroSource,0))=="Journal des quêtes","Zero list cursor handles backwards lookup");
    Check(std::string(HudText::ComboSource::ZeroItem(&zeroSource,1))=="Annuler","Zero list cursor resumes forwards lookup");
    selected=0;items.reuse=true;items.capture=true;Frame(items,selected);const auto stableID=comboID;
    io.AddMousePosEvent((comboMinimum.x+comboMaximum.x)/2,(comboMinimum.y+comboMaximum.y)/2);
    io.AddMouseButtonEvent(0,true);Frame(items,selected);io.AddMouseButtonEvent(0,false);Frame(items,selected);Frame(items,selected);
    Check(!GImGui->OpenPopupStack.empty(),"Real dropdown opens");
    items.calls=0;items.indices.clear();Frame(items,selected);
    Check(items.calls>1 && items.calls<30,"Open dropdown evaluates clipped visible rows only");
    Check(std::set<int>(items.indices.begin(),items.indices.end()).size()>1,"Getter can reuse one buffer for distinct visible entries");
    HudLocale::Publish(std::make_shared<HudLocale::Catalog>());Frame(items,selected);
    Check(comboID==stableID && !GImGui->OpenPopupStack.empty(),"Language reload preserves combo ID and open popup");
    auto* popup=GImGui->OpenPopupStack.back().Window;Check(popup!=nullptr,"Native popup window exists");
    const float row=ImGui::GetTextLineHeightWithSpacing();
    io.AddMousePosEvent(popup->Pos.x+30,popup->Pos.y+ImGui::GetStyle().WindowPadding.y+3*row+row*.5f);
    io.AddMouseButtonEvent(0,true);Frame(items,selected);io.AddMouseButtonEvent(0,false);const bool changed=Frame(items,selected);
    Check(changed && selected==3,"Mouse selects correct underlying index after reload");
    Check(GImGui->OpenPopupStack.empty(),"Selection closes dropdown");
    items.reuse=items.capture=false;items.calls=0;selected=-1;Frame(items,selected);
    Check(items.calls==0 && selected==-1,"Unselected preview invokes no getter");
    // Exercise all public overloads through real ImGui, including empty lists.
    ImGui::NewFrame();ImGui::Begin("Overloads");selected=1;
    HudText::Combo("Array",&selected,array,2);HudText::Combo("Zeros",&selected,zeros);
    HudText::Combo("Empty array",&selected,static_cast<const char* const*>(nullptr),0);
    HudText::Combo("Empty zeros",&selected,static_cast<const char*>(nullptr));
    Check(selected==1,"Array/zero/empty overloads preserve stored index");
    ImGui::End();ImGui::Render();
    ImGui::DestroyContext();
}
void ComboBenchmark(){
    ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={1200,720};io.DeltaTime=1.f/60;io.Fonts->AddFontDefault();io.Fonts->Build();
    Items items(10000);int selected=5000;Frame(items,selected);Frame(items,selected);
    std::vector<double> oldSamples,newSamples;
    for(int trial=0;trial<7;++trial){
        auto start=steady_clock::now();items.calls=0;
        for(int frame=0;frame<100;++frame)Frame(items,selected,Mode::Previous);
        oldSamples.push_back(duration<double,std::micro>(steady_clock::now()-start).count()/100);
        Check(items.calls==1000000,"Previous wrapper visits every source item");
        start=steady_clock::now();items.calls=0;
        for(int frame=0;frame<100;++frame)Frame(items,selected);
        newSamples.push_back(duration<double,std::micro>(steady_clock::now()-start).count()/100);
        Check(items.calls==100,"Lazy wrapper visits selected preview only");
    }
    std::sort(oldSamples.begin(),oldSamples.end());std::sort(newSamples.begin(),newSamples.end());
    std::cout<<"Closed 10000-entry dropdown median per headless frame: previous "<<oldSamples[3]<<" us; lazy "<<newSamples[3]<<" us.\n";
    ImGui::DestroyContext();
}
int main(int argc,char** argv){try{Check(argc==2,"Expected read-only repository root");const auto corpus=FormatTests(argv[1]);FormatBenchmark(corpus);Controls();ComboBenchmark();std::cout<<checks<<" localized-control checks passed.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
