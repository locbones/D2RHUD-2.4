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
std::string lootFile = "../D2R/lootfilter.lua";
std::string Version = "1.3.2";

using json = nlohmann::json;
static MonsterStatsDisplaySettings cachedSettings;
static D2Client* GetClientPtr();
static D2DataTablesStrc* sgptDataTables = reinterpret_cast<D2DataTablesStrc*>(Pattern::Address(0x1c9e980));
ItemFilter* itemFilter = new ItemFilter();

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
const uint32_t modNameOffset = 0x1BF084F;


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
    cachedSettings.sunderedMonUMods = settings["SunderedMonUMods"] == "true";
    cachedSettings.minionEquality = settings["MinionEquality"] == "true";

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

    // cap max hp bonus at 300%. once it gets to 500% + it can rollover quickly causing monsters to have negative hp.
    if (pPlayerCountBonus->nPlayerCount > 8 && pGame->nDifficulty > cachedSettings.HPRolloverDiff) {
        pPlayerCountBonus->nHP = 300;
    }
}

const int32_t nMaxPlayerCount = 65535;
float nMaxDamageReductionPercent = cachedSettings.HPRolloverAmt; // e.g., 90 means 90% max reduction

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


            std::ofstream log("d2r_hp.txt", std::ios::app);
            if (log.is_open()) {
                log << "Player count: " << nPlayerCount
                    << " | Reduction: " << (1.0f - damageScale) * 100.0f << "%"
                    << " | Damage scale: " << damageScale << std::endl;
                log.close();
            }

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

#pragma region Terror Zones + Sunder

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
bool isTerrorized = false;
std::vector<StatAdjustment> gStatAdjustments;
std::unordered_map<int, std::string> gStatNames;
static std::vector<std::pair<D2C_ItemStats, int>> g_randomStats;
std::unordered_map<D2C_ItemStats, int> gRandomStatsForMonsters;
bool showStatAdjusts = true;
std::string g_ItemFilterStatusMessage = "";
bool g_ShouldShowItemFilterMessage = false;
std::chrono::steady_clock::time_point g_ItemFilterMessageStartTime;
//std::string g_TerrorizedStatusMessage = "";
//bool g_ShouldShowTerrorizedMessage = false;
//std::chrono::steady_clock::time_point g_TerrorizedMessageStartTime;

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

void ApplyUModArray(const uint32_t* offsets, size_t count, uint32_t remainder)
{
    for (size_t i = 0; i < count; ++i)
    {
        uint64_t addr = Pattern::Address(offsets[i]);
        if (!addr || addr < 0x10000)
            continue;

        uint8_t* pValue = reinterpret_cast<uint8_t*>(addr);
        uint8_t oldValue = *pValue;
        int newValue = static_cast<int>(oldValue) - static_cast<int>(remainder);

        if (newValue < 0)
            newValue = 0;

        // Skip writing if value is already correct
        if (static_cast<uint8_t>(newValue) == oldValue)
            continue;

        DWORD oldProtect;
        if (VirtualProtect(pValue, 1, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            *pValue = static_cast<uint8_t>(newValue);
            VirtualProtect(pValue, 1, oldProtect, &oldProtect);
        }
        else {
            MessageBoxA(nullptr, "Failed to change memory protection!", "Error", MB_OK | MB_ICONERROR);
        }
    }
}

static void LogDebug(const std::string& msg)
{
    std::ofstream log("debug_log.txt", std::ios::app);
    if (!log.is_open())
        return;

    log << msg << "\n";
}

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
    LogDebug(std::format("Monstats Row: {}, Difficulty: {}",rowIndex, diff));

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

    if (indexRegular == -1 && indexChamp == -1 && indexUnique == -1 && indexSuperUnique == -1)
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
            else if ((pMonsterFlag & MONTYPEFLAG_UNIQUE) || (cachedSettings.minionEquality && (pMonsterFlag & MONTYPEFLAG_MINION)))
                nTCId = nTCId + (uniqResult.treasureIndex - tcCheckUnique);
            else nTCId = nTCId + (regResult.treasureIndex - tcCheckRegular);

            LogDebug(std::format("nTCId Applied to Monster: {}\n---------------------\n", nTCId));
            oDropTCTest(pGame, pMonster, pPlayer, nTCId, nQuality, nItemLevel, a7, ppItems, pnItemsDropped, nMaxItems);
        }
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

