#pragma region Includes

#include "D2RHUD.h"
#include <imgui.h>
#include "../../D2/D2Ptrs.h"
#include <sstream>
#include <windows.h>
#include <vector>
#include <queue>
#include <functional>
#include "../KeyMappings.h"
#include <fstream>
#include <string>
#include <detours/detours.h>
#include <iostream>
#include <tuple>
#include <iomanip>
#include <algorithm>
#include <cstring>
#include <thread>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include "../../D3D12Hook.h"
#include "../ItemFilter/ItemFilter.h"
#include <regex>
#include <ctime>
#include "../../D2/json.hpp"
#include <random>
#include <unordered_set>
#include <mutex>
#include <set>

#pragma endregion

#pragma region Credits

/*
- Chat Detours/Structures by Killshot
- Base Implementation by DannyisGreat
- Monster Stats and Plugin Mods by Bonesy
- Special Thanks to those who have helped ^^
*/

#pragma endregion

#pragma region Global Static/Structs

std::string lootFile = "../D2R/lootfilter.lua";
std::string Version = "1.6.1";

using json = nlohmann::json;
static MonsterStatsDisplaySettings cachedSettings;
static D2Client* GetClientPtr();
static D2DataTablesStrc* sgptDataTables = reinterpret_cast<D2DataTablesStrc*>(Pattern::Address(0x1c9e980));
ItemFilter* itemFilter = new ItemFilter();

const uint32_t modNameOffset = 0x1BF084F;
static std::string GetModName() {
    uint64_t pModNameAddr = Pattern::Address(modNameOffset);
    if (pModNameAddr == 0) {
        return "";
    }

    const char* pModName = reinterpret_cast<const char*>(pModNameAddr);
    if (!pModName) {
        return "";
    }

    return std::string(pModName);
}
std::string modName = GetModName();
std::string configFilePath = "HUDConfig_" + modName + ".json";
bool configLoaded = false;
bool grailLoaded = false;

const uint32_t sharedStashFlagOffset = 0x1BF0883;

static bool IsHardcore()
{
    uint64_t addr = Pattern::Address(sharedStashFlagOffset);
    if (!addr)
        return false;

    uint8_t value = *reinterpret_cast<uint8_t*>(addr);
    return (value & (1 << 2)) != 0;
}
static bool isHardcore = false;
std::atomic<uint32_t> g_GrailRevision{ 1 };

#pragma endregion

#pragma region *Currently Unused*

// UMod Offsets
constexpr uint32_t umod8a_Offsets[] = { 0x2FC7E4, 0xAF13F }; // Cold
constexpr uint32_t umod8b_Offsets[] = { 0x2FC8AD, 0xAF20B }; // Fire
constexpr uint32_t umod8c_Offsets[] = { 0x2FC967, 0xAF2CF }; // Light
constexpr uint32_t umod9_Offsets[] = { 0x2FC9EB, 0xAF35C }; // Fire
constexpr uint32_t umod18_Offsets[] = { 0x2FCA64, 0xAF3E9 }; // Cold
constexpr uint32_t umod17_Offsets[] = { 0x2FCADD, 0xAF479 }; // Lightning
constexpr uint32_t umod23_Offsets[] = { 0x2FCB56, 0xAF495 }; // Poison
constexpr uint32_t umod25_Offsets[] = { 0x2FCBCF, 0xAF522 }; // Magic
constexpr uint32_t umod27a_Offsets[] = { 0x2FCC56, 0xAF5B9 }; // Cold
constexpr uint32_t umod27b_Offsets[] = { 0x2FCD22, 0xAF698 }; // Fire
constexpr uint32_t umod27c_Offsets[] = { 0x2FCDDB, 0xAF75C }; // Light
constexpr uint32_t umod28_Offsets[] = { 0x2FCDEE, 0xAF778 }; // Physical

struct OriginalUMods {
    // Each element is a vector of original bytes for a single offsets-group.
    // Example: coldGroups[0] corresponds to umod8a_Offsets,
    //          coldGroups[1] -> umod18_Offsets, etc.
    std::vector<std::vector<uint8_t>> coldGroups;
    std::vector<std::vector<uint8_t>> fireGroups;
    std::vector<std::vector<uint8_t>> lightGroups;
    std::vector<std::vector<uint8_t>> poisonGroups;
    std::vector<std::vector<uint8_t>> damageGroups;
    std::vector<std::vector<uint8_t>> magicGroups;
};

OriginalUMods g_originalUModValues = {
    // coldGroups: each group displays original values for umodxx offsets
    {
        { 40, 40 },   // umod8a
        { 75, 75 },   // umod18
        { 20, 20 }    // umod27a
    },
    // fireGroups
    {
        { 40, 40 },   // umod8b
        { 75, 75 },   // umod9
        { 20, 20 }    // umod27b
    },
    // lightGroups
    {
        { 40, 40 },   // umod8c
        { 75, 75 },   // umod17
        { 20, 20 }    // umod27c
    },
    // poisonGroups
    {
        { 75, 75 }    // umod23
    },
    // damageGroups
    {
        { 50, 50 }    // umod28
    },
    // magicGroups
    {
        { 20, 20 }    // umod25
    }
};

void ApplyUModArray(const uint32_t* offsets, size_t count, uint32_t remainder, const std::vector<uint8_t>& groupOriginalValues, const std::string& statName)
{
    //LogSunder("ApplyUModArray count=" + std::to_string(count) + " remainder=" + std::to_string(remainder));

    for (size_t i = 0; i < count; ++i)
    {
        uint64_t addr = Pattern::Address(offsets[i]);
        if (!addr || addr < 0x10000)
            continue;

        uint8_t* pValue = reinterpret_cast<uint8_t*>(addr);
        uint8_t currentValue = *pValue;

        // Safely get original for this index (fallback to currentValue)
        uint8_t originalValue = (i < groupOriginalValues.size()) ? groupOriginalValues[i] : currentValue;

        // If memory doesn't match original, restore it first
        if (currentValue != originalValue)
        {
            //LogSunder(statName + " Restoring original UMod at 0x" + std::to_string(addr) + " index=" + std::to_string(i) + " current=" + std::to_string(currentValue) + " -> original=" + std::to_string(originalValue));

            DWORD oldProtect;
            if (VirtualProtect(pValue, 1, PAGE_EXECUTE_READWRITE, &oldProtect))
            {
                *pValue = originalValue;
                VirtualProtect(pValue, 1, oldProtect, &oldProtect);
                currentValue = originalValue;
            }
            else
            {
                MessageBoxA(nullptr, "Failed to change memory protection (restore)!", "Error", MB_OK | MB_ICONERROR);
                continue;
            }
        }

        // Compute expected value from ORIGINAL minus remainder
        int expectedValue = static_cast<int>(originalValue) - static_cast<int>(remainder);
        if (expectedValue < 0) expectedValue = 0;

        if (static_cast<uint8_t>(expectedValue) == currentValue)
        {
            //LogSunder(statName + " UMod already at expected value at 0x" + std::to_string(addr) + " index=" + std::to_string(i) + " value=" + std::to_string(currentValue));
            continue;
        }

        //LogSunder(statName + " Applying remainder to UModAddr[" + std::to_string(i) + "] @0x" + std::to_string(addr) + " original=" + std::to_string(originalValue) + " old=" + std::to_string(currentValue) + " new=" + std::to_string(expectedValue));

        DWORD oldProtect;
        if (VirtualProtect(pValue, 1, PAGE_EXECUTE_READWRITE, &oldProtect))
        {
            *pValue = static_cast<uint8_t>(expectedValue);
            VirtualProtect(pValue, 1, oldProtect, &oldProtect);
        }
        else
            MessageBoxA(nullptr, "Failed to change memory protection (apply)!", "Error", MB_OK | MB_ICONERROR);
    }
}

#pragma endregion

#pragma region Monster & Command Constants
const char* ResistanceNames[6] = { "   ", "   ", "   ", "   ", "   ", "   " };
constexpr  uint32_t ResistanceStats[6] = { 39, 41, 43, 45, 36, 37 };
constexpr  uint32_t Alignment = { 172 };
constexpr  uint32_t ImmunityCold = { 187 };
constexpr  uint32_t ImmunityFire = { 189 };
constexpr  uint32_t ImmunityLight = { 190 };
constexpr  uint32_t ImmunityPoison = { 191 };
constexpr  uint32_t ImmunityPhysical = { 192 };
constexpr  uint32_t ImmunityMagic = { 193 };
constexpr  ImU32 ResistanceColors[6] = { IM_COL32(170,50,50,255) ,IM_COL32(170,170,50,255) ,IM_COL32(50,90,170,255) ,IM_COL32(50,170,50,255),IM_COL32(255,255,255,255), IM_COL32(255,175,0,255) };
constexpr  const char* Seperator = "  ";
constexpr uint32_t Experience = { 21 };
std::string automaticCommand1;
std::string automaticCommand2;
std::string automaticCommand3;
std::string automaticCommand4;
std::string automaticCommand5;
std::string automaticCommand6;
std::time_t lastLogTimestamp = 0;
#pragma endregion

#pragma region Chat/Debug Structures
struct DebugCheatEntry
{ // cheats array at 1418026F0
    char name[32];
    bool(__fastcall* handler)(uint64_t pGame, uint64_t pPlayer, const char* arg);
    uint32_t alwaysEnabled;
    uint32_t hasArguments;
};

struct CMD_PACKET_BASE
{
    uint8_t opcode;
};

struct SCMD_CHATSTART_PACKET : CMD_PACKET_BASE
{
    uint8_t msgType;
    uint8_t langCode;
    uint8_t unitType;
    uint32_t id;
    uint8_t color;
    uint8_t subType;
    char sender[61];
    char message[256];
};

struct CCMD_DEBUGCHEAT_PACKET : CMD_PACKET_BASE
{
    uint8_t unk1;
    uint8_t unk2;
    char cheat[256];
};

const uint32_t mainMenuClickHandlerOffset = 0xBC88C0;
const uint32_t gameClientsOffset = 0x1D637F0;
const uint32_t sendPacketToServerOffset = 0x010EBF0;
const uint32_t executeDebugCheatOffset = 0x076BC0;
const uint32_t unitDataOffset = 0x1D442E0;
const uint32_t getUnitPtrFuncOffset = 0x066510;
const uint32_t gameChatMsgTypeOffset = 0x184ABE8;
const uint32_t CCMD_DEBUGCHEAT_HandlerOffset = 0x02AD6F0;
const uint32_t Process_SCMD_CHATSTARTOffset = 0x0107CA0;
const uint32_t GetChatManagerOffset = 0x056E440;
const uint32_t ChatManager_PushChatEntryOffset = 0x056E9A0;
const uint32_t GetUnitNameOffset = 0x027A920;
const uint32_t BroadcastChatMessageOffset = 0x02ADA90;
const uint32_t Send_SCMD_CHATSTARTOffset = 0x02CD280;
const uint32_t cheatsArrayOffset = 0x18026F0;
const uint8_t CCMD_DEBUGCHEAT = 0x15;
const uint8_t SCMD_CHATSTART = 0x26;
const uint64_t SaveAndExitButtonHash = 0x621D53C05FCA5A67;
const uint32_t detectGameStatusOffset = 0x1DC76F8; //unknown, just convenient

static D2Client* GetClientPtr()
{
    uint64_t pClients = Pattern::Address(gameClientsOffset);

    if (pClients != NULL)
    {
        D2Client* pGameClient = *(D2Client**)(pClients);
        return pGameClient;
    }

    return nullptr;
}

struct Widget
{
    uint64_t vtable;
    uint64_t hash;
};

struct QueuedAction
{
    const char* name;
    std::function<void()> action;
};

typedef bool(__fastcall* DebugCheatHandler)(uint64_t pGame, uint64_t pPlayer, const char* szArg);
typedef const char* (__fastcall* GetUnitNameFptr)(uint64_t pUnit, char* szName);
typedef bool(__fastcall* CCMD_HANDLER_Fptr)(uint64_t pGame, uint64_t pPlayer, CMD_PACKET_BASE* pPacket, uint32_t dwPacketLen);
typedef bool(__fastcall* Process_SCMD_CHATSTART_Fptr)(SCMD_CHATSTART_PACKET* pPacket);
typedef void(__fastcall* BroadcastChatMessageFptr)(uint64_t pGame, const char* szMsg, uint8_t color);
typedef void(__fastcall* GameMenuOnClickHandler)(uint64_t a1, Widget* pWidget);
typedef D2UnitStrc* (__fastcall* GetClientUnitByIdAndTypeNew)(uint64_t pTable, uint32_t id1, uint32_t id2, uint32_t dwType);
typedef void(__fastcall* SendPacketToServer)(void* pPacket);
typedef void(__fastcall* ExecuteDebugCheat)(const char* szCheat);
bool menuClickHookInstalled = false;
static GameMenuOnClickHandler mainMenuClickHandlerOrig = nullptr;
static CCMD_HANDLER_Fptr CCMD_DEBUGCHEAT_Handler_Orig = nullptr;
static Process_SCMD_CHATSTART_Fptr Process_SCMD_CHATSTART_Orig = nullptr;
static SendPacketToServer SendPacketFunc = reinterpret_cast<SendPacketToServer>(Pattern::Address(sendPacketToServerOffset));
static ExecuteDebugCheat ExecuteDebugCheatFunc = reinterpret_cast<ExecuteDebugCheat>(Pattern::Address(executeDebugCheatOffset));
static GetClientUnitByIdAndTypeNew GetClientUnitPtrFunc = reinterpret_cast<GetClientUnitByIdAndTypeNew>(Pattern::Address(getUnitPtrFuncOffset));
struct D2ChatManager;
static std::queue<QueuedAction> queuedActions;


struct handle_data
{
    DWORD process_id;
    HWND window_handle;
};

struct ChatMsg
{
    uint32_t type;
    blz_string str;
};

struct ChatOptionalStruct
{
    uint64_t value;
    bool hasValue;
};

typedef D2ChatManager* (__fastcall* GetChatManagerFptr)();
typedef D2ChatManager* (__fastcall* ChatManager_PushChatEntryFptr)(D2ChatManager* pMgr, blz_string& msg, uint32_t color, bool a4, ChatMsg& typeMsg, ChatOptionalStruct& a6, ChatOptionalStruct& a7, ChatOptionalStruct& a8, ChatOptionalStruct& a9);

static GetChatManagerFptr GetChatManager = reinterpret_cast<GetChatManagerFptr>(Pattern::Address(GetChatManagerOffset));
static ChatManager_PushChatEntryFptr ChatManager_PushChatEntry = reinterpret_cast<ChatManager_PushChatEntryFptr>(Pattern::Address(ChatManager_PushChatEntryOffset));

typedef void(__fastcall* D2GAME_UModInit_t)(D2UnitStrc* pUnit, int32_t nUMod, int32_t bUnique);
D2GAME_UModInit_t oD2GAME_UModInit = nullptr;

typedef void(__fastcall* D2GAME_SpawnChampUnique_t)(D2GameStrc* pGame, D2ActiveRoomStrc* pRoom, void* pRoomCoordList, D2UnitStrc* pUnit, int32_t bSpawnMinions, int32_t nMinGroup, int32_t nMaxGroup );
D2GAME_SpawnChampUnique_t oD2GAME_SpawnChampUnique_1402fddd0 = nullptr;

typedef void(__fastcall* D2GAME_SpawnMonsters_t)(D2GameStrc* pGame, D2ActiveRoomStrc* pRoom, void* pRoomCoordList, int32_t nX, int32_t nY, int32_t nUnitGUID, int32_t nClassId, void* a8);
D2GAME_SpawnMonsters_t oD2GAME_SpawnMonsters_140301b5f = nullptr;

typedef void(__fastcall* D2GAME_UMOD8Array_t)(D2UnitStrc* pUnit, int32_t nUMod, int32_t bUnique);
D2GAME_UMOD8Array_t oD2GAME_UMOD8Array_1402fc530 = nullptr;

typedef void* (__fastcall* DrawMonsterHPBar_t)(int param1, void* param2, void* param3);
static DrawMonsterHPBar_t DrawMonsterHPBar = reinterpret_cast<DrawMonsterHPBar_t>(Pattern::Address(0x830B0));

struct RemainderEntry
{
    int cold = 0;
    int fire = 0;
    int light = 0;
    int poison = 0;
    int damage = 0;
    int magic = 0;
};
static std::unordered_map<DWORD, RemainderEntry> g_resistRemainders;

#pragma endregion

#pragma region Bank Tabs
struct Message {
    uint64_t o1;
    uint64_t o2;
    uint64_t o3;
    uint64_t o4;
    uint64_t o5;
};

struct D2SaveSystemContainer {
    int64_t unk_0000;
    uint8_t* pData;
    uint64_t nSize;
    uint64_t nAllocated;
    small_string_opt<0x1F> tFileName;
    uint32_t unk_0058;
};
static_assert(offsetof(D2SaveSystemContainer, unk_0058) == 0x58);

#pragma pack(1)

// Both of these seem to be unused. Can use for our own uses.
struct CCMD_CUSTOM {
    uint8_t opcode;
    uint64_t magic;
    uint8_t opcode2;
};

struct SCMD_CUSTOM {
    uint8_t opcode;
    uint64_t magic;
    uint8_t opcode2;
};

// We might not use all these. Just documenting the steps mentally...
enum SharedStashPhase : uint8_t {
    PageChanged = 0,
    SaveStarted,
    SaveCompleted,
    OldSharedStashFreed,
    NewSharedStashLoadedAndAttachMessagesSent,
    NewSharedStashUnitsCreated
};

struct CCMD_STASH_PAGE_CHANGE : CCMD_CUSTOM {
    SharedStashPhase nPhase;
    uint32_t nPage;
};

struct SCMD_STASH_PAGE_CHANGE : SCMD_CUSTOM {
    SharedStashPhase nPhase;
    uint32_t nPage;
};
#pragma pack()

constexpr uint32_t bankPanelDraw = 0x18eb90;
constexpr uint32_t bankPanelMessage = 0x18ee50;
constexpr uint32_t saveSystemLoadFile = 0x6cba50;
constexpr uint32_t ccmdProcessClientSystemMessage = 0x2e1b60;
constexpr uint32_t nNumberOfTabs = 7;

// Easier to take over an unused opcode than to add to the end.
constexpr uint32_t CCMD_CUSTOM_OP_CODE = 0x3;  //CCMD_TRANSMUTE. variable length and can be large. we bake magic into it to let us know it is us.
constexpr uint32_t SCMD_CUSTOM_OP_CODE = 0x2B;  //SCMD_CORRECT_PATH. It's easier to hijack this packet as opposed to adding a new one.
constexpr uint32_t CCMD_SHARED_STASH_OP2 = 0x0;
constexpr uint32_t SCMD_SHARED_STASH_OP2 = 0x0;

struct D2BankPanelWidget;

typedef void(__fastcall* BankPanelDraw_t)(D2BankPanelWidget* pBankPanel);
static BankPanelDraw_t oBankPanelDraw = nullptr;

typedef D2SaveSystemContainer* (__fastcall* SaveSystemLoadFile_t)(int64_t* pContainer, const char* szFilename, char bFlag);
static SaveSystemLoadFile_t oSaveSystemLoadFile = nullptr;

typedef int64_t* (__fastcall* CCMD_ProcessClientGameMessage_t)(D2GameStrc* pGame, D2ClientStrc* pClient, uint8_t* pPacket, uint64_t nSize);
static CCMD_ProcessClientGameMessage_t oCCMD_ProcessClientGameMessage = nullptr; // D2GAME_PACKET_Handler_6FC89320 in D2MOO

typedef char(__fastcall* CCMD_ProcessClientSystemMessage_t)(uint8_t* pData, int64_t nSize);
static CCMD_ProcessClientSystemMessage_t oCCMD_ProcessClientSystemMessage = nullptr;

typedef void(__fastcall* SCMD_QueuePacket_t)(int64_t* pCMDManager, int32_t nClient, void** pPacketRange);
static SCMD_QueuePacket_t SCMD_QueuePacket = reinterpret_cast<SCMD_QueuePacket_t>(Pattern::Address(0x422ac0));

typedef void(__fastcall* CCMD_QueuePacket_t)(void* pPacket, int32_t nSize);
static CCMD_QueuePacket_t CCMD_QueuePacket = reinterpret_cast<CCMD_QueuePacket_t>(Pattern::Address(0x10dce0));

typedef void* (__fastcall* BankPanelOnMessage_t)(void* pWidget, Message& message);
static BankPanelOnMessage_t BankPanelMessage = reinterpret_cast<BankPanelOnMessage_t>(Pattern::Address(0x18ee50));

typedef void* (__fastcall* WidgetFindChild_t)(void* pWidget, const char* childName);
static WidgetFindChild_t WidgetFindChild = reinterpret_cast<WidgetFindChild_t>(Pattern::Address(0x576070));

typedef int64_t(__fastcall* D2GAME_PACKETCALLBACK_Rcv0x03_CCMD_RUNXY_t)(D2GameStrc* pGame, D2UnitStrc* pUnit, void* pPacket, int nPacketSize);
static D2GAME_PACKETCALLBACK_Rcv0x03_CCMD_RUNXY_t D2GAME_PACKETCALLBACK_Rcv0x20_CCMD_TRANSMUTE =
reinterpret_cast<D2GAME_PACKETCALLBACK_Rcv0x03_CCMD_RUNXY_t>(Pattern::Address(0x2ABE30));

typedef void(__fastcall* D2CLIENT_PACKETCALLBACK_Rcv0x2B_SCMD_CORRECT_PATH_t)(uint8_t* pPacket);
static D2CLIENT_PACKETCALLBACK_Rcv0x2B_SCMD_CORRECT_PATH_t D2CLIENT_PACKETCALLBACK_Rcv0x2B_SCMD_CORRECT_PATH =
reinterpret_cast<D2CLIENT_PACKETCALLBACK_Rcv0x2B_SCMD_CORRECT_PATH_t>(Pattern::Address(0xDA3B0));

static char* gpSharedStashString = reinterpret_cast<char*>(Pattern::Address(0x1577390));
static char* gpSharedStashHCString = reinterpret_cast<char*>(Pattern::Address(0x1577428));

static int64_t* gpSCMDManager = reinterpret_cast<int64_t*>(Pattern::Address(0x18682b0));
static int64_t* gpCCMDManager = reinterpret_cast<int64_t*>(Pattern::Address(0x1888310));

static D2CCMDStrc* gpCCMDHandlerTable = reinterpret_cast<D2CCMDStrc*>(Pattern::Address(0x14bfc50));
static D2SCMDStrc* gpSCMDHandlerTable = reinterpret_cast<D2SCMDStrc*>(Pattern::Address(0x1841b40));
static D2ClientStrc** gpClientList = reinterpret_cast<D2ClientStrc**>(Pattern::Address(0x1d637f0));
static D2Widget** gpPanelManager = reinterpret_cast<D2Widget**>(Pattern::Address(0x1d7c4e8));


static uint32_t gSelectedPage = 0;

const uint64_t CMD_MAGIC = 0xDEADBEEFDEADBEEF;
const std::string STASH_NAME = "Stash";
void __fastcall UpdateStashFileName(uint32_t nSelectedPage) {
    auto scString = std::format("{}_SC_Page{}\0", STASH_NAME, nSelectedPage + 1);
    auto hcString = std::format("{}_HC_Page{}\0", STASH_NAME, nSelectedPage + 1);
    DWORD oldProtect;
    // janky but does the job
    VirtualProtect(gpSharedStashString, 0x32, PAGE_READWRITE, &oldProtect);
    strcpy(gpSharedStashString, scString.c_str());
    VirtualProtect(gpSharedStashString, 0x32, oldProtect, &oldProtect);
    VirtualProtect(gpSharedStashHCString, 0x32, PAGE_READWRITE, &oldProtect);
    strcpy(gpSharedStashHCString, hcString.c_str());
    VirtualProtect(gpSharedStashHCString, 0x32, oldProtect, &oldProtect);

    /*
    static uint8_t* tcpipPatch = reinterpret_cast<uint8_t*>(Pattern::Address(0x749AC));
    VirtualProtect(tcpipPatch, 0x1, PAGE_READWRITE, &oldProtect);
    *tcpipPatch = 0xEB;
    VirtualProtect(tcpipPatch, 0x1, oldProtect, &oldProtect);
    */
}

const std::vector<uint8_t> emptyStashTab = std::vector<uint8_t>{ 0x55, 0xaa, 0x55, 0xaa, 0x01, 0x00, 0x00, 0x00, 0x62, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4a, 0x4d, 0x00, 0x00 };
void __fastcall CCMD_SendNewEmptyStashMessage() {
    byte pBuffer[0x47] = { 0x6c, 0x44, 0x1 };
    memcpy(pBuffer + 3, emptyStashTab.data(), emptyStashTab.size());
    CCMD_QueuePacket(&pBuffer, sizeof(pBuffer));
}

void __fastcall CCMD_SendSharedStashMessage(SharedStashPhase phase, uint32_t page) {
    // send 0x3. with our magic to let us know it's a custom packet
    // there seems to be validation on op code to size. need to make sure our packet is the same size of a 0x3 packet.
    CCMD_STASH_PAGE_CHANGE tStashPageChange = { CCMD_CUSTOM_OP_CODE, CMD_MAGIC, CCMD_SHARED_STASH_OP2, phase, page };
    byte pBuffer[0xD2] = { };
    memset(&pBuffer, 0, sizeof(pBuffer));
    memcpy(&pBuffer, &tStashPageChange, sizeof(tStashPageChange));
    CCMD_QueuePacket(&pBuffer, sizeof(pBuffer));
}

void __fastcall SCMD_SendSharedStashMessage(D2ClientStrc* pClient, SharedStashPhase phase, uint32_t page) {
    // send 0x9D w/ custom action. max size 257?
    SCMD_STASH_PAGE_CHANGE tStashPageChange = { SCMD_CUSTOM_OP_CODE, CMD_MAGIC, SCMD_SHARED_STASH_OP2, phase, page };
    byte pBuffer[0xCE] = { };
    memset(&pBuffer, 0, sizeof(pBuffer));
    memcpy(&pBuffer, &tStashPageChange, sizeof(tStashPageChange));
    void* packetRange[2] = {
        &pBuffer,
        (uint8_t*)&pBuffer + 0xCE
    };
    SCMD_QueuePacket(gpSCMDManager, pClient->dwClientId, packetRange);
}

void __fastcall SCMD_HandleSharedStashMessage(SCMD_STASH_PAGE_CHANGE* pData) {
    switch (pData->nPhase) {
    case SaveStarted:
        std::cout << "Client saved" << std::endl;
        CCMD_SendSharedStashMessage(SaveCompleted, pData->nPage);
        break;
    case OldSharedStashFreed:
        std::cout << "Sending new shared stash to server" << std::endl;
        UpdateStashFileName(pData->nPage);
        reinterpret_cast<void(__fastcall*)()>(Pattern::Address(0x10e080))(); // CLIENT_ReadSaveAndSend_D2CLTSYS_OPENCHAR()
        CCMD_SendSharedStashMessage(NewSharedStashLoadedAndAttachMessagesSent, pData->nPage);
        break;
    case NewSharedStashUnitsCreated:
        std::cout << "New shared stash units created" << std::endl;
        auto pBankPanel = WidgetFindChild(*gpPanelManager, "BankExpansionLayout");
        if (pBankPanel) {
            std::cout << "Selecting tab" << std::endl;
            int32_t nSelectedTab = 0;   //This doesnt really seem to matter...
            Message m = {
                "BankPanelMessage"_hash64,
                "SelectTab"_hash64,
                (uint64_t)&nSelectedTab
            };
            BankPanelMessage(pBankPanel, m);
        }
        break;
    }
}

int64_t __fastcall CCMD_HandleSharedStashMessage(D2GameStrc* pGame, D2UnitStrc* pUnit, CCMD_STASH_PAGE_CHANGE* pData) {
    auto pClient = pUnit->pPlayerData->pClient;
    switch (pData->nPhase) {
    case PageChanged:
        // First server packet
        // Think I could maybe mash this and the SUNIT_RemoveAllSharedStashes together...
        std::cout << "Saving all clients" << std::endl;
        reinterpret_cast<void(__fastcall*)(D2GameStrc*)>(Pattern::Address(0x28f510))(pClient->pGame);    // GAME_SaveClients
        //    SCMD_SendSharedStashMessage(pClient, SaveStarted, pData->nPage);
        //    break;
        //case SaveCompleted:
        std::cout << "Freeing current shared stash" << std::endl;
        reinterpret_cast<void(__fastcall*)(D2GameStrc*, D2UnitStrc*)>(Pattern::Address(0x28bf00))(pClient->pGame, pClient->pPlayer); // SUNIT_RemoveAllSharedStashes
        SCMD_SendSharedStashMessage(pClient, OldSharedStashFreed, pData->nPage);
        break;
    case NewSharedStashLoadedAndAttachMessagesSent:
        std::cout << "Loading new shared stash" << std::endl;
        std::cout << "# of saves attached: " << pClient->nSaveHeaderSize << std::endl;
        //pClient->dwClientState = 1; //CLIENTSTATE_GAME_INIT_SENT needed for D2CLTSYS_OPENCHAR to attach save to D2ClientStrc
        //pClient->dwFlags &= ~8;
        reinterpret_cast<void(__fastcall*)(D2GameStrc*, D2ClientStrc*, D2UnitStrc*)>(Pattern::Address(0x319b70))(pClient->pGame, pClient, pClient->pPlayer); // Parse shared stash save headers in D2ClientStrc
        reinterpret_cast<void(__fastcall*)(D2ClientStrc*)>(Pattern::Address(0x2a08a0))(pClient); // CLIENTS_FreeSaveHeader
        SCMD_SendSharedStashMessage(pClient, NewSharedStashUnitsCreated, pData->nPage);
        //pClient->dwFlags |= 8;
        //pClient->dwClientState = 4; //CLIENTSTATE_INGAME
        break;
    }
    return 1;
}

void __fastcall SCMDHANDLER_Custom(uint8_t* pPacket) {
    uint64_t magic = *(uint64_t*)(pPacket + 1);
    if (magic == CMD_MAGIC) {
        SCMD_CUSTOM* pData = (SCMD_CUSTOM*)pPacket;
        if (pData->opcode2 == SCMD_SHARED_STASH_OP2) {
            SCMD_HandleSharedStashMessage((SCMD_STASH_PAGE_CHANGE*)pPacket);
        }
    }
    else {
        D2CLIENT_PACKETCALLBACK_Rcv0x2B_SCMD_CORRECT_PATH(pPacket);
    }
}

int64_t __fastcall CCMDHANDLER_Custom(D2GameStrc* pGame, D2UnitStrc* pUnit, uint8_t* pPacket, int nPacketSize) {
    uint64_t magic = *(uint64_t*)(pPacket + 1);
    if (magic == CMD_MAGIC) {
        CCMD_CUSTOM* pData = (CCMD_CUSTOM*)pPacket;
        if (pData->opcode2 == CCMD_SHARED_STASH_OP2) {
            return CCMD_HandleSharedStashMessage(pGame, pUnit, (CCMD_STASH_PAGE_CHANGE*)pPacket);
        }
        return 1;
    }
    else {
        return D2GAME_PACKETCALLBACK_Rcv0x20_CCMD_TRANSMUTE(pGame, pUnit, pPacket, nPacketSize);
    }
}

char __fastcall CCMD_ProcessClientSystemMessageHook(uint8_t* pData, int64_t nSize) {
    auto result = oCCMD_ProcessClientSystemMessage(pData, nSize);;
    const int32_t nClientId = *(int32_t*)pData;
    uint8_t* pPacket = ((uint8_t*)pData + 4);
    if (*pPacket == 0x6C) { //D2CLTSYS_OPENCHAR
        auto pClient = gpClientList[nClientId];
        if (!pClient) {
            return result;
        }
        if (pClient->dwClientState == 0x4) {
            reinterpret_cast<bool(__fastcall*)(int32_t, uint8_t*, int64_t, char)>(Pattern::Address(0x2a06f0))(nClientId, pPacket + 3, pPacket[1], pPacket[2] != 0); //CLIENTS_AttachSaveFile
            return 1;
        }
    }
    return result;
}

void __fastcall GenerateSharedStash() {
    std::cout << "Generating new stash with " << nNumberOfTabs << " tabs" << std::endl;
    for (int i = 0; i < nNumberOfTabs; i++) {
        CCMD_SendNewEmptyStashMessage();
    }
}

void OnStashPageChanged(uint32_t nSelectedPage) {
    std::cout << "Sending CCMD_STASH_PAGE_CHANGE" << std::endl;
    gSelectedPage = nSelectedPage;
    CCMD_SendSharedStashMessage(PageChanged, nSelectedPage);
}

// Might be better than global vars
/*
void __fastcall HookedBankPanelMessage(D2BankPanelWidget* pBankPanel, int64_t* pMessages) {
    oBankPanelMessage(pBankPanel, pMessages);
    if (pMessages[0] == "DropdownListWidgetMessage"_hash64
        && pMessages[1] == "OptionSelected"_hash64) {
        auto pBankPages = WidgetFindChild(pBankPanel, "BankPages");
        if (pBankPages) {
            auto nSelectedPage = *(uint32_t*)((int64_t)pBankPages + 0x178C);
            OnStashPageChanged(nSelectedPage);
        }
    }
}
*/

void __fastcall HookedBankPanelDraw(D2BankPanelWidget* pBankPanel) {
    oBankPanelDraw(pBankPanel);
    //todo hook OnSelectionChange for widget?
    auto pBankPages = WidgetFindChild(pBankPanel, "BankPages");
    if (pBankPages) {
        auto nSelectedPage = *(uint32_t*)((int64_t)pBankPages + 0x178C);
        if (nSelectedPage != gSelectedPage) {
            OnStashPageChanged(nSelectedPage);
        }
    }
}

#pragma endregion

#pragma region Window/Detour Handlers
BOOL is_main_window(HWND handle)
{
    return GetWindow(handle, GW_OWNER) == (HWND)0;
}

BOOL CALLBACK enum_windows_callback(HWND handle, LPARAM lParam)
{
    handle_data* data = (handle_data*)lParam;
    unsigned long process_id = 0;
    GetWindowThreadProcessId(handle, &process_id);
    if (data->process_id != process_id || !is_main_window(handle))
        return TRUE;
    data->window_handle = handle;
    return FALSE;
}

HWND find_main_window(DWORD process_id)
{
    handle_data data;
    data.process_id = process_id;
    data.window_handle = 0;
    EnumWindows(enum_windows_callback, (LPARAM)&data);
    return data.window_handle;
}

VOID CALLBACK DelayedActionsTimerProc(
    HWND hwnd,
    UINT message,
    UINT_PTR idTimer,
    DWORD dwTime)
{
    if (idTimer != 1234)
        return;

    printf("delayed actions timer proc\n");

    if (!queuedActions.empty())
    {
        printf("%lld delayed actions in queue\n", queuedActions.size());
        auto action = queuedActions.front();
        printf("action %s will be called\n", action.name);
        action.action();
        queuedActions.pop();
        printf("action called\n");
    }
    else
    {
        printf("queue is empty, stopping timer\n");
        KillTimer(hwnd, idTimer);
    }
}
#pragma endregion

#pragma region Helper Functions
std::string TrimWhitespace(const std::string& str) {
    size_t first = str.find_first_not_of(" \n\r\t");
    size_t last = str.find_last_not_of(" \n\r\t");
    return str.substr(first, (last - first + 1));
}

std::string ReadCommandFromFile(const std::string& filename, const std::string& searchString) {
    if (!std::filesystem::exists(filename)) {
        return {};
    }
    return TrimWhitespace(readTextFollowingString(filename, searchString));
}

void ReadCommandWithValuesFromFile(const std::string& filename, const std::string& searchString, std::string& commandKey, std::string& commandValue) {
    std::string commandLine = ReadCommandFromFile(filename, searchString);

    size_t commaPos = commandLine.find(',');
    if (commaPos != std::string::npos) {
        commandKey = TrimWhitespace(commandLine.substr(0, commaPos));
        std::string valuePart = TrimWhitespace(commandLine.substr(commaPos + 1));
        size_t startQuote = valuePart.find('\"');
        size_t endQuote = valuePart.find('\"', startQuote + 1);

        if (startQuote != std::string::npos && endQuote != std::string::npos) {
            commandValue = TrimWhitespace(valuePart.substr(startQuote + 1, endQuote - startQuote - 1));
        }
        else {
            commandValue.clear();
        }
    }
    else {
        commandKey.clear();
        commandValue.clear();
    }
}

std::string Trim(const std::string& str)
{
    size_t first = str.find_first_not_of(" \t\n\r");
    size_t last = str.find_last_not_of(" \t\n\r");
    return (first == std::string::npos || last == std::string::npos) ? "" : str.substr(first, last - first + 1);
}

std::vector<std::string> ReadAutomaticCommandsFromFile(const std::string& filename)
{
    std::vector<std::string> automaticCommands;
    std::ifstream file(filename);

    if (!file.is_open()) {
        std::cerr << "Error: Failed to open file " << filename << std::endl;
        return automaticCommands;
    }

    std::string line;

    while (std::getline(file, line)) {
        if (line.find("Startup Commands:") != std::string::npos) {
            size_t pos = line.find("Startup Commands:") + std::string("Startup Commands:").length();
            std::string commands = line.substr(pos);

            while (std::getline(file, line) && !line.empty()) {
                commands += line;
            }

            size_t commaPos = 0;
            while ((commaPos = commands.find(",")) != std::string::npos) {
                std::string command = Trim(commands.substr(0, commaPos));
                if (!command.empty()) {
                    automaticCommands.push_back(command);
                }
                commands.erase(0, commaPos + 1);
            }

            std::string command = Trim(commands);
            if (!command.empty()) {
                automaticCommands.push_back(command);
            }

            break;
        }
    }

    while (automaticCommands.size() < 6) {
        automaticCommands.push_back("");
    }

    if (automaticCommands.size() >= 6) {
        automaticCommand1 = automaticCommands[0];
        automaticCommand2 = automaticCommands[1];
        automaticCommand3 = automaticCommands[2];
        automaticCommand4 = automaticCommands[3];
        automaticCommand5 = automaticCommands[4];
        automaticCommand6 = automaticCommands[5];
    }
    else {
        std::cerr << "Error: Not enough automatic commands in the config file!" << std::endl;
    }

    return automaticCommands;
}

void WriteToDebugLog(const std::string& message) {
    const std::string logDirectory = "../Launcher/";
    if (!std::filesystem::exists(logDirectory)) {
        std::filesystem::create_directories(logDirectory);
    }
    const std::string logFilePath = logDirectory + "D2RHUD_Logs.txt";

    std::ofstream logFile(logFilePath, std::ios::app);
    if (logFile.is_open()) {
        std::time_t now = std::time(nullptr);
        std::tm* localTime = std::localtime(&now);
        logFile << "[" << std::put_time(localTime, "%a %b %d %T %Y") << "] " << message << std::endl;

        logFile.close();
    }
}
#pragma endregion

#pragma region Startup Options Control

std::string GetExecutableDir()
{
    char buffer[MAX_PATH];
    GetModuleFileNameA(nullptr, buffer, MAX_PATH);
    std::filesystem::path exePath(buffer);
    return exePath.parent_path().string();
}

static int GetClientStatus() {
    uint64_t pClients = Pattern::Address(detectGameStatusOffset);
    return (pClients != NULL) ? static_cast<int>(*(uint8_t*)pClients) : -1;
}

void ExecuteCommand(const std::string& command) {
    if (command == "disabled" || command.empty()) return;

    bool isPlayerCommand = (command.find("/") != std::string::npos);
    if (isPlayerCommand) {
        CLIENT_playerCommand(command, command);
    }
    else {
        ExecuteDebugCheatFunc(command.c_str());
    }
}

int GetPlayerDifficulty(D2UnitStrc* pPlayer) {
    if (!pPlayer || !pPlayer->pDynamicPath) return -1;
    auto pRoom = pPlayer->pDynamicPath->pRoom;
    if (!pRoom || !pRoom->pDrlgRoom) return -1;
    auto pLevel = pRoom->pDrlgRoom->pLevel;
    if (!pLevel || !pLevel->pDrlg) return -1;
    return pLevel->pDrlg->nDifficulty;
}

void OnClientStatusChange() {
    std::this_thread::sleep_for(std::chrono::seconds(10));

    std::vector<std::string> commands = { automaticCommand1, automaticCommand2, automaticCommand3,
                                         automaticCommand4, automaticCommand5, automaticCommand6 };
    for (const auto& command : commands) {
        if (!command.empty() && command != "disabled") {
            ExecuteCommand(command);
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

int CheckClientStatusChange() {
    static int previousValue = -1;
    int currentValue = GetClientStatus();

    if (previousValue != 1 && currentValue == 1) {
        std::thread(OnClientStatusChange).detach();
    }

    previousValue = currentValue;
    return currentValue;
}

std::atomic<bool> keepPolling{ true };

void PollClientStatus() {
    while (keepPolling) {
        auto result = CheckClientStatusChange();
        //std::cout << "Current Status: " << result << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

void StartPolling() {
    std::thread(PollClientStatus).detach();
}

void StopPolling() {
    keepPolling = false;
}
#pragma endregion

#pragma region Monster Stats Structure

int mapColorToInt(const std::string& colorCode) {
    static const std::unordered_map<std::string, int> colorMap = {
        {"ÿc0", 0},
        {"ÿc1", 1},
        {"ÿc2", 2},
        {"ÿc3", 3},
        {"ÿc4", 4},
        {"ÿc5", 5},
        {"ÿc6", 6},
        {"ÿc7", 7},
        {"ÿc8", 8},
        {"ÿc9", 9},
        {"ÿc;", 11},
        {"ÿcA", 17},
        {"ÿcN", 25},
        {"ÿcO", 31}
    };

    auto it = colorMap.find(colorCode);
    if (it != colorMap.end()) {
        return it->second;
    }
    else {
        return -1;
    }
}

MonsterStatsDisplaySettings getMonsterStatsDisplaySetting(const std::string& configFilePath)
{
    static bool isCached = false;
    static MonsterStatsDisplaySettings cachedSettings;

    // ---- Cache check ----
    if (isCached)
    {
        std::cerr << "[DEBUG] Cache HIT — returning cached MonsterStatsDisplaySettings" << std::endl;
        return cachedSettings;
    }

    StartPolling();

    using ordered_json = nlohmann::ordered_json;
    ordered_json j;

    // ---------------- Load JSON ----------------
    try
    {
        namespace fs = std::filesystem;

        const fs::path targetPath = configFilePath;
        const fs::path templatePath = "HUDConfig_Template.json";

        // ---- First-time use detection ----
        if (!fs::exists(targetPath) && fs::exists(templatePath))
        {
            fs::rename(templatePath, targetPath);
            std::cerr << "[INFO] First-time setup: renamed HUDConfig_Template.json -> " << targetPath << std::endl;
        }

        std::ifstream file(targetPath);
        if (!file.is_open())
        {
            std::cerr << "[ERROR] Could not open config file: " << targetPath << std::endl;
            return {};
        }

        file >> j;
    }
    catch (const std::exception& e)
    {
        std::cerr << "[ERROR] Failed to parse JSON config: " << e.what() << std::endl;
        return {};
    }

    if (!j.is_object())
    {
        std::cerr << "[ERROR] Config root is not a JSON object" << std::endl;
        return {};
    }

    std::cerr << "[DEBUG] JSON config loaded successfully" << std::endl;

    // ---------------- Helper functions ----------------
    auto getBool = [&](std::initializer_list<std::string> path, bool def) -> bool
        {
            const ordered_json* cur = &j;
            for (const auto& p : path)
            {
                if (!cur->contains(p))
                    return def;
                cur = &(*cur)[p];
            }
            return cur->is_boolean() ? cur->get<bool>() : def;
        };

    auto getInt = [&](std::initializer_list<std::string> path, int def) -> int
        {
            const ordered_json* cur = &j;
            for (const auto& p : path)
            {
                if (!cur->contains(p))
                    return def;
                cur = &(*cur)[p];
            }
            return cur->is_number_integer() ? cur->get<int>() : def;
        };

    auto getString = [&](std::initializer_list<std::string> path,
        const std::string& def) -> std::string
        {
            const ordered_json* cur = &j;
            for (const auto& p : path)
            {
                if (!cur->contains(p))
                    return def;
                cur = &(*cur)[p];
            }
            return cur->is_string() ? cur->get<std::string>() : def;
        };

    // ---------------- Populate settings ----------------
    cachedSettings.monsterStatsDisplay = getBool({ "MonsterStatsDisplay" }, true);
    cachedSettings.channelColor = getString({ "Channel Color" }, "");
    cachedSettings.playerNameColor = getString({ "Player Name Color" }, "");
    cachedSettings.messageColor = getString({ "Message Color" }, "");

    cachedSettings.HPRollover = getBool({ "HPRolloverMods" }, true);
    cachedSettings.HPRolloverAmt = getInt({ "HPRollover%" }, 0);
    cachedSettings.HPRolloverDiff = getInt({ "HPRolloverDifficulty" }, 0);

    cachedSettings.sunderedMonUMods = getBool({ "SunderedMonUMods" }, true);
    cachedSettings.minionEquality = getBool({ "MinionEquality" }, true);
    cachedSettings.gambleForce = getBool({ "GambleCostControl" }, true);
    cachedSettings.SunderValue = getInt({ "SunderValue" }, 0);
    cachedSettings.CombatLog = getBool({ "CombatLog" }, true);
    cachedSettings.TransmogVisuals = getBool({ "TransmogVisuals" }, true);
    cachedSettings.ExtendedItemcodes = getBool({ "ExtendedItemcodes" }, true);
    cachedSettings.FloatingDamage = getBool({ "FloatingDamage" }, true);

    // ---------------- Finalize cache ----------------
    isCached = true;
    return cachedSettings;
}

MonsterStatsDisplaySettings settings = getMonsterStatsDisplaySetting(configFilePath);

#pragma endregion

#pragma region Chat/Command Functions
void SendDebugCheat(const char* cheat) {
    CCMD_DEBUGCHEAT_PACKET debugCheat = {};
    debugCheat.opcode = CCMD_DEBUGCHEAT;
    debugCheat.unk1 = 1;
    strcpy_s(debugCheat.cheat, cheat);
    SendPacketFunc(&debugCheat);
}

typedef void(__fastcall* Send_SCMD_CHATSTART_Fptr)(uint64_t pPlayer, SCMD_CHATSTART_PACKET* pChatStart);
static GetUnitNameFptr GetUnitName = reinterpret_cast<GetUnitNameFptr>(Pattern::Address(GetUnitNameOffset));
static BroadcastChatMessageFptr BroadcastChatMessage = reinterpret_cast<BroadcastChatMessageFptr>(Pattern::Address(BroadcastChatMessageOffset));
static Send_SCMD_CHATSTART_Fptr Send_SCMD_CHATSTART = reinterpret_cast<Send_SCMD_CHATSTART_Fptr>(Pattern::Address(Send_SCMD_CHATSTARTOffset));
static std::vector<std::string> g_automaticCommands;
typedef void(__fastcall* MONSTER_InitializeStatsAndSkills_t)(D2GameStrc* pGame, D2ActiveRoomStrc* pRoom, D2UnitStrc* pMonster, int64_t* pMonRegData);
static MONSTER_InitializeStatsAndSkills_t oMONSTER_InitializeStatsAndSkills = nullptr;
static std::unordered_set<DWORD> g_unitsEdited;

void BroadcastChatMessageCustom(uint64_t pGame, const char* szSender, const char* szMsg) {
    SCMD_CHATSTART_PACKET chatStart = {};
    chatStart.opcode = SCMD_CHATSTART;
    chatStart.msgType = 0xFF;
    chatStart.color = 4;

#undef min
    strncpy(chatStart.sender, szSender, std::min(strlen(szSender), size_t(60)));
    strncpy(chatStart.message, szMsg, std::min(strlen(szMsg), size_t(255)));

    uint64_t pClient = *reinterpret_cast<uint64_t*>(pGame + 336);
    while (pClient) {
        if (*reinterpret_cast<uint32_t*>(pClient + 4) == 4) {
            Send_SCMD_CHATSTART(pClient, &chatStart);
        }
        pClient = *reinterpret_cast<uint64_t*>(pClient + 1368);
    }
}

bool __fastcall CCMD_DEBUGCHEAT_Hook(uint64_t pGame, uint64_t pPlayer, CCMD_DEBUGCHEAT_PACKET* pCheat, uint32_t dwDataLen) {
    printf("CCMD_DEBUGCHEAT_Hook: %s\n", pCheat->cheat);

    char cheatBuf[256];
    strcpy_s(cheatBuf, pCheat->cheat);

    char* next_token = nullptr;
    const char* cheatName = strtok_s(cheatBuf, " =", &next_token);
    const char* cheatArg = strtok_s(nullptr, "", &next_token);

    DebugCheatEntry* cheatsArray = reinterpret_cast<DebugCheatEntry*>(Pattern::Address(cheatsArrayOffset));
    bool isChatMessage = true;

    for (uint32_t i = 0; i < 128; i++) {
        if (strstr(cheatName, cheatsArray[i].name) == cheatName &&
            (!cheatsArray[i].hasArguments || (cheatArg != nullptr))) {
            isChatMessage = false;
            break;
        }
    }

    if (isChatMessage) {
        char nameBuf[128] = {};
        const char* playerName = GetUnitName(pPlayer, nameBuf);
        char messageBuf[512];
        sprintf_s(messageBuf, "%s%s: %s%s", settings.playerNameColor.c_str(), playerName, settings.messageColor.c_str(), pCheat->cheat);
        BroadcastChatMessageCustom(pGame, playerName, messageBuf);
        return false;
    }

    return CCMD_DEBUGCHEAT_Handler_Orig(pGame, pPlayer, pCheat, dwDataLen);
}

bool __fastcall Process_SCMD_CHATSTART_Hook(SCMD_CHATSTART_PACKET* pPacket) {
    if (pPacket->msgType == 0xFF) {
        auto pChatMgr = GetChatManager();
        blz_string msg = { pPacket->message, strlen(pPacket->message), strlen(pPacket->message) };
        int colorValue = mapColorToInt(settings.channelColor);
        ChatMsg gameChatMsgType = *reinterpret_cast<ChatMsg*>(Pattern::Address(gameChatMsgTypeOffset));
        ChatOptionalStruct opt = {};

        ChatManager_PushChatEntry(pChatMgr, msg, colorValue, false, gameChatMsgType, opt, opt, opt, opt);
        return true;
    }
    return Process_SCMD_CHATSTART_Orig(pPacket);
}

void __fastcall GameMenuOnClickHandlerHook(uint64_t a1, Widget* pWidget) {
    WriteToDebugLog("GameMenuOnClickHandlerHook called");

    if (!pWidget) {
        WriteToDebugLog("pWidget is null. Exiting function.");
        return;
    }

    if (pWidget->hash == SaveAndExitButtonHash) {
        WriteToDebugLog("Save and Exit button detected. Executing save command");

        ExecuteDebugCheatFunc("save 1");
        WriteToDebugLog("Executed: 'save 1'");
        g_unitsEdited.clear();
        itemFilter->ClearInvOverrideCache();

        queuedActions.push({ "delayexit", [a1, pWidget] {
            WriteToDebugLog("Executing delayed exit action");
            mainMenuClickHandlerOrig(a1, pWidget);
        } });

        HWND mainWindow = find_main_window(GetCurrentProcessId());
        if (mainWindow) {
            WriteToDebugLog("Main window found. Setting timer");
            SetTimer(mainWindow, 1234, 1000, DelayedActionsTimerProc);
        }
        else {
            WriteToDebugLog("Failed to find main window");
        }

        return;
    }

    mainMenuClickHandlerOrig(a1, pWidget);
    WriteToDebugLog("Original handler executed");
}
#pragma endregion

#pragma region Monster Stats

typedef BOOL(__fastcall* STATLISTEX_SetStatListExStat_t)(D2StatListExStrc* pStatListEx, D2C_ItemStats nStat, int32_t nValue, uint16_t nLayer);
static STATLISTEX_SetStatListExStat_t STATLISTEX_SetStatListExStat = reinterpret_cast<STATLISTEX_SetStatListExStat_t>(Pattern::Address(0x1e5270));

typedef void(__fastcall* MONSTER_GetPlayerCountBonus_t)(D2GameStrc* pGame, D2PlayerCountBonusStrc* pPlayerCountBonus, D2ActiveRoomStrc* pRoom, D2UnitStrc* pMonster);
static MONSTER_GetPlayerCountBonus_t oMONSTER_GetPlayerCountBonus = nullptr;

typedef void(__fastcall* SUNITDMG_ApplyResistancesAndAbsorb_t)(D2DamageInfoStrc* pDamageInfo, D2DamageStatTableStrc* pDamageStatTableRecord, int32_t bDontAbsorb);
static SUNITDMG_ApplyResistancesAndAbsorb_t oSUNITDMG_ApplyResistancesAndAbsorb = nullptr;

static int32_t* gnVirtualPlayerCount = reinterpret_cast<int32_t*>(Pattern::Address(0x1d637e4));
int32_t playerCountGlobal;


D2MonStatsTxt* __fastcall MONSTERMODE_GetMonStatsTxtRecord(int32_t nMonsterId)
{
    if (nMonsterId >= 0 && nMonsterId < sgptDataTables->nMonStatsTxtRecordCount)
        return &sgptDataTables->pMonStatsTxt[nMonsterId];

    return nullptr;
}


void __fastcall HookedMONSTER_GetPlayerCountBonus(D2GameStrc* pGame, D2PlayerCountBonusStrc* pPlayerCountBonus, D2ActiveRoomStrc* pRoom, D2UnitStrc* pMonster) {
    oMONSTER_GetPlayerCountBonus(pGame, pPlayerCountBonus, pRoom, pMonster);
    playerCountGlobal = pPlayerCountBonus->nPlayerCount;

    if (GetModName() != "RMD-MP")
    {
        // cap max hp bonus at 300%. once it gets to 500% + it can rollover quickly causing monsters to have negative hp.
        if (pPlayerCountBonus->nPlayerCount > 8 && pGame->nDifficulty > settings.HPRolloverDiff)
            pPlayerCountBonus->nHP = 300;
    }   
}

const int32_t nMaxPlayerCount = 65535;
float nMaxDamageReductionPercent = settings.HPRolloverAmt; // e.g., 90 means 90% max reduction

void __fastcall ScaleDamage(D2DamageInfoStrc* pDamageInfo, D2DamageStatTableStrc* pDamageStatTableRecord) {
    if (pDamageInfo->bDefenderIsMonster && *pDamageStatTableRecord->pOffsetInDamageStrc > 0) {
        int nPlayerCount = STATLIST_GetUnitStatSigned(pDamageInfo->pDefender, STAT_MONSTER_PLAYERCOUNT, 0);

        if (nPlayerCount > 8) {
            // Calculate how far we are past 8 players, normalized
            float playerRatio = static_cast<float>(nPlayerCount - 8) / (nMaxPlayerCount - 8);

            // Exponential scaling: the higher the exponent, the faster the reduction accelerates
            float exponent = 2000.5f;  // adjust this to make scaling more aggressive (>2 is quite steep)
            float scaleFactor = pow(1.0f - playerRatio, exponent); // approaches 0 rapidly

            // Final reduction capped by configured percentage
            float reduction = (1.0f - scaleFactor) * (nMaxDamageReductionPercent / 100.0f);

            float damageScale = 1.0f - reduction;
            *pDamageStatTableRecord->pOffsetInDamageStrc *= damageScale;

            /*
            std::ofstream log("d2r_hp.txt", std::ios::app);
            if (log.is_open()) {
                log << "Player count: " << nPlayerCount
                    << " | Reduction: " << (1.0f - damageScale) * 100.0f << "%"
                    << " | Damage scale: " << damageScale << std::endl;
                log.close();
            }
            */
        }
    }
}

void __fastcall HookedSUNITDMG_ApplyResistancesAndAbsorb(D2DamageInfoStrc* pDamageInfo, D2DamageStatTableStrc* pDamageStatTableRecord, int32_t bDontAbsorb) {
    oSUNITDMG_ApplyResistancesAndAbsorb(pDamageInfo, pDamageStatTableRecord, bDontAbsorb);

    if (pDamageInfo->pGame->nDifficulty > settings.HPRolloverDiff) {
        ScaleDamage(pDamageInfo, pDamageStatTableRecord);
    }
}

#pragma endregion

#pragma region Sunder Mechanic

constexpr uint16_t UNIQUE_LAYER = 1337;

static void LogSunder(const std::string& msg)
{
    std::ofstream log("debug_sunder_log.txt", std::ios::app);
    if (!log.is_open()) return;

    std::time_t now = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    char timebuf[32];
    std::strftime(timebuf, sizeof(timebuf), "[%Y-%m-%d %H:%M:%S] ", &tm);
    log << timebuf << msg << "\n";
}

void StoreRemainder(D2UnitStrc* pUnit, const RemainderEntry& remainders)
{
    if (!pUnit) return;
    g_resistRemainders[pUnit->dwUnitId] = remainders;
}

RemainderEntry GetRemainder(D2UnitStrc* pUnit)
{
    if (!pUnit) return {};

    auto it = g_resistRemainders.find(pUnit->dwUnitId);
    if (it != g_resistRemainders.end())
        return it->second;

    return {};
}

uint32_t SubtractResistances(D2UnitStrc* pUnit, D2C_ItemStats nStatId, uint32_t nValue, uint16_t nLayer = 0)
{
    if (!pUnit) return 0;

    auto nCurrentValue = STATLIST_GetUnitStatSigned(pUnit, nStatId, nLayer);
    int newValue = nCurrentValue - nValue;
    int remainder = 0;

    if (nCurrentValue >= 100)
    {
        // Calculate overshoot based on cachedSettings.SunderValue
        if (newValue < settings.SunderValue) {
            remainder = settings.SunderValue - newValue;
            newValue = settings.SunderValue;
        }

        STATLISTEX_SetStatListExStat(pUnit->pStatListEx, nStatId, newValue, nLayer);
    }
    else
    {
        remainder = nValue;
        newValue = nCurrentValue;
    }

    STATLISTEX_SetStatListExStat(pUnit->pStatListEx, nStatId, newValue, nLayer);

    // Update the proper remainder entry
    auto& re = g_resistRemainders[pUnit->dwUnitId];
    switch (nStatId)
    {
    case STAT_COLDRESIST:  re.cold = remainder; break;
    case STAT_FIRERESIST:  re.fire = remainder; break;
    case STAT_LIGHTRESIST: re.light = remainder; break;
    case STAT_POISONRESIST: re.poison = remainder; break;
    case STAT_DAMAGERESIST: re.damage = remainder; break;
    case STAT_MAGICRESIST:  re.magic = remainder; break;
    default: break; // ignore other stats
    }

    //LogSunder("SU Monster Value: " + std::to_string(nCurrentValue) + ", SU Function Value: " + std::to_string(nValue) + ", SU New Value: " + std::to_string(newValue) + ", SU Remainder: " + std::to_string(remainder));

    return remainder;
}

void SetStat(D2UnitStrc* pUnit, D2C_ItemStats nStatId, uint32_t nValue) {
    int currentValue = STATLIST_GetUnitStatSigned(pUnit, nStatId, 0);

    if (currentValue >= static_cast<int>(nValue))
        return;

    int offset = static_cast<int>(nValue) - currentValue;

    STATLISTEX_SetStatListExStat(pUnit->pStatListEx, nStatId, currentValue + offset, 0);
    STATLISTEX_SetStatListExStat(pUnit->pStatListEx, nStatId, offset, UNIQUE_LAYER);
}

void AddToCurrentStat(D2UnitStrc* pUnit, D2C_ItemStats nStatId, uint32_t nValue) {
    int currentValue = STATLIST_GetUnitStatSigned(pUnit, nStatId, 0);

    if (STATLIST_GetUnitStatSigned(pUnit, STAT_ALIGNMENT, 0) != 1)
        STATLISTEX_SetStatListExStat(pUnit->pStatListEx, nStatId, currentValue + nValue, 0);
}

static void ApplySunderForStat(D2UnitStrc* pUnit, D2C_ItemStats statId, int maxVal, const std::vector<std::pair<const char*, int>>& umodCaps, const std::vector<const uint32_t*>& umodArrays, const std::vector<size_t>& umodSizes, const std::string& statName, const std::vector<std::vector<uint8_t>>& originalGroups)   // per-group originals
{
    if (maxVal <= INT_MIN)
        return;

    int rem = SubtractResistances(pUnit, statId, maxVal);
    //LogSunder(statName + " max=" + std::to_string(maxVal) + " SubtractResistances rem=" + std::to_string(rem));

    for (size_t i = 0; i < umodArrays.size(); ++i)
    {
        //LogSunder(statName + " UMod[" + std::string(umodCaps[i].first) + "] cap=" + std::to_string(umodCaps[i].second) + " final=" + std::to_string(rem));

        const std::vector<uint8_t>& groupOriginal = (i < originalGroups.size()) ? originalGroups[i] : std::vector<uint8_t>{};

        if (settings.sunderedMonUMods || cachedSettings.sunderedMonUMods)
            ApplyUModArray(umodArrays[i], umodSizes[i], rem, groupOriginal, statName);
    }
}

void __fastcall ApplyGhettoSunder(D2GameStrc* pGame, D2ActiveRoomStrc* pRoom, D2UnitStrc* pUnit, int64_t* pMonRegData, D2MonStatsInitStrc* monStatsInit)
{
    if (!pGame || !pUnit)
    {
        //LogSunder("Invalid game/unit pointer in ApplyGhettoSunder");
        return;
    }

    //LogSunder("=== Begin ApplyGhettoSunder ===");

    int maxCold = INT_MIN, maxFire = INT_MIN, maxLight = INT_MIN;
    int maxPoison = INT_MIN, maxDamage = INT_MIN, maxMagic = INT_MIN;

    for (int i = 0; i < 8; ++i)
    {
        auto pClient = gpClientList[i];
        if (!pClient) continue;

        uint32_t guid = pClient->dwUnitGUID;
        D2UnitStrc* pPlayer = UNITS_GetServerUnitByTypeAndId(pGame, UNIT_PLAYER, guid);
        if (!pPlayer) continue;

        int cold = STATLIST_GetUnitStatSigned(pPlayer, 187, 0);
        int fire = STATLIST_GetUnitStatSigned(pPlayer, 189, 0);
        int light = STATLIST_GetUnitStatSigned(pPlayer, 190, 0);
        int poison = STATLIST_GetUnitStatSigned(pPlayer, 191, 0);
        int damage = STATLIST_GetUnitStatSigned(pPlayer, 192, 0);
        int magic = STATLIST_GetUnitStatSigned(pPlayer, 193, 0);

        if (cold > maxCold)     maxCold = cold;
        if (fire > maxFire)     maxFire = fire;
        if (light > maxLight)   maxLight = light;
        if (poison > maxPoison) maxPoison = poison;
        if (damage > maxDamage) maxDamage = damage;
        if (magic > maxMagic)   maxMagic = magic;
    }

    RemainderEntry remainders{};
    remainders.cold = SubtractResistances(pUnit, STAT_COLDRESIST, maxCold);
    remainders.fire = SubtractResistances(pUnit, STAT_FIRERESIST, maxFire);
    remainders.light = SubtractResistances(pUnit, STAT_LIGHTRESIST, maxLight);
    remainders.poison = SubtractResistances(pUnit, STAT_POISONRESIST, maxPoison);
    remainders.damage = SubtractResistances(pUnit, STAT_DAMAGERESIST, maxDamage);
    remainders.magic = SubtractResistances(pUnit, STAT_MAGICRESIST, maxMagic);

    StoreRemainder(pUnit, remainders);
    //LogSunder("=== End ApplyGhettoSunder ===");
}



#pragma endregion

#pragma region Terror Zones

#pragma region - Static/Structs

struct DifficultySettings {
    std::optional<int> bound_incl_min;
    std::optional<int> bound_incl_max;
    std::optional<int> boost_level;
    std::optional<int> difficulty_scale;
    std::optional<int> boost_experience_percent;
};

struct WarningInfo {
    int announce_time_min = 0;
    int tier = 0;
};

struct LevelName {
    int id;
    std::string name;
};

struct LevelGroup {
    std::string name;
    std::vector<int> levels;
};

struct Stat {
    D2C_ItemStats id;
    int minValue;
    int maxValue;
    bool allowNegative;
    bool isBinary;
};

struct MonsterTreasureClass
{
    std::string MonsterName;
    std::string TCChecker1a;
    std::string TCChecker1b;
    std::string TCChecker1c;
    std::string Desecrated;
    std::string DesecratedChamp;
    std::string DesecratedUnique;
    std::string TCChecker2a;
    std::string TCChecker2b;
    std::string TCChecker2c;
    std::string Desecrated_N;
    std::string DesecratedChamp_N;
    std::string DesecratedUnique_N;
    std::string TCChecker3a;
    std::string TCChecker3b;
    std::string TCChecker3c;
    std::string Desecrated_H;
    std::string DesecratedChamp_H;
    std::string DesecratedUnique_H;
};

struct MonsterTreasureClassSU
{
    std::string MonsterName;
    std::string BaseMonster;
    std::string TCChecker1;
    std::string Desecrated;
    std::string TCChecker2;
    std::string Desecrated_N;
    std::string TCChecker3;
    std::string Desecrated_H;
};

struct StatValue
{
    D2C_ItemStats stat;
    std::vector<int> value;
};

struct StatAdjustment
{
    bool random;
    int minStats = 0;
    int maxStats = 0;
    std::vector<StatValue> stats_values;
};

struct StatNameEntry {
    int id;
    std::string name;
};

struct ZoneLevel {
    std::optional<int> level_id;
    bool allLevels = false;

    // Optional per-difficulty overrides
    std::optional<DifficultySettings> normal;
    std::optional<DifficultySettings> nightmare;
    std::optional<DifficultySettings> hell;
    std::vector<StatAdjustment> stat_adjustments;
};

struct ZoneGroup {
    int id = 0;
    std::vector<ZoneLevel> levels;
};

struct DesecratedZone {
    std::time_t start_time_utc = 0;
    std::time_t end_time_utc = 0;
    int terror_duration_min = 0;
    int terror_break_min = 0;
    uint64_t seed = 0;
    DifficultySettings default_normal;
    DifficultySettings default_nightmare;
    DifficultySettings default_hell;
    std::vector<WarningInfo> warnings;
    std::vector<ZoneGroup> zones;
};

struct TerrorZoneDisplayData
{
    int cycleLengthMin = 0;
    int terrorDurationMin = 0;
    int groupCount = 0;
    int activeGroupIndex = -1;
    time_t zoneStartUtc = 0;
    std::vector<ZoneLevel> activeLevels;
};

struct MonsterTreasureResult {
    int treasureIndex;
    int tcCheckIndex;
};

int playerLevel = 0;
static std::string g_ActiveZoneInfoText;
int g_ManualZoneGroupOverride = -1;
time_t g_LastToggleTime = 0;
TerrorZoneDisplayData g_TerrorZoneData;
std::vector<DesecratedZone> gDesecratedZones;
std::vector<LevelName> level_names;
std::vector<LevelGroup> level_groups;
typedef BOOL(__fastcall* QUESTRECORD_GetQuestState_t)(D2BitBufferStrc* pQuestRecord, int32_t nQuestId, int32_t nState);
static QUESTRECORD_GetQuestState_t oQUESTRECORD_GetQuestState = reinterpret_cast<QUESTRECORD_GetQuestState_t>(Pattern::Address(0x243880));
typedef int64_t(__fastcall* HUDWarnings__PopulateHUDWarnings_t)(void* pWidget);
static HUDWarnings__PopulateHUDWarnings_t oHUDWarnings__PopulateHUDWarnings = nullptr;
typedef void(__fastcall* Widget__OnClose_t)(void* pWidget);
static Widget__OnClose_t oWidget__OnClose = nullptr;
static char pCustom[1024];
typedef BOOL(__stdcall* DATATBLS_CalculateMonsterStatsByLevel_t)(int nMonsterId, int nGameType, int nDifficulty, int nLevel, short nFlags, D2MonStatsInitStrc* pMonStatsInit);
static DATATBLS_CalculateMonsterStatsByLevel_t oAdjustMonsterStats = reinterpret_cast<DATATBLS_CalculateMonsterStatsByLevel_t>(Pattern::Address(0x2356B0));
typedef void(__fastcall* DropTCTest_t)(D2GameStrc* pGame, D2UnitStrc* pMonster, D2UnitStrc* pPlayer, int32_t nTCId, int32_t nQuality, int32_t nItemLevel, int32_t a7, D2UnitStrc** ppItems, int32_t* pnItemsDropped, int32_t nMaxItems);
static DropTCTest_t oDropTCTest = nullptr;
typedef uint32_t(__fastcall* GambleForce_t)(D2UnitStrc* pItem, int nPlayerLevel);
static GambleForce_t oGambleForce = nullptr;
bool isTerrorized = false;
std::vector<StatAdjustment> gStatAdjustments;
std::unordered_map<int, std::string> gStatNames;
static std::vector<std::pair<D2C_ItemStats, int>> g_randomStats;
std::unordered_map<D2C_ItemStats, int> gRandomStatsForMonsters;
bool showStatAdjusts = true;
std::string g_ItemFilterStatusMessage = "";
bool g_ShouldShowItemFilterMessage = false;
std::chrono::steady_clock::time_point g_ItemFilterMessageStartTime;
bool initialized = false;
static char gTZInfoText[256] = { 0 };
static char gTZStatAdjText[256] = { 0 };
static std::mutex g_LogMutex;
static std::unordered_map<uint32_t, std::unordered_set<D2C_ItemStats>> g_unitsEditedStats;
static std::once_flag gZonesLoadedFlag;
static std::atomic<bool> gZonesLoaded = false;
static std::string gZonesFilePath;
namespace {
    static double gLastManualToggleTime = 0;
}

#pragma endregion

#pragma region - Helper Functions

static void LogDebug(const std::string& msg)
{
    std::ofstream log("debug_log.txt", std::ios::app);
    if (!log.is_open())
        return;

    log << msg << "\n";
}

void LogSpawnDebug(const char* fmt, ...)
{
    std::lock_guard<std::mutex> lock(g_LogMutex);

    FILE* f = fopen("spawn_debug.log", "a");
    if (!f)
        return;

    // Timestamp
    std::time_t t = std::time(nullptr);
    std::tm tmBuf{};
#ifdef _WIN32
    localtime_s(&tmBuf, &t);
#else
    tmBuf = *std::localtime(&t);
#endif

    fprintf(f, "[%04d-%02d-%02d %02d:%02d:%02d] ",
        tmBuf.tm_year + 1900,
        tmBuf.tm_mon + 1,
        tmBuf.tm_mday,
        tmBuf.tm_hour,
        tmBuf.tm_min,
        tmBuf.tm_sec);

    // Format the message
    va_list args;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);

    fprintf(f, "\n");

    fclose(f);
}

int32_t __fastcall MONSTERUNIQUE_GetSuperUniqueBossHcIdx(D2GameStrc* pGame, D2UnitStrc* pUnit)
{
    if (pUnit && pUnit->dwUnitType == UNIT_MONSTER && pUnit->pMonsterData && pUnit->pMonsterData->nTypeFlag & MONTYPEFLAG_SUPERUNIQUE)
        return pUnit->pMonsterData->wBossHcIdx;

    return -1;
}

int32_t __fastcall MONSTERUNIQUE_CheckMonTypeFlag(D2UnitStrc* pUnit, uint16_t nFlag)
{
    if (pUnit && pUnit->dwUnitType == UNIT_MONSTER && pUnit->pMonsterData)
        return (pUnit->pMonsterData->nTypeFlag & nFlag) != 0;

    return 0;
}

BOOL CalculateMonsterStats(int monsterId, int gameType, int difficulty, int level, short flags, D2MonStatsInitStrc& outStats)
{
    if (!oAdjustMonsterStats)
        return FALSE;

    return oAdjustMonsterStats(monsterId, gameType, difficulty, level, flags, &outStats);
}

inline int D2_ApplyRatio(int32_t nValue, int32_t nMultiplier, int32_t nDivisor)
{
    if (nDivisor)
    {
        if (nValue <= 0x100'000)
        {
            if (nMultiplier <= 0x10'000)
                return nMultiplier * nValue / nDivisor;

            if (nDivisor <= (nMultiplier >> 4))
                return nValue * (nMultiplier / nDivisor);
        }
        else
        {
            if (nDivisor <= (nValue >> 4))
                return nMultiplier * (nValue / nDivisor);
        }

        return ((int64_t)nMultiplier * (int64_t)nValue) / nDivisor;
    }

    return 0;
}

inline int32_t D2_ComputePercentage(int32_t nValue, int32_t nPercentage)
{
    return D2_ApplyRatio(nValue, nPercentage, 100);
}

inline uint64_t __fastcall SEED_RollRandomNumber(D2SeedStrc* pSeed)
{
    uint64_t lSeed = static_cast<uint64_t>(pSeed->dwSeed[1]) + 0x6AC690C5i64 * static_cast<uint64_t>(pSeed->dwSeed[0]);
    pSeed->lSeed = lSeed;
    return lSeed;
}

inline uint32_t __fastcall SEED_RollLimitedRandomNumber(D2SeedStrc* pSeed, int nMax)
{
    if (nMax > 0)
    {
        if ((nMax - 1) & nMax)
            return (unsigned int)SEED_RollRandomNumber(pSeed) % nMax;
        else
            return SEED_RollRandomNumber(pSeed) & (nMax - 1);
    }

    return 0;
}

uint32_t __fastcall ITEMS_RollLimitedRandomNumber(D2SeedStrc* pSeed, int32_t nMax)
{
    return SEED_RollLimitedRandomNumber(pSeed, nMax);
}

std::string StripComments(const std::string& jsonWithComments) {
    std::istringstream iss(jsonWithComments);
    std::ostringstream oss;
    std::string line;
    bool in_block_comment = false;

    while (std::getline(iss, line)) {
        std::string newLine;
        bool in_string = false;

        for (size_t i = 0; i < line.length(); ++i) {
            char c = line[i];

            if (c == '\"') {
                bool escaped = (i > 0 && line[i - 1] == '\\');
                if (!escaped) in_string = !in_string;
            }

            if (in_block_comment) {
                if (c == '*' && i + 1 < line.length() && line[i + 1] == '/') {
                    in_block_comment = false;
                    ++i;
                }
                continue;
            }

            if (!in_string && c == '/' && i + 1 < line.length() && line[i + 1] == '*') {
                in_block_comment = true;
                ++i;
                continue;
            }

            if (!in_string && c == '/' && i + 1 < line.length() && line[i + 1] == '/') {
                size_t trimEnd = newLine.find_last_not_of(" \t");
                if (trimEnd != std::string::npos) {
                    newLine = newLine.substr(0, trimEnd + 1);
                }
                else {
                    newLine.clear();
                }
                break;
            }

            newLine += c;
        }

        if (!in_block_comment)
            oss << newLine << "\n";
    }

    return oss.str();
}

bool GetBaalQuest(D2UnitStrc* pPlayer, D2GameStrc* pGame) {
    if (!pPlayer || !pPlayer->pPlayerData || !pGame)
        return false;

    auto pQuestData = pPlayer->pPlayerData->pQuestData[pGame->nDifficulty];
    if (!pQuestData)
        return false;

    return pGame->bExpansion & oQUESTRECORD_GetQuestState(pQuestData, QUESTSTATEFLAG_A5Q6, QFLAG_REWARDGRANTED);
}

int GetLevelIdFromRoom(D2ActiveRoomStrc* pRoom)
{
    if (!pRoom || !pRoom->pDrlgRoom || !pRoom->pDrlgRoom->pLevel)
        return -1;

    return pRoom->pDrlgRoom->pLevel->nLevelId;
}

std::string GetMonsterTypeFlags(uint8_t flags)
{
    std::string out;

    if (flags & MONTYPEFLAG_OTHER)        out += "OTHER ";
    if (flags & MONTYPEFLAG_SUPERUNIQUE)  out += "SUPERUNIQUE ";
    if (flags & MONTYPEFLAG_CHAMPION)     out += "CHAMPION ";
    if (flags & MONTYPEFLAG_UNIQUE)       out += "UNIQUE ";
    if (flags & MONTYPEFLAG_MINION)       out += "MINION ";
    if (flags & MONTYPEFLAG_POSSESSED)    out += "POSSESSED ";
    if (flags & MONTYPEFLAG_GHOSTLY)      out += "GHOSTLY ";
    if (flags & MONTYPEFLAG_MULTISHOT)    out += "MULTISHOT ";

    if (out.empty())
        return "NONE";

    return out;
}

#pragma endregion

#pragma region - JSON Parsers

inline std::time_t parse_time_utc(const std::string& s) {
    std::tm tm = {};
    std::istringstream ss(s);
    ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
    if (ss.fail()) {
        return 0;
    }
    return _mkgmtime(&tm);
}

void from_json(const json& j, DifficultySettings& d) {

    //Optional
    if (j.contains("bound_incl_min"))
        d.bound_incl_min = j.at("bound_incl_min").get<int>();

    if (j.contains("bound_incl_max"))
        d.bound_incl_max = j.at("bound_incl_max").get<int>();

    if (j.contains("boost_level"))
        d.boost_level = j.at("boost_level").get<int>();

    if (j.contains("difficulty_scale"))
        d.difficulty_scale = j.at("difficulty_scale").get<int>();

    if (j.contains("boost_experience_percent"))
        d.boost_experience_percent = j.at("boost_experience_percent").get<int>();
}

void from_json(const json& j, WarningInfo& w) {
    w.announce_time_min = j.at("announce_time_min").get<int>();
    w.tier = j.at("tier").get<int>();
}

void from_json(const nlohmann::json& j, LevelName& ln) {
    j.at("id").get_to(ln.id);
    j.at("name").get_to(ln.name);
}

void from_json(const nlohmann::json& j, LevelGroup& lg) {
    j.at("name").get_to(lg.name);
    j.at("levels").get_to(lg.levels);
}

inline void from_json(const nlohmann::json& j, StatValue& sv)
{
    sv.stat = static_cast<D2C_ItemStats>(j.at("stat").get<int>());
    j.at("value").get_to(sv.value);
}

inline void from_json(const nlohmann::json& j, StatAdjustment& sa)
{
    j.at("random").get_to(sa.random);
    j.at("stats_values").get_to(sa.stats_values);

    if (j.contains("minmaxStats") && j["minmaxStats"].is_array() && j["minmaxStats"].size() == 2)
    {
        sa.minStats = j["minmaxStats"][0].get<int>();
        sa.maxStats = j["minmaxStats"][1].get<int>();
    }
    else
    {
        sa.minStats = 0;
        sa.maxStats = 0;
    }
}

struct Config
{
    std::vector<StatAdjustment> stat_adjustments;
};

inline void from_json(const nlohmann::json& j, Config& cfg)
{
    if (j.contains("stat_adjustments"))
        j.at("stat_adjustments").get_to(cfg.stat_adjustments);
    else
        cfg.stat_adjustments.clear();
}

void from_json(const nlohmann::json& j, StatNameEntry& entry) {
    j.at("id").get_to(entry.id);
    j.at("name").get_to(entry.name);
}

struct StatNamesConfig {
    std::vector<StatNameEntry> stat_names;
};

void from_json(const nlohmann::json& j, StatNamesConfig& config) {
    if (j.contains("stat_names") && j["stat_names"].is_array()) {
        config.stat_names = j.at("stat_names").get<std::vector<StatNameEntry>>();
    }
}

void from_json(const json& j, ZoneLevel& zl) {
    if (j.contains("all") && j.at("all").get<bool>() == true) {
        zl.allLevels = true;
        zl.level_id.reset();  // clear any level_id
    }
    else if (j.contains("level_id")) {
        zl.level_id = j.at("level_id").get<int>();
    }
    else {
        throw std::runtime_error("ZoneLevel must have either 'level_id' or 'all: true'");
    }

    // Optional per-difficulty overrides
    if (j.contains("normal")) zl.normal = j.at("normal").get<DifficultySettings>();
    if (j.contains("nightmare")) zl.nightmare = j.at("nightmare").get<DifficultySettings>();
    if (j.contains("hell")) zl.hell = j.at("hell").get<DifficultySettings>();

    if (j.contains("stat_adjustments") && j["stat_adjustments"].is_array()) {
        zl.stat_adjustments = j.at("stat_adjustments").get<std::vector<StatAdjustment>>();
    }
    else {
        zl.stat_adjustments.clear();
    }
}

void from_json(const json& j, ZoneGroup& zg) {
    zg.id = j.at("id").get<int>();
    zg.levels = j.at("levels").get<std::vector<ZoneLevel>>();
}

void from_json(const json& j, DesecratedZone& dz) {
    dz.start_time_utc = parse_time_utc(j.at("start_time_utc").get<std::string>());
    dz.end_time_utc = parse_time_utc(j.at("end_time_utc").get<std::string>());
    dz.terror_duration_min = j.at("terror_duration_min").get<int>();
    dz.terror_break_min = j.at("terror_break_min").get<int>();
    dz.default_normal = j.at("default_normal").get<DifficultySettings>();
    dz.default_nightmare = j.at("default_nightmare").get<DifficultySettings>();
    dz.default_hell = j.at("default_hell").get<DifficultySettings>();
    dz.warnings = j.at("warnings").get<std::vector<WarningInfo>>();
    dz.zones = j.at("zones").get<std::vector<ZoneGroup>>();
    // Optional: seed
    if (j.contains("seed")) {
        dz.seed = j.at("seed").get<uint64_t>();

        if (dz.seed != 0) {
            std::mt19937_64 rng(dz.seed);
            std::shuffle(dz.zones.begin(), dz.zones.end(), rng);
        }
    }
    else {
        dz.seed = 0;
    }
}

#pragma endregion

#pragma region - Load/Save Functions

std::vector<std::string> ReadTCexFile(const std::string& filename)
{
    std::vector<std::string> treasureClasses;
    std::ifstream file(filename);
    if (!file.is_open())
    {
        std::cerr << "Error opening file: " << filename << "\n";
        return treasureClasses;
    }

    std::string line;
    while (std::getline(file, line))
    {
        std::stringstream ss(line);
        std::string firstColumn;
        if (std::getline(ss, firstColumn, '\t'))
            treasureClasses.push_back(firstColumn);
    }
    return treasureClasses;
}

std::vector<MonsterTreasureClass> ReadMonsterTreasureFile(const std::string& filename)
{
    std::vector<MonsterTreasureClass> results;
    std::ifstream file(filename);

    if (!file.is_open())
    {
        std::cerr << "Error: Unable to open file: " << filename << "\n";
        return results;
    }

    std::string line;
    bool isHeader = true;
    int idxMonsterName = -1;
    int idxTCChecker1a = -1;
    int idxTCChecker1b = -1;
    int idxTCChecker1c = -1;
    int idxDesecrated = -1;
    int idxDesecratedChamp = -1;
    int idxDesecratedUnique = -1;
    int idxTCChecker2a = -1;
    int idxTCChecker2b = -1;
    int idxTCChecker2c = -1;
    int idxDesecrated_N = -1;
    int idxDesecratedChamp_N = -1;
    int idxDesecratedUnique_N = -1;
    int idxTCChecker3a = -1;
    int idxTCChecker3b = -1;
    int idxTCChecker3c = -1;
    int idxDesecrated_H = -1;
    int idxDesecratedChamp_H = -1;
    int idxDesecratedUnique_H = -1;

    while (std::getline(file, line))
    {
        std::stringstream ss(line);
        std::vector<std::string> cols;
        std::string cell;

        while (std::getline(ss, cell, '\t'))
            cols.push_back(cell);

        if (isHeader)
        {
            for (size_t i = 0; i < cols.size(); ++i)
            {
                if (cols[i] == "Id") idxMonsterName = static_cast<int>(i);
                else if (cols[i] == "TreasureClass1") idxTCChecker1a = static_cast<int>(i);
                else if (cols[i] == "TreasureClass2") idxTCChecker1b = static_cast<int>(i);
                else if (cols[i] == "TreasureClass3") idxTCChecker1c = static_cast<int>(i);
                else if (cols[i] == "TreasureClassDesecrated") idxDesecrated = static_cast<int>(i);
                else if (cols[i] == "TreasureClassDesecratedChamp") idxDesecratedChamp = static_cast<int>(i);
                else if (cols[i] == "TreasureClassDesecratedUnique") idxDesecratedUnique = static_cast<int>(i);
                else if (cols[i] == "TreasureClass1(N)") idxTCChecker2a = static_cast<int>(i);
                else if (cols[i] == "TreasureClass2(N)") idxTCChecker2b = static_cast<int>(i);
                else if (cols[i] == "TreasureClass3(N)") idxTCChecker2c = static_cast<int>(i);
                else if (cols[i] == "TreasureClassDesecrated(N)") idxDesecrated_N = static_cast<int>(i);
                else if (cols[i] == "TreasureClassDesecratedChamp(N)") idxDesecratedChamp_N = static_cast<int>(i);
                else if (cols[i] == "TreasureClassDesecratedUnique(N)") idxDesecratedUnique_N = static_cast<int>(i);
                else if (cols[i] == "TreasureClass1(H)") idxTCChecker3a = static_cast<int>(i);
                else if (cols[i] == "TreasureClass2(H)") idxTCChecker3b = static_cast<int>(i);
                else if (cols[i] == "TreasureClass3(H)") idxTCChecker3c = static_cast<int>(i);
                else if (cols[i] == "TreasureClassDesecrated(H)") idxDesecrated_H = static_cast<int>(i);
                else if (cols[i] == "TreasureClassDesecratedChamp(H)") idxDesecratedChamp_H = static_cast<int>(i);
                else if (cols[i] == "TreasureClassDesecratedUnique(H)") idxDesecratedUnique_H = static_cast<int>(i);
            }

            isHeader = false;
            continue;
        }

        MonsterTreasureClass entry;

        if (idxMonsterName >= 0 && idxMonsterName < (int)cols.size())
            entry.MonsterName = cols[idxMonsterName];
        if (idxTCChecker1a >= 0 && idxTCChecker1a < (int)cols.size())
            entry.TCChecker1a = cols[idxTCChecker1a];
        if (idxTCChecker1b >= 0 && idxTCChecker1b < (int)cols.size())
            entry.TCChecker1b = cols[idxTCChecker1b];
        if (idxTCChecker1c >= 0 && idxTCChecker1c < (int)cols.size())
            entry.TCChecker1c = cols[idxTCChecker1c];
        if (idxDesecrated >= 0 && idxDesecrated < (int)cols.size())
            entry.Desecrated = cols[idxDesecrated];
        if (idxDesecratedChamp >= 0 && idxDesecratedChamp < (int)cols.size())
            entry.DesecratedChamp = cols[idxDesecratedChamp];
        if (idxDesecratedUnique >= 0 && idxDesecratedUnique < (int)cols.size())
            entry.DesecratedUnique = cols[idxDesecratedUnique];
        if (idxTCChecker2a >= 0 && idxTCChecker2a < (int)cols.size())
            entry.TCChecker2a = cols[idxTCChecker2a];
        if (idxTCChecker2b >= 0 && idxTCChecker2b < (int)cols.size())
            entry.TCChecker2b = cols[idxTCChecker2b];
        if (idxTCChecker2c >= 0 && idxTCChecker2c < (int)cols.size())
            entry.TCChecker2c = cols[idxTCChecker2c];
        if (idxDesecrated_N >= 0 && idxDesecrated_N < (int)cols.size())
            entry.Desecrated_N = cols[idxDesecrated_N];
        if (idxDesecratedChamp_N >= 0 && idxDesecratedChamp_N < (int)cols.size())
            entry.DesecratedChamp_N = cols[idxDesecratedChamp_N];
        if (idxDesecratedUnique_N >= 0 && idxDesecratedUnique_N < (int)cols.size())
            entry.DesecratedUnique_N = cols[idxDesecratedUnique_N];
        if (idxTCChecker3a >= 0 && idxTCChecker3a < (int)cols.size())
            entry.TCChecker3a = cols[idxTCChecker3a];
        if (idxTCChecker3b >= 0 && idxTCChecker3b < (int)cols.size())
            entry.TCChecker3b = cols[idxTCChecker3b];
        if (idxTCChecker3c >= 0 && idxTCChecker3c < (int)cols.size())
            entry.TCChecker3c = cols[idxTCChecker3c];
        if (idxDesecrated_H >= 0 && idxDesecrated_H < (int)cols.size())
            entry.Desecrated_H = cols[idxDesecrated_H];
        if (idxDesecratedChamp_H >= 0 && idxDesecratedChamp_H < (int)cols.size())
            entry.DesecratedChamp_H = cols[idxDesecratedChamp_H];
        if (idxDesecratedUnique_H >= 0 && idxDesecratedUnique_H < (int)cols.size())
            entry.DesecratedUnique_H = cols[idxDesecratedUnique_H];

        results.push_back(entry);
    }

    return results;
}

std::vector<MonsterTreasureClassSU> ReadMonsterTreasureFileSU(const std::string& filename)
{
    std::vector<MonsterTreasureClassSU> results;
    std::ifstream file(filename);

    if (!file.is_open())
    {
        std::cerr << "Error: Unable to open file: " << filename << "\n";
        return results;
    }

    std::string line;
    bool isHeader = true;
    int idxMonsterName = -1;
    int idxBaseMonster = -1;
    int idxTCChecker1 = -1;
    int idxDesecrated = -1;
    int idxTCChecker2 = -1;
    int idxDesecrated_N = -1;
    int idxTCChecker3 = -1;
    int idxDesecrated_H = -1;

    while (std::getline(file, line))
    {
        std::stringstream ss(line);
        std::vector<std::string> cols;
        std::string cell;

        while (std::getline(ss, cell, '\t'))
            cols.push_back(cell);

        if (isHeader)
        {
            for (size_t i = 0; i < cols.size(); ++i)
            {
                if (cols[i] == "Superunique") idxMonsterName = static_cast<int>(i);
                else if (cols[i] == "Class") idxBaseMonster = static_cast<int>(i);
                else if (cols[i] == "TC") idxTCChecker1 = static_cast<int>(i);
                else if (cols[i] == "TC Desecrated") idxDesecrated = static_cast<int>(i);
                else if (cols[i] == "TC(N)") idxTCChecker2 = static_cast<int>(i);
                else if (cols[i] == "TC(N) Desecrated") idxDesecrated_N = static_cast<int>(i);
                else if (cols[i] == "TC(H)") idxTCChecker3 = static_cast<int>(i);
                else if (cols[i] == "TC(H) Desecrated") idxDesecrated_H = static_cast<int>(i);
            }

            isHeader = false;
            continue;
        }

        MonsterTreasureClassSU entry;

        if (idxMonsterName >= 0 && idxMonsterName < (int)cols.size())
            entry.MonsterName = cols[idxMonsterName];
        if (idxBaseMonster >= 0 && idxBaseMonster < (int)cols.size())
            entry.BaseMonster = cols[idxBaseMonster];
        if (idxTCChecker1 >= 0 && idxTCChecker1 < (int)cols.size())
            entry.TCChecker1 = cols[idxTCChecker1];
        if (idxDesecrated >= 0 && idxDesecrated < (int)cols.size())
            entry.Desecrated = cols[idxDesecrated];
        if (idxTCChecker2 >= 0 && idxTCChecker2 < (int)cols.size())
            entry.TCChecker2 = cols[idxTCChecker2];
        if (idxDesecrated_N >= 0 && idxDesecrated_N < (int)cols.size())
            entry.Desecrated_N = cols[idxDesecrated_N];
        if (idxTCChecker3 >= 0 && idxTCChecker3 < (int)cols.size())
            entry.TCChecker3 = cols[idxTCChecker3];
        if (idxDesecrated_H >= 0 && idxDesecrated_H < (int)cols.size())
            entry.Desecrated_H = cols[idxDesecrated_H];

        results.push_back(entry);
    }

    return results;
}

MonsterTreasureResult GetMonsterTreasure(const std::vector<MonsterTreasureClass>& monsters, size_t rowIndex, int diff, int monType, const std::vector<std::string>& tcexEntries)
{
    MonsterTreasureResult result{ -1, -1 };

    if (rowIndex >= monsters.size()) {
        //LogDebug("Error: Row index out of range");
        return result;
    }

    const auto& m = monsters[rowIndex];
    std::string treasureClassValue;
    std::string tcCheck;

    //LogDebug(std::format("---------------------\nMonster: {}", m.MonsterName));
    //LogDebug(std::format("Monstats Row: {}, Difficulty: {}", rowIndex, diff));

    if (rowIndex >= 410)
        rowIndex++;

    if (diff == 0) {
        if (monType == 0) { tcCheck = m.TCChecker1a; treasureClassValue = m.Desecrated; }
        else if (monType == 1) { tcCheck = m.TCChecker1b; treasureClassValue = m.DesecratedChamp; }
        else if (monType == 2) { tcCheck = m.TCChecker1c; treasureClassValue = m.DesecratedUnique; }
    }
    else if (diff == 1) {
        if (monType == 0) { tcCheck = m.TCChecker2a; treasureClassValue = m.Desecrated_N; }
        else if (monType == 1) { tcCheck = m.TCChecker2b; treasureClassValue = m.DesecratedChamp_N; }
        else if (monType == 2) { tcCheck = m.TCChecker2c; treasureClassValue = m.DesecratedUnique_N; }
    }
    else if (diff == 2) {
        if (monType == 0) { tcCheck = m.TCChecker3a; treasureClassValue = m.Desecrated_H; }
        else if (monType == 1) { tcCheck = m.TCChecker3b; treasureClassValue = m.DesecratedChamp_H; }
        else if (monType == 2) { tcCheck = m.TCChecker3c; treasureClassValue = m.DesecratedUnique_H; }
    }

    for (size_t i = 0; i < tcexEntries.size(); ++i) {
        if (tcexEntries[i] == treasureClassValue) {
            result.treasureIndex = static_cast<int>(i) + 1;
            break;
        }
    }

    for (size_t i = 0; i < tcexEntries.size(); ++i) {
        if (tcexEntries[i] == tcCheck) {
            result.tcCheckIndex = static_cast<int>(i) + 1;
            break;
        }
    }

    //LogDebug(std::format("Treasure Class: {}", tcCheck));
    //LogDebug(std::format("TZ Treasure Class: {}", treasureClassValue));
    //LogDebug(std::format("Base TC Row: {}, Terror TC Row: {}", result.tcCheckIndex, result.treasureIndex));

    return result;
}

MonsterTreasureResult GetMonsterTreasureSU(const std::vector<MonsterTreasureClassSU>& monsters, size_t rowIndex, int diff, const std::vector<std::string>& tcexEntries)
{
    MonsterTreasureResult result{ -1, -1 };

    if (rowIndex >= monsters.size()) {
        std::cerr << "Error: Row index out of range\n";
        return result;
    }

    const auto& m = monsters[rowIndex];
    std::string treasureClassValue;
    std::string tcCheck;

    //LogDebug(std::format("---------------------\nSuperUnique: {}", m.BaseMonster));
    //LogDebug(std::format("GetMonsterTreasureSU called with: rowIndex={}", rowIndex));

    if (diff == 0) {
        tcCheck = m.TCChecker1;
        treasureClassValue = m.Desecrated;
    }
    else if (diff == 1) {
        tcCheck = m.TCChecker2;
        treasureClassValue = m.Desecrated_N;
    }
    else if (diff == 2) {
        tcCheck = m.TCChecker3;
        treasureClassValue = m.Desecrated_H;
    }
    else {
        return result;
    }

    for (size_t i = 0; i < tcexEntries.size(); ++i) {
        if (tcexEntries[i] == treasureClassValue) {
            result.treasureIndex = static_cast<int>(i);
            break;
        }
    }

    for (size_t i = 0; i < tcexEntries.size(); ++i) {
        if (tcexEntries[i] == tcCheck) {
            result.tcCheckIndex = static_cast<int>(i);
            break;
        }
    }

    //LogDebug(std::format("SU Treasure Class: {}", tcCheck));
    //LogDebug(std::format("SU TZ Treasure Class: {}", treasureClassValue));
    //LogDebug(std::format("SuperUniques Base TC Row: {}, SuperUniques Terror TC Row: {}", result.tcCheckIndex, result.treasureIndex));

    return result;
}

bool LoadDesecratedZones(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        MessageBoxA(nullptr, "Failed to open desecrated zones config file.", "Error", MB_ICONERROR);
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = StripComments(buffer.str());

    json j;
    try {
        j = json::parse(content);
    }
    catch (const std::exception& e) {
        MessageBoxA(nullptr, ("JSON parse error: " + std::string(e.what())).c_str(), "Error", MB_ICONERROR);
        return false;
    }

    if (!j.contains("desecrated_zones")) {
        MessageBoxA(nullptr, "Unable to locate a valid TZ config", "Error", MB_ICONERROR);
        return false;
    }

    if (!j.contains("level_names")) {
        MessageBoxA(nullptr, "Unable to locate valid level names for TZ", "Error", MB_ICONERROR);
        return false;
    }

    try {
        gDesecratedZones = j.at("desecrated_zones").get<std::vector<DesecratedZone>>();
        level_names = j.at("level_names").get<std::vector<LevelName>>();

        if (j.contains("level_groups") && j["level_groups"].is_array()) {
            level_groups = j.at("level_groups").get<std::vector<LevelGroup>>();
        }
        else {
            level_groups.clear();
        }

        if (j.contains("stat_adjustments") && j["stat_adjustments"].is_array()) {
            gStatAdjustments = j.at("stat_adjustments").get<std::vector<StatAdjustment>>();
        }
        else {
            gStatAdjustments.clear();
        }

        if (j.contains("stat_names") && j["stat_names"].is_array()) {
            std::vector<StatNameEntry> statEntries = j.at("stat_names").get<std::vector<StatNameEntry>>();
            gStatNames.clear();
            for (const auto& entry : statEntries) {
                gStatNames[entry.id] = entry.name;
            }
        }
        else {
            gStatNames.clear();
        }
    }
    catch (const std::exception& e) {
        MessageBoxA(nullptr, ("JSON field parse error: " + std::string(e.what())).c_str(), "Error", MB_ICONERROR);
        return false;
    }

    return true;
}

#pragma endregion

#pragma region - Terror Zone Adjustments

void AdjustMonsterLevel(D2UnitStrc* pUnit, D2C_ItemStats nStatId, uint32_t nValue, uint16_t nLayer = 0) {
    auto monsterLevel = STATLIST_GetUnitStatSigned(pUnit, nStatId, nLayer);

    if (playerLevel >= monsterLevel)
        STATLISTEX_SetStatListExStat(pUnit->pStatListEx, nStatId, nValue, nLayer);
}

void ApplyMonsterDifficultyScaling(D2UnitStrc* pUnit, const DesecratedZone& zone, const ZoneLevel* matchingZoneLevel, int difficulty, int playerLevel, int playerCountGlobal, D2GameStrc* pGame)
{
    // Get global defaults
    const DifficultySettings* globalDefaults = nullptr;
    switch (difficulty) {
    case 0: globalDefaults = &zone.default_normal; break;
    case 1: globalDefaults = &zone.default_nightmare; break;
    case 2: globalDefaults = &zone.default_hell; break;
    default: return; // invalid
    }
    if (!globalDefaults) return;

    // Get optional level-specific override
    const std::optional<DifficultySettings>* levelOverride = nullptr;
    if (matchingZoneLevel) {
        switch (difficulty) {
        case 0: levelOverride = &matchingZoneLevel->normal; break;
        case 1: levelOverride = &matchingZoneLevel->nightmare; break;
        case 2: levelOverride = &matchingZoneLevel->hell; break;
        }
    }

    // Merge values
    int boostLevel = globalDefaults->boost_level.value_or(0);
    int boundMin = globalDefaults->bound_incl_min.value_or(1);
    int boundMax = globalDefaults->bound_incl_max.value_or(99);

    if (levelOverride && levelOverride->has_value()) {
        const DifficultySettings & override = levelOverride->value();
        if (override.boost_level)     boostLevel = override.boost_level.value();
        if (override.bound_incl_min)  boundMin = override.bound_incl_min.value();
        if (override.bound_incl_max)  boundMax = override.bound_incl_max.value();
    }

    // Clamp boosted level and Apply
    int boostedLevel = std::clamp(playerLevel + boostLevel, boundMin, boundMax);

    if (pUnit->dwClassId != 333) //Ignore Diabloclone
        AdjustMonsterLevel(pUnit, STAT_LEVEL, boostedLevel);

    int32_t playerCountModifier = (playerCountGlobal >= 9) ? (playerCountGlobal - 2) * 50 : (playerCountGlobal - 1) * 50;

    // Calculate base monster stats
    D2MonStatsInitStrc monStatsInit = {};

    if (pUnit->dwClassId != 333) //Ignore Diabloclone
        CalculateMonsterStats(pUnit->dwClassId, 1, pGame->nDifficulty, STATLIST_GetUnitStatSigned(pUnit, STAT_LEVEL, 0), 7, monStatsInit);

    const int32_t nBaseHp = monStatsInit.nMinHP + ITEMS_RollLimitedRandomNumber(&pUnit->pSeed, monStatsInit.nMaxHP - monStatsInit.nMinHP + 1);
    const int32_t nHp = nBaseHp + D2_ComputePercentage(nBaseHp, playerCountModifier);
    const int32_t nShiftedHp = nHp << 8;

    // Apply core stats
    SetStat(pUnit, STAT_MAXHP, nShiftedHp);
    SetStat(pUnit, STAT_HITPOINTS, nShiftedHp);
    SetStat(pUnit, STAT_ARMORCLASS, monStatsInit.nAC);
    SetStat(pUnit, STAT_EXPERIENCE, D2_ComputePercentage(monStatsInit.nExp, ((playerCountGlobal - 8) * 100) / 5));

    if (pUnit->dwClassId != 156 && pUnit->dwClassId != 211 && pUnit->dwClassId != 242 && pUnit->dwClassId != 243 && pUnit->dwClassId != 544) //Ignore Act Bosses
        SetStat(pUnit, STAT_HPREGEN, (nShiftedHp * 2) >> 12);
}

void ApplyMonsterDifficultyScalingNonTZ(D2UnitStrc* pUnit, int difficulty, int playerLevel, int playerCountGlobal, D2GameStrc* pGame)
{
    int32_t playerCountModifier = (playerCountGlobal >= 9) ? (playerCountGlobal - 2) * 50 : (playerCountGlobal - 1) * 50;

    // Calculate base monster stats
    D2MonStatsInitStrc monStatsInit = {};
    CalculateMonsterStats(pUnit->dwClassId, 1, pGame->nDifficulty, STATLIST_GetUnitStatSigned(pUnit, STAT_LEVEL, 0), 7, monStatsInit);
    int32_t nBaseHp = 0;

    D2UnitStrc* pUnitPlayer = UNITS_GetServerUnitByTypeAndId(pGame, UNIT_PLAYER, 1);

    if (difficulty == 0)
    {
        if (pUnit->pMonsterData->nTypeFlag == MONTYPEFLAG_UNIQUE)
            nBaseHp = monStatsInit.nMinHP + ITEMS_RollLimitedRandomNumber(&pUnit->pSeed, monStatsInit.nMaxHP - monStatsInit.nMinHP + 1) * 4;
        else if (pUnit->pMonsterData->nTypeFlag == MONTYPEFLAG_CHAMPION)
            nBaseHp = monStatsInit.nMinHP + ITEMS_RollLimitedRandomNumber(&pUnit->pSeed, monStatsInit.nMaxHP - monStatsInit.nMinHP + 1) * 3;
        else if (pUnit->pMonsterData->nTypeFlag == MONTYPEFLAG_MINION)
            nBaseHp = monStatsInit.nMinHP + ITEMS_RollLimitedRandomNumber(&pUnit->pSeed, monStatsInit.nMaxHP - monStatsInit.nMinHP + 1) * 2;
        else
            nBaseHp = monStatsInit.nMinHP + ITEMS_RollLimitedRandomNumber(&pUnit->pSeed, monStatsInit.nMaxHP - monStatsInit.nMinHP + 1);
    }
    if (difficulty == 1)
    {
        if (pUnit->pMonsterData->nTypeFlag == MONTYPEFLAG_UNIQUE)
            nBaseHp = monStatsInit.nMinHP + ITEMS_RollLimitedRandomNumber(&pUnit->pSeed, monStatsInit.nMaxHP - monStatsInit.nMinHP + 1) * 3;
        else if (pUnit->pMonsterData->nTypeFlag == MONTYPEFLAG_CHAMPION)
            nBaseHp = monStatsInit.nMinHP + ITEMS_RollLimitedRandomNumber(&pUnit->pSeed, monStatsInit.nMaxHP - monStatsInit.nMinHP + 1) * 2.5;
        else if (pUnit->pMonsterData->nTypeFlag == MONTYPEFLAG_MINION)
            nBaseHp = monStatsInit.nMinHP + ITEMS_RollLimitedRandomNumber(&pUnit->pSeed, monStatsInit.nMaxHP - monStatsInit.nMinHP + 1) * 1.75;
        else
            nBaseHp = monStatsInit.nMinHP + ITEMS_RollLimitedRandomNumber(&pUnit->pSeed, monStatsInit.nMaxHP - monStatsInit.nMinHP + 1);
    }
    if (difficulty == 2)
    {
        if (pUnit->pMonsterData->nTypeFlag == MONTYPEFLAG_UNIQUE)
            nBaseHp = monStatsInit.nMinHP + ITEMS_RollLimitedRandomNumber(&pUnit->pSeed, monStatsInit.nMaxHP - monStatsInit.nMinHP + 1) * 1;
        else if (pUnit->pMonsterData->nTypeFlag == MONTYPEFLAG_CHAMPION)
            nBaseHp = monStatsInit.nMinHP + ITEMS_RollLimitedRandomNumber(&pUnit->pSeed, monStatsInit.nMaxHP - monStatsInit.nMinHP + 1) * 1;
        else if (pUnit->pMonsterData->nTypeFlag == MONTYPEFLAG_MINION)
            nBaseHp = monStatsInit.nMinHP + ITEMS_RollLimitedRandomNumber(&pUnit->pSeed, monStatsInit.nMaxHP - monStatsInit.nMinHP + 1) * 1;
        else
            nBaseHp = monStatsInit.nMinHP + ITEMS_RollLimitedRandomNumber(&pUnit->pSeed, monStatsInit.nMaxHP - monStatsInit.nMinHP + 1);
    }


    const int32_t nHp = nBaseHp + D2_ComputePercentage(nBaseHp, playerCountModifier);
    const int32_t nShiftedHp = nHp << 8;

    // Apply core stats
    SetStat(pUnit, STAT_MAXHP, nShiftedHp);
    SetStat(pUnit, STAT_HITPOINTS, nShiftedHp);
    SetStat(pUnit, STAT_ARMORCLASS, monStatsInit.nAC);
    SetStat(pUnit, STAT_EXPERIENCE, D2_ComputePercentage(monStatsInit.nExp, ((playerCountGlobal - 8) * 100) / 5));

    if (pUnit->dwClassId != 156 && pUnit->dwClassId != 211 && pUnit->dwClassId != 242 && pUnit->dwClassId != 243 && pUnit->dwClassId != 544) //Ignore Act Bosses
        SetStat(pUnit, STAT_HPREGEN, (nShiftedHp * 2) >> 12);
}

void ApplyStatsToMonster(D2UnitStrc* pUnit)
{
    std::ostringstream msgStream;

    for (const auto& [stat, value] : gRandomStatsForMonsters)
    {
        AddToCurrentStat(pUnit, stat, value);

        msgStream << "Stat ID " << static_cast<int>(stat)
            << " Applied Value: " << value << "\n";
    }
}

void InitRandomStatsForAllMonsters(bool forceNew = false) {
    static bool initialized = false;
    static std::random_device rd;
    static std::mt19937 rng(rd());

    if (!initialized || forceNew) {
        gRandomStatsForMonsters.clear();

        auto processAdjustments = [&](const std::vector<StatAdjustment>& adjustments) {
            for (const auto& adjustment : adjustments) {
                std::vector<const StatValue*> statsToApply;

                if (adjustment.random) {
                    size_t available = adjustment.stats_values.size();
                    size_t lower = 1;
                    size_t upper = available;

                    if (adjustment.minStats > 0 && static_cast<size_t>(adjustment.minStats) <= available)
                        lower = adjustment.minStats;

                    if (adjustment.maxStats > 0 && static_cast<size_t>(adjustment.maxStats) < upper)
                        upper = adjustment.maxStats;

                    if (lower > upper)
                        lower = upper;

                    std::uniform_int_distribution<size_t> countDist(lower, upper);
                    size_t numStats = countDist(rng);

                    std::vector<const StatValue*> shuffled;
                    shuffled.reserve(available);
                    for (const auto& sv : adjustment.stats_values)
                        shuffled.push_back(&sv);

                    std::shuffle(shuffled.begin(), shuffled.end(), rng);
                    statsToApply.assign(shuffled.begin(), shuffled.begin() + numStats);
                }
                else {
                    for (const auto& sv : adjustment.stats_values)
                        statsToApply.push_back(&sv);
                }

                for (const auto* statVal : statsToApply) {
                    int appliedValue = 0;

                    if (!statVal->value.empty()) {
                        if (statVal->value.size() == 1) {
                            appliedValue = statVal->value[0];
                        }
                        else {
                            int minVal = *std::min_element(statVal->value.begin(), statVal->value.end());
                            int maxVal = *std::max_element(statVal->value.begin(), statVal->value.end());
                            std::uniform_int_distribution<int> valueDist(minVal, maxVal);

                            do {
                                appliedValue = valueDist(rng);
                            } while (appliedValue == 0);
                        }
                    }

                    if (appliedValue != 0) {
                        gRandomStatsForMonsters[statVal->stat] = appliedValue;
                    }
                }
            }
            };

        processAdjustments(gStatAdjustments);

        // Apply per-level stat adjustments for all zones
        for (const auto& dz : gDesecratedZones) {
            for (const auto& zg : dz.zones) {
                for (const auto& lvl : zg.levels) {
                    if (!lvl.stat_adjustments.empty()) {
                        processAdjustments(lvl.stat_adjustments);
                    }
                }
            }
        }

        initialized = true;
    }
}

void __fastcall ForceTCDrops(D2GameStrc* pGame, D2UnitStrc* pMonster, D2UnitStrc* pPlayer, int32_t nTCId, int32_t nQuality, int32_t nItemLevel, int32_t a7, D2UnitStrc** ppItems, int32_t* pnItemsDropped, int32_t nMaxItems)
{
    // Cached file data
    static bool filesLoaded = false;
    static decltype(ReadMonsterTreasureFile("")) monsters;
    static decltype(ReadMonsterTreasureFileSU("")) superuniques;
    static std::vector<std::string> TCEx;

    if (!filesLoaded)
    {
        std::string basePath = std::format("{}/Mods/{}/{}.mpq/data/global/excel/", GetExecutableDir(), GetModName(), GetModName());
        std::string MONFile = basePath + "monstats.txt";
        std::string SUFile = basePath + "superuniques.txt";
        std::string TCEXFile = basePath + "treasureclassex.txt";

        monsters = ReadMonsterTreasureFile(MONFile);
        superuniques = ReadMonsterTreasureFileSU(SUFile);
        TCEx = ReadTCexFile(TCEXFile);

        filesLoaded = true;
    }

    D2MonStatsTxt* pMonStatsTxtRecord = MONSTERMODE_GetMonStatsTxtRecord(pMonster->dwClassId);
    if (!pMonStatsTxtRecord)
    {
        MessageBoxA(nullptr, "Failed to get monster stats record.", "Debug", MB_OK | MB_ICONERROR);
        return;
    }

    auto pMonsterFlag = pMonster->pMonsterData->nTypeFlag;
    const int32_t nSuperUniqueId = MONSTERUNIQUE_GetSuperUniqueBossHcIdx(pGame, pMonster);

    if (pMonStatsTxtRecord->nId >= monsters.size())
    {
        MessageBoxA(nullptr, "Monster ID out of range in monsters vector.", "Debug", MB_OK | MB_ICONERROR);
        return;
    }

    D2UnitStrc* pUnitPlayer = UNITS_GetServerUnitByTypeAndId(pGame, UNIT_PLAYER, 1);
    if (!pUnitPlayer)
    {
        MessageBoxA(nullptr, "Failed to get player unit.", "Debug", MB_OK | MB_ICONERROR);
        return;
    }

    int difficulty = GetPlayerDifficulty(pUnitPlayer);
    if (difficulty < 0 || difficulty > 2)
    {
        MessageBoxA(nullptr, "Invalid difficulty detected.", "Debug", MB_OK | MB_ICONERROR);
        return;
    }

    // Adjust for Expansion Row
    int adjustedSID = (nSuperUniqueId >= 42) ? nSuperUniqueId + 1 : nSuperUniqueId;
    int adjustedNID = (pMonStatsTxtRecord->nId >= 410) ? pMonStatsTxtRecord->nId + 1 : pMonStatsTxtRecord->nId;

    MonsterTreasureResult regResult = GetMonsterTreasure(monsters, adjustedNID, difficulty, 0, TCEx);
    MonsterTreasureResult champResult = GetMonsterTreasure(monsters, adjustedNID, difficulty, 1, TCEx);
    MonsterTreasureResult uniqResult = GetMonsterTreasure(monsters, adjustedNID, difficulty, 2, TCEx);
    MonsterTreasureResult superuniqResult = GetMonsterTreasureSU(superuniques, adjustedSID, difficulty, TCEx);

    // Base TC
    int tcCheckRegular = regResult.tcCheckIndex;
    int tcCheckChamp = champResult.tcCheckIndex;
    int tcCheckUnique = uniqResult.tcCheckIndex;
    int tcCheckSuperUnique = superuniqResult.tcCheckIndex;

    // Terror TC
    int indexRegular = (regResult.treasureIndex == -1) ? tcCheckRegular : regResult.treasureIndex;
    int indexChamp = (champResult.treasureIndex == -1) ? tcCheckChamp : champResult.treasureIndex;
    int indexUnique = (uniqResult.treasureIndex == -1) ? tcCheckUnique : uniqResult.treasureIndex;
    int indexSuperUnique = (superuniqResult.treasureIndex == -1) ? tcCheckSuperUnique : superuniqResult.treasureIndex;

    //LogDebug(std::format("---------------------\nnTCId: {}, indexRegular: {}, indexChamp: {},  indexUnique: {}, indexSuperUnique: {},", nTCId, indexRegular, indexChamp, indexUnique, indexSuperUnique));

    if (nTCId == 0)
        oDropTCTest(pGame, pMonster, pPlayer, nTCId, nQuality, nItemLevel, a7, ppItems, pnItemsDropped, nMaxItems);

    if ((regResult.treasureIndex == -1 || champResult.treasureIndex == -1 || uniqResult.treasureIndex == -1) && superuniqResult.treasureIndex == -1)
        oDropTCTest(pGame, pMonster, pPlayer, nTCId, nQuality, nItemLevel, a7, ppItems, pnItemsDropped, nMaxItems);

    else
    {
        // Force Boss Drops
        if (pMonStatsTxtRecord->nId == 156 || pMonStatsTxtRecord->nId == 211 || pMonStatsTxtRecord->nId == 242 || pMonStatsTxtRecord->nId == 243 || pMonStatsTxtRecord->nId == 544)
        {
            nTCId = nTCId + (uniqResult.treasureIndex - tcCheckUnique);
            oDropTCTest(pGame, pMonster, pPlayer, nTCId, nQuality, nItemLevel, a7, ppItems, pnItemsDropped, nMaxItems);
            //LogDebug(std::format("nTCId Applied to Monster: {}\n---------------------\n", nTCId));
        }
        else
        {
            if (pMonsterFlag & MONTYPEFLAG_SUPERUNIQUE)
                nTCId = nTCId + (superuniqResult.treasureIndex - tcCheckSuperUnique);
            else if (pMonsterFlag & (MONTYPEFLAG_CHAMPION | MONTYPEFLAG_POSSESSED | MONTYPEFLAG_GHOSTLY))
                nTCId = nTCId + (champResult.treasureIndex - tcCheckChamp);
            else if ((pMonsterFlag & MONTYPEFLAG_UNIQUE) || ((settings.minionEquality || cachedSettings.minionEquality) && (pMonsterFlag & MONTYPEFLAG_MINION)))
                nTCId = nTCId + (uniqResult.treasureIndex - tcCheckUnique);
            else nTCId = nTCId + (regResult.treasureIndex - tcCheckRegular);

            //LogDebug(std::format("nTCId Applied to Monster: {}\n---------------------\n", nTCId));
            oDropTCTest(pGame, pMonster, pPlayer, nTCId, nQuality, nItemLevel, a7, ppItems, pnItemsDropped, nMaxItems);
        }
    }

}

void UpdateActiveZoneInfoText(time_t currentUtc)
{
    g_ActiveZoneInfoText.clear();
    g_TerrorZoneData.activeLevels.clear();

    std::stringstream ss;

    for (const auto& zone : gDesecratedZones)
    {
        if (currentUtc < zone.start_time_utc || currentUtc > zone.end_time_utc)
            continue;

        int groupCount = static_cast<int>(zone.zones.size());
        if (groupCount == 0)
            continue;

        int cycleLengthMin = zone.terror_duration_min + zone.terror_break_min;
        if (cycleLengthMin <= 0)
            continue;

        int activeGroupIndex = g_ManualZoneGroupOverride;
        if (activeGroupIndex == -1)
        {
            int minutesSinceStart = static_cast<int>((currentUtc - zone.start_time_utc) / 60);
            int cyclePos = minutesSinceStart % (cycleLengthMin * groupCount);
            activeGroupIndex = cyclePos / cycleLengthMin;
        }
        else if (activeGroupIndex >= groupCount)
        {
            activeGroupIndex = groupCount - 1;
        }

        if (activeGroupIndex >= groupCount)
            continue;

        const ZoneGroup& activeGroup = zone.zones[activeGroupIndex];

        g_TerrorZoneData.cycleLengthMin = cycleLengthMin;
        g_TerrorZoneData.terrorDurationMin = zone.terror_duration_min;
        g_TerrorZoneData.groupCount = groupCount;
        g_TerrorZoneData.activeGroupIndex = activeGroupIndex;
        g_TerrorZoneData.zoneStartUtc = zone.start_time_utc;

        bool hasAllLevels = false;
        for (const auto& zl : activeGroup.levels)
        {
            if (zl.allLevels)
            {
                hasAllLevels = true;
                break;
            }
        }

        if (hasAllLevels)
        {
            ss << "All Levels have been Terrorized!\n";
            continue; // don't break, continue to next zone
        }

        // Build set of active level IDs
        std::unordered_set<int> activeLevelIds;
        for (const auto& zl : activeGroup.levels)
        {
            if (zl.level_id.has_value())
                activeLevelIds.insert(zl.level_id.value());
        }

        // Map level ID -> group index
        std::unordered_map<int, int> levelToGroupIndex;
        for (size_t gi = 0; gi < level_groups.size(); ++gi)
        {
            for (int lvlId : level_groups[gi].levels)
                levelToGroupIndex[lvlId] = static_cast<int>(gi);
        }

        // Determine fully present groups
        std::vector<bool> groupFullyPresent(level_groups.size(), false);
        for (size_t gi = 0; gi < level_groups.size(); ++gi)
        {
            const auto& grp = level_groups[gi];
            bool allPresent = !grp.levels.empty();
            for (int lvlId : grp.levels)
            {
                if (activeLevelIds.find(lvlId) == activeLevelIds.end())
                {
                    allPresent = false;
                    break;
                }
            }
            groupFullyPresent[gi] = allPresent;
        }

        // Keep track of levels already printed via a fully present group
        std::unordered_set<int> printedLevelIds;

        // Print fully present groups first
        for (size_t gi = 0; gi < level_groups.size(); ++gi)
        {
            if (groupFullyPresent[gi])
            {
                ss << level_groups[gi].name << "\n";
                // mark all levels in this group as printed
                for (int lvlId : level_groups[gi].levels)
                    printedLevelIds.insert(lvlId);
            }
        }

        // Print individual levels not part of fully present groups
        for (const auto& zl : activeGroup.levels)
        {
            g_TerrorZoneData.activeLevels.push_back(zl);

            if (!zl.level_id.has_value())
                continue;

            int lvlId = zl.level_id.value();

            if (printedLevelIds.find(lvlId) != printedLevelIds.end())
                continue; // already printed via a fully present group

            auto it = std::find_if(level_names.begin(), level_names.end(),
                [&](const LevelName& ln) { return ln.id == lvlId; });

            if (it != level_names.end())
                ss << it->name << "\n";
            else
                ss << "(Unknown Level ID: " << lvlId << ")\n";
        }
    }

    g_ActiveZoneInfoText = ss.str();
}

void __fastcall ApplyGhettoTerrorZone(D2GameStrc* pGame, D2ActiveRoomStrc* pRoom, D2UnitStrc* pUnit, int64_t* pMonRegData, D2MonStatsInitStrc* monStatsInit)
{
    time_t currentUtc = std::time(nullptr);
    g_ActiveZoneInfoText.clear();

    if (!pGame || !pRoom || !pUnit)
    {
        isTerrorized = false;
        return;
    }

    int levelId = GetLevelIdFromRoom(pRoom);
    if (levelId == -1)
    {
        isTerrorized = false;
        return;
    }

    D2UnitStrc* pUnitPlayer = UNITS_GetServerUnitByTypeAndId(pGame, UNIT_PLAYER, 1);
    if (!pUnitPlayer)
    {
        isTerrorized = false;
        return;
    }

    playerLevel = STATLIST_GetUnitStatSigned(pUnitPlayer, STAT_LEVEL, 0);
    int difficulty = GetPlayerDifficulty(pUnitPlayer);
    if (difficulty < 0 || difficulty > 2)
    {
        isTerrorized = false;
        return;
    }

    // Loop through desecrated zones
    for (const auto& zone : gDesecratedZones)
    {
        if (currentUtc < zone.start_time_utc || currentUtc > zone.end_time_utc)
            continue;

        int groupCount = static_cast<int>(zone.zones.size());
        if (groupCount == 0)
            continue;

        int cycleLengthMin = zone.terror_duration_min + zone.terror_break_min;
        if (cycleLengthMin <= 0)
            continue;

        int minutesSinceStart = static_cast<int>((currentUtc - zone.start_time_utc) / 60);
        int totalCycle = cycleLengthMin * groupCount;
        int cyclePos = minutesSinceStart % totalCycle;

        int activeGroupIndex = (g_ManualZoneGroupOverride == -1) ? (cyclePos / cycleLengthMin) : g_ManualZoneGroupOverride;

        int posWithinGroup = cyclePos % cycleLengthMin;
        if (activeGroupIndex < 0 || activeGroupIndex >= groupCount)
            continue;

        // Only active during terror duration, not break
        bool isInActivePhase = (posWithinGroup < zone.terror_duration_min);
        if (!isInActivePhase)
        {
            isTerrorized = false;
            continue;
        }

        const ZoneGroup& activeGroup = zone.zones[activeGroupIndex];

        // Store terror zone timing info
        g_TerrorZoneData.cycleLengthMin = cycleLengthMin;
        g_TerrorZoneData.terrorDurationMin = zone.terror_duration_min;
        g_TerrorZoneData.groupCount = groupCount;
        g_TerrorZoneData.activeGroupIndex = activeGroupIndex;
        g_TerrorZoneData.zoneStartUtc = zone.start_time_utc;
        int totalSecondsInPhase = g_TerrorZoneData.terrorDurationMin * 60;
        int secondsIntoPhase = (currentUtc - g_TerrorZoneData.zoneStartUtc) % (g_TerrorZoneData.cycleLengthMin * 60) % totalSecondsInPhase;
        int secondsRemaining = totalSecondsInPhase - secondsIntoPhase;
        int remainingMinutes = secondsRemaining / 60;
        int remainingSeconds = secondsRemaining % 60;

        double now = static_cast<double>(std::time(nullptr));
        UpdateActiveZoneInfoText(static_cast<time_t>(now));
        InitRandomStatsForAllMonsters(false);


        // Match level overrides
        const ZoneLevel* matchingZoneLevel = nullptr;
        for (const auto& zl : activeGroup.levels)
        {
            if (zl.allLevels || (zl.level_id.has_value() && zl.level_id.value() == levelId))
            {
                matchingZoneLevel = &zl;
                isTerrorized = true;

                ApplyMonsterDifficultyScaling(pUnit, zone, matchingZoneLevel, difficulty, playerLevel, playerCountGlobal, pGame);
                ApplyStatsToMonster(pUnit);
                break;
            }
        }

        if (!matchingZoneLevel)
        {
            isTerrorized = false;
            continue;
        }

        return; // success
    }

}

#pragma endregion

#pragma region - Terror Zone Controls

static void ToggleManualZoneGroupInternal(bool forward)
{
    std::string path = std::format("{0}/Mods/{1}/{1}.mpq/data/hd/global/excel/desecratedzones.json", GetExecutableDir(), GetModName());
    LoadDesecratedZones(path);
    double now = static_cast<double>(std::time(nullptr));

    if ((now - gLastManualToggleTime) <= 0.3)
        return;

    gLastManualToggleTime = now;
    int maxGroups = 0;

    // Find the first active zone with groups
    for (const auto& zone : gDesecratedZones)
    {
        if (now < zone.start_time_utc || now > zone.end_time_utc)
            continue;

        maxGroups = static_cast<int>(zone.zones.size());
        break;
    }

    if (maxGroups == 0)
        return; // no active groups

    if (g_ManualZoneGroupOverride == -1)
        g_ManualZoneGroupOverride = forward ? 0 : maxGroups - 1;
    else
    {
        if (forward)
        {
            g_ManualZoneGroupOverride++;
            if (g_ManualZoneGroupOverride >= maxGroups)
                g_ManualZoneGroupOverride = 0; // wrap to first
        }
        else
        {
            g_ManualZoneGroupOverride--;
            if (g_ManualZoneGroupOverride < 0)
                g_ManualZoneGroupOverride = maxGroups - 1; // wrap to last
        }
    }

    InitRandomStatsForAllMonsters(true);
    UpdateActiveZoneInfoText(static_cast<time_t>(now));
}

void CheckToggleForward()
{
    ToggleManualZoneGroupInternal(true);
}

void CheckToggleBackward()
{
    ToggleManualZoneGroupInternal(false);
}

std::string BuildTerrorZoneStatAdjustmentsText()
{
    if (initialized == false)
    {
        InitRandomStatsForAllMonsters(true);
        initialized = true;
    }

    if (gRandomStatsForMonsters.empty() || showStatAdjusts == false)
        return "";

    std::string finalText = "Monster Stat Adjustments:\n";

    for (const auto& [statID, value] : gRandomStatsForMonsters)
    {
        std::string statFormat;
        auto it = gStatNames.find(statID);

        if (it != gStatNames.end())
            statFormat = it->second;
        else
            statFormat = "Unknown Stat (%d)";

        if (value < 0) {
            for (char& c : statFormat) {
                if (c == '+') { c = '-'; break; }
                if (c == '-') { c = '+'; break; }
            }
        }

        size_t pos = statFormat.find("%d");
        if (pos != std::string::npos)
        {
            statFormat.replace(pos, 2, std::to_string(std::abs(value)));
        }

        finalText += statFormat + "\n";
    }

    return finalText;
}

std::string BuildTerrorZoneInfoText()
{
    if (g_ActiveZoneInfoText.empty())
        return "";

    time_t currentUtc = std::time(nullptr);
    int remainingSeconds = 0;

    if (g_TerrorZoneData.cycleLengthMin > 0 &&
        g_TerrorZoneData.groupCount > 0 &&
        g_TerrorZoneData.activeGroupIndex >= 0)
    {
        int totalCycleMinutes = g_TerrorZoneData.cycleLengthMin * g_TerrorZoneData.groupCount;
        int minutesSinceStart = static_cast<int>((currentUtc - g_TerrorZoneData.zoneStartUtc) / 60);
        int cyclePos = minutesSinceStart % totalCycleMinutes;
        int positionInCycle = cyclePos % g_TerrorZoneData.cycleLengthMin;

        if (positionInCycle < g_TerrorZoneData.terrorDurationMin)
        {
            // In terror phase
            int totalSecondsInPhase = g_TerrorZoneData.terrorDurationMin * 60;
            int secondsIntoPhase = (currentUtc - g_TerrorZoneData.zoneStartUtc) % (g_TerrorZoneData.cycleLengthMin * 60) % totalSecondsInPhase;
            remainingSeconds = totalSecondsInPhase - secondsIntoPhase;

            if (remainingSeconds == g_TerrorZoneData.terrorDurationMin * 60)
                ToggleManualZoneGroupInternal(true);
        }
        else
        {
            // In break phase
            int totalSecondsInCycle = g_TerrorZoneData.cycleLengthMin * 60;
            int secondsIntoCycle = (currentUtc - g_TerrorZoneData.zoneStartUtc) % totalSecondsInCycle;
            remainingSeconds = totalSecondsInCycle - secondsIntoCycle;
        }
    }

    // Convert remaining seconds into DHMS
    int days = remainingSeconds / 86400;
    remainingSeconds %= 86400;
    int hours = remainingSeconds / 3600;
    remainingSeconds %= 3600;
    int minutes = remainingSeconds / 60;
    int seconds = remainingSeconds % 60;

    std::string timeText;
    if (days > 0)
        timeText += std::to_string(days) + "d ";
    if (hours > 0 || days > 0)
        timeText += std::to_string(hours) + "h ";
    timeText += std::to_string(minutes) + "m " + std::to_string(seconds) + "s";

    std::string finalText = g_ActiveZoneInfoText + "Next Rotation In: " + timeText + "\n";

    return finalText;
}

#pragma endregion

#pragma region Grail Tracker

#pragma region - Static/Structs

std::vector<UniqueItemEntry> g_UniqueItems;
std::vector<SetItemEntry>    g_SetItems;
static std::unordered_set<std::string> g_ExcludedGrailItems;
static bool autoBackups = false;
static bool backupWithTimestamps = false;
static bool overwriteOldBackup = true;
static int backupIntervalMinutes = 10;
static bool triggerBackupNow = false;
static std::mutex backupMutex;
static char backupPath[260] = "C:\\MyGrailBackup";

#pragma endregion

#pragma region - RMD/Retail Grail Data

static UniqueItemEntry g_StaticUniqueItemsRMD[] = {
{ 0, 0, "Amulet of the Viper", "vip", "Amulet", false }, { 1, 1, "Staff of Kings", "msf", "Staff", false }, { 2, 2, "Horadric Staff", "hst", "Staff", false }, { 3, 3, "Hell Forge Hammer", "hfh", "Hammer", false }, { 4, 4, "KhalimFlail", "qf1", "Flail", false },
{ 5, 5, "SuperKhalimFlail", "qf2", "Flail", false }, { 6, 6, "The Gnasher", "hax", "Hand Axe", false }, { 7, 7, "Deathspade", "axe", "Axe", false }, { 8, 8, "Bladebone", "2ax", "Double Axe", false }, { 9, 9, "Skull Splitter", "mpi", "Sickle (necro)", false },
{ 10, 10, "Rakescar", "wax", "War Axe", false }, { 11, 11, "Fechmars Axe", "lax", "Large Axe", false }, { 12, 12, "Goreshovel", "bax", "Broad Axe", false }, { 13, 13, "The Chieftan", "btx", "Battle Axe", false }, { 14, 14, "Brainhew", "gax", "Great Axe (barb)", false },
{ 15, 15, "The Humongous", "gix", "Giant Axe(Barb)", false }, { 16, 16, "Iros Torch", "wnd", "Wand", false }, { 17, 17, "Maelstromwrath", "ywn", "Yew Wand", false }, { 18, 18, "Gravenspine", "bwn", "Bone Wand", false }, { 19, 19, "Umes Lament", "gwn", "Grim Wand", false },
{ 20, 20, "Felloak", "clb", "Branch (Druid)", false }, { 21, 21, "Knell Striker", "scp", "Scepter", false }, { 22, 22, "Rusthandle", "gsc", "Grand Scepter", false }, { 23, 23, "Stormeye", "wsp", "War Scepter", false }, { 24, 24, "Stoutnail", "spc", "Spiked Club", false },
{ 25, 25, "Crushflange", "mac", "Mace", false }, { 26, 26, "Bloodrise", "mst", "Morning Star", false }, { 27, 27, "The Generals Tan Do Li Ga", "fla", "Flail", false }, { 28, 28, "Ironstone", "whm", "War Hammer", false }, { 29, 29, "Bonesnap", "mau", "Maul (Barb)", false },
{ 30, 30, "Steeldriver", "gma", "Great Maul (Barb)", false }, { 31, 31, "Rixots Keen", "ssd", "Short Sword", false }, { 32, 32, "Blood Crescent", "scm", "Scimitar", false }, { 33, 33, "Krintizs Skewer", "sbr", "Saber", false }, { 34, 34, "Gleamscythe", "flc", "Falchion", false },
{ 35, 35, "Light's Beacon", "crs", "Crystal Sword (Sorc)", false }, { 36, 36, "Griswold's Edge", "bsd", "Broad Sword", false }, { 37, 37, "Hellplague", "lsd", "Long Sword", false }, { 38, 38, "Culwens Point", "wsd", "War Sword", false }, { 39, 39, "Shadowfang", "2hs", "Katana (Assassin)", false },
{ 40, 40, "Soulflay", "clm", "Claymore", false }, { 41, 41, "Kinemils Awl", "gis", "Giant Sword", false }, { 42, 42, "Blacktongue", "bsw", "Bastard Sword", false }, { 43, 43, "Ripsaw", "flb", "Flamberge", false }, { 44, 44, "The Patriarch", "gsd", "Great Sword", false },
{ 45, 45, "Gull", "dgr", "Dagger", false }, { 46, 46, "The Diggler", "dir", "Dirk", false }, { 47, 47, "The Jade Tan Do", "kri", "Kris (Necro)", false }, { 48, 48, "Irices Shard", "bld", "Blade (Necro)", false }, { 49, 49, "Shadow Strike", "tkf", "Throwing Knife", false },
{ 50, 50, "Madawc's First", "tax", "Throwing Axe", false }, { 51, 51, "Carefully", "bkf", "Balanced Knife", false }, { 52, 52, "Ancient's Assualt", "bal", "Balanced Axe", false }, { 53, 53, "Harpoonist's Training", "jav", "Javelin", false }, { 54, 54, "Glorious Point", "pil", "Pilum", false },
{ 55, 55, "Not So", "ssp", "Short Spear", false }, { 56, 56, "Double Trouble", "glv", "Glaive", false }, { 57, 57, "Straight Shot", "tsp", "Throwing Spear", false }, { 58, 58, "The Dragon Chang", "spr", "Spear (Pole Now)", false }, { 59, 59, "Razortine", "tri", "Trident (Pole Now)", false },
{ 60, 60, "Bloodthief", "brn", "Brandistock(Pole Now)", false }, { 61, 61, "Lance of Yaggai", "spt", "Spetum(Pole Now)", false }, { 62, 62, "The Tannr Gorerod", "pik", "Pike(Pole Now)", false }, { 63, 63, "Dimoaks Hew", "bar", "Bardiche", false }, { 64, 64, "Steelgoad", "vou", "Voulge", false },
{ 65, 65, "Soul Harvest", "scy", "Scythe (Necro)", false }, { 66, 66, "The Battlebranch", "pax", "Poleaxe", false }, { 67, 67, "Woestave", "hal", "Halberd", false }, { 68, 68, "The Grim Reaper", "wsc", "Thresher (Necro)", false }, { 69, 69, "Bane Ash", "sst", "Short Staff", false },
{ 70, 70, "Serpent Lord", "lst", "Long Staff", false }, { 71, 71, "Lazarus Spire", "cst", "Gnarled Staff", false }, { 72, 72, "The Salamander", "bst", "Battle Staff", false }, { 73, 73, "The Iron Jang Bong", "wst", "War Staff", false }, { 74, 74, "Pluckeye", "sbw", "Short Bow", false },
{ 75, 75, "Witherstring", "hbw", "Hunter's Bow", false }, { 76, 76, "Rimeraven", "lbw", "Long Bow", false }, { 77, 77, "Piercerib", "cbw", "Composite Bow", false }, { 78, 78, "Pullspite", "sbb", "Short Battle Bow", false }, { 79, 79, "Wizendraw", "lbb", "Long Battle Bow", false },
{ 80, 80, "Hellclap", "swb", "Short War Bow", false }, { 81, 81, "Blastbark", "lwb", "Long War Bow", false }, { 82, 82, "Leadcrow", "lxb", "Light Crossbow", false }, { 83, 83, "Ichorsting", "mxb", "Crossbow", false }, { 84, 84, "Hellcast", "hxb", "Heavy Crossbow", false },
{ 85, 85, "Doomspittle", "rxb", "Repeating Crossbow", false }, { 86, 86, "Coldkill", "9ha", "Hatchet", false }, { 87, 87, "Butcher's Pupil", "9ax", "Cleaver", false }, { 88, 88, "Islestrike", "92a", "Twin Axe", false }, { 89, 89, "Pompeii's Wrath", "9mp", "Battle Sickle (Necro)", false },
{ 90, 90, "Guardian Naga", "9wa", "Naga", false }, { 91, 91, "Warlord's Trust", "9la", "Military Axe", false }, { 92, 92, "Spellsteel", "9ba", "Bearded Axe", false }, { 93, 93, "Stormrider", "9bt", "Tabar", false }, { 94, 94, "Boneslayer Blade", "9ga", "Gothic Axe (Barb)", false },
{ 95, 95, "The Minataur", "9gi", "Ancient Axe (Barb)", false }, { 96, 96, "Suicide Branch", "9wn", "Burnt Wand", false }, { 97, 97, "Carin Shard", "9yw", "Petrified Wand", false }, { 98, 98, "Arm of King Leoric", "9bw", "Tomb Wand", false }, { 99, 99, "Blackhand Key", "9gw", "Grave Wand", false },
{ 100, 100, "Dark Clan Crusher", "9sp", "Limb(Druid)", false }, { 101, 101, "Zakarum's Hand", "9sc", "Rune Scepter", false }, { 102, 102, "The Fetid Sprinkler", "9qs", "Holy Water Sprinkler", false }, { 103, 103, "Hand of Blessed Light", "9ws", "Divine Scepter", false }, { 104, 104, "Fleshrender", "9cl", "Barbed Club", false },
{ 105, 105, "Sureshrill Frost", "9ma", "Flanged Mace", false }, { 106, 106, "Moonfall", "9mt", "Jagged Star", false }, { 107, 107, "Baezil's Vortex", "9fl", "Knout", false }, { 108, 108, "Earthshaker", "9wh", "Battle Hammer", false }, { 109, 109, "Bloodtree Stump", "9m9", "War Club (Barb)", false },
{ 110, 110, "The Gavel of Pain", "9gm", "Martel de Fer (Barb)", false }, { 111, 111, "Bloodletter", "9ss", "Gladius", false }, { 112, 112, "Coldsteel Eye", "9sm", "Cutlass", false }, { 113, 113, "Hexfire", "9sb", "Shamshir", false }, { 114, 114, "Blade of Ali Baba", "9fc", "Tulwar", false },
{ 115, 115, "Ginther's Rift", "9cr", "Dimensional Blade (sorc)", false }, { 116, 116, "Headstriker", "9bs", "Battle Sword", false }, { 117, 117, "Plague Bearer", "9ls", "Rune Sword", false }, { 118, 118, "The Atlantian", "9wd", "Ancient Sword", false }, { 119, 119, "Crainte Vomir", "92h", "Katana (Assassin)", false },
{ 120, 120, "Bing Sz Wang", "9cm", "Dacian Falx", false }, { 121, 121, "The Vile Husk", "9gs", "Tusk Sword", false }, { 122, 122, "Cloudcrack", "9b9", "Gothic Sword", false }, { 123, 123, "Todesfaelle Flamme", "9fb", "Zweihander", false }, { 124, 124, "Swordguard", "9gd", "Executioner Sword", false },
{ 125, 125, "Spineripper", "9dg", "Poignard", false }, { 126, 126, "Heart Carver", "9di", "Rondel", false }, { 127, 127, "Blackbog's Sharp", "9kr", "Cinquedeas (Necro)", false }, { 128, 128, "Stormspike", "9bl", "Stilleto (Necro)", false }, { 129, 129, "Deathbit", "9tk", "battle dart (Assassin)", false },
{ 130, 130, "The Scalper", "9ta", "Francisca (Barb)", false }, { 131, 131, "Constantly Waging", "9bk", "War Dart", false }, { 132, 132, "Realm Crusher", "9b8", "Hurlbat", false }, { 133, 133, "Quickening Strikes", "9ja", "War Javelin (Barb)", false }, { 134, 134, "Shrapnel Impact", "9pi", "Great Pilum (Barb)", false },
{ 135, 135, "Tempest Flash", "9s9", "Simbilan (Barb)", false }, { 136, 136, "Untethered", "9gl", "Spiculum (Barb)", false }, { 137, 137, "Unrelenting Will", "9ts", "Harpoon (Barb)", false }, { 138, 138, "The Impaler", "9sr", "War Spear (Now Pole)", false }, { 139, 139, "Kelpie Snare", "9tr", "Fuscina (Now Pole)", false },
{ 140, 140, "Soulfeast Tine", "9br", "War Fork (Now Pole)", false }, { 141, 141, "Hone Sundan", "9st", "Yari (Now Pole)", false }, { 142, 142, "Spire of Honor", "9p9", "Lance (Now Pole)", false }, { 143, 143, "The Meat Scraper", "9b7", "Lochaber Axe", false }, { 144, 144, "Blackleach Blade", "9vo", "Bill", false },
{ 145, 145, "Athena's Wrath", "9s8", "Battle Scythe (Necro)", false }, { 146, 146, "Pierre Tombale Couant", "9pa", "Partizan", false }, { 147, 147, "Husoldal Evo", "9h9", "Bec-de-Corbin", false }, { 148, 148, "Grim's Burning Dead", "9wc", "Grim Scythe (Necro)", false }, { 149, 149, "Razorswitch", "8ss", "Jo Stalf", false },
{ 150, 150, "Ribcracker", "8ls", "Quarterstaff", false }, { 151, 151, "Chromatic Ire", "8cs", "Cedar Staff", false }, { 152, 152, "Warpspear", "8bs", "Gothic Staff", false }, { 153, 153, "Skullcollector", "8ws", "Rune Staff", false }, { 154, 154, "Skystrike", "8sb", "Edge Bow", false },
{ 155, 155, "Riphook", "8hb", "Razor Bow", false }, { 156, 156, "Kuko Shakaku", "8lb", "CedarBow", false }, { 157, 157, "Endlesshail", "8cb", "Double Bow", false }, { 158, 158, "Whichwild String", "8s8", "Short Siege Bow", false }, { 159, 159, "Cliffkiller", "8l8", "Long Siege Bow", false },
{ 160, 160, "Magewrath", "8sw", "Rune Bow", false }, { 161, 161, "Godstrike Arch", "8lw", "Gothic Bow", false }, { 162, 162, "Langer Briser", "8lx", "Arbalest", false }, { 163, 163, "Pus Spiter", "8mx", "Siege Crossbow", false }, { 164, 164, "Buriza-Do Kyanon", "8hx", "Balista", false },
{ 165, 165, "Demon Machine", "8rx", "Chu-Ko-Nu", false }, { 166, 166, "Untrained Eye", "ktr", "Katar", false }, { 167, 167, "Redemption", "wrb", "Wrist Blade", false }, { 168, 168, "Ancient Hand", "axf", "Hatchet Hands", false }, { 169, 169, "Willbreaker", "ces", "Cestus", false },
{ 170, 170, "Skyfall Grip", "clw", "Claws", false }, { 171, 171, "Oathbinder", "btl", "Blade Talons", false }, { 172, 172, "Pride's Fan", "skr", "Scissors Katar", false }, { 173, 173, "Burning Sun", "9ar", "Quhab", false }, { 174, 174, "Severance", "9wb", "Wrist Spike", false },
{ 175, 175, "Hand of Madness", "9xf", "Fascia", false }, { 176, 176, "Vanquisher", "9cs", "Hand Scythe", false }, { 177, 177, "Wind-Forged Blade", "9lw", "Greater Claws", false }, { 178, 178, "Bartuc's Cut-Throat", "9tw", "Greater Talons", false }, { 179, 179, "Void Ripper", "9qr", "Scissors Quhab", false },
{ 180, 180, "Soul-Forged Grip", "7ar", "Suwayyah", false }, { 181, 181, "Jadetalon", "7wb", "wrist sword", false }, { 182, 182, "Malignant Touch", "7xf", "War Fist", false }, { 183, 183, "Shadowkiller", "7cs", "battle cestus", false }, { 184, 184, "Firelizard's Talons", "7lw", "feral claws", false },
{ 185, 185, "Viz-Jaq'taar Order", "7tw", "Runic Talons", false }, { 186, 186, "Mage Crusher", "7qr", "Scissors Suwayyah", false }, { 187, 187, "Razoredge", "7ha", "tomahawk", false }, { 188, 188, "Glittering Crescent", "7ax", "Small Crescent", false }, { 189, 189, "Runemaster", "72a", "ettin axe", false },
{ 190, 190, "Cranebeak", "7mp", "Reaper Sickle (Necro)", false }, { 191, 191, "Deathcleaver", "7wa", "berserker axe", false }, { 192, 192, "Blessed Beheader", "7la", "Feral Axe", false }, { 193, 193, "Ethereal Edge", "7ba", "silver-edged axe", false }, { 194, 194, "Hellslayer", "7bt", "Decapitator", false },
{ 195, 195, "Messerschmidt's Reaver", "7ga", "Champion Axe (Barb)", false }, { 196, 196, "Executioner's Justice", "7gi", "glorious axe (Barb)", false }, { 197, 197, "Bane Glow", "7wn", "Polished Wand", false }, { 198, 198, "Malthael Touch", "7yw", "Ghost Wand", false }, { 199, 199, "Boneshade", "7bw", "lich wand", false },
{ 200, 200, "Deaths's Web", "7gw", "unearthed wand", false }, { 201, 201, "Nord's Tenderizer", "7cl", "Bough (Druid)", false }, { 202, 202, "Heaven's Light", "7sc", "mighty scepter", false }, { 203, 203, "The Redeemer", "7qs", "Seraph Rod", false }, { 204, 204, "Ironward", "7ws", "caduceus", false },
{ 205, 205, "Demonlimb", "7sp", "tyrant club", false }, { 206, 206, "Stormlash", "7ma", "Reinforced Mace", false }, { 207, 207, "Baranar's Star", "7mt", "Devil Star", false }, { 208, 208, "Horizon's Tornado", "7fl", "scourge", false }, { 209, 209, "Schaefer's Hammer", "7wh", "Legendary Mallet", false },
{ 210, 210, "Windhammer", "7m7", "ogre maul (Barb)", false }, { 211, 211, "The Cranium Basher", "7gm", "Thunder Maul (Barb)", false }, { 212, 212, "Vows of Promise", "7ss", "Falcata", false }, { 213, 213, "Djinnslayer", "7sm", "ataghan", false }, { 214, 214, "Bloodmoon", "7sb", "elegant blade", false },
{ 215, 215, "Starward Fencer", "7fc", "Hydra Edge", false }, { 216, 216, "Lightsabre", "7cr", "Phase Blade (Sorc)", false }, { 217, 217, "Azurewrath", "7bs", "Conquest Sword", false }, { 218, 218, "Frostwind", "7ls", "Cryptic Sword", false }, { 219, 219, "Last Legend", "7wd", "Mythical Sword", false },
{ 220, 220, "Oashi", "72h", "Shinogi (Assassin)", false }, { 221, 221, "Gleam Rod", "7cm", "Highland Blade", false }, { 222, 222, "Flamebellow", "7gs", "balrog blade", false }, { 223, 223, "Doombringer", "7b7", "Champion Sword", false }, { 224, 224, "Burning Bane", "7fb", "Colossal Sword", false },
{ 225, 225, "The Grandfather", "7gd", "Colossus Blade", false }, { 226, 226, "Wizardspike", "7dg", "Bone Knife", false }, { 227, 227, "Rapid Strike", "7di", "Mithral Point", false }, { 228, 228, "Fleshripper", "7kr", "fanged knife (Necro)", false }, { 229, 229, "Ghostflame", "7bl", "legend spike (necro)", false },
{ 230, 230, "Sentinels Call", "7tk", "Flying Knife", false }, { 231, 231, "Gimmershred", "7ta", "flying axe (Barb)", false }, { 232, 232, "Warshrike", "7bk", "winged knife (Assassin)", false }, { 233, 233, "Lacerator", "7b8", "winged axe (Barb)", false }, { 234, 234, "Contemplation", "7ja", "Hyperion Javelin", false },
{ 235, 235, "Main Hand", "7pi", "Stygian Pilum", false }, { 236, 236, "Demon's Arch", "7s7", "balrog spear", false }, { 237, 237, "Wraithflight", "7gl", "ghost glaive", false }, { 238, 238, "Gargoyle's Bite", "7ts", "winged harpoon", false }, { 239, 239, "Arioc's Needle", "7sr", "Hyperion Spear (Now Pole)", false },
{ 240, 240, "Rock Piercer", "7tr", "Stygian Pike", false }, { 241, 241, "Viperfork", "7br", "Mancatcher", false }, { 242, 242, "Flash Forward", "7st", "Ghost Spear (Now Pole)", false }, { 243, 243, "Steelpillar", "7p7", "war pike (Now Pole)", false }, { 244, 244, "Bonehew", "7o7", "ogre axe", false },
{ 245, 245, "Tundra Tamer", "7vo", "Colossus Voulge", false }, { 246, 246, "The Reaper's Toll", "7s8", "Reaper Scythe", false }, { 247, 247, "Tomb Reaver", "7pa", "cryptic axe", false }, { 248, 248, "Wind Shatter", "l17", "Glorious Axe - LB", false }, { 249, 249, "Bonespire", "7wc", "Reaper Thresher (Necro)", false },
{ 250, 250, "Natures Intention", "6bs", "Walking Stick", false }, { 251, 251, "Thermite Quicksand", "6ls", "Stalagmite", false }, { 252, 252, "Ondal's Wisdom", "6cs", "elder staff", false }, { 253, 253, "Stone Crusher", "6bs", "Shillelagh", false }, { 254, 254, "Mang Song's Lesson", "6ws", "archon staff", false },
{ 255, 255, "Cold Crow's Caw", "6sb", "Spider Bow", false }, { 256, 256, "Trembling Vortex", "6hb", "Blade Bow", false }, { 257, 257, "Corrupted String", "6lb", "Shadow Bow", false }, { 258, 258, "Gyro Blaster", "6cb", "Great Bow", false }, { 259, 259, "Underground", "6s7", "Diamond Bow", false },
{ 260, 260, "Eaglehorn", "6l7", "Crusader Bow", false }, { 261, 261, "Widowmaker", "6sw", "ward bow", false }, { 262, 262, "Windforce", "6lw", "Hydra Bow", false }, { 263, 263, "Shadow Hunter", "6lx", "Pellet Bow", false }, { 264, 264, "Amnestys Glare", "6mx", "Gorgon Crossbow", false },
{ 265, 265, "Hellrack", "6hx", "colossus crossbow", false }, { 266, 266, "Gutsiphon", "6rx", "demon crossbow", false }, { 267, 267, "Enlightener", "ob1", "Eagle Orb", false }, { 268, 268, "Endothermic Stone", "ob2", "Sacred Globe", false }, { 269, 269, "Sensor", "ob3", "Smoked Sphere", false },
{ 270, 270, "Lightning Rod", "ob4", "Clasped Orb", false }, { 271, 271, "Energizer", "ob5", "Jared's Stone", false }, { 272, 272, "The Artemis String", "am1", "Stag Bow", false }, { 273, 273, "Pinaka", "am2", "Reflex Bow", false }, { 274, 274, "The Pain Producer", "am3", "Maiden Spear", false },
{ 275, 275, "The Poking Pike", "am4", "Maiden Pike", false }, { 276, 276, "Skovos Striker", "am5", "Maiden Javelin", false }, { 277, 277, "Risen Phoenix", "ob6", "Glowing Orb", false }, { 278, 278, "Glacial Oasis", "ob7", "Crystalline Globe", false }, { 279, 279, "Thunderous", "ob8", "Cloudy Sphere", false },
{ 280, 280, "Magic", "ob9", "Sparkling Ball", false }, { 281, 281, "The Oculus", "oba", "Swirling Crystal", false }, { 282, 282, "Windraven", "am6", "Ashwood Bow", false }, { 283, 285, "Lycander's Aim", "am7", "Ceremonial Bow", false }, { 284, 286, "Titan's Revenge", "ama", "Ceremonial Javelin", false },
{ 285, 287, "Lycander's Flank", "am9", "Ceremonial Pike", false }, { 286, 288, "Above All", "obb", "Heavenly Stone", false }, { 287, 289, "Eschuta's Temper", "obc", "eldritch orb", false }, { 288, 290, "Belphegor's Beating", "obd", "Demon Heart", false }, { 289, 291, "Tempest Firey", "obe", "Vortex Orb", false },
{ 290, 292, "Death's Fathom", "obf", "dimensional shard", false }, { 291, 293, "Bloodraven's Charge", "amb", "matriarchal bow", false }, { 292, 294, "Shredwind Hell", "amc", "Grand Matron Bow", false }, { 293, 295, "Thunderstroke", "amf", "matriarchal javelin", false }, { 294, 296, "Stoneraven", "amd", "matriarchal spear", false },
{ 295, 297, "Biggin's Bonnet", "cap", "Cap", false }, { 296, 298, "Tarnhelm", "skp", "Skull Cap", false }, { 297, 299, "Coif of Glory", "hlm", "Helm", false }, { 298, 300, "Duskdeep", "fhl", "Full Helm", false }, { 299, 301, "Howltusk", "ghm", "Great Helm", false },
{ 300, 302, "Undead Crown", "crn", "Crown (Paladin)", false }, { 301, 303, "The Face of Horror", "msk", "Mask", false }, { 302, 304, "Greyform", "qui", "Quilted Armor", false }, { 303, 305, "Blinkbats Form", "lea", "Leather Armor", false }, { 304, 306, "The Centurion", "hla", "Hard Leather", false },
{ 305, 307, "Twitchthroe", "stu", "Studded Leather", false }, { 306, 308, "Darkglow", "rng", "Ring Mail", false }, { 307, 309, "Hawkmail", "scl", "Scale Mail", false }, { 308, 310, "Sparking Mail", "chn", "Chain Mail", false }, { 309, 311, "Venomsward", "brs", "Breast Plate", false },
{ 310, 312, "Iceblink", "spl", "Splint Mail", false }, { 311, 313, "Boneflesh", "plt", "Plate Mail", false }, { 312, 314, "Rockfleece", "fld", "Field Plate", false }, { 313, 315, "Rattlecage", "gth", "Gothic Plate", false }, { 314, 316, "Goldskin", "ful", "Full Plate Mail", false },
{ 315, 317, "Victors Silk", "aar", "AncientArmor", false }, { 316, 318, "Heavenly Garb", "ltp", "Light Plate", false }, { 317, 319, "Pelta Lunata", "buc", "Buckler", false }, { 318, 320, "Umbral Disk", "sml", "Small Shield", false }, { 319, 321, "Stormguild", "lrg", "Large Shield (Zon)", false },
{ 320, 322, "Steelclash", "kit", "Kite Shield", false }, { 321, 323, "Bverrit Keep", "tow", "Tower Shield", false }, { 322, 324, "The Ward", "gts", "Gothic Shield", false }, { 323, 325, "The Hand of Broc", "lgl", "Gloves", false }, { 324, 326, "Bloodfist", "vgl", "Heavy Gloves", false },
{ 325, 327, "Chance Guards", "mgl", "Bracers", false }, { 326, 328, "Magefist", "tgl", "Light Gauntlets", false }, { 327, 329, "Frostburn", "hgl", "Gauntlets", false }, { 328, 330, "Hotspur", "lbt", "Leather Boots", false }, { 329, 331, "Gorefoot", "vbt", "Heavy Boots", false },
{ 330, 332, "Treads of Cthon", "mbt", "Chain Boots", false }, { 331, 333, "Goblin Toe", "tbt", "Light Plate Boots", false }, { 332, 334, "Tearhaunch", "hbt", "Plate Boots", false }, { 333, 335, "Lenyms Cord", "lbl", "Sash", false }, { 334, 336, "Snakecord", "vbl", "Light Belt", false },
{ 335, 337, "Nightsmoke", "mbl", "Belt", false }, { 336, 338, "Goldwrap", "tbl", "Heavy Belt", false }, { 337, 339, "Bladebuckle", "hbl", "Girdle", false }, { 338, 340, "Wormskull", "bhm", "Bone Helm (Necro)", false }, { 339, 341, "Wall of the Eyeless", "bsh", "Bone Shield (Necro)", false },
{ 340, 342, "Swordback Hold", "spk", "Spiked Shield", false }, { 341, 343, "Peasent Crown", "xap", "War Hat", false }, { 342, 344, "Rockstopper", "xkp", "Sallet", false }, { 343, 345, "Stealskull", "xlm", "Casque", false }, { 344, 346, "Darksight Helm", "xhl", "Basinet", false },
{ 345, 347, "Valkyrie Wing", "xhm", "Winged Helm", false }, { 346, 348, "Crown of Thieves", "xrn", "Grand Crown (Paladin)", false }, { 347, 349, "Blackhorn's Face", "xsk", "Death Mask", false }, { 348, 350, "The Spirit Shroud", "xui", "Ghost Armor", false }, { 349, 351, "Skin of the Vipermagi", "xea", "Serpentskin Armor", false },
{ 350, 352, "Skin of the Flayerd One", "xla", "Demonhide Armor", false }, { 351, 353, "Ironpelt", "xtu", "Tresllised Armor", false }, { 352, 354, "Spiritforge", "xng", "Linked Mail", false }, { 353, 355, "Crow Caw", "xcl", "Tigulated Mail", false }, { 354, 356, "Shaftstop", "xhn", "Mesh Armor", false },
{ 355, 357, "Duriel's Shell", "xrs", "Cuirass", false }, { 356, 358, "Skullder's Ire", "xpl", "Russet Armor", false }, { 357, 359, "Guardian Angel", "xlt", "Templar Coat", false }, { 358, 360, "Toothrow", "xld", "Sharktooth Armor", false }, { 359, 361, "Atma's Wail", "xth", "Embossed Plate", false },
{ 360, 362, "Black Hades", "xul", "Chaos Armor", false }, { 361, 363, "Corpsemourn", "xar", "Ornate Armor", false }, { 362, 364, "Que-Hegan's Wisdon", "xtp", "Mage Plate", false }, { 363, 365, "Visceratuant", "xuc", "Defender", false }, { 364, 366, "Mosers Blessed Circle", "xml", "Round Shield", false },
{ 365, 367, "Stormchaser", "xrg", "Scutum (Zon)", false }, { 366, 368, "Tiamat's Rebuke", "xit", "Dragon Shield", false }, { 367, 369, "Kerke's Sanctuary", "xow", "Pavise", false }, { 368, 370, "Radimant's Sphere", "xts", "Ancient Shield", false }, { 369, 371, "Venom Grip", "xlg", "Demonhide Gloves", false },
{ 370, 372, "Gravepalm", "xvg", "Sharkskin Gloves", false }, { 371, 373, "Ghoulhide", "xmg", "Heavy Bracers", false }, { 372, 374, "Lavagout", "xtg", "Battle Guantlets", false }, { 373, 375, "Hellmouth", "xhg", "War Gauntlets", false }, { 374, 376, "Infernostride", "xlb", "Demonhide Boots", false },
{ 375, 377, "Waterwalk", "xvb", "Sharkskin Boots", false }, { 376, 378, "Silkweave", "xmb", "Mesh Boots", false }, { 377, 379, "Wartraveler", "xtb", "Battle Boots", false }, { 378, 380, "Gorerider", "xhb", "War Boots", false }, { 379, 381, "String of Ears", "zlb", "Demonhide Sash", false },
{ 380, 382, "Razortail", "zvb", "Sharkskin Belt", false }, { 381, 383, "Gloomstrap", "zmb", "Mesh Belt", false }, { 382, 384, "Snowclash", "ztb", "Battle Belt", false }, { 383, 385, "Thudergod's Vigor", "zhb", "War Belt", false }, { 384, 386, "Vampiregaze", "xh9", "Grim Helm (Necro)", false },
{ 385, 387, "Lidless Wall", "xsh", "Grim Shield (Necro)", false }, { 386, 388, "Lance Guard", "xpk", "Barbed Shield", false }, { 387, 389, "Primal Power", "dr1", "Wolf Head", false }, { 388, 390, "Murder of Crows", "dr2", "Hawk Helm", false }, { 389, 391, "Cheetah Stance", "dr3", "Antlers", false },
{ 390, 392, "Uproar", "dr4", "Falcon Mask", false }, { 391, 393, "Flame Spirit", "dr5", "Spirit Mask", false }, { 392, 394, "Toothless Maw", "ba1", "Jawbone Cap", false }, { 393, 395, "Darkfear", "ba2", "Fanged Helm", false }, { 394, 396, "Thermal Shock", "ba3", "Horned Helm", false },
{ 395, 397, "Nature's Protector", "ba4", "Assault Helmet", false }, { 396, 398, "Reckless Fury", "ba5", "Avenger Guard", false }, { 397, 399, "Sigurd's Staunch", "pa1", "Targe", false }, { 398, 400, "Caster's Courage", "pa2", "Rondache", false }, { 399, 401, "Briar Patch", "pa3", "Heraldic Shield", false },
{ 400, 402, "Ricochet", "pa4", "Aerin Shield", false }, { 401, 403, "Favored Path", "pa5", "Crown Shield", false }, { 402, 404, "Old Friend", "ne1", "Preserved Head", false }, { 403, 405, "Decomposed Leader", "ne2", "Zombie Head", false }, { 404, 406, "Tangled Fellow", "ne3", "Unraveller Head", false },
{ 405, 407, "Stubborn Stone", "ne4", "Gargoyle Head", false }, { 406, 408, "Spiked Dreamcatcher", "ne5", "Demon Head", false }, { 407, 409, "Journeyman's Band", "ci0", "Circlet", false }, { 408, 410, "Hygieia's Purity", "ci1", "Coronet", false }, { 409, 411, "Kira's Guardian", "ci2", "tiara", false },
{ 410, 412, "Griffon's Eye", "ci3", "diadem", false }, { 411, 413, "Harlequin Crest", "uap", "Shako", false }, { 412, 414, "Tarnhelm's Revenge", "ukp", "Hydraskull", false }, { 413, 415, "Steelshade", "ulm", "armet", false }, { 414, 416, "Veil of Steel", "uhl", "Giant Conch", false },
{ 415, 417, "Nightwing's Veil", "uhm", "spired helm", false }, { 416, 418, "Crown of Ages", "urn", "corona (Pali)", false }, { 417, 419, "Andariel's Visage", "usk", "demonhead", false }, { 418, 420, "Ormus' Robes", "uui", "dusk shroud", false }, { 419, 421, "Arcane Protector", "uea", "Wyrmhide", false },
{ 420, 422, "Spell Splitter", "ula", "Scarab Husk", false }, { 421, 423, "The Gladiator's Bane", "utu", "Wire Fleece", false }, { 422, 424, "Balled Lightning", "ung", "Diamond Mail", false }, { 423, 425, "Giant Crusher", "ucl", "Loricated Mail", false }, { 424, 426, "Chained Lightning", "uhn", "Boneweave", false },
{ 425, 427, "Savitr's Garb", "urs", "Great Hauberk", false }, { 426, 428, "Arkaine's Valor", "upl", "Balrog Skin", false }, { 427, 429, "Strength Unleashed", "ult", "Hellforge Plate", false }, { 428, 430, "Leviathan", "uld", "kraken shell", false }, { 429, 431, "Duality", "uth", "Lacquered Plate", false },
{ 430, 432, "Steel Carapice", "uul", "shadow plate", false }, { 431, 433, "Tyrael's Might", "uar", "sacred armor", false }, { 432, 434, "Spiritual Protector", "utp", "Archon Plate", false }, { 433, 435, "Cleansing Ward", "uuc", "Heater", false }, { 434, 436, "Blackoak Shield", "uml", "Luna", false },
{ 435, 437, "Astrogha's Web", "urg", "Hyperion", false }, { 436, 438, "Stormshield", "uit", "Monarch", false }, { 437, 439, "Medusa's Gaze", "uow", "aegis", false }, { 438, 440, "Spirit Ward", "uts", "ward", false }, { 439, 441, "Indra's Mark", "ulg", "Bramble Mitts", false },
{ 440, 442, "Dracul's Grasp", "uvg", "vampirebone gloves", false }, { 441, 443, "Souldrain", "umg", "vambraces", false }, { 442, 444, "Carthas's Presence", "utg", "Crusader Gauntlets", false }, { 443, 445, "Steelrend", "uhg", "ogre gauntlets", false }, { 444, 446, "Mana Wyrm", "ulb", "Wyrmhide Boots", false },
{ 445, 447, "Sandstorm Trek", "uvb", "scarabshell boots", false }, { 446, 448, "Marrowwalk", "umb", "boneweave boots", false }, { 447, 449, "Crimson Shift", "utb", "Mirrored Boots", false }, { 448, 450, "Lelantus's Frenzy", "uhb", "Myrmidon Greaves", false }, { 449, 451, "Arachnid Mesh", "ulc", "spiderweb sash", false },
{ 450, 452, "Nosferatu's Coil", "uvc", "vampirefang belt", false }, { 451, 453, "Verdugo's Hearty Cord", "umc", "mithril coil", false }, { 452, 454, "Magni's Warband", "utc", "Troll Belt", false }, { 453, 455, "Arcanist's Safeguard", "uhc", "Colossus Girdle", false }, { 454, 456, "Giantskull", "uh9", "bone visage (NECRO)", false },
{ 455, 457, "Headhunter's Glory", "ush", "troll nest (Necro)", false }, { 456, 458, "Spike Thorn", "upk", "blade barrier", false }, { 457, 459, "Flame of Combat", "dr6", "Alpha Helm", false }, { 458, 460, "Mystic Command", "dr7", "Griffon Headress", false }, { 459, 461, "Rama's Protector", "dr8", "Hunter's Guise", false },
{ 460, 462, "Snow Spirit", "dr9", "Sacred Feathers", false }, { 461, 463, "Efreeti's Fury", "dra", "Totemic Mask", false }, { 462, 464, "Combat Visor", "ba6", "Jawbone Visor", false }, { 463, 465, "Strength of Pride", "ba7", "Lion Helm", false }, { 464, 466, "Fighter's Stance", "ba8", "Rage Mask", false },
{ 465, 467, "Piercing Cold", "ba9", "Savage Helmet", false }, { 466, 468, "Arreat's Face", "baa", "Slayer Guard", false }, { 467, 469, "Fara's Defender", "pa6", "Akaran Targe", false }, { 468, 470, "Rakkis's Guard", "pa7", "Akaran Rondache", false }, { 469, 471, "Assaulter's Armament", "pa8", "Protector Shield", false },
{ 470, 472, "Herald of Zakarum", "pa9", "Gilded Shield", false }, { 471, 473, "Blackheart's Barrage", "paa", "Royal Shield", false }, { 472, 474, "Mehtan's Carrion", "ne6", "Mummified Trophy", false }, { 473, 475, "Venom Storm", "ne7", "Fetish Trophy", false }, { 474, 476, "Bone Zone", "ne8", "Sexton Trophy", false },
{ 475, 477, "Contagion", "ne9", "Cantor Trophy", false }, { 476, 478, "Homunculus", "nea", "Heirophant Trophy", false }, { 477, 479, "Cerebus", "drb", "blood spirit", false }, { 478, 480, "Pack Mentality", "drc", "Sun Spirit", false }, { 479, 481, "Spiritkeeper", "drd", "earth spirit", false },
{ 480, 482, "Cavern Dweller", "dre", "sky spirit", false }, { 481, 483, "Jalal's Mane", "dra", "Dream Spirit", false }, { 482, 484, "Berserker's Stance", "bab", "Carnage Helm", false }, { 483, 485, "Wolfhowl", "bac", "fury visor", false }, { 484, 486, "Demonhorn's Edge", "bad", "destroyer helm", false },
{ 485, 487, "Halaberd's Reign", "bae", "conqueror crown", false }, { 486, 488, "Warrior's Resolve", "baf", "Guardian Crown", false }, { 487, 489, "Primordial Punisher", "pab", "Sacred Targe", false }, { 488, 490, "Alma Negra", "pac", "sacred rondache", false }, { 489, 491, "Faithful Guardian", "pad", "Ancient Shield", false },
{ 490, 492, "Dragonscale", "pae", "zakarum shield", false }, { 491, 493, "Shield of Forsaken Light", "paf", "Vortex Shield", false }, { 492, 494, "Onikuma", "neb", "Minion Skull", false }, { 493, 495, "Bone Parade", "neg", "Hellspawn Skull", false }, { 494, 496, "Elanuzuru", "ned", "Overseer Skull", false },
{ 495, 497, "Boneflame", "nee", "succubae skull", false }, { 496, 498, "Darkforce Spawn", "nef", "bloodlord skull", false }, { 497, 504, "Earthshifter", "Wp3", "Fenris Fur", false }, { 498, 510, "Shadowdancer", "Ab3", "Bladed Boots", false }, { 499, 513, "Templar's Might", "Bp3", "Enlightened Plate", false },
{ 500, 516, "Nature's Nurture", "Oa3", "Oaken Armor", false }, { 501, 519, "Firebelr", "Vg3", "Vizjerei Vestige", false }, { 502, 520, "Flightless", "aqv", "Arrows", false }, { 503, 521, "Pinpoint", "aqv", "Arrows", false }, { 504, 522, "Nokozan Relic", "amu", "Amulet", false },
{ 505, 523, "The Eye of Etlich", "amu", "Amulet", false }, { 506, 524, "The Mahim-Oak Curio", "amu", "Amulet", false }, { 507, 525, "Nagelring", "rin", "Ring", false }, { 508, 526, "Manald Heal", "rin", "Ring", false }, { 509, 527, "The Stone of Jordan", "rin", "Ring", false },
{ 510, 528, "Bul Katho's Wedding Band", "rin", "Ring", false }, { 511, 529, "The Cat's Eye", "amu", "Amulet", false }, { 512, 530, "The Rising Sun", "amu", "Amulet", false }, { 513, 531, "Crescent Moon", "amu", "Amulet", false }, { 514, 532, "Mara's Kaleidoscope", "amu", "Amulet", false },
{ 515, 533, "Atma's Scarab", "amu", "Amulet", false }, { 516, 534, "Dwarf Star", "rin", "Ring", false }, { 517, 535, "Raven Frost", "rin", "Ring", false }, { 518, 536, "Highlord's Wrath", "amu", "Amulet", false }, { 519, 537, "Saracen's Chance", "amu", "Amulet", false },
{ 520, 538, "Nature's Peace", "rin", "ring", false }, { 521, 539, "Seraph's Hymn", "amu", "amulet", false }, { 522, 540, "Wisp Projector", "rin", "ring", false }, { 523, 541, "Constricting Ring", "rin", "Ring", false }, { 524, 542, "Gheed's Fortune", "cm3", "charm", false },
{ 525, 543, "Annihilus", "cm1", "charm", false }, { 526, 544, "Carrion Wind", "rin", "ring", false }, { 527, 545, "Metalgrid", "amu", "amulet", false }, { 528, 550, "Rainbow Facet1", "jew", "jewel", false }, { 529, 551, "Rainbow Facet2", "jew", "jewel", false },
{ 530, 552, "Rainbow Facet3", "jew", "jewel", false }, { 531, 553, "Rainbow Facet4", "jew", "jewel", false }, { 532, 554, "Rainbow Facet5", "jew", "jewel", false }, { 533, 555, "Rainbow Facet6", "jew", "jewel", false }, { 534, 556, "Hellfire Torch", "cm2", "charm", false },
{ 535, 557, "Beacon of Hope", "BoH", "Beacon", false }, { 536, 558, "MythosLog", "y08", "KillTracker", false }, { 537, 559, "Storage Bag", "Z01", "bag", false }, { 538, 560, "Magefist", "tgl", "Light Gauntlets", false }, { 539, 561, "Magefist", "tgl", "Light Gauntlets", false },
{ 540, 562, "Magefist", "tgl", "Light Gauntlets", false }, { 541, 563, "Magefist", "tgl", "Light Gauntlets", false }, { 542, 564, "IceClone Armor", "St1", "", false }, { 543, 565, "IceClone Armor2", "St2", "", false }, { 544, 566, "Hydra Master", "6ls", "Stalagmite", false },
{ 545, 567, "Spiritual Savior", "utp", "Archon Plate", false }, { 546, 568, "IceClone Armor3", "St3", "", false }, { 547, 569, "Fletcher's Fury", "Ag1", "Trainee Gloves", false }, { 548, 570, "Indra's Guidance", "Ag2", "Adept Gloves", false }, { 549, 572, "Robbin's Temple", "ci1", "Coronet", false },
{ 550, 573, "Trials Charm c1", "a59", "Large Charm", false }, { 551, 574, "Trials Charm c2", "a60", "Large Charm", false }, { 552, 575, "Trials Charm c3", "a61", "Large Charm", false }, { 553, 576, "Trials Charm c4", "a62", "Large Charm", false }, { 554, 577, "Trials Charm c5", "a63", "Large Charm", false },
{ 555, 578, "Trials Charm c6", "a64", "Large Charm", false }, { 556, 579, "Trials Charm c7", "a65", "Large Charm", false }, { 557, 580, "MegaCharm", "a66", "Large Charm", false }, { 558, 581, "Spirit Striker", "aqv", "Arrows", false }, { 559, 582, "Aim of Indra", "aqv", "Arrows", false },
{ 560, 583, "Enchanted Flame", "aqv", "Arrows", false }, { 561, 584, "Mageflight", "aqv", "Arrows", false }, { 562, 585, "Energy Manipulator", "amu", "amulet", false }, { 563, 586, "Trinity", "amu", "amulet", false }, { 564, 587, "Quintessence", "amu", "amulet", false },
{ 565, 588, "Life Everlasting", "rin", "ring", false }, { 566, 589, "Hunter's Mark", "rin", "ring", false }, { 567, 590, "Unholy Commander", "cm3", "charm", false }, { 568, 591, "Tommy's Enlightener", "7qs", "Seraph Rod", false }, { 569, 592, "Curtis's Fortifier", "uhc", "Colossus Girdle", false },
{ 570, 593, "Kurec's Pride", "drd", "earth spirit", false }, { 571, 594, "Spiritual Guardian", "utp", "Archon Plate", false }, { 572, 595, "Blackmaw's Brutality", "xld", "Sharktooth Armor", false }, { 573, 596, "Spencer's Dispenser", "oba", "Swirling Crystal", false }, { 574, 597, "Fletching of Frostbite", "aqv", "Arrows", false },
{ 575, 598, "Healthy Breakfast", "cm1", "charm", false }, { 576, 599, "MythosLogAmazon", "y01", "KillTracker", false }, { 577, 600, "MythosLogAssassin", "y02", "KillTracker", false }, { 578, 601, "MythosLogBarbarian", "y03", "KillTracker", false }, { 579, 602, "MythosLogDruid", "y04", "KillTracker", false },
{ 580, 603, "MythosLogNecromancer", "y05", "KillTracker", false }, { 581, 604, "MythosLogPaladin", "y06", "KillTracker", false }, { 582, 605, "MythosLogSorceress", "y07", "KillTracker", false }, { 583, 606, "Cola Cube", "cm1", "charm", false }, { 584, 607, "Soul Stompers", "umb", "boneweave boots", false },
{ 585, 608, "MapReceipt01", "m27", "map", false }, { 586, 609, "Kingdom's Heart", "uar", "Sacred Armor", false }, { 587, 610, "Prismatic Facet", "j00", "Sacred Jewel", false }, { 588, 611, "Null Charm", "cm3", "charm", false }, { 589, 612, "SS Full Plate", "St4", "shadow plate", false },
{ 590, 613, "SS Full Plate", "St5", "shadow plate", false }, { 591, 614, "SS Full Plate", "St6", "shadow plate", false }, { 592, 615, "SS Full Plate", "St7", "shadow plate", false }, { 593, 616, "SS Full Plate", "St8", "shadow plate", false }, { 594, 617, "SS Full Plate", "St9", "shadow plate", false },
{ 595, 618, "SS Full Plate", "St0", "shadow plate", false }, { 596, 619, "Messerschmidt's Reaver SS", "Ss1", "Champion Axe (Barb)", false }, { 597, 620, "Lightsabre SS", "Ss2", "Phase Blade (Sorc)", false }, { 598, 621, "Crainte Vomir", "Ss3", "Katana (Assassin)", false }, { 599, 622, "Crainte Vomir", "Ss4", "Katana (Assassin)", false },
{ 600, 623, "Spiritual Sentinel", "utp", "Archon Plate", false }, { 601, 624, "Spiritual Warden", "utp", "Archon Plate", false }, { 602, 626, "Harlequin Crest Legacy", "uap", "Shako", false }, { 603, 627, "The Cat's Eye Legacy", "amu", "Amulet", false }, { 604, 628, "Arkaine's Valor Bugged", "upl", "Balrog Skin", false },
{ 605, 629, "String of Ears Bugged", "zlb", "Demonhide Sash", false }, { 606, 630, "Wizardspike Fused", "tgl", "Light Gauntlets", false }, { 607, 631, "Exsanguinate", "vgl", "Heavy Gloves", false }, { 608, 632, "Monar's Gale", "xts", "Ancient Shield", false }, { 609, 633, "MythosLogAmazonA", "y34", "KillTracker", false },
{ 610, 634, "MythosLogAssassinA", "y35", "KillTracker", false }, { 611, 635, "MythosLogBarbarianA", "y36", "KillTracker", false }, { 612, 636, "MythosLogDruidA", "y37", "KillTracker", false }, { 613, 637, "MythosLogNecromancerA", "y38", "KillTracker", false }, { 614, 638, "MythosLogPaladinA", "y39", "KillTracker", false },
{ 615, 639, "MythosLogSorceressA", "y40", "KillTracker", false }, { 616, 640, "MythosLogAmazonB", "y34", "KillTracker", false }, { 617, 641, "MythosLogAssassinB", "y35", "KillTracker", false }, { 618, 642, "MythosLogBarbarianB", "y36", "KillTracker", false }, { 619, 643, "MythosLogDruidB", "y37", "KillTracker", false },
{ 620, 644, "MythosLogNecromancerB", "y38", "KillTracker", false }, { 621, 645, "MythosLogPaladinB", "y39", "KillTracker", false }, { 622, 646, "MythosLogSorceressB", "y40", "KillTracker", false }, { 623, 647, "MythosLogAmazonC", "y34", "KillTracker", false }, { 624, 648, "MythosLogAssassinC", "y35", "KillTracker", false },
{ 625, 649, "MythosLogBarbarianC", "y36", "KillTracker", false }, { 626, 650, "MythosLogDruidC", "y37", "KillTracker", false }, { 627, 651, "MythosLogNecromancerC", "y38", "KillTracker", false }, { 628, 652, "MythosLogPaladinC", "y39", "KillTracker", false }, { 629, 653, "MythosLogSorceressC", "y40", "KillTracker", false },
{ 630, 654, "Black Cats Secret", "cm3", "charm", false }, { 631, 655, "Dustdevil", "l18", "Runic Talons - LB", false }, { 632, 656, "Improvise", "6sw", "ward bow", false }, { 633, 657, "Ken'Juk's Blighted Visage", "usk", "demonhead", false }, { 634, 658, "Philios Prophecy", "amc", "Pellet Bow", false },
{ 635, 659, "Whisper", "cqv", "Bolts", false }, { 636, 660, "Dragon's Cinder", "cqv", "Bolts", false }, { 637, 661, "Serpent's Fangs", "cqv", "Bolts", false }, { 638, 662, "Valkyrie Wing Legacy", "xhm", "Winged Helm", false }, { 639, 663, "War Traveler Bugged", "xtb", "Battle Boots", false },
{ 640, 664, "Undead Crown Fused", "rin", "Ring", false }, { 641, 665, "Soul of Edyrem", "m37", "Charm", false }, { 642, 668, "Black Suede", "lbt", "Leather Boots", false }, { 643, 669, "Allebasi", "amu", "Amulet", false }, { 644, 673, "Bigfoot", "xhb", "War Boots", false },
{ 645, 674, "Static Calling", "7mp", "Reaper Sickle", false }, { 646, 677, "Akara's Blessing", "amu", "Amulet", false },
};

static SetItemEntry g_StaticSetItemsRMD[] = {
    { "Civerb's Ward", 0, "Civerb's Vestments", "Large Shield", "lrg", false },
    { "Civerb's Icon", 1, "Civerb's Vestments", "Amulet", "amu", false },
    { "Civerb's Cudgel", 2, "Civerb's Vestments", "Grand Scepter", "gsc", false },
    { "Hsarus' Iron Heel", 3, "Hsarus' Defense", "Chain Boots", "mbt", false },
    { "Hsarus' Iron Fist", 4, "Hsarus' Defense", "Buckler", "buc", false },
    { "Hsarus' Iron Stay", 5, "Hsarus' Defense", "Belt", "mbl", false },
    { "Cleglaw's Tooth", 6, "Cleglaw's Brace", "Long Sword", "lsd", false },
    { "Cleglaw's Claw", 7, "Cleglaw's Brace", "Small Shield", "sml", false },
    { "Cleglaw's Pincers", 8, "Cleglaw's Brace", "Chain Gloves", "mgl", false },
    { "Iratha's Collar", 9, "Iratha's Finery", "Amulet", "amu", false },
    { "Iratha's Cuff", 10, "Iratha's Finery", "Light Gauntlets", "tgl", false },
    { "Iratha's Coil", 11, "Iratha's Finery", "Crown", "crn", false },
    { "Iratha's Cord", 12, "Iratha's Finery", "Heavy Belt", "tbl", false },
    { "Isenhart's Lightbrand", 13, "Isenhart's Armory", "Broad Sword", "bsd", false },
    { "Isenhart's Parry", 14, "Isenhart's Armory", "Gothic Shield", "gts", false },
    { "Isenhart's Case", 15, "Isenhart's Armory", "Breast Plate", "brs", false },
    { "Isenhart's Horns", 16, "Isenhart's Armory", "Full Helm", "fhl", false },
    { "Vidala's Barb", 17, "Vidala's Rig", "Long Battle Bow", "lbb", false },
    { "Vidala's Fetlock", 18, "Vidala's Rig", "Light Plated Boots", "tbt", false },
    { "Vidala's Ambush", 19, "Vidala's Rig", "Leather Armor", "lea", false },
    { "Vidala's Snare", 20, "Vidala's Rig", "Amulet", "amu", false },
    { "Milabrega's Orb", 21, "Milabrega's Regalia", "Kite Shield", "kit", false },
    { "Milabrega's Rod", 22, "Milabrega's Regalia", "War Scepter", "wsp", false },
    { "Milabrega's Diadem", 23, "Milabrega's Regalia", "Crown", "crn", false },
    { "Milabrega's Robe", 24, "Milabrega's Regalia", "Ancient Armor", "aar", false },
    { "Cathan's Rule", 25, "Cathan's Traps", "Battle Staff", "bst", false },
    { "Cathan's Mesh", 26, "Cathan's Traps", "Chain Mail", "chn", false },
    { "Cathan's Visage", 27, "Cathan's Traps", "Mask", "msk", false },
    { "Cathan's Sigil", 28, "Cathan's Traps", "Amulet", "amu", false },
    { "Cathan's Seal", 29, "Cathan's Traps", "Ring", "rin", false },
    { "Tancred's Crowbill", 30, "Tancred's Battlegear", "Sickle", "mpi", false },
    { "Tancred's Spine", 31, "Tancred's Battlegear", "Full Plate Mail", "ful", false },
    { "Tancred's Hobnails", 32, "Tancred's Battlegear", "Boots", "lbt", false },
    { "Tancred's Weird", 33, "Tancred's Battlegear", "Amulet", "amu", false },
    { "Tancred's Skull", 34, "Tancred's Battlegear", "Bone Helm", "bhm", false },
    { "Sigon's Gage", 35, "Sigon's Complete Steel", "Gauntlets", "hgl", false },
    { "Sigon's Visor", 36, "Sigon's Complete Steel", "Great Helm", "ghm", false },
    { "Sigon's Shelter", 37, "Sigon's Complete Steel", "Gothic Plate", "gth", false },
    { "Sigon's Sabot", 38, "Sigon's Complete Steel", "Greaves", "hbt", false },
    { "Sigon's Wrap", 39, "Sigon's Complete Steel", "Plated Belt", "hbl", false },
    { "Sigon's Guard", 40, "Sigon's Complete Steel", "Tower Shield", "tow", false },
    { "Infernal Cranium", 41, "Infernal Tools", "Cap", "cap", false },
    { "Infernal Torch", 42, "Infernal Tools", "Grim Wand", "gwn", false },
    { "Infernal Sign", 43, "Infernal Tools", "Heavy Belt", "tbl", false },
    { "Berserker's Headgear", 44, "Berserker's Garb", "Helm", "hlm", false },
    { "Berserker's Hauberk", 45, "Berserker's Garb", "Splint Mail", "spl", false },
    { "Berserker's Hatchet", 46, "Berserker's Garb", "Double Axe", "2ax", false },
    { "Death's Hand", 47, "Death's Disguise", "Leather Gloves", "lgl", false },
    { "Death's Guard", 48, "Death's Disguise", "Sash", "lbl", false },
    { "Death's Touch", 49, "Death's Disguise", "War Sword", "wsd", false },
    { "Angelic Sickle", 50, "Angelical Raiment", "Sabre", "sbr", false },
    { "Angelic Mantle", 51, "Angelical Raiment", "Ring Mail", "rng", false },
    { "Angelic Halo", 52, "Angelical Raiment", "Ring", "rin", false },
    { "Angelic Wings", 53, "Angelical Raiment", "Amulet", "amu", false },
    { "Arctic Horn", 54, "Arctic Gear", "Short War Bow", "swb", false },
    { "Arctic Furs", 55, "Arctic Gear", "Quilted Armor", "qui", false },
    { "Arctic Binding", 56, "Arctic Gear", "Light Belt", "vbl", false },
    { "Arctic Mitts", 57, "Arctic Gear", "Light Gauntlets", "tgl", false },
    { "Arcanna's Sign", 58, "Arcanna's Tricks", "Amulet", "amu", false },
    { "Arcanna's Deathwand", 59, "Arcanna's Tricks", "War Staff", "wst", false },
    { "Arcanna's Head", 60, "Arcanna's Tricks", "Skull Cap", "skp", false },
    { "Arcanna's Flesh", 61, "Arcanna's Tricks", "Light Plate", "ltp", false },
    { "Natalya's Totem", 62, "Natalya's Odium", "Casque", "xlm", false },
    { "Natalya's Mark", 63, "Natalya's Odium", "Scissors Suwayyah", "7qr", false },
    { "Natalya's Shadow", 64, "Natalya's Odium", "Mantle", "Ca2", false },
    { "Natalya's Soul", 65, "Natalya's Odium", "Mesh Boots", "xmb", false },
    { "Aldur's Stony Gaze", 66, "Aldur's Watchtower", "Hunter's Guise", "dr8", false },
    { "Aldur's Deception", 67, "Aldur's Watchtower", "Shadow Plate", "uul", false },
    { "Aldur's Gauntlet", 68, "Aldur's Watchtower", "Jagged Star", "9mt", false },
    { "Aldur's Advance", 69, "Aldur's Watchtower", "Battle Boots", "xtb", false },
    { "Immortal King's Will", 70, "Immortal King", "Avenger Guard", "ba5", false },
    { "Immortal King's Soul Cage", 71, "Immortal King", "Sacred Armor", "uar", false },
    { "Immortal King's Detail", 72, "Immortal King", "War Belt", "zhb", false },
    { "Immortal King's Forge", 73, "Immortal King", "War Gauntlets", "xhg", false },
    { "Immortal King's Pillar", 74, "Immortal King", "War Boots", "xhb", false },
    { "Immortal King's Stone Crusher", 75, "Immortal King", "Ogre Maul", "7m7", false },
    { "Tal Rasha's Fire-Spun Cloth", 76, "Tal Rasha's Wrappings", "Mesh Belt", "zmb", false },
    { "Tal Rasha's Adjudication", 77, "Tal Rasha's Wrappings", "Amulet", "amu", false },
    { "Tal Rasha's Lidless Eye", 78, "Tal Rasha's Wrappings", "Swirling Crystal", "oba", false },
    { "Tal Rasha's Howling Wind", 79, "Tal Rasha's Wrappings", "Lacquered Plate", "uth", false },
    { "Tal Rasha's Horadric Crest", 80, "Tal Rasha's Wrappings", "Death Mask", "xsk", false },
    { "Griswold's Valor", 81, "Griswold's Legacy", "Treasured Headdress", "Pc3", false },
    { "Griswold's Heart", 82, "Griswold's Legacy", "Ornate Plate", "xar", false },
    { "Griswolds's Redemption", 83, "Griswold's Legacy", "Caduceus", "7ws", false },
    { "Griswold's Honor", 84, "Griswold's Legacy", "Vortex Shield", "paf", false },
    { "Trang-Oul's Guise", 85, "Trang-Oul's Avatar", "Bone Visage", "uh9", false },
    { "Trang-Oul's Scales", 86, "Trang-Oul's Avatar", "Chaos Armor", "xul", false },
    { "Trang-Oul's Wing", 87, "Trang-Oul's Avatar", "Cantor Trophy", "ne9", false },
    { "Trang-Oul's Claws", 88, "Trang-Oul's Avatar", "Heavy Bracers", "xmg", false },
    { "Trang-Oul's Girth", 89, "Trang-Oul's Avatar", "Troll Belt", "utc", false },
    { "M'avina's True Sight", 90, "M'avina's Battle Hymn", "Diadem", "ci3", false },
    { "M'avina's Embrace", 91, "M'avina's Battle Hymn", "Kraken Shell", "uld", false },
    { "M'avina's Icy Clutch", 92, "M'avina's Battle Hymn", "Battle Gauntlets", "xtg", false },
    { "M'avina's Tenet", 93, "M'avina's Battle Hymn", "Sharkskin Belt", "zvb", false },
    { "M'avina's Caster", 94, "M'avina's Battle Hymn", "Grand Matron Bow", "amc", false },
    { "Telling of Beads", 95, "The Disciple", "Amulet", "amu", false },
    { "Laying of Hands", 96, "The Disciple", "Bramble Mitts", "ulg", false },
    { "Rite of Passage", 97, "The Disciple", "Demonhide Boots", "xlb", false },
    { "Spiritual Custodian", 98, "The Disciple", "Dusk Shroud", "uui", false },
    { "Credendum", 99, "The Disciple", "Mithril Coil", "umc", false },
    { "Dangoon's Teaching", 100, "Heaven's Brethren", "Reinforced Mace", "7ma", false },
    { "Heaven's Taebaek", 101, "Heaven's Brethren", "Ward", "uts", false },
    { "Haemosu's Adament", 102, "Heaven's Brethren", "Cuirass", "xrs", false },
    { "Ondal's Almighty", 103, "Heaven's Brethren", "Spired Helm", "uhm", false },
    { "Guillaume's Face", 104, "Orphan's Call", "Winged Helm", "xhm", false },
    { "Wilhelm's Pride", 105, "Orphan's Call", "Battle Belt", "ztb", false },
    { "Magnus' Skin", 106, "Orphan's Call", "Sharkskin Gloves", "xvg", false },
    { "Wihtstan's Guard", 107, "Orphan's Call", "Round Shield", "xml", false },
    { "Hwanin's Splendor", 108, "Hwanin's Majesty", "Grand Crown", "xrn", false },
    { "Hwanin's Refuge", 109, "Hwanin's Majesty", "Tigulated Mail", "xcl", false },
    { "Hwanin's Seal", 110, "Hwanin's Majesty", "Belt", "mbl", false },
    { "Hwanin's Justice", 111, "Hwanin's Majesty", "Bill", "9vo", false },
    { "Sazabi's Cobalt Redeemer", 112, "Sazabi's Grand Tribute", "Cryptic Sword", "7ls", false },
    { "Sazabi's Ghost Liberator", 113, "Sazabi's Grand Tribute", "Balrog Skin", "upl", false },
    { "Sazabi's Mental Sheath", 114, "Sazabi's Grand Tribute", "Basinet", "xhl", false },
    { "Bul-Kathos' Sacred Charge", 115, "Bul-Kathos' Children", "Colossus Blade", "7gd", false },
    { "Bul-Kathos' Tribal Guardian", 116, "Bul-Kathos' Children", "Mythical Sword", "7wd", false },
    { "Cow King's Horns", 117, "Cow King's Leathers", "War Hat", "xap", false },
    { "Cow King's Hide", 118, "Cow King's Leathers", "Studded Leather", "stu", false },
    { "Cow King's Hoofs", 119, "Cow King's Leathers", "Heavy Boots", "vbt", false },
    { "Naj's Puzzler", 120, "Naj's Ancient Set", "Elder Staff", "6cs", false },
    { "Naj's Light Plate", 121, "Naj's Ancient Set", "Hellforge Plate", "ult", false },
    { "Naj's Circlet", 122, "Naj's Ancient Set", "Circlet", "ci0", false },
    { "McAuley's Paragon", 123, "McAuley's Folly", "Cap", "cap", false },
    { "McAuley's Riprap", 124, "McAuley's Folly", "Heavy Boots", "vbt", false },
    { "McAuley's Taboo", 125, "McAuley's Folly", "Heavy Gloves", "vgl", false },
    { "McAuley's Superstition", 126, "McAuley's Folly", "Bone Wand", "bwn", false },
    { "Vessel's Atonment", 127, "Holy Vessel", "Blessed Plate", "Bp1", false },
    { "Vessel's Fufillment", 128, "Holy Vessel", "Heraldic Shield", "pa3", false },
    { "Vessel's Anointment", 129, "Holy Vessel", "Palisade Crown", "Pc1", false },
    { "Vessel's Armament", 130, "Holy Vessel", "Scepter", "scp", false },
    { "Pointed Justice", 131, "Majestic Lancer", "Maiden Javelins", "am5", false },
    { "True Parry", 132, "Majestic Lancer", "Large Shield", "lrg", false },
    { "Solidarity", 133, "Majestic Lancer", "Skovos Circle", "Zc1", false },
    { "Island Shore", 134, "Skovos Storm", "Stag Bow", "am1", false },
    { "Raging Seas", 135, "Skovos Storm", "Heavy Gloves", "vgl", false },
    { "Eye of the Storm", 136, "Skovos Storm", "Arrows", "aqv", false },
    { "Sturdy Garment", 137, "Wonder Wear", "Mesh Belt", "zmb", false },
    { "True Deflector", 138, "Wonder Wear", "Sallet", "xkp", false },
    { "Encased Corset", 139, "Wonder Wear", "Trellised Armor", "xtu", false },
    { "Silver Bracers", 140, "Wonder Wear", "Battle Gauntlets", "xtg", false },
    { "Outreach", 141, "Vizjerei Vocation", "Arcanic Touch", "Vg1", false },
    { "Masterful Teachings", 142, "Vizjerei Vocation", "Sacred Globe", "ob2", false },
    { "Inner Focus", 143, "Vizjerei Vocation", "Circlet", "ci0", false },
    { "Disruptor", 144, "Beyond Battlemage", "Dimensional Blade", "9cr", false },
    { "Bursting Desire", 145, "Beyond Battlemage", "Dragon Shield", "xit", false },
    { "Underestimated", 146, "Beyond Battlemage", "Serpentskin Armor", "xea", false },
    { "Tundra Storm", 147, "Glacial Plains", "Death Mast", "xsk", false },
    { "Enduring Onslaught", 148, "Glacial Plains", "Demonhide Sash", "zlb", false },
    { "Frozen Goliath", 149, "Glacial Plains", "Barbed Shield", "xpk", false },
    { "Rathma's Reaper", 150, "Rathma's Calling", "Battle Sickle", "9mp", false },
    { "Rathma's Shelter", 151, "Rathma's Calling", "Troll Nest", "ush", false },
    { "Rathma's Vestage", 152, "Rathma's Calling", "Bone Visage", "uh9", false },
    { "Rathma's Fortress", 153, "Rathma's Calling", "Wyrmhide", "uea", false },
    { "Stacato's Sigil", 154, "Stacatomamba's Guidance", "Ring", "rin", false },
    { "Mamba's Circle", 155, "Stacatomamba's Guidance", "Ring", "rin", false },
    { "Kreigur's Will", 156, "Kreigur's Mastery", "Shinogi", "72h", false },
    { "Kreigur's Judgement", 157, "Kreigur's Mastery", "Shinogi", "72h", false },
    { "Kami", 158, "Scarlet Sukami", "Colossal Sword", "7fb", false },
    { "Su", 159, "Scarlet Sukami", "Champion Sword", "7b7", false },
    { "Ysenob's Blood", 160, "Mirrored Flames", "Myrmidon Greaves", "uhb", false },
    { "Noertap's Pride", 161, "Mirrored Flames", "Colossus Girdle", "uhc", false },
    { "Olbaid's Deceipt", 162, "Mirrored Flames", "Ogre Gauntlets", "uhg", false },
    { "Gale Strength", 163, "Unstoppable Force", "Winged Harpoon", "7ts", false },
    { "Assault Prowess", 164, "Unstoppable Force", "Winged Harpoon", "7ts", false },
    { "Thirst for Blood", 165, "Underworld's Unrest", "Boneweave Boots", "umb", false },
    { "Rotting Reaper", 166, "Underworld's Unrest", "Reaper Sickle", "mpi", false },
    { "Siphon String", 167, "Underworld's Unrest", "Vampirefang Belt", "uvc", false },
    { "Crown of Cold", 168, "Elemental Blueprints", "Diadem", "ci3", false },
    { "Blazing Band", 169, "Elemental Blueprints", "Ring", "rin", false },
    { "Lightning Locket", 170, "Elemental Blueprints", "Amulet", "amu", false },
    { "Brewing Storm", 171, "Raijin's Rebellion", "Ward", "uts", false },
    { "Charged Chaos", 172, "Raijin's Rebellion", "Kraken Shell", "uld", false },
    { "Electron Emitter", 173, "Raijin's Rebellion", "Corona", "urn", false },
    { "Achyls' Armament", 174, "Mikael's Toxicity", "Boneweave", "uhn", false },
    { "Pendant of Pestilence", 175, "Mikael's Toxicity", "Amulet", "amu", false },
    { "Plague Protector", 176, "Mikael's Toxicity", "Troll Nest", "ush", false },
    { "Meat Masher", 177, "Warrior's Wrath", "Thunder Maul", "7gm", false },
    { "Supreme Strength", 178, "Warrior's Wrath", "Crusader Gauntlets", "utg", false },
    { "Repeating Reaper", 179, "Blessings of Artemis", "Hydra Bow", "6lw", false },
    { "Fletcher's Friend", 180, "Blessings of Artemis", "Huntress Gloves", "Ag3", false },
    { "Band of Brothers", 181, "Artio's Calling", "Ring", "rin", false },
    { "Grizzlepaw's Hide", 182, "Artio's Calling", "Grizzly Gear", "Gg3", false },
    { "Animal Instinct", 183, "Artio's Calling", "Mithril Coil", "umc", false },
    { "Justitia's Anger", 184, "Justitia's Divinity", "Cadaceus", "7ws", false },
    { "Justitia's Embrace", 185, "Justitia's Divinity", "Vortex Shield", "paf", false },
    { "Hand of Efreeti", 186, "Pulsing Presence", "Bramble Mitts", "ulg", false },
    { "Morning Frost", 187, "Pulsing Presence", "Scarabshell Boots", "uvb", false },
    { "Thunderlord's Vision", 188, "Pulsing Presence", "Demonhead", "usk", false },
    { "Coil of Heaven", 189, "Celestial Caress", "Ring", "rin", false },
    { "Band of Divinity", 190, "Celestial Caress", "Ring", "rin", false },
    { "Godly Locket", 191, "Celestial Caress", "Amulet", "amu", false },
    { "Chains of Bondage", 192, "Breaker of Chains", "Diadem", "ci3", false },
    { "Chains of Force", 193, "Breaker of Chains", "Archon Plate", "utp", false },
    { "Night's Disguise", 194, "Silhouette of Silence", "Cloak", "Ca3", false },
    { "Silent Stalkers", 195, "Silhouette of Silence", "Bladed Boots", "Ab3", false },
    { "Toxic Grasp", 196, "Silhouette of Silence", "Vampirebone Gloves", "uvg", false },
    { "Blade Binding", 197, "Mangala's Teachings", "Vampirefang Belt", "uvc", false },
    { "Murderous Intent", 198, "Mangala's Teachings", "Shako", "uap", false },
    { "Band of Suffering", 199, "Sacrificial Trinity", "Ring", "rin", false },
    { "Loop of Regret", 200, "Sacrificial Trinity", "Ring", "rin", false },
    { "Locket of Burden", 201, "Sacrificial Trinity", "Amulet", "amu", false },
    { "Bulwark of Defiance", 202, "Plates of Protection", "Aegis", "uow", false },
    { "Marauder's Mark", 203, "Plates of Protection", "Berserker Axe", "7wa", false },
    { "Girdle of Resilience", 204, "Plates of Protection", "Colossus Girdle", "uhc", false },
    { "Crippling Conch", 205, "Black Tempest", "Giant Conch", "uhl", false },
    { "Sub-Zero Sash", 206, "Black Tempest", "Spiderweb Sash", "ulc", false },
    { "Band of Permafrost", 207, "Black Tempest", "Ring", "rin", false },
    { "Morality", 208, "Memento Mori", "Cryptic Axe", "7pa", false },
    { "Remembrance", 209, "Memento Mori", "Bone Visage", "uh9", false },
    { "Harbinger", 210, "Memento Mori", "Boneweave", "uhn", false },
    { "Promethium", 211, "Cascading Caldera", "Amulet", "amu", false },
    { "Searing Step", 212, "Cascading Caldera", "Scarabshell Boots", "uvb", false },
    { "Flameward", 213, "Cascading Caldera", "Gothic Shield", "gts", false },
    { "Vortex1", 214, "Path of the Vortex", "Scissors Suwayyah", "7qr", false },
    { "Maelstrom1", 215, "Path of the Vortex", "Scissors Suwayyah", "7qr", false },
    { "Vortex2", 216, "Path of the Vortex2", "Glorious Axe", "7gi", false },
    { "Maelstrom2", 217, "Path of the Vortex2", "Glorious Axe", "7gi", false },
    { "Great Warrior", 218, "Blacklight", "Guardian Crown", "baf", false },
    { "Great Defender", 219, "Blacklight", "Blade Barrier", "upk", false },
    { "Great Warrior2", 220, "Blacklight2", "Conqueror Crown", "urn", false },
    { "Great Defender2", 221, "Blacklight2", "Zakarum Shield", "pae", false },

};

static UniqueItemEntry g_StaticUniqueItems[] = {
{ 0, 0, "The Gnasher", "hax", "Hand Axe", false }, { 1, 1, "Deathspade", "axe", "Axe", false }, { 2, 2, "Bladebone", "2ax", "Double Axe", false }, { 3, 3, "Mindrend", "mpi", "Military Pick", false }, { 4, 4, "Rakescar", "wax", "War Axe", false },
{ 5, 5, "Fechmars Axe", "lax", "Large Axe", false }, { 6, 6, "Goreshovel", "bax", "Broad Axe", false }, { 7, 7, "The Chieftan", "btx", "Battle Axe", false }, { 8, 8, "Brainhew", "gax", "Great Axe", false }, { 9, 9, "The Humongous", "gix", "Giant Axe", false },
{ 10, 10, "Iros Torch", "wnd", "Wand", false }, { 11, 11, "Maelstromwrath", "ywn", "Yew Wand", false }, { 12, 12, "Gravenspine", "bwn", "Bone Wand", false }, { 13, 13, "Umes Lament", "gwn", "Grim Wand", false }, { 14, 14, "Felloak", "clb", "Club", false },
{ 15, 15, "Knell Striker", "scp", "Scepter", false }, { 16, 16, "Rusthandle", "gsc", "Grand Scepter", false }, { 17, 17, "Stormeye", "wsp", "War Scepter", false }, { 18, 18, "Stoutnail", "spc", "Spiked Club", false }, { 19, 19, "Crushflange", "mac", "Mace", false },
{ 20, 20, "Bloodrise", "mst", "Morning Star", false }, { 21, 21, "The Generals Tan Do Li Ga", "fla", "Flail", false }, { 22, 22, "Ironstone", "whm", "War Hammer", false }, { 23, 23, "Bonesob", "mau", "Maul", false }, { 24, 24, "Steeldriver", "gma", "Great Maul", false },
{ 25, 25, "Rixots Keen", "ssd", "Short Sword", false }, { 26, 26, "Blood Crescent", "scm", "Scimitar", false }, { 27, 27, "Krintizs Skewer", "sbr", "Saber", false }, { 28, 28, "Gleamscythe", "flc", "Falchion", false }, { 29, 30, "Griswolds Edge", "bsd", "Broad Sword", false },
{ 30, 31, "Hellplague", "lsd", "Long Sword", false }, { 31, 32, "Culwens Point", "wsd", "War Sword", false }, { 32, 33, "Shadowfang", "2hs", "2-Handed Sword", false }, { 33, 34, "Soulflay", "clm", "Claymore", false }, { 34, 35, "Kinemils Awl", "gis", "Giant Sword", false },
{ 35, 36, "Blacktongue", "bsw", "Bastard Sword", false }, { 36, 37, "Ripsaw", "flb", "Flamberge", false }, { 37, 38, "The Patriarch", "gsd", "Great Sword", false }, { 38, 39, "Gull", "dgr", "Dagger", false }, { 39, 40, "The Diggler", "dir", "Dirk", false },
{ 40, 41, "The Jade Tan Do", "kri", "Kris", false }, { 41, 42, "Irices Shard", "bld", "Blade", false }, { 42, 43, "The Dragon Chang", "spr", "Spear", false }, { 43, 44, "Razortine", "tri", "Trident", false }, { 44, 45, "Bloodthief", "brn", "Brandistock", false },
{ 45, 46, "Lance of Yaggai", "spt", "Spetum", false }, { 46, 47, "The Tannr Gorerod", "pik", "Pike", false }, { 47, 48, "Dimoaks Hew", "bar", "Bardiche", false }, { 48, 49, "Steelgoad", "vou", "Voulge", false }, { 49, 50, "Soul Harvest", "scy", "Scythe", false },
{ 50, 51, "The Battlebranch", "pax", "Poleaxe", false }, { 51, 52, "Woestave", "hal", "Halberd", false }, { 52, 53, "The Grim Reaper", "wsc", "War Scythe", false }, { 53, 54, "Bane Ash", "sst", "Short Staff", false }, { 54, 55, "Serpent Lord", "lst", "Long Staff", false },
{ 55, 56, "Lazarus Spire", "cst", "Gnarled Staff", false }, { 56, 57, "The Salamander", "bst", "Battle Staff", false }, { 57, 58, "The Iron Jang Bong", "wst", "War Staff", false }, { 58, 59, "Pluckeye", "sbw", "Short Bow", false }, { 59, 60, "Witherstring", "hbw", "Hunter's Bow", false },
{ 60, 61, "Rimeraven", "lbw", "Long Bow", false }, { 61, 62, "Piercerib", "cbw", "Composite Bow", false }, { 62, 63, "Pullspite", "sbb", "Short Battle Bow", false }, { 63, 64, "Wizendraw", "lbb", "Long Battle Bow", false }, { 64, 65, "Hellclap", "swb", "Short War Bow", false },
{ 65, 66, "Blastbark", "lwb", "Long War Bow", false }, { 66, 67, "Leadcrow", "lxb", "Light Crossbow", false }, { 67, 68, "Ichorsting", "mxb", "Crossbow", false }, { 68, 69, "Hellcast", "hxb", "Heavy Crossbow", false }, { 69, 70, "Doomspittle", "rxb", "Repeating Crossbow", false },
{ 70, 71, "War Bonnet", "cap", "Cap", false }, { 71, 72, "Tarnhelm", "skp", "Skull Cap", false }, { 72, 73, "Coif of Glory", "hlm", "Helm", false }, { 73, 74, "Duskdeep", "fhl", "Full Helm", false }, { 74, 75, "Wormskull", "bhm", "Bone Helm", false },
{ 75, 76, "Howltusk", "ghm", "Great Helm", false }, { 76, 77, "Undead Crown", "crn", "Crown", false }, { 77, 78, "The Face of Horror", "msk", "Mask", false }, { 78, 79, "Greyform", "qui", "Quilted Armor", false }, { 79, 80, "Blinkbats Form", "lea", "Leather Armor", false },
{ 80, 81, "The Centurion", "hla", "Hard Leather", false }, { 81, 82, "Twitchthroe", "stu", "Studded Leather", false }, { 82, 83, "Darkglow", "rng", "Ring Mail", false }, { 83, 84, "Hawkmail", "scl", "Scale Mail", false }, { 84, 85, "Sparking Mail", "chn", "Chain Mail", false },
{ 85, 86, "Venomsward", "brs", "Breast Plate", false }, { 86, 87, "Iceblink", "spl", "Splint Mail", false }, { 87, 88, "Boneflesh", "plt", "Plate Mail", false }, { 88, 89, "Rockfleece", "fld", "Field Plate", false }, { 89, 90, "Rattlecage", "gth", "Gothic Plate", false },
{ 90, 91, "Goldskin", "ful", "Full Plate Mail", false }, { 91, 92, "Victors Silk", "aar", "AncientArmor", false }, { 92, 93, "Heavenly Garb", "ltp", "Light Plate", false }, { 93, 94, "Pelta Lunata", "buc", "Buckler", false }, { 94, 95, "Umbral Disk", "sml", "Small Shield", false },
{ 95, 96, "Stormguild", "lrg", "Large Shield", false }, { 96, 97, "Wall of the Eyeless", "bsh", "Bone Shield", false }, { 97, 98, "Swordback Hold", "spk", "Spiked Shield", false }, { 98, 99, "Steelclash", "kit", "Kite Shield", false }, { 99, 100, "Bverrit Keep", "tow", "Tower Shield", false },
{ 100, 101, "The Ward", "gts", "Gothic Shield", false }, { 101, 102, "The Hand of Broc", "lgl", "Gloves", false }, { 102, 103, "Bloodfist", "vgl", "Heavy Gloves", false }, { 103, 104, "Chance Guards", "mgl", "Bracers", false }, { 104, 105, "Magefist", "tgl", "Light Gauntlets", false },
{ 105, 106, "Frostburn", "hgl", "Gauntlets", false }, { 106, 107, "Hotspur", "lbt", "Leather Boots", false }, { 107, 108, "Gorefoot", "vbt", "Heavy Boots", false }, { 108, 109, "Treads of Cthon", "mbt", "Chain Boots", false }, { 109, 110, "Goblin Toe", "tbt", "Light Plate Boots", false },
{ 110, 111, "Tearhaunch", "hbt", "Plate Boots", false }, { 111, 112, "Lenyms Cord", "lbl", "Sash", false }, { 112, 113, "Snakecord", "vbl", "Light Belt", false }, { 113, 114, "Nightsmoke", "mbl", "Belt", false }, { 114, 115, "Goldwrap", "tbl", "Heavy Belt", false },
{ 115, 116, "Bladebuckle", "hbl", "Girdle", false }, { 116, 117, "Nokozan Relic", "amu", "Amulet", false }, { 117, 118, "The Eye of Etlich", "amu", "Amulet", false }, { 118, 119, "The Mahim-Oak Curio", "amu", "Amulet", false }, { 119, 120, "Nagelring", "rin", "Ring", false },
{ 120, 121, "Manald Heal", "rin", "Ring", false }, { 121, 122, "The Stone of Jordan", "rin", "Ring", false }, { 122, 123, "Amulet of the Viper", "vip", "Amulet", false }, { 123, 124, "Staff of Kings", "msf", "Staff", false }, { 124, 125, "Horadric Staff", "hst", "Staff", false },
{ 125, 126, "Hell Forge Hammer", "hfh", "Hammer", false }, { 126, 127, "KhalimFlail", "qf1", "Flail", false }, { 127, 128, "SuperKhalimFlail", "qf2", "Flail", false }, { 128, 129, "Coldkill", "9ha", "Hatchet", false }, { 129, 130, "Butcher's Pupil", "9ax", "Cleaver", false },
{ 130, 131, "Islestrike", "92a", "Twin Axe", false }, { 131, 132, "Pompe's Wrath", "9mp", "Crowbill", false }, { 132, 133, "Guardian Naga", "9wa", "Naga", false }, { 133, 134, "Warlord's Trust", "9la", "Military Axe", false }, { 134, 135, "Spellsteel", "9ba", "Bearded Axe", false },
{ 135, 136, "Stormrider", "9bt", "Tabar", false }, { 136, 137, "Boneslayer Blade", "9ga", "Gothic Axe", false }, { 137, 138, "The Minataur", "9gi", "Ancient Axe", false }, { 138, 139, "Suicide Branch", "9wn", "Burnt Wand", false }, { 139, 140, "Carin Shard", "9yw", "Petrified Wand", false },
{ 140, 141, "Arm of King Leoric", "9bw", "Tomb Wand", false }, { 141, 142, "Blackhand Key", "9gw", "Grave Wand", false }, { 142, 143, "Dark Clan Crusher", "9cl", "Cudgel", false }, { 143, 144, "Zakarum's Hand", "9sc", "Rune Scepter", false }, { 144, 145, "The Fetid Sprinkler", "9qs", "Holy Water Sprinkler", false },
{ 145, 146, "Hand of Blessed Light", "9ws", "Divine Scepter", false }, { 146, 147, "Fleshrender", "9sp", "Barbed Club", false }, { 147, 148, "Sureshrill Frost", "9ma", "Flanged Mace", false }, { 148, 149, "Moonfall", "9mt", "Jagged Star", false }, { 149, 150, "Baezil's Vortex", "9fl", "Knout", false },
{ 150, 151, "Earthshaker", "9wh", "Battle Hammer", false }, { 151, 152, "Bloodtree Stump", "9m9", "War Club", false }, { 152, 153, "The Gavel of Pain", "9gm", "Martel de Fer", false }, { 153, 154, "Bloodletter", "9ss", "Gladius", false }, { 154, 155, "Coldsteel Eye", "9sm", "Cutlass", false },
{ 155, 156, "Hexfire", "9sb", "Shamshir", false }, { 156, 157, "Blade of Ali Baba", "9fc", "Tulwar", false }, { 157, 158, "Ginther's Rift", "9cr", "Dimensional Blade", false }, { 158, 159, "Headstriker", "9bs", "Battle Sword", false }, { 159, 160, "Plague Bearer", "9ls", "Rune Sword", false },
{ 160, 161, "The Atlantian", "9wd", "Ancient Sword", false }, { 161, 162, "Crainte Vomir", "92h", "Espadon", false }, { 162, 163, "Bing Sz Wang", "9cm", "Dacian Falx", false }, { 163, 164, "The Vile Husk", "9gs", "Tusk Sword", false }, { 164, 165, "Cloudcrack", "9b9", "Gothic Sword", false },
{ 165, 166, "Todesfaelle Flamme", "9fb", "Zweihander", false }, { 166, 167, "Swordguard", "9gd", "Executioner Sword", false }, { 167, 168, "Spineripper", "9dg", "Poignard", false }, { 168, 169, "Heart Carver", "9di", "Rondel", false }, { 169, 170, "Blackbog's Sharp", "9kr", "Cinquedeas", false },
{ 170, 171, "Stormspike", "9bl", "Stilleto", false }, { 171, 172, "The Impaler", "9sr", "War Spear", false }, { 172, 173, "Kelpie Snare", "9tr", "Fuscina", false }, { 173, 174, "Soulfeast Tine", "9br", "War Fork", false }, { 174, 175, "Hone Sundan", "9st", "Yari", false },
{ 175, 176, "Spire of Honor", "9p9", "Lance", false }, { 176, 177, "The Meat Scraper", "9b7", "Lochaber Axe", false }, { 177, 178, "Blackleach Blade", "9vo", "Bill", false }, { 178, 179, "Athena's Wrath", "9s8", "Battle Scythe", false }, { 179, 180, "Pierre Tombale Couant", "9pa", "Partizan", false },
{ 180, 181, "Husoldal Evo", "9h9", "Bec-de-Corbin", false }, { 181, 182, "Grim's Burning Dead", "9wc", "Grim Scythe", false }, { 182, 183, "Razorswitch", "8ss", "Jo Stalf", false }, { 183, 184, "Ribcracker", "8ls", "Quarterstaff", false }, { 184, 185, "Chromatic Ire", "8cs", "Cedar Staff", false },
{ 185, 186, "Warpspear", "8bs", "Gothic Staff", false }, { 186, 187, "Skullcollector", "8ws", "Rune Staff", false }, { 187, 188, "Skystrike", "8sb", "Edge Bow", false }, { 188, 189, "Riphook", "8hb", "Razor Bow", false }, { 189, 190, "Kuko Shakaku", "8lb", "CedarBow", false },
{ 190, 191, "Endlesshail", "8cb", "Double Bow", false }, { 191, 192, "Whichwild String", "8s8", "Short Siege Bow", false }, { 192, 193, "Cliffkiller", "8l8", "Long Siege Bow", false }, { 193, 194, "Magewrath", "8sw", "Rune Bow", false }, { 194, 195, "Godstrike Arch", "8lw", "Gothic Bow", false },
{ 195, 196, "Langer Briser", "8lx", "Arbalest", false }, { 196, 197, "Pus Spiter", "8mx", "Siege Crossbow", false }, { 197, 198, "Buriza-Do Kyanon", "8hx", "Balista", false }, { 198, 199, "Demon Machine", "8rx", "Chu-Ko-Nu", false }, { 199, 201, "Peasent Crown", "xap", "War Hat", false },
{ 200, 202, "Rockstopper", "xkp", "Sallet", false }, { 201, 203, "Stealskull", "xlm", "Casque", false }, { 202, 204, "Darksight Helm", "xhl", "Basinet", false }, { 203, 205, "Valkiry Wing", "xhm", "Winged Helm", false }, { 204, 206, "Crown of Thieves", "xrn", "Grand Crown", false },
{ 205, 207, "Blackhorn's Face", "xsk", "Death Mask", false }, { 206, 208, "Vampiregaze", "xh9", "Grim Helm", false }, { 207, 209, "The Spirit Shroud", "xui", "Ghost Armor", false }, { 208, 210, "Skin of the Vipermagi", "xea", "SerpentSkin Armor", false }, { 209, 211, "Skin of the Flayerd One", "xla", "Demonhide Armor", false },
{ 210, 212, "Ironpelt", "xtu", "Tresllised Armor", false }, { 211, 213, "Spiritforge", "xng", "Linked Mail", false }, { 212, 214, "Crow Caw", "xcl", "Tigulated Mail", false }, { 213, 215, "Shaftstop", "xhn", "Mesh Armor", false }, { 214, 216, "Duriel's Shell", "xrs", "Cuirass", false },
{ 215, 217, "Skullder's Ire", "xpl", "Russet Armor", false }, { 216, 218, "Guardian Angel", "xlt", "Templar Coat", false }, { 217, 219, "Toothrow", "xld", "Sharktooth Armor", false }, { 218, 220, "Atma's Wail", "xth", "Embossed Plate", false }, { 219, 221, "Black Hades", "xul", "Chaos Armor", false },
{ 220, 222, "Corpsemourn", "xar", "Ornate Armor", false }, { 221, 223, "Que-Hegan's Wisdon", "xtp", "Mage Plate", false }, { 222, 224, "Visceratuant", "xuc", "Defender", false }, { 223, 225, "Mosers Blessed Circle", "xml", "Round Shield", false }, { 224, 226, "Stormchaser", "xrg", "Scutum", false },
{ 225, 227, "Tiamat's Rebuke", "xit", "Dragon Shield", false }, { 226, 228, "Kerke's Sanctuary", "xow", "Pavise", false }, { 227, 229, "Radimant's Sphere", "xts", "Ancient Shield", false }, { 228, 230, "Lidless Wall", "xsh", "Grim Shield", false }, { 229, 231, "Lance Guard", "xpk", "Barbed Shield", false },
{ 230, 232, "Venom Grip", "xlg", "Demonhide Gloves", false }, { 231, 233, "Gravepalm", "xvg", "Sharkskin Gloves", false }, { 232, 234, "Ghoulhide", "xmg", "Heavy Bracers", false }, { 233, 235, "Lavagout", "xtg", "Battle Guantlets", false }, { 234, 236, "Hellmouth", "xhg", "War Gauntlets", false },
{ 235, 237, "Infernostride", "xlb", "Demonhide Boots", false }, { 236, 238, "Waterwalk", "xvb", "Sharkskin Boots", false }, { 237, 239, "Silkweave", "xmb", "Mesh Boots", false }, { 238, 240, "Wartraveler", "xtb", "Battle Boots", false }, { 239, 241, "Gorerider", "xhb", "War Boots", false },
{ 240, 242, "String of Ears", "zlb", "Demonhide Sash", false }, { 241, 243, "Razortail", "zvb", "Sharkskin Belt", false }, { 242, 244, "Gloomstrap", "zmb", "Mesh Belt", false }, { 243, 245, "Snowclash", "ztb", "Battle Belt", false }, { 244, 246, "Thudergod's Vigor", "zhb", "War Belt", false },
{ 245, 248, "Harlequin Crest", "uap", "Shako", false }, { 246, 249, "Veil of Steel", "uhm", "Spired Helm", false }, { 247, 250, "The Gladiator's Bane", "utu", "Wire Fleece", false }, { 248, 251, "Arkaine's Valor", "upl", "Balrog Skin", false }, { 249, 252, "Blackoak Shield", "uml", "Luna", false },
{ 250, 253, "Stormshield", "uit", "Monarch", false }, { 251, 254, "Hellslayer", "7bt", "Decapitator", false }, { 252, 255, "Messerschmidt's Reaver", "7ga", "Champion Axe", false }, { 253, 256, "Baranar's Star", "7mt", "Devil Star", false }, { 254, 257, "Schaefer's Hammer", "7wh", "Legendary Mallet", false },
{ 255, 258, "The Cranium Basher", "7gm", "Thunder Maul", false }, { 256, 259, "Lightsabre", "7cr", "Phase Blade", false }, { 257, 260, "Doombringer", "7b7", "Champion Sword", false }, { 258, 261, "The Grandfather", "7gd", "Colossus Blade", false }, { 259, 262, "Wizardspike", "7dg", "Bone Knife", false },
{ 260, 264, "Stormspire", "7wc", "Giant Thresher", false }, { 261, 265, "Eaglehorn", "6l7", "Crusader Bow", false }, { 262, 266, "Windforce", "6lw", "Hydra Bow", false }, { 263, 268, "Bul Katho's Wedding Band", "rin", "Ring", false }, { 264, 269, "The Cat's Eye", "amu", "Amulet", false },
{ 265, 270, "The Rising Sun", "amu", "Amulet", false }, { 266, 271, "Crescent Moon", "amu", "Amulet", false }, { 267, 272, "Mara's Kaleidoscope", "amu", "Amulet", false }, { 268, 273, "Atma's Scarab", "amu", "Amulet", false }, { 269, 274, "Dwarf Star", "rin", "Ring", false },
{ 270, 275, "Raven Frost", "rin", "Ring", false }, { 271, 276, "Highlord's Wrath", "amu", "Amulet", false }, { 272, 277, "Saracen's Chance", "amu", "Amulet", false }, { 273, 279, "Arreat's Face", "baa", "Slayer Guard", false }, { 274, 280, "Homunculus", "nea", "Heirophant Trophy", false },
{ 275, 281, "Titan's Revenge", "ama", "Ceremonial Javelin", false }, { 276, 282, "Lycander's Aim", "am7", "Ceremonial Bow", false }, { 277, 283, "Lycander's Flank", "am9", "Ceremonial Pike", false }, { 278, 284, "The Oculus", "oba", "Swirling Crystal", false }, { 279, 285, "Herald of Zakarum", "pa9", "Aerin Shield", false },
{ 280, 286, "Cutthroat1", "9tw", "Runic Talons", false }, { 281, 287, "Jalal's Mane", "dra", "Dream Spirit", false }, { 282, 288, "The Scalper", "9ta", "Francisca", false }, { 283, 289, "Bloodmoon", "7sb", "elegant blade", false }, { 284, 290, "Djinnslayer", "7sm", "ataghan", false },
{ 285, 291, "Deathbit", "9tk", "battle dart", false }, { 286, 292, "Warshrike", "7bk", "winged knife", false }, { 287, 293, "Gutsiphon", "6rx", "demon crossbow", false }, { 288, 294, "Razoredge", "7ha", "tomahawk", false }, { 289, 296, "Demonlimb", "7sp", "tyrant club", false },
{ 290, 297, "Steelshade", "ulm", "armet", false }, { 291, 298, "Tomb Reaver", "7pa", "cryptic axe", false }, { 292, 299, "Deaths's Web", "7gw", "unearthed wand", false }, { 293, 300, "Nature's Peace", "rin", "ring", false }, { 294, 301, "Azurewrath", "7cr", "phase blade", false },
{ 295, 302, "Seraph's Hymn", "amu", "amulet", false }, { 296, 304, "Fleshripper", "7kr", "fanged knife", false }, { 297, 306, "Horizon's Tornado", "7fl", "scourge", false }, { 298, 307, "Stone Crusher", "7wh", "legendary mallet", false }, { 299, 308, "Jadetalon", "7wb", "wrist sword", false },
{ 300, 309, "Shadowdancer", "uhb", "myrmidon greaves", false }, { 301, 310, "Cerebus", "drb", "blood spirit", false }, { 302, 311, "Tyrael's Might", "uar", "sacred armor", false }, { 303, 312, "Souldrain", "umg", "vambraces", false }, { 304, 313, "Runemaster", "72a", "ettin axe", false },
{ 305, 314, "Deathcleaver", "7wa", "berserker axe", false }, { 306, 315, "Executioner's Justice", "7gi", "glorious axe", false }, { 307, 316, "Stoneraven", "amd", "matriarchal spear", false }, { 308, 317, "Leviathan", "uld", "kraken shell", false }, { 309, 319, "Wisp", "rin", "ring", false },
{ 310, 320, "Gargoyle's Bite", "7ts", "winged harpoon", false }, { 311, 321, "Lacerator", "7b8", "winged axe", false }, { 312, 322, "Mang Song's Lesson", "6ws", "archon staff", false }, { 313, 323, "Viperfork", "7br", "war fork", false }, { 314, 324, "Ethereal Edge", "7ba", "silver-edged axe", false },
{ 315, 325, "Demonhorn's Edge", "bad", "destroyer helm", false }, { 316, 326, "The Reaper's Toll", "7s8", "thresher", false }, { 317, 327, "Spiritkeeper", "drd", "earth spirit", false }, { 318, 328, "Hellrack", "6hx", "colossus crossbow", false }, { 319, 329, "Alma Negra", "pac", "sacred rondache", false },
{ 320, 330, "Darkforge Spawn", "nef", "bloodlord skull", false }, { 321, 331, "Widowmaker", "6sw", "ward bow", false }, { 322, 332, "Bloodraven's Charge", "amb", "matriarchal bow", false }, { 323, 333, "Ghostflame", "7bl", "legend spike", false }, { 324, 334, "Shadowkiller", "7cs", "battle cestus", false },
{ 325, 335, "Gimmershred", "7ta", "flying axe", false }, { 326, 336, "Griffon's Eye", "ci3", "diadem", false }, { 327, 337, "Windhammer", "7m7", "ogre maul", false }, { 328, 338, "Thunderstroke", "amf", "matriarchal javelin", false }, { 329, 340, "Demon's Arch", "7s7", "balrog spear", false },
{ 330, 341, "Boneflame", "nee", "succubae skull", false }, { 331, 342, "Steelpillar", "7p7", "war pike", false }, { 332, 343, "Nightwing's Veil", "uhm", "spired helm", false }, { 333, 344, "Crown of Ages", "urn", "corona", false }, { 334, 345, "Andariel's Visage", "usk", "demonhead", false },
{ 335, 347, "Dragonscale", "pae", "zakarum shield", false }, { 336, 348, "Steel Carapice", "uul", "shadow plate", false }, { 337, 349, "Medusa's Gaze", "uow", "aegis", false }, { 338, 350, "Ravenlore", "dre", "sky spirit", false }, { 339, 351, "Boneshade", "7bw", "lich wand", false },
{ 340, 353, "Flamebellow", "7gs", "balrog blade", false }, { 341, 354, "Fathom", "obf", "dimensional shard", false }, { 342, 355, "Wolfhowl", "bac", "fury visor", false }, { 343, 356, "Spirit Ward", "uts", "ward", false }, { 344, 357, "Kira's Guardian", "ci2", "tiara", false },
{ 345, 358, "Ormus' Robes", "uui", "dusk shroud", false }, { 346, 359, "Gheed's Fortune", "cm3", "charm", false }, { 347, 360, "Stormlash", "7fl", "scourge", false }, { 348, 361, "Halaberd's Reign", "bae", "conqueror crown", false }, { 349, 363, "Spike Thorn", "upk", "blade barrier", false },
{ 350, 364, "Dracul's Grasp", "uvg", "vampirebone gloves", false }, { 351, 365, "Frostwind", "7ls", "cryptic sword", false }, { 352, 366, "Templar's Might", "uar", "sacred armor", false }, { 353, 367, "Eschuta's temper", "obc", "eldritch orb", false }, { 354, 368, "Firelizard's Talons", "7lw", "feral claws", false },
{ 355, 369, "Sandstorm Trek", "uvb", "scarabshell boots", false }, { 356, 370, "Marrowwalk", "umb", "boneweave boots", false }, { 357, 371, "Heaven's Light", "7sc", "mighty scepter", false }, { 358, 373, "Arachnid Mesh", "ulc", "spiderweb sash", false }, { 359, 374, "Nosferatu's Coil", "uvc", "vampirefang belt", false },
{ 360, 375, "Metalgrid", "amu", "amulet", false }, { 361, 376, "Verdugo's Hearty Cord", "umc", "mithril coil", false }, { 362, 378, "Carrion Wind", "rin", "ring", false }, { 363, 379, "Giantskull", "uh9", "bone visage", false }, { 364, 380, "Ironward", "7ws", "caduceus", false },
{ 365, 381, "Annihilus", "cm1", "charm", false }, { 366, 382, "Arioc's Needle", "7sr", "hyperion spear", false }, { 367, 383, "Cranebeak", "7mp", "war spike", false }, { 368, 384, "Nord's Tenderizer", "7cl", "truncheon", false }, { 369, 385, "Earthshifter", "7gm", "thunder maul", false },
{ 370, 386, "Wraithflight", "7gl", "ghost glaive", false }, { 371, 387, "Bonehew", "7o7", "ogre axe", false }, { 372, 388, "Ondal's Wisdom", "6cs", "elder staff", false }, { 373, 389, "The Reedeemer", "7sc", "mighty scepter", false }, { 374, 390, "Headhunter's Glory", "ush", "troll nest", false },
{ 375, 391, "Steelrend", "uhg", "ogre gauntlets", false }, { 376, 392, "Rainbow Facet", "jew", "jewel", false }, { 377, 393, "Rainbow Facet", "jew", "jewel", false }, { 378, 394, "Rainbow Facet", "jew", "jewel", false }, { 379, 395, "Rainbow Facet", "jew", "jewel", false },
{ 380, 396, "Rainbow Facet", "jew", "jewel", false }, { 381, 397, "Rainbow Facet", "jew", "jewel", false }, { 382, 398, "Rainbow Facet", "jew", "jewel", false }, { 383, 399, "Rainbow Facet", "jew", "jewel", false }, { 384, 400, "Hellfire Torch", "cm2", "charm", false },
{ 385, 401, "Cold Rupture", "cm3", "charm", false }, { 386, 402, "Flame Rift", "cm3", "charm", false }, { 387, 403, "Crack of the Heavens", "cm3", "charm", false }, { 388, 404, "Rotting Fissure", "cm3", "charm", false }, { 389, 405, "Bone Break", "cm3", "charm", false },
{ 390, 406, "Black Cleft", "cm3", "charm", false },
};

static SetItemEntry g_StaticSetItems[] = {
    { "Civerb's Ward", 0, "Civerb's Vestments", "Large Shield", "lrg", false },
    { "Civerb's Icon", 1, "Civerb's Vestments", "Amulet", "amu", false },
    { "Civerb's Cudgel", 2, "Civerb's Vestments", "Grand Scepter", "gsc", false },
    { "Hsarus' Iron Heel", 3, "Hsarus' Defense", "Chain Boots", "mbt", false },
    { "Hsarus' Iron Fist", 4, "Hsarus' Defense", "Buckler", "buc", false },
    { "Hsarus' Iron Stay", 5, "Hsarus' Defense", "Belt", "mbl", false },
    { "Cleglaw's Tooth", 6, "Cleglaw's Brace", "Long Sword", "lsd", false },
    { "Cleglaw's Claw", 7, "Cleglaw's Brace", "Small Shield", "sml", false },
    { "Cleglaw's Pincers", 8, "Cleglaw's Brace", "Chain Gloves", "mgl", false },
    { "Iratha's Collar", 9, "Iratha's Finery", "Amulet", "amu", false },
    { "Iratha's Cuff", 10, "Iratha's Finery", "Light Gauntlets", "tgl", false },
    { "Iratha's Coil", 11, "Iratha's Finery", "Crown", "crn", false },
    { "Iratha's Cord", 12, "Iratha's Finery", "Heavy Belt", "tbl", false },
    { "Isenhart's Lightbrand", 13, "Isenhart's Armory", "Broad Sword", "bsd", false },
    { "Isenhart's Parry", 14, "Isenhart's Armory", "Gothic Shield", "gts", false },
    { "Isenhart's Case", 15, "Isenhart's Armory", "Breast Plate", "brs", false },
    { "Isenhart's Horns", 16, "Isenhart's Armory", "Full Helm", "fhl", false },
    { "Vidala's Barb", 17, "Vidala's Rig", "Long Battle Bow", "lbb", false },
    { "Vidala's Fetlock", 18, "Vidala's Rig", "Light Plated Boots", "tbt", false },
    { "Vidala's Ambush", 19, "Vidala's Rig", "Leather Armor", "lea", false },
    { "Vidala's Snare", 20, "Vidala's Rig", "Amulet", "amu", false },
    { "Milabrega's Orb", 21, "Milabrega's Regalia", "Kite Shield", "kit", false },
    { "Milabrega's Rod", 22, "Milabrega's Regalia", "War Scepter", "wsp", false },
    { "Milabrega's Diadem", 23, "Milabrega's Regalia", "Crown", "crn", false },
    { "Milabrega's Robe", 24, "Milabrega's Regalia", "Ancient Armor", "aar", false },
    { "Cathan's Rule", 25, "Cathan's Traps", "Battle Staff", "bst", false },
    { "Cathan's Mesh", 26, "Cathan's Traps", "Chain Mail", "chn", false },
    { "Cathan's Visage", 27, "Cathan's Traps", "Mask", "msk", false },
    { "Cathan's Sigil", 28, "Cathan's Traps", "Amulet", "amu", false },
    { "Cathan's Seal", 29, "Cathan's Traps", "Ring", "rin", false },
    { "Tancred's Crowbill", 30, "Tancred's Battlegear", "Military Pick", "mpi", false },
    { "Tancred's Spine", 31, "Tancred's Battlegear", "Full Plate Mail", "ful", false },
    { "Tancred's Hobnails", 32, "Tancred's Battlegear", "Boots", "lbt", false },
    { "Tancred's Weird", 33, "Tancred's Battlegear", "Amulet", "amu", false },
    { "Tancred's Skull", 34, "Tancred's Battlegear", "Bone Helm", "bhm", false },
    { "Sigon's Gage", 35, "Sigon's Complete Steel", "Gauntlets", "hgl", false },
    { "Sigon's Visor", 36, "Sigon's Complete Steel", "Great Helm", "ghm", false },
    { "Sigon's Shelter", 37, "Sigon's Complete Steel", "Gothic Plate", "gth", false },
    { "Sigon's Sabot", 38, "Sigon's Complete Steel", "Greaves", "hbt", false },
    { "Sigon's Wrap", 39, "Sigon's Complete Steel", "Plated Belt", "hbl", false },
    { "Sigon's Guard", 40, "Sigon's Complete Steel", "Tower Shield", "tow", false },
    { "Infernal Cranium", 41, "Infernal Tools", "Cap", "cap", false },
    { "Infernal Torch", 42, "Infernal Tools", "Grim Wand", "gwn", false },
    { "Infernal Sign", 43, "Infernal Tools", "Heavy Belt", "tbl", false },
    { "Berserker's Headgear", 44, "Berserker's Garb", "Helm", "hlm", false },
    { "Berserker's Hauberk", 45, "Berserker's Garb", "Splint Mail", "spl", false },
    { "Berserker's Hatchet", 46, "Berserker's Garb", "Double Axe", "2ax", false },
    { "Death's Hand", 47, "Death's Disguise", "Leather Gloves", "lgl", false },
    { "Death's Guard", 48, "Death's Disguise", "Sash", "lbl", false },
    { "Death's Touch", 49, "Death's Disguise", "War Sword", "wsd", false },
    { "Angelic Sickle", 50, "Angelical Raiment", "Sabre", "sbr", false },
    { "Angelic Mantle", 51, "Angelical Raiment", "Ring Mail", "rng", false },
    { "Angelic Halo", 52, "Angelical Raiment", "Ring", "rin", false },
    { "Angelic Wings", 53, "Angelical Raiment", "Amulet", "amu", false },
    { "Arctic Horn", 54, "Arctic Gear", "Short War Bow", "swb", false },
    { "Arctic Furs", 55, "Arctic Gear", "Quilted Armor", "qui", false },
    { "Arctic Binding", 56, "Arctic Gear", "Light Belt", "vbl", false },
    { "Arctic Mitts", 57, "Arctic Gear", "Light Gauntlets", "tgl", false },
    { "Arcanna's Sign", 58, "Arcanna's Tricks", "Amulet", "amu", false },
    { "Arcanna's Deathwand", 59, "Arcanna's Tricks", "War Staff", "wst", false },
    { "Arcanna's Head", 60, "Arcanna's Tricks", "Skull Cap", "skp", false },
    { "Arcanna's Flesh", 61, "Arcanna's Tricks", "Light Plate", "ltp", false },
    { "Natalya's Totem", 62, "Natalya's Odium", "Grim Helm", "xh9", false },
    { "Natalya's Mark", 63, "Natalya's Odium", "Scissors Suwayyah", "7qr", false },
    { "Natalya's Shadow", 64, "Natalya's Odium", "Loricated Mail", "ucl", false },
    { "Natalya's Soul", 65, "Natalya's Odium", "Mesh Boots", "xmb", false },
    { "Aldur's Stony Gaze", 66, "Aldur's Watchtower", "Hunter's Guise", "dr8", false },
    { "Aldur's Deception", 67, "Aldur's Watchtower", "Shadow Plate", "uul", false },
    { "Aldur's Gauntlet", 68, "Aldur's Watchtower", "Jagged Star", "9mt", false },
    { "Aldur's Advance", 69, "Aldur's Watchtower", "Battle Boots", "xtb", false },
    { "Immortal King's Will", 70, "Immortal King", "Avenger Guard", "ba5", false },
    { "Immortal King's Soul Cage", 71, "Immortal King", "Sacred Armor", "uar", false },
    { "Immortal King's Detail", 72, "Immortal King", "War Belt", "zhb", false },
    { "Immortal King's Forge", 73, "Immortal King", "War Gauntlets", "xhg", false },
    { "Immortal King's Pillar", 74, "Immortal King", "War Boots", "xhb", false },
    { "Immortal King's Stone Crusher", 75, "Immortal King", "Ogre Maul", "7m7", false },
    { "Tal Rasha's Fire-Spun Cloth", 76, "Tal Rasha's Wrappings", "Mesh Belt", "zmb", false },
    { "Tal Rasha's Adjudication", 77, "Tal Rasha's Wrappings", "Amulet", "amu", false },
    { "Tal Rasha's Lidless Eye", 78, "Tal Rasha's Wrappings", "Swirling Crystal", "oba", false },
    { "Tal Rasha's Howling Wind", 79, "Tal Rasha's Wrappings", "Lacquered Plate", "uth", false },
    { "Tal Rasha's Horadric Crest", 80, "Tal Rasha's Wrappings", "Death Mask", "xsk", false },
    { "Griswold's Valor", 81, "Griswold's Legacy", "Corona", "urn", false },
    { "Griswold's Heart", 82, "Griswold's Legacy", "Ornate Plate", "xar", false },
    { "Griswolds's Redemption", 83, "Griswold's Legacy", "Caduceus", "7ws", false },
    { "Griswold's Honor", 84, "Griswold's Legacy", "Vortex Shield", "paf", false },
    { "Trang-Oul's Guise", 85, "Trang-Oul's Avatar", "Bone Visage", "uh9", false },
    { "Trang-Oul's Scales", 86, "Trang-Oul's Avatar", "Chaos Armor", "xul", false },
    { "Trang-Oul's Wing", 87, "Trang-Oul's Avatar", "Cantor Trophy", "ne9", false },
    { "Trang-Oul's Claws", 88, "Trang-Oul's Avatar", "Heavy Bracers", "xmg", false },
    { "Trang-Oul's Girth", 89, "Trang-Oul's Avatar", "Troll Belt", "utc", false },
    { "M'avina's True Sight", 90, "M'avina's Battle Hymn", "Diadem", "ci3", false },
    { "M'avina's Embrace", 91, "M'avina's Battle Hymn", "Kraken Shell", "uld", false },
    { "M'avina's Icy Clutch", 92, "M'avina's Battle Hymn", "Battle Gauntlets", "xtg", false },
    { "M'avina's Tenet", 93, "M'avina's Battle Hymn", "Sharkskin Belt", "zvb", false },
    { "M'avina's Caster", 94, "M'avina's Battle Hymn", "Grand Matron Bow", "amc", false },
    { "Telling of Beads", 95, "The Disciple", "Amulet", "amu", false },
    { "Laying of Hands", 96, "The Disciple", "Bramble Mitts", "ulg", false },
    { "Rite of Passage", 97, "The Disciple", "Demonhide Boots", "xlb", false },
    { "Spiritual Custodian", 98, "The Disciple", "Dusk Shroud", "uui", false },
    { "Credendum", 99, "The Disciple", "Mithril Coil", "umc", false },
    { "Dangoon's Teaching", 100, "Heaven's Brethren", "Reinforced Mace", "7ma", false },
    { "Heaven's Taebaek", 101, "Heaven's Brethren", "Ward", "uts", false },
    { "Haemosu's Adament", 102, "Heaven's Brethren", "Cuirass", "xrs", false },
    { "Ondal's Almighty", 103, "Heaven's Brethren", "Spired Helm", "uhm", false },
    { "Guillaume's Face", 104, "Orphan's Call", "Winged Helm", "xhm", false },
    { "Wilhelm's Pride", 105, "Orphan's Call", "Battle Belt", "ztb", false },
    { "Magnus' Skin", 106, "Orphan's Call", "Sharkskin Gloves", "xvg", false },
    { "Wihtstan's Guard", 107, "Orphan's Call", "Round Shield", "xml", false },
    { "Hwanin's Splendor", 108, "Hwanin's Majesty", "Grand Crown", "xrn", false },
    { "Hwanin's Refuge", 109, "Hwanin's Majesty", "Tigulated Mail", "xcl", false },
    { "Hwanin's Seal", 110, "Hwanin's Majesty", "Belt", "mbl", false },
    { "Hwanin's Justice", 111, "Hwanin's Majesty", "Bill", "9vo", false },
    { "Sazabi's Cobalt Redeemer", 112, "Sazabi's Grand Tribute", "Cryptic Sword", "7ls", false },
    { "Sazabi's Ghost Liberator", 113, "Sazabi's Grand Tribute", "Balrog Skin", "upl", false },
    { "Sazabi's Mental Sheath", 114, "Sazabi's Grand Tribute", "Basinet", "xhl", false },
    { "Bul-Kathos' Sacred Charge", 115, "Bul-Kathos' Children", "Colossus Blade", "7gd", false },
    { "Bul-Kathos' Tribal Guardian", 116, "Bul-Kathos' Children", "Mythical Sword", "7wd", false },
    { "Cow King's Horns", 117, "Cow King's Leathers", "War Hat", "xap", false },
    { "Cow King's Hide", 118, "Cow King's Leathers", "Studded Leather", "stu", false },
    { "Cow King's Hoofs", 119, "Cow King's Leathers", "Heavy Boots", "vbt", false },
    { "Naj's Puzzler", 120, "Naj's Ancient Set", "Elder Staff", "6cs", false },
    { "Naj's Light Plate", 121, "Naj's Ancient Set", "Hellforge Plate", "ult", false },
    { "Naj's Circlet", 122, "Naj's Ancient Set", "Circlet", "ci0", false },
    { "McAuley's Paragon", 123, "McAuley's Folly", "Cap", "cap", false },
    { "McAuley's Riprap", 124, "McAuley's Folly", "Heavy Boots", "vbt", false },
    { "McAuley's Taboo", 125, "McAuley's Folly", "Heavy Gloves", "vgl", false },
    { "McAuley's Superstition", 126, "McAuley's Folly", "Bone Wand", "bwn", false },

};

#pragma endregion

#pragma region - Helper Functions

static std::vector<std::string> SplitTab(const std::string& line)
{
    std::vector<std::string> result;
    std::stringstream ss(line);
    std::string field;

    while (std::getline(ss, field, '\t'))
        result.push_back(field);

    return result;
}

static int FindColumn(const std::vector<std::string>& header, const std::string& name)
{
    for (size_t i = 0; i < header.size(); i++)
        if (header[i] == name)
            return (int)i;
    return -1;
}

static int MaxInt(int a, int b)
{
    return (a > b) ? a : b;
}

static int MaxInt3(int a, int b, int c)
{
    return MaxInt(a, MaxInt(b, c));
}

static int MaxInt4(int a, int b, int c, int d)
{
    return MaxInt(a, MaxInt(b, MaxInt(c, d)));
}

static int MaxInt5(int a, int b, int c, int d, int e)
{
    return MaxInt(a, MaxInt(b, MaxInt(c, MaxInt(d, e))));
}

static bool SafeStringToInt(const std::string& s, int& out)
{
    try {
        size_t idx = 0;
        out = std::stoi(s, &idx);
        return idx == s.size(); // ensure entire string was numeric
    }
    catch (...) {
        out = -1;
        return false;
    }
}

std::string EscapeString(const std::string& input) {
    std::string out;
    for (char c : input) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        default: out += c;
        }
    }
    return out;
}

bool GenerateStaticArrays(const std::string& filepath, int itemsPerLine = 5)
{
    std::ifstream file(filepath);
    if (!file.is_open())
        return false;

    std::ofstream outFile("StaticGrailArrays.txt");
    if (!outFile.is_open())
        return false;

    std::string line;
    std::getline(file, line); // header
    auto header = SplitTab(line);

    int colUniqueName = FindColumn(header, "index");
    int colUniqueID = FindColumn(header, "*ID");
    int colUniqueCode = FindColumn(header, "code");
    int colUniqueEnabled = FindColumn(header, "enabled");
    int colUniqueItemName = FindColumn(header, "*ItemName");

    int colSetName = FindColumn(header, "set");
    int colSetItemCode = FindColumn(header, "item");

    // ------------------------
    // Unique items
    // ------------------------
    outFile << "static UniqueItemEntry g_StaticUniqueItems[] = {\n";
    int count = 0;
    int runningIndex = 0;
    while (std::getline(file, line))
    {
        auto cols = SplitTab(line);

        if (colUniqueName >= 0 && colUniqueID >= 0 && colUniqueCode >= 0 && colUniqueEnabled >= 0 && colUniqueItemName >= 0)
        {
            int maxCol = MaxInt5(colUniqueName, colUniqueID, colUniqueCode, colUniqueEnabled, colUniqueItemName);
            if (cols.size() <= maxCol) continue;

            int idVal;
            if (!SafeStringToInt(cols[colUniqueID], idVal)) continue;

            std::string enabledStr = cols[colUniqueEnabled];
            bool enabled = (enabledStr == "1" || enabledStr == "true");
            if (!enabled) continue;

            outFile << "{ " << runningIndex << ", "  // index
                << idVal << ", \""                     // id
                << cols[colUniqueName] << "\", \""     // name
                << cols[colUniqueCode] << "\", \""      // code
                << cols[colUniqueItemName] << "\","      // ItemName
                << "false" << " }, ";             // always default to false
            count++;
            runningIndex++;  // increment index

            if (count % itemsPerLine == 0)
                outFile << "\n"; // newline after X items
        }
    }
    outFile << "\n};\n\n";

    // ------------------------
    // Set items
    // ------------------------
    file.clear();
    file.seekg(0, std::ios::beg);
    std::getline(file, line);

    outFile << "static SetItemEntry g_StaticSetItems[] = {\n";
    count = 0;
    while (std::getline(file, line))
    {
        auto cols = SplitTab(line);

        if (colSetName >= 0 && colUniqueID >= 0 && colUniqueName >= 0 && colSetItemCode >= 0 && colUniqueItemName >= 0)
        {
            int maxCol = MaxInt5(colUniqueName, colUniqueID, colSetName, colSetItemCode, colUniqueItemName);
            if (cols.size() <= maxCol) continue;

            int idVal;
            if (!SafeStringToInt(cols[colUniqueID], idVal)) continue;

            outFile << "    { \"" << cols[colUniqueName] << "\", "
                << idVal << ", \"" << cols[colSetName] << "\", \""
                << cols[colUniqueItemName] << "\", \""      // ItemName
                << cols[colSetItemCode] << "\", false }, ";
            if (count % itemsPerLine == 0)
                outFile << "\n";
        }
    }
    outFile << "\n};\n";

    outFile.close();
    file.close();

    std::cout << "Static arrays generated in StaticGrailArrays.txt with " << itemsPerLine << " items per line.\n";
    return true;
}

void SortItemLists()
{
    std::sort(g_UniqueItems.begin(), g_UniqueItems.end(),
        [](const UniqueItemEntry& a, const UniqueItemEntry& b)
        {
            return a.name < b.name;
        });

    std::sort(g_SetItems.begin(), g_SetItems.end(),
        [](const SetItemEntry& a, const SetItemEntry& b)
        {
            return a.setName < b.setName;
        });
}

void WriteResultsToFile(const std::string& output)
{
    std::ofstream out(output);

    // --- Unique Items ---
    out << "=== UNIQUE ITEMS ===\n";
    for (auto& u : g_UniqueItems)
        out << u.id << "\t" << u.name << "\t" << u.code << "\t" << u.enabled << "\n";

    // --- Set Items ---
    out << "\n=== SET ITEMS ===\n";
    for (auto& s : g_SetItems)
        out << s.id << "\t" << s.setName << "\t" << s.name << "\t" << s.code << "\t" << s.enabled << "\n";
}

#pragma endregion

#pragma region - Load/Save Functions

void SaveGrailProgress(const std::string& userPath, bool isAutoBackup)
{
    std::filesystem::path path;
    json j;
    std::string uniqueJsonStr;

    try
    {
        std::vector<UniqueItemEntry> uniqueCopy = g_UniqueItems;
        std::vector<SetItemEntry> setCopy = g_SetItems;
        std::unordered_set<std::string> excludedCopy = g_ExcludedGrailItems;

        // --- Determine base path ---
        if (userPath.empty())
            path = std::filesystem::current_path();
        else
            path = userPath;

        std::string filename = configFilePath;

        if (isAutoBackup)
        {
            if (!path.has_extension())
            {
                if (backupWithTimestamps)
                {
                    auto t = std::chrono::system_clock::to_time_t(
                        std::chrono::system_clock::now());
                    std::tm tm{};
#if defined(_WIN32)
                    localtime_s(&tm, &t);
#else
                    localtime_r(&t, &tm);
#endif
                    char buf[64];
                    strftime(buf, sizeof(buf), "GrailBackup_%Y%m%d_%H%M%S.json", &tm);
                    filename = buf;
                }
                else if (!overwriteOldBackup)
                {
                    filename = "GrailBackup.json";
                }
                path /= filename;
            }
        }
        else
        {
            path = filename;
        }

        auto parent = path.parent_path();
        if (!parent.empty())
            std::filesystem::create_directories(parent);

        // --- Build JSON ---
        // UNIQUE ITEMS
        {
            std::stringstream uniqueStream;
            uniqueStream << "[";

            bool first = true;
            int count = 0;

            for (auto& u : uniqueCopy)
            {
                if (!u.collected) continue;

                if (!first) uniqueStream << ", ";
                first = false;

                uniqueStream << "\"" << u.name << "\"";
                count++;

                if (count % 10 == 0)
                    uniqueStream << "\n  ";
            }

            uniqueStream << "]";
            uniqueJsonStr = uniqueStream.str();
            j["Unique Items"] = json::parse(uniqueJsonStr);
        }

        // EXCLUDED ITEMS
        j["Excluded Grail Items"] = json::array();
        for (auto& x : excludedCopy)
            j["Excluded Grail Items"].push_back(x);

        // AUTO BACKUP SETTINGS
        j["AutoBackups"] = {
            { "On", autoBackups },
            { "Timestamps", backupWithTimestamps },
            { "Overwrite", overwriteOldBackup },
            { "Interval", backupIntervalMinutes },
            { "Path", backupPath }
        };

        // --- Write file ---
        std::ofstream out(path);
        if (!out.is_open())
        {
            std::cout << "[Backup ERROR] Failed to open file: " << path << std::endl;
            return;
        }

        out << j.dump(4);
        std::cout << "[Backup] Grail saved to: " << path << std::endl;
    }
    catch (const std::exception& e)
    {
        std::cout << "\n[Backup ERROR] Exception encountered.\n";
        std::cout << "  Path: " << path << "\n";
        std::cout << "  Error: " << e.what() << "\n";
        std::cout << "  Unique JSON string was:\n" << uniqueJsonStr << "\n";

        try
        {
            std::cout << "\n  JSON dump so far:\n" << j.dump(4) << "\n";
        }
        catch (...)
        {
            std::cout << "  JSON dump failed.\n";
        }
    }
}

void LoadGrailProgress(const std::string& filepath)
{
    std::ifstream file(filepath);
    if (!file.is_open()) return;

    json j;
    try { file >> j; }
    catch (...) { return; }

    // --- Load excluded ---
    g_ExcludedGrailItems.clear();
    if (j.contains("Excluded Grail Items"))
    {
        for (auto& x : j["Excluded Grail Items"])
            g_ExcludedGrailItems.insert(x.get<std::string>());
    }

    // --- Load AutoBackup settings ---
    if (j.contains("AutoBackups"))
    {
        auto& a = j["AutoBackups"];
        autoBackups = a.value("On", false);
        backupWithTimestamps = a.value("Timestamps", false);
        overwriteOldBackup = a.value("Overwrite", false);
        backupIntervalMinutes = a.value("Interval", 10);

        std::string pathStr = a.value("Path", "GrailBackup.json");
        std::strncpy(backupPath, pathStr.c_str(), sizeof(backupPath));
        backupPath[sizeof(backupPath) - 1] = '\0';
    }
}

bool LoadUniqueItems(const std::string& filepath)
{
    g_UniqueItems.clear();

    // Use static array for RMD
    if (modName == "RMD-MP")
    {
        int arraySize = sizeof(g_StaticUniqueItemsRMD) / sizeof(g_StaticUniqueItemsRMD[0]);
        for (int i = 0; i < arraySize; ++i)
        {
            g_UniqueItems.push_back(g_StaticUniqueItemsRMD[i]);
        }
        return true;
    }

    // Use static array for Retail Mods if file doesn't exist
    if (!std::filesystem::exists("Mods/" + modName + "/" + modName + ".mpq/data/global/excel/uniqueitems.txt") && modName != "RMD-MP")
    {
        int arraySize = sizeof(g_StaticUniqueItems) / sizeof(g_StaticUniqueItems[0]);
        for (int i = 0; i < arraySize; ++i)
        {
            g_UniqueItems.push_back(g_StaticUniqueItems[i]);
        }
        return true;
    }

    std::ifstream file(filepath);
    if (!file.is_open())
        return false;

    std::string line;
    std::getline(file, line); // read header
    auto header = SplitTab(line);

    int colIndex = FindColumn(header, "index");
    int colID = FindColumn(header, "*ID");
    int colEnabled = FindColumn(header, "enabled");
    int colCode = FindColumn(header, "code");
    int colItemName = FindColumn(header, "*ItemName");

    if (colIndex < 0 || colID < 0 || colEnabled < 0 || colCode < 0)
        return false;

    while (std::getline(file, line))
    {
        auto cols = SplitTab(line);
        int maxCol = MaxInt4(colIndex, colID, colEnabled, colCode);
        if (cols.size() <= maxCol)
            continue;

        UniqueItemEntry entry;

        int indexVal;
        if (!SafeStringToInt(cols[colID], indexVal)) // ID
            continue;
        entry.id = indexVal;

        entry.name = cols[colIndex];   // Name from index column
        entry.code = cols[colCode];    // Code
        entry.itemName = cols[colItemName];

        std::string enabledStr = cols[colEnabled];
        entry.enabled = (enabledStr == "1" || enabledStr == "true");

        // Skip items not enabled in the file
        if (!(enabledStr == "1" || enabledStr == "true"))
            continue;

        g_UniqueItems.push_back(entry);
    }

    return true;
}

bool LoadSetItems(const std::string& filepath)
{
    g_SetItems.clear();

    // Use static array for RMD
    if (modName == "RMD-MP")
    {
        int arraySize = sizeof(g_StaticSetItemsRMD) / sizeof(g_StaticSetItemsRMD[0]);
        for (int i = 0; i < arraySize; ++i)
        {
            g_SetItems.push_back(g_StaticSetItemsRMD[i]);
        }
        return true;
    }

    // Use static array for Retail Mods if file doesn't exist
    if (!std::filesystem::exists("Mods/" + modName + "/" + modName + ".mpq/data/global/excel/setitems.txt") && modName != "RMD-MP")
    {
        int arraySize = sizeof(g_StaticSetItems) / sizeof(g_StaticSetItems[0]);
        for (int i = 0; i < arraySize; ++i)
        {
            g_SetItems.push_back(g_StaticSetItems[i]);
        }
        return true;
    }

    std::ifstream file(filepath);
    if (!file.is_open())
        return false;

    std::string line;
    std::getline(file, line);
    auto header = SplitTab(line);

    int colIndex = FindColumn(header, "index");
    int colID = FindColumn(header, "*ID");
    int colSet = FindColumn(header, "set");
    int colItem = FindColumn(header, "item");
    int colItemName = FindColumn(header, "*ItemName");

    if (colIndex < 0 || colID < 0 || colSet < 0 || colItem < 0)
        return false;

    while (std::getline(file, line))
    {
        auto cols = SplitTab(line);

        int maxCol = MaxInt4(colIndex, colID, colSet, colItem);
        if (cols.size() <= maxCol)
            continue;

        SetItemEntry entry;
        entry.name = cols[colIndex];

        int idVal;
        if (!SafeStringToInt(cols[colID], idVal))
            continue;
        entry.id = idVal;
        entry.setName = cols[colSet];
        entry.code = cols[colItem];
        entry.enabled = false;
        entry.itemName = cols[colItemName];
        g_SetItems.push_back(entry);
    }

    return true;
}

void LoadExcludedGrailItems(const std::string& filepath)
{
    g_ExcludedGrailItems.clear();

    std::ifstream file(filepath);
    if (!file.is_open())
        return;

    try
    {
        nlohmann::json j;
        file >> j;

        if (j.contains("Excluded Grail Items") && j["Excluded Grail Items"].is_array())
        {
            for (auto& item : j["Excluded Grail Items"])
            {
                if (item.is_string())
                    g_ExcludedGrailItems.insert(item.get<std::string>());
            }
        }
    }
    catch (...)
    {
        // failed to parse, just skip
    }
}

void LoadAllItemData()
{
    g_UniqueItems.clear();
    g_SetItems.clear();
    
    // Load Functions  
    LoadUniqueItems("Mods/" + modName + "/" + modName + ".mpq/data/global/excel/uniqueitems.txt");
    LoadSetItems("Mods/" + modName + "/" + modName + ".mpq/data/global/excel/setitems.txt");
    SortItemLists();
    LoadGrailProgress(configFilePath);
    

    //GenerateStaticArrays("Mods/" + modName + "/" + modName + ".mpq/data/global/excel/uniqueitems.txt", 5);
    //GenerateStaticArrays("Mods/" + modName + "/" + modName + ".mpq/data/global/excel/setitems.txt", 2);
    //WriteResultsToFile("ParsedItemData_Output.txt");
    
}

#pragma endregion

#pragma endregion

#pragma endregion

#pragma region D2I Parser

#pragma region Static/Structs

GrailStatus GetGrailStatus(uint32_t id)
{
    GrailStatus g;
    g.isGrail = true;

    // Check set items
    for (auto& s : g_SetItems)
    {
        if (s.id == id)
        {
            if (s.collected) g.collected = true;
            g.located += static_cast<int>(s.locations.size());
        }
    }

    // Check unique items
    for (auto& u : g_UniqueItems)
    {
        if (u.id == id)
        {
            if (u.collected) g.collected = true;
            g.located += static_cast<int>(u.locations.size());
        }
    }

    return g;
}

std::wstring GetSavePath()
{
    const wchar_t* valueName = L"{4C5C32FF-BB9D-43B0-B5B4-2D72E54EAAA4}";
    const wchar_t* subKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Shell Folders";

    std::wregex sidRegex(LR"(S-1-5-21-\d+-\d+-\d+-\d+$)");

    HKEY hUsers;
    if (RegOpenKeyExW(HKEY_USERS, nullptr, 0, KEY_READ, &hUsers) != ERROR_SUCCESS)
        return L"";

    wchar_t name[256];
    DWORD nameSize = 256;
    DWORD index = 0;

    std::wstring savePath;

    // Enumerate all SIDs under HKEY_USERS
    while (RegEnumKeyExW(hUsers, index++, name, &nameSize, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS)
    {
        std::wstring sid = name;
        nameSize = 256;

        if (!std::regex_match(sid, sidRegex))
            continue;

        std::wstring fullPath = sid + L"\\" + subKey;

        HKEY hKey;
        if (RegOpenKeyExW(HKEY_USERS, fullPath.c_str(), 0, KEY_READ, &hKey) == ERROR_SUCCESS)
        {
            wchar_t buffer[MAX_PATH];
            DWORD bufSize = sizeof(buffer);
            DWORD type = 0;

            if (RegQueryValueExW(hKey, valueName, nullptr, &type, (LPBYTE)buffer, &bufSize) == ERROR_SUCCESS)
            {
                if (type == REG_SZ || type == REG_EXPAND_SZ)
                {
                    savePath = buffer;
                    RegCloseKey(hKey);
                    break;
                }
            }
            RegCloseKey(hKey);
        }
    }

    RegCloseKey(hUsers);

    // Fallback: HKEY_CURRENT_USER
    if (savePath.empty())
    {
        HKEY hKey;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, subKey, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
        {
            wchar_t buffer[MAX_PATH];
            DWORD bufSize = sizeof(buffer);
            DWORD type = 0;

            if (RegQueryValueExW(hKey, valueName, nullptr, &type, (LPBYTE)buffer, &bufSize) == ERROR_SUCCESS)
            {
                if (type == REG_SZ || type == REG_EXPAND_SZ)
                    savePath = buffer;
            }
            RegCloseKey(hKey);
        }
    }

    return savePath;
}

class BitReader {
public:
    BitReader(const std::vector<uint8_t>& buffer)
        : buf(buffer), bitPos(0) {
    }

    // FIXED: LSB-first bit reading (correct for D2R)
    uint32_t ReadBits(size_t bits) {
        if (bits > 32) throw std::runtime_error("Cannot read more than 32 bits at once");
        uint32_t result = 0;
        for (size_t i = 0; i < bits; ++i) {
            size_t byteIdx = bitPos >> 3;
            size_t bitIdx = bitPos & 7;
            if (byteIdx >= buf.size()) throw std::runtime_error("Buffer overflow");
            // LSB-first: read bit from position bitIdx (0-7, starting from LSB)
            if ((buf[byteIdx] >> bitIdx) & 1) {
                result |= (1u << i);
            }
            bitPos++;
        }
        return result;
    }

    uint8_t ReadUInt8(size_t bits) { return static_cast<uint8_t>(ReadBits(bits)); }
    uint16_t ReadUInt16(size_t bits) { return static_cast<uint16_t>(ReadBits(bits)); }
    uint32_t ReadUInt32(size_t bits) { return ReadBits(bits); }
    bool ReadBit() { return ReadBits(1) != 0; }

    void SkipBits(size_t bits) { bitPos += bits; }
    void SetBitPos(size_t pos) { bitPos = pos; }
    size_t GetBitPos() const { return bitPos; }
    size_t GetBytePos() const { return bitPos >> 3; }
    void AlignToByte() { if (bitPos & 7) bitPos = ((bitPos >> 3) + 1) << 3; }

    bool HasBits(size_t n) const { return bitPos + n <= buf.size() * 8; }

private:
    const std::vector<uint8_t>& buf;
    size_t bitPos;
};

struct EarAttributes {
    uint8_t clazz = 0;
    uint8_t level = 0;
    std::string name;
};

struct Item {
    // Base header flags
    bool identified = false;
    bool socketed = false;
    bool new_flag = false;
    bool is_ear = false;
    bool starter_item = false;
    bool simple_item = false;
    bool ethereal = false;
    bool personalized = false;
    bool given_runeword = false;

    uint16_t version = 0;
    uint8_t location_id = 0;
    uint8_t equipped_id = 0;
    uint8_t position_x = 0;
    uint8_t position_y = 0;
    uint8_t alt_position_id = 0;

    // Item core data
    uint32_t id = 0;
    uint8_t level = 0;
    std::string type;

    uint8_t nr_of_items_in_sockets = 0;

    // Picture, class-specific
    bool multiple_pictures = false;
    uint8_t picture_id = 0;
    bool class_specific = false;
    uint16_t auto_affix_id = 0;

    // Quality
    uint8_t quality = 0;

    uint8_t low_quality_id = 0;
    uint8_t file_index = 0;

    uint16_t magic_prefix = 0;
    uint16_t magic_suffix = 0;

    uint16_t set_id = 0;
    uint16_t unique_id = 0;

    uint8_t rare_name_id = 0;
    uint8_t rare_name_id2 = 0;
    uint16_t magical_name_ids[6] = { 0 };

    EarAttributes ear_attributes;

    uint16_t personalized_id = 0;
    uint32_t runeword_id = 0;

    std::vector<Item> socketed_items;
};

std::unordered_map<uint32_t, std::string> g_SetItemLookup;
std::unordered_map<uint32_t, std::string> g_UniqueItemLookup;

#pragma endregion

#pragma region Huffman Tree

struct HuffmanNode {
    char value = 0;
    HuffmanNode* left = nullptr;
    HuffmanNode* right = nullptr;
    ~HuffmanNode() { delete left; delete right; }
};

static const std::vector<std::pair<char, std::string>> HUFFMAN_CODES = {
    {' ', "10"},
    {'0', "11111011"},
    {'1', "1111100"},
    {'2', "001100"},
    {'3', "1101101"},
    {'4', "11111010"},
    {'5', "00010110"},
    {'6', "1101111"},
    {'7', "01111"},
    {'8', "000100"},
    {'9', "01110"},
    {'a', "11110"},
    {'b', "0101"},
    {'c', "01000"},
    {'d', "110001"},
    {'e', "110000"},
    {'f', "010011"},
    {'g', "11010"},
    {'h', "00011"},
    {'i', "1111110"},
    {'j', "000101110"},
    {'k', "010010"},
    {'l', "11101"},
    {'m', "01101"},
    {'n', "001101"},
    {'o', "1111111"},
    {'p', "11001"},
    {'q', "11011001"},
    {'r', "11100"},
    {'s', "0010"},
    {'t', "01100"},
    {'u', "00001"},
    {'v', "1101110"},
    {'w', "00000"},
    {'x', "00111"},
    {'y', "0001010"},
    {'z', "11011000"},

    // --- Capitals ---
    {'A', "00010111101011100"},
    {'B', "00010111111001110"},
    {'C', "00010111111011001"},
    {'D', "00010111110011111"},
    {'E', "00010111101111000"},
    {'F', "00010111110011100"},
    {'G', "00010111110101011"},
    {'H', "00010111110111001"},
    {'I', "00010111110111100"},
    {'J', "0001011110000111"},
    {'K', "00010111110100100"},
    {'L', "00010111110010010"},
    {'M', "00010111111011011"},
    {'N', "0001011110000010"},
    {'O', "00010111111101011"},
    {'P', "00010111110100111"},
    {'Q', "00010111101100100"},
    {'R', "00010111111100111"},
    {'S', "00010111101101010"},
    {'T', "00010111110001010"},
    {'U', "00010111110001001"},
    {'V', "00010111110010001"},
    {'W', "00010111111101110"},
    {'X', "00010111111111010"},
    {'Y', "00010111111111101"},
    {'Z', "00010111110000100"},
};

HuffmanNode* BuildHuffmanTreeFromTable() {
    auto root = new HuffmanNode{};

    for (auto& [ch, bits] : HUFFMAN_CODES) {
        HuffmanNode* node = root;

        for (char b : bits) {
            if (b == '0') {
                if (!node->left) node->left = new HuffmanNode{};
                node = node->left;
            }
            else {
                if (!node->right) node->right = new HuffmanNode{};
                node = node->right;
            }
        }

        node->value = ch; // leaf
    }

    return root;
}

char DecodeHuffmanChar(BitReader& reader, HuffmanNode* root) {
    HuffmanNode* node = root;
    int depth = 0;
    while (node && node->value == 0 && depth++ < 30) {
        bool bit = reader.ReadBit();
        node = bit ? node->right : node->left;
    }
    if (!node) throw std::runtime_error("Invalid Huffman tree traversal");
    return node->value;
}

std::string DecodeHuffmanString(BitReader& reader, HuffmanNode* root) {
    std::string s;
    for (int i = 0; i < 4; ++i) {
        char c = DecodeHuffmanChar(reader, root);
        if (c == ' ' || c == 0) break;
        s += c;
    }
    return s;
}

std::vector<size_t> FindItemOffsets(const std::vector<uint8_t>& buf, size_t start, size_t end) {
    std::vector<size_t> offsets;
    for (size_t i = start; i + 4 < end; i++) {
        // D2R item flags have multiple patterns depending on item properties
        // Common patterns: 10 00 80 00, 10 20 a0 00, 10 08 80 00, etc.
        // Byte 0: lower nibble is typically 0 (0x10, 0x00)
        // Byte 2: has bit 7 set (0x80, 0xa0, 0xc0)
        // Byte 3: is 0x00
        bool byte0_valid = (buf[i] & 0x0F) == 0;      // lower nibble is 0
        bool byte2_valid = (buf[i + 2] & 0x80) != 0;    // bit 7 set
        bool byte3_valid = buf[i + 3] == 0x00;          // must be 0

        if (byte0_valid && byte2_valid && byte3_valid) {
            offsets.push_back(i);
        }
    }
    return offsets;
}



#pragma endregion

#pragma region Lookup Functions

void BuildItemNameLookups()
{
    g_SetItemLookup.clear();
    g_UniqueItemLookup.clear();

    for (auto& s : g_SetItems)
        g_SetItemLookup[s.id] = s.setName.empty() ? "Unknown Set Item" : s.setName;

    for (auto& u : g_UniqueItems)
        g_UniqueItemLookup[u.id] = u.name.empty() ? "Unknown Unique" : u.name;
}

std::string GetItemTypeName(const std::string& code)
{
    for (auto& s : g_SetItems)
    {
        if (s.code == code)
            return s.itemName.empty() ? code : s.itemName;
    }

    for (auto& u : g_UniqueItems)
    {
        if (u.code == code)
            return u.name.empty() ? code : u.name;
    }

    // Not Found
    return "Unknown Item";
}

std::string GetSetItemName(uint32_t id)
{
    auto it = g_SetItemLookup.find(id);
    return it != g_SetItemLookup.end() ? it->second : "Unknown Set Item";
}

std::string GetUniqueItemName(uint32_t id)
{
    auto it = g_UniqueItemLookup.find(id);
    return it != g_UniqueItemLookup.end() ? it->second : "Unknown Unique";
}

const char* GetQualityName(uint32_t q) {
    const char* names[] = { "", "Inferior", "Normal", "Superior", "Magic", "Set", "Rare", "Unique", "Crafted", "Tempered" };
    return q < 10 ? names[q] : "Unknown";
}

#pragma endregion

#pragma region Stash Parsing

Item ParseItem(const uint8_t* data, size_t size, HuffmanNode* huffmanRoot, uint32_t fileVersion) {
    Item item;
    std::vector<uint8_t> itemBuf(data, data + size);
    BitReader reader(itemBuf);

    // === FLAG BITS (32 bits) ===
    reader.SkipBits(4);                       // bits 0-3: unknown
    item.identified = reader.ReadBit();       // bit 4
    reader.SkipBits(1);                       // bit 5
    item.socketed = reader.ReadBit();         // bit 6
    reader.SkipBits(2);                       // bits 7-8
    item.new_flag = reader.ReadBit();         // bit 9
    reader.SkipBits(1);                       // bit 10
    item.is_ear = reader.ReadBit();           // bit 11
    item.starter_item = reader.ReadBit();     // bit 12
    reader.SkipBits(8);                       // bits 13-20
    item.simple_item = reader.ReadBit();      // bit 21
    item.ethereal = reader.ReadBit();         // bit 22
    reader.SkipBits(1);                       // bit 23
    item.personalized = reader.ReadBit();     // bit 24
    reader.SkipBits(1);                       // bit 25
    item.given_runeword = reader.ReadBit();   // bit 26
    reader.SkipBits(5);                       // bits 27-31

    // === VERSION ===
    if (fileVersion >= 0x61)
        item.version = reader.ReadUInt16(3);
    else
        item.version = reader.ReadUInt16(10);

    // === LOCATION DATA ===
    item.location_id = reader.ReadUInt8(3);
    item.equipped_id = reader.ReadUInt8(4);
    item.position_x = reader.ReadUInt8(4);
    item.position_y = reader.ReadUInt8(4);
    item.alt_position_id = reader.ReadUInt8(3);

    // === EAR SPECIAL CASE ===
    // Ears must have BOTH is_ear=1 AND simple_item=1
    // If is_ear is set but simple_item is not, it's a regular item with different flags
    if (item.is_ear && item.simple_item) {
        item.ear_attributes.clazz = reader.ReadUInt8(3);
        item.ear_attributes.level = reader.ReadUInt8(7);
        for (int i = 0; i < 15; i++) {
            uint8_t ch = reader.ReadUInt8(7);
            if (ch == 0) break;
            item.ear_attributes.name.push_back((char)ch);
        }
        return item;
    }

    // === ITEM TYPE (Huffman for D2R, ASCII for older) ===
    if (fileVersion >= 0x61) {
        item.type = DecodeHuffmanString(reader, huffmanRoot);
    }
    else {
        for (int i = 0; i < 4; ++i) {
            char c = reader.ReadUInt8(8);
            if (c && c != ' ') item.type += c;
        }
    }

    // === SOCKET COUNT ===
    item.nr_of_items_in_sockets = reader.ReadUInt8(item.simple_item ? 1 : 3);

    if (item.simple_item) return item;

    // === EXTENDED ITEM DATA ===
    item.id = reader.ReadUInt32(32);
    item.level = reader.ReadUInt8(7);
    item.quality = reader.ReadUInt8(4);

    // Variable picture
    item.multiple_pictures = reader.ReadBit();
    if (item.multiple_pictures) item.picture_id = reader.ReadUInt8(3);

    // Class specific
    item.class_specific = reader.ReadBit();
    if (item.class_specific) item.auto_affix_id = reader.ReadUInt16(11);

    // === QUALITY-SPECIFIC DATA ===
    switch (item.quality) {
    case 1:  // Inferior
        item.low_quality_id = reader.ReadUInt8(3);
        break;
    case 3:  // Superior
        item.file_index = reader.ReadUInt8(3);
        break;
    case 4:  // Magic
        item.magic_prefix = reader.ReadUInt16(11);
        item.magic_suffix = reader.ReadUInt16(11);
        break;
    case 5:  // Set
        item.set_id = reader.ReadUInt16(12);
        break;
    case 6:  // Rare
    case 8:  // Crafted
        item.rare_name_id = reader.ReadUInt8(8);
        item.rare_name_id2 = reader.ReadUInt8(8);
        for (int i = 0; i < 6; ++i) {
            if (reader.ReadBit()) item.magical_name_ids[i] = reader.ReadUInt16(11);
        }
        break;
    case 7:  // Unique
        item.unique_id = reader.ReadUInt16(12);
        break;
    }

    return item;
}

static void ShowItemLocationTooltip(int id, bool isSet)
{
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        return;

    ImVec2 mousePos = ImGui::GetIO().MousePos;
    ImGui::SetNextWindowPos(ImVec2(mousePos.x + 70, mousePos.y), ImGuiCond_Always);

    ImGui::BeginTooltip();
    float tooltipWidth = 420.0f;
    ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + tooltipWidth);

    int collected = 0;
    int located = 0;

    if (isSet)
    {
        for (auto& s : g_SetItems)
        {
            if (s.id == id && s.collected)
                collected++;

            if (s.id == id)
                located += (int)s.locations.size();
        }

        if (collected == 0 || located == 0)
        {
            ImGui::TextColored(ImVec4(0.9f, 0.15f, 0.15f, 1.0f), "Not Collected");
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1), "This item has not been found in your stash.");
        }
        else
        {
            if (located == 1)
                ImGui::TextColored(ImVec4(0.9f, 0.15f, 0.15f, 1.0f), "Found %d Item", located);
            else
                ImGui::TextColored(ImVec4(0.9f, 0.15f, 0.15f, 1.0f), "Found %d Items", located);

            ImGui::Separator();
            for (auto& s : g_SetItems)
            {
                if (s.id == id)
                {
                    for (auto& loc : s.locations)
                    {
                        ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "- Page %d, Tab %d  (X: %d, Y: %d)", loc.page, loc.tab, loc.x, loc.y);
                    }
                }
            }
        }
    }
    else // UNIQUE
    {
        for (auto& u : g_UniqueItems)
        {
            if (u.id == id && u.collected)
                collected++;

            if (u.id == id)
                located += (int)u.locations.size();
        }

        if (collected == 0 || located == 0)
        {
            ImGui::TextColored(ImVec4(0.9f, 0.15f, 0.15f, 1.0f), "Not Collected");
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1), "This item has not been found in your stash.");
        }
        else
        {
            if (located == 1)
                ImGui::TextColored(ImVec4(0.9f, 0.15f, 0.15f, 1.0f), "Found %d Item", located);
            else
                ImGui::TextColored(ImVec4(0.9f, 0.15f, 0.15f, 1.0f), "Found %d Items", located);

            ImGui::Separator();
            for (auto& u : g_UniqueItems)
            {
                if (u.id == id)
                {
                    for (auto& loc : u.locations)
                    {
                        ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "- Page %d, Tab %d  (X: %d, Y: %d)", loc.page, loc.tab, loc.x, loc.y);
                    }
                }
            }
        }
    }

    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

static int ExtractPageFromPath(const std::string& path)
{
    size_t p = path.find("Page");
    if (p == std::string::npos) return -1;

    p += 4; // skip "Page"
    size_t end = path.find_first_not_of("0123456789", p);
    std::string num = path.substr(p, end - p);

    int page = std::stoi(num);
    if (page < 1 || page > 64) return -1;
    return page;
}

static int ParseSharedStash(const std::string& filePath, int pageNum) {
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file) {
        std::cerr << "Failed to open file: " << filePath << std::endl;
        return 1;
    }

    int page = ExtractPageFromPath(filePath);
    size_t fileSize = file.tellg();
    file.seekg(0);
    std::vector<uint8_t> buf(fileSize);
    file.read((char*)buf.data(), fileSize);

    std::cout << "\n-----------------------------" << std::endl;
    std::cout << "File: " << filePath << std::endl;
    std::cout << "Size: " << fileSize << " bytes" << std::endl;

    // Validate header
    if (buf.size() < 16 || buf[0] != 0x55 || buf[1] != 0xAA ||
        buf[2] != 0x55 || buf[3] != 0xAA) {
        std::cerr << "Invalid D2I file header" << std::endl;
        return 1;
    }

    uint32_t version = buf[8];
    HuffmanNode* huffman = BuildHuffmanTreeFromTable();
    int totalItems = 0, uniqueCount = 0, setCount = 0;

    // Helper lambdas to mark items collected
    auto MarkSetCollected = [&](int id, int tab, int posX, int posY) {
        for (auto& s : g_SetItems) {
            if (s.id == id) {
                s.collected = true;
                s.locations.push_back({ page, tab, posX + 1, posY + 1 });
                break;
            }
        }
        };

    auto MarkUniqueCollected = [&](int id, int tab, int posX, int posY) {
        for (auto& u : g_UniqueItems) {
            if (u.id == id) {
                u.collected = true;
                u.locations.push_back({ page, tab, posX + 1, posY + 1 });
                break;
            }
        }
        };

    // Find all tab headers (55 AA 55 AA)
    std::vector<size_t> tabOffsets;
    for (size_t i = 0; i + 3 < fileSize; i++) {
        if (buf[i] == 0x55 && buf[i + 1] == 0xAA &&
            buf[i + 2] == 0x55 && buf[i + 3] == 0xAA) {
            tabOffsets.push_back(i);
        }
    }
    std::cout << "-----------------------------" << std::endl;

    // Process each tab
    for (size_t tabIdx = 0; tabIdx < tabOffsets.size(); tabIdx++) {
        size_t tabStart = tabOffsets[tabIdx];
        size_t tabEnd = (tabIdx + 1 < tabOffsets.size()) ?
            tabOffsets[tabIdx + 1] : fileSize;

        // Find JM marker
        size_t jmOffset = 0;
        for (size_t i = tabStart; i + 1 < tabEnd; i++) {
            if (buf[i] == 'J' && buf[i + 1] == 'M') {
                jmOffset = i;
                break;
            }
        }

        if (jmOffset == 0) continue;

        // Check for empty tab
        if (jmOffset + 3 < fileSize &&
            buf[jmOffset + 2] == 0 && buf[jmOffset + 3] == 0) {
            continue;
        }

        // Find items
        auto itemOffsets = FindItemOffsets(buf, jmOffset + 4, tabEnd);
        if (itemOffsets.empty()) continue;

        // Parse items
        for (size_t i = 0; i < itemOffsets.size(); i++) {
            size_t offset = itemOffsets[i];
            size_t nextOffset = (i + 1 < itemOffsets.size()) ? itemOffsets[i + 1] : tabEnd;
            size_t itemSize = nextOffset - offset;

            try {
                Item item = ParseItem(buf.data() + offset, itemSize, huffman, version);

                // Skip invalid items (empty type or non-alphanumeric)
                if (item.type.empty()) continue;
                bool validType = true;
                for (char c : item.type) {
                    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) {
                        validType = false;
                        break;
                    }
                }
                if (!validType) continue;

                totalItems++;
                std::string baseName = GetItemTypeName(item.type);

                if (item.quality == 5) {  // Set
                    std::string setName = GetSetItemName(item.set_id);
                    std::cout << "[Tab " << tabIdx + 1 << "] - " << itemOffsets.size() << " items" << std::endl;
                    std::cout << " - Base: " << baseName << " (" << item.type << ")" << std::endl;
                    std::cout << " - Quality: SET" << std::endl;
                    std::cout << " - Set ID: " << item.set_id << std::endl;
                    std::cout << " - iLvl: " << (int)item.level << std::endl;
                    std::cout << " - Pos: (" << (int)item.position_x + 1 << ", " << (int)item.position_y + 1 << ")\n" << std::endl;
                    setCount++;

                    // --- Mark the checkbox collected ---
                    MarkSetCollected(item.set_id, (int)tabIdx + 1, item.position_x, item.position_y);
                }
                if (item.quality == 7) {  // Unique
                    std::string uniqueName = GetUniqueItemName(item.unique_id);
                    std::cout << "[Tab " << tabIdx + 1 << "] - " << itemOffsets.size() << " items" << std::endl;
                    std::cout << " - Base: " << baseName << " (" << item.type << ")" << std::endl;
                    std::cout << " - Quality: UNIQUE" << std::endl;
                    std::cout << " - Unique ID: " << item.unique_id << std::endl;
                    std::cout << " - iLvl: " << (int)item.level << std::endl;
                    std::cout << " - Pos: (" << (int)item.position_x + 1 << ", " << (int)item.position_y + 1 << ")\n" << std::endl;
                    uniqueCount++;

                    // --- Mark the checkbox collected ---
                    MarkUniqueCollected(item.unique_id, (int)tabIdx + 1, item.position_x, item.position_y);
                }
                /*
                else {
                    std::cout << " - Base: " << baseName << " (" << item.type << ")" << std::endl;
                    std::cout << " - Quality: " << GetQualityName(item.quality) << std::endl;
                    std::cout << " - iLvl: " << (int)item.level << std::endl;
                    std::cout << " - Pos: (" << (int)item.position_x + 1 << ", " << (int)item.position_y + 1 << ")\n" << std::endl;
                }
                */

                std::string flags;
                if (item.ethereal) flags += "[ETH] ";
                if (item.socketed) flags += "[SOCK] ";
                if (!item.identified) flags += "[UNID] ";
                if (item.given_runeword) flags += "[RW] ";
                if (!flags.empty()) std::cout << "  " << flags << std::endl;

            }
            catch (const std::exception& ex) {
                std::cerr << "  Error parsing item at 0x" << std::hex << offset << ": " << ex.what() << std::endl;
            }
        }
    }

    std::cout << "Total Items: " << totalItems << std::endl;
    std::cout << "Set Items:   " << setCount << std::endl;
    std::cout << "Uniques:     " << uniqueCount << std::endl;

    g_GrailRevision++;
    ReloadGameFilterForGrail();

    delete huffman;
    return 0;
}

void ScanStashPages()
{
    using clock = std::chrono::steady_clock;
    static clock::time_point lastScan;
    static bool hasScanned = false;

    auto now = clock::now();

    // If we've scanned before AND cooldown hasn't expired → skip
    if (hasScanned && (now - lastScan < std::chrono::seconds(30)))
        return;

    hasScanned = true;
    lastScan = now;
    isHardcore = IsHardcore();

    for (auto& s : g_SetItems) {
        s.collected = false;
        s.locations.clear();
    }
    for (auto& u : g_UniqueItems) {
        u.collected = false;
        u.locations.clear();
    }

    namespace fs = std::filesystem;
    std::wstring stashFolder = GetSavePath() + L"\\Diablo II Resurrected\\Mods\\" + std::wstring(modName.begin(), modName.end()) + L"\\";
    std::regex pageFileRegex = isHardcore ? std::regex(R"(Stash_HC_Page(\d{1,2})\.d2i)", std::regex::icase) : std::regex(R"(Stash_SC_Page(\d{1,2})\.d2i)", std::regex::icase);
    std::vector<std::pair<int, std::string>> pages;

    for (const auto& entry : fs::directory_iterator(stashFolder))
    {
        if (!entry.is_regular_file()) continue;

        std::string filename = entry.path().filename().string();
        std::smatch match;
        if (std::regex_match(filename, match, pageFileRegex))
        {
            int pageNum = std::stoi(match[1].str());
            if (pageNum >= 1 && pageNum <= 64)
            {
                pages.emplace_back(pageNum, entry.path().string());
            }
        }
    }

    std::sort(pages.begin(), pages.end(),
        [](const auto& a, const auto& b) { return a.first < b.first; });

    for (auto& [pageNum, path] : pages)
    {
        ParseSharedStash(path, pageNum);
    }
}

#pragma endregion

#pragma endregion

#pragma region Menu System

#pragma region - Static/Structs

struct D2RHUDConfig
{
    bool MonsterStatsDisplay = true;
    std::string ChannelColor = "ÿc5";
    std::string PlayerNameColor = "ÿc8";
    std::string MessageColor = "ÿc0";
    bool HPRolloverMods = true;
    int HPRolloverPercent = 99;
    int HPRolloverDifficulty = -1;
    bool SunderedMonUMods = true;
    int SunderValue = 99;
    bool MinionEquality = true;
    bool GambleCostControl = true;
    bool CombatLog = true;
    bool TransmogVisuals = true;
    bool ExtendedItemcodes = true;
    bool FloatingDamage = true;
    std::vector<std::string> DLLsToLoad = { "D2RHUD.dll" };
};

std::vector<std::string> priorityOrder = {
        "reload", "Debug", "allowOverrides", "audioPlayback", "modTips",
        "audioVoice", "filter_titles", "filter_level", "language"
};

std::unordered_map<std::string, std::string> displayNames = {
    { "reload", "Reload Message" }, { "allowOverrides", "Allow Overrides" },
    { "modTips", "Mod Tips" }, { "Debug", "Debug Mode" },
    { "audioPlayback", "Audio Playback" }, { "audioVoice", "Audio Voice" },
    { "filter_titles", "Filter Titles" }, { "filter_level", "Filter Level" },
    { "language", "Language" }
};

std::unordered_map<std::string, std::string> g_LuaDescriptions = {
    { "reload", "Displays an in-game chat message whenever the filter is reloaded" },
    { "Debug", "Outputs details of matched/failed filter rules using in-game chat" },
    { "allowOverrides", "Allows the override_rules.lua to be loaded in addition to your own filter\nUsually used by mod authors to add 'default' content as a baseload" },
    { "audioPlayback", "Enables or disables both the Audio Playback and TTS options\nRequires Win10+ and Windows Media framework installed" },
    { "modTips", "Displays helpful mod-related tips at the bottom of item tooltips\nThese tips must be defined by the filter or mod author" },
    { "audioVoice", "Selects which TTS voice is used for audio playback (numeric ID)\nThis entry uses your own Windows TTS voices, they will differ between players" },
    { "filter_titles", "Define what the varying filter_levels should be displayed as in-game\nEntries such as 1,2,3 or Early-Game, Mid-Game, End-Game, etc are accepted" },
    { "filter_level", "Define your currently selected filter_level\nThis controls what filter rules will apply to your session (on supported filters)" },
    { "language", "Sets the filter language, such as 'enUS' or 'frFR'. Defaults to enUS if not defined\nLanguage support must be added by the filter author for proper functionality" }
};

struct LootFilterHeader
{
    std::string Title;
    std::string Version;
};

struct MemoryConfigEntry
{
    std::string Name;
    std::string Description;
    std::string Category;
    std::string Address;
    std::vector<std::string> Addresses;
    int Length = 1;
    std::string Type = "Hex";
    std::string Values;
    std::string OriginalValues;
    std::string ModdedValues;
    bool Override;
    std::string OverrideReason;
    int UniqueID;
};

struct CommandEntry
{
    std::string key;
    std::string command;
};

static std::vector<CommandEntry> g_CommandHotkeys;
static std::string g_StartupCommands;
static char searchBuffer[128] = "";
static bool filterUncollected = false;
static bool showMainMenu = false;
static bool showHUDSettingsMenu = false;
static bool showD2RHUDMenu = false;
static bool showMemoryMenu = false;
static bool showLootMenu = false;
static bool showHotkeyMenu = false;
static bool showGrailMenu = false;
static bool showBaseCodes = false;
static bool showBaseNames = false;
static bool showDuplicates = false;

LootFilterHeader g_LootFilterHeader;
std::unordered_map<std::string, std::string> g_LuaVariables;
std::unordered_map<std::string, std::string> g_LuaVariableComments;
std::unordered_map<std::string, std::pair<std::string, std::string>> g_Hotkeys;
std::vector<MemoryConfigEntry> g_MemoryConfigs;
static D2RHUDConfig d2rHUDConfig;
using ordered_json = nlohmann::ordered_json;
bool lootConfigLoaded = false;
bool lootLogicLoaded = false;
static bool showCollected = false;
static bool showExcluded = false;
bool HUDConfigLoaded = false;

#pragma endregion

#pragma region - Helper Funcs

void ImGuiTextCentered(const char* text, ImFont* font = nullptr, ImVec4 color = ImVec4(1, 1, 1, 1))
{
    ImGuiIO& io = ImGui::GetIO();
    float windowWidth = ImGui::GetWindowWidth();
    if (font) ImGui::PushFont(font);
    ImVec2 textSize = font ? font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.0f, text) : ImGui::CalcTextSize(text);
    ImGui::SetCursorPosX((windowWidth - textSize.x) * 0.5f);
    ImGui::TextColored(color, "%s", text);
    if (font) ImGui::PopFont();
}

std::string RemoveComments(const std::string& input)
{
    std::string output;
    size_t i = 0;
    while (i < input.size())
    {
        // Handle single-line comments //
        if (i + 1 < input.size() && input[i] == '/' && input[i + 1] == '/')
        {
            i += 2;
            while (i < input.size() && input[i] != '\n') i++;
        }
        // Handle multi-line comments
        else if (i + 1 < input.size() && input[i] == '/' && input[i + 1] == '*')
        {
            i += 2;
            while (i + 1 < input.size() && !(input[i] == '*' && input[i + 1] == '/')) i++;
            i += 2;
        }
        else
        {
            output += input[i];
            i++;
        }
    }

    return output;
}

std::string CleanJsonFile(const std::string& path)
{
    std::ifstream file(path);
    if (!file.is_open()) return "";
    std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    text = RemoveComments(text);
    text = std::regex_replace(text, std::regex(R"(,\s*([}\]]))"), "$1");

    return text;
}

ImVec2 CenterWindow(ImVec2 size)
{
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 centerPos = ImVec2((io.DisplaySize.x - size.x) * 0.5f,
        (io.DisplaySize.y - size.y) * 0.5f);
    ImGui::SetNextWindowPos(centerPos, ImGuiCond_Once);
    ImGui::SetNextWindowSize(size, ImGuiCond_Once);
    return centerPos;
}

ImFont* GetFont(int index)
{
    ImGuiIO& io = ImGui::GetIO();
    return (index >= 0 && index < io.Fonts->Fonts.Size) ? io.Fonts->Fonts[index] : nullptr;
}

void PushFontSafe(int index)
{
    if (ImFont* f = GetFont(index))
        ImGui::PushFont(f);
}

void PopFontSafe(int index)
{
    if (GetFont(index))
        ImGui::PopFont();
}

bool DrawWindowTitleAndClose(const char* title, bool* open)
{
    float closeBtnSize = 20.0f;
    float padding = 5.0f;
    ImVec2 contentSize = ImGui::GetContentRegionAvail();

    float titleWidth = ImGui::CalcTextSize(title).x;
    ImGui::SetCursorPosX((contentSize.x - titleWidth) * 0.5f);
    ImGui::TextColored(ImVec4(0.95f, 0.85f, 0.5f, 1.0f), "%s", title);

    ImGui::SameLine(contentSize.x - closeBtnSize - padding);
    ImVec2 btnPos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("CloseBtn", ImVec2(closeBtnSize, closeBtnSize));
    if (ImGui::IsItemClicked() && open) *open = false;

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 textSize = ImGui::CalcTextSize("X");
    ImVec2 textPos = ImVec2(btnPos.x + (closeBtnSize - textSize.x) * 0.5f,
        btnPos.y + (closeBtnSize - textSize.y) * 0.5f);
    drawList->AddText(textPos, IM_COL32(255, 80, 80, 255), "X");

    return true;
}

void DrawBottomDescription(const std::string& desc)
{
    ImGui::Dummy(ImVec2(0.0f, 5.0f));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.0f, 3.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 165, 0, 255));
    float textWidth = ImGui::CalcTextSize(desc.c_str()).x;
    float availWidth = ImGui::GetContentRegionAvail().x;
    ImGui::SetCursorPosX((availWidth - textWidth) * 0.5f);
    ImGui::TextWrapped("%s", desc.c_str());
    ImGui::PopStyleColor();
}

std::string DisplayKey(const std::string& key)
{
    std::string temp = key;
    size_t pos = 0;
    while ((pos = temp.find("VK_", pos)) != std::string::npos)
        temp.erase(pos, 3);
    return temp;
}

bool CaseInsensitiveContains(const std::string& str, const std::string& substr)
{
    auto it = std::search(
        str.begin(), str.end(),
        substr.begin(), substr.end(),
        [](char ch1, char ch2) { return std::tolower(ch1) == std::tolower(ch2); }
    );
    return (it != str.end());
}

void EnableAllInput() {
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags &= ~ImGuiConfigFlags_NoKeyboard;
    io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
    io.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
    io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
    io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableSetMousePos;
}

bool D2RHUD::IsAnyMenuOpen() {
    return showGrailMenu || showHotkeyMenu || showLootMenu || showD2RHUDMenu;
}

void ProcessBackups()
{
    static std::chrono::steady_clock::time_point lastBackup = std::chrono::steady_clock::now();

    // --- Manual backup triggered by button ---
    {
        std::lock_guard<std::mutex> lock(backupMutex);
        if (triggerBackupNow)
        {
            triggerBackupNow = false;
            SaveGrailProgress(std::string(backupPath), true);
        }
    }

    // --- Auto-backup logic ---
    if (autoBackups)
    {
        auto now = std::chrono::steady_clock::now();
        int intervalMs = backupIntervalMinutes * 60 * 1000;
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastBackup).count() >= intervalMs)
        {
            lastBackup = now;
            SaveGrailProgress(std::string(backupPath), true);
        }
    }
}

std::vector<std::string> GetPressedKeys()
{
    std::vector<std::string> pressedKeys;

    bool ctrlAdded = false;
    bool shiftAdded = false;
    bool altAdded = false;

    for (auto& [keyName, keyCode] : keyMap)
    {
        // Ignore mouse buttons
        if (keyName == "VK_LBUTTON" || keyName == "VK_RBUTTON" || keyName == "VK_MBUTTON" ||
            keyName == "VK_XBUTTON1" || keyName == "VK_XBUTTON2")
            continue;

        // High bit set means key is currently down
        if (GetAsyncKeyState(keyCode) & 0x8000)
        {
            // Handle modifiers uniquely
            if (keyName == "VK_LCONTROL" || keyName == "VK_RCONTROL" || keyName == "VK_CONTROL")
            {
                if (!ctrlAdded)
                {
                    pressedKeys.push_back("CTRL");
                    ctrlAdded = true;
                }
                continue;
            }
            if (keyName == "VK_LSHIFT" || keyName == "VK_RSHIFT" || keyName == "VK_SHIFT")
            {
                if (!shiftAdded)
                {
                    pressedKeys.push_back("SHIFT");
                    shiftAdded = true;
                }
                continue;
            }
            if (keyName == "VK_LMENU" || keyName == "VK_RMENU" || keyName == "VK_MENU")
            {
                if (!altAdded)
                {
                    pressedKeys.push_back("ALT");
                    altAdded = true;
                }
                continue;
            }

            // Non-modifier keys
            pressedKeys.push_back(keyName);
        }
    }

    return pressedKeys;
}

std::unordered_map<std::string, ImVec4> g_TextColors = {
    { "red",          ImVec4(0.98824f, 0.27451f, 0.27451f, 1.0f) },
    { "green",        ImVec4(0.0f, 0.98824f, 0.0f, 1.0f) },
    { "blue",         ImVec4(0.43137f, 0.43137f, 1.0f, 1.0f) },
    { "gold",         ImVec4(0.78039f, 0.70196f, 0.46667f, 1.0f) },
    { "gray",         ImVec4(0.38824f, 0.38824f, 0.38824f, 1.0f) },
    { "grey",         ImVec4(0.38824f, 0.38824f, 0.38824f, 1.0f) },
    { "orange",       ImVec4(1.0f, 0.65882f, 0.0f, 1.0f) },
    { "darkgreen",    ImVec4(0.0f, 0.50196f, 0.0f, 1.0f) },
    { "yellow",       ImVec4(1.0f, 1.0f, 0.39216f, 1.0f) },
    { "purple",       ImVec4(0.75294f, 0.50196f, 0.94902f, 1.0f) },
    { "white",        ImVec4(1.0f, 1.0f, 1.0f, 1.0f) },
    { "turquoise",    ImVec4(0.02353f, 0.64706f, 0.86667f, 1.0f) },
    { "pink",         ImVec4(1.0f, 0.50196f, 1.0f, 1.0f) },
    { "lilac",        ImVec4(0.66667f, 0.66667f, 1.0f, 1.0f) },
    { "black",        ImVec4(0.0f, 0.0f, 0.0f, 1.0f) },
};



#pragma endregion

#pragma region - File Load/Save

ordered_json LoadJsonConfig(const std::string& filename)
{
    if (!std::filesystem::exists(filename))
        throw std::runtime_error("Config file not found");

    std::ifstream file(filename);
    if (!file.is_open())
        throw std::runtime_error("Failed to open config file");

    ordered_json j;
    file >> j;
    return j;
}

struct Keybind
{
    std::string name;
    std::string key;
    std::string extra;
};

static std::vector<Keybind> g_Keybinds;

void ReadKeybindsFromJson(const ordered_json& j)
{
    g_Keybinds.clear();

    if (!j.contains("Keybinds") || !j["Keybinds"].is_object())
        return;

    for (const auto& [name, value] : j["Keybinds"].items())
    {
        Keybind kb;
        kb.name = name;
        kb.key = value.value("Key", "");
        kb.extra = value.value("Extra", "");

        g_Keybinds.push_back(kb);
    }
}

std::vector<std::string> ReadStartupCommandsFromJson(const ordered_json& j)
{
    std::vector<std::string> result;

    std::cout << "[StartupCmd] Enter ReadStartupCommandsFromJson()\n";

    if (!j.contains("Commands"))
    {
        std::cout << "[StartupCmd] ERROR: JSON has no 'Commands' object\n";
        return result;
    }

    const auto& cmdsObj = j["Commands"];

    if (!cmdsObj.contains("Startup Commands"))
    {
        std::cout << "[StartupCmd] WARNING: 'Commands' has no 'Startup Commands' entry\n";
    }

    std::string startup = cmdsObj.value("Startup Commands", "");
    g_StartupCommands = startup;

    std::cout << "[StartupCmd] Raw Startup Commands string: \""
        << startup << "\" (len=" << startup.size() << ")\n";

    if (!startup.empty())
    {
        std::stringstream ss(startup);
        std::string cmd;
        int index = 0;

        // Split ONLY on commas
        while (std::getline(ss, cmd, ','))
        {
            std::cout << "[StartupCmd] Parsed token [" << index
                << "]: \"" << cmd << "\" (len=" << cmd.size() << ")\n";

            if (!cmd.empty())
                result.push_back(cmd);

            ++index;
        }
    }
    else
    {
        std::cout << "[StartupCmd] Startup Commands string is EMPTY\n";
    }

    std::cout << "[StartupCmd] Parsed command count = "
        << result.size() << "\n";

    // Pad or clamp to exactly 6 entries
    result.resize(6);

    automaticCommand1 = result[0];
    automaticCommand2 = result[1];
    automaticCommand3 = result[2];
    automaticCommand4 = result[3];
    automaticCommand5 = result[4];
    automaticCommand6 = result[5];

    std::cout << "[StartupCmd] Assigned commands:\n";
    std::cout << "  [1] \"" << automaticCommand1 << "\"\n";
    std::cout << "  [2] \"" << automaticCommand2 << "\"\n";
    std::cout << "  [3] \"" << automaticCommand3 << "\"\n";
    std::cout << "  [4] \"" << automaticCommand4 << "\"\n";
    std::cout << "  [5] \"" << automaticCommand5 << "\"\n";
    std::cout << "  [6] \"" << automaticCommand6 << "\"\n";

    return result;
}

void ReadCustomCommandsFromJson(const ordered_json& j)
{
    g_CommandHotkeys.clear();

    if (!j.contains("Commands"))
        return;

    const auto& cmds = j["Commands"]["Custom Commands"];
    if (!cmds.is_array())
        return;

    for (const auto& entry : cmds)
    {
        CommandEntry ce;
        ce.key = entry.value("Key", "");
        ce.command = entry.value("Command", "");

        g_CommandHotkeys.push_back(ce);
    }
}

void LoadCommandsAndKeybinds(const std::string& filename)
{
    std::cout << "LOADKEYBINDS";


    ordered_json j = LoadJsonConfig(filename);

    ReadKeybindsFromJson(j);
    ReadStartupCommandsFromJson(j);
    ReadCustomCommandsFromJson(j);

    g_Hotkeys.clear();

    for (const auto& kb : g_Keybinds)
    {
        g_Hotkeys[kb.name] = { kb.key, kb.extra };
    }
}

const Keybind* FindKeybind(const std::string& name)
{
    for (const auto& kb : g_Keybinds)
    {
        if (kb.name == name)
            return &kb;
    }
    return nullptr;
}

void LoadHotkeys(const std::string& filename)
{
    g_Hotkeys.clear();
    g_CommandHotkeys.clear();
    g_StartupCommands.clear();

    std::ifstream file(filename);
    if (!file.is_open())
        return;

    json j;
    try
    {
        file >> j;
    }
    catch (...)
    {
        return;
    }

    // --- Load Keybinds ---
    if (j.contains("Keybinds") && j["Keybinds"].is_object())
    {
        const json& keybinds = j["Keybinds"];
        for (auto it = keybinds.begin(); it != keybinds.end(); ++it)
        {
            const std::string& name = it.key();
            const json& entry = it.value();

            std::string key;
            std::string extra;

            if (entry.contains("Key") && entry["Key"].is_string())
                key = entry["Key"].get<std::string>();
            else
                key = "";

            if (entry.contains("Enabled") && entry["Enabled"].is_boolean())
                extra = entry["Enabled"].get<bool>() ? "Enabled" : "Disabled";

            g_Hotkeys[name] = { key, extra };
        }
    }

    // --- Load Commands ---
    if (j.contains("Commands") && j["Commands"].is_object())
    {
        const json& commands = j["Commands"];

        // Startup Commands
        if (commands.contains("Startup Commands") && commands["Startup Commands"].is_string())
            g_StartupCommands = commands["Startup Commands"].get<std::string>();

        // Custom Commands
        if (commands.contains("Custom Commands") && commands["Custom Commands"].is_array())
        {
            for (const auto& cmdEntry : commands["Custom Commands"])
            {
                std::string key, command;
                if (cmdEntry.contains("Key") && cmdEntry["Key"].is_string())
                    key = cmdEntry["Key"].get<std::string>();
                else
                    key = "";

                if (cmdEntry.contains("Command") && cmdEntry["Command"].is_string())
                    command = cmdEntry["Command"].get<std::string>();

                g_CommandHotkeys.push_back({ key, command });
            }
        }
    }
}

void SaveHotkeys(const std::string& filename)
{
    json j;

    // Load existing config if present
    {
        std::ifstream in(filename);
        if (in.is_open())
        {
            try { in >> j; }
            catch (...) {}
        }
    }

    // ---------------- Keybinds ----------------
    json& keybinds = j["Keybinds"];
    if (!keybinds.is_object())
        keybinds = json::object();

    for (auto& [name, pair] : g_Hotkeys)
    {
        json& entry = keybinds[name];

        if (!pair.first.empty())
            entry["Key"] = pair.first;
        else
            entry["Key"] = nullptr;

        if (!pair.second.empty())
            entry["Enabled"] = (pair.second == "Enabled");
    }

    // ---------------- Commands ----------------
    json& commands = j["Commands"];
    if (!commands.is_object())
        commands = json::object();

    commands["Startup Commands"] = g_StartupCommands;

    // ---------------- Write file ----------------
    std::ofstream out(filename);
    if (!out.is_open())
        return;

    out << std::setw(4) << j << std::endl;
}

void LoadLootFilterConfig(const std::string& path)
{
    g_LuaVariables.clear();
    g_LuaVariableComments.clear();
    g_LootFilterHeader = {}; // reset

    std::ifstream file(path);
    if (!file.is_open())
        return;

    std::string line;
    int nestingLevel = 0; // track { } nesting

    while (std::getline(file, line))
    {
        // --- Parse top comment header ---
        if (line.rfind("---", 0) == 0)
        {
            if (line.find("Filter Title:") != std::string::npos)
                g_LootFilterHeader.Title = line.substr(line.find(":") + 1);

            // Trim leading spaces
            for (auto* str : { &g_LootFilterHeader.Title })
                if (!str->empty() && (*str)[0] == ' ') *str = str->substr(1);

            continue;
        }

        // --- Track nesting for Lua table ---
        for (char c : line)
        {
            if (c == '{') nestingLevel++;
            if (c == '}') nestingLevel--;
        }

        // Only parse top-level assignments
        if (nestingLevel == 1)
        {
            // --- Split comment from value ---
            std::string keyValuePart = line;
            std::string commentPart;

            size_t commentPos = line.find("--");
            if (commentPos != std::string::npos)
            {
                keyValuePart = line.substr(0, commentPos);
                commentPart = line.substr(commentPos + 2);
                // Trim leading spaces in comment
                while (!commentPart.empty() && commentPart.front() == ' ')
                    commentPart = commentPart.substr(1);
            }

            // --- Match key/value ---
            std::regex assignRegex(R"(^\s*([a-zA-Z_][a-zA-Z0-9_]*)\s*=\s*(.+?),?\s*$)");
            std::smatch match;
            if (std::regex_match(keyValuePart, match, assignRegex))
            {
                std::string key = match[1].str();
                std::string value = match[2].str();

                // Strip surrounding quotes for strings
                if (!value.empty() && value.front() == '"' && value.back() == '"')
                    value = value.substr(1, value.size() - 2);

                g_LuaVariables[key] = value;

                if (!commentPart.empty())
                    g_LuaVariableComments[key] = commentPart;
            }
        }
    }
}

void LoadLootFilterLogic(const std::string& path)
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        g_LootFilterHeader.Version.clear();
        return;
    }

    std::string line;
    std::regex versionRegex(R"delim(local\s+version\s*=\s*"([^"]*)")delim");

    while (std::getline(file, line))
    {
        std::smatch match;
        if (std::regex_search(line, match, versionRegex))
        {
            g_LootFilterHeader.Version = match[1].str();
            break; // found
        }
    }
}

void LoadMemoryConfigs(const std::string& path)
{
    // Open log for writing
    std::ofstream log("debug_memoryedits.log", std::ios::trunc);
    auto Log = [&](const std::string& msg)
        {
            log << msg << std::endl;
        };

    Log("=== LoadMemoryConfigs START ===");

    // --- Load main config ---
    json jMain;
    try
    {
        std::string cleanJson = CleanJsonFile(path);
        if (cleanJson.empty())
        {
            Log("Main config is empty or failed to clean.");
            return;
        }

        jMain = json::parse(cleanJson);
        Log("Main config loaded successfully.");
    }
    catch (const std::exception& e)
    {
        Log(std::string("Failed to parse main config: ") + e.what());
        return;
    }

    g_MemoryConfigs.clear();

    if (!jMain.contains("MemoryConfigs") || !jMain["MemoryConfigs"].is_array())
    {
        Log("Main config does not contain MemoryConfigs array. Creating empty array.");
        jMain["MemoryConfigs"] = json::array();
    }

    size_t index = 0;

    // --- Load main MemoryConfigs into g_MemoryConfigs ---
    for (const auto& entry : jMain["MemoryConfigs"])
    {
        try
        {
            MemoryConfigEntry m;
            m.UniqueID = index++;
            m.Name = entry.value("Name", "");
            m.Description = entry.value("Description", "");
            m.Category = entry.value("Category", "");
            m.Address = entry.value("Address", "");
            m.Addresses = entry.value("Addresses", std::vector<std::string>{});
            m.Length = entry.value("Length", 1);
            m.Type = entry.value("Type", "Hex");
            m.Values = entry.value("Values", "");
            m.OriginalValues = entry.value("OriginalValues", "");
            m.ModdedValues = entry.value("ModdedValues", "");
            m.Override = entry.value("Override", 0);
            m.OverrideReason = entry.value("OverrideReason", "");

            g_MemoryConfigs.push_back(std::move(m));
            Log("Loaded main entry: " + entry.value("Name", "") + " @ " + entry.value("Address", ""));
        }
        catch (const std::exception& e)
        {
            Log(std::string("Failed to load main entry: ") + e.what());
        }
    }

    // --- Load override config ---
    std::string overridePath = "Mods/" + modName + "/" + modName + ".mpq/data/D2RLAN/memory_overrides.json";
    json jOverride;
    std::ifstream overrideFile(overridePath);
    if (overrideFile.is_open())
    {
        try
        {
            std::string cleanOverride = CleanJsonFile(overridePath);
            if (!cleanOverride.empty())
            {
                jOverride = json::parse(cleanOverride);
                Log("Override config loaded successfully from " + overridePath);
            }
        }
        catch (const std::exception& e)
        {
            Log(std::string("Failed to parse override config: ") + e.what());
        }
    }
    else
    {
        Log("Override config file not found: " + overridePath);
    }


    // --- Merge override entries ---
    if (jOverride.contains("MemoryConfigs") && jOverride["MemoryConfigs"].is_array())
    {
        for (const auto& entry : jOverride["MemoryConfigs"])
        {
            try
            {
                std::string name = entry.value("Name", "");
                std::string address = entry.value("Address", "");

                bool exists = std::any_of(g_MemoryConfigs.begin(), g_MemoryConfigs.end(), [&](const MemoryConfigEntry& m) {
                    if (!m.Addresses.empty() && !entry.value("Addresses", std::vector<std::string>{}).empty())
                        return m.Addresses == entry["Addresses"].get<std::vector<std::string>>();
                    return m.Name == name && m.Address == address;
                    });

                if (exists)
                {
                    Log("Skipped override entry (already exists): " + name + " @ " + address);
                    continue;
                }

                MemoryConfigEntry m;
                m.Name = name;
                m.Description = entry.value("Description", "");
                m.Category = entry.value("Category", "");
                m.Address = address;
                m.Addresses = entry.value("Addresses", std::vector<std::string>{});
                m.Length = entry.value("Length", 1);
                m.Type = entry.value("Type", "Hex");
                m.Values = entry.value("Values", "");
                m.OriginalValues = entry.value("OriginalValues", "");
                m.ModdedValues = entry.value("ModdedValues", "");

                g_MemoryConfigs.push_back(std::move(m));
                Log("Added override entry: " + name + " @ " + address);
            }
            catch (const std::exception& e)
            {
                Log(std::string("Failed to load override entry: ") + e.what());
            }
        }
    }

    // --- Optional: Process entries ---
    int totalOperations = 0;
    int successfulOperations = 0;

    for (auto& entry : g_MemoryConfigs)
    {
        totalOperations++;
        bool allSucceeded = true;

        try
        {
            if (!entry.Addresses.empty())
            {
                for (auto& addr : entry.Addresses)
                    allSucceeded = false;
            }
            else if (!entry.Address.empty())
            {
                allSucceeded = false;
            }
        }
        catch (...)
        {
            allSucceeded = false;
        }

        if (allSucceeded)
            successfulOperations++;
    }

    Log("Total entries: " + std::to_string(g_MemoryConfigs.size()));
    Log("Total operations: " + std::to_string(totalOperations));
    Log("Successful operations: " + std::to_string(successfulOperations));
    Log("=== LoadMemoryConfigs END ===");
}

void LoadD2RHUDConfig(const std::string& path)
{
    std::string cleanedJson = CleanJsonFile(path);
    if (cleanedJson.empty())
    {
        std::cerr << "Failed to read or clean D2RHUD config file: " << path << std::endl;
        return;
    }

    try
    {
        nlohmann::json j = nlohmann::json::parse(cleanedJson);

        d2rHUDConfig.MonsterStatsDisplay = j.value("MonsterStatsDisplay", d2rHUDConfig.MonsterStatsDisplay);
        //d2rHUDConfig.ChannelColor = j.value("ChannelColor", d2rHUDConfig.ChannelColor);
        //d2rHUDConfig.PlayerNameColor = j.value("PlayerNameColor", d2rHUDConfig.PlayerNameColor);
        //d2rHUDConfig.MessageColor = j.value("MessageColor", d2rHUDConfig.MessageColor);
        d2rHUDConfig.HPRolloverMods = j.value("HPRolloverMods", d2rHUDConfig.HPRolloverMods);
        d2rHUDConfig.HPRolloverPercent = j.value("HPRollover%", d2rHUDConfig.HPRolloverPercent);
        d2rHUDConfig.HPRolloverDifficulty = j.value("HPRolloverDifficulty", d2rHUDConfig.HPRolloverDifficulty);
        d2rHUDConfig.SunderedMonUMods = j.value("SunderedMonUMods", d2rHUDConfig.SunderedMonUMods);
        d2rHUDConfig.SunderValue = j.value("SunderValue", d2rHUDConfig.SunderValue);
        d2rHUDConfig.MinionEquality = j.value("MinionEquality", d2rHUDConfig.MinionEquality);
        d2rHUDConfig.GambleCostControl = j.value("GambleCostControl", d2rHUDConfig.GambleCostControl);
        d2rHUDConfig.CombatLog = j.value("CombatLog", d2rHUDConfig.CombatLog);
        d2rHUDConfig.TransmogVisuals = j.value("TransmogVisuals", d2rHUDConfig.TransmogVisuals);
        d2rHUDConfig.ExtendedItemcodes = j.value("ExtendedItemcodes", d2rHUDConfig.ExtendedItemcodes);
        d2rHUDConfig.FloatingDamage = j.value("FloatingDamage", d2rHUDConfig.FloatingDamage);

        if (j.contains("DLLsToLoad"))
            d2rHUDConfig.DLLsToLoad = j["DLLsToLoad"].get<std::vector<std::string>>();

        // LOAD "Options" BLOCK
        if (j.contains("Options"))
        {
            auto& opt = j["Options"];

            showCollected = opt.value("Collected", showCollected);
            showExcluded = opt.value("Excluded", showExcluded);
            showBaseCodes = opt.value("Base Codes", showBaseCodes);
            showBaseNames = opt.value("Base Names", showBaseNames);
            showDuplicates = opt.value("Duplicates", showDuplicates);
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "Failed to parse D2RHUD config: " << e.what() << std::endl;
    }
}

bool SaveFullGrailConfig(const std::string& userPath, bool isAutoBackup)
{
    try
    {
        ordered_json j;

        // --- Determine file path ---
        std::filesystem::path path = userPath.empty() ? (std::filesystem::current_path() / configFilePath) : userPath;
        if (std::filesystem::is_directory(path)) path /= configFilePath;
        auto parent = path.parent_path();
        if (!parent.empty()) std::filesystem::create_directories(parent);

        // --- Load existing file if it exists ---
        ordered_json existing;
        if (std::filesystem::exists(path))
        {
            std::ifstream in(path);
            if (in.is_open())
            {
                in >> existing;
                in.close();
            }
        }

        if (!g_ExcludedGrailItems.empty())
        {
            j["Excluded Grail Items"] = ordered_json::array();
            for (const auto& x : g_ExcludedGrailItems)
                j["Excluded Grail Items"].push_back(x);
        }
        else if (existing.contains("Excluded Grail Items"))
        {
            j["Excluded Grail Items"] = existing["Excluded Grail Items"];
        }

        // --- Other sections (always save) ---
        j["MonsterStatsDisplay"] = d2rHUDConfig.MonsterStatsDisplay;
        j["Channel Color"] = d2rHUDConfig.ChannelColor;
        j["Player Name Color"] = d2rHUDConfig.PlayerNameColor;
        j["Message Color"] = d2rHUDConfig.MessageColor;
        j["HPRolloverMods"] = d2rHUDConfig.HPRolloverMods;
        j["HPRollover%"] = d2rHUDConfig.HPRolloverPercent;
        j["HPRolloverDifficulty"] = d2rHUDConfig.HPRolloverDifficulty;
        j["SunderedMonUMods"] = d2rHUDConfig.SunderedMonUMods;
        j["SunderValue"] = d2rHUDConfig.SunderValue;
        j["MinionEquality"] = d2rHUDConfig.MinionEquality;
        j["GambleCostControl"] = d2rHUDConfig.GambleCostControl;
        j["CombatLog"] = d2rHUDConfig.CombatLog;
        j["TransmogVisuals"] = d2rHUDConfig.TransmogVisuals;
        j["ExtendedItemcodes"] = d2rHUDConfig.ExtendedItemcodes;
        j["FloatingDamage"] = d2rHUDConfig.FloatingDamage;
        j["Options"] = ordered_json{
            {"Base Codes", showBaseCodes},
            {"Base Names", showBaseNames},
            {"Collected", showCollected},
            {"Excluded", showExcluded},
            {"Duplicates", showDuplicates}
        };
        j["AutoBackups"] = ordered_json{
            {"Interval", backupIntervalMinutes},
            {"On", autoBackups},
            {"Overwrite", overwriteOldBackup},
            {"Path", backupPath},
            {"Timestamps", backupWithTimestamps}
        };

        // --- MemoryConfigs ---
        if (!g_MemoryConfigs.empty())
        {
            j["MemoryConfigs"] = ordered_json::array();

            for (const auto& m : g_MemoryConfigs)
            {
                ordered_json entry;
                entry["Name"] = m.Name;
                entry["Description"] = m.Description;
                entry["Category"] = m.Category;
                if (!m.Address.empty()) entry["Address"] = m.Address;
                if (!m.Addresses.empty()) entry["Addresses"] = m.Addresses;
                entry["Length"] = m.Length;
                entry["Type"] = m.Type;
                entry["Values"] = m.Values;
                entry["OriginalValues"] = m.OriginalValues;
                entry["ModdedValues"] = m.ModdedValues;
                entry["Override"] = m.Override;
                entry["OverrideReason"] = m.OverrideReason;
                entry["UniqueID"] = m.UniqueID;

                j["MemoryConfigs"].push_back(entry);
            }
        }
        else if (existing.contains("MemoryConfigs"))
            j["MemoryConfigs"] = existing["MemoryConfigs"];


        // --- Serialize manually to preserve 1-line-per-entry format for Keybinds/Commands ---
        std::stringstream outFile;
        outFile << "{\n";

        bool firstItem = true;
        for (auto it = j.begin(); it != j.end(); ++it)
        {
            if (it.key() == "Keybinds" || it.key() == "Commands") continue;
            if (!firstItem) outFile << ",\n";
            firstItem = false;
            outFile << "    \"" << it.key() << "\": " << it.value().dump(4);
        }

        // --- Keybinds ---
        outFile << ",\n    \"Keybinds\": {\n";
        bool firstKey = true;
        ordered_json keybinds;
        if (g_Hotkeys.empty() && existing.contains("Keybinds"))
            keybinds = existing["Keybinds"];
        else
        {
            for (auto& [name, pair] : g_Hotkeys)
                keybinds[name] = ordered_json{ {"Key", pair.first}, {"Extra", pair.second} };
        }

        for (auto it = keybinds.begin(); it != keybinds.end(); ++it)
        {
            if (!firstKey) outFile << ",\n";
            firstKey = false;

            outFile << "        \"" << it.key() << "\": { \"Key\": \"" << it.value()["Key"].get<std::string>() << "\"";
            if (it.value().contains("Enabled")) outFile << ", \"Enabled\": true";
            else if (it.value().contains("Extra")) outFile << ", \"Extra\": \"" << it.value()["Extra"].get<std::string>() << "\"";
            outFile << " }";
        }
        outFile << "\n    }";

        // --- Commands ---
        outFile << ",\n    \"Commands\": {\n";
        ordered_json commands;
        if (g_CommandHotkeys.empty() && existing.contains("Commands"))
            commands = existing["Commands"];
        else
        {
            commands["Startup Commands"] = g_StartupCommands;
            commands["Custom Commands"] = ordered_json::array();
            for (auto& cmd : g_CommandHotkeys)
                commands["Custom Commands"].push_back(ordered_json{ {"Key", cmd.key}, {"Command", cmd.command} });
        }

        if (commands.contains("Startup Commands"))
            outFile << "        \"Startup Commands\": \"" << commands["Startup Commands"].get<std::string>() << "\",\n";
        if (commands.contains("Custom Commands"))
        {
            outFile << "        \"Custom Commands\": [\n";
            bool firstCmd = true;
            for (auto& cmd : commands["Custom Commands"])
            {
                if (!firstCmd) outFile << ",\n";
                firstCmd = false;
                outFile << "            { \"Key\": \"" << cmd["Key"].get<std::string>()
                    << "\", \"Command\": \"" << cmd["Command"].get<std::string>() << "\" }";
            }
            outFile << "\n        ]\n";
        }
        outFile << "    }\n}";

        // --- Write to file ---
        std::ofstream out(path);
        if (!out.is_open()) return false;
        out << outFile.str();
        out.close();

        return true;
    }
    catch (...) { return false; }
}

#pragma endregion

#pragma region - Mod Overrides

struct LockedValueInfo
{
    bool locked = false;
    std::string reason;
};

struct ModOverrideSettings
{
    LockedValueInfo MonsterStatsDisplay;
    LockedValueInfo SunderedMonUMods;
    LockedValueInfo MinionEquality;
    LockedValueInfo GambleCostControl;
    LockedValueInfo CombatLog;
    LockedValueInfo TransmogVisuals;
    LockedValueInfo ExtendedItemcodes;
    LockedValueInfo FloatingDamage;
    LockedValueInfo HPRolloverMods;

    LockedValueInfo HPRolloverDifficulty;
    LockedValueInfo HPRolloverPercent;
    LockedValueInfo SunderValue;

    // Forced values
    bool ForcedMonsterStatsDisplay = false;
    bool ForcedSunderedMonUMods = true;
    bool ForcedMinionEquality = false;
    bool ForcedGambleCostControl = true;
    bool ForcedCombatLog = false;
    bool ForcedTransmogVisuals = true;
    bool ForcedExtendedItemcodes = true;
    bool ForcedFloatingDamage = false;
    bool ForcedHPRolloverMods = true;

    int ForcedHPRolloverDifficulty = -1;
    int ForcedHPRolloverPercent = 99;
    int ForcedSunderValue = 99;
};

static std::unordered_map<std::string, ModOverrideSettings> g_ModOverrides;

void RegisterModOverrides()
{
    ModOverrideSettings RMD;

    RMD.ForcedMonsterStatsDisplay = false;
    RMD.SunderedMonUMods = { true, "Disabling causes immunity reduction discrepancies" };
    RMD.ForcedMinionEquality = false;
    RMD.ForcedGambleCostControl = true;
    RMD.GambleCostControl = { true, "Disabling this feature serves no benefit in ReMoDDeD" };
    RMD.ForcedCombatLog = false;
    RMD.TransmogVisuals = { false, "This feature must be enabled for now for Extended Itemcodes currently" };
    RMD.ExtendedItemcodes = { true, "We rely on this feature for our expanded item catalog\nDisabling it serves no benefit" };
    RMD.ForcedFloatingDamage = false;
    RMD.HPRolloverMods = { true, "This feature helps us keep monster HP values in check" };

    RMD.HPRolloverDifficulty = { true, "We want HP Rollover Mods to apply to all difficulties for balancing" };
    RMD.ForcedHPRolloverDifficulty = -1;
    RMD.HPRolloverPercent = { true, "For balancing reasons, maximum reduction must be applied in high player count settings" };
    RMD.ForcedHPRolloverPercent = 99;
    RMD.SunderValue = { true, "We balance sunder effects around a value of 99\nChanging this would break monster scaling" };
    RMD.ForcedSunderValue = 99;

    g_ModOverrides["RMD-MP"] = RMD;
}

void ApplyModOverrides(const std::string& modName)
{
    auto it = g_ModOverrides.find(modName);
    if (it == g_ModOverrides.end())
        return;

    const auto& o = it->second;

    if (o.MonsterStatsDisplay.locked)
        d2rHUDConfig.MonsterStatsDisplay = o.ForcedMonsterStatsDisplay;

    if (o.SunderedMonUMods.locked)
        d2rHUDConfig.SunderedMonUMods = o.ForcedSunderedMonUMods;

    if (o.MinionEquality.locked)
        d2rHUDConfig.MinionEquality = o.ForcedMinionEquality;

    if (o.GambleCostControl.locked)
    {
        d2rHUDConfig.GambleCostControl = o.ForcedGambleCostControl;
        settings.gambleForce = o.ForcedGambleCostControl;
    }

    if (o.CombatLog.locked)
        d2rHUDConfig.CombatLog = o.ForcedCombatLog;

    if (o.TransmogVisuals.locked)
        d2rHUDConfig.TransmogVisuals = o.ForcedTransmogVisuals;

    if (o.ExtendedItemcodes.locked)
        d2rHUDConfig.ExtendedItemcodes = o.ForcedExtendedItemcodes;

    if (o.FloatingDamage.locked)
        d2rHUDConfig.FloatingDamage = o.ForcedFloatingDamage;

    if (o.HPRolloverMods.locked)
        d2rHUDConfig.HPRolloverMods = o.ForcedHPRolloverMods;

    if (o.HPRolloverDifficulty.locked)
        d2rHUDConfig.HPRolloverDifficulty = o.ForcedHPRolloverDifficulty;

    if (o.HPRolloverPercent.locked)
        d2rHUDConfig.HPRolloverPercent = o.ForcedHPRolloverPercent;

    if (o.SunderValue.locked)
        d2rHUDConfig.SunderValue = o.ForcedSunderValue;
}

const LockedValueInfo* GetLockInfo(const std::string& modName, const LockedValueInfo ModOverrideSettings::* field)
{
    auto it = g_ModOverrides.find(modName);
    if (it == g_ModOverrides.end())
        return nullptr;

    const auto& info = it->second.*field;
    return info.locked ? &info : nullptr;
}

#pragma endregion

#pragma region - Menu Displays

void RightColumnSeparator(float rightWidth, float thickness = 2.0f)
{
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImVec2 p1 = ImVec2(p0.x + rightWidth, p0.y);
    ImGui::GetWindowDrawList()->AddLine(p0, p1, IM_COL32(200, 200, 200, 255), thickness);
    ImGui::Dummy(ImVec2(0.0f, thickness + 2.0f));
}

void ShowGrailMenu()
{
    static bool wasOpen = false;
    static bool initialized = false;
    if (!initialized)
    {
        LoadD2RHUDConfig(configFilePath);
        initialized = true;
    }

    if (!showGrailMenu)
    {
        // Menu just closed? Save progress including AutoBackup settings
        if (wasOpen)
            SaveFullGrailConfig(configFilePath, false);
            
        wasOpen = false;
        return;
    }
    wasOpen = true;

    static bool itemsLoaded = false;

    if (!itemsLoaded)
    {
        LoadAllItemData();
        itemsLoaded = true;
    }

    ScanStashPages();

    // ------- Tooltip helper -------
    auto ShowOffsetTooltip = [](const char* text)
        {
            ImVec2 mousePos = ImGui::GetIO().MousePos;
            ImGui::SetNextWindowPos(ImVec2(mousePos.x + 70, mousePos.y), ImGuiCond_Always);
            ImGui::BeginTooltip();
            float tooltipWidth = 600.0f;
            ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + tooltipWidth);
            ImGui::TextUnformatted(text);
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        };

    CenterWindow(ImVec2(850, 500));

    PushFontSafe(3);
    ImGui::Begin("Grail Tracker", &showGrailMenu, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar);
    DrawWindowTitleAndClose("Grail Tracker", &showGrailMenu);
    PopFontSafe(3);

    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 6));

    // Persistent State
    static int selectedCategory = 0;  // 0 = Sets, 1 = Uniques
    static int selectedType = -1;     // For types later (Goal 4)
    static int selectedSet = -1;      // For future navigation
    static int selectedUnique = -1;
    static char searchBuffer[128] = "";
    

    // Layout: Left Panel / Right Panel
    ImVec2 full = ImGui::GetContentRegionAvail();
    float leftWidth = 240.0f;

    // LEFT PANEL
    ImGui::BeginChild("left_panel", ImVec2(leftWidth, full.y), true);
    ImVec4 darkRed = ImVec4(0.6f, 0.1f, 0.1f, 1.0f);

    // --- Search ---
    ImGui::PushStyleColor(ImGuiCol_Text, darkRed);
    {
        float avail = ImGui::GetContentRegionAvail().x;
        float textWidth = ImGui::CalcTextSize("Item Search:").x;
        ImGui::SetCursorPosX((avail - textWidth) * 0.5f + ImGui::GetCursorPosX());
    }
    ImGui::Text("Item Search:");
    ImGui::PopStyleColor();

    // Center the input box under the label
    ImGui::PushItemWidth(leftWidth - 55);
    {
        float inputWidth = leftWidth - 55;
        float avail = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX((avail - inputWidth) * 0.5f + ImGui::GetCursorPosX());
    }
    ImGui::InputText("##Search", searchBuffer, IM_ARRAYSIZE(searchBuffer));
    ImGui::PopItemWidth();
    ImGui::Dummy(ImVec2(0, 5));

    // --- Filter --- Centered checkbox ---
    {
        const char* label = "Hide Collected";
        ImVec2 labelSize = ImGui::CalcTextSize(label);
        float checkboxWidth = ImGui::GetFrameHeight();
        float totalWidth = checkboxWidth + 4 + labelSize.x;
        float avail = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX((avail - totalWidth) * 0.5f + ImGui::GetCursorPosX());
    }
    ImGui::Checkbox("Hide Collected", &showCollected);

    // --- Show Excluded --- Centered checkbox ---
    {
        const char* label = "Show Excluded";
        ImVec2 labelSize = ImGui::CalcTextSize(label);
        float checkboxWidth = ImGui::GetFrameHeight();
        float totalWidth = checkboxWidth + 4 + labelSize.x;
        float avail = ImGui::GetContentRegionAvail().x;

        // center horizontally
        ImGui::SetCursorPosX((avail - totalWidth) * 0.5f + ImGui::GetCursorPosX());
    }
    ImGui::Checkbox("Show Excluded", &showExcluded);

    if (ImGui::IsItemHovered())
        ShowOffsetTooltip("displays excluded items in the list, but with grey text");

    // --- Show Base Codes --- Centered ---
    {
        const char* label = "Show Base Codes";
        ImVec2 labelSize = ImGui::CalcTextSize(label);
        float checkboxWidth = ImGui::GetFrameHeight();
        float totalWidth = checkboxWidth + 4 + labelSize.x;
        float avail = ImGui::GetContentRegionAvail().x;

        ImGui::SetCursorPosX((avail - totalWidth) * 0.5f + ImGui::GetCursorPosX());
    }
    ImGui::Checkbox("Show Base Codes", &showBaseCodes);
    if (ImGui::IsItemHovered())
        ShowOffsetTooltip("Show the raw base item codes in the list.");

    // --- Show Base Names --- Centered ---
    {
        const char* label = "Show Base Names";
        ImVec2 labelSize = ImGui::CalcTextSize(label);
        float checkboxWidth = ImGui::GetFrameHeight();
        float totalWidth = checkboxWidth + 4 + labelSize.x;
        float avail = ImGui::GetContentRegionAvail().x;

        ImGui::SetCursorPosX((avail - totalWidth) * 0.5f + ImGui::GetCursorPosX());
    }
    ImGui::Checkbox("Show Base Names", &showBaseNames);
    if (ImGui::IsItemHovered())
        ShowOffsetTooltip("Show the readable base item names in the list.");

    // --- Show Duplicates --- Centered ---
    {
        const char* label = "Show Duplicates";
        ImVec2 labelSize = ImGui::CalcTextSize(label);
        float checkboxWidth = ImGui::GetFrameHeight();
        float totalWidth = checkboxWidth + 4 + labelSize.x;

        // center horizontally
        float avail = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX((avail - totalWidth) * 0.5f + ImGui::GetCursorPosX());

        // render checkbox
        ImGui::Checkbox(label, &showDuplicates);

        // tooltip
        if (ImGui::IsItemHovered())
            ShowOffsetTooltip("Show duplicate items found in your stash");
    }


    // --- Backup Section ---
    ImGui::Dummy(ImVec2(0, 3));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 3));

    ImGui::PushStyleColor(ImGuiCol_Text, darkRed);
    {
        float avail = ImGui::GetContentRegionAvail().x;
        float textWidth = ImGui::CalcTextSize("Backup Path:").x;
        ImGui::SetCursorPosX((avail - textWidth) * 0.5f + ImGui::GetCursorPosX());
    }
    ImGui::Text("Backup Path:");
    ImGui::PopStyleColor();

    ImGui::PushItemWidth(leftWidth - 55);
    {
        float inputWidth = leftWidth - 55;
        float avail = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX((avail - inputWidth) * 0.5f + ImGui::GetCursorPosX());
    }
    ImGui::InputText("##BackupPath", backupPath, IM_ARRAYSIZE(backupPath));
    ImGui::PopItemWidth();
    ImGui::Dummy(ImVec2(0, 2));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 2));

    // --- Centered Auto-Backup Checkboxes Block ---
    const char* labels[] = { "Auto-Backups", "Use Timestamps", "Overwrite Old" };
    float widest = 0.0f;
    for (auto label : labels) {
        float w = ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2 + 20;
        if (w > widest) widest = w;
    }
    float avail = ImGui::GetContentRegionAvail().x;
    float startX = (avail - widest) * 0.5f + ImGui::GetCursorPosX();

    ImGui::SetCursorPosX(startX);
    ImGui::Checkbox("Auto-Backups", &autoBackups);
    ImGui::BeginDisabled(!autoBackups);
    if (ImGui::IsItemHovered()) ShowOffsetTooltip("Enable automatic backups on the specified interval.");

    ImGui::SetCursorPosX(startX);
    if (ImGui::Checkbox("Use Timestamps", &backupWithTimestamps))
        if (backupWithTimestamps) overwriteOldBackup = false;
    if (ImGui::IsItemHovered()) ShowOffsetTooltip("Append current date/time to backup filename to avoid overwriting.");

    ImGui::SetCursorPosX(startX);
    if (ImGui::Checkbox("Overwrite Old", &overwriteOldBackup))
        if (overwriteOldBackup) backupWithTimestamps = false;
    if (ImGui::IsItemHovered()) ShowOffsetTooltip("Overwrite previous backup file instead of creating a new one.");

    ImGui::Dummy(ImVec2(0, 2));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 2));

    // --- Centered Backup Interval ---
    ImGui::PushStyleColor(ImGuiCol_Text, darkRed);
    const char* intervalLabel = "Backup Interval:";
    ImVec2 labelSize = ImGui::CalcTextSize(intervalLabel);
    avail = ImGui::GetContentRegionAvail().x;
    ImGui::SetCursorPosX((avail - labelSize.x) * 0.5f + ImGui::GetCursorPosX());
    ImGui::Text("%s", intervalLabel);
    ImGui::PopStyleColor();

    float inputWidth = 100;
    avail = ImGui::GetContentRegionAvail().x;
    ImGui::SetCursorPosX((avail - inputWidth) * 0.5f + ImGui::GetCursorPosX());
    ImGui::SetNextItemWidth(inputWidth);
    ImGui::InputInt("##BackupInterval", &backupIntervalMinutes);

    if (ImGui::IsItemHovered())
        ShowOffsetTooltip("How often to save automatic backups.\nMeasured in minutes.");

    ImGui::EndDisabled();
    ImGui::Dummy(ImVec2(0, 3));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 3));
    ImGui::EndChild();

    // RIGHT PANEL
    ImGui::SameLine();
    ImGui::BeginChild("right_panel", ImVec2(0, full.y), true);

    std::string searchStr = searchBuffer;
    auto Trim = [](std::string s) {
        s.erase(s.begin(), std::find_if(s.begin(), s.end(),
            [](unsigned char c) { return !std::isspace(c); }));
        s.erase(std::find_if(s.rbegin(), s.rend(),
            [](unsigned char c) { return !std::isspace(c); }).base(), s.end());
        return s;
        };

    // --- Category Buttons (Sets / Uniques) ---
    auto CategoryButton = [&](const char* label, int id)
        {
            bool selected = (selectedCategory == id);
            ImVec4 textColor = (id == 0) ? ImVec4(0.4f, 1.0f, 0.4f, 1.0f) : ImVec4(1.0f, 0.84f, 0.2f, 1.0f);

            ImGui::PushStyleColor(ImGuiCol_Text, textColor);

            // Use Button instead of Selectable to avoid full-width
            if (ImGui::Button(label, ImVec2(0, 0)))
                selectedCategory = id;

            ImGui::PopStyleColor();

            // Tooltip for collection progress
            if (ImGui::IsItemHovered())
            {
                ImVec2 mousePos = ImGui::GetIO().MousePos;
                ImGui::SetNextWindowPos(ImVec2(mousePos.x + 70, mousePos.y), ImGuiCond_Always);
                ImGui::BeginTooltip();

                ImVec4 labelColor = ImVec4(0.6f, 0.8f, 1.0f, 1.0f);
                ImVec4 valueColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);

                if (id == 0) // Sets
                {
                    std::unordered_map<std::string, std::pair<int, int>> setProgress;
                    for (auto& s : g_SetItems)
                    {
                        auto& p = setProgress[s.setName];
                        p.second++;
                        if (s.collected) p.first++;
                    }

                    int collectedSets = 0;
                    for (auto& [name, p] : setProgress)
                        if (p.first == p.second)
                            collectedSets++;

                    int totalSets = (int)setProgress.size();
                    int collectedItems = 0;
                    for (auto& s : g_SetItems) if (s.collected) collectedItems++;
                    int totalItems = (int)g_SetItems.size();

                    ImGui::PushStyleColor(ImGuiCol_Text, labelColor);
                    ImGui::Text("Items Collected:");
                    ImGui::PopStyleColor();
                    ImGui::SameLine();
                    ImGui::PushStyleColor(ImGuiCol_Text, valueColor);
                    ImGui::Text("%d/%d", collectedItems, totalItems);
                    ImGui::PopStyleColor();

                    ImGui::PushStyleColor(ImGuiCol_Text, labelColor);
                    ImGui::Text("Sets Completed:");
                    ImGui::PopStyleColor();
                    ImGui::SameLine();
                    ImGui::PushStyleColor(ImGuiCol_Text, valueColor);
                    ImGui::Text("%d/%d", collectedSets, totalSets);
                    ImGui::PopStyleColor();
                }
                else // Uniques
                {
                    int collectedItems = 0;
                    for (auto& u : g_UniqueItems) if (u.collected) collectedItems++;
                    int totalItems = (int)g_UniqueItems.size();

                    ImGui::PushStyleColor(ImGuiCol_Text, labelColor);
                    ImGui::Text("Unique Items:");
                    ImGui::PopStyleColor();
                    ImGui::SameLine();
                    ImGui::PushStyleColor(ImGuiCol_Text, valueColor);
                    ImGui::Text("%d/%d", collectedItems, totalItems);
                    ImGui::PopStyleColor();
                }

                ImGui::EndTooltip();
            }
        };

    // Sets button
    CategoryButton("Sets", 0);

    // Compute button widths
    float setsWidth = ImGui::CalcTextSize("Sets").x + ImGui::GetStyle().FramePadding.x * 2;
    float uniquesWidth = ImGui::CalcTextSize("Uniques").x + ImGui::GetStyle().FramePadding.x * 2;
    float panelWidth = ImGui::GetContentRegionAvail().x;

    // Compute label width
    std::string trackerLabel = "<  Choose your collection type  >";
    float labelWidth = ImGui::CalcTextSize(trackerLabel.c_str()).x;
    float spacing = (panelWidth - setsWidth - uniquesWidth - labelWidth - 25) / 2.0f;

    // Move cursor after Sets button + spacing
    ImGui::SameLine();
    ImGui::Dummy(ImVec2(spacing, 0));
    ImGui::SameLine();

    // Draw the label
    ImGui::PushStyleColor(ImGuiCol_Text, darkRed);
    ImGui::Text("%s", trackerLabel.c_str());
    ImGui::PopStyleColor();

    // Keep Uniques button on same line, at the right
    ImGui::SameLine(panelWidth - uniquesWidth + 10);
    CategoryButton("Uniques", 1);

    ImGui::Dummy(ImVec2(0, 5));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 5));

    // --- Display the selected list ---
    if (selectedCategory == 0)
    {
        // SET ITEM LIST (GROUPED BY SET NAME)
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.988f, 0.0f, 1.0f));
        ImGui::Text("Set Items");
        ImGui::PopStyleColor();

        // Right-aligned mode label on the SAME line
        const char* modeText = IsHardcore() ? "[Hardcore]" : "[Softcore]";
        float right = ImGui::GetWindowContentRegionMax().x;
        float textWidth = ImGui::CalcTextSize(modeText).x;

        // Move cursor to right edge minus text width
        ImGui::SameLine(right - textWidth);
        ImGui::TextUnformatted(modeText);

        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, 4));

        // Build map of sets with filters
        std::unordered_map<std::string, std::vector<SetItemEntry*>> sets;
        for (auto& s : g_SetItems)
        {
            // FILTERS
            if (!searchStr.empty() &&
                !CaseInsensitiveContains(s.name, searchStr) &&
                !CaseInsensitiveContains(s.setName, searchStr))
                continue;

            if (showCollected && s.collected)
                continue;

            sets[s.setName].push_back(&s);
        }

        // Collect set names and sort alphabetically
        std::vector<std::string> sortedSetNames;
        for (auto& [setName, items] : sets)
            sortedSetNames.push_back(setName);

        std::sort(sortedSetNames.begin(), sortedSetNames.end());

        // Display sets
        for (auto& setName : sortedSetNames)
        {
            auto& items = sets[setName];

            // Filter items within this set
            std::vector<SetItemEntry*> visibleItems;
            for (auto* s : items)
            {
                // Show Duplicates filter: only keep items with duplicates
                if (showDuplicates && s->locations.size() <= 1)
                    continue;

                visibleItems.push_back(s);
            }

            // Skip this set if no items to display
            if (visibleItems.empty())
                continue;

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.988f, 0.0f, 1.0f));
            if (ImGui::TreeNode(setName.c_str()))
            {
                ImGui::PopStyleColor();

                for (auto* s : visibleItems)
                {
                    std::string label;
                    if (showBaseCodes && showBaseNames)
                        label = s->name + " (" + s->code + ", " + s->itemName + ")";
                    else if (showBaseCodes)
                        label = s->name + " (" + s->code + ")";
                    else if (showBaseNames)
                        label = s->name + " (" + s->itemName + ")";
                    else
                        label = s->name;

                    bool* checked = &s->collected;

                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.988f, 0.0f, 1.0f));
                    ImGui::Checkbox(label.c_str(), checked);

                    if (ImGui::IsItemHovered())
                        ShowItemLocationTooltip(s->id, true);

                    ImGui::PopStyleColor();
                }

                ImGui::TreePop();
            }
            else
            {
                ImGui::PopStyleColor();
            }
        }
    }
    else
    {
        // UNIQUE ITEM LIST
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.780f, 0.702f, 0.467f, 1.0f));
        ImGui::Text("Unique Items");

        // Button for excluded items
        ImGui::SameLine();
        if (ImGui::SmallButton("Excluded"))
            ImGui::OpenPopup("ExcludedItemsPopup");

        ImGui::PopStyleColor();

        // Right-aligned mode label on the SAME line
        const char* modeText = IsHardcore() ? "[Hardcore]" : "[Softcore]";
        float right = ImGui::GetWindowContentRegionMax().x;
        float textWidth = ImGui::CalcTextSize(modeText).x;

        // Move cursor to right edge minus text width
        ImGui::SameLine(right - textWidth);
        ImGui::TextUnformatted(modeText);

        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, 4));

        if (ImGui::BeginPopup("ExcludedItemsPopup"))
        {
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.0f, 1.0f), "Excluded Items:");
            ImGui::Separator();
            for (const auto& item : g_ExcludedGrailItems)
                ImGui::BulletText("%s", item.c_str());
            ImGui::EndPopup();
        }

        // Build filtered list of visible items
        std::vector<UniqueItemEntry*> visibleItems;
        for (auto& u : g_UniqueItems)
        {
            std::string trimmedName = Trim(u.name);
            bool isExcluded = g_ExcludedGrailItems.count(trimmedName) > 0;

            // FILTERS
            if (!showExcluded && isExcluded)
                continue;

            if (!searchStr.empty() && !CaseInsensitiveContains(u.name, searchStr))
                continue;

            if (showCollected && u.collected && !isExcluded)
                continue;

            // Show Duplicates filter
            if (showDuplicates && u.locations.size() <= 1)
                continue;

            visibleItems.push_back(&u);
        }

        // Render only visible items
        for (size_t i = 0; i < visibleItems.size(); ++i)
        {
            auto* u = visibleItems[i];
            std::string trimmedName = Trim(u->name);
            bool isExcluded = g_ExcludedGrailItems.count(trimmedName) > 0;

            std::string label;
            if (showBaseCodes && showBaseNames)
                label = u->name + " (" + u->code + ", " + u->itemName + ")";
            else if (showBaseCodes)
                label = u->name + " (" + u->code + ")";
            else if (showBaseNames)
                label = u->name + " (" + u->itemName + ")";
            else
                label = u->name;

            bool* checked = &u->collected;
            std::string checkboxID = label + "##" + std::to_string(i);

            // Begin horizontal line
            ImGui::BeginGroup();

            if (isExcluded)
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.55f, 0.55f, 1.0f)); // grey
            else
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.780f, 0.702f, 0.467f, 1.0f)); // gold

            if (ImGui::Checkbox(checkboxID.c_str(), checked))
            {
                // TODO: save state if needed
            }

            if (ImGui::IsItemHovered())
                ShowItemLocationTooltip(u->id, false);

            ImGui::PopStyleColor();

            // ACTION BUTTON
            float offsetX = ImGui::GetContentRegionAvail().x - 80.0f;
            if (offsetX < 0) offsetX = 0;

            ImGui::SameLine(offsetX);
            std::string buttonID;

            if (isExcluded)
            {
                buttonID = "Include##" + std::to_string(i);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.9f, 0.2f, 1.0f)); // green
                if (ImGui::SmallButton(buttonID.c_str()))
                {
                    g_ExcludedGrailItems.erase(trimmedName);
                    SaveFullGrailConfig(configFilePath, false);
                }
                if (ImGui::IsItemHovered())
                    ShowOffsetTooltip("Include this item back in your Grail hunt");
                ImGui::PopStyleColor();
            }
            else
            {
                buttonID = "Exclude##" + std::to_string(i);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.15f, 0.15f, 1.0f)); // red
                if (ImGui::SmallButton(buttonID.c_str()))
                {
                    g_ExcludedGrailItems.insert(trimmedName);
                    u->enabled = false;
                    SaveFullGrailConfig(configFilePath, false);
                }
                if (ImGui::IsItemHovered())
                    ShowOffsetTooltip("Exclude this item from your Grail hunt");
                ImGui::PopStyleColor();
            }

            ImGui::EndGroup();
        }
    }

    ImGui::EndChild();
    ImGui::End();
}

void ShowHotkeyMenu()
{
    static bool wasOpen = false;

    if (!showHotkeyMenu)
    {
        // Menu just closed? Save progress including AutoBackup settings
        if (wasOpen)
            SaveFullGrailConfig(configFilePath, false);

        wasOpen = false;
        return;
    }
    wasOpen = true;

    if (!showHotkeyMenu) return;

    EnableAllInput();
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 windowSize = ImVec2(900, 480);
    CenterWindow(windowSize);
    PushFontSafe(3);
    ImGui::Begin("D2R Hotkeys", &showHotkeyMenu, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar);
    DrawWindowTitleAndClose("D2R Hotkeys", &showHotkeyMenu);
    if (GetFont(3)) ImGui::PopFont();
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.0f, 5.0f));

    // --- Split hotkeys (Keybinds ONLY) ---
    std::vector<std::pair<std::string, std::pair<std::string, std::string>>> singleKeys;
    for (auto& [name, pair] : g_Hotkeys)
        singleKeys.push_back({ name, pair });

    static std::string hoveredHotkey;
    hoveredHotkey.clear();

    // --- 2-column layout ---
    ImGui::Columns(2, nullptr, true);
    ImGui::SetColumnWidth(0, 450);
    ImGui::SetColumnWidth(1, 450);

    ImFont* inputFont = GetFont(1);

    // --- LEFT COLUMN: Single Hotkeys ---
    float leftColumnStartY = ImGui::GetCursorPosY();
    PushFontSafe(2);
    const char* colTitle = "Standard Hotkeys";
    float colWidth = ImGui::GetColumnWidth();
    float textW = ImGui::CalcTextSize(colTitle).x;
    ImGui::SetCursorPosX((colWidth - textW) * 0.5f);
    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.3f, 1.0f), "%s", colTitle);
    ImGui::Dummy(ImVec2(0.0f, 3.0f));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.0f, 3.0f));
    PopFontSafe(2);

    static std::string activeHotkeyInput; // currently editing hotkey
    static std::vector<std::string> activeCombo;
    static bool hotkeyReleased = true;
    std::string keyDisplay;

    // Define the order of hotkeys
    static const std::vector<std::string> hotkeyOrder = {
        "Open HUDCC Menu",
        "Reload Game/Filter",
        "Identify Items",
        "Transmute",
        "Force Save",
        "Open Cube Panel",
        "Remove Ground Items",
        "Reset Skills",
        "Reset Stats",
        "Cycle Filter Level",
        "Cycle TZ Backward",
        "Cycle TZ Forward",
        "Toggle TZ Stats Display",
    };

    for (const auto& name : hotkeyOrder)
    {
        auto it = g_Hotkeys.find(name);
        if (it == g_Hotkeys.end())
            continue;

        auto& pair = it->second;

        ImGui::Text("%s:", name.c_str());
        ImGui::SameLine();

        // --- Capture multi-key input if this hotkey is active ---
        if (activeHotkeyInput == name)
        {
            auto pressedKeys = GetPressedKeys();

            // Handle DELETE key separately
            if (std::find(pressedKeys.begin(), pressedKeys.end(), "VK_DELETE") != pressedKeys.end())
            {
                // Clear the hotkey immediately
                pair.first.clear();
                g_Hotkeys[name] = pair;
                SaveFullGrailConfig("HUDConfig_" + modName + ".json", false);

                activeCombo.clear();
                hotkeyReleased = true;
                pressedKeys.clear(); // ignore DELETE for combo logic
            }

            if (!pressedKeys.empty())
                hotkeyReleased = false;

            // If all keys released, finalize the combo
            if (pressedKeys.empty() && !hotkeyReleased)
            {
                if (!activeCombo.empty())
                {
                    std::string combo;
                    for (size_t i = 0; i < activeCombo.size(); ++i)
                    {
                        combo += activeCombo[i];
                        if (i + 1 < activeCombo.size())
                            combo += " + ";
                    }

                    if (combo != pair.first)
                    {
                        pair.first = combo;
                        g_Hotkeys[name] = pair;
                        SaveFullGrailConfig("HUDConfig_" + modName + ".json", false);
                        LoadCommandsAndKeybinds("HUDConfig_" + modName + ".json");
                    }
                }

                activeCombo.clear();
                hotkeyReleased = true;
            }
            else if (!pressedKeys.empty())
            {
                // Add new keys to combo, ignoring duplicates
                for (auto& k : pressedKeys)
                {
                    if (k != "VK_DELETE" && std::find(activeCombo.begin(), activeCombo.end(), k) == activeCombo.end())
                        activeCombo.push_back(k);
                }
            }
        }


        // --- Display current hotkey ---
        keyDisplay = DisplayKey(pair.first);

        char buffer[128];
        strncpy(buffer, keyDisplay.c_str(), sizeof(buffer));
        buffer[sizeof(buffer) - 1] = '\0';

        ImGui::PushItemWidth(180);
        if (inputFont) ImGui::PushFont(inputFont);
        ImGui::InputText(("##key_" + name).c_str(), buffer, sizeof(buffer), ImGuiInputTextFlags_ReadOnly);

        if (ImGui::IsItemClicked())
        {
            activeHotkeyInput = name; // start editing this hotkey
            activeCombo.clear();
            hotkeyReleased = true;
        }

        if (!ImGui::IsItemFocused() && activeHotkeyInput == name)
            activeHotkeyInput.clear();

        if (inputFont) ImGui::PopFont();
        ImGui::PopItemWidth();
    }

    // --- RIGHT COLUMN: Command Hotkeys ---
    ImGui::NextColumn();
    ImGui::SetCursorPosY(leftColumnStartY);
    {
        const char* colTitle = "Command Hotkeys";
        float colWidth = ImGui::GetColumnWidth();
        float colStartX = ImGui::GetCursorPosX();

        float textW = ImGui::CalcTextSize(colTitle).x;
        ImGui::SetCursorPosX(colStartX + (colWidth - textW) * 0.5f);
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.3f, 1.0f), "%s", colTitle);
        ImGui::Dummy(ImVec2(0.0f, 15.0f));

        // ---- Custom Commands ----
        for (size_t i = 0; i < g_CommandHotkeys.size(); ++i)
        {
            auto& cmd = g_CommandHotkeys[i];
            std::string label = "Command " + std::to_string(i + 1);

            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(2, 130, 199, 255));
            ImGui::Text("%s:", label.c_str());
            ImGui::PopStyleColor();
            ImGui::SameLine();

            char keyBuf[128];
            strncpy(keyBuf, DisplayKey(cmd.key).c_str(), sizeof(keyBuf));
            keyBuf[sizeof(keyBuf) - 1] = '\0';

            ImGui::PushItemWidth(120);
            if (inputFont) ImGui::PushFont(inputFont);
            if (ImGui::InputText(("##cmd_key_" + std::to_string(i)).c_str(), keyBuf, sizeof(keyBuf)))
                cmd.key = keyBuf;
            if (inputFont) ImGui::PopFont();
            ImGui::PopItemWidth();

            ImGui::SameLine();

            char cmdBuf[256];
            strncpy(cmdBuf, cmd.command.c_str(), sizeof(cmdBuf));
            cmdBuf[sizeof(cmdBuf) - 1] = '\0';

            ImGui::PushItemWidth(220);
            if (inputFont) ImGui::PushFont(inputFont);
            if (ImGui::InputText(("##cmd_txt_" + std::to_string(i)).c_str(), cmdBuf, sizeof(cmdBuf)))
                cmd.command = cmdBuf;
            if (inputFont) ImGui::PopFont();
            ImGui::PopItemWidth();

            if (ImGui::IsItemHovered())
                hoveredHotkey = label;
        }

        // ---- Startup Commands ----
        ImGui::Dummy(ImVec2(0.0f, 10.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(2, 130, 199, 255));
        ImGui::Text("Startup Commands");
        ImGui::PopStyleColor();

        static char startupBuf[512] = { 0 };
        static bool startupBufInitialized = false;

        if (!startupBufInitialized)
        {
            strncpy(startupBuf, g_StartupCommands.c_str(), sizeof(startupBuf));
            startupBuf[sizeof(startupBuf) - 1] = '\0';
            startupBufInitialized = true;
        }

        ImGui::PushItemWidth(-1);
        if (inputFont) ImGui::PushFont(inputFont);

        if (ImGui::InputTextMultiline("##startup_commands", startupBuf, sizeof(startupBuf), ImVec2(-1, 80)))
        {
            g_StartupCommands = startupBuf;
        }

        if (inputFont) ImGui::PopFont();
        ImGui::PopItemWidth();

    }

    ImGui::Columns(1);

    std::string desc = hoveredHotkey.empty() ? "Hover over a hotkey to see details." : "Set the hotkey for " + hoveredHotkey + ".";
    DrawBottomDescription(desc);

    ImGui::End();
    io.ConfigFlags = ImGui::GetIO().ConfigFlags;
}

void ShowLootMenu()
{
    if (!showLootMenu)
    {
        lootConfigLoaded = false;
        lootLogicLoaded = false;
        return;
    }

    if (!lootConfigLoaded)
    {
        LoadLootFilterConfig("lootfilter_config.lua");
        LoadLootFilterLogic("lootfilter.lua");
        lootConfigLoaded = true;
        lootLogicLoaded = true;
    }

    auto RenderColoredText = [&](const std::string& text)
        {
            ImVec4 currentColor = g_TextColors["white"]; // default color
            size_t pos = 0;

            while (pos < text.size())
            {
                size_t openBrace = text.find('{', pos);

                if (openBrace != pos)
                {
                    // Render text before the next color code
                    if (openBrace == std::string::npos) openBrace = text.size();
                    ImGui::PushStyleColor(ImGuiCol_Text, currentColor);
                    ImGui::TextUnformatted(text.c_str() + pos, text.c_str() + openBrace);
                    ImGui::PopStyleColor();
                    ImGui::SameLine(0.0f, 0.0f);
                    pos = openBrace;
                }

                if (pos >= text.size()) break;

                // Must be a '{', find closing brace
                size_t closeBrace = text.find('}', pos);
                if (closeBrace == std::string::npos)
                {
                    // Invalid, just render the rest
                    ImGui::PushStyleColor(ImGuiCol_Text, currentColor);
                    ImGui::TextUnformatted(text.c_str() + pos);
                    ImGui::PopStyleColor();
                    break;
                }

                // Extract color code, convert to lowercase for case-insensitive lookup
                std::string colorCode = text.substr(pos + 1, closeBrace - pos - 1);
                std::transform(colorCode.begin(), colorCode.end(), colorCode.begin(),
                    [](unsigned char c) { return std::tolower(c); });

                // Lookup in map
                auto it = g_TextColors.find(colorCode);
                currentColor = (it != g_TextColors.end()) ? it->second : g_TextColors["white"];

                pos = closeBrace + 1; // move past the closing brace
            }
        };

    EnableAllInput();
    CenterWindow(ImVec2(800, 400));
    static std::string hoveredKey;
    hoveredKey.clear();
    PushFontSafe(3);
    ImGui::Begin("D2RLoot Settings", &showLootMenu, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar);
    DrawWindowTitleAndClose("D2RLoot Settings", &showLootMenu);
    PopFontSafe(3);
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.0f, 5.0f));

    // --- Centered Wrapped Text Helper ---
    auto CenteredWrappedText = [&](const std::string& prefix, const std::string& text,
        const ImVec4& prefixColor = ImVec4(1, 0.7f, 0.3f, 1.0f),
        const ImVec4& valueColor = ImVec4(1, 1, 1, 1))
        {
            ImVec2 avail = ImGui::GetContentRegionAvail();
            float leftOffset = 10.0f;
            float wrapWidth = avail.x - leftOffset;

            ImVec2 prefixSize = ImGui::CalcTextSize(prefix.c_str(), nullptr, false, wrapWidth);
            ImVec2 valueSize = ImGui::CalcTextSize(text.c_str(), nullptr, false, wrapWidth);
            float totalWidth = prefixSize.x + valueSize.x;
            float cursorX = leftOffset + (wrapWidth - totalWidth) * 0.5f;
            if (cursorX < leftOffset) cursorX = leftOffset;

            ImGui::SetCursorPosX(cursorX);
            ImGui::TextColored(prefixColor, "%s", prefix.c_str());
            ImGui::SameLine(0, 0);
            ImGui::TextColored(valueColor, "%s", text.c_str());
        };

    if (!g_LootFilterHeader.Version.empty())
        CenteredWrappedText("My D2RLoot Version: ", g_LootFilterHeader.Version);
    CenteredWrappedText("My Selected Filter: ", g_LootFilterHeader.Title);

    ImGui::Dummy(ImVec2(0.0f, 3.0f));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.0f, 5.0f));

    // --- Boolean Checkboxes ---
    auto RenderCheckboxLine = [&](const std::vector<std::pair<std::string, std::string>>& items)
        {
            ImVec2 avail = ImGui::GetContentRegionAvail();
            float spacing = 10.0f;
            float totalWidth = 0.0f;

            for (auto& item : items)
            {
                totalWidth += ImGui::CalcTextSize(item.first.c_str()).x + ImGui::GetStyle().FramePadding.x * 2 + ImGui::GetFrameHeight();
            }
            totalWidth += spacing * (items.size() - 1);

            float startX = (avail.x - totalWidth) * 0.5f;
            if (startX < 0.0f) startX = 0.0f;
            ImGui::SetCursorPosX(startX);

            for (size_t i = 0; i < items.size(); ++i)
            {
                if (i > 0) ImGui::SameLine(0.0f, spacing);

                std::string key = items[i].second;
                std::string val = "";
                auto it = g_LuaVariables.find(key);
                if (it != g_LuaVariables.end()) val = it->second;

                bool boolValue = (val == "true");

                if (ImGui::Checkbox(items[i].first.c_str(), &boolValue))
                {
                    auto it2 = g_LuaVariables.find(key);
                    if (it2 != g_LuaVariables.end()) it2->second = boolValue ? "true" : "false";
                    else g_LuaVariables.insert({ key, boolValue ? "true" : "false" });
                }

                ImVec2 itemMin = ImGui::GetItemRectMin();
                ImVec2 itemMax = ImGui::GetItemRectMax();
                float textWidth = ImGui::CalcTextSize(items[i].first.c_str()).x;
                itemMax.x += textWidth;
                if (ImGui::IsMouseHoveringRect(itemMin, itemMax)) hoveredKey = key;
            }

            ImGui::Dummy(ImVec2(0.0f, 3.0f));
            ImGui::Separator();
            ImGui::Dummy(ImVec2(0.0f, 3.0f));
        };

    std::vector<std::pair<std::string, std::string>> bools = {
        { "Allow Overrides", "allowOverrides" },
        { "Mod Tips", "modTips" },
        { "Debug Mode", "Debug" },
        { "Audio Playback", "audioPlayback" }
    };
    RenderCheckboxLine(bools);

    // --- Input Text Helper ---
    // Map to track which key is in edit mode
    static std::unordered_map<std::string, bool> g_EditMode;

    auto RenderInputText = [&](const std::string& key, const std::string& label, const std::string& defaultVal = "Not Defined")
        {
            // --- Get value ---
            std::string value = defaultVal;
            auto it = g_LuaVariables.find(key);
            if (it != g_LuaVariables.end()) value = it->second;

            // Strip quotes for display
            if (!value.empty() && value.front() == '"' && value.back() == '"')
                value = value.substr(1, value.size() - 2);

            std::string fullLabel = label + " = ";
            float labelWidth = ImGui::CalcTextSize(fullLabel.c_str()).x;

            ImVec2 cursorPos = ImGui::GetCursorPos();
            ImGui::SetCursorPosY(cursorPos.y - 2.0f);

            ImGui::Text("%s", fullLabel.c_str());
            ImGui::SameLine(labelWidth + 10.0f, -3.0f);

            bool isEditing = g_EditMode[key];

            if (isEditing)
            {
                // --- Edit mode: normal input ---
                char buffer[512];
                strncpy(buffer, value.c_str(), sizeof(buffer));
                buffer[sizeof(buffer) - 1] = '\0';

                std::string inputID = "##val_" + key;
                ImGui::PushItemWidth(300.0f); // adjust width as needed
                bool changed = ImGui::InputText(inputID.c_str(), buffer, sizeof(buffer));
                ImGui::PopItemWidth();

                if (ImGui::IsItemActivated()) ImGui::SetKeyboardFocusHere(-1);

                if (changed) g_LuaVariables[key] = buffer;

                ImGui::SameLine();
                if (ImGui::Button(("Done##" + key).c_str())) g_EditMode[key] = false;
            }
            else
            {
                // --- Display mode: colored text ---
                RenderColoredText(value); // uses your function

                ImGui::SameLine();
                if (ImGui::Button(("Edit##" + key).c_str())) g_EditMode[key] = true;
            }

            // --- Hover detection ---
            ImVec2 itemMin = ImGui::GetItemRectMin();
            ImVec2 itemMax = ImGui::GetItemRectMax();
            itemMin.x -= labelWidth;
            if (ImGui::IsMouseHoveringRect(itemMin, itemMax)) hoveredKey = key;
        };




    RenderInputText("reload", "Reload Message");
    RenderInputText("audioVoice", "Audio Voice");
    RenderInputText("filter_level", "Filter Level");
    RenderInputText("language", "Language");

    // --- Filter Titles ---
    auto RenderFilterTitles = [&]()
        {
            std::string key = "filter_titles";
            std::string value = "";
            auto it = g_LuaVariables.find(key);
            if (it != g_LuaVariables.end()) value = it->second;

            std::vector<std::string> titles;
            if (!value.empty())
            {
                std::regex titleRegex(R"delim("([^"]*)")delim");
                for (auto i = std::sregex_iterator(value.begin(), value.end(), titleRegex);
                    i != std::sregex_iterator(); ++i)
                    titles.push_back((*i)[1].str());
            }
            if (titles.empty()) titles.push_back("Not Defined");

            std::string ftLabel = "Filter Titles = ";
            float labelWidth = ImGui::CalcTextSize(ftLabel.c_str()).x;
            ImGui::Text("%s", ftLabel.c_str());
            ImGui::SameLine(labelWidth + 10.0f);

            bool isEditing = g_EditMode[key];

            for (size_t idx = 0; idx < titles.size(); ++idx)
            {
                if (isEditing)
                {
                    // --- Edit mode: normal input ---
                    char buffer[256];
                    strncpy(buffer, titles[idx].c_str(), sizeof(buffer));
                    buffer[sizeof(buffer) - 1] = '\0';

                    std::string inputID = "##filter_title_" + std::to_string(idx);
                    ImGui::PushItemWidth(ImGui::CalcTextSize(buffer).x + 12.0f);
                    bool changed = ImGui::InputText(inputID.c_str(), buffer, sizeof(buffer));
                    ImGui::PopItemWidth();

                    if (changed) titles[idx] = buffer;
                }
                else
                {
                    // --- Display mode: colored text ---
                    RenderColoredText(titles[idx]);
                }

                if (idx + 1 < titles.size())
                {
                    ImGui::SameLine(0, 2);
                    ImGui::Text(", ");
                    ImGui::SameLine(0, 0);
                }
            }

            ImGui::SameLine();
            if (isEditing)
            {
                if (ImGui::Button(("Done##" + key).c_str()))
                    g_EditMode[key] = false;
            }
            else
            {
                if (ImGui::Button(("Edit##" + key).c_str()))
                    g_EditMode[key] = true;
            }

            // --- Update stored value ---
            std::string newValue = "{ ";
            for (size_t i = 0; i < titles.size(); ++i)
            {
                newValue += "\"" + titles[i] + "\"";
                if (i + 1 < titles.size()) newValue += ", ";
            }
            newValue += " }";
            g_LuaVariables[key] = newValue;
        };
    RenderFilterTitles();



    // --- Bottom Description ---
    std::string desc = "Hover over an option to see its description.";
    if (!hoveredKey.empty() && g_LuaDescriptions.count(hoveredKey))
        desc = g_LuaDescriptions.at(hoveredKey);

    DrawBottomDescription(desc);

    ImGui::End();
}

void ShowMemoryMenu()
{
    if (!showMemoryMenu) return;

    LoadMemoryConfigs(configFilePath);

    EnableAllInput();
    CenterWindow(ImVec2(950, 500));

    PushFontSafe(3);
    ImGui::Begin("Memory Edit Info", &showMemoryMenu,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar);

    // STICKY TITLE
    DrawWindowTitleAndClose("Memory Edit Info", &showMemoryMenu);
    PopFontSafe(3);

    // STICKY DESCRIPTION
    PushFontSafe(2);
    DrawBottomDescription("Shows the currently enabled memory edits.\nNow fully editable.");
    PopFontSafe(2);

    ImGui::Dummy(ImVec2(0, 10));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 6));

    // persistent dropdown selections
    static std::unordered_map<int, int> selectedIndexMap;
    static std::string selectedCategory = "All";

    // 2 COLUMN LAYOUT (LEFT = STICKY)
    float leftWidth = 160.0f;
    float spacing = 10.0f;

    ImGui::Columns(2, nullptr, false); // NOT scrollable
    ImGui::SetColumnWidth(0, leftWidth);

    // LEFT COLUMN (STICKY)
    {
        std::set<std::string> categories;
        for (auto& entry : g_MemoryConfigs)
            if (!entry.Category.empty())
                categories.insert(entry.Category);

        if (ImGui::Selectable("All", selectedCategory == "All"))
            selectedCategory = "All";

        for (auto& cat : categories)
        {
            if (ImGui::Selectable(cat.c_str(), selectedCategory == cat))
                selectedCategory = cat;
        }
    }

    // RIGHT COLUMN (SCROLLABLE CHILD)
    ImGui::NextColumn();
    float rightWidth = ImGui::GetContentRegionAvail().x;
    ImGui::BeginChild("RightScrollRegion", ImVec2(rightWidth, 0), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);

    // RENDER MEMORY ENTRIES (SCROLLABLE)
    for (auto& entry : g_MemoryConfigs)
    {
        if (selectedCategory != "All" && entry.Category != selectedCategory)
            continue;

        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        RightColumnSeparator(rightWidth, 2.0f);
        ImGui::Dummy(ImVec2(0.0f, 6.0f));

        ImGui::PushID(entry.UniqueID);

        // ===== TITLE =====
        float nameWidth = ImGui::CalcTextSize(entry.Name.c_str()).x;
        ImGui::SetCursorPosX((rightWidth - nameWidth) * 0.5f);
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.1f, 1.0f), "%s", entry.Name.c_str());

        // ===== DESCRIPTION =====
        if (!entry.Description.empty())
        {
            PushFontSafe(2);

            float wrapWidth = rightWidth * 0.9f;
            std::istringstream iss(entry.Description);
            std::string word, line;
            std::vector<std::string> lines;

            while (iss >> word)
            {
                std::string testLine = line.empty() ? word : line + " " + word;
                if (ImGui::CalcTextSize(testLine.c_str()).x > wrapWidth)
                {
                    lines.push_back(line);
                    line = word;
                }
                else line = testLine;
            }
            if (!line.empty()) lines.push_back(line);

            for (auto& l : lines)
            {
                float w = ImGui::CalcTextSize(l.c_str()).x;
                ImGui::SetCursorPosX((rightWidth - w) * 0.5f);
                ImGui::TextColored(ImVec4(0.0157f, 0.380f, 0.8f, 1.0f), "%s", l.c_str());
            }

            ImGui::Dummy(ImVec2(0.0f, 6.0f));
            PopFontSafe(2);
        }

        // ===== TYPE + LENGTH (same line) =====
        {
            PushFontSafe(2);

            const char* typeOptions[] = { "Hex", "Integer" };
            int typeIndex = (entry.Type == "Integer") ? 1 : 0;

            float typeLabelWidth = ImGui::CalcTextSize("Type:").x;
            float typeComboWidth = 80.0f;

            float lengthLabelWidth = ImGui::CalcTextSize("Length:").x;
            float lengthInputWidth = 80.0f;

            float totalWidth =
                typeLabelWidth + typeComboWidth +
                10.0f +
                lengthLabelWidth + lengthInputWidth;

            float startX = (rightWidth - totalWidth) * 0.5f;

            ImGui::SetCursorPosX(startX);
            ImGui::Text("Type:");
            ImGui::SameLine();

            ImGui::SetCursorPosX(startX + typeLabelWidth + 5.0f);
            ImGui::PushItemWidth(typeComboWidth);
            if (ImGui::Combo("##type", &typeIndex, typeOptions, IM_ARRAYSIZE(typeOptions)))
                entry.Type = (typeIndex == 1 ? "Integer" : "Hex");
            ImGui::PopItemWidth();

            ImGui::SameLine();
            ImGui::SetCursorPosX(startX + typeLabelWidth + typeComboWidth + 10.0f);
            ImGui::Text("Length:");

            ImGui::SameLine();
            ImGui::SetCursorPosX(startX + typeLabelWidth + typeComboWidth + 10.0f + lengthLabelWidth + 5.0f);
            ImGui::PushItemWidth(lengthInputWidth);
            ImGui::InputInt("##length", &entry.Length, 1, 10);
            if (entry.Length < 1) entry.Length = 1;
            ImGui::PopItemWidth();

            PopFontSafe(2);
        }

        // ===== ADDRESS COMBO =====
        {
            PushFontSafe(2);
            std::vector<std::string> addrList;

            if (!entry.Address.empty())
                addrList.push_back(entry.Address);

            for (auto& a : entry.Addresses)
                addrList.push_back(a);

            if (!addrList.empty())
            {
                int& selectedIdx = selectedIndexMap[entry.UniqueID];
                if (selectedIdx >= addrList.size()) selectedIdx = 0;

                std::string current = addrList[selectedIdx];

                float labelWidth = ImGui::CalcTextSize("Address:").x;
                float comboWidth = 120.0f;

                ImGui::SetCursorPosX((rightWidth - (labelWidth + comboWidth + 10)) * 0.5f);
                ImGui::Text("Address:");
                ImGui::SameLine();

                ImGui::PushItemWidth(comboWidth);
                if (ImGui::BeginCombo("##addr", current.c_str()))
                {
                    for (int i = 0; i < addrList.size(); ++i)
                    {
                        bool selected = (selectedIdx == i);
                        if (ImGui::Selectable(addrList[i].c_str(), selected))
                            selectedIdx = i;
                        if (selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                ImGui::PopItemWidth();
            }

            PopFontSafe(2);
        }


        // ===== DYNAMIC VALUE INPUT WIDTH =====
        {
            PushFontSafe(2);

            char buffer[256];
            strncpy(buffer, entry.Values.c_str(), sizeof(buffer));
            buffer[255] = '\0';

            float textWidth = ImGui::CalcTextSize(buffer).x;
            float boxWidth = std::clamp(textWidth + 20.0f, 60.0f, 300.0f);
            float labelWidth = ImGui::CalcTextSize("Value:").x;

            ImGui::SetCursorPosX((rightWidth - (labelWidth + boxWidth + 10)) * 0.5f);
            ImGui::Text("Value:");
            ImGui::SameLine();

            ImGui::PushItemWidth(boxWidth);
            if (ImGui::InputText("##value", buffer, sizeof(buffer)))
                entry.Values = buffer;
            ImGui::PopItemWidth();

            PopFontSafe(2);
        }

        // ===== Original | Modded =====
        {
            PushFontSafe(1);

            std::string original = "In Retail: " + entry.OriginalValues;
            std::string modded = "HUD Default: " + entry.ModdedValues;
            std::string sep = "|";

            ImVec2 o = ImGui::CalcTextSize(original.c_str());
            ImVec2 m = ImGui::CalcTextSize(modded.c_str());
            ImVec2 s = ImGui::CalcTextSize(sep.c_str());

            float total = o.x + s.x + m.x + 10.0f;
            float startX = (rightWidth - total) * 0.5f;

            ImGui::SetCursorPosX(startX);
            ImGui::TextUnformatted(original.c_str());
            ImGui::SameLine();

            ImGui::SetCursorPosX(startX + o.x + 5.0f);
            ImGui::TextUnformatted(sep.c_str());
            ImGui::SameLine();

            ImGui::SetCursorPosX(startX + o.x + s.x + 10.0f);
            ImGui::TextUnformatted(modded.c_str());

            PopFontSafe(1);
        }

        ImGui::PopID();
    }

    ImGui::EndChild();     // end scrollable region
    ImGui::Columns(1);     // reset
    ImGui::End();          // end main window
}

void ShowD2RHUDMenu()
{
    if (!showD2RHUDMenu) return;
        
    static std::string saveStatusMessage = "";
    static ImVec4 saveStatusColor = ImVec4(0, 1, 0, 1); // default green
    static bool initialized = false;
    if (!initialized)
    {
        LoadD2RHUDConfig(configFilePath);
        RegisterModOverrides();
        ApplyModOverrides(modName);
        initialized = true;
    }

    ImVec2 windowSize = ImVec2(850, 520);
    ImVec2 centerPos = ImVec2((ImGui::GetIO().DisplaySize.x - windowSize.x) * 0.5f, (ImGui::GetIO().DisplaySize.y - windowSize.y) * 0.5f);
    ImGui::SetNextWindowPos(centerPos, ImGuiCond_Once);
    ImGui::SetNextWindowSize(windowSize, ImGuiCond_Once);

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags &= ~ImGuiConfigFlags_NoKeyboard;

    int fontIndex = 3;
    ImFont* fontSize = (fontIndex >= 0 && fontIndex < io.Fonts->Fonts.Size) ? io.Fonts->Fonts[fontIndex] : nullptr;
    if (fontSize) ImGui::PushFont(fontSize);

    if (ImGui::Begin("D2RHUD Options", &showD2RHUDMenu, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar))
    {
        // --- CENTERED WINDOW TITLE WITH TOP-RIGHT CLOSE BUTTON ---
        ImGuiIO& io = ImGui::GetIO();
        int fontIndex = 3;
        ImFont* fontSize = (fontIndex >= 0 && fontIndex < io.Fonts->Fonts.Size) ? io.Fonts->Fonts[fontIndex] : nullptr;
        const char* windowTitle = "D2RHUD Options";
        float closeBtnSize = 20.0f;
        float padding = -10.0f;

        // Compute window content size
        ImVec2 contentSize = ImGui::GetContentRegionAvail();
        float titleWidth = ImGui::CalcTextSize(windowTitle).x;
        ImGui::SetCursorPosX((contentSize.x - titleWidth) * 0.5f);
        ImGui::TextColored(ImVec4(0.95f, 0.85f, 0.5f, 1.0f), "%s", windowTitle);

        // --- Same line: position close button at top-right ---
        ImGui::SameLine(contentSize.x - closeBtnSize - padding);
        ImVec2 btnPos = ImGui::GetCursorScreenPos();

        // Invisible button to capture clicks
        ImGui::InvisibleButton("CloseBtn", ImVec2(closeBtnSize, closeBtnSize));
        if (ImGui::IsItemClicked())
        {
            showD2RHUDMenu = false;
        }

        // Draw centered "X" in button
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 textSize = ImGui::CalcTextSize("X");
        ImVec2 textPos = ImVec2(
            btnPos.x + (closeBtnSize - textSize.x) * 0.5f,
            btnPos.y + (closeBtnSize - textSize.y) * 0.5f
        );
        drawList->AddText(textPos, IM_COL32(255, 80, 80, 255), "X");
        if (fontSize) ImGui::PopFont();
        ImGui::Separator();

        // --- two columns: left = controls, right = descriptions ---
        ImGui::Columns(2, nullptr, true);
        ImGui::SetColumnWidth(0, 280.0f); // width of control column

        static std::string descriptionTitle = "";
        static std::string descriptionText = "";
        static std::string descriptionNote = "";

        // --- SUBHEADER (centered in first column) ---
        ImVec2 columnStartPos = ImGui::GetCursorPos();
        float columnWidth = ImGui::GetColumnWidth();
        float headerWidth = ImGui::CalcTextSize("Current Options").x;
        ImGui::SetCursorPosX(columnStartPos.x + (columnWidth - headerWidth) * 0.5f);
        ImGui::TextColored(ImVec4(0.0157f, 0.380f, 0.8f, 1.0f), "Current Options");
        ImGui::Separator();

        // --- MENU CONTROLS (LEFT COLUMN) ---
        auto drawCheckbox = [&](const char* label, bool* value, const char* title, const char* desc, const char* descnote, const LockedValueInfo* lockInfo)
            {
                // Disable checkbox if locked
                if (lockInfo && lockInfo->locked)
                    ImGui::BeginDisabled();

                ImGui::Checkbox(label, value);

                if (lockInfo && lockInfo->locked)
                    ImGui::EndDisabled();

                // Update main description variables (existing behavior) even if disabled
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                {
                    descriptionTitle = title;
                    descriptionText = desc;
                    descriptionNote = descnote;
                }

                if (lockInfo && lockInfo->locked && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                {
                    ImVec2 mousePos = ImGui::GetIO().MousePos;
                    ImGui::SetNextWindowPos(ImVec2(mousePos.x + 70, mousePos.y), ImGuiCond_Always);
                    ImGui::BeginTooltip();
                    float tooltipWidth = 600.0f;
                    ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + tooltipWidth);
                    ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "Locked by Mod Author(s)"); // red
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "This setting cannot be changed while this mod is active"); // gray
                    ImGui::TextWrapped("");
                    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Reason:"); // orange
                    ImGui::SameLine();
                    ImGui::TextWrapped("%s", lockInfo->reason.c_str());
                    ImGui::PopTextWrapPos();
                    ImGui::EndTooltip();
                }

            };

        auto drawLabeledInput = [&](const char* label, auto widgetFunc, const char* title, const char* desc, const char* descnote, bool sameLine = true, int slValue = 35, const LockedValueInfo* lockInfo = nullptr)
            {
                ImGui::BeginGroup();

                ImGui::Text("%s", label);

                if (sameLine)
                    ImGui::SameLine(0.0f, slValue);

                // Disable input if locked
                if (lockInfo && lockInfo->locked)
                    ImGui::BeginDisabled();

                widgetFunc();

                if (lockInfo && lockInfo->locked)
                    ImGui::EndDisabled();

                ImGui::EndGroup();

                // Right Panel Description
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                {
                    descriptionTitle = title;
                    descriptionText = desc;
                    descriptionNote = descnote;
                }

                // Lock Reason Tooltip
                if (lockInfo && lockInfo->locked &&
                    ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                {
                    ImVec2 mousePos = ImGui::GetIO().MousePos;
                    ImGui::SetNextWindowPos(ImVec2(mousePos.x + 70, mousePos.y), ImGuiCond_Always);

                    ImGui::BeginTooltip();
                    ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + 600.0f);

                    ImGui::TextColored(
                        ImVec4(1.0f, 0.2f, 0.2f, 1.0f),
                        "Locked by Mod Author(s)"
                    );

                    ImGui::TextColored(
                        ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                        "This value cannot be changed while this mod is active\n"
                    );

                    ImGui::TextWrapped("");

                    ImGui::TextColored(
                        ImVec4(1.0f, 0.5f, 0.0f, 1.0f),
                        "Reason:"
                    );
                    ImGui::SameLine();
                    ImGui::TextWrapped("%s", lockInfo->reason.c_str());

                    ImGui::PopTextWrapPos();
                    ImGui::EndTooltip();
                }
            };

        drawCheckbox("Monster Stats Display", &d2rHUDConfig.MonsterStatsDisplay, "Monster Stats Display", "- Displays HP and Resistances of Monsters in real-time\n- Uses data retrieved directly from the game for accuracy\n- Values for -Enemy Resistance% (when applied by items) are not shown, they don't actually affect monsters\n\n", "This setting is best displayed using either of the Advanced Display Modes in D2RLAN", GetLockInfo(modName, &ModOverrideSettings::MonsterStatsDisplay));
        drawCheckbox("Sundered Monster UMods", &d2rHUDConfig.SunderedMonUMods, "Sundered Monster UMods", "- Allows Sundering mechanic to reduce MonUMod bonuses\n(Bonuses such as Cold Enchanted, Stone Skin, etc)\n- This edit applies when the monster is spawned\n\n", "If the monster is under 100 resistance (sunder threshold), then this edit will not apply", GetLockInfo(modName, &ModOverrideSettings::SunderedMonUMods));
        drawCheckbox("Minion Equality", &d2rHUDConfig.MinionEquality, "Minion Equality", "- Champ/Uniques minions assume their master's drops\n(Determined by your TreasureClassEx.txt entries)\n\n", "Requires TreasureClassEx.txt, Levels.txt, Monstats.txt and SuperUniques.txt to correctly map the drops between types", GetLockInfo(modName, &ModOverrideSettings::MinionEquality));
        drawCheckbox("Gamble Cost Control", &d2rHUDConfig.GambleCostControl, "Gamble Cost Control", "- Allows the gamble cost column to be used by AMW.txt\n- If a value of -1 is specified, it will use retail cost logic\n- If Disabled, only ring and amulet costs may be changed\n\n", "The maximum gamble cost will be determined by your in-game gold limits (defined in Memory Edits)", GetLockInfo(modName, &ModOverrideSettings::GambleCostControl));
        drawCheckbox("Combat Log", &d2rHUDConfig.CombatLog, "Combat Log", "- Displays real-time combat logs in system chat\n    - RNG rolls for Hit, Block and Dodge results\n    - Elemental Type, +DMG%, Length and Final Damage\n    - Remaining Monster Health\n- Uses data retrieved directly from the game for accuracy\n\n", "This feature is also utilized by the 'attackinfo 1' cheat\nExpect formatting and usage to adapt over time", GetLockInfo(modName, &ModOverrideSettings::CombatLog));
        drawCheckbox("Transmog Visuals", &d2rHUDConfig.TransmogVisuals, "Transmog Visuals", "- Allows you to transform visuals of applicable items\n(Make your Hand Axe look like a Short Sword, etc)\n- Requires mod to provide items/sets/uniques.json\n- Mod Author decides all transmog possibilities\n- Applies both in-game and at the main menu\n\n", "Currently only basic status text is added to the item\nExpect it to include the target item name in the future", GetLockInfo(modName, &ModOverrideSettings::TransmogVisuals));
        drawCheckbox("Extended Itemcodes", &d2rHUDConfig.ExtendedItemcodes, "Extended Itemcodes", "- Allows you to use 4-length item codes\n(Such as ic69, twrk, 1337, etc)\n- Without this, 4-length item codes will have no visuals\n- Visuals are applied to both inventory and world views\n\n", "Currently requires the Transmog Visuals option to be enabled", GetLockInfo(modName, &ModOverrideSettings::ExtendedItemcodes));
        drawCheckbox("Floating Damage (Beta)", &d2rHUDConfig.FloatingDamage, "Floating Damage (Beta)", "- Displays floating damage values in-game\n(Currently in beta, quirks are almost guaranteed)\n- Uses data retrieved directly from game for accuracy\n- Value displayed is after any +element dmg%\n\n", "Expect more options and aesthetic improvements over time", GetLockInfo(modName, &ModOverrideSettings::FloatingDamage));
        drawCheckbox("HP Rollover Mods", &d2rHUDConfig.HPRolloverMods, "HP Rollover Mods", "- Prevents HP rollovers on high player counts by:\n- Capping the maximum health bonus % applied to monsters\n- Applying damage reduction logic to the Player\n- Scales Reduction % by the calculated rollover amount\n\n", "This feature cannot guarantee no rollovers\nHowever, it should work for mods with retail-ish values", GetLockInfo(modName, &ModOverrideSettings::HPRolloverMods));

        // --- HPRolloverDifficulty ---
        const char* difficultyItems[] = { "Normal or Higher", "Nightmare or Higher", "Hell Only" };
        int difficultyIndex = d2rHUDConfig.HPRolloverDifficulty + 1;

        // label hover detection:
        drawLabeledInput(
            "HP Rollover Difficulty",
            [&]() {
                ImGui::PushItemWidth(250);
                if (ImGui::Combo("##HPRolloverDifficulty", &difficultyIndex,
                    difficultyItems, IM_ARRAYSIZE(difficultyItems)))
                {
                    d2rHUDConfig.HPRolloverDifficulty = difficultyIndex - 1;
                }
                ImGui::PopItemWidth();
            },
            "HP Rollover Difficulty", "- Control the applied Difficulty of HP Rollover Mods\n\n", "Only valid if HP Rollover Mods are enabled", false, 35.0f, GetLockInfo(modName, &ModOverrideSettings::HPRolloverDifficulty)
        );

        ImGui::Dummy(ImVec2(0.0f, 3.0f));

        drawLabeledInput(
            "HP Rollover %",
            [&]() {
                ImGui::PushItemWidth(100);
                if (ImGui::InputInt("##HPRolloverPercent", &d2rHUDConfig.HPRolloverPercent, 1, 10))
                {
                    d2rHUDConfig.HPRolloverPercent =
                        std::clamp(d2rHUDConfig.HPRolloverPercent, 0, 100);
                }
                ImGui::PopItemWidth();
            },
            "HP Rollover %", "- Controls the maximum amount of Damage reduction\n\n", "Only valid if HP Rollover Mods are enabled", true, 35.0f, GetLockInfo(modName, &ModOverrideSettings::HPRolloverPercent)
        );

        drawLabeledInput(
            "Sunder Value",
            [&]() {
                ImGui::PushItemWidth(100);
                if (ImGui::InputInt("##SunderValue", &d2rHUDConfig.SunderValue, 1, 10))
                {
                    d2rHUDConfig.SunderValue =
                        std::clamp(d2rHUDConfig.SunderValue, 0, 100);
                }
                ImGui::PopItemWidth();
            },
            "Sunder Value", "- Controls the Sundered monster value\n(When specified value is reached, reduction stops)\n- Only applies when monster is above 100 resistance\n- Applies edit at the time of monster spawn\n- States like Conviction will apply at full effect\n(Instead of by 1/5 if the monster is immune)\n- For TCPIP, the highest sunder value for each element\nFound among all players will be applied\n\n", "Requires compatible sunder-edited mod files to use\nMore info available at D2RModding Discord", true, 42.0f, GetLockInfo(modName, &ModOverrideSettings::SunderValue)
        );

        // --- END LEFT COLUMN ---
        ImGui::NextColumn();

        // --- DESCRIPTION PANEL (RIGHT COLUMN) ---
        float colX = ImGui::GetColumnOffset(1);
        float colW = ImGui::GetColumnWidth(1);
        fontIndex = 3;
        fontSize = (fontIndex >= 0 && fontIndex < io.Fonts->Fonts.Size) ? io.Fonts->Fonts[fontIndex] : nullptr;
        if (fontSize) ImGui::PushFont(fontSize);

        // get column-local cursor X and width
        float colLocalX = ImGui::GetCursorPosX();
        float colWidthLocal = ImGui::GetColumnWidth(1);

        // measure title and center it (use local coords)
        float titleW = ImGui::CalcTextSize(descriptionTitle.c_str()).x;
        ImGui::SetCursorPosX((colLocalX + (colWidthLocal - titleW) * 0.5f) - 10.0f);
        ImGui::TextColored(ImVec4(0.0157f, 0.380f, 0.8f, 1.0f), "%s", descriptionTitle.c_str());
        if (fontSize) ImGui::PopFont();

        // --- Description (right column, fixed width 570px) ---
        if (!descriptionText.empty())
        {
            const float colWidth = 550.0f;
            const float paddingRight = 10.0f;
            const float wrapPos = ImGui::GetCursorPosX() + colWidth - paddingRight;
            ImGui::PushTextWrapPos(wrapPos);

            // Center the wrapped main description text
            ImVec2 textSize = ImGui::CalcTextSize(descriptionText.c_str(), nullptr, false, colWidth);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (colWidth - textSize.x) * 0.5f);
            ImGui::TextWrapped("%s", descriptionText.c_str());

            // Center the wrapped description note in orangish-yellow
            ImVec2 noteSize = ImGui::CalcTextSize(descriptionNote.c_str(), nullptr, false, colWidth);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (colWidth - noteSize.x) * 0.5f);
            ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.0f, 1.0f), "%s", descriptionNote.c_str());

            ImGui::PopTextWrapPos();
        }

        ImGui::Columns(1);
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0.0f, 3.0f));

        // --- Centered buttons on the same line ---
        float windowWidth = ImGui::GetWindowSize().x;
        float buttonWidth = 150.0f;
        float spacing = 20.0f;
        float totalWidth = buttonWidth * 2 + spacing;
        ImGui::SetCursorPosX((windowWidth - totalWidth) * 0.5f);

        // --- Test Settings Button ---
        ImGui::PushID("TestSettings");
        if (ImGui::Button("Test Settings", ImVec2(buttonWidth, 0)))
        {
            // Update cachedSettings with the latest values
            cachedSettings.HPRollover = d2rHUDConfig.HPRolloverMods;
            cachedSettings.SunderValue = d2rHUDConfig.SunderValue;
            cachedSettings.monsterStatsDisplay = d2rHUDConfig.MonsterStatsDisplay;
            cachedSettings.HPRolloverAmt = d2rHUDConfig.HPRolloverPercent;
            cachedSettings.sunderedMonUMods = d2rHUDConfig.SunderedMonUMods;
            cachedSettings.minionEquality = d2rHUDConfig.MinionEquality;
            cachedSettings.gambleForce = d2rHUDConfig.GambleCostControl;
            cachedSettings.CombatLog = d2rHUDConfig.CombatLog;
            cachedSettings.TransmogVisuals = d2rHUDConfig.TransmogVisuals;
            cachedSettings.ExtendedItemcodes = d2rHUDConfig.ExtendedItemcodes;
            cachedSettings.FloatingDamage = d2rHUDConfig.FloatingDamage;

            saveStatusMessage = "New Settings Applied!";
            saveStatusColor = ImVec4(0.0f, 1.0f, 0.0f, 1.0f); // green
        }

        if (ImGui::IsItemHovered())
        {
            descriptionTitle = "Test Settings";
            descriptionText = "- Applies your current settings to the active game session\n- These settings will not persist for future game launches\n\n";
            descriptionNote = "This applies only to the options in this panel";
        }
        ImGui::PopID();
        ImGui::SameLine(0.0f, spacing);
        ImGui::PushID("SaveConfig");

        // Make the button green
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.3f, 0.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.0f, 0.5f, 0.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.1f, 0.6f, 0.1f, 1.0f));

        if (ImGui::Button("Save Config", ImVec2(buttonWidth, 0)))
        {
            namespace fs = std::filesystem;
            std::string modName = GetModName();
            fs::path relativePath = configFilePath;
            fs::path configPath = fs::absolute(relativePath);

            if (fs::exists(configPath)) {
                bool result = SaveFullGrailConfig(configFilePath, false);

                if (result)
                {
                    saveStatusMessage = "Config saved successfully!";
                    saveStatusColor = ImVec4(0.0f, 1.0f, 0.0f, 1.0f); // green
                }
                else
                {
                    saveStatusMessage = "Failed to save config!";
                    saveStatusColor = ImVec4(0.8f, 0.3f, 0.0f, 1.0f); // dark orange/red
                }
            }
            else {
                bool result = SaveFullGrailConfig(configFilePath, false);

                if (result)
                {
                    saveStatusMessage = "Config saved successfully!";
                    saveStatusColor = ImVec4(0.0f, 1.0f, 0.0f, 1.0f); // green
                }
                else
                {
                    saveStatusMessage = "Failed to save config!";
                    saveStatusColor = ImVec4(0.8f, 0.3f, 0.0f, 1.0f); // dark orange/red
                }
            }

            // Update cached settings as before...
            cachedSettings.HPRollover = d2rHUDConfig.HPRolloverMods;
            cachedSettings.SunderValue = d2rHUDConfig.SunderValue;
            cachedSettings.monsterStatsDisplay = d2rHUDConfig.MonsterStatsDisplay;
            cachedSettings.HPRolloverAmt = d2rHUDConfig.HPRolloverPercent;
            cachedSettings.sunderedMonUMods = d2rHUDConfig.SunderedMonUMods;
            cachedSettings.minionEquality = d2rHUDConfig.MinionEquality;
            cachedSettings.gambleForce = d2rHUDConfig.GambleCostControl;
            cachedSettings.CombatLog = d2rHUDConfig.CombatLog;
            cachedSettings.TransmogVisuals = d2rHUDConfig.TransmogVisuals;
            cachedSettings.ExtendedItemcodes = d2rHUDConfig.ExtendedItemcodes;
            cachedSettings.FloatingDamage = d2rHUDConfig.FloatingDamage;

            // Reload HUD config
            d2rHUDConfig.HPRolloverPercent = cachedSettings.HPRolloverAmt;
            d2rHUDConfig.SunderValue = cachedSettings.SunderValue;
            d2rHUDConfig.MonsterStatsDisplay = cachedSettings.monsterStatsDisplay;
            d2rHUDConfig.HPRolloverMods = cachedSettings.HPRollover;
            d2rHUDConfig.SunderedMonUMods = cachedSettings.sunderedMonUMods;
            d2rHUDConfig.MinionEquality = cachedSettings.minionEquality;
            d2rHUDConfig.GambleCostControl = cachedSettings.gambleForce;
            d2rHUDConfig.CombatLog = cachedSettings.CombatLog;
            d2rHUDConfig.TransmogVisuals = cachedSettings.TransmogVisuals;
            d2rHUDConfig.ExtendedItemcodes = cachedSettings.ExtendedItemcodes;
            d2rHUDConfig.FloatingDamage = cachedSettings.FloatingDamage;
        }
        // Assign hover description
        if (ImGui::IsItemHovered())
        {
            descriptionTitle = "Save Config";
            descriptionText = "- Applies your current settings and updates the config file\n- This ensures your settings are kept for future launches\n\n";
            descriptionNote = "This applies only to the options in this panel";
        }

        ImGui::PopStyleColor(3);
        ImGui::PopID();

        // --- Status message below buttons ---
        if (!saveStatusMessage.empty())
        {
            ImGui::Spacing();
            ImGui::SetCursorPosX((windowWidth - ImGui::CalcTextSize(saveStatusMessage.c_str()).x) * 0.5f);
            ImGui::TextColored(saveStatusColor, "%s", saveStatusMessage.c_str());
        }

        ImGui::End();
    }
}

void ShowHUDSettingsMenu()
{
    if (!showHUDSettingsMenu)
        return;


    EnableAllInput();
    CenterWindow(ImVec2(800, 400));
    static std::string hoveredKey;
    hoveredKey.clear();
    PushFontSafe(3);
    ImGui::Begin("HUD Settings", &showHUDSettingsMenu, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar);
    DrawWindowTitleAndClose("HUD Settings", &showHUDSettingsMenu);
    PopFontSafe(3);
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.0f, 5.0f));

    // --- Centered Wrapped Text Helper ---
    auto CenteredWrappedText = [&](const std::string& prefix, const std::string& text,
        const ImVec4& prefixColor = ImVec4(1, 0.7f, 0.3f, 1.0f),
        const ImVec4& valueColor = ImVec4(1, 1, 1, 1))
        {
            ImVec2 avail = ImGui::GetContentRegionAvail();
            float leftOffset = 10.0f;
            float wrapWidth = avail.x - leftOffset;

            ImVec2 prefixSize = ImGui::CalcTextSize(prefix.c_str(), nullptr, false, wrapWidth);
            ImVec2 valueSize = ImGui::CalcTextSize(text.c_str(), nullptr, false, wrapWidth);
            float totalWidth = prefixSize.x + valueSize.x;
            float cursorX = leftOffset + (wrapWidth - totalWidth) * 0.5f;
            if (cursorX < leftOffset) cursorX = leftOffset;

            ImGui::SetCursorPosX(cursorX);
            ImGui::TextColored(prefixColor, "%s", prefix.c_str());
            ImGui::SameLine(0, 0);
            ImGui::TextColored(valueColor, "%s", text.c_str());
        };

    if (!g_LootFilterHeader.Version.empty())
        CenteredWrappedText("My D2RLoot Version: ", g_LootFilterHeader.Version);
    CenteredWrappedText("My Selected Filter: ", g_LootFilterHeader.Title);

    ImGui::Dummy(ImVec2(0.0f, 3.0f));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.0f, 5.0f));

    // --- Boolean Checkboxes ---
    auto RenderCheckboxLine = [&](const std::vector<std::pair<std::string, std::string>>& items)
        {
            ImVec2 avail = ImGui::GetContentRegionAvail();
            float spacing = 10.0f;
            float totalWidth = 0.0f;

            for (auto& item : items)
            {
                totalWidth += ImGui::CalcTextSize(item.first.c_str()).x + ImGui::GetStyle().FramePadding.x * 2 + ImGui::GetFrameHeight();
            }
            totalWidth += spacing * (items.size() - 1);

            float startX = (avail.x - totalWidth) * 0.5f;
            if (startX < 0.0f) startX = 0.0f;
            ImGui::SetCursorPosX(startX);

            for (size_t i = 0; i < items.size(); ++i)
            {
                if (i > 0) ImGui::SameLine(0.0f, spacing);

                std::string key = items[i].second;
                std::string val = "";
                auto it = g_LuaVariables.find(key);
                if (it != g_LuaVariables.end()) val = it->second;

                bool boolValue = (val == "true");

                if (ImGui::Checkbox(items[i].first.c_str(), &boolValue))
                {
                    auto it2 = g_LuaVariables.find(key);
                    if (it2 != g_LuaVariables.end()) it2->second = boolValue ? "true" : "false";
                    else g_LuaVariables.insert({ key, boolValue ? "true" : "false" });
                }

                ImVec2 itemMin = ImGui::GetItemRectMin();
                ImVec2 itemMax = ImGui::GetItemRectMax();
                float textWidth = ImGui::CalcTextSize(items[i].first.c_str()).x;
                itemMax.x += textWidth;
                if (ImGui::IsMouseHoveringRect(itemMin, itemMax)) hoveredKey = key;
            }

            ImGui::Dummy(ImVec2(0.0f, 3.0f));
            ImGui::Separator();
            ImGui::Dummy(ImVec2(0.0f, 3.0f));
        };

    std::vector<std::pair<std::string, std::string>> bools = {
        { "Allow Overrides", "allowOverrides" },
        { "Mod Tips", "modTips" },
        { "Debug Mode", "Debug" },
        { "Audio Playback", "audioPlayback" }
    };
    RenderCheckboxLine(bools);

    // --- Input Text Helper ---
    auto RenderInputText = [&](const std::string& key, const std::string& label, const std::string& defaultVal = "Not Defined")
        {
            std::string value = defaultVal;
            auto it = g_LuaVariables.find(key);
            if (it != g_LuaVariables.end()) value = it->second;
            if (!value.empty() && value.front() == '"' && value.back() == '"') value = value.substr(1, value.size() - 2);

            std::string fullLabel = label + " = ";
            float labelWidth = ImGui::CalcTextSize(fullLabel.c_str()).x;
            float valueWidth = ImGui::CalcTextSize(value.c_str()).x + 8.0f;

            ImVec2 cursorPos = ImGui::GetCursorPos();
            ImGui::SetCursorPosY(cursorPos.y - 2.0f);

            ImGui::Text("%s", fullLabel.c_str());
            ImGui::SameLine(labelWidth + 10.0f, -3.0f);

            char buffer[256];
            strncpy(buffer, value.c_str(), sizeof(buffer));
            buffer[sizeof(buffer) - 1] = '\0';

            std::string inputID = "##val_" + key;
            ImGui::PushItemWidth(valueWidth);
            bool changed = ImGui::InputText(inputID.c_str(), buffer, sizeof(buffer));
            if (ImGui::IsItemActivated()) ImGui::SetKeyboardFocusHere(-1);
            if (changed)
            {
                auto it2 = g_LuaVariables.find(key);
                if (it2 != g_LuaVariables.end()) it2->second = buffer;
                else g_LuaVariables.insert({ key, buffer });
            }
            ImGui::PopItemWidth();

            ImVec2 itemMin = ImGui::GetItemRectMin();
            ImVec2 itemMax = ImGui::GetItemRectMax();
            itemMin.x -= labelWidth;
            if (ImGui::IsMouseHoveringRect(itemMin, itemMax)) hoveredKey = key;
        };

    RenderInputText("reload", "Reload Message");
    RenderInputText("audioVoice", "Audio Voice");
    RenderInputText("filter_level", "Filter Level");
    RenderInputText("language", "Language");

    // --- Filter Titles ---
    auto RenderFilterTitles = [&]()
        {
            std::string key = "filter_titles";
            std::string value = "";
            auto it = g_LuaVariables.find(key);
            if (it != g_LuaVariables.end()) value = it->second;

            std::vector<std::string> titles;
            if (!value.empty())
            {
                std::regex titleRegex(R"delim("([^"]*)")delim");
                for (auto i = std::sregex_iterator(value.begin(), value.end(), titleRegex);
                    i != std::sregex_iterator(); ++i)
                    titles.push_back((*i)[1].str());
            }
            if (titles.empty()) titles.push_back("Not Defined");

            std::string ftLabel = "Filter Titles = ";
            float labelWidth = ImGui::CalcTextSize(ftLabel.c_str()).x;
            ImGui::Text("%s", ftLabel.c_str());
            ImGui::SameLine(labelWidth + 10.0f);

            for (size_t idx = 0; idx < titles.size(); ++idx)
            {
                char buffer[256];
                strncpy(buffer, titles[idx].c_str(), sizeof(buffer));
                buffer[sizeof(buffer) - 1] = '\0';

                float textWidth = ImGui::CalcTextSize(buffer).x;
                ImGui::PushItemWidth(textWidth + 8.0f);

                std::string inputID = "##filter_title_" + std::to_string(idx);
                bool changed = ImGui::InputText(inputID.c_str(), buffer, sizeof(buffer));
                ImGui::PopItemWidth();

                if (changed) titles[idx] = buffer;
                if (idx + 1 < titles.size()) { ImGui::SameLine(0, 2); ImGui::Text(", "); ImGui::SameLine(0, 0); }

                ImVec2 itemMin = ImGui::GetItemRectMin();
                ImVec2 itemMax = ImGui::GetItemRectMax();
                itemMin.x -= labelWidth;
                if (ImGui::IsMouseHoveringRect(itemMin, itemMax)) hoveredKey = key;
            }

            std::string newValue = "{ ";
            for (size_t i = 0; i < titles.size(); ++i)
            {
                newValue += "\"" + titles[i] + "\"";
                if (i + 1 < titles.size()) newValue += ", ";
            }
            newValue += " }";

            auto it2 = g_LuaVariables.find(key);
            if (it2 != g_LuaVariables.end()) it2->second = newValue;
            else g_LuaVariables.insert({ key, newValue });
        };
    RenderFilterTitles();

    // --- Bottom Description ---
    std::string desc = "Hover over an option to see its description.";
    if (!hoveredKey.empty() && g_LuaDescriptions.count(hoveredKey))
        desc = g_LuaDescriptions.at(hoveredKey);

    DrawBottomDescription(desc);

    ImGui::End();
}

void ShowMainMenu()
{
    if (!showMainMenu)
        return;

    ImGuiIO& io = ImGui::GetIO();
    ImVec2 windowSize = ImVec2(640, 400);

    // Center the window only on the first run
    static bool firstRun = true;
    if (firstRun)
    {
        ImVec2 centerPos = ImVec2((io.DisplaySize.x - windowSize.x) * 0.5f, (io.DisplaySize.y - windowSize.y) * 0.5f);
        ImGui::SetNextWindowPos(centerPos, ImGuiCond_Once);
        firstRun = false;
    }

    ImGui::SetNextWindowSize(windowSize, ImGuiCond_Always);
    ImGui::Begin("D2RHUD Control Center", &showMainMenu, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar);

    // --- HEADER ---
    int fontIndex = 3;
    ImFont* fontSize = (fontIndex >= 0 && fontIndex < io.Fonts->Fonts.Size) ? io.Fonts->Fonts[fontIndex] : nullptr;
    if (fontSize) ImGui::PushFont(fontSize);

    /*
    // ---- Hud Settings Button ----
    {
        const char* gearIcon = reinterpret_cast<const char*>(u8"\u216C"); // or u8"⚙🔧"
        float btnSize = 20.0f;
        float padding = 35.0f;

        ImVec2 startPos = ImGui::GetCursorScreenPos();  // top-left of window content

        // Position gear on far left
        ImGui::SetCursorPosX(padding);

        // Button hitbox
        ImGui::InvisibleButton("SettingsBtn", ImVec2(btnSize, btnSize));
        bool gearClicked = ImGui::IsItemClicked();

        // Draw the icon centered in the hitbox
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 gearPos = ImGui::GetItemRectMin();
        ImVec2 gearSize = ImGui::CalcTextSize(gearIcon);

        dl->AddText(ImVec2(
            gearPos.x + (btnSize - gearSize.x) * 0.5f,
            gearPos.y + (btnSize - gearSize.y) * 0.5f
        ), IM_COL32(230, 230, 230, 255), gearIcon);

        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Open D2RHUDCC Settings");

        if (gearClicked)
        {
            showHUDSettingsMenu = true;
            
        }
        ShowHUDSettingsMenu();
    }
    */

    // --- TITLE AND CLOSE BUTTON ---
    const char* windowTitle = "D2RHUD Control Center";
    float closeBtnSize = 20.0f;
    float padding = 5.0f;
    ImVec2 contentSize = ImGui::GetContentRegionAvail();

    float titleWidth = ImGui::CalcTextSize(windowTitle).x;
    ImGui::SetCursorPosX((contentSize.x - titleWidth) * 0.5f);
    ImGui::TextColored(ImVec4(0.95f, 0.85f, 0.5f, 1.0f), "%s", windowTitle);

    ImGui::SameLine(contentSize.x - closeBtnSize - padding);
    ImVec2 btnPos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("CloseBtn", ImVec2(closeBtnSize, closeBtnSize));
    if (ImGui::IsItemClicked()) showMainMenu = false;

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 textSize = ImGui::CalcTextSize("X");
    ImVec2 textPos = ImVec2(btnPos.x + (closeBtnSize - textSize.x) * 0.5f,
        btnPos.y + (closeBtnSize - textSize.y) * 0.5f);
    drawList->AddText(textPos, IM_COL32(255, 80, 80, 255), "X");
    if (fontSize) ImGui::PopFont();

    fontIndex = 1;
    fontSize = (fontIndex >= 0 && fontIndex < io.Fonts->Fonts.Size) ? io.Fonts->Fonts[fontIndex] : nullptr;
    if (fontSize) ImGui::PushFont(fontSize);
    ImGuiTextCentered("Quickly view or command custom options offered by D2RHUD");
    ImGuiTextCentered("This menu can be toggled using the hotkey set in D2RLAN");
    if (fontSize) ImGui::PopFont();

    fontIndex = 2;
    fontSize = (fontIndex >= 0 && fontIndex < io.Fonts->Fonts.Size) ? io.Fonts->Fonts[fontIndex] : nullptr;
    if (fontSize) ImGui::PushFont(fontSize);

    // Prepare version line
    std::string label = "Version:";
    std::string number = Version;
    ImVec2 labelSize = ImGui::CalcTextSize(label.c_str());
    ImVec2 numberSize = ImGui::CalcTextSize(number.c_str());
    float totalWidth = labelSize.x + numberSize.x;
    float contentWidth = ImGui::GetWindowContentRegionMax().x - ImGui::GetWindowContentRegionMin().x;
    ImGui::SetCursorPosX((contentWidth - totalWidth) * 0.5f);

    // Draw the two colored segments
    ImGui::TextColored(ImVec4(0.95f, 0.85f, 0.5f, 1.0f), "%s", label.c_str());
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.0157f, 0.380f, 0.8f, 1.0f), "%s", number.c_str());

    if (fontSize) ImGui::PopFont();
    ImGui::Separator();

    // --- MENU BUTTONS AND DESCRIPTION AREA ---
    float buttonWidth = 200.0f;
    float buttonHeight = 50.0f;
    float separatorX = buttonWidth + 20.0f;
    float descriptionWidth = windowSize.x - separatorX - 15.0f;
    const char* descriptionTitle = "";
    const char* descriptionText = "";
    const char* descriptionNote = "";

    fontIndex = 2;
    fontSize = (fontIndex >= 0 && fontIndex < io.Fonts->Fonts.Size) ? io.Fonts->Fonts[fontIndex] : nullptr;
    if (fontSize) ImGui::PushFont(fontSize);
    
    if (ImGui::Button("D2RHUD Options", ImVec2(buttonWidth, buttonHeight)))
        showD2RHUDMenu = true;
    ShowD2RHUDMenu();
    if (ImGui::IsItemHovered()) { descriptionTitle = "D2RHUD Options"; descriptionText = "Explore and Control enabled D2RHUD Options\n\n- Values are retrieved and stored in D2RLAN/Launcher/config.json\n- Overrides can be applied by the author in data/D2RLAN/config_override.json\n- Expect implementation changes over the next updates"; }
    
    if (ImGui::Button("Memory Edit Info", ImVec2(buttonWidth, buttonHeight)))
        showMemoryMenu = true;
    ShowMemoryMenu();
    if (ImGui::IsItemHovered()) { descriptionTitle = "Memory Edit Info"; descriptionText = "View your currently active memory edits\n\n- Values are retrieved and stored in D2RLAN/Launcher/config.json\n- These edits provide additional 'hardcode only' options to the game\n- For some entries, game restart will be needed for them to apply\n- This panel is currently read-only during early development"; }
    
    if (ImGui::Button("D2RLoot Settings", ImVec2(buttonWidth, buttonHeight)))
        showLootMenu = true;
    ShowLootMenu();
    if (ImGui::IsItemHovered()) { descriptionTitle = "D2RLoot Settings"; descriptionText = "Explore and control your currently active loot filter\n\n- Filters operate in real-time with user-defined rules\n- Accessible in D2RLAN > Options > Loot Filter\n- Rules are defined in D2RLAN/D2R/lootfilter_config.lua"; }
    
    if (ImGui::Button("Hotkey Controls", ImVec2(buttonWidth, buttonHeight)))
        showHotkeyMenu = true;
    ShowHotkeyMenu();
    if (ImGui::IsItemHovered()) { descriptionTitle = "Hotkey Controls"; descriptionText = "Manage your hotkeys used by various tools\n\n- Hotkeys are achieved by utilizing internal game functions\n- They can also be used to dynamically control your loot filter\n- Hotkeys are defined in D2RLAN/Launcher/D2RLAN_Config.txt"; }

    if (ImGui::Button("Grail Tracker", ImVec2(buttonWidth, buttonHeight)))
        showGrailMenu = true;
    ShowGrailMenu();
    if (ImGui::IsItemHovered()) { descriptionTitle = "Grail Tracker"; descriptionText = "View the progress of your Set/Unique item hunting\n\n- Grail Entries are manually stored for now\n- This feature works for all mods* (or TCP)\n(Mod must have included set/unique items.txt files)\n- Grail Progress/Settings are stored in D2RLAN/D2R/HUD_Settings_ModName.json"; }
    if (fontSize) ImGui::PopFont();

    // Vertical separator + Description Panel
    ImGui::GetWindowDrawList()->AddLine(ImVec2(ImGui::GetWindowPos().x + separatorX, ImGui::GetWindowPos().y + 100.0f), ImVec2(ImGui::GetWindowPos().x + separatorX, ImGui::GetWindowPos().y + windowSize.y - 3.0f), IM_COL32(180, 150, 80, 255), 2.0f);
    ImGui::SetCursorPosX(separatorX - 200.0f);
    ImGui::SetCursorPosY(100.0f);

    // Title style
    fontIndex = 3;
    fontSize = (fontIndex >= 0 && fontIndex < io.Fonts->Fonts.Size) ? io.Fonts->Fonts[fontIndex] : nullptr;
    if (fontSize) ImGui::PushFont(fontSize);
    if (descriptionTitle && descriptionTitle[0] != '\0')
    {
        ImVec2 textSize = ImGui::CalcTextSize(descriptionTitle);
        ImGui::SetCursorPosX(separatorX + 5.0f + (descriptionWidth - textSize.x) * 0.5f);
        ImGui::TextColored(ImVec4(0.0157f, 0.380f, 0.8f, 1.0f), descriptionTitle);
    }
    if (fontSize) ImGui::PopFont();

    // Description style
    fontIndex = 1;
    fontSize = (fontIndex >= 0 && fontIndex < io.Fonts->Fonts.Size) ? io.Fonts->Fonts[fontIndex] : nullptr;
    if (fontSize) ImGui::PushFont(fontSize);
    if (descriptionText && descriptionText[0] != '\0')
    {
        ImVec2 textSize = ImGui::CalcTextSize(descriptionText, nullptr, false, descriptionWidth);
        ImGui::SetCursorPosX(separatorX + 5.0f + (descriptionWidth - textSize.x) * 0.5f);
        ImGui::TextWrapped("%s", descriptionText);
    }
    if (fontSize) ImGui::PopFont();

    ImGui::End();
}

#pragma endregion

#pragma endregion

#pragma region Game Hooks

void __fastcall Hooked_D2GAME_UMOD8Array_1402fc530(D2UnitStrc* pUnit, int32_t nUMod, int32_t bUnique)
{
    // Call the original function
    oD2GAME_UMOD8Array_1402fc530(pUnit, nUMod, bUnique);

    if (!pUnit)
        return;

    // Get stored remainders
    RemainderEntry remainders = GetRemainder(pUnit);

    auto& processedStats = g_unitsEditedStats[pUnit->dwUnitId];

    // Helper lambda to handle each stat
    auto ApplyFinalValue = [&](D2C_ItemStats statId, int remainder, const char* name)
        {
            // Skip if this stat was already processed for this unit
            if (processedStats.find(statId) != processedStats.end())
                return;

            int nCurrentValue = STATLIST_GetUnitStatSigned(pUnit, statId, 0);
            int finalValue = 0;
            int wastedValue = 0;

            if ((nCurrentValue >= settings.SunderValue + 1) && settings.sunderedMonUMods == true)
            {
                finalValue = nCurrentValue - remainder;
                //LogSpawnDebug("  Stat %s: nCurrentValue=%d, finalValue=%d, wastedValue=%d, remainder=%d", name, nCurrentValue, finalValue, wastedValue, remainder);

                if (finalValue < settings.SunderValue)
                {
                    wastedValue = settings.SunderValue - finalValue;
                    finalValue = settings.SunderValue;
                    //LogSpawnDebug("  Stat %s: nCurrentValue=%d, finalValue=%d, wastedValue=%d, remainder=%d", name, nCurrentValue, finalValue, wastedValue, remainder);
                }

                STATLISTEX_SetStatListExStat(pUnit->pStatListEx, statId, finalValue, 0);

                // Mark this stat as processed for this unit
                processedStats.insert(statId);
            }
        };

    // Apply for all six resistances
    ApplyFinalValue(STAT_COLDRESIST, remainders.cold, "Cold");
    ApplyFinalValue(STAT_FIRERESIST, remainders.fire, "Fire");
    ApplyFinalValue(STAT_LIGHTRESIST, remainders.light, "Light");
    ApplyFinalValue(STAT_POISONRESIST, remainders.poison, "Poison");
    ApplyFinalValue(STAT_DAMAGERESIST, remainders.damage, "Damage");
    ApplyFinalValue(STAT_MAGICRESIST, remainders.magic, "Magic");
}

void __fastcall HookedMONSTER_InitializeStatsAndSkills(D2GameStrc* pGame, D2ActiveRoomStrc* pRoom, D2UnitStrc* pUnit, int64_t* pMonRegData)
{
    oMONSTER_InitializeStatsAndSkills(pGame, pRoom, pUnit, pMonRegData);

    if (!pUnit || pUnit->dwUnitType != UNIT_MONSTER || !pUnit->pMonsterData || !pUnit->pMonsterData->pMonstatsTxt)
        return;

    int32_t nClassId = pUnit->dwClassId;
    auto pMonStatsTxtRecord = pUnit->pMonsterData->pMonstatsTxt;
    auto wMonStatsEx = sgptDataTables->pMonStatsTxt[nClassId].wMonStatsEx;

    if (wMonStatsEx >= sgptDataTables->nMonStats2TxtRecordCount)
        return;

    D2UnitStrc* pUnitPlayer = UNITS_GetServerUnitByTypeAndId(pGame, UNIT_PLAYER, 1);
    if (!pUnitPlayer)
        return;

    int difficulty = GetPlayerDifficulty(pUnitPlayer);
    if (difficulty < 0 || difficulty > 2)
        return;

    D2MonStatsInitStrc monStatsInit = {};

    // Build the file path ONCE (outside the lambda)
    if (gZonesFilePath.empty())
    {
        gZonesFilePath = GetExecutableDir();
        gZonesFilePath += "/Mods/";
        gZonesFilePath += modName;
        gZonesFilePath += "/";
        gZonesFilePath += modName;
        gZonesFilePath += ".mpq/data/hd/global/excel/desecratedzones.json";
    }

    std::call_once(gZonesLoadedFlag, []() {
        if (LoadDesecratedZones(gZonesFilePath))
            gZonesLoaded = true;
        });

    ApplyGhettoSunder(pGame, pRoom, pUnit, pMonRegData, &monStatsInit);

    if (!GetBaalQuest(pUnitPlayer, pGame))
        return;

    if (gZonesLoaded)
        ApplyGhettoTerrorZone(pGame, pRoom, pUnit, pMonRegData, &monStatsInit);

    ApplyMonsterDifficultyScalingNonTZ(pUnit, difficulty, playerLevel, playerCountGlobal, pGame);

    time_t currentUtc = std::time(nullptr);

    for (const auto& zone : gDesecratedZones)
    {
        const DifficultySettings* difficultySettings = nullptr;
        switch (difficulty)
        {
        case 0: difficultySettings = &zone.default_normal; break;
        case 1: difficultySettings = &zone.default_nightmare; break;
        case 2: difficultySettings = &zone.default_hell; break;
        }
        if (!difficultySettings)
            continue;

        //ApplyStatAdjustments(pGame, pRoom, pUnit, pMonRegData, &monStatsInit, *difficultySettings);
    }
}

uint32_t __fastcall Hooked_ITEMS_CalculateGambleCost(D2UnitStrc* pItem, int nPlayerLevel)
{
    if (!pItem || !pItem->pItemData || !sgptDataTables || !sgptDataTables->pItemsTxt)
        return oGambleForce(pItem, nPlayerLevel);

    if (settings.gambleForce || cachedSettings.gambleForce)
    {
        D2ItemsTxt* itemTxt = &sgptDataTables->pItemsTxt[pItem->dwClassId];

        if (itemTxt->dwGambleCost == -1)
            return oGambleForce(pItem, nPlayerLevel);
        else
            return itemTxt->dwGambleCost;
    }
    else
        return oGambleForce(pItem, nPlayerLevel);
}

int64_t Hooked_HUDWarnings__PopulateHUDWarnings(void* pWidget) {
    D2GameStrc* pGame = nullptr;
    D2Client* pGameClient = GetClientPtr();
    D2UnitStrc* pUnitPlayer = nullptr;

    if (pGameClient != nullptr) {
        pGame = (D2GameStrc*)pGameClient->pGame;
        pUnitPlayer = UNITS_GetServerUnitByTypeAndId(pGame, UNIT_PLAYER, 1);
    }

    auto result = oHUDWarnings__PopulateHUDWarnings(pWidget);

    void* tzInfoTextWidget = WidgetFindChild(pWidget, "TerrorZoneInfoText");
    void* tzStatAdjustmentsWidget = WidgetFindChild(pWidget, "TerrorZoneStatAdjustments");

    if (!tzInfoTextWidget && !tzStatAdjustmentsWidget) {
        return result;
    }

    if (!GetBaalQuest(pUnitPlayer, pGame)) {
        return result;
    }

    // TerrorZoneInfoText
    if (tzInfoTextWidget) {
        char** pOriginal = (char**)((int64_t)tzInfoTextWidget + 0x88);
        int64_t* nLength = (int64_t*)((int64_t)tzInfoTextWidget + 0x90);

        std::string finalText = BuildTerrorZoneInfoText();
        if (!finalText.empty()) {
            strncpy(gTZInfoText, finalText.c_str(), sizeof(gTZInfoText) - 1);
            gTZInfoText[sizeof(gTZInfoText) - 1] = '\0';

            *pOriginal = gTZInfoText;
            *nLength = strlen(gTZInfoText) + 1;
        }
    }

    // TerrorZoneStatAdjustments
    if (tzStatAdjustmentsWidget) {
        char** pOriginal = (char**)((int64_t)tzStatAdjustmentsWidget + 0x88);
        int64_t* nLength = (int64_t*)((int64_t)tzStatAdjustmentsWidget + 0x90);

        std::string finalText = BuildTerrorZoneStatAdjustmentsText();

        if (finalText.empty()) {
            gTZStatAdjText[0] = '\0';
            *pOriginal = gTZStatAdjText;
            *nLength = 0;
        }
        else {
            strncpy(gTZStatAdjText, finalText.c_str(), sizeof(gTZStatAdjText) - 1);
            gTZStatAdjText[sizeof(gTZStatAdjText) - 1] = '\0';
            *pOriginal = gTZStatAdjText;
            *nLength = strlen(gTZStatAdjText) + 1;
        }
    }

    return result;
}

void Hooked__Widget__OnClose(void* pWidget) {
    oWidget__OnClose(pWidget);
    char* pName = *(reinterpret_cast<char**>(reinterpret_cast<char*>(pWidget) + 0x8));
    if (strcmp(pName, "AutoMap") == 0) {
        gTZInfoText[0] = '\0';
        gTZStatAdjText[0] = '\0';
    }
}

void __fastcall HookedDropTCTest(D2GameStrc* pGame, D2UnitStrc* pMonster, D2UnitStrc* pPlayer, int32_t nTCId, int32_t nQuality, int32_t nItemLevel, int32_t a7, D2UnitStrc** ppItems, int32_t* pnItemsDropped, int32_t nMaxItems)
{
    if (isTerrorized == false)
    {
        oDropTCTest(pGame, pMonster, pPlayer, nTCId, nQuality, nItemLevel, a7, ppItems, pnItemsDropped, nMaxItems);
        return;
    }
    else
        ForceTCDrops(pGame, pMonster, pPlayer, nTCId, nQuality, nItemLevel, a7, ppItems, pnItemsDropped, nMaxItems);
}

#pragma region Floating Damage

struct FloatingDamage
{
    std::string text;
    float offsetX;
    float offsetY;
    float displayIndex;
    std::chrono::steady_clock::time_point spawnTime;
    float invLifetime;
    ImVec2 textSize;
};

std::vector<FloatingDamage> g_FloatingDamageList;
typedef void(__fastcall* DamageInfo_t)(void* param1, D2UnitStrc* attacker, D2UnitStrc* target, int damage, int param5, void* param6, int param7, void* param8, char param9, void* param10);
DamageInfo_t oDamageInfo = reinterpret_cast<DamageInfo_t>(Pattern::Address(0x27ab90));

void __fastcall Hooked_DamageInfo(void* param1, D2UnitStrc* attacker, D2UnitStrc* target, int baseDamage, int param5, void* param6, int finalDamage, void* param8, char param9, void* param10)
{
    int actualDamage = finalDamage >> 8;
    oDamageInfo(param1, attacker, target, baseDamage, param5, param6, finalDamage, param8, param9, param10);

    if (!attacker || !attacker->pDynamicPath || !target || !target->pDynamicPath)
        return;

    D2DynamicPathStrc* a = attacker->pDynamicPath;
    D2DynamicPathStrc* t = target->pDynamicPath;

    // Subtile Positions
    constexpr float INV_SUBTILE = 1.0f / 65536.0f;

    float ax = a->wPosX + a->wOffsetX * INV_SUBTILE;
    float ay = a->wPosY + a->wOffsetY * INV_SUBTILE;
    float tx = t->wPosX + t->wOffsetX * INV_SUBTILE;
    float ty = t->wPosY + t->wOffsetY * INV_SUBTILE;
    float dx = tx - ax;
    float dy = ty - ay;

    // Isometric Projection Offsets
    float isoX = dx - dy;
    float isoY = dx + dy;
    float pixelOffsetX = 40.0f + isoX * 30.0f;
    float pixelOffsetY = 120.0f + isoY * -12.5f;

    // Value Properties
    constexpr float DAMAGE_LIFETIME = 2.0f;

    FloatingDamage dmg;
    dmg.text = std::to_string(actualDamage);
    dmg.offsetX = pixelOffsetX;
    dmg.offsetY = pixelOffsetY;
    dmg.spawnTime = std::chrono::steady_clock::now();
    dmg.invLifetime = 1.0f / DAMAGE_LIFETIME;
    dmg.textSize = ImGui::CalcTextSize(dmg.text.c_str());
    dmg.displayIndex = (float)g_FloatingDamageList.size();
    g_FloatingDamageList.push_back(dmg);

    /*
    // Debug
    std::cout << std::fixed << std::setprecision(3)
        << "[DamageHook] Dmg:" << actualDamage
        << " Δ(" << dx << "," << dy << ")"
        << " ISO(" << isoX << "," << isoY << ")"
        << " Px(" << pixelOffsetX << "," << pixelOffsetY << ")\n";
    */
}

#pragma endregion


#pragma endregion

#pragma region Draw Loop for Detours and Stats Display
void D2RHUD::OnDraw() {
    D2GameStrc* pGame = nullptr;
    D2Client* pGameClient = GetClientPtr();

    if (pGameClient != nullptr)
        pGame = (D2GameStrc*)pGameClient->pGame;
    
    if (!configLoaded)
    {
        LoadCommandsAndKeybinds("HUDConfig_" + modName + ".json");
        LoadD2RHUDConfig(configFilePath);
        RegisterModOverrides();
        ApplyModOverrides(modName);
        
        configLoaded = true;
    }
    
        

    if (!menuClickHookInstalled)
    {
        mainMenuClickHandlerOrig = reinterpret_cast<GameMenuOnClickHandler>(Pattern::Address(mainMenuClickHandlerOffset));
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourAttach(&(PVOID&)mainMenuClickHandlerOrig, GameMenuOnClickHandlerHook);
        DetourTransactionCommit();
        CCMD_DEBUGCHEAT_Handler_Orig = reinterpret_cast<CCMD_HANDLER_Fptr>(Pattern::Address(CCMD_DEBUGCHEAT_HandlerOffset));
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourAttach(&(PVOID&)CCMD_DEBUGCHEAT_Handler_Orig, CCMD_DEBUGCHEAT_Hook);
        DetourTransactionCommit();
        Process_SCMD_CHATSTART_Orig = reinterpret_cast<Process_SCMD_CHATSTART_Fptr>(Pattern::Address(Process_SCMD_CHATSTARTOffset));
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourAttach(&(PVOID&)Process_SCMD_CHATSTART_Orig, Process_SCMD_CHATSTART_Hook);
        DetourTransactionCommit();
        menuClickHookInstalled = true;

        g_ItemFilterStatusMessage = std::format("D2RHUD {} Loaded Successfully!", Version);
        g_ShouldShowItemFilterMessage = true;
        g_ItemFilterMessageStartTime = std::chrono::steady_clock::now();
    }

    if (g_ShouldShowItemFilterMessage)
    {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - g_ItemFilterMessageStartTime);
        int timeoutSeconds = 3; //default time
        ImU32 color = IM_COL32(199, 179, 119, 255); //default gold
        int fontIndex = 4; //default font size

        if (g_ItemFilterStatusMessage.find("D2RHUD") != std::string::npos)
        {
            color = IM_COL32(3, 110, 32, 255); // green
            timeoutSeconds = 5;
            fontIndex = 3;
        }

        if (elapsed.count() < timeoutSeconds)
        {
            ImGuiIO& io = ImGui::GetIO();
            ImFont* chosenFont = (fontIndex >= 0 && fontIndex < io.Fonts->Fonts.Size) ? io.Fonts->Fonts[fontIndex] : nullptr;

                if (chosenFont)
                    ImGui::PushFont(chosenFont);

                auto drawList = ImGui::GetBackgroundDrawList();
                ImVec2 screenSize = io.DisplaySize;
                ImVec2 textSize = ImGui::CalcTextSize(g_ItemFilterStatusMessage.c_str());
                ImVec2 textPos = ImVec2((screenSize.x - textSize.x) * 0.5f,
                    (screenSize.y - textSize.y) * 0.1f);

                drawList->AddText(textPos, color, g_ItemFilterStatusMessage.c_str());

                if (chosenFont)
                    ImGui::PopFont();
        }
        else
            g_ShouldShowItemFilterMessage = false;
    }

    if (!oBankPanelDraw) {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        oBankPanelDraw = reinterpret_cast<BankPanelDraw_t>(Pattern::Address(bankPanelDraw));
        DetourAttach(&(PVOID&)oBankPanelDraw, HookedBankPanelDraw);
        DetourTransactionCommit();
        UpdateStashFileName(gSelectedPage);
        DWORD oldProtect = 0;
        VirtualProtect(&gpCCMDHandlerTable[CCMD_CUSTOM_OP_CODE], sizeof(D2CCMDStrc), PAGE_EXECUTE_READWRITE, &oldProtect);
        gpCCMDHandlerTable[CCMD_CUSTOM_OP_CODE].pfHandler = (CCMDHANDLER*)&CCMDHANDLER_Custom;
        VirtualProtect(&gpCCMDHandlerTable[CCMD_CUSTOM_OP_CODE], sizeof(D2CCMDStrc), oldProtect, &oldProtect);

        VirtualProtect(&gpSCMDHandlerTable[SCMD_CUSTOM_OP_CODE], sizeof(D2SCMDStrc), PAGE_EXECUTE_READWRITE, &oldProtect);
        gpSCMDHandlerTable[SCMD_CUSTOM_OP_CODE].pfHandler = (SCMDHANDLER*)&SCMDHANDLER_Custom;
        VirtualProtect(&gpSCMDHandlerTable[SCMD_CUSTOM_OP_CODE], sizeof(D2SCMDStrc), oldProtect, &oldProtect);

        // Patch default "generate new shared stash" code
        size_t nSize = 0x10e5e6 - 0x10e45a;
        auto SharedStashGenerate = (uint8_t*)Pattern::Address(0x10e45a);
        VirtualProtect(SharedStashGenerate, nSize, PAGE_EXECUTE_READWRITE, &oldProtect);
        memset(SharedStashGenerate, 0x90, nSize);   //noop it.
        uint8_t* p = SharedStashGenerate;
        *p++ = 0x48; *p++ = 0xB8;                          // mov rax, imm64
        *(uint64_t*)p = (uint64_t)&GenerateSharedStash;    // imm64
        p += 8;
        *p++ = 0xFF; *p++ = 0xD0;                          // call rax
        VirtualProtect(SharedStashGenerate, nSize, oldProtect, &oldProtect);
    }

    if (!oCCMD_ProcessClientSystemMessage) {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        oCCMD_ProcessClientSystemMessage = reinterpret_cast<CCMD_ProcessClientSystemMessage_t>(Pattern::Address(ccmdProcessClientSystemMessage));
        DetourAttach(&(PVOID&)oCCMD_ProcessClientSystemMessage, CCMD_ProcessClientSystemMessageHook);
        DetourTransactionCommit();
    }

    if ((settings.HPRollover || cachedSettings.HPRollover) && !oSUNITDMG_ApplyResistancesAndAbsorb) {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        oSUNITDMG_ApplyResistancesAndAbsorb = reinterpret_cast<SUNITDMG_ApplyResistancesAndAbsorb_t>(Pattern::Address(0x3253d0));
        DetourAttach(&(PVOID&)oSUNITDMG_ApplyResistancesAndAbsorb, HookedSUNITDMG_ApplyResistancesAndAbsorb);

        if (settings.FloatingDamage || cachedSettings.FloatingDamage)
            DetourAttach(&(PVOID&)oDamageInfo, Hooked_DamageInfo);

        DetourTransactionCommit();
    }

    if (settings.FloatingDamage || cachedSettings.FloatingDamage)
    {
        // ================= Floating Damage Render =================
        {
            ImGuiIO& io = ImGui::GetIO();
            auto drawList = ImGui::GetBackgroundDrawList();
            ImVec2 screenSize = io.DisplaySize;
            int fontIndex = 3;
            ImFont* chosenFont = (fontIndex >= 0 && fontIndex < io.Fonts->Fonts.Size) ? io.Fonts->Fonts[fontIndex] : nullptr;

            if (chosenFont)
                ImGui::PushFont(chosenFont);

            auto now = std::chrono::steady_clock::now();
            const float riseSpeed = 300.0f;      // pixels/sec
            const float perValueDelay = 0.05f;   // stagger for readability

            for (size_t i = 0; i < g_FloatingDamageList.size(); )
            {
                FloatingDamage& d = g_FloatingDamageList[i];

                // Use chrono for elapsed time
                auto now = std::chrono::steady_clock::now();
                float elapsed = std::chrono::duration<float>(now - d.spawnTime).count();

                if (elapsed >= 2.0f)
                {
                    if (i != g_FloatingDamageList.size() - 1)
                        g_FloatingDamageList[i] = std::move(g_FloatingDamageList.back());
                    g_FloatingDamageList.pop_back();
                    continue;
                }

                // Compute rise, fade, etc.
                float rise = elapsed * riseSpeed + d.displayIndex * perValueDelay * riseSpeed;
                int alpha = (int)(255 * (1.0f - elapsed * d.invLifetime));
                ImVec2 pos(
                    screenSize.x * 0.5f + d.offsetX - d.textSize.x * 0.5f,
                    screenSize.y * 0.5f - d.offsetY - rise
                );
                drawList->AddText(pos, IM_COL32(255, 64, 64, alpha), d.text.c_str());

                ++i;
            }


            if (chosenFont)
                ImGui::PopFont();
        }
    }
    


    if (showMainMenu)
        ShowMainMenu();

    /*
    if (!oD2GAME_UModInit)
    {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        oD2GAME_UModInit = reinterpret_cast<D2GAME_UModInit_t>(Pattern::Address(0x2FDDB0));
        DetourAttach(&(PVOID&)oD2GAME_UModInit, Hooked_D2GAME_UModInit);
        DetourTransactionCommit();
    }

    if (!oD2GAME_SpawnChampUnique_1402fddd0)
    {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        oD2GAME_SpawnChampUnique_1402fddd0 = reinterpret_cast<D2GAME_SpawnChampUnique_t>(Pattern::Address(0x2FDDD0));
        DetourAttach(&(PVOID&)oD2GAME_SpawnChampUnique_1402fddd0, Hooked_D2GAME_SpawnChampUnique_1402fddd0);
        DetourTransactionCommit();
    }
    */

    
    if (!oD2GAME_UMOD8Array_1402fc530)
    {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        oD2GAME_UMOD8Array_1402fc530 = reinterpret_cast<D2GAME_UMOD8Array_t>(Pattern::Address(0x2FC530));
        DetourAttach(&(PVOID&)oD2GAME_UMOD8Array_1402fc530, Hooked_D2GAME_UMOD8Array_1402fc530);
        DetourTransactionCommit();
    }

    /*
    if (!oD2GAME_SpawnMonsters_140301b5f)
    {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        oD2GAME_SpawnMonsters_140301b5f = reinterpret_cast<D2GAME_SpawnMonsters_t>(Pattern::Address(0x301B5F));
        DetourAttach(&(PVOID&)oD2GAME_SpawnMonsters_140301b5f, Hooked_D2GAME_SpawnMonsters_140301b5f);
        DetourTransactionCommit();
    }
    */

    if ((settings.HPRollover || cachedSettings.HPRollover) && !oMONSTER_GetPlayerCountBonus) {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        oMONSTER_GetPlayerCountBonus = reinterpret_cast<MONSTER_GetPlayerCountBonus_t>(Pattern::Address(0x341120));
        DetourAttach(&(PVOID&)oMONSTER_GetPlayerCountBonus, HookedMONSTER_GetPlayerCountBonus);
        DetourTransactionCommit();

        // Patch max player count for testing
        /*
        DWORD oldProtect = 0;
        {
            for (uint32_t patches : {0x135EF8, 0x135F16, 0x1E31CCC, 0x1E31D64}) {
                auto PlayerCount = (uint32_t*)Pattern::Address(patches);
                VirtualProtect(PlayerCount, 4, PAGE_EXECUTE_READWRITE, &oldProtect);
                *PlayerCount = 65535;
                VirtualProtect(PlayerCount, 4, oldProtect, &oldProtect);
            }
        }
        {
            size_t nSize = 17;
            auto PlayerCount = (uint8_t*)Pattern::Address(0x11FE12);
            VirtualProtect(PlayerCount, nSize, PAGE_EXECUTE_READWRITE, &oldProtect);
            memset(PlayerCount, 0x90, nSize);
            uint8_t* p = PlayerCount;
            *p++ = 0x41; *p++ = 0x89; *p++ = 0xC4;
            VirtualProtect(PlayerCount, nSize, oldProtect, &oldProtect);
        }
        {
            size_t nSize = 3;
            auto PlayerCount = (uint8_t*)Pattern::Address(0x136910);
            VirtualProtect(PlayerCount, nSize, PAGE_EXECUTE_READWRITE, &oldProtect);
            memset(PlayerCount, 0x90, nSize);
            uint8_t* p = PlayerCount;
            *p++ = 0x89; *p++ = 0xC8;
            VirtualProtect(PlayerCount, nSize, oldProtect, &oldProtect);
        }
        */

    }

    
    if (!oMONSTER_InitializeStatsAndSkills) {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        oMONSTER_InitializeStatsAndSkills = reinterpret_cast<MONSTER_InitializeStatsAndSkills_t>(Pattern::Address(0x33f380));
        DetourAttach(&(PVOID&)oMONSTER_InitializeStatsAndSkills, HookedMONSTER_InitializeStatsAndSkills);
        DetourTransactionCommit();
    }
    

    static bool settingsLoaded = false;

    if (!itemFilter->bInstalled) {
        if (!settingsLoaded) {
            itemFilter->Install(settings);
            settingsLoaded = true;  // mark as cached
        }
        else {
            itemFilter->Install(cachedSettings);
        }
    }

    if (configLoaded && settingsLoaded && !grailLoaded && IsPlayerInGame())
    {
        LoadAllItemData();
        ScanStashPages();
        g_GrailRevision++;
        ReloadGameFilterForGrail();
        grailLoaded = true;
    }

    if (!oHUDWarnings__PopulateHUDWarnings && GetClientStatus() == 1) {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        oHUDWarnings__PopulateHUDWarnings = reinterpret_cast<HUDWarnings__PopulateHUDWarnings_t>(Pattern::Address(0xbb2a90));
        DetourAttach(&(PVOID&)oHUDWarnings__PopulateHUDWarnings, Hooked_HUDWarnings__PopulateHUDWarnings);
        oWidget__OnClose = reinterpret_cast<Widget__OnClose_t>(Pattern::Address(0x5766f0));
        DetourAttach(&(PVOID&)oWidget__OnClose, Hooked__Widget__OnClose);
        DetourTransactionCommit();
        CheckToggleForward();

        if (settings.CombatLog || cachedSettings.CombatLog)
            ExecuteDebugCheatFunc("attackinfo 1");
    }

    if (!oDropTCTest) {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        oDropTCTest = reinterpret_cast<DropTCTest_t>(Pattern::Address(0x2f9d50));
        DetourAttach(&(PVOID&)oDropTCTest, HookedDropTCTest);
        DetourTransactionCommit();
    }

    if (!oGambleForce) {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        oGambleForce = reinterpret_cast<GambleForce_t>(Pattern::Address(0x1FC0B0));
        DetourAttach(&(PVOID&)oGambleForce, Hooked_ITEMS_CalculateGambleCost);
        DetourTransactionCommit();
    }

    auto drawList = ImGui::GetBackgroundDrawList();
    auto min = drawList->GetClipRectMin();
    auto max = drawList->GetClipRectMax();
    auto width = max.x - min.x;
    auto center = width / 2.f;

    ImGuiIO& io = ImGui::GetIO();
    ImVec2 display_size = io.DisplaySize;
    float ypercent1 = display_size.y * 0.0745f;
    float ypercent2 = display_size.y * 0.043f;

    ImFont* selectedFont = nullptr;
    bool fontPushed = false;

    if (display_size.y <= 720)
        selectedFont = io.Fonts->Fonts[0];
    else if (display_size.y <= 900)
        selectedFont = io.Fonts->Fonts[1];
    else if (display_size.y <= 1080)
        selectedFont = io.Fonts->Fonts[2];
    else if (display_size.y <= 1440)
        selectedFont = io.Fonts->Fonts[3];
    else if (display_size.y <= 2160)
        selectedFont = io.Fonts->Fonts[4];

    if (selectedFont)
    {
        ImGui::PushFont(selectedFont);
        fontPushed = true;
    }

    do
    {
        /*
        if (!settings.monsterStatsDisplay)
        {
            if (fontPushed)
                ImGui::PopFont();
            return;
        }
        */

        if (!gMouseHover->IsHovered) break;
        if (gMouseHover->HoveredUnitType > UNIT_MONSTER) break;

        D2UnitStrc* pUnit, * pUnitServer, * pUnitPlayer;
        if (pGame != nullptr)
        {
            pUnit = UNITS_GetServerUnitByTypeAndId(pGame, gMouseHover->HoveredUnitType, gMouseHover->HoveredUnitId);
            pUnitServer = UNITS_GetServerUnitByTypeAndId(pGame, gMouseHover->HoveredUnitType, gMouseHover->HoveredUnitId);
            pUnitPlayer = UNITS_GetServerUnitByTypeAndId(pGame, UNIT_PLAYER, 1);
        }
        else
        {
            pUnit = GetClientUnitPtrFunc(Pattern::Address(unitDataOffset + 0x400 * gMouseHover->HoveredUnitType), gMouseHover->HoveredUnitId & 0x7F, gMouseHover->HoveredUnitId, gMouseHover->HoveredUnitType);
            pUnitServer = GetClientUnitPtrFunc(Pattern::Address(unitDataOffset + 0x400 * gMouseHover->HoveredUnitType), gMouseHover->HoveredUnitId & 0x7F, gMouseHover->HoveredUnitId, gMouseHover->HoveredUnitType);
        }

        if (!pUnit || !pUnitServer) break;
        if (STATLIST_GetUnitStatSigned(pUnitServer, STAT_HITPOINTS, 0) == 0) break;

        if (pUnitServer && (cachedSettings.monsterStatsDisplay|| settings.monsterStatsDisplay))
        {
            float totalWidth = 0.f;
            float spaceWidth = ImGui::CalcTextSize(Seperator).x;
            std::string resistances[6];
            float widths[6];

            for (int i = 0; i < 6; i++)
            {
                int resistanceValue = STATLIST_GetUnitStatSigned(pUnitServer, ResistanceStats[i], 0);
                resistances[i] = std::format("{}", resistanceValue);
            }

            for (int i = 0; i < 6; i++)
            {
                if (i > 0) totalWidth += spaceWidth;
                widths[i] = ImGui::CalcTextSize(resistances[i].c_str()).x;
                totalWidth += widths[i];
            }

            float startX = center - (totalWidth / 2.f);
            for (int i = 0; i < 6; i++)
            {
                if (i > 0) startX += spaceWidth;
                drawList->AddText({ startX, ypercent1 }, ResistanceColors[i], resistances[i].c_str());
                startX += widths[i];
            }

            if (pUnitServer)
            {
                if (pGame == nullptr)
                {
                    auto clienthp = std::format("{}%", ((STATLIST_GetUnitStatSigned(pUnitServer, STAT_HITPOINTS, 0) >> 8) * 100) / (STATLIST_GetUnitStatSigned(pUnitServer, STAT_MAXHP, 0) >> 8));
                    auto width = ImGui::CalcTextSize(clienthp.c_str()).x;
                    drawList->AddText({ center - (width / 2.0f) + 1, ypercent2 }, IM_COL32(255, 255, 255, 255), clienthp.c_str());
                }
                else
                {
                    auto hp = std::format("{} / {}", STATLIST_GetUnitStatSigned(pUnitServer, STAT_HITPOINTS, 0) >> 8, STATLIST_GetUnitStatSigned(pUnitServer, STAT_MAXHP, 0) >> 8);
                    auto width = ImGui::CalcTextSize(hp.c_str()).x;
                    drawList->AddText({ center - (width / 2.0f) + 1, ypercent2 }, IM_COL32(255, 255, 255, 255), hp.c_str());
                }              
            }
        }

    } while (false);

    if (fontPushed)
        ImGui::PopFont();
}

#pragma endregion

#pragma region Hotkey Handler

bool D2RHUD::OnKeyPressed(short key)
{
    struct BindingMatch { bool matched; int modifierCount; };
    struct PendingAction { int modifierCount; std::function<void()> action; };
    std::vector<PendingAction> matches;

    auto GetVirtualKeyFromName = [&](std::string token) -> short
        {
            std::transform(token.begin(), token.end(), token.begin(), ::toupper);

            // modifiers
            if (token == "CTRL" || token == "CONTROL" || token == "VK_CTRL" || token == "VK_CONTROL")
                return VK_CONTROL;
            if (token == "SHIFT" || token == "VK_SHIFT")
                return VK_SHIFT;
            if (token == "ALT" || token == "MENU" || token == "VK_ALT" || token == "VK_MENU")
                return VK_MENU;

            // mouse
            if (token == "LBUTTON") return VK_LBUTTON;
            if (token == "RBUTTON") return VK_RBUTTON;
            if (token == "MBUTTON") return VK_MBUTTON;
            if (token == "XBUTTON1") return VK_XBUTTON1;
            if (token == "XBUTTON2") return VK_XBUTTON2;

            // function keys: F1–F24
            if (token.size() >= 2 && token[0] == 'F')
            {
                int f = std::atoi(token.c_str() + 1);
                if (f >= 1 && f <= 24)
                    return VK_F1 + (f - 1);
            }

            // fallback to keyMap
            auto it = keyMap.find(token);
            return it != keyMap.end() ? it->second : 0;
        };

    auto GetCurrentlyPressedKeys = [&](short triggeringKey) -> std::unordered_set<short>
        {
            std::unordered_set<short> pressed;
            pressed.insert(triggeringKey);

            for (auto& [_, vk] : keyMap)
            {
                if (GetAsyncKeyState(vk) & 0x8000)
                    pressed.insert(vk);
            }

            return pressed;
        };

    auto IsBindingPressed = [&](const std::string& binding,
        const std::unordered_set<short>& pressed) -> BindingMatch
        {
            BindingMatch result{ false, 0 };
            if (binding.empty()) return result;

            std::vector<short> keys;
            size_t start = 0;

            while (start < binding.size())
            {
                size_t end = binding.find('+', start);
                std::string token = binding.substr(start, end - start);
                token.erase(0, token.find_first_not_of(" \t"));
                token.erase(token.find_last_not_of(" \t") + 1);

                short vk = GetVirtualKeyFromName(token);
                if (!vk) return result;

                keys.push_back(vk);
                if (end == std::string::npos) break;
                start = end + 1;
            }

            for (short vk : keys)
                if (!pressed.contains(vk))
                    return result;

            result.matched = true;
            result.modifierCount = static_cast<int>(keys.size() - 1);
            return result;
        };

    auto pressedKeys = GetCurrentlyPressedKeys(key);

    auto CheckAndAddMatch =
        [&](const std::string& binding, int modifierExtra,
            std::function<void()> action, bool ignoreClientStatus = false)
        {
            if (binding.empty()) return;

            auto match = IsBindingPressed(binding, pressedKeys);
            if (match.matched && (ignoreClientStatus || GetClientStatus() == 1))
            {
                matches.push_back({ match.modifierCount + modifierExtra, action });
            }
        };

    // ---------------- Built-in actions ----------------

    if (auto kb = FindKeybind("Transmute"))
        CheckAndAddMatch(kb->key, 0, []() { D2CLIENT_Transmute(); });

    if (auto kb = FindKeybind("Identify Items"))
        CheckAndAddMatch(kb->key, 0, []() { ExecuteDebugCheatFunc("idall 1"); });

    if (auto kb = FindKeybind("Force Save"))
        CheckAndAddMatch(kb->key, 0, []() { ExecuteDebugCheatFunc("save 1"); });

    if (auto kb = FindKeybind("Reset Stats"))
        CheckAndAddMatch(kb->key, 0, []() { ExecuteDebugCheatFunc("resetstats 1"); });

    if (auto kb = FindKeybind("Reset Skills"))
        CheckAndAddMatch(kb->key, 0, []() { ExecuteDebugCheatFunc("resetskills 1"); });

    if (auto kb = FindKeybind("Remove Ground Items"))
        CheckAndAddMatch(kb->key, 0, []() { ExecuteDebugCheatFunc("itemgroundclear 1"); });

    // ---------------- Custom commands ----------------

    for (const auto& cmd : g_CommandHotkeys)
    {
        CheckAndAddMatch(cmd.key, 0, [cmd]() {
            if (cmd.command.find('/') != std::string::npos)
                CLIENT_playerCommand(cmd.command, cmd.command);
            else
                ExecuteDebugCheatFunc(cmd.command.c_str());
            });
    }

    if (auto kb = FindKeybind("Reload Game/Filter"))
        CheckAndAddMatch(kb->key, 0, [=]() {
        if (itemFilter) itemFilter->ReloadGameFilter();
            }, true);

    // ---------------- UI / panels ----------------

    if (auto kb = FindKeybind("Open Cube Panel"))
    {
        CheckAndAddMatch(kb->key, 0, [=]() {
            if (!gpClientList) return;
            auto pClient = *gpClientList;
            if (!pClient || !pClient->pGame) return;

            reinterpret_cast<int32_t(__fastcall*)(D2GameStrc*, D2UnitStrc*)>(
                Pattern::Address(0x34F5A0)
                )(pClient->pGame, pClient->pPlayer);
            });
    }

    if (auto kb = FindKeybind("Open HUDCC Menu"))
    {
        CheckAndAddMatch(kb->key, 0, [=]() {
            bool wasOpen = showGrailMenu && showMainMenu;
            showMainMenu = !showMainMenu;

            if (wasOpen && !showMainMenu)
                SaveFullGrailConfig(configFilePath, false);
            }, true);
    }

    // ---------------- Cycle filter / TZ ----------------
    if (auto kb = FindKeybind("Cycle Filter Level"))
        CheckAndAddMatch(kb->key, 0, [=]() {
        if (itemFilter) itemFilter->CycleFilter();
            });

    if (auto kb = FindKeybind("Cycle TZ Forward"))
        CheckAndAddMatch(kb->key, 0, []() { CheckToggleForward(); });

    if (auto kb = FindKeybind("Cycle TZ Backward"))
        CheckAndAddMatch(kb->key, 0, []() { CheckToggleBackward(); });

    // ---------------- Version display ----------------

    if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) &&
        (GetAsyncKeyState(VK_MENU) & 0x8000) &&
        (GetAsyncKeyState('V') & 0x8000))
    {
        matches.push_back({ 2, [=]() {
            ShowVersionMessage();
            OnStashPageChanged(gSelectedPage + 1);
        } });
    }

    // ---------------- Execute best match ----------------

    if (!matches.empty())
    {
        auto best = std::max_element(
            matches.begin(), matches.end(),
            [](const PendingAction& a, const PendingAction& b) {
                return a.modifierCount < b.modifierCount;
            });

        best->action();
        return true;
    }

    return itemFilter->OnKeyPressed(key);
}

void D2RHUD::ShowVersionMessage()
{
    std::string parsedVersion = "Unknown";

    try
    {
        std::ifstream file(lootFile);
        if (file.is_open())
        {
            std::string line;
            std::regex versionRegex(R"(local\s+version\s*=\s*\"([^\"]+)\")");
            std::smatch match;

            while (std::getline(file, line))
            {
                if (std::regex_search(line, match, versionRegex))
                {
                    parsedVersion = match[1];
                    break;
                }
            }
        }
    }
    catch (const std::exception& ex)
    {
        parsedVersion = std::string("Error: ") + ex.what();
    }

    std::string message = "D2RHUD Version: " + Version + "\n"
        "LootFilter Version: " + parsedVersion;

    MessageBoxA(nullptr, message.c_str(), "Debug Display", MB_OK | MB_ICONINFORMATION);
}


#pragma endregion
