// Included after the native marketplace widget helpers. Only native client
// callbacks touch widgets/player pointers. The render tick renews value-only
// handoff records and never calls inventory or page-loading functions.
#include "MarketplacePagePackets.h"
#include "MarketplaceGoldDeposit.h"
static Marketplace::GoldDeposit::Detector gMarketGoldDeposit;
static uint64_t gMarketGoldPoll=0,gMarketGoldRequestPoll=0;
static std::mutex gLivePagePacketsMutex;
static uint32_t gLivePacketPage=UINT32_MAX;
static std::vector<std::vector<uint8_t>> gLivePagePackets;
static bool MarketplaceLivePreparePage(uint32_t page) try {
    std::lock_guard lock(gLivePagePacketsMutex);gLivePacketPage=UINT32_MAX;gLivePagePackets.clear();
    auto path=Marketplace::LiveCustody::window.PageFile(page+1);
    // Preflight every RMD-MP page before freeing the current native stash.
    // This also avoids re-reading the character save to reload shared tabs.
    if(!path&&modName=="RMD-MP"){
        const auto saved=GetSavePath();if(saved.empty())throw std::runtime_error("Save folder is unavailable.");
        path=std::filesystem::path(saved)/L"Diablo II Resurrected"/L"Mods"/std::filesystem::u8path(modName)/
            (std::wstring(L"Stash_")+(IsHardcore()?L"HC":L"SC")+L"_Page"+std::to_wstring(page+1)+L".d2i");
    }
    if(!path)return true;
    std::vector<uint8_t> bytes;
    std::error_code error;const bool exists=std::filesystem::exists(*path,error);if(error)throw std::runtime_error("Cannot read the trade page.");
    if(!exists){for(unsigned tab=0;tab<7;++tab)bytes.insert(bytes.end(),emptyStashTab.begin(),emptyStashTab.end());}
    else{bytes=Marketplace::ReadFile(*path,7*65535);if(bytes!=Marketplace::ReadFile(*path,7*65535))throw std::runtime_error("Trade page changed while preparing its reload.");}
    gLivePagePackets=Marketplace::PagePackets::Build(bytes);gLivePacketPage=page;return true;
}catch(const std::exception& e){Marketplace::LiveCustody::window.Error(e.what());return false;}catch(...){return false;}
static int MarketplaceLiveSendFreshPage(uint32_t page) try {
    std::lock_guard lock(gLivePagePacketsMutex);
    if(gLivePacketPage!=page)return Marketplace::LiveCustody::window.Protected(page+1)?-1:0;
    for(auto& packet:gLivePagePackets)CCMD_QueuePacket(packet.data(),int32_t(packet.size()));
    return 1; // Retain until the final acknowledgement or a prepared rollback.
}catch(...){return -1;}
static void MarketplaceLiveRecoverPage() try {
    if(!gMarketplacePageRecovery.load()||gMarketplaceRollbackAttempted.exchange(true))return;
    const auto previous=gMarketplacePreviousPage.load();
    if(!MarketplaceLivePreparePage(previous)){
        const std::string error="The previous stash page could not be read. Leave the game and retry; existing files were preserved.";
        Marketplace::LiveCustody::window.Error(error);MarketplaceStashNotice(error);return;
    }
    UpdateStashFileName(previous);gSelectedPage=previous;
    const auto fresh=MarketplaceLiveSendFreshPage(previous);
    if(fresh<0){Marketplace::LiveCustody::window.Error("Unable to restore the previous stash page. Existing files were preserved.");return;}
    if(!fresh)reinterpret_cast<void(__fastcall*)()>(Pattern::Address(0x10e080))();
    gMarketplacePageRecovery=false;gMarketplacePagePhase=NewSharedStashUnitsCreated;
    CCMD_SendSharedStashMessage(NewSharedStashLoadedAndAttachMessagesSent,previous);
}catch(...){Marketplace::LiveCustody::window.Error("Unable to restore the stash page. Existing files were preserved.");}
static std::atomic<bool> gLiveStashClosed{false};
static std::atomic<bool> gLivePageKnown{false};
static std::atomic<uint64_t> gLiveConnectPoll{0};
static std::atomic<unsigned> gLiveRestorePage{0};
static std::atomic<unsigned> gLiveRefreshReturn{0},gLiveRefreshTab{0};
static int MarketplaceLiveReloadTab(uint32_t page){
    if(gLiveRefreshReturn.load()!=page+1)return gMarketplacePageTab;
    const auto tab=int(gLiveRefreshTab.load());
    if(!Marketplace::LiveCustody::window.Refreshing())gLiveRefreshReturn=0;
    return tab;
}
static void MarketplaceLiveRefreshTick() try {
    if(gMarketplacePageSwitching.load()||!Marketplace::LiveCustody::window.Refreshing())return;
    if(!Marketplace::LiveCustody::window.RefreshReady(GetTickCount64()))return;
    if(gLiveStashClosed.load()){
        Marketplace::LiveCustody::window.FinishRefresh();Marketplace::Panel::stashSyncing=false;return;
    }
    if(!MarketplaceLiveCursor())OnStashPageChanged(gLiveRefreshReturn-1,false);
}catch(...){}

