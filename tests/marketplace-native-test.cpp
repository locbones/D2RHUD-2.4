#define wmain ExistingMarketplaceTests
#include "marketplace-test.cpp"
#undef wmain
#include "../d2rhud/plugin/MarketplacePanel.h"
#include "../d2rhud/plugin/MarketplaceLiveCustody.h"
#include "../d2rhud/d2/Reclass.h"
#include "../d2rhud/Pattern.h"
#include <map>

static D2UnitStrc player{},item{};
static unsigned gSelectedPage=16;
static bool IsHardcore(){return false;}
static D2UnitStrc* GetClientPlayerUnit(){return &player;}
static std::map<uint32_t,uintptr_t> addresses;
DWORD64 Pattern::Address(uint32_t rva){return addresses[rva];}
static std::map<std::string,void*> widgets;
static void* WidgetFindChild(void*,const char* name){return widgets[name];}
#include "../d2rhud/plugin/MarketplaceNative.inl"

template<class T>void Put(void* target,size_t at,T value){memcpy(static_cast<uint8_t*>(target)+at,&value,sizeof(value));}
static void* gridWidget=nullptr;
static uint64_t __fastcall Resolve(void* widget,uint64_t* out){const int x=widget==gridWidget?40:50,y=widget==gridWidget?200:40;return *out=uint32_t(x)|(uint64_t(uint32_t(y))<<32);}
static bool writeFailure=false;
static void* dispatchedRoot=nullptr;static void* dispatchedMessage=nullptr;
struct Replayed {uint64_t verb;uint32_t key;bool control;};static std::vector<Replayed> replayed;
static void __fastcall Dispatch(void* root,void* message){dispatchedRoot=root;dispatchedMessage=message;
    using Marketplace::NativePanel::Field;
    if(Field<uint64_t>(message)==Marketplace::Panel::MessageHash("InputMessage"))replayed.push_back({Field<uint64_t>(message,8),Field<uint32_t>(Field<void*>(message,16),16),bool(GetKeyState(VK_CONTROL)&0x8000)});
}
static unsigned opened=0,closed=0;
static void __fastcall OpenWidget(void* widget){assert(Marketplace::NativePanel::Field<uint8_t>(widget,0x50));++opened;}
static void __fastcall CloseWidget(void* widget){assert(Marketplace::NativePanel::Field<uint8_t>(widget,0x50));++closed;}
static int __fastcall Save(D2UnitStrc*,Marketplace::NativePanel::ItemBits* bits,int saved,int children,int gamble){
    assert(saved==1&&children==1&&gamble==0&&bits->bits==32768*8);const auto bytes=Unique(true);
    memcpy(bits->data,bytes.data(),bytes.size());bits->bytes=bytes.size();bits->error=writeFailure;return int(bytes.size());
}
static std::set<void*> nativeTextAllocations;static unsigned textAssignments=0;
static void* __fastcall AssignText(void* target,const char* text,uint64_t length){
    using Marketplace::NativePanel::Field;auto pointer=Field<char*>(target);const auto capacity=Field<uint64_t>(target,0x10);
    if(!(capacity>>63)){assert(nativeTextAllocations.erase(pointer)==1);assert(HeapFree(GetProcessHeap(),0,pointer));}
    auto replacement=static_cast<char*>(HeapAlloc(GetProcessHeap(),0,length+1));assert(replacement);nativeTextAllocations.insert(replacement);
    memcpy(replacement,text,length);replacement[length]=0;Put(target,0,replacement);Put(target,8,length);Put(target,0x10,length);++textAssignments;return target;
}
static void DestroyText(void* widget){
    const auto pointer=Marketplace::NativePanel::Field<void*>(widget,0x88);assert(nativeTextAllocations.erase(pointer)==1);assert(HeapFree(GetProcessHeap(),0,pointer));
}
int main(){
    using namespace Marketplace;
    std::array<uint8_t,0x600> grid{},tabs{},image{},pages{},row{};
    gridWidget=grid.data();widgets={{"grid",grid.data()},{"BankTabs",tabs.data()},{"BankPages",pages.data()}};
    void* children[]{image.data()};const char* name="Image3";
    Put(tabs.data(),0x58,children);Put(tabs.data(),0x60,uint64_t(1));Put(image.data(),8,name);image[0x90]=1;
    Put(grid.data(),0x580,int32_t(100));Put(grid.data(),0x584,int32_t(100));Put(grid.data(),0x5f0,int32_t(16));Put(grid.data(),0x5f4,int32_t(13));Put(grid.data(),0x80,.5f);
    Put(pages.data(),0x78,int32_t(408));Put(pages.data(),0x80,.5f);
    D2InventoryStrc inventory{};D2SharedInventoryStrc shared{};D2ItemDataStrc data{};D2StaticPathStrc path{};
    player.dwUnitType=0;player.dwUnitId=7;player.pInventory=&inventory;inventory.pFirstSharedInv=&shared;shared.dwUnitId=100;
    item.dwUnitType=4;item.dwUnitId=77;item.pItemData=&data;item.pStaticPath=&path;data.dwOwnerGUID=102;data.dwInitSeed=123;
    Put(&data,0xb8,uint8_t(7));path.tGameCoord={2,3};row[0x116]=2;row[0x117]=2;
    std::array<D2UnitStrc*,128> buckets{};buckets[0]=&item;auto rowPointer=row.data();uint32_t rowCount=1;
    addresses={{0x576f00,reinterpret_cast<uintptr_t>(&Resolve)},{0x1d442e0+4*0x400,reinterpret_cast<uintptr_t>(buckets.data())},
        {0x1ca0390,reinterpret_cast<uintptr_t>(&rowPointer)},{0x1ca0398,reinterpret_cast<uintptr_t>(&rowCount)}};
    const auto before=data;const auto unitBefore=item;
    auto capture=[&](){return NativePanel::Capture(tabs.data(),17,false,&player,1600,1000,{150,360});};
    auto view=capture();assert(view.owner==7&&view.page==17&&view.tab==3&&view.hovered.unit==77&&view.hovered.bytes.empty());
    assert(view.hovered.rect.Contains(150.f/1600,360.f/1000)&&view.grid.right>.52f);
    Panel::SetPin({77,123,7,17,3,false});view=capture();assert(view.pinned.unit==77);
    data.dwOwnerGUID=101;assert(!capture().hovered.unit&&!capture().pinned.unit);data.dwOwnerGUID=102;
    name="Image0";Put(image.data(),8,name);assert(!capture().hovered.unit);name="Image3";Put(image.data(),8,name);
    Put(&inventory,0x40,reinterpret_cast<void*>(1));assert(capture().cursorOccupied&&!capture().hovered.unit);Put(&inventory,0x40,static_cast<void*>(nullptr));
    Panel::Item serialized;serialized.unit=77;serialized.seed=123;NativePanel::SerializeWith(&item,serialized,Save);
    assert(serialized.bytes==Unique(true)&&ReadItem(serialized.bytes,Tables(),3).children.size()==1);
    std::array<D2SharedInventoryStrc,7> sharedTabs{};
    for(size_t n=0;n<7;++n){sharedTabs[n].dwUnitId=100+uint32_t(n);sharedTabs[n].pNextCorpse=n<6?&sharedTabs[n+1]:nullptr;}
    inventory.pFirstSharedInv=sharedTabs.data();inventory.pLastSharedInv=&sharedTabs[6];inventory.nSharedInvCount=7;
    Panel::ScanRequest request{1,7,17,false};auto deposit=NativePanel::CaptureDeposit(&player,request,Save);
    assert(deposit.error.empty()&&deposit.items.size()==1&&deposit.items[0].tab==3&&deposit.items[0].bytes==Unique(true));
    std::array<D2UnitStrc,7> goldUnits{};std::array<D2UnitStrc*,128> playerBuckets{};
    for(unsigned n=0;n<7;++n){goldUnits[n].dwUnitId=100+n;playerBuckets[100+n]=&goldUnits[n];}
    auto statStub=static_cast<uint8_t*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));assert(statStub);
    const uint8_t statBytes[]{0x48,0x83,0xec,0x28,0x45,0x0f,0xb7,0xc8,0x48,0x85,0xc9,0x74,0x42,0x48,0x8b,0x89,0,0,0,0,
        0xb8,0x09,0x03,0,0,0x48,0x83,0xc4,0x28,0xc3}; // Guarded native entry; fixture returns 777 gold per shared container.
    memcpy(statStub,statBytes,sizeof(statBytes));FlushInstructionCache(GetCurrentProcess(),statStub,sizeof(statBytes));
    addresses[0x1e5620]=reinterpret_cast<uintptr_t>(statStub);addresses[0x1d442e0]=reinterpret_cast<uintptr_t>(playerBuckets.data());
    auto goldDeposit=NativePanel::CaptureDeposit(&player,request,Save);assert(goldDeposit.goldValid&&goldDeposit.gold==7*777);
    playerBuckets[106]=nullptr;assert(!NativePanel::CaptureDeposit(&player,request,Save).goldValid);
    addresses.erase(0x1e5620);addresses.erase(0x1d442e0);VirtualFree(statStub,0,MEM_RELEASE);
    inventory.nSharedInvCount=6;assert(!NativePanel::CaptureDeposit(&player,request,Save).error.empty());inventory.nSharedInvCount=7;
    sharedTabs[6].pNextCorpse=&sharedTabs[0];assert(!NativePanel::CaptureDeposit(&player,request,Save).error.empty());sharedTabs[6].pNextCorpse=nullptr;
    sharedTabs[4].dwUnitId=102;assert(!NativePanel::CaptureDeposit(&player,request,Save).error.empty());sharedTabs[4].dwUnitId=104;
    Put(&inventory,0x40,reinterpret_cast<void*>(1));assert(!NativePanel::CaptureDeposit(&player,request,Save).error.empty());Put(&inventory,0x40,static_cast<void*>(nullptr));
    const auto sequence=Panel::RequestScan(7,17,false);Panel::PublishScan({{sequence+1,7,17,false},{},"wrong request"});assert(!Panel::TakeScan(sequence).request.sequence);
    deposit.request.sequence=sequence;Panel::PublishScan(std::move(deposit));assert(Panel::TakeScan(sequence).items.size()==1&&!Panel::TakeScan(sequence).request.sequence);
    writeFailure=true;NativePanel::SerializeWith(&item,serialized,Save);assert(serialized.bytes.empty());
    assert(!memcmp(&before,&data,sizeof(data))&&!memcmp(&unitBefore,&item,sizeof(item)));
    auto window=CreateWindowExW(0,L"STATIC",L"Marketplace input fixture",WS_POPUP,0,0,1000,800,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(window);
    view.tick=GetTickCount64();Panel::Publish(view);Panel::Regions({{.05f,.05f,.25f,.25f}},false,GetTickCount64());
    assert(!Panel::HandleInput(window,WM_KEYDOWN,'A',0));
    assert(Panel::HandleInput(window,WM_LBUTTONDOWN,0,MAKELPARAM(100,100)));
    assert(Panel::HandleInput(window,WM_MOUSEMOVE,0,MAKELPARAM(900,700)));
    assert(Panel::HandleInput(window,WM_LBUTTONUP,0,MAKELPARAM(900,700)));
    assert(!Panel::HandleInput(window,WM_LBUTTONDOWN,0,MAKELPARAM(900,700)));
    Panel::Regions({{.05f,.05f,.25f,.25f}},true,GetTickCount64());
    assert(Panel::KeyboardActive()&&Panel::HandleInput(window,WM_CHAR,'z',0));
    assert(Panel::HandleInput(window,WM_KEYDOWN,VK_ESCAPE,0)&&Panel::dismiss.exchange(false));
    bool outside=false;
    assert(!Panel::HandleInput(window,WM_LBUTTONDOWN,0,MAKELPARAM(900,700),&outside)&&outside&&Panel::dismiss.exchange(false));
    Panel::Publish({});assert(!Panel::KeyboardActive()&&!Panel::HandleInput(window,WM_CHAR,'z',0));
    Panel::Hide();DestroyWindow(window);
    // Native JSON controls use a message queue and stable text/input storage.
    std::array<uint8_t,0x700> nativeRoot{},nativeLabel{},nativeInput{},nativeShop{},nativeClose{},foreignButton{};
    std::array<uint8_t,0x40> rootVtable{};Put(rootVtable.data(),0x30,reinterpret_cast<uintptr_t>(&OpenWidget));Put(rootVtable.data(),0x38,reinterpret_cast<uintptr_t>(&CloseWidget));Put(nativeRoot.data(),0,rootVtable.data());
    std::array<uint8_t,0xe40> nativeTabs{};std::array<uint8_t,0x28> tabsVtable{};
    std::array<uint8_t,18> tabsCode{0x48,0x83,0xbb,0xf8,0x0a,0,0,0,0x74,0x36,0x48,0x83,0xbb,0,0x0b,0,0,0};
    addresses[0x57e2e8]=reinterpret_cast<uintptr_t>(tabsCode.data());addresses[0x57da20]=1;
    addresses[0x576960]=reinterpret_cast<uintptr_t>(&Dispatch);
    Put(tabsVtable.data(),0x20,uintptr_t(1));Put(nativeTabs.data(),0,tabsVtable.data());Put(nativeTabs.data(),0x520,int(4));
    for(size_t n=0;n<4;++n)Put(nativeTabs.data(),0xb18+n*0x20,Panel::MessageHash("BankPanelMessage"));
    std::array<uint8_t,0xb0> buttonVtable{};
    std::array<uint8_t,18> clickCode{0x48,0x83,0xb9,0x20,0x05,0,0,0,0x74,0x16,0x48,0x83,0xb9,0x28,0x05,0,0,0};
    addresses[0x580310]=reinterpret_cast<uintptr_t>(clickCode.data());
    Put(buttonVtable.data(),0xa8,reinterpret_cast<uintptr_t>(clickCode.data()));
    for(auto widget:{nativeShop.data(),nativeClose.data(),foreignButton.data()}){
        Put(widget,0,buttonVtable.data());Put(widget,0x520,Panel::MessageHash("BankPanelMessage"));
    }
    std::array<uint8_t,4> drawGuard{0x80,0x7f,0x51,0},inputGuard{0x80,0x7f,0x50,0};
    addresses[0x5768b3]=reinterpret_cast<uintptr_t>(drawGuard.data());addresses[0x5769ff]=reinterpret_cast<uintptr_t>(inputGuard.data());
    std::array<uintptr_t,4> textVtable{};addresses[0x15acb98]=reinterpret_cast<uintptr_t>(textVtable.data());
    addresses[0x57acf0]=123;addresses[0x57b700]=456;textVtable[0]=123;textVtable[3]=456;
    std::array<uint8_t,11> stringStore{0xc6,0x04,0x03,0,0x48,0x8b,0xc7,0x48,0x89,0x5f,0x08};
    std::array<uint8_t,15> textDestructor{0x48,0x83,0xb9,0x98,0,0,0,0,0x8b,0xf2,0x48,0x8b,0xf9,0x7c,0x23};
    addresses[0x6232c]=reinterpret_cast<uintptr_t>(stringStore.data());addresses[0x57acfa]=reinterpret_cast<uintptr_t>(textDestructor.data());
    addresses[0x622d0]=reinterpret_cast<uintptr_t>(&AssignText);Put(nativeLabel.data(),0,textVtable.data());
    Put(nativeLabel.data(),0x88,reinterpret_cast<char*>(nativeLabel.data()+0xa0));Put(nativeLabel.data(),0x98,uint64_t(0x800000000000000fULL));
    nativeRoot[0xb8]=0xab;nativeLabel[0xb8]=0xcd;
    widgets["MarketplaceWorkspace"]=nativeRoot.data();widgets["MarketStatus"]=nativeLabel.data();widgets["DepositPage"]=nativeInput.data();
    widgets["MarketplaceShop"]=nativeShop.data();widgets["MarketCloseButton"]=nativeClose.data();
    widgets["MarketSections"]=nativeTabs.data();
    std::array<uint8_t,0x700> nativeSetup{};nativeSetup[0x50]=nativeSetup[0x51]=nativeInput[0x50]=nativeInput[0x51]=1;
    Put(nativeSetup.data(),0x30,nativeRoot.data());Put(nativeInput.data(),0x30,nativeSetup.data());widgets["MarketSetupGroup"]=nativeSetup.data();
    Put(nativeClose.data(),0x528,Panel::MessageHash("Close")); // Use the stock JSON action.
    const std::array<int32_t,4> originalHit{2,3,4,5};memcpy(tabs.data()+0x38,originalHit.data(),sizeof(originalHit));
    // Replay the actual JSON loader failure: valid domain, zero custom action.
    // Binding must happen while the marketplace is still closed.
    Panel::Present({});UpdateMarketplaceWidgets(tabs.data());
    const auto domain=NativePanel::Field<uint64_t>(nativeShop.data(),0x520);
    const auto action=NativePanel::Field<uint64_t>(nativeShop.data(),0x528);
    assert(domain==Panel::MessageHash("BankPanelMessage")&&action==Panel::MessageHash("MarketOpen"));
    assert(Panel::NativeMessage(domain,action)&&Panel::TakeNativeActions()==std::vector<uint64_t>{action});
    assert(NativePanel::Field<uint64_t>(nativeClose.data(),0x528)==Panel::MessageHash("Close"));
    assert(!Panel::NativeMessage(domain,Panel::MessageHash("Close")));
    const std::array<const char*,4> tabCommands{"MarketBrowse","MarketItems","MarketAgreements","MarketSetup"};
    for(size_t n=0;n<4;++n){const auto command=NativePanel::Field<uint64_t>(nativeTabs.data(),0xb20+n*0x20);
        assert(command==Panel::MessageHash(tabCommands[n])&&Panel::NativeMessage(domain,command));}
    assert(Panel::TakeNativeActions().size()==4);
    Put(foreignButton.data(),0x528,Panel::MessageHash("Close"));
    assert(!NativePanel::BindButton(foreignButton.data(),action)&&NativePanel::Field<uint64_t>(foreignButton.data(),0x528)==Panel::MessageHash("Close"));
    Put(foreignButton.data(),0x528,uint64_t(0));Put(foreignButton.data(),0x520,uint64_t(0));
    assert(!NativePanel::BindButton(foreignButton.data(),action));
    assert(!NativePanel::BindButton(nativeLabel.data(),action));
    Put(nativeInput.data(),0x530,uint64_t(0x8000000000000000ULL|15));
    Panel::Present({true,{{"MarketStatus","Native marketplace text",true,false,0},{"DepositPage","17",true,true,1}},3});
    UpdateMarketplaceWidgets(tabs.data());
    assert(opened==1&&closed==0);
    assert(textAssignments==1&&nativeTextAllocations.size()==1&&NativePanel::Field<uint64_t>(nativeLabel.data(),0x90)==23);
    UpdateMarketplaceWidgets(tabs.data());assert(textAssignments==1); // No native allocation on an unchanged paint.
    const auto longText=std::string(2048,'x');assert(NativePanel::Text::Bind(nativeLabel.data(),longText));assert(nativeTextAllocations.size()==1);
    assert(NativePanel::Text::Bind(nativeLabel.data(),"Native marketplace text"));assert(nativeTextAllocations.size()==1);
    assert(!NativePanel::Text::Bind(nativeShop.data(),"foreign button"));
    assert(!NativePanel::Text::Bind(nativeLabel.data(),std::string(4096,'x')));

    assert(NativePanel::Field<int>(nativeTabs.data(),0xd68)==3);
    assert(NativePanel::Field<int32_t>(tabs.data(),0x38)==32768);
    uint64_t message=0;assert(NativePanel::DispatchInput(tabs.data(),0x4ae067c6248b6042ULL,&message)&&dispatchedRoot==nativeRoot.data()&&dispatchedMessage==&message);
    assert(!NativePanel::DispatchInput(tabs.data(),domain,&message));
    // Saving and closing can arrive before the next render tick. Close must
    // not erase a user's already submitted asking price.
    assert(Panel::NativeMessage(domain,Panel::MessageHash("MarketSavePrice")));
    assert(Panel::NativeMessage(domain,Panel::MessageHash("Close")));
    assert(Panel::TakeNativeActions()==std::vector<uint64_t>({Panel::MessageHash("MarketSavePrice"),Panel::MessageHash("MarketClose")}));
    for(int n=0;n<70;++n)Panel::NativeMessage(domain,Panel::MessageHash("MarketRefresh"));
    assert(Panel::NativeMessage(domain,Panel::MessageHash("Close")));
    const auto saturated=Panel::TakeNativeActions();assert(saturated.size()==65&&saturated.back()==Panel::MessageHash("MarketClose"));
    assert(std::all_of(saturated.begin(),saturated.end()-1,[](auto action){return action==Panel::MessageHash("MarketRefresh");}));
    assert(nativeRoot[0x50]==1&&nativeRoot[0x51]==1&&nativeRoot[0xb8]==0xab&&nativeLabel[0xb8]==0xcd&&nativeLabel[0x50]==0&&std::string(NativePanel::Field<char*>(nativeLabel.data(),0x88))=="Native marketplace text");
    assert(NativePanel::Field<uint64_t>(nativeInput.data(),0x528)==2&&Panel::ReadNativeInputs().at("DepositPage").text=="17");
    memcpy(nativeInput.data()+0x538,"18",3);UpdateMarketplaceWidgets(tabs.data());assert(Panel::ReadNativeInputs().at("DepositPage").text=="18"); // no per-frame clobber
    Panel::Present({true,{{"DepositPage","19",true,true,2}}});UpdateMarketplaceWidgets(tabs.data());assert(Panel::ReadNativeInputs().at("DepositPage").text=="19");
    Panel::Present({true,{{"MarketSetupGroup",{},false},{"DepositPage","19",true,true,2}}});UpdateMarketplaceWidgets(tabs.data());
    assert(!Panel::ReadNativeInputs().contains("DepositPage"));
    Put(nativeInput.data(),0x528,uint64_t(0));nativeInput[0x538]=0; // Native lifecycle can reset a closed field.
    Panel::Present({true,{{"MarketSetupGroup",{},true},{"DepositPage","19",true,true,2}}});UpdateMarketplaceWidgets(tabs.data());
    assert(Panel::ReadNativeInputs().at("DepositPage").text=="19"); // Same revision must be restored after showing it.
    assert(!Panel::NativeMessage(Panel::MessageHash("BankPanelMessage"),Panel::MessageHash("SelectTab")));
    assert(!Panel::NativeMessage(0,Panel::MessageHash("MarketOpen")));
    assert(Panel::NativeMessage(Panel::MessageHash("BankPanelMessage"),Panel::MessageHash("MarketOpen"))&&Panel::TakeNativeActions().size()==1);
    std::array<uint8_t,0x800> messageInput{};std::array<uintptr_t,5> messageVtable{};
    addresses[0x58e0c0]=0xabc;messageVtable[4]=0xabc;Put(messageInput.data(),0,messageVtable.data());Put(messageInput.data(),0x30,nativeRoot.data());messageInput[0x50]=messageInput[0x51]=1;
    Put(messageInput.data(),0x520,reinterpret_cast<char*>(messageInput.data()+0x538));Put(messageInput.data(),0x530,uint64_t(0x800000000000000fULL));widgets["MarketOfferMessage"]=messageInput.data();
    std::array<uint8_t,0x100> chartBar{};widgets["MarketChartBar0"]=chartBar.data();
    const auto longMessage=std::string(350,'m');Panel::NativeView longInputView{true,{{"MarketOfferMessage",longMessage,true,true,1}}};longInputView.rectangles.push_back({"MarketChartBar0",1235,1050,12,122});Panel::Present(longInputView);UpdateMarketplaceWidgets(tabs.data());
    assert(Panel::ReadNativeInputs().at("MarketOfferMessage").text==longMessage&&NativePanel::Field<uint64_t>(messageInput.data(),0x528)==350);
    assert(NativePanel::Field<int32_t>(chartBar.data(),0x70)==1235&&NativePanel::Field<int32_t>(chartBar.data(),0x7c)==122);
    assert(!NativePanel::Text::BindInput(messageInput.data(),std::string(2049,'x')));
    auto inputAllocation=NativePanel::Field<char*>(messageInput.data(),0x520);inputAllocation[0]='X';UpdateMarketplaceWidgets(tabs.data());assert(Panel::ReadNativeInputs().at("MarketOfferMessage").text.front()=='X');
    assert(nativeTextAllocations.erase(inputAllocation)==1);HeapFree(GetProcessHeap(),0,inputAllocation);widgets.erase("MarketOfferMessage");
    std::array<uint8_t,17> keyCode{0x48,0x8b,0x46,0x08,0x48,0xb9,0xd8,0xf9,0x6a,0x28,0x21,0x9a,0x6c,0x0c,0x48,0x3b,0xc1};
    std::array<uint8_t,17> characterCode{0x41,0x0f,0xb7,0x46,0x10};
    addresses[0x58e482]=reinterpret_cast<uintptr_t>(keyCode.data());addresses[0x58e4d6]=reinterpret_cast<uintptr_t>(characterCode.data());
    assert(Panel::NativeWindowInput(WM_KEYDOWN,'C',0));assert(Panel::NativeWindowInput(WM_CHAR,'c',0));assert(Panel::NativeWindowInput(WM_KEYUP,'C',0));
    std::array<BYTE,256> previousKeys{};GetKeyboardState(previousKeys.data());
    auto controlKeys=previousKeys;controlKeys[VK_CONTROL]=0x80;SetKeyboardState(controlKeys.data());
    assert(Panel::NativeWindowInput(WM_KEYDOWN,'A',0));SetKeyboardState(previousKeys.data());
    NativePanel::ReplayKeyboard(nativeRoot.data());
    assert(replayed.size()==4&&replayed[0].verb==Panel::MessageHash("KeyDown")&&replayed[0].key=='C');
    assert(replayed[1].verb==0xb6b6f0a8c4aa4214ULL&&replayed[1].key=='c'&&replayed[2].verb==Panel::MessageHash("KeyUp"));
    assert(replayed[3].control&&bool(GetKeyState(VK_CONTROL)&0x8000)==bool(previousKeys[VK_CONTROL]&0x80));
    assert(Panel::NativeWindowInput(WM_KEYDOWN,VK_ESCAPE,0));assert(Panel::TakeNativeActions()==std::vector<uint64_t>{Panel::MessageHash("MarketClose")});
    assert(!Panel::NativeWindowInput(WM_SYSKEYDOWN,VK_F4,LPARAM(1)<<29)&&!Panel::NativeWindowInput(WM_MOUSEMOVE,0,0));
    Panel::Present({});assert(Panel::NativeWindowInput(WM_KEYUP,'A',0));assert(Panel::NativeWindowInput(WM_KEYUP,VK_ESCAPE,0));
    assert(!Panel::NativeWindowInput(WM_KEYDOWN,'C',0)); // No capture after closing.
    Panel::Present({});UpdateMarketplaceWidgets(tabs.data());assert(nativeRoot[0x50]==0&&nativeRoot[0x51]==0&&nativeRoot[0xb8]==0xab);
    assert(opened==1&&closed==1);UpdateMarketplaceWidgets(tabs.data());assert(closed==1);
    assert(!memcmp(tabs.data()+0x38,originalHit.data(),sizeof(originalHit)));
    assert(!NativePanel::DispatchInput(tabs.data(),0x4ae067c6248b6042ULL,&message));
    assert(!Panel::NativeMessage(domain,Panel::MessageHash("Close")));
    Put(nativeTabs.data(),0x520,int(8));assert(!NativePanel::TabsSupported(nativeTabs.data()));
    DestroyText(nativeLabel.data());assert(nativeTextAllocations.empty()); // Native teardown only frees its own allocations.
    std::cout<<"Native stash capture and binding: native tab messages/selection, modal input dispatch, restored bank hit margins, ordered save/close actions and bounded close queue, stock close action, zero-action binding, foreign-control guards, text/input revisions and source preservation passed.\n";
}
