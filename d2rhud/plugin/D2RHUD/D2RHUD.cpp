#pragma region Includes

#include "D2RHUD.h"
#include "../GrailTracker.h"
#include "../FloatingDamage.h"
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
#include <future>
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
#include <urlmon.h>
#include <shellapi.h>
#include <mmsystem.h>
#pragma comment(lib, "urlmon.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "winmm.lib")

#pragma endregion

#pragma region Credits

/*
- Chat Detours/Structures by Killshot
- Base Implementation by DannyisGreat
- Camera patterns by Shizza
- Monster Stats, Plugin Mods and extended features by Bonesy
- Stash Searching by CelestialRay
- Special Thanks to those who have helped ^^
*/

#pragma endregion

#pragma region Global Static/Structs

std::string lootFile = "../D2R/lootfilter.lua";
std::string Version = "1.7.4";

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
bool isHardcore = false;
std::atomic<uint32_t> g_GrailRevision{ 1 };
static int TerrorStat = 0;

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
static ChatManager_PushChatEntryFptr oChatManager_PushChatEntry = reinterpret_cast<ChatManager_PushChatEntryFptr>(Pattern::Address(ChatManager_PushChatEntryOffset));

typedef void(__fastcall* D2GAME_UModInit_t)(D2UnitStrc* pUnit, int32_t nUMod, int32_t bUnique);
D2GAME_UModInit_t oD2GAME_UModInit = nullptr;

typedef void(__fastcall* D2GAME_SpawnChampUnique_t)(D2GameStrc* pGame, D2ActiveRoomStrc* pRoom, void* pRoomCoordList, D2UnitStrc* pUnit, int32_t bSpawnMinions, int32_t nMinGroup, int32_t nMaxGroup);
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

static std::string TrimCommandToken(const std::string& s)
{
    const char* ws = " \t\r\n";
    size_t start = s.find_first_not_of(ws);
    if (start == std::string::npos)
        return "";
    size_t end = s.find_last_not_of(ws);
    return s.substr(start, end - start + 1);
}

static std::queue<std::function<void()>> g_keySimQueue;
static constexpr UINT_PTR kKeySimTimerId = 1235;

static VOID CALLBACK KeySimTimerProc(HWND hwnd, UINT message, UINT_PTR idTimer, DWORD dwTime)
{
    (void)message;
    (void)dwTime;
    if (idTimer != kKeySimTimerId)
        return;

    if (g_keySimQueue.empty())
    {
        KillTimer(hwnd, kKeySimTimerId);
        return;
    }

    std::function<void()> action = std::move(g_keySimQueue.front());
    g_keySimQueue.pop();
    if (action)
        action();

    if (!g_keySimQueue.empty())
        SetTimer(hwnd, kKeySimTimerId, 16, KeySimTimerProc);
    else
        KillTimer(hwnd, kKeySimTimerId);
}

static void QueueOnGameMainThread(std::function<void()> action)
{
    HWND hwnd = find_main_window(GetCurrentProcessId());
    if (!hwnd || !action)
        return;

    g_keySimQueue.push(std::move(action));
    SetTimer(hwnd, kKeySimTimerId, 16, KeySimTimerProc);
}

static void PostSyntheticKeyTap(HWND hwnd, WORD vk, bool useSysKeyMessages)
{
    if (!hwnd)
        return;

    const UINT scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    const LPARAM keyDownLParam = 1 | (static_cast<LPARAM>(scan) << 16);
    const LPARAM keyUpLParam = keyDownLParam | (static_cast<LPARAM>(1) << 30) | (static_cast<LPARAM>(1) << 31);

    if (useSysKeyMessages)
    {
        PostMessageW(hwnd, WM_SYSKEYDOWN, vk, keyDownLParam);
        PostMessageW(hwnd, WM_SYSKEYUP, vk, keyUpLParam);
    }
    else
    {
        PostMessageW(hwnd, WM_KEYDOWN, vk, keyDownLParam);
        PostMessageW(hwnd, WM_KEYUP, vk, keyUpLParam);
    }
}

static void SimulateStartupKeypress(WORD vk, bool useSysKeyMessages)
{
    QueueOnGameMainThread([vk, useSysKeyMessages]() {
        HWND hwnd = find_main_window(GetCurrentProcessId());
        if (!hwnd)
            return;
        SetForegroundWindow(hwnd);
        PostSyntheticKeyTap(hwnd, vk, useSysKeyMessages);
    });
}

static bool TryExecuteKeypressStartupCommand(const std::string& command)
{
    std::string token = TrimCommandToken(command);
    if (token.size() >= 2 && token.front() == '[' && token.back() == ']')
        token = TrimCommandToken(token.substr(1, token.size() - 2));

    std::string lower = token;
    std::transform(lower.begin(), lower.end(), lower.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (lower == "tab")
    {
        SimulateStartupKeypress(VK_TAB, false);
        return true;
    }
    if (lower == "alt")
    {
        SimulateStartupKeypress(VK_MENU, true);
        return true;
    }
    return false;
}

void ExecuteCommand(const std::string& command) {
    if (command == "disabled" || command.empty()) return;

    if (TryExecuteKeypressStartupCommand(command))
        return;

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

static std::atomic<uint64_t> s_startupCommandRunId{ 0 };

static void QueueStartupCommandExecution(float initialDelaySeconds)
{
    const uint64_t runId = ++s_startupCommandRunId;
    std::thread([runId, initialDelaySeconds]() {
        if (initialDelaySeconds > 0.0f)
            std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(initialDelaySeconds * 1000.0f)));

        if (runId != s_startupCommandRunId.load())
            return;
        if (GetClientStatus() != 1)
            return;

        const std::vector<std::string> commands = {
            automaticCommand1, automaticCommand2, automaticCommand3,
            automaticCommand4, automaticCommand5, automaticCommand6
        };
        for (const auto& command : commands)
        {
            if (runId != s_startupCommandRunId.load())
                return;
            if (!command.empty() && command != "disabled")
            ExecuteCommand(command);
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    }).detach();
}

void OnClientStatusChange()
{
    QueueStartupCommandExecution(3.0f);
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
        std::cerr << "[DEBUG] Cache HIT â€” returning cached MonsterStatsDisplaySettings" << std::endl;
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
static BroadcastChatMessageFptr oBroadcastChatMessage = reinterpret_cast<BroadcastChatMessageFptr>(Pattern::Address(BroadcastChatMessageOffset));
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

static bool IsFloatingDamageActive();
static void TryQueuePoisonCombatMessage(const char* text, D2UnitStrc* targetOverride = nullptr);
static void TryQueuePoisonDamageFromStatTable(D2DamageInfoStrc* pDamageInfo, D2DamageStatTableStrc* pDamageStatTableRecord);
static void EnsureSunitdmHook();
static void QueuePoisonDamageEvent(int amount, float screenX, float screenY, uint32_t unitType, uint32_t unitId, FloatingDamage::Element element);
static void UpdatePoisonDoTWatch();
static void SyncPoisonWatchHp(uint32_t unitType, uint32_t unitId);
static bool IsChatSoundsEnabled();
static void PlayChatNotificationSound();
static void QueueChatNotificationForSender(const char* sender, const char* message, uint32_t unitType, uint32_t unitId, bool hasUnit);
static void ProcessPendingChatNotificationSound();
static void RefreshLocalChatFilterNameCache();

static char g_cachedLocalChatFilterName[64] = {};
static uint32_t g_cachedLocalUnitId = UINT32_MAX;
static uint32_t g_cachedLocalUnitType = UINT32_MAX;

// C-only helper: client-slot character name (host slot 0 is not always local).
static bool SafeGetLocalChatName_Read(char* out, size_t outSize)
{
    static int32_t* gpClientPlayerListIndex = reinterpret_cast<int32_t*>(Pattern::Address(0x1d442d8));
    if (!out || outSize == 0 || !gpClientList || !gpClientPlayerListIndex)
        return false;

    const int32_t idx = *gpClientPlayerListIndex;
    if (idx < 0 || idx >= 8)
        return false;

    D2ClientStrc* pClient = gpClientList[idx];
    if (!pClient || !pClient->szName[0])
        return false;

    strncpy_s(out, outSize, pClient->szName, _TRUNCATE);
    return out[0] != '\0';
}

static void RefreshLocalChatFilterNameCache()
{
    if (!IsPlayerInGame())
    {
        g_cachedLocalChatFilterName[0] = '\0';
        g_cachedLocalUnitId = UINT32_MAX;
        g_cachedLocalUnitType = UINT32_MAX;
        return;
    }

    static auto s_lastRefresh = std::chrono::steady_clock::time_point{};
    const auto now = std::chrono::steady_clock::now();
    if (g_cachedLocalChatFilterName[0] && now - s_lastRefresh < std::chrono::milliseconds(500))
        return;
    s_lastRefresh = now;

    g_cachedLocalChatFilterName[0] = '\0';
    g_cachedLocalUnitId = UINT32_MAX;
    g_cachedLocalUnitType = UINT32_MAX;

    if (D2UnitStrc* pLocal = GetClientPlayerUnit())
    {
        g_cachedLocalUnitId = pLocal->dwUnitId;
        g_cachedLocalUnitType = pLocal->dwUnitType;
        D2PlayerDataStrc* pData = pLocal->pPlayerData;
        if (pData && pData->szName[0])
            strncpy_s(g_cachedLocalChatFilterName, pData->szName, _TRUNCATE);
    }

    if (!g_cachedLocalChatFilterName[0])
        SafeGetLocalChatName_Read(g_cachedLocalChatFilterName, sizeof(g_cachedLocalChatFilterName));
}

static bool IsPendingChatFromLocalUnit(uint32_t unitType, uint32_t unitId)
{
    if (g_cachedLocalUnitId == UINT32_MAX)
        return false;
    return g_cachedLocalUnitType == unitType && g_cachedLocalUnitId == unitId;
}

static bool IsChatSenderOtherPlayer(const char* sender)
{
    if (!sender || !sender[0])
        return false;
    if (!g_cachedLocalChatFilterName[0])
        return false;

    return _stricmp(sender, g_cachedLocalChatFilterName) != 0;
}

static const char* SkipD2ColorCodesForChat(const char* p)
{
    while (p && (unsigned char)p[0] == 0xFF && p[1] == 'c' && p[2])
        p += 3;
    return p;
}

static bool StartsWithIgnoreCase(const char* text, const char* prefix)
{
    if (!text || !prefix || !prefix[0])
        return false;
    return _strnicmp(text, prefix, strlen(prefix)) == 0;
}

static bool StartsWithPhraseIgnoreCase(const char* text, const char* phrase)
{
    if (!StartsWithIgnoreCase(text, phrase))
        return false;

    const size_t len = strlen(phrase);
    const char next = text[len];
    return next == '\0' || next == ' ' || next == '.' || next == '!' || next == '?';
}

static bool IsLikelySystemFeedbackMessage(const char* text)
{
    if (!text || !text[0])
        return true;

    const char* p = SkipD2ColorCodesForChat(text);
    if (!p[0])
        return true;

    static const char* kPhrases[] = {
        "Out of mana",
        "Not enough mana",
        "Not enough gold",
        "Not here",
        "Not in town",
        "I can't carry",
        "I can't get there",
        "I can't do that",
        "I cannot carry",
        "I cannot get there",
        "I cannot do that",
        "You can't carry",
        "You can't do that",
        "You cannot carry",
        "You cannot do that",
        "Cannot carry",
        "Cannot equip",
        "Need more gold",
        "Need more mana",
        "Requires level",
        "Locked",
        "No room",
        "No space",
        "Inventory is full",
        "Imbued unbalanced",
        "That is not yours",
        "That is owned",
        "This is not yours",
        "Must identify",
        nullptr
    };

    for (const char** it = kPhrases; *it; ++it)
    {
        if (StartsWithPhraseIgnoreCase(p, *it))
            return true;
    }

    return false;
}

static bool IsGameSystemChatEntry(const ChatMsg& typeMsg)
{
    const ChatMsg* pGameMsgType = reinterpret_cast<const ChatMsg*>(Pattern::Address(gameChatMsgTypeOffset));
    return pGameMsgType && typeMsg.type == pGameMsgType->type;
}

static char g_pendingChatSoundSender[64] = {};
static char g_pendingChatSoundMessage[256] = {};
static uint32_t g_pendingChatSoundUnitType = 0;
static uint32_t g_pendingChatSoundUnitId = 0;
static volatile LONG g_hasPendingChatSound = 0;
static volatile LONG g_pendingChatSoundHasUnit = 0;

static void QueueChatNotificationForSender(const char* sender, const char* message, uint32_t unitType, uint32_t unitId, bool hasUnit)
{
    if (!sender || !sender[0] || !message || !message[0])
        return;
    if (IsLikelySystemFeedbackMessage(message))
        return;

    strncpy_s(g_pendingChatSoundSender, sender, _TRUNCATE);
    strncpy_s(g_pendingChatSoundMessage, message, _TRUNCATE);
    if (hasUnit)
    {
        g_pendingChatSoundUnitType = unitType;
        g_pendingChatSoundUnitId = unitId;
        InterlockedExchange(&g_pendingChatSoundHasUnit, 1);
    }
    else
        InterlockedExchange(&g_pendingChatSoundHasUnit, 0);

    InterlockedExchange(&g_hasPendingChatSound, 1);
}

bool __fastcall Process_SCMD_CHATSTART_Hook(SCMD_CHATSTART_PACKET* pPacket) {
    if (IsFloatingDamageActive() && pPacket && pPacket->message[0])
        TryQueuePoisonCombatMessage(pPacket->message);

    if (pPacket && pPacket->msgType == 0xFF) {
        auto pChatMgr = GetChatManager();
        blz_string msg = { pPacket->message, strlen(pPacket->message), strlen(pPacket->message) };
        int colorValue = mapColorToInt(settings.channelColor);
        ChatMsg gameChatMsgType = *reinterpret_cast<ChatMsg*>(Pattern::Address(gameChatMsgTypeOffset));
        ChatOptionalStruct opt = {};

        oChatManager_PushChatEntry(pChatMgr, msg, colorValue, false, gameChatMsgType, opt, opt, opt, opt);

        if (pPacket->sender[0] && pPacket->message[0])
            QueueChatNotificationForSender(pPacket->sender, pPacket->message, pPacket->unitType, pPacket->id, true);
        return true;
    }

    const bool result = Process_SCMD_CHATSTART_Orig(pPacket);
    if (pPacket && pPacket->sender[0] && pPacket->message[0])
        QueueChatNotificationForSender(pPacket->sender, pPacket->message, pPacket->unitType, pPacket->id, true);
    return result;
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
static bool g_SunitdmHookInstalled = false;

void __fastcall HookedSUNITDMG_ApplyResistancesAndAbsorb(D2DamageInfoStrc* pDamageInfo, D2DamageStatTableStrc* pDamageStatTableRecord, int32_t bDontAbsorb);

static void EnsureSunitdmHook()
{
    if (g_SunitdmHookInstalled)
        return;

    const uint64_t addr = Pattern::Address(0x3253d0);
    if (!addr)
        return;

    oSUNITDMG_ApplyResistancesAndAbsorb = reinterpret_cast<SUNITDMG_ApplyResistancesAndAbsorb_t>(addr);

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&(PVOID&)oSUNITDMG_ApplyResistancesAndAbsorb, HookedSUNITDMG_ApplyResistancesAndAbsorb);
    DetourTransactionCommit();
    g_SunitdmHookInstalled = true;
}

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

static void ApplySunderClampToMonster(D2GameStrc* pGame, D2UnitStrc* pUnit, bool requireUModSetting);

void __fastcall HookedSUNITDMG_ApplyResistancesAndAbsorb(D2DamageInfoStrc* pDamageInfo, D2DamageStatTableStrc* pDamageStatTableRecord, int32_t bDontAbsorb) {
    if (pDamageInfo && pDamageInfo->bDefenderIsMonster && pDamageInfo->pDefender)
        ApplySunderClampToMonster(pDamageInfo->pGame, pDamageInfo->pDefender, true);

    oSUNITDMG_ApplyResistancesAndAbsorb(pDamageInfo, pDamageStatTableRecord, bDontAbsorb);

    TryQueuePoisonDamageFromStatTable(pDamageInfo, pDamageStatTableRecord);

    if ((settings.HPRollover || cachedSettings.HPRollover) && pDamageInfo && pDamageInfo->pGame && pDamageInfo->pGame->nDifficulty > settings.HPRolloverDiff) {
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

struct SunderStatSnapshot
{
    int cold = 0;
    int fire = 0;
    int light = 0;
    int poison = 0;
    int damage = 0;
    int magic = 0;
};

static D2GameStrc* g_lastSunderGame = nullptr;

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

static void MergeSunderStatsFromPlayer(SunderStatSnapshot& stats, D2UnitStrc* pPlayer)
{
    if (!pPlayer) return;

    int cold = STATLIST_GetUnitStatSigned(pPlayer, STAT_ITEM_PIERCE_COLD_IMMUNITY, 0);
    int fire = STATLIST_GetUnitStatSigned(pPlayer, STAT_ITEM_PIERCE_FIRE_IMMUNITY, 0);
    int light = STATLIST_GetUnitStatSigned(pPlayer, STAT_ITEM_PIERCE_LIGHT_IMMUNITY, 0);
    int poison = STATLIST_GetUnitStatSigned(pPlayer, STAT_ITEM_PIERCE_POISON_IMMUNITY, 0);
    int damage = STATLIST_GetUnitStatSigned(pPlayer, STAT_ITEM_PIERCE_DAMAGE_IMMUNITY, 0);
    int magic = STATLIST_GetUnitStatSigned(pPlayer, STAT_ITEM_PIERCE_MAGIC_IMMUNITY, 0);

    if (cold > stats.cold)     stats.cold = cold;
    if (fire > stats.fire)     stats.fire = fire;
    if (light > stats.light)   stats.light = light;
    if (poison > stats.poison) stats.poison = poison;
    if (damage > stats.damage) stats.damage = damage;
    if (magic > stats.magic)   stats.magic = magic;
}

static SunderStatSnapshot GetActiveSunderStats(D2GameStrc* pGame)
{
    SunderStatSnapshot stats{};

    if (pGame)
    {
        for (D2ClientStrc* pClient = pGame->pClientList; pClient; pClient = pClient->pNext)
        {
            if (pClient->pGame && pClient->pGame != pGame)
                continue;

            MergeSunderStatsFromPlayer(stats, pClient->pPlayer);

            if (pClient->dwUnitGUID)
                MergeSunderStatsFromPlayer(stats, UNITS_GetServerUnitByTypeAndId(pGame, UNIT_PLAYER, pClient->dwUnitGUID));
        }
    }

    for (int i = 0; i < 8; ++i)
    {
        auto pClient = gpClientList[i];
        if (!pClient) continue;

        if (pGame && pClient->pGame && pClient->pGame != pGame)
            continue;

        MergeSunderStatsFromPlayer(stats, pClient->pPlayer);

        if (pGame && pClient->dwUnitGUID)
            MergeSunderStatsFromPlayer(stats, UNITS_GetServerUnitByTypeAndId(pGame, UNIT_PLAYER, pClient->dwUnitGUID));
    }

    return stats;
}

static void ApplySunderClampToMonster(D2GameStrc* pGame, D2UnitStrc* pUnit, bool requireUModSetting)
{
    if (!pGame || !pUnit || !pUnit->pStatListEx)
        return;

    if (requireUModSetting && settings.sunderedMonUMods != true)
        return;

    g_lastSunderGame = pGame;

    SunderStatSnapshot sunderStats = GetActiveSunderStats(pGame);
    RemainderEntry remainders{};

    auto ApplyStat = [&](D2C_ItemStats statId, int sunderValue, int& remainder)
        {
            if (sunderValue < 100)
                return;

            remainder = sunderValue;

            int nCurrentValue = STATLIST_GetUnitStatSigned(pUnit, statId, 0);
            if (nCurrentValue >= 100)
                STATLISTEX_SetStatListExStat(pUnit->pStatListEx, statId, settings.SunderValue, 0);
        };

    ApplyStat(STAT_COLDRESIST, sunderStats.cold, remainders.cold);
    ApplyStat(STAT_FIRERESIST, sunderStats.fire, remainders.fire);
    ApplyStat(STAT_LIGHTRESIST, sunderStats.light, remainders.light);
    ApplyStat(STAT_POISONRESIST, sunderStats.poison, remainders.poison);
    ApplyStat(STAT_DAMAGERESIST, sunderStats.damage, remainders.damage);
    ApplyStat(STAT_MAGICRESIST, sunderStats.magic, remainders.magic);

    StoreRemainder(pUnit, remainders);
}

uint32_t SubtractResistances(D2UnitStrc* pUnit, D2C_ItemStats nStatId, int nValue, uint16_t nLayer = 0)
{
    if (!pUnit || nValue <= 0) return 0;

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

    ApplySunderClampToMonster(pGame, pUnit, false);

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
    int random_start_offset = 0; // set at load: random group index for initial cycle position
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
    // Optional: seed (mix with time so shuffle order varies per load; otherwise same seed => same order => same zone at index 0)
    if (j.contains("seed")) {
        dz.seed = j.at("seed").get<uint64_t>();

        if (dz.seed != 0 && !dz.zones.empty()) {
            std::mt19937_64 rng(dz.seed ^ static_cast<uint64_t>(std::time(nullptr)));
            std::shuffle(dz.zones.begin(), dz.zones.end(), rng);
        }
    }
    else {
        dz.seed = 0;
    }
    // Random initial zone group (which zone is "active") so starting location varies per load
    int n = static_cast<int>(dz.zones.size());
    if (n > 0) {
        std::random_device rd;
        std::mt19937 gen(rd() ^ static_cast<unsigned>(std::time(nullptr)));
        std::uniform_int_distribution<> dist(0, n - 1);
        dz.random_start_offset = dist(gen);
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
        LogDebug("Error: Row index out of range");
        return result;
    }

    const auto& m = monsters[rowIndex];
    std::string treasureClassValue;
    std::string tcCheck;

    LogDebug(std::format("---------------------\nMonster: {}", m.MonsterName));
    LogDebug(std::format("Monstats Row: {}, Difficulty: {}", rowIndex, diff));

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
            result.treasureIndex = static_cast<int>(i) - 1;
            break;
        }
    }

    for (size_t i = 0; i < tcexEntries.size(); ++i) {
        if (tcexEntries[i] == tcCheck) {
            result.tcCheckIndex = static_cast<int>(i) + 1;
            break;
        }
    }

    LogDebug(std::format("Treasure Class: {}", tcCheck));
    LogDebug(std::format("TZ Treasure Class: {}", treasureClassValue));
    LogDebug(std::format("Base TC Row: {}, Terror TC Row: {}", result.tcCheckIndex, result.treasureIndex));

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

    LogDebug(std::format("---------------------\nSuperUnique: {}", m.BaseMonster));
    LogDebug(std::format("GetMonsterTreasureSU called with: rowIndex={}", rowIndex));

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

    LogDebug(std::format("SU Treasure Class: {}", tcCheck));
    LogDebug(std::format("SU TZ Treasure Class: {}", treasureClassValue));
    LogDebug(std::format("SuperUniques Base TC Row: {}, SuperUniques Terror TC Row: {}", result.tcCheckIndex, result.treasureIndex));

    return result;
}

bool LoadDesecratedZones(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        // Fallback: try path with .mpq stripped (e.g. extracted mod folder: Mods/ModName/data/...)
        std::string alt = filename;
        size_t pos = alt.find(".mpq/data");
        if (pos != std::string::npos)
            alt.replace(pos, 9, "/data");
        file.open(alt);
    }
    if (!file.is_open()) {
        MessageBoxA(nullptr, ("Failed to open desecrated zones config file.\nTried: " + filename).c_str(), "Error", MB_ICONERROR);
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

        // Randomize initial zone group for each desecrated zone (cycle start position)
        std::random_device rd;
        std::mt19937 gen(rd() ^ static_cast<unsigned>(std::time(nullptr)));
        for (auto& zone : gDesecratedZones) {
            int n = static_cast<int>(zone.zones.size());
            if (n > 0) {
                std::uniform_int_distribution<> dist(0, n - 1);
                zone.random_start_offset = dist(gen);
            }
        }

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

    LogDebug(std::format("---------------------\nnTCId: {}, indexRegular: {}, indexChamp: {},  indexUnique: {}, indexSuperUnique: {},", nTCId, indexRegular, indexChamp, indexUnique, indexSuperUnique));

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
            LogDebug(std::format("nTCId Applied to Monster: {}\n---------------------\n", nTCId));
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

            LogDebug(std::format("nTCId Applied to Monster: {}\n---------------------\n", nTCId));
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
            // Use only the random offset (set at load) so the active zone varies per load and isn't tied to fixed shuffle order
            activeGroupIndex = zone.random_start_offset % groupCount;
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
        int posWithinGroup = cyclePos % cycleLengthMin;

        // Use only the random offset (set at load) so the active zone varies per load and isn't tied to fixed shuffle order
        int activeGroupIndex = (g_ManualZoneGroupOverride == -1)
            ? (zone.random_start_offset % groupCount)
            : g_ManualZoneGroupOverride;

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


#pragma region D2I Parser

#pragma region Static/Structs

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

// Stash parsing moved to GrailTracker.cpp (FindItemOffsets)
#include "../GrailStatus.h"

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
    else
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

            if (modName == "RMD-MP" && id == 556)
            {
                ImGui::Separator();
                ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f), "Quest 31: Cube 2x Town Portal scrolls to receive your completion ticket");
            }
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

                        if (modName == "RMD-MP" && id == 556)
                        {
                            ImGui::Separator();
                            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f), "Quest 31: Cube 2x Town Portal scrolls to receive your completion ticket");
                        }
                    }
                }
            }
        }
    }

    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

#pragma endregion

#pragma region Menu System

#pragma region - Static/Structs

struct CameraPreset
{
    std::string Name;
    float Pitch = 0.0f;
    float Height = 0.0f;
    float Pan = 0.0f;
    float Roll = 0.0f;
    float Zoom = 0.0f;
    bool HasValues = false;
};

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
    bool ChatSounds = true;
    std::string ChatSoundPath;
    std::vector<std::string> DLLsToLoad = { "D2RHUD.dll" };
    std::array<CameraPreset, 3> CameraPresets{};
    bool CameraAutoloadLastPreset = false;
    int CameraLastPresetIndex = 0;  // 0 = Default, 1â€“3 = preset slot
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

struct LootFilterRule
{
    std::string comment;
    std::vector<std::pair<std::string, std::string>> fields;
    std::string rawLua;
};

std::vector<LootFilterRule> g_LootFilterRules;

struct CommandEntry
{
    std::string key;
    std::string command;
};

static constexpr int kCustomCommandSlotCount = 6;
static std::vector<CommandEntry> g_CommandHotkeys;

static void EnsureCustomCommandSlots()
{
    while ((int)g_CommandHotkeys.size() < kCustomCommandSlotCount)
        g_CommandHotkeys.push_back({});
    if ((int)g_CommandHotkeys.size() > kCustomCommandSlotCount)
        g_CommandHotkeys.resize(kCustomCommandSlotCount);
}

static std::string CustomCommandHotkeyId(size_t index)
{
    return "CustomCommand:" + std::to_string(index);
}

static std::string EscapeJsonStringForConfig(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s)
    {
        switch (c)
        {
        case '\\': out += "\\\\"; break;
        case '"':  out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:   out += c; break;
        }
    }
    return out;
}

static std::string g_StartupCommands;
static std::string s_registeredStartupSnapshot;

static std::vector<std::string> ApplyStartupCommandsFromString(const std::string& startup);

static void RegisterStartupCommands(const std::string& startupText)
{
    ApplyStartupCommandsFromString(startupText);
    s_registeredStartupSnapshot = startupText;
}
static char searchBuffer[128] = "";
static bool filterUncollected = false;
static bool showMainMenu = false;
static bool showHUDSettingsMenu = false;
static bool showD2RHUDMenu = false;
static bool showLootMenu = false;
static bool showHotkeyMenu = false;
static bool showGrailMenu = false;
static bool showCameraMenu = false;
static bool showFloatingDamageMenu = false;
static void ShowFloatingDamageMenu();
static bool showSettingsPanel = false;
static std::string s_UITheme = "Default";
// Per-slot image override: s_UIColorImageOverrides[i] = filename from D2RHUD_Images (empty = use color only)
static std::vector<std::string> s_UIColorImageOverrides;
static std::vector<ImVec4> s_UICustomColors;  // when theme is "Custom", applied by index
static std::vector<std::pair<std::string, std::vector<ImVec4>>> s_UIThemePresets;  // user-added presets
struct ThemeColorEntry { const char* label; int imguiCol; };
static const int kImGuiColSentinel = -1;  // theme-only color (not applied to ImGui style; used for checkboxes)
static const ThemeColorEntry s_ThemeColorEntries[] = {
    {"Text", ImGuiCol_Text},
    {"Text Disabled", ImGuiCol_TextDisabled},
    {"Border", ImGuiCol_Border},
    {"Frame Bg", ImGuiCol_FrameBg},
    {"Frame Bg Hovered", ImGuiCol_FrameBgHovered},
    {"Frame Bg Active", ImGuiCol_FrameBgActive},
    {"Check Mark", ImGuiCol_CheckMark},
    {"Checkbox", kImGuiColSentinel},
    {"Checkbox Hovered", kImGuiColSentinel},
    {"Checkbox Active", kImGuiColSentinel},
    {"Button", ImGuiCol_Button},
    {"Button Hovered", ImGuiCol_ButtonHovered},
    {"Button Active", ImGuiCol_ButtonActive},
    {"Header", ImGuiCol_Header},
    {"Header Hovered", ImGuiCol_HeaderHovered},
    {"Header Active", ImGuiCol_HeaderActive},
    {"Separator", ImGuiCol_Separator},
};
static const int s_ThemeColorCount = (int)(sizeof(s_ThemeColorEntries) / sizeof(s_ThemeColorEntries[0]));

// Per-window background: order matches UI list (Control Center, Theme Control, D2RHUD Options, Loot, Grail, Hotkeys, Camera)
enum WindowBgId { kWindowBg_ControlCenter = 0, kWindowBg_ThemeControl, kWindowBg_D2RHUDOptions, kWindowBg_Loot, kWindowBg_Grail, kWindowBg_Hotkeys, kWindowBg_Camera, kWindowBgCount };
static const char* const s_WindowBgNames[kWindowBgCount] = {
    "D2RHUD Control Center", "Theme Control", "D2RHUD Options", "D2RLoot Settings", "Grail Tracker", "Hotkey Controls", "Camera Controls"
};
static ImVec4 s_WindowBgColors[kWindowBgCount];
static std::string s_WindowBgImageOverrides[kWindowBgCount];

static bool ThemeButton(const char* label, const ImVec2& size = ImVec2(0, 0));  // implemented later
static bool ThemeCheckbox(const char* label, bool* v);  // implemented later
static bool showBaseCodes = false;
static bool showBaseNames = false;
static bool showDuplicates = false;

// Persistent camera state for Camera Controls menu
struct CameraState
{
    bool programEnabled = true;
    bool valuesFound = false;
    bool zoomFound = false;

    // Track whether a scan has been attempted (for UI messaging)
    bool didScanAngles = false;
    bool didScanZoom = false;

    // Track whether the last scan attempt was in-game or out-of-game
    bool lastAngleScanInGame = false;
    bool lastZoomScanInGame = false;

    float pitch = 0.0f;
    float height = 0.0f;
    float pan = 0.0f;
    float roll = 0.0f;
    float zoom = 0.0f;

    float defaultPitch = 0.0f;
    float defaultHeight = 0.0f;
    float defaultPan = 0.0f;
    float defaultRoll = 0.0f;
    float defaultZoom = 0.0f;
};

static CameraState g_CameraState;

LootFilterHeader g_LootFilterHeader;
std::unordered_map<std::string, std::string> g_LuaVariables;
std::unordered_map<std::string, std::string> g_LuaVariableComments;
std::unordered_map<std::string, std::pair<std::string, std::string>> g_Hotkeys;
static D2RHUDConfig d2rHUDConfig;
using ordered_json = nlohmann::ordered_json;
bool lootConfigLoaded = false;
bool lootLogicLoaded = false;
static bool showCollected = false;
static bool showExcluded = false;
bool HUDConfigLoaded = false;
static std::string s_pendingFilter;
static std::string s_activeFilterInternalName;
static std::string s_lootFilterUpdateStatus;
static std::chrono::steady_clock::time_point s_lootFilterUpdateStatusTime;
static std::atomic<bool> s_lootFilterUpdating{ false };
static std::string s_lootFilterNewVersion;
static bool s_lootFilterUpdateApplied = false;

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

// Returns 2.0f for 4K resolution (width >= 3840 or height >= 2160), else 1.0f. Used to scale all menu elements.
float GetMenuScaleFactor()
{
    ImVec2 display = ImGui::GetIO().DisplaySize;
    return (display.x >= 3840.0f || display.y >= 2160.0f) ? 2.0f : 1.0f;
}

ImVec2 CenterWindow(ImVec2 size)
{
    float scale = GetMenuScaleFactor();
    size.x *= scale;
    size.y *= scale;
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
    float scale = GetMenuScaleFactor();
    float closeBtnSize = 20.0f * scale;
    float padding = 5.0f * scale;
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
    return showGrailMenu || showHotkeyMenu || showLootMenu || showD2RHUDMenu || showCameraMenu || showFloatingDamageMenu;
}