static void MarketplaceLiveObserve(const char* event,uint32_t requestedPage){
    wchar_t profile[32768]{};const auto size=GetEnvironmentVariableW(L"USERPROFILE",profile,32768);
    if(!size||size>=32768)return;
    try{
        const auto cfg=Marketplace::LiveCustody::Read(std::filesystem::path(profile)/L".d2r-marketplace/custody-enrollment.json");
        const auto root=std::filesystem::path(cfg.at("state_directory").get<std::string>());
        const auto view=Marketplace::LiveCustody::window.Snapshot();
        const auto name=std::string_view(event).starts_with("native-hooks-")?L"live-native-hooks.json":L"live-native-status.json";
        Marketplace::LiveCustody::Write(root/name,{{"schema",1},{"event",event},{"tick",GetTickCount64()},
            {"requested_page",requestedPage},{"pid",GetCurrentProcessId()},{"page",gSelectedPage+1},{"enabled",view.enabled},{"parked",view.parked},{"status",view.status}},false);
    }catch(...){}
}
static bool MarketplaceLiveConfigure() try {
    if(modName!="RMD-MP"||GetClientStatus()!=1)return false;
    auto player=GetClientPlayerUnit();D2UnitStrc value{};
    if(!Marketplace::NativePanel::Read(player,value)||value.dwUnitType||!value.pInventory)return false;
    wchar_t profile[32768]{};const auto size=GetEnvironmentVariableW(L"USERPROFILE",profile,32768);
    if(!size||size>=32768)return false;
    const auto saved=GetSavePath();if(saved.empty())return false;
    const auto folder=std::filesystem::path(saved)/L"Diablo II Resurrected"/L"Mods"/std::filesystem::u8path(modName);
    return Marketplace::LiveCustody::window.Configure(std::filesystem::path(profile)/L".d2r-marketplace/custody-enrollment.json",folder,value.dwUnitId,IsHardcore(),gSelectedPage+1);
}catch(...){return false;}
static bool MarketplaceLiveCursor(){
    return Marketplace::StashCursor::Busy(GetClientPlayerUnit());
}
static bool MarketplaceLiveCanNavigate(bool carry,uint32_t* cursor){
    const auto state=Marketplace::StashCursor::Inspect(GetClientPlayerUnit(),cursor);
    return state==Marketplace::StashCursor::State::Empty||(carry&&!gLiveStashClosed.load()&&
        !Marketplace::Panel::stashSyncing.load()&&state==Marketplace::StashCursor::State::Carried);
}
static void MarketplaceLiveRestoreView(int tab){
    auto panel=WidgetFindChild(*gpPanelManager,"BankExpansionLayout");if(!panel)return;
    if(!Marketplace::NativePanel::SelectBankTab(panel,tab))return;
    Message update{"BankPanelMessage"_hash64,"SelectTab"_hash64};
    BankPanelMessage(panel,update);
}
static bool MarketplaceLiveParkView(){
    auto panel=WidgetFindChild(*gpPanelManager,"BankExpansionLayout");
    if(!panel){gMarketplacePageTab=0;return !MarketplaceLiveCursor();}
    const auto tab=Marketplace::NativePanel::BankTab(panel);if(tab<0)return false;
    if(!Marketplace::NativePanel::SelectBankTab(panel,0))return false;
    gMarketplacePageTab=tab;
    // This changes only the grid's owner/context, never the held item's mode,
    // inventory or identity. No shared-owner pointer remains in the grid.
    Message update{"BankPanelMessage"_hash64,"SelectTab"_hash64};
    BankPanelMessage(panel,update);
    if(Marketplace::NativePanel::PersonalBankGrid(panel,GetClientPlayerUnit()))return true;
    MarketplaceLiveRestoreView(tab);return false;
}
static void MarketplaceLiveClosed() try {
    gMarketGoldDeposit.Reset();gLiveStashClosed=true;gLiveRestorePage=0;Marketplace::Panel::stashSyncing=false;
    gMarketplacePageRequests.Reset();
    Marketplace::LiveCustody::window.RememberClosed(Marketplace::LiveCustody::window.Refreshing()?gLiveRefreshReturn.load():gSelectedPage.load()+1);
    if(gMarketplacePageSwitching.load()||!MarketplaceLiveConfigure()||MarketplaceLiveCursor()){MarketplaceLiveObserve("close-deferred");return;}
    MarketplaceLiveObserve("stash-closed");
    const auto page=Marketplace::LiveCustody::window.Close(gSelectedPage+1,false,!Marketplace::LiveCustody::window.Refreshing());
    if(page)OnStashPageChanged(page-1,false);
    else Marketplace::LiveCustody::window.OpenGrant(gSelectedPage+1);
}catch(...){}
static void MarketplaceLiveContinueClosed() try {
    // OnClose can race a short write or an occupied cursor. Retry from the
    // native event thread even though the bank's draw callback is now inactive.
    if(!gLiveStashClosed.load()||gMarketplacePageSwitching.load()||MarketplaceLiveCursor())return;
    const auto view=Marketplace::LiveCustody::window.Snapshot();if(!view.enabled)return;
    const auto parking=Marketplace::LiveCustody::window.Close(gSelectedPage+1,false,false);
    if(parking)OnStashPageChanged(parking-1,false);
    else Marketplace::LiveCustody::window.OpenGrant(gSelectedPage+1);
}catch(...){}
static void MarketplaceLiveOpened() try {
    gLivePageKnown=true;gLiveConnectPoll=0;
    gMarketGoldDeposit.Reset();gMarketGoldPoll=0;gMarketGoldRequestPoll=0;gLiveStashClosed=false;Marketplace::Panel::stashSyncing=Marketplace::LiveCustody::window.Refreshing();
    // Website changes and route migrations can finish while the panel is
    // closed. Re-read them before restoring a parked grant; otherwise that
    // grant can keep advertising the old market page indefinitely.
    const bool configured=!gMarketplacePageSwitching.load()&&MarketplaceLiveConfigure();
    const auto view=Marketplace::LiveCustody::window.Snapshot();
    if(view.enabled&&(view.parked||gMarketplacePageSwitching.load()))gLiveRestorePage=Marketplace::LiveCustody::window.Restore(true);
    if(!gLiveRestorePage.load()&&!gMarketplacePageSwitching.load()&&configured)
        Marketplace::LiveCustody::window.OpenGrant(gSelectedPage+1,true);
    MarketplaceLiveObserve("stash-opened");
}catch(...){}
static bool MarketplaceLiveAllowPage(uint32_t page) try {
    return Marketplace::LiveCustody::window.Permit(page+1);
}catch(...){return false;}
static void MarketplaceLiveLoaded(uint32_t page) try {
    gLivePageKnown=true;gLiveConnectPoll=0;
    gMarketGoldDeposit.Reset();gMarketGoldPoll=0;gMarketGoldRequestPoll=0;
    if(Marketplace::LiveCustody::window.Refreshing()&&page+1==gLiveRefreshReturn){
        Marketplace::LiveCustody::window.FinishRefresh();Marketplace::Panel::stashSyncing=false;
    }
    {std::lock_guard lock(gLivePagePacketsMutex);gLivePacketPage=UINT32_MAX;gLivePagePackets.clear();}
    // Only the final native load acknowledgement establishes which page is
    // in memory. Refresh routes here too, including changes made in Setup
    // while the stash stayed open. Configure reacquires the write exclusion
    // before revoking an older grant.
    MarketplaceLiveConfigure();
    if(gLiveStashClosed.load()&&!MarketplaceLiveCursor()){
        const auto parking=Marketplace::LiveCustody::window.Close(page+1,false,false);
        if(parking)gMarketplacePageRequests.Request(parking-1);
        else Marketplace::LiveCustody::window.OpenGrant(page+1);
    }
    else Marketplace::LiveCustody::window.OpenGrant(page+1,true);
    MarketplaceLiveObserve("page-loaded");
}catch(...){}
static void MarketplaceLiveConnect(uint64_t now) try {
    // Native event thread only. Enrollment/capability may arrive after OnOpen.
    // Reconcile the handshake without resetting navigation or reloading goods.
    if(!gLivePageKnown.load()||gMarketplacePageSwitching.load()||now<gLiveConnectPoll)return;
    gLiveConnectPoll=now+2000;
    if(MarketplaceLiveCursor())return;
    const auto before=Marketplace::LiveCustody::window.Snapshot();
    if(!MarketplaceLiveConfigure())return;
    const auto after=Marketplace::LiveCustody::window.Snapshot();
    if(!before.enabled&&gLiveStashClosed.load())Marketplace::LiveCustody::window.RememberClosed(gSelectedPage+1);
    if(!gLiveStashClosed.load()&&!after.parked)Marketplace::LiveCustody::window.OpenGrant(gSelectedPage+1,true);
}catch(...){}
static void MarketplaceLiveSelectPage(void* panel) try {
    const auto pages=WidgetFindChild(panel,"BankPages");if(!pages)return;
    const uint32_t selection=gSelectedPage.load();
    const auto shown=Marketplace::NativePanel::Field<uint32_t>(pages,0x178C);
    SIZE_T written=0;
    if(shown==selection||(WriteProcessMemory(GetCurrentProcess(),static_cast<uint8_t*>(pages)+0x178C,&selection,sizeof(selection),&written)&&written==sizeof(selection)))
        gMarketplacePageSelection.Show(selection);
}catch(...){}
static void MarketplaceLivePollPage(void* panel) try {
    if(Marketplace::Panel::stashSyncing.load()){MarketplaceLiveSelectPage(panel);return;}
    const auto pages=WidgetFindChild(panel,"BankPages");if(!pages)return;
    const auto selection=Marketplace::NativePanel::Field<uint32_t>(pages,0x178C);
    if(gMarketplacePageSelection.Changed(selection,gSelectedPage.load())){
        gLiveRestorePage=0;OnStashPageChanged(selection);
    }
    MarketplaceLiveSelectPage(panel);
}catch(...){}
static bool MarketplaceLiveGold(void* panel,uint64_t now){
    const auto view=Marketplace::LiveCustody::window.Snapshot();
    if(!view.enabled||view.deposit!=gSelectedPage+1||gLiveStashClosed.load()||gMarketplacePageSwitching.load()||Marketplace::Panel::stashSyncing.load()){
        gMarketGoldDeposit.Reset();return false;
    }
    if(now>=gMarketGoldPoll){
        gMarketGoldPoll=now+100;
        auto player=GetClientPlayerUnit();D2UnitStrc local{};D2InventoryStrc inventory{};
        const auto tab=Marketplace::NativePanel::Tab(panel);
        if(tab<1||tab>7||!Marketplace::NativePanel::Read(player,local)||local.dwUnitType||!Marketplace::NativePanel::Read(local.pInventory,inventory)||inventory.nSharedInvCount!=7){gMarketGoldDeposit.Reset();return false;}
        std::array<uint8_t,16> code{};
        if(!Marketplace::NativePanel::Read(reinterpret_cast<void*>(Pattern::Address(0x1e5620)),code)||code!=std::array<uint8_t,16>{0x48,0x83,0xec,0x28,0x45,0x0f,0xb7,0xc8,0x48,0x85,0xc9,0x74,0x42,0x48,0x8b,0x89})return false;
        const auto stat=reinterpret_cast<int32_t(__fastcall*)(D2UnitStrc*,uint32_t,uint16_t)>(Pattern::Address(0x1e5620));
        const auto carried=stat(player,14,0);if(carried<0)return false;
        uint64_t gold=0;auto node=inventory.pFirstSharedInv;std::array<uint32_t,7> seen{};
        for(unsigned i=0;i<7;++i){
            D2SharedInventoryStrc part{};if(!Marketplace::NativePanel::Read(node,part)||!part.dwUnitId||std::find(seen.begin(),seen.begin()+i,part.dwUnitId)!=seen.begin()+i){gMarketGoldDeposit.Reset();return false;}
            seen[i]=part.dwUnitId;auto unit=Marketplace::NativePanel::Field<D2UnitStrc*>(reinterpret_cast<void*>(Pattern::Address(0x1d442e0)),(part.dwUnitId&127)*sizeof(void*));bool found=false;
            for(unsigned n=0;unit&&n<256;++n){D2UnitStrc value{};if(!Marketplace::NativePanel::Read(unit,value))break;
                if(!value.dwUnitType&&value.dwUnitId==part.dwUnitId){const auto amount=stat(unit,15,0);if(amount<0)break;gold+=uint32_t(amount);found=true;break;}
                if(value.pListNext==unit)break;unit=value.pListNext;
            }
            if(!found||(i==6&&(node!=inventory.pLastSharedInv||part.pNextCorpse))){gMarketGoldDeposit.Reset();return false;}node=part.pNextCorpse;
        }
        gMarketGoldDeposit.Observe(local.dwUnitId,gSelectedPage+1,unsigned(tab),uint32_t(carried),gold,now);
    }
    return gMarketGoldDeposit.Pending().has_value();
}
static void MarketplaceLivePaint(void* panel) try {
    MarketplaceLiveRefreshTick();
    const auto now=GetTickCount64();const bool goldDeposit=MarketplaceLiveGold(panel,now);
    if(!gLiveStashClosed.load()&&!gMarketplacePageSwitching.load()&&!Marketplace::Panel::stashSyncing.load()&&!MarketplaceLiveCursor()&&
        now>=Marketplace::Panel::stashLastInput.load()+(goldDeposit?150:1000)&&!(GetAsyncKeyState(VK_LBUTTON)&0x8000)&&!(GetAsyncKeyState(VK_RBUTTON)&0x8000)){
        bool modal=false;
        for(const auto name:{"DropGoldModal","StackSplitModal","QuantityModal"})if(auto popup=WidgetFindChild(*gpPanelManager,name))modal|=Marketplace::NativePanel::Field<uint8_t>(popup,0x50)!=0;
        const auto tab=Marketplace::NativePanel::Tab(panel);
        // Reload only for queued marketplace work or a matched gold deposit.
        if(!modal&&tab>=0&&WidgetFindChild(panel,"MarketplaceSyncMask")){
            if(goldDeposit&&now>=gMarketGoldRequestPoll){gMarketGoldRequestPoll=now+500;const auto proof=gMarketGoldDeposit.Pending();
                if(proof&&Marketplace::LiveCustody::window.RequestGoldDeposit(gSelectedPage+1,unsigned(tab),proof->carriedBefore,proof->carriedAfter,proof->stashBefore,proof->stashAfter,now))gMarketGoldDeposit.Taken();
            }
            const auto park=Marketplace::LiveCustody::window.BeginRefresh(gSelectedPage+1,unsigned(tab),now);
            if(park){gLiveRefreshReturn=gSelectedPage+1;gLiveRefreshTab=unsigned(tab);Marketplace::Panel::stashSyncing=true;OnStashPageChanged(park-1,false);}
        }
    }
    if(auto mask=WidgetFindChild(panel,"MarketplaceSyncMask"))Marketplace::NativePanel::Visibility(mask,Marketplace::Panel::stashSyncing.load(),false);
    MarketplaceLivePumpRequests();
    if(Marketplace::Panel::stashSyncing.load())return;
    const auto requested=gLiveRestorePage.load();
    if(!requested||gMarketplacePageSwitching.load()||MarketplaceLiveCursor())return;
    const auto pages=WidgetFindChild(panel,"BankPages");
    if(!pages)return;
    if(requested!=gSelectedPage+1)OnStashPageChanged(requested-1,false);
    if(requested==gSelectedPage+1)gLiveRestorePage=0;
    MarketplaceLiveSelectPage(panel);
}catch(...){}
static void MarketplaceLiveTick(bool inGame) try {
    Marketplace::LiveCustody::window.Tick(inGame);
    if(!inGame){gLivePageKnown=false;gLiveConnectPoll=0;}
    if(!inGame){Marketplace::Panel::stashSyncing=false;gLiveRefreshReturn=0;gMarketplacePageSelection.Reset();gMarketplacePageRequests.Reset();gMarketplacePageRecovery=false;gMarketplaceRollbackAttempted=false;gLiveStashClosed=false;gLiveRestorePage=0;gMarketplacePageSwitching=false;gMarketplacePagePhase=-1;gMarketplacePageCarry=false;gMarketplacePageTab=0;}
}catch(...){}
static void __fastcall HookedMarketplaceBankOpen(void* panel){
    Marketplace::Panel::OpenStash();oMarketplaceBankOpen(panel);MarketplaceLiveOpened();
    try{
    if(Marketplace::LiveCustody::window.Snapshot().enabled){
        MarketplaceLiveSelectPage(panel);
    }
    }catch(...){}
}
static void __fastcall HookedMarketplaceBankClose(void* panel){
    Marketplace::Panel::CloseStash();oMarketplaceBankClose(panel);MarketplaceLiveClosed();
}
