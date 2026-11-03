#define NOMINMAX
#include "../d2rhud/plugin/MarketplacePanel.h"
#include <cassert>
#include <iostream>
int main(){
    using namespace Marketplace::Panel;POINT mouse{};assert(GetCursorPos(&mouse));
    // An invisible local test control; no cursor movement or input is sent.
    const auto window=CreateWindowExW(0,L"STATIC",L"market-input-test",WS_POPUP,mouse.x-100,mouse.y-100,200,200,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(window);
    Hide();Frame f;f.tick=GetTickCount64();f.owner=7;f.page=11;f.tab=3;f.hovered.unit=55;f.hovered.bytes={1,2};f.hovered.rect={0,0,1,1};Publish(f);
    auto click=[&]{return WithdrawalWindowInput(window,WM_LBUTTONDOWN,0,MAKELPARAM(100,100));};
    WatchWithdrawals(8,11,false,{{3,"0102","0304"}});assert(!click());
    WatchWithdrawals(7,12,false,{{3,"0102","0304"}});assert(!click());
    WatchWithdrawals(7,11,true,{{3,"0102","0304"}});assert(!click());
    WatchWithdrawals(7,11,false,{{4,"0102","0304"}});assert(!click());
    WatchWithdrawals(7,11,false,{{3,"0102","0304"}});f.cursorOccupied=true;Publish(f);assert(!click());f.cursorOccupied=false;
    f.tick=GetTickCount64()-1000;Publish(f);assert(!click());f.tick=GetTickCount64();Publish(f);
    assert(click());const auto request=TakeWithdrawal();assert(request.sequence&&request.savedHex=="0102"&&request.context.hovered.unit==55);assert(!TakeWithdrawal().sequence);
    assert(WithdrawalWindowInput(window,WM_LBUTTONUP,0,0));
    f.hovered.bytes={3,4};Publish(f);assert(click());assert(TakeWithdrawal().savedHex=="0102");assert(WithdrawalWindowInput(window,WM_LBUTTONUP,0,0));
    NativeView view;view.open=true;Present(view);assert(!click());Hide();DestroyWindow(window);
    std::cout<<"Native withdrawal input: exact saved/legacy bytes, owner/page/tab/mode isolation, stale frames, cursor safety, held click/release and value-only prompt requests passed.\n";
}
