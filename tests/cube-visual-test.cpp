#include "../d2rhud/plugin/CubeVisual.cpp"
#include <cassert>
#include "../d2rhud/json.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <vector>
#include "quest-journal-stubs.inl"

GameAssets::GameAssets(const std::filesystem::path&,const std::string&) {}
GameAssets::~GameAssets() {}
std::vector<uint8_t> GameAssets::Read(const std::string&) {return {};}
std::string GameAssets::ReadTableSource(const std::string&,bool) {return {};}

using namespace CubeVisual;
using namespace CubeVisual::Data;
std::map<uint32_t,uintptr_t> bindings;
uintptr_t testImage=0;
LONG WINAPI NativeFault(EXCEPTION_POINTERS* error){
    if(testImage && error->ExceptionRecord->ExceptionCode==EXCEPTION_ACCESS_VIOLATION){
        const auto& c=*error->ContextRecord;
        std::cerr<<"Native fixture access violation at RVA "<<std::hex<<(c.Rip-testImage)
                 <<" rcx="<<c.Rcx<<" rdx="<<c.Rdx<<" r8="<<c.R8<<" r9="<<c.R9<<std::dec<<'\n';
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
DWORD64 Pattern::Address(uint32_t rva){return testImage ? testImage+rva : bindings[rva];}
std::array<std::array<uint8_t,0x1b4>,4> items{};
std::array<uint8_t,0x150> unique{};
std::array<uint8_t,0x1bc> set{};
std::array<uint8_t,0x144> statRecord{};
uintptr_t itemTable=reinterpret_cast<uintptr_t>(items.data()),uniqueTable=reinterpret_cast<uintptr_t>(unique.data()),setTable=reinterpret_cast<uintptr_t>(set.data()),statTable=reinterpret_cast<uintptr_t>(statRecord.data());
uint64_t itemCount=items.size(),specialCount=1,statCount=1;
uintptr_t linker=1;
uintptr_t recipeTable=0;
uint64_t recipeCount=0;
D2UnitStrc item{},player{};
D2ItemDataStrc itemData{};
std::array<uint8_t,0xb58> extended{};
std::array<uint8_t,0x68> savedList{};
struct SavedEntry {uint32_t key=0;int32_t value=0;} savedEntry;
int& stored=savedEntry.value;
int addCalls=0,createCalls=0,linkerCalls=0;
bool addSuccess=true;
uint32_t notifiedFlags=0;int notifications=0;
int allocations=0,attachments=0;
uint32_t observedBase=0,observedQuality=0;int observedSpecial=0;
int __fastcall Row(void*,const char* name){return strcmp(name,"transmog")==0 ? 0 : -1;}
int __fastcall Get(D2UnitStrc* unit,uint32_t id,uint16_t layer){assert(unit==&item && !id && !layer);return stored;}
int __fastcall SetSaved(void* list,int id,int value,uint16_t layer){assert(list==savedList.data() && !id && !layer);stored=value;return 1;}
void* __fastcall AllocateSaved(uint32_t flags,int expiration,uint32_t type,uint32_t id){
    assert(flags==0x40 && !expiration && type==4 && id==item.dwUnitId);++allocations;return savedList.data();
}
void __fastcall AttachSaved(Unit* unit,void* list,int merge){
    assert(unit==&item && list==savedList.data() && merge==1);++attachments;
    auto address=reinterpret_cast<uintptr_t>(list);memcpy(extended.data()+0x68,&address,8);
}
const char* __fastcall Asset(void*,D2UnitStrc* unit){
    assert(unit!=&item && unit->pItemData!=&itemData);
    observedBase=unit->dwClassId;observedQuality=unit->pItemData->dwQualityNo;observedSpecial=unit->pItemData->dwFileIndex;
    assert(unit->pStatListEx==item.pStatListEx && unit->pItemData->dwInitSeed==itemData.dwInitSeed);
    return "items/target_model";
}
int __fastcall ParseTarget(Output* output,char* name,int,int){
    if(!strcmp(name,"cap") || !strcmp(name,"Z004")){output->type=252;output->base=1;return 1;}
    if(!strcmp(name,"The Unique") || !strcmp(name,"Griswold's Edge")){output->type=252;output->quality=7;output->flags=8;output->special=1;return 1;}
    if(!strcmp(name,"The Set")){output->type=252;output->quality=5;output->flags=8;output->special=1;return 1;}
    return 0;
}
int __fastcall NativeCodeLookup(void*,uint32_t code){
    for(size_t i=0;i<items.size();++i){uint32_t value=0;memcpy(&value,items[i].data()+0x80,4);if(code==value)return int(i);}
    return -1;
}
int __fastcall NativeNameLookup(void* dictionary,const char* name){
    if(dictionary==reinterpret_cast<void*>(1) && !strcmp(name,"The Unique"))return 0;
    if(dictionary==reinterpret_cast<void*>(2) && !strcmp(name,"The Set"))return 0;
    return -1;
}
const char* __fastcall NativeAsset(void*,uint32_t code,int quality,int special,bool identified){
    uint32_t expected=0;memcpy(&expected,items[observedBase].data()+0x80,4);
    assert(code==expected && quality==observedQuality && special==observedSpecial && identified);
    return "items/native_target_model";
}
void __fastcall Link(char* text,Recipe* recipe,int index,int,int,int){++linkerCalls;assert(!strcmp(text,"useitem") || !strcmp(text,"Red Portal"));recipe->outputs[index].type=!strcmp(text,"Red Portal") ? 4 : 254;}
bool __fastcall MockAdd(D2UnitStrc*,const Location*,const uint32_t*,size_t,bool){++addCalls;assert(stored<0);return addSuccess;}
void __fastcall MockNotify(Game*,Unit*,Unit*,uint32_t flags,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t){notifiedFlags=flags;++notifications;}
void __fastcall MockCreate(Game*,D2UnitStrc* owner,Recipe* recipe,Input* inputs){
    ++createCalls;assert(!recipe->outputs[0].flags && recipe->outputs[0].type==254);
    assert(!recipe->outputs[0].level && !recipe->outputs[0].playerLevel && !recipe->outputs[0].itemLevel);
    Location location{0,3,0,0,0};const uint32_t id=inputs[0].item->dwUnitId;
    if(AddHook(owner,&location,&id,1,false))NotifyHook(nullptr,owner,inputs[0].item,0,2,0,3,0,0);
}
void Reset(){
    installed=true;stored=0;addCalls=createCalls=linkerCalls=0;addSuccess=true;current=nullptr;
    notifications=0;notifiedFlags=0;
    allocations=attachments=0;
    sourceTable={};sourcePath.clear();installRoot.clear();sourceMod.clear();recipeTable=0;recipeCount=0;
    item={};player={};itemData={};item.dwUnitType=4;item.dwUnitId=32;item.dwClassId=0;item.pItemData=&itemData;
    extended={};savedList={};savedEntry={};item.pStatListEx=reinterpret_cast<D2StatListExStrc*>(extended.data());
    uint32_t extendedFlags=0x80000000,magicFlags=0x40;uint64_t one=1;
    auto savedPointer=reinterpret_cast<uintptr_t>(savedList.data());auto entryPointer=reinterpret_cast<uintptr_t>(&savedEntry);
    memcpy(extended.data()+0x1c,&extendedFlags,4);memcpy(extended.data()+0x68,&savedPointer,8);
    memcpy(savedList.data()+0x1c,&magicFlags,4);memcpy(savedList.data()+0x30,&entryPointer,8);memcpy(savedList.data()+0x38,&one,8);
    itemData.dwQualityNo=6;itemData.dwFileIndex=42;itemData.dwInitSeed=12345;
    statRecord={};statRecord[4]=2;statRecord[8]=16;statRecord[0x15]=15;uint32_t bias=16384;memcpy(statRecord.data()+0x18,&bias,4);
    uint32_t codes[]{0x20786168,0x20706163,0x2030305a,0x3430305a};
    for(size_t i=0;i<items.size();++i)memcpy(items[i].data()+0x80,&codes[i],4);
    memcpy(unique.data()+0x28,&codes[2],4);memcpy(set.data()+0x28,&codes[3],4);
    originalLinker=Link;originalCreate=MockCreate;originalAdd=MockAdd;originalNotify=MockNotify;
}
void Bind(){
    bindings={{0x1ca0390,reinterpret_cast<uintptr_t>(&itemTable)},{0x1ca0398,reinterpret_cast<uintptr_t>(&itemCount)},
        {0x1c9fc70,reinterpret_cast<uintptr_t>(&uniqueTable)},{0x1c9fc78,reinterpret_cast<uintptr_t>(&specialCount)},
        {0x1c9fc50,reinterpret_cast<uintptr_t>(&setTable)},{0x1c9fc58,reinterpret_cast<uintptr_t>(&specialCount)},
        {0x1c9fb78,reinterpret_cast<uintptr_t>(&statTable)},{0x1c9fb80,reinterpret_cast<uintptr_t>(&statCount)},
        {0x1c9fb90,reinterpret_cast<uintptr_t>(&linker)},{0x6d0470,reinterpret_cast<uintptr_t>(Row)},
        {0x1ca0b18,reinterpret_cast<uintptr_t>(&recipeTable)},{0x1ca0b20,reinterpret_cast<uintptr_t>(&recipeCount)},
        {0x1e5620,reinterpret_cast<uintptr_t>(Get)},{0x1e5270,reinterpret_cast<uintptr_t>(SetSaved)},
        {0x1e4700,reinterpret_cast<uintptr_t>(AllocateSaved)},{0x1e4ea0,reinterpret_cast<uintptr_t>(AttachSaved)},
        {0x2616b0,reinterpret_cast<uintptr_t>(ParseTarget)},{0x42d430,reinterpret_cast<uintptr_t>(Asset)}};
}
void CompiledVisualNames(){
    Bind();Reset();const auto previous=CompiledNames::database.load();
    auto names=std::make_shared<CompiledNames::Database>();(*names)[0x1c9fb90]["hud_visual"]=0;CompiledNames::database=names;
    uintptr_t empty=0;bindings[0x1c9fb90]=reinterpret_cast<uintptr_t>(&empty);
    bindings[0x2616b0]=reinterpret_cast<uintptr_t>(+[](Output*,char*,int,int)->int{return 0;});
    memcpy(unique.data()+2,"The Unique",11);memcpy(set.data()+2,"The Set",8);
    assert(Resolve("cap",0,0).index==1 && Resolve("Z004",0,0).index==3);
    assert(Resolve("the unique",0,0).kind==1 && Resolve("The Set",0,0).kind==2);
    assert(!Resolve("Unknown target",0,0).Valid());
    Recipe recipe{};std::string output="useitem,vis=cap";LinkerHook(output.data(),&recipe,0,0,1,0);
    Input inputs[3]{{&item,0,75},{},{}};CreateHook(nullptr,&player,&recipe,inputs);
    assert(createCalls==1 && stored==Target(0,1).Encode());
    (*names)[0x1c9fb90]["hud_visual"]=511;assert(StatId()<0);
    CompiledNames::database=previous;Bind();Reset();
    std::cout<<"BIN visual recipes resolve compiled item codes/set/unique names and validated saved-stat metadata without TXT linkers.\n";
}
int mapPrepares=0,mapCreates=0;bool mapAllowed=true,mapReuse=false;
int mapResolved=0;
bool PrepareMap(Game*,Unit*,Recipe& recipe){
    ++mapPrepares;assert(CubeMap::Data::IsMap(recipe.outputs[0]));
    if(mapAllowed && mapResolved){assert(recipe.outputs[0].base==CubeMap::Data::RandomDestination);recipe.outputs[0].base=uint16_t(mapResolved);}
    return mapAllowed;
}
void __fastcall CreateMap(Game* game,Unit* player,Recipe* recipe,Input*){
    ++mapCreates;assert(mapPrepares && preparedMap);
    if(mapReuse)assert(recipe->outputs[0].type==254 && !recipe->outputs[0].special && !recipe->outputs[0].base && !(recipe->outputs[0].flags&CubeMap::Data::PreserveInput));
    else {assert(PortalActionHook(game,player,&recipe->outputs[0])==1);if(mapResolved)assert(recipe->outputs[0].base==mapResolved);}
}
void MapRecipes(){
    Reset();originalCreate=CreateMap;SetMapPrepare(PrepareMap);
    Recipe recipe;recipe.header[0]=1;recipe.header[0x10]=1;char text[]="map=512";
    LinkerHook(text,&recipe,0,0,1,0);assert(CubeMap::Data::IsMap(recipe.outputs[0]) && recipe.outputs[0].base==512);
    Input inputs[3]{};mapPrepares=mapCreates=0;mapAllowed=false;
    // The shipped event BIN lost its unresolved m01 input and consequently
    // matches ANY single item. Refuse it before portal preparation/consumption.
    mapAllowed=true;CreateHook(nullptr,&player,&recipe,inputs);assert(!mapPrepares && !mapCreates);
    recipe.header[0x14]=1;mapAllowed=false;
    CreateHook(nullptr,&player,&recipe,inputs);assert(mapPrepares==1 && !mapCreates && !preparedMap);
    mapAllowed=true;CreateHook(nullptr,&player,&recipe,inputs);assert(mapCreates==1 && !preparedMap);
    assert(PortalActionHook(nullptr,&player,&recipe.outputs[0])==0); // No unprepared callback can succeed.
    recipe.outputs[1].type=252;recipe.outputs[1].base=2;CreateHook(nullptr,&player,&recipe,inputs);assert(mapPrepares==3 && mapCreates==2);
    recipe.outputs[1]={};recipe.outputs[0].mods[0].property=2;CreateHook(nullptr,&player,&recipe,inputs);assert(mapCreates==2);
    recipe.outputs[0].mods[0].property=-1;
    recipeTable=reinterpret_cast<uintptr_t>(&recipe);recipeCount=1;
    sourceTable=ParseSource("enabled\tnuminputs\toutput\n1\t1\tmap=256\n");
    CreateHook(nullptr,&player,&recipe,inputs);assert(mapCreates==3 && recipe.outputs[0].base==512);
    sourceTable.rows=2;CreateHook(nullptr,&player,&recipe,inputs);assert(mapCreates==3);
    sourceTable=ParseSource("enabled\tnuminputs\toutput\toutput b\toutput c\n1\t1\tuseitem,map=78\tEv12\tEv12\n");
    recipe.outputs[1].type=252;recipe.outputs[1].base=2;recipe.outputs[2]=recipe.outputs[1];mapReuse=true;
    CreateHook(nullptr,&player,&recipe,inputs);assert(mapCreates==4 && recipe.outputs[0].base==512);
    mapAllowed=false;CreateHook(nullptr,&player,&recipe,inputs);assert(mapCreates==4);mapAllowed=true;mapReuse=false;
    Reset();originalCreate=CreateMap;recipe={};recipe.header[0]=1;recipe.header[0x10]=1;recipe.header[0x14]=1;
    char randomText[]="map=zzz";LinkerHook(randomText,&recipe,0,0,1,0);
    assert(CubeMap::Data::IsMap(recipe.outputs[0]) && !recipe.outputs[0].base);
    for(int destination:{78,512}){mapResolved=destination;const auto before=mapCreates;CreateHook(nullptr,&player,&recipe,inputs);assert(mapCreates==before+1 && !recipe.outputs[0].base);}
    // Compiled table recovery must restore the random marker, rather than
    // accidentally reusing the destination chosen by an earlier transmute.
    recipeTable=reinterpret_cast<uintptr_t>(&recipe);recipeCount=1;
    sourceTable=ParseSource("enabled\tnuminputs\toutput\n1\t1\tmap=zzz\n");recipe.outputs[0].base=78;mapResolved=700;
    const auto before=mapCreates;CreateHook(nullptr,&player,&recipe,inputs);assert(mapCreates==before+1 && recipe.outputs[0].base==78);
    mapAllowed=false;CreateHook(nullptr,&player,&recipe,inputs);assert(mapCreates==before+1);mapAllowed=true;
    sourceTable={};recipe.outputs[1]=CubeMap::Data::Encode(78);const auto prepares=mapPrepares;
    CreateHook(nullptr,&player,&recipe,inputs);assert(mapPrepares==prepares && mapCreates==before+1);
    mapResolved=0;
    SetMapPrepare(nullptr);Reset();
    std::cout<<"Cube map preparation precedes consumption; useitem and additional ordinary outputs are preserved.\n";
}
void Adapters(){
    for(auto name:{"cap","Z004","The Unique","The Set"}){
        Reset();Recipe recipe{};recipe.header[0]=1;std::string text="useitem,visual="+std::string(name);
        LinkerHook(text.data(),&recipe,0,0,12,0);
        assert(recipe.header[0] && recipe.outputs[0].PureVisual());
        // The user's useitem,vis=Griswold's Edge recipe retained lvl=125.
        recipe.outputs[0].level=125;recipe.outputs[0].playerLevel=50;recipe.outputs[0].itemLevel=99;
        assert(recipe.outputs[0].PureVisual());
        Input inputs[3]{{&item,0,75},{},{}};
        const auto before=itemData;const auto unitBefore=item;
        CreateHook(nullptr,&player,&recipe,inputs);
        assert(createCalls==1 && addCalls==1 && stored==-int(recipe.outputs[0].base) && !current);
        assert(notifications==1 && (notifiedFlags&1));
        assert(!memcmp(&before,&itemData,sizeof(before)) && !memcmp(&unitBefore,&item,sizeof(item)));
        assert(!strcmp(WorldPath(reinterpret_cast<void*>(1),&item),"items/target_model"));
        const int kind=Target::Decode(stored).kind;
        assert(observedBase==uint32_t(kind==0 ? 1 : kind==1 ? 2 : 3));
        assert(observedQuality==uint32_t(kind==0 ? 2 : kind==1 ? 7 : 5));
        assert(observedSpecial==(kind==0 ? -1 : 0));
        assert(!memcmp(&before,&itemData,sizeof(before)) && !memcmp(&unitBefore,&item,sizeof(item)));
        // Round-trip the configured item save field and signed packet width.
        const int persisted=int((stored+16384)&0x7fff)-16384;
        assert(persisted==stored && int16_t(stored)==stored);
        addSuccess=false;stored=123;CreateHook(nullptr,&player,&recipe,inputs);assert(stored==123);
    }
    for(auto text:{"useitem,vis=missing","useitem,vis=cap,qty=3","useitem,vis=","useitem,vis=cap,visual=cap"}){
        Reset();Recipe recipe{};recipe.header[0]=1;std::string source=text;
        LinkerHook(source.data(),&recipe,0,0,1,0);assert(!recipe.header[0] && !linkerCalls);
    }
    Reset();Recipe recipe{};std::string source="vis=cap";LinkerHook(source.data(),&recipe,0,0,1,0);
    Input inputs[3]{{&item,0,75},{},{}};
    recipe.outputs[0].mods[0].property=3;CreateHook(nullptr,&player,&recipe,inputs);assert(!createCalls && !stored);
    recipe.outputs[0].mods[0].property=-1;statRecord[8]=1;CreateHook(nullptr,&player,&recipe,inputs);assert(!createCalls);
    Reset();stored=1;assert(WorldPath(reinterpret_cast<void*>(1),&item)==nullptr); // Existing positive transmog mappings.
    stored=-12289;assert(WorldPath(reinterpret_cast<void*>(1),&item)==nullptr);
    Reset();Recipe preloaded{};preloaded.header[0]=1;preloaded.header[0x10]=1;
    preloaded.outputs[0].type=254;preloaded.outputs[0].level=125;
    sourceTable=ParseSource("description\tenabled\tnuminputs\toutput\tlvl\r\nTest 2\t1\t1\tuseitem,vis=Griswold's Edge\t125\r\n");
    assert(sourceTable.rows==1 && sourceTable.visuals.at(0).outputs[0].target=="Griswold's Edge");
    recipeTable=reinterpret_cast<uintptr_t>(&preloaded);recipeCount=1;
    const auto recoveredBefore=diagnostics.sourceMatches.load();
    CreateHook(nullptr,&player,&preloaded,inputs);
    assert(createCalls==1 && addCalls==1 && stored==Target(1,0).Encode() && diagnostics.sourceMatches==recoveredBefore+1);
    assert(notifications==1 && notifiedFlags==1);
    assert(!preloaded.outputs[0].flags && !preloaded.outputs[0].base && preloaded.outputs[0].level==125);
    ++sourceTable.rows;createCalls=0;stored=0;
    CreateHook(nullptr,&player,&preloaded,inputs);assert(!createCalls && !stored);
    NotifyHook(nullptr,&player,&item,0,2,0,3,0,0);assert(notifiedFlags==0); // Ordinary moves keep the original protocol.
    --sourceTable.rows;sourceTable.visuals.at(0).inputs=2;
    CreateHook(nullptr,&player,&preloaded,inputs);assert(!createCalls && !stored);
    Reset();Recipe plain{};std::string visual="vis=cap";LinkerHook(visual.data(),&plain,0,0,1,0);
    uintptr_t noList=0;memcpy(extended.data()+0x68,&noList,8);
    CreateHook(nullptr,&player,&plain,inputs);
    assert(allocations==1 && attachments==1 && createCalls==1 && stored==-2 && SavedList(&item)==savedList.data());
    // A nonzero-state modifier is not the saved base item modifier list.
    uint32_t otherState=98;memcpy(savedList.data()+0x20,&otherState,4);assert(!SavedList(&item));
    item.pStatListEx=nullptr;createCalls=0;CreateHook(nullptr,&player,&plain,inputs);assert(!createCalls);
    std::cout<<"Cube adapters preserve all item bytes, localize native targets, apply metadata before publication and roll back failed placement.\n";
}
struct BitWrite {uint32_t value;size_t bits;};
std::vector<BitWrite> bitWrites;
void __fastcall CaptureBits(void*,uint32_t value,size_t bits){bitWrites.push_back({value,bits});}
uintptr_t __fastcall Zero(){return 0;}
void NativeSavedModifiers(uint8_t* image){
    auto stub=[&](uint32_t rva,auto function){const uint8_t jump[]{0xff,0x25,0,0,0,0};memcpy(image+rva,jump,6);auto target=reinterpret_cast<uintptr_t>(function);memcpy(image+rva+6,&target,8);};
    auto pointer=[&](uint32_t rva,uintptr_t value){memcpy(image+rva,&value,8);};
    Reset();
    std::vector<std::array<uint8_t,0x144>> stats(487);
    stats[435]=statRecord;
    pointer(0x1c9fb78,reinterpret_cast<uintptr_t>(stats.data()));pointer(0x1c9fb80,stats.size());
    pointer(0x1ca0390,itemTable);pointer(0x1ca0398,itemCount);
    std::array<SavedEntry,16> baseValues{},fullValues{},modValues{};
    auto vector=[&](uint8_t* list,size_t offset,auto& entries,size_t count){
        auto address=reinterpret_cast<uintptr_t>(entries.data());uint64_t length=count,capacity=0x8000000000000010ULL;
        memcpy(list+offset,&address,8);memcpy(list+offset+8,&length,8);memcpy(list+offset+16,&capacity,8);
    };
    vector(extended.data(),0x30,baseValues,0);vector(extended.data(),0x80,fullValues,0);vector(savedList.data(),0x30,modValues,0);
    auto parent=reinterpret_cast<uintptr_t>(extended.data()),owner=reinterpret_cast<uintptr_t>(&item);
    memcpy(savedList.data()+0x58,&parent,8);memcpy(extended.data()+0x78,&owner,8);
    stub(0x14383a0,&memmove);stub(0x1438100,&memset);stub(0x1434c50,Zero);
    // Capture only bit output and unrelated item-type/name services. Native
    // stat insertion, propagation and the complete item serializer still run.
    stub(0x6c98d0,CaptureBits);
    for(auto rva:{0x1fe680,0x1faad0,0x1f9e70,0x1f9d70,0x1fe7a0,0x1fe3c0,0x2052c0})stub(rva,Zero);
    FlushInstructionCache(GetCurrentProcess(),image,0x2000000);
    // Exercise native attachment for an ordinary item with no modifier list.
    uintptr_t none=0;memcpy(extended.data()+0x68,&none,8);memcpy(savedList.data()+0x58,&none,8);
    reinterpret_cast<void(__fastcall*)(Unit*,void*,int)>(image+0x1e4ea0)(&item,savedList.data(),1);
    assert(SavedList(&item)==savedList.data());
    auto serialize=reinterpret_cast<void(__fastcall*)(Unit*,void*,int,int)>(image+0x205990);
    D2StaticPathStrc path{};item.pStaticPath=&path;
    std::array<uint8_t,0x28> buffer{};itemData.dwQualityNo=2;itemData.dwItemFlags=0x10;item.dwItemMode=0;
    auto contains=[&](uint32_t value,size_t bits){return std::any_of(bitWrites.begin(),bitWrites.end(),[&](const auto& w){return w.value==value && w.bits==bits;});};
    // Reproduce the defect: a valid negative selector in base/full stats is
    // visible through GetUnitStat, but is absent from the serialized item.
    reinterpret_cast<void(__fastcall*)(Unit*,int,int,uint16_t)>(image+0x1e5440)(&item,435,-4133,0);
    assert(baseValues[0].key==(435u<<16) && baseValues[0].value==-4133);
    bitWrites.clear();serialize(&item,buffer.data(),0,0);assert(!contains(435,9));
    reinterpret_cast<void(__fastcall*)(Unit*,int,int,uint16_t)>(image+0x1e5440)(&item,435,0,0);
    assert(SavedList(&item)==savedList.data());
    SetSavedStat(SavedList(&item),435,-4133);
    assert(SavedStat(savedList.data(),435)==-4133 && modValues[0].key==(435u<<16));
    assert(fullValues[0].value==-4133);
    bitWrites.clear();serialize(&item,buffer.data(),0,0);
    assert(contains(435,9) && contains(uint32_t(-4133+16384),15));
    SetSavedStat(savedList.data(),435,-1);bitWrites.clear();serialize(&item,buffer.data(),0,0);
    assert(contains(435,9) && contains(16383,15));
    // Bag balances use the same saved MAGIC list. Verify every tier through
    // the real native stat insertion, propagation and item serializer too.
    const std::array runeIds{292,293,294,476};const std::array runePoints{4,75,222,7};
    for(size_t n=0;n<4;++n){auto& record=stats[runeIds[n]];record[8]=32;record[0x15]=n==3 ? 32 : 20;
        const uint32_t bias=n==3 ? 49152 : 48575;memcpy(record.data()+0x18,&bias,4);SetSavedStat(SavedList(&item),runeIds[n],runePoints[n]);}
    bitWrites.clear();serialize(&item,buffer.data(),0,0);
    for(size_t n=0;n<4;++n){assert(SavedStat(SavedList(&item),runeIds[n])==runePoints[n]);assert(contains(runeIds[n],9));assert(contains(uint32_t(runePoints[n]+(n==3?49152:48575)),n==3?32:20));}
    std::cout<<"All four rune bag balances survive actual native saved-stat insertion and item serialization.\n";
    std::cout<<"Real native serialization omits the old base-only selector and includes changed selectors from saved item modifiers.\n";
}
#include "cube-rune-bag-test.inl"
std::filesystem::path eventTableFixture;
#include "quest-cube-native-test.inl"
#include "quest-property-native-test.inl"
#include "cube-useitem-test.inl"
void Executable(const std::filesystem::path& path){
    std::ifstream input(path,std::ios::binary|std::ios::ate);assert(input);
    std::vector<uint8_t> file(size_t(input.tellg()));input.seekg(0);input.read(reinterpret_cast<char*>(file.data()),file.size());
    auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(file.data());auto nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(file.data()+dos->e_lfanew);
    auto image=static_cast<uint8_t*>(VirtualAlloc(nullptr,nt->OptionalHeader.SizeOfImage,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));assert(image);
    auto sections=IMAGE_FIRST_SECTION(nt);
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i)memcpy(image+sections[i].VirtualAddress,file.data()+sections[i].PointerToRawData,sections[i].SizeOfRawData);
    testImage=reinterpret_cast<uintptr_t>(image);assert(Supported());
    for(const auto& sig:Signatures){image[sig.rva]^=1;assert(!Supported());image[sig.rva]^=1;}
    DWORD previous=0;assert(VirtualProtect(image,nt->OptionalHeader.SizeOfImage,PAGE_EXECUTE_READWRITE,&previous));
    installed=false;assert(Install());Uninstall();assert(Supported());
    QuestCubeNativeTest::Native(image);
    QuestCubeOutputTest::Native(image);
    CubeUseTest::Native(image);
    // Real output parser/linker, with only their table lookup/CRT dependencies replaced.
    auto stub=[&](uint32_t rva,auto function){const uint8_t jump[]{0xff,0x25,0,0,0,0};memcpy(image+rva,jump,6);auto target=reinterpret_cast<uintptr_t>(function);memcpy(image+rva+6,&target,8);};
    stub(0x1448c80,&strcspn);
    FlushInstructionCache(GetCurrentProcess(),image,nt->OptionalHeader.SizeOfImage);
    Output out{};char useitem[]="useitem";
    assert(reinterpret_cast<Parser>(image+0x2616b0)(&out,useitem,1,0) && out.type==254 && !out.flags);
    Recipe recipe{};char output[]="useitem";
    reinterpret_cast<Linker>(image+0x2620a0)(output,&recipe,2,0,1,0);
    assert(recipe.outputs[2].type==254 && recipe.outputs[0].type==0);
    const auto compilerLinker=originalLinker;const bool compilerInstalled=installed;originalLinker=reinterpret_cast<Linker>(image+0x2620a0);installed=true;
    for(int input=1;input<=7;++input)for(int slot=0;slot<3;++slot){
        Recipe encoded{};encoded.header[0]=1;std::string text="useitem"+std::to_string(input);
        LinkerHook(text.data(),&encoded,slot,0,1,0);assert(encoded.header[0]&&IndexedUse(encoded.outputs[slot])&&UseInput(encoded.outputs[slot])==input);
        std::array<uint8_t,sizeof(Recipe)> bin{};memcpy(bin.data(),&encoded,sizeof(encoded));Recipe loaded{};memcpy(&loaded,bin.data(),sizeof(loaded));assert(UseInput(loaded.outputs[slot])==input);
    }
    originalLinker=compilerLinker;installed=compilerInstalled;

    Reset();
    auto pointer=[&](uint32_t rva,uintptr_t value){memcpy(image+rva,&value,8);};
    pointer(0x1ca0390,itemTable);pointer(0x1ca0398,itemCount);
    pointer(0x1c9eba0,3); // Item-code linker; host lookup uses the compiled table.
    pointer(0x1c9fc68,1);pointer(0x1c9fc48,2);
    pointer(0x1c9fc70,uniqueTable);pointer(0x1c9fc50,setTable);
    stub(0x6d01a0,NativeCodeLookup);stub(0x6d0470,NativeNameLookup);
    FlushInstructionCache(GetCurrentProcess(),image,nt->OptionalHeader.SizeOfImage);
    for(const auto& entry:std::array<std::pair<const char*,int>,3>{{{"cap",0},{"The Unique",1},{"The Set",2}}}){
        const auto target=Resolve(entry.first,1,0);assert(target.Valid() && target.kind==entry.second);
        assert(target.index==(entry.second==0 ? 1 : 0));
    }
    assert(Resolve("Z004",1,0).index==3);
    pointer(0x1c9fb78,statTable);pointer(0x1c9fb80,statCount);pointer(0x1c9fb90,linker);
    pointer(0x1c9fc78,specialCount);pointer(0x1c9fc58,specialCount);
    stub(0x6d0470,Row);stub(0x1e5620,Get);stub(0x42d550,NativeAsset);
    FlushInstructionCache(GetCurrentProcess(),image,nt->OptionalHeader.SizeOfImage);
    for(int kind=0;kind<3;++kind){
        installed=true;stored=Target{kind,kind==0 ? 1 : 0}.Encode();
        observedBase=kind==0 ? 1 : kind==1 ? 2 : 3;observedQuality=kind==0 ? 2 : kind==1 ? 7 : 5;observedSpecial=kind==0 ? -1 : 0;
        const auto before=itemData;
        assert(!strcmp(WorldPath(reinterpret_cast<void*>(1),&item),"items/native_target_model"));
        assert(!memcmp(&before,&itemData,sizeof(before)) && item.dwClassId==0);
    }
    installed=false;
    NativeSavedModifiers(image);QuestPropertyNativeTest::Native(image);RuneBagTest::NativeOutputRefusal(image);
    testImage=0;VirtualFree(image,0,MEM_RELEASE);
    std::cout<<"All native cube signatures, MinHook lifecycle and actual native useitem parser/linker offsets passed in a private image.\n";
}
void ConditionParsing(){
    const std::string header="enabled\tnuminputs\top\tparam\tvalue\top2\tparam2\tvalue2\top3\tparam3\tvalue3\toplogic\toutput\n";
    auto source=[&](const std::string& values){return ParseSource(header+values+"\n");};
    assert(source("1\t2\t18\t398\t7\t\t\t\t\t\t\t\tcap").conditions.empty());
    auto parsed=source("1\t2\t18\t398\t7\t3\t12\t10\t18\t474\t5\t or \tcap");
    assert(parsed.conditions.size()==1 && parsed.visuals.empty() && parsed.conditions.at(0).valid && parsed.conditions.at(0).any);
    for(const char* number:{"3x","-1","29","2147483648"})assert(!source(std::string("1\t2\t18\t398\t7\t")+number+"\t12\t10\t\t\t\tAND\tcap").conditions.at(0).valid);
    assert(!source("1\t2\t18\t398\t7\t3\t12abc\t10\t\t\t\tAND\tcap").conditions.at(0).valid);
    assert(!source("1\t2\t18\t398\t7\t3\t12\t2147483648\t\t\t\tAND\tcap").conditions.at(0).valid);
    assert(source("1\t2\t18\t398\t7\t3\t12\t-2147483648\t\t\t\t\tcap").conditions.at(0).valid);
    std::cout<<"Extended cube column parsing preserves ordinary rows and rejects invalid operators, numbers and logic.\n";
}
void ConditionMetadata(const std::filesystem::path& excel){
    const auto path=excel.parent_path().parent_path()/"D2RHUD/table-metadata.json";
    std::ifstream file(path,std::ios::binary);assert(file);const auto metadata=nlohmann::json::parse(file);
    const auto text=metadata.at("tables").at("cubemain").at("source").get<std::string>();
    const auto header=text.substr(0,text.find('\n'));
    for(const char* column:{"op2","param2","value2","op3","param3","value3","oplogic"})assert(header.find(column)!=std::string::npos);
    std::ifstream binary(excel/"cubemain.bin",std::ios::binary);uint32_t rows=0;binary.read(reinterpret_cast<char*>(&rows),4);
    const auto source=ParseSource(text);assert(source.rows==rows&&source.conditions.empty());
    std::cout<<"Installed BIN metadata preserves all "<<rows<<" recipes and exposes seven blank extended-condition columns without changing current recipe behavior.\n";
}
int main(int argc,char** argv){
    std::cout<<std::unitbuf;
    AddVectoredExceptionHandler(1,NativeFault);
    for(const auto& text:{"useitem,vis=cap","visual=cap"," USEITEM , VIS = Z004 ","useitem,visual=The Unique","\"useitem,vis=cap\"","useitem,visual=\"The, Unique\""}){auto option=Parse(text);assert(option.present && option.valid && !option.target.empty());}
    assert(!Parse("useitem,eth").present);
    for(const auto& text:{"useitem,vis=","useitem,vis=cap,visual=cap","usetype,vis=cap","useitem,vis=cap,qty=2","useitem,visual=\"bad"})assert(!Parse(text).valid);
    for(int kind=0;kind<3;++kind)for(int index:{0,4095}){Target target{kind,index};auto other=Target::Decode(target.Encode());assert(other.kind==kind && other.index==index);}
    if(argc>3)eventTableFixture=argv[3];
    for(int n=1;n<=7;++n){const auto token=ParseUseItem("useitem"+std::to_string(n)+",eth");assert(token.present&&token.valid&&token.input==n&&token.native=="useitem,eth");}
    for(const char* token:{"useitem0","useitem8","useitem77","useitem2,useitem3"})assert(!ParseUseItem(token).valid);
    ConditionParsing();Bind();Reset();CompiledVisualNames();Adapters();MapRecipes();RuneBagTest::Run();if(argc>2){RuneBagTest::LoadedTables(argv[2]);ConditionMetadata(argv[2]);}if(argc>1)Executable(argv[1]);
}