BOOL CalculateMonsterStats(int monsterId, int gameType, int difficulty,
    int level, short flags, D2MonStatsInitStrc& outStats)
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

bool GetBaalQuest(D2UnitStrc* pPlayer, D2GameStrc* pGame) {
    if (!pPlayer || !pPlayer->pPlayerData || !pGame)
        return false;

    auto pQuestData = pPlayer->pPlayerData->pQuestData[pGame->nDifficulty];
    if (!pQuestData)
        return false;

    return pGame->bExpansion & oQUESTRECORD_GetQuestState(pQuestData, QUESTSTATEFLAG_A5Q6, QFLAG_REWARDGRANTED);
}

bool initialized = false;

std::string BuildTerrorZoneStatAdjustmentsText()
{
    if (initialized == false)
    {
        InitRandomStatsForAllMonsters(true);
        initialized = true;
    }

    if (gRandomStatsForMonsters.empty() || showStatAdjusts == false)
        return "";

    std::string finalText = "Stat Adjustments:\n";

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
    int remainingMinutes = 0;
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
            int secondsRemaining = totalSecondsInPhase - secondsIntoPhase;
            remainingMinutes = secondsRemaining / 60;
            remainingSeconds = secondsRemaining % 60;
        }
        else
        {
            // In break phase
            int totalSecondsInCycle = g_TerrorZoneData.cycleLengthMin * 60;
            int secondsIntoCycle = (currentUtc - g_TerrorZoneData.zoneStartUtc) % totalSecondsInCycle;
            int secondsRemaining = totalSecondsInCycle - secondsIntoCycle;
            remainingMinutes = secondsRemaining / 60;
            remainingSeconds = secondsRemaining % 60;
        }
    }

    std::string finalText = g_ActiveZoneInfoText +
        "Next Rotation In: " + std::to_string(remainingMinutes) + "m " +
        std::to_string(remainingSeconds) + "s\n";

    return finalText;
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

namespace {
    static double gLastManualToggleTime = 0;
}

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

static char gTZInfoText[256] = { 0 };
static char gTZStatAdjText[256] = { 0 };

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

uint32_t SubtractResistances(D2UnitStrc* pUnit, D2C_ItemStats nStatId, uint32_t nValue, uint16_t nLayer = 0) {
    auto nCurrentValue = STATLIST_GetUnitStatSigned(pUnit, nStatId, nLayer);

    if (nCurrentValue >= 100) {
        int newValue = nCurrentValue - nValue;

        //Calculate overshoot
        uint32_t remainder = 0;
        if (newValue < 99) {
            remainder = 99 - newValue;
            newValue = 99;
        }

        STATLISTEX_SetStatListExStat(pUnit->pStatListEx, nStatId, newValue, nLayer);
        return remainder;
    }

    return 0;
}

constexpr uint16_t UNIQUE_LAYER = 1337;

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

int GetLevelIdFromRoom(D2ActiveRoomStrc* pRoom)
{
    if (!pRoom || !pRoom->pDrlgRoom || !pRoom->pDrlgRoom->pLevel)
        return -1;

    return pRoom->pDrlgRoom->pLevel->nLevelId;
}

void AdjustMonsterLevel(D2UnitStrc* pUnit, D2C_ItemStats nStatId, uint32_t nValue, uint16_t nLayer = 0) {
    auto monsterLevel = STATLIST_GetUnitStatSigned(pUnit, nStatId, nLayer);

    if (playerLevel >= monsterLevel)
        STATLISTEX_SetStatListExStat(pUnit->pStatListEx, nStatId, nValue, nLayer);
}

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

static void ApplySunderForStat(D2UnitStrc* pUnit, D2C_ItemStats statId, int maxVal,
    const std::vector<std::pair<const char*, int>>& umodCaps,
    const std::vector<const uint32_t*>& umodArrays,
    const std::vector<size_t>& umodSizes,
    const std::string& statName)
{
    if (maxVal <= INT_MIN)
        return;

    int rem = SubtractResistances(pUnit, statId, maxVal);
    //LogSunder(statName + " max=" + std::to_string(maxVal) + " SubtractResistances rem=" + std::to_string(rem));

    for (size_t i = 0; i < umodArrays.size(); ++i) {
        int val = rem - umodCaps[i].second;
        if (val < 0) val = 0;
        if (val > umodCaps[i].second) val = umodCaps[i].second;

        //LogSunder(statName + " UMod[" + std::string(umodCaps[i].first) + "] cap=" + std::to_string(umodCaps[i].second) + " final=" + std::to_string(val));

        if (cachedSettings.sunderedMonUMods)
            ApplyUModArray(umodArrays[i], umodSizes[i], val);
    }
}

