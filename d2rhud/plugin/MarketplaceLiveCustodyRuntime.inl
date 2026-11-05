// Included after the native marketplace widget helpers. Only native client
// callbacks touch widgets/player pointers. The render tick renews value-only
// handoff records and never calls inventory or page-loading functions.
#include "MarketplacePagePackets.h"
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
static std::atomic<unsigned> gLiveRestorePage{0};
static std::atomic<unsigned> gLiveRefreshReturn{0},gLiveRefreshTab{0};
static int MarketplaceLiveReloadTab(uint32_t page){
    if(gLiveRefreshReturn.load()!=page+1)return 0;
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
    if(!MarketplaceLiveCursor())OnStashPageChanged(gLiveRefreshReturn-1);
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
    const auto player=GetClientPlayerUnit();D2UnitStrc value{};
    return !Marketplace::NativePanel::Read(player,value)||!value.pInventory||Marketplace::NativePanel::Field<void*>(value.pInventory,0x40)!=nullptr;
}
static void MarketplaceLiveClosed() try {
    gLiveStashClosed=true;gLiveRestorePage=0;Marketplace::Panel::stashSyncing=false;
    gMarketplacePageRequests.Reset();
    Marketplace::LiveCustody::window.RememberClosed(Marketplace::LiveCustody::window.Refreshing()?gLiveRefreshReturn.load():gSelectedPage.load()+1);
    if(gMarketplacePageSwitching.load()||!MarketplaceLiveConfigure()||MarketplaceLiveCursor()){MarketplaceLiveObserve("close-deferred");return;}
    MarketplaceLiveObserve("stash-closed");
    const auto page=Marketplace::LiveCustody::window.Close(gSelectedPage+1,false,!Marketplace::LiveCustody::window.Refreshing());
    if(page)OnStashPageChanged(page-1);
    else Marketplace::LiveCustody::window.OpenGrant(gSelectedPage+1);
}catch(...){}
static void MarketplaceLiveContinueClosed() try {
    // OnClose can race a short write or an occupied cursor. Retry from the
    // native event thread even though the bank's draw callback is now inactive.
    if(!gLiveStashClosed.load()||gMarketplacePageSwitching.load()||MarketplaceLiveCursor())return;
    const auto view=Marketplace::LiveCustody::window.Snapshot();if(!view.enabled)return;
    const auto parking=Marketplace::LiveCustody::window.Close(gSelectedPage+1,false,false);
    if(parking)OnStashPageChanged(parking-1);
    else Marketplace::LiveCustody::window.OpenGrant(gSelectedPage+1);
}catch(...){}
static void MarketplaceLiveOpened() try {
    gLiveStashClosed=false;Marketplace::Panel::stashSyncing=Marketplace::LiveCustody::window.Refreshing();
    const auto view=Marketplace::LiveCustody::window.Snapshot();
    if(view.enabled&&(view.parked||gMarketplacePageSwitching.load()))gLiveRestorePage=Marketplace::LiveCustody::window.Restore(true);
    if(!gLiveRestorePage.load()&&!gMarketplacePageSwitching.load()&&MarketplaceLiveConfigure())
        Marketplace::LiveCustody::window.OpenGrant(gSelectedPage+1,true);
    MarketplaceLiveObserve("stash-opened");
}catch(...){}
static bool MarketplaceLiveAllowPage(uint32_t page) try {
    return Marketplace::LiveCustody::window.Permit(page+1);
}catch(...){return false;}
static void MarketplaceLiveLoaded(uint32_t page) try {
    if(Marketplace::LiveCustody::window.Refreshing()&&page+1==gLiveRefreshReturn){
        Marketplace::LiveCustody::window.FinishRefresh();Marketplace::Panel::stashSyncing=false;
    }
    {std::lock_guard lock(gLivePagePacketsMutex);gLivePacketPage=UINT32_MAX;gLivePagePackets.clear();}
    if(gLiveStashClosed.load()&&!MarketplaceLiveCursor()){
        const auto parking=Marketplace::LiveCustody::window.Close(page+1,false,false);
        if(parking)gMarketplacePageRequests.Request(parking-1);
        else Marketplace::LiveCustody::window.OpenGrant(page+1);
    }
    else Marketplace::LiveCustody::window.OpenGrant(page+1,true);
    MarketplaceLiveObserve("page-loaded");
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
static void MarketplaceLivePaint(void* panel) try {
    MarketplaceLiveRefreshTick();
    const auto now=GetTickCount64();
    if(!gLiveStashClosed.load()&&!gMarketplacePageSwitching.load()&&!Marketplace::Panel::stashSyncing.load()&&!MarketplaceLiveCursor()&&
        now>=Marketplace::Panel::stashLastInput.load()+1000&&!(GetAsyncKeyState(VK_LBUTTON)&0x8000)&&!(GetAsyncKeyState(VK_RBUTTON)&0x8000)){
        bool modal=false;
        for(const auto name:{"DropGoldModal","StackSplitModal","QuantityModal"})if(auto popup=WidgetFindChild(*gpPanelManager,name))modal|=Marketplace::NativePanel::Field<uint8_t>(popup,0x50)!=0;
        const auto tab=Marketplace::NativePanel::Tab(panel);
        // Only a requested delivery may reload the page, with a native status label.
        if(!modal&&tab>=0&&WidgetFindChild(panel,"MarketplaceSyncMask")){
            const auto park=Marketplace::LiveCustody::window.BeginRefresh(gSelectedPage+1,unsigned(tab),now);
            if(park){gLiveRefreshReturn=gSelectedPage+1;gLiveRefreshTab=unsigned(tab);Marketplace::Panel::stashSyncing=true;OnStashPageChanged(park-1);}
        }
    }
    if(auto mask=WidgetFindChild(panel,"MarketplaceSyncMask"))Marketplace::NativePanel::Visibility(mask,Marketplace::Panel::stashSyncing.load(),false);
    MarketplaceLivePumpRequests();
    if(Marketplace::Panel::stashSyncing.load())return;
    const auto requested=gLiveRestorePage.load();
    if(!requested||gMarketplacePageSwitching.load()||MarketplaceLiveCursor())return;
    const auto pages=WidgetFindChild(panel,"BankPages");
    if(!pages)return;
    if(requested!=gSelectedPage+1)OnStashPageChanged(requested-1);
    if(requested==gSelectedPage+1)gLiveRestorePage=0;
    MarketplaceLiveSelectPage(panel);
}catch(...){}
static void MarketplaceLiveTick(bool inGame) try {
    Marketplace::LiveCustody::window.Tick(inGame);
    if(!inGame){Marketplace::Panel::stashSyncing=false;gLiveRefreshReturn=0;gMarketplacePageSelection.Reset();gMarketplacePageRequests.Reset();gMarketplacePageRecovery=false;gMarketplaceRollbackAttempted=false;gLiveStashClosed=false;gLiveRestorePage=0;gMarketplacePageSwitching=false;gMarketplacePagePhase=-1;}
}catch(...){}
static void __fastcall HookedMarketplaceBankOpen(void* panel){
    oMarketplaceBankOpen(panel);MarketplaceLiveOpened();
    try{
    if(Marketplace::LiveCustody::window.Snapshot().enabled){
        MarketplaceLiveSelectPage(panel);
    }
    }catch(...){}
}
static void __fastcall HookedMarketplaceBankClose(void* panel){
    oMarketplaceBankClose(panel);MarketplaceLiveClosed();
}
