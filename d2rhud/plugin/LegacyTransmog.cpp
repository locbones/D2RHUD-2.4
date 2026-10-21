#include "LegacyTransmog.h"
#include "LegacyTransmogData.h"
#include "../Pattern.h"
#include "../d2/Reclass.h"
#include <MinHook.h>
#include <algorithm>
#include <atomic>
#include <string>
#include <cstdio>
#include <mutex>
#include <vector>
#include "StackSplitData.h"

namespace LegacyTransmog {
namespace {
using namespace Data;
using Unit = D2UnitStrc;
using Game = D2GameStrc;
template<class T> T Native(uint32_t rva) { return reinterpret_cast<T>(Pattern::Address(rva)); }
template<class T> bool Read(uintptr_t address, T& value) {
    SIZE_T count = 0;
    return address && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address), &value, sizeof(T), &count) && count == sizeof(T);
}
using CanUse = bool(__fastcall*)(Unit*, int);
using Use = void(__fastcall*)(Unit*, int);
using Packet = int(__fastcall*)(Game*, Unit*, const uint8_t*, int);
// The action footer is separate from the reversed item-description lines.
// Grow its native string before layout measures the footer and its background.
struct TooltipString { char* text; uint64_t length, capacity; };
using Tooltip = TooltipString*(__fastcall*)(void*, TooltipString*, Unit*);
CanUse originalCanUse = nullptr;
Use originalUse = nullptr;
Packet originalPacket = nullptr;
Tooltip originalTooltip = nullptr;
std::atomic<bool> ready{false};
// Local diagnostics let us distinguish an input gate, an unsent command and a
// rejected server transaction without changing a player's live inventory.
struct Diagnostics {
    std::atomic<uint32_t> clientChecks{0}, clientUses{0}, sentCommands{0}, serverCommands{0};
    std::atomic<int> result{-1};
} diagnostics;
const char* status = "Legacy item transmogrify is not installed.";
char compatibilityStatus[192]{};
constexpr std::array<uint32_t, 4> Hooks{0x1303a0, 0x1304a0, 0x2b7370, 0x1c0410};
bool SplitEligible(Unit* player,Unit* item,bool authoritative=false);

