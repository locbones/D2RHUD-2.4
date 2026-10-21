// Included inside LegacyTransmog's native adapter. Inventory mutations run
// only in the authoritative 0x26 packet handler, never on the render thread.
std::mutex splitMutex;
SplitView splitView;
Game* splitExpectedGame=nullptr;
struct SplitSession {
    Game* game=nullptr;uint32_t owner=0,item=0,created=0,total=0,token=0,base=0,seed=0,copySeed=0;
    bool choosing=false;
} splitSession;
uint32_t splitSequence=0;
constexpr UINT SplitSubmitMessage=WM_APP+0x5d7;
HWND splitWindow=nullptr;
RECT splitInputBounds{};
bool splitEditing=false,splitMouseHeld=false;
StackData::Request splitPending;
std::atomic<bool> splitReady{false};
std::atomic<bool> quantityRightClicks{true};
int ItemQuantity(Unit* item){return Native<int(__fastcall*)(Unit*,int,uint16_t)>(0x1e5620)(item,70,0);}
void QuantitySet(Unit* item,int value){Native<void(__fastcall*)(Unit*,int,int,uint16_t)>(0x1e5440)(item,70,value,0);}
bool SplitEligible(Unit* player,Unit* item,bool authoritative){
    if(!splitReady.load() || !quantityRightClicks.load())return false;
    Unit p{},i{};D2InventoryStrc inv{};D2ItemDataStrc data{};uintptr_t cursor=0,table=0;uint64_t count=0;
    if(!Read(reinterpret_cast<uintptr_t>(player),p) || p.dwUnitType || p.dwAnimMode==0 || p.dwAnimMode==17 || (p.dwFlags&0x10000) ||
       !p.pInventory || !Read(reinterpret_cast<uintptr_t>(p.pInventory),inv) || inv.dwSignature!=0x01020304 || inv.pOwner!=player ||
       !Read(reinterpret_cast<uintptr_t>(p.pInventory)+0x40,cursor) || cursor ||
       !Read(reinterpret_cast<uintptr_t>(item),i) || i.dwUnitType!=4 || i.dwItemMode!=0 ||
       !Read(reinterpret_cast<uintptr_t>(i.pItemData),data) || !StackData::StoragePage(data.nPage) || data.pExtraData.pParentInv!=p.pInventory ||
       (authoritative && (data.dwCommandFlags || (data.dwItemFlags&0x2e9) || (i.dwFlagsEx&0x14))) ||
       !Read(Pattern::Address(0x1ca0390),table) || !Read(Pattern::Address(0x1ca0398),count) || !table || count>1000000 || i.dwClassId>=count)return false;
    // A nested inventory cannot be divided into quantities without copying
    // its contents. Ordinary magic/rare/unique stack modifiers are supported.
    if(i.pInventory){D2InventoryStrc children{};if(!Read(reinterpret_cast<uintptr_t>(i.pInventory),children) || children.pFirstItem)return false;}
    std::array<uint8_t,ItemRecordSize> record{};
    return Read(table+size_t(i.dwClassId)*record.size(),record) && !record[0x141] && record[0x13a] && ItemQuantity(item)>1;
}
std::string SplitName(Unit* item){
    uintptr_t table=0;uint16_t index=0;
    if(!Read(Pattern::Address(0x1ca0390),table) || !Read(table+size_t(item->dwClassId)*ItemRecordSize+0xfc,index))return "Item stack";
    auto text=Native<const char*(__fastcall*)(uint16_t,const char*)>(0x413860)(index,nullptr);
    std::string name;
    for(size_t n=0;n<256;++n){char ch=0;if(!Read(reinterpret_cast<uintptr_t>(text)+n,ch))return "Item stack";if(!ch)return name.empty()?"Item stack":name;name+=ch=='\n'||ch=='\r'?' ':ch;}
    return "Item stack";
}
void SendSplit(StackData::Request request){
    auto packet=StackData::Encode(request);
    Native<void(__fastcall*)(const void*,int)>(0x10e710)(packet.data(),int(packet.size()));
}
void StartSplit(Unit* player,Unit* item){
    const int quantity=ItemQuantity(item);const auto name=SplitName(item);
    Game* expectedGame=nullptr;uintptr_t client=0;
    if(Read(Pattern::Address(0x1d637f0),client))for(size_t n=0;client && n<64;++n){
        D2ClientStrc value{};if(!Read(client,value))break;
        if(value.dwClientState==4 && value.dwUnitGUID==player->dwUnitId){expectedGame=value.pGame;break;}
        const auto next=reinterpret_cast<uintptr_t>(value.pNext);if(next==client)break;client=next;
    }
    if(!expectedGame)return;
    StackData::Request request;
    {std::lock_guard lock(splitMutex);
        if(splitView.phase!=SplitView::Closed)return;
        if(!++splitSequence)++splitSequence;
        splitExpectedGame=expectedGame;
        splitView={SplitView::Waiting,splitSequence,player->dwUnitId,item->dwUnitId,uint32_t(quantity),GetTickCount64(),name,"Finding a free storage slot..."};
        uintptr_t table=0;std::array<char,4> code{};
        if(Read(Pattern::Address(0x1ca0390),table) && table && Read(table+size_t(item->dwClassId)*ItemRecordSize+0x80,code)){
            for(char ch:code){if(!ch || ch==' ')break;splitView.iconCode+=ch;}
        }
        splitInputBounds={};splitEditing=splitMouseHeld=false;
        POINT point{};RECT client{};
        if(splitWindow && GetCursorPos(&point) && ScreenToClient(splitWindow,&point) && GetClientRect(splitWindow,&client) && client.right>0 && client.bottom>0){
            splitView.anchorX=std::clamp(float(point.x)/client.right,0.0f,1.0f);splitView.anchorY=std::clamp(float(point.y)/client.bottom,0.0f,1.0f);
        }
        request={player->dwUnitId,item->dwUnitId,1,uint32_t(quantity),splitSequence,StackData::Begin};
    }
    SendSplit(request);
}
struct ItemBits {uint8_t* data;uint64_t bits,bytes=0,bit=0;uint32_t error=0,padding=0;};
static_assert(sizeof(ItemBits)==0x28);
Unit* CopyStack(Game* game,Unit* source){
    // Same native saved-item path as 2f83d0, with a bounded larger buffer,
    // explicit overflow refusal, no source flags changed and no child copies.
    std::array<uint8_t,32768> storage{};ItemBits bits{storage.data(),storage.size()*8};
    Native<int(__fastcall*)(Unit*,ItemBits*,int,int,int)>(0x207610)(source,&bits,1,0,0);
    const auto length=bits.bytes+(bits.bit?1:0);
    if(bits.error || !length || length>storage.size())return nullptr;
    std::array<uint8_t,0x28> header{};
    if(!Native<int(__fastcall*)(const void*,size_t,int,void*,int)>(0x204dc0)(storage.data(),size_t(length),1,header.data(),98))return nullptr;
    int used=0;
    using Load=Unit*(__fastcall*)(Game*,D2ActiveRoomStrc*,const void*,int,int,void*,int*,int);
    Unit* item=Native<Load>(0x2f5770)(game,nullptr,storage.data(),int(length),0,header.data(),&used,98);
    if(!item)return nullptr;
    if(used!=length || item->dwUnitType!=4 || item->dwClassId!=source->dwClassId || item->dwUnitId==source->dwUnitId ||
       !item->pItemData || item->pItemData->pExtraData.pParentInv){
        Native<void(__fastcall*)(Game*,Unit*)>(0x2f70a0)(game,item);return nullptr;
    }
    Native<void(__fastcall*)(Unit*,int)>(0x209c60)(item,4);item->pItemData->nPage=255;
    return item;
}
void PublishStack(Game* game,Unit* player,Unit* item,bool existing){
    Location location;Native<Location*(__fastcall*)(Location*,Unit*)>(0x213a20)(&location,item);
    auto notify=Native<NotifyItem>(0x316b80);
    // Republish the full saved item rather than truncating mod quantities to
    // the older one-byte quantity-update packet. GUID and server item survive.
    if(existing)notify(game,player,item,0x20,4,location.mode,location.page,location.body,uint32_t(location.x)|(uint32_t(location.y)<<16));
    notify(game,player,item,1,2,location.mode,location.page,location.body,uint32_t(location.x)|(uint32_t(location.y)<<16));
}
int SplitPacket(Game* game,Unit* player,std::span<const uint8_t> bytes){
    StackData::Request request;
    if(!splitReady.load() || !quantityRightClicks.load() || !StackData::Decode(bytes,request) || request.owner!=player->dwUnitId || !LocalServer(player->dwUnitId))return 0;
    std::lock_guard lock(splitMutex);
    if(game!=splitExpectedGame || request.token!=splitView.token || request.owner!=splitView.owner || request.item!=splitView.item || request.expected!=splitView.total ||
       GetTickCount64()-splitView.opened>60000)return 0;
    auto error=[&](const char* message){splitSession={};splitView.phase=SplitView::Error;splitView.message=message;return 0;};
    auto find=Native<Unit*(__fastcall*)(Game*,int,uint32_t)>(0x2c49c0);
    Unit* source=find(game,4,request.item);
    if(request.action==StackData::Begin){
        if(splitView.phase!=SplitView::Waiting || request.amount!=1)return 0;
        if(!source || !SplitEligible(player,source,true) || ItemQuantity(source)!=request.expected)return error("The original stack changed. Nothing was split.");
        Unit* copy=CopyStack(game,source);
        if(!copy)return error("Unable to copy the stack safely. Nothing was split.");
        auto destroy=[&](){Native<void(__fastcall*)(Game*,Unit*)>(0x2f70a0)(game,copy);};
        QuantitySet(copy,1);
        if(ItemQuantity(copy)!=1){destroy();return error("Unable to set the new quantity. Nothing was split.");}
        QuantitySet(source,int(request.expected)-1);
        if(ItemQuantity(source)!=int(request.expected)-1){QuantitySet(source,int(request.expected));destroy();return error("Unable to update the original quantity. Nothing was split.");}
        const uint32_t id=copy->dwUnitId;const auto page=source->pItemData->nPage;
        bool added=false;
        for(uint32_t destination:{uint32_t(page),uint32_t(0)}){
            for(uint16_t y=0;y<16 && !added;++y)for(uint16_t x=0;x<16 && !added;++x){
                Location location{0,destination,x,y,0};added=Native<AddItem>(0x2d5e10)(player,&location,&id,1,false);
            }
            if(added || page==0)break;
        }
        if(!added){QuantitySet(source,int(request.expected));destroy();return error("No free storage slot. The original stack is unchanged.");}
        // Separate the persistent fingerprint as well as the runtime GUID.
        // Saved modifiers have already been read; changing identity does not
        // reroll them. Do not create two saved items with the same fingerprint.
        const uint64_t random=uint64_t(uint32_t(player->pSeed.LowSeed()))*0x6ac690c5ULL+uint32_t(player->pSeed.HighSeed());
        player->pSeed.LowSeed()=int32_t(uint32_t(random));player->pSeed.HighSeed()=int32_t(uint32_t(random>>32));
        auto fingerprint=uint32_t(random);
        if(!fingerprint || fingerprint==source->pItemData->dwInitSeed)fingerprint=source->pItemData->dwInitSeed==0xffffffff ? 1 : source->pItemData->dwInitSeed+1;
        copy->pItemData->dwInitSeed=fingerprint;
        splitSession={game,request.owner,request.item,id,request.expected,request.token,source->dwClassId,source->pItemData->dwInitSeed,fingerprint,true};
        PublishStack(game,player,source,true);PublishStack(game,player,copy,false);
        splitView.phase=SplitView::Choosing;splitView.message="One item separated. Choose the final size of the new stack.";
        return 0;
    }
    if(splitView.phase!=SplitView::Submitting || !splitSession.choosing || splitSession.game!=game || splitSession.token!=request.token)return 0;
    Unit* copy=find(game,4,splitSession.created);
    // Quantity-one stacks still need the same ownership/transaction checks.
    // SplitEligible intentionally rejects quantity one, so validate that copy
    // through the ordinary item ownership fields below.
    Unit owner{},other{};D2ItemDataStrc data{};uintptr_t cursor=0;
    if(!source || !copy || !Read(reinterpret_cast<uintptr_t>(player),owner) || !owner.pInventory ||
       !Read(reinterpret_cast<uintptr_t>(owner.pInventory)+0x40,cursor) || cursor || owner.dwAnimMode==0 || owner.dwAnimMode==17 || (owner.dwFlags&0x10000) ||
       source->dwClassId!=splitSession.base || source->pItemData->dwInitSeed!=splitSession.seed || source->dwItemMode!=0 ||
       source->pItemData->pExtraData.pParentInv!=owner.pInventory || !StackData::StoragePage(source->pItemData->nPage) || source->pItemData->dwCommandFlags ||
       (source->pItemData->dwItemFlags&0x2e9) || (source->dwFlagsEx&0x14) ||
       !Read(reinterpret_cast<uintptr_t>(copy),other) || other.dwClassId!=splitSession.base || other.dwItemMode!=0 ||
       !Read(reinterpret_cast<uintptr_t>(other.pItemData),data) || data.pExtraData.pParentInv!=owner.pInventory || !StackData::StoragePage(data.nPage) ||
       data.dwInitSeed!=splitSession.copySeed || data.dwCommandFlags || (data.dwItemFlags&0x2e9) || (other.dwFlagsEx&0x14) ||
       ItemQuantity(source)!=int(request.expected)-1 || ItemQuantity(copy)!=1)return error("The stacks changed. The initial one-item split was kept.");
    QuantitySet(source,int(request.expected-request.amount));QuantitySet(copy,int(request.amount));
    if(ItemQuantity(source)!=int(request.expected-request.amount) || ItemQuantity(copy)!=int(request.amount)){
        QuantitySet(source,int(request.expected)-1);QuantitySet(copy,1);return error("Quantity update refused. The initial one-item split was kept.");
    }
    splitSession={};PublishStack(game,player,source,true);PublishStack(game,player,copy,true);
    splitView={};return 0;
}
constexpr std::array<uint32_t,7> SplitRoutines{0x1e5620,0x207610,0x204dc0,0x2f5770,0x209c60,0x10e710,0x2c42a0};
// Populated from the supported executable. The existing module guards the
// shared inventory, stat, use, notification and creation dependencies too.
constexpr std::array<uint64_t,7> SplitHashes{
    0x1bfb159b91b26fc1ULL,0x1d0b5b0bcd8efc2aULL,0xf4b76c645f24e1ebULL,0xe949e161fab49919ULL,
    0xb07f641ccd9447b1ULL,0x61fe78ba3f4ff47fULL,0x93947f0b0c8a951aULL};
bool SplitSupported(){
    std::array<uint8_t,32> code{};
    for(size_t i=0;i<SplitRoutines.size();++i)if(!Read(Pattern::Address(SplitRoutines[i]),code) || Hash(code)!=SplitHashes[i])return false;
    return true;
}