bool D2RHUD::TryCloseMenuOnEscape()
{
    if (showHotkeyMenu) { showHotkeyMenu = false; return true; }
    if (showGrailMenu) { showGrailMenu = false; return true; }
    if (showLootMenu) { showLootMenu = false; return true; }
    if (showCameraMenu) { showCameraMenu = false; return true; }
    if (showFloatingDamageMenu) { showFloatingDamageMenu = false; return true; }
    if (showD2RHUDMenu) { showD2RHUDMenu = false; return true; }
    if (showSettingsPanel) { showSettingsPanel = false; return true; }
    if (showHUDSettingsMenu) { showHUDSettingsMenu = false; return true; }
    if (showMainMenu) { showMainMenu = false; return true; }
    return false;
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

static bool IsTZCycleHotkey(const std::string& name)
{
    return name == "Cycle TZ Forward" || name == "Cycle TZ Backward";
}

static bool IsTZCycleUnavailable(const std::string& name)
{
    if (!IsTZCycleHotkey(name))
        return false;

    if (modName != "RMD-MP")
        return false;

    return TerrorStat != 1;
}

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

static std::vector<std::string> ApplyStartupCommandsFromString(const std::string& startup)
{
    std::vector<std::string> result;

    g_StartupCommands = startup;

    if (!startup.empty())
    {
        std::stringstream ss(startup);
        std::string cmd;
        while (std::getline(ss, cmd, ','))
        {
            cmd = TrimCommandToken(cmd);
            if (!cmd.empty())
                result.push_back(cmd);
        }
    }

    result.resize(6);

    automaticCommand1 = result[0];
    automaticCommand2 = result[1];
    automaticCommand3 = result[2];
    automaticCommand4 = result[3];
    automaticCommand5 = result[4];
    automaticCommand6 = result[5];

    return result;
}

static void RegisterStartupCommandsAfterEdit(const std::string& startupText)
{
    ApplyStartupCommandsFromString(startupText);
    if (startupText == s_registeredStartupSnapshot)
        return;
    s_registeredStartupSnapshot = startupText;
    if (GetClientStatus() == 1)
        QueueStartupCommandExecution(3.0f);
}

std::vector<std::string> ReadStartupCommandsFromJson(const ordered_json& j)
{
    std::cout << "[StartupCmd] Enter ReadStartupCommandsFromJson()\n";

    if (!j.contains("Commands"))
    {
        std::cout << "[StartupCmd] ERROR: JSON has no 'Commands' object\n";
        EnsureCustomCommandSlots();
        return std::vector<std::string>(6);
    }

    const auto& cmdsObj = j["Commands"];

    if (!cmdsObj.contains("Startup Commands"))
        std::cout << "[StartupCmd] WARNING: 'Commands' has no 'Startup Commands' entry\n";

    const std::string startup = cmdsObj.value("Startup Commands", "");
    std::cout << "[StartupCmd] Raw Startup Commands string: \""
        << startup << "\" (len=" << startup.size() << ")\n";

    std::vector<std::string> result = ApplyStartupCommandsFromString(startup);
    s_registeredStartupSnapshot = startup;

    std::cout << "[StartupCmd] Parsed command count = " << result.size() << "\n";
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

    if (!j.contains("Commands") || !j["Commands"].is_object())
    {
        EnsureCustomCommandSlots();
        return;
    }

    const auto& commands = j["Commands"];
    if (!commands.contains("Custom Commands") || !commands["Custom Commands"].is_array())
    {
        EnsureCustomCommandSlots();
        return;
    }

    for (const auto& entry : commands["Custom Commands"])
    {
        CommandEntry ce;
        ce.key = entry.value("Key", "");
        ce.command = entry.value("Command", "");

        g_CommandHotkeys.push_back(ce);
    }

    EnsureCustomCommandSlots();
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

    commands["Custom Commands"] = json::array();
    EnsureCustomCommandSlots();
    for (const auto& cmd : g_CommandHotkeys)
        commands["Custom Commands"].push_back(json{ {"Key", cmd.key}, {"Command", cmd.command} });

    // ---------------- Write file ----------------
    std::ofstream out(filename);
    if (!out.is_open())
        return;

    out << std::setw(4) << j << std::endl;
}

// Returns the app-root "My Filters" directory (used for filter list and imports). May not exist yet.
static std::string GetModFiltersDir()
{
    std::string appRoot = GetExecutableDir();
    if (appRoot.empty()) return "";
    return (std::filesystem::path(appRoot) / "My Filters").string();
}

// Returns the mod's D2RLAN filters directory path (tries .mpq path then extracted /data path). May not exist.
static std::string GetModD2RLANFiltersDir()
{
    std::string modName = GetModName();
    if (modName.empty()) return "";
    auto tryPath = [&](const std::string& subpath) -> std::string {
        std::string p = GetExecutableDir() + "/Mods/" + modName + "/" + modName + ".mpq/data/" + subpath + "/filters";
        if (std::filesystem::exists(p)) return p;
        p = GetExecutableDir() + "/Mods/" + modName + "/data/" + subpath + "/filters";
        return std::filesystem::exists(p) ? p : "";
        };
    std::string p = tryPath("D2RLAN");
    if (!p.empty()) return p;
    return tryPath("d2rlan");
}

// Returns the mod's D2RLAN/filters/sounds directory path. May not exist.
static std::string GetModD2RLANSoundsDir()
{
    std::string filtersDir = GetModD2RLANFiltersDir();
    if (filtersDir.empty()) return "";
    return (std::filesystem::path(filtersDir) / "sounds").string();
}

// Appends filter names from dir (dirs and .lua base names, excludes Sounds folder) into names set.
static void AppendFilterNamesFromDir(const std::string& dir, std::set<std::string>& names)
{
    if (dir.empty() || !std::filesystem::exists(dir)) return;
    namespace fs = std::filesystem;
    for (const auto& entry : fs::directory_iterator(dir))
    {
        std::string name = entry.path().filename().string();
        if (entry.is_directory())
        {
            std::string lower = name;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            if (lower != "sounds")
                names.insert(name);
        }
        else if (entry.path().extension() == ".lua")
            names.insert(entry.path().stem().string());
    }
}

// Lists filter names from both My Filters and mod's D2RLAN/filters (merged, deduped, sorted).
static std::vector<std::string> GetModFilterNames()
{
    std::set<std::string> names;
    AppendFilterNamesFromDir(GetModFiltersDir(), names);
    AppendFilterNamesFromDir(GetModD2RLANFiltersDir(), names);
    return std::vector<std::string>(names.begin(), names.end());
}

// Returns the app-root "My Filters/sounds" directory. May not exist yet.
static std::string GetModSoundsDir()
{
    std::string filtersDir = GetModFiltersDir();
    if (filtersDir.empty()) return "";
    return (std::filesystem::path(filtersDir) / "sounds").string();
}

static bool IsSoundExtension(const std::filesystem::path& p);

// Lists sound files from both My Filters/sounds and mod's D2RLAN/sounds. Returns (fullPath, filename) for each.
static std::vector<std::pair<std::string, std::string>> GetSoundFilesFromBothLocations()
{
    std::vector<std::pair<std::string, std::string>> out;
    std::error_code ec;
    auto addFromDir = [&](const std::string& dir) {
        if (dir.empty() || !std::filesystem::exists(dir, ec)) return;
        for (const auto& entry : std::filesystem::directory_iterator(dir, std::filesystem::directory_options::skip_permission_denied, ec))
        {
            if (!entry.is_regular_file(ec)) continue;
            if (!IsSoundExtension(entry.path())) continue;
            std::string name = entry.path().filename().string();
            out.push_back({ entry.path().string(), name });
        }
        };
    addFromDir(GetModSoundsDir());
    addFromDir(GetModD2RLANSoundsDir());
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.second < b.second; });
    return out;
}

// Display name for filter list; internal name is file/dir name.
static std::string GetFilterDisplayName(const std::string& internalName)
{
    if (internalName == "lootfilter_config_blank") return "No Filter";
    if (internalName == "lootfilter_default") return GetModName() + "'s Default Filter";
    if (internalName == "override_rules") return GetModName() + "'s Override Rules";
    return internalName;
}

// Ordered list: all filters except special two (sorted), then override_rules, then lootfilter_config_blank.
static std::vector<std::string> GetOrderedFilterList()
{
    std::vector<std::string> raw = GetModFilterNames();
    std::vector<std::string> out;
    std::vector<std::string> tail;
    for (const auto& name : raw)
    {
        if (name == "lootfilter_config_blank")
            tail.push_back(name);
        else if (name == "override_rules")
            tail.insert(tail.begin(), name);
        else
            out.push_back(name);
    }
    std::sort(out.begin(), out.end());
    for (const auto& t : tail) out.push_back(t);
    return out;
}

// Path to lootfilter_config.lua (same directory as lootfilter.lua / lootFile).
static std::string GetLootFilterConfigPath()
{
    std::filesystem::path p(lootFile);
    return (p.parent_path() / "lootfilter_config.lua").string();
}

// Absolute path to the active config file (for opening in default app).
static std::string GetLootFilterConfigPathAbsolute()
{
    std::filesystem::path p(GetLootFilterConfigPath());
    if (p.is_relative())
        p = std::filesystem::current_path() / p;
    return std::filesystem::absolute(p).string();
}

static void OpenInShell(const std::string& pathOrUrl)
{
    ShellExecuteA(nullptr, "open", pathOrUrl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

// Play sound file using Windows API (WAV: PlaySound; MP3: MCI; FLAC: fallback to default app).
static void PlaySoundFile(const std::string& path)
{
    if (path.empty()) return;
    std::string ext = std::filesystem::path(path).extension().string();
    for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
    if (ext == ".wav")
    {
        PlaySoundA(path.c_str(), nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
        return;
    }
    if (ext == ".mp3")
    {
        mciSendStringA("close snd", nullptr, 0, nullptr);
        std::string cmd = "open \"" + path + "\" type mpegvideo alias snd";
        if (mciSendStringA(cmd.c_str(), nullptr, 0, nullptr) == 0)
            mciSendStringA("play snd", nullptr, 0, nullptr);
        return;
    }
    if (ext == ".flac")
    {
        OpenInShell(path);
        return;
    }
}

bool SaveFullGrailConfig(const std::string& userPath, bool isAutoBackup);

#pragma region Chat Sounds

static bool IsChatSoundsEnabled()
{
    return d2rHUDConfig.ChatSounds;
}

static std::atomic<bool> g_chatSoundPathCacheDirty{ true };
static std::string g_cachedChatSoundPlayPath;

static void InvalidateChatSoundPathCache()
{
    g_chatSoundPathCacheDirty.store(true, std::memory_order_release);
}

static std::string ResolveChatNotificationSoundPath()
{
    if (!g_chatSoundPathCacheDirty.load(std::memory_order_acquire))
        return g_cachedChatSoundPlayPath;

    g_chatSoundPathCacheDirty.store(false, std::memory_order_release);
    g_cachedChatSoundPlayPath.clear();

    try
    {
        if (!d2rHUDConfig.ChatSoundPath.empty() && std::filesystem::exists(d2rHUDConfig.ChatSoundPath))
            g_cachedChatSoundPlayPath = d2rHUDConfig.ChatSoundPath;
        else
        {
            const auto soundFiles = GetSoundFilesFromBothLocations();
            if (!soundFiles.empty() && std::filesystem::exists(soundFiles.front().first))
                g_cachedChatSoundPlayPath = soundFiles.front().first;
        }
    }
    catch (...)
    {
        g_cachedChatSoundPlayPath.clear();
    }

    return g_cachedChatSoundPlayPath;
}

static void PlayChatNotificationSoundOnWorker(std::string path)
{
    static auto lastPlay = std::chrono::steady_clock::time_point{};
    const auto now = std::chrono::steady_clock::now();
    if (now - lastPlay < std::chrono::milliseconds(350))
        return;
    lastPlay = now;

    try
    {
        if (!path.empty())
            PlaySoundFile(path);
        else
            MessageBeep(MB_OK);
    }
    catch (...)
    {
        MessageBeep(MB_OK);
    }
}

static void PlayChatNotificationSound()
{
    PlayChatNotificationSoundOnWorker(ResolveChatNotificationSoundPath());
}

static void ProcessPendingChatNotificationSound()
{
    if (InterlockedExchange(&g_hasPendingChatSound, 0) == 0)
        return;
    if (!IsChatSoundsEnabled() || !IsPlayerInGame())
        return;

    RefreshLocalChatFilterNameCache();

    char sender[64] = {};
    strncpy_s(sender, g_pendingChatSoundSender, _TRUNCATE);

    if (InterlockedExchange(&g_pendingChatSoundHasUnit, 0) != 0)
    {
        if (IsPendingChatFromLocalUnit(g_pendingChatSoundUnitType, g_pendingChatSoundUnitId))
            return;
    }

    if (IsLikelySystemFeedbackMessage(g_pendingChatSoundMessage))
        return;

    if (!IsChatSenderOtherPlayer(sender))
        return;

    PlayChatNotificationSound();
}

static void EnsureChatManagerHook();

static void UpdateChatSoundsFeature()
{
    if (IsChatSoundsEnabled())
        EnsureChatManagerHook();
}

static bool DrawChatSoundSelector(float menuScale);

#pragma endregion

static bool CopyModFilterToActive(const std::string& filterName);
void LoadLootFilterConfig(const std::string& path);
void LoadLootFilterLogic(const std::string& path);
static bool ShouldDrawFrameBgImage();
static void BeginFrameBgImageRegion();
static void EndFrameBgImageRegion();

// Import Filter: paste path only. No modal, no directory listing - avoids freeze when injected.
static bool s_showImportPathInput = false;
static bool s_importPathInputJustOpened = false;
static char s_importPathBuf[1024] = {};
static std::string s_importPathInput;
static std::string s_importPathError;
static std::string s_importSuccessMessage;
static std::chrono::steady_clock::time_point s_importSuccessTime;
static const float s_importSuccessDurationSec = 3.0f;

// Create your own filter: prompt for title/type/description, then write template.
static bool s_showCreateFilterPopup = false;
static bool s_createFilterPopupJustOpened = false;
static char s_createFilterTitleBuf[256] = "No Filter";
static char s_createFilterTypeBuf[256] = "(None)";
static char s_createFilterDescBuf[1024] = "This is not a filter, it applies no changes.";

static void OpenCreateFilterPopup()
{
    s_showCreateFilterPopup = true;
    s_createFilterPopupJustOpened = true;
    strncpy(s_createFilterTitleBuf, "No Filter", sizeof(s_createFilterTitleBuf) - 1);
    s_createFilterTitleBuf[sizeof(s_createFilterTitleBuf) - 1] = '\0';
    strncpy(s_createFilterTypeBuf, "(None)", sizeof(s_createFilterTypeBuf) - 1);
    s_createFilterTypeBuf[sizeof(s_createFilterTypeBuf) - 1] = '\0';
    strncpy(s_createFilterDescBuf, "This is not a filter, it applies no changes.", sizeof(s_createFilterDescBuf) - 1);
    s_createFilterDescBuf[sizeof(s_createFilterDescBuf) - 1] = '\0';
}

// Strip surrounding quotes and whitespace (handles "Copy as path" from Explorer).
static std::string TrimPathPaste(std::string s)
{
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '"' || s.front() == '\''))
        s.erase(0, 1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '"' || s.back() == '\''))
        s.pop_back();
    return s;
}

static void OpenImportPathInput()
{
    s_showImportPathInput = true;
    s_importPathInputJustOpened = true;
    s_importPathError.clear();
    s_importSuccessMessage.clear();
}

// --- Import Sounds (paste folder path, copy .mp3/.flac/.wav into My Filters/sounds) ---
static bool s_showImportSoundsPathInput = false;
static bool s_importSoundsPathInputJustOpened = false;
static char s_importSoundsPathBuf[1024] = {};
static std::string s_importSoundsPathError;

static void OpenImportSoundsPathInput()
{
    s_showImportSoundsPathInput = true;
    s_importSoundsPathInputJustOpened = true;
    s_importSoundsPathError.clear();
}

static bool IsSoundExtension(const std::filesystem::path& p)
{
    std::string ext = p.extension().string();
    if (ext.empty()) return false;
    for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
    return ext == ".mp3" || ext == ".flac" || ext == ".wav";
}

static void DrawImportSoundsPathRow()
{
    if (!s_showImportSoundsPathInput) return;
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("Paste folder path containing .mp3, .flac, .wav files (they will be copied into My Filters/sounds):");
    ImGui::SetNextItemWidth(-80.0f);
    if (s_importSoundsPathInputJustOpened)
    {
        s_importSoundsPathInputJustOpened = false;
        s_importSoundsPathBuf[0] = '\0';
    }
    BeginFrameBgImageRegion();
    bool doImport = ImGui::InputText("##importsounds_path", s_importSoundsPathBuf, sizeof(s_importSoundsPathBuf), ImGuiInputTextFlags_EnterReturnsTrue);
    EndFrameBgImageRegion();
    ImGui::SameLine();
    if (ThemeButton("Import##sounds", ImVec2(70.0f, 0.0f))) doImport = true;
    ImGui::SameLine();
    if (ThemeButton("Cancel##sounds", ImVec2(70.0f, 0.0f)))
    {
        s_showImportSoundsPathInput = false;
        s_importSoundsPathError.clear();
    }
    if (doImport)
    {
        s_importSoundsPathError.clear();
        std::string pathTrim = TrimPathPaste(s_importSoundsPathBuf);
        if (pathTrim.empty())
            s_importSoundsPathError = "Enter a path.";
        else
        {
            std::string destDir = GetModSoundsDir();
            if (destDir.empty())
                s_importSoundsPathError = "My Filters/sounds path not found.";
            else
            {
                std::filesystem::path srcDir(pathTrim);
                std::error_code ec;
                if (!std::filesystem::exists(srcDir, ec) || !std::filesystem::is_directory(srcDir, ec))
                    s_importSoundsPathError = "Folder not found.";
                else
                {
                    std::filesystem::create_directories(destDir, ec);
                    int copied = 0;
                    for (const auto& entry : std::filesystem::directory_iterator(srcDir, std::filesystem::directory_options::skip_permission_denied, ec))
                    {
                        if (!entry.is_regular_file(ec)) continue;
                        if (!IsSoundExtension(entry.path())) continue;
                        std::filesystem::path dest = std::filesystem::path(destDir) / entry.path().filename();
                        std::filesystem::copy_file(entry.path(), dest, std::filesystem::copy_options::overwrite_existing, ec);
                        if (!ec) copied++;
                    }
                    s_showImportSoundsPathInput = false;
                    s_importSoundsPathBuf[0] = '\0';
                    if (copied == 0)
                        s_importSoundsPathError = "No .mp3, .flac or .wav files found in folder.";
                }
            }
        }
    }
    if (!s_importSoundsPathError.empty())
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", s_importSoundsPathError.c_str());
    ImGui::Separator();
    ImGui::Spacing();
}

static char s_chatSoundCustomPathBuf[1024] = {};
static bool s_chatSoundCustomPathBufInit = false;
static bool s_chatSoundUseCustomPath = false;

static float GetOptionsLeftColumnContentWidth()
{
    const float pad = ImGui::GetStyle().WindowPadding.x;
    return (std::max)(1.0f, ImGui::GetColumnWidth(0) - pad * 2.0f);
}

static void SetOptionsLeftColumnContentStartX()
{
    ImGui::SetCursorPosX(ImGui::GetColumnOffset(0) + ImGui::GetStyle().WindowPadding.x);
}

static bool DrawChatSoundSelector(float menuScale)
{
    bool changed = false;
    if (!d2rHUDConfig.ChatSounds)
        ImGui::BeginDisabled();

    const auto soundFiles = GetSoundFilesFromBothLocations();
    const int kAutoIdx = 0;
    const int kCustomIdx = 1 + static_cast<int>(soundFiles.size());

    int currentIdx = kAutoIdx;
    if (!d2rHUDConfig.ChatSoundPath.empty())
    {
        bool matched = false;
        for (size_t i = 0; i < soundFiles.size(); ++i)
        {
            if (soundFiles[i].first == d2rHUDConfig.ChatSoundPath)
            {
                currentIdx = 1 + static_cast<int>(i);
                matched = true;
                s_chatSoundUseCustomPath = false;
                break;
            }
        }
        if (!matched)
        {
            currentIdx = kCustomIdx;
            s_chatSoundUseCustomPath = true;
        }
    }
    else if (s_chatSoundUseCustomPath)
        currentIdx = kCustomIdx;

    const bool customMode = (currentIdx == kCustomIdx);

    std::string preview = soundFiles.empty() ? "Auto (system beep)" : "Auto (first in folder)";
    if (customMode)
        preview = "Custom path...";
    else if (currentIdx > 0 && currentIdx <= static_cast<int>(soundFiles.size()))
        preview = soundFiles[static_cast<size_t>(currentIdx - 1)].second;

    const float playBtnW = 45.0f + ImGui::GetStyle().FramePadding.x * 2.0f;
    const float comboW = (std::max)(80.0f, ImGui::GetContentRegionAvail().x - playBtnW - ImGui::GetStyle().ItemSpacing.x);
    ImGui::PushItemWidth(comboW);
    BeginFrameBgImageRegion();
    if (ImGui::BeginCombo("##chat_sound", preview.c_str()))
    {
        const bool autoSelected = (currentIdx == kAutoIdx);
        if (ImGui::Selectable(soundFiles.empty() ? "Auto (system beep)" : "Auto (first in folder)", autoSelected))
        {
            if (!autoSelected)
            {
                d2rHUDConfig.ChatSoundPath.clear();
                s_chatSoundUseCustomPath = false;
                s_chatSoundCustomPathBufInit = false;
                changed = true;
            }
        }
        for (size_t i = 0; i < soundFiles.size(); ++i)
        {
            const int idx = 1 + static_cast<int>(i);
            if (ImGui::Selectable(soundFiles[i].second.c_str(), currentIdx == idx))
            {
                if (currentIdx != idx)
                {
                    d2rHUDConfig.ChatSoundPath = soundFiles[i].first;
                    s_chatSoundUseCustomPath = false;
                    s_chatSoundCustomPathBufInit = false;
                    changed = true;
                }
            }
        }
        if (ImGui::Selectable("Custom path...", customMode))
        {
            if (!customMode)
            {
                strncpy_s(s_chatSoundCustomPathBuf, d2rHUDConfig.ChatSoundPath.c_str(), _TRUNCATE);
                s_chatSoundUseCustomPath = true;
                s_chatSoundCustomPathBufInit = true;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    EndFrameBgImageRegion();
    ImGui::PopItemWidth();

    ImGui::SameLine();
    if (ThemeButton("Play##chatsound", ImVec2(45.0f, 0.0f)))
    {
        std::string path;
        if (customMode)
        {
            path = TrimPathPaste(s_chatSoundCustomPathBuf);
            if (path.empty())
                path = d2rHUDConfig.ChatSoundPath;
        }
        else if (currentIdx == kAutoIdx)
        {
            if (!soundFiles.empty())
                path = soundFiles.front().first;
        }
        else if (currentIdx > 0 && currentIdx <= static_cast<int>(soundFiles.size()))
            path = soundFiles[static_cast<size_t>(currentIdx - 1)].first;

        if (!path.empty() && std::filesystem::exists(path))
            PlaySoundFile(path);
        else
            MessageBeep(MB_OK);
    }

    if (customMode)
    {
        if (!s_chatSoundCustomPathBufInit)
        {
            strncpy_s(s_chatSoundCustomPathBuf, d2rHUDConfig.ChatSoundPath.c_str(), _TRUNCATE);
            s_chatSoundCustomPathBufInit = true;
        }
        ImGui::Dummy(ImVec2(0.0f, 2.0f));
        SetOptionsLeftColumnContentStartX();
        ImGui::PushItemWidth(GetOptionsLeftColumnContentWidth());
        BeginFrameBgImageRegion();
        ImGui::InputText("##chat_sound_custom", s_chatSoundCustomPathBuf, sizeof(s_chatSoundCustomPathBuf));
        EndFrameBgImageRegion();
        ImGui::PopItemWidth();
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            const std::string path = TrimPathPaste(s_chatSoundCustomPathBuf);
            if (path != d2rHUDConfig.ChatSoundPath)
            {
                d2rHUDConfig.ChatSoundPath = path;
                changed = true;
            }
        }
    }
    else
        s_chatSoundCustomPathBufInit = false;

    if (!d2rHUDConfig.ChatSounds)
        ImGui::EndDisabled();

    return changed;
}

// Draw inline path row when Import is expanded. No popup, no directory iteration.
static void DrawImportFilterPathRow()
{
    if (!s_showImportPathInput) return;
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("Paste full path to a .lua filter file (e.g. from Explorer: Shift+Right-click file, Copy as path):");
    ImGui::SetNextItemWidth(-80.0f);
    if (s_importPathInputJustOpened)
    {
        s_importPathInputJustOpened = false;
        strncpy(s_importPathBuf, s_importPathInput.c_str(), sizeof(s_importPathBuf) - 1);
        s_importPathBuf[sizeof(s_importPathBuf) - 1] = '\0';
    }
    BeginFrameBgImageRegion();
    bool doImport = ImGui::InputText("##importpath", s_importPathBuf, sizeof(s_importPathBuf), ImGuiInputTextFlags_EnterReturnsTrue);
    EndFrameBgImageRegion();
    s_importPathInput = s_importPathBuf;
    ImGui::SameLine();
    if (ThemeButton("Import", ImVec2(70.0f, 0.0f))) doImport = true;
    ImGui::SameLine();
    if (ThemeButton("Cancel", ImVec2(70.0f, 0.0f)))
    {
        s_showImportPathInput = false;
        s_importPathError.clear();
    }
    if (doImport)
    {
        s_importPathError.clear();
        s_importSuccessMessage.clear();
        std::string pathTrim = TrimPathPaste(s_importPathInput);
        if (pathTrim.empty())
            s_importPathError = "Enter a path.";
        else
        {
            std::string dir = GetModFiltersDir();
            if (dir.empty())
                s_importPathError = "My Filters path not found.";
            else
            {
                std::filesystem::path src(pathTrim);
                std::error_code ec;
                if (!std::filesystem::exists(src, ec) || !std::filesystem::is_regular_file(src, ec))
                    s_importPathError = "File not found.";
                else if (src.extension() != ".lua")
                    s_importPathError = "File must be .lua";
                else
                {
                    std::filesystem::create_directories(dir, ec);
                    std::filesystem::path dest = std::filesystem::path(dir) / src.filename();
                    std::filesystem::copy_file(src, dest, std::filesystem::copy_options::overwrite_existing, ec);
                    if (ec)
                        s_importPathError = "Copy failed.";
                    else
                    {
                        std::string filterName = src.stem().string();
                        if (CopyModFilterToActive(filterName))
                        {
                            s_activeFilterInternalName = filterName;
                            LoadLootFilterConfig(GetLootFilterConfigPath());
                            LoadLootFilterLogic(lootFile);
                        }
                        s_importSuccessMessage = "Filter imported successfully.";
                        s_importSuccessTime = std::chrono::steady_clock::now();
                        s_importPathInput.clear();
                        s_importPathBuf[0] = '\0';
                    }
                }
            }
        }
    }
    if (!s_importSuccessMessage.empty())
    {
        float elapsed = std::chrono::duration<float>(std::chrono::steady_clock::now() - s_importSuccessTime).count();
        if (elapsed >= s_importSuccessDurationSec)
        {
            s_importSuccessMessage.clear();
            s_showImportPathInput = false;
        }
        else
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s", s_importSuccessMessage.c_str());
    }
    if (!s_importPathError.empty())
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", s_importPathError.c_str());
    ImGui::Separator();
    ImGui::Spacing();
}

// Returns true if version string a is strictly newer than b (e.g. "1.4.2" > "1.4.1").
static bool IsVersionNewer(const std::string& a, const std::string& b)
{
    auto parse = [](const std::string& s) -> std::vector<int> {
        std::vector<int> parts;
        size_t i = 0;
        while (i < s.size())
        {
            while (i < s.size() && (s[i] == '.' || !std::isdigit(static_cast<unsigned char>(s[i])))) i++;
            if (i >= s.size()) break;
            int n = 0;
            while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i])))
                n = n * 10 + (s[i++] - '0');
            parts.push_back(n);
        }
        return parts;
        };
    std::vector<int> va = parse(a), vb = parse(b);
    size_t n = (va.size() >= vb.size()) ? va.size() : vb.size();
    for (size_t i = 0; i < n; i++)
    {
        int na = (i < va.size()) ? va[i] : 0;
        int nb = (i < vb.size()) ? vb[i] : 0;
        if (na != nb) return na > nb;
    }
    return false;
}

static const char* s_lootFilterUpdateUrl = "https://github.com/locbones/D2RLAN-Filters/raw/refs/heads/main/lootfilter.lua";

