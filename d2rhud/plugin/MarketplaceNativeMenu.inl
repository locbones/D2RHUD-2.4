// Included inside Marketplace::UI. All presentation is performed by native
// Button/TextBox/InputTextBox/Widget controls declared in the stash JSON.
inline bool MenuNumber(const std::string& text,int& value,int maximum=MaxQuantity){
    if(text.empty()||text.size()>10)return false;uint64_t n=0;
    for(unsigned char c:text){if(c<'0'||c>'9')return false;n=n*10+c-'0';if(n>uint64_t(maximum))return false;}
    if(!n)return false;value=int(n);return true;
}
inline std::string MenuInput(const std::map<std::string,Panel::NativeInput>& inputs,const std::string& name,uint64_t revision,const std::string& fallback){
    const auto at=inputs.find(name);return at!=inputs.end()&&at->second.revision==revision?at->second.text:fallback;
}
inline std::string ActivityNumber(int64_t value);
inline std::string MenuBundle(const std::vector<Requirement>& price){
    if(IsOpen(price))return "Open to Offers | seller approval required";
    if(IsFree(price))return "Free | no payment required";
    std::string text;for(const auto& r:price){if(!text.empty())text+='\n';text+=ActivityNumber(r.quantity)+" x "+TradeRequirementName(r);if(r.ethereal>=0)text+=r.ethereal?" (ethereal)":" (non-ethereal)";}return text;
}
inline bool SocialMatches(const State&,const Listing&);
inline std::vector<int> MenuRows(State& state,const LootFilterConfig::Builder::Catalog& catalog){
    std::vector<int> rows;const bool own=state.section==Section::Items;
    if(state.section==Section::Agreements){const auto count=state.activityType==0?Threads(state).size():state.settings.agreements.size();for(size_t n=0;n<count;++n)rows.push_back(int(n));return rows;}
    const auto payment=Spendable(state.snapshot.deposit,state.settings,state.snapshot.goldValid?state.snapshot.depositGold:0,&state.snapshot.feed);
    const auto count=own?OwnItems(state).size():state.snapshot.feed.listings.size();
    auto item=[&](int i)->const SavedItem&{return own?OwnItems(state)[i]:state.snapshot.feed.listings[i].item;};
    auto price=[&](int i){if(!own)return state.snapshot.feed.listings[i].price;if(auto d=OwnPrice(state,item(i)))return d->price;return std::vector<Requirement>{};};
    for(size_t n=0;n<count;++n){const auto i=int(n);const auto& value=item(i);const auto p=price(i);
        if(own?(!FilterQuality(value,state.quality)||!TradeMatches(value,"",p,state.search,0,0,catalog)):
            !TradeMatches(value,state.snapshot.feed.listings[i].seller,p,state.search,state.quality,state.searchScope,catalog))continue;
        if(!own&&!SocialMatches(state,state.snapshot.feed.listings[i]))continue;
        if(own&&((state.tabFilter&&value.tab!=unsigned(state.tabFilter))||(state.pricedOnly&&p.empty())))continue;
        if(!own&&state.affordableOnly&&(state.snapshot.feed.listings[i].own||!ListingFresh(state.snapshot.feed,state.snapshot.feed.listings[i],UnixNow())||!state.settings.enabled||(!state.snapshot.depositValid&&state.snapshot.feed.escrowFunds.empty())||!CanPay(p,payment)))continue;
        rows.push_back(i);
    }
    std::stable_sort(rows.begin(),rows.end(),[&](int a,int b){
        if((state.sort==1||state.sort==5)&&item(a).quality!=item(b).quality)return state.sort==5?item(a).quality<item(b).quality:item(a).quality>item(b).quality;
        if(state.sort==2||state.sort==6){if(own&&item(a).tab!=item(b).tab)return state.sort==6?item(a).tab>item(b).tab:item(a).tab<item(b).tab;if(!own){const auto sa=HudOptions::Lower(state.snapshot.feed.listings[a].seller),sb=HudOptions::Lower(state.snapshot.feed.listings[b].seller);if(sa!=sb)return state.sort==6?sa>sb:sa<sb;}}
        if(state.sort==3&&price(a).size()!=price(b).size())return price(a).size()<price(b).size();const auto na=HudOptions::Lower(Name(item(a),catalog)),nb=HudOptions::Lower(Name(item(b),catalog));return state.sort==4?na>nb:na<nb;
    });return rows;
}
inline std::vector<Requirement> MenuPayments(const State& state,const LootFilterConfig::Builder::Catalog& catalog){
    if(state.paymentKind==2)return {{"gld","Gold (market page)",2,-1,1,-1,"gold"}};
    std::vector<Requirement> rows;const auto q=HudOptions::Lower(state.query);
    auto match=[&](const std::string& name,const std::string& code){return HudOptions::Lower(name+" "+code).find(q)!=std::string::npos;};
    if(state.paymentKind==1){for(const auto& resource:BagResources)if(state.tables.bagStats.contains(resource.stat)&&match(resource.label,resource.stat))
        rows.push_back({"Z01",resource.label,2,-1,1,-1,"bag",resource.stat});return rows;}
    if(state.query.empty())return rows;
    for(const auto& named:catalog.namedItems){const auto quality=named.quality=="Unique"?7:5;const auto label=CodexName(modName,Code(named.code),quality,named.label);if(match(label,named.code))rows.push_back({Code(named.code),label,quality,named.index,1,-1});}
    for(const auto& base:catalog.items){const auto label=CodexName(modName,Code(base.value),2,base.label);if(base.value!="gld"&&match(label,base.value))rows.push_back({Code(base.value),label,2,-1,1,-1});}return rows;
}
inline bool MenuQuantities(State& state,const std::map<std::string,Panel::NativeInput>& inputs){
    if(IsFree(state.price)||IsOpen(state.price))return true;
    bool valid=true;for(int n=0;n<4;++n){const auto i=state.priceOffset+n;if(i>=int(state.price.size()))break;
        int quantity=0;const auto text=MenuInput(inputs,"Quantity"+std::to_string(n),state.priceRevision,std::to_string(state.price[i].quantity));
        if(MenuNumber(text,quantity,QuantityLimit(state.price[i])))state.price[i].quantity=quantity;else valid=false;
    }return valid;
}
#include "MarketplaceActivityMenu.inl"
#include "MarketplaceSocialUI.inl"
#include "MarketplaceWalletUI.inl"
#include "MarketplaceTransferUI.inl"
#include "MarketplaceCampaignUI.inl"
inline void NativeMenu(State& state,const Panel::Frame& view,const LootFilterConfig::Builder::Catalog& catalog){
    UpdateOwnInventory(state);
    PollSocial(state);PollTransfers(state);PollCampaigns(state);
    const auto inputs=Panel::ReadNativeInputs();
    const auto choices=Panel::ReadNativeChoices();
    if(state.open&&state.editing&&!state.counterOffer)state.listingComment=MenuInput(inputs,"MarketListingComment",state.priceRevision,state.listingComment);
    if(state.section==Section::Items){
        const auto at=choices.find("MarketVaultView");
        if(at!=choices.end()&&at->second.revision==state.walletRevision&&at->second.selection>=0&&at->second.selection<4){
            const int view=at->second.selection,current=state.walletOpen?state.walletMode+1:0;
            if(view!=current){state.walletOpen=view>0;state.walletMode=(std::max)(0,view-1);state.walletOffset=0;state.ownSelection=-1;state.filtersOpen=false;ResetWalletForm(state);++state.walletRevision;}
        }
    }
    if(state.walletOpen){const auto at=choices.find("MarketWalletMode");if(at!=choices.end()&&at->second.revision==state.walletRevision&&at->second.selection>=0&&at->second.selection<3&&state.walletMode!=at->second.selection){state.walletMode=at->second.selection;state.walletOffset=0;ResetWalletForm(state);}}
    if(state.open&&state.editing&&!state.adding)for(int slot=0;slot<4;++slot){
        const auto i=state.priceOffset+slot;const auto at=choices.find("MarketTorchClass"+std::to_string(slot));
        if(i<int(state.price.size())&&ClassChoice(state.price[i])&&at!=choices.end()&&at->second.revision==state.priceRevision&&at->second.selection>=0&&at->second.selection<8)state.price[i].classId=at->second.selection-1;
    }
    if(state.open){for(const auto& [name,value]:choices)if(value.revision==state.filterRevision){
        if(name=="MarketActivityType"&&state.section==Section::Agreements&&value.selection>=0&&value.selection<2&&state.activityType!=value.selection){state.activityType=value.selection;state.marketSelection=-1;state.listOffset=0;state.historyOpen=state.receiptOpen=state.editing=state.counterOffer=false;}
        if(name=="MarketHistoryKind"&&state.historyOpen&&value.selection>=0&&value.selection<2&&state.historyKind!=value.selection){state.historyKind=value.selection;state.historyOffset=0;}
        if(name=="MarketChartCurrency"&&value.selection>=0&&value.selection<7)state.chartCurrency=value.selection;
        if(name=="MarketOfferAction"&&value.selection>=0&&value.selection<4)state.offerAction=value.selection;
        if(name=="MarketReceiptMode"&&value.selection>=0&&value.selection<2&&state.receiptMode!=value.selection){state.receiptMode=value.selection;state.receiptPage=0;}
        if(name=="MarketPaymentKind"&&state.editing&&state.adding&&value.selection>=0&&value.selection<3&&state.paymentKind!=value.selection){
            state.paymentKind=value.selection;state.query.clear();state.paymentOffset=0;++state.paymentRevision;}
        int* target=nullptr;int count=0;
        if(name=="MarketFavorites"){target=&state.favoriteFilter;count=3;}if(name=="MarketQuality"){target=&state.quality;count=6;}if(name=="MarketSearchScope"){target=&state.searchScope;count=3;}if(name=="MarketSort"){target=&state.sort;count=7;}if(name=="MarketTab"){target=&state.tabFilter;count=8;}
        if(target&&value.selection>=0&&value.selection<count&&*target!=value.selection){*target=value.selection;state.listOffset=0;}
    }}
    if(state.open){
        const auto seller=MenuInput(inputs,"MarketSellerSearch",state.sellerRevision,state.sellerSearch);if(seller!=state.sellerSearch){state.sellerSearch=seller;state.sellerFilter.clear();state.listOffset=0;}
        if(state.walletOpen&&state.walletForm&&!state.walletReview)state.walletAmount=MenuInput(inputs,"MarketWalletAmount",state.walletRevision,state.walletAmount);
        state.profileName=MenuInput(inputs,"MarketProfileName",state.profileRevision,state.profileName);
        const auto search=MenuInput(inputs,"MarketSearch",state.searchRevision,state.search);if(search!=state.search){state.search=search;state.listOffset=0;}
        const auto query=MenuInput(inputs,"PaymentSearch",state.paymentRevision,state.query);if(query!=state.query){state.query=query;state.paymentOffset=0;}
        state.message=MenuInput(inputs,"MarketOfferMessage",state.messageRevision,state.message);
        if(state.editing)MenuQuantities(state,inputs);
        if(state.section==Section::Setup){int n=0;if(MenuNumber(MenuInput(inputs,"DepositPage",state.routeRevision,std::to_string(state.depositChoice)),n,64))state.depositChoice=n;
            if(MenuNumber(MenuInput(inputs,"WithdrawPage",state.routeRevision,std::to_string(state.withdrawChoice)),n,64))state.withdrawChoice=n;}
    }
    bool priceSaveFailed=false;
    for(const auto command:Panel::TakeNativeActions()){
        auto is=[&](const std::string& name){return command==Panel::MessageHash(name);};
        if(state.withdrawalConfirm){if(is("MarketWithdrawForMove"))WithdrawBeforeMove(state);else if(is("MarketKeepListing")||is("MarketClose")||is("MarketBackReceipt"))Close(state);continue;}
        if(is("MarketOpen")){try{Load(state,modName);state.open=true;state.section=Section::Browse;state.listOffset=0;SetupChoices(state);Refresh(state);}catch(const std::exception& e){Notice(state,e.what());}continue;}
        if(!state.open)continue;if(is("MarketClose")){if(!priceSaveFailed)Close(state);continue;}
        if(is("MarketFilterOpen")){state.filtersOpen=true;continue;}
        if(is("MarketFilterBack")||is("MarketFilterApply")){state.filtersOpen=false;continue;}
        if(is("MarketFilterReset")){state.quality=state.sort=state.searchScope=state.tabFilter=state.favoriteFilter=0;state.pricedOnly=state.affordableOnly=state.hideOwn=state.hideOtherOffers=false;state.search.clear();++state.searchRevision;state.sellerSearch.clear();state.sellerFilter.clear();++state.sellerRevision;++state.filterRevision;state.listOffset=0;continue;}
        if(is("MarketTermsPrev")){state.termsOffset=(std::max)(0,state.termsOffset-4);continue;}
        if(is("MarketTermsNext")){state.termsOffset+=4;continue;}
        if(is("MarketFullPrice")){const auto* item=ActivityItem(state);std::vector<Requirement> terms;if(item&&state.section==Section::Items){if(const auto d=OwnPrice(state,*item))terms=d->price;}else if(state.section==Section::Browse&&state.marketSelection>=0&&state.marketSelection<int(state.snapshot.feed.listings.size()))terms=state.snapshot.feed.listings[state.marketSelection].price;else if(state.section==Section::Agreements&&state.marketSelection>=0&&state.marketSelection<int(state.settings.agreements.size()))terms=state.settings.agreements[state.marketSelection].price;if(!terms.empty())OpenReceipt(state,"Full asking price",MenuBundle(terms),terms);continue;}
        if(is("MarketBackList")){state.offerBoard=false;state.ownSelection=state.marketSelection=-1;state.editing=state.adding=state.counterOffer=state.historyOpen=state.receiptOpen=false;state.replyTo.clear();continue;}
        bool tab=false;for(int n=0;n<4;++n)if(is(std::array<const char*,4>{"MarketBrowse","MarketItems","MarketAgreements","MarketSetup"}[n])){state.section=Section(n);state.filtersOpen=false;state.termsOffset=0;state.campaignOpen=false;state.transferOpen=false;state.transferResolve.clear();state.walletOpen=false;state.offerBoard=false;state.editing=state.adding=state.counterOffer=state.historyOpen=state.receiptOpen=false;state.replyTo.clear();state.ownSelection=state.marketSelection=-1;state.search.clear();++state.searchRevision;state.listOffset=0;if(n==3){SetupChoices(state);state.statusUntil=0;}if(n==2){state.activityType=Threads(state).empty()?1:0;++state.filterRevision;}tab=true;}
        if(tab)continue;
        if(CampaignCommand(state,command))continue;
        if(TransferCommand(state,command,inputs))continue;
        if(WalletCommand(state,command,inputs))continue;
        if(SocialCommand(state,command))continue;
        if(ActivityCommand(state,command))continue;
        if(is("MarketRefresh")){try{Load(state,modName);SetupChoices(state);Refresh(state);state.listOffset=0;}catch(const std::exception& e){Notice(state,e.what());}continue;}
        if(is("MarketSync")){Notice(state,LiveCustody::window.RequestSync(GetTickCount64())?"Sync requested. Leave the stash idle with an empty cursor; pending deliveries will retry safely.":"Save sync is unavailable. Keep Pandemonium running and open the stash to reconnect.");continue;}
        if(is("MarketReload")){try{Load(state,modName);SetupChoices(state);Refresh(state);Notice(state,"Changes discarded.");}catch(const std::exception& e){Notice(state,e.what());}continue;}
        if(is("MarketEnabled"))state.enabledChoice=!state.enabledChoice;
        if(is("MarketAuto"))state.autoChoice=!state.autoChoice;
        if(is("MarketDepositCurrent")){state.depositChoice=int(view.page);++state.routeRevision;}
        if(is("MarketWithdrawCurrent")){state.withdrawChoice=int(view.page);++state.routeRevision;}
        if(is("MarketSaveSetup")){
            int deposit=0;const bool valid=MenuNumber(MenuInput(inputs,"DepositPage",state.routeRevision,std::to_string(state.depositChoice)),deposit,64);const int withdraw=deposit;
            if(!state.valid||!valid){Notice(state,"Choose a market page from 1 to 64.");continue;}
            const auto before=state.settings;state.settings.page=deposit;state.settings.withdrawPage=withdraw;state.settings.enabled=state.enabledChoice;state.settings.autoTrade=state.autoChoice;state.settings.simulation.enabled=false;
            if(deposit!=before.page)state.settings.drafts.clear();
            try{Save(state,modName);SetupChoices(state);if(deposit!=before.page||before.simulation.enabled)Refresh(state);RefreshListings(state);Notice(state,"Saved.");}catch(const std::exception& e){state.settings=before;Notice(state,e.what());}continue;
        }
        if(is("MarketSort")){state.sort=(state.sort+1)%7;++state.filterRevision;state.listOffset=0;}
        if(is("MarketQuality")){state.quality=(state.quality+1)%6;++state.filterRevision;state.listOffset=0;}
        if(is("MarketTab")){state.tabFilter=(state.tabFilter+1)%8;++state.filterRevision;state.listOffset=0;}
        if(is("MarketPriced")){state.pricedOnly=!state.pricedOnly;state.listOffset=0;}
        if(is("MarketAffordable")){state.affordableOnly=!state.affordableOnly;state.listOffset=0;}
        auto rows=MenuRows(state,catalog);
        if(is("MarketPrev"))state.listOffset=(std::max)(0,state.listOffset-6);
        if(is("MarketNext")&&state.listOffset+6<int(rows.size()))state.listOffset+=6;
        for(int slot=0;slot<6;++slot)if(is("MarketCard"+std::to_string(slot))&&state.listOffset+slot<int(rows.size())){
            state.filtersOpen=false;state.termsOffset=0;const auto index=rows[state.listOffset+slot];if(state.section==Section::Items)state.ownSelection=index;else state.marketSelection=index;state.editing=state.adding=state.counterOffer=state.historyOpen=state.receiptOpen=false;state.threadOffset=0;state.message.clear();++state.messageRevision;
            if(state.section==Section::Agreements&&state.activityType==0){const auto& t=Threads(state)[index];const auto self=state.snapshot.activity.self.empty()?"local-player":state.snapshot.activity.self;state.offerAction=t.proposer==self?2:0;++state.filterRevision;}
        }
        if(state.section==Section::Items&&state.ownSelection>=0&&state.ownSelection<int(OwnItems(state).size())){
            if(is("MarketReturn")){state.selected=OwnItems(state)[state.ownSelection];if(!state.selected.escrowId.empty()&&state.selected.escrowState!="claiming")WritePrice(state,true);}
            if(is("MarketPrice")&&state.valid&&OwnItems(state)[state.ownSelection].escrowState!="claiming"){state.selected=OwnItems(state)[state.ownSelection];state.priceSaveError.clear();state.price.clear();state.listingComment.clear();if(auto draft=OwnPrice(state,state.selected)){state.price=draft->price;state.listingComment=draft->comment;}state.editing=true;state.adding=false;state.priceOffset=0;++state.priceRevision;}
        }
        if(is("MarketCounter")&&state.section==Section::Browse&&state.marketSelection>=0&&state.marketSelection<int(state.snapshot.feed.listings.size())){
            const auto& listing=state.snapshot.feed.listings[state.marketSelection];
            if(!listing.own&&ListingFresh(state.snapshot.feed,listing,UnixNow())&&(!listing.simulated||state.settings.simulation.enabled)){state.counterTarget=listing;state.selected=listing.item;state.price=IsOpen(listing.price)?std::vector<Requirement>{}:listing.price;state.editing=state.counterOffer=true;state.adding=state.historyOpen=state.receiptOpen=false;state.replyTo.clear();state.message.clear();++state.messageRevision;state.priceOffset=0;++state.priceRevision;}
        }
            if(is("MarketCancelPrice")){state.editing=state.adding=state.counterOffer=false;state.replyTo.clear();}
            if(state.editing){
                if(!state.counterOffer&&is("MarketFreePrice")){state.price=FreePrice();state.adding=false;state.priceOffset=0;++state.priceRevision;}
                if(is("MarketSavePrice")){if(MenuQuantities(state,inputs)&&Validate(state.price).empty()&&state.valid){if(state.counterOffer)SubmitCounter(state);else priceSaveFailed=!WritePrice(state,false);}else {priceSaveFailed=true;state.priceSaveError="Enter valid quantities and non-overlapping payment items.";Notice(state,state.priceSaveError);}}
                if(!state.counterOffer&&is("MarketRemovePrice")&&OwnPrice(state,state.selected))WritePrice(state,true);
                if(is("MarketAddPayment")&&state.price.size()<MaxPriceLines){if(IsFree(state.price)||IsOpen(state.price))state.price.clear();state.adding=!state.adding;state.query.clear();state.paymentOffset=0;++state.paymentRevision;}
                if(is("MarketPricePrev")){state.priceOffset=(std::max)(0,state.priceOffset-4);++state.priceRevision;}
                if(is("MarketPriceNext")&&state.priceOffset+4<int(state.price.size())){state.priceOffset+=4;++state.priceRevision;}
                auto options=MenuPayments(state,catalog);
                if(is("MarketPaymentPrev"))state.paymentOffset=(std::max)(0,state.paymentOffset-4);
                if(is("MarketPaymentNext")&&state.paymentOffset+4<int(options.size()))state.paymentOffset+=4;
                for(int slot=0;slot<4;++slot){const auto line=state.priceOffset+slot;
                    if(state.adding&&is("MarketPayment"+std::to_string(slot))&&state.paymentOffset+slot<int(options.size())&&state.price.size()<MaxPriceLines){state.price.push_back(options[state.paymentOffset+slot]);state.adding=false;state.priceOffset=int((state.price.size()-1)/4)*4;++state.priceRevision;}
                    if(!state.adding&&line<int(state.price.size())){
                        if(is("MarketEth"+std::to_string(slot))&&state.price[line].kind=="item")state.price[line].ethereal=state.price[line].ethereal==1?-1:state.price[line].ethereal+1;
                        if(is("MarketRemove"+std::to_string(slot))){state.price.erase(state.price.begin()+line);state.priceOffset=state.price.empty()?0:(std::min)(state.priceOffset,int((state.price.size()-1)/4)*4);++state.priceRevision;}
                    }
                }
            }
        if(is("MarketConfirm")&&state.section==Section::Browse&&state.marketSelection>=0&&state.marketSelection<int(state.snapshot.feed.listings.size())){
            const auto& listing=state.snapshot.feed.listings[state.marketSelection];
            if(listing.own)Notice(state,"This is your local listing. Edit its price in My Items.");
            else if(state.valid&&(state.settings.enabled||IsFree(listing.price))&&(IsFree(listing.price)||state.snapshot.depositValid||!state.snapshot.feed.escrowFunds.empty())&&ListingFresh(state.snapshot.feed,listing,UnixNow())&&CanPay(listing.price,Spendable(state.snapshot.deposit,state.settings,state.snapshot.goldValid?state.snapshot.depositGold:0,&state.snapshot.feed))){
                if(!listing.simulated&&!listing.sellerId.empty()&&!state.snapshot.activity.self.empty()){state.counterTarget=listing;state.price=listing.price;state.replyTo.clear();SubmitCounter(state);}
                else Agree(state,listing);
            }
            else Notice(state,"Refresh the offer and deposit; the complete payment must be available.");
        }
        if(is("MarketConfirm")&&state.section==Section::Agreements&&state.activityType==1&&state.marketSelection>=0&&state.marketSelection<int(state.settings.agreements.size())){
            const auto before=state.settings.agreements;state.settings.agreements.erase(state.settings.agreements.begin()+state.marketSelection);
            try{Save(state,modName);state.marketSelection=-1;Notice(state,"Agreement draft removed.");}catch(const std::exception& e){state.settings.agreements=before;Notice(state,e.what());}
        }
    }
    Panel::NativeView result;result.open=state.open;result.section=int(state.section);if(!result.open){Panel::Present(std::move(result));return;}
    auto show=[&](const std::string& name,bool visible){result.widgets.push_back({name,{},visible});};
    auto text=[&](const std::string& name,std::string value){result.widgets.push_back({name,value.empty()?" ":std::move(value),true});};
    auto input=[&](const std::string& name,const std::string& value,uint64_t revision){result.widgets.push_back({name,value,true,true,revision});};
    auto preview=[&](const std::string& name,const std::string& label,const SavedItem* item){result.previews.push_back({name,label,item?item->itemHex:std::string()});};
    auto image=[&](const std::string& name,const SavedItem* item,int w,int h){const auto art=item?state.visuals.Resolve(item->code,item->quality,item->index,item->picture):ItemVisuals::Visual{};
        result.images.push_back({name,art.filename,w,h,bool(art)});};
    const bool own=state.section==Section::Items,browse=state.section==Section::Browse,agreements=state.section==Section::Agreements,setup=state.section==Section::Setup;
    const bool conversations=agreements&&state.activityType==0;
    show("MarketListGroup",!setup);show("MarketSetupGroup",setup);show("MarketFilters",browse||own);show("MarketOwnFilters",own);show("MarketAffordFilter",browse);
    show("MarketSearchScope",browse);show("MarketSearchScopeTitle",browse);
    show("MarketFilterPanel",state.filtersOpen&&(browse||own));show("MarketWalletButton",false);
    const bool vaultNavigation=own&&!state.editing&&!state.transferOpen&&!state.historyOpen&&state.ownSelection<0&&!state.walletForm&&!state.filtersOpen;
    show("MarketVaultNavigation",vaultNavigation);

    show("MarketFooterDivider",!setup);show("MarketStatusBackground",!setup);show("MarketRefreshButton",!setup);
    show("MarketSyncButton",!setup&&!state.editing);
    show("MarketActivityFilter",agreements);
    const auto now=GetTickCount64();std::string status=now<state.statusUntil?state.status:state.snapshot.feedError;
    if(status.empty())status=state.snapshot.depositError;if(status.empty()&&state.settings.simulation.enabled)status=state.snapshot.sellerError;if(status.empty())status=state.publishError;if(status.empty())status=state.snapshot.activity.error;if(!state.tableError.empty())status=state.tableError;
    if(state.editing&&!state.priceSaveError.empty())status=state.priceSaveError;
    if(status.empty()&&!state.context.syncStatus.empty())status=state.context.syncStatus;
    if(status.empty())status=state.scanning.valid()||state.refresh?"Refreshing...":!state.snapshot.activity.self.empty()?state.snapshot.feed.custodyEnabled?"Vault trades settle once agreed and funded. Your market page refreshes for incoming deliveries when idle.":"Listings sync now; unload the market page to sync its goods.":"Offline | Changes are saved locally.";text("MarketStatus",CompactActivity(status,70,2));show("MarketStatus",!state.editing&&!setup);
    result.choices={{"MarketQuality",state.quality,state.filterRevision},{"MarketSearchScope",state.searchScope,state.filterRevision},{"MarketSort",state.sort,state.filterRevision},{"MarketTab",state.tabFilter,state.filterRevision},{"MarketPaymentKind",state.paymentKind,state.filterRevision},{"MarketActivityType",state.activityType,state.filterRevision},{"MarketHistoryKind",state.historyKind,state.filterRevision},{"MarketChartCurrency",state.chartCurrency,state.filterRevision},{"MarketOfferAction",state.offerAction,state.filterRevision},{"MarketReceiptMode",state.receiptMode,state.filterRevision}};
    text("MarketPricedLabel",state.pricedOnly?"Priced only: On":"Priced only: Off");
    text("MarketAffordableLabel",state.affordableOnly?"Affordable only: On":"Affordable only: Off");input("MarketSearch",state.search,state.searchRevision);
    if(vaultNavigation)result.choices.push_back({"MarketVaultView",state.walletOpen?state.walletMode+1:0,state.walletRevision});
    PresentCampaigns(state,result,catalog);if(state.campaignOpen){Panel::Present(std::move(result));return;}
    PresentTransfer(state,result,catalog);if(state.transferOpen){Panel::Present(std::move(result));return;}
    if(setup){
        input("DepositPage",std::to_string(state.depositChoice),state.routeRevision);
        text("MarketEnabledLabel",state.enabledChoice?"Enabled":"Disabled");text("MarketAutoLabel",state.autoChoice?"Automatic":"Manual");
        text("MarketPolicy",state.autoChoice?"Exact asking price + Vault funds = automatic trade":"Review each offer yourself");
        int deposit=0;const auto depositText=MenuInput(inputs,"DepositPage",state.routeRevision,std::to_string(state.depositChoice));
        const bool routesValid=MenuNumber(depositText,deposit,64);const int withdraw=deposit;
        const bool unlist=routesValid&&deposit!=state.settings.page&&!state.settings.drafts.empty();
        const bool dirty=!routesValid||deposit!=state.settings.page||withdraw!=state.settings.withdrawPage||state.enabledChoice!=state.settings.enabled||state.autoChoice!=state.settings.autoTrade;
        text("MarketRouteNotice",!routesValid?"Enter a page from 1 to 64.":now<state.statusUntil?state.status:unlist?"Changing deposit unlists "+std::to_string(state.settings.drafts.size())+" priced item(s).":"");
        text("MarketSaveSetupButtonLabel",unlist?"Unlist + save":"Save changes");show("MarketReloadButton",dirty);
        PresentSocial(state,result);
    PresentWallet(state,result);Panel::Present(std::move(result));return;
    }
    auto rows=MenuRows(state,catalog);if(state.listOffset>=int(rows.size()))state.listOffset=rows.empty()?0:int((rows.size()-1)/6)*6;
    show("MarketPrevButton",state.listOffset>0);show("MarketNextButton",state.listOffset+6<int(rows.size()));
    text("MarketFilterOpenButtonLabel",(state.quality||state.sort||state.searchScope||state.tabFilter||state.favoriteFilter||state.pricedOnly||state.affordableOnly||state.hideOwn||state.hideOtherOffers||!state.sellerSearch.empty()||!state.sellerFilter.empty())?"Filters *":"Filters");
    text("MarketCollectionHint",own?"SEARCH YOUR ITEMS":"SEARCH ITEMS OR WANTED PAYMENT");
    show("MarketCollectionHint",!own);
    text("MarketCount",std::to_string(rows.size())+(own?" items":conversations?" conversations":agreements?" agreement drafts":" offers")+" | Page "+std::to_string(state.listOffset/6+1));
    text("MarketEmpty",own?(state.ownSeller?"No seller items. Enable Test seller in Setup, load its page, then Refresh.":"No matching deposit items. Load the market page and Refresh."):agreements?"Review an offer in Browse to save its exact terms here.":state.settings.simulation.enabled?"No matching offers. Price seller items in My Items > Simulated seller.":"No matching offers. Price items in My Items, or clear the search / affordability filters.");show("MarketEmpty",rows.empty());show("MarketEmptyArt",rows.empty());
    if(conversations&&rows.empty())text("MarketEmpty","No conversations yet.\nMake an offer from Browse to get started.");
    text("MarketListTitle",own?(state.ownSeller?"SIMULATED SELLER INVENTORY":"YOUR MARKET PAGE"):conversations?"YOUR CONVERSATIONS":agreements?"AGREEMENT DRAFTS":"BROWSE TRADES");
    for(int slot=0;slot<6;++slot){const auto at=state.listOffset+slot;show("MarketCard"+std::to_string(slot),at<int(rows.size()));if(at>=int(rows.size()))continue;const auto i=rows[at];std::string name,detail,tag;const SavedItem* visual=nullptr;
        SavedItem threadVisual;
        if(conversations){const auto& t=Threads(state)[i];name=t.sellerName;const auto self=state.snapshot.activity.self;detail=ActivityTerms(t.price);tag=ShortActivity(t.seller==self?t.buyerName:t.sellerName,24)+" | "+ActivityKind(t.status);try{threadVisual=ReadItem(Unhex(t.itemHex),state.tables,1);visual=&threadVisual;name=Name(threadVisual,catalog);}catch(...){} }
        else if(own){const auto& item=OwnItems(state)[i];visual=&item;name=Name(item,catalog);const auto d=OwnPrice(state,item);detail=d?ActivityTerms(d->price):"Set an asking price";tag=item.escrowId.empty()?"Tab "+std::to_string(item.tab)+(d?" | Listed":" | Not listed"):item.escrowState=="listed"?"In Vault | Listed":item.escrowState=="claiming"?"Delivery in progress":"In Vault | Available";}
        else if(agreements){const auto& a=state.settings.agreements[i];name=a.seller;detail=(a.simulated?"SIMULATION | ":"Local draft | ")+std::to_string(a.price.size())+" payment type(s)";
            const auto match=std::find_if(state.snapshot.feed.listings.begin(),state.snapshot.feed.listings.end(),[&](const auto& l){return l.seller==a.seller&&l.id==a.listing&&SameTerms(a,l);});
            if(match!=state.snapshot.feed.listings.end()){visual=&match->item;name=Name(*visual,catalog);}}
        else{const auto& l=state.snapshot.feed.listings[i];visual=&l.item;name=Name(l.item,catalog);detail=ActivityTerms(l.price);tag=(l.own?"Your trade":ShortActivity(l.seller,24))+" | "+std::to_string(l.activeOfferCount)+" offers";}
        preview("MarketCardName"+std::to_string(slot),CompactActivity(name,32,2),visual);image("MarketCardImage"+std::to_string(slot),visual,128,190);show("MarketInspect"+std::to_string(slot),visual!=nullptr);
        text("MarketCardInfo"+std::to_string(slot),CompactActivity(detail,30,2));text("MarketCardTag"+std::to_string(slot),CompactActivity(tag,40,1));
        show("MarketCardSelected"+std::to_string(slot),i==(own?state.ownSelection:state.marketSelection));
    }
    auto& selection=own?state.ownSelection:state.marketSelection;
    if(own&&state.editing&&!state.counterOffer){
        const auto& inventory=OwnItems(state);
        const auto current=std::find_if(inventory.begin(),inventory.end(),[&](const SavedItem& item){return state.selected.escrowId.empty()?item.escrowId.empty()&&item.tab==state.selected.tab&&item.itemHex==state.selected.itemHex:item.escrowId==state.selected.escrowId;});
        selection=current==inventory.end()?-1:int(current-inventory.begin());
    }
    if(std::find(rows.begin(),rows.end(),selection)==rows.end()){selection=-1;state.editing=state.adding=state.counterOffer=false;}
    const bool focus=selection>=0||state.receiptOpen;show("MarketCollection",!focus&&!state.filtersOpen);show("MarketFocus",focus&&!state.filtersOpen);show("MarketItemHero",!state.receiptOpen);
    text("MarketBackListButtonLabel",conversations?"All conversations":own?"My items":"Back to browse");
    text("MarketDetailTitle",state.editing?(state.adding?"STEP 1 | CHOOSE PAYMENT":state.price.empty()?"SET AN ASKING PRICE":"STEP 2 | QUANTITIES & REVIEW"):conversations?"CONVERSATION":own?"YOUR ITEM":"TRADE DETAILS");
    show("MarketItemDetail",selection>=0&&!state.editing&&!state.historyOpen&&!state.receiptOpen&&!conversations);show("MarketPriceEditor",state.editing);show("MarketPriceLines",state.editing&&!state.adding&&!IsFree(state.price));show("MarketPaymentPicker",state.editing&&state.adding);
    show("MarketInspectSelected",selection>=0&&(!agreements||conversations));show("MarketItemSummary",selection>=0);show("MarketItemFacts",selection>=0);show("MarketItemExtra",selection>=0);
    show("MarketPriceButton",own&&selection>=0&&!state.editing&&state.valid);show("MarketGiftItemButton",own&&selection>=0&&!state.editing&&!OwnItems(state)[selection].escrowId.empty()&&OwnItems(state)[selection].escrowState!="claiming");show("MarketConfirmButton",selection>=0&&!own&&!state.editing&&!conversations&&(!browse||!state.snapshot.feed.listings[selection].own));
    show("MarketReturnButton",own&&selection>=0&&!state.editing&&!OwnItems(state)[selection].escrowId.empty()&&OwnItems(state)[selection].escrowState!="claiming");
    if(own&&selection>=0&&OwnItems(state)[selection].escrowState=="claiming")show("MarketPriceButton",false);
    text("MarketCounterButtonLabel","Counter-offer");show("MarketCounterButton",browse&&selection>=0&&!state.editing&&!state.snapshot.feed.listings[selection].own&&!IsFree(state.snapshot.feed.listings[selection].price));show("MarketDetailBody",!state.editing);
    if(selection>=0){
        std::string summary,bundle,facts,extra;std::vector<Requirement> terms;const SavedItem* visual=nullptr;
        if(conversations){const auto& t=Threads(state)[selection];visual=ActivityItem(state);summary=visual?Name(*visual,catalog):"Conversation item";facts="With "+(t.seller==state.snapshot.activity.self?t.buyerName:t.sellerName);extra=t.development||t.simulated?"Test trade | Click the item to inspect":"Click the item to inspect";terms=t.price;}
        else if(agreements){const auto& a=state.settings.agreements[selection];summary="Seller: "+a.seller;bundle="Agreed payment (all required):";terms=a.price;
            const auto l=std::find_if(state.snapshot.feed.listings.begin(),state.snapshot.feed.listings.end(),[&](const auto& l){return l.seller==a.seller&&l.id==a.listing;});
            facts=l!=state.snapshot.feed.listings.end()&&ListingFresh(state.snapshot.feed,*l,UnixNow())&&SameTerms(a,*l)?(a.simulated?"SIMULATION | Terms match test offer":"Terms match cached offer | local draft"):"Offer missing, changed or expired | review again";text("MarketConfirmLabel","Remove agreement draft");
        }else{
            const auto& item=own?OwnItems(state)[selection]:state.snapshot.feed.listings[selection].item;visual=&item;
            summary=Name(item,catalog);facts=Quality(item.quality)+(item.Payment().ethereal?std::string(" | Ethereal"):std::string());if(item.level)facts+=" | Level "+std::to_string(item.level);if(item.quantity>1)facts+=" | Quantity "+std::to_string(item.quantity);extra="Click the item to inspect";if(!item.children.empty())extra="Includes socketed items | Click to inspect";
            if(own){text("MarketPriceLabel",OwnPrice(state,item)?"Edit asking price":"Set asking price");if(auto d=OwnPrice(state,item)){bundle="ASKING PRICE";terms=d->price;}else bundle=item.escrowId.empty()?"NOT LISTED YET":"AVAILABLE IN VAULT";if(!item.escrowId.empty())extra=item.escrowState=="listed"?"Listed from Vault | Return it to your market page anytime":"Held in your Vault | Available for payment or delivery";else if(!OwnEnabled(state))extra="Deposit disabled | Enable it in Setup to list or gift";}
            else{const auto& l=state.snapshot.feed.listings[selection];bundle=l.own?"YOUR ASKING PRICE":"ASKING PRICE";facts="Seller: "+ShortActivity(l.seller,48);terms=l.price;if(!l.sellerId.empty())extra=l.escrow?"In Vault | Ready for an agreed, funded trade":"Awaiting the seller's stash sync";text("MarketConfirmLabel",IsFree(l.price)?"Claim free item":l.simulated?"Accept test offer":!l.sellerId.empty()?"Propose asking price":"Agree to terms (draft)");}
        }
        preview("MarketItemSummary",CompactActivity(summary,60,2),visual);image("MarketItemImage",visual,146,156);text("MarketItemFacts",CompactActivity(facts,55,1));text("MarketItemExtra",CompactActivity(extra,64,1));text("MarketPaymentBundle",bundle);
        if(state.termsOffset>=int(terms.size()))state.termsOffset=terms.empty()?0:int((terms.size()-1)/4)*4;
        show("MarketTermsPrevButton",state.termsOffset>0);show("MarketTermsNextButton",state.termsOffset+4<int(terms.size()));show("MarketFullPriceButton",terms.size()>4||std::any_of(terms.begin(),terms.end(),[](const auto& r){return MenuBundle({r}).size()>68;}));
        text("MarketTermsPage",terms.size()>4?std::to_string(state.termsOffset+1)+" - "+std::to_string((std::min)(state.termsOffset+4,int(terms.size())))+" of "+std::to_string(terms.size()):terms.size()>1?"All payment types required":"");
        for(int n=0;n<4;++n){const auto at=state.termsOffset+n;show("MarketTermRow"+std::to_string(n),at<int(terms.size()));if(at>=int(terms.size()))continue;
            const auto& r=terms[at];text("MarketTerm"+std::to_string(n),CompactActivity(MenuBundle({r}),68,2));
            const auto art=r.kind=="gold"?ItemVisuals::Visual{"items/custom/gold_m",1,1}:state.visuals.Resolve(r.code,r.quality,r.index);
            result.images.push_back({"MarketTermImage"+std::to_string(n),art.filename,70,70,bool(art)});
        }

    }
    std::string comment;
    if(selection>=0&&!state.editing&&!conversations){if(own){if(const auto draft=OwnPrice(state,OwnItems(state)[selection]))comment=draft->comment;}else if(browse)comment=state.snapshot.feed.listings[selection].comment;}
    show("MarketListingCommentDisplay",!comment.empty()&&!state.historyOpen&&!state.receiptOpen);text("MarketListingCommentText",CompactActivity(comment,110,2));
    if(state.editing){
        bool testCounter=state.counterTarget.simulated;if(!state.replyTo.empty()){const auto& threads=Threads(state);const auto reply=std::find_if(threads.begin(),threads.end(),[&](const auto& t){return t.id==state.replyTo;});testCounter=reply!=threads.end()&&reply->simulated;}
        show("MarketPriceStart",!state.adding&&(state.price.empty()||IsFree(state.price)));show("MarketFreePriceButton",!state.counterOffer&&!state.adding);text("MarketPriceStartTitle",IsFree(state.price)?"FREE | FIRST SUCCESSFUL CLAIM":"WHAT WOULD YOU LIKE IN RETURN?");text("MarketPriceStartHelp",IsFree(state.price)?"No payment or further approval. Save to list for free.":"Add a payment, then choose the amount.");
        input("PaymentSearch",state.query,state.paymentRevision);
        show("MarketListingCommentGroup",!state.counterOffer&&!state.adding);input("MarketListingComment",state.listingComment,state.priceRevision);
        const auto validation=MenuQuantities(state,inputs)?Validate(state.price):"Enter a valid quantity: items up to 1000000; gold/bag units up to 2000000000.";
        show("PaymentSearchField",state.paymentKind!=2);show("MarketPaymentSearchTitle",state.paymentKind!=2);show("MarketGoldHelp",state.paymentKind==2);
        text("MarketEditNotice",!state.priceSaveError.empty()?state.priceSaveError:now<state.statusUntil?state.status:state.adding?"Search by part of a name, then click a result to add it to the price.":state.price.empty()?"Start with Add payment. You can add more than one payment type.":!validation.empty()?validation:state.counterOffer?(testCounter?"SIMULATION: seller accepts its exact asking terms, otherwise counters with that price.":"Review your offer, then submit it to the seller."):state.ownSeller?"SIMULATION: Save asking price lists this item in Browse. No items move.":"Every payment type listed is required.");
        text("MarketSavePriceButtonLabel",IsFree(state.price)?"List for free":state.counterOffer?(testCounter?"Send test offer":"Submit counter-offer"):"Save asking price");
        text("MarketAddPaymentButtonLabel",state.adding?"Back to price":"Add payment");
        show("MarketPricePrevButton",state.priceOffset>0);show("MarketPriceNextButton",state.priceOffset+4<int(state.price.size()));
        show("MarketRemovePriceButton",!state.counterOffer&&!state.adding&&OwnPrice(state,state.selected)!=nullptr);show("MarketSavePriceButton",!state.adding&&!state.price.empty());
        for(int slot=0;slot<4;++slot){const auto i=state.priceOffset+slot;show("MarketPriceLine"+std::to_string(slot),i<int(state.price.size()));if(i>=int(state.price.size())||IsFree(state.price)){show("MarketPriceLine"+std::to_string(slot),false);continue;}
            const auto& r=state.price[i];text("MarketPriceName"+std::to_string(slot),CompactActivity(TradeRequirementName(r),28,2));
            const auto priceArt=r.kind=="gold"?ItemVisuals::Visual{"items/custom/gold_m",1,1}:state.visuals.Resolve(r.code,r.quality,r.index);
            result.images.push_back({"MarketPriceImage"+std::to_string(slot),priceArt.filename,40,36,bool(priceArt)});input("Quantity"+std::to_string(slot),std::to_string(r.quantity),state.priceRevision);
            show("MarketTorchClass"+std::to_string(slot),ClassChoice(r));
            if(ClassChoice(r))result.choices.push_back({"MarketTorchClass"+std::to_string(slot),r.classId+1,state.priceRevision});
            show("MarketEth"+std::to_string(slot),r.kind=="item"&&!ClassChoice(r));if(r.kind!="item")text("MarketUnits"+std::to_string(slot),r.kind=="gold"?"Gold pieces":"Stored bag units");else show("MarketUnits"+std::to_string(slot),false);
            text("MarketEthLabel"+std::to_string(slot),r.ethereal<0?"Any condition":r.ethereal?"Ethereal only":"Non-ethereal");
        }
        auto options=MenuPayments(state,catalog);if(state.paymentOffset>=int(options.size()))state.paymentOffset=0;
        show("MarketPaymentPrevButton",state.paymentOffset>0);show("MarketPaymentNextButton",state.paymentOffset+4<int(options.size()));
        text("MarketPaymentCount",state.paymentKind==2?(state.snapshot.goldValid?"Deposit gold: "+std::to_string(state.snapshot.depositGold)+". Choose Gold below, then enter an amount.":"Gold balance unavailable. Save the market page and Refresh."):
            state.paymentKind==1?(state.tables.bagStats.empty()?"Bag balances are unavailable: matching RMD-MP stat metadata is required.":options.empty()?"No matching bag balance. Try rune, gem, jewel or core.":"Choose a balance below. Amounts use the bag's displayed units."):
            state.query.empty()?"Click the outlined box above. Try Zod, Stone or Ring.":options.empty()?"No matches. Try a shorter name, such as Zod or Ring.":std::to_string(options.size())+" matches. Choose one below, then set its quantity.");
        for(int slot=0;slot<4;++slot){const auto i=state.paymentOffset+slot;show("MarketPayment"+std::to_string(slot),i<int(options.size()));if(i<int(options.size())){text("MarketPaymentLabel"+std::to_string(slot),CompactActivity(options[i].label,68,2));
                const auto& r=options[i];const auto paymentArt=r.kind=="gold"?ItemVisuals::Visual{"items/custom/gold_m",1,1}:state.visuals.Resolve(r.code,r.quality,r.index);
                result.images.push_back({"MarketPaymentImage"+std::to_string(slot),paymentArt.filename,60,66,bool(paymentArt)});}}
    }
    PresentActivity(state,result);
    PresentSocial(state,result);
    PresentWallet(state,result);
    Panel::Present(std::move(result));
}
