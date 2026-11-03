#define NOMINMAX
#include "../d2rhud/plugin/RunTrackerNative.cpp"
#include <cassert>
#include <iostream>
using namespace RunTracker;
namespace {
uint32_t selector=0;D2UnitStrc player{},sword{},stash{},ground{};D2PlayerDataStrc playerData{};D2InventoryStrc inventory{};
D2ItemDataStrc swordData{},stashData{},groundData{};D2SharedInventoryStrc shared{};D2DataTablesStrc tables{};D2ItemsTxt rows[2]{};
D2DynamicPathStrc path{};D2ActiveRoomStrc room{};D2DrlgRoomStrc drlgRoom{};D2DrlgLevelStrc level{};D2DrlgStrc drlg{};
std::array<D2UnitStrc*,5*128> hash{};int names=0,guards=0;bool locked=false;
D2UnitStrc* __fastcall Local(uint32_t){return &player;}
int32_t __fastcall Stat(D2UnitStrc* item,uint32_t stat,uint16_t){assert(locked);if(stat==STAT_LEVEL)return 115;if(stat==STAT_QUANTITY&&item==&stash)return 4;return 0;}
void Name(D2UnitStrc* item,char* output){assert(locked);++names;strcpy_s(output,1024,item==&sword?"The Grandfather":"Ber Rune");}
}
bool RunTracker::Enabled(){return true;}
DWORD64 Pattern::Address(uint32_t rva){
    if(rva==0x1d442d8)return reinterpret_cast<DWORD64>(&selector);
    if(rva==0x6b050)return reinterpret_cast<DWORD64>(Local);
    if(rva==0x1c9e980)return reinterpret_cast<DWORD64>(&tables);
    if(rva>=0x1d442e0&&rva<0x1d442e0+sizeof(hash))return reinterpret_cast<DWORD64>(hash.data())+rva-0x1d442e0;
    return 0;
}
int main(){
    const auto unicode=Clean(std::string(767,'x')+"Ж");assert(Json::accept(Json(unicode).dump())&&unicode.size()==767);
    STATLIST_GetUnitStatSigned=Stat;SetNameProvider(Name);
    SetReadGuard([](void(*read)(void*),void* context){assert(!locked);locked=true;++guards;read(context);locked=false;});
    player.dwUnitType=0;player.dwUnitId=1;player.dwAnimMode=1;player.pPlayerData=&playerData;strcpy_s(playerData.szName,"NativeHero");player.pInventory=&inventory;player.pDynamicPath=&path;
    inventory.pOwner=&player;inventory.pFirstItem=inventory.pLastItem=&sword;
    sword.dwUnitType=4;sword.dwUnitId=9;sword.dwAnimMode=IMODE_STORED;sword.pItemData=&swordData;swordData.dwInitSeed=12345;swordData.dwQualityNo=7;swordData.pExtraData.pParentInv=&inventory;
    path.pRoom=&room;room.pDrlgRoom=&drlgRoom;drlgRoom.pLevel=&level;level.nLevelId=108;level.pDrlg=&drlg;drlg.nDifficulty=2;
    tables.pItemsTxt=rows;tables.nItemsTxtRecordCount=2;memcpy(rows[0].szCode,"7gd",4);memcpy(rows[1].szCode,"r30",4);
    HudFrame::Context context;context.inGame=true;
    auto f=Sample(context,1,"RMD-MP");assert(f.character=="NativeHero"&&f.area==108&&f.level==115&&f.difficulty==2&&f.storage.at("Character").size()==1&&f.storage.at("Character")[0].name=="The Grandfather");assert(names==1);
    Native().captured=0;f=Sample(context,1,"RMD-MP");assert(names==1); // names cached, no repeated native formatting
    assert(f.storage.at("Character")[0].sockets==0&&f.storage.at("Character")[0].stackableKnown&&!f.storage.at("Character")[0].stackable);
    D2InventoryStrc sockets{};D2UnitStrc socket{};D2ItemDataStrc socketData{};sword.pInventory=&sockets;sockets.pFirstItem=sockets.pLastItem=&socket;socket.dwUnitType=4;socket.pItemData=&socketData;socketData.pExtraData.pParentInv=&sockets;
    Native().captured=0;f=Sample(context,1,"RMD-MP");assert(f.storage.at("Character")[0].sockets==1);
    socketData.pExtraData.pNextItem=&socket;Native().captured=0;f=Sample(context,1,"RMD-MP");assert(f.storage.at("Character")[0].sockets==-1);sword.pInventory=nullptr;
    shared.dwUnitId=42;inventory.pFirstSharedInv=inventory.pLastSharedInv=&shared;inventory.nSharedInvCount=1;
    stash.dwUnitType=4;stash.dwUnitId=10;stash.dwClassId=1;stash.dwAnimMode=IMODE_STORED;stash.pItemData=&stashData;stashData.dwInitSeed=54321;stashData.dwOwnerGUID=42;stashData.dwQualityNo=2;
    ground.dwUnitType=4;ground.dwUnitId=11;ground.dwAnimMode=IMODE_ONGROUND;ground.pItemData=&groundData;groundData.dwOwnerGUID=42;
    hash[4*128]=&ground;ground.pListNext=&stash;
    SetStashPage(2,true);StashVisible();Native().captured=0;f=Sample(context,1,"RMD-MP");assert(f.storage.size()==2&&f.storage.at("Shared page 3 / tab 1").size()==1&&f.storage.at("Shared page 3 / tab 1")[0].quantity==4&&names==2);
    assert(f.storage.at("Shared page 3 / tab 1")[0].stackableKnown&&f.storage.at("Shared page 3 / tab 1")[0].stackable);
    SetStashPage(3,false);Native().captured=0;f=Sample(context,1,"RMD-MP");assert(f.storage.size()==1); // no partial page during switch
    SetStashPage(3,true);Native().captured=0;f=Sample(context,1,"RMD-MP");assert(f.storage.contains("Shared page 4 / tab 1"));
    stash.pListNext=&stash;Native().captured=0;f=Sample(context,1,"RMD-MP");assert(f.storage.size()==1);stash.pListNext=nullptr; // cyclic hash cannot become an empty stash
    swordData.pExtraData.pNextItem=&sword;Native().captured=0;f=Sample(context,1,"RMD-MP");assert(!f.storage.contains("Character"));swordData.pExtraData.pNextItem=nullptr;
    context.loading=true;f=Sample(context,1,"RMD-MP");assert(f.storage.empty());context.loading=false;selector=8;f=Sample(context,1,"RMD-MP");assert(f.loading&&f.storage.empty());selector=0;
    Native().captured=0;f=Sample(context,2,"RMD-MP");assert(names>2); // new session invalidates name cache
    assert(guards>5&&!locked);std::cout<<"PASS: guarded native character/area/difficulty/level; inventory and shared-page ownership; cursor-safe linked lists; cached original names; stack quantities; ground items excluded; loading/page-switch/cyclic and unavailable containers withheld; session invalidation. Private fixture memory only.\n";
}
