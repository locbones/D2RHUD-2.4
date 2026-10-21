enum class SocketBehavior {Commit,Refuse,Pass,Moved,Consumed};
SocketBehavior socketBehavior=SocketBehavior::Commit;
D2InventoryStrc socketInventory{};D2PlayerDataStrc socketPlayerData{};
Unit* socketSource=nullptr;Unit* socketTarget=nullptr;int socketCalls=0,socketClassify=3;
int socketQuantityNotice=0;
void __fastcall SocketQuantityUpdate(D2ClientStrc* owner,Unit* item,int,int stat,int quantity,uint16_t layer){
    assert(owner==&client&&item==socketSource&&stat==70&&!layer);socketQuantityNotice=quantity;
}
std::array<uint8_t,21> SocketCommand(){
    std::array<uint8_t,21> packet{};packet[0]=0x28;
    memcpy(packet.data()+1,&socketSource->dwUnitId,4);memcpy(packet.data()+5,&socketTarget->dwUnitId,4);
    const uint32_t xy=2|(3<<16);memcpy(packet.data()+17,&xy,4);return packet;
}
void Cursor(Unit* item){memcpy(reinterpret_cast<uint8_t*>(&inv)+0x40,&item,sizeof(item));}
int __fastcall ClassifySocket(Game* g,Unit* p,uint32_t source,uint32_t target){
    assert(g==&game&&p==&player&&source==socketSource->dwUnitId&&target==socketTarget->dwUnitId);return socketClassify;
}
int __fastcall SocketOriginal(Game* g,Unit* p,const uint8_t* packet,int size){
    ++socketCalls;assert(g==&game&&p==&player&&packet&&size==21&&packet[0]==0x28);
    if(socketBehavior==SocketBehavior::Pass)return 7;
    assert(Entry(socketSource).quantity==1&&lastCreated&&Entry(lastCreated).quantity>=1);
    if(socketBehavior==SocketBehavior::Refuse)return 1;
    if(socketBehavior==SocketBehavior::Moved||socketBehavior==SocketBehavior::Consumed){
        socketSource->pItemData->pExtraData.pParentInv=nullptr;Cursor(nullptr);
        if(socketBehavior==SocketBehavior::Consumed)Entry(socketSource).freed=true;
        else socketSource->dwItemMode=3;
        return 0;
    }
    socketSource->dwItemMode=6;socketSource->pItemData->pExtraData.pParentInv=&socketInventory;Cursor(nullptr);return 0;
}
void PrepareSocket(int quantity=88){
    PrepareSplit(quantity);socketStackReady=true;socketCalls=0;socketClassify=3;socketBehavior=SocketBehavior::Commit;socketQuantityNotice=0;
    socketSource=items.empty()?nullptr:&items.front().unit;
    socketSource->dwItemMode=4;socketSource->pItemData->nPage=255;Cursor(socketSource);
    socketSource->pItemData->wMagicPrefix[0]=123;
    socketTarget=Make(3);socketTarget->pItemData->pExtraData.pParentInv=&inv;socketTarget->pItemData->dwItemFlags|=0x800;
    socketInventory={};socketInventory.dwSignature=0x01020304;socketInventory.pOwner=socketTarget;socketTarget->pInventory=&socketInventory;
    socketPlayerData={};socketPlayerData.pClient=&client;player.pPlayerData=&socketPlayerData;
    bindings[0x2b1dd0]=uintptr_t(ClassifySocket);originalSocketPacket=SocketOriginal;
    bindings[0x2ce230]=uintptr_t(SocketQuantityUpdate);
}
void SocketStackContract(){
    for(int quantity:{2,88,9999,100000}){
        PrepareSocket(quantity);quantityRightClicks=false;auto packet=SocketCommand();const auto seed=socketSource->pItemData->dwInitSeed;
        assert(SocketPacketHook(&game,&player,packet.data(),21)==0);
        assert(socketCalls==1&&socketSource->dwItemMode==6&&Entry(socketSource).quantity==1);
        assert(socketSource->pItemData->pExtraData.pParentInv==&socketInventory&&lastCreated->pItemData->pExtraData.pParentInv==&inv);
        assert(Entry(lastCreated).quantity==quantity-1&&lastCreated->dwItemMode==0&&lastCreated->pItemData->nPage==0);
        assert(lastCreated->pItemData->dwInitSeed!=seed&&lastCreated->pItemData->dwQualityNo==socketSource->pItemData->dwQualityNo);
        const auto count=items.size();socketBehavior=SocketBehavior::Pass;
        assert(SocketPacketHook(&game,&player,packet.data(),21)==7&&items.size()==count); // Replay cannot split a socketed unit.
    }
    PrepareSocket();auto packet=SocketCommand();acceptedX=-1;
    assert(SocketPacketHook(&game,&player,packet.data(),21)==0&&!socketCalls&&Entry(socketSource).quantity==88&&Entry(lastCreated).freed);
    assert(socketSource->dwItemMode==4&&SocketField<Unit*>(&inv,0x40)==socketSource);
    assert(socketQuantityNotice==88);
    for(int failure=0;failure<3;++failure){
        PrepareSocket();packet=SocketCommand();serializationFails=failure==0;parseFails=failure==1;loadFails=failure==2;
        SocketPacketHook(&game,&player,packet.data(),21);assert(!socketCalls&&Entry(socketSource).quantity==88&&socketSource->dwItemMode==4);
    }
    PrepareSocket();packet=SocketCommand();socketBehavior=SocketBehavior::Refuse;
    assert(SocketPacketHook(&game,&player,packet.data(),21)==1&&Entry(socketSource).quantity==88&&Entry(lastCreated).freed);
    PrepareSocket();packet=SocketCommand();socketBehavior=SocketBehavior::Refuse;retainOnRemove=true;
    assert(SocketPacketHook(&game,&player,packet.data(),21)==1&&Entry(socketSource).quantity==1&&Entry(lastCreated).quantity==87&&!Entry(lastCreated).freed);
    assert(socketQuantityNotice==1);
    for(auto behavior:{SocketBehavior::Moved,SocketBehavior::Consumed}){
        PrepareSocket();packet=SocketCommand();socketBehavior=behavior;
        assert(SocketPacketHook(&game,&player,packet.data(),21)==0&&Entry(socketSource).quantity==1&&Entry(lastCreated).quantity==87);
        assert(!Entry(lastCreated).freed&&!socketQuantityNotice); // Never restore quantity to a moved/consumed source.
    }
    for(int invalid=0;invalid<10;++invalid){
        PrepareSocket();packet=SocketCommand();socketBehavior=SocketBehavior::Pass;
        if(invalid==0)client.pGame=nullptr;
        if(invalid==1)socketSource->pItemData->pExtraData.pParentInv=nullptr;
        if(invalid==2)socketTarget->pItemData->pExtraData.pParentInv=nullptr;
        if(invalid==3)Cursor(nullptr);
        if(invalid==4)socketSource->dwItemMode=0;
        if(invalid==5)player.dwAnimMode=17;
        if(invalid==6)Entry(socketSource).quantity=1;
        if(invalid==7)socketClassify=2;
        if(invalid==8)packet[17]++;
        if(invalid==9)socketStackReady=false;
        const auto before=Entry(socketSource).quantity;
        assert(SocketPacketHook(&game,&player,packet.data(),21)==7&&items.size()==2&&Entry(socketSource).quantity==before);
    }
    for(int unsafe=0;unsafe<3;++unsafe){
        PrepareSocket();packet=SocketCommand();
        if(unsafe==0)table[2][0x13a]=0;
        if(unsafe==1)socketSource->pItemData->dwCommandFlags=1;
        D2InventoryStrc children{};if(unsafe==2){children.pFirstItem=socketSource;socketSource->pInventory=&children;}
        assert(SocketPacketHook(&game,&player,packet.data(),21)==0&&!socketCalls&&items.size()==2&&Entry(socketSource).quantity==88);
    }
    // A remote LAN player's authoritative client need not be the host's first client.
    PrepareSocket();packet=SocketCommand();clientHead=nullptr;
    assert(SocketPacketHook(&game,&player,packet.data(),21)==0&&socketCalls==1&&Entry(lastCreated).quantity==87);
    socketStackReady=false;splitTest=false;Bind();Reset();
    std::cout<<"Stack socketing: quantity conservation, fresh identity, inventory placement, native refusal/rollback, no-space/serialization failures, ownership/location/replay guards, remote LAN players and independent quantity toggle passed.\n";
}
// Execute the actual packet entry, socket validation/parser, socket insertion
// routine and saved-item wrappers from an owned mapped EXE. Grid/stat-property
// attachment, allocator, item-body codec and transport are explicit boundaries.
Unit* socketBucketValue=nullptr;int nativeSocketAttachments=0,nativeSocketProperties=0;
std::vector<std::vector<uint8_t>> socketSystemPackets;
Unit** __fastcall SocketBucket(int type,Game* g,uint32_t id){socketBucketValue=Find(g,type,id);return &socketBucketValue;}
D2InventoryStrc* __fastcall SocketInventory(Unit* item){return item->pInventory;}
int __fastcall SocketCapacity(Unit*){return 6;}
int __fastcall SocketItemType(Unit*,int type){return type==0x35;}
int __fastcall SocketInteraction(Unit*,Unit*){return 0;}
int __fastcall SocketAttach(D2InventoryStrc* inventory,Unit* item){
    assert(inventory==&socketInventory&&item==socketSource&&Entry(item).quantity==1&&!item->pItemData->pExtraData.pParentInv);
    item->pItemData->pExtraData.pParentInv=inventory;++nativeSocketAttachments;return 1;
}
void __fastcall SocketProperties(void*,Unit* item,Unit* target,int){
    assert(item==socketSource&&target==socketTarget&&Entry(item).quantity==1);++nativeSocketProperties;
}
void* __fastcall SocketRuneword(Unit*){return nullptr;}
uint32_t __fastcall SocketPosition(Unit*){return 2|(3<<16);}
D2PlayerDataStrc* __fastcall SocketPlayerData(Unit* p){assert(p==&player);return &socketPlayerData;}
void __fastcall SocketTransport(void*,uint32_t,void** range){
    auto begin=static_cast<uint8_t*>(range[0]),end=static_cast<uint8_t*>(range[1]);assert(end>=begin&&end-begin<512);
    socketSystemPackets.emplace_back(begin,end);
}
bool __fastcall SocketNativeRemove(Unit* p,const Location* updates,const Location* removals,const uint32_t* updateIds,
    const uint32_t* removalIds,size_t updateCount,size_t removalCount,bool special){
    assert(p==&player&&updates&&removals&&updateIds&&removalIds&&updateCount==1&&removalCount==1&&!special);
    assert(*updateIds==socketTarget->dwUnitId&&*removalIds==socketSource->dwUnitId&&removals->mode==4);
    socketSource->pItemData->pExtraData.pParentInv=nullptr;Cursor(nullptr);return true;
}
void NativeSocketStackContract(uint8_t* image){
    auto stub=[&](uint32_t rva,auto function){const uint8_t jump[]{0xff,0x25,0,0,0,0};memcpy(image+rva,jump,6);auto address=uintptr_t(function);memcpy(image+rva+6,&address,8);};
    // The optimized native bucket helper preserves R8/R9 for its callers.
    // Preserve these registers across the C++ lookup boundary as well.
    auto bridge=image+0x2001000;uint8_t code[]{0x51,0x52,0x41,0x50,0x41,0x51,0x41,0x52,0x41,0x53,
        0x48,0x83,0xec,0x28,0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xd0,0x48,0x83,0xc4,0x28,
        0x41,0x5b,0x41,0x5a,0x41,0x59,0x41,0x58,0x5a,0x59,0xc3};
    const auto lookup=uintptr_t(SocketBucket);memcpy(code+16,&lookup,8);memcpy(bridge,code,sizeof(code));
    stub(0x2c4950,bridge);stub(0x2c49c0,Find);stub(0x1e5440,SetStat);stub(0x1e5620,StackQuantity);
    stub(0x2d5e10,Add);stub(0x2dba00,SocketNativeRemove);stub(0x213a20,GetLocation);stub(0x2f70a0,Free);
    stub(0x208330,SocketInventory);stub(0x1fdf10,SocketCapacity);stub(0x1fe680,SocketItemType);stub(0x1ffcd0,SocketInteraction);
    stub(0x1fa060,OrdinaryItemType);stub(0x20fb30,SocketAttach);stub(0x2d42d0,SocketProperties);stub(0x1fe3c0,SocketRuneword);
    stub(0x209530,GetRoom);stub(0x2090c0,SocketPosition);stub(0x20c230,SocketPlayerData);stub(0x316b80,Notify);stub(0x2d0130,NoOperation);
    uintptr_t transportTable[6]{};transportTable[5]=uintptr_t(SocketTransport);auto transport=uintptr_t(transportTable);
    memcpy(image+0x18682b0,&transport,8);
    FlushInstructionCache(GetCurrentProcess(),image,0x2002000);
    for(int quantity:{88,9999}){
        PrepareSocket(quantity);nativeStackSource=socketSource;table[3][0x13f]=1;
        socketPlayerData.pClient=&client;client.pPlayer=&player;client.dwFlags=1;game.pClientList=&client;
        originalSocketPacket=reinterpret_cast<Packet>(image+SocketPacketRva);
        auto packet=SocketCommand();nativeSocketAttachments=nativeSocketProperties=0;
        assert(SocketPacketHook(&game,&player,packet.data(),21)==0);
        assert(nativeSocketAttachments==1&&nativeSocketProperties==1&&socketSource->dwItemMode==6);
        assert(Entry(socketSource).quantity==1&&Entry(lastCreated).quantity==quantity-1);
        assert(lastCreated->pItemData->wMagicPrefix[0]==123&&lastCreated->pItemData->dwQualityNo==socketSource->pItemData->dwQualityNo);
        assert(lastCreated->pItemData->dwInitSeed!=socketSource->pItemData->dwInitSeed);
    }
    // The real variable-width stat packet retains cursor quantities past 255.
    for(int quantity:{1,88,9999,100000}){
        PrepareSocket(quantity);socketSystemPackets.clear();
        SocketRepublish(&game,&player,socketSource,true);assert(socketSystemPackets.size()==1);
        const auto& packet=socketSystemPackets.front();assert(packet.size()>2&&packet[0]==0x3e&&packet[1]==packet.size());
        size_t offset=16;auto bits=[&](int n){uint32_t out=0;for(int bit=0;bit<n;++bit,++offset)out|=uint32_t((packet[offset/8]>>(offset%8))&1)<<bit;return out;};
        auto variable=[&]{const int width=!bits(1)?8:!bits(1)?16:32;return bits(width);};
        assert(variable()==socketSource->dwUnitId&&bits(1)==1&&bits(9)==70&&variable()==quantity);
    }
    game.pClientList=nullptr;testImage=0;socketStackReady=false;splitTest=false;Bind();Reset();
    std::cout<<"Actual native socket packet/parser, filler validation, socket insertion/mode and native saved-item copy preserve 1 + remainder; original modifier callback sees quantity 1. Native variable-width cursor packets preserve 1/88/9999/100000. Grid/property attachment, item-body codec, allocator and transport are boundaries.\n";
}
