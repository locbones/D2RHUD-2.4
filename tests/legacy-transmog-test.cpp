// Native adapters run against owned units and mock game functions, never a save.
#include "../d2rhud/plugin/LegacyTransmog.cpp"
#include <cassert>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <vector>
#include <intrin.h>

using namespace LegacyTransmog;
using namespace LegacyTransmog::Data;
std::map<uint32_t, uintptr_t> bindings;
uintptr_t testImage = 0;
DWORD64 Pattern::Address(uint32_t rva) { return testImage ? testImage + rva : bindings[rva]; }
struct OwnedItem { D2UnitStrc unit{}; D2ItemDataStrc data{}; D2StaticPathStrc path{}; bool freed = false; int quantity=0; };
std::deque<OwnedItem> items;
std::array<std::array<uint8_t, ItemRecordSize>, 4> table{};
uintptr_t tablePointer = reinterpret_cast<uintptr_t>(table.data());
uint64_t itemCount = table.size();
uint32_t selector = 0;
D2UnitStrc player{};
D2InventoryStrc inv{};
D2ClientStrc client{};
D2ClientStrc* clientHead = &client;
D2GameStrc game{};
D2DynamicPathStrc playerPath{};
D2ActiveRoomStrc room{};
int createCalls = 0, removeCalls = 0, addCalls = 0, fallbackCalls = 0, sends = 0, groundCalls = 0;
bool failCreate = false, failRemove = false, groundAvailable = true, failRestore = false;
bool retainOnRemove = false;
extern bool splitTest;
int acceptedX = 2, acceptedY = 3;
uint32_t nextId = 100;
Drop lastDrop{};
int lastQuantity = -1;
Unit* lastCreated = nullptr;
struct Notification { uint32_t id, command, flags; Location location; };
std::vector<Notification> notifications;
OwnedItem& Entry(Unit* item) { for (auto& entry : items) if (&entry.unit == item) return entry; std::abort(); }
Unit* Make(uint32_t cls) {
    auto& entry = items.emplace_back();
    entry.unit.dwUnitType = 4; entry.unit.dwClassId = cls; entry.unit.dwUnitId = nextId++;
    entry.unit.pItemData = &entry.data; entry.unit.pStaticPath = &entry.path;
    entry.data.dwItemLevel = 42; entry.data.dwInitSeed = 12345; entry.data.dwItemFlags = 0x10;
    entry.path.tGameCoord = {2,3};
    return &entry.unit;
}
Unit* __fastcall GetPlayer(uint32_t index) { assert(index == 0); return &player; }
Unit* __fastcall Find(Game* g, int type, uint32_t id) {
    assert(g == &game && type == 4);
    for (auto& entry : items) if (!entry.freed && entry.unit.dwUnitId == id) return &entry.unit;
    return nullptr;
}
Unit* __fastcall Create(Game* g, Drop* drop, int force) {
    ++createCalls; assert(g == &game && !force); lastDrop = *drop;
    if (failCreate) return nullptr;
    lastCreated = Make(drop->id); lastCreated->dwItemMode = 4; lastCreated->pItemData->nPage = 255;
    return lastCreated;
}
void __fastcall SetStat(Unit* item, int stat, int value, uint16_t layer) {
    assert(stat == 70 && !layer); lastQuantity = value;Entry(item).quantity=value;
}
Location* __fastcall GetLocation(Location* location, Unit* item) {
    *location = {item->dwItemMode, item->pItemData->nPage, uint16_t(item->pStaticPath->tGameCoord.X), uint16_t(item->pStaticPath->tGameCoord.Y), 0};
    return location;
}
bool __fastcall Remove(Unit* p, const Location* location, const Location* second, const uint32_t* ids,
    const uint32_t* ids2, size_t count, size_t count2, bool special) {
    ++removeCalls; assert(p == &player && !location && second && !ids && ids2 && !count && count2 == 1 && !special);
    auto item = Find(&game,4,*ids2); assert(item && item->pItemData->pExtraData.pParentInv == &inv);
    if (failRemove) return false;
    if (retainOnRemove) return true;
    item->pItemData->pExtraData.pParentInv = nullptr;
    return true;
}
bool __fastcall Add(Unit* p, const Location* location, const uint32_t* ids, size_t count, bool special) {
    ++addCalls; assert(p == &player && count == 1 && !special);
    auto item = Find(&game,4,*ids); assert(item && !item->pItemData->pExtraData.pParentInv && !item->pStaticPath->pRoom);
    bool accepted = item != lastCreated ? (!failRestore || location->mode == 4) :
        (location->mode == 0 && location->page == 0 && location->x == acceptedX && location->y == acceptedY);
    if(splitTest)accepted=location->mode==0 && StackData::StoragePage(location->page) && location->x==acceptedX && location->y==acceptedY;
    if (!accepted) return false;
    item->pItemData->pExtraData.pParentInv = &inv; item->dwItemMode = location->mode;
    item->pItemData->nPage = uint8_t(location->page); item->pStaticPath->tGameCoord = {location->x, location->y};
    return true;
}
void __fastcall Free(Game* g, Unit* item) {
    assert(g == &game && item && !item->pItemData->pExtraData.pParentInv && !item->pStaticPath->pRoom);
    auto& entry = Entry(item); assert(!entry.freed); entry.freed = true;
}
void __fastcall Notify(Game* g,Unit* p,Unit* item,uint32_t flags,uint32_t command,uint32_t mode,uint32_t page,uint32_t body,uint32_t xy) {
    assert(g==&game && p==&player && !Entry(item).freed);
    notifications.push_back({item->dwUnitId,command,flags,{mode,page,uint16_t(xy),uint16_t(xy>>16),body}});
}
D2ActiveRoomStrc* __fastcall GetRoom(Unit* item) { return item == &player ? &room : item->pStaticPath->pRoom; }
D2ActiveRoomStrc* __fastcall Space(D2ActiveRoomStrc* r, D2CoordStrc* from, D2CoordStrc* to, int size,
    int mask, int mask2, int flag, int tries, int step) {
    assert(r == &room && size == 1 && mask == 0x3e01 && mask2 == 0x801 && flag == 1 && tries == 50 && step == 1);
    *to = *from; return groundAvailable ? &room : nullptr;
}
void __fastcall Ground(Game* g, Unit* p, Unit* item, D2ActiveRoomStrc* r, int x, int y) {
    ++groundCalls; assert(g == &game && p == &player && item == lastCreated && r == &room);
    item->pStaticPath->pRoom = r; item->pStaticPath->tGameCoord = {uint32_t(x),uint32_t(y)}; item->dwItemMode = 3;
}
void __fastcall Send(bool targeted, uint32_t id, uint32_t action, uint32_t page, uint32_t mode, uint32_t owner,
    uint32_t a, uint32_t b, uint32_t c, void* d, void* e, void* f) {
    ++sends; assert(!targeted && Find(&game,4,id) && !action && !page && !mode && owner == player.dwUnitId && !a && b == 255 && !c && !d && !e && !f);
}
bool __fastcall FallbackCan(Unit*, int) { ++fallbackCalls; return false; }
void __fastcall FallbackUse(Unit*, int) { ++fallbackCalls; }
int __fastcall FallbackPacket(Game*, Unit*, const uint8_t*, int) { ++fallbackCalls; return 3; }
const char* __fastcall LocalizedName(uint16_t index, const char* fallback) {
    if (index == 100) return "Original Charm";
    if (index == 101) return "Destination Charm";
    if (index == 102) return "\xc3\x89" "p\xc3\xa9" "e\nCharm";
    if (index == 103) return "\xc3\xbf" "c3Sapphire \xc3\xbf" "c4Remover";
    return fallback;
}
TooltipString* __fastcall AppendText(TooltipString* out, const char* text, size_t size) {
    assert(out->length + size < (out->capacity & 0x7fffffffffffffffULL));
    memcpy(out->text + out->length, text, size); out->length += size; out->text[out->length] = 0;
    return out;
}
bool emptyFooter = false;
const char* nativeControls = "Ctrl + Left-Click to Drop";
TooltipString* __fastcall BuildTooltip(void* widget, TooltipString* out, Unit* item) {
    assert(widget == &player && item);
    out->length = 0;
    out->text[0] = 0;
    return emptyFooter ? out : AppendText(out, nativeControls, strlen(nativeControls));
}
std::array<uint8_t,36> Command(Unit* item) {
    std::array<uint8_t,36> bytes{}; bytes[0] = 0x26;
    memcpy(bytes.data()+2,&player.dwUnitId,4); memcpy(bytes.data()+10,&item->dwUnitId,4); return bytes;
}
void Reset() {
    splitView={};splitSession={};splitPending={};splitReady=false;quantityRightClicks=true;
    items.clear(); lastCreated = nullptr; table = {};
    notifications.clear();
    for (size_t i=0;i<table.size();++i) { uint32_t code = 0x2032305a + uint32_t(i << 16); memcpy(table[i].data()+0x80,&code,4); }
    for (size_t i=0;i<2;++i) { table[i][0x141]=1; table[i][0x142]=1; table[i][0x143]=1; memcpy(table[i].data()+0xd0,table[1-i].data()+0x80,4); }
    player = {}; player.dwUnitId=77; player.dwAnimMode=1; player.pInventory=&inv; player.pDynamicPath=&playerPath;
    inv = {}; inv.dwSignature=0x01020304; inv.pOwner=&player;
    client = {}; client.dwUnitGUID=77; client.dwClientState=4; client.pGame=&game; clientHead=&client;
    playerPath.wPosX=500; playerPath.wPosY=500;
    createCalls=removeCalls=addCalls=fallbackCalls=sends=groundCalls=0;
    failCreate=failRemove=failRestore=retainOnRemove=false; groundAvailable=true; acceptedX=2; acceptedY=3;
    for (uint16_t i=0;i<table.size();++i) { uint16_t name=100+i;memcpy(table[i].data()+0xfc,&name,2); }
    originalCanUse=FallbackCan; originalUse=FallbackUse; originalPacket=FallbackPacket; originalTooltip=BuildTooltip; ready=true;
    emptyFooter=false;
}
Unit* Source() { auto item=Make(0); item->pItemData->pExtraData.pParentInv=&inv; return item; }
void Bind() {
    bindings={{0x1ca0390,reinterpret_cast<uintptr_t>(&tablePointer)},{0x1ca0398,reinterpret_cast<uintptr_t>(&itemCount)},
        {0x1d442d8,reinterpret_cast<uintptr_t>(&selector)},{0x1d637f0,reinterpret_cast<uintptr_t>(&clientHead)},
        {0x6b050,reinterpret_cast<uintptr_t>(GetPlayer)},{0x2c49c0,reinterpret_cast<uintptr_t>(Find)},
        {0x2f58c0,reinterpret_cast<uintptr_t>(Create)},{0x1e5440,reinterpret_cast<uintptr_t>(SetStat)},
        {0x213a20,reinterpret_cast<uintptr_t>(GetLocation)},{0x2dba00,reinterpret_cast<uintptr_t>(Remove)},
        {0x2d5e10,reinterpret_cast<uintptr_t>(Add)},{0x2f70a0,reinterpret_cast<uintptr_t>(Free)},
        {0x209530,reinterpret_cast<uintptr_t>(GetRoom)},{0x231510,reinterpret_cast<uintptr_t>(Space)},
        {0x2fa090,reinterpret_cast<uintptr_t>(Ground)},{0x10eae0,reinterpret_cast<uintptr_t>(Send)},
        {0x316b80,reinterpret_cast<uintptr_t>(Notify)},
        {0x413860,reinterpret_cast<uintptr_t>(LocalizedName)},{0x5c7e0,reinterpret_cast<uintptr_t>(AppendText)}};
}
void TooltipContract() {
    Reset(); auto source=Source();
    char storage[1024]{}; TooltipString out{storage,0,sizeof(storage)-1};
    assert(TooltipHook(&player,&out,source)==&out);
    assert(std::string(storage)=="\xc3\xbf" "c5Right-Click to change to Destination Charm\xc3\xbf" "c5\n"+std::string(nativeControls));
    uint16_t coloredName=103;memcpy(table[1].data()+0xfc,&coloredName,2);
    TooltipHook(&player,&out,source);
    assert(std::string(storage)=="\xc3\xbf" "c5Right-Click to change to \xc3\xbf" "c3Sapphire \xc3\xbf" "c4Remover\xc3\xbf" "c5\n"+std::string(nativeControls));
    // The renderer visits these logical lines in reverse: the native drop
    // instruction is above the new action, with the same explicit grey applied
    // to its prefix and the following native controls.
    assert(std::string(storage).substr(out.length-strlen(nativeControls))==nativeControls);
    emptyFooter=true;TooltipHook(&player,&out,source);
    assert(std::string(storage)=="\xc3\xbf" "c5Right-Click to change to \xc3\xbf" "c3Sapphire \xc3\xbf" "c4Remover\xc3\xbf" "c5");
    emptyFooter=false;
    source->dwClassId=1; assert(TargetName(source)=="Original Charm");
    source->dwClassId=0;
    uint32_t target=0;memcpy(&target,table[2].data()+0x80,4);memcpy(table[0].data()+0xd0,&target,4);
    assert(TargetName(source)=="\xc3\x89" "p\xc3\xa9" "e Charm");
    uint16_t missing=65535;memcpy(table[2].data()+0xfc,&missing,2);assert(TargetName(source).empty());
    table[0][0x141]=0;assert(TargetName(source).empty());
    TooltipHook(&player,&out,source);assert(std::string(storage)==nativeControls);
    Reset();source=Source();clientHead=nullptr;
    TooltipHook(&player,&out,source);assert(std::string(storage)==nativeControls);
    Reset();ready=false;source=Source();
    TooltipHook(&player,&out,source);assert(std::string(storage)==nativeControls);
    std::cout << "Control footer respects reversed line order, uses matching explicit control greys, preserves name colours and handles empty/invalid/remote hints.\n";
}
// Run the real native wrapper control flow, replacing only game/world services.
// In particular, do NOT replace 2dba00: its two lists have different semantics.
Unit* nativeLookup = nullptr;
int appliedRemovals = 0, appliedAdds = 0;
void __fastcall Snapshot(void* out, Unit* p, D2InventoryStrc* inventory, Game* g, Unit**, size_t) {
    memset(out,0,0x4880);
    auto fields=static_cast<uintptr_t*>(out);
    fields[0]=reinterpret_cast<uintptr_t>(p);fields[1]=reinterpret_cast<uintptr_t>(inventory);fields[2]=reinterpret_cast<uintptr_t>(g);
    static_cast<uint8_t*>(out)[0x4878]=1;
}
Unit* __fastcall SnapshotRemove(void*,const Location*,bool) { return nativeLookup; }
bool __fastcall SnapshotAdd(void* ctx,Unit* item,const Location*,bool) { static_cast<Unit**>(ctx)[8]=item;return true; }
bool __fastcall Validate(void*,void*,bool) { return true; }
void __fastcall ApplyRemove(void**,const Location*) { ++appliedRemovals;nativeLookup->pItemData->pExtraData.pParentInv=nullptr; }
void __fastcall ApplyAdd(void** ctx,const Location* location) {
    ++appliedAdds;auto item=static_cast<Unit**>(*ctx)[8];assert(item);
    item->pItemData->pExtraData.pParentInv=&inv;item->dwItemMode=location->mode;item->pItemData->nPage=uint8_t(location->page);
}
Unit** __fastcall Bucket(int type,Game* g,uint32_t id) { assert(type==4 && g==&game && nativeLookup->dwUnitId==id);return &nativeLookup; }
void __fastcall NoOperation() {}
int __fastcall OrdinaryItemType(Unit*) { return 41; }
uintptr_t privateImage = 0;
const char* __fastcall PrivateAssertion() { return "private native test"; }
void __fastcall NativeAssertion(int,const char*,const char*,int line) {
    std::cerr << "Native assertion line " << line << " at RVA " << std::hex << (reinterpret_cast<uintptr_t>(_ReturnAddress())-privateImage) << "\n";
    std::abort();
}
void NativeInventoryContract(uint8_t* image) {
    std::cout << "Checking native inventory list contract...\n";
    auto stub=[&](uint32_t rva,auto function) {
        // Absolute indirect jump in an owned executable buffer; no live process.
        const uint8_t jump[]{0xff,0x25,0,0,0,0};memcpy(image+rva,jump,sizeof(jump));
        auto target=reinterpret_cast<uintptr_t>(function);memcpy(image+rva+6,&target,sizeof(target));
    };
    privateImage=reinterpret_cast<uintptr_t>(image);
    stub(0x6bc3b0,PrivateAssertion);stub(0x6bc0f0,NativeAssertion);
    stub(0x2d8a50,Snapshot);stub(0x2d9000,SnapshotRemove);stub(0x2d9c90,SnapshotAdd);stub(0x2d9120,Validate);
    stub(0x2dc070,ApplyRemove);stub(0x2dc2a0,ApplyAdd);stub(0x2c4950,Bucket);
    stub(0x2d67e0,NoOperation);stub(0x2d4070,NoOperation);stub(0x2d4870,NoOperation);stub(0x2a1ef0,NoOperation);
    stub(0x1fa060,OrdinaryItemType);
    stub(0x1434c50,NoOperation);
    FlushInstructionCache(GetCurrentProcess(),image,0x2000000);
    Reset();auto source=Source();nativeLookup=source;
    *reinterpret_cast<Game**>(reinterpret_cast<uint8_t*>(&player)+0xd8)=&game;
    const uint32_t id=source->dwUnitId;Location location{0,0,2,3,0};
    auto nativeRemove=reinterpret_cast<RemoveItem>(image+0x2dba00);
    appliedRemovals=appliedAdds=0;
    assert(nativeRemove(&player,&location,nullptr,&id,nullptr,1,0,false));
    assert(!appliedRemovals && source->pItemData->pExtraData.pParentInv==&inv); // Old call reproduces the bug.
    Inventory adapter{&game,&player,source,nullptr,{},0,location};
    assert(adapter.Remove(location));
    assert(appliedRemovals==1 && !source->pItemData->pExtraData.pParentInv);
    assert(adapter.Add(source,location));
    assert(appliedAdds==1 && source->pItemData->pExtraData.pParentInv==&inv);
    std::cout << "Real native inventory wrappers reproduce the old update/remove mix-up and confirm corrected detachment and addition.\n";
}
struct ClientPacket { uint32_t id; uint8_t action; uint32_t mode, flags; uint8_t page; };
std::vector<ClientPacket> clientPackets;
bool __fastcall NoItemType(Unit*,int) { return false; }
void __fastcall CapturePacket(D2ClientStrc* c,Unit* owner,Unit* item,uint8_t action,int extra) {
    assert(c==&client && owner==&player && !extra);
    clientPackets.push_back({item->dwUnitId,action,item->dwItemMode,item->pItemData->dwItemFlags,item->pItemData->nPage});
}
void NativeNotificationContract(uint8_t* image) {
    auto stub=[&](uint32_t rva,auto function) {
        const uint8_t jump[]{0xff,0x25,0,0,0,0};memcpy(image+rva,jump,sizeof(jump));
        auto target=reinterpret_cast<uintptr_t>(function);memcpy(image+rva+6,&target,sizeof(target));
    };
    stub(0x1fe680,NoItemType);stub(0x2cdfc0,CapturePacket);stub(0x2cfc80,NoOperation);
    FlushInstructionCache(GetCurrentProcess(),image,0x2000000);
    Reset();auto source=Source();auto replacement=Make(1);
    source->pItemData->pExtraData.pParentInv=nullptr;
    replacement->pItemData->pExtraData.pParentInv=&inv;
    client.dwFlags=1;client.pPlayer=&player;game.pClientList=&client;
    D2PlayerDataStrc playerData{};playerData.pClient=&client;player.pPlayerData=&playerData;
    Location origin{0,0,2,3,0};
    Inventory adapter{&game,&player,source,replacement,{},1,origin};
    clientPackets.clear();
    auto sourceFlags=source->pItemData->dwItemFlags;auto replacementFlags=replacement->pItemData->dwItemFlags;
    adapter.Notify(source,origin,0x20,4);
    adapter.Notify(replacement,origin,1,2);
    assert(clientPackets.size()==2 && clientPackets[0].id==source->dwUnitId && clientPackets[0].action==5 &&
        clientPackets[1].id==replacement->dwUnitId && clientPackets[1].action==4 && (clientPackets[1].flags&1));
    assert(source->pItemData->dwItemFlags==sourceFlags && replacement->pItemData->dwItemFlags==replacementFlags &&
        !source->pItemData->dwCommandFlags && !replacement->pItemData->dwCommandFlags);
    player.pPlayerData=nullptr;game.pClientList=nullptr;
    std::cout << "Real native notification routines emit remove/add packets in order and restore temporary item fields.\n";
}
void NativeTooltipContract(uint8_t* image) {
    // One bucket makes the real localizer's hash independent of the test ID.
    std::array<uint8_t,32> node{};
    uint16_t index=101;memcpy(node.data()+8,&index,2);
    const char* name="Localized Target";memcpy(node.data()+24,&name,8);
    auto bucket=reinterpret_cast<uintptr_t>(node.data());
    uint64_t map[]{1,reinterpret_cast<uintptr_t>(&bucket),0,0};
    memcpy(image+0x185da20,map,sizeof(map));memcpy(image+0x185da40,map,sizeof(map));
    auto lookup=reinterpret_cast<const char*(__fastcall*)(uint16_t,const char*)>(image+0x413860);
    for(uint8_t alternate:{uint8_t(0),uint8_t(1)}) {
        image[0x1d60dd7]=alternate;
        assert(lookup(101,nullptr)==name && lookup(100,nullptr)==nullptr);
    }
    // Execute the native append/reserve implementation against owned storage.
    // Capacity avoids requiring the game's allocator in this private image.
    const uint8_t jump[]{0xff,0x25,0,0,0,0};memcpy(image+0x14383a0,jump,sizeof(jump));
    auto move=reinterpret_cast<uintptr_t>(&memmove);memcpy(image+0x14383a6,&move,8);
    FlushInstructionCache(GetCurrentProcess(),image,0x2000000);
    char storage[1024]="Native stats\n";
    TooltipString out{storage,strlen(storage),0x80000000000003ffULL};
    const std::string legend="\xc3\xbf" "c0Right-Click to change to Localized Target\xc3\xbf" "c0\n";
    auto append=reinterpret_cast<TooltipString*(__fastcall*)(TooltipString*,const char*,size_t)>(image+0x5c7e0);
    assert(append(&out,legend.data(),legend.size())==&out);
    assert(std::string(storage)=="Native stats\n"+legend && out.length==strlen(storage));
    std::cout << "Real native localization and tooltip append preserve content and update the measured string length.\n";
}
#include "stack-split-test.inl"
#include "socket-stack-test.inl"
void VerifyExecutable(const std::filesystem::path& path) {
    std::ifstream input(path,std::ios::binary|std::ios::ate); assert(input);
    std::vector<uint8_t> file(size_t(input.tellg())); input.seekg(0); input.read(reinterpret_cast<char*>(file.data()),file.size()); assert(input);
    auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(file.data()); assert(dos->e_magic == IMAGE_DOS_SIGNATURE);
    auto nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(file.data()+dos->e_lfanew); assert(nt->Signature == IMAGE_NT_SIGNATURE);
    auto image=static_cast<uint8_t*>(VirtualAlloc(nullptr,nt->OptionalHeader.SizeOfImage,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE)); assert(image);
    auto sections=IMAGE_FIRST_SECTION(nt);
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i) memcpy(image+sections[i].VirtualAddress,file.data()+sections[i].PointerToRawData,sections[i].SizeOfRawData);
    testImage=reinterpret_cast<uintptr_t>(image); assert(Supported() && SplitSupported() && SocketStackSupported());
    for(auto rva:{SocketPacketRva,0x2b1dd0u,0x2ce230u}){image[rva]^=1;assert(!SocketStackSupported());image[rva]^=1;}
    for(size_t i=0;i<SplitRoutines.size();++i){image[SplitRoutines[i]]^=1;assert(!SplitSupported());image[SplitRoutines[i]]^=1;}
    for(const auto& sig:Signatures) { image[sig.rva]^=1; uint32_t failed=0; assert(!Supported(&failed) && failed==sig.rva); image[sig.rva]^=1; }
    DWORD previous = 0; assert(VirtualProtect(image,nt->OptionalHeader.SizeOfImage,PAGE_EXECUTE_READWRITE,&previous));
    ready=false;
    image[Signatures[0].rva]^=1; assert(!Install()); image[Signatures[0].rva]^=1;
    std::array<uint8_t,32> descriptionEntry{};memcpy(descriptionEntry.data(),image+0x1bb940,descriptionEntry.size());
    assert(Install() && ready.load() && splitReady.load() && socketStackReady.load() && !Supported());
    assert(!memcmp(descriptionEntry.data(),image+0x1bb940,descriptionEntry.size())); // Main reversed description is untouched.
    Uninstall(); assert(!ready.load() && !socketStackReady.load() && Supported() && SocketStackSupported());
    NativeInventoryContract(image);
    NativeNotificationContract(image);
    NativeTooltipContract(image);
    NativeStackCopyContract(image);
    NativeSocketStackContract(image);
    testImage=0; VirtualFree(image,0,MEM_RELEASE);
    std::cout << "All 20 native routine signatures match; each rejects modified instructions. MinHook install/uninstall passed in an owned executable image.\n";
}
int main(int argc,char** argv) {
    std::cout << std::unitbuf;
    Bind(); Reset();
    TooltipContract();Reset();
    // Real live Z02 copies: client retains NEWITEM, authoritative copy does not.
    auto clientItem=Source(); clientItem->pItemData->dwItemFlags=0xa00011; clientItem->dwFlagsEx=0x2000080;
    assert(CanUseHook(clientItem,1)); UseHook(clientItem,1); assert(sends==1);
    assert(PersonalItem(&player,clientItem) && !PersonalItem(&player,clientItem,true));
    clientItem->pItemData->dwItemFlags=0xa80010;clientItem->dwFlagsEx=0x6000000;
    assert(PersonalItem(&player,clientItem,true));
    Reset();
    auto source=Source(); assert(CanUseHook(source,1)); assert(!CanUseHook(source,2)); UseHook(source,1); assert(sends==1 && !createCalls);
    auto bytes=Command(source); assert(PacketHook(&game,&player,bytes.data(),36)==0);
    assert(Entry(source).freed && lastCreated->dwClassId==1 && lastQuantity==1 && addCalls==1);
    assert(notifications.size()==2 && notifications[0].id==source->dwUnitId && notifications[0].command==4 &&
        notifications[1].id==lastCreated->dwUnitId && notifications[1].command==2 && notifications[1].flags==1);
    assert(lastDrop.level==42 && lastDrop.quality==2 && lastDrop.index==2 && lastDrop.mode==4 && lastDrop.itemSeed==12345);
    source=lastCreated; bytes=Command(source); PacketHook(&game,&player,bytes.data(),36);
    assert(Entry(source).freed && lastCreated->dwClassId==0 && createCalls==2); // Z02 <-> Z03
    Reset(); source=Source(); acceptedX=7;acceptedY=5;bytes=Command(source);PacketHook(&game,&player,bytes.data(),36);
    assert(Entry(source).freed && lastCreated->pStaticPath->tGameCoord.X==7 && lastCreated->pStaticPath->tGameCoord.Y==5);
    Reset(); source=Source();acceptedX=-1;bytes=Command(source);PacketHook(&game,&player,bytes.data(),36);
    assert(Entry(source).freed && groundCalls==1 && lastCreated->dwItemMode==3);
    Reset(); source=Source();acceptedX=-1;groundAvailable=false;bytes=Command(source);PacketHook(&game,&player,bytes.data(),36);
    assert(!Entry(source).freed && source->pItemData->pExtraData.pParentInv==&inv && Entry(lastCreated).freed);
    assert(notifications.empty());
    Reset(); source=Source();acceptedX=-1;groundAvailable=false;failRestore=true;bytes=Command(source);PacketHook(&game,&player,bytes.data(),36);
    assert(!Entry(source).freed && source->dwItemMode==4 && Entry(lastCreated).freed);
    for(bool createFailure:{true,false}) { Reset(); source=Source(); failCreate=createFailure;failRemove=!createFailure;bytes=Command(source);PacketHook(&game,&player,bytes.data(),36);assert(!Entry(source).freed && source->pItemData->pExtraData.pParentInv==&inv); }
    Reset();source=Source();retainOnRemove=true;bytes=Command(source);PacketHook(&game,&player,bytes.data(),36);
    assert(!Entry(source).freed && source->pItemData->pExtraData.pParentInv==&inv && Entry(lastCreated).freed && !addCalls);
    for(int invalid=0;invalid<11;++invalid) {
        Reset(); source=Source(); bytes=Command(source);
        if(invalid==0)source->pItemData->nPage=4;
        if(invalid==1)source->dwItemMode=1;
        if(invalid==2)source->pItemData->pExtraData.pParentInv=nullptr;
        if(invalid==3)player.dwAnimMode=17;
        if(invalid==4)source->pItemData->dwCommandFlags=1;
        if(invalid==5)source->pItemData->dwItemFlags|=8;
        if(invalid==6)bytes[2]^=1;
        if(invalid==7)bytes[1]=1;
        if(invalid==8)table[0][0xd0]='!';
        if(invalid==9){table[0][0x142]=20;table[0][0x143]=10;}
        if(invalid==10)source->pItemData->dwItemFlags|=1;
        PacketHook(&game,&player,bytes.data(),36);assert(!createCalls && !Entry(source).freed && !fallbackCalls);
    }
    Reset(); source=Source();clientHead=nullptr;assert(!CanUseHook(source,1));UseHook(source,1);assert(!sends);
    Reset(); source=Make(2);bytes=Command(source);assert(PacketHook(&game,&player,bytes.data(),36)==3);assert(fallbackCalls==1);
    Reset();source=Source();uint32_t fourCode=0x31323334;memcpy(table[3].data()+0x80,&fourCode,4);memcpy(table[0].data()+0xd0,&fourCode,4);
    bytes=Command(source);PacketHook(&game,&player,bytes.data(),36);assert(lastCreated->dwClassId==3);
    Definition def{true,1,3,9};uint32_t low=1,high=666;
    bool seen[9]{};for(int i=0;i<10000;++i){int qty=Quantity(def,low,high);assert(qty>=3 && qty<9);seen[qty]=true;}for(int i=3;i<9;++i)assert(seen[i]);
    def.minimum=def.maximum=200;assert(Quantity(def,low,high)==200);def.minimum=0;assert(Quantity(def,low,high)==0);
    assert(!Decode(std::span<const uint8_t>{}).enabled);
    std::cout << "Round trips, creation/quantity ABI, placement, ground fallback, rollback, packet validation, local-server gating and full four-byte targets passed.\n";
    StackSplitContract();
    SocketStackContract();
    if(argc>1)VerifyExecutable(argv[1]);
}