static void LootFilterUpdateThread()
{
    std::string destPath = std::filesystem::path(lootFile).is_relative()
        ? (std::filesystem::current_path() / lootFile).string()
        : lootFile;
    std::string tempPath = destPath + ".tmp";
    HRESULT hr = URLDownloadToFileA(nullptr, s_lootFilterUpdateUrl, tempPath.c_str(), 0, nullptr);
    if (FAILED(hr))
    {
        s_lootFilterUpdateStatusTime = std::chrono::steady_clock::now();
        s_lootFilterUpdateStatus = "Update failed (download error)";
        s_lootFilterUpdating = false;
        return;
    }
    std::ifstream in(tempPath);
    if (!in)
    {
        s_lootFilterUpdateStatusTime = std::chrono::steady_clock::now();
        s_lootFilterUpdateStatus = "Update failed (could not read)";
        std::filesystem::remove(tempPath);
        s_lootFilterUpdating = false;
        return;
    }
    std::stringstream buf;
    buf << in.rdbuf();
    in.close();
    std::filesystem::remove(tempPath);
    std::string content = buf.str();
    std::regex versionRegex(R"delim(local\s+version\s*=\s*"([^"]*)")delim");
    std::smatch match;
    std::string remoteVersion;
    for (std::sregex_iterator it(content.begin(), content.end(), versionRegex), end; it != end; ++it)
    {
        remoteVersion = (*it)[1].str();
        break;
    }
    if (remoteVersion.empty())
    {
        s_lootFilterUpdateStatusTime = std::chrono::steady_clock::now();
        s_lootFilterUpdateStatus = "Update failed (no version in file)";
        s_lootFilterUpdating = false;
        return;
    }
    std::string currentVersion = g_LootFilterHeader.Version;
    if (!currentVersion.empty() && !IsVersionNewer(remoteVersion, currentVersion))
    {
        s_lootFilterUpdateStatusTime = std::chrono::steady_clock::now();
        s_lootFilterUpdateStatus = "No update available";
        s_lootFilterUpdating = false;
        return;
    }
    std::ofstream out(destPath);
    if (!out || !out.write(content.data(), content.size()))
    {
        s_lootFilterUpdateStatusTime = std::chrono::steady_clock::now();
        s_lootFilterUpdateStatus = "Update failed (could not write)";
        s_lootFilterUpdating = false;
        return;
    }
    out.close();
    g_LootFilterHeader.Version = remoteVersion;
    s_lootFilterNewVersion = remoteVersion;
    s_lootFilterUpdateStatusTime = std::chrono::steady_clock::now();
    s_lootFilterUpdateStatus = "Updated to " + remoteVersion;
    s_lootFilterUpdating = false;
}

// Copies a filter's config from My Filters or mod D2RLAN/filters to lootfilter_config.lua (tries My Filters first).
// filterName is a directory name or .lua base name.
static bool CopyModFilterToActive(const std::string& filterName)
{
    std::string destPath = GetLootFilterConfigPath();
    namespace fs = std::filesystem;
    fs::path dest(destPath);
    auto tryDir = [&](const std::string& dir) -> bool {
        if (dir.empty() || !fs::exists(dir)) return false;
        fs::path base(dir);
        auto copyIfExists = [&](const fs::path& src) {
            if (fs::exists(src)) { fs::copy_file(src, dest, fs::copy_options::overwrite_existing); return true; }
            return false;
            };
        fs::path sub = base / filterName;
        fs::path subConfig = base / (filterName + "_config.lua");
        fs::path subLua = base / (filterName + ".lua");
        if (fs::is_directory(sub) && copyIfExists(sub / "lootfilter_config.lua")) return true;
        if (copyIfExists(subConfig)) return true;
        if (copyIfExists(subLua)) return true;
        return false;
        };
    if (tryDir(GetModFiltersDir())) return true;
    return tryDir(GetModD2RLANFiltersDir());
}

// Sanitize a string to a valid filter filename stem (alphanumeric + underscore).
static std::string SanitizeFilterName(std::string s)
{
    for (size_t i = 0; i < s.size(); i++)
    {
        if (s[i] == ' ' || s[i] == '\t') s[i] = '_';
        else if (!std::isalnum(static_cast<unsigned char>(s[i])) && s[i] != '_')
        {
            s.erase(i, 1);
            i--;
        }
    }
    while (!s.empty() && (s.front() == '_' || s.front() == ' ')) s.erase(0, 1);
    while (!s.empty() && (s.back() == '_' || s.back() == ' ')) s.pop_back();
    if (s.empty()) s = "my_filter";
    return s;
}

static void DrawCreateFilterPopup()
{
    if (!s_showCreateFilterPopup) return;
    if (s_createFilterPopupJustOpened)
    {
        ImGui::OpenPopup("Create your own filter");
        s_createFilterPopupJustOpened = false;
    }
    ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_FirstUseEver);
    if (ImGui::BeginPopupModal("Create your own filter", &s_showCreateFilterPopup, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextWrapped("Fill in the following. These will appear as comments at the top of your filter.");
        ImGui::Spacing();
        ImGui::Text("Title - Name of your filter");
        ImGui::SetNextItemWidth(-1.0f);
        BeginFrameBgImageRegion();
        ImGui::InputText("##create_title", s_createFilterTitleBuf, sizeof(s_createFilterTitleBuf));
        EndFrameBgImageRegion();
        ImGui::Spacing();
        ImGui::Text("Type - Earlygame, Endgame, etc.");
        ImGui::SetNextItemWidth(-1.0f);
        BeginFrameBgImageRegion();
        ImGui::InputText("##create_type", s_createFilterTypeBuf, sizeof(s_createFilterTypeBuf));
        EndFrameBgImageRegion();
        ImGui::Spacing();
        ImGui::Text("Description - What your filter focuses on");
        ImGui::SetNextItemWidth(-1.0f);
        BeginFrameBgImageRegion();
        ImGui::InputText("##create_desc", s_createFilterDescBuf, sizeof(s_createFilterDescBuf), ImGuiInputTextFlags_None);
        EndFrameBgImageRegion();
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        if (ThemeButton("Create", ImVec2(100.0f, 0.0f)))
        {
            std::string title(s_createFilterTitleBuf);
            std::string type(s_createFilterTypeBuf);
            std::string desc(s_createFilterDescBuf);
            while (!title.empty() && (title.back() == ' ' || title.back() == '\t')) title.pop_back();
            while (!title.empty() && (title.front() == ' ' || title.front() == '\t')) title.erase(0, 1);
            while (!type.empty() && (type.back() == ' ' || type.back() == '\t')) type.pop_back();
            while (!type.empty() && (type.front() == ' ' || type.front() == '\t')) type.erase(0, 1);
            while (!desc.empty() && (desc.back() == ' ' || desc.back() == '\t' || desc.back() == '\n')) desc.pop_back();
            while (!desc.empty() && (desc.front() == ' ' || desc.front() == '\t' || desc.front() == '\n')) desc.erase(0, 1);
            if (title.empty()) title = "No Filter";
            if (type.empty()) type = "(None)";
            if (desc.empty()) desc = "This is not a filter, it applies no changes.";
            std::string content = "--- Filter Title: " + title + "\n";
            content += "--- Filter Type: " + type + "\n";
            content += "--- Filter Description: " + desc + "\n";
            content += "return {\n";
            content += "    allowOverrides = true,\n";
            content += "    rules = {\n\n";
            content += "    }\n";
            content += "}\n";
            std::string destPath = GetLootFilterConfigPathAbsolute();
            std::error_code ec;
            std::filesystem::create_directories(std::filesystem::path(destPath).parent_path(), ec);
            std::ofstream out(destPath);
            if (out && out.write(content.data(), content.size()))
            {
                out.close();
                std::string filterName = SanitizeFilterName(title);
                std::string myFiltersDir = GetModFiltersDir();
                if (!myFiltersDir.empty())
                {
                    std::filesystem::create_directories(myFiltersDir, ec);
                    std::string configPath = (std::filesystem::path(myFiltersDir) / (filterName + "_config.lua")).string();
                    std::ofstream out2(configPath);
                    if (out2) out2.write(content.data(), content.size());
                }
                s_activeFilterInternalName = filterName;
                s_pendingFilter.clear();
                LoadLootFilterConfig(GetLootFilterConfigPath());
                LoadLootFilterLogic(lootFile);
            }
            s_showCreateFilterPopup = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ThemeButton("Cancel", ImVec2(100.0f, 0.0f)))
        {
            s_showCreateFilterPopup = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void LoadLootFilterConfig(const std::string& path)
{
    g_LuaVariables.clear();
    g_LuaVariableComments.clear();
    g_LootFilterHeader = {}; // reset
    g_LootFilterRules.clear();

    std::ifstream file(path);
    if (!file.is_open())
        return;

    std::string line;
    int nestingLevel = 0; // track { } nesting
    LootFilterRule currentRule;
    std::string pendingRuleComment;   // comment text from line before opening {
    std::string pendingRuleCommentLine; // full line for rawLua
    std::regex assignRegex(R"(^\s*([a-zA-Z_][a-zA-Z0-9_]*)\s*=\s*(.+?),?\s*$)");

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

        int levelAtStart = nestingLevel;

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

        // --- Parse rules block (nesting level 2 = inside rules = {, level 3 = inside a rule {}) ---
        if (levelAtStart == 2)
        {
            std::string trimmed = line;
            size_t start = trimmed.find_first_not_of(" \t");
            if (start != std::string::npos && trimmed[start] == '{')
            {
                currentRule = {};
                size_t commentPos = line.find("--", start + 1);
                if (commentPos != std::string::npos)
                {
                    std::string sameLine = line.substr(commentPos + 2);
                    size_t cstart = sameLine.find_first_not_of(" \t-");
                    if (cstart != std::string::npos) sameLine = sameLine.substr(cstart);
                    if (!sameLine.empty())
                    {
                        currentRule.comment = sameLine;
                        currentRule.rawLua = line + "\n";
                    }
                    else
                    {
                        currentRule.rawLua = line + "\n";
                    }
                    pendingRuleComment.clear();
                    pendingRuleCommentLine.clear();
                }
                else
                {
                    currentRule.rawLua = pendingRuleCommentLine.empty() ? (line + "\n") : (pendingRuleCommentLine + "\n" + line + "\n");
                    if (!pendingRuleComment.empty())
                    {
                        currentRule.comment = pendingRuleComment;
                        pendingRuleComment.clear();
                        pendingRuleCommentLine.clear();
                    }
                }
            }
            else if (start != std::string::npos && trimmed.size() >= 2 && trimmed[start] == '-' && trimmed[start + 1] == '-')
            {
                pendingRuleCommentLine = line;
                pendingRuleComment = trimmed.substr(start + 2);
                size_t cstart = pendingRuleComment.find_first_not_of(" \t-");
                if (cstart != std::string::npos) pendingRuleComment = pendingRuleComment.substr(cstart);
                else pendingRuleComment.clear();
            }
            else if (start != std::string::npos && trimmed[start] == '}')
            {
                pendingRuleComment.clear();
                pendingRuleCommentLine.clear();
            }
        }
        else if (levelAtStart == 3)
        {
            currentRule.rawLua += line + "\n";
            std::string keyValuePart = line;
            size_t commentPos = line.find("--");
            if (commentPos != std::string::npos)
                keyValuePart = line.substr(0, commentPos);
            std::smatch match;
            if (std::regex_match(keyValuePart, match, assignRegex))
            {
                std::string key = match[1].str();
                std::string value = match[2].str();
                if (!value.empty() && value.front() == '"' && value.back() == '"')
                    value = value.substr(1, value.size() - 2);
                currentRule.fields.push_back({ key, value });
            }
            if (nestingLevel == 2)
            {
                if (!currentRule.rawLua.empty() || !currentRule.fields.empty() || !currentRule.comment.empty())
                    g_LootFilterRules.push_back(currentRule);
                currentRule = {};
            }
        }
    }
}

static std::string FormatLuaValue(const std::string& value)
{
    if (value == "true" || value == "false")
        return value;
    if (!value.empty() && value.find_first_not_of("0123456789.-") == std::string::npos)
        return value;
    if (!value.empty() && value.front() == '{' && value.back() == '}')
        return value;  // table literal
    std::string escaped;
    escaped.reserve(value.size() + 8);
    for (char c : value)
    {
        if (c == '\\') escaped += "\\\\";
        else if (c == '"') escaped += "\\\"";
        else escaped += c;
    }
    return "\"" + escaped + "\"";
}

void SaveLootFilterConfig(const std::string& path)
{
    std::ifstream in(path);
    if (!in.is_open())
        return;
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line))
        lines.push_back(line);
    in.close();

    std::regex assignRegex(R"(^\s*([a-zA-Z_][a-zA-Z0-9_]*)\s*=\s*(.+?),?\s*$)");
    std::regex rulesLineRegex(R"(^\s*rules\s*=\s*\{)");
    int nestingLevel = 0;
    std::unordered_set<std::string> writtenKeys;
    size_t lastLevel1AssignLineIndex = 0;   // last level-1 line that is key = value (not "}" or "rules = {")
    size_t firstRulesLineIndex = (size_t)-1;
    size_t rulesEndLineIndex = (size_t)-1;
    std::string defaultIndent = "    ";

    for (size_t i = 0; i < lines.size(); i++)
    {
        int levelBefore = nestingLevel;
        for (char c : lines[i])
        {
            if (c == '{') nestingLevel++;
            if (c == '}') nestingLevel--;
        }
        if (firstRulesLineIndex != (size_t)-1 && levelBefore == 2 && nestingLevel == 1)
            rulesEndLineIndex = i;
        if (levelBefore == 1)
        {
            std::string keyValuePart = lines[i];
            size_t commentPos = lines[i].find("--");
            if (commentPos != std::string::npos)
                keyValuePart = lines[i].substr(0, commentPos);
            if (firstRulesLineIndex == (size_t)-1 && std::regex_search(keyValuePart, rulesLineRegex))
                firstRulesLineIndex = i;
            std::smatch match;
            if (std::regex_match(keyValuePart, match, assignRegex))
            {
                lastLevel1AssignLineIndex = i;
                if (defaultIndent == "    ")
                    defaultIndent = keyValuePart.substr(0, match.position(1));
                std::string key = match[1].str();
                auto it = g_LuaVariables.find(key);
                if (it != g_LuaVariables.end())
                {
                    std::string value = it->second;
                    std::string outValue = FormatLuaValue(value);
                    std::string indent = keyValuePart.substr(0, match.position(1));
                    std::string newLine = indent + key + " = " + outValue + ",";
                    auto cit = g_LuaVariableComments.find(key);
                    if (cit != g_LuaVariableComments.end() && !cit->second.empty())
                        newLine += "  -- " + cit->second;
                    lines[i] = newLine;
                    writtenKeys.insert(key);
                }
            }
        }
    }

    // Add any global options that exist in g_LuaVariables but were not in the file.
    // Insert them above the first "rules" line so they stay with other global options.
    static const std::vector<std::string> preferredKeyOrder = {
        "allowOverrides", "modTips", "Debug", "audioPlayback",
        "reload", "audioVoice", "filter_level", "language", "filter_titles"
    };
    std::unordered_set<std::string> unwritten;
    for (const auto& p : g_LuaVariables)
        if (writtenKeys.find(p.first) == writtenKeys.end())
            unwritten.insert(p.first);
    std::vector<std::string> keysToAdd;
    for (const auto& k : preferredKeyOrder)
    {
        auto it = unwritten.find(k);
        if (it != unwritten.end()) { keysToAdd.push_back(*it); unwritten.erase(it); }
    }
    for (const auto& k : unwritten)
        keysToAdd.push_back(k);

    size_t insertIndex = (firstRulesLineIndex != (size_t)-1) ? firstRulesLineIndex : (lastLevel1AssignLineIndex + 1);
    for (size_t j = 0; j < keysToAdd.size(); j++)
    {
        const std::string& key = keysToAdd[j];
        std::string value = g_LuaVariables.at(key);
        std::string outValue = FormatLuaValue(value);
        std::string newLine = defaultIndent + key + " = " + outValue + ",";
        auto cit = g_LuaVariableComments.find(key);
        if (cit != g_LuaVariableComments.end() && !cit->second.empty())
            newLine += "  -- " + cit->second;
        lines.insert(lines.begin() + insertIndex + j, newLine);
    }
    if (firstRulesLineIndex != (size_t)-1) firstRulesLineIndex += keysToAdd.size();
    if (rulesEndLineIndex != (size_t)-1) rulesEndLineIndex += keysToAdd.size();

    // Build rules block content from g_LootFilterRules (use rawLua when present, else build from fields).
    // Re-apply rule/field indentation so edited content (which may have had spacing stripped in the UI) matches the original file style.
    auto BuildRulesLines = [&defaultIndent]() -> std::vector<std::string>
        {
            std::string ruleIndent = defaultIndent + "    ";
            std::string fieldIndent = ruleIndent + "    ";
            auto trimLead = [](std::string s) {
                size_t start = s.find_first_not_of(" \t");
                return start != std::string::npos ? s.substr(start) : s;
                };
            std::vector<std::string> out;
            out.push_back(defaultIndent + "rules = {");
            for (size_t r = 0; r < g_LootFilterRules.size(); r++)
            {
                const auto& rule = g_LootFilterRules[r];
                if (!rule.rawLua.empty())
                {
                    std::vector<std::string> ruleLines;
                    size_t pos = 0;
                    while (pos < rule.rawLua.size())
                    {
                        size_t next = rule.rawLua.find('\n', pos);
                        if (next == std::string::npos) next = rule.rawLua.size();
                        ruleLines.push_back(rule.rawLua.substr(pos, next - pos));
                        pos = next + 1;
                    }
                    for (size_t i = 0; i < ruleLines.size(); i++)
                    {
                        std::string line = trimLead(ruleLines[i]);
                        bool isRuleLevel = (i == 0 || i == ruleLines.size() - 1 || line == "{" || line == "}");
                        if (isRuleLevel)
                            out.push_back(ruleIndent + line);
                        else
                            out.push_back(fieldIndent + line);
                    }
                }
                else
                {
                    std::string ruleIndent = "        ";
                    std::string fieldIndent = "            ";
                    std::string commentTrim = rule.comment;
                    size_t cstart = commentTrim.find_first_not_of(" \t");
                    if (cstart != std::string::npos) commentTrim = commentTrim.substr(cstart);
                    if (!commentTrim.empty())
                        out.push_back(ruleIndent + "{ -- " + commentTrim);
                    else
                        out.push_back(ruleIndent + "{");
                    for (size_t f = 0; f < rule.fields.size(); f++)
                    {
                        const std::string& k = rule.fields[f].first;
                        std::string v = rule.fields[f].second;
                        out.push_back(fieldIndent + k + " = " + v + ",");
                    }
                    out.push_back(ruleIndent + (r + 1 < g_LootFilterRules.size() ? "}," : "}"));
                }
            }
            out.push_back(defaultIndent + "}");
            return out;
        };

    // Replace existing rules block in place (only when we found it); never append a second block
    if (!g_LootFilterRules.empty() && firstRulesLineIndex != (size_t)-1 && rulesEndLineIndex != (size_t)-1 && rulesEndLineIndex >= firstRulesLineIndex)
    {
        std::vector<std::string> rulesLines = BuildRulesLines();
        lines.erase(lines.begin() + firstRulesLineIndex, lines.begin() + rulesEndLineIndex + 1);
        for (size_t r = 0; r < rulesLines.size(); r++)
            lines.insert(lines.begin() + firstRulesLineIndex + r, rulesLines[r]);
    }

    std::ofstream out(path);
    if (!out.is_open())
        return;
    for (size_t i = 0; i < lines.size(); i++)
    {
        out << lines[i];
        if (i + 1 < lines.size()) out << "\n";
    }
    out.close();
}

void LoadLootFilterLogic(const std::string& path)
{
    auto tryLoadVersion = [](std::ifstream& file) {
        std::string line;
        std::regex versionRegex(R"delim(local\s+version\s*=\s*"([^"]*)")delim");
        while (std::getline(file, line))
        {
            std::smatch match;
            if (std::regex_search(line, match, versionRegex))
            {
                g_LootFilterHeader.Version = match[1].str();
                return true;
            }
        }
        return false;
        };

    std::ifstream file(path);
    if (file.is_open() && tryLoadVersion(file))
        return;
    file.close();
    g_LootFilterHeader.Version.clear();
    if (path != lootFile)
    {
        file.open(lootFile);
        if (file.is_open())
            tryLoadVersion(file);
    }
}

static void ApplyUITheme(const std::string& themeName);  // implemented later
static std::vector<ImVec4> GetCurrentThemeColors();     // implemented later
static bool ShouldDrawWindowBackgroundImage(WindowBgId id);  // implemented later
static void DrawWindowBackgroundImage(WindowBgId id);       // implemented later
static bool ShouldDrawFrameBgImage();                   // implemented later
static void BeginFrameBgImageRegion();                  // implemented later
static void EndFrameBgImageRegion();                    // implemented later

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
        settings.sunderedMonUMods = d2rHUDConfig.SunderedMonUMods;
        settings.SunderValue = d2rHUDConfig.SunderValue;
        d2rHUDConfig.MinionEquality = j.value("MinionEquality", d2rHUDConfig.MinionEquality);
        d2rHUDConfig.GambleCostControl = j.value("GambleCostControl", d2rHUDConfig.GambleCostControl);
        d2rHUDConfig.CombatLog = j.value("CombatLog", d2rHUDConfig.CombatLog);
        d2rHUDConfig.TransmogVisuals = j.value("TransmogVisuals", d2rHUDConfig.TransmogVisuals);
        d2rHUDConfig.ExtendedItemcodes = j.value("ExtendedItemcodes", d2rHUDConfig.ExtendedItemcodes);
        d2rHUDConfig.FloatingDamage = j.value("FloatingDamage", d2rHUDConfig.FloatingDamage);
        FloatingDamage::LoadFromJson(j);
        d2rHUDConfig.FloatingDamage = FloatingDamage::GetConfig().enabled;
        settings.FloatingDamage = d2rHUDConfig.FloatingDamage;
        cachedSettings.FloatingDamage = d2rHUDConfig.FloatingDamage;
        d2rHUDConfig.ChatSounds = j.value("ChatSounds", d2rHUDConfig.ChatSounds);
        d2rHUDConfig.ChatSoundPath = j.value("ChatSoundPath", d2rHUDConfig.ChatSoundPath);
        InvalidateChatSoundPathCache();

        if (j.contains("DLLsToLoad"))
            d2rHUDConfig.DLLsToLoad = j["DLLsToLoad"].get<std::vector<std::string>>();

        // Camera presets (optional)
        if (j.contains("CameraPresets") && j["CameraPresets"].is_array())
        {
            auto& arr = j["CameraPresets"];
            size_t count = std::min<size_t>(arr.size(), d2rHUDConfig.CameraPresets.size());
            for (size_t i = 0; i < count; ++i)
            {
                const auto& pj = arr[i];
                CameraPreset preset;
                preset.Name = pj.value("Name", std::string{});
                preset.Pitch = pj.value("Pitch", 0.0f);
                preset.Height = pj.value("Height", 0.0f);
                preset.Pan = pj.value("Pan", 0.0f);
                preset.Roll = pj.value("Roll", 0.0f);
                preset.Zoom = pj.value("Zoom", 0.0f);
                preset.HasValues = pj.value("HasValues", true);
                d2rHUDConfig.CameraPresets[i] = preset;
            }
        }
        d2rHUDConfig.CameraAutoloadLastPreset = j.value("CameraAutoloadLastPreset", d2rHUDConfig.CameraAutoloadLastPreset);
        d2rHUDConfig.CameraLastPresetIndex = j.value("CameraLastPresetIndex", d2rHUDConfig.CameraLastPresetIndex);

        if (j.contains("UIThemePresets") && j["UIThemePresets"].is_array())
        {
            s_UIThemePresets.clear();
            for (const auto& item : j["UIThemePresets"])
            {
                std::string name = item.value("Name", std::string{});
                std::vector<ImVec4> colors;
                if (item.contains("Colors") && item["Colors"].is_array())
                {
                    for (const auto& c : item["Colors"])
                    {
                        if (c.is_array() && c.size() >= 4)
                            colors.push_back(ImVec4((float)c[0], (float)c[1], (float)c[2], (float)c[3]));
                    }
                }
                if (!name.empty() && (int)colors.size() == s_ThemeColorCount)
                    s_UIThemePresets.push_back({ name, colors });
            }
        }
        if (j.contains("UICustomColors") && j["UICustomColors"].is_array())
        {
            s_UICustomColors.clear();
            for (const auto& c : j["UICustomColors"])
            {
                if (c.is_array() && c.size() >= 4)
                    s_UICustomColors.push_back(ImVec4((float)c[0], (float)c[1], (float)c[2], (float)c[3]));
            }
            if ((int)s_UICustomColors.size() == 20)
            {
                std::vector<ImVec4> migrated;
                migrated.reserve(18);
                for (int i = 0; i < 20; ++i)
                    if (i != 3 && i != 4) migrated.push_back(s_UICustomColors[i]);
                s_UICustomColors = std::move(migrated);
            }
            if ((int)s_UICustomColors.size() == 18)
            {
                std::vector<ImVec4> migrated;
                migrated.reserve(14);
                for (int i = 0; i < 18; ++i)
                    if (i != 7 && i != 8 && i != 16 && i != 17) migrated.push_back(s_UICustomColors[i]);
                s_UICustomColors = std::move(migrated);
            }
            // Insert Check Mark color at index 7 when upgrading from 16 to 17 theme entries
            if ((int)s_UICustomColors.size() == 16)
                s_UICustomColors.insert(s_UICustomColors.begin() + 7, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
            // Remove Window Bg (index 2) when migrating from 17 to 16 theme entries (per-window bg instead)
            if ((int)s_UICustomColors.size() == 17)
                s_UICustomColors.erase(s_UICustomColors.begin() + 2);
            // Insert Checkbox, Checkbox Hovered, Checkbox Active at index 7 when upgrading from 14 to 17
            if ((int)s_UICustomColors.size() == 14) {
                ImVec4 fb = ImVec4(0.20f, 0.20f, 0.20f, 1.00f), fbh = ImVec4(0.28f, 0.28f, 0.28f, 1.00f), fba = ImVec4(0.32f, 0.32f, 0.32f, 1.00f);
                if (ImGui::GetCurrentContext()) {
                    ImGuiStyle& st = ImGui::GetStyle();
                    fb = st.Colors[ImGuiCol_FrameBg]; fbh = st.Colors[ImGuiCol_FrameBgHovered]; fba = st.Colors[ImGuiCol_FrameBgActive];
                }
                s_UICustomColors.insert(s_UICustomColors.begin() + 7, fba);
                s_UICustomColors.insert(s_UICustomColors.begin() + 7, fbh);
                s_UICustomColors.insert(s_UICustomColors.begin() + 7, fb);
            }
        }
        s_UITheme = j.value("UITheme", s_UITheme);
        {
            bool valid = (s_UITheme == "Default" || s_UITheme == "Golden Sunset" || s_UITheme == "Hell's Embrace" || s_UITheme == "RMD Blue" || s_UITheme == "RMD Red" || s_UITheme == "RMD Purple" || s_UITheme == "RMD Green" || s_UITheme == "RMD Gold" || s_UITheme == "RMD Dark" || s_UITheme == "Custom");
            if (!valid)
                for (const auto& p : s_UIThemePresets)
                    if (p.first == s_UITheme) { valid = true; break; }
            if (!valid)
                s_UITheme = "Default";
        }
        if (j.contains("UIColorImageOverrides") && j["UIColorImageOverrides"].is_array())
        {
            s_UIColorImageOverrides.clear();
            for (const auto& el : j["UIColorImageOverrides"])
                s_UIColorImageOverrides.push_back(el.is_string() ? el.get<std::string>() : "");
            if ((int)s_UIColorImageOverrides.size() == 20)
            {
                std::vector<std::string> migrated;
                migrated.reserve(18);
                for (int i = 0; i < 20; ++i)
                    if (i != 3 && i != 4) migrated.push_back(s_UIColorImageOverrides[i]);
                s_UIColorImageOverrides = std::move(migrated);
            }
            if ((int)s_UIColorImageOverrides.size() == 18)
            {
                std::vector<std::string> migrated;
                migrated.reserve(14);
                for (int i = 0; i < 18; ++i)
                    if (i != 7 && i != 8 && i != 16 && i != 17) migrated.push_back(s_UIColorImageOverrides[i]);
                s_UIColorImageOverrides = std::move(migrated);
            }
            // Insert Check Mark slot (no image) at index 7 when upgrading from 16 to 17 theme entries
            if ((int)s_UIColorImageOverrides.size() == 16)
                s_UIColorImageOverrides.insert(s_UIColorImageOverrides.begin() + 7, "");
            // Insert 3 empty image overrides for Checkbox at index 7 when upgrading from 14 to 17
            if ((int)s_UIColorImageOverrides.size() == 14) {
                s_UIColorImageOverrides.insert(s_UIColorImageOverrides.begin() + 7, "");
                s_UIColorImageOverrides.insert(s_UIColorImageOverrides.begin() + 7, "");
                s_UIColorImageOverrides.insert(s_UIColorImageOverrides.begin() + 7, "");
            }
            // Remove Window Bg (index 2) when migrating from 17 to 16 theme entries; copy old single window bg image to all per-window slots
            if ((int)s_UIColorImageOverrides.size() == 17)
            {
                std::string oldWindowBgImage = (s_UIColorImageOverrides.size() > 2 && !s_UIColorImageOverrides[2].empty()) ? s_UIColorImageOverrides[2] : "";
                s_UIColorImageOverrides.erase(s_UIColorImageOverrides.begin() + 2);
                if (!oldWindowBgImage.empty())
                    for (int w = 0; w < kWindowBgCount; ++w)
                        s_WindowBgImageOverrides[w] = oldWindowBgImage;
            }
            if ((int)s_UIColorImageOverrides.size() != s_ThemeColorCount)
                s_UIColorImageOverrides.resize(s_ThemeColorCount, "");
        }
        else
            s_UIColorImageOverrides.resize(s_ThemeColorCount, "");
        if (j.contains("WindowBgColors") && j["WindowBgColors"].is_array())
        {
            const auto& arr = j["WindowBgColors"];
            int srcCount = (int)arr.size();
            int orderVer = j.contains("WindowBgOrderVersion") ? (int)j["WindowBgOrderVersion"] : 0;
            const int old8ToNew7[] = { 7, 5, 6, 3, 0, 1, 2 };
            const int orderV1_8ToNew7[] = { 0, 1, 2, 4, 5, 6, 7 };
            if (srcCount == 9) {
                for (int w = 0; w < kWindowBgCount && w < 7; ++w) {
                    if (arr[w].is_array() && arr[w].size() >= 4)
                        s_WindowBgColors[w] = ImVec4((float)arr[w][0], (float)arr[w][1], (float)arr[w][2], (float)arr[w][3]);
                }
                if (arr[8].is_array() && arr[8].size() >= 4)
                    s_WindowBgColors[kWindowBg_Camera] = ImVec4((float)arr[8][0], (float)arr[8][1], (float)arr[8][2], (float)arr[8][3]);
            }
            else if (srcCount == 8 && orderVer < 2) {
                const int* map = (orderVer == 0) ? old8ToNew7 : orderV1_8ToNew7;
                for (int w = 0; w < kWindowBgCount; ++w) {
                    int o = map[w];
                    if (arr[o].is_array() && arr[o].size() >= 4)
                        s_WindowBgColors[w] = ImVec4((float)arr[o][0], (float)arr[o][1], (float)arr[o][2], (float)arr[o][3]);
                }
            }
            else {
                for (int w = 0; w < kWindowBgCount && w < srcCount; ++w) {
                    if (arr[w].is_array() && arr[w].size() >= 4)
                        s_WindowBgColors[w] = ImVec4((float)arr[w][0], (float)arr[w][1], (float)arr[w][2], (float)arr[w][3]);
                }
            }
        }
        if (j.contains("WindowBgImageOverrides") && j["WindowBgImageOverrides"].is_array())
        {
            const auto& arr = j["WindowBgImageOverrides"];
            int srcCount = (int)arr.size();
            int orderVer = j.contains("WindowBgOrderVersion") ? (int)j["WindowBgOrderVersion"] : 0;
            const int old8ToNew7[] = { 7, 5, 6, 3, 0, 1, 2 };
            const int orderV1_8ToNew7[] = { 0, 1, 2, 4, 5, 6, 7 };
            if (srcCount == 9) {
                for (int w = 0; w < kWindowBgCount && w < 7; ++w)
                    s_WindowBgImageOverrides[w] = arr[w].is_string() ? arr[w].get<std::string>() : "";
                if (arr[8].is_string())
                    s_WindowBgImageOverrides[kWindowBg_Camera] = arr[8].get<std::string>();
            }
            else if (srcCount == 8 && orderVer < 2) {
                const int* map = (orderVer == 0) ? old8ToNew7 : orderV1_8ToNew7;
                for (int w = 0; w < kWindowBgCount; ++w) {
                    int o = map[w];
                    s_WindowBgImageOverrides[w] = arr[o].is_string() ? arr[o].get<std::string>() : "";
                }
            }
            else {
                for (int w = 0; w < kWindowBgCount && w < srcCount; ++w)
                    s_WindowBgImageOverrides[w] = arr[w].is_string() ? arr[w].get<std::string>() : "";
            }
        }
        if (ImGui::GetCurrentContext() != nullptr)
        {
            if (s_UITheme == "Custom" && (int)s_UICustomColors.size() != s_ThemeColorCount)
            {
                ApplyUITheme("Default");
                s_UICustomColors = GetCurrentThemeColors();
            }
            else
                ApplyUITheme(s_UITheme);
            if (!j.contains("WindowBgColors"))
                for (int w = 0; w < kWindowBgCount; ++w)
                    s_WindowBgColors[w] = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
            for (int w = 0; w < kWindowBgCount; ++w)
                D3D12::RequestWindowBgImageReload(w, s_WindowBgImageOverrides[w]);
            for (int i = 3; i <= 5 && i < (int)s_UIColorImageOverrides.size(); ++i)
                D3D12::RequestFrameBgImageReload(i - 3, (int)s_UIColorImageOverrides.size() > i ? s_UIColorImageOverrides[i] : std::string());
            for (int i = 7; i <= 9 && i < (int)s_UIColorImageOverrides.size(); ++i)
                D3D12::RequestCheckboxImageReload(i - 7, (int)s_UIColorImageOverrides.size() > i ? s_UIColorImageOverrides[i] : std::string());
            for (int i = 10; i <= 12 && i < (int)s_UIColorImageOverrides.size(); ++i)
                D3D12::RequestButtonImageReload(i - 10, (int)s_UIColorImageOverrides.size() > i ? s_UIColorImageOverrides[i] : std::string());
        }

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
        j["ChatSounds"] = d2rHUDConfig.ChatSounds;
        if (!d2rHUDConfig.ChatSoundPath.empty())
            j["ChatSoundPath"] = d2rHUDConfig.ChatSoundPath;
        FloatingDamage::GetConfig().enabled = d2rHUDConfig.FloatingDamage;
        FloatingDamage::SaveToJson(j);
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

        // --- Camera presets ---
        {
            ordered_json presets = ordered_json::array();
            for (const auto& preset : d2rHUDConfig.CameraPresets)
            {
                if (!preset.HasValues && preset.Name.empty())
                    continue;

                ordered_json pj;
                pj["Name"] = preset.Name;
                pj["Pitch"] = preset.Pitch;
                pj["Height"] = preset.Height;
                pj["Pan"] = preset.Pan;
                pj["Roll"] = preset.Roll;
                pj["Zoom"] = preset.Zoom;
                pj["HasValues"] = preset.HasValues;
                presets.push_back(pj);
            }
            if (!presets.empty())
            {
                j["CameraPresets"] = presets;
            }
            else if (existing.contains("CameraPresets"))
            {
                // Preserve existing presets if we don't have new ones yet
                j["CameraPresets"] = existing["CameraPresets"];
            }
        }
        j["CameraAutoloadLastPreset"] = d2rHUDConfig.CameraAutoloadLastPreset;
        j["CameraLastPresetIndex"] = d2rHUDConfig.CameraLastPresetIndex;
        j["UITheme"] = s_UITheme;
        if ((int)s_UIColorImageOverrides.size() != s_ThemeColorCount)
            s_UIColorImageOverrides.resize(s_ThemeColorCount, "");
        {
            ordered_json arr = ordered_json::array();
            for (const auto& s : s_UIColorImageOverrides)
                arr.push_back(s);
            j["UIColorImageOverrides"] = arr;
        }
        {
            ordered_json customArr = ordered_json::array();
            for (const auto& v : s_UICustomColors)
                customArr.push_back(ordered_json::array({ v.x, v.y, v.z, v.w }));
            j["UICustomColors"] = customArr;
        }
        {
            j["WindowBgOrderVersion"] = 2;
            ordered_json winBgArr = ordered_json::array();
            for (int w = 0; w < kWindowBgCount; ++w)
                winBgArr.push_back(ordered_json::array({ s_WindowBgColors[w].x, s_WindowBgColors[w].y, s_WindowBgColors[w].z, s_WindowBgColors[w].w }));
            j["WindowBgColors"] = winBgArr;
        }
        {
            ordered_json winBgImgArr = ordered_json::array();
            for (int w = 0; w < kWindowBgCount; ++w)
                winBgImgArr.push_back(s_WindowBgImageOverrides[w]);
            j["WindowBgImageOverrides"] = winBgImgArr;
        }
        {
            ordered_json presetsArr = ordered_json::array();
            for (const auto& p : s_UIThemePresets)
            {
                ordered_json colorsArr = ordered_json::array();
                for (const auto& v : p.second)
                    colorsArr.push_back(ordered_json::array({ v.x, v.y, v.z, v.w }));
                presetsArr.push_back(ordered_json{ {"Name", p.first}, {"Colors", colorsArr} });
            }
            j["UIThemePresets"] = presetsArr;
        }

        if (existing.contains("MemoryConfigs"))
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
        EnsureCustomCommandSlots();
        ordered_json commands;
            commands["Startup Commands"] = g_StartupCommands;
            commands["Custom Commands"] = ordered_json::array();
        for (const auto& cmd : g_CommandHotkeys)
                commands["Custom Commands"].push_back(ordered_json{ {"Key", cmd.key}, {"Command", cmd.command} });

        if (commands.contains("Startup Commands"))
            outFile << "        \"Startup Commands\": \"" << EscapeJsonStringForConfig(commands["Startup Commands"].get<std::string>()) << "\",\n";
        if (commands.contains("Custom Commands"))
        {
            outFile << "        \"Custom Commands\": [\n";
            bool firstCmd = true;
            for (auto& cmd : commands["Custom Commands"])
            {
                if (!firstCmd) outFile << ",\n";
                firstCmd = false;
                outFile << "            { \"Key\": \"" << EscapeJsonStringForConfig(cmd["Key"].get<std::string>())
                    << "\", \"Command\": \"" << EscapeJsonStringForConfig(cmd["Command"].get<std::string>()) << "\" }";
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

    settings.sunderedMonUMods = d2rHUDConfig.SunderedMonUMods;
    settings.SunderValue = d2rHUDConfig.SunderValue;
    settings.FloatingDamage = d2rHUDConfig.FloatingDamage;
    cachedSettings.FloatingDamage = d2rHUDConfig.FloatingDamage;
    FloatingDamage::GetConfig().enabled = d2rHUDConfig.FloatingDamage;
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

    const bool justOpened = !wasOpen;
    wasOpen = true;

    const bool playerInGame = IsPlayerInGame();
    static bool prevPlayerInGame = false;
    const bool enteredGame = playerInGame && !prevPlayerInGame;

    auto RequestStashScanAfterSave = []()
        {
            g_ForceStashRescan = true;
            // Stash .d2i files are written asynchronously after save; scan too early clears collected state from stale files.
            g_DeferStashScanUntil = ImGui::GetTime() + 0.5;
        };

    if (playerInGame && (justOpened || enteredGame))
    {
        ExecuteDebugCheatFunc("save 1");
        RequestStashScanAfterSave();
    }
    else if (!playerInGame)
        g_ForceStashRescan = false;

    static bool itemsLoaded = false;

    if (!itemsLoaded)
    {
        LoadAllItemData();
        itemsLoaded = true;
    }

    static bool prevStashParseDebug = false;
    static int prevStashPageFilter = 0;
    if (playerInGame && showStashParseDebug && !prevStashParseDebug)
        g_ForceStashRescan = true;
    if (playerInGame && showStashParseDebug && g_StashScanPageFilter != prevStashPageFilter)
        g_ForceStashRescan = true;
    if (!showStashParseDebug)
        g_StashDebugEntries.clear();
    prevStashParseDebug = showStashParseDebug;
    prevStashPageFilter = g_StashScanPageFilter;

    static bool stashScanHardcoreInit = false;
    static bool stashScanLastHardcore = false;
    const bool hardcoreNow = IsHardcore();
    if (playerInGame && stashScanHardcoreInit && hardcoreNow != stashScanLastHardcore)
        g_ForceStashRescan = true;
    stashScanLastHardcore = hardcoreNow;
    stashScanHardcoreInit = true;
    prevPlayerInGame = playerInGame;

    const bool stashScanPending = playerInGame && (g_ForceStashRescan || g_StashScanInProgress);

    float menuScale = GetMenuScaleFactor();
    // ------- Tooltip helper -------
    auto ShowOffsetTooltip = [menuScale](const char* text)
        {
            ImVec2 mousePos = ImGui::GetIO().MousePos;
            ImGui::SetNextWindowPos(ImVec2(mousePos.x + 70.0f * menuScale, mousePos.y), ImGuiCond_Always);
            ImGui::BeginTooltip();
            float tooltipWidth = 600.0f * menuScale;
            ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + tooltipWidth);
            ImGui::TextUnformatted(text);
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        };

    constexpr float kGrailHeightNormal = 500.0f;
    float debugTableH = 0.0f;
    float debugSectionH = 0.0f;
    if (showStashParseDebug)
    {
        const float debugChromeH = 4.0f * menuScale
            + ImGui::GetFrameHeightWithSpacing()
            + ImGui::GetStyle().ItemSpacing.y * 2.0f;
        debugTableH = 300.0f * menuScale;
        const float debugGap = 8.0f * menuScale;
        debugSectionH = debugChromeH + debugTableH + debugGap;
    }

    const float grailBaseH = kGrailHeightNormal + (showStashParseDebug ? debugSectionH / menuScale : 0.0f);
    CenterWindow(ImVec2(850, grailBaseH));
    ImGui::SetNextWindowSize(
        ImVec2(850.0f * menuScale, grailBaseH * menuScale),
        ImGuiCond_Always);

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ShouldDrawWindowBackgroundImage(kWindowBg_Grail) ? ImVec4(0, 0, 0, 0) : s_WindowBgColors[kWindowBg_Grail]);
    PushFontSafe(3);
    const ImGuiWindowFlags grailWindowFlags =
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar;
    if (ImGui::Begin("Grail Tracker", &showGrailMenu, grailWindowFlags))
    {
        if (ShouldDrawWindowBackgroundImage(kWindowBg_Grail))
            DrawWindowBackgroundImage(kWindowBg_Grail);
        DrawWindowTitleAndClose("Grail Tracker", &showGrailMenu);
        PopFontSafe(3);

        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, 6.0f * menuScale));

        // Persistent State
        static int selectedCategory = 0;  // 0 = Sets, 1 = Uniques
        static int selectedType = -1;     // For types later (Goal 4)
        static int selectedSet = -1;      // For future navigation
        static int selectedUnique = -1;
        static char searchBuffer[128] = "";


        // Layout: Left Panel / Right Panel (+ optional debug strip at bottom)
        ImVec2 full = ImGui::GetContentRegionAvail();
        ImVec2 mainAvail = full;
        if (showStashParseDebug)
            mainAvail.y = (full.y > debugSectionH) ? (full.y - debugSectionH) : 0.0f;
        float leftWidth = 240.0f * menuScale;

        // LEFT PANEL
        ImGui::BeginChild("left_panel", ImVec2(leftWidth, mainAvail.y), true);
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
        ImGui::PushItemWidth(leftWidth - 55.0f * menuScale);
        {
            float inputWidth = leftWidth - 55.0f * menuScale;
            float avail = ImGui::GetContentRegionAvail().x;
            ImGui::SetCursorPosX((avail - inputWidth) * 0.5f + ImGui::GetCursorPosX());
        }
        BeginFrameBgImageRegion();
        ImGui::InputText("##Search", searchBuffer, IM_ARRAYSIZE(searchBuffer));
        EndFrameBgImageRegion();
        ImGui::PopItemWidth();
        ImGui::Dummy(ImVec2(0, 5.0f * menuScale));

        // --- Filter --- Centered checkbox ---
        {
            const char* label = "Hide Collected";
            ImVec2 labelSize = ImGui::CalcTextSize(label);
            float checkboxWidth = ImGui::GetFrameHeight();
            float totalWidth = checkboxWidth + 4 + labelSize.x;
            float avail = ImGui::GetContentRegionAvail().x;
            ImGui::SetCursorPosX((avail - totalWidth) * 0.5f + ImGui::GetCursorPosX());
        }
        ThemeCheckbox("Hide Collected", &showCollected);

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
        ThemeCheckbox("Show Excluded", &showExcluded);

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
        ThemeCheckbox("Show Base Codes", &showBaseCodes);
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
        ThemeCheckbox("Show Base Names", &showBaseNames);
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
            ThemeCheckbox(label, &showDuplicates);

            // tooltip
            if (ImGui::IsItemHovered())
                ShowOffsetTooltip("Show duplicate items found in your stash");
        }

        // --- Debug view ---
        {
            const char* label = "Debug View";
            ImVec2 labelSize = ImGui::CalcTextSize(label);
            float checkboxWidth = ImGui::GetFrameHeight();
            float totalWidth = checkboxWidth + 4 + labelSize.x;
            float avail = ImGui::GetContentRegionAvail().x;
            ImGui::SetCursorPosX((avail - totalWidth) * 0.5f + ImGui::GetCursorPosX());
        }
        ThemeCheckbox("Debug View", &showStashParseDebug);
        if (ImGui::IsItemHovered())
            ShowOffsetTooltip("Show parsed stash item codes and set/unique IDs from .d2i files (for troubleshooting grail matching).");

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

        ImGui::PushItemWidth(leftWidth - 55.0f * menuScale);
        {
            float inputWidth = leftWidth - 55;
            float avail = ImGui::GetContentRegionAvail().x;
            ImGui::SetCursorPosX((avail - inputWidth) * 0.5f + ImGui::GetCursorPosX());
        }
        BeginFrameBgImageRegion();
        ImGui::InputText("##BackupPath", backupPath, IM_ARRAYSIZE(backupPath));
        EndFrameBgImageRegion();
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
        ThemeCheckbox("Auto-Backups", &autoBackups);
        ImGui::BeginDisabled(!autoBackups);
        if (ImGui::IsItemHovered()) ShowOffsetTooltip("Enable automatic backups on the specified interval.");

        ImGui::SetCursorPosX(startX);
        if (ThemeCheckbox("Use Timestamps", &backupWithTimestamps))
            if (backupWithTimestamps) overwriteOldBackup = false;
        if (ImGui::IsItemHovered()) ShowOffsetTooltip("Append current date/time to backup filename to avoid overwriting.");

        ImGui::SetCursorPosX(startX);
        if (ThemeCheckbox("Overwrite Old", &overwriteOldBackup))
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
        BeginFrameBgImageRegion();
        ImGui::InputInt("##BackupInterval", &backupIntervalMinutes);
        EndFrameBgImageRegion();
        if (ImGui::IsItemHovered())
            ShowOffsetTooltip("How often to save automatic backups.\nMeasured in minutes.");

        ImGui::EndDisabled();
        ImGui::Dummy(ImVec2(0, 3));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, 3));
        ImGui::EndChild();

        // RIGHT PANEL
        ImGui::SameLine();
        ImGui::BeginChild("right_panel", ImVec2(0, mainAvail.y), true);

        if (!playerInGame)
        {
            const char* offlineMsg = "Enter a game to scan your stash.";
            ImVec2 panelAvail = ImGui::GetContentRegionAvail();
            ImVec2 textSize = ImGui::CalcTextSize(offlineMsg);
            ImGui::SetCursorPos(ImVec2(
                (panelAvail.x - textSize.x) * 0.5f,
                (panelAvail.y - textSize.y) * 0.5f));
            ImGui::PushStyleColor(ImGuiCol_Text, darkRed);
            ImGui::TextUnformatted(offlineMsg);
            ImGui::PopStyleColor();
        }
        else if (stashScanPending)
        {
            const char* scanMsg = "Scanning Stash...";
            ImVec2 panelAvail = ImGui::GetContentRegionAvail();
            ImVec2 textSize = ImGui::CalcTextSize(scanMsg);
            ImGui::SetCursorPos(ImVec2(
                (panelAvail.x - textSize.x) * 0.5f,
                (panelAvail.y - textSize.y) * 0.5f));
            ImGui::PushStyleColor(ImGuiCol_Text, darkRed);
            ImGui::TextUnformatted(scanMsg);
            ImGui::PopStyleColor();
        }
        else
        {

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
                if (ThemeButton(label, ImVec2(0, 0)))
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
                        ThemeCheckbox(label.c_str(), checked);

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

                if (ThemeCheckbox(checkboxID.c_str(), checked))
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

        } // !stashScanPending

        ImGui::EndChild();

        if (playerInGame && showStashParseDebug && !stashScanPending)
        {
            static bool debugOnlyUnmatched = false;
            static bool debugOnlySetUnique = false;

            ImGui::Dummy(ImVec2(0, 4.0f * menuScale));
            ImGui::Separator();

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.1f, 0.1f, 1.0f));
            ImGui::Text("Debug View (%zu items)", g_StashDebugEntries.size());
            ImGui::PopStyleColor();

            ImGui::SameLine();
            if (ThemeButton("Rescan Now"))
            {
                if (playerInGame)
                {
                    ExecuteDebugCheatFunc("save 1");
                    g_DeferStashScanUntil = ImGui::GetTime() + 0.5;
                    g_ForceStashRescan = true;
                }
            }
            if (ImGui::IsItemHovered())
                ShowOffsetTooltip("Re-read stash .d2i pages immediately.");

            const char* pagePreview = (g_StashScanPageFilter == 0) ? "All Pages" : nullptr;
            char pagePreviewBuf[16] = {};
            if (!pagePreview)
            {
                snprintf(pagePreviewBuf, sizeof(pagePreviewBuf), "Page %d", g_StashScanPageFilter);
                pagePreview = pagePreviewBuf;
            }

            ImGui::SetNextItemWidth(140.0f * menuScale);
            if (ImGui::BeginCombo("##StashPageFilter", pagePreview))
            {
                const bool allSelected = (g_StashScanPageFilter == 0);
                if (ImGui::Selectable("All Pages", allSelected))
                {
                    if (!allSelected)
                    {
                        g_StashScanPageFilter = 0;
                        g_ForceStashRescan = true;
                    }
                    ImGui::SetItemDefaultFocus();
                }

                for (int page : g_AvailableStashPages)
                {
                    char label[24];
                    snprintf(label, sizeof(label), "Page %d", page);
                    const bool selected = (g_StashScanPageFilter == page);
                    if (ImGui::Selectable(label, selected))
                    {
                        if (!selected)
                        {
                            g_StashScanPageFilter = page;
                            g_ForceStashRescan = true;
                        }
                    }
                }

                if (g_AvailableStashPages.empty())
                    ImGui::TextDisabled("No stash pages found");

                ImGui::EndCombo();
            }
            if (ImGui::IsItemHovered())
                ShowOffsetTooltip(
                    "All Pages: scan every stash file for grail and debug.\n"
                    "Single page: only parse that page (faster debug; grail reflects that page only).");

            ImGui::SameLine();
            ThemeCheckbox("Only unmatched Set/Unique", &debugOnlyUnmatched);
            ImGui::SameLine();
            ThemeCheckbox("Only Set/Unique quality", &debugOnlySetUnique);

            ImGui::BeginChild("stash_debug_scroll", ImVec2(0, debugTableH), true);
            const ImGuiTableFlags tableFlags =
                ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable |
                ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit;

            if (ImGui::BeginTable("stash_debug_table", 10, tableFlags))
            {
                ImGui::TableSetupColumn("Page");
                ImGui::TableSetupColumn("Tab");
                ImGui::TableSetupColumn("X");
                ImGui::TableSetupColumn("Y");
                ImGui::TableSetupColumn("Code");
                ImGui::TableSetupColumn("Quality");
                ImGui::TableSetupColumn("SetID");
                ImGui::TableSetupColumn("UniqID");
                ImGui::TableSetupColumn("Match");
                ImGui::TableSetupColumn("Grail / Note", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();

                for (const auto& e : g_StashDebugEntries)
                {
                    if (debugOnlyUnmatched && e.grailMatched)
                        continue;
                    if (debugOnlySetUnique && e.quality != 5 && e.quality != 7)
                        continue;

                    ImGui::TableNextRow();

                    ImVec4 rowColor = ImVec4(0.75f, 0.75f, 0.75f, 1.0f);
                    if (e.grailMatched)
                        rowColor = ImVec4(0.45f, 1.0f, 0.55f, 1.0f);
                    else if (e.quality == 5 || e.quality == 7)
                        rowColor = ImVec4(1.0f, 0.85f, 0.35f, 1.0f);

                    ImGui::PushStyleColor(ImGuiCol_Text, rowColor);

                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%d", e.page);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%d", e.tab);
                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%d", e.x);
                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("%d", e.y);
                    ImGui::TableSetColumnIndex(4);
                    ImGui::TextUnformatted(e.code.c_str());
                    ImGui::TableSetColumnIndex(5);
                    ImGui::Text("%u %s", e.quality, GetQualityName(e.quality));
                    ImGui::TableSetColumnIndex(6);
                    if (e.quality == 5)
                        ImGui::Text("%u", e.setId);
                    else
                        ImGui::TextUnformatted("-");
                    ImGui::TableSetColumnIndex(7);
                    if (e.quality == 7)
                        ImGui::Text("%u", e.uniqueId);
                    else
                        ImGui::TextUnformatted("-");
                    ImGui::TableSetColumnIndex(8);
                    ImGui::TextUnformatted(e.grailMatched ? "yes" : "no");
                    ImGui::TableSetColumnIndex(9);
                    if (!e.grailName.empty())
                        ImGui::TextUnformatted(e.grailName.c_str());
                    else
                        ImGui::TextUnformatted(e.note.c_str());

                    ImGui::PopStyleColor();
                }

                ImGui::EndTable();
            }

            ImGui::EndChild();
        }

        if (playerInGame && g_ForceStashRescan && ImGui::GetTime() >= g_DeferStashScanUntil)
            ScanStashPages();

        ImGui::PopStyleColor();
        ImGui::End();
    }
    else
        ImGui::PopStyleColor();
}

