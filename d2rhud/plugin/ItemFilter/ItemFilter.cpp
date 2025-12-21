#define SOL_ALL_SAFETIES_ON 1

#include "ItemFilter.h"
#include <detours/detours.h>
#include <sol/sol.hpp>
#include <filesystem>
#include <set>
#include <imgui.h>
#include "../d2rhud/d2rhud.h"
#include <fstream>
#include <sstream>
#include <string>
#include "../../json.hpp"
#include <mutex>

#pragma region Global Static/Structs

#pragma pack(1)
class D2GSPacketSrv0A {
public:
	uint8_t nHeader;
	uint8_t nUnitType;
	uint32_t dwUnitGUID;
};
#pragma pack()

static SCMDHANDLER oD2CLIENT_PACKETCALLBACK_Rcv0x0A_SCMD_REMOVEUNIT = reinterpret_cast<SCMDHANDLER>(Pattern::Address(0xD6D10));
static SCMDHANDLER oD2CLIENT_PACKETCALLBACK_Rcv0x9C_SCMD_ITEMEX = reinterpret_cast<SCMDHANDLER>(Pattern::Address(0xDD040));
static SCMDHANDLER oD2CLIENT_PACKETCALLBACK_Rcv0x9D_SCMD_ITEMUNITEX = reinterpret_cast<SCMDHANDLER>(Pattern::Address(0xDD4E0));
typedef int64_t(__fastcall* ITEMS_Serialize_t)(D2UnitStrc* pUnit, D2BufferStrc* pBuffer, uint32_t a3, int32_t a4, int32_t a5);
static ITEMS_Serialize_t oITEMS_Serialize = reinterpret_cast<ITEMS_Serialize_t>(Pattern::Address(0x207610));
typedef void* (__fastcall* BASE64_Encode_t)(char* pOutput, int64_t* pOutputLength, uint8_t* pInput, uint64_t nInputLength);
static BASE64_Encode_t oBASE64_Encode = reinterpret_cast<BASE64_Encode_t>(Pattern::Address(0xae5970));
typedef D2UnitStrc*(__fastcall* UNITS_AllocUnit_t)(uint32_t dwUnitType);
static UNITS_AllocUnit_t oUNITS_AllocUnit = reinterpret_cast<UNITS_AllocUnit_t>(Pattern::Address(0x208670));
typedef void(__fastcall* UNITS_FreeUnit_t)(D2UnitStrc* pUnit);
static UNITS_FreeUnit_t oUNITS_FreeUnit = reinterpret_cast<UNITS_FreeUnit_t>(Pattern::Address(0x208b80));
typedef void(__fastcall* ITEMS_GetName_t)(D2UnitStrc* pUnit, char* pBuffer);
static ITEMS_GetName_t oITEMS_GetName = reinterpret_cast<ITEMS_GetName_t>(Pattern::Address(0x149b60));
typedef bool(__fastcall* ITEMS_CheckItemTypeId_t)(D2UnitStrc* pUnit, int32_t nItemType);
static ITEMS_CheckItemTypeId_t oITEMS_CheckItemTypeId = reinterpret_cast<ITEMS_CheckItemTypeId_t>(Pattern::Address(0x1fe680));
typedef void*(__fastcall* CHAT_PushMessage_t)(void* pChatManager, small_string_opt<16>* pMessage, int32_t color, bool a4, void* pMessageType, void* a6, void* a7, void* a8, void* a9);
static CHAT_PushMessage_t oCHAT_PushMessage = reinterpret_cast<CHAT_PushMessage_t>(Pattern::Address(0x56e9a0));
static void* gpChatManager = reinterpret_cast<void*>(Pattern::Address(0x1e43550));
static void* gpGameMessageType = reinterpret_cast<void*>(Pattern::Address(0x184abe8));
static D2DataTablesStrc* sgptDataTables = reinterpret_cast<D2DataTablesStrc*>(Pattern::Address(0x1c9e980));
static D2UnitStrc** ppClientUnitList = reinterpret_cast<D2UnitStrc**>(Pattern::Address(0x1d442e0));
static int32_t* gpClientPlayerListIndex = reinterpret_cast<int32_t*>(Pattern::Address(0x1d442d8));
static int32_t* gpClientPlayerIds = reinterpret_cast<int32_t*>(Pattern::Address(0x1bf1e80));
static D2ClientStrc** gpClientList = reinterpret_cast<D2ClientStrc**>(Pattern::Address(0x1d637f0));
static sol::state lua;
static sol::protected_function applyFilter;
static MonsterStatsDisplaySettings cachedSettings;
typedef void(__fastcall* GFX_DrawFilledRect_t)(int x, int y, int w, int h, float* rgba);
static GFX_DrawFilledRect_t oGFX_DrawFilledRect = reinterpret_cast<GFX_DrawFilledRect_t>(Pattern::Address(0x439280));
typedef int64_t* (__fastcall* UI_DrawGroundItemBackground_t)(D2UnitRectStrc* pRect, const char* szText, float* rgba);
static UI_DrawGroundItemBackground_t oUI_DrawGroundItemBackground = reinterpret_cast<UI_DrawGroundItemBackground_t>(Pattern::Address(0xc027f0));
typedef int64_t* (__fastcall* TooltipsPanel_DrawTooltip_t)(int64_t* a1);
static TooltipsPanel_DrawTooltip_t oTooltipsPanel_DrawTooltip = reinterpret_cast<TooltipsPanel_DrawTooltip_t>(Pattern::Address(0x5ba0c0));
typedef D2UnitStrc* (__fastcall* UNITS_GetHoveredUnit_t)(uint32_t nClientPlayerListIndex);
static UNITS_GetHoveredUnit_t oUNITS_GetHoveredUnit = reinterpret_cast<UNITS_GetHoveredUnit_t>(Pattern::Address(0xdedb0));
typedef void(__fastcall* DATATBLS_LoadAllTxts_t)();
static DATATBLS_LoadAllTxts_t oDATATBLS_LoadAllTxts = reinterpret_cast<DATATBLS_LoadAllTxts_t>(Pattern::Address(0x1de6c0));
typedef void(__fastcall* DATATBLS_UnloadAllBins_t)();
static DATATBLS_UnloadAllBins_t oDATATBLS_UnloadAllBins = reinterpret_cast<DATATBLS_UnloadAllBins_t>(Pattern::Address(0x1dd580));
typedef void(__fastcall* DATATBLS_LoadAllJson_t)();
static DATATBLS_LoadAllJson_t oDATATBLS_LoadAllJson = reinterpret_cast<DATATBLS_LoadAllJson_t>(Pattern::Address(0x6054f));
typedef void(__fastcall* UI_BuildGroundItemTooltip_t)(D2UnitStrc* pUnit, char* szTooltipText, int64_t a3, uint32_t* nColorCode);
static UI_BuildGroundItemTooltip_t oUI_BuildGroundItemTooltip = reinterpret_cast<UI_BuildGroundItemTooltip_t>(Pattern::Address(0x83F90));
typedef uint8_t(__fastcall* ITEMS_GetMaxSockets_t)(D2UnitStrc* pUnit);
static ITEMS_GetMaxSockets_t oITEMS_GetMaxSockets = reinterpret_cast<ITEMS_GetMaxSockets_t>(Pattern::Address(0x1FDE00));
typedef void(__fastcall* ITEMS_GetStatsDescription_t)(D2UnitStrc* pUnit, char* pBuffer, uint64_t nBufferSize, int a4, int a5, int a6, unsigned int a7, int a8);
ITEMS_GetStatsDescription_t oITEMS_GetStatsDescription = reinterpret_cast<ITEMS_GetStatsDescription_t>(Pattern::Address(0x1c72d0));
class D2ItemUnitStrc : public D2UnitStrc {};
class D2PlayerUnitStrc : public D2UnitStrc {};
static std::unordered_map<DWORD, int32_t> g_TransmogValueByUnitId;
static std::mutex g_TransmogValueMutex;
static std::unordered_map<DWORD, std::string> g_StoredItemNames;
static std::mutex g_StoredItemNamesMutex;
uint32_t crc32_table[256];

static std::string GetModName() {
	uint64_t pModNameAddr = Pattern::Address(0x1BF084F);
	if (pModNameAddr == 0) {
		return "";
	}

	const char* pModName = reinterpret_cast<const char*>(pModNameAddr);
	if (!pModName) {
		return "";
	}

	return std::string(pModName);
}