bool ItemDefinition(Unit* item, Definition& def, int* targetId = nullptr) {
    Unit copy{}; uintptr_t table = 0; uint64_t count = 0;
    if (!Read(reinterpret_cast<uintptr_t>(item), copy) || copy.dwUnitType != 4 ||
        !Read(Pattern::Address(0x1ca0390), table) || !Read(Pattern::Address(0x1ca0398), count) ||
        !table || count > 1000000 || copy.dwClassId >= count) return false;
    std::array<uint8_t, ItemRecordSize> record{};
    if (!Read(table + copy.dwClassId * ItemRecordSize, record)) return false;
    def = Decode(record);
    if (!targetId) return true;
    if (!def.Valid()) return false;
    // Compare all four compiled bytes, including the fourth byte of mod codes.
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t code = 0;
        if (!Read(table + i * ItemRecordSize + 0x80, code)) return false;
        if (code == def.target) { *targetId = static_cast<int>(i); return true; }
    }
    return false;
}
Unit* LocalPlayer() {
    uint32_t selector = 8;
    if (!Read(Pattern::Address(0x1d442d8), selector) || selector >= 8) return nullptr;
    return Native<Unit*(__fastcall*)(uint32_t)>(0x6b050)(selector);
}
bool LocalServer(uint32_t playerId) {
    // This is the server client list, not the similarly named game-mode flag.
    uintptr_t client = 0;
    if (!Read(Pattern::Address(0x1d637f0), client)) return false;
    for (size_t n = 0; client && n < 64; ++n) {
        D2ClientStrc copy{};
        if (!Read(client, copy)) return false;
        if (copy.dwClientState == 4 && copy.pGame && copy.dwUnitGUID == playerId) return true;
        const auto next = reinterpret_cast<uintptr_t>(copy.pNext);
        if (next == client) break;
        client = next;
    }
    return false;
}
std::string TargetName(Unit* item) {
    Definition def; int targetId = -1; uintptr_t table = 0; uint16_t nameIndex = 0;
    if (!ItemDefinition(item, def, &targetId) || !Read(Pattern::Address(0x1ca0390), table) ||
        !Read(table + size_t(targetId) * ItemRecordSize + 0xfc, nameIndex)) return {};
    // This is the compiled name index, not the item code or a JSON string ID.
    const auto name = Native<const char*(__fastcall*)(uint16_t, const char*)>(0x413860)(nameIndex, nullptr);
    std::string result;
    for (size_t i = 0; i < 512; ++i) {
        char ch = 0;
        if (!Read(reinterpret_cast<uintptr_t>(name) + i, ch)) return {};
        if (!ch) return result;
        result += (ch == '\r' || ch == '\n') ? ' ' : ch;
    }
    return {}; // Do not append an invalid or unterminated localization entry.
}
void AppendLegend(TooltipString* text, Unit* item) {
    if (!ready.load() || !text || !text->text) return;
    const auto player = LocalPlayer();
    if (!player || !LocalServer(player->dwUnitId)) return;
    const auto name = TargetName(item);
    const bool split=name.empty() && SplitEligible(player,item);
    if (name.empty() && !split) return;
    // Both native text blocks render their lines bottom-to-top. Prepend the
    // new action to put it below the existing controls on screen. Use the same
    // explicit control grey before and after the independently coloured name;
    // inherited footer grey differs from the inline palette's grey.
    std::string legend = split ? "\xc3\xbf" "c5Right-Click to separate stack" : "\xc3\xbf" "c5Right-Click to change to ";
    if(!split)legend += name;
    // Inline colours carry across logical lines even though their screen order
    // is reversed. Restore control grey before the existing action text.
    legend += "\xc3\xbf" "c5";
    if (text->length) { legend += '\n'; legend.append(text->text, size_t(text->length)); }
    text->length = 0;
    text->text[0] = 0;
    Native<TooltipString*(__fastcall*)(TooltipString*, const char*, size_t)>(0x5c7e0)(text, legend.data(), legend.size());
}
TooltipString* __fastcall TooltipHook(void* widget, TooltipString* text, Unit* item) {
    auto result = originalTooltip(widget, text, item);
    // Use the native control footer, separate from the item description.
    AppendLegend(result, item);
    return result;
}
bool PersonalItem(Unit* player, Unit* item, bool authoritative = false) {
    Unit p{}, i{}; D2ItemDataStrc data{}; D2InventoryStrc inv{}; uintptr_t cursor = 0;
    return Read(reinterpret_cast<uintptr_t>(player), p) && p.dwUnitType == 0 && p.dwAnimMode != 0 && p.dwAnimMode != 17 &&
        !(p.dwFlags & 0x10000) && p.pInventory && Read(reinterpret_cast<uintptr_t>(p.pInventory), inv) &&
        inv.dwSignature == 0x01020304 && inv.pOwner == player &&
        Read(reinterpret_cast<uintptr_t>(p.pInventory) + 0x40, cursor) && !cursor &&
        Read(reinterpret_cast<uintptr_t>(item), i) && i.dwUnitType == 4 && i.dwItemMode == 0 &&
        Read(reinterpret_cast<uintptr_t>(i.pItemData), data) && data.nPage == 0 &&
        data.pExtraData.pParentInv == p.pInventory &&
        // Client inventory copies retain IFLAG_NEWITEM (0x1) after receipt.
        // These transaction/pending flags are only meaningful on the server;
        // native server inventory preflight checks them again before mutation.
        (!authoritative || (!data.dwCommandFlags && !(data.dwItemFlags & 0x2e9) && !(i.dwFlagsEx & 0x14)));
}
void StartSplit(Unit* player,Unit* item);
bool __fastcall CanUseHook(Unit* item, int mask) {
    Definition def;
    if (!ready.load() || !ItemDefinition(item, def)) return originalCanUse(item, mask);
    if(!def.enabled){
        auto player=LocalPlayer();
        if(player && LocalServer(player->dwUnitId) && SplitEligible(player,item))return (mask&1)!=0;
        return originalCanUse(item,mask);
    }
    ++diagnostics.clientChecks;
    auto player = LocalPlayer();
    return (mask & 1) && def.Valid() && player && LocalServer(player->dwUnitId) && PersonalItem(player, item);
}
void __fastcall UseHook(Unit* item, int mask) {
    Definition def;
    if (!ready.load() || !ItemDefinition(item, def)) { originalUse(item, mask); return; }
    if(!def.enabled){
        auto player=LocalPlayer();
        if(player && LocalServer(player->dwUnitId) && SplitEligible(player,item)){
            if(mask&1)StartSplit(player,item);
            return;
        }
        originalUse(item,mask);return;
    }
    ++diagnostics.clientUses;
    auto player = LocalPlayer();
    if (!(mask & 1) || !def.Valid() || !player || !LocalServer(player->dwUnitId) || !PersonalItem(player, item)) return;
    using SendUse = void(__fastcall*)(bool, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
        uint32_t, uint32_t, uint32_t, void*, void*, void*);
    // The regular 36-byte 0x26 use command. Its server handler owns all changes;
    // never create/delete items on the render/input thread.
    ++diagnostics.sentCommands;
    Native<SendUse>(0x10eae0)(false, item->dwUnitId, 0, 0, 0, player->dwUnitId, 0, 255, 0, nullptr, nullptr, nullptr);
}
using AddItem = bool(__fastcall*)(Unit*, const Location*, const uint32_t*, size_t, bool);
// Native arguments are updates, removals, update IDs, removal IDs and their
// respective counts. Updates validate/reinsert the same item; only removals
// detach it. A true return alone does not mean an item has been detached.
using RemoveItem = bool(__fastcall*)(Unit*, const Location*, const Location*, const uint32_t*, const uint32_t*, size_t, size_t, bool);
using NotifyItem = void(__fastcall*)(Game*, Unit*, Unit*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
struct Inventory {
    Game* game;
    Unit* player;
    Unit* oldItem;
    Unit* newItem = nullptr;
    Definition definition;
    int targetId;
    Location origin;
    bool Create() {
        Drop drop;
        drop.game = reinterpret_cast<uintptr_t>(game);
        drop.seed = reinterpret_cast<uintptr_t>(&oldItem->pSeed);
        drop.level = static_cast<int>(oldItem->pItemData->dwItemLevel);
        drop.format = static_cast<uint16_t>(game->dwGameVersion);
        drop.id = targetId; drop.x = origin.x; drop.y = origin.y;
        uint32_t unitSeed = 0;
        if (!Read(reinterpret_cast<uintptr_t>(oldItem) + 0x30, unitSeed)) return false;
        drop.unitSeed = unitSeed; drop.itemSeed = oldItem->pItemData->dwInitSeed;
        newItem = Native<Unit*(__fastcall*)(Game*, Drop*, int)>(0x2f58c0)(game, &drop, 0);
        if (!newItem || !newItem->pItemData) return false;
        auto low = static_cast<uint32_t>(player->pSeed.LowSeed());
        auto high = static_cast<uint32_t>(player->pSeed.HighSeed());
        const int quantity = Quantity(definition, low, high);
        player->pSeed.LowSeed() = static_cast<int32_t>(low); player->pSeed.HighSeed() = static_cast<int32_t>(high);
        Native<void(__fastcall*)(Unit*, int, int, uint16_t)>(0x1e5440)(newItem, 70, quantity, 0);
        // New normal items must be identified. Preserve native creation flags.
        newItem->pItemData->dwItemFlags |= 0x10;
        return true;
    }
    bool Remove(Location location) {
        const uint32_t id = oldItem->dwUnitId;
        if (!Native<RemoveItem>(0x2dba00)(player, nullptr, &location, nullptr, &id, 0, 1, false)) return false;
        return oldItem->pItemData->pExtraData.pParentInv == nullptr;
    }
    bool Add(Unit* item, Location location) {
        const uint32_t id = item->dwUnitId;
        return Native<AddItem>(0x2d5e10)(player, &location, &id, 1, false);
    }
    bool AddNew(Location location) { return Add(newItem, location); }
    bool RestoreOld(Location location) { return Add(oldItem, location); }
    bool DropNew() {
        auto path = player->pDynamicPath;
        if (!path) return false;
        auto room = Native<D2ActiveRoomStrc*(__fastcall*)(Unit*)>(0x209530)(player);
        if (!room) return false;
        D2CoordStrc from{path->wPosX, path->wPosY}, to{};
        using Space = D2ActiveRoomStrc*(__fastcall*)(D2ActiveRoomStrc*, D2CoordStrc*, D2CoordStrc*, int, int, int, int, int, int);
        room = Native<Space>(0x231510)(room, &from, &to, 1, 0x3e01, 0x801, 1, 50, 1);
        if (!room) return false;
        using Ground = void(__fastcall*)(Game*, Unit*, Unit*, D2ActiveRoomStrc*, int, int);
        Native<Ground>(0x2fa090)(game, player, newItem, room, to.X, to.Y);
        return newItem->dwItemMode == 3 && Native<D2ActiveRoomStrc*(__fastcall*)(Unit*)>(0x209530)(newItem);
    }
    void Destroy(Unit* item) { Native<void(__fastcall*)(Game*, Unit*)>(0x2f70a0)(game, item); }
    void DestroyNew() { Destroy(newItem); }
    void Notify(Unit* item, Location location, uint32_t flags, uint32_t command) {
        Native<NotifyItem>(0x316b80)(game, player, item, flags, command, location.mode, location.page,
            location.body, uint32_t(location.x) | (uint32_t(location.y) << 16));
    }
    void DestroyOld() {
        // Inventory transactions mutate the authoritative inventory only. The
        // packet handler normally sends these notifications after committing.
        // Remove the old icon before publishing the replacement in its slot.
        Notify(oldItem, origin, 0x20, 4);
        if (newItem->dwItemMode == 0) {
            Location destination;
            Native<Location*(__fastcall*)(Location*, Unit*)>(0x213a20)(&destination, newItem);
            Notify(newItem, destination, 1, 2);
        }
        // Ground placement already publishes the new world item natively.
        Destroy(oldItem);
    }
};
#include "StackSplit.inl"
#include "SocketStack.inl"
int __fastcall PacketHook(Game* game, Unit* player, const uint8_t* packet, int size) {
    if (!ready.load() || !game || !player || !packet || size != 36 || packet[0] != 0x26)
        return originalPacket(game, player, packet, size);
    if(StackData::Recognized({packet,size_t(size)}))return SplitPacket(game,player,{packet,size_t(size)});
    uint32_t id = 0, owner = 0;
    memcpy(&id, packet + 10, 4); memcpy(&owner, packet + 2, 4);
    auto item = Native<Unit*(__fastcall*)(Game*, int, uint32_t)>(0x2c49c0)(game, 4, id);
    Definition def;
    if (!item || !ItemDefinition(item, def) || !def.enabled) return originalPacket(game, player, packet, size);
    ++diagnostics.serverCommands;
    diagnostics.result = -2;
    // Recognized transmog items never fall through to their old potion/scroll
    // spell when a definition or packet is invalid. This also rejects replays.
    int targetId = -1;
    if (owner != player->dwUnitId || player->dwUnitType != 0 || packet[1] || packet[14] || packet[15] ||
        packet[16] || packet[17] || !PersonalItem(player, item, true) || !ItemDefinition(item, def, &targetId)) return 0;
    Location location;
    Native<Location*(__fastcall*)(Location*, Unit*)>(0x213a20)(&location, item);
    Inventory inventory{game, player, item, nullptr, def, targetId, location};
    const auto result = Replace(inventory, location);
    diagnostics.result = static_cast<int>(result);
    if (result == Result::RecoveryCursor) OutputDebugStringA("D2RHUD: transmog placement required recovery; original item retained.");
    return 0;
}
bool Supported(uint32_t* failed = nullptr) {
    std::array<uint8_t, 0x1300> code{};
    for (const auto& signature : Signatures) {
        SIZE_T count = 0;
        if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(Pattern::Address(signature.rva)),
            code.data(), signature.size, &count) || count != signature.size || Hash({code.data(), signature.size}) != signature.hash) {
            if (failed) *failed = signature.rva;
            return false;
        }
    }
    return true;
}
}
SplitView GetSplitView(){
    std::lock_guard lock(splitMutex);
    uint8_t playing=0;
    if(splitView.phase!=SplitView::Closed &&
       (!ready.load() || !Read(Pattern::Address(0x1dc76f8),playing) || playing!=1 || GetTickCount64()-splitView.opened>60000)){
        splitView={};splitPending={};
    }
    return splitView;
}
void SetSplitInputRegion(uint32_t token,RECT bounds,int width,int height,bool editing){
    std::lock_guard lock(splitMutex);
    if(splitView.phase==SplitView::Closed || splitView.token!=token || width<=0 || height<=0)return;
    RECT client{};
    if(splitWindow && GetClientRect(splitWindow,&client) && client.right>0 && client.bottom>0){
        bounds={MulDiv(bounds.left,client.right,width),MulDiv(bounds.top,client.bottom,height),MulDiv(bounds.right,client.right,width),MulDiv(bounds.bottom,client.bottom,height)};
    }
    splitInputBounds=bounds;splitEditing=editing;
}
bool SplitHit(POINT point){return PtInRect(&splitInputBounds,point)!=FALSE;}
bool SplitCapturesInput(UINT message,LPARAM data){
    std::lock_guard lock(splitMutex);
    if(splitView.phase==SplitView::Closed)return false;
    if(message>=WM_KEYFIRST && message<=WM_KEYLAST)return splitEditing;
    POINT point{short(LOWORD(data)),short(HIWORD(data))};
    if(message==WM_INPUT){
        return splitEditing || splitMouseHeld || (splitWindow && GetCursorPos(&point) && ScreenToClient(splitWindow,&point) && SplitHit(point));
    }
    if(message<WM_MOUSEFIRST || message>WM_MOUSELAST)return false;
    if(message==WM_MOUSEWHEEL || message==WM_MOUSEHWHEEL)ScreenToClient(splitWindow,&point);
    const bool capture=splitMouseHeld || SplitHit(point);
    if(message==WM_LBUTTONDOWN && capture)splitMouseHeld=true;
    if(message==WM_LBUTTONUP)splitMouseHeld=false;
    return capture;
}
void CloseSplit(uint32_t token){std::lock_guard lock(splitMutex);if(splitView.token==token){splitView={};splitPending={};}}
void SubmitSplit(uint32_t token,int amount){
    std::lock_guard lock(splitMutex);
    if(splitView.phase!=SplitView::Choosing || token!=splitView.token || amount<1 || uint32_t(amount)>=splitView.total)return;
    if(!splitWindow){splitView.phase=SplitView::Error;splitView.message="Input window unavailable. The one-item split was kept.";return;}
    splitPending={splitView.owner,splitView.item,uint32_t(amount),splitView.total,token,StackData::Finish};
    splitView.phase=SplitView::Submitting;
    if(!PostMessageW(splitWindow,SplitSubmitMessage,0,0)){splitPending={};splitView.phase=SplitView::Error;splitView.message="Unable to submit. The one-item split was kept.";}
}
bool SplitWindowMessage(HWND window,UINT message,WPARAM key,LPARAM data,bool* outsideClick){
    StackData::Request pending;
    if(outsideClick)*outsideClick=false;
    {std::lock_guard lock(splitMutex);splitWindow=window;
        if(message==SplitSubmitMessage){pending=splitPending;splitPending={};}
        else if(message==WM_KEYDOWN && key==VK_ESCAPE && splitView.phase!=SplitView::Closed){splitView={};splitPending={};return true;}
        else if(splitView.phase!=SplitView::Closed &&
                (message==WM_LBUTTONDOWN || message==WM_RBUTTONDOWN || message==WM_MBUTTONDOWN || message==WM_LBUTTONDBLCLK || message==WM_RBUTTONDBLCLK) &&
                splitInputBounds.right>splitInputBounds.left && !SplitHit({short(LOWORD(data)),short(HIWORD(data))})){
            // Dismiss without eating the click; the initial single-item split stays.
            splitView={};splitPending={};splitEditing=splitMouseHeld=false;if(outsideClick)*outsideClick=true;
        }
        else if(message==WM_KILLFOCUS || (message==WM_ACTIVATEAPP && !key)){splitView={};splitPending={};splitEditing=splitMouseHeld=false;}
    }
    if(message!=SplitSubmitMessage)return false;
    if(pending.token && ready.load()){
        const auto player=LocalPlayer();
        if(player && player->dwUnitId==pending.owner && LocalServer(pending.owner))SendSplit(pending);
        else CloseSplit(pending.token);
    }
    return true;
}
bool Install() {
    if (ready.load()) return true;
    uint32_t failed = 0;
    if (!Supported(&failed)) {
        std::snprintf(compatibilityStatus, sizeof(compatibilityStatus), "Legacy item transmogrify unavailable: signature mismatch at D2R.exe+0x%x.", failed);
        status = compatibilityStatus;
        return false;
    }
    const auto initialized = MH_Initialize();
    if (initialized != MH_OK && initialized != MH_ERROR_ALREADY_INITIALIZED) return false;
    void* replacements[]{reinterpret_cast<void*>(CanUseHook), reinterpret_cast<void*>(UseHook), reinterpret_cast<void*>(PacketHook), reinterpret_cast<void*>(TooltipHook)};
    void** originals[]{reinterpret_cast<void**>(&originalCanUse), reinterpret_cast<void**>(&originalUse), reinterpret_cast<void**>(&originalPacket), reinterpret_cast<void**>(&originalTooltip)};
    size_t created = 0;
    for (; created < Hooks.size(); ++created) {
        if (MH_CreateHook(reinterpret_cast<void*>(Pattern::Address(Hooks[created])), replacements[created], originals[created]) != MH_OK) break;
    }
    if (created == Hooks.size()) {
        bool queued = true;
        for (auto rva : Hooks) if (MH_QueueEnableHook(reinterpret_cast<void*>(Pattern::Address(rva))) != MH_OK) queued = false;
        if (queued && MH_ApplyQueued() == MH_OK) {
            ready.store(true);
            splitReady.store(SplitSupported());
            InstallSocketStacks();
            status = "Legacy item transmogrify: active for local games (Transmogrify / TMogType / TMogMin / TMogMax).";
            return true;
        }
    }
    for (size_t i = 0; i < created; ++i) MH_QueueDisableHook(reinterpret_cast<void*>(Pattern::Address(Hooks[i])));
    MH_ApplyQueued();
    for (size_t i = 0; i < created; ++i) { auto address = reinterpret_cast<void*>(Pattern::Address(Hooks[i])); MH_DisableHook(address); MH_RemoveHook(address); }
    status = "Legacy item transmogrify hooks could not be installed.";
    return false;
}
const char* Status() { return status; }
void SetQuantityRightClicks(bool enabled){
    quantityRightClicks.store(enabled);
    if(!enabled){std::lock_guard lock(splitMutex);splitView={};splitSession={};splitPending={};}
}
const char* StackSplitStatus(){
    if(!splitReady.load())return "Stack splitting unavailable: native saved-item signatures did not match or hooks are unavailable.";
    return quantityRightClicks.load() ? "Stack splitting: available for the local game host; right-click a non-transmog stack." : "Quantity Right-Clicks is disabled in D2RHUD Options.";
}
const char* SocketStackStatus(){return socketStackStatus.load();}
void Uninstall() {
    UninstallSocketStacks();
    if (!ready.exchange(false)) return;
    splitReady=false;
    {std::lock_guard lock(splitMutex);splitView={};splitSession={};}
    for (auto rva : Hooks) MH_QueueDisableHook(reinterpret_cast<void*>(Pattern::Address(rva)));
    if (MH_ApplyQueued() == MH_OK) for (auto rva : Hooks) MH_RemoveHook(reinterpret_cast<void*>(Pattern::Address(rva)));
}
}