void ShowHotkeyMenu()
{
    static bool wasOpen = false;

    if (!showHotkeyMenu)
    {
        // Menu just closed? Save progress including AutoBackup settings
        if (wasOpen)
        {
            RegisterStartupCommandsAfterEdit(g_StartupCommands);
            SaveFullGrailConfig(configFilePath, false);
        }

        wasOpen = false;
        return;
    }
    wasOpen = true;

    if (!showHotkeyMenu) return;

    EnableAllInput();
    ImGuiIO& io = ImGui::GetIO();
    float menuScale = GetMenuScaleFactor();
    ImVec2 windowSize = ImVec2(900, 480);
    CenterWindow(windowSize);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ShouldDrawWindowBackgroundImage(kWindowBg_Hotkeys) ? ImVec4(0, 0, 0, 0) : s_WindowBgColors[kWindowBg_Hotkeys]);
    PushFontSafe(3);
    if (ImGui::Begin("D2R Hotkeys", &showHotkeyMenu, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar))
    {
        if (ShouldDrawWindowBackgroundImage(kWindowBg_Hotkeys))
            DrawWindowBackgroundImage(kWindowBg_Hotkeys);
        DrawWindowTitleAndClose("D2R Hotkeys", &showHotkeyMenu);
        if (GetFont(3)) ImGui::PopFont();
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0.0f, 5.0f * menuScale));

        // --- Split hotkeys (Keybinds ONLY) ---
        std::vector<std::pair<std::string, std::pair<std::string, std::string>>> singleKeys;
        for (auto& [name, pair] : g_Hotkeys)
            singleKeys.push_back({ name, pair });

        static std::string hoveredHotkey;
        hoveredHotkey.clear();

        // --- 2-column layout ---
        ImGui::Columns(2, nullptr, true);
        ImGui::SetColumnWidth(0, 450.0f * menuScale);
        ImGui::SetColumnWidth(1, 450.0f * menuScale);

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
            "Filtered Items Toggle",
            "Cycle TZ Backward",
            "Cycle TZ Forward",
            "Toggle TZ Stats Display",
        };

        // Ensure every standard hotkey has an entry so it appears in the menu (e.g. after config load or new options)
        for (const auto& name : hotkeyOrder)
        {
            if (g_Hotkeys.find(name) == g_Hotkeys.end())
                g_Hotkeys[name] = { "", "" };
        }

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

            bool tzUnavailable = IsTZCycleUnavailable(name);

            ImGui::PushItemWidth(180.0f * menuScale);
            if (inputFont) ImGui::PushFont(inputFont);
            BeginFrameBgImageRegion();
            ImGui::InputText(("##key_" + name).c_str(), buffer, sizeof(buffer), ImGuiInputTextFlags_ReadOnly);
            EndFrameBgImageRegion();

            // Tooltip
            if (ImGui::IsItemHovered() && tzUnavailable)
            {
                ImVec2 mousePos = ImGui::GetMousePos();

                ImGui::SetNextWindowPos(ImVec2(mousePos.x + 80.0f, mousePos.y), ImGuiCond_Always);
                ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(320.0f * GetMenuScaleFactor(), FLT_MAX));

                ImGui::BeginTooltip();
                ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f), "TZ Cycling Disabled!");
                ImGui::Separator();
                ImGui::PushTextWrapPos(300.0f);
                ImGui::TextUnformatted("This hotkey is disabled until you complete a certain task in our mod\n\nI hope you enjoy riddles...");
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }

            if (ImGui::IsItemClicked())
            {
                activeHotkeyInput = name;
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

            EnsureCustomCommandSlots();

            // ---- Custom Commands ----
            for (size_t i = 0; i < g_CommandHotkeys.size(); ++i)
            {
                auto& cmd = g_CommandHotkeys[i];
                const std::string hotkeyId = CustomCommandHotkeyId(i);
                std::string label = "Command " + std::to_string(i + 1);

                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(2, 130, 199, 255));
                ImGui::Text("%s:", label.c_str());
                ImGui::PopStyleColor();
                ImGui::SameLine();

                if (activeHotkeyInput == hotkeyId)
                {
                    auto pressedKeys = GetPressedKeys();
                    if (std::find(pressedKeys.begin(), pressedKeys.end(), "VK_DELETE") != pressedKeys.end())
                    {
                        cmd.key.clear();
                        SaveFullGrailConfig("HUDConfig_" + modName + ".json", false);
                        activeCombo.clear();
                        hotkeyReleased = true;
                        pressedKeys.clear();
                    }
                    if (!pressedKeys.empty())
                        hotkeyReleased = false;
                    if (pressedKeys.empty() && !hotkeyReleased)
                    {
                        if (!activeCombo.empty())
                        {
                            std::string combo;
                            for (size_t j = 0; j < activeCombo.size(); ++j)
                            {
                                combo += activeCombo[j];
                                if (j + 1 < activeCombo.size())
                                    combo += " + ";
                            }
                            if (combo != cmd.key)
                            {
                                cmd.key = combo;
                                SaveFullGrailConfig("HUDConfig_" + modName + ".json", false);
                            }
                        }
                        activeCombo.clear();
                        hotkeyReleased = true;
                    }
                    else if (!pressedKeys.empty())
                    {
                        for (auto& k : pressedKeys)
                        {
                            if (k != "VK_DELETE" && std::find(activeCombo.begin(), activeCombo.end(), k) == activeCombo.end())
                                activeCombo.push_back(k);
                        }
                    }
                }

                char keyBuf[128];
                strncpy(keyBuf, DisplayKey(cmd.key).c_str(), sizeof(keyBuf));
                keyBuf[sizeof(keyBuf) - 1] = '\0';

                ImGui::PushItemWidth(88.0f * menuScale);
                if (inputFont) ImGui::PushFont(inputFont);
                BeginFrameBgImageRegion();
                ImGui::InputText(("##cmd_key_" + std::to_string(i)).c_str(), keyBuf, sizeof(keyBuf), ImGuiInputTextFlags_ReadOnly);
                EndFrameBgImageRegion();
                if (inputFont) ImGui::PopFont();
                ImGui::PopItemWidth();

                if (ImGui::IsItemClicked())
                {
                    activeHotkeyInput = hotkeyId;
                    activeCombo.clear();
                    hotkeyReleased = true;
                }
                if (!ImGui::IsItemFocused() && activeHotkeyInput == hotkeyId)
                    activeHotkeyInput.clear();

                ImGui::SameLine();

                char cmdBuf[256];
                strncpy(cmdBuf, cmd.command.c_str(), sizeof(cmdBuf));
                cmdBuf[sizeof(cmdBuf) - 1] = '\0';

                ImGui::PushItemWidth(-1);
                if (inputFont) ImGui::PushFont(inputFont);
                BeginFrameBgImageRegion();
                if (ImGui::InputText(("##cmd_txt_" + std::to_string(i)).c_str(), cmdBuf, sizeof(cmdBuf)))
                    cmd.command = cmdBuf;
                EndFrameBgImageRegion();
                if (inputFont) ImGui::PopFont();
                ImGui::PopItemWidth();

                if (ImGui::IsItemDeactivatedAfterEdit())
                    SaveFullGrailConfig("HUDConfig_" + modName + ".json", false);

                if (ImGui::IsItemHovered())
                    hoveredHotkey = label;
            }

            // ---- Startup Commands ----
            ImGui::Dummy(ImVec2(0.0f, 10.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(2, 130, 199, 255));
            ImGui::Text("Startup Commands");
            ImGui::PopStyleColor();
            ImGui::TextWrapped("Comma-separated. Use [Tab] or [Alt] for a key tap. Changes apply when you click away; if already in-game, commands run again after ~3s.");

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
            BeginFrameBgImageRegion();
            ImGui::InputTextMultiline("##startup_commands", startupBuf, sizeof(startupBuf), ImVec2(-1, 80));
            EndFrameBgImageRegion();
            if (inputFont) ImGui::PopFont();
            ImGui::PopItemWidth();

            if (ImGui::IsItemDeactivatedAfterEdit())
            {
                g_StartupCommands = startupBuf;
                RegisterStartupCommandsAfterEdit(g_StartupCommands);
                SaveFullGrailConfig("HUDConfig_" + modName + ".json", false);
            }

        }

        ImGui::Columns(1);

        std::string desc = hoveredHotkey.empty() ? "Hover over a hotkey to see details." : "Set the hotkey for " + hoveredHotkey + ".";
        DrawBottomDescription(desc);

        ImGui::PopStyleColor();
        ImGui::End();
    }
    else
        ImGui::PopStyleColor();
    io.ConfigFlags = ImGui::GetIO().ConfigFlags;
}

// Camera Controller
void ShowCameraMenu()
{
    static bool wasOpen = false;
    static bool didAutoScanThisOpen = false;

    if (!showCameraMenu)
    {
        wasOpen = false;
        didAutoScanThisOpen = false;
        return;
    }
    bool justOpened = !wasOpen;
    wasOpen = true;
    static bool s_autoloadDoneThisOpen = false;
    static int selectedPreset = 0;
    if (justOpened)
    {
        s_autoloadDoneThisOpen = false;
        // If autoload is enabled, start from the last-used preset.
        // Otherwise always start from Default in the dropdown.
        if (d2rHUDConfig.CameraAutoloadLastPreset)
            selectedPreset = std::clamp(d2rHUDConfig.CameraLastPresetIndex, 0, 3);
        else
            selectedPreset = 0;
    }

    CameraState& camera = g_CameraState;

    // Async scan state: futures and resolved flags live in this scope so we poll on the main thread.
    static std::future<DWORD64> angleScanFuture;
    static std::future<DWORD64> zoomScanFuture;
    static bool anglePatternResolved = false;
    static bool zoomPatternResolved = false;

    auto ScanAngleValues = []()
        {
            CameraState& camera = g_CameraState;
            if (camera.valuesFound)
                return;

            camera.didScanAngles = true;
            camera.lastAngleScanInGame = IsPlayerInGame();
            if (!camera.lastAngleScanInGame)
                return;

            if (anglePatternResolved)
            {
                if (gCameraPitchValue && gCameraHeightValue && gCameraPanValue && gCameraRollValue)
                {
                    camera.pitch = *gCameraPitchValue;
                    camera.height = *gCameraHeightValue;
                    camera.pan = *gCameraPanValue;
                    camera.roll = *gCameraRollValue;
                    camera.defaultPitch = camera.pitch;
                    camera.defaultHeight = camera.height;
                    camera.defaultPan = camera.pan;
                    camera.defaultRoll = camera.roll;
                    camera.valuesFound = true;
                }
                return;
            }

            if (angleScanFuture.valid())
                return;

            angleScanFuture = std::async(std::launch::async, []() {
                return Pattern::ScanProcess("44 74 64 BF 3F DB 74 3E F4 41 BD 3E");
                });
        };

    auto ScanZoomValue = []()
        {
            CameraState& camera = g_CameraState;
            if (camera.zoomFound)
                return;

            camera.didScanZoom = true;
            camera.lastZoomScanInGame = IsPlayerInGame();
            if (!camera.lastZoomScanInGame)
                return;

            if (zoomPatternResolved)
            {
                if (gCameraZoomValue)
                {
                    camera.zoom = *gCameraZoomValue;
                    camera.defaultZoom = camera.zoom;
                    camera.zoomFound = true;
                }
                return;
            }

            if (zoomScanFuture.valid())
                return;

            zoomScanFuture = std::async(std::launch::async, []() {
                DWORD64 base = Pattern::ScanProcess("00 00 06 00 00 00 ? ? ? ? ? ? ? 43 ? ? ? ? ? ? ? ? 00 00 00 00 01 00 00 00 00 00 00 00 00 00 80 3F");
                return base ? (base + 0x32) : 0ull;
                });
        };

    auto ApplyAngleValues = []()
        {
            CameraState& camera = g_CameraState;
            if (!camera.programEnabled || !camera.valuesFound)
                return;

            if (gCameraPitchValue)  *gCameraPitchValue = camera.pitch;
            if (gCameraHeightValue) *gCameraHeightValue = camera.height;
            if (gCameraPanValue)    *gCameraPanValue = camera.pan;
            if (gCameraRollValue)   *gCameraRollValue = camera.roll;
        };

    auto ApplyZoomValue = []()
        {
            CameraState& camera = g_CameraState;
            if (!camera.programEnabled || !camera.zoomFound || !gCameraZoomValue)
                return;

            *gCameraZoomValue = camera.zoom;
        };

    // Auto-scan once when the window is opened (fast enough to start immediately).
    if (!didAutoScanThisOpen)
    {
        didAutoScanThisOpen = true;
        ScanAngleValues();
        ScanZoomValue();
    }

    // Poll async scan results on the main thread and apply when ready
    if (angleScanFuture.valid() && angleScanFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
    {
        DWORD64 pitchAddr = angleScanFuture.get();
        if (pitchAddr)
        {
            gCameraPitchValue = reinterpret_cast<float*>(pitchAddr);
            gCameraHeightValue = reinterpret_cast<float*>(pitchAddr + 0x4);
            gCameraPanValue = reinterpret_cast<float*>(pitchAddr + 0x8);
            gCameraRollValue = reinterpret_cast<float*>(pitchAddr - 0x4);
            anglePatternResolved = true;

            CameraState& cam = g_CameraState;
            cam.pitch = *gCameraPitchValue;
            cam.height = *gCameraHeightValue;
            cam.pan = *gCameraPanValue;
            cam.roll = *gCameraRollValue;
            cam.defaultPitch = cam.pitch;
            cam.defaultHeight = cam.height;
            cam.defaultPan = cam.pan;
            cam.defaultRoll = cam.roll;
            cam.valuesFound = true;
        }
    }

    if (zoomScanFuture.valid() && zoomScanFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
    {
        DWORD64 zoomAddr = zoomScanFuture.get();
        if (zoomAddr)
        {
            gCameraZoomValue = reinterpret_cast<float*>(zoomAddr);
            zoomPatternResolved = true;

            CameraState& cam = g_CameraState;
            cam.zoom = *gCameraZoomValue;
            cam.defaultZoom = cam.zoom;
            cam.zoomFound = true;
        }
    }

    const bool angleScanning = angleScanFuture.valid() && angleScanFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready;
    const bool zoomScanning = zoomScanFuture.valid() && zoomScanFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready;

    // Only autoload after both scans have completed and default values are captured
    if (!s_autoloadDoneThisOpen
        && d2rHUDConfig.CameraAutoloadLastPreset
        && d2rHUDConfig.CameraLastPresetIndex >= 1
        && d2rHUDConfig.CameraLastPresetIndex <= 3
        && !angleScanning && !zoomScanning
        && camera.valuesFound && camera.zoomFound)
    {
        const CameraPreset& p = d2rHUDConfig.CameraPresets[d2rHUDConfig.CameraLastPresetIndex - 1];
        if (p.HasValues)
        {
            camera.pitch = p.Pitch;
            camera.height = p.Height;
            camera.pan = p.Pan;
            camera.roll = p.Roll;
            camera.zoom = p.Zoom;

            ApplyAngleValues();
            ApplyZoomValue();
        }

        // Either we applied a preset or there was nothing to apply;
        s_autoloadDoneThisOpen = true;
    }

    EnableAllInput();
    float menuScale = GetMenuScaleFactor();
    ImVec2 windowSize = ImVec2(680.0f * menuScale, 380.0f * menuScale);
    CenterWindow(windowSize);

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ShouldDrawWindowBackgroundImage(kWindowBg_Camera) ? ImVec4(0, 0, 0, 0) : s_WindowBgColors[kWindowBg_Camera]);
    PushFontSafe(3);
    if (ImGui::Begin("Camera Controls", &showCameraMenu, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar))
    {
        if (ShouldDrawWindowBackgroundImage(kWindowBg_Camera))
            DrawWindowBackgroundImage(kWindowBg_Camera);
        DrawWindowTitleAndClose("Camera Controls", &showCameraMenu);
        PopFontSafe(3);

        ImGui::Separator();
        ImGui::Dummy(ImVec2(0.0f, 4.0f * menuScale));

        // Top info line
        PushFontSafe(1);
        ImGuiTextCentered("Adjust in-game camera pitch, height, pan, roll, and zoom.");
        ImGuiTextCentered("Camera values are scanned automatically when you open this window.");
        PopFontSafe(1);

        ImGui::Dummy(ImVec2(0.0f, 4.0f * menuScale));

        // Status text (auto-scan runs on open)
        ImVec4 okColor = ImVec4(0.4f, 0.9f, 0.4f, 1.0f);
        ImVec4 warnColor = ImVec4(0.9f, 0.7f, 0.2f, 1.0f);
        ImVec4 errColor = ImVec4(0.9f, 0.3f, 0.3f, 1.0f);

        ImVec4 angleColor = camera.valuesFound ? okColor : (camera.didScanAngles ? errColor : warnColor);
        ImVec4 zoomColor = camera.zoomFound ? okColor : (camera.didScanZoom ? errColor : warnColor);

        const char* angleStatus = angleScanning ? "scanning..."
            : (camera.valuesFound ? "found" : (camera.didScanAngles ? "not found (pattern missing)" : "not scanned"));
        const char* zoomStatus = zoomScanning ? "scanning..."
            : (camera.zoomFound ? "found" : (camera.didScanZoom ? "not found (pattern missing)" : "not scanned"));

        char statusBuf[96];
        snprintf(statusBuf, sizeof(statusBuf), "Angles: %s  Zoom: %s", angleStatus, zoomStatus);
        float statusWidth = ImGui::CalcTextSize(statusBuf).x;
        float avail = ImGui::GetContentRegionAvail().x;
        const float statusOffset = 8.0f * menuScale;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - statusWidth) * 0.5f + statusOffset);
        ImGui::TextColored(angleColor, "Angles: %s", angleStatus);
        ImGui::SameLine();
        ImGui::TextColored(zoomColor, "Zoom: %s", zoomStatus);

        ImGui::Dummy(ImVec2(0.0f, 6.0f * menuScale));

        const float cameraLabelWidth = 90.0f * menuScale;
        const float cameraInputWidth = 200.0f * menuScale;
        const float presetColWidth = 350.0f * menuScale;

        // Left: Presets | Right: All values + reset buttons
        ImGui::BeginChild("CameraPresets", ImVec2(presetColWidth, 0), true);
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.3f, 1.0f), "Presets");
        ImGui::Separator();

        static char presetNameBuf[64] = {};
        static int lastPresetIndex = -1;
        if (lastPresetIndex != selectedPreset)
        {
            CameraPreset p{};
            if (selectedPreset > 0 && selectedPreset <= static_cast<int>(d2rHUDConfig.CameraPresets.size()))
            {
                p = d2rHUDConfig.CameraPresets[selectedPreset - 1];

                // When switching to a saved preset, update the input values to match it
                if (p.HasValues)
                {
                    camera.pitch = p.Pitch;
                    camera.height = p.Height;
                    camera.pan = p.Pan;
                    camera.roll = p.Roll;
                    camera.zoom = p.Zoom;
                }
            }
            else if (selectedPreset == 0 && camera.valuesFound && camera.zoomFound)
            {
                // Default slot: show the captured in-game defaults in the inputs
                camera.pitch = camera.defaultPitch;
                camera.height = camera.defaultHeight;
                camera.pan = camera.defaultPan;
                camera.roll = camera.defaultRoll;
                camera.zoom = camera.defaultZoom;
            }

            strncpy(presetNameBuf, p.Name.c_str(), sizeof(presetNameBuf));
            presetNameBuf[sizeof(presetNameBuf) - 1] = '\0';
            lastPresetIndex = selectedPreset;
        }

        static char presetLabel0[64] = "Default";
        static char presetLabel1[64] = "Preset 1";
        static char presetLabel2[64] = "Preset 2";
        static char presetLabel3[64] = "Preset 3";
        for (int i = 0; i < 3; ++i)
        {
            char* buf = (i == 0) ? presetLabel1 : (i == 1) ? presetLabel2 : presetLabel3;
            const CameraPreset& p = d2rHUDConfig.CameraPresets[i];
            if (!p.Name.empty())
            {
                strncpy(buf, p.Name.c_str(), sizeof(presetLabel1) - 1);
                buf[sizeof(presetLabel1) - 1] = '\0';
            }
            else
            {
                snprintf(buf, sizeof(presetLabel1), "Preset %d", i + 1);
            }
        }
        const char* presetLabels[4] = { presetLabel0, presetLabel1, presetLabel2, presetLabel3 };
        ImGui::Text("Slot");
        ImGui::SameLine(cameraLabelWidth);
        ImGui::SetNextItemWidth(-1);
        BeginFrameBgImageRegion();
        ImGui::Combo("##PresetSlot", &selectedPreset, presetLabels, IM_ARRAYSIZE(presetLabels));
        EndFrameBgImageRegion();

        ImGui::Text("Name");
        ImGui::SameLine(cameraLabelWidth);
        ImGui::SetNextItemWidth(-1);
        BeginFrameBgImageRegion();
        ImGui::InputText("##PresetName", presetNameBuf, sizeof(presetNameBuf));
        EndFrameBgImageRegion();

        if (ThemeButton("Save Current to Preset", ImVec2(-1, 0)))
        {
            if (selectedPreset > 0 && selectedPreset <= static_cast<int>(d2rHUDConfig.CameraPresets.size()))
            {
                CameraPreset& p = d2rHUDConfig.CameraPresets[selectedPreset - 1];
                p.Name = presetNameBuf;
                p.Pitch = camera.pitch;
                p.Height = camera.height;
                p.Pan = camera.pan;
                p.Roll = camera.roll;
                p.Zoom = camera.zoom;
                p.HasValues = true;
                SaveFullGrailConfig(configFilePath, false);
            }
        }
        if (ThemeButton("Apply Preset", ImVec2(-1, 0)))
        {
            if (selectedPreset == 0)
            {
                if (camera.valuesFound) {
                    camera.pitch = camera.defaultPitch;
                    camera.height = camera.defaultHeight;
                    camera.pan = camera.defaultPan;
                    camera.roll = camera.defaultRoll;
                    ApplyAngleValues();
                }
                if (camera.zoomFound) {
                    camera.zoom = camera.defaultZoom;
                    ApplyZoomValue();
                }
            }
            else if (selectedPreset > 0 && selectedPreset <= static_cast<int>(d2rHUDConfig.CameraPresets.size()))
            {
                const CameraPreset& p = d2rHUDConfig.CameraPresets[selectedPreset - 1];
                if (p.HasValues)
                {
                    camera.pitch = p.Pitch;
                    camera.height = p.Height;
                    camera.pan = p.Pan;
                    camera.roll = p.Roll;
                    camera.zoom = p.Zoom;
                    if (camera.valuesFound) ApplyAngleValues();
                    if (camera.zoomFound) ApplyZoomValue();
                }
            }
            d2rHUDConfig.CameraLastPresetIndex = selectedPreset;
            SaveFullGrailConfig(configFilePath, false);
        }
        if (ThemeCheckbox("Autoload last preset", &d2rHUDConfig.CameraAutoloadLastPreset))
            SaveFullGrailConfig(configFilePath, false);
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("CameraValues", ImVec2(0, 0), true);
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.3f, 1.0f), "Camera values");
        ImGui::Separator();

        ImGui::BeginDisabled(!camera.programEnabled || !camera.valuesFound);
        ImGui::Text("Pitch");
        ImGui::SameLine(cameraLabelWidth);
        ImGui::SetNextItemWidth(cameraInputWidth);
        if (ImGui::DragFloat("##Pitch", &camera.pitch, 0.01f, -10.0f, 10.0f, "%.3f"))
            ApplyAngleValues();
        ImGui::Text("Yaw");
        ImGui::SameLine(cameraLabelWidth);
        ImGui::SetNextItemWidth(cameraInputWidth);
        if (ImGui::DragFloat("##Pan", &camera.pan, 0.01f, -10.0f, 10.0f, "%.3f"))
            ApplyAngleValues();
        ImGui::Text("Roll");
        ImGui::SameLine(cameraLabelWidth);
        ImGui::SetNextItemWidth(cameraInputWidth);
        if (ImGui::DragFloat("##Roll", &camera.roll, 0.01f, -10.0f, 10.0f, "%.3f"))
            ApplyAngleValues();
        ImGui::Text("Height");
        ImGui::SameLine(cameraLabelWidth);
        ImGui::SetNextItemWidth(cameraInputWidth);
        if (ImGui::DragFloat("##Height", &camera.height, 0.01f, -10.0f, 10.0f, "%.3f"))
            ApplyAngleValues();
        ImGui::EndDisabled();

        ImGui::BeginDisabled(!camera.programEnabled || !camera.zoomFound);
        ImGui::Text("Zoom");
        ImGui::SameLine(cameraLabelWidth);
        ImGui::SetNextItemWidth(cameraInputWidth);
        if (ImGui::DragFloat("##Zoom", &camera.zoom, 0.01f, -5.0f, 5.0f, "%.3f"))
            ApplyZoomValue();
        ImGui::EndDisabled();

        ImGui::Dummy(ImVec2(0.0f, 4.0f * menuScale));
        ImGui::BeginDisabled(!camera.valuesFound);
        if (ThemeButton("Reset Angles"))
        {
            camera.pitch = camera.defaultPitch;
            camera.height = camera.defaultHeight;
            camera.pan = camera.defaultPan;
            camera.roll = camera.defaultRoll;
            ApplyAngleValues();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!camera.zoomFound);
        if (ThemeButton("Reset Zoom"))
        {
            camera.zoom = camera.defaultZoom;
            ApplyZoomValue();
        }
        ImGui::EndDisabled();

        ImGui::EndChild();

        ImGui::PopStyleColor();
        ImGui::End();
    }
    else
        ImGui::PopStyleColor();
}

