// Native wallet widgets, using the same private, idempotent command outbox.
inline std::string WalletLabel(const Json& row){const auto kind=row.value("kind",std::string());if(kind=="gold")return "Gold";const auto stat=row.value("stat",std::string());for(const auto& r:BagResources)if(stat==r.stat)return r.label;return stat;}
inline Json WalletRows(const State& state){
    if(state.walletMode==0)return state.snapshot.feed.walletBalances;
    return state.snapshot.feed.wallet.value(state.walletMode==1?"withdrawals":"entries",Json::array());
}
inline void ResetWalletForm(State& state){state.walletForm=state.walletReview=false;state.walletCancel=Json();state.walletAsset=Json();}
inline bool WalletCommand(State& state,uint64_t command,const std::map<std::string,Panel::NativeInput>& inputs){
    auto is=[&](const std::string& name){return command==Panel::MessageHash(name);};
    if(is("MarketWallet")){state.walletOpen=true;state.ownSelection=-1;state.walletOffset=0;state.walletMode=0;ResetWalletForm(state);++state.walletRevision;return true;}
    if(!state.walletOpen)return false;
    try{
        if(is("MarketWalletBack")){if(state.walletForm)ResetWalletForm(state);else state.walletOpen=false;return true;}
        if(is("MarketWalletPrev")){state.walletOffset=(std::max)(0,state.walletOffset-4);return true;}
        if(is("MarketWalletNext")){if(state.walletOffset+4<int(WalletRows(state).size()))state.walletOffset+=4;return true;}
        for(int n=0;n<4;++n)if(is("MarketWalletAction"+std::to_string(n))){const auto rows=WalletRows(state);const int i=state.walletOffset+n;if(i>=int(rows.size()))return true;
            ResetWalletForm(state);const auto& row=rows[i];
            if(state.walletMode==0){state.walletAsset=row;state.walletAmount=std::to_string((std::min)(int64_t(2000000000),row.at("quantity").get<int64_t>()));}
            else if(state.walletMode==1&&row.value("status",std::string())=="queued"){state.walletCancel=row;state.walletReview=true;}
            else return true;state.walletForm=true;++state.walletRevision;return true;
        }
        if(is("MarketWalletSubmit")&&state.walletForm){
            if(!state.walletPending.empty())throw std::runtime_error("Wait for the previous Vault request to sync.");
            if(!state.walletCancel.is_null()){
                state.walletPending=QueueSocial(state,{{"type","wallet_cancel"},{"withdrawal_id",state.walletCancel.at("id")},{"expected_revision",state.walletCancel.at("revision")}});
            }else{
                int amount=0;state.walletAmount=MenuInput(inputs,"MarketWalletAmount",state.walletRevision,state.walletAmount);
                if(!MenuNumber(state.walletAmount,amount,2000000000)||int64_t(amount)>state.walletAsset.at("quantity").get<int64_t>())throw std::runtime_error("Enter a whole amount within your available balance (up to 2000000000 per request).");
                if(!state.walletReview){state.walletReview=true;return true;}
                const auto kind=state.walletAsset.at("kind").get<std::string>();Json row={{"kind",kind},{"code",kind=="gold"?"gld":"Z01"},{"label",WalletLabel(state.walletAsset)},{"quality",2},{"index",-1},{"ethereal",-1},{"quantity",amount}};
                if(kind=="bag")row["stat"]=state.walletAsset.at("stat");
                state.walletPending=QueueSocial(state,{{"type","wallet_withdraw"},{"balances",Json::array({row})}});
            }
            ResetWalletForm(state);state.walletMode=1;state.walletOffset=0;++state.walletRevision;Notice(state,"Vault withdrawal queued. The stash refreshes automatically when idle.");return true;
        }
    }catch(const std::exception& e){Notice(state,e.what());return true;}
    return false;
}
inline void PresentWallet(State& state,Panel::NativeView& result){
    auto show=[&](const std::string& n,bool v){result.widgets.push_back({n,{},v});};
    auto text=[&](const std::string& n,const std::string& v){result.widgets.push_back({n,v.empty()?" ":v,true});};
    show("MarketWalletGroup",state.walletOpen);if(!state.walletOpen)return;
    if(!state.walletPending.empty()&&state.snapshot.activity.results.contains(state.walletPending))state.walletPending.clear();
    show("MarketListGroup",false);show("MarketSetupGroup",false);show("MarketWalletRows",!state.walletForm);show("MarketWalletForm",state.walletForm);
    text("MarketWalletBackButtonLabel",state.walletForm?"Back to Vault":"Items");
    text("MarketWalletTitle",state.walletMode==0?"VAULT | GOLD & RESOURCES":state.walletMode==1?"VAULT | WITHDRAWALS":"VAULT | ACTIVITY");
    text("MarketWalletHelp","Funds stay available until spent or withdrawn. The market page refreshes automatically when the stash is idle. Bag currencies arrive in a new, mergeable Storage Bag.");
    show("MarketWalletMode",false);result.choices.push_back({"MarketWalletMode",state.walletMode,state.walletRevision});
    const auto rows=WalletRows(state);state.walletOffset=rows.empty()?0:(std::min)(state.walletOffset,int((rows.size()-1)/4)*4);
    text("MarketWalletPage","Page "+std::to_string(state.walletOffset/4+1)+" / "+std::to_string((std::max)(size_t(1),(rows.size()+3)/4)));
    show("MarketWalletPrevButton",!state.walletForm&&state.walletOffset>0);show("MarketWalletNextButton",!state.walletForm&&state.walletOffset+4<int(rows.size()));show("MarketWalletPage",!state.walletForm);
    for(int n=0;n<4;++n){const int i=state.walletOffset+n;const std::string name="MarketWalletRow"+std::to_string(n);const bool visible=i<int(rows.size());show(name,visible);if(!visible)continue;
        const auto& row=rows[i];std::string label,detail;bool action=false;
        if(state.walletMode==0){label=WalletLabel(row);detail=std::to_string(row.at("quantity").get<int64_t>())+" available to trade";action=true;}
        else if(state.walletMode==1){const auto status=row.at("status").get<std::string>();label=status=="queued"?"Waiting for stash sync":status=="delivering"?"Delivering":status=="completed"?"Delivered":"Cancelled";
            detail=ActivityTerms(ReadPrice(row.at("remaining").empty()?row.at("requested"):row.at("remaining")));if(!row.value("message",std::string()).empty())detail+=" | "+row.at("message").get<std::string>();action=status=="queued";}
        else{const auto amount=row.at("amount").get<int64_t>();label=WalletLabel(row)+" "+(amount>0?"+":"")+std::to_string(amount);const auto actionName=row.at("action").get<std::string>();detail=(actionName=="gift_sent"?"Gift sent":actionName=="gift_received"?"Gift received":actionName)+" | "+std::to_string(row.at("available").get<int64_t>())+" available after";}
        show("MarketWalletGift"+std::to_string(n)+"Button",state.walletMode==0&&action);text(name+"Title",CompactActivity(label,38,1));text(name+"Detail",CompactActivity(detail,46,2));show("MarketWalletAction"+std::to_string(n)+"Button",action);text("MarketWalletAction"+std::to_string(n)+"ButtonLabel",state.walletMode==0?"Withdraw":"Cancel remainder");
    }
    text("MarketWalletEmpty",rows.empty()?"Nothing here yet.":"");show("MarketWalletEmpty",rows.empty()&&!state.walletForm);
    if(state.walletForm){const bool cancel=!state.walletCancel.is_null();text("MarketWalletFormTitle",cancel?"CANCEL REMAINING WITHDRAWAL":(state.walletReview?"CONFIRM WITHDRAWAL":"CHOOSE AN AMOUNT"));
        text("MarketWalletAsset",cancel?WrapActivity(ActivityTerms(ReadPrice(state.walletCancel.at("remaining"))),82):WalletLabel(state.walletAsset)+" | "+std::to_string(state.walletAsset.at("quantity").get<int64_t>())+" available");
        show("MarketWalletAmountField",!cancel&&!state.walletReview);show("MarketWalletAmountTitle",!cancel&&!state.walletReview);
        result.widgets.push_back({"MarketWalletAmount",state.walletAmount,true,true,state.walletRevision});
        text("MarketWalletReviewAmount",!cancel&&state.walletReview?state.walletAmount+" x "+WalletLabel(state.walletAsset):"");show("MarketWalletReviewAmount",!cancel&&state.walletReview);
        text("MarketWalletFormHelp",cancel?"Only the undelivered remainder returns to your trading balance. Amounts already delivered stay in your game.":"Requested funds are reserved immediately. The market page refreshes automatically when the stash is idle. Any amount that does not fit stays queued and can be cancelled.");
        text("MarketWalletSubmitButtonLabel",cancel?"Cancel remainder":state.walletReview?"Queue withdrawal":"Review amount");
    }
}
