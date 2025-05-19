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

/*
- Chat Detours/Structures by Killshot
- Base Implementation by DannyisGreat
- Monster Stats and Plugin Mods by Bonesy
- Special Thanks to those who have helped ^^
*/

//Config file for handling chat coloring, stats display and memory edits
//Startup memory edits found in the config file are performed by D2RLAN 
std::string configFilePath = "config.json";
std::string filename = "../Launcher/D2RLAN_Config.txt";
std::string Version = "1.0.5 HF";

struct MonsterStatsDisplaySettings {
    bool monsterStatsDisplay;
    std::string channelColor;
    std::string playerNameColor;
    std::string messageColor;
    bool socketDisplay;
    bool HPRollover;
    std::int32_t HPRolloverAmt;
    std::int32_t HPRolloverDiff;
};
static MonsterStatsDisplaySettings cachedSettings;

static D2Client* GetClientPtr();

static D2DataTablesStrc* sgptDataTables = reinterpret_cast<D2DataTablesStrc*>(Pattern::Address(0x1c9e980));

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

#pragma region Item Name
const int32_t getItemNameBufferSize = 0x400;
const uint32_t getItemNameOffset = 0x149b60;

typedef void(__fastcall* GetItemName_t)(D2UnitStrc* pUnit, char* pBuffer);
static GetItemName_t oGetItemName = nullptr;

void __fastcall HookedGetItemName(D2UnitStrc* pUnit, char* pBuffer) {
    oGetItemName(pUnit, pBuffer);
    auto pUnitToUse = pUnit;
    D2GameStrc* pGame = nullptr;
    D2Client* pGameClient = GetClientPtr();
    if (pGameClient != nullptr) {
        pGame = (D2GameStrc*)pGameClient->pGame;
        if (pGame != nullptr) {
            pUnitToUse = UNITS_GetServerUnitByTypeAndId(pGame, UNIT_ITEM, pUnit->dwUnitId);
        }
    }
    if (cachedSettings.socketDisplay && pUnitToUse != nullptr) {
        int32_t nSockets = STATLIST_GetUnitStatSigned(pUnitToUse, STAT_ITEM_NUMSOCKETS, 0);
        if (nSockets > 0) {
            snprintf(pBuffer, getItemNameBufferSize, "%s ÿcN(%d)\0", pBuffer, nSockets);
        }
    }
}

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

struct SCMD_CUSTOM  {
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

typedef int64_t*(__fastcall* CCMD_ProcessClientGameMessage_t)(D2GameStrc* pGame, D2ClientStrc* pClient, uint8_t* pPacket, uint64_t nSize);
static CCMD_ProcessClientGameMessage_t oCCMD_ProcessClientGameMessage = nullptr; // D2GAME_PACKET_Handler_6FC89320 in D2MOO

typedef char(__fastcall* CCMD_ProcessClientSystemMessage_t)(uint8_t* pData, int64_t nSize);
static CCMD_ProcessClientSystemMessage_t oCCMD_ProcessClientSystemMessage = nullptr;

typedef void (__fastcall* SCMD_QueuePacket_t)(int64_t* pCMDManager, int32_t nClient, void** pPacketRange);
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


using SCMDHANDLER = int64_t(__fastcall*)(uint8_t* pPacket);
using SCMDHANDLEREX = int64_t(__fastcall*)(D2UnitStrc* pUnit, uint8_t* pPacket);

class D2SCMDStrc {
public:
    SCMDHANDLER* pfHandler;
    int64_t nPacketSize;
    SCMDHANDLEREX* pfHandlerEx;
};

using CCMDHANDLER = int64_t(__fastcall*)(D2GameStrc* pGame, D2UnitStrc* pUnit, uint8_t* pPacket, int nPacketSize);
class D2CCMDStrc {
public:
    CCMDHANDLER* pfHandler;
};

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
    byte pBuffer[0x47] = { 0x6c, 0x44, 0x1  };
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
    } else {
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

            // Handle any remaining command after the last comma
            std::string command = Trim(commands);
            if (!command.empty()) {
                automaticCommands.push_back(command);
            }

            break;
        }
    }

    // Handle less than 6 commands by filling with empty strings or skipping
    while (automaticCommands.size() < 6) {
        automaticCommands.push_back("");  // Optionally add empty strings if less than 6
    }

    // Now assign the values to your automaticCommand variables if necessary
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

        // Write formatted log entry
        logFile << "[" << std::put_time(localTime, "%a %b %d %T %Y") << "] " << message << std::endl;

        logFile.close();
    }
}
#pragma endregion