void ShowLootMenu()
{
    if (!showLootMenu)
    {
        if (lootConfigLoaded)
            SaveLootFilterConfig("lootfilter_config.lua");
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
    float menuScale = GetMenuScaleFactor();
    CenterWindow(ImVec2(1200, 720));
    static std::string hoveredKey;
    hoveredKey.clear();
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ShouldDrawWindowBackgroundImage(kWindowBg_Loot) ? ImVec4(0, 0, 0, 0) : s_WindowBgColors[kWindowBg_Loot]);
    PushFontSafe(3);
    if (ImGui::Begin("D2RLoot Settings", &showLootMenu, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar))
    {
        if (ShouldDrawWindowBackgroundImage(kWindowBg_Loot))
            DrawWindowBackgroundImage(kWindowBg_Loot);
        DrawWindowTitleAndClose("D2RLoot Settings", &showLootMenu);
        PopFontSafe(3);
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0.0f, 3.0f));
        DrawCreateFilterPopup();

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

        if (!s_lootFilterUpdateStatus.empty())
        {
            if (s_lootFilterUpdateStatus.size() >= 9 && s_lootFilterUpdateStatus.compare(0, 9, "Updated to") == 0 && !s_lootFilterUpdateApplied)
            {
                if (!s_lootFilterNewVersion.empty())
                    g_LootFilterHeader.Version = s_lootFilterNewVersion;
                else
                    LoadLootFilterLogic(lootFile);
                s_lootFilterUpdateApplied = true;
            }
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::seconds>(now - s_lootFilterUpdateStatusTime).count() >= 4)
            {
                s_lootFilterUpdateStatus.clear();
                s_lootFilterUpdateStatusTime = {};
                s_lootFilterNewVersion.clear();
                s_lootFilterUpdateApplied = false;
            }
        }
        if (!g_LootFilterHeader.Version.empty())
            CenteredWrappedText("My D2RLoot Version: ", g_LootFilterHeader.Version);
        ImGui::SameLine(0.0f, 12.0f);
        if (ThemeButton(s_lootFilterUpdating ? "Updating..." : "Update"))
        {
            if (!s_lootFilterUpdating)
            {
                s_lootFilterUpdating = true;
                s_lootFilterUpdateStatus.clear();
                s_lootFilterUpdateApplied = false;
                std::thread(LootFilterUpdateThread).detach();
            }
        }
        if (!s_lootFilterUpdateStatus.empty())
        {
            ImGui::SameLine(0.0f, 8.0f);
            ImGui::TextColored(ImVec4(0.7f, 0.9f, 0.7f, 1.0f), "%s", s_lootFilterUpdateStatus.c_str());
        }

        // --- My Selected Filter: centered styled display + dropdown to change ---
        std::vector<std::string> orderedFilters = GetOrderedFilterList();
        std::string currentDisplayName = s_activeFilterInternalName.empty()
            ? (g_LootFilterHeader.Title.empty() ? "Custom" : g_LootFilterHeader.Title)
            : GetFilterDisplayName(s_activeFilterInternalName);
        CenteredWrappedText("My Selected Filter: ", currentDisplayName);

        float comboWidth = 220.0f;
        ImVec2 avail = ImGui::GetContentRegionAvail();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail.x - comboWidth) * 0.5f);
        std::string comboLabel = s_pendingFilter.empty() ? currentDisplayName : GetFilterDisplayName(s_pendingFilter);
        ImGui::SetNextItemWidth(comboWidth);
        BeginFrameBgImageRegion();
        if (ImGui::BeginCombo("##selected_filter", comboLabel.c_str()))
        {
            for (const auto& internalName : orderedFilters)
            {
                std::string displayName = GetFilterDisplayName(internalName);
                bool isActive = (internalName == s_activeFilterInternalName);
                std::string label = displayName + (isActive ? " \xe2\x9c\x93" : "");
                if (ImGui::Selectable(label.c_str(), isActive))
                {
                    s_pendingFilter = internalName;
                }
            }
            ImGui::EndCombo();
        }
        EndFrameBgImageRegion();
        if (!s_pendingFilter.empty())
        {
            ImGui::Dummy(ImVec2(0.0f, 4.0f));
            std::string applyLabel = "Apply \"" + GetFilterDisplayName(s_pendingFilter) + "\":";
            float applyW = ImGui::CalcTextSize(applyLabel.c_str()).x;
            ImVec2 availApply = ImGui::GetContentRegionAvail();
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availApply.x - applyW) * 0.5f);
            ImGui::Text("%s", applyLabel.c_str());
            float btnW1 = ImGui::CalcTextSize("Use this filter").x + ImGui::GetStyle().FramePadding.x * 2;
            ImVec2 availApply2 = ImGui::GetContentRegionAvail();
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availApply2.x - btnW1) * 0.5f);
            if (ThemeButton("Use this filter"))
            {
                if (CopyModFilterToActive(s_pendingFilter))
                {
                    s_activeFilterInternalName = s_pendingFilter;
                    LoadLootFilterConfig(GetLootFilterConfigPath());
                    LoadLootFilterLogic(lootFile);
                }
                s_pendingFilter.clear();
            }
        }

        ImGui::Dummy(ImVec2(0.0f, 5.0f));

        // --- Global options (collapsible) ---
        if (ImGui::CollapsingHeader("Global options", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent(8.0f);
            ImGui::Dummy(ImVec2(0.0f, 2.0f));

            // --- Boolean Checkboxes ---
            auto RenderCheckboxLine = [&](const std::vector<std::pair<std::string, std::string>>& items, std::function<void()> onChanged = nullptr)
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

                        if (ThemeCheckbox(items[i].first.c_str(), &boolValue))
                        {
                            auto it2 = g_LuaVariables.find(key);
                            if (it2 != g_LuaVariables.end()) it2->second = boolValue ? "true" : "false";
                            else g_LuaVariables.insert({ key, boolValue ? "true" : "false" });
                            if (onChanged) onChanged();
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
            RenderCheckboxLine(bools, []() { SaveLootFilterConfig("lootfilter_config.lua"); });

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
                        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 60.0f);
                        BeginFrameBgImageRegion();
                        bool changed = ImGui::InputText(inputID.c_str(), buffer, sizeof(buffer));
                        EndFrameBgImageRegion();
                        ImGui::SameLine();
                        if (ThemeButton(("Done##" + key).c_str())) g_EditMode[key] = false;

                        if (ImGui::IsItemActivated()) ImGui::SetKeyboardFocusHere(-1);

                        if (changed) {
                            g_LuaVariables[key] = buffer;
                            SaveLootFilterConfig("lootfilter_config.lua");
                        }
                    }
                    else
                    {
                        // --- Display mode: colored text ---
                        RenderColoredText(value); // uses your function

                        ImGui::SameLine();
                        if (ThemeButton(("Edit##" + key).c_str())) g_EditMode[key] = true;
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
                    bool anyTitleChanged = false;

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
                            BeginFrameBgImageRegion();
                            bool changed = ImGui::InputText(inputID.c_str(), buffer, sizeof(buffer));
                            EndFrameBgImageRegion();
                            ImGui::PopItemWidth();

                            if (changed) { titles[idx] = buffer; anyTitleChanged = true; }
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
                        if (ThemeButton(("Done##" + key).c_str()))
                            g_EditMode[key] = false;
                    }
                    else
                    {
                        if (ThemeButton(("Edit##" + key).c_str()))
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
                    if (anyTitleChanged) SaveLootFilterConfig("lootfilter_config.lua");
                };
            RenderFilterTitles();

            ImGui::Unindent(8.0f);
            ImGui::Dummy(ImVec2(0.0f, 5.0f));
        }

        // --- Rules section ---
        static bool s_showAddRulePopup = false;
        static bool s_addRulePopupJustOpened = false;
        static char s_addRuleNameBuf[256] = {};
        ImGui::Dummy(ImVec2(0.0f, 2.0f));
        if (ImGui::CollapsingHeader("Rules", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent(8.0f);
            ImGui::Dummy(ImVec2(0.0f, 2.0f));
            if (ImGui::BeginChild("RulesArea", ImVec2(0, 280), true, ImGuiWindowFlags_None))
            {
                if (g_LootFilterRules.empty())
                {
                    ImGui::TextWrapped("No rules in config, or rules table is empty. Add a new rule using the button below (or edit lootfilter_config.lua directly)");
                }
                else
                {
                    static char s_rawEditBuf[8192];
                    static int s_rawEditRuleIndex = -1;
                    for (size_t r = 0; r < g_LootFilterRules.size(); r++)
                    {
                        LootFilterRule& rule = g_LootFilterRules[r];
                        std::string ruleLabel = "Rule " + std::to_string(r + 1);
                        if (!rule.rawLua.empty())
                        {
                            size_t firstLineEnd = rule.rawLua.find('\n');
                            std::string firstLine = firstLineEnd == std::string::npos ? rule.rawLua : rule.rawLua.substr(0, firstLineEnd);
                            size_t trim = firstLine.find_first_not_of(" \t");
                            if (trim != std::string::npos) firstLine = firstLine.substr(trim);
                            if (!firstLine.empty() && firstLine[0] == '{')
                            {
                                size_t p = firstLine.find("--");
                                if (p != std::string::npos)
                                {
                                    p += 2;
                                    while (p < firstLine.size() && (firstLine[p] == '-' || firstLine[p] == ' ' || firstLine[p] == '\t'))
                                        p++;
                                    firstLine = firstLine.substr(p);
                                }
                                else
                                    firstLine = firstLine.substr(1);
                                trim = firstLine.find_first_not_of(" \t");
                                if (trim != std::string::npos) firstLine = firstLine.substr(trim);
                            }
                            if (!firstLine.empty()) ruleLabel = firstLine;
                        }
                        else if (!rule.comment.empty())
                            ruleLabel = rule.comment;
                        if (!ruleLabel.empty() && ruleLabel.size() >= 2 && ruleLabel[0] == '-' && ruleLabel[1] == '-')
                        {
                            size_t d = 2;
                            while (d < ruleLabel.size() && (ruleLabel[d] == '-' || ruleLabel[d] == ' ' || ruleLabel[d] == '\t'))
                                d++;
                            if (d < ruleLabel.size())
                                ruleLabel = ruleLabel.substr(d);
                        }
                        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
                        if (ImGui::TreeNode(("##rule" + std::to_string(r)).c_str(), "%s", ruleLabel.c_str()))
                        {
                            if (s_rawEditRuleIndex != (int)r)
                            {
                                s_rawEditRuleIndex = (int)r;
                                std::vector<std::string> lines;
                                size_t pos = 0;
                                while (pos < rule.rawLua.size())
                                {
                                    size_t next = rule.rawLua.find('\n', pos);
                                    if (next == std::string::npos) next = rule.rawLua.size();
                                    lines.push_back(rule.rawLua.substr(pos, next - pos));
                                    pos = next + 1;
                                }
                                while (!lines.empty())
                                {
                                    size_t start = lines.back().find_first_not_of(" \t");
                                    if (start != std::string::npos) break;
                                    lines.pop_back();
                                }
                                std::string displayLua;
                                for (size_t i = 0; i < lines.size(); i++)
                                {
                                    std::string line = lines[i];
                                    size_t start = line.find_first_not_of(" \t");
                                    if (start != std::string::npos) line = line.substr(start);
                                    if (i > 0 && i < lines.size() - 1 && !line.empty())
                                        line = "\t" + line;
                                    if (!displayLua.empty()) displayLua += "\n";
                                    displayLua += line;
                                }
                                while (!displayLua.empty() && (displayLua.back() == '\n' || displayLua.back() == '\r'))
                                    displayLua.pop_back();
                                strncpy(s_rawEditBuf, displayLua.c_str(), sizeof(s_rawEditBuf) - 1);
                                s_rawEditBuf[sizeof(s_rawEditBuf) - 1] = '\0';
                            }
                            float lineCount = 1.0f;
                            for (const char* p = s_rawEditBuf; *p; p++) if (*p == '\n') lineCount += 1.0f;
                            float lineH = ImGui::GetTextLineHeightWithSpacing();
                            float editHeight = lineCount * lineH + ImGui::GetStyle().FramePadding.y * 2.0f - 1.0f * lineH;
                            editHeight = std::clamp(editHeight, 60.0f, 500.0f);
                            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                            BeginFrameBgImageRegion();
                            if (ImGui::InputTextMultiline(("##raw_rule" + std::to_string(r)).c_str(), s_rawEditBuf, sizeof(s_rawEditBuf), ImVec2(-1, editHeight)))
                            {
                                std::string saved = s_rawEditBuf;
                                while (!saved.empty() && (saved.back() == '\n' || saved.back() == '\r'))
                                    saved.pop_back();
                                rule.rawLua = saved;
                                SaveLootFilterConfig("lootfilter_config.lua");
                            }
                            EndFrameBgImageRegion();
                            static int s_copiedRuleIndex = -1;
                            static std::chrono::steady_clock::time_point s_copiedAt;
                            bool showCopied = (s_copiedRuleIndex == (int)r) && (std::chrono::steady_clock::now() - s_copiedAt) < std::chrono::milliseconds(1500);
                            if (ThemeButton(showCopied ? "Copied!" : "Copy code"))
                            {
                                ImGui::SetClipboardText(s_rawEditBuf);
                                s_copiedRuleIndex = (int)r;
                                s_copiedAt = std::chrono::steady_clock::now();
                            }
                            ImGui::SameLine();
                            if (ThemeButton("Remove rule"))
                            {
                                g_LootFilterRules.erase(g_LootFilterRules.begin() + r);
                                SaveLootFilterConfig("lootfilter_config.lua");
                                if (s_copiedRuleIndex == (int)r) s_copiedRuleIndex = -1;
                                else if (s_copiedRuleIndex > (int)r) s_copiedRuleIndex--;
                                if (s_rawEditRuleIndex == (int)r) s_rawEditRuleIndex = -1;
                                else if (s_rawEditRuleIndex > (int)r) s_rawEditRuleIndex--;
                                ImGui::TreePop();
                                ImGui::PopTextWrapPos();
                                break;
                            }
                            if (s_copiedRuleIndex == (int)r && (std::chrono::steady_clock::now() - s_copiedAt) >= std::chrono::milliseconds(1500))
                                s_copiedRuleIndex = -1;
                            ImGui::TreePop();
                        }
                        ImGui::PopTextWrapPos();
                    }
                }
                ImGui::Dummy(ImVec2(0.0f, 4.0f));
                float addRuleW = ImGui::CalcTextSize("Add new rule").x + ImGui::GetStyle().FramePadding.x * 2;
                ImVec2 rulesAvail = ImGui::GetContentRegionAvail();
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (rulesAvail.x - addRuleW) * 0.5f);
                if (ThemeButton("Add new rule"))
                {
                    s_showAddRulePopup = true;
                    s_addRulePopupJustOpened = true;
                    s_addRuleNameBuf[0] = '\0';
                }
                if (s_showAddRulePopup)
                {
                    if (s_addRulePopupJustOpened)
                    {
                        ImGui::OpenPopup("Add rule");
                        s_addRulePopupJustOpened = false;
                    }
                    ImGui::SetNextWindowSize(ImVec2(320.0f, 0.0f), ImGuiCond_FirstUseEver);
                    if (ImGui::BeginPopupModal("Add rule", &s_showAddRulePopup, ImGuiWindowFlags_AlwaysAutoResize))
                    {
                        ImGui::Text("Rule name (optional, shown as comment):");
                        ImGui::SetNextItemWidth(-1.0f);
                        BeginFrameBgImageRegion();
                        ImGui::InputText("##addrule_name", s_addRuleNameBuf, sizeof(s_addRuleNameBuf));
                        EndFrameBgImageRegion();
                        ImGui::Spacing();
                        if (ThemeButton("Add", ImVec2(80.0f, 0.0f)))
                        {
                            if (!g_LootFilterRules.empty())
                            {
                                std::string& lastRaw = g_LootFilterRules.back().rawLua;
                                while (!lastRaw.empty() && (lastRaw.back() == '\n' || lastRaw.back() == '\r' || lastRaw.back() == ' ' || lastRaw.back() == '\t'))
                                    lastRaw.pop_back();
                                if (!lastRaw.empty() && lastRaw.back() == '}')
                                    lastRaw += ",";
                                lastRaw += "\n";
                            }
                            LootFilterRule newRule;
                            std::string name(s_addRuleNameBuf);
                            while (!name.empty() && (name.back() == ' ' || name.back() == '\t')) name.pop_back();
                            while (!name.empty() && (name.front() == ' ' || name.front() == '\t')) name.erase(0, 1);
                            if (!name.empty())
                                newRule.rawLua = "-- " + name + "\n";
                            newRule.rawLua += "{\n    code = \"isc\",\n    hide = true\n}\n";
                            g_LootFilterRules.push_back(newRule);
                            SaveLootFilterConfig("lootfilter_config.lua");
                            s_showAddRulePopup = false;
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::SameLine();
                        if (ThemeButton("Cancel", ImVec2(80.0f, 0.0f)))
                        {
                            s_showAddRulePopup = false;
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::EndPopup();
                    }
                }
            }
            ImGui::EndChild();
            ImGui::Unindent(8.0f);
        }

        ImGui::Dummy(ImVec2(0.0f, 3.0f));

        // --- Sounds section ---
        if (ImGui::CollapsingHeader("Sounds", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Indent(8.0f);
            ImGui::Dummy(ImVec2(0.0f, 2.0f));
            std::vector<std::pair<std::string, std::string>> soundFiles = GetSoundFilesFromBothLocations();
            if (soundFiles.empty())
            {
                ImGui::TextWrapped("No sounds in My Filters/sounds or mod yet. Use \"Import sounds\" to add .mp3, .flac or .wav files.");
            }
            else
            {
                if (ImGui::BeginChild("SoundsList", ImVec2(0, 120), true, ImGuiWindowFlags_None))
                {
                    for (size_t i = 0; i < soundFiles.size(); i++)
                    {
                        const std::string& path = soundFiles[i].first;
                        const std::string& name = soundFiles[i].second;
                        ImGui::Text("%s", name.c_str());
                        float btnX = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 55.0f;
                        if (btnX > ImGui::GetCursorPosX()) ImGui::SameLine(btnX);
                        std::string playId = "Play##" + name + std::to_string(i);
                        if (ThemeButton(playId.c_str(), ImVec2(50.0f, 0.0f)))
                        {
                            if (std::filesystem::exists(path))
                                PlaySoundFile(path);
                        }
                    }
                }
                ImGui::EndChild();  // always call after BeginChild, regardless of return value
            }
            ImGui::Dummy(ImVec2(0.0f, 4.0f));
            float importSoundsW = ImGui::CalcTextSize("Import sounds").x + ImGui::GetStyle().FramePadding.x * 2;
            ImVec2 soundsAvail = ImGui::GetContentRegionAvail();
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (soundsAvail.x - importSoundsW) * 0.5f);
            if (ThemeButton("Import sounds"))
                OpenImportSoundsPathInput();
            DrawImportSoundsPathRow();
            ImGui::Unindent(8.0f);
        }

        ImGui::Dummy(ImVec2(0.0f, 3.0f));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0.0f, 4.0f));

        // --- Import / Open / Guide / Create buttons ---
        float btnImportW = ImGui::CalcTextSize("Import Filter").x + ImGui::GetStyle().FramePadding.x * 2;
        float btnOpenW = ImGui::CalcTextSize("Open Filter").x + ImGui::GetStyle().FramePadding.x * 2;
        float btnCreateW = ImGui::CalcTextSize("Create my own filter").x + ImGui::GetStyle().FramePadding.x * 2;
        float btnGuideW = ImGui::CalcTextSize("Filter Guide").x + ImGui::GetStyle().FramePadding.x * 2;
        float spacing = ImGui::GetStyle().ItemSpacing.x;
        float totalBtnW = btnImportW + btnOpenW + btnCreateW + btnGuideW + spacing * 3;
        ImVec2 availBtns = ImGui::GetContentRegionAvail();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availBtns.x - totalBtnW) * 0.5f);
        if (ThemeButton("Import Filter"))
            OpenImportPathInput();
        ImGui::SameLine();
        if (ThemeButton("Open Filter"))
        {
            std::string path = GetLootFilterConfigPathAbsolute();
            if (std::filesystem::exists(path))
                OpenInShell(path);
        }
        ImGui::SameLine();
        if (ThemeButton("Create my own filter"))
            OpenCreateFilterPopup();
        ImGui::SameLine();
        if (ThemeButton("Filter Guide"))
        {
            OpenInShell("https://locbones.github.io/D2RLAN-LootFilterGuide");
        }
        DrawImportFilterPathRow();
        ImGui::Dummy(ImVec2(0.0f, 3.0f));

        // --- Bottom Description ---
        std::string desc = "Hover over an option to see its description.";
        if (!hoveredKey.empty() && g_LuaDescriptions.count(hoveredKey))
            desc = g_LuaDescriptions.at(hoveredKey);

        DrawBottomDescription(desc);

        ImGui::PopStyleColor();
        ImGui::End();
    }
    else
        ImGui::PopStyleColor();
}

static void ApplyUITheme(const std::string& themeName);

static void ApplyThemeColorsToStyle(const std::vector<ImVec4>& colors)
{
    if ((int)colors.size() != s_ThemeColorCount) return;
    ImGuiStyle& style = ImGui::GetStyle();
    for (int i = 0; i < s_ThemeColorCount; ++i) {
        if (s_ThemeColorEntries[i].imguiCol == kImGuiColSentinel)
            continue;  // checkbox colors: applied only when drawing checkboxes, not to global style
        // Use transparent when an image override is set so the loaded image shows instead of the color
        bool useImage = (int)s_UIColorImageOverrides.size() > i && !s_UIColorImageOverrides[i].empty();
        if (useImage && ((i >= 3 && i <= 5) || (i >= 7 && i <= 9) || (i >= 10 && i <= 12)))
            style.Colors[s_ThemeColorEntries[i].imguiCol] = ImVec4(0, 0, 0, 0);
        else
            style.Colors[s_ThemeColorEntries[i].imguiCol] = colors[i];
    }
}

static std::vector<ImVec4> GetCurrentThemeColors()
{
    ImGuiStyle& style = ImGui::GetStyle();
    std::vector<ImVec4> out;
    out.reserve(s_ThemeColorCount);
    for (int i = 0; i < s_ThemeColorCount; ++i) {
        if (s_ThemeColorEntries[i].imguiCol == kImGuiColSentinel) {
            if (i < (int)s_UICustomColors.size())
                out.push_back(s_UICustomColors[i]);
            else if (i == 7)
                out.push_back(style.Colors[ImGuiCol_FrameBg]);
            else if (i == 8)
                out.push_back(style.Colors[ImGuiCol_FrameBgHovered]);
            else
                out.push_back(style.Colors[ImGuiCol_FrameBgActive]);
        }
        else
            out.push_back(style.Colors[s_ThemeColorEntries[i].imguiCol]);
    }
    return out;
}

static bool ShouldDrawWindowBackgroundImage(WindowBgId id)
{
    if (id < 0 || id >= kWindowBgCount) return false;
    return !s_WindowBgImageOverrides[id].empty() && D3D12::GetWindowBgTextureId((int)id) != 0;
}

static void DrawWindowBackgroundImage(WindowBgId id)
{
    if (id < 0 || id >= kWindowBgCount) return;
    uint64_t texId = D3D12::GetWindowBgTextureId((int)id);
    if (texId == 0) return;
    ImVec2 winPos = ImGui::GetWindowPos();
    ImVec2 winSize = ImGui::GetWindowSize();
    ImVec2 bgMin(winPos.x, winPos.y);
    ImVec2 bgMax(winPos.x + winSize.x, winPos.y + winSize.y);
    ImGui::GetWindowDrawList()->AddImage((ImTextureID)texId, bgMin, bgMax, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE);
}

static bool ShouldDrawFrameBgImage()
{
    // If we have image overrides set but no texture loaded yet, re-request load (retry until it succeeds)
    for (int s = 0; s < 3; ++s) {
        if (s + 3 < (int)s_UIColorImageOverrides.size() && !s_UIColorImageOverrides[s + 3].empty() && D3D12::GetFrameBgTextureId(s) == 0)
            D3D12::RequestFrameBgImageReload(s, s_UIColorImageOverrides[s + 3]);
    }
    for (int s = 0; s < 3; ++s) {
        if (s + 7 < (int)s_UIColorImageOverrides.size() && !s_UIColorImageOverrides[s + 7].empty() && D3D12::GetCheckboxTextureId(s) == 0)
            D3D12::RequestCheckboxImageReload(s, s_UIColorImageOverrides[s + 7]);
    }
    for (int s = 0; s < 3; ++s)
        if (D3D12::GetFrameBgTextureId(s) != 0) return true;
    return false;
}

// Frame Bg image should appear only inside input boxes/dropdowns (not as window body).
// Slots 0=default, 1=hovered, 2=active. Call Begin before widget, End after.
static bool s_InFrameBgImageRegion = false;
static void BeginFrameBgImageRegion()
{
    if (s_InFrameBgImageRegion) return;
    if (!ShouldDrawFrameBgImage()) return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (dl) { dl->ChannelsSplit(2); dl->ChannelsSetCurrent(1); s_InFrameBgImageRegion = true; }
}
static void EndFrameBgImageRegion()
{
    if (!s_InFrameBgImageRegion) return;
    s_InFrameBgImageRegion = false;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (!dl) return;
    // Pick texture by widget state: active -> slot 2, hovered -> slot 1, else slot 0
    int slot = ImGui::IsItemActive() ? 2 : (ImGui::IsItemHovered() ? 1 : 0);
    uint64_t texId = D3D12::GetFrameBgTextureId(slot);
    if (texId == 0 && slot != 0)
        texId = D3D12::GetFrameBgTextureId(0);
    if (texId != 0) {
        ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
        dl->ChannelsSetCurrent(0);
        dl->AddImage((ImTextureID)texId, mn, mx, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE);
    }
    dl->ChannelsMerge();
}

static bool ThemeButton(const char* label, const ImVec2& size)
{
    uint64_t texIdAny = 0;
    for (int s = 2; s >= 0 && texIdAny == 0; --s)
        texIdAny = D3D12::GetButtonTextureId(s);
    if (texIdAny == 0)
        return ImGui::Button(label, size);
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
    // RMD themes: add 5px left/right padding for background image borders; overall button width +10px
    if (s_UITheme == "RMD Blue" || s_UITheme == "RMD Red" || s_UITheme == "RMD Purple" || s_UITheme == "RMD Green" || s_UITheme == "RMD Gold" || s_UITheme == "RMD Dark") {
        ImGuiStyle& style = ImGui::GetStyle();
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(style.FramePadding.x + 5.0f, style.FramePadding.y));
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const int channels = 2;
    if (dl) {
        dl->ChannelsSplit(channels);
        dl->ChannelsSetCurrent(1);  // Button draws to channel 1 so image (channel 0) is behind
    }
    bool ret = ImGui::Button(label, size);
    if (dl) {
        // Pick slot after Button: 2 = active, 1 = hovered, 0 = default; fallback to lower slot
        int slot = ImGui::IsItemActive() ? 2 : (ImGui::IsItemHovered() ? 1 : 0);
        uint64_t texId = 0;
        for (int s = slot; s >= 0 && texId == 0; --s)
            texId = D3D12::GetButtonTextureId(s);
        if (texId != 0) {
            ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
            dl->ChannelsSetCurrent(0);
            dl->AddImage((ImTextureID)texId, mn, mx, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE);
        }
        dl->ChannelsMerge();
    }
    if (s_UITheme == "RMD Blue" || s_UITheme == "RMD Red" || s_UITheme == "RMD Purple" || s_UITheme == "RMD Green" || s_UITheme == "RMD Gold" || s_UITheme == "RMD Dark")
        ImGui::PopStyleVar(1);
    ImGui::PopStyleColor(1);
    return ret;
}

static bool ThemeCheckbox(const char* label, bool* v)
{
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4 cb = ((int)s_UICustomColors.size() > 7) ? s_UICustomColors[7] : style.Colors[ImGuiCol_FrameBg];
    ImVec4 cbH = ((int)s_UICustomColors.size() > 8) ? s_UICustomColors[8] : style.Colors[ImGuiCol_FrameBgHovered];
    ImVec4 cbA = ((int)s_UICustomColors.size() > 9) ? s_UICustomColors[9] : style.Colors[ImGuiCol_FrameBgActive];
    uint64_t texIdAny = 0;
    for (int s = 2; s >= 0 && texIdAny == 0; --s)
        texIdAny = D3D12::GetCheckboxTextureId(s);
    if (texIdAny == 0) {
        ImGui::PushStyleColor(ImGuiCol_FrameBg, cb);
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, cbH);
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, cbA);
        bool ret = ImGui::Checkbox(label, v);
        ImGui::PopStyleColor(3);
        return ret;
    }
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(0, 0, 0, 0));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (dl) {
        dl->ChannelsSplit(2);
        dl->ChannelsSetCurrent(1);
    }
    bool ret = ImGui::Checkbox(label, v);
    if (dl) {
        int slot = ImGui::IsItemActive() ? 2 : (ImGui::IsItemHovered() ? 1 : 0);
        uint64_t texId = D3D12::GetCheckboxTextureId(slot);
        if (texId == 0 && slot != 0) texId = D3D12::GetCheckboxTextureId(0);
        if (texId != 0) {
            // Draw image only over the checkbox box (left square), not the label text.
            // Extend right (and bottom) by 2px so the image border isn't clipped by ImGui's frame rounding.
            ImVec2 mn = ImGui::GetItemRectMin();
            float frameH = ImGui::GetFrameHeight();
            ImVec2 boxMax(mn.x + frameH + 2.0f, mn.y + frameH + 2.0f);
            dl->ChannelsSetCurrent(0);
            dl->AddImage((ImTextureID)texId, mn, boxMax, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE);
        }
        dl->ChannelsMerge();
    }
    ImGui::PopStyleColor(3);
    return ret;
}

static void ClearThemeImageOverrides()
{
    s_UIColorImageOverrides.resize(s_ThemeColorCount, "");
    for (int i = 0; i < s_ThemeColorCount; ++i)
        s_UIColorImageOverrides[i] = "";
    for (int i = 3; i <= 5; ++i)
        D3D12::RequestFrameBgImageReload(i - 3, "");
    for (int i = 7; i <= 9; ++i)
        D3D12::RequestCheckboxImageReload(i - 7, "");
    for (int i = 10; i <= 12; ++i)
        D3D12::RequestButtonImageReload(i - 10, "");
    for (int w = 0; w < kWindowBgCount; ++w)
    {
        s_WindowBgImageOverrides[w] = "";
        D3D12::RequestWindowBgImageReload(w, "");
    }
}