std::string ModName = GetModName();
std::string gWelcomeMessage;
sol::protected_function getWelcomeMessage;

std::unordered_map<std::string, std::string> colorMap = {
	{"white", "ÿc0"}, {"red", "ÿc1"}, {"green", "ÿc2"}, {"blue", "ÿc3"},
	{"gold", "ÿc4"}, {"grey", "ÿc5"}, {"gray", "ÿc5"}, {"black", "ÿc6"},
	{"tan", "ÿc7"}, {"orange", "ÿc8"}, {"yellow", "ÿc9"}, {"purple", "ÿc;"},
	{"dark green", "ÿcA"}, {"turquoise", "ÿcN"}, {"pink", "ÿcO"}, {"lilac", "ÿcP"}
};

#pragma endregion

#pragma region Global Helpers

D2UnitStrc* GetUnitByIdAndType(D2UnitStrc** ppUnitsList, uint32_t nUnitId, D2C_UnitTypes nUnitType) {
	auto ppUnitList = &ppUnitsList[nUnitType * 0x80];
	auto pHashEntry = ppUnitList[nUnitId & 0x7F];
	while (pHashEntry && pHashEntry->dwUnitId != nUnitId) {
		pHashEntry = pHashEntry->pListNext;
	}
	return pHashEntry;
}

D2UnitStrc* GetClientPlayerUnit() {
	if (!ppClientUnitList || !gpClientPlayerListIndex || *gpClientPlayerListIndex < 0)
		return nullptr;

	uint32_t playerId = gpClientPlayerIds[*gpClientPlayerListIndex];
	return GetUnitByIdAndType(ppClientUnitList, playerId, UNIT_PLAYER);
}

bool IsPlayerInGame() {
	return GetClientPlayerUnit() != nullptr;
}

void PrintGameMessage(std::string message) {
	std::cout << message << std::endl;
	small_string_opt<16> tMessage = {
		message.c_str(),
		message.size(),
		16
	};
	int64_t unk = 0;
	oCHAT_PushMessage(gpChatManager, &tMessage, 4, 0, gpGameMessageType, &unk, &unk, &unk, &unk);
}

bool HandleError(const sol::protected_function_result& result) {
	if (!result.valid()) {
		sol::error err = result;
		PrintGameMessage(err.what());
		return true;
	}
	return false;
}

inline bool IsHovered(float* rgba) {
	return rgba[0] == 0.f && rgba[1] == 0.25f && rgba[2] == 0.5f && rgba[3] == 1.f;
}

int32_t STATLIST_GetUnitStatSignedLayer0(D2UnitStrc* pThat, uint32_t nStatId) {
	return STATLIST_GetUnitStatSigned(pThat, nStatId, 0);
}

std::string GetExeDir()
{
	char buffer[MAX_PATH];
	GetModuleFileNameA(nullptr, buffer, MAX_PATH);
	std::filesystem::path exePath(buffer);
	return exePath.parent_path().string();
}

int GetRarity(D2UnitStrc* pItem)
{
	if (!pItem) return -1;

	auto& pItemTxt = sgptDataTables->pItemsTxt[pItem->dwClassId];

	if (strncmp(pItemTxt.szCode, pItemTxt.dwUltraCode, 4) == 0)
		return 2; // Elite
	else if (strncmp(pItemTxt.szCode, pItemTxt.dwUberCode, 4) == 0)
		return 1; // Exceptional
	else if (strncmp(pItemTxt.szCode, pItemTxt.dwNormCode, 4) == 0)
		return 0; // Normal

	return -1; // Unknown
}

std::string ReplaceColorCodes(const std::string& input) {
	std::string result = input;
	size_t pos = 0;

	while ((pos = result.find('{', pos)) != std::string::npos) {
		size_t end = result.find('}', pos);
		if (end == std::string::npos) break;

		std::string code = result.substr(pos + 1, end - pos - 1);
		// convert to lowercase to match map keys
		std::transform(code.begin(), code.end(), code.begin(), ::tolower);

		auto it = colorMap.find(code);
		if (it != colorMap.end()) {
			result.replace(pos, end - pos + 1, it->second);
		}
		else {
			// if not found, just remove braces
			result.replace(pos, 1, ""); // remove '{'
			result.replace(pos + (end - pos - 1), 1, ""); // remove '}'
		}

		pos++; // move past current replacement
	}

	return result;
}

void HandleItemBackground(D2UnitStrcCustom* pUnit, D2UnitRectStrc* pRect, float* rgba) {
	if (pUnit && pUnit->pFilterResult) {
		auto bIsHovered = IsHovered(rgba);
		sol::protected_function fBackground = bIsHovered ? pUnit->pFilterResult->cbHoveredBackgroundFunction : pUnit->pFilterResult->cbBackgroundFunction;
		if (fBackground.valid()) {
			HandleError(fBackground(
				reinterpret_cast<D2ItemUnitStrc*>(pUnit),
				pUnit->pFilterResult,
				GetTickCount()
			));
		}
		auto& backGroundColor = bIsHovered ? pUnit->pFilterResult->nHoveredBackgroundColorGround : pUnit->pFilterResult->nBackgroundColorGround;
		rgba[0] = backGroundColor[0];
		rgba[1] = backGroundColor[1];
		rgba[2] = backGroundColor[2];
		rgba[3] = backGroundColor[3];

		auto& border = bIsHovered ? pUnit->pFilterResult->nHoveredBorderColorGround : pUnit->pFilterResult->nBorderColorGround;
		float borderWidth = (border.size() >= 5) ? border[4] : 0.0f;

		if (borderWidth > 0.0f && border.size() >= 4) {
			oGFX_DrawFilledRect(
				pRect->nX - borderWidth,
				pRect->nY - borderWidth,
				pRect->nX + pRect->nW + borderWidth,
				pRect->nY + pRect->nH + borderWidth,
				border.data()
			);
		}
	}
}

int32_t GetStoredTransmogValue(DWORD dwUnitId)
{
	std::lock_guard<std::mutex> lock(g_TransmogValueMutex);
	auto it = g_TransmogValueByUnitId.find(dwUnitId);
	return (it != g_TransmogValueByUnitId.end()) ? it->second : 0;
}

const char* GetStoredItemName(DWORD dwUnitId)
{
	std::lock_guard<std::mutex> lock(g_StoredItemNamesMutex);
	auto it = g_StoredItemNames.find(dwUnitId);
	return (it != g_StoredItemNames.end()) ? it->second.c_str() : nullptr;
}

void InitCRC32Table()
{
	const uint32_t polynomial = 0xEDB88320;
	for (uint32_t i = 0; i < 256; ++i)
	{
		uint32_t crc = i;
		for (uint32_t j = 0; j < 8; ++j)
			crc = (crc >> 1) ^ ((crc & 1) ? polynomial : 0);
		crc32_table[i] = crc;
	}
}

uint32_t ComputeCRC32(const std::string& data)
{
	uint32_t crc = 0xFFFFFFFF;
	for (unsigned char c : data)
		crc = (crc >> 8) ^ crc32_table[(crc ^ c) & 0xFF];
	return crc ^ 0xFFFFFFFF;
}

bool ReadFileToString(const std::string& path, std::string& outStr)
{
	std::ifstream in(path, std::ios::binary);
	if (!in.is_open()) return false;
	in.seekg(0, std::ios::end);
	size_t size = in.tellg();
	in.seekg(0, std::ios::beg);
	outStr.resize(size);
	in.read(&outStr[0], size);
	return true;
}

#pragma endregion

#pragma region Hooks

