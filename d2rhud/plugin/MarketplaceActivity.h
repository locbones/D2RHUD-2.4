#pragma once
#include "MarketplaceStash.h"
#include "MarketplaceStore.h"
#include <random>
#include <ctime>

namespace Marketplace {
inline std::vector<uint8_t> Unhex(const std::string& value){
    if(!ValidItemHex(value))throw std::runtime_error("Invalid marketplace item bytes.");
    auto digit=[](char c){return c<='9'?c-'0':c-'a'+10;};std::vector<uint8_t> bytes;bytes.reserve(value.size()/2);
    for(size_t i=0;i<value.size();i+=2)bytes.push_back(uint8_t(digit(value[i])*16+digit(value[i+1])));return bytes;
}
struct OfferEvent {int64_t at=0;std::string kind,actor,message;std::vector<Requirement> price;};
struct CounterThread {
    std::string id,listing,itemId,itemHex,buyer,seller,proposer,buyerName,sellerName,status="draft";
    std::string settlementLabel;
    int revision=0,listingRevision=0;uint32_t seed=0;
    bool simulated=false,local=false,development=false;
    std::vector<Requirement> price;std::vector<OfferEvent> events;
};
struct Sale {
    std::string id,itemId,code,seller,buyer,receipt,uuid;uint32_t seed=0;
    int quality=2,index=-1,ethereal=0,sockets=0;int64_t at=0;
    std::vector<Requirement> price;
};
struct GiftCampaign {std::string id,title,announcement,from,message;int64_t expires=0;bool claimed=false,claimable=false;std::vector<SavedItem> items;std::vector<Requirement> balances;};
struct Activity {std::string self,name,error;int64_t expires=0;std::vector<Sale> sales;std::vector<CounterThread> threads;Json results=Json::object();std::vector<GiftCampaign> gifts;};
struct LocalActivity {std::vector<CounterThread> threads;Json commands=Json::array();};
inline std::string ActivityId(){std::random_device random;std::array<uint8_t,16> bytes{};for(auto& b:bytes)b=uint8_t(random());return Hex(bytes);}
inline std::string ActivityText(const Json& row,const char* key,size_t maximum=128,bool empty=false){
    auto text=row.at(key).get<std::string>();if((text.empty()&&!empty)||text.size()>maximum||std::any_of(text.begin(),text.end(),[](unsigned char c){return c<32||c==127;}))throw std::runtime_error("Invalid marketplace activity text.");return text;
}
inline uint32_t ActivitySeed(const Json& value){if(!value.is_number_integer()||(value.is_number_integer()&&value.get<int64_t>()<0)||value.get<uint64_t>()>UINT32_MAX)throw std::runtime_error("Invalid item seed.");return uint32_t(value.get<uint64_t>());}
inline Json ThreadJson(const CounterThread& t){
    Json events=Json::array();for(const auto& e:t.events)events.push_back({{"at",e.at},{"kind",e.kind},{"actor_name",e.actor},{"message",e.message},{"price",e.price.empty()?Json::array():PriceJson(e.price)}});
    return {{"id",t.id},{"listing",t.listing},{"item",t.itemId},{"item_hex",t.itemHex},{"buyer",t.buyer},{"seller",t.seller},{"proposer",t.proposer},{"buyer_name",t.buyerName},{"seller_name",t.sellerName},{"status",t.status},{"revision",t.revision},{"listing_revision",t.listingRevision},{"seed",t.seed},{"simulated",t.simulated},{"development",t.development},{"price",PriceJson(t.price)},{"events",events}};
}
inline CounterThread ReadThread(const Json& row,bool local){
    CounterThread t;t.local=local;t.id=ActivityText(row,"id");t.listing=ActivityText(row,"listing");t.itemId=ActivityText(row,"item",128,true);t.itemHex=ActivityText(row,"item_hex",65536);
    if(!ValidItemHex(t.itemHex))throw std::runtime_error("Invalid conversation item.");
    t.buyer=ActivityText(row,"buyer");t.seller=ActivityText(row,"seller");t.proposer=ActivityText(row,"proposer");t.buyerName=ActivityText(row,"buyer_name");t.sellerName=ActivityText(row,"seller_name");t.status=ActivityText(row,"status",32);
    if(t.buyer==t.seller||(t.proposer!=t.buyer&&t.proposer!=t.seller))throw std::runtime_error("Invalid conversation parties.");
    static const std::set<std::string> states{"draft","queued","pending","accepted","completed","rejected","withdrawn","superseded"};if(!states.contains(t.status))throw std::runtime_error("Invalid conversation state.");
    if(t.status=="accepted"&&row.contains("settlement_progress")&&row["settlement_progress"].is_object()&&row["settlement_progress"].contains("label"))
        t.settlementLabel=ActivityText(row["settlement_progress"],"label",96);
    t.revision=Integer(row.at("revision"),0,INT32_MAX);t.listingRevision=Integer(row.at("listing_revision"),0,INT32_MAX);t.seed=ActivitySeed(row.at("seed"));t.simulated=row.value("simulated",false);t.development=row.value("development",false);t.price=ReadPrice(row.at("price"));
    const auto& events=row.at("events");if(!events.is_array()||events.size()>512)throw std::runtime_error("Conversation log is too large.");
    for(const auto& e:events){OfferEvent event;event.at=Integer(e.at("at"),1,INT32_MAX);event.kind=ActivityText(e,"kind",32);event.actor=ActivityText(e,"actor_name");event.message=ActivityText(e,"message",2048,true);
        if(!e.at("price").empty())event.price=ReadPrice(e.at("price"));t.events.push_back(std::move(event));}return t;
}
inline void ActivityEnvelope(const Json& doc,const std::string& mod,bool hardcore){
    if(doc.at("schema")!=1||doc.at("game_mode")!="2.4"||doc.at("mod")!=mod||doc.at("hardcore")!=hardcore||!doc.at("hardcore").is_boolean())throw std::runtime_error("Activity cache belongs to another game mode.");
}
inline Activity ReadActivity(const Json& doc,const std::string& mod,bool hardcore,int64_t now,const SaveTables* tables=nullptr){
    ActivityEnvelope(doc,mod,hardcore);Activity result;result.self=ActivityText(doc.at("self"),"id");result.name=ActivityText(doc.at("self"),"name");result.expires=Integer(doc.at("expires_at"),1,INT32_MAX);
    if(result.expires<=now||result.expires>now+86400)throw std::runtime_error("Activity cache expired. Sync before responding.");
    const auto& sales=doc.at("history");const auto& offers=doc.at("offers");if(!sales.is_array()||sales.size()>1000||!offers.is_array()||offers.size()>256)throw std::runtime_error("Activity cache is too large.");
    std::set<std::string> saleIds,receipts,threadIds;
    for(const auto& row:sales){Sale s;s.id=ActivityText(row,"id");s.itemId=ActivityText(row,"item");s.code=Code(ActivityText(row,"code",4));s.seed=ActivitySeed(row.at("seed"));s.uuid=row.value("item_uuid",std::string());if(!s.uuid.empty()&&(s.uuid.size()!=32||s.uuid.find_first_not_of("0123456789abcdef")!=std::string::npos))throw std::runtime_error("Invalid saved UUID.");s.quality=Integer(row.at("quality"),1,9);s.index=Integer(row.at("item_index"),-1,MaxQuantity);s.ethereal=Integer(row.at("ethereal"),0,1);s.sockets=Integer(row.at("sockets"),0,6);s.at=Integer(row.at("at"),1,INT32_MAX);
        if(row.at("kind")!="completed_sale"||s.at>now||!saleIds.insert(s.id).second)throw std::runtime_error("Invalid completed-sale history.");s.seller=ActivityText(row,"seller_name");s.buyer=ActivityText(row,"buyer_name");s.receipt=ActivityText(row,"receipt");if(!receipts.insert(s.receipt).second)throw std::runtime_error("Duplicate sale receipt.");s.price=ReadPrice(row.at("price"));result.sales.push_back(std::move(s));}
    for(const auto& row:offers){auto t=ReadThread(row,false);if(t.simulated||t.revision<1||(t.buyer!=result.self&&t.seller!=result.self)||!threadIds.insert(t.id).second)throw std::runtime_error("Invalid private conversation.");result.threads.push_back(std::move(t));}
    if(doc.contains("gifts")){
        const auto& gifts=doc.at("gifts");if(!gifts.is_array()||gifts.size()>8)throw std::runtime_error("Invalid community gift list.");std::set<std::string> ids;
        for(const auto& row:gifts){GiftCampaign g;g.id=ActivityText(row,"id",32);g.title=ActivityText(row,"title",80);g.from=ActivityText(row.at("owner"),"name",128);g.announcement=row.at("announcement").get<std::string>();
            if(!ids.insert(g.id).second||g.announcement.size()>512)throw std::runtime_error("Invalid gift announcement.");
            g.claimed=row.at("claimed").get<bool>();g.claimable=row.at("claimable").get<bool>();if(!row.at("expires").is_null())g.expires=Integer(row.at("expires"),1,INT32_MAX);
            if(g.claimed&&!row.at("claim").is_null()){g.message=row.at("claim").at("message").get<std::string>();if(g.message.size()>2048)throw std::runtime_error("Invalid gift message.");}
            if(row.contains("balances")&&!row.at("balances").empty()){g.balances=ReadPrice(row.at("balances"));for(const auto& b:g.balances)if(b.kind!="gold"&&b.kind!="bag")throw std::runtime_error("Invalid gift currency.");}
            const auto& items=row.at("items");if(!items.is_array()||items.size()>16||(!items.empty()&&!tables)||(items.empty()&&g.balances.empty()))throw std::runtime_error("Gift item definitions are unavailable.");
            for(const auto& item:items)g.items.push_back(ReadItem(Unhex(ActivityText(item,"item_hex",65536)),*tables,1));result.gifts.push_back(std::move(g));
        }
    }
    if(doc.contains("command_results")){const auto& rows=doc["command_results"];if(!rows.is_object()||rows.size()>4096)throw std::runtime_error("Invalid command receipts.");result.results=rows;}if(doc.contains("publication_error"))result.error=ActivityText(doc,"publication_error",512,true);return result;
}
inline Json LocalActivityJson(const LocalActivity& local,const std::string& mod,bool hardcore){
    if(local.threads.size()>256||local.commands.size()>4096)throw std::runtime_error("Local marketplace log is full. Sync or archive it first.");
    Json threads=Json::array();for(const auto& t:local.threads)threads.push_back(ThreadJson(t));
    return {{"schema",1},{"game_mode","2.4"},{"mod",mod},{"hardcore",hardcore},{"threads",threads},{"commands",local.commands}};
}
inline LocalActivity ReadLocalActivity(const Json& doc,const std::string& mod,bool hardcore){
    ActivityEnvelope(doc,mod,hardcore);LocalActivity local;const auto& threads=doc.at("threads");const auto& commands=doc.at("commands");
    if(!threads.is_array()||threads.size()>256||!commands.is_array()||commands.size()>4096)throw std::runtime_error("Invalid local activity log.");
    std::set<std::string> ids;for(const auto& row:threads){auto t=ReadThread(row,true);if(!ids.insert(t.id).second)throw std::runtime_error("Duplicate conversation.");local.threads.push_back(std::move(t));}
    for(const auto& c:commands){ActivityText(c,"id",64);ActivityText(c,"type",32);if(c.dump().size()>32768)throw std::runtime_error("Invalid queued command.");local.commands.push_back(c);}return local;
}
inline std::vector<CounterThread> MergeThreads(const Activity& remote,const LocalActivity& local){
    auto rows=remote.threads;
    for(const auto& t:local.threads){auto at=std::find_if(rows.begin(),rows.end(),[&](const auto& r){return r.id==t.id;});if(at==rows.end())rows.push_back(t);
        else {bool pending=false;for(const auto& c:local.commands)if(c.value("offer_id",c.value("id",std::string()))==t.id&&!remote.results.contains(c.at("id").get<std::string>()))pending=true;
            if(pending)for(const auto& e:t.events)if(e.kind.starts_with("queued_"))at->events.push_back(e);}}
    for(auto& t:rows)for(const auto& c:local.commands)if(c.value("offer_id",c.value("id",std::string()))==t.id&&remote.results.contains(c.at("id").get<std::string>())){
        const auto& r=remote.results[c.at("id").get<std::string>()];if(r.contains("error")){auto error=r.at("error").get<std::string>();if(error.size()>512)error.resize(512);t.events.push_back({0,"sync_error","Service",error,{}});if(t.status=="queued")t.status="draft";}}
    return rows;
}
inline std::vector<const Sale*> ItemSales(const Activity& data,const SavedItem& item,bool similar,const std::string& ledgerId={}){
    std::set<std::string> instances;if(!similar&&ledgerId.empty()&&(!item.uuid.empty()||item.id))for(const auto& s:data.sales)if(!item.uuid.empty()?s.uuid==item.uuid:s.seed==item.id&&s.code==item.code)instances.insert(s.itemId);
    std::vector<const Sale*> rows;
    for(const auto& s:data.sales){const bool same=similar?s.code==item.code&&s.quality==item.quality&&s.index==item.index&&s.ethereal==int(bool(item.flags&(1u<<22)))&&s.sockets==int(item.children.size()):
        !ledgerId.empty()?s.itemId==ledgerId:!item.uuid.empty()?instances.size()==1&&s.uuid==item.uuid:item.id&&instances.size()==1&&s.seed==item.id&&s.code==item.code;if(same)rows.push_back(&s);}
    std::stable_sort(rows.begin(),rows.end(),[](const auto* a,const auto* b){return a->at>b->at;});return rows;
}
inline bool SeedAmbiguous(const Activity& data,const SavedItem& item){std::set<std::string> ids;for(const auto& s:data.sales)if(!item.uuid.empty()?s.uuid==item.uuid:s.seed==item.id&&s.code==item.code)ids.insert(s.itemId);return ids.size()>1;}
inline bool ChartCurrency(const Requirement& r,int currency){
    if(currency==0)return r.kind=="gold";if(currency>=1&&currency<=5){const std::array<const char*,5> names{"RB_Low","RB_Mid","RB_High","RB_Ultra","GB_Total"};return r.kind=="bag"&&r.stat==names[currency-1];}
    return currency==6&&r.kind=="item"&&r.code=="r33"&&r.quality==2&&r.index==-1&&r.ethereal==-1;
}
}
