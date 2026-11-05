// Included after the native marketplace adapters and live-custody helpers.
#include "MarketplaceHotkey.h"
namespace Marketplace::Hotkey {
inline std::atomic<bool> requested{false};
inline Session session;
// 2.4 uses UI index 24 for BankExpansionLayout, 1 for inventory. LoD's
// UI_STASH enum is 25 here (the cube), so never use that older enum.
inline constexpr int StashPanel=24,InventoryPanel=1;
inline constexpr uint32_t UIFlags=0x1d53fb8;
inline bool NativeSupported(){
    static const bool result=[](){
        struct Signature {uint32_t rva;uint64_t hash;};
        // The OpenPanel entry is detoured by the journal adapter. Check its
        // unchanged body and the panel-name resolver, plus ClosePanel.
        const Signature signatures[]{{0x86608,0xe4fae52bd139c930ULL},{0x86860,0x8773941eeb5f8596ULL},{0x86030,0x2225ea27c1edcda3ULL},{0x86149,0x7af67fb96395bed0ULL}};
        for(const auto& s:signatures){std::array<uint8_t,32> bytes{};if(!NativePanel::Read(reinterpret_cast<void*>(Pattern::Address(s.rva)),bytes))return false;
            uint64_t hash=14695981039346656037ULL;for(auto byte:bytes)hash=(hash^byte)*1099511628211ULL;if(hash!=s.hash)return false;}
        return NativePanel::WidgetLayoutSupported();
    }();return result;
}
inline bool Flag(int panel){return NativePanel::Field<uint8_t>(reinterpret_cast<void*>(Pattern::Address(UIFlags+panel)))!=0;}
inline void Request(){requested=true;}
}
static void MarketplaceHotkeyTick(bool playing,bool blocked){
    using namespace Marketplace;using namespace Marketplace::Hotkey;
    const bool request=requested.exchange(false);if(!request&&!session.Busy())return;
    Hotkey::Context context;context.now=GetTickCount64();context.playing=playing;
    D2UnitStrc player{};if(playing&&NativePanel::Read(GetClientPlayerUnit(),player)&&player.dwUnitType==0)context.owner=player.dwUnitId;
    context.stashOpen=playing&&Flag(StashPanel);context.inventoryOpen=playing&&Flag(InventoryPanel);
    context.marketOpen=Panel::NativeOpen();const bool cursor=playing&&MarketplaceLiveCursor();
    context.syncing=Panel::stashSyncing.load()||gMarketplacePageSwitching.load()||cursor;
    context.allowed=playing&&!blocked&&!cursor&&!(player.dwFlags&0x10000)&&player.dwAnimMode!=0&&player.dwAnimMode!=17;
    for(int panel:{5,9,11,13,19,22,25})if(playing&&Flag(panel))context.allowed=false;
    {std::lock_guard lock(Panel::mutex);const auto& capture=Panel::frame;context.paintTick=capture.tick;
        context.paintReady=capture.owner==context.owner&&!capture.suspended&&Panel::Fresh(context.now,capture.tick);}
    if(request&&context.allowed){
        auto bank=gpPanelManager?WidgetFindChild(*gpPanelManager,"BankExpansionLayout"):nullptr;
        context.available=bank&&WidgetFindChild(bank,"MarketplaceWorkspace")&&NativeSupported();
    }
    const auto action=session.Tick(context,request);
    Panel::standaloneMarket=session.Busy();
    NativePanel::ShortcutVisuals(session.Busy());
    using NativePanelCall=bool(__fastcall*)(int,bool);
    if(action.openStash)reinterpret_cast<NativePanelCall>(Pattern::Address(0x865e0))(StashPanel,false);
    if(action.openMarket)Panel::NativeMessage(Panel::MessageHash("BankPanelMessage"),Panel::MessageHash("MarketOpen"));
    if(action.closeStash)reinterpret_cast<NativePanelCall>(Pattern::Address(0x86860))(StashPanel,false);
    if(action.closeInventory)reinterpret_cast<NativePanelCall>(Pattern::Address(0x86860))(InventoryPanel,false);
    // OpenPanel may enable inventory drawing during its stock OnOpen callback.
    NativePanel::ShortcutVisuals(session.Busy());
    if(action.notice)MarketplaceStashNotice(action.notice);
}
