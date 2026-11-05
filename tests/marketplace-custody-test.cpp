#define wmain ExistingMarketplaceMain
#include "marketplace-test.cpp"
#undef wmain
#include "../tools/marketplace/CustodyPlan.h"
namespace C=Marketplace::Custody;
using C::Bytes;
using C::Read;using C::Write;using C::TakeItem;using C::Parcel;using C::Debit;using C::Deliver;using C::Gold;using C::Plan;using C::DecodePage;using C::Quantity;using C::Bag;using C::Balance;using C::Bits;using C::Load;
int wmain(int argc,wchar_t** argv){
    Marketplace::Custody::Tables tables;tables.save=::Tables();tables.sizes={{"rin",{1,1}},{"r33",{1,1}},{"Z01",{1,1}}};
    // Extended and compact items both carry an optional 128-bit persisted
    // identity. It is independent of the legacy seed and stash coordinates.
    for(bool compact:{false,true}){
        Writer w;w.Add((1u<<4)|(compact?(1u<<21):0),32);w.Add(0,3);w.Add(0,3);w.Add(0,4);w.Add(2,4);w.Add(3,4);w.Add(5,3);w.Code(compact?"r33 ":"rin ");
        if(!compact){w.Add(0,3);w.Add(0,32);w.Add(88,7);w.Add(7,4);w.Add(0,1);w.Add(0,1);w.Add(42,12);}
        w.Add(1,1);for(unsigned n=0;n<16;++n)w.Add(n*17,8);
        if(!compact){w.Add(39,9);w.Add(60,8);w.Add(511,9);}w.Align();
        const auto item=ReadItem(w.bytes,tables.save,1);assert(item.uuid=="00112233445566778899aabbccddeeff"&&item.id==0&&item.ToJson()["item_uuid"]==item.uuid);
        auto moved=w.bytes;Bits(moved,42,4,8);assert(ReadItem(moved,tables.save,7).uuid==item.uuid&&item.itemHex==Hex(w.bytes));
        if(!compact){auto sale=Sale{};sale.uuid=item.uuid;sale.itemId="uuid-ledger";Activity activity;activity.sales.push_back(sale);assert(ItemSales(activity,item,false).size()==1);}
    }
    Settings settings;settings.enabled=true;settings.page=11;settings.withdrawPage=2;
    {
        const auto saved=ReadItem(Unique(true),tables.save,3);const auto live=ReadItem(Unhex(LegacyItemHex(saved.itemHex,saved.children)),tables.save,3);
        assert(live.id==0&&saved.id!=0&&ReconcileLiveIdentity(live,{saved},tables.save).itemHex==saved.itemHex);
        auto movedBytes=Unhex(live.itemHex);Bits(movedBytes,42,4,7);const auto moved=ReconcileLiveIdentity(ReadItem(movedBytes,tables.save,2),{saved},tables.save);
        assert(moved.id==saved.id&&moved.x==7&&moved.tab==2&&moved.properties==saved.properties&&moved.children==saved.children);
        auto other=Unhex(saved.itemHex);CandyCornSaveData::Detail::Budget budget;CandyCornSaveData::Detail::Bits bits{other,53,&budget,true};std::array<char,4> code{};assert(bits.Code(code));bits.Skip(3);Bits(other,bits.pos,32,99);
        assert(ReconcileLiveIdentity(live,{saved,ReadItem(other,tables.save,3)},tables.save).id==0);
        auto nestedBytes=Unique(true);nestedBytes.resize(nestedBytes.size()-Rune().size());const auto child=Unique(false);nestedBytes.insert(nestedBytes.end(),child.begin(),child.end());
        const auto nested=ReadItem(nestedBytes,tables.save,3);const auto zeroed=ReadItem(Unhex(LegacyItemHex(nested.itemHex,nested.children)),tables.save,3);
        assert(zeroed.children[0]["saved_id"]==0&&nested.children[0]["saved_id"]!=0&&ReconcileLiveIdentity(zeroed,{nested},tables.save).itemHex==nested.itemHex);
    }
    const auto raw=Page();const auto original=raw;auto page=Read(raw,tables);assert(Write(page,tables)==raw);
    const auto items=ReadStash(raw,tables.save);Parcel item;TakeItem(page,items[0],item);const auto removed=Write(page,tables);
    assert(ReadStash(removed,tables.save).size()==1&&item.items[0].children.size()==1&&Gold(page)==Gold(Read(raw,tables)));
    Refuses([&]{TakeItem(page,items[0],item);});
    Parcel payment;Debit(page,{{"r33","Zod",2,-1,1,-1},{"gld","Gold",2,-1,200000,-1,"gold"}},settings,tables,payment);
    assert(ReadStash(Write(page,tables),tables.save).empty()&&Gold(page)+200000==Gold(Read(raw,tables)));
    Deliver(page,item,settings,tables,0);assert(ReadStash(Write(page,tables),tables.save)[0].children==items[0].children);
    Deliver(page,payment,settings,tables,2500000);assert(Gold(page)==Gold(Read(raw,tables))&&ReadStash(Write(page,tables),tables.save).size()==2);
    settings.drafts={{3,items[1].itemHex,{{"gld","Gold",2,-1,1,-1,"gold"}}}};auto protectedPage=Read(raw,tables);
    Refuses([&]{Parcel p;Debit(protectedPage,{{"r33","Zod",2,-1,1,-1}},settings,tables,p);});settings.drafts.clear();
    Refuses([&]{auto full=Read(raw,tables);Parcel p;p.balances={{"gld","Gold",2,-1,1,-1,"gold"}};Deliver(full,p,settings,tables,123456);});
    auto bad=Read(raw,tables);bad.tabs[2].push_back(bad.tabs[2][0]);Refuses([&]{Write(bad,tables);});
    Json request={{"schema",1},{"game_mode","2.4"},{"mod","RMD-MP"},{"hardcore",false},{"settings",SettingsJson(settings,"RMD-MP",false)},{"source_hex",Hex(raw)},
        {"take",Json::array({{{"tab",3},{"ordinal",1},{"item_hex",items[0].itemHex}}})}};
    const auto plan=Plan(request,tables);assert(plan["simulation"]==true&&ReadStash(DecodePage(plan["after_hex"]),tables.save).size()==1&&raw==original);
    auto duplicate=request;duplicate["take"].push_back(duplicate["take"][0]);Refuses([&]{Plan(duplicate,tables);});
    auto wrongMode=request;wrongMode["hardcore"]=true;Refuses([&]{Plan(wrongMode,tables);});auto wrongMod=request;wrongMod["mod"]="Other";Refuses([&]{Plan(wrongMod,tables);});
    auto noMoney=request;noMoney["debit"]=PriceJson({{"gld","Gold",2,-1,1000000,-1,"gold"}});Refuses([&]{Plan(noMoney,tables);});
    for(size_t n=0;n<raw.size();++n)Refuses([&]{Read(std::span(raw).first(n),tables);});
    // Partial stack debits preserve all other bits; no duplicated stack units.
    auto stackTables=tables;stackTables.save.items[1].stackable=true;Writer stack;stack.Add(1<<4,32);stack.Add(0,3);stack.Add(0,3);stack.Add(0,4);stack.Add(0,4);stack.Add(0,4);stack.Add(5,3);stack.Code("rin ");
    stack.Add(0,3);stack.Add(77,32);stack.Add(1,7);stack.Add(2,4);stack.Add(0,1);stack.Add(0,1);stack.Add(0,1);stack.Add(40,9);stack.Add(511,9);stack.Align();
    const auto savedStack=ReadItem(stack.bytes,stackTables.save,1);assert(Quantity(savedStack,15,stackTables).quantity==15);
    auto stackPage=Read(raw,tables);for(auto& rows:stackPage.tabs)rows.clear();stackPage.tabs[0]={savedStack};Parcel units;Debit(stackPage,{{"rin","Ring",2,-1,15,-1}},settings,stackTables,units);
    assert(stackPage.tabs[0][0].quantity==25&&units.items[0].quantity==15&&units.items[0].id==savedStack.id);
    // Bag fields use hash-bound symbolic metadata, preserve unrelated fields,
    // and refuse absent/ambiguous stats or bit-width overflow.
    auto bagTables=tables;bagTables.save.items.insert(bagTables.save.items.begin(),{{'Z','0','1',' '}});bagTables.save.bagStats={{"GB_Total",100},{"RB_Low",101}};
    bagTables.save.stats[100]={12,0,0,5,true};bagTables.save.stats[101]={20,0,0,10,true};
    Writer bag;bag.Add(1<<4,32);bag.Add(0,3);bag.Add(0,3);bag.Add(0,4);bag.Add(0,4);bag.Add(0,4);bag.Add(5,3);bag.Code("Z01 ");
    bag.Add(0,3);bag.Add(99,32);bag.Add(1,7);bag.Add(2,4);bag.Add(0,1);bag.Add(0,1);bag.Add(0,1);bag.Add(100,9);bag.Add(45,12);bag.Add(101,9);bag.Add(510,20);bag.Add(511,9);bag.Align();
    const auto savedBag=ReadItem(bag.bytes,bagTables.save,1);auto smaller=Bag(savedBag,"GB_Total",15,bagTables);
    assert(Balance(smaller,"GB_Total")==15&&Balance(smaller,"RB_Low")==500&&Bag(savedBag,"GB_Total",0,bagTables).properties.size()==2);
    Refuses([&]{Bag(savedBag,"GB_Total",4091,bagTables);});Refuses([&]{Bag(savedBag,"GB_Missing",1,bagTables);});
    auto bagPage=Read(raw,tables);for(auto& rows:bagPage.tabs)rows.clear();bagPage.tabs[0]={savedBag};Parcel gems;
    Debit(bagPage,{{"Z01","Gems",2,-1,25,-1,"bag","GB_Total"}},settings,bagTables,gems);assert(Balance(bagPage.tabs[0][0],"GB_Total")==15);
    settings.drafts={{1,bagPage.tabs[0][0].itemHex,{{"r33","Zod",2,-1,1,-1}}}}; // Same bytes on a deposit page cannot reserve a delivery bag.
    Deliver(bagPage,gems,settings,bagTables,0);assert(bagPage.tabs[0][0].itemHex==savedBag.itemHex);
    // Single-page delivery creates its own currency bag, with no pre-existing
    // container, a stable claim UUID and only the requested currency amounts.
    Settings onePage;onePage.enabled=true;onePage.page=onePage.withdrawPage=11;
    const auto oneJson=SettingsJson(onePage,"RMD-MP",false);assert(oneJson.at("withdraw_page")==11&&ReadSettings(oneJson,"RMD-MP",false).withdrawPage==11);
    auto parcelPage=Read(raw,tables);for(auto& rows:parcelPage.tabs)rows.clear();
    const auto parcelBefore=Write(parcelPage,bagTables);const std::string parcelId="0123456789abcdef0123456789abcdef";
    Json parcelRequest={{"schema",1},{"game_mode","2.4"},{"mod","RMD-MP"},{"hardcore",false},{"settings",oneJson},{"source_hex",Hex(parcelBefore)},
        {"currency_bag_id",parcelId},{"credit_balances",PriceJson({{"Z01","Gems",2,-1,25,-1,"bag","GB_Total"},{"Z01","Runes",2,-1,100,-1,"bag","RB_Low"}})}};
    const auto mint=Plan(parcelRequest,bagTables);const auto minted=ReadStash(DecodePage(mint.at("after_hex")),bagTables.save);
    assert(minted.size()==1&&minted[0].uuid==parcelId&&Balance(minted[0],"GB_Total")==25&&Balance(minted[0],"RB_Low")==100);
    assert(mint.at("currency_bags").size()==1&&mint.at("delivered").empty()&&Plan(parcelRequest,bagTables)==mint);
    auto otherParcel=parcelRequest;otherParcel["currency_bag_id"]="abcdef0123456789abcdef0123456789";assert(Plan(otherParcel,bagTables).at("after_hex")!=mint.at("after_hex"));
    auto multiple=parcelRequest;multiple["credit_balances"][0]["quantity"]=9000;const auto many=Plan(multiple,bagTables);assert(many.at("currency_bags").size()==3);
    uint64_t total=0;std::set<std::string> parcelUuids;for(const auto& row:many.at("currency_bags")){const auto bag=ReadItem(C::Decode(row.at("item_hex")),bagTables.save,1);total+=Balance(bag,"GB_Total");assert(parcelUuids.insert(bag.uuid).second);}assert(total==9000&&Plan(multiple,bagTables)==many);
    auto excessive=parcelRequest;excessive["credit_balances"][0]["quantity"]=uint64_t(MaxResourceQuantity)+1;Refuses([&]{Plan(excessive,bagTables);});
    std::vector<std::vector<uint8_t>> packed;for(unsigned y=0;y<13;++y)for(unsigned x=0;x<16;++x){auto b=Rune();Bits(b,42,4,x);Bits(b,46,4,y);packed.push_back(b);}
    Bytes full;for(unsigned n=0;n<7;++n){const auto tab=Tab(packed);full.insert(full.end(),tab.begin(),tab.end());}
    auto fullPage=Read(full,tables);Refuses([&]{Deliver(fullPage,item,settings,tables,0);});assert(Write(fullPage,tables)==full);
    size_t realPages=0,realItems=0;
    if(argc==3){const auto real=Load(argv[1]);for(const auto& file:std::filesystem::directory_iterator(argv[2])){
        const auto name=file.path().filename().wstring();if(file.path().extension()!=L".d2i"||!name.starts_with(L"Stash_SC_Page"))continue;
        const auto before=ReadFile(file.path());try{auto parsed=Read(before,real);assert(Write(parsed,real)==before);for(const auto& rows:parsed.tabs)realItems+=rows.size();++realPages;}
        catch(const std::exception& e){std::cout<<"Refused "<<file.path().filename().string()<<": "<<e.what()<<'\n';}assert(ReadFile(file.path())==before);}
        assert(realPages>0);
    }
    std::cout<<"Custody: exact item/socket conservation, stack splits, gold and bag debits/credits, priced-payment exclusion, grid capacity, malformed input and immutable source checks passed. Real pages="<<realPages<<", items="<<realItems<<".\n";
}
