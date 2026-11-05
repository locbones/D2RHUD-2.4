#define NOMINMAX
#include "../d2rhud/plugin/MarketplaceStash.h"
#include "../d2rhud/plugin/MarketplaceStore.h"
#include "../d2rhud/plugin/MarketplaceWorkspace.h"
#include <cassert>
#include <iostream>
#include <set>

using namespace Marketplace;
namespace {
template<class F>void Refuses(F action){bool refused=false;try{action();}catch(const std::exception&){refused=true;}assert(refused);}
struct Writer {
    std::vector<uint8_t> bytes;size_t pos=0;
    void Add(uint32_t value,unsigned count){for(unsigned i=0;i<count;++i){if(pos%8==0)bytes.push_back(0);bytes.back()|=uint8_t(((value>>i)&1)<<(pos%8));++pos;}}
    void Code(const char* code){for(unsigned i=0;i<4;++i)for(char b:CandyCornSaveData::Detail::Codes[uint8_t(code[i])])Add(b-'0',1);}
    void Align(){while(pos%8)Add(0,1);}
};
std::vector<uint8_t> Rune(unsigned x=0){Writer w;w.Add((1u<<21)|(1u<<4),32);w.Add(0,3);w.Add(0,3);w.Add(0,4);w.Add(x,4);w.Add(1,4);w.Add(5,3);w.Code("r33 ");w.Align();return w.bytes;}
std::vector<uint8_t> Unique(bool socketed=false,unsigned index=42){
    Writer w;w.Add((1u<<4)|(socketed?(1u<<11):0)|(1u<<22),32);w.Add(0,3);w.Add(0,3);w.Add(0,4);w.Add(2,4);w.Add(3,4);w.Add(5,3);w.Code("rin ");
    w.Add(socketed?1:0,3);w.Add(0x12345678,32);w.Add(85,7);w.Add(7,4);w.Add(0,1);w.Add(0,1);w.Add(index,12);w.Add(0,1);
    if(socketed)w.Add(1,4);w.Add(39,9);w.Add(60,8);w.Add(511,9);w.Align();
    if(socketed){const auto child=Rune();w.bytes.insert(w.bytes.end(),child.begin(),child.end());}return w.bytes;
}
std::vector<uint8_t> Tab(const std::vector<std::vector<uint8_t>>& items){
    std::vector<uint8_t> bytes(68);auto put=[&](size_t at,uint32_t v){CandyCornSaveData::Detail::Store(bytes,at,v);};
    put(0,0xaa55aa55);put(4,1);put(8,98);put(12,123456);
    for(size_t at=20;at<64;++at)bytes[at]=uint8_t(at);
    bytes[64]='J';bytes[65]='M';bytes[66]=uint8_t(items.size());bytes[67]=uint8_t(items.size()>>8);
    for(const auto& item:items)bytes.insert(bytes.end(),item.begin(),item.end());put(16,uint32_t(bytes.size()));return bytes;
}
std::vector<uint8_t> Page(){std::vector<uint8_t> bytes;for(unsigned tab=0;tab<7;++tab){const auto part=tab==2?Tab({Unique(true),Rune(4)}):Tab({});bytes.insert(bytes.end(),part.begin(),part.end());}return bytes;}
SaveTables Tables(){SaveTables t;t.items={{{'r','3','3',' '}},{{'r','i','n',' '}}};t.stats.resize(512);t.stats[39]={8,0,0,10,true};t.stats[194]={4,0,0,0,true};return t;}
void Prices(){
    const std::vector<Requirement> open={{"open","Open to Offers",2,-1,1,-1,"open",{}}};
    assert(IsOpen(ReadPrice(PriceJson(open)))&&!CanPay(open,{}));auto invalidOpen=open;invalidOpen[0].quantity=2;assert(!Validate(invalidOpen).empty());
    invalidOpen=open;invalidOpen.push_back({"gld","Gold",2,-1,1,-1,"gold",{}});assert(!Validate(invalidOpen).empty());
    const std::vector<Requirement> price={{"rin","Unique A",7,42,1,-1},{"rin","Unique B",7,43,1,-1},{"r33","Zod Rune",2,-1,40,-1}};
    assert(Validate(price).empty());assert(ReadPrice(PriceJson(price)).size()==3);
    std::vector<PaymentItem> payment={{"rin",7,42,1,true,false},{"rin",7,43,1,true,true},{"r33 ",2,-1,40,true,false}};
    assert(CanPay(price,payment));payment.back().quantity=39;assert(!CanPay(price,payment));payment.back().quantity=40;
    payment[1].index=42;assert(!CanPay(price,payment));payment[1].index=43;payment[0].identified=false;assert(!CanPay(price,payment));
    payment[0].identified=true;auto restricted=price;restricted[1].ethereal=0;assert(!CanPay(restricted,payment));restricted[1].ethereal=1;assert(CanPay(restricted,payment));
    auto duplicate=price;duplicate.push_back(price.back());assert(!Validate(duplicate).empty());
    auto invalid=price;invalid[0].quantity=0;assert(!Validate(invalid).empty());invalid[0].quantity=MaxQuantity+1;assert(!Validate(invalid).empty());
    auto json=PriceJson(price);json[0]["quantity"]=1.5;Refuses([&]{ReadPrice(json);});json[0]["quantity"]=uint64_t(-1);Refuses([&]{ReadPrice(json);});
    auto overlap=std::vector<Requirement>{{"rin","A",7,42,1,-1},{"rin","A",7,42,1,0}};assert(!Validate(overlap).empty());
    overlap[0].ethereal=1;assert(Validate(overlap).empty());assert(!CanPay({},payment));
    for(unsigned cls=0;cls<7;++cls){
        Writer w;w.Add(1u<<4,32);w.Add(0,3);w.Add(0,3);w.Add(0,4);w.Add(0,4);w.Add(0,4);w.Add(5,3);w.Code("cm2 ");
        w.Add(0,3);w.Add(0x13572468+cls,32);w.Add(99,7);w.Add(7,4);w.Add(0,1);w.Add(0,1);w.Add(556,12);w.Add(0,1);
        w.Add(83,9);w.Add(cls,3);w.Add(3,3);w.Add(511,9);w.Align();
        auto t=Tables();t.items.push_back({{'c','m','2',' '}});std::sort(t.items.begin(),t.items.end(),[](const auto& a,const auto& b){return a.code<b.code;});t.stats[83]={3,3,0,0,true};
        const auto parsed=ReadItem(w.bytes,t,1);assert(parsed.Payment().classId==int(cls)&&parsed.itemHex==Hex(w.bytes));
    }
    Requirement torch{"cm2","Hellfire Torch",7,556,1,-1};torch.classId=1;
    assert(ReadPrice(PriceJson({torch}))[0].classId==1&&RequirementName(torch)=="Hellfire Torch (Sorceress)");
    assert(!PriceJson({price.back()})[0].contains("class_id"));
    SavedItem rolled;rolled.code="cm2";rolled.quality=7;rolled.index=556;rolled.flags=1u<<4;
    rolled.properties={{{"id",83},{"param",2},{"value",3},{"list","base"}}};
    assert(!CanPay({torch},{rolled.Payment()}));rolled.properties[0]["param"]=1;assert(CanPay({torch},{rolled.Payment()}));
    auto necro=torch;necro.classId=2;assert(Validate({torch,necro}).empty());
    assert(!CanPay({torch,necro},{rolled.Payment()}));auto necroRoll=rolled;necroRoll.properties[0]["param"]=2;
    assert(CanPay({torch,necro},{rolled.Payment(),necroRoll.Payment()}));
    auto anyTorch=torch;anyTorch.classId=-1;assert(!Validate({anyTorch,torch}).empty());assert(!Validate({torch,torch}).empty());
    assert(CanPay({anyTorch},{necroRoll.Payment()}));
    rolled.properties[0]["list"]="set";assert(!CanPay({torch},{rolled.Payment()}));
    rolled.properties[0]["list"]="base";rolled.properties[0]["value"]=0;assert(!CanPay({torch},{rolled.Payment()}));
    rolled.properties[0]["value"]=3;rolled.properties.push_back({{"id",83},{"param",2},{"value",3},{"list","base"}});assert(!CanPay({torch},{rolled.Payment()}));
    rolled.code="rin";assert(rolled.Payment().classId==-1);rolled.code="cm2";
    auto badClass=PriceJson({torch});for(auto value:{Json(7),Json(-2),Json(true),Json(1.5),Json("1")}){badClass[0]["class_id"]=value;Refuses([&]{ReadPrice(badClass);});}
    auto badTorch=torch;badTorch.code="rin";assert(!Validate({badTorch}).empty());
    assert(SimulationAccepts({torch},{torch})&&!SimulationAccepts({torch},{necro}));
    std::cout<<"Exact unique IDs, AND bundles, 40-unit stacks, ethereal/identified rules and malformed/overlapping prices passed.\n";
}
void Resources(){
    const Requirement gems{"Z01","Gems (bag)",2,-1,40,-1,"bag","GB_Total"};
    const Requirement runes{"Z01","Low rune points (bag)",2,-1,500,-1,"bag","RB_Low"};
    const Requirement gold{"gld","Gold (deposit page)",2,-1,1500000,-1,"gold"};
    const std::vector<Requirement> asking{gems,runes,gold};
    assert(Validate(asking).empty());const auto roundTrip=ReadPrice(PriceJson(asking));
    assert(roundTrip[0].stat=="GB_Total"&&roundTrip[2].kind=="gold"&&roundTrip[2].quantity==1500000);
    SavedItem bag;bag.code="Z01";bag.flags=1<<4;bag.tab=3;bag.itemHex="ab";
    SaveTables tables;tables.bagStats={{"GB_Total",100},{"RB_Low",101}};
    bag.properties={{{"id",100},{"list","base"},{"param",0},{"value",40}},{{"id",101},{"list","base"},{"param",0},{"value",500}}};
    ReadBagBalances(bag,tables);assert(bag.balances.size()==2);Settings settings;
    auto encodedTables=Tables();encodedTables.bagStats=tables.bagStats;
    encodedTables.items.insert(encodedTables.items.begin(),{{'Z','0','1',' '}});
    encodedTables.stats[100]={12,0,0,5,true};encodedTables.stats[101]={20,0,0,10,true};
    Writer encoded;encoded.Add(1<<4,32);encoded.Add(0,3);encoded.Add(0,3);encoded.Add(0,4);encoded.Add(0,4);encoded.Add(0,4);encoded.Add(5,3);encoded.Code("Z01 ");
    encoded.Add(0,3);encoded.Add(123,32);encoded.Add(1,7);encoded.Add(2,4);encoded.Add(0,1);encoded.Add(0,1);encoded.Add(0,1);
    encoded.Add(100,9);encoded.Add(45,12);encoded.Add(101,9);encoded.Add(510,20);encoded.Add(511,9);encoded.Align();
    const auto parsedBag=ReadItem(encoded.bytes,encodedTables,3);
    assert(parsedBag.balances.size()==2&&CanPay({gems,runes},Spendable({parsedBag},settings)));
    assert(CanPay(asking,Spendable({bag},settings,1500000)));
    assert(!CanPay(asking,Spendable({bag},settings,1499999)));
    auto changed=asking;changed[0].quantity=41;assert(!CanPay(changed,Spendable({bag},settings,1500000)));
    changed=asking;changed.push_back(gems);assert(!Validate(changed).empty());
    changed=asking;changed.push_back({"Z01","Storage bag",2,-1,1,-1});assert(!Validate(changed).empty());
    changed=asking;changed[0].stat="untrusted_stat";assert(!Validate(changed).empty());
    changed=asking;changed[0].ethereal=0;assert(!Validate(changed).empty());
    changed=asking;changed[2].kind="item";assert(!Validate(changed).empty());
    auto otherTier=asking;otherTier[1].stat="RB_Mid";assert(!SimulationAccepts(asking,otherTier));
    assert(SimulationAccepts(asking,roundTrip));
    settings.drafts={{3,"ab",{gold}}};assert(!CanPay(asking,Spendable({bag},settings,1500000))); // Selling a bag reserves its contents too.
    bag.properties.push_back(bag.properties[0]);ReadBagBalances(bag,tables);assert(bag.balances.empty());
    bag.properties.erase(bag.properties.end()-1);bag.properties[1]["value"]=-1;ReadBagBalances(bag,tables);assert(bag.balances.empty());
    uint64_t savedGold=0;ReadStash(Page(),Tables(),&savedGold);assert(savedGold==7*123456);
    std::cout<<"Typed gold/bag prices, exact saved balance units, insufficient funds, reserved bags, ambiguity and round trips passed.\n";
}
void Stashes(){
    const auto page=Page(),before=page;const auto items=ReadStash(page,Tables());assert(page==before&&items.size()==2);
    const auto& unique=items[0];assert(unique.tab==3&&unique.ordinal==1&&unique.x==2&&unique.y==3&&unique.container==5);
    assert(unique.id==0x12345678&&unique.index==42&&unique.level==85&&unique.quality==7&&unique.Payment().ethereal);
    assert(unique.children.size()==1&&unique.itemHex==Hex(Unique(true)));assert(items[1].itemHex==Hex(Rune(4)));
    assert(unique.properties[0]["id"]==39&&unique.properties[0]["raw"]==60&&unique.properties[0]["value"]==50);
    assert(unique.children[0]["code"]=="r33"&&items[1].code=="r33"&&items[1].quantity==1);
    for(size_t n=0;n<page.size();++n)Refuses([&]{ReadStash(std::span(page).first(n),Tables());});
    auto bad=page;bad[8]=99;Refuses([&]{ReadStash(bad,Tables());});bad=page;bad[16]=0;Refuses([&]{ReadStash(bad,Tables());});
    auto missingChild=Unique(true);const auto child=Rune();missingChild.resize(missingChild.size()-child.size());
    auto broken=Tab({missingChild});for(int i=1;i<7;++i){auto tab=Tab({});broken.insert(broken.end(),tab.begin(),tab.end());}Refuses([&]{ReadStash(broken,Tables());});
    auto extra=Tab({});bad=page;bad.insert(bad.end(),extra.begin(),extra.end());Refuses([&]{ReadStash(bad,Tables());});
    auto unsupported=Tables();unsupported.items.erase(unsupported.items.begin()+1);Refuses([&]{ReadStash(page,unsupported);});
    std::cout<<"Seven tabs, exact parent/socket bytes, identity, saved properties, every truncated prefix and unsupported layouts passed.\n";
}
void Storage(const std::filesystem::path& root){
    std::filesystem::create_directories(root);const auto path=root/L"Marketplace_2.4_SC.json";
    Settings settings;settings.enabled=true;settings.page=17;settings.drafts={{3,Hex(Unique()),{{"r33","Zod Rune",2,-1,40,-1}}}};
    const auto json=SettingsJson(settings,"RMD-MP",false);const auto next=json.dump(2);SaveSettingsFile(path,"",next);
    const auto read=ReadSettings(Json::parse(ReadSettingsFile(path)),"RMD-MP",false);assert(read.page==17&&read.drafts[0].price[0].quantity==40);
    SaveSettingsFile(path,next,next);Refuses([&]{SaveSettingsFile(path,"",next+" ");});assert(ReadSettingsFile(path)==next);
    Refuses([&]{ReadSettings(json,"OtherMod",false);});Refuses([&]{ReadSettings(json,"RMD-MP",true);});
    auto bad=json;bad["page"]=uint64_t(-1);Refuses([&]{ReadSettings(bad,"RMD-MP",false);});
    bad=json;bad["drafts"][0]["item_hex"]="zz";Refuses([&]{ReadSettings(bad,"RMD-MP",false);});
    bad=json;bad["drafts"].push_back(bad["drafts"][0]);Refuses([&]{ReadSettings(bad,"RMD-MP",false);});
    Refuses([&]{SaveSettingsFile(root/L"never.d2i","","data");});assert(!std::filesystem::exists(root/L"never.d2i"));
    auto lock=path;lock+=L".lock";HANDLE lease=CreateFileW(lock.c_str(),GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);assert(lease!=INVALID_HANDLE_VALUE);
    Refuses([&]{SaveSettingsFile(path,next,next+" ");});CloseHandle(lease);assert(ReadSettingsFile(path)==next);
    std::cout<<"Draft round trips, mode separation, atomic sidecars, stale writer/lease refusal and save write rejection passed.\n";
}
Json FeedFixture(){return {{"schema",Schema},{"game_mode","2.4"},{"mod","MarketplaceFixture"},{"hardcore",false},{"expires_at",UnixNow()+3600},
    {"listings",Json::array({{{"seller","Trader A"},{"id","ring-1"},{"revision","revision-1"},{"item_hex",Hex(Unique(false,43))},
        {"price",PriceJson({{"r33","Zod Rune",2,-1,1,-1}})}}})}};}
void Workspace(){
    const auto source=FeedFixture();const auto feed=ReadFeed(source,"MarketplaceFixture",false,Tables(),UnixNow());assert(feed.listings.size()==1);
    const auto& listing=feed.listings[0];assert(listing.item.index==43&&listing.item.quality==7);
    AgreementDraft agreement{listing.seller,listing.id,listing.revision,listing.item.itemHex,listing.price};assert(SameTerms(agreement,listing));
    auto changed=listing;changed.revision="revision-2";assert(!SameTerms(agreement,changed));changed=listing;changed.price[0].quantity=2;assert(!SameTerms(agreement,changed));
    auto settings=Settings{};settings.enabled=true;settings.autoTrade=true;settings.page=17;settings.withdrawPage=18;settings.agreements={agreement};
    const auto document=SettingsJson(settings,"MarketplaceFixture",false);const auto back=ReadSettings(document,"MarketplaceFixture",false);
    assert(back.autoTrade&&back.withdrawPage==17&&back.agreements.size()==1&&SameTerms(back.agreements[0],listing));
    auto old=document;old.erase("withdraw_page");old.erase("auto_trade");old.erase("agreements");const auto legacy=ReadSettings(old,"MarketplaceFixture",false);
    assert(legacy.withdrawPage==17&&!legacy.autoTrade&&legacy.agreements.empty());
    auto bad=document;bad["withdraw_page"]=17;assert(ReadSettings(bad,"MarketplaceFixture",false).withdrawPage==17);
    bad=document;bad["auto_trade"]=1;Refuses([&]{ReadSettings(bad,"MarketplaceFixture",false);});
    bad=document;bad["agreements"].push_back(bad["agreements"][0]);Refuses([&]{ReadSettings(bad,"MarketplaceFixture",false);});
    bad=source;bad["listings"].push_back(bad["listings"][0]);Refuses([&]{ReadFeed(bad,"MarketplaceFixture",false,Tables(),UnixNow());});
    bad=source;bad["expires_at"]=UnixNow()-1;Refuses([&]{ReadFeed(bad,"MarketplaceFixture",false,Tables(),UnixNow());});
    bad=source;bad["expires_at"]=UnixNow()+86401;Refuses([&]{ReadFeed(bad,"MarketplaceFixture",false,Tables(),UnixNow());});
    bad=source;bad["listings"][0]["item_hex"]="00";Refuses([&]{ReadFeed(bad,"MarketplaceFixture",false,Tables(),UnixNow());});
    Refuses([&]{ReadFeed(source,"Other",false,Tables(),UnixNow());});Refuses([&]{ReadFeed(source,"MarketplaceFixture",true,Tables(),UnixNow());});
    const auto items=ReadStash(Page(),Tables());assert(CanPay(listing.price,Spendable(items,settings)));
    settings.drafts={{3,items[1].itemHex,listing.price}};assert(!CanPay(listing.price,Spendable(items,settings))); // listed payment unavailable
    settings.drafts={{3,items[0].itemHex,listing.price}};assert(Spendable(items,settings).size()==1); // socketed rune not spendable
    settings.drafts.clear();assert(Spendable(items,settings).size()==2);auto two=listing.price;two[0].quantity=2;assert(!CanPay(two,Spendable(items,settings)));
    std::cout<<"Routing migration, approval policy, exact agreement revisions, cache expiry/mode/identity guards and listed/socketed payment exclusion passed.\n";
}
void OwnListings(){
    auto inventory=ReadStash(Page(),Tables());Settings settings;settings.enabled=true;settings.page=11;
    settings.drafts={{inventory[0].tab,inventory[0].itemHex,{{"r33","Zod Rune",2,-1,40,-1}}}};
    auto feed=ReadFeed(FeedFixture(),"MarketplaceFixture",false,Tables(),UnixNow());
    AppendOwnListings(feed,inventory,settings,true);assert(feed.listings.size()==2&&feed.listings.back().own&&!feed.listings.back().simulated);
    const auto listing=feed.listings.back();assert(listing.item.itemHex==inventory[0].itemHex&&listing.price[0].quantity==40&&listing.id.starts_with("local-11-"));
    assert(ListingFresh(Feed{},listing,UnixNow())&&!ListingFresh(Feed{},feed.listings.front(),UnixNow()));
    AgreementDraft fake{listing.seller,listing.id,listing.revision,listing.item.itemHex,listing.price};assert(!SameTerms(fake,listing));
    AppendOwnListings(feed,inventory,settings,true);assert(feed.listings.size()==2); // Rebuild never duplicates.
    settings.drafts[0].price[0].quantity=1;AppendOwnListings(feed,inventory,settings,true);assert(feed.listings.back().revision!=listing.revision&&feed.listings.back().price[0].quantity==1);
    AppendOwnListings(feed,{},settings,true);assert(feed.listings.size()==1&&!feed.listings.front().own); // Missing exact item is unlisted.
    AppendOwnListings(feed,inventory,settings,true);AppendOwnListings(feed,inventory,settings,false);assert(feed.listings.size()==1);
    settings.drafts.clear();AppendOwnListings(feed,inventory,settings,true);assert(feed.listings.size()==1);
    std::cout<<"Newly priced deposit items appear locally in Browse, update/remove without a cache, cannot match purchases, and preserve player offers.\n";
}
void Simulation(const std::filesystem::path& root){
    Settings settings;settings.enabled=true;settings.page=17;settings.withdrawPage=18;settings.simulation={true,19,{}};
    const auto page=Page();for(auto n:{17,19}){std::ofstream file(root/(L"Stash_SC_Page"+std::to_wstring(n)+L".d2i"),std::ios::binary);file.write(reinterpret_cast<const char*>(page.data()),page.size());}
    auto snapshot=ReadWorkspace(root,settings,"MarketplaceFixture",false,Tables());
    assert(snapshot.depositValid&&snapshot.sellerValid&&snapshot.seller.size()==2&&snapshot.feed.listings.empty());
    const auto item=snapshot.seller[0];const std::vector<Requirement> asking{{"r33","Zod Rune",2,-1,2,-1}};
    settings.simulation.drafts={{item.tab,item.itemHex,asking}};
    snapshot=ReadWorkspace(root,settings,"MarketplaceFixture",false,Tables());assert(snapshot.feed.listings.size()==1);
    const auto listing=snapshot.feed.listings[0];assert(listing.simulated&&listing.item.itemHex==item.itemHex&&listing.price[0].quantity==2);
    assert(ListingFresh(snapshot.feed,listing,UnixNow())&&!ListingFresh(snapshot.feed,Listing{},UnixNow()));
    assert(Spendable(snapshot.deposit,settings).size()==2); // Seller prices never reserve player deposit.
    settings.agreements={{listing.seller,listing.id,listing.revision,listing.item.itemHex,listing.price,true}};
    const auto json=SettingsJson(settings,"MarketplaceFixture",false);const auto back=ReadSettings(json,"MarketplaceFixture",false);
    assert(back.simulation.enabled&&back.simulation.page==19&&back.simulation.drafts.size()==1&&back.agreements[0].simulated&&SameTerms(back.agreements[0],listing));
    auto changed=listing;changed.simulated=false;assert(!SameTerms(back.agreements[0],changed));
    settings.simulation.drafts[0].price[0].quantity=1;AppendSimulation(snapshot.feed,snapshot.seller,settings,true);
    assert(snapshot.feed.listings.size()==1&&!SameTerms(back.agreements[0],snapshot.feed.listings[0]));
    auto offer=asking;assert(SimulationAccepts(asking,offer));offer[0].quantity=1;assert(!SimulationAccepts(asking,offer));
    offer=asking;offer[0].label="Localized name";assert(SimulationAccepts(asking,offer));offer[0].ethereal=0;assert(!SimulationAccepts(asking,offer));
    AppendSimulation(snapshot.feed,snapshot.seller,settings,false);assert(snapshot.feed.listings.empty());
    settings.simulation.enabled=false;AppendSimulation(snapshot.feed,snapshot.seller,settings,true);assert(snapshot.feed.listings.empty());
    settings.simulation.enabled=true;settings.simulation.page=17;Refuses([&]{SettingsJson(settings,"MarketplaceFixture",false);});
    auto bad=json;bad["simulation"]["enabled"]=1;Refuses([&]{ReadSettings(bad,"MarketplaceFixture",false);});
    bad=json;bad["simulation"]["page"]=65;Refuses([&]{ReadSettings(bad,"MarketplaceFixture",false);});
    auto legacy=json;legacy.erase("simulation");assert(!ReadSettings(legacy,"MarketplaceFixture",false).simulation.enabled);
    assert(ReadFile(root/L"Stash_SC_Page17.d2i")==page&&ReadFile(root/L"Stash_SC_Page19.d2i")==page);
    std::cout<<"Manual simulated-seller prices, route isolation, provenance, revisions, strict counter-offers, expiry isolation and unchanged saves passed.\n";
}
void Real(const std::filesystem::path& excel,const std::filesystem::path& saves){
    const auto tables=LoadSaveTables([&](const std::string& name){return ReadFile(excel/(name+".bin"),64*1024*1024);});
    size_t pages=0,items=0,refused=0;
    for(const auto& entry:std::filesystem::directory_iterator(saves)){
        const auto name=entry.path().filename().wstring();
        if(entry.path().extension()!=L".d2i"||!(name.starts_with(L"Stash_SC_Page")||name.starts_with(L"Stash_HC_Page")))continue;
        const auto before=ReadFile(entry.path());
        try{const auto read=ReadStash(before,tables);++pages;items+=read.size();for(const auto& item:read)assert(!item.itemHex.empty());}
        catch(const std::exception& e){++refused;std::cout<<"Refused "<<entry.path().filename().string()<<": "<<e.what()<<'\n';}
        assert(ReadFile(entry.path())==before);
    }
    assert(pages>0);std::cout<<"Read-only real-save audit: "<<pages<<" pages, "<<items<<" top-level items, "<<refused<<" safely refused. No source saves written.\n";
}
}
int wmain(int argc,wchar_t** argv){
    assert(argc==2||argc==4);const auto root=std::filesystem::path(argv[1])/(L"run-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    Prices();Resources();Stashes();Storage(root);Workspace();OwnListings();Simulation(root);if(argc==4)Real(argv[2],argv[3]);std::cout<<"Marketplace tests passed.\n";return 0;
}
