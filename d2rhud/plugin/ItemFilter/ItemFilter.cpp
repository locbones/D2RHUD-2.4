#define SOL_ALL_SAFETIES_ON 1

#include "ItemFilter.h"
#include <detours/detours.h>
#include <sol/sol.hpp>
#include <filesystem>
#include <set>

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

D2UnitStrc* GetUnitByIdAndType(D2UnitStrc** ppUnitsList, uint32_t nUnitId, D2C_UnitTypes nUnitType) {
	auto ppUnitList = &ppUnitsList[nUnitType * 0x80];
	auto pHashEntry = ppUnitList[nUnitId & 0x7F];
	while (pHashEntry && pHashEntry->dwUnitId != nUnitId) {
		pHashEntry = pHashEntry->pListNext;
	}
	return pHashEntry;
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


void __fastcall Hooked_ITEMS_GetName(D2UnitStrc* pUnit, char* pBuffer) {
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
		char cBuffer[0x400];
		snprintf(cBuffer, 0x400, pUnitCustom->pFilterResult->szName.c_str(), pBuffer);
		if (cachedSettings.socketDisplay) {
			int32_t nSockets = STATLIST_GetUnitStatSigned(pUnitToUse, STAT_ITEM_NUMSOCKETS, 0);
			if (nSockets > 0) {
				snprintf(cBuffer, 0x400, "%s ÿcN(%d)\0", cBuffer, nSockets);
			}
		}
		strncpy(pBuffer, cBuffer, 0x400);
	}
}

void RegisterD2ItemFilterResultStrc(sol::state& s) {
	auto type = s.new_usertype<D2ItemFilterResultStrc>("D2ItemFilterResultStrc", sol::no_constructor,
		"Hide", &D2ItemFilterResultStrc::bHide,
		"Name", &D2ItemFilterResultStrc::szName
	);
}

int32_t STATLIST_GetUnitStatSignedLayer0(D2UnitStrc* pThat, uint32_t nStatId) {
	return STATLIST_GetUnitStatSigned(pThat, nStatId, 0);
}

class D2ItemUnitStrc : public D2UnitStrc {};
class D2PlayerUnitStrc : public D2UnitStrc {};

