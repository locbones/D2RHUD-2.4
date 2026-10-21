bool splitTest=false,serializationFails=false,parseFails=false,loadFails=false;
std::array<uint32_t,6> serializedStack{};
int __fastcall StackQuantity(Unit* item,int stat,uint16_t layer){assert(stat==70 && !layer);return Entry(item).quantity;}
int __fastcall SerializeStack(Unit* item,ItemBits* bits,int saved,int children,int gamble){
    assert(saved==1 && !children && !gamble && bits->bits==32768*8 && !bits->bytes && !bits->bit);
    serializedStack={item->dwClassId,uint32_t(Entry(item).quantity),item->pItemData->dwInitSeed,item->pItemData->dwQualityNo,item->pItemData->dwItemLevel,item->pItemData->dwItemFlags};
    memcpy(bits->data,serializedStack.data(),sizeof(serializedStack));bits->bytes=sizeof(serializedStack);bits->error=serializationFails;return int(bits->bytes);
}
int __fastcall ParseStack(const void* data,size_t size,int saved,void* header,int version){
    assert(saved==1 && version==98 && size==sizeof(serializedStack) && !memcmp(data,serializedStack.data(),size));
    memcpy(header,serializedStack.data(),4);return !parseFails;
}
Unit* __fastcall LoadStack(Game* g,D2ActiveRoomStrc* r,const void* data,int size,int flags,void* header,int* used,int version){
    assert(g==&game && !r && !flags && version==98 && size==sizeof(serializedStack) && !memcmp(data,serializedStack.data(),size));
    assert(*static_cast<uint32_t*>(header)==serializedStack[0]);
    if(loadFails)return nullptr;
    lastCreated=Make(serializedStack[0]);lastCreated->pItemData->dwInitSeed=serializedStack[2];
    lastCreated->pItemData->dwQualityNo=serializedStack[3];lastCreated->pItemData->dwItemLevel=serializedStack[4];lastCreated->pItemData->dwItemFlags=serializedStack[5]|0x80000;
    Entry(lastCreated).quantity=int(serializedStack[1]);*used=size;return lastCreated;
}
void __fastcall StackMode(Unit* item,int mode){item->dwItemMode=mode;}
void __fastcall SendStack(const void* packet,int size){++sends;assert(size==36);PacketHook(&game,&player,static_cast<const uint8_t*>(packet),size);}
void StackBindings(){
    bindings[0x1e5620]=reinterpret_cast<uintptr_t>(StackQuantity);bindings[0x207610]=reinterpret_cast<uintptr_t>(SerializeStack);
    bindings[0x204dc0]=reinterpret_cast<uintptr_t>(ParseStack);bindings[0x2f5770]=reinterpret_cast<uintptr_t>(LoadStack);
    bindings[0x209c60]=reinterpret_cast<uintptr_t>(StackMode);bindings[0x10e710]=reinterpret_cast<uintptr_t>(SendStack);
}
Unit* PrepareSplit(int quantity=88,int page=0){
    Bind();StackBindings();Reset();splitTest=true;splitReady=true;serializationFails=parseFails=loadFails=false;
    table[2][0x13a]=1;memcpy(table[2].data()+0x80,"Y20 ",4);auto source=Make(2);source->pItemData->pExtraData.pParentInv=&inv;source->pItemData->nPage=uint8_t(page);
    source->pItemData->dwQualityNo=6;Entry(source).quantity=quantity;acceptedX=4;acceptedY=3;return source;
}
void FinishSplit(Unit* source,int amount){
    splitView.phase=SplitView::Submitting;
    auto packet=StackData::Encode({player.dwUnitId,source->dwUnitId,uint32_t(amount),splitView.total,splitView.token,StackData::Finish});
    PacketHook(&game,&player,packet.data(),int(packet.size()));
}
void StackSplitContract(){
    auto source=PrepareSplit();assert(CanUseHook(source,1));assert(!CanUseHook(source,2));UseHook(source,1);
    auto copy=lastCreated;assert(copy && splitView.phase==SplitView::Choosing && Entry(source).quantity==87 && Entry(copy).quantity==1);
    assert(splitView.iconCode=="Y20");
    assert(copy->dwClassId==source->dwClassId && copy->dwUnitId!=source->dwUnitId && copy->pItemData->dwQualityNo==6 && copy->pItemData->dwInitSeed && copy->pItemData->dwInitSeed!=source->pItemData->dwInitSeed);
    assert(copy->pItemData->pExtraData.pParentInv==&inv && source->pItemData->pExtraData.pParentInv==&inv && !removeCalls);
    assert(notifications.size()==3 && notifications[0].id==source->dwUnitId && notifications[0].command==4 && notifications[1].command==2 && notifications[2].id==copy->dwUnitId);
    UseHook(source,1);assert(sends==1); // repeated input cannot create another copy
    auto replay=StackData::Encode({player.dwUnitId,source->dwUnitId,1,88,splitView.token,StackData::Begin});PacketHook(&game,&player,replay.data(),36);assert(items.size()==2);
    FinishSplit(source,20);assert(splitView.phase==SplitView::Closed && Entry(source).quantity==68 && Entry(copy).quantity==20);
    PacketHook(&game,&player,replay.data(),36);assert(Entry(source).quantity==68 && Entry(copy).quantity==20 && items.size()==2);
    for(int page:{0,3,4}){source=PrepareSplit(88,page);UseHook(source,1);copy=lastCreated;assert(copy && copy->pItemData->nPage==page);FinishSplit(source,87);assert(Entry(source).quantity==1 && Entry(copy).quantity==87);}
    source=PrepareSplit(2);UseHook(source,1);FinishSplit(source,1);assert(Entry(source).quantity==1 && Entry(lastCreated).quantity==1);
    source=PrepareSplit(100000);UseHook(source,1);FinishSplit(source,70000);assert(Entry(source).quantity==30000 && Entry(lastCreated).quantity==70000);
    source=PrepareSplit();UseHook(source,1);CloseSplit(splitView.token);assert(Entry(source).quantity==87 && Entry(lastCreated).quantity==1);
    source=PrepareSplit();acceptedX=-1;UseHook(source,1);assert(Entry(source).quantity==88 && Entry(lastCreated).freed && splitView.phase==SplitView::Error && notifications.empty() && !groundCalls);
    for(int failure=0;failure<3;++failure){source=PrepareSplit();serializationFails=failure==0;parseFails=failure==1;loadFails=failure==2;UseHook(source,1);assert(Entry(source).quantity==88 && splitView.phase==SplitView::Error && notifications.empty());}
    for(int invalid=0;invalid<7;++invalid){
        source=PrepareSplit();UseHook(source,1);copy=lastCreated;
        if(invalid==0)Entry(source).quantity=86;
        if(invalid==1)Entry(copy).quantity=2;
        if(invalid==2)copy->pItemData->pExtraData.pParentInv=nullptr;
        if(invalid==3)source->pItemData->nPage=2;
        if(invalid==4)source->pItemData->dwCommandFlags=1;
        if(invalid==5)copy->pItemData->dwInitSeed++;
        if(invalid==6)player.dwAnimMode=17;
        const auto beforeSource=Entry(source).quantity,beforeCopy=Entry(copy).quantity;
        FinishSplit(source,20);assert(splitView.phase==SplitView::Error && Entry(source).quantity==beforeSource && Entry(copy).quantity==beforeCopy);
    }
    for(int invalid=0;invalid<8;++invalid){
        source=PrepareSplit();
        if(invalid==0)table[2][0x141]=1;
        if(invalid==1)Entry(source).quantity=1;
        if(invalid==2)source->pItemData->nPage=2;
        if(invalid==3)source->pItemData->pExtraData.pParentInv=nullptr;
        if(invalid==4)source->dwItemMode=1;
        if(invalid==5)player.dwAnimMode=0;
        if(invalid==6)splitReady=false;
        D2InventoryStrc children{};if(invalid==7){children.pFirstItem=source;source->pInventory=&children;}
        assert(!SplitEligible(&player,source));
    }
    source=PrepareSplit();clientHead=nullptr;assert(!CanUseHook(source,1));UseHook(source,1);assert(!sends);
    source=PrepareSplit();SetQuantityRightClicks(false);assert(!SplitEligible(&player,source) && !CanUseHook(source,1));UseHook(source,1);assert(!sends && items.size()==1);
    SetQuantityRightClicks(true);assert(SplitEligible(&player,source));UseHook(source,1);assert(splitView.phase==SplitView::Choosing);
    SetQuantityRightClicks(false);assert(splitView.phase==SplitView::Closed && Entry(source).quantity==87 && Entry(lastCreated).quantity==1);
    auto transmog=Source();assert(CanUseHook(transmog,1)); // conversion remains independent of the quantity toggle
    source=PrepareSplit();UseHook(source,1);splitView.opened-=60001;FinishSplit(source,20);assert(Entry(source).quantity==87 && Entry(lastCreated).quantity==1);
    source=PrepareSplit();UseHook(source,1);splitExpectedGame=nullptr;FinishSplit(source,20);assert(Entry(source).quantity==87 && Entry(lastCreated).quantity==1);
    source=PrepareSplit();UseHook(source,1);auto stale=StackData::Encode({player.dwUnitId,source->dwUnitId,20,88,splitView.token+1,StackData::Finish});
    splitView.phase=SplitView::Submitting;PacketHook(&game,&player,stale.data(),36);assert(Entry(source).quantity==87 && Entry(lastCreated).quantity==1);
    source=PrepareSplit();UseHook(source,1);auto token=splitView.token;
    auto window=CreateWindowExW(0,L"STATIC",L"stack-test",0,0,0,0,0,HWND_MESSAGE,nullptr,GetModuleHandleW(nullptr),nullptr);assert(window);
    SplitWindowMessage(window,WM_NULL,0,0);SetSplitInputRegion(token,{100,100,300,160},1920,1080,false);
    assert(!SplitCapturesInput(WM_KEYDOWN,0) && !SplitCapturesInput(WM_MOUSEMOVE,MAKELPARAM(20,20)));
    assert(SplitCapturesInput(WM_LBUTTONDOWN,MAKELPARAM(150,120)));
    assert(SplitCapturesInput(WM_MOUSEMOVE,MAKELPARAM(20,20)) && SplitCapturesInput(WM_LBUTTONUP,MAKELPARAM(20,20)));
    assert(!SplitCapturesInput(WM_MOUSEMOVE,MAKELPARAM(20,20)));
    SetSplitInputRegion(token,{100,100,300,160},1920,1080,true);assert(SplitCapturesInput(WM_CHAR,0));
    bool outside=false;assert(!SplitWindowMessage(window,WM_LBUTTONDOWN,0,MAKELPARAM(20,20),&outside) && outside);
    assert(splitView.phase==SplitView::Closed && Entry(source).quantity==87 && Entry(lastCreated).quantity==1);
    source=PrepareSplit();UseHook(source,1);token=splitView.token;
    SplitWindowMessage(window,WM_NULL,0,0);SubmitSplit(token,20);assert(splitView.phase==SplitView::Submitting);
    SplitWindowMessage(window,SplitSubmitMessage,0,0);assert(Entry(source).quantity==68 && Entry(lastCreated).quantity==20);
    DestroyWindow(window);
    for(uint32_t amount:{0u,88u,0xffffffffu}){StackData::Request request{77,100,amount,88,1,StackData::Finish},decoded;auto bytes=StackData::Encode(request);assert(!StackData::Decode(bytes,decoded));}
    auto bytes=StackData::Encode({77,100,20,88,1,StackData::Finish});StackData::Request decoded;assert(StackData::Decode(bytes,decoded));
    for(int offset:{6,7,8,9,15,16,17,30,31,32,33,34,35}){auto bad=bytes;bad[offset]^=1;assert(!StackData::Decode(bad,decoded));}
    splitTest=false;Bind();Reset();
    std::cout<<"Stack split: 88 -> 87+1 -> 68+20, identity/properties, personal inventory/cube/stash, no-room rollback, cancel, failure/replay/ownership gates and input-thread submission passed.\n";
}
uintptr_t stackImage=0;Unit* nativeStackSource=nullptr;
int __fastcall NativeStackZero(){return 0;}
int __fastcall NativeFullStack(Unit* item,ItemBits* bits,int saved,int gamble){
    assert(item==nativeStackSource && saved==1 && !gamble);
    auto write=reinterpret_cast<void(__fastcall*)(ItemBits*,uint32_t,int)>(stackImage+0x6c98d0);
    // Native save header fields, then enough opaque payload to exercise the
    // parser/load hand-off. Item-body serialization is a boundary here.
    write(bits,1,3);write(bits,0,3);write(bits,0,4);write(bits,2,4);write(bits,3,4);write(bits,1,3);write(bits,0,3);
    write(bits,uint32_t(Entry(item).quantity),32);return 0;
}
uint32_t __fastcall NativeStackCode(void*){return 0x20746c62;}
int __fastcall NativeStackCodeLookup(void*,uint32_t code){assert(code==0x20746c62);return 2;}
Unit* __fastcall NativeStackAllocate(int type,int base,int x,int y,Game* g,D2ActiveRoomStrc* room,uint16_t flags,int mode,int unknown){
    assert(type==4 && base==2 && x==2 && y==3 && g==&game && !room && flags==1 && mode==0 && !unknown);
    lastCreated=Make(uint32_t(base));return lastCreated;
}
int __fastcall NativeStackLoadBody(Unit* item,const void* bytes,size_t length,int saved,Game* g,int version,int* error){
    assert(item==lastCreated && bytes && length>8 && saved==1 && g==&game && version==98 && error);*error=0;
    *item->pItemData=*nativeStackSource->pItemData;item->pItemData->pExtraData.pParentInv=nullptr;
    Entry(item).quantity=Entry(nativeStackSource).quantity;return int(length);
}
void NativeStackCopyContract(uint8_t* image){
    auto observer=AddVectoredExceptionHandler(1,[](EXCEPTION_POINTERS* context)->LONG{
        std::cerr<<"Native stack fixture exception 0x"<<std::hex<<context->ExceptionRecord->ExceptionCode<<" at RVA 0x"<<(context->ContextRecord->Rip-stackImage)<<" accessing 0x"<<context->ExceptionRecord->ExceptionInformation[1]<<"\n";return EXCEPTION_CONTINUE_SEARCH;
    });
    auto stub=[&](uint32_t rva,auto function){const uint8_t jump[]{0xff,0x25,0,0,0,0};memcpy(image+rva,jump,6);auto target=reinterpret_cast<uintptr_t>(function);memcpy(image+rva+6,&target,8);};
    stackImage=reinterpret_cast<uintptr_t>(image);Reset();nativeStackSource=Source();nativeStackSource->dwClassId=2;Entry(nativeStackSource).quantity=88;
    nativeStackSource->pItemData->dwQualityNo=6;
    uint64_t count=4;memcpy(image+0x1ca0398,&count,8);memcpy(image+0x1ca0390,&tablePointer,8);
    stub(0x1ff7f0,NativeStackZero);stub(0x205990,NativeFullStack);stub(0x1f9530,NativeStackCode);stub(0x6d01a0,NativeStackCodeLookup);
    stub(0x2c42a0,NativeStackAllocate);stub(0x204f90,NativeStackLoadBody);stub(0x2f8660,NoOperation);stub(0x2f8740,NoOperation);stub(0x22e750,NoOperation);
    FlushInstructionCache(GetCurrentProcess(),image,0x2000000);
    auto originalFlags=nativeStackSource->pItemData->dwItemFlags;
    auto copy=CopyStack(&game,nativeStackSource);
    assert(copy && copy!=nativeStackSource && copy->dwUnitId!=nativeStackSource->dwUnitId && copy->dwClassId==2 &&
        copy->dwItemMode==4 && copy->pItemData->nPage==255 && !copy->pItemData->pExtraData.pParentInv && Entry(copy).quantity==88);
    assert(copy->pItemData->dwQualityNo==6 && nativeStackSource->pItemData->dwItemFlags==originalFlags);
    RemoveVectoredExceptionHandler(observer);
    std::cout<<"Actual native save wrapper/bit writer, saved-header parser, allocation/load wrapper and mode setter confirm stack-copy ABI, source flags and fresh identity. Item-body serializer/loader and allocator are test boundaries.\n";
}
