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
std::string Version = "1.1.5";

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
    std::vector<std::pair<int, int>> stat_adjustments;
};

struct WarningInfo {
    int announce_time_min = 0;
    int tier = 0;
};

struct LevelName {
    int id;
    std::string name;
};

struct ZoneLevel {
    int level_id = 0;

    // Optional per-difficulty overrides
    std::optional<DifficultySettings> normal;
    std::optional<DifficultySettings> nightmare;
    std::optional<DifficultySettings> hell;
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
};

// Structure for stat definition
struct Stat {
    D2C_ItemStats id;
    int minValue;
    int maxValue;
    bool allowNegative; // determines if negative values are possible
    bool isBinary;      // for stats like cannot be frozen
};

int playerLevel = 0;
static std::string g_ActiveZoneInfoText;
int g_ManualZoneGroupOverride = -1;
time_t g_LastToggleTime = 0;
TerrorZoneDisplayData g_TerrorZoneData;
std::vector<DesecratedZone> gDesecratedZones;
std::vector<LevelName> level_names;
typedef BOOL(__fastcall* QUESTRECORD_GetQuestState_t)(D2BitBufferStrc* pQuestRecord, int32_t nQuestId, int32_t nState);
static QUESTRECORD_GetQuestState_t oQUESTRECORD_GetQuestState = reinterpret_cast<QUESTRECORD_GetQuestState_t>(Pattern::Address(0x243880));
typedef int64_t(__fastcall* HUDWarnings__PopulateHUDWarnings_t)(void* pWidget);
static HUDWarnings__PopulateHUDWarnings_t oHUDWarnings__PopulateHUDWarnings = nullptr;
typedef void(__fastcall* Widget__OnClose_t)(void* pWidget);
static Widget__OnClose_t oWidget__OnClose = nullptr;
static char pCustom[1024];

typedef BOOL(__stdcall* DATATBLS_CalculateMonsterStatsByLevel_t)(int nMonsterId, int nGameType, int nDifficulty, int nLevel, short nFlags, D2MonStatsInitStrc* pMonStatsInit);
static DATATBLS_CalculateMonsterStatsByLevel_t oAdjustMonsterStats = reinterpret_cast<DATATBLS_CalculateMonsterStatsByLevel_t>(Pattern::Address(0x2356B0));

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

    //Unused for now
    if (j.contains("stat_adjustments")) {
        d.stat_adjustments.clear();
        for (const auto& item : j.at("stat_adjustments")) {
            if (item.is_array() && item.size() == 2) {
                d.stat_adjustments.emplace_back(item[0].get<int>(), item[1].get<int>());
            }
        }
    }
}

void from_json(const json& j, WarningInfo& w) {
    w.announce_time_min = j.at("announce_time_min").get<int>();
    w.tier = j.at("tier").get<int>();
}

