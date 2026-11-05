#pragma once
// Pure save transformations. No filesystem writes, network calls or authority
// to settle a sale. The workspace tool currently applies these to copies only.
#include "../../d2rhud/plugin/MarketplaceWorkspace.h"
#include "../../d2rhud/plugin/TableMetadata.h"
#include "../../d2rhud/plugin/StorageBagFormat.h"

namespace Marketplace::Custody {
using Bytes=std::vector<uint8_t>;
struct Tables {std::string mod="RMD-MP";SaveTables save;std::map<std::string,std::pair<unsigned,unsigned>> sizes;};
struct Page {std::array<Bytes,7> headers;std::array<std::vector<SavedItem>,7> tabs;};
inline void Require(bool yes,const char* message){if(!yes)throw std::runtime_error(message);}
inline Tables Load(const std::filesystem::path& data){
    const auto excel=data/"global/excel";const auto stats=ReadFile(excel/"itemstatcost.bin",64*1024*1024);
    std::vector<std::string> names;
    const auto metadata=data/"D2RHUD/table-metadata.json";
    if(std::filesystem::exists(metadata)){const auto doc=Json::parse(ReadFile(metadata,32*1024*1024));if(auto row=TableMetadata::Entry(doc,"itemstatcost",stats);row&&row->contains("names"))names=(*row)["names"].get<std::vector<std::string>>();}
    Tables result;
    const auto package=data.parent_path();
    Require(package.extension()==L".mpq"&&package.parent_path().filename()==package.stem(),"Mod data must belong to its matching installed mod package.");
    result.mod=package.stem().string();Require(!result.mod.empty()&&result.mod.size()<=64&&result.mod.find('|')==std::string::npos,"Invalid installed mod identity.");
    result.save=LoadSaveTables([&](const auto& name){return ReadFile(excel/(name+".bin"),64*1024*1024);},names);
    for(const auto* name:{"weapons","armor","misc"}){const auto raw=ReadFile(excel/(std::string(name)+".bin"),64*1024*1024);
        for(size_t at=4;at<raw.size();at+=0x1b4){std::string code(reinterpret_cast<const char*>(raw.data()+at+0x80),4);code=Code(code);
            Require(!code.empty()&&result.sizes.emplace(code,std::pair(unsigned(raw[at+0x116]),unsigned(raw[at+0x117]))).second,"Duplicate inventory definition.");}}
    return result;
}
inline Bytes Decode(const std::string& hex){return Unhex(hex);}
inline Bytes DecodePage(const std::string& hex){
    Require(!hex.empty()&&hex.size()<=16*1024*1024&&hex.size()%2==0&&hex.find_first_not_of("0123456789abcdef")==std::string::npos,"Invalid custody page payload.");
    auto digit=[](char c){return c<='9'?c-'0':c-'a'+10;};Bytes bytes;bytes.reserve(hex.size()/2);for(size_t n=0;n<hex.size();n+=2)bytes.push_back(uint8_t(digit(hex[n])*16+digit(hex[n+1])));return bytes;
}
inline Page Read(std::span<const uint8_t> raw,const Tables& tables){
    Page page;const auto items=ReadStash(raw,tables.save);size_t at=0;
    for(auto& h:page.headers){h.assign(raw.begin()+at,raw.begin()+at+68);at+=CandyCornSaveData::Detail::Word(raw,at+16);}
    for(const auto& item:items)page.tabs[item.tab-1].push_back(item);return page;
}
inline void Bits(Bytes& bytes,size_t at,unsigned n,uint32_t value){
    Require(n<=32&&at+n<=bytes.size()*8&&(n==32||uint64_t(value)<(uint64_t(1)<<n)),"Invalid saved bit edit.");
    for(unsigned i=0;i<n;++i){const auto bit=at+i;const uint8_t mask=uint8_t(1u<<(bit%8));bytes[bit/8]=(bytes[bit/8]&~mask)|uint8_t(((value>>i)&1)<<(bit%8));}
}
inline uint64_t Gold(const Page& page){uint64_t sum=0;for(const auto& h:page.headers)sum+=CandyCornSaveData::Detail::Word(h,12);return sum;}
inline std::pair<unsigned,unsigned> Size(const SavedItem& item,const Tables& tables){
    const auto found=tables.sizes.find(item.code);Require(found!=tables.sizes.end(),"Missing item grid dimensions.");
    const auto [w,h]=found->second;Require(w&&w<=16&&h&&h<=13,"Item cannot fit the RMD-MP stash grid.");return {w,h};
}
using Grid=std::array<std::array<bool,16*13>,7>;
inline Grid Occupancy(const Page& page,const Tables& tables){
    Grid used{};for(size_t tab=0;tab<7;++tab)for(const auto& item:page.tabs[tab]){
        const auto [w,h]=Size(item,tables);Require(item.container==5&&item.x+w<=16&&item.y+h<=13,"Invalid stash item placement.");
        const auto bytes=Decode(item.itemHex);Require(((bytes[4]>>3)&7)==0,"Only stored stash items can enter custody.");
        Require(item.quantity>0&&item.code!="gld","Invalid stack or unsupported loose-gold item.");
        for(unsigned y=item.y;y<item.y+h;++y)for(unsigned x=item.x;x<item.x+w;++x){Require(!used[tab][y*16+x],"Overlapping stash items.");used[tab][y*16+x]=true;}
    }return used;
}
inline Bytes Write(const Page& page,const Tables& tables){
    Occupancy(page,tables);Bytes result;
    for(size_t tab=0;tab<7;++tab){auto record=page.headers[tab];Require(record.size()==68&&page.tabs[tab].size()<=4096,"Invalid stash header.");
        record[66]=uint8_t(page.tabs[tab].size());record[67]=uint8_t(page.tabs[tab].size()>>8);
        for(const auto& item:page.tabs[tab]){const auto bytes=Decode(item.itemHex);record.insert(record.end(),bytes.begin(),bytes.end());}
        CandyCornSaveData::Detail::Store(record,16,uint32_t(record.size()));result.insert(result.end(),record.begin(),record.end());
    }
    ReadStash(result,tables.save);return result;
}
inline CandyCornSaveData::Detail::Prefix Prefix(CandyCornSaveData::Detail::State& state,const Tables& tables){
    CandyCornSaveData::Detail::Prefix p;Require(CandyCornSaveData::Detail::ItemPrefix(state,tables.save,0,p),"Invalid editable item prefix.");return p;
}
inline SavedItem Quantity(const SavedItem& item,uint32_t quantity,const Tables& tables){
    auto bytes=Decode(item.itemHex);CandyCornSaveData::Detail::Budget budget;CandyCornSaveData::Detail::State s;s.bits={bytes,0,&budget,true};const auto p=Prefix(s,tables);
    Require(p.definition->stackable&&item.quality==2&&item.children.empty()&&quantity>0&&quantity<512,"This stack cannot be safely split.");
    Bits(bytes,s.bits.pos,9,quantity);const auto changed=ReadItem(bytes,tables.save,item.tab);Require(changed.quantity==quantity&&changed.id==item.id,"Stack edit did not round-trip.");return changed;
}
inline uint64_t BagMaximum(const Tables& tables,const std::string& symbol){
    const auto found=tables.save.bagStats.find(symbol);Require(found!=tables.save.bagStats.end(),"Bag stat metadata is unavailable or stale.");
    const auto def=CandyCornSaveData::Detail::Stat(tables.save,found->second);Require(def&&def->saveBits&&def->saveBits<=32&&!def->paramBits,"Invalid bag stat definition.");
    const auto capacity=int64_t((uint64_t(1)<<def->saveBits)-1)-def->saveAdd;Require(capacity>=0,"Invalid bag capacity.");return uint64_t((std::min)(int64_t(MaxResourceQuantity),capacity));
}
inline SavedItem Bag(const SavedItem& item,const std::string& symbol,uint32_t amount,const Tables& tables){
    Require(item.code=="Z01"&&item.quality==2&&item.children.empty()&&amount<=BagMaximum(tables,symbol),"This bag balance cannot be edited.");
    const auto id=tables.save.bagStats.at(symbol);auto bytes=Decode(item.itemHex);CandyCornSaveData::Detail::Budget budget;CandyCornSaveData::Detail::State s;s.bits={bytes,0,&budget,true};const auto p=Prefix(s,tables);
    Require(!p.definition->stackable&&!(p.flags&(1u<<26))&&p.sockets==0,"Unsupported bag format.");
    auto& b=s.bits;if(p.flags&(1u<<11)){const auto def=CandyCornSaveData::Detail::Stat(tables.save,194);Require(def!=nullptr,"Missing bag socket definition.");b.Skip(def->saveBits);}
    unsigned matches=0;size_t position=0;unsigned width=0;int32_t add=0;
    auto field=[&](unsigned stat){const auto def=CandyCornSaveData::Detail::Stat(tables.save,stat);Require(def!=nullptr,"Unknown bag property.");b.Skip(def->paramBits);
        if(stat==id){Require(!def->paramBits,"Parameterized bag balance.");++matches;position=b.pos;width=def->saveBits;add=def->saveAdd;}b.Skip(def->saveBits);};
    bool end=false;for(unsigned n=0;n<4096&&b.ok;++n){const auto stat=b.Read(9);if(stat==511){end=true;break;}field(stat);if(stat==17||stat==48||stat==50||stat==52)field(stat+1);else if(stat==54||stat==57){field(stat+1);field(stat+2);}}
    Require(b.ok&&end&&matches==1,"Missing or ambiguous bag balance; no synthetic properties are added.");
    const auto raw=int64_t(amount)+add;Require(raw>=0&&uint64_t(raw)<(uint64_t(1)<<width),"Bag balance overflow.");Bits(bytes,position,width,uint32_t(raw));
    auto changed=ReadItem(bytes,tables.save,item.tab);auto before=item.properties,after=changed.properties;
    for(auto& row:before)if(row.at("id")==id){row["raw"]=raw;row["value"]=amount;}
    Require(before==after&&changed.children==item.children,"Unrelated bag properties changed.");return changed;
}
inline uint32_t Balance(const SavedItem& item,const std::string& stat){
    for(const auto& row:item.balances)if(row.stat==stat)return row.quantity;return 0;
}
inline bool Protected(const SavedItem& item,const Settings& settings){return std::any_of(settings.drafts.begin(),settings.drafts.end(),[&](const auto& d){return d.tab==item.tab&&d.itemHex==item.itemHex;});}
struct Parcel {std::vector<SavedItem> items;std::vector<Requirement> balances;};
inline void TakeItem(Page& page,const SavedItem& wanted,Parcel& parcel){
    Require(wanted.tab>=1&&wanted.tab<=7&&wanted.ordinal>0,"Select an exact tab and item ordinal.");auto& rows=page.tabs[wanted.tab-1];
    const auto at=std::find_if(rows.begin(),rows.end(),[&](const auto& item){return item.ordinal==wanted.ordinal&&item.itemHex==wanted.itemHex;});
    Require(at!=rows.end(),"The selected item changed or was already removed.");parcel.items.push_back(*at);rows.erase(at);
}
inline void Debit(Page& page,const std::vector<Requirement>& price,const Settings& settings,const Tables& tables,Parcel& parcel){
    Require(Validate(price).empty(),"Invalid debit bundle.");
    for(const auto& r:price){uint64_t remaining=r.quantity;
        if(r.kind=="gold"){
            for(auto& header:page.headers){const auto gold=CandyCornSaveData::Detail::Word(header,12);const auto take=(std::min)(remaining,uint64_t(gold));CandyCornSaveData::Detail::Store(header,12,uint32_t(gold-take));remaining-=take;}
        }else for(auto& rows:page.tabs)for(size_t n=0;n<rows.size()&&remaining;){auto& item=rows[n];if(Protected(item,settings)){++n;continue;}
            if(r.kind=="bag"){
                const auto amount=Balance(item,r.stat);const auto take=(std::min)(remaining,uint64_t(amount));if(take)item=Bag(item,r.stat,uint32_t(amount-take),tables);remaining-=take;++n;
            }else if(Matches(r,item.Payment())){
                const auto take=(std::min)(remaining,uint64_t(item.quantity));if(take==item.quantity){parcel.items.push_back(item);rows.erase(rows.begin()+n);}
                else{parcel.items.push_back(Quantity(item,uint32_t(take),tables));item=Quantity(item,uint32_t(item.quantity-take),tables);++n;}remaining-=take;
            }else ++n;
        }
        Require(remaining==0,"The unpriced deposit cannot supply the complete payment.");if(r.kind!="item")parcel.balances.push_back(r);
    }
}
// Build a normal RMD currency container from the installed save definitions.
// The claim ticket supplies its persisted UUID, so crash recovery recreates
// exactly the same parcel rather than minting another identity.
inline SavedItem CurrencyBag(const Tables& tables,const std::vector<Requirement>& balances,const std::string& identity){
    Require(identity.size()==32&&identity.find_first_not_of("0123456789abcdef")==std::string::npos,"Invalid currency parcel identity.");
    auto def=std::find_if(tables.save.items.begin(),tables.save.items.end(),[](const auto& d){return std::string(d.code.data(),4)=="Z01 ";});
    Require(def!=tables.save.items.end()&&!def->armor&&!def->weapon&&!def->stackable&&!def->gold&&!def->tome&&!def->bodyPart&&!def->charm&&!def->questDifficulty,"This mod does not support currency bag parcels.");
    Bytes bytes;size_t pos=0;auto add=[&](uint32_t value,unsigned count){for(unsigned bit=0;bit<count;++bit){if(pos%8==0)bytes.push_back(0);bytes.back()|=uint8_t(((value>>bit)&1)<<(pos%8));++pos;}};
    add(1u<<4,32);add(StorageBagFormat::Current,3);add(0,3);add(0,4);add(0,4);add(0,4);add(5,3);
    for(unsigned char c:std::string("Z01 "))for(char bit:CandyCornSaveData::Detail::Codes[c])add(bit-'0',1);
    const auto uuid=Decode(identity);uint32_t seed=0;for(unsigned n=0;n<4;++n)seed|=uint32_t(uuid[n])<<(n*8);
    add(0,3);add(seed?seed:1,32);add(1,7);add(2,4);add(0,1);add(0,1);add(1,1);for(auto value:uuid)add(value,8);
    for(const auto& [symbol,id]:tables.save.bagStats){
        const auto stat=CandyCornSaveData::Detail::Stat(tables.save,id);const auto maximum=BagMaximum(tables,symbol);uint64_t amount=0;
        for(const auto& r:balances)if(r.kind=="bag"&&r.stat==symbol)amount+=r.quantity;
        Require(amount<=maximum,"Currency parcel exceeds bag capacity.");const auto raw=int64_t(amount)+stat->saveAdd;
        Require(id<511&&raw>=0&&uint64_t(raw)<(uint64_t(1)<<stat->saveBits),"Invalid currency parcel property.");add(id,9);add(uint32_t(raw),stat->saveBits);
    }
    for(const auto& r:balances)if(r.kind=="bag")Require(tables.save.bagStats.contains(r.stat),"Unknown currency parcel stat.");
    add(511,9);while(pos%8)add(0,1);
    auto item=ReadItem(bytes,tables.save,1);Require(item.uuid==identity&&item.code=="Z01"&&item.quality==2,"Currency parcel did not round-trip.");
    for(const auto& r:balances)if(r.kind=="bag")Require(Balance(item,r.stat)==r.quantity,"Currency parcel balance did not round-trip.");return item;
}
inline unsigned CurrencyBagSlots(const Page& page,const Tables& tables){
    if(!tables.sizes.contains("Z01"))return 0;const auto [w,h]=tables.sizes.at("Z01");if(!w||!h||w>16||h>13)return 0;
    auto grid=Occupancy(page,tables);unsigned slots=0;for(unsigned tab=0;tab<7;++tab)for(unsigned y=0;y+h<=13;++y)for(unsigned x=0;x+w<=16;++x){
        bool free=true;for(unsigned j=y;j<y+h;++j)for(unsigned i=x;i<x+w;++i)free=free&&!grid[tab][j*16+i];if(!free)continue;
        ++slots;for(unsigned j=y;j<y+h;++j)for(unsigned i=x;i<x+w;++i)grid[tab][j*16+i]=true;
    }return slots;
}
inline void Deliver(Page& page,const Parcel& parcel,const Settings& settings,const Tables& tables,uint32_t goldCap,Json* delivered=nullptr){
    (void)settings; // Items and deliveries share a page. Occupancy includes every local item.
    auto grid=Occupancy(page,tables);
    for(const auto& original:parcel.items){const auto [w,h]=Size(original,tables);bool placed=false;
        for(unsigned tab=0;tab<7&&!placed;++tab)for(unsigned y=0;y+h<=13&&!placed;++y)for(unsigned x=0;x+w<=16&&!placed;++x){
            bool free=true;for(unsigned j=y;j<y+h;++j)for(unsigned i=x;i<x+w;++i)free=free&&!grid[tab][j*16+i];if(!free)continue;
            auto bytes=Decode(original.itemHex);Bits(bytes,35,3,0);Bits(bytes,38,4,0);Bits(bytes,42,4,x);Bits(bytes,46,4,y);Bits(bytes,50,3,5);
            auto item=ReadItem(bytes,tables.save,tab+1);Require(item.properties==original.properties&&item.children==original.children&&item.id==original.id,"Delivery changed item properties or sockets.");
            item.ordinal=unsigned(page.tabs[tab].size()+1);if(delivered)delivered->push_back(item.ToJson());page.tabs[tab].push_back(std::move(item));for(unsigned j=y;j<y+h;++j)for(unsigned i=x;i<x+w;++i)grid[tab][j*16+i]=true;placed=true;
        }Require(placed,"Delivery page has insufficient grid space.");
    }
    for(const auto& r:parcel.balances){uint64_t remaining=r.quantity;
        if(r.kind=="gold"){
            Require(goldCap>0&&goldCap<=MaxResourceQuantity,"Choose an explicit per-tab gold capacity for this simulation.");
            for(auto& header:page.headers){const auto gold=CandyCornSaveData::Detail::Word(header,12);const auto room=gold<goldCap?goldCap-gold:0;const auto add=(std::min)(remaining,uint64_t(room));CandyCornSaveData::Detail::Store(header,12,uint32_t(gold+add));remaining-=add;}
        }else if(r.kind=="bag"){
            const auto maximum=BagMaximum(tables,r.stat);
            for(auto& rows:page.tabs)for(auto& item:rows){if(item.code!="Z01"||item.quality!=2)continue;
                // Only existing serialized stats are editable, including zero.
                const auto id=tables.save.bagStats.at(r.stat);if(std::none_of(item.properties.begin(),item.properties.end(),[&](const auto& p){return p.at("id")==id&&p.at("list")=="base";}))continue;
                const auto amount=Balance(item,r.stat);Require(amount<=maximum,"Existing bag balance exceeds its capacity.");const auto add=(std::min)(remaining,maximum-amount);if(add)item=Bag(item,r.stat,uint32_t(amount+add),tables);remaining-=add;
            }
        }else Require(false,"A resource parcel cannot contain an item requirement.");
        Require(!remaining,"Delivery cannot hold the complete gold or bag balance.");
    }
}
inline Json ParcelJson(const Parcel& parcel){Json rows=Json::array();for(const auto& item:parcel.items)rows.push_back(item.ToJson());return {{"items",rows},{"balances",parcel.balances.empty()?Json::array():PriceJson(parcel.balances)}};}
inline Json Plan(const Json& request,const Tables& tables){
    Require(request.at("schema")==1&&request.at("game_mode")=="2.4"&&request.at("mod")==tables.mod&&request.at("hardcore").is_boolean(),"Invalid custody simulation envelope.");
    const auto raw=DecodePage(request.at("source_hex").get<std::string>());
    auto page=Read(raw,tables);Occupancy(page,tables);auto settings=ReadSettings(request.at("settings"),tables.mod,request.at("hardcore").get<bool>());
    const auto excluded=request.value("debit_exclude",Json::array());Require(excluded.is_array()&&excluded.size()<=4096,"Invalid protected currency parcels.");
    for(const auto& row:excluded){const auto tab=unsigned(Integer(row.at("tab"),1,7));const auto hex=row.at("item_hex").get<std::string>();
        const auto found=std::find_if(page.tabs[tab-1].begin(),page.tabs[tab-1].end(),[&](const auto& item){return item.itemHex==hex&&item.code=="Z01"&&item.quality==2;});
        Require(found!=page.tabs[tab-1].end(),"Protected currency parcel changed.");settings.drafts.push_back({tab,hex,{}});
    }
    Parcel removed;const auto selectors=request.value("take",Json::array());Require(selectors.is_array()&&selectors.size()<=512,"Too many selected items.");
    for(const auto& row:selectors){SavedItem item;item.tab=Integer(row.at("tab"),1,7);item.ordinal=Integer(row.at("ordinal"),1,4096);item.itemHex=row.at("item_hex").get<std::string>();TakeItem(page,item,removed);}
    if(request.contains("debit")&&!request.at("debit").empty())Debit(page,ReadPrice(request.at("debit")),settings,tables,removed);
    Parcel delivery;const auto credits=request.value("credit_items",Json::array());Require(credits.is_array()&&credits.size()<=512,"Too many delivery items.");for(const auto& row:credits)delivery.items.push_back(ReadItem(Decode(row.at("item_hex").get<std::string>()),tables.save,1));
    if(request.contains("credit_balances")&&!request.at("credit_balances").empty())delivery.balances=ReadPrice(request.at("credit_balances"));
    const auto identity=request.value("currency_bag_id",std::string());Json bags=Json::array();
    if(!identity.empty()&&std::any_of(delivery.balances.begin(),delivery.balances.end(),[](const auto& r){return r.kind=="bag";})){
        auto remaining=delivery.balances;unsigned ordinal=0;Parcel parcel;
        while(std::any_of(remaining.begin(),remaining.end(),[](const auto& r){return r.kind=="bag"&&r.quantity;})){
            Require(ordinal<1456,"Too many currency parcel bags.");auto chunk=remaining;
            for(size_t n=0;n<remaining.size();++n)if(remaining[n].kind=="bag"){
                chunk[n].quantity=uint32_t((std::min)(uint64_t(remaining[n].quantity),BagMaximum(tables,remaining[n].stat)));Require(chunk[n].quantity||!remaining[n].quantity,"Currency bag cannot hold this resource.");remaining[n].quantity-=chunk[n].quantity;
            }
            std::erase_if(chunk,[](const auto& r){return r.kind!="bag"||!r.quantity;});
            auto uuid=Decode(identity);const uint32_t index=ordinal++;for(unsigned n=0;n<4;++n)uuid[12+n]^=uint8_t(index>>(n*8));
            parcel.items.push_back(CurrencyBag(tables,chunk,Hex(uuid)));
        }
        Json placed=Json::array();Deliver(page,parcel,settings,tables,0,&placed);bags=std::move(placed);std::erase_if(delivery.balances,[](const auto& r){return r.kind=="bag";});
    }
    Json delivered=Json::array();Deliver(page,delivery,settings,tables,uint32_t(Integer(request.value("gold_cap",Json(0)),0,MaxResourceQuantity)),&delivered);
    const auto after=Write(page,tables);return {{"schema",1},{"simulation",true},{"after_hex",Hex(after)},{"removed",ParcelJson(removed)},{"delivered",delivered},{"currency_bags",bags},{"gold_before",Gold(Read(raw,tables))},{"gold_after",Gold(page)}};
}
}