static void ApplyUITheme(const std::string& themeName)
{
    ImGuiStyle& style = ImGui::GetStyle();
    if (themeName == "Dark")
    {
        ImGui::StyleColorsDark();
        ClearThemeImageOverrides();
        for (int w = 0; w < kWindowBgCount; ++w)
            s_WindowBgColors[w] = style.Colors[ImGuiCol_WindowBg];
    }
    else if (themeName == "Golden Sunset")
    {
        ImGui::StyleColorsDark();
        ImVec4* colors = style.Colors;
        colors[ImGuiCol_Text] = ImVec4(0.85f, 0.75f, 0.55f, 1.00f);
        colors[ImGuiCol_TextDisabled] = ImVec4(0.45f, 0.40f, 0.30f, 1.00f);
        colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.06f, 0.05f, 0.94f);
        colors[ImGuiCol_ChildBg] = ImVec4(0.10f, 0.08f, 0.06f, 1.00f);
        colors[ImGuiCol_PopupBg] = ImVec4(0.10f, 0.08f, 0.06f, 0.98f);
        colors[ImGuiCol_Border] = ImVec4(0.45f, 0.35f, 0.20f, 0.50f);
        colors[ImGuiCol_FrameBg] = ImVec4(0.18f, 0.12f, 0.08f, 1.00f);
        colors[ImGuiCol_FrameBgHovered] = ImVec4(0.35f, 0.22f, 0.10f, 1.00f);
        colors[ImGuiCol_FrameBgActive] = ImVec4(0.42f, 0.28f, 0.12f, 1.00f);
        colors[ImGuiCol_TitleBg] = ImVec4(0.15f, 0.10f, 0.05f, 1.00f);
        colors[ImGuiCol_TitleBgActive] = ImVec4(0.35f, 0.22f, 0.08f, 1.00f);
        colors[ImGuiCol_Button] = ImVec4(0.30f, 0.20f, 0.08f, 1.00f);
        colors[ImGuiCol_ButtonHovered] = ImVec4(0.55f, 0.38f, 0.12f, 1.00f);
        colors[ImGuiCol_ButtonActive] = ImVec4(0.70f, 0.50f, 0.18f, 1.00f);
        colors[ImGuiCol_Header] = ImVec4(0.28f, 0.18f, 0.06f, 1.00f);
        colors[ImGuiCol_HeaderHovered] = ImVec4(0.45f, 0.30f, 0.10f, 1.00f);
        colors[ImGuiCol_HeaderActive] = ImVec4(0.55f, 0.38f, 0.12f, 1.00f);
        colors[ImGuiCol_Separator] = ImVec4(0.45f, 0.35f, 0.20f, 0.50f);
        colors[ImGuiCol_SliderGrab] = ImVec4(0.65f, 0.45f, 0.15f, 1.00f);
        colors[ImGuiCol_SliderGrabActive] = ImVec4(0.85f, 0.65f, 0.25f, 1.00f);
        ClearThemeImageOverrides();
        for (int w = 0; w < kWindowBgCount; ++w)
            s_WindowBgColors[w] = style.Colors[ImGuiCol_WindowBg];
    }
    else if (themeName == "Hell's Embrace")
    {
        ImGui::StyleColorsDark();
        ImVec4* colors = style.Colors;
        colors[ImGuiCol_Text] = ImVec4(0.95f, 0.85f, 0.85f, 1.00f);
        colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.40f, 0.40f, 1.00f);
        colors[ImGuiCol_WindowBg] = ImVec4(0.04f, 0.02f, 0.02f, 0.94f);
        colors[ImGuiCol_ChildBg] = ImVec4(0.06f, 0.03f, 0.03f, 1.00f);
        colors[ImGuiCol_PopupBg] = ImVec4(0.06f, 0.03f, 0.03f, 0.98f);
        colors[ImGuiCol_Border] = ImVec4(0.45f, 0.15f, 0.15f, 0.50f);
        colors[ImGuiCol_FrameBg] = ImVec4(0.12f, 0.05f, 0.05f, 1.00f);
        colors[ImGuiCol_FrameBgHovered] = ImVec4(0.25f, 0.08f, 0.08f, 1.00f);
        colors[ImGuiCol_FrameBgActive] = ImVec4(0.35f, 0.10f, 0.10f, 1.00f);
        colors[ImGuiCol_TitleBg] = ImVec4(0.08f, 0.02f, 0.02f, 1.00f);
        colors[ImGuiCol_TitleBgActive] = ImVec4(0.22f, 0.06f, 0.06f, 1.00f);
        colors[ImGuiCol_Button] = ImVec4(0.20f, 0.05f, 0.05f, 1.00f);
        colors[ImGuiCol_ButtonHovered] = ImVec4(0.45f, 0.10f, 0.10f, 1.00f);
        colors[ImGuiCol_ButtonActive] = ImVec4(0.60f, 0.12f, 0.12f, 1.00f);
        colors[ImGuiCol_Header] = ImVec4(0.18f, 0.05f, 0.05f, 1.00f);
        colors[ImGuiCol_HeaderHovered] = ImVec4(0.35f, 0.08f, 0.08f, 1.00f);
        colors[ImGuiCol_HeaderActive] = ImVec4(0.50f, 0.10f, 0.10f, 1.00f);
        colors[ImGuiCol_Separator] = ImVec4(0.40f, 0.15f, 0.15f, 0.50f);
        colors[ImGuiCol_SliderGrab] = ImVec4(0.55f, 0.15f, 0.15f, 1.00f);
        colors[ImGuiCol_SliderGrabActive] = ImVec4(0.75f, 0.20f, 0.20f, 1.00f);
        ClearThemeImageOverrides();
        for (int w = 0; w < kWindowBgCount; ++w)
            s_WindowBgColors[w] = style.Colors[ImGuiCol_WindowBg];
    }
    else if (themeName == "RMD Blue")
    {
        static const ImVec4 s_RMDBlueColors[] = {
            ImVec4(1.0f, 1.0f, 1.0f, 1.0f),
            ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
            ImVec4(0.43f, 0.43f, 0.5f, 0.5f),
            ImVec4(0.16f, 0.29f, 0.48f, 0.54f),
            ImVec4(0.26f, 0.59f, 0.98f, 0.4f),
            ImVec4(0.26f, 0.59f, 0.98f, 0.67f),
            ImVec4(0.26f, 0.59f, 0.98f, 1.0f),
            ImVec4(0.16f, 0.29f, 0.48f, 0.54f),
            ImVec4(0.26f, 0.59f, 0.98f, 0.4f),
            ImVec4(0.26f, 0.59f, 0.98f, 0.67f),
            ImVec4(0.26f, 0.59f, 0.98f, 0.4f),
            ImVec4(0.26f, 0.59f, 0.98f, 1.0f),
            ImVec4(0.06f, 0.53f, 0.98f, 1.0f),
            ImVec4(0.186f, 0.466f, 0.796f, 0.31f),
            ImVec4(0.26f, 0.59f, 0.98f, 0.8f),
            ImVec4(0.26f, 0.59f, 0.98f, 1.0f),
            ImVec4(0.43f, 0.43f, 0.5f, 0.5f),
        };
        static const char* const s_RMDBlueImageOverrides[] = {
            "", "", "", "Input_BG.png", "Input_BGH.png", "Input_BG.png", "",
            "Checkbox_BG.png", "Checkbox_BG.png", "Checkbox_BG.png",
            "RMD_Button_B.png", "RMD_Button_BA.png", "", "", "", "", ""
        };
        static const ImVec4 s_RMDBlueWindowBgColors[] = {
            ImVec4(0.06f, 0.06f, 0.06f, 0.94f), ImVec4(0.06f, 0.06f, 0.06f, 0.94f), ImVec4(0.06f, 0.06f, 0.06f, 0.94f),
            ImVec4(0.06f, 0.06f, 0.06f, 0.94f), ImVec4(0.06f, 0.06f, 0.06f, 0.94f), ImVec4(0.06f, 0.06f, 0.06f, 0.94f),
            ImVec4(0.06f, 0.06f, 0.06f, 0.94f), ImVec4(0.06f, 0.06f, 0.06f, 0.94f)
        };
        static const char* const s_RMDBlueWindowBgImages[] = {
            "InfoBG_B2.png", "InfoBG_B1.png", "InfoBG_B2.png", "InfoBG_B3.png", "InfoBG_B4.png",
            "InfoBG_B2.png", "InfoBG_B2.png", "InfoBG_B3.png"
        };
        s_UICustomColors.assign(s_RMDBlueColors, s_RMDBlueColors + s_ThemeColorCount);
        s_UIColorImageOverrides.resize(s_ThemeColorCount, "");
        for (int i = 0; i < s_ThemeColorCount && i < (int)(sizeof(s_RMDBlueImageOverrides) / sizeof(s_RMDBlueImageOverrides[0])); ++i)
            s_UIColorImageOverrides[i] = s_RMDBlueImageOverrides[i];
        for (int w = 0; w < kWindowBgCount; ++w) {
            s_WindowBgColors[w] = s_RMDBlueWindowBgColors[w];
            s_WindowBgImageOverrides[w] = s_RMDBlueWindowBgImages[w];
        }
        for (int i = 3; i <= 5; ++i)
            D3D12::RequestFrameBgImageReload(i - 3, s_UIColorImageOverrides[i]);
        for (int i = 7; i <= 9; ++i)
            D3D12::RequestCheckboxImageReload(i - 7, s_UIColorImageOverrides[i]);
        for (int i = 10; i <= 12; ++i)
            D3D12::RequestButtonImageReload(i - 10, s_UIColorImageOverrides[i]);
        for (int w = 0; w < kWindowBgCount; ++w)
            D3D12::RequestWindowBgImageReload(w, s_WindowBgImageOverrides[w]);
        ApplyThemeColorsToStyle(s_UICustomColors);
    }
    else if (themeName == "RMD Red")
    {
        static const ImVec4 s_RMDColors[] = {
            ImVec4(1.0f, 1.0f, 1.0f, 1.0f), ImVec4(0.5f, 0.5f, 0.5f, 1.0f), ImVec4(0.5f, 0.35f, 0.35f, 0.5f),
            ImVec4(0.48f, 0.16f, 0.16f, 0.54f), ImVec4(0.98f, 0.26f, 0.26f, 0.4f), ImVec4(0.98f, 0.26f, 0.26f, 0.67f), ImVec4(0.98f, 0.26f, 0.26f, 1.0f),
            ImVec4(0.48f, 0.16f, 0.16f, 0.54f), ImVec4(0.98f, 0.26f, 0.26f, 0.4f), ImVec4(0.98f, 0.26f, 0.26f, 0.67f),
            ImVec4(0.98f, 0.26f, 0.26f, 0.4f), ImVec4(0.98f, 0.26f, 0.26f, 1.0f), ImVec4(0.98f, 0.12f, 0.06f, 1.0f),
            ImVec4(0.796f, 0.186f, 0.186f, 0.31f), ImVec4(0.98f, 0.26f, 0.26f, 0.8f), ImVec4(0.98f, 0.26f, 0.26f, 1.0f), ImVec4(0.5f, 0.4f, 0.4f, 0.5f),
        };
        static const char* const s_RMDImageOverrides[] = {
            "", "", "", "Input_BG.png", "Input_BGH.png", "Input_BG.png", "",
            "Checkbox_BG.png", "Checkbox_BG.png", "Checkbox_BG.png",
            "RMD_Button_R.png", "RMD_Button_RA.png", "", "", "", "", ""
        };
        static const ImVec4 s_RMDWindowBgColors[] = {
            ImVec4(0.06f, 0.06f, 0.06f, 0.94f), ImVec4(0.06f, 0.06f, 0.06f, 0.94f), ImVec4(0.06f, 0.06f, 0.06f, 0.94f),
            ImVec4(0.06f, 0.06f, 0.06f, 0.94f), ImVec4(0.06f, 0.06f, 0.06f, 0.94f), ImVec4(0.06f, 0.06f, 0.06f, 0.94f), ImVec4(0.06f, 0.06f, 0.06f, 0.94f), ImVec4(0.06f, 0.06f, 0.06f, 0.94f)
        };
        static const char* const s_RMDWindowBgImages[] = {
            "InfoBG_R2.png", "InfoBG_R1.png", "InfoBG_R2.png", "InfoBG_R3.png", "InfoBG_R4.png",
            "InfoBG_R2.png", "InfoBG_R2.png", "InfoBG_R3.png"
        };
        s_UICustomColors.assign(s_RMDColors, s_RMDColors + s_ThemeColorCount);
        s_UIColorImageOverrides.resize(s_ThemeColorCount, "");
        for (int i = 0; i < s_ThemeColorCount && i < (int)(sizeof(s_RMDImageOverrides) / sizeof(s_RMDImageOverrides[0])); ++i)
            s_UIColorImageOverrides[i] = s_RMDImageOverrides[i];
        for (int w = 0; w < kWindowBgCount; ++w) {
            s_WindowBgColors[w] = s_RMDWindowBgColors[w];
            s_WindowBgImageOverrides[w] = s_RMDWindowBgImages[w];
        }
        for (int i = 3; i <= 5; ++i)
            D3D12::RequestFrameBgImageReload(i - 3, s_UIColorImageOverrides[i]);
        for (int i = 7; i <= 9; ++i)
            D3D12::RequestCheckboxImageReload(i - 7, s_UIColorImageOverrides[i]);
        for (int i = 10; i <= 12; ++i)
            D3D12::RequestButtonImageReload(i - 10, s_UIColorImageOverrides[i]);
        for (int w = 0; w < kWindowBgCount; ++w)
            D3D12::RequestWindowBgImageReload(w, s_WindowBgImageOverrides[w]);
        ApplyThemeColorsToStyle(s_UICustomColors);
    }
    else if (themeName == "RMD Purple")
    {
        static const ImVec4 s_RMDColors[] = {
            ImVec4(1.0f, 1.0f, 1.0f, 1.0f), ImVec4(0.5f, 0.5f, 0.5f, 1.0f), ImVec4(0.45f, 0.4f, 0.5f, 0.5f),
            ImVec4(0.35f, 0.16f, 0.48f, 0.54f), ImVec4(0.6f, 0.25f, 0.98f, 0.4f), ImVec4(0.65f, 0.3f, 0.98f, 0.67f), ImVec4(0.7f, 0.35f, 0.98f, 1.0f),
            ImVec4(0.35f, 0.16f, 0.48f, 0.54f), ImVec4(0.6f, 0.25f, 0.98f, 0.4f), ImVec4(0.65f, 0.3f, 0.98f, 0.67f),
            ImVec4(0.6f, 0.25f, 0.98f, 0.4f), ImVec4(0.7f, 0.35f, 0.98f, 1.0f), ImVec4(0.55f, 0.15f, 0.98f, 1.0f),
            ImVec4(0.5f, 0.2f, 0.75f, 0.31f), ImVec4(0.65f, 0.3f, 0.98f, 0.8f), ImVec4(0.7f, 0.35f, 0.98f, 1.0f), ImVec4(0.45f, 0.4f, 0.5f, 0.5f),
        };
        static const char* const s_RMDImageOverrides[] = {
            "", "", "", "Input_BG.png", "Input_BGH.png", "Input_BG.png", "",
            "Checkbox_BG.png", "Checkbox_BG.png", "Checkbox_BG.png",
            "RMD_Button_P.png", "RMD_Button_PA.png", "", "", "", "", ""
        };
        static const ImVec4 s_RMDWindowBgColors[] = {
            ImVec4(0.06f, 0.06f, 0.06f, 0.94f), ImVec4(0.06f, 0.06f, 0.06f, 0.94f), ImVec4(0.06f, 0.06f, 0.06f, 0.94f),
            ImVec4(0.06f, 0.06f, 0.06f, 0.94f), ImVec4(0.06f, 0.06f, 0.06f, 0.94f), ImVec4(0.06f, 0.06f, 0.06f, 0.94f), ImVec4(0.06f, 0.06f, 0.06f, 0.94f), ImVec4(0.06f, 0.06f, 0.06f, 0.94f)
        };
        static const char* const s_RMDWindowBgImages[] = {
            "InfoBG_P2.png", "InfoBG_P1.png", "InfoBG_P2.png", "InfoBG_P3.png", "InfoBG_P4.png",
            "InfoBG_P2.png", "InfoBG_P2.png", "InfoBG_P3.png"
        };
        s_UICustomColors.assign(s_RMDColors, s_RMDColors + s_ThemeColorCount);
        s_UIColorImageOverrides.resize(s_ThemeColorCount, "");
        for (int i = 0; i < s_ThemeColorCount && i < (int)(sizeof(s_RMDImageOverrides) / sizeof(s_RMDImageOverrides[0])); ++i)
            s_UIColorImageOverrides[i] = s_RMDImageOverrides[i];
        for (int w = 0; w < kWindowBgCount; ++w) {
            s_WindowBgColors[w] = s_RMDWindowBgColors[w];
            s_WindowBgImageOverrides[w] = s_RMDWindowBgImages[w];
        }
        for (int i = 3; i <= 5; ++i)
            D3D12::RequestFrameBgImageReload(i - 3, s_UIColorImageOverrides[i]);
        for (int i = 7; i <= 9; ++i)
            D3D12::RequestCheckboxImageReload(i - 7, s_UIColorImageOverrides[i]);
        for (int i = 10; i <= 12; ++i)
            D3D12::RequestButtonImageReload(i - 10, s_UIColorImageOverrides[i]);
        for (int w = 0; w < kWindowBgCount; ++w)
            D3D12::RequestWindowBgImageReload(w, s_WindowBgImageOverrides[w]);
        ApplyThemeColorsToStyle(s_UICustomColors);
    }
    else if (themeName == "RMD Green")
    {
        static const ImVec4 s_RMDColors[] = {
            ImVec4(1.0f, 1.0f, 1.0f, 1.0f), ImVec4(0.5f, 0.5f, 0.5f, 1.0f), ImVec4(0.35f, 0.45f, 0.38f, 0.5f),
            ImVec4(0.16f, 0.4f, 0.2f, 0.54f), ImVec4(0.25f, 0.85f, 0.35f, 0.4f), ImVec4(0.28f, 0.88f, 0.38f, 0.67f), ImVec4(0.3f, 0.9f, 0.35f, 1.0f),
            ImVec4(0.16f, 0.4f, 0.2f, 0.54f), ImVec4(0.25f, 0.85f, 0.35f, 0.4f), ImVec4(0.28f, 0.88f, 0.38f, 0.67f),
            ImVec4(0.25f, 0.85f, 0.35f, 0.4f), ImVec4(0.3f, 0.9f, 0.35f, 1.0f), ImVec4(0.1f, 0.9f, 0.25f, 1.0f),
            ImVec4(0.18f, 0.6f, 0.25f, 0.31f), ImVec4(0.28f, 0.88f, 0.38f, 0.8f), ImVec4(0.3f, 0.9f, 0.35f, 1.0f), ImVec4(0.4f, 0.5f, 0.43f, 0.5f),
        };
        static const char* const s_RMDImageOverrides[] = {
            "", "", "", "Input_BG.png", "Input_BGH.png", "Input_BG.png", "",
            "Checkbox_BG.png", "Checkbox_BG.png", "Checkbox_BG.png",
            "RMD_Button_BG.png", "RMD_Button_GA.png", "", "", "", "", ""
        };
        static const ImVec4 s_RMDWindowBgColors[] = {
            ImVec4(0.06f, 0.06f, 0.06f, 0.94f), ImVec4(0.06f, 0.06f, 0.06f, 0.94f), ImVec4(0.06f, 0.06f, 0.06f, 0.94f),
            ImVec4(0.06f, 0.06f, 0.06f, 0.94f), ImVec4(0.06f, 0.06f, 0.06f, 0.94f), ImVec4(0.06f, 0.06f, 0.06f, 0.94f), ImVec4(0.06f, 0.06f, 0.06f, 0.94f), ImVec4(0.06f, 0.06f, 0.06f, 0.94f)
        };
        static const char* const s_RMDWindowBgImages[] = {
            "InfoBG_G2.png", "InfoBG_G1.png", "InfoBG_G2.png", "InfoBG_G3.png", "InfoBG_G4.png",
            "InfoBG_G2.png", "InfoBG_G2.png", "InfoBG_G3.png"
        };
        s_UICustomColors.assign(s_RMDColors, s_RMDColors + s_ThemeColorCount);
        s_UIColorImageOverrides.resize(s_ThemeColorCount, "");
        for (int i = 0; i < s_ThemeColorCount && i < (int)(sizeof(s_RMDImageOverrides) / sizeof(s_RMDImageOverrides[0])); ++i)
            s_UIColorImageOverrides[i] = s_RMDImageOverrides[i];
        for (int w = 0; w < kWindowBgCount; ++w) {
            s_WindowBgColors[w] = s_RMDWindowBgColors[w];
            s_WindowBgImageOverrides[w] = s_RMDWindowBgImages[w];
        }
        for (int i = 3; i <= 5; ++i)
            D3D12::RequestFrameBgImageReload(i - 3, s_UIColorImageOverrides[i]);
        for (int i = 7; i <= 9; ++i)
            D3D12::RequestCheckboxImageReload(i - 7, s_UIColorImageOverrides[i]);
        for (int i = 10; i <= 12; ++i)
            D3D12::RequestButtonImageReload(i - 10, s_UIColorImageOverrides[i]);
        for (int w = 0; w < kWindowBgCount; ++w)
            D3D12::RequestWindowBgImageReload(w, s_WindowBgImageOverrides[w]);
        ApplyThemeColorsToStyle(s_UICustomColors);
    }
    else if (themeName == "RMD Gold")
    {
        static const ImVec4 s_RMDColors[] = {
            ImVec4(1.0f, 1.0f, 1.0f, 1.0f), ImVec4(0.5f, 0.5f, 0.5f, 1.0f), ImVec4(0.5f, 0.45f, 0.35f, 0.5f),
            ImVec4(0.45f, 0.35f, 0.1f, 0.54f), ImVec4(0.95f, 0.75f, 0.2f, 0.4f), ImVec4(0.96f, 0.78f, 0.25f, 0.67f), ImVec4(0.98f, 0.8f, 0.25f, 1.0f),
            ImVec4(0.45f, 0.35f, 0.1f, 0.54f), ImVec4(0.95f, 0.75f, 0.2f, 0.4f), ImVec4(0.96f, 0.78f, 0.25f, 0.67f),
            ImVec4(0.95f, 0.75f, 0.2f, 0.4f), ImVec4(0.98f, 0.8f, 0.25f, 1.0f), ImVec4(0.98f, 0.8f, 0.1f, 1.0f),
            ImVec4(0.75f, 0.55f, 0.15f, 0.31f), ImVec4(0.96f, 0.78f, 0.25f, 0.8f), ImVec4(0.98f, 0.8f, 0.25f, 1.0f), ImVec4(0.5f, 0.48f, 0.4f, 0.5f),
        };
        static const char* const s_RMDImageOverrides[] = {
            "", "", "", "Input_BG.png", "Input_BGH.png", "Input_BG.png", "",
            "Checkbox_BG.png", "Checkbox_BG.png", "Checkbox_BG.png",
            "RMD_Button_Y.png", "RMD_Button_YA.png", "", "", "", "", ""
        };
        static const ImVec4 s_RMDWindowBgColors[] = {
            ImVec4(0.06f, 0.06f, 0.06f, 0.94f), ImVec4(0.06f, 0.06f, 0.06f, 0.94f), ImVec4(0.06f, 0.06f, 0.06f, 0.94f),
            ImVec4(0.06f, 0.06f, 0.06f, 0.94f), ImVec4(0.06f, 0.06f, 0.06f, 0.94f), ImVec4(0.06f, 0.06f, 0.06f, 0.94f), ImVec4(0.06f, 0.06f, 0.06f, 0.94f), ImVec4(0.06f, 0.06f, 0.06f, 0.94f)
        };
        static const char* const s_RMDWindowBgImages[] = {
            "InfoBG_Y2.png", "InfoBG_Y1.png", "InfoBG_Y2.png", "InfoBG_Y3.png", "InfoBG_Y4.png",
            "InfoBG_Y2.png", "InfoBG_Y2.png", "InfoBG_Y3.png"
        };
        s_UICustomColors.assign(s_RMDColors, s_RMDColors + s_ThemeColorCount);
        s_UIColorImageOverrides.resize(s_ThemeColorCount, "");
        for (int i = 0; i < s_ThemeColorCount && i < (int)(sizeof(s_RMDImageOverrides) / sizeof(s_RMDImageOverrides[0])); ++i)
            s_UIColorImageOverrides[i] = s_RMDImageOverrides[i];
        for (int w = 0; w < kWindowBgCount; ++w) {
            s_WindowBgColors[w] = s_RMDWindowBgColors[w];
            s_WindowBgImageOverrides[w] = s_RMDWindowBgImages[w];
        }
        for (int i = 3; i <= 5; ++i)
            D3D12::RequestFrameBgImageReload(i - 3, s_UIColorImageOverrides[i]);
        for (int i = 7; i <= 9; ++i)
            D3D12::RequestCheckboxImageReload(i - 7, s_UIColorImageOverrides[i]);
        for (int i = 10; i <= 12; ++i)
            D3D12::RequestButtonImageReload(i - 10, s_UIColorImageOverrides[i]);
        for (int w = 0; w < kWindowBgCount; ++w)
            D3D12::RequestWindowBgImageReload(w, s_WindowBgImageOverrides[w]);
        ApplyThemeColorsToStyle(s_UICustomColors);
    }
    else if (themeName == "RMD Dark")
    {
        static const ImVec4 s_RMDColors[] = {
            ImVec4(0.9f, 0.9f, 0.9f, 1.0f), ImVec4(0.45f, 0.45f, 0.45f, 1.0f), ImVec4(0.35f, 0.35f, 0.38f, 0.5f),
            ImVec4(0.18f, 0.2f, 0.24f, 0.54f), ImVec4(0.28f, 0.3f, 0.35f, 0.4f), ImVec4(0.32f, 0.34f, 0.4f, 0.67f), ImVec4(0.5f, 0.52f, 0.58f, 1.0f),
            ImVec4(0.18f, 0.2f, 0.24f, 0.54f), ImVec4(0.28f, 0.3f, 0.35f, 0.4f), ImVec4(0.32f, 0.34f, 0.4f, 0.67f),
            ImVec4(0.28f, 0.3f, 0.35f, 0.4f), ImVec4(0.45f, 0.48f, 0.55f, 1.0f), ImVec4(0.35f, 0.38f, 0.45f, 1.0f),
            ImVec4(0.22f, 0.24f, 0.28f, 0.31f), ImVec4(0.38f, 0.4f, 0.46f, 0.8f), ImVec4(0.45f, 0.48f, 0.55f, 1.0f), ImVec4(0.35f, 0.35f, 0.38f, 0.5f),
        };
        static const char* const s_RMDImageOverrides[] = {
            "", "", "", "Input_BG.png", "Input_BGH.png", "Input_BG.png", "",
            "Checkbox_BG.png", "Checkbox_BG.png", "Checkbox_BG.png",
            "RMD_Button_D.png", "RMD_Button_DA.png", "", "", "", "", ""
        };
        static const ImVec4 s_RMDWindowBgColors[] = {
            ImVec4(0.06f, 0.06f, 0.06f, 0.94f), ImVec4(0.06f, 0.06f, 0.06f, 0.94f), ImVec4(0.06f, 0.06f, 0.06f, 0.94f),
            ImVec4(0.06f, 0.06f, 0.06f, 0.94f), ImVec4(0.06f, 0.06f, 0.06f, 0.94f), ImVec4(0.06f, 0.06f, 0.06f, 0.94f), ImVec4(0.06f, 0.06f, 0.06f, 0.94f), ImVec4(0.06f, 0.06f, 0.06f, 0.94f)
        };
        static const char* const s_RMDWindowBgImages[] = {
            "InfoBG_D2.png", "InfoBG_D1.png", "InfoBG_D2.png", "InfoBG_D3.png", "InfoBG_D4.png",
            "InfoBG_D2.png", "InfoBG_D2.png", "InfoBG_D3.png"
        };
        s_UICustomColors.assign(s_RMDColors, s_RMDColors + s_ThemeColorCount);
        s_UIColorImageOverrides.resize(s_ThemeColorCount, "");
        for (int i = 0; i < s_ThemeColorCount && i < (int)(sizeof(s_RMDImageOverrides) / sizeof(s_RMDImageOverrides[0])); ++i)
            s_UIColorImageOverrides[i] = s_RMDImageOverrides[i];
        for (int w = 0; w < kWindowBgCount; ++w) {
            s_WindowBgColors[w] = s_RMDWindowBgColors[w];
            s_WindowBgImageOverrides[w] = s_RMDWindowBgImages[w];
        }
        for (int i = 3; i <= 5; ++i)
            D3D12::RequestFrameBgImageReload(i - 3, s_UIColorImageOverrides[i]);
        for (int i = 7; i <= 9; ++i)
            D3D12::RequestCheckboxImageReload(i - 7, s_UIColorImageOverrides[i]);
        for (int i = 10; i <= 12; ++i)
            D3D12::RequestButtonImageReload(i - 10, s_UIColorImageOverrides[i]);
        for (int w = 0; w < kWindowBgCount; ++w)
            D3D12::RequestWindowBgImageReload(w, s_WindowBgImageOverrides[w]);
        ApplyThemeColorsToStyle(s_UICustomColors);
    }
    else if (themeName == "Custom" && (int)s_UICustomColors.size() == s_ThemeColorCount)
    {
        ApplyThemeColorsToStyle(s_UICustomColors);
    }
    else
    {
        for (const auto& p : s_UIThemePresets)
        {
            if (p.first == themeName && (int)p.second.size() == s_ThemeColorCount)
            {
                ApplyThemeColorsToStyle(p.second);
                ClearThemeImageOverrides();
                ImVec4 windowBg = style.Colors[ImGuiCol_WindowBg];
                for (int w = 0; w < kWindowBgCount; ++w)
                    s_WindowBgColors[w] = windowBg;
                return;
            }
        }
        // Default: classic dark
        ImGui::StyleColorsDark();
        ClearThemeImageOverrides();
        for (int w = 0; w < kWindowBgCount; ++w)
            s_WindowBgColors[w] = style.Colors[ImGuiCol_WindowBg];
    }
}

void ShowSettingsPanel()
{
    if (!showSettingsPanel) return;

    float menuScale = GetMenuScaleFactor();
    ImVec2 panelSize = ImVec2(480.0f * menuScale, 560.0f * menuScale);
    ImVec2 centerPos = ImVec2((ImGui::GetIO().DisplaySize.x - panelSize.x) * 0.5f, (ImGui::GetIO().DisplaySize.y - panelSize.y) * 0.5f);
    ImGui::SetNextWindowPos(centerPos, ImGuiCond_Once);
    ImGui::SetNextWindowSize(panelSize, ImGuiCond_Once);

    ImGuiIO& io = ImGui::GetIO();
    int fontIndexTitle = 3;
    int fontIndexBody = 1;
    ImFont* fontTitle = (fontIndexTitle >= 0 && fontIndexTitle < io.Fonts->Fonts.Size) ? io.Fonts->Fonts[fontIndexTitle] : nullptr;
    ImFont* fontBody = (fontIndexBody >= 0 && fontIndexBody < io.Fonts->Fonts.Size) ? io.Fonts->Fonts[fontIndexBody] : nullptr;
    if (fontTitle) ImGui::PushFont(fontTitle);

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ShouldDrawWindowBackgroundImage(kWindowBg_ThemeControl) ? ImVec4(0, 0, 0, 0) : s_WindowBgColors[kWindowBg_ThemeControl]);
    if (ImGui::Begin("Theme Control", &showSettingsPanel, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar))
    {
        if (ShouldDrawWindowBackgroundImage(kWindowBg_ThemeControl))
            DrawWindowBackgroundImage(kWindowBg_ThemeControl);
        const char* panelTitle = "Theme Control";
        float titleWidth = ImGui::CalcTextSize(panelTitle).x;
        ImVec2 contentSize = ImGui::GetContentRegionAvail();
        float closeBtnSize = 18.0f;
        float padding = 5.0f;
        ImGui::SetCursorPosX((contentSize.x - titleWidth) * 0.5f);
        ImGui::TextColored(ImVec4(0.95f, 0.85f, 0.5f, 1.0f), "%s", panelTitle);
        ImGui::SameLine(contentSize.x - closeBtnSize - padding);
        ImVec2 btnPos = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("CloseBtnSettings", ImVec2(closeBtnSize, closeBtnSize));
        if (ImGui::IsItemClicked())
            showSettingsPanel = false;
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 textSize = ImGui::CalcTextSize("X");
        ImVec2 textPos = ImVec2(btnPos.x + (closeBtnSize - textSize.x) * 0.5f, btnPos.y + (closeBtnSize - textSize.y) * 0.5f);
        drawList->AddText(textPos, IM_COL32(255, 80, 80, 255), "X");
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0.0f, 4.0f * menuScale));
        if (fontTitle) ImGui::PopFont();
        if (fontBody) ImGui::PushFont(fontBody);

        // Build theme list: Default, Golden Sunset, Hell's Embrace, RMD Blue, RMD Red, RMD Purple, RMD Green, RMD Gold, RMD Dark, Custom, then user presets
        static std::vector<std::string> s_ThemeComboNames;
        s_ThemeComboNames.clear();
        s_ThemeComboNames.push_back("Default");
        s_ThemeComboNames.push_back("Golden Sunset");
        s_ThemeComboNames.push_back("Hell's Embrace");
        s_ThemeComboNames.push_back("RMD Blue");
        s_ThemeComboNames.push_back("RMD Red");
        s_ThemeComboNames.push_back("RMD Purple");
        s_ThemeComboNames.push_back("RMD Green");
        s_ThemeComboNames.push_back("RMD Gold");
        s_ThemeComboNames.push_back("RMD Dark");
        s_ThemeComboNames.push_back("Custom");
        for (const auto& p : s_UIThemePresets)
            s_ThemeComboNames.push_back(p.first);

        static int themeComboIndex = 0;
        themeComboIndex = 0;
        for (size_t i = 0; i < s_ThemeComboNames.size(); ++i)
            if (s_ThemeComboNames[i] == s_UITheme) { themeComboIndex = (int)i; break; }

        // --- Theme preset ---
        ImGui::Text("Theme preset");
        ImGui::SameLine(110.0f);
        ImGui::SetNextItemWidth(180.0f);
        BeginFrameBgImageRegion();
        if (ImGui::Combo("##Theme", &themeComboIndex, [](void* data, int idx, const char** out) {
            const auto* names = (const std::vector<std::string>*)data;
            if (idx >= 0 && idx < (int)names->size()) { *out = (*names)[idx].c_str(); return true; }
            return false;
            }, &s_ThemeComboNames, (int)s_ThemeComboNames.size()))
        {
            s_UITheme = s_ThemeComboNames[themeComboIndex];
            ApplyUITheme(s_UITheme);
            s_UICustomColors = GetCurrentThemeColors();  // update color pickers to show selected preset
            SaveFullGrailConfig(configFilePath, false);
        }
        EndFrameBgImageRegion();

        // Save current as new preset
        static char s_NewPresetName[64] = "";
        ImGui::SetNextItemWidth(180.0f);
        BeginFrameBgImageRegion();
        ImGui::InputTextWithHint("##NewPreset", "New preset name", s_NewPresetName, sizeof(s_NewPresetName));
        EndFrameBgImageRegion();
        ImGui::SameLine();
        if (ThemeButton("Save preset", ImVec2(100.0f, 0)))
        {
            std::string name(s_NewPresetName);
            if (!name.empty())
            {
                for (auto it = s_UIThemePresets.begin(); it != s_UIThemePresets.end(); ++it)
                    if (it->first == name) { s_UIThemePresets.erase(it); break; }
                s_UIThemePresets.push_back({ name, GetCurrentThemeColors() });
                s_NewPresetName[0] = '\0';
                SaveFullGrailConfig(configFilePath, false);
            }
        }

        ImGui::Dummy(ImVec2(0.0f, 4.0f));
        ImGui::Separator();
        ImGui::Text("Customize colors");
        ImGui::Dummy(ImVec2(0.0f, 2.0f));

        // Ensure Custom colors are populated when editing
        if ((int)s_UICustomColors.size() != s_ThemeColorCount)
            s_UICustomColors = GetCurrentThemeColors();

        if (ImGui::BeginChild("ThemeColors", ImVec2(0, -4.0f), true))
        {
            if ((int)s_UIColorImageOverrides.size() != s_ThemeColorCount)
                s_UIColorImageOverrides.resize(s_ThemeColorCount, "");
            static std::vector<std::string> s_ImageList;
            static int s_ImagePickerForSlot = -1;
            static bool s_OpenImagePickerThisFrame = false;
            static ImVec2 s_ImagePickerPos = ImVec2(0, 0);
            const float cameraBtnSize = 22.0f;
            int camW = 0, camH = 0;
            D3D12::GetCameraButtonTextureSize(&camW, &camH);
            ImTextureID cameraTexId = (ImTextureID)(uint64_t)D3D12::GetCameraButtonTextureId();
            for (int i = 0; i < s_ThemeColorCount; ++i)
            {
                ImGui::PushID(i);
                ImVec4* col = &s_UICustomColors[i];
                bool showImageBtn = (i >= 3 && i <= 5) || (i >= 7 && i <= 9) || (i >= 10 && i <= 12);
                bool imageActive = showImageBtn && (int)s_UIColorImageOverrides.size() > i && !s_UIColorImageOverrides[i].empty();
                if (imageActive)
                {
                    float patchSize = ImGui::GetFrameHeight();
                    ImGui::InvisibleButton("##colorpatch", ImVec2(patchSize, patchSize));
                    ImVec2 pmin = ImGui::GetItemRectMin();
                    ImVec2 pmax = ImGui::GetItemRectMax();
                    ImGui::GetWindowDrawList()->AddRectFilled(pmin, pmax, IM_COL32(50, 50, 50, 255));
                    if (cameraTexId && camW > 0 && camH > 0)
                    {
                        float scale = patchSize / (camW > camH ? (float)camW : (float)camH);
                        if (scale > 1.0f) scale = 1.0f;
                        ImVec2 drawSize((float)camW * scale, (float)camH * scale);
                        ImVec2 center(pmin.x + (patchSize - drawSize.x) * 0.5f, pmin.y + (patchSize - drawSize.y) * 0.5f);
                        ImGui::GetWindowDrawList()->AddImage(cameraTexId, center, ImVec2(center.x + drawSize.x, center.y + drawSize.y), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE);
                    }
                    ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
                    ImGui::TextUnformatted(s_ThemeColorEntries[i].label);
                    if (ImGui::IsItemHovered())
                    {
                        ImVec2 mouse = ImGui::GetIO().MousePos;
                        ImGui::SetNextWindowPos(ImVec2(mouse.x + 30.0f, mouse.y - 30.0f), ImGuiCond_Always);
                        const std::string& imgName = (int)s_UIColorImageOverrides.size() > i ? s_UIColorImageOverrides[i] : "";
                        ImGui::SetTooltip("Image: %s\nUse the image button to choose \"(none)\" to use color again.", imgName.empty() ? "(none)" : imgName.c_str());
                    }
                }
                else
                {
                    if (ImGui::ColorEdit4(s_ThemeColorEntries[i].label, (float*)col, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar))
                    {
                        s_UITheme = "Custom";
                        ApplyThemeColorsToStyle(s_UICustomColors);
                        SaveFullGrailConfig(configFilePath, false);
                    }
                }
                if (showImageBtn)
                {
                    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - cameraBtnSize - 8.0f);
                    if (ImGui::InvisibleButton("##ImgBtn", ImVec2(cameraBtnSize, cameraBtnSize)))
                    {
                        s_ImagePickerForSlot = i;
                        s_ImagePickerPos.x = ImGui::GetItemRectMin().x;
                        s_ImagePickerPos.y = ImGui::GetItemRectMax().y + 2.0f;
                        s_OpenImagePickerThisFrame = true;
                    }
                    if (cameraTexId && camW > 0 && camH > 0)
                    {
                        ImVec2 btnMin = ImGui::GetItemRectMin();
                        float scale = cameraBtnSize / (camW > camH ? (float)camW : (float)camH);
                        if (scale > 1.0f) scale = 1.0f;
                        ImVec2 drawSize((float)camW * scale, (float)camH * scale);
                        ImVec2 center(btnMin.x + (cameraBtnSize - drawSize.x) * 0.5f, btnMin.y + (cameraBtnSize - drawSize.y) * 0.5f);
                        ImGui::GetWindowDrawList()->AddImage(cameraTexId, center, ImVec2(center.x + drawSize.x, center.y + drawSize.y), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE);
                    }
                    if (ImGui::IsItemHovered())
                    {
                        ImVec2 mouse = ImGui::GetIO().MousePos;
                        ImGui::SetNextWindowPos(ImVec2(mouse.x + 30.0f, mouse.y - 30.0f), ImGuiCond_Always);
                        ImGui::SetTooltip("Use image from the 'D2RHUD_Images' folder for this element");
                    }
                }
                ImGui::PopID();
            }
            ImGui::Dummy(ImVec2(0.0f, 6.0f));
            ImGui::Separator();
            ImGui::Text("Window backgrounds");
            ImGui::Dummy(ImVec2(0.0f, 2.0f));
            static int s_WindowBgImagePickerSlot = -1;
            static bool s_OpenWindowBgImagePickerThisFrame = false;
            static ImVec2 s_WindowBgImagePickerPos = ImVec2(0, 0);
            for (int w = 0; w < kWindowBgCount; ++w)
            {
                ImGui::PushID(w + 1000);
                bool hasImage = !s_WindowBgImageOverrides[w].empty() && D3D12::GetWindowBgTextureId(w) != 0;
                if (hasImage)
                {
                    float patchSize = ImGui::GetFrameHeight();
                    ImGui::InvisibleButton("##winbgpatch", ImVec2(patchSize, patchSize));
                    ImVec2 pmin = ImGui::GetItemRectMin(), pmax = ImGui::GetItemRectMax();
                    ImGui::GetWindowDrawList()->AddRectFilled(pmin, pmax, IM_COL32(50, 50, 50, 255));
                    if (cameraTexId && camW > 0 && camH > 0)
                    {
                        float scale = patchSize / (camW > camH ? (float)camW : (float)camH);
                        if (scale > 1.0f) scale = 1.0f;
                        ImVec2 drawSize((float)camW * scale, (float)camH * scale);
                        ImVec2 center(pmin.x + (patchSize - drawSize.x) * 0.5f, pmin.y + (patchSize - drawSize.y) * 0.5f);
                        ImGui::GetWindowDrawList()->AddImage(cameraTexId, center, ImVec2(center.x + drawSize.x, center.y + drawSize.y), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE);
                    }
                    ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
                    ImGui::TextUnformatted(s_WindowBgNames[w]);
                    if (ImGui::IsItemHovered())
                    {
                        ImVec2 mouse = ImGui::GetIO().MousePos;
                        ImGui::SetNextWindowPos(ImVec2(mouse.x + 30.0f, mouse.y - 30.0f), ImGuiCond_Always);
                        ImGui::SetTooltip("Image: %s\nUse the image button to choose \"(none)\" to use color.", s_WindowBgImageOverrides[w].empty() ? "(none)" : s_WindowBgImageOverrides[w].c_str());
                    }
                }
                else if (ImGui::ColorEdit4(s_WindowBgNames[w], (float*)&s_WindowBgColors[w], ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar))
                    SaveFullGrailConfig(configFilePath, false);
                ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - cameraBtnSize - 8.0f);
                if (ImGui::InvisibleButton("##WinBgImgBtn", ImVec2(cameraBtnSize, cameraBtnSize)))
                {
                    s_WindowBgImagePickerSlot = w;
                    s_WindowBgImagePickerPos.x = ImGui::GetItemRectMin().x;
                    s_WindowBgImagePickerPos.y = ImGui::GetItemRectMax().y + 2.0f;
                    s_OpenWindowBgImagePickerThisFrame = true;
                }
                if (cameraTexId && camW > 0 && camH > 0)
                {
                    ImVec2 btnMin = ImGui::GetItemRectMin();
                    float scale = cameraBtnSize / (camW > camH ? (float)camW : (float)camH);
                    if (scale > 1.0f) scale = 1.0f;
                    ImVec2 drawSize((float)camW * scale, (float)camH * scale);
                    ImVec2 center(btnMin.x + (cameraBtnSize - drawSize.x) * 0.5f, btnMin.y + (cameraBtnSize - drawSize.y) * 0.5f);
                    ImGui::GetWindowDrawList()->AddImage(cameraTexId, center, ImVec2(center.x + drawSize.x, center.y + drawSize.y), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE);
                }
                if (ImGui::IsItemHovered())
                {
                    ImVec2 mouse = ImGui::GetIO().MousePos;
                    ImGui::SetNextWindowPos(ImVec2(mouse.x + 30.0f, mouse.y - 30.0f), ImGuiCond_Always);
                    ImGui::SetTooltip("Use image from D2RHUD_Images for this window background");
                }
                ImGui::PopID();
            }
            if (s_OpenWindowBgImagePickerThisFrame && s_WindowBgImagePickerSlot >= 0)
            {
                ImGui::SetNextWindowPos(s_WindowBgImagePickerPos);
                ImGui::OpenPopup("##WindowBgImagePicker");
                s_OpenWindowBgImagePickerThisFrame = false;
            }
            if (ImGui::BeginPopup("##WindowBgImagePicker", ImGuiWindowFlags_NoTitleBar))
            {
                if (s_WindowBgImagePickerSlot >= 0 && s_WindowBgImagePickerSlot < kWindowBgCount)
                {
                    int w = s_WindowBgImagePickerSlot;
                    static std::vector<std::string> s_WindowBgImageList;
                    s_WindowBgImageList.clear();
                    s_WindowBgImageList.push_back("(none)");
                    try {
                        std::string imagesPath = D3D12::GetD2RHUDImagesPath();
                        if (std::filesystem::exists(imagesPath) && std::filesystem::is_directory(imagesPath)) {
                            for (const auto& entry : std::filesystem::directory_iterator(imagesPath)) {
                                if (!entry.is_regular_file()) continue;
                                std::string ext = entry.path().extension().string();
                                for (auto& c : ext) c = (char)tolower((unsigned char)c);
                                if (ext == ".png" || ext == ".jpg" || ext == ".jpeg")
                                    s_WindowBgImageList.push_back(entry.path().filename().string());
                            }
                            std::sort(s_WindowBgImageList.begin() + 1, s_WindowBgImageList.end());
                        }
                    }
                    catch (...) {}
                    const std::string& cur = s_WindowBgImageOverrides[w];
                    float listH = (float)((int)s_WindowBgImageList.size() * 22 + 8);
                    if (listH > 250.0f) listH = 250.0f;
                    if (ImGui::BeginChild("##WinBgImageDropList", ImVec2(320.0f, listH), true))
                    {
                        for (int k = 0; k < (int)s_WindowBgImageList.size(); ++k)
                        {
                            bool isSelected = (k == 0 && cur.empty()) || (k > 0 && s_WindowBgImageList[k] == cur);
                            if (ImGui::Selectable(s_WindowBgImageList[k].c_str(), isSelected))
                            {
                                s_WindowBgImageOverrides[w] = (k == 0) ? "" : s_WindowBgImageList[k];
                                D3D12::RequestWindowBgImageReload(w, s_WindowBgImageOverrides[w]);
                                SaveFullGrailConfig(configFilePath, false);
                                ImGui::CloseCurrentPopup();
                            }
                        }
                    }
                    ImGui::EndChild();
                }
                ImGui::EndPopup();
            }
            else
                s_WindowBgImagePickerSlot = -1;

            if (s_OpenImagePickerThisFrame && s_ImagePickerForSlot >= 0)
            {
                ImGui::SetNextWindowPos(s_ImagePickerPos);
                ImGui::OpenPopup("##ImagePicker");
                s_OpenImagePickerThisFrame = false;
            }
            if (ImGui::BeginPopup("##ImagePicker", ImGuiWindowFlags_NoTitleBar))
            {
                if (s_ImagePickerForSlot >= 0 && s_ImagePickerForSlot < s_ThemeColorCount)
                {
                    int idx = s_ImagePickerForSlot;
                    s_ImageList.clear();
                    s_ImageList.push_back("(none)");
                    try {
                        std::string imagesPath = D3D12::GetD2RHUDImagesPath();
                        if (std::filesystem::exists(imagesPath) && std::filesystem::is_directory(imagesPath)) {
                            for (const auto& entry : std::filesystem::directory_iterator(imagesPath)) {
                                if (!entry.is_regular_file()) continue;
                                std::string ext = entry.path().extension().string();
                                for (auto& c : ext) c = (char)tolower((unsigned char)c);
                                if (ext == ".png" || ext == ".jpg" || ext == ".jpeg")
                                    s_ImageList.push_back(entry.path().filename().string());
                            }
                            std::sort(s_ImageList.begin() + 1, s_ImageList.end());
                        }
                    }
                    catch (...) {}
                    const std::string& cur = s_UIColorImageOverrides[idx];
                    float listH = (float)((int)s_ImageList.size() * 22 + 8);
                    if (listH > 250.0f) listH = 250.0f;
                    if (ImGui::BeginChild("##ImageDropList", ImVec2(320.0f, listH), true))
                    {
                        for (int k = 0; k < (int)s_ImageList.size(); ++k)
                        {
                            bool isSelected = (k == 0 && cur.empty()) || (k > 0 && s_ImageList[k] == cur);
                            if (ImGui::Selectable(s_ImageList[k].c_str(), isSelected))
                            {
                                s_UIColorImageOverrides[idx] = (k == 0) ? "" : s_ImageList[k];
                                if (idx >= 3 && idx <= 5)
                                    D3D12::RequestFrameBgImageReload(idx - 3, s_UIColorImageOverrides[idx]);
                                else if (idx >= 7 && idx <= 9)
                                    D3D12::RequestCheckboxImageReload(idx - 7, s_UIColorImageOverrides[idx]);
                                else if (idx >= 10 && idx <= 12)
                                    D3D12::RequestButtonImageReload(idx - 10, s_UIColorImageOverrides[idx]);
                                ApplyThemeColorsToStyle(s_UICustomColors);  // apply transparent for image slots so image shows when loaded
                                SaveFullGrailConfig(configFilePath, false);
                                ImGui::CloseCurrentPopup();
                            }
                        }
                    }
                    ImGui::EndChild();
                }
                ImGui::EndPopup();
            }
            else
                s_ImagePickerForSlot = -1;
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }
    else
        ImGui::PopStyleColor();
    ImGui::End();
    if (fontBody) ImGui::PopFont();
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
        ApplyUITheme(s_UITheme);
        initialized = true;
    }

    float menuScale = GetMenuScaleFactor();
    ImVec2 windowSize = ImVec2(850.0f * menuScale, 520.0f * menuScale);
    ImVec2 centerPos = ImVec2((ImGui::GetIO().DisplaySize.x - windowSize.x) * 0.5f, (ImGui::GetIO().DisplaySize.y - windowSize.y) * 0.5f);
    ImGui::SetNextWindowPos(centerPos, ImGuiCond_Once);
    ImGui::SetNextWindowSize(windowSize, ImGuiCond_Once);

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags &= ~ImGuiConfigFlags_NoKeyboard;

    int fontIndex = 3;
    ImFont* fontSize = (fontIndex >= 0 && fontIndex < io.Fonts->Fonts.Size) ? io.Fonts->Fonts[fontIndex] : nullptr;
    if (fontSize) ImGui::PushFont(fontSize);

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ShouldDrawWindowBackgroundImage(kWindowBg_D2RHUDOptions) ? ImVec4(0, 0, 0, 0) : s_WindowBgColors[kWindowBg_D2RHUDOptions]);
    if (ImGui::Begin("D2RHUD Options", &showD2RHUDMenu, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar))
    {
        if (ShouldDrawWindowBackgroundImage(kWindowBg_D2RHUDOptions))
            DrawWindowBackgroundImage(kWindowBg_D2RHUDOptions);
        // --- CENTERED WINDOW TITLE WITH TOP-RIGHT CLOSE BUTTON ---
        ImGuiIO& io = ImGui::GetIO();
        int fontIndex = 3;
        ImFont* fontSize = (fontIndex >= 0 && fontIndex < io.Fonts->Fonts.Size) ? io.Fonts->Fonts[fontIndex] : nullptr;
        const char* windowTitle = "D2RHUD Options";
        float closeBtnSize = 20.0f * menuScale;
        float padding = -10.0f * menuScale;

        // Compute window content size
        ImVec2 contentSize = ImGui::GetContentRegionAvail();
        float titleWidth = ImGui::CalcTextSize(windowTitle).x;

        // --- Centered window title (themes icon only on Control Center) ---
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
        ImGui::SetColumnWidth(0, 280.0f * menuScale); // width of control column

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

                ThemeCheckbox(label, value);

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
        drawCheckbox("Chat Sounds", &d2rHUDConfig.ChatSounds, "Chat Sounds", "- Plays a notification sound when other players send chat\n- Does not play for your own messages\n- Controlled by D2RHUD options (not chat panel widget)\n\n", "Toggle this to enable or disable chat notification sounds", nullptr);
        if (ImGui::IsItemDeactivatedAfterEdit())
            SaveFullGrailConfig(configFilePath, false);

        bool chatSoundSelectionChanged = false;
        drawLabeledInput(
            "Chat Sound",
            [&]() {
                chatSoundSelectionChanged = DrawChatSoundSelector(menuScale);
            },
            "Chat Sound",
            "- Sound played when another player sends chat (if Chat Sounds is enabled)\n"
            "- Auto uses the first file in My Filters/sounds or the mod sounds folder\n"
            "- Pick a listed file or choose Custom path for any .wav, .mp3, or .flac\n\n",
            "Add sounds in D2RLoot Settings > Sounds, or paste a full path for custom",
            false,
            35.0f,
            nullptr
        );
        if (chatSoundSelectionChanged)
        {
            InvalidateChatSoundPathCache();
            SaveFullGrailConfig(configFilePath, false);
        }

        drawCheckbox("Floating Damage", &d2rHUDConfig.FloatingDamage, "Floating Damage", "- Displays floating damage values above monsters\n- Coalesces rapid ticks (poison, fire, etc.)\n- Optional DPS meter with rolling window\n- Customize appearance in the Floating Damage menu\n\n", "Enable or disable floating damage display", GetLockInfo(modName, &ModOverrideSettings::FloatingDamage));
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            FloatingDamage::GetConfig().enabled = d2rHUDConfig.FloatingDamage;
            settings.FloatingDamage = d2rHUDConfig.FloatingDamage;
            cachedSettings.FloatingDamage = d2rHUDConfig.FloatingDamage;
        }
        drawCheckbox("HP Rollover Mods", &d2rHUDConfig.HPRolloverMods, "HP Rollover Mods", "- Prevents HP rollovers on high player counts by:\n- Capping the maximum health bonus % applied to monsters\n- Applying damage reduction logic to the Player\n- Scales Reduction % by the calculated rollover amount\n\n", "This feature cannot guarantee no rollovers\nHowever, it should work for mods with retail-ish values", GetLockInfo(modName, &ModOverrideSettings::HPRolloverMods));

        // --- HPRolloverDifficulty ---
        const char* difficultyItems[] = { "Normal or Higher", "Nightmare or Higher", "Hell Only" };
        int difficultyIndex = d2rHUDConfig.HPRolloverDifficulty + 1;

        // label hover detection:
        drawLabeledInput(
            "HP Rollover Difficulty",
            [&]() {
                ImGui::PushItemWidth(250.0f * menuScale);
                BeginFrameBgImageRegion();
                if (ImGui::Combo("##HPRolloverDifficulty", &difficultyIndex,
                    difficultyItems, IM_ARRAYSIZE(difficultyItems)))
                {
                    d2rHUDConfig.HPRolloverDifficulty = difficultyIndex - 1;
                }
                EndFrameBgImageRegion();
                ImGui::PopItemWidth();
            },
            "HP Rollover Difficulty", "- Control the applied Difficulty of HP Rollover Mods\n\n", "Only valid if HP Rollover Mods are enabled", false, 35.0f, GetLockInfo(modName, &ModOverrideSettings::HPRolloverDifficulty)
        );

        ImGui::Dummy(ImVec2(0.0f, 3.0f));

        drawLabeledInput(
            "HP Rollover %",
            [&]() {
                ImGui::PushItemWidth(100.0f * menuScale);
                BeginFrameBgImageRegion();
                if (ImGui::InputInt("##HPRolloverPercent", &d2rHUDConfig.HPRolloverPercent, 1, 10))
                {
                    d2rHUDConfig.HPRolloverPercent =
                        std::clamp(d2rHUDConfig.HPRolloverPercent, 0, 100);
                }
                EndFrameBgImageRegion();
                ImGui::PopItemWidth();
            },
            "HP Rollover %", "- Controls the maximum amount of Damage reduction\n\n", "Only valid if HP Rollover Mods are enabled", true, 35.0f, GetLockInfo(modName, &ModOverrideSettings::HPRolloverPercent)
        );

        drawLabeledInput(
            "Sunder Value",
            [&]() {
                ImGui::PushItemWidth(100.0f * menuScale);
                BeginFrameBgImageRegion();
                if (ImGui::InputInt("##SunderValue", &d2rHUDConfig.SunderValue, 1, 10))
                {
                    d2rHUDConfig.SunderValue =
                        std::clamp(d2rHUDConfig.SunderValue, 0, 100);
                }
                EndFrameBgImageRegion();
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
        ImGui::SetCursorPosX((colLocalX + (colWidthLocal - titleW) * 0.5f) - 10.0f * menuScale);
        ImGui::TextColored(ImVec4(0.0157f, 0.380f, 0.8f, 1.0f), "%s", descriptionTitle.c_str());
        if (fontSize) ImGui::PopFont();

        // --- Description (right column, fixed width 570px) ---
        if (!descriptionText.empty())
        {
            const float colWidth = 550.0f * menuScale;
            const float paddingRight = 10.0f * menuScale;
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
        if (ThemeButton("Test Settings", ImVec2(buttonWidth, 0)))
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
            settings.sunderedMonUMods = cachedSettings.sunderedMonUMods;
            settings.SunderValue = cachedSettings.SunderValue;

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

        if (ThemeButton("Save Config", ImVec2(buttonWidth, 0)))
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
            settings.sunderedMonUMods = cachedSettings.sunderedMonUMods;
            settings.SunderValue = cachedSettings.SunderValue;

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

        ImGui::PopStyleColor();
        ImGui::End();
    }
    else
        ImGui::PopStyleColor();
}

void ShowHUDSettingsMenu()
{
    if (!showHUDSettingsMenu)
        return;


    EnableAllInput();
    CenterWindow(ImVec2(800, 400));
    static std::string hoveredKey;
    hoveredKey.clear();
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ShouldDrawWindowBackgroundImage(kWindowBg_ThemeControl) ? ImVec4(0, 0, 0, 0) : s_WindowBgColors[kWindowBg_ThemeControl]);
    PushFontSafe(3);
    if (ImGui::Begin("HUD Settings", &showHUDSettingsMenu, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar))
    {
        if (ShouldDrawWindowBackgroundImage(kWindowBg_ThemeControl))
            DrawWindowBackgroundImage(kWindowBg_ThemeControl);
        DrawWindowTitleAndClose("HUD Settings", &showHUDSettingsMenu);
        PopFontSafe(3);
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0.0f, 5.0f));
        DrawCreateFilterPopup();

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

        static bool hudTriedFilterLoad = false;
        if (!hudTriedFilterLoad && g_LootFilterHeader.Version.empty())
        {
            LoadLootFilterConfig("lootfilter_config.lua");
            LoadLootFilterLogic("lootfilter.lua");
            hudTriedFilterLoad = true;
        }
        if (!s_lootFilterUpdateStatus.empty())
        {
            if (s_lootFilterUpdateStatus.size() >= 9 && s_lootFilterUpdateStatus.compare(0, 9, "Updated to") == 0 && !s_lootFilterUpdateApplied)
            {
                if (!s_lootFilterNewVersion.empty())
                    g_LootFilterHeader.Version = s_lootFilterNewVersion;
                else
                    LoadLootFilterLogic(lootFile);
                s_lootFilterUpdateApplied = true;
            }
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::seconds>(now - s_lootFilterUpdateStatusTime).count() >= 4)
            {
                s_lootFilterUpdateStatus.clear();
                s_lootFilterUpdateStatusTime = {};
                s_lootFilterNewVersion.clear();
                s_lootFilterUpdateApplied = false;
            }
        }
        if (!g_LootFilterHeader.Version.empty())
            CenteredWrappedText("My D2RLoot Version: ", g_LootFilterHeader.Version);
        ImGui::SameLine(0.0f, 12.0f);
        if (ThemeButton(s_lootFilterUpdating ? "Updating...##hud" : "Update##hud"))
        {
            if (!s_lootFilterUpdating)
            {
                s_lootFilterUpdating = true;
                s_lootFilterUpdateStatus.clear();
                s_lootFilterUpdateApplied = false;
                std::thread(LootFilterUpdateThread).detach();
            }
        }
        if (!s_lootFilterUpdateStatus.empty())
        {
            ImGui::SameLine(0.0f, 8.0f);
            ImGui::TextColored(ImVec4(0.7f, 0.9f, 0.7f, 1.0f), "%s", s_lootFilterUpdateStatus.c_str());
        }

        // --- My Selected Filter: centered styled display + dropdown to change ---
        std::vector<std::string> orderedFiltersHUD = GetOrderedFilterList();
        std::string currentDisplayNameHUD = s_activeFilterInternalName.empty()
            ? (g_LootFilterHeader.Title.empty() ? "Custom" : g_LootFilterHeader.Title)
            : GetFilterDisplayName(s_activeFilterInternalName);
        CenteredWrappedText("My Selected Filter: ", currentDisplayNameHUD);

        float comboWidthHUD = 220.0f;
        ImVec2 availHUD = ImGui::GetContentRegionAvail();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availHUD.x - comboWidthHUD) * 0.5f);
        std::string comboLabelHUD = s_pendingFilter.empty() ? currentDisplayNameHUD : GetFilterDisplayName(s_pendingFilter);
        ImGui::SetNextItemWidth(comboWidthHUD);
        BeginFrameBgImageRegion();
        if (ImGui::BeginCombo("##selected_filter_hud", comboLabelHUD.c_str()))
        {
            for (const auto& internalName : orderedFiltersHUD)
            {
                std::string displayName = GetFilterDisplayName(internalName);
                bool isActive = (internalName == s_activeFilterInternalName);
                std::string label = displayName + (isActive ? " \xe2\x9c\x93" : "");
                if (ImGui::Selectable(label.c_str(), isActive))
                {
                    s_pendingFilter = internalName;
                }
            }
            ImGui::EndCombo();
        }
        EndFrameBgImageRegion();
        if (!s_pendingFilter.empty())
        {
            ImGui::Dummy(ImVec2(0.0f, 4.0f));
            std::string applyLabelHUD = "Apply \"" + GetFilterDisplayName(s_pendingFilter) + "\":";
            float applyWHUD = ImGui::CalcTextSize(applyLabelHUD.c_str()).x;
            ImVec2 availApplyHUD = ImGui::GetContentRegionAvail();
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availApplyHUD.x - applyWHUD) * 0.5f);
            ImGui::Text("%s", applyLabelHUD.c_str());
            float btnW1HUD = ImGui::CalcTextSize("Use this filter").x + ImGui::GetStyle().FramePadding.x * 2;
            ImVec2 availApply2HUD = ImGui::GetContentRegionAvail();
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availApply2HUD.x - btnW1HUD) * 0.5f);
            if (ThemeButton("Use this filter##hud"))
            {
                if (CopyModFilterToActive(s_pendingFilter))
                {
                    s_activeFilterInternalName = s_pendingFilter;
                    LoadLootFilterConfig(GetLootFilterConfigPath());
                    LoadLootFilterLogic(lootFile);
                }
                s_pendingFilter.clear();
            }
        }

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

                    if (ThemeCheckbox(items[i].first.c_str(), &boolValue))
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
                BeginFrameBgImageRegion();
                bool changed = ImGui::InputText(inputID.c_str(), buffer, sizeof(buffer));
                EndFrameBgImageRegion();
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
                    BeginFrameBgImageRegion();
                    bool changed = ImGui::InputText(inputID.c_str(), buffer, sizeof(buffer));
                    EndFrameBgImageRegion();
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

        ImGui::Dummy(ImVec2(0.0f, 3.0f));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
        float btnImportWHUD = ImGui::CalcTextSize("Import Filter").x + ImGui::GetStyle().FramePadding.x * 2;
        float btnOpenWHUD = ImGui::CalcTextSize("Open Filter").x + ImGui::GetStyle().FramePadding.x * 2;
        float btnCreateWHUD = ImGui::CalcTextSize("Create my own filter").x + ImGui::GetStyle().FramePadding.x * 2;
        float btnGuideWHUD = ImGui::CalcTextSize("Filter Guide").x + ImGui::GetStyle().FramePadding.x * 2;
        float spacingBtnsHUD = ImGui::GetStyle().ItemSpacing.x;
        float totalBtnWHUD2 = btnImportWHUD + btnOpenWHUD + btnCreateWHUD + btnGuideWHUD + spacingBtnsHUD * 3;
        ImVec2 availBtnsHUD = ImGui::GetContentRegionAvail();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availBtnsHUD.x - totalBtnWHUD2) * 0.5f);
        if (ThemeButton("Import Filter##hud"))
            OpenImportPathInput();
        ImGui::SameLine();
        if (ThemeButton("Open Filter##hud"))
        {
            std::string path = GetLootFilterConfigPathAbsolute();
            if (std::filesystem::exists(path))
                OpenInShell(path);
        }
        ImGui::SameLine();
        if (ThemeButton("Create my own filter##hud"))
            OpenCreateFilterPopup();
        ImGui::SameLine();
        if (ThemeButton("Filter Guide##hud"))
        {
            OpenInShell("https://locbones.github.io/D2RLAN-LootFilterGuide");
        }
        DrawImportFilterPathRow();
        ImGui::Dummy(ImVec2(0.0f, 3.0f));

        // --- Bottom Description ---
        std::string desc = "Hover over an option to see its description.";
        if (!hoveredKey.empty() && g_LuaDescriptions.count(hoveredKey))
            desc = g_LuaDescriptions.at(hoveredKey);

        DrawBottomDescription(desc);

        ImGui::PopStyleColor();
        ImGui::End();
    }
    else
        ImGui::PopStyleColor();
}

