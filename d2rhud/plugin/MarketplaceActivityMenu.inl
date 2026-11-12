// Included inside Marketplace::UI after MenuBundle.
inline std::string ActivityDate(int64_t at){if(at<=0)return "Pending";const time_t time=time_t(at);tm date{};gmtime_s(&date,&time);char text[32]{};strftime(text,sizeof(text),"%Y-%m-%d %H:%M UTC",&date);return text;}
inline std::string ShortActivity(std::string text,size_t n=120){for(char& c:text)if(c=='\n'||c=='\r')c=' ';if(text.size()>n){while(n&&(uint8_t(text[n])&0xc0)==0x80)--n;text.resize(n);text+="...";}return text;}
inline std::string WrapActivity(const std::string& value,size_t maximum=40){
    // Native word wrap need not split long tokens. Keep the full message inside
    // its rectangle even for links or uninterrupted text, at UTF-8 boundaries.
    std::string out;size_t columns=0,lastSpace=std::string::npos,afterSpace=0;
    for(size_t i=0;i<value.size();){const auto c=uint8_t(value[i]);size_t bytes=c<0x80?1:c<0xe0?2:c<0xf0?3:4;bytes=(std::min)(bytes,value.size()-i);
        if(c=='\n'){out+='\n';columns=afterSpace=0;lastSpace=std::string::npos;++i;continue;}
        if(columns>=maximum){if(lastSpace!=std::string::npos){out[lastSpace]='\n';columns=afterSpace;}else{out+='\n';columns=0;}lastSpace=std::string::npos;afterSpace=0;}
        if(c==' '){lastSpace=out.size();afterSpace=0;}else ++afterSpace;
        out.append(value,i,bytes);++columns;i+=bytes;
    }return out;
}
// TextBoxWidget does not clip its text: bound summaries before native rendering.
// Full prices/messages stay available in the paginated reader.
inline std::string CompactActivity(const std::string& value,size_t columns,size_t lines){
    auto wrapped=WrapActivity(value,columns);size_t start=0;
    for(size_t line=1;line<=lines;++line){const auto end=wrapped.find('\n',start);if(end==std::string::npos)return wrapped;
        if(line==lines){size_t cut=start,count=0;while(cut<end&&count+3<columns){const auto c=uint8_t(wrapped[cut]);cut+=(std::min)(size_t(c<0x80?1:c<0xe0?2:c<0xf0?3:4),end-cut);++count;}return wrapped.substr(0,cut)+"...";}start=end+1;
    }return wrapped;
}
inline std::vector<std::string> ActivityPages(const std::string& value){
    // Bound both line length and count: a native TextBox can draw outside its
    // rectangle. Page the complete UTF-8 message instead of relying on clipping.
    const auto wrapped=WrapActivity(value,64);std::vector<std::string> pages;std::string page;size_t start=0,lines=0;
    for(size_t i=0;i<=wrapped.size();++i)if(i==wrapped.size()||wrapped[i]=='\n'){
        if(lines)page+='\n';page.append(wrapped,start,i-start);start=i+1;
        if(++lines==14){pages.push_back(std::move(page));page.clear();lines=0;}
    }
    if(lines||pages.empty())pages.push_back(std::move(page));return pages;
}
inline std::string ActivityNumber(int64_t value){auto text=std::to_string(value);for(int i=int(text.size())-3;i>0;i-=3)text.insert(i,",");return text;}
inline std::string ActivityTerms(const std::vector<Requirement>& price){
    if(IsOpen(price))return "Open to Offers | seller approval required";
    if(IsFree(price))return "Free | no payment required";if(price.empty())return "No payment set";std::string out;
    for(size_t i=0;i<(std::min)(price.size(),size_t(2));++i){if(i)out+=" + ";out+=ActivityNumber(price[i].quantity)+" x "+(price[i].kind=="gold"?"Gold":TradeRequirementName(price[i]));}
    if(price.size()>2)out+=" + "+std::to_string(price.size()-2)+" more";return ShortActivity(out,110);
}
inline std::string ActivityKind(const std::string& kind){
    if(kind=="gifted")return "Gift addressed";
    if(kind=="completed"||kind=="completed_sale")return "Trade completed";if(kind=="accept"||kind=="accepted")return "Agreed";if(kind=="reject"||kind=="rejected")return "Declined";if(kind=="withdraw"||kind=="withdrawn")return "Withdrawn";
    if(kind=="proposed")return "Made an offer";if(kind=="counter")return "Counter-offer";if(kind=="message")return "Message";if(kind=="pending")return "Awaiting a reply";
    if(kind.starts_with("queued_"))return "Sending...";return kind;
}
inline const SavedItem* ActivityItem(State& state){
    if(state.section==Section::Items&&state.ownSelection>=0&&state.ownSelection<int(OwnItems(state).size()))return &OwnItems(state)[state.ownSelection];
    if(state.section==Section::Browse&&state.marketSelection>=0&&state.marketSelection<int(state.snapshot.feed.listings.size()))return &state.snapshot.feed.listings[state.marketSelection].item;
    if(state.section==Section::Agreements&&state.activityType==0){const auto& threads=Threads(state);if(state.marketSelection>=0&&state.marketSelection<int(threads.size())){
        const auto& t=threads[state.marketSelection];try{if(state.threadItemHex!=t.itemHex){state.threadItem=ReadItem(Unhex(t.itemHex),state.tables,1);state.threadItemHex=t.itemHex;}return &state.threadItem;}catch(...){return nullptr;}}}return nullptr;
}
inline std::string ActivityLedgerId(State& state){
    if(state.section==Section::Items){const auto* item=ActivityItem(state);if(item&&!item->escrowId.empty())return item->escrowId;if(item&&!item->ledgerId.empty())return item->ledgerId;}if(state.section==Section::Browse&&state.marketSelection>=0&&state.marketSelection<int(state.snapshot.feed.listings.size()))return state.snapshot.feed.listings[state.marketSelection].itemId;
    if(state.section==Section::Items)if(const auto item=ActivityItem(state))for(const auto& l:state.snapshot.feed.listings)if(l.own&&l.item.tab==item->tab&&l.item.itemHex==item->itemHex&&!l.itemId.empty())return l.itemId;
    if(state.section==Section::Agreements&&state.activityType==0&&state.marketSelection>=0&&state.marketSelection<int(Threads(state).size()))return Threads(state)[state.marketSelection].itemId;return {};}
