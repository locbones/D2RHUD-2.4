#pragma once
#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>
#include <map>
#include <string_view>
#include <array>
#include <bitset>

// Native paint publishes value-only snapshots. ImGui and window input never
// retain a game unit/widget pointer. Coordinates are fractions of client size.
namespace Marketplace::Panel {
struct Rect {
    float left=0,top=0,right=0,bottom=0;
    bool Contains(float x,float y)const{return x>=left&&x<right&&y>=top&&y<bottom;}
};
struct Item {
    uint32_t unit=0,seed=0;
    Rect rect;
    std::vector<uint8_t> bytes;
    unsigned tab=0;
};
struct PageHighlight {Rect row,clip;unsigned page=0;};
struct Frame {
    uint64_t tick=0;
    uint32_t owner=0;
    unsigned page=1,tab=0;
    bool hardcore=false,cursorOccupied=false;
    Rect grid;
    PageHighlight marketChoice;
    Item hovered,pinned;
    std::vector<Item> cells; // Value-only occupied cell snapshots, amortized on native paint.
    std::string error;
    std::string syncStatus;
    bool suspended=false;
};
struct Pin {uint32_t unit=0,seed=0,owner=0;unsigned page=0,tab=0;bool hardcore=false;};
inline std::atomic<bool> stashSyncing{false};
inline std::atomic<bool> stashClosed{false};
// Shortcut sessions retain the bank lifecycle but paint only the marketplace.
inline std::atomic<bool> standaloneMarket{false};
inline std::atomic<unsigned> marketPage{0};
inline std::atomic<uint64_t> stashLastInput{0};
// Value-only input observation. Never touch game pointers on the window thread.
inline bool StashSyncWindowInput(UINT message,WPARAM key,LPARAM param){
    const auto now=GetTickCount64();
    if(stashSyncing.load()){
        if(message==WM_LBUTTONDOWN||message==WM_LBUTTONUP||message==WM_LBUTTONDBLCLK||message==WM_RBUTTONDOWN||message==WM_RBUTTONUP||message==WM_RBUTTONDBLCLK||message==WM_MOUSEWHEEL)return true;
        if(message==WM_INPUT){RAWINPUT raw{};UINT size=sizeof(raw);if(GetRawInputData(reinterpret_cast<HRAWINPUT>(param),RID_INPUT,&raw,&size,sizeof(RAWINPUTHEADER))!=UINT(-1)&&raw.header.dwType==RIM_TYPEMOUSE&&raw.data.mouse.usButtonFlags)return true;}
        // Escape, Alt+F4 and gameplay keys retain their normal behavior.
        return (message==WM_KEYDOWN||message==WM_KEYUP||message==WM_CHAR)&&key==VK_RETURN;
    }
    if(message==WM_LBUTTONUP||message==WM_RBUTTONUP||(message==WM_KEYUP&&key==VK_RETURN)){
        stashLastInput=now;
    }else if(message==WM_KEYDOWN||message==WM_CHAR||message==WM_LBUTTONDOWN||message==WM_RBUTTONDOWN)stashLastInput=now;
    return false;
}
inline std::mutex mutex;
inline Frame frame;
struct WithdrawalWatch {unsigned tab=0;std::string savedHex,liveHex;};
struct WithdrawalRequest {Frame context;std::string savedHex;uint64_t sequence=0;};
inline uint32_t withdrawalOwner=0;inline unsigned withdrawalPage=0;
inline bool withdrawalHardcore=false,withdrawalLeft=false,withdrawalRight=false;
inline uint64_t withdrawalSequence=0;inline std::vector<WithdrawalWatch> withdrawalWatches;
inline WithdrawalRequest withdrawalRequest;
inline void WatchWithdrawals(uint32_t owner,unsigned page,bool hardcore,std::vector<WithdrawalWatch> watches){std::lock_guard lock(mutex);withdrawalOwner=owner;withdrawalPage=page;withdrawalHardcore=hardcore;withdrawalWatches=std::move(watches);}
inline WithdrawalRequest TakeWithdrawal(){std::lock_guard lock(mutex);auto request=std::move(withdrawalRequest);withdrawalRequest={};return request;}
inline Pin pin;
struct ScanRequest {uint64_t sequence=0;uint32_t owner=0;unsigned page=0;bool hardcore=false;};
struct ScanResult {ScanRequest request;std::vector<Item> items;std::string error;uint64_t gold=0;bool goldValid=false;};
inline ScanRequest scanRequest;
inline ScanResult scanResult;
inline uint64_t scanSequence=0;
inline uint64_t RequestScan(uint32_t owner,unsigned page,bool hardcore){std::lock_guard lock(mutex);scanRequest={++scanSequence,owner,page,hardcore};scanResult={};return scanSequence;}
inline ScanRequest PendingScan(){std::lock_guard lock(mutex);return scanRequest;}
inline void PublishScan(ScanResult next){std::lock_guard lock(mutex);if(next.request.sequence==scanRequest.sequence){scanResult=std::move(next);scanRequest={};}}
inline ScanResult TakeScan(uint64_t sequence){std::lock_guard lock(mutex);if(!sequence||sequence!=scanResult.request.sequence)return {};auto result=std::move(scanResult);scanResult={};return result;}
inline void CancelScan(){std::lock_guard lock(mutex);scanRequest={};scanResult={};}
struct Input {
    std::vector<Rect> regions;
    uint64_t tick=0;
    bool editor=false,leftHeld=false,rightHeld=false;
};
inline Input input;
inline std::atomic<bool> dismiss{false};
// Native JSON widgets own drawing, focus, hit testing and tooltips. This bridge
// carries commands, input values and presentation only, never widget pointers.
constexpr uint64_t MessageHash(std::string_view value){uint64_t h=14695981039346656037ULL;for(unsigned char c:value){h^=c;h*=1099511628211ULL;}return h;}
inline const std::vector<std::string>& NativeCommands(){
    static const auto names=[](){std::vector<std::string> result={"MarketCampaignOpen","MarketCampaignBack","MarketCampaignPrev","MarketCampaignNext","MarketCampaignItemsPrev","MarketCampaignItemsNext","MarketCampaignClaim","MarketFilterOpen","MarketFilterBack","MarketFilterApply","MarketFilterReset","MarketTermsPrev","MarketTermsNext","MarketFullPrice","MarketFreePrice","MarketGiftItem","MarketGiftBack","MarketGiftSubmit","MarketOpen","MarketClose","MarketWithdrawForMove","MarketKeepListing","MarketBrowse","MarketItems","MarketAgreements","MarketSetup","MarketRefresh","MarketSort","MarketQuality","MarketTab","MarketPriced","MarketAffordable","MarketPrev","MarketNext","MarketPrice","MarketReturn","MarketWallet","MarketConfirm","MarketSavePrice","MarketRemovePrice","MarketCancelPrice","MarketAddPayment","MarketPaymentPrev","MarketPaymentNext","MarketPricePrev","MarketPriceNext","MarketSaveSetup","MarketReload","MarketEnabled","MarketAuto","MarketDepositCurrent","MarketWithdrawCurrent","MarketInspectSelected","MarketCounter"};
        for(auto prefix:{"MarketCard","MarketAgreement","MarketInspect"})for(int i=0;i<6;++i)result.push_back(std::string(prefix)+std::to_string(i));
        for(auto prefix:{"MarketPayment","MarketEth","MarketRemove","MarketWalletGift"})for(int i=0;i<4;++i)result.push_back(std::string(prefix)+std::to_string(i));
        for(const auto* command:{"MarketWalletBack","MarketWalletPrev","MarketWalletNext","MarketWalletSubmit","MarketWalletAction0","MarketWalletAction1","MarketWalletAction2","MarketWalletAction3","MarketHideOwn","MarketHideOtherOffers","MarketSaveName","MarketFavoriteTrade","MarketFavoriteSeller","MarketSellerTrades","MarketViewOffers","MarketBackOffers","MarketOffersFirst","MarketOffersNext","MarketOffersRefresh","MarketBid0","MarketBid1","MarketBid2","MarketBid3","MarketHistory","MarketThreadHistory","MarketBackHistory","MarketHistoryPrev","MarketHistoryNext","MarketReviewOffer","MarketSendMessage","MarketLogPrev","MarketLogNext","MarketApplyOfferAction","MarketBackReceipt","MarketBackList","MarketReceiptPrev","MarketReceiptNext","MarketAcceptOffer","MarketRejectOffer","MarketWithdrawOffer","MarketCounterOffer"})result.push_back(command);
        for(int n=0;n<3;++n){result.push_back("MarketSale"+std::to_string(n));result.push_back("MarketLog"+std::to_string(n));if(n<2)result.push_back("MarketSimilarSale"+std::to_string(n));}return result;}();return names;
}
inline std::vector<uint64_t> nativeActions;
// Caller owns mutex. A close follows accepted actions; it never cancels a
// save click already delivered by the native controls. Reserve its own slot.
inline void QueueNativeClose(){
    const auto close=MessageHash("MarketClose");
    if(std::find(nativeActions.begin(),nativeActions.end(),close)==nativeActions.end())nativeActions.push_back(close);
}
struct WidgetUpdate {std::string name,text;bool visible=true,input=false;uint64_t revision=0;};
struct ChoiceUpdate {std::string name;int selection=0;uint64_t revision=0;};
struct ChoiceInput {int selection=0;uint64_t revision=0;};
struct ImageUpdate {std::string name,filename;int width=0,height=0;bool visible=true;};
struct PreviewUpdate {std::string name,label,itemHex;};
struct RectUpdate {std::string name;int x=0,y=0,width=0,height=0;};
struct NativeView {bool open=false;std::vector<WidgetUpdate> widgets;int section=0;std::vector<ChoiceUpdate> choices;std::vector<ImageUpdate> images;std::vector<PreviewUpdate> previews;std::vector<RectUpdate> rectangles;};
inline NativeView nativeView;
struct NativeInput {std::string text;uint64_t revision=0;};
struct KeyboardEvent {UINT message=0;uint32_t key=0;std::array<BYTE,256> state{};};
inline std::vector<KeyboardEvent> keyboardEvents;
inline std::bitset<256> capturedKeys;
inline std::map<std::string,NativeInput> nativeInputs;
inline std::map<std::string,ChoiceInput> nativeChoices;
inline void NativeChoices(std::map<std::string,ChoiceInput> next){std::lock_guard lock(mutex);nativeChoices=std::move(next);}
inline auto ReadNativeChoices(){std::lock_guard lock(mutex);return nativeChoices;}
inline void Present(NativeView next){std::lock_guard lock(mutex);nativeView=std::move(next);}
inline NativeView Presentation(){std::lock_guard lock(mutex);return nativeView;}
inline bool NativeOpen(){std::lock_guard lock(mutex);return nativeView.open;}
inline bool NativeMessage(uint64_t domain,uint64_t command){
    if(domain!=MessageHash("BankPanelMessage"))return false;
    std::lock_guard lock(mutex);
    // Use the stock close action in JSON, which 2.4 always resolves. It closes
    // this workspace first; outside the market the original stash handles it.
    if(command==MessageHash("Close")){
        if(!nativeView.open)return false;command=MessageHash("MarketClose");
    }
    if(command==MessageHash("MarketClose")){QueueNativeClose();return true;}
    if(std::find(nativeActions.begin(),nativeActions.end(),MessageHash("MarketClose"))!=nativeActions.end())return true;
    for(const auto& name:NativeCommands())if(command==MessageHash(name)){if(nativeActions.size()<64)nativeActions.push_back(command);return true;}return false;
}
inline void NativeInputs(std::map<std::string,NativeInput> next){std::lock_guard lock(mutex);nativeInputs=std::move(next);}
inline auto ReadNativeInputs(){std::lock_guard lock(mutex);return nativeInputs;}
inline auto TakeNativeActions(){std::lock_guard lock(mutex);auto next=std::move(nativeActions);nativeActions.clear();return next;}
inline bool Fresh(uint64_t now,uint64_t then){return then&&now>=then&&now-then<250;}
inline void Publish(Frame next){std::lock_guard lock(mutex);if(!stashClosed.load())frame=std::move(next);}
inline void OpenStash(){std::lock_guard lock(mutex);stashClosed=false;}
inline void CloseStash(){std::lock_guard lock(mutex);stashClosed=true;frame={};}
// Known capture gaps are not a stash-close event. Keep the workspace/session
// while switching pages, serializing a scan, or temporarily losing focus.
inline void SuspendCapture(){std::lock_guard lock(mutex);frame.suspended=true;frame.tick=GetTickCount64();}
inline Frame Current(){std::lock_guard lock(mutex);return frame;}
inline Pin Pinned(){std::lock_guard lock(mutex);return pin;}
inline void SetPin(Pin next){std::lock_guard lock(mutex);pin=next;}
inline bool SameContext(const Frame& a,const Frame& b){return a.owner==b.owner&&a.page==b.page&&a.tab==b.tab&&a.hardcore==b.hardcore;}
inline bool Matches(const Pin& p,const Frame& f){return p.unit&&p.owner==f.owner&&p.page==f.page&&p.tab==f.tab&&p.hardcore==f.hardcore;}
inline void Regions(std::vector<Rect> regions,bool editor,uint64_t now){std::lock_guard lock(mutex);input.regions=std::move(regions);input.editor=editor;input.tick=now;}
inline void Hide(){std::lock_guard lock(mutex);input.regions.clear();input.editor=false;input.tick=0;pin={};scanRequest={};scanResult={};nativeView={};nativeInputs.clear();nativeChoices.clear();nativeActions.clear();keyboardEvents.clear();withdrawalWatches.clear();withdrawalRequest={};}
inline bool NativeActive(){std::lock_guard lock(mutex);return nativeView.open&&Fresh(GetTickCount64(),frame.tick);}
inline bool KeyboardActive(){std::lock_guard lock(mutex);const auto now=GetTickCount64();return Fresh(now,frame.tick)&&(nativeView.open||(input.editor&&Fresh(now,input.tick)));}
inline auto TakeKeyboardEvents(){std::lock_guard lock(mutex);auto events=std::move(keyboardEvents);keyboardEvents.clear();return events;}
// Stop at the first window hook, before HUD shortcuts and the game's global
// input mapper. Replay value-only events exclusively into the native workspace
// on its paint thread; no widget/game pointers cross into the window hook.
inline bool WithdrawalWindowInput(HWND window,UINT message,WPARAM key,LPARAM param){
    std::lock_guard lock(mutex);bool left=message==WM_LBUTTONDOWN||message==WM_LBUTTONDBLCLK,right=message==WM_RBUTTONDOWN||message==WM_RBUTTONDBLCLK;
    bool leftUp=message==WM_LBUTTONUP,rightUp=message==WM_RBUTTONUP;
    if(message==WM_INPUT){RAWINPUT raw{};UINT size=sizeof(raw);const auto read=GetRawInputData(reinterpret_cast<HRAWINPUT>(param),RID_INPUT,&raw,&size,sizeof(RAWINPUTHEADER));
        if(read==UINT(-1)||raw.header.dwType!=RIM_TYPEMOUSE)return false;const auto flags=raw.data.mouse.usButtonFlags;
        left=flags&RI_MOUSE_LEFT_BUTTON_DOWN;right=flags&RI_MOUSE_RIGHT_BUTTON_DOWN;leftUp=flags&RI_MOUSE_LEFT_BUTTON_UP;rightUp=flags&RI_MOUSE_RIGHT_BUTTON_UP;
    }
    if((leftUp&&withdrawalLeft)||(rightUp&&withdrawalRight)){if(leftUp)withdrawalLeft=false;if(rightUp)withdrawalRight=false;return true;}
    if(!left&&!right)return false;
    if(nativeView.open||!Fresh(GetTickCount64(),frame.tick)||frame.cursorOccupied||!frame.hovered.unit||frame.hovered.bytes.empty()||frame.owner!=withdrawalOwner||frame.page!=withdrawalPage||frame.hardcore!=withdrawalHardcore)return false;
    POINT point{};RECT client{};if(!GetCursorPos(&point)||!ScreenToClient(window,&point)||!GetClientRect(window,&client)||client.right<=0||client.bottom<=0||!frame.hovered.rect.Contains(float(point.x)/client.right,float(point.y)/client.bottom))return false;
    std::string hex;constexpr char digits[]="0123456789abcdef";for(auto value:frame.hovered.bytes){hex+=digits[value>>4];hex+=digits[value&15];}
    for(const auto& watch:withdrawalWatches)if(watch.tab==frame.tab&&(watch.savedHex==hex||watch.liveHex==hex)){
        withdrawalRequest={frame,watch.savedHex,++withdrawalSequence};withdrawalLeft|=left;withdrawalRight|=right;return true;
    }
    return false;
}
inline bool NativeWindowInput(UINT message,WPARAM key,LPARAM param){
    std::lock_guard lock(mutex);
    if(message==WM_KILLFOCUS||(message==WM_ACTIVATEAPP&&!key)){keyboardEvents.clear();capturedKeys.reset();return false;}
    const bool down=message==WM_KEYDOWN||message==WM_SYSKEYDOWN;
    const bool up=message==WM_KEYUP||message==WM_SYSKEYUP;
    if(up&&key<256&&capturedKeys.test(key)){capturedKeys.reset(key);if(!nativeView.open)return true;}
    if(!nativeView.open)return false;
    // Preserve the operating-system close shortcut and normal window lifecycle.
    if((down||up)&&key==VK_F4&&(param&(LPARAM(1)<<29)))return false;
    if(message==WM_INPUT){
        RAWINPUTHEADER header{};UINT size=sizeof(header);
        return GetRawInputData(reinterpret_cast<HRAWINPUT>(param),RID_HEADER,&header,&size,sizeof(header))==sizeof(header)&&header.dwType==RIM_TYPEKEYBOARD;
    }
    if(!down&&!up&&message!=WM_CHAR&&message!=WM_SYSCHAR&&message!=WM_UNICHAR&&message!=WM_DEADCHAR&&message!=WM_SYSDEADCHAR)return false;
    if(down&&key<256)capturedKeys.set(key);
    if(down&&key==VK_ESCAPE){QueueNativeClose();return true;}
    if((message==WM_CHAR||message==WM_SYSCHAR||message==WM_UNICHAR)&&key<32)return true;
    if(message==WM_DEADCHAR||message==WM_SYSDEADCHAR||(message==WM_UNICHAR&&(key==UNICODE_NOCHAR||key>0xffff)))return true;
    if(keyboardEvents.size()<256){KeyboardEvent event{message,uint32_t(key)};GetKeyboardState(event.state.data());keyboardEvents.push_back(std::move(event));}
    return true;
}
// Must run before native stash search, gameplay hotkeys and ImGui's previous-
// frame capture flags. Capture down AND up, including a release outside a chip.
inline bool HandleInput(HWND window,UINT message,WPARAM key,LPARAM param,bool* outsideClick=nullptr) {
    if(outsideClick)*outsideClick=false;
    std::lock_guard lock(mutex);const auto now=GetTickCount64();
    const bool active=Fresh(now,input.tick)&&Fresh(now,frame.tick);
    const bool leftUp=message==WM_LBUTTONUP,rightUp=message==WM_RBUTTONUP;
    if((leftUp&&input.leftHeld)||(rightUp&&input.rightHeld)){
        if(leftUp)input.leftHeld=false;if(rightUp)input.rightHeld=false;return true;
    }
    if(!active){input.leftHeld=input.rightHeld=false;return false;}
    if(message==WM_KEYDOWN||message==WM_KEYUP||message==WM_SYSKEYDOWN||message==WM_SYSKEYUP||message==WM_CHAR||message==WM_SYSCHAR){
        if(!input.editor)return false;
        if(message==WM_KEYDOWN&&key==VK_ESCAPE)dismiss=true;
        return true;
    }
    if(message==WM_INPUT)return input.editor;
    const bool mouse=message==WM_MOUSEMOVE||message==WM_LBUTTONDOWN||message==WM_LBUTTONDBLCLK||leftUp||
        message==WM_RBUTTONDOWN||message==WM_RBUTTONDBLCLK||rightUp||message==WM_MBUTTONDOWN||message==WM_MBUTTONUP||
        message==WM_MOUSEWHEEL||message==WM_MOUSEHWHEEL;
    if(!mouse)return false;
    POINT point{short(LOWORD(param)),short(HIWORD(param))};
    if(message==WM_MOUSEWHEEL||message==WM_MOUSEHWHEEL)ScreenToClient(window,&point);
    RECT client{};if(!GetClientRect(window,&client)||client.right<=0||client.bottom<=0)return false;
    const float x=float(point.x)/client.right,y=float(point.y)/client.bottom;
    const bool inside=std::any_of(input.regions.begin(),input.regions.end(),[&](const Rect& r){return r.Contains(x,y);});
    if(inside){
        if(message==WM_LBUTTONDOWN||message==WM_LBUTTONDBLCLK)input.leftHeld=true;
        if(message==WM_RBUTTONDOWN||message==WM_RBUTTONDBLCLK)input.rightHeld=true;
    }else if(input.editor&&(message==WM_LBUTTONDOWN||message==WM_RBUTTONDOWN)){
        dismiss=true;if(outsideClick)*outsideClick=true;
    }
    return inside||input.leftHeld||input.rightHeld;
}
}