void ShowMainMenu()
{
    if (!showMainMenu)
        return;

    ImGuiIO& io = ImGui::GetIO();
    float menuScale = GetMenuScaleFactor();
    ImVec2 windowSize = ImVec2(640.0f * menuScale, 450.0f * menuScale);

    // Center the window only on the first run
    static bool firstRun = true;
    if (firstRun)
    {
        ImVec2 centerPos = ImVec2((io.DisplaySize.x - windowSize.x) * 0.5f, (io.DisplaySize.y - windowSize.y) * 0.5f);
        ImGui::SetNextWindowPos(centerPos, ImGuiCond_Once);
        firstRun = false;
    }

    ImGui::SetNextWindowSize(windowSize, ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ShouldDrawWindowBackgroundImage(kWindowBg_ControlCenter) ? ImVec4(0, 0, 0, 0) : s_WindowBgColors[kWindowBg_ControlCenter]);
    if (ImGui::Begin("D2RHUD Control Center", &showMainMenu, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar))
    {
        if (ShouldDrawWindowBackgroundImage(kWindowBg_ControlCenter))
            DrawWindowBackgroundImage(kWindowBg_ControlCenter);
        // --- HEADER ---
        int fontIndex = 3;
        ImFont* fontSize = (fontIndex >= 0 && fontIndex < io.Fonts->Fonts.Size) ? io.Fonts->Fonts[fontIndex] : nullptr;
        if (fontSize) ImGui::PushFont(fontSize);

        const char* windowTitle = "D2RHUD Control Center";
        float closeBtnSize = 20.0f * menuScale;
        float padding = 5.0f * menuScale;
        ImVec2 contentSize = ImGui::GetContentRegionAvail();
        float titleWidth = ImGui::CalcTextSize(windowTitle).x;

        // --- Gear icon at top-left: opens Settings panel (uses theme.png if available, else shows "Settings" text) ---
        float gearBtnSize = 28.0f * menuScale;
        float leftPadding = 8.0f * menuScale;
        ImGui::SetCursorPosX(leftPadding);
        ImGui::InvisibleButton("SettingsGear", ImVec2(gearBtnSize, gearBtnSize));
        if (ImGui::IsItemClicked())
            showSettingsPanel = true;
        if (ImGui::IsItemHovered())
        {
            ImVec2 mouse = ImGui::GetIO().MousePos;
            ImGui::SetNextWindowPos(ImVec2(mouse.x + 30.0f, mouse.y - 30.0f), ImGuiCond_Always);
            ImGui::SetTooltip("Theme Control (Beta)");
        }
        ImVec2 gearMin = ImGui::GetItemRectMin();
        int gearW = 0, gearH = 0;
        D3D12::GetGearTextureSize(&gearW, &gearH);
        ImTextureID gearTexId = (ImTextureID)(uint64_t)D3D12::GetGearTextureId();
        if (gearTexId && gearW > 0 && gearH > 0) {
            ImVec2 gearSize((float)gearW, (float)gearH);
            float scale = (gearBtnSize - 4.0f) / (gearSize.x > gearSize.y ? gearSize.x : gearSize.y);
            if (scale > 1.0f) scale = 1.0f;
            ImVec2 drawSize(gearSize.x * scale, gearSize.y * scale);
            ImVec2 center(gearMin.x + (gearBtnSize - drawSize.x) * 0.5f, gearMin.y + (gearBtnSize - drawSize.y) * 0.5f);
            ImGui::GetWindowDrawList()->AddImage(gearTexId, center, ImVec2(center.x + drawSize.x, center.y + drawSize.y), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 255));
        }
        else {
            const char* label = "Settings";
            ImVec2 labelSize = ImGui::CalcTextSize(label);
            ImVec2 textPos(gearMin.x + (gearBtnSize - labelSize.x) * 0.5f, gearMin.y + (gearBtnSize - labelSize.y) * 0.5f);
            ImGui::GetWindowDrawList()->AddText(textPos, IM_COL32(230, 230, 230, 255), label);
        }

        // --- Centered title ---
        ImGui::SameLine((contentSize.x - titleWidth) * 0.5f);
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
        float buttonWidth = 200.0f * menuScale;
        float buttonHeight = 50.0f * menuScale;
        float separatorX = buttonWidth + 20.0f * menuScale;
        float descriptionWidth = windowSize.x - separatorX - 15.0f * menuScale;
        const char* descriptionTitle = "";
        const char* descriptionText = "";
        const char* descriptionNote = "";

        fontIndex = 2;
        fontSize = (fontIndex >= 0 && fontIndex < io.Fonts->Fonts.Size) ? io.Fonts->Fonts[fontIndex] : nullptr;
        if (fontSize) ImGui::PushFont(fontSize);

        if (ThemeButton("D2RHUD Options", ImVec2(buttonWidth, buttonHeight)))
            showD2RHUDMenu = true;
        ShowD2RHUDMenu();
        if (showSettingsPanel)
            ShowSettingsPanel();
        if (ImGui::IsItemHovered()) { descriptionTitle = "D2RHUD Options"; descriptionText = "Explore and Control enabled D2RHUD Options\n\n- Values are retrieved and stored in D2RLAN/Launcher/config.json\n- Overrides can be applied by the author in data/D2RLAN/config_override.json\n- Expect implementation changes over the next updates"; }

        if (ThemeButton("D2RLoot Settings", ImVec2(buttonWidth, buttonHeight)))
            showLootMenu = true;
        ShowLootMenu();
        if (ImGui::IsItemHovered()) { descriptionTitle = "D2RLoot Settings"; descriptionText = "Explore and control your currently active loot filter\n\n- Filters operate in real-time with user-defined rules\n- Accessible in D2RLAN > Options > Loot Filter\n- Rules are defined in D2RLAN/D2R/lootfilter_config.lua"; }

        if (ThemeButton("Grail Tracker", ImVec2(buttonWidth, buttonHeight)))
            showGrailMenu = true;
        ShowGrailMenu();
        if (ImGui::IsItemHovered()) { descriptionTitle = "Grail Tracker"; descriptionText = "View the progress of your Set/Unique item hunting\n\n- Grail Entries are manually stored for now\n- This feature works for all mods* (or TCP)\n(Mod must have included set/unique items.txt files)\n- Grail Progress/Settings are stored in D2RLAN/D2R/HUD_Settings_ModName.json"; }

        if (ThemeButton("Hotkey Controls", ImVec2(buttonWidth, buttonHeight)))
            showHotkeyMenu = true;
        ShowHotkeyMenu();
        if (ImGui::IsItemHovered()) { descriptionTitle = "Hotkey Controls"; descriptionText = "Manage your hotkeys used by various tools\n\n- Hotkeys are achieved by utilizing internal game functions\n- They can also be used to dynamically control your loot filter\n- Hotkeys are defined in D2RLAN/Launcher/D2RLAN_Config.txt"; }

        if (ThemeButton("Camera Controls", ImVec2(buttonWidth, buttonHeight)))
            showCameraMenu = true;
        ShowCameraMenu();
        if (ImGui::IsItemHovered()) { descriptionTitle = "Camera Controls"; descriptionText = "Modify the in-game camera angles and zoom\n\n- Scans memory to find camera control values\n- Angle/zoom scans required for every fresh session\n- Future versions likely to include more automation/controls"; }

        if (ThemeButton("Floating Damage", ImVec2(buttonWidth, buttonHeight)))
            showFloatingDamageMenu = true;
        ShowFloatingDamageMenu();
        if (ImGui::IsItemHovered()) { descriptionTitle = "Floating Damage"; descriptionText = "Customize floating damage display and DPS meter\n\n- Pop/fade animations, hit combining, and spread columns\n- Enable or disable in D2RHUD Options"; }

        if (fontSize) ImGui::PopFont();

        // Vertical separator + Description Panel (on 4K, move description down ~300px so it isn't shifted too high)
        float descriptionPanelY = 100.0f + (menuScale >= 2.0f ? 100.0f : 0.0f);
        ImGui::GetWindowDrawList()->AddLine(ImVec2(ImGui::GetWindowPos().x + separatorX, ImGui::GetWindowPos().y + descriptionPanelY), ImVec2(ImGui::GetWindowPos().x + separatorX, ImGui::GetWindowPos().y + windowSize.y - 3.0f), IM_COL32(180, 150, 80, 255), 2.0f);
        ImGui::SetCursorPosX(separatorX - 200.0f);
        ImGui::SetCursorPosY(descriptionPanelY);

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

        ImGui::PopStyleColor();
        ImGui::End();
    }
    else
        ImGui::PopStyleColor();
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

    if (g_lastSunderGame)
    {
        ApplySunderClampToMonster(g_lastSunderGame, pUnit, true);
        return;
    }

    // Get stored remainders
    RemainderEntry remainders = GetRemainder(pUnit);

    // Helper lambda to handle each stat
    auto ApplyFinalValue = [&](D2C_ItemStats statId, int remainder, const char* name)
        {
            if (remainder <= 0 || settings.sunderedMonUMods != true)
                return;

            int nCurrentValue = STATLIST_GetUnitStatSigned(pUnit, statId, 0);

            if (nCurrentValue >= 100)
                STATLISTEX_SetStatListExStat(pUnit->pStatListEx, statId, settings.SunderValue, 0);
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
    if (pGame)
        g_lastSunderGame = pGame;

    if (pUnit && pUnit->dwUnitType == UNIT_MONSTER)
    {
        g_unitsEditedStats.erase(pUnit->dwUnitId);
        g_resistRemainders.erase(pUnit->dwUnitId);
    }

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
    ApplyGhettoSunder(pGame, pRoom, pUnit, pMonRegData, &monStatsInit);

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

    // Only update and show our terror zone schedule when the player has completed the Baal quest
    if (pGame && pUnitPlayer && GetBaalQuest(pUnitPlayer, pGame))
    {
        UpdateActiveZoneInfoText(static_cast<time_t>(std::time(nullptr)));

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
        LogDebug("debug logging: {}\n---------------------\n");
        oDropTCTest(pGame, pMonster, pPlayer, nTCId, nQuality, nItemLevel, a7, ppItems, pnItemsDropped, nMaxItems);
        return;
    }
    else
        ForceTCDrops(pGame, pMonster, pPlayer, nTCId, nQuality, nItemLevel, a7, ppItems, pnItemsDropped, nMaxItems);

    LogDebug("debug logging: {}\n---------------------\n");
}

#pragma region Floating Damage

static ImVec2 g_LastDisplaySize = ImVec2(1920.0f, 1080.0f);
static bool g_DamageInfoHookInstalled = false;

typedef void(__fastcall* DamageInfo_t)(void* param1, D2UnitStrc* attacker, D2UnitStrc* target, int damage, int param5, void* param6, int param7, void* param8, char param9, void* param10);
DamageInfo_t oDamageInfo = nullptr;

static bool IsFloatingDamageActive()
{
    return d2rHUDConfig.FloatingDamage || settings.FloatingDamage || cachedSettings.FloatingDamage;
}

void __fastcall Hooked_DamageInfo(void* param1, D2UnitStrc* attacker, D2UnitStrc* target, int baseDamage, int param5, void* param6, int finalDamage, void* param8, char param9, void* param10);

static bool LooksLikeGamePointer(const void* ptr)
{
    const uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    return addr >= 0x10000;
}

static bool IsCommittedReadableMemory(const void* address, size_t size)
{
    if (!LooksLikeGamePointer(address) || size == 0)
        return false;

    const uint8_t* cursor = static_cast<const uint8_t*>(address);
    const uint8_t* end = cursor + size;

    while (cursor < end)
    {
        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery(cursor, &mbi, sizeof(mbi)) == 0)
            return false;
        if (mbi.State != MEM_COMMIT)
            return false;
        if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))
            return false;

        const uint8_t* regionEnd = static_cast<const uint8_t*>(mbi.BaseAddress) + mbi.RegionSize;
        if (regionEnd <= cursor)
            return false;

        cursor = regionEnd;
    }

    return true;
}

static bool SafeReadShortString(const char* src, char* dst, size_t dstSize)
{
    if (!src || !dst || dstSize == 0 || !LooksLikeGamePointer(src))
        return false;

    __try
    {
        for (size_t i = 0; i + 1 < dstSize; ++i)
        {
            const char c = src[i];
            dst[i] = c;
            if (c == '\0')
                return true;
        }
        dst[dstSize - 1] = '\0';
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        if (dstSize > 0)
            dst[0] = '\0';
        return false;
    }
}

static bool NameEqualsI(const char* value, const char* token)
{
    return value && token && _stricmp(value, token) == 0;
}

static bool NameContainsI(const char* value, const char* token)
{
    if (!value || !token || !token[0])
        return false;

    const size_t tokenLen = strlen(token);
    if (tokenLen == 0)
        return false;

    for (const char* cursor = value; *cursor; ++cursor)
    {
        if (_strnicmp(cursor, token, tokenLen) == 0)
            return true;
    }
    return false;
}

static FloatingDamage::Element ElementFromDamageTypeName(const char* name)
{
    if (!name || !name[0])
        return FloatingDamage::Element::Physical;

    if (NameEqualsI(name, "Fire") || NameEqualsI(name, "Burn"))
        return FloatingDamage::Element::Fire;
    if (NameEqualsI(name, "Lightning") || NameEqualsI(name, "Ltng") || NameEqualsI(name, "Light") || NameEqualsI(name, "ligt"))
        return FloatingDamage::Element::Lightning;
    if (NameEqualsI(name, "Cold"))
        return FloatingDamage::Element::Cold;
    if (NameEqualsI(name, "Poison") || NameEqualsI(name, "Pois") || NameEqualsI(name, "pois"))
        return FloatingDamage::Element::Poison;
    if (NameEqualsI(name, "Magic") || NameEqualsI(name, "Mag") || NameEqualsI(name, "magc"))
        return FloatingDamage::Element::Magic;
    if (NameEqualsI(name, "Physical") || NameEqualsI(name, "Phys") || NameEqualsI(name, "Attack"))
        return FloatingDamage::Element::Physical;

    if (NameContainsI(name, "POISON:") || NameContainsI(name, " poison damage"))
        return FloatingDamage::Element::Poison;

    if (NameContainsI(name, "ligt") || NameContainsI(name, "Lightning") || NameContainsI(name, "Ltng"))
        return FloatingDamage::Element::Lightning;
    if (NameContainsI(name, "Fire"))
        return FloatingDamage::Element::Fire;
    if (NameContainsI(name, "Cold"))
        return FloatingDamage::Element::Cold;
    if (NameContainsI(name, "pois") || NameContainsI(name, "Poison"))
        return FloatingDamage::Element::Poison;
    if (NameContainsI(name, "magc") || NameContainsI(name, "Magic"))
        return FloatingDamage::Element::Magic;

    return FloatingDamage::Element::Physical;
}

static FloatingDamage::Element ElementFromReductionType(int32_t type)
{
    switch (type)
    {
    case 1: return FloatingDamage::Element::Fire;
    case 2: return FloatingDamage::Element::Lightning;
    case 3: return FloatingDamage::Element::Cold;
    case 4: return FloatingDamage::Element::Poison;
    case 5: return FloatingDamage::Element::Magic;
    case 0:
    default: return FloatingDamage::Element::Physical;
    }
}

static FloatingDamage::Element ElementFromTypeIndex(int index)
{
    switch (index)
    {
    case 1: return FloatingDamage::Element::Fire;
    case 2: return FloatingDamage::Element::Lightning;
    case 3: return FloatingDamage::Element::Cold;
    case 4: return FloatingDamage::Element::Poison;
    case 5: return FloatingDamage::Element::Magic;
    case 0:
    default: return FloatingDamage::Element::Physical;
    }
}

static bool SafeReadShortWideStringToUtf8(const wchar_t* src, char* dst, size_t dstSize)
{
    if (!src || !dst || dstSize == 0 || !LooksLikeGamePointer(src))
        return false;

    __try
    {
        wchar_t buffer[128]{};
        for (size_t i = 0; i + 1 < 128; ++i)
        {
            buffer[i] = src[i];
            if (src[i] == L'\0')
                break;
        }

        const int written = WideCharToMultiByte(CP_UTF8, 0, buffer, -1, dst, static_cast<int>(dstSize), nullptr, nullptr);
        return written > 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        if (dstSize > 0)
            dst[0] = '\0';
        return false;
    }
}

static bool SafeReadBlzString(const blz_string* src, char* dst, size_t dstSize)
{
    if (!src || !dst || dstSize == 0 || !LooksLikeGamePointer(src))
        return false;
    if (!IsCommittedReadableMemory(src, sizeof(blz_string)))
        return false;

    __try
    {
        const char* text = src->str;
        if (!text || !LooksLikeGamePointer(text))
        {
            if (src->length > 0 && src->length < sizeof(src->data))
                text = src->data;
            else
                return false;
        }

        return SafeReadShortString(text, dst, dstSize);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        if (dstSize > 0)
            dst[0] = '\0';
        return false;
    }
}

static bool TextLooksLikeDamageType(const char* text)
{
    if (!text || !text[0])
        return false;

    return NameEqualsI(text, "Physical") ||
        NameEqualsI(text, "Phys") ||
        NameEqualsI(text, "Attack") ||
        NameEqualsI(text, "ligt") ||
        NameEqualsI(text, "magc") ||
        NameEqualsI(text, "pois") ||
        NameContainsI(text, "POISON:") ||
        NameContainsI(text, " poison damage") ||
        NameContainsI(text, "Fire") ||
        NameContainsI(text, "Cold") ||
        NameContainsI(text, "Lightning") ||
        NameContainsI(text, "Light") ||
        NameContainsI(text, "Ltng") ||
        NameContainsI(text, "ligt") ||
        NameContainsI(text, "Poison") ||
        NameContainsI(text, "pois") ||
        NameContainsI(text, "Magic") ||
        NameContainsI(text, "magc") ||
        NameContainsI(text, "Burn");
}

static FloatingDamage::Element TryElementFromReadableText(const char* text)
{
    if (!TextLooksLikeDamageType(text))
        return FloatingDamage::Element::Physical;

    return ElementFromDamageTypeName(text);
}