void from_json(const json& j, ZoneLevel& zl) {
    // Required
    j.at("level_id").get_to(zl.level_id);

    // Optional
    if (j.contains("normal")) zl.normal = j.at("normal").get<DifficultySettings>();
    if (j.contains("nightmare")) zl.nightmare = j.at("nightmare").get<DifficultySettings>();
    if (j.contains("hell")) zl.hell = j.at("hell").get<DifficultySettings>();
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

void from_json(const nlohmann::json& j, LevelName& ln) {
    j.at("id").get_to(ln.id);
    j.at("name").get_to(ln.name);
}

std::string StripComments(const std::string& content) {
    std::string result;
    bool inComment = false;

    for (size_t i = 0; i < content.length(); ++i) {
        if (!inComment && content[i] == '/' && i + 1 < content.length() && content[i + 1] == '*') {
            inComment = true;
            ++i;
        }
        else if (inComment && content[i] == '*' && i + 1 < content.length() && content[i + 1] == '/') {
            inComment = false;
            ++i;
        }
        else if (!inComment) {
            result += content[i];
        }
    }

    return result;
}

//Unused for now
std::vector<std::pair<D2C_ItemStats, int>> SelectRandomStats(std::mt19937& rng) {
    std::vector<Stat> allStats = {
        { STAT_FIRERESIST, 10, 10, true,  false },
        { STAT_COLDRESIST, 20, 20, true,  false },
        { STAT_LIGHTRESIST, 30, 30, true,  false },
        // Add more as needed
    };

    // Shuffle to randomize selection order
    std::shuffle(allStats.begin(), allStats.end(), rng);

    // Decide how many stats to include in this group
    std::uniform_int_distribution<> countDist(1, std::min<int>(5, allStats.size()));
    int statCount = countDist(rng);

    std::vector<std::pair<D2C_ItemStats, int>> groupedStats;
    std::uniform_real_distribution<> chance(0.0, 1.0);

    for (int i = 0; i < statCount; ++i) {
        const Stat& s = allStats[i];
        int value = 0;

        if (s.isBinary) {
            value = 1;
        }
        else if (s.allowNegative && s.minValue < 0) {
            bool chooseNegative = chance(rng) < 0.25;

            int upperBound = (s.maxValue < 0) ? s.maxValue : 0;
            int lowerBound = (s.minValue > 0) ? s.minValue : 0;

            if (chooseNegative) {
                std::uniform_int_distribution<> dist(s.minValue, upperBound);
                value = dist(rng);
            }
            else {
                std::uniform_int_distribution<> dist(lowerBound, s.maxValue);
                value = dist(rng);
            }
        }
        else {
            std::uniform_int_distribution<> dist(s.minValue, s.maxValue);
            value = dist(rng);
        }

        groupedStats.emplace_back(s.id, value);
    }

    return groupedStats;
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
    }
    catch (const std::exception& e) {
        MessageBoxA(nullptr, ("JSON field parse error: " + std::string(e.what())).c_str(), "Error", MB_ICONERROR);
        return false;
    }

    std::ofstream log("desecrated_zones_log.txt", std::ios::trunc);
    if (!log.is_open()) {
        MessageBoxA(nullptr, "Failed to create log file.", "Error", MB_ICONERROR);
        return true;
    }
    /*
    for (size_t i = 0; i < gDesecratedZones.size(); ++i) {
        const auto& zone = gDesecratedZones[i];

        log << "Zone " << i << ":\n";
        log << "Start Time: " << zone.start_time_utc << "\n";
        log << "End Time: " << zone.end_time_utc << "\n";

        const auto logDifficulty = [&](const std::string& label, const DifficultySettings& diff) {
            log << "  " << label << ":\n";
            log << "    bound_incl_min: " << diff.bound_incl_min << "\n";
            log << "    bound_incl_max: " << diff.bound_incl_max << "\n";
            log << "    boost_level: " << diff.boost_level << "\n";
            log << "    difficulty_scale: " << diff.difficulty_scale << "\n";
            log << "    boost_experience_percent: " << diff.boost_experience_percent << "\n";
            };

        logDifficulty("Normal", zone.default_normal);
        logDifficulty("Nightmare", zone.default_nightmare);
        logDifficulty("Hell", zone.default_hell);

        log << "\n";
    }
    */

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
            // Auto mode: calculate based on time
            int minutesSinceStart = static_cast<int>((currentUtc - zone.start_time_utc) / 60);
            int cyclePos = minutesSinceStart % (cycleLengthMin * groupCount);
            activeGroupIndex = cyclePos / cycleLengthMin;
        }
        else
        {
            // Clamp manual override index to groupCount-1
            if (activeGroupIndex >= groupCount)
                activeGroupIndex = groupCount - 1;
        }

        if (activeGroupIndex >= groupCount)
            continue;

        const ZoneGroup& activeGroup = zone.zones[activeGroupIndex];

        // Store terror zone timing info for live update display
        g_TerrorZoneData.cycleLengthMin = cycleLengthMin;
        g_TerrorZoneData.terrorDurationMin = zone.terror_duration_min;
        g_TerrorZoneData.groupCount = groupCount;
        g_TerrorZoneData.activeGroupIndex = activeGroupIndex;
        g_TerrorZoneData.zoneStartUtc = zone.start_time_utc;

        std::stringstream ss;
        for (const auto& zl : activeGroup.levels)
        {
            auto it = std::find_if(level_names.begin(), level_names.end(),
                [&](const LevelName& ln) { return ln.id == zl.level_id; });

            if (it != level_names.end())
                ss << it->name << "\n";
            else
                ss << "(Unknown Level ID: " << zl.level_id << ")\n";
        }

        g_ActiveZoneInfoText = ss.str();

        break;
    }
}

