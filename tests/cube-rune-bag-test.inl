namespace RuneBagTest {
template<size_t N>using Bytes=std::array<uint8_t,N>;
template<class T>void Put(void* p,size_t offset,T value){memcpy(static_cast<uint8_t*>(p)+offset,&value,sizeof(value));}
std::array<Bytes<0x1b4>,40> definitions{};
std::array<Bytes<0xe4>,3> types{};
std::array<Bytes<0x144>,4> costs{};
std::array<Bytes<0x2e>,4> props{};
std::vector<Recipe> recipes;
std::array<Unit,300> units{};std::array<D2ItemDataStrc,300> data{};
D2InventoryStrc inventory{};Unit owner{};Game game{};
Bytes<0xb58> root{};Bytes<0x68> list{};
struct Entry{uint32_t key;int value;};std::array<Entry,5> balances{};
uint8_t* itemTable=definitions[0].data();uint8_t* typeTable=types[0].data();uint8_t* costTable=costs[0].data();uint8_t* propTable=props[0].data();Recipe* recipeTable=nullptr;
uint64_t itemRows=40,typeRows=3,costRows=4,propRows=4,recipeRows=0;
void* statLink=reinterpret_cast<void*>(11);void* propLink=reinterpret_cast<void*>(12);
int nativeMatches=0,nativeCreates=0,consumed=0,writes=0,failStat=-1;bool placement=true;
uint32_t publication=0;
int __fastcall Name(void* linker,const char* name){
    const std::array stats{"RB_Low","RB_Mid","RB_High","RB_Ultra"};
    const std::array props{"RB-Low","RB-Mid","RB-High","RB-Ultra"};
    for(int n=0;n<4;++n)if(!strcmp(name,linker==statLink ? stats[n] : props[n]))return n;
    return -1;
}
int __fastcall Stat(Unit* item,uint32_t stat,uint16_t layer){assert(!layer);if(stat==70)return 7;assert(item==&units[0] && stat<4);return balances[stat].value;}
Unit* __fastcall Lookup(Game* g,int type,uint32_t id){assert(g==&game && type==4);return units[0].dwUnitId==id ? &units[0] : nullptr;}
int __fastcall Set(void* saved,int stat,int value,uint16_t layer){
    assert(saved==list.data() && stat>=0 && stat<4 && !layer);++writes;
    if(stat==failStat){failStat=-1;return 0;}balances[stat].value=value;return 1;
}
bool __fastcall Add(Unit* player,const Location*,const uint32_t* ids,size_t count,bool special){assert(player==&owner && count==1 && *ids==units[0].dwUnitId && !special);return placement;}
void __fastcall Notify(Game*,Unit* player,Unit* item,uint32_t flags,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t){assert(player==&owner && item==&units[0]);publication=flags;}
RuneMatch* __fastcall Match(RuneMatch* result,Game*,Unit*,Input*){++nativeMatches;*result={123,true};return result;}
void __fastcall Create(Game* g,Unit* player,Recipe* recipe,Input* inputs){
    assert(g==&game && player==&owner && inputs[0].item==&units[0]);++nativeCreates;
    assert(recipe->outputs[0].type==254 && !recipe->outputs[0].flags && !recipe->outputs[1].type && !recipe->outputs[2].type);
    for(const auto& mod:recipe->outputs[0].mods)assert(mod.property==-1); // No double-credit from the anchor's mods.
    Location location{0,3,0,0,0};const uint32_t id=units[0].dwUnitId;
    if(!AddHook(player,&location,&id,1,false))return;
    for(auto item=inventory.pFirstItem;item;item=item->pItemData->pExtraData.pNextItem)if(item!=&units[0])++consumed;
    NotifyHook(g,player,inputs[0].item,0,2,0,3,0,0);
}
void Reset(size_t runeCount=5){
    definitions={};types={};costs={};props={};units={};data={};inventory={};owner={};game={};root={};list={};recipes.assign(37,Recipe{});
    for(size_t n=0;n<4;++n){balances[n]={uint32_t(n)<<16,int(10+n*10)};costs[n][8]=32;costs[n][0x15]=20;Put(costs[n].data(),0x18,uint32_t(48575));props[n][0x18]=17;Put(props[n].data(),0x20,uint16_t(n));}
    balances[4]={uint32_t(10)<<16,98765};
    Put(root.data(),0x1c,uint32_t(0x80000000));Put(root.data(),0x68,list.data());Put(list.data(),0x1c,uint32_t(0x40));Put(list.data(),0x30,balances.data());Put(list.data(),0x38,uint64_t(balances.size()));
    Put(definitions[0].data(),0x80,uint32_t(0x2031305a));Put(types[1].data(),0,uint32_t(0x656e7572));Put(types[2].data(),4,int16_t(1));
    for(size_t n=1;n<=37;++n){Put(definitions[n].data(),0x80,uint32_t(0x100000+n));Put(definitions[n].data(),0x126,int16_t(n==37 ? 2 : 1));
        auto& r=recipes[n-1];r.header[0]=1;r.header[3]=255;r.header[0x10]=2;Put(r.header.data(),0x12,uint16_t(100));
        Put(r.header.data(),0x14,uint16_t(1));Put(r.header.data(),0x16,uint16_t(0));r.header[0x1b]=0;
        Put(r.header.data(),0x1c,uint16_t(1));Put(r.header.data(),0x1e,uint16_t(n));r.header[0x23]=0;r.outputs[0].type=254;
        const int tier=n<=33 ? int(n-1)/11 : 3;const int points=n<=33 ? 1<<((n-1)%11) : 1<<(n-34);
        r.outputs[0].mods[0]={tier,0,int16_t(points),int16_t(points),0};
    }
    // A custom inherited rune can grant all four balances in a single recipe.
    for(int n=0;n<4;++n)recipes[36].outputs[0].mods[n]={n,0,int16_t(n+1),int16_t(n+1),0};
    Put(definitions[38].data(),0x80,uint32_t(0x2031307a)); // z01 is an unrelated Enhance Crystal.
    owner.dwUnitType=0;owner.dwUnitId=44;owner.dwAnimMode=1;owner.pInventory=&inventory;game.bExpansion=1;
    inventory.dwSignature=0x1020304;inventory.pOwner=&owner;inventory.pFirstItem=&units[0];
    for(size_t n=0;n<=runeCount;++n){auto& item=units[n];item.dwUnitType=4;item.dwUnitId=uint32_t(100+n);item.pItemData=&data[n];item.dwClassId=n ? 1 : 0;
        data[n].nPage=3;data[n].dwItemLevel=69;data[n].dwInitSeed=uint32_t(0x123400+n);data[n].dwQualityNo=7;data[n].pExtraData.pParentInv=&inventory;
        data[n].pExtraData.pNextItem=n<runeCount ? &units[n+1] : nullptr;}
    units[0].pStatListEx=reinterpret_cast<D2StatListExStrc*>(root.data());
    if(runeCount==5){units[1].dwClassId=1;units[2].dwClassId=3;units[3].dwClassId=12;units[4].dwClassId=33;units[5].dwClassId=34;}
    recipeTable=recipes.data();recipeRows=recipes.size();nativeMatches=nativeCreates=consumed=writes=0;failStat=-1;placement=true;publication=0;
    bindings={{0x1ca0390,uintptr_t(&itemTable)},{0x1ca0398,uintptr_t(&itemRows)},{0x1c9fbf0,uintptr_t(&typeTable)},{0x1c9fbf8,uintptr_t(&typeRows)},
        {0x1c9fb78,uintptr_t(&costTable)},{0x1c9fb80,uintptr_t(&costRows)},{0x1c9fb90,uintptr_t(&statLink)},
        {0x1c9ebc0,uintptr_t(&propTable)},{0x1c9ebc8,uintptr_t(&propRows)},{0x1c9ebd8,uintptr_t(&propLink)},
        {0x1ca0b18,uintptr_t(&recipeTable)},{0x1ca0b20,uintptr_t(&recipeRows)},{0x6d0470,uintptr_t(Name)},{0x1e5620,uintptr_t(Stat)},{0x1e5270,uintptr_t(Set)},{0x2c49c0,uintptr_t(Lookup)}};
    statLink=reinterpret_cast<void*>(11);propLink=reinterpret_cast<void*>(12);
    installed=true;current=nullptr;runeTransaction=nullptr;originalRuneMatch=Match;originalCreate=Create;originalAdd=Add;originalNotify=Notify;
}
void Run(){
    const auto savedBindings=bindings;const auto savedCreate=originalCreate;const auto savedAdd=originalAdd;const auto savedNotify=originalNotify;const auto savedMatch=originalRuneMatch;const bool wasInstalled=installed;
    Reset();const auto bag=units[0];const auto metadata=data[0];RuneMatch match;Input inputs[3]{};
    assert(RuneMatchHook(&match,&game,&owner,inputs)==&match && match.matched && !nativeMatches && !writes);
    const auto beforeRecipes=recipes;
    CreateHook(&game,&owner,recipeTable+match.row,inputs);
    assert(consumed==5 && nativeCreates==1 && publication==1 && balances[0].value==15 && balances[1].value==21 && balances[2].value==1054 && balances[3].value==41 && balances[4].value==98765);
    assert(!memcmp(&bag,&units[0],sizeof(bag)) && !memcmp(&metadata,&data[0],sizeof(metadata)) && !memcmp(beforeRecipes.data(),recipes.data(),recipes.size()*sizeof(Recipe)));
    // No TXT source is provided by GameAssets in this test. BIN startup may
    // leave either null linkers or allocated linkers containing no names.
    for(const bool emptyLinkers:{false,true})for(const size_t count:{size_t(1),size_t(5)}){
        Reset(count);
        if(!emptyLinkers)statLink=propLink=nullptr;
        bindings[0x6d0470]=uintptr_t(+[](void*,const char*)->int{assert(false && "Rune deposits must not query TXT name linkers");return -1;});
        const auto old=balances;
        RuneMatchHook(&match,&game,&owner,inputs);assert(match.matched && !writes);
        CreateHook(&game,&owner,recipeTable+match.row,inputs);
        assert(consumed==count && nativeCreates==1 && publication==1);
        assert(balances[0].value==old[0].value+(count==1 ? 1 : 5));
        assert(balances[1].value==old[1].value+(count==1 ? 0 : 1));
        assert(balances[2].value==old[2].value+(count==1 ? 0 : 1024));
        assert(balances[3].value==old[3].value+(count==1 ? 0 : 1));
        assert(balances[4].value==old[4].value);
    }
    // Compiled property IDs can move independently of their saved-stat IDs.
    Reset();std::swap(props[0],props[2]);
    for(auto& recipe:recipes)for(auto& mod:recipe.outputs[0].mods){if(mod.property==0)mod.property=2;else if(mod.property==2)mod.property=0;}
    assert((PlanRunes(&game,&owner).values==std::array<int,4>{5,1,1024,1}));
    // Reject ambiguous or unserializable compiled balances without consuming.
    Reset();Put(props[1].data(),0x20,uint16_t(0));assert(!PlanRunes(&game,&owner).valid);
    Reset();costs[1][0x15]=0;assert(!PlanRunes(&game,&owner).valid);
    Reset();props[1][0x19]=17;assert(!PlanRunes(&game,&owner).valid);
    Reset();recipes[0].header[4]=16;Put(recipes[0].header.data(),8,int(0));Put(recipes[0].header.data(),12,int(14));
    assert(!PlanRunes(&game,&owner).valid);Put(recipes[0].header.data(),12,int(15));assert(PlanRunes(&game,&owner).valid);
    // Every existing rune keeps its own value, rather than reusing the first
    // rune's recipe value for all inputs; custom inherited runes can add four stats.
    Reset(37);for(int n=1;n<=37;++n)units[n].dwClassId=n;
    auto plan=PlanRunes(&game,&owner);assert(plan.valid && plan.runes==37 && (plan.values==std::array<int,4>{2048,2049,2050,11}));
    Reset(1);units[1].dwClassId=37;plan=PlanRunes(&game,&owner);assert(plan.valid && (plan.values==std::array<int,4>{1,2,3,4}));
    Reset(3);assert(PlanRunes(&game,&owner).values[0]==3);
    Reset(1);definitions[1][0x13a]=1;assert(PlanRunes(&game,&owner).values[0]==7);
    Reset(255);assert(PlanRunes(&game,&owner).valid);Reset(256);assert(PlanRunes(&game,&owner).applicable && !PlanRunes(&game,&owner).valid);
    Reset(255);for(size_t n=1;n<=255;++n)units[n].dwClassId=11;
    RuneMatchHook(&match,&game,&owner,inputs);assert(match.matched);CreateHook(&game,&owner,recipeTable+match.row,inputs);
    assert(consumed==255 && balances[0].value==261130); // Exceeds int16 cube modifier range without truncation.
    // A matched preview changes nothing. Failure to save or place any part of
    // the output cannot debit runes or change even one pre-existing balance.
    for(int failure=0;failure<6;++failure){
        Reset();RuneMatchHook(&match,&game,&owner,inputs);const auto old=balances;
        if(failure==0)placement=false;
        if(failure==1)failStat=1;
        if(failure==2)balances[2].value=999999;
        if(failure==3)recipes[0].outputs[0].mods[0].max=2;
        if(failure==4)data[1].pExtraData.pParentInv=nullptr;
        if(failure==5)inputs[0].item=&units[1];
        const auto expected=balances;CreateHook(&game,&owner,recipeTable+match.row,inputs);
        assert(!consumed && !memcmp(expected.data(),balances.data(),sizeof(balances)) && !runeTransaction);
        if(failure!=0)assert(!nativeCreates);
    }
    // The helper has no effect on other combinations, two bags, an empty bag,
    // or the similarly named lower-case z01 item.
    for(int other=0;other<4;++other){Reset(other==0?0:1);if(other==1)units[1].dwClassId=0;if(other==2)units[1].dwClassId=39;if(other==3)units[0].dwClassId=38;
        RuneMatchHook(&match,&game,&owner,inputs);assert(nativeMatches==1 && match.matched && match.row==123 && !writes && !nativeCreates);}
    Reset(1);recipes.push_back(recipes[0]);recipes.back().outputs[0].mods[0].min=recipes.back().outputs[0].mods[0].max=5;recipeTable=recipes.data();recipeRows=recipes.size();
    RuneMatchHook(&match,&game,&owner,inputs);assert(!match.matched && !writes && !nativeCreates);
    bindings=savedBindings;originalCreate=savedCreate;originalAdd=savedAdd;originalNotify=savedNotify;originalRuneMatch=savedMatch;installed=wasInstalled;
    std::cout<<"Mixed rune bag deposits sum all 36 rune values and inherited/custom multi-stat runes, preserve the existing useitem bag and other contents, bound capacity/inputs, republish saved stats and roll back save/placement failures without consuming runes.\n";
}
void LoadedTables(const std::filesystem::path& directory){
    auto read=[&](const char* name,size_t stride){
        std::ifstream input(directory/name,std::ios::binary|std::ios::ate);assert(input);
        std::vector<uint8_t> bytes(size_t(input.tellg()));input.seekg(0);assert(input.read(reinterpret_cast<char*>(bytes.data()),bytes.size()));
        const auto count=RuneField<uint32_t>(bytes.data());assert(bytes.size()==4+size_t(count)*stride);bytes.erase(bytes.begin(),bytes.begin()+4);return bytes;
    };
    auto weapons=read("weapons.bin",0x1b4),armor=read("armor.bin",0x1b4),misc=read("misc.bin",0x1b4);
    weapons.insert(weapons.end(),armor.begin(),armor.end());weapons.insert(weapons.end(),misc.begin(),misc.end());
    auto nativeRecipes=read("cubemain.bin",sizeof(Recipe)),nativeCosts=read("itemstatcost.bin",0x144),nativeProps=read("properties.bin",0x2e),nativeTypes=read("itemtypes.bin",0xe4);
    const auto savedBindings=bindings;Reset(36);
    itemTable=weapons.data();itemRows=weapons.size()/0x1b4;typeTable=nativeTypes.data();typeRows=nativeTypes.size()/0xe4;
    costTable=nativeCosts.data();costRows=nativeCosts.size()/0x144;propTable=nativeProps.data();propRows=nativeProps.size()/0x2e;
    recipeTable=reinterpret_cast<Recipe*>(nativeRecipes.data());recipeRows=nativeRecipes.size()/sizeof(Recipe);
    statLink=propLink=nullptr;
    bindings[0x6d0470]=uintptr_t(+[](void*,const char*)->int{assert(false && "BIN fixtures have no TXT names");return -1;});
    bindings[0x1e5620]=uintptr_t(+[](Unit*,uint32_t,uint16_t)->int{return 0;});
    for(size_t n=0;n<itemRows;++n){const auto code=RuneField<uint32_t>(itemTable+n*0x1b4,0x80);if(code==0x2031305a)units[0].dwClassId=uint32_t(n);
        for(unsigned rune=1;rune<=36;++rune){const auto wanted=uint32_t('r')|uint32_t('0'+rune/10)<<8|uint32_t('0'+rune%10)<<16|uint32_t(' ')<<24;if(code==wanted)units[rune].dwClassId=uint32_t(n);}}
    const auto plan=PlanRunes(&game,&owner);
    assert(plan.applicable && plan.valid && plan.runes==36 && (plan.values==std::array<int,4>{2047,2047,2047,7}));
    assert((plan.stats==std::array<int,4>{292,293,294,476}) && (plan.properties==std::array<int,4>{264,265,263,266}));
    for(size_t rune=1;rune<=36;++rune){
        data[0].pExtraData.pNextItem=&units[rune];data[rune].pExtraData.pNextItem=nullptr;
        const auto single=PlanRunes(&game,&owner);std::array<int,4> expected{};
        const size_t tier=rune<=33 ? (rune-1)/11 : 3;expected[tier]=1<<((rune<=33 ? rune-1 : rune-34)%11);
        assert(single.valid && single.runes==1 && single.values==expected);
    }
    itemTable=definitions[0].data();itemRows=40;typeTable=types[0].data();typeRows=3;costTable=costs[0].data();costRows=4;propTable=props[0].data();propRows=4;
    bindings=savedBindings;
    std::cout<<"BIN-only tables with no TXT/name linkers resolve all 36 individual rune deposits and mixed totals: 2047 low/mid/high and 7 ultra.\n";
}
int nativePreflights=0;
void NativeOutputRefusal(uint8_t* image){
    const auto savedBindings=bindings;Reset();
    std::map<uint32_t,std::array<uint8_t,14>> oldCode;std::map<uint32_t,uint64_t> oldData;
    auto stub=[&](uint32_t rva,auto fn){std::array<uint8_t,14> old{};memcpy(old.data(),image+rva,old.size());oldCode.emplace(rva,old);
        const uint8_t jump[]{0xff,0x25,0,0,0,0};memcpy(image+rva,jump,6);const auto target=reinterpret_cast<uintptr_t>(fn);memcpy(image+rva+6,&target,8);};
    for(const auto& [rva,address]:bindings)if(rva>=0x1c00000){oldData.emplace(rva,RuneField<uint64_t>(image,rva));Put(image,rva,*reinterpret_cast<const uint64_t*>(address));}
    stub(0x6d0470,Name);stub(0x1e5620,Stat);stub(0x1e5270,Set);stub(0x2c49c0,Lookup);
    stub(0x208330,+[](Unit*)->void*{return &inventory;});
    stub(0x213a00,+[](void* location)->void*{memset(location,0,16);return location;});
    stub(0x213a20,+[](void* location,Unit*)->void*{memset(location,0,16);return location;});
    stub(0x2dba00,+[]()->bool{++nativePreflights;return false;});stub(0x1434c50,+[](){});
    FlushInstructionCache(GetCurrentProcess(),image,0x2000000);
    originalCreate=reinterpret_cast<decltype(originalCreate)>(image+0x306c50);nativePreflights=0;
    RuneMatch match;Input inputs[3]{};RuneMatchHook(&match,&game,&owner,inputs);assert(match.matched);
    const auto before=balances;CreateHook(&game,&owner,recipeTable+match.row,inputs);
    assert(nativePreflights==1 && !consumed && !runeTransaction && !memcmp(before.data(),balances.data(),sizeof(balances)));
    // Now execute native useitem selection, input destruction and output
    // publication as well. Only engine inventory/network boundaries are stubs.
    stub(0x2dba00,+[]()->bool{++nativePreflights;return true;});
    stub(0x2c4950,+[](int type,Game* g,uint32_t id)->Unit**{
        assert(type==4 && g==&game);static Unit* found=nullptr;found=nullptr;
        for(size_t n=0;n<=5;++n)if(units[n].dwUnitId==id)found=&units[n];return &found;
    });
    stub(0x2f70a0,+[](Game* g,Unit* item){
        assert(g==&game && item && item!=&units[0]);++consumed;
    });
    stub(0x22e750,+[](Unit*){});
    stub(0x209e40,+[]()->int{return 3;});
    stub(0x210200,+[](void*,Unit* item,int,int* x,int* y,uint8_t)->int{assert(item==&units[0]);*x=0;*y=0;return 1;});
    stub(0x2d5e10,AddHook);stub(0x316b80,NotifyHook);stub(0x1fe680,+[]()->int{return 0;});
    int maximumLevel=99;oldData.emplace(0x1c9fd68,RuneField<uint64_t>(image,0x1c9fd68));Put(image,0x1c9fd68,&maximumLevel);
    originalAdd=+[](Unit* player,const Location*,const uint32_t* ids,size_t count,bool)->bool{
        assert(player==&owner);if(count){assert(count==1 && *ids==units[0].dwUnitId);}return true;
    };
    originalNotify=+[](Game*,Unit* player,Unit* item,uint32_t flags,uint32_t command,uint32_t,uint32_t,uint32_t,uint32_t){
        assert(player==&owner);if(item==&units[0] && command==2)publication=flags;
    };
    FlushInstructionCache(GetCurrentProcess(),image,0x2000000);
    CreateHook(&game,&owner,recipeTable+match.row,inputs);
    assert(consumed==5 && publication==1 && units[0].dwUnitId==100 && units[0].dwClassId==0);
    assert(balances[0].value==15 && balances[1].value==21 && balances[2].value==1054 && balances[3].value==41 && balances[4].value==98765);
    for(const auto& [rva,bytes]:oldCode)memcpy(image+rva,bytes.data(),bytes.size());for(const auto& [rva,value]:oldData)Put(image,rva,value);
    FlushInstructionCache(GetCurrentProcess(),image,0x2000000);bindings=savedBindings;installed=false;
    std::cout<<"Actual native cube-output code keeps the original useitem bag/GUID, destroys exactly five mixed rune inputs, publishes all new balances and rolls back a refused preflight.\n";
}
}