void __fastcall Hooked_ITEMS_GetName(D2UnitStrc* pUnit, char* pBuffer)
{
	oITEMS_GetName(pUnit, pBuffer);

	auto pUnitToUse = pUnit;
	if (gpClientList) {
		auto pClient = *gpClientList;

		if (pClient && pClient->pGame)
			pUnitToUse = UNITS_GetServerUnitByTypeAndId(pClient->pGame, (D2C_UnitTypes)pUnit->dwUnitType, pUnit->dwUnitId);
	}

	if (pUnitToUse != nullptr) {
		auto pUnitCustom = (D2UnitStrcCustom*)pUnit;
		char cBuffer[0x400];
		snprintf(cBuffer, 0x400, pUnitCustom->pFilterResult->szName.c_str(), pBuffer);
		strncpy(pBuffer, cBuffer, 0x400);

		if (pUnitCustom->pFilterResult->cbNameFunction.valid()) {
			std::string szName = pBuffer;
			sol::protected_function_result result = pUnitCustom->pFilterResult->cbNameFunction(reinterpret_cast<D2ItemUnitStrc*>(pUnit), szName, GetTickCount());
			if (HandleError(result)) return;

			sol::object data = result;

			if (data.valid() && data.is<std::string>())
				szName = data.as<std::string>();

			strncpy(pBuffer, szName.c_str(), 0x400);
		}
		{
			std::lock_guard<std::mutex> lock(g_StoredItemNamesMutex);
			g_StoredItemNames[pUnit->dwUnitId] = pBuffer;
		}
	}
}

void __fastcall Hooked_ITEMS_GetNameGold(D2UnitStrc* pUnit, char* pBuffer) {
	oITEMS_GetName(pUnit, pBuffer);
	auto pUnitToUse = pUnit;
	if (gpClientList) {
		auto pClient = *gpClientList;
		if (pClient && pClient->pGame) {
			pUnitToUse = UNITS_GetServerUnitByTypeAndId(pClient->pGame, (D2C_UnitTypes)pUnit->dwUnitType, pUnit->dwUnitId);
		}
	}
	if (pUnitToUse != nullptr) {
		auto pUnitCustom = (D2UnitStrcCustom*)pUnit;
		char cBuffer[0x7F];
		snprintf(cBuffer, 0x7F, pUnitCustom->pFilterResult->szName.c_str(), pBuffer);
		strncpy(pBuffer, cBuffer, 0x7F);

		if (pUnitCustom->pFilterResult->cbNameFunction.valid()) {
			std::string szName = std::string(pBuffer);
			sol::protected_function_result result = pUnitCustom->pFilterResult->cbNameFunction(
				reinterpret_cast<D2ItemUnitStrc*>(pUnit),
				szName,
				GetTickCount()
			);
			if (HandleError(result)) {
				return;
			}
			sol::object data = result;
			if (data.valid() && data.is<std::string>()) {
				szName = data.as<std::string>();
			}
			strncpy(pBuffer, szName.c_str(), 0x400);

		}
	}
}

void __fastcall Hooked_UI_BuildGroundItemTooltip(D2UnitStrc* pUnit, char* szTooltipText, int64_t a3, uint32_t* nColorCode) {
	oUI_BuildGroundItemTooltip(pUnit, szTooltipText, a3, nColorCode);
	auto pUnitCustom = (D2UnitStrcCustom*)pUnit;
	bool shouldUseItemFilter = pUnitCustom
		&& pUnitCustom->pFilterResult
		&& (pUnitCustom->pFilterResult->szName != "%s"
			|| pUnitCustom->pFilterResult->cbNameFunction != sol::nil);
	if (oITEMS_CheckItemTypeId(pUnit, ITEMTYPE_GOLD)
		&& shouldUseItemFilter) {
		Hooked_ITEMS_GetNameGold(pUnit, szTooltipText);
	}
}

void Hooked_ITEMS_GetStatsDescription(D2UnitStrc* pUnit, char* pBuffer, uint64_t nBufferSize, int a4, int a5, int a6, unsigned int a7, int a8)
{
	oITEMS_GetStatsDescription(pUnit, pBuffer, nBufferSize, a4, a5, a6, a7, a8);

	auto pUnitToUse = pUnit;
	if (gpClientList) {
		auto pClient = *gpClientList;
		if (pClient && pClient->pGame)
			pUnitToUse = UNITS_GetServerUnitByTypeAndId(pClient->pGame, (D2C_UnitTypes)pUnit->dwUnitType, pUnit->dwUnitId);
	}

	if (pUnitToUse != nullptr) {
		auto pUnitCustom = (D2UnitStrcCustom*)pUnit;
		char cBuffer[0x400];
		snprintf(cBuffer, 0x400, pUnitCustom->pFilterResult->szDescription.c_str(), pBuffer);
		strncpy(pBuffer, cBuffer, 0x400);

		if (pUnitCustom->pFilterResult->cbDescFunction.valid()) {
			std::string szName = std::string(pBuffer);
			auto result = pUnitCustom->pFilterResult->cbDescFunction(reinterpret_cast<D2ItemUnitStrc*>(pUnit), szName, GetTickCount());

			if (HandleError(result))
				return;

			sol::object data = result;

			if (data.valid() && data.is<std::string>())
				szName = data.as<std::string>();

			strncpy(pBuffer, szName.c_str(), 0x400);
		}
	}

	// Apply Transmog Text
	if (cachedSettings.TransmogVisuals)
	{
		int v = GetStoredTransmogValue(pUnit->dwUnitId);
		if (v > 0)
		{
			const char* dbg = "ÿc8-Transmogged-ÿc0\n";
			size_t len = strlen(pBuffer);

			// ensure no overflow
			if (len < nBufferSize - strlen(dbg) - 1)
				strncat(pBuffer, dbg, nBufferSize - len - 1);
		}
	}	
}

int64_t* __fastcall Hooked_TooltipsPanel_DrawTooltip(int64_t* a1) {
	auto pUnit = reinterpret_cast<D2UnitStrcCustom*>(oUNITS_GetHoveredUnit(*gpClientPlayerListIndex));
	HandleItemBackground(pUnit, reinterpret_cast<D2UnitRectStrc*>((uint32_t*)a1 + 0x18), (float*)a1 + 0x5A);
	return oTooltipsPanel_DrawTooltip(a1);
}

int64_t* __fastcall Hooked_UI_DrawGroundItemBackground(D2UnitRectStrc* pRect, const char* szText, float* rgba) {
	auto pUnit = reinterpret_cast<D2UnitStrcCustom*>(GetUnitByIdAndType(ppClientUnitList, pRect->dwUnitId, UNIT_ITEM));
	HandleItemBackground(pUnit, pRect, rgba);
	return oUI_DrawGroundItemBackground(pRect, szText, rgba);
}

#pragma endregion

#pragma region LUA Registers

void RegisterD2ItemFilterResultStrc(sol::state& s) {
	auto type = s.new_usertype<D2ItemFilterResultStrc>("D2ItemFilterResultStrc", sol::no_constructor,
		"Hide", &D2ItemFilterResultStrc::bHide,
		"Name", &D2ItemFilterResultStrc::szName,
		"Background", &D2ItemFilterResultStrc::nBackgroundColorGround,
		"HoveredBackground", &D2ItemFilterResultStrc::nHoveredBackgroundColorGround,
		"Border", &D2ItemFilterResultStrc::nBorderColorGround,
		"HoveredBorder", &D2ItemFilterResultStrc::nHoveredBorderColorGround,
		"BackgroundFunction", &D2ItemFilterResultStrc::cbBackgroundFunction,
		"HoveredBackgroundFunction", &D2ItemFilterResultStrc::cbHoveredBackgroundFunction,
		"NameFunction", &D2ItemFilterResultStrc::cbNameFunction,
		"Description", &D2ItemFilterResultStrc::szDescription,
		"DescriptionFunction", &D2ItemFilterResultStrc::cbDescFunction
	);
}