void CheckToggleManualZoneGroup()
{
    bool ctrlPressed = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    bool altPressed = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
    bool tPressed = (GetAsyncKeyState('Z') & 0x8000) != 0;

    time_t now = std::time(nullptr);

    if (ctrlPressed && altPressed && tPressed && (now - g_LastToggleTime) > 0.3)
    {
        g_LastToggleTime = now;

        // Find max groups available in the current active zone
        int maxGroups = 0;
        for (const auto& zone : gDesecratedZones)
        {
            if (now < zone.start_time_utc || now > zone.end_time_utc)
                continue;

            maxGroups = static_cast<int>(zone.zones.size());
            break;
        }

        if (maxGroups == 0)
        {
            g_ManualZoneGroupOverride = -1;
        }
        else
        {
            if (g_ManualZoneGroupOverride == -1)
                g_ManualZoneGroupOverride = 1; // Start at next group
            else
                g_ManualZoneGroupOverride++;

            if (g_ManualZoneGroupOverride >= maxGroups)
                g_ManualZoneGroupOverride = -1; // Wrap back to auto mode
        }

        UpdateActiveZoneInfoText(now);

        /*
        std::string msg = (g_ManualZoneGroupOverride == -1) ?
            "TerrorZone group override OFF (auto mode)" :
            "TerrorZone group override ON: Group " + std::to_string(g_ManualZoneGroupOverride);
        MessageBoxA(nullptr, msg.c_str(), "TerrorZone Override", MB_OK | MB_ICONINFORMATION);
        */
    }
}


int64_t Hooked_HUDWarnings__PopulateHUDWarnings(void* pWidget) {
    D2GameStrc* pGame = nullptr;
    D2Client* pGameClient = GetClientPtr();
    D2UnitStrc* pUnitPlayer = nullptr;

    if (pGameClient != nullptr)
    {
        pGame = (D2GameStrc*)pGameClient->pGame;
        pUnitPlayer = UNITS_GetServerUnitByTypeAndId(pGame, UNIT_PLAYER, 1);
    }

    auto result = oHUDWarnings__PopulateHUDWarnings(pWidget);

    auto tzInfoText = reinterpret_cast<int64_t>(WidgetFindChild(pWidget, "TerrorZoneInfoText"));
    if (!tzInfoText) {
        return result;
    }

    // Don't draw the custom HUD warning if Baal quest isn't complete
    if (!GetBaalQuest(pUnitPlayer, pGame)) {
        return result;
    }

    char** pOriginal = (char**)(tzInfoText + 0x88);
    int64_t* nLength = (int64_t*)(tzInfoText + 0x90);

    std::string finalText = BuildTerrorZoneInfoText();
    if (finalText.empty())
        return result;

    strncpy(pCustom, finalText.c_str(), 255); // assuming pCustom is a char[256] or similar
    pCustom[255] = '\0'; // null-terminate just in case

    *pOriginal = pCustom;
    *nLength = strlen(pCustom) + 1;

    return result;
}

