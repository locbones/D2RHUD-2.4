#define NOMINMAX
#include "../d2rhud/plugin/RunTrackerAnalysis.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <chrono>
using namespace RunTracker;
using namespace RunTracker::Analysis;
constexpr int64_t Now=1793600000;
Item Equipment(){Item i;i.id=1;i.code="7gd";i.name="The Grandfather";i.quality=7;i.fileIndex=222;i.sockets=0;i.stackableKnown=true;return i;}
Json Payment(int amount=100,const char* code="gld",const char* kind="gold"){return {{"code",code},{"kind",kind},{"quantity",amount},{"quality",2},{"index",-1},{"ethereal",-1},{"label",code}};}
Json Trade(const char* id,int price=100){return {{"id",id},{"kind","completed_sale"},{"code","7gd"},{"quality",7},{"item_index",222},{"ethereal",0},{"sockets",0},{"at",Now-60},{"price",Json::array({Payment(price)})}};}
Json Envelope(Json rows){return {{"schema",1},{"game_mode","2.4"},{"mod","Fixture"},{"hardcore",false},{"expires_at",Now-1},{"history",rows}};}
Sources Data(){Sources s;s.mod="Fixture";s.bases["7gd"]={"Weapons","swor"};s.bases["r30"]={"Runes","rune"};s.bases["cap"]={"Armor","helm"};s.treasures["Root"]={2,3,{{"Branch",2},{"cap",2}}};s.treasures["Branch"]={0,1,{{"7gd",1},{"r30",3}}};return s;}
Input Finds(){Input in;in.runs=1;auto item=Equipment();in.entries.push_back({item,108,"run","Hero",Now,true,false,"Fixture"});item.code="r30";item.name="Ber Rune";item.quality=2;item.fileIndex=-1;item.quantity=9;item.stackable=true;in.entries.push_back({item,108,"run","Hero",Now,true,false,"Fixture"});in.activeMs[108]=3600000;in.visits[108]=2;in.activeMs[2]=60000;in.visits[2]=1;return in;}
void MarketTests(){
    auto s=Data();auto rows=Json::array({Trade("one",100),Trade("two",200),Trade("three",300),Trade("four",400)});
    auto mixed=Trade("mixed");mixed["price"].push_back(Payment(1,"r33","item"));rows.push_back(mixed);
    auto free=Trade("free");free["price"][0]["kind"]="free";rows.push_back(free);
    auto bundle=Trade("bundle");bundle["bundle"]={{"item_count",2}};rows.push_back(bundle);
    auto bad=Trade("future");bad["at"]=Now+1;rows.push_back(bad);bad=Trade("eth");bad["ethereal"]=5;rows.push_back(bad);
    bad=Trade("simulation");bad["simulated"]=true;rows.push_back(bad);rows.push_back(rows[0]);rows.push_back(Json::object());
    auto other=Trade("other",5);other["price"]=Json::array({Payment(5,"r33","item")});rows.push_back(other);
    ReadMarket(s,Envelope(rows),"Fixture",false,Now);assert(s.sales.size()==5&&s.currencies.size()==2&&s.ignoredSales==8);assert(s.marketStatus.find("stale")!=std::string::npos);
    Options o;o.currency=s.sales[0].currency;auto in=Finds();auto report=Build(in,s,o,Now);assert(report.valuable.size()==1&&report.valuable[0].unit==250&&report.valuable[0].samples==4&&report.unpriced==1);
    o.minSamples=5;assert(Build(in,s,o,Now).valuable.empty());o.minSamples=1;o.days=1;
    for(auto& sale:s.sales)sale.at=Now-86401;assert(Build(in,s,o,Now).valuable.empty());
    bool rejected=false;try{ReadMarket(s,Envelope(rows),"Another",false,Now);}catch(...){rejected=true;}assert(rejected);
    rejected=false;try{ReadMarket(s,Envelope(rows),"Fixture",true,Now);}catch(...){rejected=true;}assert(rejected);
}
void ComparisonTests(){
    auto s=Data();auto in=Finds();auto item=Equipment();s.sales.push_back({Identity(item),"gold","Gold",100,0,Now-60});Options o;o.currency="gold";o.minSamples=1;
    assert(Build(in,s,o,Now).valuable.size()==1);
    for(int variant=0;variant<5;++variant){auto copy=in;if(variant==0)copy.entries[0].modeKnown=false;if(variant==1)copy.entries[0].hardcore=true;if(variant==2)copy.entries[0].mod="Other";if(variant==3)copy.entries[0].item.sockets=-1;if(variant==4)copy.entries[0].item.stackableKnown=false;assert(Build(copy,s,o,Now).valuable.empty());}
    in.entries[0].item.quality=6;s.sales[0].identity=Identity(in.entries[0].item);assert(Build(in,s,o,Now).valuable.empty());o.broadQualities=true;assert(Build(in,s,o,Now).valuable.size()==1);
    s.sales.clear();s.sales.push_back({Identity(in.entries[1].item),"gold","Gold",300,0,Now-60});assert(Build(in,s,o,Now).valuable.empty());s.sales[0].quantity=3;auto r=Build(in,s,o,Now);assert(r.valuable.size()==1&&r.valuable[0].unit==100&&r.valuable[0].total==900);
    // Both shares use the same denominator; areas with no acquisitions remain visible.
    assert(r.categories.size()==2&&r.categories[0].percent==50&&r.units==10&&r.areas.size()==2);o.units=true;r=Build(in,s,o,Now);assert(r.categories[0].name=="Runes"&&r.categories[0].percent==90);
    double percent=0;for(const auto& share:r.categories)percent+=share.percent;assert(std::abs(percent-100)<1e-9);
    for(const auto& area:r.areas)if(area.id==108)assert(area.priced==1&&area.entries==2&&area.estimate==900&&area.activeMs==3600000&&area.visits==2);
}
void RarityTests(){
    auto s=Data();auto w=TableWeight(s,"Root","7gd");assert(w.complete&&std::abs(w.value-1./12)<1e-9);
    s.namedCodes["Grandfather"]="7gd";s.treasures["Alias"]={0,1,{{"Grandfather",1}}};assert(TableWeight(s,"Alias","7gd").value==1);
    s.treasures["Cycle"]={0,1,{{"Cycle",1},{"7gd",1}}};assert(!TableWeight(s,"Cycle","7gd").complete);
    s.treasures["Bad"]={0,-2,{{"7gd",1}}};assert(!TableWeight(s,"Bad","7gd").complete);
    s.treasures["Generated"]={0,1,{{"weap87",1},{"7gd",1}}};assert(!TableWeight(s,"Generated","7gd").complete);
    Options o;o.treasure="Root";auto r=Build(Finds(),s,o,Now);assert(r.rarityComplete&&r.rare.size()==2&&r.rare[0].entry.item.code=="7gd");o.treasure="Generated";r=Build(Finds(),s,o,Now);assert(!r.rarityComplete&&r.rare.empty());
}
void BoundsAndCompatibility(){
    Run run;run.id="old-run";run.mod="Fixture";run.startedUtc=Now;run.items.push_back({Equipment(),108,0});auto json=Encode(run);json.erase("modeKnown");json.erase("hardcore");for(auto& item:json["items"]){item.erase("sockets");item.erase("stackableKnown");item.erase("stackable");}Run old;assert(Decode(json,old)&&!old.modeKnown&&old.items[0].item.sockets==-1&&!old.items[0].item.stackableKnown);
    std::vector<const Run*> runs(501,&run);auto gathered=Gather(runs);assert(gathered.runs==500&&gathered.truncated);
    auto in=Finds();for(int i=0;i<1000;++i)in.entries.push_back(in.entries[0]);auto s=Data();Options o;o.treasure="Root";s.sales.push_back({Identity(Equipment()),"gold","Gold",100,1,Now-1});o.currency="gold";o.minSamples=1;auto r=Build(in,s,o,Now);assert(r.valuable.size()==10&&r.rare.size()==10);
    // Representative analysis size remains bounded and works off detached data.
    in.entries.resize(100000,in.entries[0]);const auto start=std::chrono::steady_clock::now();r=Build(in,s,o,Now);std::cout<<"100000-entry analysis: "<<std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count()<<" ms\n";
}
void LoaderTests(const std::filesystem::path& root){
    const auto install=root/"analysis-fixture";const auto excel=install/"Mods/Fixture/Fixture.mpq/data/global/excel";std::filesystem::create_directories(excel);const auto cache=install/"cache";std::filesystem::create_directories(cache);
    std::ofstream(excel/"itemtypes.txt")<<"ItemType\tCode\tEquiv1\nWeapon\tweap\t\nSword\tswor\tweap\nRune\trune\t\n";
    std::ofstream(excel/"weapons.txt")<<"name\tcode\ttype\nSword\t7gd\tswor\n";std::ofstream(excel/"misc.txt")<<"name\tcode\ttype\nBer\tr30\trune\n";
    std::ofstream(excel/"treasureclassex.txt")<<"Treasure Class\tPicks\tNoDrop\tItem1\tProb1\tItem2\tProb2\nRoot\t1\t2\t7gd\t2\tr30\t4\n";
    const auto doc=Envelope(Json::array({Trade("one")})).dump();const auto path=cache/"MarketplaceActivity_2.4_SC.json";std::ofstream(path)<<doc;
    auto s=Load(install,"Fixture",cache,false,Now);assert(s.bases.at("7gd").category=="Weapons"&&s.bases.at("r30").category=="Runes"&&s.treasures.size()==1&&s.sales.size()==1);assert(TableWeight(s,"Root","7gd").value==.25);
    std::ifstream input(path);std::string after((std::istreambuf_iterator<char>(input)),{});assert(after==doc);input.close();
    std::ofstream(path)<<"{bad";s=Load(install,"Fixture",cache,false,Now);assert(s.sales.empty()&&!s.marketStatus.empty()&&s.bases.size()==2);
}
int main(int argc,char** argv){assert(argc>=2);MarketTests();ComparisonTests();RarityTests();BoundsAndCompatibility();LoaderTests(argv[1]);if(argc>2){auto s=Load(argv[2],"RMD-MP",{},false,Now);assert(s.bases.size()>1000&&!s.treasures.empty());std::cout<<"Installed mod catalog: "<<s.bases.size()<<" bases, "<<s.treasures.size()<<" treasure classes\n";}std::cout<<"PASS: market matching, currencies, stale cache, exclusions, medians, stack normalization, mode isolation, composition, area coverage, TC weights and incomplete paths, limits, legacy journals, real asset loader and read-only cache.\n";}
