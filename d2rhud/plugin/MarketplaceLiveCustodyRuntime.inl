// Included after the native marketplace widget helpers. Only native client
// callbacks touch widgets/player pointers. The render tick renews value-only
// handoff records and never calls inventory or page-loading functions.
#include "MarketplacePagePackets.h"
static std::mutex gLivePagePacketsMutex;
static uint32_t gLivePacketPage=UINT32_MAX;
static std::vector<std::vector<uint8_t>> gLivePagePackets;
static bool MarketplaceLivePreparePage(uint32_t page) try {
    std::lock_guard lock(gLivePagePacketsMutex);gLivePacketPage=UINT32_MAX;gLivePagePackets.clear();
    const auto path=Marketplace::LiveCustody::window.PageFile(page+1);if(!path)return true;
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
    gLivePagePackets.clear();gLivePacketPage=UINT32_MAX;return 1;
}catch(...){return -1;}
static std::atomic<bool> gLiveStashClosed{false};
static std::atomic<unsigned> gLiveRestorePage{0};
static void MarketplaceLiveObserve(const char* event){
    wchar_t profile[32768]{};const auto size=GetEnvironmentVariableW(L"USERPROFILE",profile,32768);
    if(!size||size>=32768)return;
    try{
        const auto cfg=Marketplace::LiveCustody::Read(std::filesystem::path(profile)/L".d2r-marketplace/custody-enrollment.json");
        const auto root=std::filesystem::path(cfg.at("state_directory").get<std::string>());
        const auto view=Marketplace::LiveCustody::window.Snapshot();
        const auto name=std::string_view(event).starts_with("native-hooks-")?L"live-native-hooks.json":L"live-native-status.json";
        Marketplace::LiveCustody::Write(root/name,{{"schema",1},{"event",event},{"tick",GetTickCount64()},
            {"pid",GetCurrentProcessId()},{"page",gSelectedPage+1},{"enabled",view.enabled},{"parked",view.parked},{"status",view.status}});
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
    gLiveStashClosed=true;gLiveRestorePage=0;
    if(gMarketplacePageSwitching.load()||!MarketplaceLiveConfigure()||MarketplaceLiveCursor()){MarketplaceLiveObserve("close-deferred");return;}
    MarketplaceLiveObserve("stash-closed");
    const auto page=Marketplace::LiveCustody::window.Close(gSelectedPage+1,false);
    if(page)OnStashPageChanged(page-1);
    else Marketplace::LiveCustody::window.OpenGrant(gSelectedPage+1);
}catch(...){}
static void MarketplaceLiveOpened() try {
    gLiveStashClosed=false;
    const auto view=Marketplace::LiveCustody::window.Snapshot();
    if(view.enabled&&(view.parked||gMarketplacePageSwitching.load()))gLiveRestorePage=Marketplace::LiveCustody::window.Restore();
    if(!gLiveRestorePage.load()&&!gMarketplacePageSwitching.load()&&MarketplaceLiveConfigure())
        Marketplace::LiveCustody::window.OpenGrant(gSelectedPage+1,true);
    MarketplaceLiveObserve("stash-opened");
}catch(...){}
static bool MarketplaceLiveAllowPage(uint32_t page) try {
    return Marketplace::LiveCustody::window.Permit(page+1);
}catch(...){return false;}
static void MarketplaceLiveLoaded(uint32_t page) try {
    if(gLiveStashClosed.load()&&!MarketplaceLiveCursor())Marketplace::LiveCustody::window.OpenGrant(page+1);
    else Marketplace::LiveCustody::window.OpenGrant(page+1,true);
    MarketplaceLiveObserve("page-loaded");
}catch(...){}
static void MarketplaceLivePaint(void* panel) try {
    const auto requested=gLiveRestorePage.load();
    if(!requested||gMarketplacePageSwitching.load()||MarketplaceLiveCursor())return;
    const auto pages=WidgetFindChild(panel,"BankPages");
    if(!pages)return;
    if(Marketplace::NativePanel::Field<uint32_t>(pages,0x178C)!=gSelectedPage){gLiveRestorePage=0;return;}
    uint32_t selection=gSelectedPage;
    if(Marketplace::LiveCustody::window.Permit(requested)){
        gLiveRestorePage=0;
        if(requested!=gSelectedPage+1)OnStashPageChanged(requested-1);
        selection=gSelectedPage;
    }
    SIZE_T written=0;WriteProcessMemory(GetCurrentProcess(),static_cast<uint8_t*>(pages)+0x178C,&selection,sizeof(selection),&written);
}catch(...){}
static void MarketplaceLiveTick(bool inGame) try {
    Marketplace::LiveCustody::window.Tick(inGame);
    if(!inGame){gLiveStashClosed=false;gLiveRestorePage=0;gMarketplacePageSwitching=false;gMarketplacePagePhase=-1;}
}catch(...){}
static void __fastcall HookedMarketplaceBankOpen(void* panel){
    oMarketplaceBankOpen(panel);MarketplaceLiveOpened();
    try{
    if(Marketplace::LiveCustody::window.Snapshot().enabled){
        if(auto pages=WidgetFindChild(panel,"BankPages")){SIZE_T count=0;WriteProcessMemory(GetCurrentProcess(),static_cast<uint8_t*>(pages)+0x178C,&gSelectedPage,sizeof(gSelectedPage),&count);}
    }
    }catch(...){}
}
static void __fastcall HookedMarketplaceBankClose(void* panel){
    oMarketplaceBankClose(panel);MarketplaceLiveClosed();
}