void RegisterD2UnitStrc(sol::state& s) {
	// Base D2UnitStrc
	auto unitType = s.new_usertype<D2UnitStrc>("D2UnitStrc", sol::no_constructor,
		"Address", sol::readonly_property([](D2UnitStrc* pThat) -> std::string { std::stringstream ss; ss << std::hex << pThat; return ss.str(); }),
		"ID", &D2UnitStrc::dwUnitId,
		"Mode", &D2UnitStrc::dwAnimMode
	);

	unitType["Stat"] = sol::overload(
		sol::resolve<int32_t(D2UnitStrc*, uint32_t)>(STATLIST_GetUnitStatSignedLayer0),
		sol::resolve<int32_t(D2UnitStrc*, uint32_t, uint16_t)>(STATLIST_GetUnitStatSigned)
	);

	// Player unit
	auto playerType = s.new_usertype<D2PlayerUnitStrc>("D2PlayerUnitStrc", sol::no_constructor,
		"Class", &D2UnitStrc::dwClassId,
		"Name", sol::readonly_property([](D2UnitStrc* pThat) -> const char* { return pThat->pPlayerData->szName; }),
		"Data", sol::readonly_property([](D2UnitStrc* pThat) -> D2PlayerDataStrc* { return pThat->pPlayerData; }),
		"Difficulty", sol::readonly_property([](D2UnitStrc* pThat) -> int32_t { return pThat->pDynamicPath && pThat->pDynamicPath->pRoom && pThat->pDynamicPath->pRoom->pDrlgRoom && pThat->pDynamicPath->pRoom->pDrlgRoom->pLevel && pThat->pDynamicPath->pRoom->pDrlgRoom->pLevel->pDrlg ? pThat->pDynamicPath->pRoom->pDrlgRoom->pLevel->pDrlg->nDifficulty : -1; }),
		sol::base_classes, sol::bases<D2UnitStrc>()
	);

	// Item unit
	auto itemType = s.new_usertype<D2ItemUnitStrc>("D2ItemUnitStrc", sol::no_constructor,
		"Name", sol::readonly_property([](D2UnitStrc* pThat) -> std::string {
			char pBuffer[0x400] = {};
			Hooked_ITEMS_GetName(pThat, pBuffer);
			return std::string(pBuffer);
			}),
		/*
		"DescriptionString", sol::readonly_property([](D2UnitStrc* pThat) -> std::string {
			char pBuffer[0x400] = {};
			Hooked_ITEMS_GetStatsDescription(pThat, pBuffer, 0x400, 0, 0, 0, 0, 0);
			std::string desc = pBuffer;

			// --- Debug output to file ---
			{
				std::ofstream outFile("Description_Debug.txt", std::ios::out | std::ios::trunc);
				if (outFile.is_open()) {
					outFile << desc;
					outFile.close();
				}
			}

			// --- Secondary output with "Attack Rating" -> "Attack Rate" ---
			std::string modifiedDesc = desc;
			size_t pos = 0;
			const std::string search = "Attack Rating";
			const std::string replace = "Attack Rate";
			while ((pos = modifiedDesc.find(search, pos)) != std::string::npos) {
				modifiedDesc.replace(pos, search.length(), replace);
				pos += replace.length();
			}

			// Optional: also output this modified version to a separate file
			{
				std::ofstream outFile2("Description_Modified_Debug.txt", std::ios::out | std::ios::trunc);
				if (outFile2.is_open()) {
					outFile2 << modifiedDesc;
					outFile2.close();
				}
			}

			return desc; // Keep original DescriptionString as the property value
			}),
			*/
		"Data", sol::readonly_property([](D2UnitStrc* pThat) -> D2ItemDataStrc* { return pThat->pItemData; }),
		"Txt", sol::readonly_property([](D2UnitStrc* pThat) -> D2ItemsTxt { return sgptDataTables->pItemsTxt[pThat->dwClassId]; }),
		"IsOnGround", sol::readonly_property([](D2UnitStrc* pThat) -> bool { return pThat->dwAnimMode == IMODE_ONGROUND; }),
		"IsEquipped", sol::readonly_property([](D2UnitStrc* pThat) -> bool { return pThat->dwAnimMode == IMODE_EQUIP; }),
		"Rarity", sol::readonly_property([](D2UnitStrc* pThat) -> int32_t {
			switch (pThat->dwUnitType) {
			case UNIT_ITEM:
			{
				auto pItemTxt = sgptDataTables->pItemsTxt[pThat->dwClassId];
				if (strncmp(pItemTxt.szCode, pItemTxt.dwUltraCode, 4) == 0) return 2;
				else if (strncmp(pItemTxt.szCode, pItemTxt.dwUberCode, 4) == 0) return 1;
				else if (strncmp(pItemTxt.szCode, pItemTxt.dwNormCode, 4) == 0) return 0;
				return -1;
			}
			default: return -1;
			}
			}),
		"Area", sol::readonly_property([](D2UnitStrc* pThat) -> int32_t { return pThat->pStaticPath && pThat->pStaticPath->pRoom && pThat->pStaticPath->pRoom->pDrlgRoom && pThat->pStaticPath->pRoom->pDrlgRoom->pLevel ? pThat->pStaticPath->pRoom->pDrlgRoom->pLevel->nLevelId : 0; }),
		sol::base_classes, sol::bases<D2UnitStrc>()
	);

	itemType["IsType"] = oITEMS_CheckItemTypeId;
	itemType["MaxSockets"] = oITEMS_GetMaxSockets;
	itemType["Link"] = [](D2UnitStrc* pThat, char color) -> std::string {
		char pLinkData[0x558] = {};
		char pName[0x400] = {};
		char pOutput[0x400] = {};
		int64_t nLinkDataSize = 0x558;
		uint8_t pSerialized[0x400] = {};
		D2BufferStrc pBuffer = { pSerialized, pThat->dwUnitType }; // example serialization
		uint32_t nOldAnimMode = pThat->dwAnimMode;
		pThat->dwAnimMode = IMODE_STORED;
		oITEMS_Serialize(pThat, &pBuffer, 1, 1, 0);
		pThat->dwAnimMode = nOldAnimMode;

		oBASE64_Encode(pLinkData, &nLinkDataSize, pSerialized, pBuffer.nUnk0x18 ? pBuffer.nUnk0x10 + 1 : pBuffer.nUnk0x10);

		Hooked_ITEMS_GetName(pThat, pName);
		std::string szName = std::string(pName);
		size_t pos = szName.rfind('\n');
		if (pos != std::string::npos) szName.erase(0, pos + 1);

		std::string szNameFormatted = std::format("ÿc0ÿc{}[{}ÿc{}]ÿc0", color, szName, color);
		snprintf(pOutput, 0x400, "%sÿi%d.%d.%s", szNameFormatted.c_str(), 0, static_cast<int>(szNameFormatted.length() - 4), pLinkData);
		return std::string(pOutput);
		};
}

void RegisterD2ItemDataStrc(sol::state& s) {
	auto type = s.new_usertype<D2ItemDataStrc>("D2ItemDataStrc", sol::no_constructor,
		"Quality", &D2ItemDataStrc::dwQualityNo,
		"Owner", &D2ItemDataStrc::dwOwnerGUID,
		"Flags", &D2ItemDataStrc::dwItemFlags,
		"BodyLoc", &D2ItemDataStrc::nBodyLoc,
		"Page", &D2ItemDataStrc::nPage,
		"FileIndex", &D2ItemDataStrc::dwFileIndex,
		"RarePrefix", &D2ItemDataStrc::wRarePrefix,
		"RareSuffix", &D2ItemDataStrc::wRareSuffix,
		"MagicPrefixes", sol::readonly_property([](D2ItemDataStrc* pThat) -> std::vector<uint16_t> { return { pThat->wMagicPrefix[0], pThat->wMagicPrefix[1], pThat->wMagicPrefix[2] }; }),
		"MagicSuffixes", sol::readonly_property([](D2ItemDataStrc* pThat) -> std::vector<uint16_t> { return { pThat->wMagicSuffix[0], pThat->wMagicSuffix[1], pThat->wMagicSuffix[2] }; }),
		"Gfx", &D2ItemDataStrc::nInvGfxIdx,
		"IsIdentified", sol::readonly_property([](D2ItemDataStrc* pThat) -> bool { return (pThat->dwItemFlags & IFLAG_IDENTIFIED) != 0; }),
		"IsEthereal", sol::readonly_property([](D2ItemDataStrc* pThat) -> bool { return (pThat->dwItemFlags & IFLAG_ETHEREAL) != 0; }),
		"ItemLevel", &D2ItemDataStrc::dwItemLevel
	);
}

void RegisterD2PlayerDataStrc(sol::state& s) {
	auto type = s.new_usertype<D2PlayerDataStrc>("D2PlayerDataStrc", sol::no_constructor,
		"Name", &D2PlayerDataStrc::szName
	);
}