#pragma region Startup Options Control
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


MonsterStatsDisplaySettings getMonsterStatsDisplaySetting(const std::string& configFilePath) {
    StartPolling();
    std::vector<std::string> automaticCommands = ReadAutomaticCommandsFromFile(filename);
    static bool isCached = false;

    if (isCached) {
        return cachedSettings;
    }

    std::ifstream configFile(configFilePath);
    if (!configFile.is_open()) {
        std::cout << "Error: Could not open the config file: " << configFilePath << std::endl;
        return cachedSettings;
    }

    // Helper to clean a value (trim spaces, quotes, etc.)
    auto cleanValue = [](std::string value) -> std::string {
        value.erase(0, value.find_first_not_of(" \t\""));
        value.erase(value.find_last_not_of(" \t\n\r\",") + 1);
        return value;
        };

    // Helper to strip inline comments
    auto stripComments = [](std::string& line) {
        std::size_t commentPos = line.find("//");
        if (commentPos == std::string::npos)
            commentPos = line.find("#");
        if (commentPos != std::string::npos)
            line = line.substr(0, commentPos);
        };

    std::unordered_map<std::string, std::string> settings;
    std::string line;
    while (std::getline(configFile, line)) {
        stripComments(line);

        std::size_t pos = line.find(":");
        if (pos == std::string::npos) continue;

        std::string key = line.substr(0, pos);
        std::string value = cleanValue(line.substr(pos + 1));
        settings[cleanValue(key)] = value;
    }

    if (settings["MonsterStatsDisplay"] == "true") {
        cachedSettings.monsterStatsDisplay = true;
    }
    else if (settings["MonsterStatsDisplay"] == "false") {
        cachedSettings.monsterStatsDisplay = false;
    }
    else {
        std::cerr << "Error: 'MonsterStatsDisplay' must be true or false." << std::endl;
    }

    cachedSettings.channelColor = settings["Channel Color"];
    cachedSettings.playerNameColor = settings["Player Name Color"];
    cachedSettings.messageColor = settings["Message Color"];
    cachedSettings.socketDisplay = settings["SocketDisplay"] == "true";
    cachedSettings.HPRollover = settings["HPRolloverMods"] == "true";

    try {
        cachedSettings.HPRolloverAmt = std::stoi(settings["HPRollover%"]);
        cachedSettings.HPRolloverDiff = std::stoi(settings["HPRolloverDifficulty"]);
    }
    catch (const std::exception& e) {
        std::cerr << "Error parsing numeric values: " << e.what() << std::endl;
        cachedSettings.HPRolloverAmt = 0;
        cachedSettings.HPRolloverDiff = 0;
    }

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

void __fastcall HookedMONSTER_GetPlayerCountBonus(D2GameStrc* pGame, D2PlayerCountBonusStrc* pPlayerCountBonus, D2ActiveRoomStrc* pRoom, D2UnitStrc* pMonster) {
    oMONSTER_GetPlayerCountBonus(pGame, pPlayerCountBonus, pRoom, pMonster);
    // cap max hp bonus at 300%. once it gets to 500% + it can rollover quickly causing monsters to have negative hp.
    if (pPlayerCountBonus->nPlayerCount > 8 && pGame->nDifficulty > cachedSettings.HPRolloverDiff) {
        pPlayerCountBonus->nHP = 300;
    }
}

const int32_t nMaxPlayerCount = 65535;
float nMaxDamageReductionPercent = cachedSettings.HPRolloverAmt;
void __fastcall ScaleDamage(D2DamageInfoStrc* pDamageInfo, D2DamageStatTableStrc* pDamageStatTableRecord) {
    if (pDamageInfo->bDefenderIsMonster && *pDamageStatTableRecord->pOffsetInDamageStrc > 0) {
        auto nPlayerCount = STATLIST_GetUnitStatSigned(pDamageInfo->pDefender, STAT_MONSTER_PLAYERCOUNT, 0);
        if (nPlayerCount > 8) {
            float ratio = static_cast<float>(nPlayerCount - 8) / (nMaxPlayerCount - 8);
            float damageScale = 1.0f - ratio * (nMaxDamageReductionPercent / 100.0f);
            *pDamageStatTableRecord->pOffsetInDamageStrc *= damageScale;
            /*
            // Open log file in append mode
            std::ofstream log("d2r_hp.txt", std::ios::app);
            if (log.is_open()) {
                log << "Player count " << nPlayerCount << " scaling damage to " << (1.f - damageScale) * 100.f;
                log << "% reduction. Damage before " << std::dec << (*pDamageStatTableRecord->pOffsetInDamageStrc >> 8);
            }

            *pDamageStatTableRecord->pOffsetInDamageStrc *= damageScale;

            if (log.is_open()) {
                log << ". Damage after " << std::dec << (*pDamageStatTableRecord->pOffsetInDamageStrc >> 8) << std::endl;
                log.close(); // optional, handled by destructor, but explicit here
            }
            */
        }
    }
}

void __fastcall HookedSUNITDMG_ApplyResistancesAndAbsorb(D2DamageInfoStrc* pDamageInfo, D2DamageStatTableStrc* pDamageStatTableRecord, int32_t bDontAbsorb) {
    oSUNITDMG_ApplyResistancesAndAbsorb(pDamageInfo, pDamageStatTableRecord, bDontAbsorb);

    if (pDamageInfo->pGame->nDifficulty > cachedSettings.HPRolloverDiff) {
        ScaleDamage(pDamageInfo, pDamageStatTableRecord);
    }
}

#pragma endregion


#pragma region Draw Loop for Detours and Stats Display
void D2RHUD::OnDraw() {

    D2GameStrc* pGame = nullptr;
    D2Client* pGameClient = GetClientPtr();

    if (pGameClient != nullptr)
        pGame = (D2GameStrc*)pGameClient->pGame;

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
    }

    if (!oGetItemName) {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        oGetItemName = reinterpret_cast<GetItemName_t>(Pattern::Address(getItemNameOffset));
        DetourAttach(&(PVOID&)oGetItemName, HookedGetItemName);
        DetourTransactionCommit();
    }

    if (!oBankPanelDraw) {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        oBankPanelDraw = reinterpret_cast<BankPanelDraw_t>(Pattern::Address(bankPanelDraw));
        DetourAttach(&(PVOID&)oBankPanelDraw, HookedBankPanelDraw);
        DetourTransactionCommit();
        UpdateStashFileName(gSelectedPage);
        DWORD oldProtect = 0;
        std::cout << "gpCCMDHandlerTable " << std::hex << gpCCMDHandlerTable << std::endl;
        VirtualProtect(&gpCCMDHandlerTable[CCMD_CUSTOM_OP_CODE], sizeof(D2CCMDStrc), PAGE_EXECUTE_READWRITE, &oldProtect);
        gpCCMDHandlerTable[CCMD_CUSTOM_OP_CODE].pfHandler = (CCMDHANDLER*)&CCMDHANDLER_Custom;
        VirtualProtect(&gpCCMDHandlerTable[CCMD_CUSTOM_OP_CODE], sizeof(D2CCMDStrc), oldProtect, &oldProtect);

        std::cout << "gpSCMDHandlerTable " << std::hex << gpSCMDHandlerTable << std::endl;
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

    if (cachedSettings.HPRollover && !oSUNITDMG_ApplyResistancesAndAbsorb) {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        oSUNITDMG_ApplyResistancesAndAbsorb = reinterpret_cast<SUNITDMG_ApplyResistancesAndAbsorb_t>(Pattern::Address(0x3253d0));
        DetourAttach(&(PVOID&)oSUNITDMG_ApplyResistancesAndAbsorb, HookedSUNITDMG_ApplyResistancesAndAbsorb);
        DetourTransactionCommit();
    }

    if (cachedSettings.HPRollover && !oMONSTER_GetPlayerCountBonus) {
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
    

    if (!settings.monsterStatsDisplay)
        return;

    auto drawList = ImGui::GetBackgroundDrawList();
    auto min = drawList->GetClipRectMin();
    auto max = drawList->GetClipRectMax();
    auto width = max.x - min.x;
    auto center = width / 2.f;

    ImGuiIO& io = ImGui::GetIO();
    ImVec2 display_size = io.DisplaySize;
    float ypercent1 = display_size.y * 0.0745f;
    float ypercent2 = display_size.y * 0.043f;

    if (display_size.y <= 720)
        ImGui::PushFont(io.Fonts->Fonts[0]);
    if (display_size.y > 720 && display_size.y <= 900)
        ImGui::PushFont(io.Fonts->Fonts[1]);
    if (display_size.y > 900 && display_size.y <= 1080)
        ImGui::PushFont(io.Fonts->Fonts[2]);
    if (display_size.y > 1080 && display_size.y <= 1440)
        ImGui::PushFont(io.Fonts->Fonts[3]);
    if (display_size.y > 1440 && display_size.y <= 2160)
        ImGui::PushFont(io.Fonts->Fonts[4]);

    if (!gMouseHover->IsHovered) {
        return;
    }

    if (gMouseHover->HoveredUnitType > UNIT_MONSTER) {
        return;
    }

    D2UnitStrc* pUnit, * pUnitServer;
    //D2UnitStrc* pUnitPlayer = UNITS_GetServerUnitByTypeAndId(pGame, UNIT_PLAYER, 1);

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

    if (!pUnit || !pUnitServer)
        return;

    // std::cout << "Monster Stats Display is enabled." << std::endl;
    // Check if HP is greater than 0 (avoid displaying NPC stats)
    if (STATLIST_GetUnitStatSigned(pUnitServer, STAT_HITPOINTS, 0) != 0)
    {
        // Retrieve all Resistance stats and display them equally spaced and centered based on resolution variables
        if (pUnit)
        {
            float totalWidth = 0.f;
            float spaceWidth = ImGui::CalcTextSize(Seperator).x;
            std::string resistances[6];
            float widths[6];

            for (int i = 0; i < 6; i++)
            {
                int resistanceValue = STATLIST_GetUnitStatSigned(pUnit, ResistanceStats[i], 0);
                resistances[i] = std::format("{}", resistanceValue);
            }

            for (int i = 0; i < 6; i++)
            {
                if (i > 0)
                {
                    totalWidth += spaceWidth;
                }
                widths[i] = ImGui::CalcTextSize(resistances[i].c_str()).x;
                totalWidth += widths[i];
            }
            auto startX = center - (totalWidth / 2.f);
            for (int i = 0; i < 6; i++)
            {
                if (i > 0)
                {
                    startX += spaceWidth;
                }

                drawList->AddText({ startX, ypercent1 }, ResistanceColors[i], resistances[i].c_str());
                startX += widths[i];
            }

            // Retrieve HP stat and add both value and % of max to display
            if (pUnitServer)
            {
                // client has no HP data
                if (pGame == nullptr)
                {
                    auto clienthp = std::format("{}%", ((STATLIST_GetUnitStatSigned(pUnitServer, STAT_HITPOINTS, 0) >> 8) * 100) / (STATLIST_GetUnitStatSigned(pUnitServer, STAT_MAXHP, 0) >> 8));
                    auto width = ImGui::CalcTextSize(clienthp.c_str()).x;
                    drawList->AddText({ center - (width / 2.0f) + 1, ypercent2 }, IM_COL32(255, 255, 255, 255), clienthp.c_str());
                }
                else
                {
                    auto hp = std::format("{} / {}",
                        STATLIST_GetUnitStatSigned(pUnitServer, STAT_HITPOINTS, 0) >> 8,
                        STATLIST_GetUnitStatSigned(pUnitServer, STAT_MAXHP, 0) >> 8);
                    auto width = ImGui::CalcTextSize(hp.c_str()).x;
                    drawList->AddText({ center - (width / 2.0f) + 1, ypercent2 }, IM_COL32(255, 255, 255, 255), hp.c_str());
                }
            }

            /* Debug Example - Retrieves stat references from D2Enums.h, Remove the // at start of line to use */
            //std::string coldimmunity1 = std::format("EXP%: {}", STATLIST_GetUnitStatSigned(pUnitPlayer, 85, 0));
            //drawList->AddText({ 20, 10 }, IM_COL32(170, 50, 50, 255), coldimmunity1.c_str());
        }
    }
}

#pragma endregion

#pragma region Hotkey Handler
bool D2RHUD::OnKeyPressed(short key)
{
    std::unordered_map<std::string, const char*> commands = {
        { "Transmute: ", "unused" },
        { "Identify Items: ", "idall 1" },
        { "Force Save: ", "save 1" },
        { "Reset Stats: ", "resetstats 1" },
        { "Reset Skills: ", "resetskills 1" },
        { "Remove Ground Items: ", "itemgroundclear 1" },
        { "Custom Command 1: ", nullptr },
        { "Custom Command 2: ", nullptr },
        { "Custom Command 3: ", nullptr },
        { "Custom Command 4: ", nullptr },
        { "Custom Command 5: ", nullptr },
        { "Custom Command 6: ", nullptr },
    };

    for (const auto& [searchString, debugCommand] : commands) {
        if (debugCommand != nullptr) { // Standard Commands
            std::string result = ReadCommandFromFile(filename, searchString);
            if (!result.empty()) {
                auto it = keyMap.find(result);
                if (it != keyMap.end() && key == it->second) {
                    if (GetClientStatus() == 1)
                    {
                        if (searchString == "Transmute: ")
                            D2CLIENT_Transmute();
                        else
                            ExecuteDebugCheatFunc(debugCommand);
                    }

                    return true;
                }
            }
        }
        else { // Custom Commands
            std::string commandKey, commandValue;
            ReadCommandWithValuesFromFile(filename, searchString, commandKey, commandValue);

            if (!commandKey.empty() && !commandValue.empty()) {
                auto it = keyMap.find(commandKey);
                if (it != keyMap.end() && key == it->second) {
                    if (GetClientStatus() == 1) {
                        if (commandValue.find("/") != std::string::npos)
                            CLIENT_playerCommand(commandValue, commandValue);
                        else
                            ExecuteDebugCheatFunc(commandValue.c_str());
                    }
                    return true;
                }
            }
        }
    }

    // Track key presses for the version display
    if (key == VK_CONTROL) ctrlPressed = true;
    if (key == VK_MENU) altPressed = true;
    if (key == 'V') vPressed = true;

    // If CTRL + ALT + V are pressed together, show the message box
    if (ctrlPressed && altPressed && vPressed) {
        ShowVersionMessage();
        OnStashPageChanged(gSelectedPage + 1);
        ctrlPressed = altPressed = vPressed = false; // Reset state
        return true;
    }

    return false;
}

//Show D2RHUD Version Info as a MessageBox Popup
void D2RHUD::ShowVersionMessage()
{
    std::string message = "Version: " + std::string(Version);
    MessageBoxA(nullptr, message.c_str(), "Debug Display", MB_OK | MB_ICONINFORMATION);
}
#pragma endregion
