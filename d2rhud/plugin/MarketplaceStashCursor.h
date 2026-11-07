#pragma once
#include <Windows.h>
#include <cstddef>
#include "../d2/Reclass.h"

namespace Marketplace::StashCursor {
static_assert(offsetof(D2InventoryStrc,pCursorItem)==0x40);
enum class State { Unknown,Empty,Carried };
template<class T>inline bool Read(const void* source,T& value){
    SIZE_T count=0;return source&&ReadProcessMemory(GetCurrentProcess(),source,&value,sizeof(value),&count)&&count==sizeof(value);
}
// Match the native inventory getter (2.4 RVA 0x208330), including its live
// reference at +0x98. Never treat failed reads as an empty cursor.
inline State Inspect(const D2UnitStrc* player,uint32_t* cursor=nullptr){
    if(cursor)*cursor=0;
    D2UnitStrc unit{};D2InventoryStrc inventory{};
    if(!Read(player,unit)||unit.dwUnitType)return State::Unknown;
    auto source=unit.pInventory;
    if(unit.pInventoryControl){
        uint32_t references=0;
        if(!Read(static_cast<const uint8_t*>(unit.pInventoryControl)+8,references))return State::Unknown;
        if(references&&unit.pInventoryReference)source=unit.pInventoryReference;
    }
    if(!Read(source,inventory)||inventory.dwSignature!=0x01020304||inventory.pOwner!=player)return State::Unknown;
    if(!inventory.pCursorItem)return State::Empty;
    D2UnitStrc held{};D2ItemDataStrc data{};
    if(!Read(inventory.pCursorItem,held)||held.dwUnitType!=4||held.dwItemMode!=4||!held.dwUnitId||!Read(held.pItemData,data)||
        data.pExtraData.pParentInv!=source)return State::Unknown;
    // A completed native pickup belongs to the real player, not one of the
    // shared-owner inventories destroyed during page replacement. Keep this
    // same live item; never drop, serialize/recreate or change its identity.
    if(cursor)*cursor=held.dwUnitId;
    return State::Carried;
}
inline bool Busy(const D2UnitStrc* player){return Inspect(player)!=State::Empty;}
inline bool CanSwitch(const D2UnitStrc* player,bool carry){const auto state=Inspect(player);return state==State::Empty||(carry&&state==State::Carried);}
}
