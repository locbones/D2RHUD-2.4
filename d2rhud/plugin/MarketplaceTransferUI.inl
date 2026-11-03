// Native JSON widgets only. Recipient lookup is read-only; the second click
// confirms a stable player ID rather than a potentially ambiguous typed name.
inline void OpenTransfer(State& s,const SavedItem* item,const Json& balance=Json()){
    s.transferOpen=true;s.transferReview=false;s.transferItem=item?*item:SavedItem{};s.transferBalance=balance;
    s.transferTo.clear();s.transferRecipient.clear();s.transferName.clear();s.transferNote.clear();s.transferResolve.clear();s.transferPending.clear();
    s.transferAmount=balance.is_null()?"":std::to_string((std::min)(int64_t(MaxResourceQuantity),balance.at("quantity").get<int64_t>()));++s.transferRevision;
}
inline void PollTransfers(State& s){
    if(!s.transferResolve.empty()&&s.snapshot.activity.results.contains(s.transferResolve)){
        const auto r=s.snapshot.activity.results.at(s.transferResolve);s.transferResolve.clear();
        if(r.contains("error"))Notice(s,r.at("error").get<std::string>());
        else if(s.transferOpen){s.transferRecipient=r.at("recipient_id").get<std::string>();s.transferName=r.at("recipient_name").get<std::string>();s.transferReview=true;}
    }
    if(!s.transferPending.empty()&&s.snapshot.activity.results.contains(s.transferPending)){
        const auto r=s.snapshot.activity.results.at(s.transferPending);s.transferPending.clear();
        if(r.contains("error"))Notice(s,r.at("error").get<std::string>());
        else{s.transferOpen=false;Refresh(s);Notice(s,"Gift sent. The recipient has been notified.");}
    }
}
inline bool TransferCommand(State& s,uint64_t command,const std::map<std::string,Panel::NativeInput>& inputs){
    auto is=[&](const std::string& n){return command==Panel::MessageHash(n);};
    if(is("MarketGiftItem")){if(s.section==Section::Items&&s.ownSelection>=0&&s.ownSelection<int(OwnItems(s).size())){const auto& item=OwnItems(s)[s.ownSelection];if(!item.escrowId.empty()&&item.escrowState!="claiming")OpenTransfer(s,&item);}return true;}
    for(int n=0;n<4;++n)if(is("MarketWalletGift"+std::to_string(n))){const auto rows=WalletRows(s);if(s.walletOpen&&s.walletMode==0&&s.walletOffset+n<int(rows.size()))OpenTransfer(s,nullptr,rows[s.walletOffset+n]);return true;}
    if(!s.transferOpen)return false;
    if(is("MarketGiftBack")){if(!s.transferPending.empty())return true;if(s.transferReview){s.transferReview=false;++s.transferRevision;}else{s.transferOpen=false;s.transferResolve.clear();}return true;}
    if(!is("MarketGiftSubmit"))return false;
    try{
        if(!s.transferPending.empty()||!s.transferResolve.empty())return true;
        if(!s.transferReview){
            s.transferTo=MenuInput(inputs,"MarketGiftRecipient",s.transferRevision,s.transferTo);s.transferNote=MenuInput(inputs,"MarketGiftNote",s.transferRevision,s.transferNote);
            if(s.transferTo.empty()||s.transferTo.size()>64||s.transferNote.size()>2048)throw std::runtime_error("Enter an exact linked player name and a short optional note.");
            if(!s.transferBalance.is_null()){int amount=0;s.transferAmount=MenuInput(inputs,"MarketGiftAmount",s.transferRevision,s.transferAmount);if(!MenuNumber(s.transferAmount,amount,MaxResourceQuantity)||amount>s.transferBalance.at("quantity").get<int64_t>())throw std::runtime_error("Choose an amount within your available balance.");}
            s.transferResolve=QueueSocial(s,{{"type","gift_recipient"},{"to",s.transferTo}});Notice(s,"Verifying the gift recipient...");return true;
        }
        Json packet={{"to",s.transferRecipient},{"message",s.transferNote}};
        if(s.transferBalance.is_null()){packet["type"]="gift_item";packet["item_id"]=s.transferItem.escrowId;}
        else{const auto kind=s.transferBalance.at("kind").get<std::string>();Json r={{"kind",kind},{"code",kind=="gold"?"gld":"Z01"},{"label",WalletLabel(s.transferBalance)},{"quality",2},{"index",-1},{"ethereal",-1},{"quantity",std::stoi(s.transferAmount)}};if(kind=="bag")r["stat"]=s.transferBalance.at("stat");packet["type"]="gift_balances";packet["balances"]=Json::array({r});}
        s.transferPending=QueueSocial(s,std::move(packet));Notice(s,"Gift queued. Waiting for transfer confirmation.");
    }catch(const std::exception& e){Notice(s,e.what());}return true;
}
inline void PresentTransfer(State& s,Panel::NativeView& result,const LootFilterConfig::Builder::Catalog& catalog){
    auto show=[&](const std::string& n,bool v){result.widgets.push_back({n,{},v});};auto text=[&](const std::string& n,const std::string& v){result.widgets.push_back({n,v.empty()?" ":v,true});};
    show("MarketGiftGroup",s.transferOpen);if(!s.transferOpen)return;
    show("MarketListGroup",false);show("MarketSetupGroup",false);show("MarketWalletGroup",false);
    const bool balance=!s.transferBalance.is_null();text("MarketGiftAsset",balance?WalletLabel(s.transferBalance)+" | "+std::to_string(s.transferBalance.at("quantity").get<int64_t>())+" available":Name(s.transferItem,catalog));
    text("MarketGiftHelp",s.transferReview?"Review the recipient carefully. Sending transfers ownership immediately.":"Choose an exact linked player name. The recipient can be offline.");
    show("MarketGiftRecipientField",!s.transferReview);show("MarketGiftRecipientTitle",!s.transferReview);show("MarketGiftNoteField",!s.transferReview);show("MarketGiftNoteTitle",!s.transferReview);
    show("MarketGiftAmountField",balance&&!s.transferReview);show("MarketGiftAmountTitle",balance&&!s.transferReview);
    for(const auto& [n,v]:std::array<std::pair<const char*,std::string>,3>{{{"MarketGiftRecipient",s.transferTo},{"MarketGiftNote",s.transferNote},{"MarketGiftAmount",s.transferAmount}}})result.widgets.push_back({n,v,true,true,s.transferRevision});
    text("MarketGiftReview",s.transferReview?"TO: "+s.transferName+"\n\n"+(balance?s.transferAmount+" x "+WalletLabel(s.transferBalance):Name(s.transferItem,catalog))+"\n\n"+WrapActivity(s.transferNote,80):"");show("MarketGiftReview",s.transferReview);
    text("MarketGiftSubmitButtonLabel",!s.transferPending.empty()?"Sending...":!s.transferResolve.empty()?"Verifying...":s.transferReview?"Send gift":"Review recipient");
    text("MarketGiftBackButtonLabel",s.transferReview?"Edit gift":"Back");
}