void __fastcall ApplyGhettoSunder(D2GameStrc* pGame, D2ActiveRoomStrc* pRoom, D2UnitStrc* pUnit, int64_t* pMonRegData, D2MonStatsInitStrc* monStatsInit)
{
    if (!pGame || !pUnit) {
        //LogSunder("Invalid game or unit pointer, aborting ApplyGhettoSunder.");
        return;
    }

    //LogSunder("=== Begin ApplyGhettoSunder ===");

    // Track max stat values
    int maxCold = INT_MIN, maxFire = INT_MIN, maxLight = INT_MIN;
    int maxPoison = INT_MIN, maxDamage = INT_MIN, maxMagic = INT_MIN;

    for (int i = 0; i < 8; ++i) {
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

        //LogSunder("Player[" + std::to_string(i) + "] GUID=" + std::to_string(guid) + " Cold=" + std::to_string(cold) + " Fire=" + std::to_string(fire) + " Light=" + std::to_string(light) + " Poison=" + std::to_string(poison) + " Damage=" + std::to_string(damage) + " Magic=" + std::to_string(magic));

        if (cold > maxCold) maxCold = cold;
        if (fire > maxFire) maxFire = fire;
        if (light > maxLight) maxLight = light;
        if (poison > maxPoison) maxPoison = poison;
        if (damage > maxDamage) maxDamage = damage;
        if (magic > maxMagic) maxMagic = magic;
    }

    // Apply each resist type with corresponding UMods
    if (STATLIST_GetUnitStatSigned(pUnit, STAT_COLDRESIST, 0) >= 100)
        ApplySunderForStat(pUnit, STAT_COLDRESIST, maxCold, { {"40",40},{"75",75},{"20",20} }, { umod8a_Offsets, umod18_Offsets, umod27a_Offsets }, { sizeof(umod8a_Offsets) / sizeof(umod8a_Offsets[0]), sizeof(umod18_Offsets) / sizeof(umod18_Offsets[0]), sizeof(umod27a_Offsets) / sizeof(umod27a_Offsets[0]) }, "Cold");
    
    if (STATLIST_GetUnitStatSigned(pUnit, STAT_FIRERESIST, 0) >= 100)
        ApplySunderForStat(pUnit, STAT_FIRERESIST, maxFire, { {"40",40},{"75",75},{"20",20} }, { umod8b_Offsets, umod9_Offsets, umod27b_Offsets }, { sizeof(umod8b_Offsets) / sizeof(umod8b_Offsets[0]), sizeof(umod9_Offsets) / sizeof(umod9_Offsets[0]), sizeof(umod27b_Offsets) / sizeof(umod27b_Offsets[0]) }, "Fire");

    if (STATLIST_GetUnitStatSigned(pUnit, STAT_LIGHTRESIST, 0) >= 100)
        ApplySunderForStat(pUnit, STAT_LIGHTRESIST, maxLight, { {"40",40},{"75",75},{"20",20} }, { umod8c_Offsets, umod17_Offsets, umod27c_Offsets }, { sizeof(umod8c_Offsets) / sizeof(umod8c_Offsets[0]), sizeof(umod17_Offsets) / sizeof(umod17_Offsets[0]), sizeof(umod27c_Offsets) / sizeof(umod27c_Offsets[0]) }, "Light");

    if (STATLIST_GetUnitStatSigned(pUnit, STAT_POISONRESIST, 0) >= 100)
        ApplySunderForStat(pUnit, STAT_POISONRESIST, maxPoison, { {"75",75} }, { umod23_Offsets }, { sizeof(umod23_Offsets) / sizeof(umod23_Offsets[0]) }, "Poison");

    if (STATLIST_GetUnitStatSigned(pUnit, STAT_DAMAGERESIST, 0) >= 100)
        ApplySunderForStat(pUnit, STAT_DAMAGERESIST, maxDamage, { {"50",50} }, { umod28_Offsets }, { sizeof(umod28_Offsets) / sizeof(umod28_Offsets[0]) }, "Damage");

    if (STATLIST_GetUnitStatSigned(pUnit, STAT_MAGICRESIST, 0) >= 100)
        ApplySunderForStat(pUnit, STAT_MAGICRESIST, maxMagic, { {"20",20} }, { umod25_Offsets }, { sizeof(umod25_Offsets) / sizeof(umod25_Offsets[0]) }, "Magic");

    //LogSunder("=== End ApplyGhettoSunder ===");
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

    //MessageBoxA(NULL, msgStream.str().c_str(), "Stat Adjustment Debug", MB_OK);
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
    AdjustMonsterLevel(pUnit, STAT_LEVEL, boostedLevel);
    int32_t playerCountModifier = (playerCountGlobal >= 9) ? (playerCountGlobal - 2) * 50 : (playerCountGlobal - 1) * 50;

    // Calculate base monster stats
    D2MonStatsInitStrc monStatsInit = {};
    CalculateMonsterStats(pUnit->dwClassId, 1, pGame->nDifficulty, STATLIST_GetUnitStatSigned(pUnit, STAT_LEVEL, 0), 7, monStatsInit);
    const int32_t nBaseHp = monStatsInit.nMinHP + ITEMS_RollLimitedRandomNumber(&pUnit->pSeed, monStatsInit.nMaxHP - monStatsInit.nMinHP + 1);
    const int32_t nHp = nBaseHp + D2_ComputePercentage(nBaseHp, playerCountModifier);
    const int32_t nShiftedHp = nHp << 8;

    // Apply core stats
    SetStat(pUnit, STAT_MAXHP, nShiftedHp);
    SetStat(pUnit, STAT_HITPOINTS, nShiftedHp);
    SetStat(pUnit, STAT_ARMORCLASS, monStatsInit.nAC);
    SetStat(pUnit, STAT_EXPERIENCE, D2_ComputePercentage(monStatsInit.nExp, ((playerCountGlobal - 8) * 100) / 5));
    SetStat(pUnit, STAT_HPREGEN, (nShiftedHp * 2) >> 12);
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

    //Loop through zones
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
        int cyclePos = minutesSinceStart % (cycleLengthMin * groupCount);
        int activeGroupIndex = (g_ManualZoneGroupOverride == -1) ? (cyclePos / cycleLengthMin) : g_ManualZoneGroupOverride;

        if (activeGroupIndex < 0 || activeGroupIndex >= groupCount)
            continue;

        const ZoneGroup& activeGroup = zone.zones[activeGroupIndex];

        // Store terror zone timing info
        g_TerrorZoneData.cycleLengthMin = cycleLengthMin;
        g_TerrorZoneData.terrorDurationMin = zone.terror_duration_min;
        g_TerrorZoneData.groupCount = groupCount;
        g_TerrorZoneData.activeGroupIndex = activeGroupIndex;
        g_TerrorZoneData.zoneStartUtc = zone.start_time_utc;

        double now = static_cast<double>(std::time(nullptr));
        UpdateActiveZoneInfoText(static_cast<time_t>(now));
        InitRandomStatsForAllMonsters(false);

        // Try to find a ZoneLevel override for the current level
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

        // If no matching ZoneLevel was found, skip this monster
        if (!matchingZoneLevel)
        {
            isTerrorized = false;
            continue;
        }

        return;
    }
}

