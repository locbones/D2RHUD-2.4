// Included inside Marketplace::UI. Commands use the existing private outbox;
// all controls and offer cards are native widgets in the stash layout JSON.
inline bool Favorite(const State& state,const std::string& kind,const std::string& target){return state.snapshot.feed.favorites.contains({kind,target});}
inline bool SocialMatches(const State& state,const Listing& listing){
    return (!state.hideOwn||!listing.own)&&(!state.hideOtherOffers||!listing.otherOfferCount)&&
        (!state.sellerFilter.empty()?listing.sellerId==state.sellerFilter:state.sellerSearch.empty()||HudOptions::Lower(listing.seller).find(HudOptions::Lower(state.sellerSearch))!=std::string::npos)&&
        (!state.favoriteFilter||(state.favoriteFilter==1?Favorite(state,"listing",listing.id):Favorite(state,"seller",listing.sellerId)));
}
inline std::string QueueSocial(State& state,Json command){
    if(state.snapshot.activity.self.empty())throw std::runtime_error("Link the marketplace companion before changing your profile or favorites.");
    const auto before=state.local;const auto id=ActivityId();command["id"]=id;state.local.commands.push_back(std::move(command));
    try{SaveLocal(state);}catch(...){state.local=before;throw;}
    state.socialPending.push_back(id);state.socialPoll=0;return id;
}
inline void RequestOffers(State& state,int64_t before=0){
    if(!state.offerRequest.empty())return;
    Json query={{"type","offer_page"},{"listing_id",state.offerListing}};if(before)query["before"]=before;
    state.offerRequest=QueueSocial(state,std::move(query));state.offerPage=Json::object();
}
inline void PollSocial(State& state){
    bool pending=false;
    for(auto it=state.socialPending.begin();it!=state.socialPending.end();){
        const auto result=state.snapshot.activity.results.find(*it);
        if(result==state.snapshot.activity.results.end()){pending=true;++it;continue;}
        if(result->contains("error"))Notice(state,result->at("error").get<std::string>());
        if(*it==state.offerRequest){
            if(!result->contains("error")&&result->value("listing_id",std::string())==state.offerListing&&result->contains("rows")&&result->at("rows").is_array()&&result->at("rows").size()<=4)state.offerPage=*result;
            state.offerRequest.clear();
        }
        if(result->contains("name")){state.profileName=result->at("name").get<std::string>();++state.profileRevision;}
        it=state.socialPending.erase(it);
    }
    // Command acknowledgement only means the withdrawal was queued. Keep
    // reading the local cache through delivery, including browser-originated
    // requests, so the same row becomes Delivered without a manual Refresh.
    if((pending||state.walletOpen)&&GetTickCount64()>=state.socialPoll){state.refresh=true;state.socialPoll=GetTickCount64()+1000;}
}
inline bool SocialCommand(State& state,uint64_t command){
    auto is=[&](const std::string& name){return command==Panel::MessageHash(name);};
    try{
        if(is("MarketHideOwn")){state.hideOwn=!state.hideOwn;state.listOffset=0;return true;}
        if(is("MarketHideOtherOffers")){state.hideOtherOffers=!state.hideOtherOffers;state.listOffset=0;return true;}
        if(is("MarketSaveName")){QueueSocial(state,{{"type","profile_name"},{"name",state.profileName},{"expected_name",state.snapshot.activity.name}});Notice(state,"Name update queued. Your player ID and history stay the same.");return true;}
        if(is("MarketBackOffers")){state.offerBoard=false;return true;}
        if(is("MarketOffersFirst")||is("MarketOffersNext")||is("MarketOffersRefresh")){
            if(!state.offerRequest.empty())return true;
            if(is("MarketOffersNext")&&state.offerPage.contains("next_before")&&!state.offerPage["next_before"].is_null()){const auto next=state.offerPage["next_before"].get<int64_t>();state.offerBefore.push_back(next);RequestOffers(state,next);}
            else{state.offerBefore.clear();RequestOffers(state);}return true;
        }
        for(int n=0;n<4;++n)if(is("MarketBid"+std::to_string(n))){
            if(!state.offerPage.contains("rows")||n>=int(state.offerPage["rows"].size()))return true;
            const auto& bid=state.offerPage["rows"][n];
            if(bid.contains("conversation")){
                const auto thread=ReadThread(bid["conversation"],false);auto& threads=state.snapshot.activity.threads;
                auto at=std::find_if(threads.begin(),threads.end(),[&](const auto& t){return t.id==thread.id;});if(at==threads.end())threads.push_back(thread);else *at=thread;
                ++state.activityGeneration;state.section=Section::Agreements;state.activityType=0;state.offerBoard=false;
                const auto& merged=Threads(state);state.marketSelection=int(std::find_if(merged.begin(),merged.end(),[&](const auto& t){return t.id==thread.id;})-merged.begin());
                state.ownSelection=-1;state.threadOffset=0;state.historyOpen=state.receiptOpen=false;++state.filterRevision;
            }else{OpenReceipt(state,bid.value("alias",std::string("Anonymous offer")),"Exact proposed payment | "+bid.value("status",std::string()),ReadPrice(bid.at("price")));state.receiptMode=1;}return true;
        }
        if(state.section==Section::Browse&&state.marketSelection>=0&&state.marketSelection<int(state.snapshot.feed.listings.size())){
            const auto listing=state.snapshot.feed.listings[state.marketSelection];
            if(is("MarketFavoriteTrade")||is("MarketFavoriteSeller")){
                const std::string kind=is("MarketFavoriteTrade")?"listing":"seller",target=kind=="listing"?listing.id:listing.sellerId;
                if(listing.sellerId.empty())throw std::runtime_error("Favorites are available after this trade syncs to the service.");
                QueueSocial(state,{{"type","favorite"},{"kind",kind},{"target",target},{"enabled",!Favorite(state,kind,target)}});Notice(state,"Favorite update queued.");return true;
            }
            if(is("MarketSellerTrades")){state.sellerFilter=listing.sellerId;state.sellerSearch=listing.seller;++state.sellerRevision;state.marketSelection=-1;state.listOffset=0;state.hideOwn=false;state.favoriteFilter=0;++state.filterRevision;return true;}
            if(is("MarketViewOffers")){if(listing.sellerId.empty())throw std::runtime_error("Sync this listing before viewing its offers.");state.offerListing=listing.id;state.offerBoard=true;state.offerBefore.clear();RequestOffers(state);return true;}
        }
    }catch(const std::exception& error){Notice(state,error.what());return true;}
    return false;
}
inline void PresentSocial(State& state,Panel::NativeView& result){
    auto show=[&](const std::string& name,bool visible){result.widgets.push_back({name,{},visible});};
    auto text=[&](const std::string& name,const std::string& value){result.widgets.push_back({name,value.empty()?" ":value,true});};
    const bool browse=state.section==Section::Browse,detail=browse&&state.marketSelection>=0&&!state.editing&&!state.receiptOpen&&!state.historyOpen;
    show("MarketSocialFilters",browse);show("MarketBrowseSocial",detail&&!state.offerBoard);
    text("MarketHideOwnButtonLabel",state.hideOwn?"Hide my trades: On":"Hide my trades: Off");text("MarketHideOtherOffersButtonLabel",state.hideOtherOffers?"No competing offers: On":"No competing offers: Off");
    result.widgets.push_back({"MarketSellerSearch",state.sellerSearch,true,true,state.sellerRevision});
    result.choices.push_back({"MarketFavorites",state.favoriteFilter,state.filterRevision});
    if(state.section==Section::Setup){result.widgets.push_back({"MarketProfileName",state.profileName,true,true,state.profileRevision});text("MarketProfileHint","3-15 characters. Your player ID stays the same.");}
    if(detail&&state.marketSelection<int(state.snapshot.feed.listings.size())){const auto& l=state.snapshot.feed.listings[state.marketSelection];
        text("MarketFavoriteTradeButtonLabel",Favorite(state,"listing",l.id)?"Saved trade":"Save trade");text("MarketFavoriteSellerButtonLabel",Favorite(state,"seller",l.sellerId)?"Favorite seller":"Save seller");
        text("MarketViewOffersButtonLabel",std::to_string(l.activeOfferCount)+" active offers");}
    const bool board=state.offerBoard&&browse&&!state.editing&&!state.receiptOpen;
    show("MarketOfferBoard",board);if(!board)return;
    show("MarketItemDetail",false);show("MarketHistoryPanel",false);
    const bool loaded=state.offerPage.contains("rows");text("MarketBoardCount",!state.offerRequest.empty()?"Loading offers...":loaded?std::to_string(state.offerPage.value("total",0))+" proposals | anonymous to other traders":"Could not load offers. Refresh to try again.");
    show("MarketOffersNextButton",loaded&&!state.offerPage.value("next_before",Json()).is_null());show("MarketOffersFirstButton",!state.offerBefore.empty());
    for(int n=0;n<4;++n){const bool visible=loaded&&n<int(state.offerPage["rows"].size());const auto name="MarketBid"+std::to_string(n);show(name+"Card",visible);if(!visible)continue;
        const auto& row=state.offerPage["rows"][n];const auto price=ReadPrice(row.at("price"));const auto& first=price.front();
        text(name+"Name",row.value("alias",std::string("Anonymous bidder")));text(name+"State",ShortActivity(ActivityKind(row.value("status",std::string()))+(row.value("from",std::string())=="seller"?" | Seller counter":" | Buyer offer"),30));
        text(name+"Price",WrapActivity(ShortActivity(ActivityTerms(price),84),44));text(name+"ButtonLabel",row.contains("conversation")?"Review offer":"Full price");
        const auto art=first.kind=="gold"?ItemVisuals::Visual{"items/custom/gold_m",1,1}:state.visuals.Resolve(first.code,first.quality,first.index);
        result.images.push_back({name+"Image",art.filename,72,72,bool(art)});
    }
}
