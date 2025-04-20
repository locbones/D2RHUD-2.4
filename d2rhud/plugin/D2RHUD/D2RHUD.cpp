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
std::string Version = "1.0.3";

struct MonsterStatsDisplaySettings {
    bool monsterStatsDisplay;
    std::string channelColor;
    std::string playerNameColor;
    std::string messageColor;
    bool socketDisplay;
};
static MonsterStatsDisplaySettings cachedSettings;

static D2Client* GetClientPtr();

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

#pragma region Bank Tabs
struct Message {
    uint64_t o1;
    uint64_t o2;
    uint64_t o3;
    uint64_t o4;
    uint64_t o5;
};

const uint32_t getSelectedBankPanelTab = 0x18f1a0;
const uint32_t bankPanelDraw = 0x18eb90;
const uint32_t bankPanelOnMessage = 0x18ee50;
const uint32_t widgetFindChild = 0x576070;
struct D2BankPanelWidget;

typedef uint8_t(__fastcall* GetSelectedBankPanelTab_t)(D2BankPanelWidget* pBankPanel);
static GetSelectedBankPanelTab_t oGetSelectedBankPanelTab = nullptr;

typedef void(__fastcall* BankPanelDraw_t)(D2BankPanelWidget* pBankPanel);
static BankPanelDraw_t oBankPanelDraw = nullptr;

typedef void* (__fastcall* BankPanelOnMessage_t)(void* pWidget, Message &message);
static BankPanelOnMessage_t BankPanelMessage = reinterpret_cast<BankPanelOnMessage_t>(Pattern::Address(bankPanelOnMessage));

typedef void*(__fastcall* WidgetFindChild_t)(void* pWidget, const char* childName);
static WidgetFindChild_t WidgetFindChild = reinterpret_cast<WidgetFindChild_t>(Pattern::Address(widgetFindChild));


static uint8_t gSelectedPage = 0;
static blz_string gTab0OriginalText = { nullptr };

uint8_t __fastcall HookedGetSelectedBankPanelTab(D2BankPanelWidget* pBankPanel) {
    auto result = oGetSelectedBankPanelTab(pBankPanel);
    // TODO: page size?
    result += (gSelectedPage * 8);
    return result;
}

void __fastcall HookedBankPanelDraw(D2BankPanelWidget* pBankPanel) {
    oBankPanelDraw(pBankPanel);
    auto pBankPages = WidgetFindChild(pBankPanel, "BankPages");
    if (pBankPages) {
        auto nSelectedPage = *(int32_t*)((int64_t)pBankPages + 0x178C);
        if (nSelectedPage != gSelectedPage) {
            gSelectedPage = nSelectedPage;
            
            // Update tab text.
            auto pBankTabs = WidgetFindChild(pBankPanel, "BankTabs");
            auto pTab0 = WidgetFindChild(pBankTabs, "Text0");
            auto pTab1 = WidgetFindChild(pBankTabs, "Text1");
            std::cout << "BankPanel: " << std::hex << pBankPanel << " BankPages: " << std::hex << pBankPages << " BankTabs: " << std::hex << pBankTabs << std::endl;
            if (gTab0OriginalText.str == nullptr) {
                auto tab0OriginalText = (blz_string*)((int64_t)pTab0 + 0x88);
                gTab0OriginalText = {
                    (const char*)malloc(tab0OriginalText->length + 1),
                    tab0OriginalText->length,
                    tab0OriginalText->alloc
                };
                memcpy(&gTab0OriginalText.data, &tab0OriginalText->data, 16);
                std::strncpy((char*)gTab0OriginalText.str, tab0OriginalText->str, tab0OriginalText->length + 1);
            }
            if (gSelectedPage > 0) {
                // Set tab 0 text to tab 1 text.
                auto tab0Text = (blz_string*)((int64_t)pTab0 + 0x88);
                auto tab1Text = (blz_string*)((int64_t)pTab1 + 0x88);
                memcpy(tab0Text, tab1Text, sizeof(blz_string));
            } else {
                // Revert tab text
                auto tab0Text = (blz_string*)((int64_t)pTab0 + 0x88);
                memcpy(tab0Text, &gTab0OriginalText, sizeof(blz_string));
            }
            int32_t nSelectedTab = 0;   //This doesnt really seem to matter...
            Message m = {
                "BankPanelMessage"_hash64,
                "SelectTab"_hash64,
                (uint64_t)&nSelectedTab
            };
            BankPanelMessage(pBankPanel, m);
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
typedef void(__fastcall* SendPacketToServer)(CMD_PACKET_BASE* pPacket);
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
        std::cout << "Current Status: " << CheckClientStatusChange() << std::endl;
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

    auto cleanValue = [](std::string value) -> std::string {
        value.erase(0, value.find_first_not_of(" \t\""));
        value.erase(value.find_last_not_of(" \t\n\r\",") + 1);
        return value;
        };

    std::unordered_map<std::string, std::string> settings;
    std::string line;
    while (std::getline(configFile, line)) {
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

    if (!oGetSelectedBankPanelTab) {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        oGetSelectedBankPanelTab = reinterpret_cast<GetSelectedBankPanelTab_t>(Pattern::Address(getSelectedBankPanelTab));
        DetourAttach(&(PVOID&)oGetSelectedBankPanelTab, HookedGetSelectedBankPanelTab);
        DetourTransactionCommit();
    }

    if (!oBankPanelDraw) {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        oBankPanelDraw = reinterpret_cast<BankPanelDraw_t>(Pattern::Address(bankPanelDraw));
        DetourAttach(&(PVOID&)oBankPanelDraw, HookedBankPanelDraw);
        DetourTransactionCommit();
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

    std::cout << "Monster Stats Display is enabled." << std::endl;
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
