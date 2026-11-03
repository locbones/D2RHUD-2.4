namespace BagMergeTest {
template<size_t N>using Bytes=std::array<uint8_t,N>;
template<class T>void Put(void* p,size_t offset,T value){memcpy(static_cast<uint8_t*>(p)+offset,&value,sizeof(value));}
std::array<Unit,8> items{};std::array<D2ItemDataStrc,8> data{};std::array<bool,8> alive{};
std::array<Bytes<0xb58>,8> roots{};std::array<Bytes<0x68>,8> lists{};
struct Entry {uint32_t key;int value;bool operator==(const Entry&)const=default;};std::array<std::array<Entry,17>,8> values{};
std::array<Bytes<0x144>,16> costs{};Bytes<0x1b4> definition{};
D2InventoryStrc inventory{};Unit owner{},cube{};Unit* bucket=nullptr;
struct Storage {Game value{};Bytes<0x6000> tail{};} storage;Game& game=storage.value;
Recipe recipe{};uint8_t* itemTable=definition.data();uint8_t* costTable=costs[0].data();Recipe* recipeTable=&recipe;
uint64_t itemRows=1,costRows=16,recipeRows=1;uintptr_t noLinker=0;
// RMD-MP's current save/send encodings. These expected limits are independent
// of the production calculation, including the signed network and int32 caps.
struct Limit {uint8_t bits,send;uint32_t add;int maximum;};
constexpr std::array<Limit,16> limits{{
    {12,16,95,4000},{20,32,48575,1000000},{20,32,48575,1000000},{20,32,48575,1000000},{32,32,49152,INT_MAX},
    {14,14,6384,8191},{11,16,10,2037},{11,16,10,2037},
    {8,16,55,200},{8,16,55,200},{8,16,55,200},{8,16,55,200},{8,16,55,200},{8,16,55,200},
    {11,12,30,2017},{11,12,30,2017}
}};
int writes=0,consumed=0,creates=0,failStat=-1,preflights=0;bool placement=true,permit=true,generate=true;uint32_t published=0;
Unit* Find(uint32_t id){for(size_t n=0;n<items.size();++n)if(alive[n]&&items[n].dwUnitId==id)return &items[n];return nullptr;}
int __fastcall Stat(Unit* item,uint32_t stat,uint16_t layer){
    assert(!layer);if(item==&owner)return stat==12?99:0;
    if(stat==70)return 1;assert(item&&item->dwUnitId>=100&&item->dwUnitId<108);
    const auto n=item->dwUnitId-100;
    for(size_t i=0;i<RuneField<uint64_t>(lists[n].data(),0x38);++i)if(values[n][i].key==(stat<<16))return values[n][i].value;
    return 0;
}
int __fastcall Set(void* list,int stat,int value,uint16_t layer){
    assert(!layer&&stat>=0&&stat<16);++writes;
    for(size_t n=0;n<lists.size();++n)if(list==lists[n].data()){
        if(stat==failStat){failStat=-1;return 0;}
        auto count=RuneField<uint64_t>(lists[n].data(),0x38);
        for(size_t i=0;i<count;++i)if(values[n][i].key==(uint32_t(stat)<<16)){values[n][i].value=value;return 1;}
        assert(count<values[n].size());values[n][count++]={uint32_t(stat)<<16,value};Put(lists[n].data(),0x38,count);return 1;
    }
    assert(false);return 0;
}
void Destroy(Unit* item){
    assert(item&&alive[item->dwUnitId-100]);alive[item->dwUnitId-100]=false;++consumed;
    auto link=&inventory.pFirstItem;while(*link){if(*link==item){*link=item->pItemData->pExtraData.pNextItem;break;}link=&(*link)->pItemData->pExtraData.pNextItem;}
    auto next=&bucket;while(*next){if(*next==item){*next=RuneField<Unit*>(item,0x150);break;}next=reinterpret_cast<Unit**>(reinterpret_cast<uint8_t*>(*next)+0x150);}
    item->pItemData->pExtraData.pParentInv=nullptr;
}
bool __fastcall Add(Unit* player,const Location*,const uint32_t* ids,size_t count,bool){assert(player==&owner);for(size_t n=0;n<count;++n)assert(Find(ids[n]));return placement;}
void __fastcall Notify(Game*,Unit* player,Unit*,uint32_t flags,uint32_t command,uint32_t,uint32_t,uint32_t,uint32_t){assert(player==&owner);if(command==2)published=flags;}
void __fastcall Create(Game* g,Unit* player,Recipe*,Input* inputs){
    ++creates;Location location{};const auto id=inputs[0].item->dwUnitId;
    if(!generate){std::array<uint32_t,8> ids{};size_t count=0;for(auto item=inventory.pFirstItem;item;item=item->pItemData->pExtraData.pNextItem)ids[count++]=item->dwUnitId;AddHook(player,&location,ids.data(),count,false);return;}
    if(!AddHook(player,&location,&id,1,false))return;
    for(auto item=inventory.pFirstItem;item;){auto next=item->pItemData->pExtraData.pNextItem;if(item!=inputs[0].item)Destroy(item);item=next;}
    NotifyHook(g,player,inputs[0].item,0,2,0,3,0,0);
}
void Reset(int count=3,bool quantity=true){
    items={};data={};alive={};roots={};lists={};values={};costs={};definition={};owner={};cube={};inventory={};storage={};bucket=nullptr;
    writes=consumed=creates=preflights=0;failStat=-1;placement=permit=generate=true;published=0;useMatch={};useTransaction=nullptr;activeWideRecipe=nullptr;bagMergeTransaction=nullptr;
    owner.dwUnitType=0;owner.dwUnitId=55;owner.dwAnimMode=1;owner.pInventory=&inventory;game.bExpansion=1;
    inventory.dwSignature=0x1020304;inventory.pOwner=&owner;inventory.pFirstItem=&items[0];
    Put(definition.data(),0x80,uint32_t(0x2031305a));definition[0x116]=definition[0x117]=1;
    for(size_t n=0;n<costs.size();++n){costs[n][8]=32;costs[n][0x15]=20;Put(costs[n].data(),0x18,uint32_t(48575));}
    for(int n=0;n<count;++n){
        auto& item=items[n];item.dwUnitType=4;item.dwUnitId=100+n;item.pItemData=&data[n];alive[n]=true;
        Put(&item,0x150,bucket);bucket=&item;
        data[n].nPage=3;data[n].dwItemLevel=69;data[n].dwQualityNo=7;data[n].dwInitSeed=0x123400+n;data[n].dwItemFlags=0x10;
        data[n].pExtraData.pParentInv=&inventory;data[n].pExtraData.pNextItem=n+1<count?&items[n+1]:nullptr;
        item.pStatListEx=reinterpret_cast<D2StatListExStrc*>(roots[n].data());
        Put(roots[n].data(),0x1c,uint32_t(0x80000000));Put(roots[n].data(),0x68,lists[n].data());
        Put(lists[n].data(),0x1c,uint32_t(0x40));Put(lists[n].data(),0x30,values[n].data());Put(lists[n].data(),0x38,uint64_t(17));
        for(int stat=0;stat<16;++stat)values[n][stat]={uint32_t(stat)<<16,(n+1)*(stat+1)};
        values[n][16]={100u<<16,98765+n}; // Unrelated/quest state must not be summed.
    }
    recipe={};recipe.header[0]=1;recipe.header[3]=255;recipe.header[0x10]=uint8_t(count);recipe.outputs[0].type=254;
    for(int n=0;n<(quantity?1:count);++n){Put(recipe.header.data(),0x14+n*8,uint16_t(1));recipe.header[0x1b+n*8]=quantity?uint8_t(count):1;}
    auto names=std::make_shared<CompiledNames::Database>();
    for(size_t n=0;n<BagBalanceNames.size();++n)(*names)[0x1c9fb90][CompiledNames::Fold(BagBalanceNames[n])]=int(n);
    CompiledNames::database=names;sourceMod="RMD-MP";sourceTable={};sourcePath.clear();installed=true;
}
void Bind(){
    bindings={{0x1ca0390,uintptr_t(&itemTable)},{0x1ca0398,uintptr_t(&itemRows)},{0x1c9fb78,uintptr_t(&costTable)},{0x1c9fb80,uintptr_t(&costRows)},
        {0x1c9fb90,uintptr_t(&noLinker)},{0x1ca0b18,uintptr_t(&recipeTable)},{0x1ca0b20,uintptr_t(&recipeRows)},
        {0x1e5620,uintptr_t(Stat)},{0x1e5270,uintptr_t(Set)},
        {0x2c49c0,uintptr_t(+[](Game*,int,uint32_t id)->Unit*{return Find(id);})}};
    originalCreate=Create;originalAdd=Add;originalNotify=Notify;
}
void Boundary(int stat,bool overflow){
    Reset(2);const auto limit=limits[stat];costs[stat][0x15]=limit.bits;costs[stat][8]=limit.send;Put(costs[stat].data(),0x18,limit.add);
    values[0][stat].value=limit.maximum-(overflow?0:1);values[1][stat].value=1;
}
void Run(){
    const auto oldBindings=bindings;const auto oldCreate=originalCreate;const auto oldAdd=originalAdd;const auto oldNotify=originalNotify;
    const auto names=CompiledNames::database.load();const auto mod=sourceMod;const auto sources=sourceTable;const auto path=sourcePath;
    Bind();
    for(int count:{2,3,7})for(bool quantity:{true,false}){
        Reset(count,quantity);const auto before=values;auto metadata=data[0];metadata.pExtraData.pNextItem=nullptr;Input inputs[3]{{&items[0],0,69}};
        assert(MergeStorageBags(&game,&owner,&recipe,inputs));
        assert(creates==1&&consumed==count-1&&alive[0]&&(published&1)&&!bagMergeTransaction);
        for(int stat=0;stat<16;++stat)assert(values[0][stat].value==count*(count+1)/2*(stat+1));
        assert(values[0][16].value==98765&&!memcmp(&metadata,&data[0],sizeof(metadata)));
        for(int n=1;n<count;++n)assert(values[n]==before[n]);
    }
    for(int failure=0;failure<10;++failure){
        Reset();Input inputs[3]{{&items[0],0,69}};
        if(failure==0)failStat=8;
        if(failure==1)placement=false;
        if(failure==2)values[2][15].value=1000000;
        if(failure==3)values[1][0].value=-1;
        if(failure==4)CompiledNames::database=std::make_shared<CompiledNames::Database>();
        if(failure==5)costs[4][0x15]=0;
        if(failure==6)inputs[0].item=nullptr;
        if(failure==7)data[1].pExtraData.pParentInv=nullptr;
        if(failure==8)recipe.outputs[1].type=254;
        if(failure==9)generate=false;
        const auto before=values;assert(MergeStorageBags(&game,&owner,&recipe,inputs));
        assert(!consumed&&values==before&&!bagMergeTransaction);if(failure!=1&&failure!=9)assert(!creates);
    }
    Reset();for(auto& bag:values)for(int n=0;n<16;++n)bag[n].value=0;
    Input inputs[3]{{&items[0],0,69}};assert(MergeStorageBags(&game,&owner,&recipe,inputs)&&consumed==2&&!writes);
    Reset();sourceMod="OtherMod";assert(!MergeStorageBags(&game,&owner,&recipe,inputs)&&!writes&&!consumed);
    Reset();Put(definition.data(),0x80,uint32_t(0x2031307a));assert(!MergeStorageBags(&game,&owner,&recipe,inputs)&&!writes&&!consumed);
    Reset(1);assert(!MergeStorageBags(&game,&owner,&recipe,inputs)&&!writes&&!consumed);
    for(const auto levels:std::array<std::array<uint8_t,3>,4>{{{125,0,0},{0,100,0},{0,0,100},{0,50,50}}}){
        Reset(2);recipe.outputs[0].level=levels[0];recipe.outputs[0].playerLevel=levels[1];recipe.outputs[0].itemLevel=levels[2];
        assert(MergeStorageBags(&game,&owner,&recipe,inputs)&&consumed==1&&values[0][0].value==3);
        assert(recipe.outputs[0].level==levels[0]&&recipe.outputs[0].playerLevel==levels[1]&&recipe.outputs[0].itemLevel==levels[2]);
    }
    for(int stat=0;stat<int(limits.size());++stat)for(bool overflow:{false,true}){
        Boundary(stat,overflow);const auto before=values;Input chosen[3]{{&items[0],0,69}};
        assert(MergeStorageBags(&game,&owner,&recipe,chosen));
        if(overflow)assert(!writes&&!creates&&!consumed&&!published&&values==before&&alive[0]&&alive[1]);
        else assert(consumed==1&&(published&1)&&values[0][stat].value==limits[stat].maximum);
    }
    bindings=oldBindings;originalCreate=oldCreate;originalAdd=oldAdd;originalNotify=oldNotify;CompiledNames::database=names;sourceMod=mod;sourceTable=sources;sourcePath=path;
    std::cout<<"Storage bag merge: 2/3/7 bags, all 16 counters, exact-limit success and limit+1 refusal for every RMD-MP encoding (save/send/int32), empty bags, unrelated properties, mod/code scoping and save/placement refusal passed.\n";
}
void Native(uint8_t* image){
    const auto names=CompiledNames::database.load();const auto mod=sourceMod;const auto sources=sourceTable;const auto path=sourcePath;
    const auto oldCreate=originalCreate;const auto oldAdd=originalAdd;const auto oldNotify=originalNotify;const auto oldMatch=originalRuneMatch;
    const bool ready=QuestNative::ready,oldConditions=extendedConditions,oldInputs=multipleUseInputs,oldOutputs=multipleUseOutputs;
    const auto rules=::testQuestRules;::testQuestRules.reset();assert(InitQuestMatcher());
    std::map<uint32_t,std::array<uint8_t,14>> code;std::map<uint32_t,uint64_t> globals;
    auto stub=[&](uint32_t rva,auto fn){std::array<uint8_t,14> old{};memcpy(old.data(),image+rva,14);code.try_emplace(rva,old);const uint8_t jump[]{0xff,0x25,0,0,0,0};memcpy(image+rva,jump,6);auto target=reinterpret_cast<uintptr_t>(fn);memcpy(image+rva+6,&target,8);};
    auto global=[&](uint32_t rva,uint64_t value){globals.try_emplace(rva,RuneField<uint64_t>(image,rva));Put(image,rva,value);};
    global(0x1ca0390,uintptr_t(itemTable));global(0x1ca0398,itemRows);global(0x1c9fb78,uintptr_t(costTable));global(0x1c9fb80,costRows);global(0x1c9fb90,0);
    global(0x1ca0b18,uintptr_t(recipeTable));global(0x1ca0b20,1);global(0x1bfa310,0);int maxLevel=99;global(0x1c9fd68,uintptr_t(&maxLevel));
    global(0x149c3f0,uintptr_t(+[]()->DWORD{return 100;}));
    stub(0x1e5620,Stat);stub(0x1e5270,Set);
    auto aggregate=+[](void* root,uint32_t key,void*)->int{
        for(size_t n=0;n<roots.size();++n)if(root==roots[n].data())return Stat(&items[n],key>>16,uint16_t(key));
        return 0;
    };
    stub(0x1e2e70,aggregate);stub(0x1e2e00,aggregate);stub(0x1e56a0,Stat);
    stub(0x2c49c0,+[](Game*,int,uint32_t id)->Unit*{return Find(id);});stub(0x2c4950,+[](int,Game*)->Unit**{return &bucket;});
    stub(0x208330,+[](Unit* player)->void*{return player->pInventory;});
    stub(0x1f9e70,+[](Unit* item)->uint32_t{return item==&cube?0x20786f62:RuneField<uint32_t>(definition.data(),0x80);});
    stub(0x213a00,+[](void* location)->void*{memset(location,0,16);return location;});
    stub(0x213a20,+[](void* location,Unit*)->void*{memset(location,0,16);return location;});
    stub(0x2dba00,+[]()->bool{++preflights;return permit;});stub(0x1434c50,+[](){});
    stub(0x2f70a0,+[](Game*,Unit* item){Destroy(item);});stub(0x22e750,+[](Unit*){});stub(0x209e40,+[]()->int{return 3;});
    stub(0x210200,+[](void*,Unit*,int,int* x,int* y,uint8_t)->int{*x=0;*y=0;return 1;});
    stub(0x1fe680,+[]()->int{return 0;});stub(0x2d5e10,AddHook);stub(0x316b80,NotifyHook);stub(0x28f510,+[](Game*){});
    stub(0xae3a10,+[]()->int{return 0;});stub(0xae4920,+[](void* time,void*,int)->void*{static int fields[8]{0,0,0,2026,10,30,0,0};memcpy(time,fields,sizeof(fields));return time;});
    global(0x1baa468,uintptr_t(+[](void* time){memset(time,0,8);}));
    originalAdd=Add;originalNotify=Notify;originalCreate=reinterpret_cast<decltype(originalCreate)>(image+0x306c50);originalRuneMatch=reinterpret_cast<RuneMatcher>(image+0x308840);
    SetExtendedConditions(false);SetMultipleUseInputs(false);SetMultipleUseOutputs(false);FlushInstructionCache(GetCurrentProcess(),image,0x2000000);
    for(int count:{2,3,7}){
        Reset(count);RuneMatch match;Input inputs[3]{};RuneMatchHook(&match,&game,&owner,inputs);
        assert(match.matched&&match.row==0&&!writes&&!consumed);
        const int kept=int(inputs[0].item->dwUnitId-100);
        CreateHook(&game,&owner,&recipe,inputs);
        assert(consumed==count-1&&alive[kept]&&(published&1)&&preflights==1);
        for(int stat=0;stat<16;++stat)assert(values[kept][stat].value==count*(count+1)/2*(stat+1));
        assert(values[kept][16].value==98765+kept);
        // Native identical-code inputs use one descriptor with qty=N.
        Reset(count,false);RuneMatchHook(&match,&game,&owner,inputs);assert(!match.matched&&!writes&&!consumed);
    }
    // Regression from the running mod: the authored two-bag recipe has lvl=125
    // and the two bags hold disjoint, sparse saved counters (500 gems/444 high).
    Reset(2);Put(recipe.header.data(),0x12,uint16_t(100));recipe.outputs[0].level=125;maxLevel=125;
    values={};values[0][0]={0u<<16,500};values[1][0]={3u<<16,444};
    Put(lists[0].data(),0x38,uint64_t(1));Put(lists[1].data(),0x38,uint64_t(1));
    RuneMatch live;Input liveInputs[3]{};RuneMatchHook(&live,&game,&owner,liveInputs);assert(live.matched&&!writes&&!consumed);
    auto liveBag=liveInputs[0].item;const auto liveId=liveBag->dwUnitId;
    CreateHook(&game,&owner,&recipe,liveInputs);
    assert(consumed==1&&Find(liveId)==liveBag&&(published&1));
    // Native useitem keeps the existing bag's level; a populated lvl column
    // must not prevent merging or force a newly generated replacement bag.
    assert(Stat(liveBag,0,0)==500&&Stat(liveBag,3,0)==444&&liveBag->pItemData->dwItemLevel==69);
    assert(recipe.outputs[0].level==125);
    assert(SavedStat(SavedList(liveBag),0)==500&&SavedStat(SavedList(liveBag),3)==444);
    maxLevel=99;
    for(int stat:{0,4,5,8,15})for(bool overflow:{false,true}){
        Boundary(stat,overflow);const auto before=values;RuneMatch boundary;Input chosen[3]{};
        RuneMatchHook(&boundary,&game,&owner,chosen);assert(boundary.matched&&!writes&&!consumed);
        auto kept=chosen[0].item;CreateHook(&game,&owner,&recipe,chosen);
        if(overflow)assert(!writes&&!preflights&&!consumed&&!published&&values==before&&alive[0]&&alive[1]);
        else assert(preflights==1&&consumed==1&&(published&1)&&Stat(kept,stat,0)==limits[stat].maximum);
    }
    Reset();permit=false;RuneMatch match;Input inputs[3]{};RuneMatchHook(&match,&game,&owner,inputs);assert(match.matched);
    const auto before=values;CreateHook(&game,&owner,&recipe,inputs);assert(!consumed&&preflights==1&&values==before);
    Reset();recipe.header[4]=18;Put(recipe.header.data(),8,int(0));Put(recipe.header.data(),12,int(999));
    RuneMatchHook(&match,&game,&owner,inputs);assert(!match.matched&&!writes&&!consumed);
    Reset();SetExtendedConditions(true);Conditions condition;condition.enabled=1;condition.inputs=3;condition.tests[1]={18,0,5};
    sourceTable.conditions[0]=condition;
    RuneMatchHook(&match,&game,&owner,inputs);assert(!match.matched&&!writes&&!consumed);
    for(int n=0;n<3;++n)values[n][0].value=5;
    RuneMatchHook(&match,&game,&owner,inputs);assert(match.matched&&!writes&&!consumed);
    CreateHook(&game,&owner,&recipe,inputs);assert(consumed==2&&values[inputs[0].item->dwUnitId-100][0].value==15);
    // Native client/server request: prepare must not alter saved items; commit
    // invokes the same server creator, then republishes the retained GUID.
    stub(0x308840,RuneMatchHook);stub(0x306c50,CreateHook);FlushInstructionCache(GetCurrentProcess(),image,0x2000000);
    QuestNative::Invocation nativeMatch;assert(nativeMatch.Build(&recipe,1));originalRuneMatch=reinterpret_cast<RuneMatcher>(nativeMatch.memory);
    Reset();recipe.outputs[0].level=125;maxLevel=125;cube.dwUnitType=4;cube.dwUnitId=900;Put(&cube,0x150,bucket);bucket=&cube;
    std::array<uint8_t,43> request{};Put(request.data(),1,uint32_t(900));request[7]=2;
    for(int n=0;n<3;++n){Put(request.data(),8+n*5,uint32_t(100+n));request[12+n*5]=uint8_t(n);}
    auto phase=[&](bool commit){return reinterpret_cast<int(__fastcall*)(Game*,Unit*,void*,bool)>(image+0x2b3a70)(&game,&owner,request.data(),commit);};
    assert(phase(false)==4&&!writes&&!consumed&&!published);
    // Creator trampoline bypasses the test detour, as MinHook does in-game.
    UseCreateInvocation native;assert(native.Build());originalCreate=reinterpret_cast<decltype(originalCreate)>(native.memory);
    assert(phase(true)==0&&consumed==2&&alive[2]&&(published&1)&&values[2][15].value==96);
    assert(data[2].dwItemLevel==69&&recipe.outputs[0].level==125);
    for(const auto& [rva,bytes]:code)memcpy(image+rva,bytes.data(),14);for(const auto& [rva,value]:globals)Put(image,rva,value);
    FlushInstructionCache(GetCurrentProcess(),image,0x2000000);
    originalCreate=oldCreate;originalAdd=oldAdd;originalNotify=oldNotify;originalRuneMatch=oldMatch;CompiledNames::database=names;sourceMod=mod;sourceTable=sources;sourcePath=path;
    QuestNative::ready=ready;SetExtendedConditions(oldConditions);SetMultipleUseInputs(oldInputs);SetMultipleUseOutputs(oldOutputs);::testQuestRules=rules;
    std::cout<<"Actual native matcher/creator merges 2/3/7 bags (Z01,qty=N), preserves retained GUID, consumes donors, restores balances on preflight refusal; lvl=125 with sparse 500-gem/444-high balances and server prepare/commit passed.\n";
}
}