static FloatingDamage::Element TryElementFromStringCandidate(const void* candidate)
{
    if (!candidate || !LooksLikeGamePointer(candidate))
        return FloatingDamage::Element::Physical;

    char text[256]{};

    if (SafeReadShortString(static_cast<const char*>(candidate), text, sizeof(text)))
    {
        const FloatingDamage::Element element = TryElementFromReadableText(text);
        if (element != FloatingDamage::Element::Physical)
            return element;
    }

    if (SafeReadShortWideStringToUtf8(static_cast<const wchar_t*>(candidate), text, sizeof(text)))
    {
        const FloatingDamage::Element element = TryElementFromReadableText(text);
        if (element != FloatingDamage::Element::Physical)
            return element;
    }

    if (SafeReadBlzString(static_cast<const blz_string*>(candidate), text, sizeof(text)))
    {
        const FloatingDamage::Element element = TryElementFromReadableText(text);
        if (element != FloatingDamage::Element::Physical)
            return element;
    }

    if (IsCommittedReadableMemory(candidate, sizeof(void*)))
    {
        __try
        {
            const char* indirect = *static_cast<const char* const*>(candidate);
            if (indirect && SafeReadShortString(indirect, text, sizeof(text)))
            {
                const FloatingDamage::Element element = TryElementFromReadableText(text);
                if (element != FloatingDamage::Element::Physical)
                    return element;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    return FloatingDamage::Element::Physical;
}

static FloatingDamage::Element TryElementFromStatTable(const D2DamageStatTableStrc* pTable)
{
    if (!pTable)
        return FloatingDamage::Element::Physical;

    char typeName[32]{};
    if (pTable->szName && SafeReadShortString(pTable->szName, typeName, sizeof(typeName)))
    {
        const FloatingDamage::Element fromName = ElementFromDamageTypeName(typeName);
        if (fromName != FloatingDamage::Element::Physical ||
            NameEqualsI(typeName, "Physical") ||
            NameEqualsI(typeName, "Phys") ||
            NameEqualsI(typeName, "Attack"))
        {
            return fromName;
        }
    }

    return ElementFromReductionType(pTable->nDamageReductionType);
}

static const char* AdvancePastColorCodes(const char* text)
{
    while (text && *text)
    {
        if (static_cast<unsigned char>(text[0]) == 0xFF && text[1] == 'c' && text[2])
        {
            text += 3;
            continue;
        }
        break;
    }
    return text;
}

static const char* FindCaseInsensitive(const char* haystack, const char* needle)
{
    if (!haystack || !needle || !needle[0])
        return nullptr;

    const size_t needleLen = strlen(needle);
    for (const char* cursor = haystack; *cursor; ++cursor)
    {
        if (_strnicmp(cursor, needle, needleLen) == 0)
            return cursor;
    }
    return nullptr;
}

static D2GameStrc* GetActiveGame()
{
    D2Client* pGameClient = GetClientPtr();
    if (!pGameClient)
        return nullptr;

    return reinterpret_cast<D2GameStrc*>(pGameClient->pGame);
}

static bool TryReadTextCandidate(const void* candidate, char* dst, size_t dstSize)
{
    if (!candidate || !dst || dstSize == 0 || !LooksLikeGamePointer(candidate))
        return false;

    if (SafeReadShortString(static_cast<const char*>(candidate), dst, dstSize) && dst[0])
        return true;

    if (SafeReadShortWideStringToUtf8(static_cast<const wchar_t*>(candidate), dst, dstSize) && dst[0])
        return true;

    if (SafeReadBlzString(static_cast<const blz_string*>(candidate), dst, dstSize) && dst[0])
        return true;

    return false;
}

static void CollectHookCombatText(void* param1, void* param6, void* param8, void* param10, char* dst, size_t dstSize)
{
    if (!dst || dstSize == 0)
        return;

    dst[0] = '\0';

    const void* candidates[] = { param8, param10, param6, param1 };
    for (const void* candidate : candidates)
    {
        char text[512]{};
        if (!TryReadTextCandidate(candidate, text, sizeof(text)))
            continue;

        if (!dst[0] || strlen(text) > strlen(dst))
            strncpy_s(dst, dstSize, text, _TRUNCATE);
    }
}

static int ParseCombatLogDamageAmount(const char* text)
{
    if (!text || !text[0])
        return 0;

    const char* dmgLabel = FindCaseInsensitive(text, " poison damage");
    if (dmgLabel && dmgLabel > text)
    {
        const char* cursor = dmgLabel;
        while (cursor > text && cursor[-1] == ' ')
            --cursor;

        int amount = 0;
        int multiplier = 1;
        while (cursor > text && cursor[-1] >= '0' && cursor[-1] <= '9')
        {
            amount += (cursor[-1] - '0') * multiplier;
            multiplier *= 10;
            --cursor;
        }

        if (amount > 0)
            return amount;
    }

    const char* cursor = text;
    while ((cursor = FindCaseInsensitive(cursor, "takes")) != nullptr)
    {
        cursor += 5;
        for (int attempt = 0; attempt < 8 && *cursor; ++attempt)
        {
            cursor = AdvancePastColorCodes(cursor);
            while (*cursor == ' ')
                ++cursor;

            if (*cursor >= '0' && *cursor <= '9')
            {
                int amount = 0;
                while (*cursor >= '0' && *cursor <= '9')
                {
                    amount = (amount * 10) + (*cursor - '0');
                    ++cursor;
                }

                if (amount > 0)
                    return amount;
            }

            if (static_cast<unsigned char>(*cursor) == 0xFF && cursor[1] == 'c' && cursor[2])
                cursor += 3;
            else if (*cursor)
                ++cursor;
        }
    }

    return 0;
}

static bool ExtractBracketedName(const char* text, char* out, size_t outSize)
{
    if (!text || !out || outSize == 0)
        return false;

    const char* open = strchr(text, '[');
    if (!open)
        return false;

    ++open;
    const char* close = strchr(open, ']');
    if (!close || close <= open)
        return false;

    const size_t length = static_cast<size_t>(close - open);
    if (length == 0 || length >= outSize)
        return false;

    memcpy(out, open, length);
    out[length] = '\0';
    return true;
}

static const char* StripLeadingColorCodes(const char* text, char* scratch, size_t scratchSize)
{
    if (!text)
        return "";

    strncpy_s(scratch, scratchSize, text, _TRUNCATE);
    char* cursor = scratch;
    while (*cursor)
    {
        char* next = cursor;
        if (static_cast<unsigned char>(*next) == 0xFF && next[1] == 'c' && next[2])
            next += 3;
        if (next == cursor)
            break;
        cursor = next;
    }

    return cursor;
}

static bool UnitNameMatches(const char* unitName, const char* bracketName)
{
    char strippedUnit[128]{};
    char strippedBracket[128]{};
    const char* lhs = StripLeadingColorCodes(unitName, strippedUnit, sizeof(strippedUnit));
    const char* rhs = StripLeadingColorCodes(bracketName, strippedBracket, sizeof(strippedBracket));

    if (!lhs[0] || !rhs[0])
        return false;

    return _stricmp(lhs, rhs) == 0 || NameContainsI(lhs, rhs) || NameContainsI(rhs, lhs);
}

static D2UnitStrc* FindClientMonsterByDisplayName(const char* displayName)
{
    if (!displayName || !displayName[0])
        return nullptr;

    auto* ppClientUnitList = reinterpret_cast<D2UnitStrc**>(Pattern::Address(unitDataOffset));
    if (!ppClientUnitList)
        return nullptr;

    D2UnitStrc** ppMonsterList = &ppClientUnitList[UNIT_MONSTER * 0x80];
    for (int bucket = 0; bucket < 0x80; ++bucket)
    {
        for (D2UnitStrc* pUnit = ppMonsterList[bucket]; pUnit; pUnit = pUnit->pListNext)
        {
            if (!pUnit->pDynamicPath)
                continue;

            char nameBuf[128]{};
            const char* unitName = GetUnitName(reinterpret_cast<uint64_t>(pUnit), nameBuf);
            if (!unitName || !unitName[0])
                unitName = nameBuf;

            if (UnitNameMatches(unitName, displayName))
                return pUnit;
        }
    }

    return nullptr;
}

static bool ComputeScreenPositionForTarget(D2UnitStrc* attacker, D2UnitStrc* target, float& screenX, float& screenY)
{
    if (!target || !target->pDynamicPath)
        return false;

    if (!attacker || !attacker->pDynamicPath)
        attacker = target;

    D2DynamicPathStrc* a = attacker->pDynamicPath;
    D2DynamicPathStrc* t = target->pDynamicPath;

    constexpr float INV_SUBTILE = 1.0f / 65536.0f;

    const float ax = a->wPosX + a->wOffsetX * INV_SUBTILE;
    const float ay = a->wPosY + a->wOffsetY * INV_SUBTILE;
    const float tx = t->wPosX + t->wOffsetX * INV_SUBTILE;
    const float ty = t->wPosY + t->wOffsetY * INV_SUBTILE;
    const float dx = tx - ax;
    const float dy = ty - ay;

    const float isoX = dx - dy;
    const float isoY = dx + dy;
    const float pixelOffsetX = 40.0f + isoX * 30.0f;
    const float pixelOffsetY = 120.0f + isoY * -12.5f;

    screenX = g_LastDisplaySize.x * 0.5f + pixelOffsetX;
    screenY = g_LastDisplaySize.y * 0.5f - pixelOffsetY;
    return true;
}

static std::unordered_map<uint64_t, std::pair<FloatingDamage::Element, std::chrono::steady_clock::time_point>> g_targetElementCache;

static D2UnitStrc* GetClientUnitByIdAndType(uint32_t unitId, uint32_t unitType)
{
    auto* ppClientUnitList = reinterpret_cast<D2UnitStrc**>(Pattern::Address(unitDataOffset));
    if (!ppClientUnitList)
        return nullptr;

    D2UnitStrc** ppUnitList = &ppClientUnitList[unitType * 0x80];
    D2UnitStrc* pUnit = ppUnitList[unitId & 0x7F];
    while (pUnit && pUnit->dwUnitId != unitId)
        pUnit = pUnit->pListNext;

    return pUnit;
}

static bool ReadBlzStringMessage(const blz_string* msg, char* dst, size_t dstSize)
{
    if (!msg || !dst || dstSize == 0)
        return false;

    return SafeReadBlzString(msg, dst, dstSize);
}

// "ÿc4Playerÿc1: hello" or "Player: hello" -> Player
static bool TryParsePlayerChatSender(const char* text, std::string& outSender)
{
    if (!text || !text[0])
        return false;

    const char* p = text;
    while ((unsigned char)p[0] == 0xFF && p[1] == 'c' && p[2])
        p += 3;

    const char* colon = strchr(p, ':');
    if (!colon || colon == p || colon[1] != ' ')
        return false;

    const size_t nameLen = static_cast<size_t>(colon - p);
    if (nameLen == 0 || nameLen > 60)
        return false;

    for (size_t i = 0; i < nameLen; ++i)
    {
        const unsigned char c = static_cast<unsigned char>(p[i]);
        if (c == '\n' || c == '\r' || c == 0xFF)
            return false;
    }

    outSender.assign(p, nameLen);
    while (!outSender.empty() && (outSender.back() == ' ' || outSender.back() == '\t'))
        outSender.pop_back();

    return !outSender.empty();
}

static bool TryParsePlayerChatLine(const char* text, std::string& outSender, std::string& outBody)
{
    outSender.clear();
    outBody.clear();
    if (!TryParsePlayerChatSender(text, outSender))
        return false;

    const char* p = SkipD2ColorCodesForChat(text);
    const char* colon = strchr(p, ':');
    if (!colon || colon[1] != ' ')
        return false;

    outBody = SkipD2ColorCodesForChat(colon + 2);
    return !outBody.empty();
}

static D2UnitStrc* FindCachedPoisonTarget()
{
    const auto now = std::chrono::steady_clock::now();

    for (const auto& entry : g_targetElementCache)
    {
        if (entry.second.second <= now)
            continue;
        if (entry.second.first != FloatingDamage::Element::Poison)
            continue;

        const uint32_t unitType = static_cast<uint32_t>(entry.first >> 32);
        const uint32_t unitId = static_cast<uint32_t>(entry.first & 0xFFFFFFFFu);
        D2UnitStrc* pUnit = GetClientUnitByIdAndType(unitId, unitType);
        if (pUnit && pUnit->pDynamicPath)
            return pUnit;
    }

    return nullptr;
}

struct RecentDamageQueueEntry {
    uint64_t targetKey = 0;
    std::chrono::steady_clock::time_point queuedAt{};
};

static RecentDamageQueueEntry g_recentDamageQueue{};

struct PoisonWatchEntry {
    int32_t lastHpPoints = -1;
};

static std::unordered_map<uint64_t, PoisonWatchEntry> g_poisonWatch;
static std::unordered_map<uint64_t, std::chrono::steady_clock::time_point> g_lastPoisonHookQueue;
static std::unordered_map<uint64_t, std::chrono::steady_clock::time_point> g_lastNonPoisonDamageOnTarget;

static uint64_t MakeTargetElementKey(uint32_t unitType, uint32_t unitId);
static FloatingDamage::Element ResolveTargetElement(
    FloatingDamage::Element detected,
    uint32_t unitType,
    uint32_t unitId);

static void QueueTargetDamage(
    int amount,
    float screenX,
    float screenY,
    uint32_t unitType,
    uint32_t unitId,
    FloatingDamage::Kind kind,
    FloatingDamage::Element element)
{
    if (amount <= 0)
        return;

    if (element != FloatingDamage::Element::Poison)
    {
        const uint64_t targetKey = MakeTargetElementKey(unitType, unitId);
        const auto now = std::chrono::steady_clock::now();
        if (g_recentDamageQueue.targetKey == targetKey &&
            now - g_recentDamageQueue.queuedAt < std::chrono::milliseconds(20))
        {
            return;
        }

        g_recentDamageQueue = { targetKey, now };
        g_lastNonPoisonDamageOnTarget[targetKey] = now;
        SyncPoisonWatchHp(unitType, unitId);
    }

    FloatingDamage::QueueGameDamage(amount, screenX, screenY, unitType, unitId, kind, element);
}

static std::pair<uint32_t, uint32_t> g_lastPoisonTarget{ UINT32_MAX, UINT32_MAX };

static void TryQueuePoisonCombatMessage(const char* text, D2UnitStrc* targetOverride)
{
    if (!text || !text[0])
        return;

    if (!FindCaseInsensitive(text, "POISON:") && !FindCaseInsensitive(text, " poison damage"))
        return;

    const int amount = ParseCombatLogDamageAmount(text);
    if (amount <= 0)
        return;

    D2UnitStrc* target = targetOverride;
    if (!target)
    {
        char bracketName[128]{};
        if (ExtractBracketedName(text, bracketName, sizeof(bracketName)))
            target = FindClientMonsterByDisplayName(bracketName);
    }

    if (!target)
        target = GetClientUnitByIdAndType(g_lastPoisonTarget.second, g_lastPoisonTarget.first);

    if (!target)
        target = FindCachedPoisonTarget();

    if (!target || !target->pDynamicPath)
        return;

    g_lastPoisonTarget = { target->dwUnitType, target->dwUnitId };

    float screenX = 0.0f;
    float screenY = 0.0f;
    if (!ComputeScreenPositionForTarget(target, target, screenX, screenY))
        return;

    const FloatingDamage::Element element = ResolveTargetElement(
        FloatingDamage::Element::Poison,
        target->dwUnitType,
        target->dwUnitId);

    QueuePoisonDamageEvent(
        amount,
        screenX,
        screenY,
        target->dwUnitType,
        target->dwUnitId,
        element);
}

static FloatingDamage::Element DetectDamageElement(void* param1, int param5, void* param6, void* param8, void* param10)
{
    char combatText[512]{};
    CollectHookCombatText(param1, param6, param8, param10, combatText, sizeof(combatText));
    if (combatText[0])
    {
        const FloatingDamage::Element fromCombat = ElementFromDamageTypeName(combatText);
        if (fromCombat != FloatingDamage::Element::Physical)
            return fromCombat;
    }

    const FloatingDamage::Element fromParam8 = TryElementFromStringCandidate(param8);
    if (fromParam8 != FloatingDamage::Element::Physical)
        return fromParam8;

    const FloatingDamage::Element fromParam6Text = TryElementFromStringCandidate(param6);
    if (fromParam6Text != FloatingDamage::Element::Physical)
        return fromParam6Text;

    if (param6 && LooksLikeGamePointer(param6) &&
        IsCommittedReadableMemory(param6, sizeof(D2DamageStatTableStrc)))
    {
        __try
        {
            const auto* pTable = reinterpret_cast<const D2DamageStatTableStrc*>(param6);
            const FloatingDamage::Element fromTable = TryElementFromStatTable(pTable);
            if (fromTable != FloatingDamage::Element::Physical)
                return fromTable;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }
    else if (param6 && !LooksLikeGamePointer(param6))
    {
        const int typeIndex = static_cast<int>(reinterpret_cast<intptr_t>(param6));
        if (typeIndex >= 0 && typeIndex <= 16)
        {
            const FloatingDamage::Element fromIndex = ElementFromTypeIndex(typeIndex);
            if (fromIndex != FloatingDamage::Element::Physical)
                return fromIndex;
        }
    }

    if (param5 >= 0 && param5 <= 16)
    {
        const FloatingDamage::Element fromParam5 = ElementFromTypeIndex(param5);
        if (fromParam5 != FloatingDamage::Element::Physical)
            return fromParam5;
    }

    const FloatingDamage::Element fromParam10 = TryElementFromStringCandidate(param10);
    if (fromParam10 != FloatingDamage::Element::Physical)
        return fromParam10;

    const FloatingDamage::Element fromParam1 = TryElementFromStringCandidate(param1);
    if (fromParam1 != FloatingDamage::Element::Physical)
        return fromParam1;

    return FloatingDamage::Element::Physical;
}

static uint64_t MakeTargetElementKey(uint32_t unitType, uint32_t unitId)
{
    return (static_cast<uint64_t>(unitType) << 32) | static_cast<uint64_t>(unitId);
}

static FloatingDamage::Element ResolveTargetElement(
    FloatingDamage::Element detected,
    uint32_t unitType,
    uint32_t unitId)
{
    const uint64_t key = MakeTargetElementKey(unitType, unitId);
    const auto now = std::chrono::steady_clock::now();
    constexpr auto kDoTElementCacheDuration = std::chrono::seconds(60);

    if (detected != FloatingDamage::Element::Physical)
    {
        g_targetElementCache[key] = { detected, now + kDoTElementCacheDuration };
        return detected;
    }

    const auto it = g_targetElementCache.find(key);
    if (it != g_targetElementCache.end())
    {
        if (it->second.second > now)
            return it->second.first;

        g_targetElementCache.erase(it);
    }

    return FloatingDamage::Element::Physical;
}

static int32_t ReadUnitHpPoints(D2UnitStrc* pUnit)
{
    if (!pUnit)
        return -1;

    __try
    {
        return STATLIST_GetUnitStatSigned(pUnit, STAT_HITPOINTS, 0) >> 8;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return -1;
    }
}

static int32_t ReadUnitHpPointsById(uint32_t unitType, uint32_t unitId, D2UnitStrc* pClientUnit)
{
    if (D2GameStrc* pGame = GetActiveGame())
    {
        D2UnitStrc* pServerUnit = UNITS_GetServerUnitByTypeAndId(
            pGame,
            static_cast<D2C_UnitTypes>(unitType),
            unitId);
        if (pServerUnit)
        {
            const int32_t serverHp = ReadUnitHpPoints(pServerUnit);
            if (serverHp >= 0)
                return serverHp;
        }
    }

    return ReadUnitHpPoints(pClientUnit);
}

static void SyncPoisonWatchHp(uint32_t unitType, uint32_t unitId)
{
    D2UnitStrc* pUnit = GetClientUnitByIdAndType(unitId, unitType);
    if (!pUnit)
        return;

    const uint64_t key = MakeTargetElementKey(unitType, unitId);
    g_poisonWatch[key].lastHpPoints = ReadUnitHpPointsById(unitType, unitId, pUnit);
}

static bool IsTargetPoisonActive(uint32_t unitType, uint32_t unitId)
{
    const uint64_t key = MakeTargetElementKey(unitType, unitId);
    const auto now = std::chrono::steady_clock::now();

    const auto cacheIt = g_targetElementCache.find(key);
    if (cacheIt != g_targetElementCache.end() &&
        cacheIt->second.first == FloatingDamage::Element::Poison &&
        cacheIt->second.second > now)
    {
        return true;
    }

    D2UnitStrc* pUnit = GetClientUnitByIdAndType(unitId, unitType);
    if (!pUnit)
        return g_poisonWatch.find(key) != g_poisonWatch.end();

    __try
    {
        if (STATLIST_GetUnitStatSigned(pUnit, STAT_POISON_COUNT, 0) > 0)
            return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }

    return g_poisonWatch.find(key) != g_poisonWatch.end();
}

static void QueuePoisonDamageEvent(
    int amount,
    float screenX,
    float screenY,
    uint32_t unitType,
    uint32_t unitId,
    FloatingDamage::Element element)
{
    if (!IsFloatingDamageActive() || amount <= 0)
        return;

    const uint64_t key = MakeTargetElementKey(unitType, unitId);
    const auto now = std::chrono::steady_clock::now();
    const auto recentHookIt = g_lastPoisonHookQueue.find(key);
    if (recentHookIt != g_lastPoisonHookQueue.end() &&
        now - recentHookIt->second < std::chrono::milliseconds(8))
    {
        return;
    }

    g_lastPoisonHookQueue[key] = now;
    g_lastPoisonTarget = { unitType, unitId };
    ResolveTargetElement(FloatingDamage::Element::Poison, unitType, unitId);
    SyncPoisonWatchHp(unitType, unitId);

    FloatingDamage::QueueGameDamage(
        amount,
        screenX,
        screenY,
        unitType,
        unitId,
        FloatingDamage::Kind::Normal,
        element);
}

static void UpdatePoisonDoTWatch()
{
    if (!IsFloatingDamageActive())
        return;

    const auto now = std::chrono::steady_clock::now();
    constexpr auto kHookGracePeriod = std::chrono::milliseconds(120);
    constexpr auto kStaleWatchTimeout = std::chrono::seconds(3);

    std::vector<uint64_t> keys;
    keys.reserve(g_poisonWatch.size() + g_targetElementCache.size());
    for (const auto& entry : g_poisonWatch)
        keys.push_back(entry.first);
    for (const auto& entry : g_targetElementCache)
    {
        if (entry.second.first == FloatingDamage::Element::Poison && entry.second.second > now)
            keys.push_back(entry.first);
    }

    std::sort(keys.begin(), keys.end());
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());

    for (const uint64_t key : keys)
    {
        const uint32_t unitType = static_cast<uint32_t>(key >> 32);
        const uint32_t unitId = static_cast<uint32_t>(key & 0xFFFFFFFFu);

        D2UnitStrc* pUnit = GetClientUnitByIdAndType(unitId, unitType);
        if (!pUnit || !pUnit->pDynamicPath)
            continue;

        if (!IsTargetPoisonActive(unitType, unitId))
        {
            const auto watchIt = g_poisonWatch.find(key);
            if (watchIt != g_poisonWatch.end())
            {
                const auto lastHookIt = g_lastPoisonHookQueue.find(key);
                if (lastHookIt == g_lastPoisonHookQueue.end() ||
                    now - lastHookIt->second > kStaleWatchTimeout)
                {
                    g_poisonWatch.erase(watchIt);
                }
            }
            continue;
        }

        const auto hookIt = g_lastPoisonHookQueue.find(key);
        if (hookIt != g_lastPoisonHookQueue.end() && now - hookIt->second < kHookGracePeriod)
        {
            SyncPoisonWatchHp(unitType, unitId);
            continue;
        }

        const auto nonPoisonIt = g_lastNonPoisonDamageOnTarget.find(key);
        if (nonPoisonIt != g_lastNonPoisonDamageOnTarget.end() && now - nonPoisonIt->second < kHookGracePeriod)
        {
            SyncPoisonWatchHp(unitType, unitId);
            continue;
        }

        const int32_t hp = ReadUnitHpPointsById(unitType, unitId, pUnit);
        if (hp < 0)
            continue;

        PoisonWatchEntry& watch = g_poisonWatch[key];
        if (watch.lastHpPoints >= 0 && hp < watch.lastHpPoints)
        {
            const int delta = watch.lastHpPoints - hp;
            if (delta > 0)
            {
                float screenX = 0.0f;
                float screenY = 0.0f;
                if (ComputeScreenPositionForTarget(pUnit, pUnit, screenX, screenY))
                {
                    const FloatingDamage::Element element = ResolveTargetElement(
                        FloatingDamage::Element::Poison,
                        unitType,
                        unitId);

                    g_lastPoisonHookQueue[key] = now;
                    FloatingDamage::QueueGameDamage(
                        delta,
                        screenX,
                        screenY,
                        unitType,
                        unitId,
                        FloatingDamage::Kind::Normal,
                        element);
                }
            }
        }

        watch.lastHpPoints = hp;
    }
}

static bool IsPoisonStatTableRecord(const D2DamageStatTableStrc* pTable)
{
    if (!pTable)
        return false;

    if (TryElementFromStatTable(pTable) == FloatingDamage::Element::Poison)
        return true;

    return pTable->nDamageReductionType == 4;
}

static int ReadPoisonDamageAmount(D2DamageInfoStrc* pDamageInfo, D2DamageStatTableStrc* pTable)
{
    if (pTable && pTable->pOffsetInDamageStrc &&
        LooksLikeGamePointer(pTable->pOffsetInDamageStrc) &&
        IsCommittedReadableMemory(pTable->pOffsetInDamageStrc, sizeof(int32_t)))
    {
        const int amount = *pTable->pOffsetInDamageStrc;
        if (amount > 0)
            return amount;
    }

    if (pDamageInfo && pDamageInfo->pDamage &&
        LooksLikeGamePointer(pDamageInfo->pDamage) &&
        IsCommittedReadableMemory(pDamageInfo->pDamage, sizeof(D2DamageStrc)))
    {
        return pDamageInfo->pDamage->dwPoisDamage;
    }

    return 0;
}

static void TryQueuePoisonDamageFromStatTable(D2DamageInfoStrc* pDamageInfo, D2DamageStatTableStrc* pDamageStatTableRecord)
{
    if (!IsFloatingDamageActive())
        return;
    if (!pDamageInfo || !pDamageStatTableRecord || !pDamageInfo->pDefender)
        return;
    if (!pDamageInfo->pDefender->pDynamicPath)
        return;
    if (!IsCommittedReadableMemory(pDamageStatTableRecord, sizeof(D2DamageStatTableStrc)))
        return;

    __try
    {
        if (!IsPoisonStatTableRecord(pDamageStatTableRecord))
            return;

        const int amount = ReadPoisonDamageAmount(pDamageInfo, pDamageStatTableRecord);
        if (amount <= 0)
            return;

        D2UnitStrc* target = pDamageInfo->pDefender;
        D2UnitStrc* attacker = pDamageInfo->pAttacker;

        float screenX = 0.0f;
        float screenY = 0.0f;
        if (!ComputeScreenPositionForTarget(attacker, target, screenX, screenY))
            return;

        const FloatingDamage::Element resolved = ResolveTargetElement(
            FloatingDamage::Element::Poison,
            target->dwUnitType,
            target->dwUnitId);

        QueuePoisonDamageEvent(
            amount,
            screenX,
            screenY,
            target->dwUnitType,
            target->dwUnitId,
            resolved);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
}

static void EnsureDamageInfoHook();
static bool g_BroadcastChatHookInstalled = false;
static bool g_ChatManagerHookInstalled = false;

D2ChatManager* __fastcall Hooked_ChatManager_PushChatEntry(
    D2ChatManager* pMgr,
    blz_string& msg,
    uint32_t color,
    bool a4,
    ChatMsg& typeMsg,
    ChatOptionalStruct& a6,
    ChatOptionalStruct& a7,
    ChatOptionalStruct& a8,
    ChatOptionalStruct& a9)
{
    char text[512]{};
    const bool hasText = ReadBlzStringMessage(&msg, text, sizeof(text));

    if (IsFloatingDamageActive() && hasText)
        TryQueuePoisonCombatMessage(text);

    D2ChatManager* result = oChatManager_PushChatEntry(pMgr, msg, color, a4, typeMsg, a6, a7, a8, a9);

    if (IsGameSystemChatEntry(typeMsg))
        return result;

    // After the game finishes pushing chat (re-entrancy safe). Joiners usually only get this path.
    if (hasText)
    {
        std::string sender;
        std::string body;
        if (TryParsePlayerChatLine(text, sender, body))
            QueueChatNotificationForSender(sender.c_str(), body.c_str(), 0, 0, false);
    }

    return result;
}

static void EnsureChatManagerHook()
{
    if (g_ChatManagerHookInstalled || !oChatManager_PushChatEntry)
        return;

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&(PVOID&)oChatManager_PushChatEntry, Hooked_ChatManager_PushChatEntry);
    DetourTransactionCommit();
    g_ChatManagerHookInstalled = true;
}

void __fastcall Hooked_BroadcastChatMessage(uint64_t pGame, const char* szMsg, uint8_t color)
{
    // Disabled for chat sounds to avoid triggering on system/status broadcasts.

    if (szMsg && IsFloatingDamageActive())
    {
        char text[512]{};
        if (SafeReadShortString(szMsg, text, sizeof(text)))
            TryQueuePoisonCombatMessage(text);
    }

    oBroadcastChatMessage(pGame, szMsg, color);
}

static void EnsureBroadcastChatHook()
{
    if (g_BroadcastChatHookInstalled || !oBroadcastChatMessage)
        return;

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&(PVOID&)oBroadcastChatMessage, Hooked_BroadcastChatMessage);
    DetourTransactionCommit();
    g_BroadcastChatHookInstalled = true;
}

static void EnsureDamageInfoHook()
{
    if (g_DamageInfoHookInstalled)
        return;

    const uint64_t addr = Pattern::Address(0x27ab90);
    if (!addr)
        return;

    oDamageInfo = reinterpret_cast<DamageInfo_t>(addr);

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&(PVOID&)oDamageInfo, Hooked_DamageInfo);
    DetourTransactionCommit();
    g_DamageInfoHookInstalled = true;

    EnsureBroadcastChatHook();
    EnsureChatManagerHook();
    EnsureSunitdmHook();
}

void __fastcall Hooked_DamageInfo(void* param1, D2UnitStrc* attacker, D2UnitStrc* target, int baseDamage, int param5, void* param6, int finalDamage, void* param8, char param9, void* param10)
{
    char combatText[512]{};
    CollectHookCombatText(param1, param6, param8, param10, combatText, sizeof(combatText));

    const int actualDamage = finalDamage >> 8;
    const int parsedDamage = ParseCombatLogDamageAmount(combatText);
    const bool isPoisonCombat = FindCaseInsensitive(combatText, "POISON:") != nullptr ||
        FindCaseInsensitive(combatText, " poison damage") != nullptr;
    int displayDamage = actualDamage > 0 ? actualDamage : parsedDamage;
    if (isPoisonCombat && parsedDamage > 0)
        displayDamage = parsedDamage;
    const FloatingDamage::Element detectedElement = DetectDamageElement(param1, param5, param6, param8, param10);

    oDamageInfo(param1, attacker, target, baseDamage, param5, param6, finalDamage, param8, param9, param10);

    if (!target || !target->pDynamicPath)
        return;

    if (isPoisonCombat && parsedDamage > 0)
    {
        TryQueuePoisonCombatMessage(combatText, target);
        return;
    }

    if (displayDamage <= 0)
        return;

    if (!attacker || !attacker->pDynamicPath)
        attacker = target;

    const FloatingDamage::Element element = ResolveTargetElement(
        detectedElement,
        target->dwUnitType,
        target->dwUnitId);

    float screenX = 0.0f;
    float screenY = 0.0f;
    if (!ComputeScreenPositionForTarget(attacker, target, screenX, screenY))
        return;

    if (element == FloatingDamage::Element::Poison)
    {
        QueuePoisonDamageEvent(
            displayDamage,
            screenX,
            screenY,
            target->dwUnitType,
            target->dwUnitId,
            element);
        return;
    }

    QueueTargetDamage(
        displayDamage,
        screenX,
        screenY,
        target->dwUnitType,
        target->dwUnitId,
        FloatingDamage::Kind::Normal,
        element);
}

void ShowFloatingDamageMenu()
{
    static bool wasOpen = false;

    if (!showFloatingDamageMenu)
    {
        if (wasOpen)
            SaveFullGrailConfig(configFilePath, false);
        wasOpen = false;
        return;
    }

    wasOpen = true;

    float menuScale = GetMenuScaleFactor();
    ImVec2 windowSize = ImVec2(860.0f * menuScale, 820.0f * menuScale);
    ImGui::SetNextWindowSize(windowSize, ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Floating Damage", &showFloatingDamageMenu, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar))
    {
        DrawWindowTitleAndClose("Floating Damage", &showFloatingDamageMenu);
        FloatingDamage::DrawSettingsPanel(menuScale);
    }
    ImGui::End();
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

    RefreshLocalChatFilterNameCache();
    ProcessPendingChatNotificationSound();

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
        EnsureChatManagerHook();
        menuClickHookInstalled = true;

        g_ItemFilterStatusMessage = std::format("D2RHUD {} Loaded Successfully!", Version);
        g_ShouldShowItemFilterMessage = true;
        g_ItemFilterMessageStartTime = std::chrono::steady_clock::now();
    }

    UpdateChatSoundsFeature();

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

    if ((settings.HPRollover || cachedSettings.HPRollover || settings.sunderedMonUMods || cachedSettings.sunderedMonUMods) && !g_SunitdmHookInstalled)
        EnsureSunitdmHook();

        {
            ImGuiIO& io = ImGui::GetIO();
        g_LastDisplaySize = io.DisplaySize;

        const bool floatingDamageEnabled = IsFloatingDamageActive();
        const bool floatingDamagePreview =
            showFloatingDamageMenu || FloatingDamage::HasDisplayActivity();

        if (floatingDamageEnabled)
        {
            EnsureDamageInfoHook();
            FloatingDamage::GetConfig().enabled = true;
            UpdatePoisonDoTWatch();
        }
        else
        {
            FloatingDamage::GetConfig().enabled = false;
        }

        if (floatingDamageEnabled || floatingDamagePreview)
        {
            FloatingDamage::Update(io.DeltaTime);
            FloatingDamage::Render(ImGui::GetBackgroundDrawList(), io.DisplaySize);
        }
    }



    // Scale all menu UI by 200% on 4K resolution
    {
        bool anyMenuOpen = showMainMenu || showHUDSettingsMenu || showD2RHUDMenu || showSettingsPanel || showLootMenu || showHotkeyMenu || showGrailMenu || showCameraMenu || showFloatingDamageMenu;
        ImGuiIO& io = ImGui::GetIO();
        io.FontGlobalScale = anyMenuOpen ? GetMenuScaleFactor() : 1.0f;
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
    else if (display_size.y <= 2400)
        selectedFont = io.Fonts->Fonts[4];

    if (selectedFont)
    {
        ImGui::PushFont(selectedFont);
        fontPushed = true;
    }

    do
    {
        if (!gMouseHover->IsHovered) break;
        if (gMouseHover->HoveredUnitType > UNIT_MONSTER) break;

        D2UnitStrc* pUnit = nullptr;
        D2UnitStrc* pUnitServer = nullptr;
        if (pGame != nullptr)
        {
            pUnit = UNITS_GetServerUnitByTypeAndId(pGame, gMouseHover->HoveredUnitType, gMouseHover->HoveredUnitId);
            pUnitServer = UNITS_GetServerUnitByTypeAndId(pGame, gMouseHover->HoveredUnitType, gMouseHover->HoveredUnitId);
        }
        else
        {
            pUnit = GetClientUnitPtrFunc(Pattern::Address(unitDataOffset + 0x400 * gMouseHover->HoveredUnitType), gMouseHover->HoveredUnitId & 0x7F, gMouseHover->HoveredUnitId, gMouseHover->HoveredUnitType);
            pUnitServer = GetClientUnitPtrFunc(Pattern::Address(unitDataOffset + 0x400 * gMouseHover->HoveredUnitType), gMouseHover->HoveredUnitId & 0x7F, gMouseHover->HoveredUnitId, gMouseHover->HoveredUnitType);
        }

        // TCP/IP joiner is not server player id 1; use client list (same as item filter). Fallback keeps SP / host working if client list is not ready.
        D2UnitStrc* pUnitPlayer = GetClientPlayerUnit();
        if (!pUnitPlayer && pGame != nullptr)
            pUnitPlayer = UNITS_GetServerUnitByTypeAndId(pGame, UNIT_PLAYER, 1);

        for (int i = 0; i < 8; ++i)
        {
            auto pClient = gpClientList[i];
            if (!pClient) continue;

            uint32_t guid = pClient->dwUnitGUID;
            D2UnitStrc* pPlayer = UNITS_GetServerUnitByTypeAndId(pGame, UNIT_PLAYER, guid);
            if (!pPlayer) continue;

            TerrorStat = STATLIST_GetUnitStatSigned(pPlayer, 361, 0);
        }

        /* Debug Example - Retrieves stat references from D2Enums.h, Remove the // at start of line to use

        if (pUnitServer)
        {
            std::string mystatname = std::format("Defense: {}", STATLIST_GetUnitStatSigned(pUnitServer, 31, 0));
            drawList->AddText({ 20, 10 }, IM_COL32(170, 50, 50, 255), mystatname.c_str());
        }
        */

        if (!pUnit || !pUnitServer) break;

        if (STATLIST_GetUnitStatSigned(pUnitServer, STAT_HITPOINTS, 0) == 0) break;

        if (pUnitServer && (cachedSettings.monsterStatsDisplay || settings.monsterStatsDisplay))
        {
            if (pGame != nullptr)
                ApplySunderClampToMonster(pGame, pUnitServer, true);

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

            // function keys: F1â€“F24
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
        if (cmd.key.empty() || cmd.command.empty())
            continue;
        CheckAndAddMatch(cmd.key, 0, [cmd]() { ExecuteCommand(cmd.command); });
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

    if (auto kb = FindKeybind("Filtered Items Toggle"))
        CheckAndAddMatch(kb->key, 0, [=]() {
        ItemFilter::SetShowFilteredItems(!ItemFilter::GetShowFilteredItems());
            }, true);

    if (auto kb = FindKeybind("Cycle TZ Forward"))
    {
        if (modName == "RMD-MP")
        {
            if (TerrorStat == 1)
                CheckAndAddMatch(kb->key, 0, []() { CheckToggleForward(); });
        }
        else
            CheckAndAddMatch(kb->key, 0, []() { CheckToggleForward(); });
    }


    if (auto kb = FindKeybind("Cycle TZ Backward"))
    {
        if (modName == "RMD-MP")
        {
            if (TerrorStat == 1)
                CheckAndAddMatch(kb->key, 0, []() { CheckToggleBackward(); });
        }
        else
            CheckAndAddMatch(kb->key, 0, []() { CheckToggleBackward(); });
    }

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