void RegisterD2ItemsTxt(sol::state& s) {
	auto type = s.new_usertype<D2ItemsTxt>("D2ItemsTxt", sol::no_constructor,
		"Code", sol::readonly_property([](D2ItemsTxt* pThat) -> std::string { return std::string(pThat->szCode, pThat->szCode[3] == 0x20 ? 3 : 4);  }),
		"UberCode", sol::readonly_property([](D2ItemsTxt* pThat) -> std::string { return std::string(pThat->dwUberCode, pThat->dwUberCode[3] == 0x20 ? 3 : 4);  }),
		"UltraCode", sol::readonly_property([](D2ItemsTxt* pThat) -> std::string { return std::string(pThat->dwUltraCode, pThat->dwUltraCode[3] == 0x20 ? 3 : 4);  })
	);
}

void RegisterBasicTypes(sol::state& s) {
	// Register basic container factory functions so we don't have to do stuff like ipairs in lua.
	s.set_function("SSet", [](sol::variadic_args args) { std::set<std::string> l; for (auto&& arg : args) { l.insert(arg.as<std::string>()); } return l; });
	s.set_function("ISet", [](sol::variadic_args args) { std::set<std::int32_t> l; for (auto&& arg : args) { l.insert(arg.as<std::int32_t>()); } return l; });
}

#pragma endregion

#pragma region Filter Load/Apply

void LoadScript() {
	applyFilter = sol::nil;
	getWelcomeMessage = sol::nil;

	auto env = sol::environment(lua, sol::create, lua.globals());
	std::string fullPath = std::filesystem::absolute("lootfilter.lua").string();
	sol::load_result load_result = lua.load_file(fullPath);
	if (!load_result.valid()) {
		sol::error err = load_result;
		PrintGameMessage(err.what());
		return;
	}

	sol::protected_function func = load_result;
	sol::set_environment(env, func);
	if (HandleError(load_result())) return;

	applyFilter = env["ApplyFilter"];
	getWelcomeMessage = env["GetWelcomeMessage"];

	sol::set_environment(env, applyFilter);
}

void InitializeLUA() {
	lua.open_libraries(sol::lib::base, sol::lib::package,
		sol::lib::table, sol::lib::coroutine, sol::lib::string,
		sol::lib::math, sol::lib::io, sol::lib::os);
	RegisterBasicTypes(lua);
	RegisterD2ItemFilterResultStrc(lua);
	RegisterD2ItemDataStrc(lua);
	RegisterD2PlayerDataStrc(lua);
	RegisterD2ItemsTxt(lua);
	RegisterD2UnitStrc(lua);
	lua["print"] = [](sol::this_state s, sol::variadic_args va) {
		sol::state_view l(s);
		for (const sol::object& v : va) {
			std::string s = l["tostring"](v);
			PrintGameMessage(s);
		}
		};
	LoadScript();
}

void DoFilter(D2UnitStrc* pItem) {
	auto pItemToApplyFilterOn = pItem;
	if (!pItem) {
		return;
	}
	auto pItemCustom = reinterpret_cast<D2UnitStrcCustom*>(pItem);
	// Do Filter
	if (applyFilter.valid()) {
		delete pItemCustom->pFilterResult;
		pItemCustom->pFilterResult = new D2ItemFilterResultStrc();
		if (gpClientList) {
			auto pClient = *gpClientList;
			if (pClient && pClient->pGame) {
				pItemToApplyFilterOn = UNITS_GetServerUnitByTypeAndId(pClient->pGame, (D2C_UnitTypes)pItemToApplyFilterOn->dwUnitType, pItemToApplyFilterOn->dwUnitId);
			}
		}
		HandleError(applyFilter(
			reinterpret_cast<D2PlayerUnitStrc*>(GetUnitByIdAndType(ppClientUnitList, gpClientPlayerIds[*gpClientPlayerListIndex], UNIT_PLAYER)),
			reinterpret_cast<D2ItemUnitStrc*>(pItemToApplyFilterOn),
			pItemCustom->pFilterResult));
	}

	if (pItemCustom->pFilterResult
		&& pItemCustom->pFilterResult->bHide) {
		// Todo... Hide item instead of removing it?
		D2GSPacketSrv0A remove = { 0xA, UNIT_ITEM, pItemToApplyFilterOn->dwUnitId };
		oD2CLIENT_PACKETCALLBACK_Rcv0x0A_SCMD_REMOVEUNIT(reinterpret_cast<uint8_t*>(&remove));
	}
}

void DoFilter(uint32_t nUnitId) {
	DoFilter(GetUnitByIdAndType(ppClientUnitList, nUnitId, UNIT_ITEM));
}

#pragma region Filter Apply Hooks

int64_t __fastcall Hooked_D2CLIENT_PACKETCALLBACK_Rcv0x9C_SCMD_ITEMEX(uint8_t* pPacket) {
	auto result = oD2CLIENT_PACKETCALLBACK_Rcv0x9C_SCMD_ITEMEX(pPacket);
	DoFilter(*(uint32_t*)(pPacket + 4));
	return result;
}

int64_t __fastcall Hooked_D2CLIENT_PACKETCALLBACK_Rcv0x9D_SCMD_ITEMUNITEX(uint8_t* pPacket) {
	auto result = oD2CLIENT_PACKETCALLBACK_Rcv0x9D_SCMD_ITEMUNITEX(pPacket);
	DoFilter(*(uint32_t*)(pPacket + 4));
	return result;
}

void __fastcall Hooked_UNITS_FreeUnit(D2UnitStrc* pUnit) {
	if (((D2UnitStrcCustom*)pUnit)->pFilterResult) {
		delete ((D2UnitStrcCustom*)pUnit)->pFilterResult;
	}
	oUNITS_FreeUnit(pUnit);
}

D2UnitStrc* __fastcall Hooked_UNITS_AllocUnit(uint32_t dwUnitType) {
	D2UnitStrc* pUnit = oUNITS_AllocUnit(dwUnitType);
	((D2UnitStrcCustom*)pUnit)->pFilterResult = new D2ItemFilterResultStrc();
	return pUnit;
}

#pragma endregion

#pragma endregion

#pragma region Transmog System

#pragma region Static/Structs

struct ItemAssets {
	std::string normal;
	std::string uber;
	std::string ultra;

	ItemAssets() = default;
	ItemAssets(const std::string& asset) : normal(asset), uber(asset), ultra(asset) {}
	ItemAssets(const std::string& n, const std::string& u, const std::string& ul)
		: normal(n), uber(u), ultra(ul) {
	}
};

struct ItemRow {
	std::string code_name;
	ItemAssets base;
	ItemAssets sets;
	ItemAssets uniques;
	std::string override_asset;
};

struct TransmogRow {
	int index;
	std::string code_name;
	std::string base_path;
	std::string sets_normal;
	std::string sets_exceptional;
	std::string sets_elite;
	std::string uniques_normal;
	std::string uniques_exceptional;
	std::string uniques_elite;
};

std::vector<TransmogRow> g_TransmogTable;

static auto BaseAddress = reinterpret_cast<uintptr_t>(GetModuleHandle(nullptr));
static auto UNITS_GetInvGfxFromJson = reinterpret_cast<blz__string16 * (__fastcall*)(blz__string16*, D2UnitStrc*)>(BaseAddress + 0x87ee0);
static auto UNITS_GetItemGfxFromJson = reinterpret_cast<const char* (__fastcall*)(void*, D2UnitStrc*, int32_t, uint32_t)>(BaseAddress + 0x42d2c0);
std::unordered_map<std::string, std::string> g_SpecialItemGfx;
static std::unordered_map<D2UnitStrc*, std::string> g_InvGfxCache;
static std::unordered_map<D2UnitStrc*, std::string> g_ItemGfxCache;
static std::mutex g_CacheMutex;
static auto& gJsonManager = *reinterpret_cast<D2JSONManagerStrc*>(BaseAddress + 0x1e3d8b0);
static auto& gJsonManagerU = *reinterpret_cast<D2JSONManagerStrc*>(BaseAddress + 0x1e3f8b0);
using json = nlohmann::json;
std::vector<ItemRow> g_ItemRows;
extern std::vector<TransmogRow> g_TransmogTable;
static std::unordered_map<DWORD, const char*> g_LastTransmogPathByUnitId;
static std::mutex g_LastTransmogPathMutex;

#pragma endregion

#pragma region Load/Save Functions

