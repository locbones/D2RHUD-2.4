#define wmain ExistingMarketplaceTests
#include "marketplace-test.cpp"
#undef wmain
#include "../d2rhud/plugin/MarketplaceActivity.h"
int main(){
    using namespace Marketplace;
    const int now=int(UnixNow());const auto item=ReadItem(Unique(false,43),Tables(),1);
    const std::vector<Requirement> price{{"gld","Gold",2,-1,1500,-1,"gold"}};
    CounterThread thread;thread.id="offer";thread.listing="listing";thread.itemId="instance";thread.itemHex=item.itemHex;thread.seed=item.id;
    thread.buyer="buyer";thread.seller="seller";thread.proposer="buyer";thread.buyerName="Buyer";thread.sellerName="Seller";thread.status="pending";thread.revision=thread.listingRevision=1;thread.price=price;thread.events={{now,"proposed","Buyer","A message longer than fifteen characters",price}};
    Json sale={{"id","sale"},{"item","instance"},{"seed",item.id},{"code",item.code},{"quality",item.quality},{"item_index",item.index},{"ethereal",1},{"sockets",0},{"at",now},{"kind","completed_sale"},{"seller_name","Seller"},{"buyer_name","Buyer"},{"receipt","receipt"},{"price",PriceJson(price)}};
    Json doc={{"schema",1},{"game_mode","2.4"},{"mod","RMD-MP"},{"hardcore",false},{"expires_at",now+3600},{"self",{{"id","buyer"},{"name","Buyer"}}},{"history",Json::array({sale})},{"offers",Json::array({ThreadJson(thread)})}};
    auto activity=ReadActivity(doc,"RMD-MP",false,now);assert(activity.threads.size()==1&&activity.sales.size()==1);
    assert(ItemSales(activity,item,false).size()==1&&ItemSales(activity,item,true).size()==1&&ChartCurrency(price[0],0)&&!ChartCurrency(price[0],1));
    auto waiting=doc;waiting["offers"][0]["status"]="accepted";
    waiting["offers"][0]["settlement_progress"]={{"code","seller_deposit"},{"label","Waiting for seller's Vault deposit"}};
    assert(ReadActivity(waiting,"RMD-MP",false,now).threads[0].settlementLabel=="Waiting for seller's Vault deposit");
    auto changed=doc;changed["history"][0]["kind"]="accept";Refuses([&]{ReadActivity(changed,"RMD-MP",false,now);});
    changed=doc;changed["history"].push_back(sale);Refuses([&]{ReadActivity(changed,"RMD-MP",false,now);});
    changed=doc;changed["self"]["id"]="unrelated";Refuses([&]{ReadActivity(changed,"RMD-MP",false,now);});
    Refuses([&]{ReadActivity(doc,"Other",false,now);});Refuses([&]{ReadActivity(doc,"RMD-MP",true,now);});Refuses([&]{ReadActivity(doc,"RMD-MP",false,now+3601);});
    changed=doc;auto duplicateSeed=sale;duplicateSeed["id"]="sale-2";duplicateSeed["item"]="separate-instance";duplicateSeed["receipt"]="receipt-2";changed["history"].push_back(duplicateSeed);
    activity=ReadActivity(changed,"RMD-MP",false,now);assert(SeedAmbiguous(activity,item)&&ItemSales(activity,item,false).empty());assert(ItemSales(activity,item,false,"instance").size()==1&&ItemSales(activity,item,true).size()==2);
    LocalActivity local;thread.local=true;thread.status="draft";local.threads.push_back(thread);
    auto restored=ReadLocalActivity(LocalActivityJson(local,"RMD-MP",false),"RMD-MP",false);assert(restored.threads[0].events[0].message.size()>15);
    assert(MergeThreads(activity,restored).size()==1); // Remote authority replaces local draft state.
    std::cout<<"Activity: completed-only history, seed collision separation, comparable identity, currency separation, private conversations, message persistence and mode/expiry guards passed.\n";
}
