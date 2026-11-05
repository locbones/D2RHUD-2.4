#pragma once
#include "MarketplaceStash.h"
#include "MarketplaceStore.h"
#include "MarketplaceActivity.h"
#include <chrono>

namespace Marketplace {
struct Listing {
    std::string seller,id,revision;
    SavedItem item;
    std::vector<Requirement> price;
    bool simulated=false,own=false;
    std::string sellerId,itemId;
    int64_t expires=0;
    bool development=false;
    bool escrow=false;
    int offerCount=0,activeOfferCount=0,otherOfferCount=0;
    std::string comment;
};
inline int64_t UnixNow(){return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
inline std::string SavedIdentity(const std::string& hex){auto bytes=Unhex(hex);if(bytes.size()<7)throw std::runtime_error("Invalid item identity bytes.");for(size_t bit=35;bit<53;++bit)bytes[bit/8]&=uint8_t(~(1u<<(bit%8)));return Hex(bytes);}
inline SavedItem ReconcileLiveIdentity(SavedItem live,const std::vector<SavedItem>& saved,const SaveTables& tables){
    // The client serializer can omit persisted fingerprints (Unit+0x30).
    // Recover identity only from one exact saved counterpart, including every
    // modifier/socket and UUID. Never treat the runtime unit GUID as a seed.
    if(live.id||!live.uuid.empty())return live;
    const auto key=SavedIdentity(LegacyItemHex(live.itemHex,live.children));const SavedItem* match=nullptr;
    for(const auto& candidate:saved)if((candidate.id||!candidate.uuid.empty())&&SavedIdentity(LegacyItemHex(candidate.itemHex,candidate.children))==key){if(match)return live;match=&candidate;}
    if(!match)return live;
    auto bytes=Unhex(match->itemHex),position=Unhex(live.itemHex);for(size_t bit=35;bit<53;++bit)bytes[bit/8]=uint8_t((bytes[bit/8]&~(1u<<(bit%8)))|(position[bit/8]&(1u<<(bit%8))));
    auto result=ReadItem(bytes,tables,live.tab);result.ordinal=live.ordinal;return result;
}
struct Feed {std::vector<Listing> listings;int64_t expires=0;std::vector<SavedItem> escrowItems;std::vector<PaymentItem> escrowFunds;bool custodyEnabled=false;std::string walletText;Json wallet=Json::object(),walletBalances=Json::array();std::map<std::string,std::set<std::string>> lineage;std::set<std::pair<std::string,std::string>> favorites;};
// A read-only cache contract, not proof of escrow or authority to settle.
// Reparse item bytes with the installed mod's tables instead of trusting the
// remote item's claimed quality, quantity, identity or saved stats.
inline Feed ReadFeed(const Json& source,const std::string& mod,bool hardcore,const SaveTables& tables,int64_t now){
    if(source.at("schema")!=Schema||source.at("game_mode")!="2.4"||source.at("mod")!=mod||
        source.at("hardcore")!=hardcore||!source.at("hardcore").is_boolean())throw std::runtime_error("Marketplace cache belongs to another game mode.");
    const auto& rows=source.at("listings");if(!rows.is_array()||rows.size()>4096)throw std::runtime_error("Marketplace cache is too large.");
    const auto expiry=Integer(source.at("expires_at"),1,INT32_MAX);
    if(expiry<=now||int64_t(expiry)>now+86400)throw std::runtime_error("Marketplace cache expired or has an invalid expiry. Refresh it before agreeing.");
    Feed result;result.expires=expiry;std::set<std::pair<std::string,std::string>> ids;
    if(source.contains("social")){const auto& favorites=source.at("social").at("favorites");if(!favorites.is_array()||favorites.size()>2048)throw std::runtime_error("Invalid favorites.");for(const auto& f:favorites){const auto kind=f.at("kind").get<std::string>(),target=f.at("target").get<std::string>();if((kind!="listing"&&kind!="seller")||!ValidIdentity(target))throw std::runtime_error("Invalid favorite.");result.favorites.emplace(kind,target);}}
    for(const auto& row:rows){
        Listing entry;entry.seller=row.at("seller").get<std::string>();entry.id=row.at("id").get<std::string>();entry.revision=row.at("revision").get<std::string>();
        if(!ValidIdentity(entry.seller)||!ValidIdentity(entry.id)||!ValidIdentity(entry.revision)||!ids.emplace(entry.seller,entry.id).second)
            throw std::runtime_error("Invalid or duplicate marketplace listing identity.");
        entry.item=ReadItem(Unhex(row.at("item_hex").get<std::string>()),tables,Integer(row.value("tab",Json(1)),1,7));
        entry.comment=row.value("comment",std::string());if(!ValidListingComment(entry.comment))throw std::runtime_error("Invalid listing comment.");
        entry.price=ReadPrice(row.at("price"));entry.sellerId=row.value("seller_id",std::string());entry.itemId=row.value("item_id",std::string());entry.own=row.value("own",false);
        entry.escrow=row.value("custody",std::string())=="escrow";
        entry.offerCount=Integer(row.value("offer_count",Json(0)),0,INT32_MAX);entry.activeOfferCount=Integer(row.value("active_offer_count",Json(0)),0,INT32_MAX);entry.otherOfferCount=Integer(row.value("other_offer_count",Json(0)),0,INT32_MAX);
        entry.expires=row.contains("expires_at")?Integer(row.at("expires_at"),1,INT32_MAX):expiry;entry.development=row.value("development",false);
        if((!entry.sellerId.empty()&&!ValidIdentity(entry.sellerId))||(!entry.itemId.empty()&&!ValidIdentity(entry.itemId)))throw std::runtime_error("Invalid service listing identity.");if(entry.expires>now)result.listings.push_back(std::move(entry));
    }
    if(source.contains("escrow")){
        const auto& escrow=source.at("escrow");result.custodyEnabled=escrow.value("enrolled",false);
        const auto& items=escrow.at("items");const auto& balances=escrow.at("balances");
        if(!items.is_array()||items.size()>4096||!balances.is_array()||balances.size()>32)throw std::runtime_error("Vault cache exceeds its limits.");
        std::set<std::string> assets;
        for(const auto& r:items){auto item=ReadItem(Unhex(r.at("item_hex").get<std::string>()),tables,Integer(r.at("metadata").at("tab"),1,7));
            item.escrowId=ActivityText(r,"item",32);item.escrowState=ActivityText(r,"state",16);
            if(!assets.insert(item.escrowId).second||!std::set<std::string>{"available","listed","claiming"}.contains(item.escrowState))throw std::runtime_error("Invalid Vault asset state.");
            item.escrowDraft={item.tab,item.itemHex,{}};item.escrowDraft.comment=r.at("metadata").value("comment",std::string());if(!r.at("metadata").at("price").empty())item.escrowDraft.price=ReadPrice(r.at("metadata").at("price"));
            if(item.escrowState=="available")result.escrowFunds.push_back(item.Payment());result.escrowItems.push_back(std::move(item));}
        for(const auto& r:balances){const auto kind=ActivityText(r,"kind",8);const auto stat=ActivityText(r,"stat",32,true);
            if((kind!="gold"&&kind!="bag")||(kind=="gold"&&!stat.empty())||(kind=="bag"&&!tables.bagStats.contains(stat)))throw std::runtime_error("Invalid Vault balance type.");
            if(!r.at("quantity").is_number_integer()||r.at("quantity").get<int64_t>()<1||r.at("quantity").get<int64_t>()>9000000000000000LL)throw std::runtime_error("Invalid Vault balance quantity.");
            result.escrowFunds.push_back({kind=="gold"?"gld":"Z01",2,-1,uint32_t((std::min)(uint64_t(UINT32_MAX),r.at("quantity").get<uint64_t>())),true,false,kind,stat});}
        result.walletBalances=balances;
        if(escrow.contains("wallet")){
            const auto& wallet=escrow.at("wallet");
            if(!wallet.is_object()||!wallet.at("withdrawals").is_array()||wallet.at("withdrawals").size()>64||!wallet.at("entries").is_array()||wallet.at("entries").size()>40)throw std::runtime_error("Invalid Vault activity cache.");
            for(const auto& r:wallet.at("withdrawals")){ActivityText(r,"id",32);const auto status=ActivityText(r,"status",16);if(!std::set<std::string>{"queued","delivering","completed","cancelled"}.contains(status))throw std::runtime_error("Invalid withdrawal state.");(void)ReadPrice(r.at("requested"));if(!r.at("remaining").empty())(void)ReadPrice(r.at("remaining"));ActivityText(r,"message",512,true);Integer(r.at("revision"),1,INT32_MAX);}
            for(const auto& r:wallet.at("entries")){ActivityText(r,"kind",8);ActivityText(r,"stat",32,true);ActivityText(r,"action",32);if(!r.at("amount").is_number_integer()||!r.at("available").is_number_integer())throw std::runtime_error("Invalid Vault activity amount.");}
            result.wallet=wallet;
        }
        for(const auto& r:balances){if(!result.walletText.empty())result.walletText+='\n';std::string name=r.at("kind")=="gold"?"Gold":r.at("stat").get<std::string>();
            for(const auto& resource:BagResources)if(resource.stat==name){name=resource.label;break;}result.walletText+=name+": "+std::to_string(r.at("quantity").get<int64_t>());}
        if(escrow.contains("lineage")){const auto& rows=escrow.at("lineage");if(!rows.is_array()||rows.size()>4096)throw std::runtime_error("Item lineage cache exceeds its limit.");
            for(const auto& r:rows){const auto id=ActivityText(r,"item",32);const auto item=ReadItem(Unhex(r.at("item_hex").get<std::string>()),tables,1);result.lineage[SavedIdentity(item.itemHex)].insert(id);}}
    }
    return result;
}
inline void ApplyLineage(std::vector<SavedItem>& items,const Feed& feed){for(auto& item:items){item.ledgerId.clear();const auto found=feed.lineage.find(SavedIdentity(item.itemHex));if(found!=feed.lineage.end()&&found->second.size()==1)item.ledgerId=*found->second.begin();}}
inline bool SameTerms(const AgreementDraft& agreement,const Listing& listing){
    return !listing.own&&agreement.simulated==listing.simulated&&agreement.seller==listing.seller&&agreement.listing==listing.id&&agreement.revision==listing.revision&&
        agreement.itemHex==listing.item.itemHex&&PriceJson(agreement.price)==PriceJson(listing.price);
}
inline std::vector<PaymentItem> Spendable(const std::vector<SavedItem>& items,const Settings& settings,uint64_t gold=0,const Feed* feed=nullptr){
    std::vector<PaymentItem> result;
    for(const auto& item:items){
        const bool listed=std::any_of(settings.drafts.begin(),settings.drafts.end(),[&](const auto& d){return d.tab==item.tab&&d.itemHex==item.itemHex;});
        if(!listed){result.push_back(item.Payment());result.insert(result.end(),item.balances.begin(),item.balances.end());}
    }
    if(gold)result.push_back({"gld",2,-1,uint32_t((std::min)(gold,uint64_t(UINT32_MAX))),true,false,"gold"});
    if(feed)result.insert(result.end(),feed->escrowFunds.begin(),feed->escrowFunds.end());return result; // Socket children stay attached to their parent.
}
inline bool ListingFresh(const Feed& feed,const Listing& listing,int64_t now){return listing.simulated||(listing.own&&listing.sellerId.empty())||(feed.expires>now&&(!listing.expires||listing.expires>now));}
inline uint64_t SimulationHash(const std::string& text){uint64_t h=14695981039346656037ULL;for(unsigned char c:text){h^=c;h*=1099511628211ULL;}return h;}
inline void AppendSimulation(Feed& feed,const std::vector<SavedItem>& inventory,const Settings& settings,bool valid){
    std::erase_if(feed.listings,[](const auto& l){return l.simulated;});
    if(!settings.simulation.enabled||!valid)return;
    for(const auto& item:inventory){
        const auto price=std::find_if(settings.simulation.drafts.begin(),settings.simulation.drafts.end(),[&](const auto& d){return d.tab==item.tab&&d.itemHex==item.itemHex;});
        if(price==settings.simulation.drafts.end())continue;
        if(std::any_of(feed.listings.begin(),feed.listings.end(),[&](const auto& l){return l.development&&l.item.tab==item.tab&&l.item.itemHex==item.itemHex&&PriceJson(l.price)==PriceJson(price->price);}))continue;
        const auto identity=std::to_string(settings.simulation.page)+"-"+std::to_string(item.tab)+"-"+std::to_string(SimulationHash(item.itemHex));
        feed.listings.push_back({"Simulated seller","test-"+identity,"test-"+std::to_string(SimulationHash(item.itemHex+PriceJson(price->price).dump())),item,price->price,true});
    }
}
// Show freshly priced deposit items immediately, without pretending that a
// local draft has been uploaded or letting its owner buy it from themselves.
inline void AppendOwnListings(Feed& feed,const std::vector<SavedItem>& inventory,const Settings& settings,bool valid,bool retainHosted=false){
    std::erase_if(feed.listings,[&](const auto& l){if(!l.own||l.escrow)return false;if(retainHosted&&!l.sellerId.empty())return false;if(l.sellerId.empty()||!settings.enabled||!valid)return true;
        return std::none_of(inventory.begin(),inventory.end(),[&](const auto& item){return item.tab==l.item.tab&&item.itemHex==l.item.itemHex&&std::any_of(settings.drafts.begin(),settings.drafts.end(),[&](const auto& d){return d.tab==item.tab&&d.itemHex==item.itemHex&&PriceJson(d.price)==PriceJson(l.price);});});});
    if(!settings.enabled||!valid)return;
    for(const auto& item:inventory){
        const auto price=std::find_if(settings.drafts.begin(),settings.drafts.end(),[&](const auto& d){return d.tab==item.tab&&d.itemHex==item.itemHex;});
        if(price==settings.drafts.end())continue;
        if(std::any_of(feed.listings.begin(),feed.listings.end(),[&](const auto& l){return l.own&&!l.sellerId.empty()&&l.item.tab==item.tab&&l.item.itemHex==item.itemHex&&PriceJson(l.price)==PriceJson(price->price);}))continue;
        const auto identity=std::to_string(settings.page)+"-"+std::to_string(item.tab)+"-"+std::to_string(SimulationHash(item.itemHex));
        feed.listings.push_back({"You (local draft)","local-"+identity,"local-"+std::to_string(SimulationHash(item.itemHex+PriceJson(price->price).dump())),item,price->price,false,true});
    }
}
// The development seller holds its manually entered asking terms. An exact
// counter-offer is accepted; a different bundle gets the asking terms back.
inline bool SimulationAccepts(const std::vector<Requirement>& asking,const std::vector<Requirement>& proposed){
    if(!Validate(asking).empty()||!Validate(proposed).empty()||asking.size()!=proposed.size())return false;
    for(const auto& a:asking)if(std::none_of(proposed.begin(),proposed.end(),[&](const auto& p){return a.kind==p.kind&&a.stat==p.stat&&a.code==p.code&&a.quality==p.quality&&a.index==p.index&&a.quantity==p.quantity&&a.ethereal==p.ethereal&&a.classId==p.classId;}))return false;
    return true;
}
struct WorkspaceSnapshot {
    std::vector<SavedItem> deposit;
    Feed feed;
    std::string depositError,feedError;
    bool depositValid=false;
    uint64_t depositGold=0;bool goldValid=false;
    Activity activity;
    std::vector<SavedItem> seller;std::string sellerError;bool sellerValid=false;
};
inline WorkspaceSnapshot ReadWorkspace(const std::filesystem::path& folder,const Settings& settings,const std::string& mod,bool hardcore,const SaveTables& tables){
    WorkspaceSnapshot result;
    try{
        const auto path=folder/((hardcore?L"Stash_HC_Page":L"Stash_SC_Page")+std::to_wstring(settings.page)+L".d2i");
        const auto before=ReadFile(path);result.deposit=ReadStash(before,tables,&result.depositGold);
        if(before!=ReadFile(path))throw std::runtime_error("Deposit changed while reading. Refresh after saving finishes.");
        result.depositValid=true;result.goldValid=true;
    }catch(const std::exception& e){result.deposit.clear();result.depositError=e.what();}
    try{
        const auto path=folder/(hardcore?L"MarketplaceFeed_2.4_HC.json":L"MarketplaceFeed_2.4_SC.json");
        if(std::filesystem::exists(path)){
            const auto bytes=ReadFile(path,16*1024*1024);
            result.feed=ReadFeed(Json::parse(bytes.begin(),bytes.end()),mod,hardcore,tables,UnixNow());
        }
    }catch(const std::exception& e){result.feed={};result.feedError=e.what();}
    if(settings.simulation.enabled)try{
        const auto path=folder/((hardcore?L"Stash_HC_Page":L"Stash_SC_Page")+std::to_wstring(settings.simulation.page)+L".d2i");
        const auto before=ReadFile(path);result.seller=ReadStash(before,tables);
        if(before!=ReadFile(path))throw std::runtime_error("Simulated seller page changed while reading. Refresh after saving finishes.");
        result.sellerValid=true;
    }catch(const std::exception& e){result.seller.clear();result.sellerError=e.what();}
    AppendSimulation(result.feed,result.seller,settings,result.sellerValid);
    // The UI can replace the saved deposit with a newer live snapshot. Keep
    // hosted identities until it reconciles against that preferred inventory.
    AppendOwnListings(result.feed,result.deposit,settings,result.depositValid,true);
    try{const auto path=folder/(hardcore?L"MarketplaceActivity_2.4_HC.json":L"MarketplaceActivity_2.4_SC.json");if(std::filesystem::exists(path)){const auto bytes=ReadFile(path,16*1024*1024);result.activity=ReadActivity(Json::parse(bytes.begin(),bytes.end()),mod,hardcore,UnixNow(),&tables);}}
    catch(const std::exception& e){result.activity={};result.activity.error=e.what();}
    return result;
}
}