inline void OpenReceipt(State& state,std::string title,std::string description,const std::vector<Requirement>& price){state.receiptOpen=true;state.receiptMode=state.receiptPage=0;++state.filterRevision;state.receiptTitle=std::move(title);state.receiptDescription=std::move(description);state.receiptPrice=price;}
inline bool ActivityCommand(State& state,uint64_t command){
    auto is=[&](const std::string& value){return command==Panel::MessageHash(value);};
    if(is("MarketBackReceipt")){state.receiptOpen=false;return true;}
    if(state.receiptOpen&&is("MarketReceiptPrev")){state.receiptPage=(std::max)(0,state.receiptPage-1);return true;}
    if(state.receiptOpen&&is("MarketReceiptNext")){if(state.receiptPage+1<int(ActivityPages(state.receiptMode?MenuBundle(state.receiptPrice):state.receiptDescription).size()))++state.receiptPage;return true;}
    if(is("MarketBackHistory")){state.historyOpen=false;return true;}
    const auto item=ActivityItem(state);
    if(is("MarketHistory")||is("MarketThreadHistory")){if(item){state.historyOpen=true;state.historyOffset=0;state.receiptOpen=false;}return true;}
    if(state.historyOpen&&item){
        const auto rows=ItemSales(state.snapshot.activity,*item,state.historyKind==1,ActivityLedgerId(state));const int page=state.historyKind?2:3;
        if(is("MarketHistoryPrev")){state.historyOffset=(std::max)(0,state.historyOffset-page);return true;}
        if(is("MarketHistoryNext")){if(state.historyOffset+page<int(rows.size()))state.historyOffset+=page;return true;}
        for(int slot=0;slot<page;++slot)if(is((state.historyKind?"MarketSimilarSale":"MarketSale")+std::to_string(slot))){const auto at=state.historyOffset+slot;if(at<int(rows.size())){
            const auto& s=*rows[at];OpenReceipt(state,"Completed sale | "+ActivityDate(s.at),s.seller+" -> "+s.buyer+"\nHistory ID "+s.itemId+(s.uuid.empty()?(s.seed?"\nSaved seed "+std::to_string(s.seed):"\nNo native seed stored"):"\nSaved UUID "+s.uuid)+"\nReceipt "+s.receipt,s.price);}return true;}
    }
    if(state.section==Section::Agreements&&state.activityType==0&&state.marketSelection>=0&&state.marketSelection<int(Threads(state).size())){
        const auto thread=Threads(state)[state.marketSelection];
        if(is("MarketReviewOffer")){OpenReceipt(state,"Payment details",thread.buyerName+" -> "+thread.sellerName,thread.price);state.receiptMode=1;return true;}
        if(is("MarketSendMessage")){ThreadAction(state,thread,"message");return true;}
        if(is("MarketLogPrev")){if(state.threadOffset+3<int(thread.events.size()))state.threadOffset+=3;return true;}
        if(is("MarketLogNext")){state.threadOffset=(std::max)(0,state.threadOffset-3);return true;}
        for(int n=0;n<3;++n)if(is("MarketLog"+std::to_string(n))){const auto at=int(thread.events.size())-1-state.threadOffset-n;if(at>=0){const auto& e=thread.events[at];OpenReceipt(state,ActivityKind(e.kind)+" | "+e.actor,ActivityDate(e.at)+"\n\n"+e.message,e.price);}return true;}
        int action=is("MarketAcceptOffer")?0:is("MarketRejectOffer")?1:is("MarketWithdrawOffer")?2:is("MarketCounterOffer")?3:is("MarketApplyOfferAction")?state.offerAction:-1;
        if(action>=0){
            if(action==3){
                const auto self=state.snapshot.activity.self.empty()?"local-player":state.snapshot.activity.self;
                if(!item||thread.status!="pending"||(!thread.simulated&&(thread.proposer==self||PendingCommand(state,thread.id)))){Notice(state,"Only the recipient of a pending, synced proposal can counter it.");return true;}
                state.selected=*item;state.price=thread.price;state.replyTo=thread.id;state.replyRevision=thread.revision;state.editing=state.counterOffer=true;state.adding=state.historyOpen=false;state.priceOffset=0;++state.priceRevision;
            }else ThreadAction(state,thread,std::array<const char*,3>{"accept","reject","withdraw"}[(std::clamp)(action,0,2)]);return true;
        }
    }return false;
}
inline void PresentActivity(State& state,Panel::NativeView& result){
    auto show=[&](const std::string& name,bool visible){result.widgets.push_back({name,{},visible});};
    auto text=[&](const std::string& name,const std::string& value){result.widgets.push_back({name,value.empty()?" ":value,true});};
    const bool conversation=state.section==Section::Agreements&&state.activityType==0&&state.marketSelection>=0&&state.marketSelection<int(Threads(state).size());
    const auto item=ActivityItem(state);const bool history=state.historyOpen&&item&&!state.editing&&!state.receiptOpen;
    show("MarketHistoryPanel",history);show("MarketThreadPanel",conversation&&!state.historyOpen&&!state.receiptOpen&&!state.editing);show("MarketReceiptPanel",state.receiptOpen&&!state.editing);
    show("MarketWithdrawForMoveButton",state.withdrawalConfirm);show("MarketKeepListingButton",state.withdrawalConfirm);
    if(state.withdrawalConfirm){show("MarketReceiptMode",false);show("MarketBackReceiptButton",false);show("MarketReceiptPrevButton",false);show("MarketReceiptNextButton",false);text("MarketReceiptTitle",state.receiptTitle);text("MarketReceiptDescription",state.receiptDescription);text("MarketReceiptPage","");for(int n=0;n<16;++n)show("MarketReceiptTerm"+std::to_string(n),false);return;}
    if(state.receiptOpen&&!state.editing){text("MarketReceiptTitle",state.receiptTitle);
        const auto pages=ActivityPages(state.receiptMode?MenuBundle(state.receiptPrice):state.receiptDescription);state.receiptPage=(std::clamp)(state.receiptPage,0,int(pages.size())-1);
        if(!state.receiptMode)text("MarketReceiptDescription",pages[state.receiptPage]);else show("MarketReceiptDescription",false);
        show("MarketReceiptPrevButton",state.receiptPage>0);show("MarketReceiptNextButton",state.receiptPage+1<int(pages.size()));
        text("MarketReceiptPage",pages.size()>1?"Page "+std::to_string(state.receiptPage+1)+" / "+std::to_string(pages.size()):"");
        for(int n=0;n<16;++n){if(state.receiptMode&&n==0)text("MarketReceiptTerm0",pages[state.receiptPage]);else show("MarketReceiptTerm"+std::to_string(n),false);}return;}
    if(history){
        const bool similar=state.historyKind==1;const auto rows=ItemSales(state.snapshot.activity,*item,similar,ActivityLedgerId(state));const int page=similar?2:3;
        if(state.historyOffset>=int(rows.size()))state.historyOffset=rows.empty()?0:int((rows.size()-1)/page)*page;
        show("MarketHistoryExact",!similar);show("MarketHistorySimilar",similar);show("MarketHistoryEmpty",rows.empty()&&!similar);
        text("MarketHistorySummary",!state.snapshot.activity.error.empty()?ShortActivity(state.snapshot.activity.error):!similar&&ActivityLedgerId(state).empty()&&SeedAmbiguous(state.snapshot.activity,*item)?"Identity collision: choose a registered ledger instance before linking its history.":
            std::to_string(rows.size())+(similar?" similar sales":" completed sales"));
        show("MarketHistoryPrevButton",state.historyOffset>0);show("MarketHistoryNextButton",state.historyOffset+page<int(rows.size()));
        for(int n=0;n<page;++n){const auto name=(similar?"MarketSimilarSale":"MarketSale")+std::to_string(n);const auto at=state.historyOffset+n;show(name,at<int(rows.size()));if(at<int(rows.size())){
            const auto& s=*rows[at];text(name+"Text",ActivityDate(s.at).substr(0,10)+" | "+ShortActivity(s.seller+" -> "+s.buyer,50)+"\n"+ShortActivity(MenuBundle(s.price),50)+" | full terms");}}
        if(similar){
            std::vector<const Sale*> graph;for(auto it=rows.rbegin();it!=rows.rend();++it)if((*it)->price.size()==1&&ChartCurrency((*it)->price[0],state.chartCurrency))graph.push_back(*it);
            if(graph.size()>16)graph.erase(graph.begin(),graph.end()-16);int maximum=1;for(const auto* s:graph)maximum=(std::max)(maximum,s->price[0].quantity);
            text("MarketChartSummary",graph.empty()?"No sales in this currency yet":std::to_string(graph.size())+" sales");
            text("MarketChartMax",maximum>=1000000?std::to_string(maximum/1000000)+"M":std::to_string(maximum));
            text("MarketChartFirst",graph.empty()?"":ActivityDate(graph.front()->at));text("MarketChartLast",graph.empty()?"":ActivityDate(graph.back()->at));
            for(int n=0;n<16;++n){show("MarketChartBar"+std::to_string(n),n<int(graph.size()));if(n>=int(graph.size()))continue;
                const auto span=graph.back()->at-graph.front()->at;const double fraction=span?double(graph[n]->at-graph.front()->at)/span:graph.size()>1?double(n)/(graph.size()-1):.5;
                const int height=(std::max)(2,int(195.0*graph[n]->price[0].quantity/maximum));result.rectangles.push_back({"MarketChartBar"+std::to_string(n),205+int(1815*fraction),1108-height,16,height});}
        }
    }
    if(conversation&&!history&&!state.editing){const auto& t=Threads(state)[state.marketSelection];
        const auto self=state.snapshot.activity.self.empty()?"local-player":state.snapshot.activity.self;const bool mine=t.proposer==self;
        const bool pending=PendingCommand(state,t.id);
        text("MarketThreadState",pending?"Sending your response...":t.status=="pending"?(mine?"Waiting for their reply":"Your turn to respond"):t.settlementLabel.empty()?ActivityKind(t.status):t.settlementLabel);
        text("MarketThreadTerms",t.price.empty()?"No payment set":CompactActivity(ActivityNumber(t.price[0].quantity)+" x "+(t.price[0].kind=="gold"?"Gold":TradeRequirementName(t.price[0])),50,1)+(t.price.size()>1?" + "+std::to_string(t.price.size()-1)+" more":""));
        const auto& payment=t.price.empty()?Requirement{}:t.price[0];
        const auto art=payment.kind=="gold"?ItemVisuals::Visual{"items/custom/gold_m",1,1}:state.visuals.Resolve(payment.code,payment.quality,payment.index);
        result.images.push_back({"MarketThreadPaymentImage",art.filename,74,70,bool(art)});
        if(state.threadOffset>=int(t.events.size()))state.threadOffset=0;
        for(int n=0;n<3;++n){const int at=int(t.events.size())-1-state.threadOffset-n;const auto name="MarketLog"+std::to_string(n);show(name,at>=0);if(at>=0){const auto& e=t.events[at];
            text(name+"Meta",CompactActivity(e.actor+" | "+ActivityKind(e.kind),60,1));text(name+"Time",ActivityDate(e.at));
            text(name+"Text",CompactActivity(e.message.empty()?ActivityTerms(e.price):e.message,78,1));}}
        show("MarketLogPrevButton",state.threadOffset+3<int(t.events.size()));show("MarketLogNextButton",state.threadOffset>0);
        text("MarketLogPage",t.events.empty()?"No messages yet":std::to_string((std::max)(1,int(t.events.size())-state.threadOffset-2))+" - "+std::to_string(int(t.events.size())-state.threadOffset)+" of "+std::to_string(t.events.size()));
        result.widgets.push_back({"MarketOfferMessage",state.message,true,true,state.messageRevision});
        show("MarketSendMessageButton",!pending);const bool decide=!pending&&(t.status=="pending"||(t.local&&t.status=="draft"));show("MarketOfferAction",decide);show("MarketApplyOfferActionButton",decide);
        const bool recipient=t.simulated||!mine;
        show("MarketAcceptOfferButton",decide&&recipient&&t.status=="pending");show("MarketRejectOfferButton",decide&&recipient&&t.status=="pending");
        show("MarketCounterOfferButton",decide&&recipient&&t.status=="pending");show("MarketWithdrawOfferButton",decide&&(t.simulated||mine));
        text("MarketMessageTitle",pending?"MESSAGE SENDING...":"YOUR MESSAGE");
        text("MarketSendMessageButtonLabel",t.simulated?"Log test message":t.local&&t.status=="draft"?"Save message":"Send message");
    }
}
