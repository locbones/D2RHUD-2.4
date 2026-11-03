// Private-image integration of indexed/multiple useitem matching, saved-item
// cloning, native consumption and the server publication boundary.
namespace CubeUseTest {
std::array<Unit,64> items{};std::array<D2ItemDataStrc,64> data{};
std::array<D2InventoryStrc,64> sockets{};std::array<bool,64> alive{};
struct Payload {uint32_t base,seed,quality,level,flags;int quantity,value,children;};
std::array<Payload,64> payload{};
std::array<std::array<uint8_t,0x1b4>,8> definitions{};
std::array<std::array<uint8_t,0x144>,512> costs{};
D2InventoryStrc inventory{};Unit owner{};struct NativeGameStorage {Game value{};std::array<uint8_t,0x6000> tail{};} gameStorage;Game& game=gameStorage.value;Unit cube{};Unit* bucketHead=nullptr;
int next=16,removed=0,serialized=0,loaded=0,attached=0,preflights=0;
int bulkGenerated=0,bulkFailGeneration=-1;
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
    // Native op18 reads the item's aggregate stat list. Candidate evaluation
    // copies the Unit, so model the shared stat-list payload rather than its
    // original Unit address.
    stub(0x1e2e70,+[](void* stats,uint32_t key,void*)->int{return stats&&key==(70u<<16)?static_cast<Payload*>(stats)->quantity:0;});
    stub(0x1e2e00,+[](void* stats,uint32_t key,void*)->int{return stats&&key==(70u<<16)?static_cast<Payload*>(stats)->quantity:0;});
    stub(0x1e56a0,+[](Unit* item,uint32_t stat,uint16_t)->int{return item&&stat==70&&item->pStatListEx?reinterpret_cast<Payload*>(item->pStatListEx)->quantity:0;});
    Reset(recipes[0],2);output(0,2);recipes[0].header[0x10]=26;
    for(size_t i=0;i<items.size();++i)items[i].pStatListEx=reinterpret_cast<D2StatListExStrc*>(&payload[i]);
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
    for(bool wide:{false,true}){
        UseCreateInvocation invocation;assert(invocation.Build(false,wide));const auto base=reinterpret_cast<DWORD64>(invocation.memory);DWORD64 found=0;
        auto function=RtlLookupFunctionEntry(base+0x302c,&found,nullptr);assert(function&&found==base);
        std::array<uint64_t,2048> stack{};auto entry=reinterpret_cast<uint8_t*>(stack.data()+1500);
        Put(entry,0,uint64_t(0x12345678));Put(entry-8,0,uint64_t(0x55aa));Put(entry-0x40,0,uint64_t(0x15));
        CONTEXT context{};context.Rip=base+0x302c;context.Rsp=reinterpret_cast<DWORD64>(entry-(wide?0x1968:0x1768)-96);context.Rbp=reinterpret_cast<DWORD64>(entry-(wide?0x1868:0x1668));
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
    {
        Reset(recipes[0]);WideRecipe wide(recipes[0]);UseTransaction seven{&game,&owner};
        for(size_t i=0;i<7;++i){wide.outputs[i].type=254;wide.outputs[i].mods[0]={44,0,int16_t(i+1),int16_t(i+1),0};seven.inputs[i]={&items[i],-1,50};}
        UseScope use(seven);UseCreateInvocation invocation;assert(invocation.Build(false,true));nativeSevenActive=true;
        reinterpret_cast<Create>(invocation.memory)(&game,&owner,reinterpret_cast<Recipe*>(&wide),seven.inputs.data());nativeSevenActive=false;
        assert(published.size()==7&&!removed);for(size_t i=0;i<7;++i)assert(published[i]==100+i&&payload[i].value==43+2*i);
        std::cout<<"Native seven-output frame preserves all seven source GUIDs and applies every independent modifier.\n";
    }
    {
        const auto savedNames=CompiledNames::database.load();const bool savedExtended=extendedOutputs;SetExtendedOutputs(true);
        auto names=std::make_shared<CompiledNames::Database>();(*names)[0x1c9ebd8]["test-property"]=44;CompiledNames::database=names;global(0x1c9ebd8,0);
        stub(0x1448c80,&strcspn);stub(0x143d184,+[](const char* text)->int{return atoi(text);});
        Reset(recipes[0]);for(int i=0;i<3;++i){output(i,i+1);recipes[0].outputs[i].mods[0]={44,0,int16_t(i+1),int16_t(i+1),0};}
        sourceTable=ParseSource("enabled\tnuminputs\toutput\toutput b\toutput c\toutput d\td lvl\td mod 1\td mod 1 min\td mod 1 max\toutput e\te mod 1\te mod 1 min\te mod 1 max\toutput f\toutput g\n"
            "1\t7\tuseitem1\tuseitem2\tuseitem3\tuseitem4\t80\ttest-property\t4\t4\tuseitem5\ttest-property\t5\t5\tuseitem6\tuseitem7\n");
        create();assert(published==std::vector<uint32_t>({100,101,102,103,104,105,106})&&!removed);
        assert(payload[3].value==49&&payload[4].value==51);
        sourceTable={};SetExtendedOutputs(savedExtended);CompiledNames::database=savedNames;
        std::cout<<"D-G source columns compile levels/modifiers and preserve all seven matched input GUIDs through the public hook.\n";
    }
    {
        const bool oldBulk=bulkOutputs;const bool oldExtended=extendedOutputs;const auto oldNames=CompiledNames::database.load();SetBulkOutputs(true);
        std::array<uint8_t,6*32> grids{};std::array<uint8_t,0xb00> playerStats{};static std::array<bool,12> occupied{};
        stub(0x1e2e70,+[](void* stats,uint32_t key,void*)->int{if(key==(12u<<16))return 99;return stats&&key==(70u<<16)?static_cast<Payload*>(stats)->quantity:0;});
        auto resetBulk=[&](int count=1){Reset(recipes[0],count);bulkGenerated=0;bulkFailGeneration=-1;grids={};occupied={};grids[5*32+16]=4;grids[5*32+17]=3;
            inventory.pGrids=reinterpret_cast<D2InventoryGridStrc*>(grids.data());Put(&inventory,0x28,uint64_t(6));owner.pStatListEx=reinterpret_cast<D2StatListExStrc*>(playerStats.data());
            for(auto& definition:definitions){definition[0x13a]=0;definition[0x116]=definition[0x117]=1;}
        };
        auto generatedOutput=[&](int slot,int base,int quantity,int modifier=0){auto& out=recipes[0].outputs[slot];out={};out.type=252;out.base=uint16_t(base);out.quantity=uint8_t(quantity);out.quality=6;out.level=50;if(modifier)out.mods[0]={44,0,int16_t(modifier),int16_t(modifier),0};};
        stub(0x2f58c0,+[](Game* g,LegacyTransmog::Data::Drop* drop,int)->Unit*{
            assert(g==&game);const int ordinal=bulkGenerated++;if(ordinal==bulkFailGeneration)return nullptr;
            // Native generation may return an unidentified item. Identification
            // happens later, after cube placement and before client publication.
            const int index=next++;assert(index<int(items.size()));Initialize(index,{uint32_t(drop->id),uint32_t(0xaabb00+ordinal),uint32_t(drop->quality),uint32_t(drop->level),0,1,42,0});
            reinterpret_cast<uint8_t*>(g)[0x3640]|=1;return &items[index];
        });
        stub(0x6bc3b0,+[]()->const char*{return "test";});stub(0x6bc0f0,+[](int,const char*,const char*,int line){std::cerr<<"Native assertion line "<<line<<std::endl;});stub(0x143d4e4,+[](int){abort();});
        stub(0x1fdb30,+[](Unit*)->int{return 500;});
        stub(0x1e5440,+[](Unit* item,uint32_t stat,int value,uint16_t){assert(stat==70);payload[Index(item)].quantity=value;});
        const auto oldBulkAdd=originalAdd;
        originalNotify=+[](Game* g,Unit* player,Unit* item,uint32_t flags,uint32_t command,uint32_t,uint32_t,uint32_t,uint32_t){
            assert(g==&game&&player==&owner);if(command==2){if(bulkTransaction){assert(flags&1);assert(item->pItemData->dwItemFlags&0x10);}published.push_back(item->dwUnitId);}
        };
        originalAdd=+[](Unit* player,const Location* places,const uint32_t* ids,size_t count,bool)->bool{
            assert(player==&owner);for(size_t n=0;n<count;++n){auto item=Find(ids[n]);assert(item);
                if(bulkTransaction){assert(places[n].mode==0&&places[n].page==3&&places[n].x<4&&places[n].y<3);
                    const int w=definitions[item->dwClassId][0x116],h=definitions[item->dwClassId][0x117];assert(places[n].x+w<=4&&places[n].y+h<=3);
                    for(int y=0;y<h;++y)for(int x=0;x<w;++x){const int at=(places[n].y+y)*4+places[n].x+x;assert(!occupied[at]);occupied[at]=true;}}
                item->dwItemMode=0;item->pItemData->nPage=3;item->pItemData->pExtraData.pParentInv=&inventory;placed.push_back(ids[n]);
            }return true;
        };
        resetBulk();generatedOutput(0,1,3,5);const auto unchangedRecipe=recipes[0];create();
        assert(preflights==1&&!alive[0]&&published.size()==3&&bulkGenerated==3&&!memcmp(&unchangedRecipe,&recipes[0],sizeof(Recipe)));
        std::set<uint32_t> seeds;for(auto id:published){auto item=Find(id);assert(item&&payload[Index(item)].value==47);seeds.insert(payload[Index(item)].seed);}assert(seeds.size()==3);
        resetBulk();for(int slot=0;slot<3;++slot)generatedOutput(slot,slot+1,2,slot+1);create();assert(published.size()==6&&bulkGenerated==6&&!alive[0]);
        std::map<uint32_t,int> copies;for(auto id:published){auto item=Find(id);++copies[item->dwClassId];assert(payload[Index(item)].value==42+int(item->dwClassId));}assert(copies[1]==2&&copies[2]==2&&copies[3]==2);
        resetBulk();generatedOutput(0,1,2);generatedOutput(1,2,1);definitions[1][0x116]=2;definitions[2][0x116]=2;definitions[2][0x117]=2;create();assert(published.size()==3);
        for(int failure=0;failure<3;++failure){
            resetBulk();generatedOutput(0,1,failure==0?13:3,5);
            if(failure==1){definitions[1][0x116]=2;definitions[1][0x117]=2;}if(failure==2)bulkFailGeneration=1;
            const auto original=payload[0];create();assert(preflights==0&&alive[0]&&published.empty()&&payload[0].value==original.value&&payload[0].quantity==original.quantity&&inventory.pFirstItem==&items[0]);
            assert(reinterpret_cast<uint8_t*>(&game)[0x3640]==0);for(int i=16;i<next;++i)assert(!alive[i]);
        }
        resetBulk();generatedOutput(0,1,7);definitions[1][0x13a]=1;create();assert(published.size()==1&&bulkGenerated==1&&payload[Index(Find(published[0]))].quantity==7);
        resetBulk();generatedOutput(0,1,7);definitions[1][0x13a]=1;generatedOutput(1,2,2);create();assert(published.size()==3&&bulkGenerated==3);
        auto stack=std::find_if(published.begin(),published.end(),[](auto id){return Find(id)->dwClassId==1;});assert(stack!=published.end()&&payload[Index(Find(*stack))].quantity==7);
        for(bool preserving:{false,true}){
            resetBulk();if(preserving){output(0,1);recipes[0].outputs[0].quantity=3;}else generatedOutput(0,1,3);
            permit=false;create();assert(preflights==1&&alive[0]&&published.empty()&&placed.empty());for(int i=16;i<next;++i)assert(!alive[i]);
        }
        resetBulk();generatedOutput(0,1,3);recipes[0].outputs[0].flags=0x20;create();assert(!bulkGenerated&&!preflights&&alive[0]&&published.empty());
        resetBulk();output(0,1);recipes[0].outputs[0].quantity=3;recipes[0].outputs[0].mods[0]={44,0,5,5,0};create();
        assert(alive[0]&&published.size()==3&&published[0]==100&&payload[0].value==47);for(auto id:published)assert(payload[Index(Find(id))].value==47);
        resetBulk(7);output(2,3);recipes[0].outputs[2].quantity=3;create();assert(alive[2]&&published.size()==3&&published[0]==102);
        resetBulk();data[0].dwItemFlags&=~0x10u;output(0,1);recipes[0].outputs[0].quantity=3;create();
        assert(published.size()==3);for(auto id:published)assert(Find(id)->pItemData->dwItemFlags&0x10);
        for(bool full:{false,true}){
            resetBulk();generatedOutput(0,1,full?13:3);generatedOutput(1,2,7);definitions[2][0x13a]=1;
            QuestJournal::RecipeGate gate{};QuestOutputScope journal(&game,&owner,gate,recipes[0]);questOutput=&journal;create();questOutput=nullptr;
            assert(journal.Outcome()==(full?QuestJournal::Outcome::Rejected:QuestJournal::Outcome::Committed));
        }
        resetBulk();generatedOutput(0,1,3);SetBulkOutputs(false);create();assert(published.size()==1&&bulkGenerated==1);SetBulkOutputs(true);
        SetExtendedOutputs(true);
        auto extraNames=std::make_shared<CompiledNames::Database>();(*extraNames)[0x1c9ebd8]["test-property"]=44;(*extraNames)[0x1c9fbe8]["weap"]=2;CompiledNames::database=extraNames;global(0x1c9fbe8,0);
        ExtraOutputSource symbols;symbols.command="weap,mag,qty=2";symbols.levels={"50","25","10"};
        for(auto& mod:symbols.mods)mod={"test-property","-1","-10","20","35"};
        Output encoded;assert(CompileExtra(symbols,encoded,0,6));assert(encoded.type==253&&encoded.base==2&&encoded.quality==4&&encoded.quantity==2);
        assert(encoded.level==50&&encoded.playerLevel==25&&encoded.itemLevel==10);
        for(const auto& mod:encoded.mods)assert(mod.property==44&&mod.param==65535&&mod.min==-10&&mod.max==20&&mod.chance==35);
        auto sevenSource=[&](const std::string& last){
            sourceTable=ParseSource("enabled\tnuminputs\toutput\toutput b\toutput c\toutput d\toutput e\toutput f\toutput g\tg mod 1\tg mod 1 min\tg mod 1 max\n"
                "1\t7\tuseitem1\tuseitem2\tuseitem3\tuseitem4\tuseitem5\tuseitem6\t"+last+"\ttest-property\t7\t7\n");
        };
        resetBulk(7);for(int i=0;i<3;++i)output(i,i+1);sevenSource("useitem7,qty=3");create();
        assert(published.size()==9&&published[6]==106);for(auto id:published)assert(Find(id)->pItemData->dwItemFlags&0x10);
        assert(payload[6].value==55);for(size_t i=7;i<9;++i)assert(payload[Index(Find(published[i]))].value==55);
        resetBulk(7);for(int i=0;i<3;++i)output(i,i+1);sevenSource("useitem7,qty=7");create();
        assert(published.empty()&&inventory.pFirstItem==&items[0]);for(int i=0;i<7;++i)assert(alive[i]&&payload[i].value==42+i);
        resetBulk(7);for(int i=0;i<3;++i)output(i,1);
        sourceTable=ParseSource("enabled\tnuminputs\toutput\toutput b\toutput c\toutput d\toutput e\toutput f\toutput g\n1\t7\tuseitem\tuseitem\tuseitem\tuseitem\tuseitem\tuseitem\tuseitem\n");
        create();assert(published.size()==7&&published[0]==100&&serialized==6);for(auto id:published)assert(payload[Index(Find(id))].value==42);
        // The same source metadata is accepted while disabled: only native A-C run.
        resetBulk(7);for(int i=0;i<3;++i)output(i,i+1);sevenSource("useitem7");SetExtendedOutputs(false);create();assert(published.size()==3&&removed==4);SetExtendedOutputs(true);
        // Invalid extra fields refuse the transaction before native consumption.
        resetBulk(7);for(int i=0;i<3;++i)output(i,i+1);sevenSource("useitem7");
        sourceTable.extended.at(0).outputs[3].levels[0]="bad";create();assert(!removed&&published.empty());
        // Ordinary D-G generation uses the loaded BIN definitions, including
        // all three level formulas and independent quality/quantity/mod fields.
        resetBulk(7);for(int i=0;i<3;++i)generatedOutput(i,i+1,1);
        sourceTable=ParseSource("enabled\tnuminputs\toutput\toutput d\td lvl\toutput e\te plvl\toutput f\tf ilvl\toutput g\tg lvl\tg mod 1\tg mod 1 min\tg mod 1 max\n"
            "1\t7\t\te00,rar\t60\tf00,mag\t50\tg00,uni\t50\th00,rar\t70\ttest-property\t7\t7\n");
        create();assert(published.size()==7&&bulkGenerated==7&&removed==7);
        assert(Find(published[3])->pItemData->dwItemLevel==60&&Find(published[4])->pItemData->dwItemLevel==49&&Find(published[5])->pItemData->dwItemLevel==25);
        assert(payload[Index(Find(published[6]))].value==49);for(auto id:published)assert(Find(id)->pItemData->dwItemFlags&0x10);
        auto ordinarySource=sourceTable;
        resetBulk(7);for(int i=0;i<3;++i)generatedOutput(i,i+1,1);sourceTable=ordinarySource;
        sourceTable.extended.at(0).outputs[3].command="h00,rar,qty=3";create();assert(published.size()==9&&bulkGenerated==9);
        resetBulk(7);for(int i=0;i<3;++i)generatedOutput(i,i+1,1);sourceTable=ordinarySource;
        sourceTable.extended.at(0).outputs[0].command="e00,qty=7";definitions[4][0x13a]=1;sourceTable.extended.at(0).outputs[3].command="h00,qty=2";
        create();assert(published.size()==8&&bulkGenerated==8);assert(payload[Index(Find(published[3]))].quantity==7);
        // D-G-only recipes are valid; source selection still follows input columns.
        resetBulk(7);sevenSource("useitem7");sourceTable.useItems.at(0).outputs[0]={};sourceTable.useItems.at(0).outputs[1]={};sourceTable.useItems.at(0).outputs[2]={};
        create();assert(published==std::vector<uint32_t>({103,104,105,106})&&removed==3);
        resetBulk(7);sevenSource("useitem7");for(size_t i=0;i<6;++i)sourceTable.useItems.at(0).outputs[i]={};
        for(size_t i=0;i<3;++i)sourceTable.extended.at(0).outputs[i]={};create();assert(published==std::vector<uint32_t>{106}&&removed==6);
        sourceTable={};SetExtendedOutputs(oldExtended);CompiledNames::database=oldNames;
        // Both server request phases still use the native matcher and the
        // requesting player's cube. Bulk generation runs only during commit.
        QuestNative::Invocation serverMatcher;assert(serverMatcher.Build(recipes.data(),recipes.size()));originalRuneMatch=reinterpret_cast<RuneMatcher>(serverMatcher.memory);
        stub(0x308840,RuneMatchHook);stub(0x306c50,CreateHook);FlushInstructionCache(GetCurrentProcess(),image,0x2000000);
        resetBulk(7);generatedOutput(0,1,3);cube={};cube.dwUnitType=4;cube.dwUnitId=900;Put(&cube,0x150,bucketHead);bucketHead=&cube;
        assert(phase(false)==4&&published.empty()&&!bulkGenerated);placed.clear();assert(phase(true)==0&&published.size()==3&&bulkGenerated==3);
        SetExtendedOutputs(true);CompiledNames::database=extraNames;resetBulk(7);for(int i=0;i<3;++i)output(i,i+1);sevenSource("useitem7,qty=2");
        cube={};cube.dwUnitType=4;cube.dwUnitId=900;Put(&cube,0x150,bucketHead);bucketHead=&cube;
        assert(phase(false)==4&&published.empty()&&!serialized&&!bulkGenerated);placed.clear();assert(phase(true)==0&&published.size()==8&&published[6]==106);
        sourceTable={};SetExtendedOutputs(oldExtended);CompiledNames::database=oldNames;
        memcpy(image+0x308840,QuestNative::original.data(),14);memcpy(image+0x306c50,useCreateSource.data(),14);oldCode.erase(0x308840);oldCode.erase(0x306c50);
        FlushInstructionCache(GetCurrentProcess(),image,0x2000000);originalAdd=oldBulkAdd;SetBulkOutputs(oldBulk);
        std::cout<<"Native A-G outputs, all five modifiers, BIN symbol resolution, level formulas, identified bulk copies, mixed stacks, cube capacity/geometry/generation refusal, source GUIDs, disabled fallback and server prepare/commit passed.\n";
    }
    {
        // Seven six-socket outputs can return 42 separate socket contents.
        // Exercise the native auxiliary array beyond its old 18-item limit.
        Reset(recipes[0]);WideRecipe wide(recipes[0]);UseTransaction seven{&game,&owner};
        std::array<std::array<uint8_t,0x30>,64> paths{};
        static std::array<uint8_t,0x480> playerData{};
        stub(0x20c230,+[](Unit*)->void*{return playerData.data();});
        stub(0x20fca0,+[](D2InventoryStrc* inv)->Unit*{return inv?inv->pFirstItem:nullptr;});
        stub(0x20efc0,+[](Unit* item){auto inv=item->pItemData->pExtraData.pParentInv;assert(inv&&inv->pFirstItem==item);inv->pFirstItem=item->pItemData->pExtraData.pNextItem;item->pItemData->pExtraData.pParentInv=nullptr;item->pItemData->pExtraData.pNextItem=nullptr;});
        for(int i=0;i<7;++i){
            wide.outputs[i].type=254;wide.outputs[i].flags=0x20;seven.inputs[i]={&items[i],-1,50};
            sockets[i].dwSignature=0x1020304;sockets[i].pOwner=&items[i];items[i].pInventory=&sockets[i];
            for(int n=0;n<6;++n){const int index=7+i*6+n;Initialize(index,{7,uint32_t(0xabcd+index),2,20,0x10,1,123,0});
                items[index].pStaticPath=reinterpret_cast<D2StaticPathStrc*>(paths[index].data());
                data[index].pExtraData.pParentInv=&sockets[i];data[index].pExtraData.pNextItem=n<5?&items[index+1]:nullptr;
            }
            sockets[i].pFirstItem=&items[7+i*6];
        }
        UseScope use(seven);UseCreateInvocation invocation;assert(invocation.Build(false,true));nativeSevenActive=true;
        reinterpret_cast<Create>(invocation.memory)(&game,&owner,reinterpret_cast<Recipe*>(&wide),seven.inputs.data());nativeSevenActive=false;
        assert(published.size()==49&&!removed);for(int i=0;i<49;++i)assert(alive[i]&&data[i].pExtraData.pParentInv==&inventory);
        std::cout<<"Seven native unsocket outputs safely return all 42 socket contents.\n";
    }
    for(const auto& [rva,bytes]:oldCode)memcpy(image+rva,bytes.data(),bytes.size());for(const auto& [rva,value]:oldData)Put(image,rva,value);
    FlushInstructionCache(GetCurrentProcess(),image,0x2000000);sourceTable=savedSources;sourcePath=savedPath;useMatch=savedUse;SetMultipleUseOutputs(oldOutputs);SetMultipleUseInputs(oldInputs);installed=oldInstalled;
    originalCreate=oldCreate;originalAdd=oldAdd;originalNotify=oldNotify;originalRuneMatch=oldMatch;::testQuestRules=oldRules;QuestNative::ready=oldReady;
    std::cout<<"Native useitem1..7 matching, independent A/B/C copies, original GUID preservation, quantities/socket contents, per-output modifiers, full client publication, disabled/missing/stale inputs and copy/preflight/placement cleanup passed.\n";
}
}
