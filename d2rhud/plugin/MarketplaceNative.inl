// Included by D2RHUD.cpp after its native widget/player helpers. All calls run
// within the existing bank-panel paint hook; nothing is inserted into inventory.
#include <set>
namespace Marketplace::NativePanel {
template<class T>bool Read(const void* source,T& result){SIZE_T count=0;return source&&ReadProcessMemory(GetCurrentProcess(),source,&result,sizeof(T),&count)&&count==sizeof(T);}
template<class T>T Field(const void* source,size_t offset=0){T result{};if(source)Read(static_cast<const uint8_t*>(source)+offset,result);return result;}
inline float Scale(void* widget){
    float result=1;
    for(unsigned depth=0;widget&&depth<16;++depth,widget=Field<void*>(widget,0x30)){
        const auto scale=Field<float>(widget,0x80);if(scale>.001f&&scale<10)result*=scale;
    }
    return result;
}
inline int Tab(void* panel){
    const auto tabs=WidgetFindChild(panel,"BankTabs");if(!tabs)return -1;
    const auto children=Field<void**>(tabs,0x58);const auto count=Field<uint64_t>(tabs,0x60);if(!children||count>256)return -1;
    for(size_t i=0;i<count;++i){
        auto child=Field<void*>(children,i*sizeof(void*));auto name=Field<const char*>(child,8);std::array<char,7> text{};
        if(!Read(name,text)||memcmp(text.data(),"Image",5)||text[5]<'0'||text[5]>'7'||text[6])continue;
        if(Field<uint8_t>(child,0x90)==1)return text[5]-'0';
    }
    return -1;
}
struct ItemBits {uint8_t* data;uint64_t bits,bytes=0,bit=0;uint32_t error=0,padding=0;};
static_assert(sizeof(ItemBits)==0x28);
inline bool SerializerSupported(){
    static const bool supported=[](){
        std::array<uint8_t,32> code{};
        if(!Read(reinterpret_cast<void*>(Pattern::Address(0x207610)),code))return false;
        uint64_t hash=14695981039346656037ULL;for(auto b:code){hash^=b;hash*=1099511628211ULL;}
        return hash==0x1d0b5b0bcd8efc2aULL;
    }();return supported;
}
using SaveItem=int(__fastcall*)(D2UnitStrc*,ItemBits*,int,int,int);
inline void SerializeWith(D2UnitStrc* unit,Panel::Item& item,SaveItem save){
    item.bytes.clear();
    std::vector<uint8_t> bytes(32768);ItemBits bits{bytes.data(),bytes.size()*8};
    save(unit,&bits,1,1,0);
    const auto size=bits.bytes+(bits.bit?1:0);
    if(bits.error||!size||size>bytes.size())return;
    const auto data=Field<D2ItemDataStrc*>(unit,0x10);
    if(Field<uint32_t>(unit,8)!=item.unit||Field<uint32_t>(data,0x10)!=item.seed)return;
    bytes.resize(size);item.bytes=std::move(bytes);
}
inline void Serialize(D2UnitStrc* unit,Panel::Item& item){
    if(SerializerSupported())SerializeWith(unit,item,reinterpret_cast<SaveItem>(Pattern::Address(0x207610)));
}
inline bool WidgetLayoutSupported(){
    // Common Widget::Draw and Widget::OnMessage gate children at +0x51
    // (draw) and +0x50 (messages). Reclass's panel-specific +0xb8 field
    // does not apply to generic Rectangle/TextBox/Button widgets.
    static const bool supported=[](){std::array<uint8_t,4> draw{},input{};
        return Read(reinterpret_cast<void*>(Pattern::Address(0x5768b3)),draw)&&Read(reinterpret_cast<void*>(Pattern::Address(0x5769ff)),input)&&
            draw==std::array<uint8_t,4>{0x80,0x7f,0x51,0}&&input==std::array<uint8_t,4>{0x80,0x7f,0x50,0};}();return supported;
}
inline void Visibility(void* widget,bool shown,bool messages=true){
    const auto enabled=Field<uint8_t>(widget,0x50)!=0;
    auto lifecycle=[&](size_t slot){const auto table=Field<void*>(widget);const auto call=Field<uintptr_t>(table,slot);
        if(call)reinterpret_cast<void(__fastcall*)(void*)>(call)(widget);};
    // A hidden native subtree must release keyboard focus and pressed states.
    // Raw flag changes alone bypass InputTextBox/Button OnClose behavior.
    if(messages&&enabled&&!shown)lifecycle(0x38);
    const uint8_t value=shown?1:0;SIZE_T count=0;
    WriteProcessMemory(GetCurrentProcess(),static_cast<uint8_t*>(widget)+0x51,&value,1,&count);
    if(messages){WriteProcessMemory(GetCurrentProcess(),static_cast<uint8_t*>(widget)+0x50,&value,1,&count);
        if(!enabled&&shown)lifecycle(0x30);}
}
inline bool VisibleIn(void* widget,void* root){
    for(unsigned depth=0;widget&&depth<32;++depth,widget=Field<void*>(widget,0x30)){
        if(!Field<uint8_t>(widget,0x50)||!Field<uint8_t>(widget,0x51))return false;
        if(widget==root)return true;
    }return false;
}
inline bool BindButton(void* widget,uint64_t command){
    // 2.4's JSON message resolver recognizes BankPanelMessage but resolves
    // unregistered Market* actions to zero. ButtonWidget::OnClick explicitly
    // ignores either zero hash. Bind our actions on the native controls;
    // native hit testing, pressed/hover states, sound and dispatch stay intact.
    const auto table=Field<void*>(widget);
    if(!table||Field<uintptr_t>(table,0xa8)!=Pattern::Address(0x580310)||
        Field<uint64_t>(widget,0x520)!=Panel::MessageHash("BankPanelMessage"))return false;
    static const bool supported=[](){std::array<uint8_t,18> code{};
        return Read(reinterpret_cast<void*>(Pattern::Address(0x580310)),code)&&code==std::array<uint8_t,18>{
            0x48,0x83,0xb9,0x20,0x05,0,0,0,0x74,0x16,0x48,0x83,0xb9,0x28,0x05,0,0,0};}();
    if(!supported)return false;
    const auto previous=Field<uint64_t>(widget,0x528);
    if(previous==command)return true;
    if(previous)return false; // Never replace another native action.
    SIZE_T count=0;return WriteProcessMemory(GetCurrentProcess(),static_cast<uint8_t*>(widget)+0x528,&command,sizeof(command),&count)&&count==sizeof(command);
}
inline void BindButtons(void* panel,void* root){
    BindButton(WidgetFindChild(panel,"MarketplaceShop"),Panel::MessageHash("MarketOpen"));
    for(const auto& command:Panel::NativeCommands()){
        if(command=="MarketOpen")continue;
        auto widget=WidgetFindChild(root,command.c_str());
        if(!widget)widget=WidgetFindChild(root,(command+"Button").c_str());
        BindButton(widget,Panel::MessageHash(command));
    }
}
inline bool TabsSupported(void* tabs){
    const auto table=Field<void*>(tabs);
    if(!table||Field<uintptr_t>(table,0x20)!=Pattern::Address(0x57da20)||Field<int>(tabs,0x520)!=4)return false;
    static const bool supported=[](){std::array<uint8_t,18> code{};
        return Read(reinterpret_cast<void*>(Pattern::Address(0x57e2e8)),code)&&code==std::array<uint8_t,18>{
            0x48,0x83,0xbb,0xf8,0x0a,0,0,0,0x74,0x36,0x48,0x83,0xbb,0,0x0b,0,0,0};}();
    return supported;
}
inline void BindTabs(void* root,int section){
    auto tabs=WidgetFindChild(root,"MarketSections");if(!TabsSupported(tabs))return;
    // TabBarWidget owns selection, frames and text colors. Its four native
    // tabMessages use 0x20-byte records, dispatched by SetSelection (2.4).
    const std::array<const char*,4> commands{"MarketBrowse","MarketItems","MarketAgreements","MarketSetup"};
    for(size_t n=0;n<commands.size();++n){
        const auto offset=0xb18+n*0x20;
        if(Field<uint64_t>(tabs,offset)!=Panel::MessageHash("BankPanelMessage"))continue;
        const auto previous=Field<uint64_t>(tabs,offset+8),command=Panel::MessageHash(commands[n]);
        if(!previous){SIZE_T count=0;WriteProcessMemory(GetCurrentProcess(),static_cast<uint8_t*>(tabs)+offset+8,&command,sizeof(command),&count);}
    }
    if(section>=0&&section<4){SIZE_T count=0;WriteProcessMemory(GetCurrentProcess(),static_cast<uint8_t*>(tabs)+0xd68,&section,sizeof(section),&count);}
}
inline bool DispatchInput(void* panel,uint64_t domain,void* message){
    // The BankPanel has its own Escape/hotkey processing before its children.
    // A modal marketplace must receive input before that stash behavior.
    if(domain!=0x4ae067c6248b6042ULL||!Panel::NativeOpen()||!WidgetLayoutSupported())return false;
    auto root=WidgetFindChild(panel,"MarketplaceWorkspace");if(!root||!Field<uint8_t>(root,0x50))return false;
    reinterpret_cast<void(__fastcall*)(void*,void*)>(Pattern::Address(0x576960))(root,message);return true;
}
inline void ModalHitArea(void* panel,bool marketOpen){
    struct Saved {void* panel=nullptr;void* root=nullptr;bool active=false;std::array<int32_t,4> hit{};};
    static Saved saved;
    const auto root=WidgetFindChild(panel,"MarketplaceWorkspace");
    if(saved.panel!=panel||saved.root!=root){saved={};saved.panel=panel;saved.root=root;}
    auto write=[&](const std::array<int32_t,4>& value){SIZE_T count=0;return WriteProcessMemory(GetCurrentProcess(),static_cast<uint8_t*>(panel)+0x38,value.data(),sizeof(value),&count)&&count==sizeof(value);};
    if(marketOpen&&!saved.active){
        // The full-screen child extends beyond the bank's normal hit area.
        // Only expand hit margins; bank/grid geometry and lifecycle stay intact.
        if(Read(static_cast<uint8_t*>(panel)+0x38,saved.hit))saved.active=write({32768,32768,32768,32768});
    }else if(!marketOpen&&saved.active&&write(saved.hit))saved.active=false;
}
#include "MarketplaceNativeKeyboard.inl"
// One on-demand, read-only capture of all seven loaded shared containers. The
// paint hook is the only caller; no game pointers cross to the render thread.
inline Panel::ScanResult CaptureDeposit(D2UnitStrc* player,Panel::ScanRequest request,SaveItem save){
    Panel::ScanResult result;result.request=request;
    auto fail=[&](const char* why){result.items.clear();result.error=why;return result;};
    D2UnitStrc local{};D2InventoryStrc inventory{};
    if(!save||!Read(player,local)||local.dwUnitType||local.dwUnitId!=request.owner||!Read(local.pInventory,inventory)||
        inventory.nSharedInvCount!=7||Field<void*>(local.pInventory,0x40))return fail("All seven deposit tabs must be loaded and the cursor empty.");
    std::array<uint32_t,7> containers{};auto node=inventory.pFirstSharedInv;
    for(size_t n=0;n<7;++n){
        D2SharedInventoryStrc value{};if(!Read(node,value)||!value.dwUnitId||std::find(containers.begin(),containers.begin()+n,value.dwUnitId)!=containers.begin()+n)
            return fail("The loaded deposit containers could not be verified.");
        containers[n]=value.dwUnitId;if(n==6&&(node!=inventory.pLastSharedInv||value.pNextCorpse))return fail("The loaded deposit tab chain is incomplete.");node=value.pNextCorpse;
    }
    // BankPanel selects a shared player-type container from the client player
    // hash (2.4 BankPanel::UpdateGrid, 0x18f240). GoldBank is stat 15 on that
    // container, not carried gold or the character's personal stash.
    std::array<uint8_t,16> statCode{};
    if(Read(reinterpret_cast<void*>(Pattern::Address(0x1e5620)),statCode)&&statCode==std::array<uint8_t,16>{
        0x48,0x83,0xec,0x28,0x45,0x0f,0xb7,0xc8,0x48,0x85,0xc9,0x74,0x42,0x48,0x8b,0x89}){
        uint64_t gold=0;unsigned found=0;
        for(const auto id:containers){
            auto unit=Field<D2UnitStrc*>(reinterpret_cast<void*>(Pattern::Address(0x1d442e0)),(id&127)*sizeof(void*));
            for(unsigned n=0;unit&&n<256;++n){
                D2UnitStrc value{};if(!Read(unit,value))break;
                if(!value.dwUnitType&&value.dwUnitId==id){
                    const auto amount=reinterpret_cast<int32_t(__fastcall*)(D2UnitStrc*,uint32_t,uint16_t)>(Pattern::Address(0x1e5620))(unit,15,0);
                    if(amount>=0){gold+=uint32_t(amount);++found;}break;
                }
                if(value.pListNext==unit)break;unit=value.pListNext;
            }
        }
        if(found==7){result.gold=gold;result.goldValid=true;}
    }
    std::set<uint32_t> ids;size_t visited=0,totalBytes=0;
    for(unsigned bucket=0;bucket<128;++bucket){
        auto unit=Field<D2UnitStrc*>(reinterpret_cast<void*>(Pattern::Address(0x1d442e0+4*0x400)),bucket*sizeof(void*));
        while(unit){
            D2UnitStrc value{};if(++visited>20000||!Read(unit,value))return fail("The loaded deposit item list could not be read safely.");
            if(value.dwUnitType==4&&value.dwItemMode==0&&value.pItemData&&Field<uint8_t>(value.pItemData,0xb8)==7){
                const auto owner=Field<uint32_t>(value.pItemData,0xc);const auto at=std::find(containers.begin(),containers.end(),owner);
                if(at!=containers.end()){
                    if(result.items.size()>=4096||!ids.insert(value.dwUnitId).second)return fail("Duplicate or excessive deposit items.");
                    Panel::Item item;item.unit=value.dwUnitId;item.seed=Field<uint32_t>(value.pItemData,0x10);item.tab=unsigned(at-containers.begin()+1);
                    SerializeWith(unit,item,save);totalBytes+=item.bytes.size();
                    if(item.bytes.empty()||totalBytes>8*1024*1024)return fail("A loaded deposit item could not be serialized safely.");
                    result.items.push_back(std::move(item));
                }
            }
            if(value.pListNext==unit)return fail("Invalid deposit item chain.");unit=value.pListNext;
        }
    }
    return result;
}
inline Panel::Frame Capture(void* panel,unsigned page,bool hardcore,D2UnitStrc* player,int width,int height,POINT mouse){
    Panel::Frame view;view.tick=GetTickCount64();view.page=page;view.hardcore=hardcore;
    if(!panel||!player||page<1||page>64||width<=0||height<=0)return {};
    D2UnitStrc local{};if(!Read(player,local)||local.dwUnitType||!local.pInventory)return {};
    view.owner=local.dwUnitId;
    const auto tab=Tab(panel);if(tab<0)return {};view.tab=unsigned(tab);
    auto grid=WidgetFindChild(panel,"grid");if(!grid)return {};
    const auto columns=Field<int32_t>(grid,0x5f0),rows=Field<int32_t>(grid,0x5f4);
    const float scale=Scale(grid);const auto cellW=Field<int32_t>(grid,0x580)*scale,cellH=Field<int32_t>(grid,0x584)*scale;
    if(columns<=0||columns>64||rows<=0||rows>64||cellW<1||cellH<1)return {};
    uint64_t xy=0;
    reinterpret_cast<uint64_t(__fastcall*)(void*,uint64_t*)>(Pattern::Address(0x576f00))(grid,&xy);
    const auto x=int32_t(xy),y=int32_t(xy>>32);
    view.grid={float(x)/width,float(y)/height,(x+columns*cellW)/width,(y+rows*cellH)/height};
    view.cursorOccupied=Field<void*>(local.pInventory,0x40)!=nullptr;
    if(!tab||view.cursorOccupied)return view;
    const auto first=Field<void*>(local.pInventory,0x68);const auto base=Field<uint32_t>(first);
    if(!base||base>UINT32_MAX-7)return {};
    const auto container=base+tab-1;
    const auto table=Field<const uint8_t*>(reinterpret_cast<void*>(Pattern::Address(0x1ca0390)));
    const auto count=Field<uint32_t>(reinterpret_cast<void*>(Pattern::Address(0x1ca0398)));
    if(!table||count>100000)return {};
    const auto pin=Panel::Pinned();size_t visited=0;
    for(unsigned bucket=0;bucket<128;++bucket){
        auto unit=Field<D2UnitStrc*>(reinterpret_cast<void*>(Pattern::Address(0x1d442e0+4*0x400)),bucket*sizeof(void*));
        while(unit&&++visited<=20000){
            D2UnitStrc value{};if(!Read(unit,value))return {};
            const auto next=value.pListNext;
            if(value.dwUnitType==4&&value.dwItemMode==0&&value.dwClassId<count&&value.pItemData&&value.pStaticPath&&
                Field<uint8_t>(value.pItemData,0xb8)==7&&Field<uint32_t>(value.pItemData,0xc)==container){
                const auto cx=Field<uint32_t>(value.pStaticPath,0x10),cy=Field<uint32_t>(value.pStaticPath,0x14);
                const auto row=table+size_t(value.dwClassId)*0x1b4;
                const auto w=Field<uint8_t>(row,0x116),h=Field<uint8_t>(row,0x117);
                if(w&&w<=8&&h&&h<=8&&cx<unsigned(columns)&&cy<unsigned(rows)&&cx+w<=unsigned(columns)&&cy+h<=unsigned(rows)){
                    Panel::Item item;item.unit=value.dwUnitId;item.seed=Field<uint32_t>(value.pItemData,0x10);
                    item.rect={(x+cx*cellW)/width,(y+cy*cellH)/height,(x+(cx+w)*cellW)/width,(y+(cy+h)*cellH)/height};
                    const bool hovered=item.rect.Contains(float(mouse.x)/width,float(mouse.y)/height);
                    const bool pinned=Panel::Matches(pin,view)&&pin.unit==item.unit&&pin.seed==item.seed;
                    if(hovered||pinned){Serialize(unit,item);if(item.bytes.empty())view.error="This item could not be read safely.";}
                    if(hovered)view.hovered=item;if(pinned)view.pinned=std::move(item);
                }
            }
            if(next==unit)return {};unit=next;
        }
        if(visited>20000)return {};
    }
    return view;
}
}
#include "MarketplaceNativeDropdowns.inl"
#include "MarketplaceNativeImages.inl"
#include "MarketplaceNativeLinks.inl"
#include "MarketplaceNativeText.inl"
static bool InspectMarketplaceItem(void* panel,uint64_t domain,uint64_t command){
    using namespace Marketplace;using namespace NativePanel;
    if(domain!=Panel::MessageHash("BankPanelMessage")||!Panel::NativeOpen())return false;
    std::string name;if(command==Panel::MessageHash("MarketInspectSelected"))name="MarketItemSummary";
    for(int n=0;n<6;++n)if(command==Panel::MessageHash("MarketInspect"+std::to_string(n)))name="MarketCardName"+std::to_string(n);
    if(name.empty())return false;auto root=WidgetFindChild(panel,"MarketplaceWorkspace");auto widget=WidgetFindChild(root,name.c_str());
    if(VisibleIn(widget,root))Links::Inspect(widget);return true;
}
static bool DispatchMarketplaceInput(void* panel,uint64_t domain,void* message){return Marketplace::NativePanel::DispatchInput(panel,domain,message);}
static void UpdateMarketplaceWidgets(void* panel){
    using namespace Marketplace;using namespace NativePanel;
    auto root=WidgetFindChild(panel,"MarketplaceWorkspace");if(!root||!WidgetLayoutSupported())return;
    // Bind while closed too: the shop button must work before the first open.
    static void* lastRoot=nullptr;if(lastRoot!=root){Images::Forget();lastRoot=root;}
    BindButtons(panel,root);
    const auto view=Panel::Presentation();
    BindTabs(root,view.open?view.section:0);
    ModalHitArea(panel,view.open);
    auto write=[](void* widget,size_t offset,const void* value,size_t size){SIZE_T count=0;return WriteProcessMemory(GetCurrentProcess(),static_cast<uint8_t*>(widget)+offset,value,size,&count)&&count==size;};
    Visibility(root,view.open);
    struct InputBinding {void* widget=nullptr;uint64_t revision=0;};
    static std::map<std::string,InputBinding> buffers;
    if(view.open)for(const auto& update:view.widgets){
        auto widget=WidgetFindChild(root,update.name.c_str());if(!widget)continue;
        // Preserve TextBox/Image input policy; only containers and buttons
        // receive explicit message enable/disable updates with visibility.
        Visibility(widget,update.visible,!update.input&&update.text.empty());
        if(!update.input&&update.text.empty())continue;
        auto& buffer=buffers[update.name];if(buffer.widget!=widget){buffer.widget=widget;buffer.revision=0;}
        if(update.input){
            if(!VisibleIn(widget,root)){buffer.revision=0;continue;}
            if(update.name=="MarketOfferMessage"){
                if(buffer.revision!=update.revision&&Text::BindInput(widget,update.text)){
                    uint64_t caret=0;for(unsigned char c:update.text)if((c&0xc0)!=0x80)++caret;
                    write(widget,0x548,&caret,sizeof(caret));const int32_t selection=int32_t(caret);write(widget,0x670,&selection,sizeof(selection));write(widget,0x674,&selection,sizeof(selection));buffer.revision=update.revision;
                }continue;
            }
            if(buffer.revision==update.revision||update.text.size()>15||!(Field<uint64_t>(widget,0x530)&0x8000000000000000ULL))continue;
            std::array<char,16> input{};memcpy(input.data(),update.text.data(),update.text.size());const uint64_t size=update.text.size();
            if(write(widget,0x538,input.data(),input.size())&&write(widget,0x528,&size,sizeof(size))){const int32_t caret=int32_t(size);write(widget,0x548,&caret,sizeof(caret));write(widget,0x670,&caret,sizeof(caret));write(widget,0x674,&caret,sizeof(caret));buffer.revision=update.revision;}
        }else{
            Text::Bind(widget,update.text);
        }
    }
    std::map<std::string,Panel::ChoiceInput> choices;
    if(view.open){
        for(const auto& r:view.rectangles){if(r.x<0||r.y<0||r.width<1||r.height<1||r.x+r.width>2250||r.y+r.height>1635)continue;
            auto widget=WidgetFindChild(root,r.name.c_str());if(!widget)continue;const std::array<int32_t,4> rect{r.x,r.y,r.width,r.height};write(widget,0x70,rect.data(),sizeof(rect));}
        for(const auto& update:view.choices){auto widget=WidgetFindChild(root,update.name.c_str());if(VisibleIn(widget,root))
            if(const auto value=BindChoice(widget,update.selection,update.revision))choices[update.name]={value->selection,value->revision};}
        for(const auto& update:view.images){auto widget=WidgetFindChild(root,update.name.c_str());if(widget){
            const bool loaded=Images::Set(widget,update.filename,update.width,update.height);Visibility(widget,update.visible&&loaded,false);}}
        for(const auto& update:view.previews){auto widget=WidgetFindChild(root,update.name.c_str());if(widget&&VisibleIn(widget,root))Links::Bind(widget,update);}
    }
    Panel::NativeChoices(std::move(choices));
    ReplayKeyboard(root);
    std::map<std::string,Panel::NativeInput> inputs;
    for(const auto name:{"MarketSearch","MarketSellerSearch","MarketProfileName","MarketWalletAmount","PaymentSearch","DepositPage","WithdrawPage","SellerPage","Quantity0","Quantity1","Quantity2","Quantity3","MarketOfferMessage"}){
        auto widget=WidgetFindChild(root,name);if(!widget)continue;
        if(!view.open||!VisibleIn(widget,root)){buffers[name].revision=0;continue;}
        const auto length=Field<uint64_t>(widget,0x528),capacity=Field<uint64_t>(widget,0x530);
        if(!strcmp(name,"MarketOfferMessage")){
            if(length>2048||length>(capacity&0x7fffffffffffffffULL))continue;
            const auto pointer=Field<const char*>(widget,0x520);std::string text(size_t(length),'\0');SIZE_T count=0;
            if(pointer&&(!length||(ReadProcessMemory(GetCurrentProcess(),pointer,text.data(),size_t(length),&count)&&count==length)))inputs[name]={std::move(text),buffers[name].revision};continue;
        }
        if(length>15||!(capacity&0x8000000000000000ULL))continue;std::array<char,16> text{};
        if(Read(static_cast<uint8_t*>(widget)+0x538,text))inputs[name]={std::string(text.data(),size_t(length)),buffers[name].revision};
    }
    Panel::NativeInputs(std::move(inputs));
}
static void CaptureMarketplacePanel(void* panel,bool switching){
    if(switching){Marketplace::Panel::Publish({});return;}
    HWND window=GetForegroundWindow();DWORD process=0;GetWindowThreadProcessId(window,&process);
    RECT client{};POINT mouse{};
    if(process!=GetCurrentProcessId()||!GetClientRect(window,&client)||!GetCursorPos(&mouse)||!ScreenToClient(window,&mouse)){
        Marketplace::Panel::Publish({});return;
    }
    auto view=Marketplace::NativePanel::Capture(panel,gSelectedPage+1,IsHardcore(),GetClientPlayerUnit(),client.right,client.bottom,mouse);
    view.syncStatus=Marketplace::LiveCustody::window.Snapshot().status;
    const auto request=Marketplace::Panel::PendingScan();
    if(request.sequence&&request.owner==view.owner&&request.page==view.page&&request.hardcore==view.hardcore&&view.tab&&!view.cursorOccupied){
        const auto save=Marketplace::NativePanel::SerializerSupported()?reinterpret_cast<Marketplace::NativePanel::SaveItem>(Pattern::Address(0x207610)):nullptr;
        Marketplace::Panel::PublishScan(Marketplace::NativePanel::CaptureDeposit(GetClientPlayerUnit(),request,save));
    }
    // A large one-shot serialization can outlast the input freshness window.
    // The completed snapshot and this geometry were captured on the paint
    // thread; publish their completion time rather than their start time.
    if(view.owner)view.tick=GetTickCount64();
    Marketplace::Panel::Publish(std::move(view));
}