const char* GetTransmog(D2UnitStrc* pItem)
{
	if (!pItem) return nullptr;

	static int32_t transmogStatIndex = -1;
	static std::once_flag initFlag;
	static int g_MaxTransmogApply = 2;
	static std::unordered_map<DWORD, int> s_TransmogAppliedCount;
	static std::mutex s_TransmogMutex;

	// Limit transmog applications only while player is in game
	if (IsPlayerInGame()) {
		std::lock_guard<std::mutex> lock(s_TransmogMutex);
		int count = s_TransmogAppliedCount[pItem->dwUnitId];
		if (count < g_MaxTransmogApply) {
			s_TransmogAppliedCount[pItem->dwUnitId] = count + 1;
		}
		else {
			// Already reached max applications, return last used path
			std::lock_guard<std::mutex> lock2(g_LastTransmogPathMutex);
			auto it = g_LastTransmogPathByUnitId.find(pItem->dwUnitId);
			if (it != g_LastTransmogPathByUnitId.end())
				return it->second;
		}
	}

	// Initialize transmog stat index and table
	std::call_once(initFlag, [&]() {
		std::string statFile = std::format("{}/Mods/{}/{}.mpq/data/global/excel/itemstatcost.txt", GetExeDir(), ModName, ModName);
		std::ifstream file(statFile);
		if (!file.is_open()) return;

		std::string line;
		std::vector<std::string> headers;
		if (std::getline(file, line)) {
			std::stringstream ss(line);
			std::string col;
			while (std::getline(ss, col, '\t')) headers.push_back(col);
		}

		int statColIndex = -1;
		for (size_t i = 0; i < headers.size(); ++i) {
			if (_stricmp(headers[i].c_str(), "Stat") == 0) {
				statColIndex = static_cast<int>(i);
				break;
			}
		}
		if (statColIndex == -1) return;

		int rowIndex = 0;
		while (std::getline(file, line)) {
			std::stringstream ss(line);
			std::string col;
			int colIndex = 0;
			std::string statName;
			while (std::getline(ss, col, '\t')) {
				if (colIndex == statColIndex) { statName = col; break; }
				++colIndex;
			}
			if (_stricmp(statName.c_str(), "transmog") == 0) {
				transmogStatIndex = rowIndex;
				break;
			}
			++rowIndex;
		}

		std::string transmogFile = std::format("{}/Mods/{}/{}.mpq/data/global/excel/transmog_table.txt", GetExeDir(), ModName, ModName);
		std::ifstream tableFile(transmogFile);
		if (!tableFile.is_open()) return;

		std::getline(tableFile, line); // skip header
		while (std::getline(tableFile, line)) {
			std::stringstream ss(line);
			std::string col;
			TransmogRow row;
			int colIndex = 0;

			while (std::getline(ss, col, '\t')) {
				switch (colIndex) {
				case 0: row.index = std::stoi(col); break;
				case 1: row.code_name = col; break;
				case 2: row.base_path = col; break;
				case 3: row.sets_normal = col; break;
				case 4: row.sets_exceptional = col; break;
				case 5: row.sets_elite = col; break;
				case 6: row.uniques_normal = col; break;
				case 7: row.uniques_exceptional = col; break;
				case 8: row.uniques_elite = col; break;
				}
				++colIndex;
			}
			g_TransmogTable.push_back(row);
		}
		});

	if (transmogStatIndex == -1) return nullptr;
	int32_t transmogValue = STATLIST_GetUnitStatSigned(pItem, transmogStatIndex, 0);
	{
		std::lock_guard<std::mutex> lock(g_TransmogValueMutex);
		g_TransmogValueByUnitId[pItem->dwUnitId] = transmogValue;
	}
	if (transmogValue <= 0 || transmogValue > static_cast<int32_t>(g_TransmogTable.size())) return nullptr;
	const TransmogRow& row = g_TransmogTable[transmogValue - 1];
	int rarity = GetRarity(pItem);
	const std::string* pathToUse = nullptr;

	if (!row.base_path.empty())
		pathToUse = &row.base_path;

	else if (!row.sets_normal.empty()) {
		switch (rarity) {
		case 0: pathToUse = &row.sets_normal; break;
		case 1: pathToUse = &row.sets_exceptional; break;
		case 2: pathToUse = &row.sets_elite; break;
		default: pathToUse = &row.sets_normal; break;
		}
	}
	else if (!row.uniques_normal.empty()) {
		switch (rarity) {
		case 0: pathToUse = &row.uniques_normal; break;
		case 1: pathToUse = &row.uniques_exceptional; break;
		case 2: pathToUse = &row.uniques_elite; break;
		default: pathToUse = &row.uniques_normal; break;
		}
	}

	if (!pathToUse) return nullptr;

	return pathToUse->c_str();
}

const char* GetForcedItemcodeOverride(D2UnitStrc* pItem)
{
	if (!pItem)
		return nullptr;

	static int g_MaxOverrideApply = 2;
	static std::unordered_map<DWORD, int> s_OverrideAppliedCount;
	static std::unordered_map<DWORD, const char*> s_LastOverrideCache;
	static std::mutex s_OverrideMutex;

	{
		std::lock_guard<std::mutex> lock(s_OverrideMutex);

		// If we've reached the max applications in-game, return last cached override
		if (IsPlayerInGame()) {
			int count = s_OverrideAppliedCount[pItem->dwUnitId];
			if (count >= g_MaxOverrideApply) {
				auto it = s_LastOverrideCache.find(pItem->dwUnitId);
				if (it != s_LastOverrideCache.end())
					return it->second;
				else
					return nullptr; // no previous override, fallback
			}
			s_OverrideAppliedCount[pItem->dwUnitId] = count + 1;
		}
	}

	// --- Extract first 3–4 chars of the item code ---
	char fixedCode[5] = {};
	strncpy(fixedCode, sgptDataTables->pItemsTxt[pItem->dwClassId].szCode, 4);
	const char* code = fixedCode;

	if (code[0] == '\0') {
		std::cout << "[Override] EMPTY code\n";
		return nullptr;
	}

	// --- Scan transmog table for matching code_name ---
	const char* overridePath = nullptr;
	for (auto& row : g_TransmogTable)
	{
		if (_stricmp(row.code_name.c_str(), code) == 0)
		{
			std::cout << "[Override Match] code=" << row.code_name << " base=" << row.base_path << "\n";

			if (!row.base_path.empty())
				overridePath = row.base_path.c_str();
			else if (!row.sets_normal.empty())
				overridePath = row.sets_normal.c_str();
			else if (!row.uniques_normal.empty())
				overridePath = row.uniques_normal.c_str();

			break;
		}
	}

	if (!overridePath)
		return nullptr;

	// --- Cache the last applied override per item ---
	if (IsPlayerInGame()) {
		std::lock_guard<std::mutex> lock(s_OverrideMutex);
		s_LastOverrideCache[pItem->dwUnitId] = overridePath;
	}

	return overridePath;
}

bool LoadItemAssets(const std::string& filename)
{
	std::ifstream file(filename);
	if (!file.is_open()) return false;

	json j;
	try { file >> j; }
	catch (...) { return false; }

	for (auto& entry : j) {
		if (!entry.is_object() || entry.size() != 1) continue;
		auto it = entry.begin();
		std::string code = it.key();
		json value = it.value();

		ItemRow row;
		row.code_name = code;

		if (value.contains("asset"))
		{
			std::string asset = value["asset"].get<std::string>();

			row.base = ItemAssets(asset);

			// For 4-letter itemcodes, Override visual json
			if (code.length() == 4)
				row.override_asset = asset;
		}
		else if (value.contains("normal") || value.contains("uber") || value.contains("ultra")) {
			std::string normal = value.value("normal", "");
			std::string uber = value.value("uber", "");
			std::string ultra = value.value("ultra", "");

			if (filename.find("sets.json") != std::string::npos) {
				row.sets = ItemAssets(normal, uber, ultra);
			}
			else if (filename.find("uniques.json") != std::string::npos) {
				row.uniques = ItemAssets(normal, uber, ultra);
			}
		}

		g_ItemRows.push_back(row);
	}

	return true;
}

