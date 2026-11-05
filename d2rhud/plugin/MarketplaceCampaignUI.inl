// Community gifts use native JSON widgets and the existing guarded command
// outbox. Gift item decoding happens on the workspace worker, not per frame.
inline const GiftCampaign* SelectedCampaign(State& s){
    const auto& gifts=s.snapshot.activity.gifts;
    if(gifts.empty())return nullptr;
    const auto found=std::find_if(gifts.begin(),gifts.end(),[&](const auto& g){return g.id==s.campaignId;});
    if(found!=gifts.end())return &*found;s.campaignId=gifts.front().id;s.campaignItemOffset=0;return &gifts.front();
}
inline void CampaignReceipt(State& s,const std::string& title,const std::string& message){
    OpenReceipt(s,"Gift claimed",title+"\n\nItems are queued for your market page. Gold and bag currencies are available in your Vault.\n\n"+message,FreePrice());
}
inline void PollCampaigns(State& s){
    if(s.campaignPending.empty()||!s.snapshot.activity.results.contains(s.campaignPending))return;
    const auto r=s.snapshot.activity.results.at(s.campaignPending);s.campaignPending.clear();
    if(r.contains("error")){Notice(s,r.at("error").get<std::string>());return;}
    const auto id=r.value("campaign_id",s.campaignId);const auto found=std::find_if(s.snapshot.activity.gifts.begin(),s.snapshot.activity.gifts.end(),[&](const auto& g){return g.id==id;});
    if(s.open&&s.campaignOpen&&s.campaignId==id)CampaignReceipt(s,found!=s.snapshot.activity.gifts.end()?found->title:"Community gift",r.value("message",std::string()));
    Notice(s,"Gift claimed. Check your market page and Vault.");s.refresh=true;
}
inline bool CampaignCommand(State& s,uint64_t action){
    auto is=[&](const char* name){return action==Panel::MessageHash(name);};
    if(is("MarketCampaignOpen")){
        s.campaignOpen=true;s.campaignItemOffset=0;s.editing=s.adding=s.counterOffer=s.historyOpen=s.receiptOpen=s.walletOpen=s.walletForm=s.transferOpen=s.filtersOpen=s.offerBoard=false;s.ownSelection=s.marketSelection=-1;s.replyTo.clear();SelectedCampaign(s);return true;
    }
    if(!s.campaignOpen)return false;
    if(is("MarketCampaignBack")){s.campaignOpen=false;s.receiptOpen=false;return true;}
    const auto g=SelectedCampaign(s);if(!g)return false;
    if(is("MarketCampaignPrev")||is("MarketCampaignNext")){
        const auto& gifts=s.snapshot.activity.gifts;const auto index=size_t(g-gifts.data());const auto next=(index+gifts.size()+(is("MarketCampaignNext")?1:-1))%gifts.size();s.campaignId=gifts[next].id;s.campaignItemOffset=0;s.receiptOpen=false;return true;
    }
    if(is("MarketCampaignItemsPrev")){s.campaignItemOffset=(std::max)(0,s.campaignItemOffset-4);return true;}
    if(is("MarketCampaignItemsNext")){if(s.campaignItemOffset+4<int(g->items.size()+g->balances.size()))s.campaignItemOffset+=4;return true;}
    if(!is("MarketCampaignClaim"))return false;
    if(g->claimed){CampaignReceipt(s,g->title,g->message);return true;}
    if(!s.campaignPending.empty())return true;
    if(!g->claimable||(g->expires&&g->expires<=UnixNow())){Notice(s,"This gift is unavailable. Link your player code or refresh its claim deadline.");return true;}
    try{s.campaignPending=QueueSocial(s,{{"type","campaign_claim"},{"campaign_id",g->id}});Notice(s,"Claiming your gift...");}catch(const std::exception& e){Notice(s,e.what());}return true;
}
inline void PresentCampaigns(State& s,Panel::NativeView& view,const LootFilterConfig::Builder::Catalog& catalog){
    auto show=[&](const std::string& n,bool visible){view.widgets.push_back({n,{},visible});};
    auto text=[&](const std::string& n,const std::string& value){view.widgets.push_back({n,value.empty()?" ":value,true});};
    const auto& gifts=s.snapshot.activity.gifts;const auto ready=std::count_if(gifts.begin(),gifts.end(),[](const auto& g){return !g.claimed;});
    show("MarketCampaignBadge",!gifts.empty());show("MarketCampaignBadgeGlow",ready>0);
    text("MarketCampaignOpenButtonLabel",ready?"Gifts ("+std::to_string(ready)+")":"Community gifts");
    show("MarketCampaignPanel",s.campaignOpen&&!s.receiptOpen);if(!s.campaignOpen)return;
    show("MarketListGroup",s.receiptOpen);show("MarketSetupGroup",false);show("MarketWalletGroup",false);show("MarketVaultNavigation",false);show("MarketGiftGroup",false);
    show("MarketStatusBackground",true);show("MarketFooterDivider",true);show("MarketRefreshButton",true);
    if(s.receiptOpen){
        show("MarketOfferBoard",false);show("MarketCollection",false);show("MarketFocus",true);show("MarketBackListButton",false);show("MarketDetailTitle",false);show("MarketFocusDivider",false);show("MarketItemHero",false);show("MarketItemDetail",false);show("MarketPriceEditor",false);
        PresentActivity(s,view);return;
    }
    const auto g=SelectedCampaign(s);show("MarketCampaignEmpty",!g);show("MarketCampaignBody",g!=nullptr);if(!g){text("MarketCampaignTitle","Community gifts");return;}
    text("MarketCampaignTitle",CompactActivity(g->title,34,2));
    text("MarketCampaignFrom","A gift from "+g->from);
    text("MarketCampaignExpiry",g->expires?"Claim before "+ActivityDate(g->expires):"No claim deadline");
    text("MarketCampaignAnnouncement",CompactActivity(g->announcement,80,3));
    text("MarketCampaignNumber","Gift "+std::to_string(size_t(g-gifts.data())+1)+" of "+std::to_string(gifts.size()));
    show("MarketCampaignPrevButton",gifts.size()>1);show("MarketCampaignNextButton",gifts.size()>1);
    const auto total=g->items.size()+g->balances.size();
    s.campaignItemOffset=(std::min)(s.campaignItemOffset,int((total?total-1:0)/4)*4);
    text("MarketCampaignItemsPage",std::to_string(g->items.size())+" item(s) + "+std::to_string(g->balances.size())+" currencies | Bundle page "+std::to_string(s.campaignItemOffset/4+1));
    for(int slot=0;slot<4;++slot){const auto i=s.campaignItemOffset+slot;show("MarketCampaignCard"+std::to_string(slot),i<int(total));if(i>=int(total))continue;
        if(i>=int(g->items.size())){const auto& b=g->balances[i-g->items.size()];const auto art=s.visuals.Resolve(b.code,2,-1,-1);
            view.images.push_back({"MarketCampaignItemImage"+std::to_string(slot),art.filename,112,180,bool(art)});
            view.previews.push_back({"MarketCampaignItemLink"+std::to_string(slot),CompactActivity(b.label,30,2),{}});
            text("MarketCampaignItemFacts"+std::to_string(slot),std::to_string(b.quantity)+"\nVault credit on claim");continue;}
        const auto& item=g->items[i];
        const auto name=Name(item,catalog);const auto art=s.visuals.Resolve(item.code,item.quality,item.index,item.picture);
        view.images.push_back({"MarketCampaignItemImage"+std::to_string(slot),art.filename,112,180,bool(art)});
        view.previews.push_back({"MarketCampaignItemLink"+std::to_string(slot),CompactActivity(name,30,2),item.itemHex});
        text("MarketCampaignItemFacts"+std::to_string(slot),"Quantity "+std::to_string(item.quantity)+"\nSelect the name to inspect");
    }
    show("MarketCampaignItemsPrevButton",s.campaignItemOffset>0);show("MarketCampaignItemsNextButton",s.campaignItemOffset+4<int(total));
    text("MarketCampaignHelp",g->claimed?"You have received this gift. View the owner's message anytime.":"Claim once. Items go to your market page; gold and currencies are added to your Vault.");
    show("MarketCampaignClaimButton",g->claimed||g->claimable);
    text("MarketCampaignClaimButtonLabel",!s.campaignPending.empty()?"Claiming...":g->claimed?"View message":"Claim gift");
}
