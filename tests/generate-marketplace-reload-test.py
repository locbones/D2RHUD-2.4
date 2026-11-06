"""Compile the production stash handlers against isolated native-call stubs."""
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[1]
source = (root / 'd2rhud/plugin/D2RHUD/D2RHUD.cpp').read_text(encoding='utf-8-sig')
runtime = (root / 'd2rhud/plugin/MarketplaceLiveCustodyRuntime.inl').read_text(encoding='utf-8-sig')
def between(text, start, end):
    return text[text.index(start):text.index(end, text.index(start))]
enum = between(source, 'enum SharedStashPhase', 'struct CCMD_STASH_PAGE_CHANGE')
client = between(source, 'void __fastcall SCMD_HandleSharedStashMessage', 'int64_t __fastcall CCMD_HandleSharedStashMessage')
server = between(source, 'int64_t __fastcall CCMD_HandleSharedStashMessage', 'void __fastcall SCMDHANDLER_Custom')
pump = between(source, 'void OnStashPageChanged(uint32_t nSelectedPage) {', 'static void* __fastcall HookedMarketplaceBankMessage')
recover = between(runtime, 'static void MarketplaceLiveRecoverPage() try', 'static std::atomic<bool> gLiveStashClosed')
closed = between(runtime, 'static void MarketplaceLiveContinueClosed() try', 'static void MarketplaceLiveOpened() try')
opened = between(runtime, 'static void MarketplaceLiveOpened() try', 'static bool MarketplaceLiveAllowPage')
connect = between(runtime, 'static void MarketplaceLiveConnect(uint64_t now) try', 'static void MarketplaceLiveSelectPage')
refresh = between(runtime, 'static std::atomic<unsigned> gLiveRefreshReturn', 'static void MarketplaceLiveObserve')
draw = between(source, 'void __fastcall HookedBankPanelDraw(D2BankPanelWidget* pBankPanel) {', '#pragma endregion')
selection = between(runtime, 'static void MarketplaceLiveSelectPage(void* panel) try', 'static bool MarketplaceLiveGold(void* panel,uint64_t now)')
header = r'''
#define NOMINMAX
#include "d2rhud/RuntimeTelemetry.h"
#include "d2rhud/plugin/MarketplacePageRequests.h"
#include <cassert>
#include <iostream>
#include <vector>
#include <utility>
struct D2GameStrc{};struct D2ClientStrc;
struct PlayerData{D2ClientStrc* pClient;};struct D2UnitStrc{PlayerData* pPlayerData;};
struct D2ClientStrc{D2GameStrc* pGame;D2UnitStrc* pPlayer;int nSaveHeaderSize=8;};
struct Message{uint64_t a,b,c;};constexpr uint64_t operator""_hash64(const char*,size_t){return 1;}
static int lastTab=-1;
static void* manager=nullptr;static void** gpPanelManager=&manager;
static bool widgets=false;static std::array<uint8_t,0x1790> pageWidget{};
void* WidgetFindChild(void*,const char*){return widgets?pageWidget.data():nullptr;}void BankPanelMessage(void*,Message& m){lastTab=*reinterpret_cast<int32_t*>(m.c);}
void Shown(uint32_t page){memcpy(pageWidget.data()+0x178C,&page,sizeof(page));}
uint32_t Shown(){uint32_t page;memcpy(&page,pageWidget.data()+0x178C,sizeof(page));return page;}
namespace Marketplace::Panel{inline std::atomic<bool> stashSyncing{false};}
namespace Marketplace::NativePanel {template<class T>T Field(void* p,size_t offset){T value;memcpy(&value,static_cast<uint8_t*>(p)+offset,sizeof(value));return value;}}
namespace Marketplace::LiveCustody{struct Window{std::string error;bool refreshing=false,ready=false,parked=false,enabled=true;unsigned remembered=0;void RememberClosed(unsigned page){remembered=page;}unsigned granted=0;bool Refreshing(){return refreshing;}bool RefreshReady(uint64_t){return ready;}void FinishRefresh(){refreshing=false;}struct View{std::string status;bool enabled;unsigned parking;bool parked;};View Snapshot(){return {error,enabled,1,parked};}unsigned Restore(bool){return 2;}unsigned RefreshPage(){return 2;}unsigned Close(unsigned page,bool,bool=true){return page==11||page==2?1:0;}bool OpenGrant(unsigned page,bool=false){granted=page;parked=true;return true;}void Error(const std::string& e){error=e;}};inline Window window;}
static std::atomic<uint32_t> gSelectedPage{10},gMarketplacePreviousPage{10};
static std::atomic<bool> gLiveStashClosed{false},gLivePageKnown{false};static std::atomic<uint64_t> gLiveConnectPoll{0};static std::atomic<unsigned> gLiveRestorePage{0};
static std::atomic<bool> gMarketplacePageSwitching{false},gMarketplacePageRecovery{false},gMarketplaceRollbackAttempted{false};
static std::atomic<int> gMarketplacePagePhase{-1};static Marketplace::PageRequests gMarketplacePageRequests;static Marketplace::PageSelection gMarketplacePageSelection;
static uint64_t clockNow=1000;uint64_t Clock(){return clockNow;}
#define GetTickCount64 Clock
static int configures=0;static bool configureReady=true;static bool MarketplaceLiveConfigure(){++configures;if(!configureReady)return false;Marketplace::LiveCustody::window.enabled=true;Marketplace::LiveCustody::window.parked=false;return true;}
static void MarketplaceLiveObserve(const char*,uint32_t=0){}
struct GoldDetector{void Reset(){}};static GoldDetector gMarketGoldDeposit;static uint64_t gMarketGoldPoll=0,gMarketGoldRequestPoll=0;
static bool allowed=true,prepared=true,cursor=false;static int freshResult=1,saves=0,frees=0,parses=0,headers=0,parseError=0,loaded=-1,renamed=-1;
static bool MarketplaceLiveAllowPage(uint32_t){return allowed;}
static bool MarketplaceLivePreparePage(uint32_t){return prepared;}
static bool MarketplaceLiveCursor(){return cursor;}
static int MarketplaceLiveSendFreshPage(uint32_t){return freshResult;}
static void MarketplaceLiveLoaded(uint32_t page){assert(gMarketplacePageSwitching);loaded=int(page);if(Marketplace::LiveCustody::window.Refreshing()&&page+1==Marketplace::LiveCustody::window.RefreshPage()){Marketplace::LiveCustody::window.FinishRefresh();Marketplace::Panel::stashSyncing=false;}}
static void UpdateStashFileName(uint32_t page){renamed=int(page);}
struct D2BankPanelWidget{};namespace RunTracker{void StashVisible(){}}
static void MarketplaceLiveSelectPage(void*);static void MarketplaceLivePumpRequests();static void MarketplaceLiveRecoverPage();void OnStashPageChanged(uint32_t);
static void MarketplaceStashNotice(const std::string&){}
static std::optional<uint32_t> drawClick;
void MarketplaceLivePaint(void*){MarketplaceLivePumpRequests();}void UpdateMarketplaceWidgets(void*){}
bool DrawStandaloneMarketplace(void*){return false;}
void CaptureMarketplacePanel(void*,bool){}void oBankPanelDraw(D2BankPanelWidget*){if(drawClick){Shown(*drawClick);drawClick.reset();}}
void Save(D2GameStrc*){++saves;}void Free(D2GameStrc*,D2UnitStrc*){++frees;}
int Parse(D2GameStrc*,D2ClientStrc*,D2UnitStrc*){++parses;return parseError;}void Headers(D2ClientStrc*){++headers;}
void GenericRead(){}
namespace Pattern{uintptr_t Address(uint32_t rva){switch(rva){case 0x28f510:return uintptr_t(Save);case 0x28bf00:return uintptr_t(Free);case 0x319b70:return uintptr_t(Parse);case 0x2a08a0:return uintptr_t(Headers);case 0x10e080:return uintptr_t(GenericRead);default:assert(false);return 0;}}}
'''
wire = r'''
struct CCMD_STASH_PAGE_CHANGE{SharedStashPhase nPhase;uint32_t nPage;};
using SCMD_STASH_PAGE_CHANGE=CCMD_STASH_PAGE_CHANGE;
static std::vector<CCMD_STASH_PAGE_CHANGE> commands,acks;
void CCMD_SendSharedStashMessage(SharedStashPhase phase,uint32_t page){commands.push_back({phase,page});}
void SCMD_SendSharedStashMessage(D2ClientStrc*,SharedStashPhase phase,uint32_t page){acks.push_back({phase,page});}
'''
main = r'''
void Reset(){configures=0;configureReady=true;gLivePageKnown=false;gLiveConnectPoll=0;Marketplace::LiveCustody::window.enabled=true;Marketplace::LiveCustody::window.remembered=0;Marketplace::LiveCustody::window.parked=false;Marketplace::LiveCustody::window.granted=0;lastTab=-1;Marketplace::LiveCustody::window.refreshing=false;Marketplace::LiveCustody::window.ready=false;gLiveRefreshReturn=0;gLiveRefreshTab=0;Marketplace::Panel::stashSyncing=false;drawClick.reset();widgets=false;gMarketplacePageSelection.Reset();gLiveRestorePage=0;gLiveStashClosed=false;gSelectedPage=10;gMarketplacePreviousPage=10;gMarketplacePageSwitching=false;gMarketplacePagePhase=-1;gMarketplacePageRequests.Reset();gMarketplacePageRecovery=false;gMarketplaceRollbackAttempted=false;allowed=prepared=true;cursor=false;freshResult=1;commands.clear();acks.clear();saves=frees=parses=headers=parseError=0;loaded=renamed=-1;clockNow=1000;}
void Ack(SharedStashPhase phase,uint32_t page){SCMD_STASH_PAGE_CHANGE packet{phase,page};SCMD_HandleSharedStashMessage(&packet);}
int main(){
 D2GameStrc game;D2UnitStrc unit{};D2ClientStrc client{&game,&unit};PlayerData data{&client};unit.pPlayerData=&data;
 // Late enrollment after the stash opens retries on the native timer,
 // never forcing a page switch or configuring while holding an item.
 Reset();configureReady=false;Marketplace::LiveCustody::window.enabled=false;MarketplaceLiveOpened();
 assert(gLivePageKnown&&Marketplace::LiveCustody::window.granted==0);
 MarketplaceLiveConnect(1000);assert(configures==2&&commands.empty());
 configureReady=true;MarketplaceLiveConnect(1500);assert(configures==2);
 cursor=true;MarketplaceLiveConnect(3000);assert(configures==2);cursor=false;
 MarketplaceLiveConnect(5000);assert(configures==3&&Marketplace::LiveCustody::window.granted==11&&commands.empty());
 Reset();configureReady=false;Marketplace::LiveCustody::window.enabled=false;MarketplaceLiveOpened();gLiveStashClosed=true;
 configureReady=true;MarketplaceLiveConnect(1000);assert(Marketplace::LiveCustody::window.remembered==11&&commands.empty());
 MarketplaceLiveContinueClosed();assert(commands.size()==1&&gSelectedPage==0);
 Reset();gLivePageKnown=true;gMarketplacePageSwitching=true;MarketplaceLiveConnect(1000);assert(configures==0);
 // A parked handoff must not skip settings refresh on a real stash open.
 Reset();Marketplace::LiveCustody::window.parked=true;MarketplaceLiveOpened();
 assert(configures==1&&Marketplace::LiveCustody::window.granted==11&&gLiveRestorePage==0);
 Reset();gMarketplacePageSwitching=true;MarketplaceLiveOpened();assert(configures==0); // never configure an unfinished load
 Reset();allowed=false;OnStashPageChanged(1);assert(commands.empty()&&!gMarketplacePageSwitching&&gSelectedPage==10);
 OnStashPageChanged(3);allowed=true;clockNow+=100;MarketplaceLivePumpRequests();assert(commands.size()==1&&commands.back().nPage==3);
 // A newer request during loading survives. Stale acknowledgements do nothing.
 OnStashPageChanged(4);Ack(NewSharedStashUnitsCreated,3);assert(gMarketplacePageSwitching);
 Ack(OldSharedStashFreed,2);assert(commands.size()==1);Ack(OldSharedStashFreed,3);assert(commands.back().nPhase==NewSharedStashLoadedAndAttachMessagesSent);
 Ack(OldSharedStashFreed,3);assert(commands.size()==2);Ack(NewSharedStashUnitsCreated,3);assert(gMarketplacePageSwitching&&loaded==3);
 MarketplaceLivePumpRequests();assert(gMarketplacePageSwitching&&gSelectedPage==4&&commands.back().nPage==4);
 // Header/file preflight refusal never frees the existing stash or sends a switch.
 Reset();prepared=false;OnStashPageChanged(1);assert(commands.empty()&&!gMarketplacePageSwitching&&gSelectedPage==10&&loaded==10);
 Reset();cursor=true;OnStashPageChanged(1);assert(commands.empty());cursor=false;clockNow+=100;MarketplaceLivePumpRequests();assert(commands.size()==1);
 // Execute the real server failure branch, then restore the original page
 // without a second save of the partial/empty destination.
 Reset();OnStashPageChanged(1);auto packet=commands.back();CCMD_HandleSharedStashMessage(&game,&unit,&packet);
 assert(saves==1&&frees==1&&acks.back().nPhase==OldSharedStashFreed);Ack(acks.back().nPhase,acks.back().nPage);
 parseError=27;packet=commands.back();CCMD_HandleSharedStashMessage(&game,&unit,&packet);
 assert(parses==1&&headers==1&&frees==2&&acks.back().nPhase==SharedStashLoadFailed);
 Ack(acks.back().nPhase,acks.back().nPage);assert(gSelectedPage==10&&renamed==10&&commands.back().nPage==10&&commands.back().nPhase==NewSharedStashLoadedAndAttachMessagesSent);
 parseError=0;packet=commands.back();CCMD_HandleSharedStashMessage(&game,&unit,&packet);Ack(acks.back().nPhase,acks.back().nPage);
 assert(saves==1&&parses==2&&headers==2&&!gMarketplacePageSwitching&&loaded==10);
 // Failed prepared-packet submission also returns to the intact previous page.
 Reset();OnStashPageChanged(1);freshResult=-1;Ack(OldSharedStashFreed,1);assert(gMarketplaceRollbackAttempted&&saves==0);
 // Malformed source/owner packets never invoke native save or inventory calls.
 Reset();packet={PageChanged,64};CCMD_HandleSharedStashMessage(&game,&unit,&packet);assert(saves==0);packet.nPage=1;CCMD_HandleSharedStashMessage(nullptr,&unit,&packet);assert(saves==0);
 // A closed bank has no draw callbacks: the native timer must still retry
 // parking after lease/cursor contention, without any bank rendering.
 Reset();gLiveStashClosed=true;allowed=false;MarketplaceLiveContinueClosed();assert(commands.empty());
 allowed=true;clockNow+=250;MarketplaceLiveContinueClosed();assert(gMarketplacePageSwitching&&gSelectedPage==0);
 Reset();gLiveStashClosed=true;cursor=true;MarketplaceLiveContinueClosed();assert(commands.empty());cursor=false;MarketplaceLiveContinueClosed();assert(gSelectedPage==0);
 // The native control still shows page 11 when a timer starts deferred page 2.
 // Polling that unchanged display must never enqueue a return to page 11.
 Reset();widgets=true;Shown(10);gMarketplacePageSelection.Show(10);allowed=false;
 Shown(1);MarketplaceLivePollPage(nullptr);assert(commands.empty()&&Shown()==10);
 allowed=true;clockNow+=100;MarketplaceLivePumpRequests();assert(gSelectedPage==1);
 MarketplaceLivePollPage(nullptr);assert(Shown()==1&&commands.size()==1);
 Ack(OldSharedStashFreed,1);Ack(NewSharedStashUnitsCreated,1);
 for(int i=0;i<30;++i){HookedBankPanelDraw(nullptr);MarketplaceLivePumpRequests();}
 assert(!gMarketplacePageSwitching&&gSelectedPage==1&&commands.size()==2);
 // Clicks during a load are accepted; latest wins, old highlights do not.
 Reset();widgets=true;Shown(10);gMarketplacePageSelection.Show(10);
 Shown(1);MarketplaceLivePollPage(nullptr);assert(gSelectedPage==1);
 drawClick=3;HookedBankPanelDraw(nullptr);drawClick=4;HookedBankPanelDraw(nullptr);
 Ack(OldSharedStashFreed,1);Ack(NewSharedStashUnitsCreated,1);assert(gSelectedPage==4);
 MarketplaceLivePollPage(nullptr);Ack(OldSharedStashFreed,4);Ack(NewSharedStashUnitsCreated,4);
 for(int i=0;i<30;++i){HookedBankPanelDraw(nullptr);MarketplaceLivePumpRequests();}
 assert(!gMarketplacePageSwitching&&gSelectedPage==4&&Shown()==4&&commands.size()==4);
 Reset();Marketplace::Panel::stashSyncing=true;OnStashPageChanged(44);assert(commands.empty());OnStashPageChanged(1);assert(commands.size()==1);
 // Run the production automatic-refresh continuation through real packet
 // handlers: wait for receipt/cursor, return to page 2, restore tab 4 once.
 Reset();widgets=true;Shown(0);gMarketplacePageSelection.Show(0);gSelectedPage=0;
 gLiveRefreshReturn=2;gLiveRefreshTab=4;Marketplace::LiveCustody::window.refreshing=true;
 MarketplaceLiveRefreshTick();assert(commands.empty());Marketplace::LiveCustody::window.ready=true;cursor=true;
 MarketplaceLiveRefreshTick();assert(commands.empty());cursor=false;MarketplaceLiveRefreshTick();assert(commands.size()==1&&gSelectedPage==1);
 MarketplaceLiveRefreshTick();assert(commands.size()==1);Ack(OldSharedStashFreed,1);Ack(NewSharedStashUnitsCreated,1);
 assert(lastTab==4&&gLiveRefreshReturn==0&&!Marketplace::LiveCustody::window.Refreshing());
 for(int i=0;i<30;++i){MarketplaceLiveRefreshTick();HookedBankPanelDraw(nullptr);}assert(commands.size()==2);
 Reset();gLiveStashClosed=true;Marketplace::LiveCustody::window.refreshing=true;Marketplace::LiveCustody::window.ready=true;
 MarketplaceLiveRefreshTick();assert(commands.empty()&&!Marketplace::LiveCustody::window.Refreshing());
 std::cout<<"PASS: production client/server stash handlers; stable widget selection, navigation during loading, queued latest navigation, short-lock/cursor retries, stale/duplicate ACKs, preflight refusal, parse-error ACK and rollback without saving partial pages. No game code executed.\n";
}
'''
Path(sys.argv[1]).write_text(header+enum+wire+recover+closed+refresh+opened+connect+selection+client+server+pump+draw+main, encoding='utf-8')