void ExportItemAssetsToTxt(const std::string& outputFile)
{
	// Initialize CRC32 table if needed
	static bool crcInit = false;
	if (!crcInit) { InitCRC32Table(); crcInit = true; }

	// Build new content in memory
	std::string newContent;
	newContent += "index\tcode_name\tcode_normal\tsets_normal\tsets_exceptional\tsets_elite\tuniques_normal\tuniques_exceptional\tuniques_elite\n";

	int index = 1;
	for (auto& row : g_ItemRows) {
		newContent += std::to_string(index) + "\t"
			+ row.code_name + "\t"
			+ row.base.normal + "\t"
			+ row.sets.normal + "\t" + row.sets.uber + "\t" + row.sets.ultra + "\t"
			+ row.uniques.normal + "\t" + row.uniques.uber + "\t" + row.uniques.ultra
			+ "\n";
		++index;
	}

	// Check existing file
	std::string oldContent;
	if (ReadFileToString(outputFile, oldContent)) {
		uint32_t oldCRC = ComputeCRC32(oldContent);
		uint32_t newCRC = ComputeCRC32(newContent);

		if (oldCRC == newCRC) {
			std::cout << "[INFO] File unchanged, not writing: " << outputFile << "\n";
			return;
		}
	}

	// Write new content
	std::ofstream out(outputFile, std::ios::binary | std::ios::trunc);
	if (!out.is_open()) return;

	out.write(newContent.data(), newContent.size());
	out.close();

	std::cout << "[INFO] Exported " << index - 1 << " rows to " << outputFile << "\n";
}

void GetItemVisuals()
{
	// Load Bases
	LoadItemAssets(std::format("{}/Mods/{}/{}.mpq/data/hd/items/items.json", GetExeDir(), ModName, ModName));

	// Load Sets/Uniques
	LoadItemAssets(std::format("{}/Mods/{}/{}.mpq/data/hd/items/sets.json", GetExeDir(), ModName, ModName));
	LoadItemAssets(std::format("{}/Mods/{}/{}.mpq/data/hd/items/uniques.json", GetExeDir(), ModName, ModName));

	// Generate Transmog Table
	ExportItemAssetsToTxt(std::format("{}/Mods/{}/{}.mpq/data/global/excel/transmog_table.txt", GetExeDir(), ModName, ModName));

}

#pragma endregion

#pragma region Hooks

blz__string16* __fastcall Hooked_UNITS_GetInvGfxFromJson(blz__string16* a1, D2UnitStrc* pUnit)
{
	auto ret = UNITS_GetInvGfxFromJson(a1, pUnit);

	if (!pUnit || pUnit->dwUnitType != 4 || !ret || !ret->pData)
		return ret;

	if (!cachedSettings.ExtendedItemcodes)
		return ret;

	char* buffer = ret->pData;
	static int g_MaxOverrideApply = 2;
	static std::unordered_map<DWORD, int> s_InvOverrideAppliedCount;
	static std::unordered_map<DWORD, std::string> s_InvLastOverrideCache;
	static std::mutex s_InvMutex;
	std::string finalPath;
	{
		std::lock_guard<std::mutex> lock(s_InvMutex);

		// Return last applied override if in-game and limit reached
		if (IsPlayerInGame()) {
			int count = s_InvOverrideAppliedCount[pUnit->dwUnitId];
			if (count >= g_MaxOverrideApply) {
				auto it = s_InvLastOverrideCache.find(pUnit->dwUnitId);
				if (it != s_InvLastOverrideCache.end())
					finalPath = it->second;
			}
			else
				s_InvOverrideAppliedCount[pUnit->dwUnitId] = count + 1;
		}
	}

	if (finalPath.empty()) {
		const char* forced = GetForcedItemcodeOverride(pUnit);
		if (forced) {
			std::cout << "[Forced Override - INV] " << forced << "\n";

			if (_strnicmp(forced, "items/", 6) == 0) {
				finalPath = forced;
			}
			else {
				finalPath = "items/weapon/";
				finalPath += forced;
			}

			// Cache the override if in-game
			if (IsPlayerInGame()) {
				std::lock_guard<std::mutex> lock(s_InvMutex);
				s_InvLastOverrideCache[pUnit->dwUnitId] = finalPath;
			}
		}
	}

	if (!finalPath.empty()) {
		size_t len = finalPath.length();
		strncpy(buffer, finalPath.c_str(), len + 1);
		buffer[len] = 0;
		ret->nLength = static_cast<uint32_t>(len);
		return ret;
	}

	// (disabled) Transmog override
	/*
	const char* transmog = Hooked_ITEMS_GetTransmog(pItem);
	if (transmog && transmog[0] != 0)
	{
		std::string finalPath = "items/weapon/";
		finalPath += transmog;

		std::cout << "[Transmog Override - INV] " << finalPath << "\n";

		size_t len = finalPath.length();
		strncpy(buffer, finalPath.c_str(), len + 1);
		buffer[len] = 0;
		ret->nLength = (uint32_t)len;
		return ret;
	}
	*/

	return ret;
}

const char* __fastcall Hooked_UNITS_GetItemGfxFromJson(void* a1, D2UnitStrc* pUnit, int32_t a3, uint32_t a4)
{
	auto original = UNITS_GetItemGfxFromJson(a1, pUnit, a3, a4);
	if (!pUnit || pUnit->dwUnitType != 4 || !original)
		return original;

	// Check for forced override from items.json (4-letter code)
	if (cachedSettings.ExtendedItemcodes)
	{
		const char* forced = GetForcedItemcodeOverride(pUnit);
		if (forced)
		{
			std::cout << "[Forced Override] Item: " << pUnit->dwClassId << " → " << forced << "\n";
			return forced;
		}
	}
	
	// Check for transmog
	if (cachedSettings.TransmogVisuals)
	{
		const char* transmogPath = GetTransmog(pUnit);
		if (transmogPath && transmogPath[0] != 0)
		{
			std::cout << "[Transmog] Using: " << transmogPath << "\n";
			return transmogPath;
		}
	}
	

	return original;
}

#pragma endregion

#pragma endregion

#pragma region Color Dye System

static auto (*gpPaletteDataTable)[9][21][256] = reinterpret_cast<uint8_t(*)[9][21][256]>(Pattern::Address(0x1ddf7c0));
static auto ITEMS_GetColor = reinterpret_cast<uint8_t * (__fastcall*)(D2UnitStrc * pPlayer, D2UnitStrc * pItem, int32_t * pColorIndex, int nTransType)>(Pattern::Address(0x1FEF30));

uint8_t* Hooked_ITEMS_GetColor(D2UnitStrc* pPlayer, D2UnitStrc* pItem, int32_t* pColorIndex, int nTransType)
{
	if (cachedSettings.TransmogVisuals || cachedSettings.ExtendedItemcodes)
		GetTransmog(pItem);

	auto result = ITEMS_GetColor(pPlayer, pItem, pColorIndex, nTransType);
	if (!pItem)
		return result;

	static int32_t colorDyeStatIndex = -1;
	static std::once_flag colorDyeInitFlag;

	std::call_once(colorDyeInitFlag, [&]() {
		std::string basePath = std::format("{}/Mods/{}/{}.mpq/data/global/excel/", GetExeDir(), ModName, ModName);
		std::string statFile = basePath + "itemstatcost.txt";
		std::ifstream file(statFile);
		if (!file.is_open()) return;

		std::string line;
		std::vector<std::string> headers;
		if (std::getline(file, line)) {
			std::stringstream ss(line);
			std::string col;
			while (std::getline(ss, col, '\t')) headers.push_back(col);
		}

		int statColIndex = -1;
		for (size_t i = 0; i < headers.size(); ++i) {
			if (_stricmp(headers[i].c_str(), "Stat") == 0) {
				statColIndex = static_cast<int>(i);
				break;
			}
		}
		if (statColIndex == -1) return;

		int rowIndex = 0;
		while (std::getline(file, line)) {
			std::stringstream ss(line);
			std::string col;
			int colIndex = 0;
			std::string statName;
			while (std::getline(ss, col, '\t')) {
				if (colIndex == statColIndex) { statName = col; break; }
				++colIndex;
			}
			if (_stricmp(statName.c_str(), "color_dye") == 0) {
				colorDyeStatIndex = rowIndex;
				break;
			}
			++rowIndex;
		}
		});

	if (colorDyeStatIndex == -1) return result;

	int32_t nColor = STATLIST_GetUnitStatSigned(pItem, colorDyeStatIndex, 0) - 1;
	if (nColor > 0 && nColor < 22) {
		return (*gpPaletteDataTable)[5][nColor];
	}

	return result;
}

#pragma endregion

#pragma region Filter Controls