void __fastcall HookedMONSTER_InitializeStatsAndSkills(D2GameStrc* pGame, D2ActiveRoomStrc* pRoom, D2UnitStrc* pUnit, int64_t* pMonRegData) {
oMONSTER_InitializeStatsAndSkills(pGame, pRoom, pUnit, pMonRegData);

    if (!pUnit || pUnit->dwUnitType != UNIT_MONSTER || !pUnit->pMonsterData || !pUnit->pMonsterData->pMonstatsTxt) {
        return;
    }

    int32_t nClassId = pUnit->dwClassId;
    auto pMonStatsTxtRecord = pUnit->pMonsterData->pMonstatsTxt;
    auto wMonStatsEx = sgptDataTables->pMonStatsTxt[nClassId].wMonStatsEx;

    if (wMonStatsEx >= sgptDataTables->nMonStats2TxtRecordCount) {
        return;
    }

    D2UnitStrc* pUnitPlayer = UNITS_GetServerUnitByTypeAndId(pGame, UNIT_PLAYER, 1);
    if (!pUnitPlayer)
        return;

    
    int difficulty = GetPlayerDifficulty(pUnitPlayer);
    if (difficulty < 0 || difficulty > 2)
        return;

    auto pMonStats2TxtRecord = sgptDataTables->pMonStats2Txt[wMonStatsEx];
    D2MonStatsInitStrc monStatsInit = {};
    std::string path = std::format("{0}/Mods/{1}/{1}.mpq/data/hd/global/excel/desecratedzones.json", GetExecutableDir(), GetModName());
    ApplyGhettoSunder(pGame, pRoom, pUnit, pMonRegData, &monStatsInit);

    if (!GetBaalQuest(pUnitPlayer, pGame)) {
        return;
    }

    LoadDesecratedZones(path);
    ApplyGhettoTerrorZone(pGame, pRoom, pUnit, pMonRegData, &monStatsInit);
    
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

#pragma endregion

#pragma region Draw Loop for Detours and Stats Display
void D2RHUD::OnDraw() {
    D2GameStrc* pGame = nullptr;
    D2Client* pGameClient = GetClientPtr();

    if (pGameClient != nullptr)
        pGame = (D2GameStrc*)pGameClient->pGame;

    if (g_ShouldShowItemFilterMessage)
    {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - g_ItemFilterMessageStartTime);

        if (elapsed.count() < 3)
        {
            ImGuiIO& io = ImGui::GetIO();
            ImFont* largeFont = io.Fonts->Fonts[4];
            if (largeFont)
                ImGui::PushFont(largeFont);

            auto drawList = ImGui::GetBackgroundDrawList();
            ImVec2 screenSize = ImGui::GetIO().DisplaySize;
            ImVec2 textSize = ImGui::CalcTextSize(g_ItemFilterStatusMessage.c_str());
            ImVec2 textPos = ImVec2((screenSize.x - textSize.x) * 0.5f, (screenSize.y - textSize.y) * 0.1f);

            drawList->AddText(textPos, IM_COL32(199, 179, 119, 255), g_ItemFilterStatusMessage.c_str());

            if (largeFont)
                ImGui::PopFont();
        }
        else
            g_ShouldShowItemFilterMessage = false;
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


    if (!oMONSTER_InitializeStatsAndSkills) {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        oMONSTER_InitializeStatsAndSkills = reinterpret_cast<MONSTER_InitializeStatsAndSkills_t>(Pattern::Address(0x33f380));
        DetourAttach(&(PVOID&)oMONSTER_InitializeStatsAndSkills, HookedMONSTER_InitializeStatsAndSkills);
        DetourTransactionCommit();
    }

    if (!itemFilter->bInstalled) {
        itemFilter->Install(cachedSettings);
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
    }

    if (!oDropTCTest) {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        oDropTCTest = reinterpret_cast<DropTCTest_t>(Pattern::Address(0x2f9d50));
        DetourAttach(&(PVOID&)oDropTCTest, HookedDropTCTest);
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
        if (!settings.monsterStatsDisplay)
        {
            if (fontPushed)
                ImGui::PopFont();
            return;
        }

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

        if (pUnitServer)
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
                    auto hp = std::format("{} / {}",
                        STATLIST_GetUnitStatSigned(pUnitServer, STAT_HITPOINTS, 0) >> 8,
                        STATLIST_GetUnitStatSigned(pUnitServer, STAT_MAXHP, 0) >> 8);
                    auto width = ImGui::CalcTextSize(hp.c_str()).x;
                    drawList->AddText({ center - (width / 2.0f) + 1, ypercent2 }, IM_COL32(255, 255, 255, 255), hp.c_str());
                }

                
                //std::string ac = std::format("Pierce IDX: {}", STATLIST_GetUnitStatSigned(pUnitPlayer, STAT_PIERCE_IDX, 0));
                //drawList->AddText({ 20, 10 }, IM_COL32(170, 50, 50, 255), ac.c_str());
                
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
        { "Open Cube Panel: ", nullptr },
    };

    auto GetMouseVirtualKey = [](const std::string& str) -> short {
        if (str == "VK_LBUTTON")   return VK_LBUTTON;
        if (str == "VK_RBUTTON")  return VK_RBUTTON;
        if (str == "VK_XBUTTON1") return VK_MBUTTON;
        if (str == "VK_XBUTTON1")     return VK_XBUTTON1;
        if (str == "VK_XBUTTON2")     return VK_XBUTTON2;
        return 0;
        };

    auto IsBindingPressed = [&](const std::string& binding, short key) -> bool {
        short mouseVK = GetMouseVirtualKey(binding);
        if (mouseVK != 0 && key == mouseVK) return true;
        auto it = keyMap.find(binding);
        return it != keyMap.end() && key == it->second;
        };

    // --- Handle standard + custom commands ---
    for (const auto& [searchString, debugCommand] : commands) {
        if (debugCommand != nullptr) { // Standard Commands
            std::string binding = ReadCommandFromFile(filename, searchString);
            if (!binding.empty() && IsBindingPressed(binding, key) && GetClientStatus() == 1) {
                if (searchString == "Transmute: ")
                    D2CLIENT_Transmute();
                else
                    ExecuteDebugCheatFunc(debugCommand);
                return true;
            }
        }
        else { // Custom Commands
            std::string commandKey, commandValue;
            ReadCommandWithValuesFromFile(filename, searchString, commandKey, commandValue);
            if (!commandKey.empty() && !commandValue.empty() &&
                IsBindingPressed(commandKey, key) && GetClientStatus() == 1)
            {
                if (commandValue.find("/") != std::string::npos)
                    CLIENT_playerCommand(commandValue, commandValue);
                else
                    ExecuteDebugCheatFunc(commandValue.c_str());
                return true;
            }
        }
    }

    // --- Open Cube Panel ---
    {
        std::string binding = ReadCommandFromFile(filename, "Open Cube Panel: ");
        if (!binding.empty() && IsBindingPressed(binding, key) && GetClientStatus() == 1) {
            if (gpClientList) {
                auto pClient = *gpClientList;
                if (pClient && pClient->pGame) {
                    reinterpret_cast<int32_t(__fastcall*)(D2GameStrc*, D2UnitStrc*)>(
                        Pattern::Address(0x34F5A0)
                        )(pClient->pGame, pClient->pPlayer);
                }
            }
            return true;
        }
    }

    // --- Cycle Terror Zones ---
    {
        std::string binding = ReadCommandFromFile(filename, "Cycle TZ Forward: ");
        if (!binding.empty() && IsBindingPressed(binding, key) && GetClientStatus() == 1) {
            CheckToggleForward();
            return true;
        }
    }
    {
        std::string binding = ReadCommandFromFile(filename, "Cycle TZ Backward: ");
        if (!binding.empty() && IsBindingPressed(binding, key) && GetClientStatus() == 1) {
            CheckToggleBackward();
            return true;
        }
    }

    // --- Toggle Stat Adjustments Display ---
    {
        std::string raw = ReadCommandFromFile(filename, "Toggle Stat Adjustments Display: ");
        if (!raw.empty()) {
            std::string boolPart;
            std::string keyPart;
            auto commaPos = raw.find(',');
            if (commaPos != std::string::npos) {
                boolPart = raw.substr(0, commaPos);
                keyPart = raw.substr(commaPos + 1);
                keyPart.erase(0, keyPart.find_first_not_of(" \t"));
            }
            else {
                keyPart = raw;
            }

            if (!boolPart.empty()) {
                std::string trimmed = boolPart;
                trimmed.erase(0, trimmed.find_first_not_of(" \t"));
                trimmed.erase(trimmed.find_last_not_of(" \t") + 1);
                showStatAdjusts = (trimmed == "true" || trimmed == "1");
            }

            if (!keyPart.empty() && IsBindingPressed(keyPart, key) && GetClientStatus() == 1) {
                showStatAdjusts = !showStatAdjusts;

                std::ostringstream updatedLine;
                updatedLine << "Toggle Stat Adjustments Display: "
                    << (showStatAdjusts ? "true" : "false")
                    << ", " << keyPart;

                std::ifstream inFile(filename);
                std::ostringstream buffer;
                std::string line;
                std::string targetPrefix = "Toggle Stat Adjustments Display: ";
                while (std::getline(inFile, line)) {
                    if (line.find(targetPrefix) == 0)
                        buffer << updatedLine.str() << "\n";
                    else
                        buffer << line << "\n";
                }
                inFile.close();
                std::ofstream outFile(filename, std::ios::trunc);
                outFile << buffer.str();
                outFile.close();

                return true;
            }
        }
    }

    // --- Version display (Ctrl + Alt + V) ---
    if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) &&
        (GetAsyncKeyState(VK_MENU) & 0x8000) &&
        (GetAsyncKeyState('V') & 0x8000))
    {
        ShowVersionMessage();
        OnStashPageChanged(gSelectedPage + 1);
        return true;
    }

    return itemFilter->OnKeyPressed(key);
}

//Show D2RHUD Version Info as a MessageBox Popup
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