void RegisterD2UnitStrc(sol::state& s) {
	auto unitType = s.new_usertype<D2UnitStrc>("D2UnitStrc", sol::no_constructor,
		"Address", sol::readonly_property([](D2UnitStrc* pThat) -> std::string { std::stringstream ss; ss << std::hex << pThat; return ss.str(); }),
		"ID", &D2UnitStrc::dwUnitId,
		"Mode", &D2UnitStrc::dwAnimMode
	);
	unitType["Stat"] = sol::overload(
		sol::resolve<int32_t(D2UnitStrc*, uint32_t)>(STATLIST_GetUnitStatSignedLayer0),
		sol::resolve<int32_t(D2UnitStrc*, uint32_t, uint16_t)>(STATLIST_GetUnitStatSigned)
	);

	auto playerType = s.new_usertype<D2PlayerUnitStrc>("D2PlayerUnitStrc", sol::no_constructor,
		"Class", &D2UnitStrc::dwClassId,
		"Name", sol::readonly_property([](D2UnitStrc* pThat) -> const char* { return pThat->pPlayerData->szName; }),
		"Data", sol::readonly_property([](D2UnitStrc* pThat) -> D2PlayerDataStrc* { return pThat->pPlayerData; }),
		"Difficulty", sol::readonly_property([](D2UnitStrc* pThat) -> int32_t { return pThat->pDynamicPath && pThat->pDynamicPath->pRoom && pThat->pDynamicPath->pRoom->pDrlgRoom && pThat->pDynamicPath->pRoom->pDrlgRoom->pLevel && pThat->pDynamicPath->pRoom->pDrlgRoom->pLevel->pDrlg ? pThat->pDynamicPath->pRoom->pDrlgRoom->pLevel->pDrlg->nDifficulty : -1; }),
		sol::base_classes, sol::bases<D2UnitStrc>()
	);

	auto itemType = s.new_usertype<D2ItemUnitStrc>("D2ItemUnitStrc", sol::no_constructor,
		"Name", sol::readonly_property([](D2UnitStrc* pThat) -> const char* {
			char* pBuffer = new char[0x400];
			Hooked_ITEMS_GetName(pThat, pBuffer);
			return pBuffer;
		}),
		"Data", sol::readonly_property([](D2UnitStrc* pThat) -> D2ItemDataStrc* { return pThat->pItemData; }),
		"Txt", sol::readonly_property([](D2UnitStrc* pThat) -> D2ItemsTxt { return sgptDataTables->pItemsTxt[pThat->dwClassId]; }),
		"IsOnGround", sol::readonly_property([](D2UnitStrc* pThat) -> bool { return pThat->dwAnimMode == IMODE_ONGROUND; }),
		"IsEquipped", sol::readonly_property([](D2UnitStrc* pThat) -> bool { return pThat->dwAnimMode == IMODE_EQUIP; }),
		"Rarity", sol::readonly_property([](D2UnitStrc* pThat) -> int32_t {
			switch (pThat->dwUnitType) {
			case UNIT_ITEM:
			{
				auto pItemTxt = sgptDataTables->pItemsTxt[pThat->dwClassId];
				if (strncmp(pItemTxt.szCode, pItemTxt.dwUltraCode, 4) == 0) {
					return 2;
				}
				else if (strncmp(pItemTxt.szCode, pItemTxt.dwUberCode, 4) == 0) {
					return 1;
				}
				else if (strncmp(pItemTxt.szCode, pItemTxt.dwNormCode, 4) == 0) {
					return 0;
				}
				return -1;
			}
			default: return -1;
			}
		}),
		"Area", sol::readonly_property([](D2UnitStrc* pThat) -> int32_t { return pThat->pStaticPath && pThat->pStaticPath->pRoom && pThat->pStaticPath->pRoom->pDrlgRoom && pThat->pStaticPath->pRoom->pDrlgRoom->pLevel ? pThat->pStaticPath->pRoom->pDrlgRoom->pLevel->nLevelId : 0; }),
		sol::base_classes, sol::bases<D2UnitStrc>()
	);
	itemType["IsType"] = oITEMS_CheckItemTypeId;
}

int32_t ReadLocationFromExternalProcess(HANDLE hProcess)
{
	constexpr uintptr_t LOCATION_ADDR = 0x1C7538C;
	int32_t value = 0;
	SIZE_T bytesRead = 0;

	if (ReadProcessMemory(hProcess, reinterpret_cast<LPCVOID>(LOCATION_ADDR), &value, sizeof(value), &bytesRead) && bytesRead == sizeof(value))
	{
		return value;
	}

	// Return a default or error value if failed
	return -1;
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
		"IsEthereal", sol::readonly_property([](D2ItemDataStrc* pThat) -> bool { return (pThat->dwItemFlags & IFLAG_ETHEREAL) != 0; })
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
	s.set_function("SSet", [](sol::variadic_args args) { std::set<std::string> l; for (auto&& arg : args) {	l.insert(arg.as<std::string>()); } return l; });
	s.set_function("ISet", [](sol::variadic_args args) { std::set<std::int32_t> l; for (auto&& arg : args) { l.insert(arg.as<std::int32_t>()); } return l; });
}

bool HandleError(sol::protected_function_result result) {
	if (!result.valid()) {
		sol::error err = result;
		PrintGameMessage(err.what());
		return true;
	}
	return false;
}

void LoadScript() {
	applyFilter = sol::nil;
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
	sol::set_environment(env, applyFilter);
}

void InitializeLUA() {
	lua.open_libraries(sol::lib::base, sol::lib::package,
		sol::lib::table, sol::lib::coroutine, sol::lib::string,
		sol::lib::math);
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

bool ItemFilter::OnKeyPressed(short key) {
	if (key == 'R' && (GetKeyState(VK_CONTROL) & 0x8000)) {
		PrintGameMessage("Loot Filter Reloaded");
		LoadScript();
		auto ppClientItem = &ppClientUnitList[UNIT_ITEM * 0x80];
		for (int i = 0; i < 128; i++) {
			auto pItem = ppClientItem[i];
			while (pItem) {
				DoFilter(pItem);
				pItem = pItem->pListNext;
			}
		}
		return true;
	}
	return false;
}