bool ItemFilter::Install(MonsterStatsDisplaySettings settings) {
	cachedSettings = settings;
	if (!bInstalled) {
		DetourTransactionBegin();
		DetourUpdateThread(GetCurrentThread());
		
		DetourAttach(&(PVOID&)oD2CLIENT_PACKETCALLBACK_Rcv0x9C_SCMD_ITEMEX, Hooked_D2CLIENT_PACKETCALLBACK_Rcv0x9C_SCMD_ITEMEX);
		DetourAttach(&(PVOID&)oD2CLIENT_PACKETCALLBACK_Rcv0x9D_SCMD_ITEMUNITEX, Hooked_D2CLIENT_PACKETCALLBACK_Rcv0x9D_SCMD_ITEMUNITEX);

		// Expand D2UnitStrc to add the item filter data to it
		DetourAttach(&(PVOID&)oUNITS_AllocUnit, Hooked_UNITS_AllocUnit);
		DetourAttach(&(PVOID&)oUNITS_FreeUnit, Hooked_UNITS_FreeUnit);

		// Stuff we want to do w/ the filter
		DetourAttach(&(PVOID&)oITEMS_GetName, Hooked_ITEMS_GetName);
		DetourAttach(&(PVOID&)oUI_DrawGroundItemBackground, Hooked_UI_DrawGroundItemBackground);
		DetourAttach(&(PVOID&)oUI_BuildGroundItemTooltip, Hooked_UI_BuildGroundItemTooltip);
		DetourAttach(&(PVOID&)oTooltipsPanel_DrawTooltip, Hooked_TooltipsPanel_DrawTooltip);
		DetourAttach(&(PVOID&)oITEMS_GetStatsDescription, Hooked_ITEMS_GetStatsDescription);

		DetourAttach(&(PVOID&)ITEMS_GetColor, Hooked_ITEMS_GetColor);

		if (cachedSettings.TransmogVisuals || cachedSettings.ExtendedItemcodes)
		{
			GetItemVisuals();
			DetourAttach(&(PVOID&)UNITS_GetInvGfxFromJson, Hooked_UNITS_GetInvGfxFromJson);
			DetourAttach(&(PVOID&)UNITS_GetItemGfxFromJson, Hooked_UNITS_GetItemGfxFromJson);
		}

		DetourUpdateThread(GetCurrentThread());

		
		
		DWORD oldProtect = 0;
		auto UnitSize = (int32_t*)Pattern::Address(0x2086ca);
		VirtualProtect(UnitSize, sizeof(int32_t), PAGE_EXECUTE_READWRITE, &oldProtect);
		*UnitSize = sizeof(D2UnitStrcCustom);
		VirtualProtect(UnitSize, sizeof(int32_t), oldProtect, &oldProtect);
		sgptDataTables->tUnitMemoryPool.nStructSize = sizeof(D2UnitStrcCustom);


		DetourTransactionCommit();
		InitializeLUA();

		bInstalled = true;
	}
	return bInstalled;
}

void ItemFilter::ReloadGameFilter()
{
	LoadScript();

	std::string message = "Loot Filter Reloaded";

	if (getWelcomeMessage.valid()) {
		sol::protected_function_result result = getWelcomeMessage();
		if (result.valid()) {
			sol::object obj = result;
			if (obj.is<std::string>()) {
				std::string welcome = obj.as<std::string>();
				if (!welcome.empty()) message = welcome;
			}
		}
	}

	PrintGameMessage(message.c_str());

	auto ppClientItem = &ppClientUnitList[UNIT_ITEM * 0x80];
	for (int i = 0; i < 128; i++) {
		auto pItem = ppClientItem[i];
		while (pItem) {
			DoFilter(pItem);
			pItem = pItem->pListNext;
		}
	}

	bool inGame = IsPlayerInGame();

	std::cout << "INGAME: " << inGame;

	if (!inGame) {
		oDATATBLS_UnloadAllBins();
		oDATATBLS_LoadAllTxts();
		
		g_ItemFilterStatusMessage = ".TXT Files have been reloaded!";
		g_ShouldShowItemFilterMessage = true;
		g_ItemFilterMessageStartTime = std::chrono::steady_clock::now();
	}
	else {
		g_ShouldShowItemFilterMessage = false;
		//oDATATBLS_LoadAllJson();
	}
}

void ItemFilter::CycleFilter()
{
	std::string filename = "lootfilter_config.lua";

	std::ifstream inFile(filename);
	if (!inFile.is_open()) {
		MessageBoxA(nullptr, "Failed to open item filter file.", "Error", MB_OK | MB_ICONERROR);
		return;
	}

	std::ostringstream buffer;
	std::string line;
	int currentLevel = 1;
	bool foundLevel = false;
	bool foundTitles = false;
	std::vector<std::string> filterTitles;

	// Read the file to detect current filter_level and titles
	while (std::getline(inFile, line)) {
		// Detect filter_level
		if (line.find("filter_level =") != std::string::npos) {
			foundLevel = true;
			size_t eqPos = line.find('=');
			if (eqPos != std::string::npos) {
				std::string value = line.substr(eqPos + 1);
				value.erase(0, value.find_first_not_of(" \t"));
				value.erase(value.find_last_not_of(" \t,") + 1);
				try { currentLevel = std::stoi(value); }
				catch (...) { currentLevel = 1; }
			}
		}

		// Detect filter_titles
		// --- Updated filter_titles parsing ---
		if (line.find("filter_titles =") != std::string::npos) {
			foundTitles = true;
			size_t braceStart = line.find('{');
			size_t braceEnd = line.rfind('}');
			if (braceStart != std::string::npos && braceEnd != std::string::npos && braceEnd > braceStart) {
				std::string titlesStr = line.substr(braceStart + 1, braceEnd - braceStart - 1);
				filterTitles.clear();

				bool inQuotes = false;
				std::string currentTitle;

				for (char c : titlesStr) {
					if (c == '"') {
						inQuotes = !inQuotes;
					}
					else if (c == ',' && !inQuotes) {
						currentTitle.erase(0, currentTitle.find_first_not_of(" \t"));
						currentTitle.erase(currentTitle.find_last_not_of(" \t") + 1);
						if (!currentTitle.empty())
							filterTitles.push_back(ReplaceColorCodes(currentTitle));
						currentTitle.clear();
					}
					else {
						currentTitle += c;
					}
				}

				// Add last title
				currentTitle.erase(0, currentTitle.find_first_not_of(" \t"));
				currentTitle.erase(currentTitle.find_last_not_of(" \t") + 1);
				if (!currentTitle.empty())
					filterTitles.push_back(ReplaceColorCodes(currentTitle));
			}
		}

		buffer << line << "\n";
	}

	inFile.close();

	// If no filter_level or filter_titles were found, do nothing
	if (!foundLevel || !foundTitles || filterTitles.empty()) {
		return;
	}

	// Adjust currentLevel based on number of titles
	int maxLevels = static_cast<int>(filterTitles.size());
	currentLevel = (currentLevel % maxLevels) + 1;

	// Rewrite file with updated level
	std::string fileContent = buffer.str();
	size_t pos = fileContent.find("filter_level =");
	if (pos != std::string::npos) {
		size_t endLine = fileContent.find('\n', pos);
		std::string newLine = "filter_level = " + std::to_string(currentLevel) + ",";
		fileContent.replace(pos, endLine - pos, newLine);
	}

	std::ofstream outFile(filename, std::ios::trunc);
	if (!outFile.is_open()) {
		MessageBoxA(nullptr, "Failed to write item filter file.", "Error", MB_OK | MB_ICONERROR);
		return;
	}
	outFile << fileContent;
	outFile.close();

	// Build message using filter level description
	std::string description = "Unknown";
	if (currentLevel >= 1 && currentLevel <= maxLevels)
		description = filterTitles[currentLevel - 1];

	std::string message = "ÿcNFilter Level Applied: ÿc4" + description;
	PrintGameMessage(message.c_str());

	// Reload the updated script and apply the filter immediately
	LoadScript();

	auto ppClientItem = &ppClientUnitList[UNIT_ITEM * 0x80];
	for (int i = 0; i < 128; i++) {
		auto pItem = ppClientItem[i];
		while (pItem) {
			DoFilter(pItem);
			pItem = pItem->pListNext;
		}
	}
}

#pragma endregion
