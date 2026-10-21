// Server-side socket command. This is independent of the right-click chooser:
// it protects stacks for every player on this offline/TCP-IP game host.
constexpr uint32_t SocketPacketRva=0x2b83a0;
Packet originalSocketPacket=nullptr;
std::atomic<bool> socketStackReady{false};
std::atomic<const char*> socketStackStatus{"Automatic stack socketing is not installed."};
template<class T>T SocketField(const void* pointer,size_t offset=0){T value{};Read(reinterpret_cast<uintptr_t>(pointer)+offset,value);return value;}
bool SocketStackSupported(){
    if(!SplitSupported())return false;
    for(const auto& signature:std::array<Signature,3>{{
        {SocketPacketRva,0x280,0x8b307f66e9fe250dULL},
        {0x2b1dd0,0x2f1,0x2796470c7856a7cbULL},
        {0x2ce230,0x221,0x334a860c9adfd7baULL}}}){
        std::vector<uint8_t> bytes(signature.size);
        SIZE_T read=0;
        if(!ReadProcessMemory(GetCurrentProcess(),Native<void*>(signature.rva),bytes.data(),bytes.size(),&read)||
            read!=bytes.size()||Hash(bytes)!=signature.hash)return false;
    }return true;
}
bool SocketOwner(Game* game,Unit* player){
    if(!game||!player||player->dwUnitType||!player->pPlayerData||!player->pInventory||
        player->pInventory->dwSignature!=0x01020304||player->pInventory->pOwner!=player||
        player->dwAnimMode==0||player->dwAnimMode==17||(player->dwFlags&0x10000))return false;
    const auto client=SocketField<D2ClientStrc*>(player->pPlayerData,0xf0);
    D2ClientStrc value{};
    return Read(reinterpret_cast<uintptr_t>(client),value)&&value.pGame==game&&
        value.dwUnitGUID==player->dwUnitId&&value.dwClientState==4;
}
void SocketRepublish(Game* game,Unit* player,Unit* item,bool existing){
    if(!item||!item->pItemData||item->pItemData->pExtraData.pParentInv!=player->pInventory)return;
    if(item->dwItemMode==4){
        // A cursor stack is already present on the client. Use its native
        // variable-width unit-stat packet, never a put-in-inventory action.
        const auto client=SocketField<D2ClientStrc*>(player->pPlayerData,0xf0);
        Native<void(__fastcall*)(D2ClientStrc*,Unit*,int,int,int,uint16_t)>(0x2ce230)(client,item,0,70,ItemQuantity(item),0);
    }else PublishStack(game,player,item,existing);
}
// A failed native socket may still leave a legitimate 1 + remainder split.
// Only restore the original quantity after the remainder is detached/deleted.
bool UndoSocketSplit(Game* game,Unit* player,Unit* source,Unit* remainder,int total){
    if(remainder->pItemData->pExtraData.pParentInv==player->pInventory){
        Location location;Native<Location*(__fastcall*)(Location*,Unit*)>(0x213a20)(&location,remainder);
        const uint32_t id=remainder->dwUnitId;
        Native<RemoveItem>(0x2dba00)(player,nullptr,&location,nullptr,&id,0,1,false);
    }
    if(remainder->pItemData->pExtraData.pParentInv){
        SocketRepublish(game,player,source,true);SocketRepublish(game,player,remainder,false);
        return false;
    }
    Native<void(__fastcall*)(Game*,Unit*)>(0x2f70a0)(game,remainder);
    QuantitySet(source,total);SocketRepublish(game,player,source,true);return true;
}
int __fastcall SocketPacketHook(Game* game,Unit* player,const uint8_t* packet,int size){
    if(!socketStackReady.load()||!packet||size!=21||packet[0]!=0x28||!SocketOwner(game,player))
        return originalSocketPacket(game,player,packet,size);
    uint32_t sourceId=0,targetId=0;memcpy(&sourceId,packet+1,4);memcpy(&targetId,packet+5,4);
    const auto find=Native<Unit*(__fastcall*)(Game*,int,uint32_t)>(0x2c49c0);
    auto source=find(game,4,sourceId),target=find(game,4,targetId);
    if(!source||!target||source==target||source->dwUnitType!=4||target->dwUnitType!=4||!source->pItemData||!target->pItemData||
        source->dwItemMode!=4||SocketField<Unit*>(player->pInventory,0x40)!=source||
        source->pItemData->pExtraData.pParentInv!=player->pInventory||target->pItemData->pExtraData.pParentInv!=player->pInventory)
        return originalSocketPacket(game,player,packet,size);
    const int total=ItemQuantity(source);
    if(total<=1)return originalSocketPacket(game,player,packet,size);
    // The original native classifier checks socket-filler type, identification,
    // remaining sockets and the recipient item. Other item-on-item operations
    // (stacking, repairs, cube placement) retain their own command paths.
    if(Native<int(__fastcall*)(Game*,Unit*,uint32_t,uint32_t)>(0x2b1dd0)(game,player,sourceId,targetId)!=3)
        return originalSocketPacket(game,player,packet,size);
    Location destination;Native<Location*(__fastcall*)(Location*,Unit*)>(0x213a20)(&destination,target);
    uint32_t body=0,page=0,xy=0;memcpy(&body,packet+9,4);memcpy(&page,packet+13,4);memcpy(&xy,packet+17,4);
    if((destination.mode==1?body!=destination.body:
        destination.mode!=0||body||page!=destination.page||xy!=(uint32_t(destination.x)|(uint32_t(destination.y)<<16))))
        return originalSocketPacket(game,player,packet,size);
    auto refuse=[&]{SocketRepublish(game,player,source,true);return 0;};
    const auto data=source->pItemData;
    uintptr_t table=0;uint64_t count=0;std::array<uint8_t,ItemRecordSize> record{};
    if(data->dwCommandFlags||(data->dwItemFlags&0x2e9)||(source->dwFlagsEx&0x14)||
        !Read(Pattern::Address(0x1ca0390),table)||!Read(Pattern::Address(0x1ca0398),count)||!table||count>1000000||source->dwClassId>=count||
        !Read(table+size_t(source->dwClassId)*ItemRecordSize,record)||!record[0x13a]||
        (source->pInventory&&source->pInventory->pFirstItem))return refuse();
    auto remainder=CopyStack(game,source);if(!remainder)return refuse();
    QuantitySet(remainder,total-1);QuantitySet(source,1);
    if(ItemQuantity(remainder)!=total-1||ItemQuantity(source)!=1){
        QuantitySet(source,total);Native<void(__fastcall*)(Game*,Unit*)>(0x2f70a0)(game,remainder);return refuse();
    }
    const uint32_t id=remainder->dwUnitId;bool added=false;
    for(uint16_t y=0;y<16&&!added;++y)for(uint16_t x=0;x<16&&!added;++x){
        Location location{0,0,x,y,0};added=Native<AddItem>(0x2d5e10)(player,&location,&id,1,false);
    }
    if(!added){QuantitySet(source,total);Native<void(__fastcall*)(Game*,Unit*)>(0x2f70a0)(game,remainder);return refuse();}
    // Give the copy its own persistent fingerprint without rerolling modifiers.
    const uint64_t random=uint64_t(uint32_t(player->pSeed.LowSeed()))*0x6ac690c5ULL+uint32_t(player->pSeed.HighSeed());
    player->pSeed.LowSeed()=int32_t(uint32_t(random));player->pSeed.HighSeed()=int32_t(uint32_t(random>>32));
    auto fingerprint=uint32_t(random);
    if(!fingerprint||fingerprint==data->dwInitSeed)fingerprint=data->dwInitSeed==0xffffffff?1:data->dwInitSeed+1;
    remainder->pItemData->dwInitSeed=fingerprint;
    // Native socketing now sees exactly one item. Its normal property transfer,
    // runeword handling and client socket notifications remain authoritative.
    const int result=originalSocketPacket(game,player,packet,size);
    auto current=find(game,4,sourceId);
    if(current==source&&source->dwItemMode==6&&target->pInventory&&data->pExtraData.pParentInv==target->pInventory){
        SocketRepublish(game,player,remainder,false);return result;
    }
    if(current==source&&source->dwItemMode==4&&ItemQuantity(source)==1&&data->pExtraData.pParentInv==player->pInventory&&
        SocketField<Unit*>(player->pInventory,0x40)==source)
        UndoSocketSplit(game,player,source,remainder,total);
    else SocketRepublish(game,player,remainder,false); // Never recreate consumed/moved source quantities.
    return result;
}
void InstallSocketStacks(){
    if(socketStackReady.load())return;
    if(!SocketStackSupported()){socketStackStatus="Automatic stack socketing unavailable: native socket/saved-item signatures do not match.";return;}
    auto target=Native<void*>(SocketPacketRva);
    if(MH_CreateHook(target,reinterpret_cast<void*>(SocketPacketHook),reinterpret_cast<void**>(&originalSocketPacket))!=MH_OK)return;
    if(MH_EnableHook(target)!=MH_OK){MH_RemoveHook(target);originalSocketPacket=nullptr;return;}
    socketStackReady=true;socketStackStatus="Automatic stack socketing: one item per socket, remainder to inventory; available on this offline/TCP-IP host.";
}
void UninstallSocketStacks(){
    if(socketStackReady.exchange(false)){auto target=Native<void*>(SocketPacketRva);MH_DisableHook(target);MH_RemoveHook(target);}
    originalSocketPacket=nullptr;
}
