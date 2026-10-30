// Private-image integration of indexed/multiple useitem matching, saved-item
// cloning, native consumption and the server publication boundary.
namespace CubeUseTest {
std::array<Unit,64> items{};std::array<D2ItemDataStrc,64> data{};
std::array<D2InventoryStrc,64> sockets{};std::array<bool,64> alive{};
struct Payload {uint32_t base,seed,quality,level,flags;int quantity,value,children;};
std::array<Payload,64> payload{};
std::array<std::array<uint8_t,0x1b4>,8> definitions{};
std::array<std::array<uint8_t,0x144>,512> costs{};
D2InventoryStrc inventory{};Unit owner{};Game game{};Unit cube{};Unit* bucketHead=nullptr;
int next=16,removed=0,serialized=0,loaded=0,attached=0,preflights=0;
bool permit=true,serializeFailure=false,parseFailure=false,loadFailure=false,attachFailure=false;
int failOutput=-1;std::vector<uint32_t> placed,published;
int Index(const Unit* item){for(int i=0;i<int(items.size());++i)if(item==&items[i])return i;assert(false);return -1;}
template<class T>void Put(void* p,size_t offset,T value){memcpy(static_cast<uint8_t*>(p)+offset,&value,sizeof(value));}
Unit* Find(uint32_t id){for(int i=0;i<int(items.size());++i)if(alive[i]&&items[i].dwUnitId==id)return &items[i];return nullptr;}
void Destroy(Unit* item){
    const int i=Index(item);assert(alive[i]);
    if(item->pInventory)for(auto child=item->pInventory->pFirstItem;child;){auto nextItem=child->pItemData->pExtraData.pNextItem;Destroy(child);child=nextItem;}
    auto link=&inventory.pFirstItem;
    for(size_t n=0;*link&&n<64;++n){if(*link==item){*link=item->pItemData->pExtraData.pNextItem;break;}link=&(*link)->pItemData->pExtraData.pNextItem;}
    auto bucket=&bucketHead;while(*bucket){if(*bucket==item){*bucket=QuestNative::Field<Unit*>(item,0x150);break;}bucket=reinterpret_cast<Unit**>(reinterpret_cast<uint8_t*>(*bucket)+0x150);}
    alive[i]=false;item->pItemData->pExtraData.pParentInv=nullptr;++removed;
}
void Initialize(int i,Payload value){
    auto& item=items[i];item={};data[i]={};sockets[i]={};payload[i]=value;alive[i]=true;
    Put(&item,0x150,bucketHead);bucketHead=&item;
    item.dwUnitType=4;item.dwUnitId=uint32_t(100+i);item.dwClassId=value.base;item.pItemData=&data[i];
    data[i].dwInitSeed=value.seed;data[i].dwQualityNo=value.quality;data[i].dwItemLevel=value.level;data[i].dwItemFlags=value.flags;
}
void Reset(Recipe& recipe,int count=7){
    items={};data={};sockets={};alive={};payload={};owner={};inventory={};game={};bucketHead=nullptr;
    next=16;removed=serialized=loaded=attached=preflights=0;permit=true;serializeFailure=parseFailure=loadFailure=attachFailure=false;failOutput=-1;placed.clear();published.clear();useMatch={};useTransaction=nullptr;
    owner.dwUnitType=0;owner.dwUnitId=55;owner.dwAnimMode=1;owner.pInventory=&inventory;game.bExpansion=1;
    inventory.dwSignature=0x1020304;inventory.pOwner=&owner;inventory.pFirstItem=&items[0];
    for(int i=0;i<count;++i){Initialize(i,{uint32_t(i),uint32_t(0x123400+i),6,50,0x10,1,42+i,0});data[i].nPage=3;data[i].pExtraData.pParentInv=&inventory;data[i].pExtraData.pNextItem=i+1<count?&items[i+1]:nullptr;}
    recipe={};recipe.header[0]=1;recipe.header[3]=255;recipe.header[0x10]=uint8_t(count);
    for(int i=0;i<count;++i){Put(recipe.header.data(),0x14+i*8,uint16_t(1));Put(recipe.header.data(),0x16+i*8,uint16_t(i));}
}
int __fastcall Save(Unit* item,UseItemBits* bits,int saved,int children,int gamble){
    assert(saved==1&&children==1&&!gamble&&bits->bits==128*1024*8);++serialized;
    auto value=payload[Index(item)];value.seed=item->pItemData->dwInitSeed;value.quality=item->pItemData->dwQualityNo;value.level=item->pItemData->dwItemLevel;value.flags=item->pItemData->dwItemFlags;
    memcpy(bits->data,&value,sizeof(value));size_t length=sizeof(value);
    if(value.children){assert(item->pInventory);for(auto child=item->pInventory->pFirstItem;child;child=child->pItemData->pExtraData.pNextItem){const auto childValue=payload[Index(child)];memcpy(bits->data+length,&childValue,sizeof(childValue));length+=sizeof(childValue);}}
    bits->bytes=length;bits->error=serializeFailure;return int(length);
}
int __fastcall ParseHeader(const void* bytes,size_t length,int saved,void* header,int version){
    assert(saved==1&&version==98);if(parseFailure||length<sizeof(Payload))return 0;
    Payload value;memcpy(&value,bytes,sizeof(value));Put(header,0,value.base);Put(header,0x14,int32_t(value.children));return 1;
}
Unit* __fastcall Load(Game* g,D2ActiveRoomStrc* room,const void* bytes,int length,int flags,void* header,int* used,int version){
    assert(g==&game&&!room&&!flags&&version==98);if(loadFailure)return nullptr;assert(length>=sizeof(Payload));
    Payload value;memcpy(&value,bytes,sizeof(value));assert(QuestNative::Field<uint32_t>(header,0)==value.base);
    const int i=next++;assert(i<int(items.size()));Initialize(i,value);*used=sizeof(Payload);++loaded;return &items[i];
}
void Native(uint8_t* image){
    const bool oldReady=QuestNative::ready;assert(InitQuestMatcher());
    const auto savedSources=sourceTable;const auto savedPath=sourcePath;const auto savedUse=useMatch;
    const bool oldOutputs=multipleUseOutputs,oldInputs=multipleUseInputs,oldInstalled=installed;
    const auto oldCreate=originalCreate;const auto oldAdd=originalAdd;const auto oldNotify=originalNotify;const auto oldMatch=originalRuneMatch;
    const auto oldRules=::testQuestRules;::testQuestRules.reset();sourceTable={};sourcePath.clear();
    std::map<uint32_t,std::array<uint8_t,14>> oldCode;std::map<uint32_t,uint64_t> oldData;
    auto stub=[&](uint32_t rva,auto fn){std::array<uint8_t,14> bytes{};memcpy(bytes.data(),image+rva,bytes.size());oldCode.emplace(rva,bytes);const uint8_t jump[]{0xff,0x25,0,0,0,0};memcpy(image+rva,jump,6);auto target=reinterpret_cast<uintptr_t>(fn);memcpy(image+rva+6,&target,8);};
    auto global=[&](uint32_t rva,uint64_t value){oldData.emplace(rva,QuestNative::Field<uint64_t>(image,rva));Put(image,rva,value);};
    assert(useCreateReady&&useCopyReady&&QuestNative::ready);
    definitions={};for(size_t i=0;i<definitions.size();++i)Put(definitions[i].data(),0x80,uint32_t(0x20303061+i));
    global(0x1ca0390,reinterpret_cast<uintptr_t>(definitions.data()));global(0x1ca0398,definitions.size());
    global(0x1c9fb78,reinterpret_cast<uintptr_t>(costs.data()));global(0x1c9fb80,costs.size());global(0x1bfa310,0);
    int maxLevel=99;global(0x1c9fd68,reinterpret_cast<uintptr_t>(&maxLevel));
    std::vector<Recipe> recipes(1);global(0x1ca0b18,reinterpret_cast<uintptr_t>(recipes.data()));global(0x1ca0b20,recipes.size());
    stub(0xae3a10,+[]()->int{return 0;});stub(0xae4920,+[](void* time,void*,int)->void*{static int fields[8]{0,0,0,2026,10,29,0,0};memcpy(time,fields,sizeof(fields));return time;});
    global(0x1baa468,reinterpret_cast<uintptr_t>(+[](void* time){memset(time,0,8);}));
    stub(0x208330,+[](Unit* player)->void*{return player->pInventory;});
    stub(0x1f9e70,+[](Unit* item)->uint32_t{return item==&cube?0x20786f62:QuestNative::Field<uint32_t>(definitions.at(item->dwClassId).data(),0x80);});
    stub(0x1e5620,+[](Unit* item,uint32_t stat,uint16_t)->int{if(item==&owner)return stat==12?99:0;return stat==70?payload[Index(item)].quantity:0;});
    stub(0x2c49c0,+[](Game* g,int type,uint32_t id)->Unit*{assert(g==&game&&type==4);return Find(id);});
    stub(0x2c4950,+[](int,Game*)->Unit**{return &bucketHead;});
    stub(0x213a00,+[](void* location)->void*{memset(location,0,16);return location;});
    stub(0x213a20,+[](Location* location,Unit*)->Location*{*location={};return location;});
    stub(0x2dba00,+[]()->bool{++preflights;return permit;});stub(0x1434c50,+[](){});
    stub(0x2f70a0,+[](Game* g,Unit* item){assert(g==&game);Destroy(item);});
    stub(0x22e750,+[](Unit*){});stub(0x209e40,+[]()->int{return 3;});
    stub(0x210200,+[](void*,Unit*,int,int* x,int* y,uint8_t)->int{*x=0;*y=0;return 1;});
    stub(0x1fe680,+[]()->int{return 0;});stub(0x2d5e10,AddHook);stub(0x316b80,NotifyHook);
    stub(0x207610,Save);stub(0x204dc0,ParseHeader);stub(0x2f5770,Load);
    stub(0x209c60,+[](Unit* item,int mode){item->dwItemMode=mode;});stub(0x2f8660,+[](Game*,Unit*){});stub(0x2f8740,+[](Game*,Unit*){});
    stub(0x2d7480,+[](Game* g,Unit*,uint32_t childId,uint32_t parentId)->int{
        assert(g==&game);if(attachFailure)return 0;auto child=Find(childId),parent=Find(parentId);assert(child&&parent);auto& inv=sockets[Index(parent)];inv.dwSignature=0x1020304;inv.pOwner=parent;inv.pFirstItem=child;parent->pInventory=&inv;child->pItemData->pExtraData.pParentInv=&inv;++attached;return 1;
    });
    stub(0x248c00,+[](int,int,Unit* item,void*,uintptr_t,uintptr_t,uintptr_t,const int32_t* property,uintptr_t,uintptr_t,void*){assert(property[2]==property[3]);payload[Index(item)].value+=property[2];});
    originalAdd=+[](Unit* player,const Location*,const uint32_t* ids,size_t count,bool)->bool{
        assert(player==&owner);for(size_t n=0;n<count;++n){if(int(ids[n])==failOutput)return false;auto item=Find(ids[n]);assert(item);item->dwItemMode=0;item->pItemData->nPage=3;item->pItemData->pExtraData.pParentInv=&inventory;placed.push_back(ids[n]);}return true;
    };
    originalNotify=+[](Game* g,Unit* player,Unit* item,uint32_t flags,uint32_t command,uint32_t,uint32_t,uint32_t,uint32_t){
        assert(g==&game&&player==&owner);if(command==2){assert(flags&1);published.push_back(item->dwUnitId);}
    };
    originalCreate=reinterpret_cast<Create>(image+0x306c50);originalRuneMatch=reinterpret_cast<RuneMatcher>(image+0x308840);installed=true;SetMultipleUseOutputs(true);SetMultipleUseInputs(true);
    FlushInstructionCache(GetCurrentProcess(),image,0x2000000);
    auto match=[&](){RuneMatch found;Input inputs[3]{};RuneMatchHook(&found,&game,&owner,inputs);return found.matched;};
    auto create=[&](){RuneMatch found;Input inputs[3]{};RuneMatchHook(&found,&game,&owner,inputs);assert(found.matched);CreateHook(&game,&owner,recipes.data()+found.row,inputs);};
    auto output=[&](int slot,int input){auto& out=recipes[0].outputs[slot];out={};out.type=254;out.special=UseInputMarker|input;};
    for(int source=1;source<=7;++source){
        Reset(recipes[0]);output(0,source);assert(match());assert(useMatch.inputs[source-1]==&items[source-1]);create();
        assert(alive[source-1]&&published==std::vector<uint32_t>{uint32_t(99+source)}&&removed==6&&!serialized);
    }
    Reset(recipes[0]);for(int out=0;out<3;++out){output(out,1);recipes[0].outputs[out].special=0;}
    SetMultipleUseInputs(false);create();assert(serialized==2&&published.size()==3&&published[0]==100);SetMultipleUseInputs(true);
    for(int slot=1;slot<3;++slot){Reset(recipes[0]);output(slot,2);create();assert(published==std::vector<uint32_t>{101}&&removed==6);}
    Reset(recipes[0]);output(1,1);output(2,1);recipes[0].outputs[1].special=recipes[0].outputs[2].special=0;
    SetMultipleUseInputs(false);create();assert(serialized==1&&published.size()==2&&published[0]==100);SetMultipleUseInputs(true);
    // Distinct outputs reuse their own actual input, with the corresponding
    // original GUID and independent levels, seed, quality and saved modifiers.
    Reset(recipes[0]);output(0,7);output(1,2);output(2,3);create();
    assert(published==std::vector<uint32_t>({106,101,102})&&removed==4&&!serialized);
    // Recipe 18: one quantity-25 stack plus one weapon are TWO native cube
    // items. Op 18 checks stack quantity; qty=25 instead requests 25 units.
    stub(0x1e56a0,+[](Unit* item,uint32_t stat,uint16_t)->int{return stat==70?payload[Index(item)].quantity:0;});
    Reset(recipes[0],2);output(0,2);recipes[0].header[0x10]=26;
    recipes[0].header[4]=18;Put(recipes[0].header.data(),8,int32_t(70));Put(recipes[0].header.data(),12,int32_t(25));
    definitions[0][0x13a]=1;payload[0].quantity=25;
    assert(!match()&&!removed&&published.empty());
    recipes[0].header[0x1b]=25;assert(!match());recipes[0].header[0x1b]=0;
    recipes[0].header[0x10]=2;payload[0].quantity=24;assert(!match());
    payload[0].quantity=25;assert(match()&&useMatch.inputs[1]==&items[1]);
    recipes[0].outputs[0].mods[0]={44,0,500,500,0};
    create();assert(!alive[0]&&alive[1]&&removed==1&&published==std::vector<uint32_t>{101}&&payload[1].value==543);
    definitions[0][0x13a]=0;
    std::cout<<"Recipe 18 quantity-25 stack + weapon: numinputs=26 rejects, numinputs=2 with op18/stat70/25 accepts, quantity24 rejects, useitem2 retains weapon and applies its output modifier.\n";
    // Indexed sources retain the native upgrade-base calculation for their
    // own definition, including when the upgraded output is in slot C.
    stub(0x6d01a0,+[](void*,uint32_t code)->int{for(size_t i=0;i<definitions.size();++i)if(QuestNative::Field<uint32_t>(definitions[i].data(),0x80)==code)return int(i);return -1;});
    global(0x1c9eba0,1);
    Reset(recipes[0]);output(2,7);recipes[0].outputs[2].flags=0x100;
    Put(definitions[6].data(),0x8c,QuestNative::Field<uint32_t>(definitions[7].data(),0x80));
    const auto upgraded=ResolveUseInput(&game,&owner,recipes[0],2,7,&items[6]);
    assert(upgraded&&upgraded->item==&items[6]&&upgraded->base==7);
    Put(definitions[6].data(),0x8c,uint32_t(0));
    // A/B/C from input 1 keep the original once and copy its pristine state.
    Reset(recipes[0]);for(int out=0;out<3;++out){output(out,1);recipes[0].outputs[out].mods[0]={44,0,int16_t(1+4*out),int16_t(1+4*out),0};}
    definitions[0][0x13a]=1;payload[0].quantity=37;Initialize(8,{7,0xaaaa,2,20,0x10,1,123,0});payload[0].children=1;
    sockets[0].pFirstItem=&items[8];sockets[0].pOwner=&items[0];items[0].pInventory=&sockets[0];data[8].pExtraData.pParentInv=&sockets[0];
    create();assert(serialized==2&&loaded==4&&attached==2&&published.size()==3&&std::set<uint32_t>(published.begin(),published.end()).size()==3);
    for(int out=0;out<3;++out){auto item=Find(published[out]);assert(item);const auto& value=payload[Index(item)];
        assert(value.quantity==37&&value.value==43+4*out&&item->pItemData->dwInitSeed==0x123400&&item->pItemData->dwQualityNo==6&&item->pItemData->dwItemLevel==50);
        assert(item->pInventory&&item->pInventory->pFirstItem&&payload[Index(item->pInventory->pFirstItem)].value==123);
    }
    definitions[0][0x13a]=0;
    // Disabled features, nonexistent descriptors and absent source slots do
    // not match, even before a native prepare could move ingredients.
    Reset(recipes[0]);output(0,7);SetMultipleUseInputs(false);assert(!match()&&!removed&&!serialized);SetMultipleUseInputs(true);
    output(1,2);SetMultipleUseOutputs(false);assert(!match()&&!removed&&!serialized);SetMultipleUseOutputs(true);
    Put(recipes[0].header.data(),0x44,uint16_t(0));assert(!match());
    for(int failure=0;failure<5;++failure){
        Reset(recipes[0]);output(0,1);output(1,1);output(2,1);
        serializeFailure=failure==0;parseFailure=failure==1;loadFailure=failure==2;permit=failure!=3;
        if(failure==4)failOutput=117;
        create();if(failure<3)assert(!removed&&!preflights&&published.empty());
        if(failure==3)assert(removed==2&&published.empty()&&alive[0]);
        for(int i=16;i<next;++i)if(alive[i])assert(items[i].pItemData->pExtraData.pParentInv==&inventory);
    }
    Reset(recipes[0]);output(0,1);output(1,1);Initialize(8,{7,0xaaaa,2,20,0x10,1,123,0});payload[0].children=1;
    sockets[0].pFirstItem=&items[8];sockets[0].pOwner=&items[0];items[0].pInventory=&sockets[0];data[8].pExtraData.pParentInv=&sockets[0];
    attachFailure=true;create();assert(!preflights&&published.empty()&&alive[0]&&alive[8]);for(int i=16;i<next;++i)assert(!alive[i]);
    {
        UseCreateInvocation invocation;assert(invocation.Build());const auto base=reinterpret_cast<DWORD64>(invocation.memory);DWORD64 found=0;
        auto function=RtlLookupFunctionEntry(base+0x302c,&found,nullptr);assert(function&&found==base);
        std::array<uint64_t,2048> stack{};auto entry=reinterpret_cast<uint8_t*>(stack.data()+1500);
        Put(entry,0,uint64_t(0x12345678));Put(entry-8,0,uint64_t(0x55aa));Put(entry-0x40,0,uint64_t(0x15));
        CONTEXT context{};context.Rip=base+0x302c;context.Rsp=reinterpret_cast<DWORD64>(entry-0x1768-96);context.Rbp=reinterpret_cast<DWORD64>(entry-0x1668);
        void* handler=nullptr;DWORD64 establisher=0;RtlVirtualUnwind(UNW_FLAG_NHANDLER,base,context.Rip,function,&context,&handler,&establisher,nullptr);
        assert(context.Rip==0x12345678&&context.Rsp==reinterpret_cast<DWORD64>(entry+8)&&context.Rbp==0x55aa&&context.R15==0x15);
    }
    // Replay the actual server-side inventory request in its prepare and
    // commit phases, with normal client item identifiers and cube coordinates.
    stub(0x28f510,+[](Game*){});global(0x149c3f0,reinterpret_cast<uintptr_t>(+[]()->DWORD{return 100;}));
    stub(0x308840,RuneMatchHook);stub(0x306c50,CreateHook);FlushInstructionCache(GetCurrentProcess(),image,0x2000000);
    Reset(recipes[0]);output(0,7);output(1,2);output(2,7);
    cube={};cube.dwUnitType=4;cube.dwUnitId=900;Put(&cube,0x150,bucketHead);bucketHead=&cube;
    std::array<uint8_t,43> request{};Put(request.data(),1,uint32_t(900));request[7]=6;
    for(int i=0;i<7;++i){Put(request.data(),8+i*5,uint32_t(100+i));request[12+i*5]=uint8_t(i);}
    auto phase=[&](bool commit){return reinterpret_cast<int(__fastcall*)(Game*,Unit*,void*,bool)>(image+0x2b3a70)(&game,&owner,request.data(),commit);};
    SetMultipleUseInputs(false);assert(phase(false)==1&&phase(true)==1&&placed.empty()&&published.empty()&&!serialized&&!removed);SetMultipleUseInputs(true);
    assert(phase(false)==4&&published.empty()&&!serialized&&!removed);placed.clear();
    assert(phase(true)==0&&published.size()==3&&published[0]==106&&published[1]==101&&published[2]!=106&&serialized==1);
    // Restore the request entry detours before direct matcher-only cases.
    memcpy(image+0x308840,QuestNative::original.data(),14);memcpy(image+0x306c50,useCreateSource.data(),14);
    oldCode.erase(0x308840);oldCode.erase(0x306c50);FlushInstructionCache(GetCurrentProcess(),image,0x2000000);
    // A stale selected pointer is rejected before the native commit.
    Reset(recipes[0]);output(0,7);assert(match());data[6].nPage=0;const auto before=removed;Input empty[3]{};CreateHook(&game,&owner,recipes.data(),empty);assert(removed==before&&published.empty());
    for(const auto& [rva,bytes]:oldCode)memcpy(image+rva,bytes.data(),bytes.size());for(const auto& [rva,value]:oldData)Put(image,rva,value);
    FlushInstructionCache(GetCurrentProcess(),image,0x2000000);sourceTable=savedSources;sourcePath=savedPath;useMatch=savedUse;SetMultipleUseOutputs(oldOutputs);SetMultipleUseInputs(oldInputs);installed=oldInstalled;
    originalCreate=oldCreate;originalAdd=oldAdd;originalNotify=oldNotify;originalRuneMatch=oldMatch;::testQuestRules=oldRules;QuestNative::ready=oldReady;
    std::cout<<"Native useitem1..7 matching, independent A/B/C copies, original GUID preservation, quantities/socket contents, per-output modifiers, full client publication, disabled/missing/stale inputs and copy/preflight/placement cleanup passed.\n";
}
}
