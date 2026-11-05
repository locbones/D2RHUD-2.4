// Native-art marketplace, opened from the stash's shop button.
#include "MarketplaceStash.h"
#include "MarketplaceStore.h"
#include "MarketplacePanel.h"
#include "MarketplaceWorkspace.h"
#include <future>
#include "MarketplaceItemVisuals.h"
#include "MarketplaceCodexNames.h"

namespace Marketplace::UI {
struct TablesResult {SaveTables tables;ItemVisuals::Catalog visuals;std::string error;};
struct SnapshotResult {WorkspaceSnapshot data;uint64_t generation=0;};
enum class Section {Browse,Items,Agreements,Setup};
struct PublicationExportState {std::string original,fingerprint;bool loaded=false;int64_t at=0;};
struct State {
    std::filesystem::path path;
    Settings settings;
    std::string original,status,tableError,query,priceSaveError,listingComment;
    bool valid=false,hardcore=false,editing=false,adding=false;
    bool tablesStarted=false;
    uint64_t statusUntil=0,resetPoll=0;std::string resetId;
    Panel::Frame context;
    SavedItem selected;
    std::vector<Requirement> price;
    SaveTables tables;
    ItemVisuals::Catalog visuals;
    std::future<TablesResult> loading;
    bool open=false,refresh=true,affordableOnly=false,pricedOnly=false,filtersOpen=false;int termsOffset=0;
    Section section=Section::Browse;
    std::string search;
    int sort=0,quality=0,searchScope=0,tabFilter=0,ownSelection=-1,marketSelection=-1;
    std::string sellerSearch,sellerFilter,profileName,offerListing,offerRequest;
    int favoriteFilter=0;bool hideOwn=false,hideOtherOffers=false,offerBoard=false;
    Json offerPage=Json::object();std::vector<int64_t> offerBefore;std::vector<std::string> socialPending;uint64_t socialPoll=0;
    uint64_t sellerRevision=1,profileRevision=1;
    int depositChoice=1,withdrawChoice=2;
    bool enabledChoice=false,autoChoice=false;
    bool ownSeller=false,counterOffer=false;Listing counterTarget;
    WorkspaceSnapshot snapshot;
    std::vector<SavedItem> ownInventory;
    std::future<SnapshotResult> scanning;
    uint64_t generation=1;
    uint64_t liveRequest=0;
    bool liveDeposit=false,liveSeller=false;unsigned livePage=0;
    std::map<std::tuple<std::string,int,int,bool>,std::string> names;
    int listOffset=0,priceOffset=0,paymentOffset=0,paymentKind=0;
    LocalActivity local;std::string localOriginal,message,replyTo;bool localValid=true,historyOpen=false;
    int activityType=1,historyKind=0,historyOffset=0,chartCurrency=0,threadOffset=0,replyThread=-1;
    uint64_t messageRevision=1;
    bool walletOpen=false,walletForm=false,walletReview=false;int walletMode=0,walletOffset=0;uint64_t walletRevision=1;std::string walletAmount,walletPending;Json walletAsset,walletCancel;
    bool receiptOpen=false;std::string receiptTitle,receiptDescription;std::vector<Requirement> receiptPrice;
    bool campaignOpen=false;std::string campaignId,campaignPending;int campaignItemOffset=0;
    bool transferOpen=false,transferReview=false;SavedItem transferItem;Json transferBalance;
    std::string transferTo,transferRecipient,transferName,transferNote,transferAmount,transferResolve,transferPending;uint64_t transferRevision=1;
    bool withdrawalConfirm=false;SavedItem withdrawalItem;std::string withdrawalWatchVersion;std::vector<Panel::WithdrawalWatch> withdrawalWatches;
    int offerAction=0,replyRevision=0,receiptMode=0,receiptPage=0;uint64_t activityGeneration=1,threadGeneration=0;std::vector<CounterThread> mergedThreads;
    SavedItem threadItem;std::string threadItemHex;
    std::array<PublicationExportState,2> publications;std::string publishError;
    uint64_t searchRevision=1,routeRevision=1,priceRevision=1,paymentRevision=1,filterRevision=1;
};
inline State& EditorState(){static auto* state=new State;return *state;}
inline std::string TradeRequirementName(const Requirement& r){auto shown=r;shown.label=CodexName(modName,r.code,r.quality,r.label);return RequirementName(shown);}
inline std::string Name(const SavedItem& item,const LootFilterConfig::Builder::Catalog& catalog) {
    const auto key=std::tuple(item.code,item.quality,item.index,bool(item.flags&(1u<<4)));
    auto& names=EditorState().names;if(auto found=names.find(key);found!=names.end())return found->second;
    const auto base=catalog.bases.find(item.code);const auto baseName=CodexName(modName,item.code,2,base==catalog.bases.end()?item.code:base->second.name);
    if(!(item.flags&(1u<<4)))return "Unidentified "+baseName;
    for(const auto& named:catalog.namedItems)if(named.code==item.code&&named.index==item.index&&
        ((item.quality==7&&named.quality=="Unique")||(item.quality==5&&named.quality=="Set"))){const auto label=CodexName(modName,item.code,item.quality,named.label);if(catalog.loaded)names[key]=label;return label;}
    if(catalog.loaded)names[key]=baseName;return baseName;
}
inline void Save(State& state,const std::string& mod){
    const auto next=SettingsJson(state.settings,mod,state.hardcore).dump(2)+"\n";
    // A companion can reformat an equivalent document. Keep the actual CAS
    // against the bytes just read, while still refusing semantic conflicts.
    const auto current=ReadSettingsFile(state.path);
    if(current!=state.original&&(current.empty()||state.original.empty()||Json::parse(current)!=Json::parse(state.original)))
        throw std::runtime_error("Marketplace settings changed in another instance. Reload before saving.");
    SaveSettingsFile(state.path,current,next);state.original=next;
}
inline void Load(State& state,const std::string& mod){
    state.valid=false;state.original=ReadSettingsFile(state.path);
    state.settings=state.original.empty()?Settings{}:ReadSettings(Json::parse(state.original),mod,state.hardcore);
    // Retire the local test-seller route without losing its prices or old logs.
    // The separate service test trader remains available to development tools.
    if(state.settings.simulation.enabled){state.settings.simulation.enabled=false;Save(state,mod);}
    state.ownSeller=false;
    state.valid=true;
    state.local={};state.localValid=false;state.localOriginal=ReadSettingsFile(state.path.parent_path()/(state.hardcore?L"MarketplaceActivityLocal_2.4_HC.json":L"MarketplaceActivityLocal_2.4_SC.json"));
    if(!state.localOriginal.empty())state.local=ReadLocalActivity(Json::parse(state.localOriginal),mod,state.hardcore);state.localValid=true;++state.activityGeneration;
}
inline void Close(State& state){state.campaignOpen=false;state.filtersOpen=false;state.termsOffset=0;state.transferOpen=false;state.transferResolve.clear();state.walletOpen=false;state.offerBoard=false;state.open=state.editing=state.adding=state.counterOffer=state.historyOpen=state.receiptOpen=state.withdrawalConfirm=false;Panel::SetPin({});Panel::CancelScan();state.liveRequest=0;}
inline void Refresh(State& state){state.offerBoard=false;++state.generation;state.refresh=true;state.snapshot={};state.liveDeposit=state.liveSeller=false;state.liveRequest=0;state.livePage=0;Panel::CancelScan();state.ownSelection=state.marketSelection=-1;state.editing=state.adding=state.counterOffer=state.historyOpen=state.receiptOpen=false;state.historyOffset=state.threadOffset=state.receiptPage=0;++state.activityGeneration;}
inline void SetupChoices(State& state){state.profileName=state.snapshot.activity.name;++state.profileRevision;state.depositChoice=state.settings.page;state.withdrawChoice=state.settings.withdrawPage;state.enabledChoice=state.settings.enabled;state.autoChoice=state.settings.autoTrade;++state.routeRevision;}
inline const Draft* AskingPrice(const State& state,const SavedItem& item){
    for(const auto& draft:state.settings.drafts)if(draft.tab==item.tab&&draft.itemHex==item.itemHex)return &draft;return nullptr;
}
inline void Notice(State& state,const std::string& text){state.status=text;state.statusUntil=GetTickCount64()+5000;}
inline void PollMarketplaceReset(State& state){
    if(GetTickCount64()<state.resetPoll)return;state.resetPoll=GetTickCount64()+1000;
    try{const auto marker=Json::parse(ReadFile(state.path.parent_path()/(state.hardcore?L"MarketplaceReset_2.4_HC.json":L"MarketplaceReset_2.4_SC.json"),4096));
        if(marker.at("schema")!=1||marker.at("game_mode")!="2.4"||marker.at("mod")!=modName||marker.at("hardcore")!=state.hardcore)return;
        const auto id=marker.at("id").get<std::string>();if(id.size()!=32||id==state.resetId)return;
        const bool open=state.open;Close(state);Load(state,modName);SetupChoices(state);Refresh(state);state.open=open;state.section=Section::Items;
        state.message.clear();state.replyTo.clear();state.socialPending.clear();state.walletPending.clear();state.transferPending.clear();state.offerRequest.clear();state.resetId=id;
        Notice(state,"Marketplace reset complete. Old trade drafts were cleared.");
    }catch(...){} // A missing or interrupted sidecar never interrupts the game.
}
inline const std::vector<SavedItem>& OwnItems(const State& state){return state.ownSeller?state.snapshot.seller:state.snapshot.feed.escrowItems.empty()?state.snapshot.deposit:state.ownInventory;}
inline void UpdateOwnInventory(State& state){state.ownInventory=state.snapshot.deposit;state.ownInventory.insert(state.ownInventory.end(),state.snapshot.feed.escrowItems.begin(),state.snapshot.feed.escrowItems.end());}
inline const std::vector<CounterThread>& Threads(State& state);
// Background scans replace/reorder collections. Keep the user's place by exact
// item/record identity instead of a row index (or clearing every selection).
struct SelectionBookmark {
    std::string listing,thread;SavedItem item;bool own=false;
    explicit SelectionBookmark(State& state){
        if(state.section==Section::Items&&state.ownSelection>=0&&state.ownSelection<int(OwnItems(state).size())){item=OwnItems(state)[state.ownSelection];own=true;}
        if(state.section==Section::Browse&&state.marketSelection>=0&&state.marketSelection<int(state.snapshot.feed.listings.size()))listing=state.snapshot.feed.listings[state.marketSelection].id;
        if(state.section==Section::Agreements&&state.activityType==0&&state.marketSelection>=0&&state.marketSelection<int(Threads(state).size()))thread=Threads(state)[state.marketSelection].id;
    }
    void Restore(State& state)const{
        auto find=[](const auto& rows,auto matches){int found=-1;for(int i=0;i<int(rows.size());++i)if(matches(rows[i])){if(found>=0)return -1;found=i;}return found;};
        if(own)state.ownSelection=find(OwnItems(state),[&](const auto& row){return item.escrowId.empty()?row.escrowId.empty()&&row.tab==item.tab&&row.itemHex==item.itemHex:row.escrowId==item.escrowId;});
        if(!listing.empty())state.marketSelection=find(state.snapshot.feed.listings,[&](const auto& row){return row.id==listing;});
        if(!thread.empty())state.marketSelection=find(Threads(state),[&](const auto& row){return row.id==thread;});
    }
};
inline const Draft* OwnPrice(const State& state,const SavedItem& item){
    if(!item.escrowId.empty())return item.escrowState=="listed"&&!item.escrowDraft.price.empty()?&item.escrowDraft:nullptr;
    const auto& drafts=state.ownSeller?state.settings.simulation.drafts:state.settings.drafts;
    for(const auto& draft:drafts)if(draft.tab==item.tab&&draft.itemHex==item.itemHex)return &draft;return nullptr;
}
inline void ExportPublication(State& state,bool test=false){
    const auto enabled=test?state.settings.simulation.enabled:state.settings.enabled;const auto valid=test?state.snapshot.sellerValid:state.snapshot.depositValid;
    if(!state.valid||(enabled&&!valid))return;
    if(ReadSettingsFile(state.path)!=state.original)throw std::runtime_error("Marketplace settings changed in another instance. Reload before saving.");
    const auto& inventory=test?state.snapshot.seller:state.snapshot.deposit;const auto& drafts=test?state.settings.simulation.drafts:state.settings.drafts;
    Json rows=Json::array();if(enabled)for(const auto& item:inventory){const auto price=std::find_if(drafts.begin(),drafts.end(),[&](const auto& d){return d.tab==item.tab&&d.itemHex==item.itemHex;});if(price==drafts.end())continue;auto row=item.ToJson();row["price"]=PriceJson(price->price);row["comment"]=price->comment;rows.push_back(std::move(row));}
    if(rows.size()>512)throw std::runtime_error("Publish at most 512 priced items at a time.");
    Json doc={{"schema",1},{"game_mode","2.4"},{"mod",modName},{"hardcore",state.hardcore},{"page",test?state.settings.simulation.page:state.settings.page},{"enabled",enabled},{"source",(test?state.liveSeller:state.liveDeposit)?"live":"saved"},{"custody","local_save"},{"purchasable",false},{"items",rows}};
    auto& publication=state.publications[test?1:0];const auto fingerprint=doc.dump();const auto now=UnixNow();if(fingerprint==publication.fingerprint&&now<publication.at+300)return;
    const auto path=state.path.parent_path()/((test?std::wstring(L"MarketplaceTestPublish_2.4_"):std::wstring(L"MarketplacePublish_2.4_"))+(state.hardcore?L"HC.json":L"SC.json"));
    // The companion also publishes canonical saved items. Its replacement of
    // this derived snapshot is expected; only the actual settings use the
    // cached revision above. Keep the atomic compare-and-swap for this write.
    publication.original=ReadSettingsFile(path);publication.loaded=true;
    if(!publication.original.empty()){
        const auto previous=Json::parse(publication.original);
        if(previous.value("schema",0)!=1||previous.value("mod",std::string())!=modName||previous.value("game_mode",std::string())!="2.4"||previous.at("hardcore")!=state.hardcore)
            throw std::runtime_error("Publication belongs to a different game mode.");
    }
    doc["id"]=ActivityId();doc["captured_at"]=now;const auto next=doc.dump(2)+"\n";if(next.size()>8*1024*1024)throw std::runtime_error("Publication exceeds the service size limit.");
    SaveSettingsFile(path,publication.original,next);publication.original=next;publication.fingerprint=fingerprint;publication.at=now;
}
inline void RefreshListings(State& state){ApplyLineage(state.snapshot.deposit,state.snapshot.feed);AppendSimulation(state.snapshot.feed,state.snapshot.seller,state.settings,state.snapshot.sellerValid);AppendOwnListings(state.snapshot.feed,state.snapshot.deposit,state.settings,state.snapshot.depositValid);
    state.publishError.clear();for(bool test:{false,true})try{ExportPublication(state,test);}catch(const std::exception& e){state.publishError=std::string(test?"Test seller publication unavailable: ":"Listing publication unavailable: ")+e.what();}}
inline bool OwnEnabled(const State& state){return state.ownSeller?state.settings.simulation.enabled:state.settings.enabled;}
inline const std::vector<CounterThread>& Threads(State& state){if(state.threadGeneration!=state.activityGeneration){state.mergedThreads=MergeThreads(state.snapshot.activity,state.local);state.threadGeneration=state.activityGeneration;}return state.mergedThreads;}
inline void SaveLocal(State& state){
    if(!state.localValid)throw std::runtime_error("Reload the activity log before editing it.");
    const auto document=LocalActivityJson(state.local,modName,state.hardcore);(void)ReadLocalActivity(document,modName,state.hardcore);const auto next=document.dump(2)+"\n";
    SaveSettingsFile(state.path.parent_path()/(state.hardcore?L"MarketplaceActivityLocal_2.4_HC.json":L"MarketplaceActivityLocal_2.4_SC.json"),state.localOriginal,next);state.localOriginal=next;++state.activityGeneration;
}
inline bool WritePrice(State& state,bool remove){
    state.priceSaveError.clear();
    if(!remove&&!ValidListingComment(state.listingComment)){state.priceSaveError="Write a listing comment of up to 240 characters.";Notice(state,state.priceSaveError);return false;}
    if(!state.selected.escrowId.empty()){
        const auto before=state.local;
        try{Json command={{"id",ActivityId()},{"type",remove?"escrow_return":"escrow_price"},{"item_id",state.selected.escrowId}};
            if(!remove){command["price"]=PriceJson(state.price);command["comment"]=state.listingComment;}state.local.commands.push_back(std::move(command));SaveLocal(state);
            Notice(state,remove?"Return queued for page "+std::to_string(state.settings.withdrawPage)+". The stash refreshes automatically when idle.":"Asking price update queued. Refresh after sync.");state.editing=state.adding=false;Panel::SetPin({});
        }catch(const std::exception& e){state.local=before;state.priceSaveError=e.what();Notice(state,e.what());return false;}return true;
    }
    try{
        const auto current=ReadSettingsFile(state.path);
        auto latest=current.empty()?Settings{}:ReadSettings(Json::parse(current),modName,state.hardcore);
        const auto baseline=state.original.empty()?Settings{}:ReadSettings(Json::parse(state.original),modName,state.hardcore);
        // Merge only this item's price into the latest settings. A sync of an
        // unrelated item/preference must not erase the user's submitted edit.
        // Setup changes and competing edits of this same item remain conflicts.
        if(latest.enabled!=baseline.enabled||latest.page!=baseline.page||latest.simulation.enabled!=baseline.simulation.enabled||latest.simulation.page!=baseline.simulation.page)
            throw std::runtime_error("Trade setup changed while pricing this item. Reload before saving; your entered price is still here.");
        auto& drafts=state.ownSeller?latest.simulation.drafts:latest.drafts;
        const auto& original=state.ownSeller?baseline.simulation.drafts:baseline.drafts;
        auto price=[&](const auto& rows){for(const auto& draft:rows)if(draft.tab==state.selected.tab&&draft.itemHex==state.selected.itemHex)return Json{{"price",PriceJson(draft.price)},{"comment",draft.comment}};return Json::object();};
        const auto wanted=remove?Json::object():Json{{"price",PriceJson(state.price)},{"comment",state.listingComment}};
        if(price(drafts)!=price(original)&&price(drafts)!=wanted)
            throw std::runtime_error("This item's asking price changed in another instance. Your entered price is still here; refresh to review the other change.");
        std::erase_if(drafts,[&](const Draft& draft){return draft.tab==state.selected.tab&&draft.itemHex==state.selected.itemHex;});
        if(!remove)drafts.push_back({state.selected.tab,state.selected.itemHex,state.price,state.listingComment});
        const auto next=SettingsJson(latest,modName,state.hardcore).dump(2)+"\n";
        SaveSettingsFile(state.path,current,next);state.settings=std::move(latest);state.original=next;
        RefreshListings(state);Notice(state,remove?"Asking price removed.":!OwnEnabled(state)?"Price saved. Enable your deposit in Setup to list it.":"Asking price saved as a draft.");state.editing=state.adding=false;Panel::SetPin({});return true;
    }catch(const std::exception& e){state.priceSaveError=e.what();Notice(state,e.what());return false;}
}
inline const char* Quality(int q){switch(q){case 3:return "Superior";case 4:return "Magic";case 5:return "Set";case 6:return "Rare";case 7:return "Unique";case 8:return "Crafted";default:return "Normal";}}
inline bool FilterQuality(const SavedItem& item,int f){return !f||(f==1&&item.quality==7)||(f==2&&item.quality==5)||(f==3&&item.quality==4)||(f==4&&item.quality==6)||(f==5&&item.code.size()==3&&item.code[0]=='r'&&item.code[1]>='0'&&item.code[1]<='9'&&item.code[2]>='0'&&item.code[2]<='9');}
inline bool TradeMatches(const SavedItem& item,const std::string& seller,const std::vector<Requirement>& price,
    const std::string& query,int quality,int scope,const LootFilterConfig::Builder::Catalog& catalog){
    const auto needle=HudOptions::Lower(query);
    const bool sellerMatch=!needle.empty()&&HudOptions::Lower(seller).find(needle)!=std::string::npos;
    auto matches=[&](const SavedItem& value,const std::string& label){
        return FilterQuality(value,quality)&&(needle.empty()||sellerMatch||
            HudOptions::Lower(Name(value,catalog)+" "+value.code+" "+label).find(needle)!=std::string::npos);
    };
    if(scope!=2&&matches(item,{}))return true;
    if(scope!=1)for(const auto& payment:price){
        SavedItem wanted;wanted.code=payment.code;wanted.quality=payment.quality;wanted.index=payment.index;wanted.flags=1u<<4;
        // Balance rows are payment too, but never match an equipment-quality filter.
        if(!payment.kind.empty()&&payment.kind!="item"&&quality)continue;
        if(matches(wanted,TradeRequirementName(payment)))return true;
    }
    return false;
}
inline void Agree(State& state,const Listing& listing){
    if(listing.own){Notice(state,"This is your local listing. Edit its price in My Items.");return;}
    const auto before=state.settings.agreements;std::erase_if(state.settings.agreements,[&](const auto& a){return a.simulated==listing.simulated&&a.seller==listing.seller&&a.listing==listing.id;});
    state.settings.agreements.push_back({listing.seller,listing.id,listing.revision,listing.item.itemHex,listing.price,listing.simulated});
    try{Save(state,modName);Notice(state,listing.simulated?"SIMULATION: seller accepted. Test agreement saved; no items moved.":"Agreement draft saved. Nothing has been submitted or transferred.");}
    catch(const std::exception& e){state.settings.agreements=before;Notice(state,e.what());}
}
inline bool PendingCommand(const State& state,const std::string& offer){for(const auto& c:state.local.commands)if(c.value("offer_id",c.value("id",std::string()))==offer&&!state.snapshot.activity.results.contains(c.at("id").get<std::string>()))return true;return false;}
inline bool ThreadAction(State& state,const CounterThread& thread,const std::string& action,const std::vector<Requirement>& price={}){
    auto before=state.local;
    try{
        if(state.message.size()>2048)throw std::runtime_error("Keep messages within 512 characters.");
        const auto self=state.snapshot.activity.self.empty()?"local-player":state.snapshot.activity.self;
        if(!thread.simulated&&PendingCommand(state,thread.id))throw std::runtime_error("Sync the pending action before replying again.");
        if(action!="message"&&thread.status!="pending"&&!(thread.local&&action=="withdraw"&&thread.status=="draft"))throw std::runtime_error("This offer is no longer awaiting a decision.");
        if(action=="message"&&state.message.empty())throw std::runtime_error("Write a message first.");
        if(!thread.simulated&&action!="message"&&((action=="withdraw")!=(thread.proposer==self)))throw std::runtime_error("Only the recipient can accept, reject or counter this proposal.");
        if(!thread.simulated&&!thread.local&&state.snapshot.activity.expires<=UnixNow())throw std::runtime_error("Sync this conversation before responding.");
        auto at=std::find_if(state.local.threads.begin(),state.local.threads.end(),[&](const auto& t){return t.id==thread.id;});
        if(at==state.local.threads.end()){state.local.threads.push_back(thread);at=state.local.threads.end()-1;at->local=true;std::erase_if(at->events,[](const auto& event){return event.kind=="sync_error";});}
        const auto terms=action=="counter"?price:thread.price;
        if(thread.simulated||thread.status=="draft"){
            if(action=="accept")at->status="accepted";if(action=="reject")at->status="rejected";if(action=="withdraw")at->status="withdrawn";
            if(action=="counter"){at->price=price;at->proposer=self;}
            at->events.push_back({UnixNow(),action,thread.simulated?"You (test)":"You (local draft)",state.message,action=="message"?std::vector<Requirement>{}:terms});++at->revision;
        }else{
            Json c={{"id",ActivityId()},{"offer_id",thread.id},{"type",action},{"expected_revision",thread.revision},{"message",state.message}};
            if(action=="counter")c["price"]=PriceJson(price);state.local.commands.push_back(c);
            at->events.push_back({UnixNow(),"queued_"+action,"You",state.message,action=="message"?std::vector<Requirement>{}:terms});
        }
        SaveLocal(state);state.message.clear();++state.messageRevision;Notice(state,thread.simulated?"Test response logged. No sale or transfer occurred.":thread.status=="draft"?"Local draft updated. Connect the service to exchange messages.":"Response queued. Sync confirms delivery and the seller's decision.");return true;
    }catch(const std::exception& e){state.local=std::move(before);Notice(state,e.what());return false;}
}
inline void SubmitCounter(State& state){
    if(!state.replyTo.empty()){
        const auto& threads=Threads(state);const auto t=std::find_if(threads.begin(),threads.end(),[&](const auto& t){return t.id==state.replyTo;});
        if(t==threads.end()||t->revision!=state.replyRevision){Notice(state,"Conversation changed. Refresh and try again.");return;}
        const auto thread=*t;if(ThreadAction(state,thread,"counter",state.price)){state.replyTo.clear();state.editing=state.adding=state.counterOffer=false;state.section=Section::Agreements;state.activityType=0;}return;
    }
    const AgreementDraft expected{state.counterTarget.seller,state.counterTarget.id,state.counterTarget.revision,state.counterTarget.item.itemHex,state.counterTarget.price,state.counterTarget.simulated};
    const auto listing=std::find_if(state.snapshot.feed.listings.begin(),state.snapshot.feed.listings.end(),[&](const auto& l){return l.simulated&&SameTerms(expected,l);});
    const auto target=std::find_if(state.snapshot.feed.listings.begin(),state.snapshot.feed.listings.end(),[&](const auto& l){return SameTerms(expected,l);});
    if(target==state.snapshot.feed.listings.end()||(!target->simulated&&!ListingFresh(state.snapshot.feed,*target,UnixNow()))||(target->simulated&&!state.settings.simulation.enabled)){Notice(state,"The offer changed. Refresh and review it again.");return;}
    if(!IsFree(state.price)&&((!state.snapshot.depositValid&&state.snapshot.feed.escrowFunds.empty())||!state.settings.enabled||!CanPay(state.price,Spendable(state.snapshot.deposit,state.settings,state.snapshot.goldValid?state.snapshot.depositGold:0,&state.snapshot.feed)))){Notice(state,"Your unpriced deposit must contain the complete counter-offer.");return;}
    auto before=state.local;
    try{
        CounterThread t;t.id=ActivityId();t.listing=target->id;t.itemId=target->itemId;t.itemHex=target->item.itemHex;t.seed=target->item.id;t.buyer=state.snapshot.activity.self.empty()?"local-player":state.snapshot.activity.self;t.seller=target->sellerId.empty()?target->seller:target->sellerId;
        t.proposer=t.buyer;t.buyerName="You";t.sellerName=target->seller;t.price=state.price;t.simulated=target->simulated;t.development=target->development;t.local=true;
        const auto revision=target->revision;try{if(revision.find_first_not_of("0123456789")==std::string::npos)t.listingRevision=std::stoi(revision);}catch(...){}
        const bool connected=!t.simulated&&!target->sellerId.empty()&&!state.snapshot.activity.self.empty()&&t.listingRevision>0&&state.snapshot.activity.expires>UnixNow();
        t.status=t.simulated?"pending":connected?"queued":"draft";t.events.push_back({UnixNow(),"proposed","You",state.message,t.price});
        if(connected)state.local.commands.push_back({{"id",t.id},{"type","propose"},{"listing_id",t.listing},{"listing_revision",t.listingRevision},{"price",PriceJson(t.price)},{"message",state.message}});
        if(t.simulated){t.events.push_back({UnixNow(),SimulationAccepts(target->price,state.price)?"accept":"counter",target->seller,"Test seller response; no items moved.",target->price});t.status=SimulationAccepts(target->price,state.price)?"accepted":"pending";t.proposer=t.seller;t.price=target->price;}
        state.local.threads.push_back(t);SaveLocal(state);state.message.clear();++state.messageRevision;
    }catch(const std::exception& e){state.local=std::move(before);Notice(state,e.what());return;}
    if(target->simulated&&listing!=state.snapshot.feed.listings.end()&&SimulationAccepts(listing->price,state.price))Agree(state,*listing);
    else Notice(state,target->simulated?"SIMULATION: seller counters with its listed asking price. Review the payment terms.":"Counter-offer saved in Offers & Logs. Service sync is required for delivery.");
    state.editing=state.adding=state.counterOffer=false;
}
#include "MarketplaceWithdrawalUI.inl"
#include "MarketplaceNativeMenu.inl"
}
#include "MarketplaceCellBorders.inl"
namespace Marketplace::UI {
inline void DrawStash(bool allowed=true){
    auto& state=EditorState();const auto now=GetTickCount64();const auto view=Panel::Current();
    if(!allowed){Close(state);Panel::Hide();return;}
    if(view.suspended)return;
    if(!view.owner||!Panel::Fresh(now,view.tick)){Close(state);Panel::Hide();return;}
    if(state.context.owner!=view.owner||state.context.hardcore!=view.hardcore){Close(state);Refresh(state);}
    state.context=view;if(Panel::dismiss.exchange(false))Panel::NativeMessage(Panel::MessageHash("BankPanelMessage"),Panel::MessageHash("MarketClose"));
    const auto saved=GetSavePath();if(saved.empty()){Panel::Hide();return;}
    const auto folder=std::filesystem::path(saved)/L"Diablo II Resurrected"/L"Mods"/std::filesystem::u8path(modName);
    const auto path=folder/(view.hardcore?L"Marketplace_2.4_HC.json":L"Marketplace_2.4_SC.json");
    if(path!=state.path){Close(state);Refresh(state);state.names.clear();state.path=path;state.hardcore=view.hardcore;state.resetId.clear();state.resetPoll=0;state.tablesStarted=false;state.tables={};state.visuals={};state.tableError.clear();state.publications={};state.publishError.clear();
        try{Load(state,modName);SetupChoices(state);}catch(const std::exception& e){Notice(state,e.what());}
    }
    PollMarketplaceReset(state);
    const auto& catalog=GetLootFilterBuilderCatalog();
    if(!state.tablesStarted){state.tablesStarted=true;const auto install=std::filesystem::path(GetExecutableDir());const auto mod=modName;
        state.loading=std::async(std::launch::async,[install,mod](){TablesResult result;try{GameAssets assets(install,mod);auto tables=[&](const std::string& name){return assets.ReadTable(name,true).binary;};result.tables=LoadSaveTables(tables,HudOptions::Lower(mod)=="rmd-mp"?assets.ReadTableNames("itemstatcost",true):std::vector<std::string>{});
            try{result.visuals=ItemVisuals::Catalog::Load(tables,[&](const std::string& name){return assets.Read(name);},[&](const std::string& name){return assets.ReadTableNames(name,true);});}catch(const std::exception& e){result.error=std::string("Item artwork unavailable: ")+e.what();}}catch(const std::exception& e){result.error=e.what();}return result;});
    }
    if(state.loading.valid()&&state.loading.wait_for(std::chrono::seconds(0))==std::future_status::ready){auto result=state.loading.get();state.tables=std::move(result.tables);state.visuals=std::move(result.visuals);state.tableError=std::move(result.error);}
    if(state.scanning.valid()&&state.scanning.wait_for(std::chrono::seconds(0))==std::future_status::ready){
        try{auto result=state.scanning.get();if(result.generation==state.generation){
            const SelectionBookmark selection(state);
            // Live capture may win the race with the disk scan. Recover its
            // persisted IDs from the completed saved scan before preferring
            // the live positions; otherwise canonical asking prices disappear.
            if(state.liveDeposit){for(auto& item:state.snapshot.deposit)item=ReconcileLiveIdentity(std::move(item),result.data.deposit,state.tables);result.data.deposit=std::move(state.snapshot.deposit);result.data.depositValid=true;result.data.depositError.clear();result.data.depositGold=state.snapshot.depositGold;result.data.goldValid=state.snapshot.goldValid;}
            if(state.liveSeller){for(auto& item:state.snapshot.seller)item=ReconcileLiveIdentity(std::move(item),result.data.seller,state.tables);result.data.seller=std::move(state.snapshot.seller);result.data.sellerValid=true;result.data.sellerError.clear();}
            state.snapshot=std::move(result.data);++state.activityGeneration;RefreshListings(state);state.refresh=false;
            UpdateOwnInventory(state);selection.Restore(state);
        }}
        catch(const std::exception& e){Notice(state,e.what());state.refresh=false;}
    }
    if(state.open&&state.refresh&&!state.scanning.valid()&&!state.tables.items.empty()&&state.valid){
        state.refresh=false;const auto generation=state.generation;const auto settings=state.settings;const auto mod=modName;const auto hc=view.hardcore;auto tables=state.tables;
        state.scanning=std::async(std::launch::async,[folder,settings,mod,hc,tables=std::move(tables),generation](){return SnapshotResult{ReadWorkspace(folder,settings,mod,hc,tables),generation};});
    }
    if(state.liveRequest&&state.livePage!=view.page){Panel::CancelScan();state.liveRequest=0;}
    if(state.open&&!state.liveRequest&&(view.page==unsigned(state.settings.page)||(state.settings.simulation.enabled&&view.page==unsigned(state.settings.simulation.page)))&&view.tab&&!view.cursorOccupied&&!state.tables.items.empty()){
        state.livePage=view.page;state.liveRequest=Panel::RequestScan(view.owner,view.page,view.hardcore);
    }
    if(auto result=Panel::TakeScan(state.liveRequest);result.request.sequence){
        try{
            if(!result.error.empty())throw std::runtime_error(result.error);
            const SelectionBookmark selection(state);
            std::vector<SavedItem> items;std::array<unsigned,8> ordinals{};
            const auto& prior=result.request.page==unsigned(state.settings.page)?state.snapshot.deposit:state.snapshot.seller;
            for(const auto& item:result.items){auto savedItem=ReconcileLiveIdentity(ReadItem(item.bytes,state.tables,item.tab),prior,state.tables);savedItem.ordinal=++ordinals[item.tab];items.push_back(std::move(savedItem));}
            if(result.request.page==unsigned(state.settings.page)){state.snapshot.deposit=std::move(items);state.snapshot.depositValid=true;state.snapshot.depositError.clear();state.liveDeposit=true;state.snapshot.depositGold=result.gold;state.snapshot.goldValid=result.goldValid;}
            else if(state.settings.simulation.enabled&&result.request.page==unsigned(state.settings.simulation.page)){state.snapshot.seller=std::move(items);state.snapshot.sellerValid=true;state.snapshot.sellerError.clear();state.liveSeller=true;}
            RefreshListings(state);
            UpdateOwnInventory(state);selection.Restore(state);
        }catch(const std::exception& e){Notice(state,std::string("Live stash snapshot unavailable; using saved data. ")+e.what());}
    }
    CellBorders::Draw(state.path,state.settings,view,state.tables,state.open);
    UpdateWithdrawalPrompt(state,view,catalog);
    NativeMenu(state,view,catalog);
    Panel::Regions({},false,now); // The game widgets own input; ImGui captures nothing.
}
}