void Hooked__Widget__OnClose(void* pWidget) {
    oWidget__OnClose(pWidget);
    char* pName = *(reinterpret_cast<char**>(reinterpret_cast<char*>(pWidget) + 0x8));
    if (strcmp(pName, "AutoMap") == 0) {
        pCustom[0] = '\0';
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

void SubtractResistances(D2UnitStrc* pUnit, D2C_ItemStats nStatId, uint32_t nValue, uint16_t nLayer = 0) {
    auto nCurrentValue = STATLIST_GetUnitStatSigned(pUnit, nStatId, nLayer);

    if (nCurrentValue >= 100) {

        int newValue = nCurrentValue - nValue;
        if (newValue < 99) { newValue = 99; }
        STATLISTEX_SetStatListExStat(pUnit->pStatListEx, nStatId, newValue, nLayer);
    }
}

constexpr uint16_t UNIQUE_LAYER = 1337;

void AddToCurrentStat(D2UnitStrc* pUnit, D2C_ItemStats nStatId, uint32_t nValue) {
    int currentValue = STATLIST_GetUnitStatSigned(pUnit, nStatId, 0);

    // If the stat is already >= nValue, don't do anything
    if (currentValue >= static_cast<int>(nValue))
        return;

    int offset = static_cast<int>(nValue) - currentValue;

    // Output before/after stats via message box
    char msg[256];
    sprintf_s(msg, sizeof(msg), "Stat %d before: %d\nSetting to: %d\nOffset applied: %d", nStatId, currentValue, nValue, offset);
    //MessageBoxA(nullptr, msg, "Stat Adjustment", MB_OK | MB_ICONINFORMATION);

    // Apply the offset to get to the desired value
    STATLISTEX_SetStatListExStat(pUnit->pStatListEx, nStatId, currentValue + offset, 0);
    STATLISTEX_SetStatListExStat(pUnit->pStatListEx, nStatId, offset, UNIQUE_LAYER);
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

void __fastcall ApplyGhettoSunder(D2GameStrc* pGame, D2ActiveRoomStrc* pRoom, D2UnitStrc* pUnit, int64_t* pMonRegData, D2MonStatsInitStrc* monStatsInit) {
    if (!pGame || !pUnit)
        return;

    // Track max stat values found among all players
    int maxColdResist = INT_MIN;
    int maxFireResist = INT_MIN;
    int maxLightResist = INT_MIN;
    int maxPoisonResist = INT_MIN;
    int maxDamageResist = INT_MIN;
    int maxMagicResist = INT_MIN;

    for (int i = 0; i < 8; ++i) {
        auto pClient = gpClientList[i];
        if (!pClient)
            continue;

        uint32_t unitGUID = pClient->dwUnitGUID;
        D2UnitStrc* pPlayerUnit = UNITS_GetServerUnitByTypeAndId(pGame, UNIT_PLAYER, unitGUID);
        if (!pPlayerUnit)
            continue;

        int cold = STATLIST_GetUnitStatSigned(pPlayerUnit, 187, 0);
        int fire = STATLIST_GetUnitStatSigned(pPlayerUnit, 189, 0);
        int light = STATLIST_GetUnitStatSigned(pPlayerUnit, 190, 0);
        int poison = STATLIST_GetUnitStatSigned(pPlayerUnit, 191, 0);
        int damage = STATLIST_GetUnitStatSigned(pPlayerUnit, 192, 0);
        int magic = STATLIST_GetUnitStatSigned(pPlayerUnit, 193, 0);

        if (cold > maxColdResist) maxColdResist = cold;
        if (fire > maxFireResist) maxFireResist = fire;
        if (light > maxLightResist) maxLightResist = light;
        if (poison > maxPoisonResist) maxPoisonResist = poison;
        if (damage > maxDamageResist) maxDamageResist = damage;
        if (magic > maxMagicResist) maxMagicResist = magic;
    }

    // Now apply highest values to the monster unit
    if (maxColdResist > INT_MIN)
        SubtractResistances(pUnit, STAT_COLDRESIST, maxColdResist);
    if (maxFireResist > INT_MIN)
        SubtractResistances(pUnit, STAT_FIRERESIST, maxFireResist);
    if (maxLightResist > INT_MIN)
        SubtractResistances(pUnit, STAT_LIGHTRESIST, maxLightResist);
    if (maxPoisonResist > INT_MIN)
        SubtractResistances(pUnit, STAT_POISONRESIST, maxPoisonResist);
    if (maxDamageResist > INT_MIN)
        SubtractResistances(pUnit, STAT_DAMAGERESIST, maxDamageResist);
    if (maxMagicResist > INT_MIN)
        SubtractResistances(pUnit, STAT_MAGICRESIST, maxMagicResist);
}

void __fastcall ApplyStatAdjustments(
    D2GameStrc* pGame,
    D2ActiveRoomStrc* pRoom,
    D2UnitStrc* pUnit,
    int64_t* pMonRegData,
    D2MonStatsInitStrc* monStatsInit,
    const DifficultySettings& settings)
{
    if (!pGame)
        return;

    for (int i = 0; i < 8; ++i) {
        auto pClient = gpClientList[i];
        if (!pClient)
            continue;

        uint32_t unitGUID = pClient->dwUnitGUID;
        D2UnitStrc* pPlayerUnit = UNITS_GetServerUnitByTypeAndId(pGame, UNIT_PLAYER, unitGUID);
        if (!pPlayerUnit)
            continue;

        for (const auto& [statID, value] : settings.stat_adjustments) {
            AddToCurrentStat(pPlayerUnit, static_cast<D2C_ItemStats>(statID), value);
        }
    }
}

void __fastcall ApplyGhettoTerrorZone(D2GameStrc* pGame, D2ActiveRoomStrc* pRoom, D2UnitStrc* pUnit, int64_t* pMonRegData, D2MonStatsInitStrc* monStatsInit)
{
    g_ActiveZoneInfoText.clear();

    if (!pGame || !pRoom || !pUnit)
        return;

    int levelId = GetLevelIdFromRoom(pRoom);
    if (levelId == -1)
        return;

    D2UnitStrc* pUnitPlayer = UNITS_GetServerUnitByTypeAndId(pGame, UNIT_PLAYER, 1);
    if (!pUnitPlayer)
        return;

    playerLevel = STATLIST_GetUnitStatSigned(pUnitPlayer, STAT_LEVEL, 0);
    int difficulty = GetPlayerDifficulty(pUnitPlayer);
    if (difficulty < 0 || difficulty > 2)
        return;

    time_t currentUtc = std::time(nullptr);

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

        std::stringstream ss;
        for (const auto& zl : activeGroup.levels)
        {
            auto it = std::find_if(level_names.begin(), level_names.end(),
                [&](const LevelName& ln) { return ln.id == zl.level_id; });

            if (it != level_names.end())
                ss << it->name << "\n";
            else
                ss << "(Unknown Level ID: " << zl.level_id << ")\n";
        }

        g_ActiveZoneInfoText = ss.str();
        // MessageBoxA(nullptr, ss.str().c_str(), "Active Zone Info Text", MB_OK | MB_ICONINFORMATION);

         // Time remaining logic
        int positionInFullCycle = cyclePos % cycleLengthMin;

        int remainingMinutes = 0;
        int remainingSeconds = 0;
        if (positionInFullCycle < zone.terror_duration_min)
        {
            int totalSecondsInPhase = zone.terror_duration_min * 60;
            int secondsIntoPhase = (currentUtc - zone.start_time_utc) % (cycleLengthMin * 60) % (zone.terror_duration_min * 60);
            int secondsRemaining = totalSecondsInPhase - secondsIntoPhase;
            remainingMinutes = secondsRemaining / 60;
            remainingSeconds = secondsRemaining % 60;
        }
        else
        {
            int totalSecondsInCycle = cycleLengthMin * 60;
            int secondsIntoCycle = (currentUtc - zone.start_time_utc) % totalSecondsInCycle;
            int secondsRemaining = totalSecondsInCycle - secondsIntoCycle;
            remainingMinutes = secondsRemaining / 60;
            remainingSeconds = secondsRemaining % 60;
        }

        // Try to find a ZoneLevel override for the current level
        const ZoneLevel* matchingZoneLevel = nullptr;
        for (const auto& zl : activeGroup.levels)
        {
            if (zl.level_id == levelId)
            {
                matchingZoneLevel = &zl;
                break;
            }
        }
        if (!matchingZoneLevel)
            continue;

        // Get global fallback settings for this difficulty
        const DifficultySettings* globalDefaults = nullptr;
        switch (difficulty)
        {
        case 0: globalDefaults = &zone.default_normal; break;
        case 1: globalDefaults = &zone.default_nightmare; break;
        case 2: globalDefaults = &zone.default_hell; break;
        }
        if (!globalDefaults)
            continue;

        // Get level-specific override for this difficulty
        const std::optional<DifficultySettings>* levelOverride = nullptr;
        switch (difficulty)
        {
        case 0: levelOverride = &matchingZoneLevel->normal; break;
        case 1: levelOverride = &matchingZoneLevel->nightmare; break;
        case 2: levelOverride = &matchingZoneLevel->hell; break;
        }

        // Merge level override with global fallback
        int boostLevel = globalDefaults->boost_level.value_or(0);
        int boundMin = globalDefaults->bound_incl_min.value_or(1);
        int boundMax = globalDefaults->bound_incl_max.value_or(99);

        if (levelOverride && levelOverride->has_value())
        {
            const DifficultySettings & override = levelOverride->value();
            if (override.boost_level) boostLevel = override.boost_level.value();
            if (override.bound_incl_min) boundMin = override.bound_incl_min.value();
            if (override.bound_incl_max) boundMax = override.bound_incl_max.value();
        }

        // Clamp the level to TZ specs
        int boostedLevel = playerLevel + boostLevel;
        if (boostedLevel < boundMin)
            boostedLevel = boundMin;
        else if (boostedLevel > boundMax)
            boostedLevel = boundMax;

        // Apply TZ Levels
        int before = STATLIST_GetUnitStatSigned(pUnit, STAT_LEVEL, 0);
        AdjustMonsterLevel(pUnit, STAT_LEVEL, boostedLevel);
        int after = STATLIST_GetUnitStatSigned(pUnit, STAT_LEVEL, 0);

        D2PlayerCountBonusStrc* pPlayerCountBonus;
        D2MonStatsInitStrc monStatsInit = {};
        D2MonStatsTxt* pMonStatsTxtRecord = pUnit->pMonsterData->pMonstatsTxt;
        int32_t playerCountModifier = 0;

        if (playerCountGlobal >= 9)
            playerCountModifier = (playerCountGlobal - 2) * 50;
        else
            playerCountModifier = (playerCountGlobal - 1) * 50;

        // Calculate for MonInit
        CalculateMonsterStats(pUnit->dwClassId, 1, pGame->nDifficulty, STATLIST_GetUnitStatSigned(pUnit, STAT_LEVEL, 0), 7, monStatsInit);

        const int32_t nBaseHp = monStatsInit.nMinHP + ITEMS_RollLimitedRandomNumber(&pUnit->pSeed, monStatsInit.nMaxHP - monStatsInit.nMinHP + 1);
        const int32_t nHp = nBaseHp + D2_ComputePercentage(nBaseHp, playerCountModifier);
        const int32_t nShiftedHp = nHp << 8;

        AddToCurrentStat(pUnit, STAT_MAXHP, nShiftedHp);
        AddToCurrentStat(pUnit, STAT_HITPOINTS, nShiftedHp);
        AddToCurrentStat(pUnit, STAT_ARMORCLASS, monStatsInit.nAC);
        AddToCurrentStat(pUnit, STAT_EXPERIENCE, D2_ComputePercentage(monStatsInit.nExp, ((playerCountGlobal - 8) * 100) / 5));
        AddToCurrentStat(pUnit, STAT_HPREGEN, (nShiftedHp * 2) >> 12);

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

    if (!GetBaalQuest(pUnitPlayer, pGame)) {
        return;
    }

    auto pMonStats2TxtRecord = sgptDataTables->pMonStats2Txt[wMonStatsEx];
    D2MonStatsInitStrc monStatsInit = {};
    std::string path = std::format("{0}/Mods/{1}/{1}.mpq/data/hd/global/excel/desecratedzones.json", GetExecutableDir(), GetModName());
    LoadDesecratedZones(path);
    ApplyGhettoTerrorZone(pGame, pRoom, pUnit, pMonRegData, &monStatsInit);
    ApplyGhettoSunder(pGame, pRoom, pUnit, pMonRegData, &monStatsInit);

    //MessageBoxA(nullptr, GetBaalQuest(pUnitPlayer, pGame) ? "Baal quest is complete." : "Baal quest is not complete.", "Terror Zone Boost Applied", MB_OK);

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

std::string g_ItemFilterStatusMessage = "";
bool g_ShouldShowItemFilterMessage = false;
std::chrono::steady_clock::time_point g_ItemFilterMessageStartTime;

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

    if (!oHUDWarnings__PopulateHUDWarnings) {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        oHUDWarnings__PopulateHUDWarnings = reinterpret_cast<HUDWarnings__PopulateHUDWarnings_t>(Pattern::Address(0xbb2a90));
        DetourAttach(&(PVOID&)oHUDWarnings__PopulateHUDWarnings, Hooked_HUDWarnings__PopulateHUDWarnings);
        oWidget__OnClose = reinterpret_cast<Widget__OnClose_t>(Pattern::Address(0x5766f0));
        DetourAttach(&(PVOID&)oWidget__OnClose, Hooked__Widget__OnClose);
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
        CheckToggleManualZoneGroup();

        if (!settings.monsterStatsDisplay)
        {
            if (fontPushed)
                ImGui::PopFont();
            return;
        }

        if (!gMouseHover->IsHovered) break;
        if (gMouseHover->HoveredUnitType > UNIT_MONSTER) break;

        D2UnitStrc* pUnit, * pUnitServer;
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

        if (!pUnit || !pUnitServer) break;

        if (STATLIST_GetUnitStatSigned(pUnitServer, STAT_HITPOINTS, 0) == 0) break;

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

                /*
                std::string ac = std::format("Atk Rating: {}", STATLIST_GetUnitStatSigned(pUnitServer, STAT_ARMORCLASS, 0));
                drawList->AddText({ 20, 10 }, IM_COL32(170, 50, 50, 255), ac.c_str());
                std::string xp = std::format("Experience: {}", STATLIST_GetUnitStatSigned(pUnitServer, STAT_EXPERIENCE, 0));
                drawList->AddText({ 20, 30 }, IM_COL32(170, 50, 50, 255), xp.c_str());
                */
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

    // Handle standard and custom commands
    for (const auto& [searchString, debugCommand] : commands) {
        if (debugCommand != nullptr) { // Standard Commands
            std::string result = ReadCommandFromFile(filename, searchString);
            if (!result.empty()) {
                auto it = keyMap.find(result);
                if (it != keyMap.end() && key == it->second && GetClientStatus() == 1) {
                    if (searchString == "Transmute: ")
                        D2CLIENT_Transmute();
                    else
                        ExecuteDebugCheatFunc(debugCommand);
                    return true;
                }
            }
        }
        else { // Custom Commands
            std::string commandKey, commandValue;
            ReadCommandWithValuesFromFile(filename, searchString, commandKey, commandValue);
            if (!commandKey.empty() && !commandValue.empty()) {
                auto it = keyMap.find(commandKey);
                if (it != keyMap.end() && key == it->second && GetClientStatus() == 1) {
                    if (commandValue.find("/") != std::string::npos)
                        CLIENT_playerCommand(commandValue, commandValue);
                    else
                        ExecuteDebugCheatFunc(commandValue.c_str());
                    return true;
                }
            }
        }
    }

    // === Handle Open Panel Commands (like opening cube) ===
    std::string openPanelKey = ReadCommandFromFile(filename, "Open Cube Panel: ");
    if (!openPanelKey.empty()) {
        auto it = keyMap.find(openPanelKey);
        if (it != keyMap.end() && key == it->second && GetClientStatus() == 1) {
            if (gpClientList) {
                auto pClient = *gpClientList;
                if (pClient && pClient->pGame) {
                    reinterpret_cast<int32_t(__fastcall*)(D2GameStrc*, D2UnitStrc*)>(
                        Pattern::Address(0x34F5A0)
                        )(pClient->pGame, pClient->pPlayer); // SKILLITEM_pSpell07_OpenCube
                }
            }
            return true;
        }
    }

    // Version display
    if (key == VK_CONTROL) ctrlPressed = true;
    if (key == VK_MENU) altPressed = true;
    if (key == 'V') vPressed = true;

    if (ctrlPressed && altPressed && vPressed) {
        ShowVersionMessage();
        OnStashPageChanged(gSelectedPage + 1);
        ctrlPressed = altPressed = vPressed = false;
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
