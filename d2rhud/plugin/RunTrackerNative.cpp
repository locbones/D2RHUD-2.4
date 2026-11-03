#include "RunTracker.h"
#include "../Pattern.h"
#include "../SessionEpoch.h"
#include "../ProcessLifetime.h"
#include "../d2/D2Ptrs.h"
#include "ItemFilter/ItemText.h"
#include <Windows.h>
#include <chrono>
#include <atomic>
#include <set>

namespace RunTracker {
namespace {
template<class T> bool Read(const T* address,T& value){SIZE_T size=0;return address&&ReadProcessMemory(GetCurrentProcess(),address,&value,sizeof(value),&size)&&size==sizeof(value);}
template<class T> T Value(const T* address){T value{};Read(address,value);return value;}
using NameProvider=void(*)(D2UnitStrc*,char*);
std::atomic<NameProvider> originalName{nullptr};
using ReadGuard=void(*)(void(*)(void*),void*);std::atomic<ReadGuard> readGuard{nullptr};
std::atomic<bool> inventoryDirty{true};
std::atomic<uint32_t> stashPage{0};std::atomic<bool> stashReady{false};
std::atomic<uint64_t> stashPaint{0};
struct NativeState {uint64_t epoch=0,captured=0;std::map<std::pair<uint32_t,uint32_t>,std::pair<uint64_t,Item>> names;};
NativeState& Native(){return HudLifetime::State<NativeState>();}
std::string Clean(std::string text){
    std::string result;
    for(size_t i=0;i<text.size();){
        if(i+3<text.size()&&static_cast<unsigned char>(text[i])==0xc3&&static_cast<unsigned char>(text[i+1])==0xbf&&text[i+2]=='c'){i+=4;continue;}
        if(i+2<text.size()&&static_cast<unsigned char>(text[i])==0xff&&text[i+1]=='c'){i+=3;continue;}
        if(static_cast<unsigned char>(text[i])>=32)result+=text[i];else result+=' ';++i;
    }
    const auto length=MultiByteToWideChar(CP_UTF8,0,result.data(),int(result.size()),nullptr,0);
    if(length){std::wstring wide(length,L'\0');MultiByteToWideChar(CP_UTF8,0,result.data(),int(result.size()),wide.data(),length);const auto bytes=WideCharToMultiByte(CP_UTF8,0,wide.data(),length,nullptr,0,nullptr,nullptr);std::string valid(bytes,'\0');WideCharToMultiByte(CP_UTF8,0,wide.data(),length,valid.data(),bytes,nullptr,nullptr);result=std::move(valid);}
    char bounded[769]{};ItemText::Copy(bounded,sizeof(bounded),result);return bounded;
}
int Area(D2UnitStrc* unit){
    const auto path=Value(&unit->pDynamicPath);if(!path)return 0;
    const auto room=Value(&path->pRoom);if(!room)return 0;
    const auto drlg=Value(&room->pDrlgRoom);if(!drlg)return 0;
    const auto level=Value(&drlg->pLevel);return level?int(Value(&level->nLevelId)):0;
}
bool CaptureItem(D2UnitStrc* pointer,Item& item,D2ItemDataStrc& data){
    D2UnitStrc unit{};
    if(!Read(pointer,unit)||unit.dwUnitType!=4||!Read(unit.pItemData,data))return false;
    auto& cache=Native().names;
    const auto key=std::make_pair(unit.dwUnitId,data.dwInitSeed);
    const auto signature=(uint64_t(unit.dwClassId)<<32)|data.dwItemFlags;
    const auto cached=cache.find(key);
    if(cached!=cache.end()&&cached->second.first==signature&&cached->second.second.quality==int(data.dwQualityNo)&&cached->second.second.fileIndex==data.dwFileIndex){item=cached->second.second;}
    else{
        item.id=unit.dwUnitId;item.seed=data.dwInitSeed;item.quality=int(data.dwQualityNo);item.fileIndex=data.dwFileIndex;item.level=int(data.dwItemLevel);item.identified=(data.dwItemFlags&0x10)!=0;item.ethereal=(data.dwItemFlags&0x400000)!=0;
        const auto tables=reinterpret_cast<D2DataTablesStrc*>(Pattern::Address(0x1c9e980));
        const auto rows=Value(&tables->pItemsTxt);const auto count=Value(&tables->nItemsTxtRecordCount);
        D2ItemsTxt row{};if(rows&&count>0&&count<1000000&&unit.dwClassId<uint64_t(count)&&Read(rows+unit.dwClassId,row))item.code.assign(row.szCode,strnlen_s(row.szCode,4));else return false;
        char buffer[1024]{};if(const auto name=originalName.load())name(pointer,buffer);
        item.name=Clean(std::string(buffer,strnlen_s(buffer,sizeof(buffer)-1)));if(item.name.empty())item.name=item.code;
        if(cache.size()<16384)cache[key]={signature,item};
    }
    // This is a read-only view of owned items, not a scan of world drops.
    // The native getter is the same verified stat accessor used by the HUD.
    const auto quantity=STATLIST_GetUnitStatSigned?STATLIST_GetUnitStatSigned(pointer,STAT_QUANTITY,0):0;
    item.quantity=quantity>0?quantity:1;
    item.stackableKnown=STATLIST_GetUnitStatSigned!=nullptr;item.stackable=quantity>0;
    // Marketplace history records occupied sockets, not total capacity.
    // Read that same count so empty sockets cannot masquerade as filled ones.
    item.sockets=0;
    if(unit.pInventory){
        D2InventoryStrc sockets{};
        if(!Read(unit.pInventory,sockets))item.sockets=-1;
        else {auto child=sockets.pFirstItem;std::set<D2UnitStrc*> seen;
            while(child){D2UnitStrc socket{};D2ItemDataStrc socketData{};
                if(seen.size()>=255||!seen.insert(child).second||!Read(child,socket)||socket.dwUnitType!=4||!Read(socket.pItemData,socketData)||socketData.pExtraData.pParentInv!=unit.pInventory){item.sockets=-1;break;}
                ++item.sockets;child=socketData.pExtraData.pNextItem;
            }
        }
    }
    item.location=unit.dwAnimMode==IMODE_EQUIP?"Equipped":unit.dwAnimMode==IMODE_INBELT?"Belt":unit.dwAnimMode==IMODE_ONCURSOR?"Cursor":data.nPage==INVPAGE_STASH?"Personal stash":data.nPage==INVPAGE_CUBE?"Cube":"Inventory";return true;
}
bool CaptureContainer(D2UnitStrc* owner,const std::string& scope,Storage& storage){
    D2UnitStrc unit{};D2InventoryStrc inventory{};
    if(!Read(owner,unit)||unit.dwUnitType!=0||!Read(unit.pInventory,inventory)||inventory.pOwner!=owner)return false;
    std::vector<Item> items;std::set<D2UnitStrc*> seen;
    auto pointer=inventory.pFirstItem;D2UnitStrc* tail=nullptr;
    while(pointer){
        if(seen.size()>=4096||!seen.insert(pointer).second)return false;
        Item item;D2ItemDataStrc data{};if(!CaptureItem(pointer,item,data)||data.pExtraData.pParentInv!=unit.pInventory)return false;
        items.push_back(std::move(item));tail=pointer;pointer=data.pExtraData.pNextItem;
    }
    // Cursor items remain possessions while dragged between inventory/stash.
    const auto cursor=Value(reinterpret_cast<D2UnitStrc**>(reinterpret_cast<uintptr_t>(unit.pInventory)+0x40));
    if(cursor&&!seen.contains(cursor)){Item item;D2ItemDataStrc data{};if(!CaptureItem(cursor,item,data))return false;items.push_back(std::move(item));}
    D2InventoryStrc check{};if(tail!=inventory.pLastItem||!Read(unit.pInventory,check)||check.pFirstItem!=inventory.pFirstItem||check.pLastItem!=inventory.pLastItem)return false;
    storage[scope]=std::move(items);return true;
}
void CaptureStorage(D2UnitStrc* player,Frame& frame){
    D2UnitStrc unit{};D2InventoryStrc inventory{};
    if(!Read(player,unit)||!Read(unit.pInventory,inventory))return;
    // The player's inventory includes equipment, belt, cube and personal stash.
    CaptureContainer(player,"Character",frame.storage);
    if(inventory.nSharedInvCount>127||!stashReady.load()||GetTickCount64()-stashPaint.load()>1000)return;const auto page=stashPage.load();
    std::set<D2SharedInventoryStrc*> seen;
    std::map<uint32_t,std::string> containers;
    auto node=inventory.pFirstSharedInv;
    for(uint32_t n=0;node&&n<inventory.nSharedInvCount;++n){
        D2SharedInventoryStrc entry{};if(!seen.insert(node).second||!Read(node,entry)||!entry.dwUnitId)break;
        if(!containers.emplace(entry.dwUnitId,"Shared page "+std::to_string(page+1)+" / tab "+std::to_string(n+1)).second)return;
        node=entry.pNextCorpse;
    }
    if(node||containers.size()!=inventory.nSharedInvCount)return;
    // Shared items are keyed by their shared player-container owner in the
    // client item hash (also used by BankPanel). They need not have their own
    // client-side inventory linked list. Only scan this view around stash paint.
    Storage shared;for(const auto& [owner,scope]:containers)shared[scope]={};
    std::set<D2UnitStrc*> visited;
    for(unsigned bucket=0;bucket<128;++bucket){
        auto pointer=Value(reinterpret_cast<D2UnitStrc**>(Pattern::Address(0x1d442e0+4*0x400+bucket*8)));
        while(pointer){D2UnitStrc value{};if(visited.size()>=20000||!visited.insert(pointer).second||!Read(pointer,value))return;
            if(value.dwUnitType==4&&value.dwAnimMode==IMODE_STORED&&value.pItemData){D2ItemDataStrc data{};if(!Read(value.pItemData,data))return;
                if(const auto at=containers.find(data.dwOwnerGUID);at!=containers.end()){Item item;if(!CaptureItem(pointer,item,data))return;item.location=at->second;shared[at->second].push_back(std::move(item));}
            }pointer=value.pListNext;
        }
    }
    for(auto& [scope,items]:shared)frame.storage.emplace(scope,std::move(items));
    if(!stashReady.load()||stashPage.load()!=page){std::erase_if(frame.storage,[](const auto& entry){return entry.first!="Character";});}
}
}
void InventoryChanged(){if(Enabled())inventoryDirty.store(true,std::memory_order_relaxed);}
void SetNameProvider(void(*name)(D2UnitStrc*,char*)){originalName.store(name);}
void SetStashPage(uint32_t page,bool ready){stashReady.store(false);stashPage.store(page);stashReady.store(ready);}
void StashVisible(){if(Enabled())stashPaint.store(GetTickCount64());}
void SetReadGuard(void(*guard)(void(*)(void*),void*)){readGuard.store(guard);}
Frame SampleProtected(const HudFrame::Context& context,uint64_t epoch,const std::string& mod){
    Frame f;f.epoch=epoch;f.now=GetTickCount64();f.utc=std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();f.mod=mod;f.loading=context.loading;
    auto& state=Native();if(state.epoch!=epoch){state.epoch=epoch;state.captured=0;state.names.clear();inventoryDirty.store(true);}
    if(!epoch||context.loading)return f;
    const auto selector=Value(reinterpret_cast<uint32_t*>(Pattern::Address(0x1d442d8)));if(selector>=8){f.loading=true;return f;}
    const auto local=reinterpret_cast<D2UnitStrc*(__fastcall*)(uint32_t)>(Pattern::Address(0x6b050))(selector);
    D2UnitStrc unit{};if(!Read(local,unit)||unit.dwUnitType!=0){f.loading=true;return f;}
    D2PlayerDataStrc data{};if(Read(unit.pPlayerData,data))f.character.assign(data.szName,strnlen_s(data.szName,16));
    f.area=Area(local);f.town=Town(f.area);f.dead=unit.dwAnimMode==0||unit.dwAnimMode==17;
    if(!f.area||f.character.empty()){f.loading=true;return f;}
    if(STATLIST_GetUnitStatSigned)f.level=STATLIST_GetUnitStatSigned(local,STAT_LEVEL,0);
    const auto path=Value(&local->pDynamicPath);const auto room=path?Value(&path->pRoom):nullptr;const auto drlg=room?Value(&room->pDrlgRoom):nullptr;const auto level=drlg?Value(&drlg->pLevel):nullptr;const auto root=level?Value(&level->pDrlg):nullptr;
    if(root)f.difficulty=Value(&root->nDifficulty);
    if(f.now-state.captured>=1000||(inventoryDirty.load(std::memory_order_relaxed)&&f.now-state.captured>=200)){
        inventoryDirty.store(false,std::memory_order_relaxed);state.captured=f.now;CaptureStorage(local,f);
    }
    return f;
}
Frame Sample(const HudFrame::Context& context,uint64_t epoch,const std::string& mod){
    struct Request{const HudFrame::Context& context;uint64_t epoch;const std::string& mod;Frame result;};
    Request request{context,epoch,mod,{}};
    const auto read=[](void* value){auto& r=*static_cast<Request*>(value);r.result=SampleProtected(r.context,r.epoch,r.mod);};
    if(const auto guard=readGuard.load())guard(read,&request);
    else {request.result.epoch=epoch;request.result.mod=mod;request.result.now=GetTickCount64();request.result.utc=std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();request.result.loading=true;}
    return std::move(request.result);
}
}
