// Included inside Marketplace::UI. A native mouse action is held before it
// reaches the game. Confirmation withdraws the listing; no inventory action
// or coordinate click is replayed. The player can then move the item normally.
inline void UpdateWithdrawalPrompt(State& state,const Panel::Frame& view,const LootFilterConfig::Builder::Catalog& catalog){
    if(state.original!=state.withdrawalWatchVersion&&!state.tables.items.empty()){
        state.withdrawalWatches.clear();
        for(const auto& draft:state.settings.drafts)try{
            const auto item=ReadItem(Unhex(draft.itemHex),state.tables,draft.tab);
            state.withdrawalWatches.push_back({draft.tab,draft.itemHex,LegacyItemHex(item.itemHex,item.children)});
        }catch(...){}
        state.withdrawalWatchVersion=state.original;
    }
    Panel::WatchWithdrawals(view.owner,unsigned(state.settings.page),view.hardcore,state.valid&&state.settings.enabled?state.withdrawalWatches:std::vector<Panel::WithdrawalWatch>{});
    const auto request=Panel::TakeWithdrawal();if(!request.sequence)return;
    if(!state.valid||request.context.owner!=view.owner||request.context.page!=view.page||request.context.hardcore!=view.hardcore||view.cursorOccupied)return;
    const auto draft=std::find_if(state.settings.drafts.begin(),state.settings.drafts.end(),[&](const Draft& d){return d.tab==request.context.tab&&d.itemHex==request.savedHex;});
    if(draft==state.settings.drafts.end())return;
    try{
        state.withdrawalItem=ReadItem(Unhex(draft->itemHex),state.tables,draft->tab);
        state.open=state.receiptOpen=state.withdrawalConfirm=true;state.editing=state.adding=state.historyOpen=false;state.ownSelection=state.marketSelection=-1;
        state.refresh=true;
        state.receiptMode=state.receiptPage=0;state.receiptPrice.clear();state.receiptTitle="WITHDRAW BEFORE MOVING?";
        state.receiptDescription=Name(state.withdrawalItem,catalog)+"\n\nThis item is listed for trade.\n\nWithdraw its asking price and cancel linked pending offers?\n\nAfter withdrawing, you can move the item normally.\nCompleted sales remain in its history.";
        ++state.filterRevision;
    }catch(const std::exception& e){Notice(state,e.what());}
}
inline void WithdrawBeforeMove(State& state){
    if(!state.withdrawalConfirm)return;
    const auto item=state.withdrawalItem;const auto before=state.settings;bool saved=false;
    try{
        const auto view=Panel::Current();if(view.owner!=state.context.owner||view.page!=unsigned(state.settings.page)||view.hardcore!=state.hardcore||view.cursorOccupied)throw std::runtime_error("The stash changed. Review the item again.");
        std::set<std::string> listings,items,offers;
        for(const auto& l:state.snapshot.feed.listings)if(l.own&&!l.escrow&&l.item.tab==item.tab&&SavedIdentity(l.item.itemHex)==SavedIdentity(item.itemHex)){listings.insert(l.id);if(!l.itemId.empty())items.insert(l.itemId);}
        for(const auto& t:state.local.threads)if(listings.contains(t.listing)||(ValidItemHex(t.itemHex)&&SavedIdentity(t.itemHex)==SavedIdentity(item.itemHex)))offers.insert(t.id);
        std::erase_if(state.settings.drafts,[&](const Draft& d){return d.tab==item.tab&&d.itemHex==item.itemHex;});
        std::erase_if(state.settings.agreements,[&](const AgreementDraft& a){return listings.contains(a.listing);});
        Save(state,modName);saved=true;
        auto local=state.local;
        for(auto it=state.local.commands.begin();it!=state.local.commands.end();){
            if(listings.contains(it->value("listing_id",std::string()))||items.contains(it->value("item_id",std::string()))||offers.contains(it->value("offer_id",std::string())))it=state.local.commands.erase(it);else ++it;
        }
        for(auto& t:state.local.threads)if(offers.contains(t.id)&&(t.status=="pending"||t.status=="draft"||t.status=="accepted")){t.status="withdrawn";t.events.push_back({UnixNow(),"withdrawn","You","Item withdrawn before moving.",{}});}
        for(const auto& id:items)state.local.commands.push_back({{"id",ActivityId()},{"type","withdraw_physical"},{"item_id",id},{"item_hex",item.itemHex}});
        if(items.empty())state.local.commands.push_back({{"id",ActivityId()},{"type","withdraw_physical"},{"item_hex",item.itemHex},{"tab",item.tab},{"page",state.settings.page}});
        try{SaveLocal(state);}catch(...){state.local=std::move(local);RefreshListings(state);throw;}
        RefreshListings(state);Close(state);state.withdrawalWatchVersion.clear();Notice(state,"Listing withdrawn. You can now move the item.");
    }catch(const std::exception& e){if(!saved)state.settings=before;Notice(state,e.what());}
}
