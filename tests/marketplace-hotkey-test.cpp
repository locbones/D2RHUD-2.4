#include "../d2rhud/plugin/MarketplaceHotkey.h"
#include <iostream>
#include <stdexcept>
#include <string>
#define CHECK(v) do{if(!(v))throw std::runtime_error("Shortcut check at line "+std::to_string(__LINE__)+": " #v);}while(false)
int main()try{
 using namespace Marketplace::Hotkey;Context c;c.now=100;c.paintTick=99;c.owner=7;c.playing=c.allowed=c.available=true;
 Session s;auto a=s.Tick(c,true);CHECK(a.openStash&&!a.openMarket&&s.Busy());
 CHECK(!s.Tick(c,true).openStash);c.stashOpen=c.inventoryOpen=c.paintReady=true;CHECK(!s.Tick(c,false).openMarket); // Prior paint cannot open a fresh host.
 c.now=c.paintTick=101;a=s.Tick(c,false);CHECK(a.openMarket&&!a.openStash);CHECK(!s.Tick(c,true).openMarket);
 c.marketOpen=true;s.Tick(c,false);c.marketOpen=false;a=s.Tick(c,false);CHECK(a.closeStash&&a.closeInventory&&!s.Busy());
 // A stash/inventory opened by the player remains open on marketplace exit.
 s.Reset();a=s.Tick(c,true);CHECK(a.openMarket&&!a.openStash);c.marketOpen=true;s.Tick(c,false);c.marketOpen=false;a=s.Tick(c,false);CHECK(!a.closeStash&&!a.closeInventory&&!s.Busy());
 c.stashOpen=false;c.inventoryOpen=true;c.paintReady=false;s.Tick(c,true);c.stashOpen=true;c.now=c.paintTick=102;c.paintReady=true;s.Tick(c,false);c.marketOpen=true;s.Tick(c,false);c.marketOpen=false;a=s.Tick(c,false);CHECK(a.closeStash&&!a.closeInventory);
 // Never close a stash during its durable write/reload handoff.
 c.stashOpen=c.inventoryOpen=false;c.now=200;s.Tick(c,true);c.stashOpen=c.inventoryOpen=c.paintReady=true;c.now=c.paintTick=201;s.Tick(c,false);c.marketOpen=true;s.Tick(c,false);c.marketOpen=false;c.syncing=true;CHECK(!s.Tick(c,false).closeStash&&s.Busy());c.syncing=false;CHECK(s.Tick(c,false).closeStash&&!s.Busy());
 // Requests expire and clean up only their own host; character changes drop state.
 c.stashOpen=c.inventoryOpen=c.paintReady=false;c.now=300;s.Tick(c,true);c.stashOpen=c.inventoryOpen=true;c.now=5300;a=s.Tick(c,false);CHECK(a.notice&&a.closeStash&&a.closeInventory&&!s.Busy());
 c.now=6000;c.stashOpen=false;s.Tick(c,true);c.owner=8;a=s.Tick(c,false);CHECK(!a.closeStash&&!a.closeInventory&&!s.Busy());
 c.available=false;a=s.Tick(c,true);CHECK(a.notice&&!a.openStash&&!s.Busy());c.available=true;c.allowed=false;CHECK(!s.Tick(c,true).openStash&&!s.Busy());c.allowed=true;c.syncing=true;CHECK(!s.Tick(c,true).openStash&&!s.Busy());c.syncing=false;c.playing=false;CHECK(!s.Tick(c,true).openStash&&!s.Busy());
 std::cout<<"Marketplace shortcut: gameplay/stash entry, fresh native paint, held-key coalescing, original-panel restoration, durable sync deferral, timeout and character/menu guards passed.\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
