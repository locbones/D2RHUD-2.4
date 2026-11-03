#pragma once
#include "D2Structs.h"
#include "../Pattern.h"

FUNC_DEF(int32_t, __fastcall, STATLIST_GetUnitStatSigned, (D2UnitStrc* pUnit, uint32_t nStatId, uint16_t nLayer));


FUNC_DEF(D2UnitStrc*, __fastcall, UNITS_GetClientUnitByIdAndType, (uint32_t nUnitId, D2C_UnitTypes nUnitType));
FUNC_DEF(D2UnitStrc*, __fastcall, UNITS_GetServerUnitByTypeAndId, (D2GameStrc* pGame, D2C_UnitTypes nUnitType, uint32_t nUnitId));

FUNC_DEF(void, __fastcall, ITEMS_Description, (D2UnitStrc* pItem, char* pBuffer, uint64_t nBufferSize, int a4, int a5, int a6, unsigned int a7, int a8));

FUNC_DEF(__int64, __fastcall, INVENTORY_AutoSort, (uint32_t pUnitID, D2C_ItemInvPage page));
FUNC_DEF(void, __fastcall, D2CLIENT_Transmute, ());
FUNC_DEF(void, __fastcall, CLIENT_RevealAutoMapRoom, ());
FUNC_DEF(void, __fastcall, CLIENT_playerCommand, (std::string name, std::string name2));
FUNC_DEF(void, __fastcall, CLIENT_DebugExit, (int name2));

VAR_DEF(D2MouseHoverStruct, gMouseHover);
VAR_DEF(D2GameStrc*, gCurrentSinglePlayerGame);

namespace D2Ptrs {
    // All verified and working on
    // 1.4.71510 
    static void Initialize() {
        FUNC_PATTERN(STATLIST_GetUnitStatSigned, NULL, "48 83 EC 28 45 0F B7 C8 48 85 C9 74 42 48 8B 89 ? ? ? ? 48 85 C9 74 2F");

        FUNC_PATTERNREF(UNITS_GetClientUnitByIdAndType, NULL, "E8 ? ? ? ? 8B 4F 05", 1);
        FUNC_PATTERNREF(UNITS_GetServerUnitByTypeAndId, NULL, "E8 ? ? ? ? 46 8D 04 37 ", 1);
        FUNC_PATTERNREF(ITEMS_Description, NULL, "E8 ? ? ? ? 8B 8C 24 ? ? ? ? 48 FF C7", 1);

        FUNC_PATTERN(INVENTORY_AutoSort, NULL, "40 53 56 41 55 B8 ? ? ? ?");
        FUNC_PATTERNREF(D2CLIENT_Transmute, NULL, "E8 ? ? ? ? EB 41 48 8B 03", 1);
        FUNC_PATTERN(CLIENT_RevealAutoMapRoom, NULL, "48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 30 E8 ? ? ? ? 33 FF");

        VAR_PATTERNREF(gMouseHover, NULL, "48 8D 3D ? ? ? ? BB ? ? ? ? 48 8B CF E8 ? ? ? ? 48 83 C7 10 ", 3);
        //VAR_PATTERNREF(gCurrentSinglePlayerGame, NULL, "48 89 05 ? ? ? ? 48 85 C0 0F 84 ? ? ? ? 44 8B C7 ", 3);
        VAR_PATTERNREF(gCurrentSinglePlayerGame, NULL, "48 8B 15 ? ? ? ? 33 C9 48 8D 42 01 48 83 F8 01 48 0F 47 CA", 3);

        VAR_PATTERNREF(CLIENT_playerCommand, NULL, "E8 E6 A6 0A 00", 1);

        VAR_PATTERNREF(CLIENT_DebugExit, NULL, "E8 ?? ?? ?? ?? CC 45 33 DB", 1)
    }
};