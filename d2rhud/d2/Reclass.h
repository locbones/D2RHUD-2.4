#pragma once

#include <cstdint>

#pragma pack(push, 1)
// Created with ReClass.NET 1.2 by KN4CK3R

class D2SeedStrc {
public:
	union // offset 0x0000
	{
		int32_t dwSeed[2]; // 0x0000: dwSeed[0] = LowSeed, dwSeed[1] = HighSeed
		int64_t lSeed;     // 0x0000: full 64-bit combined seed
	};
	int32_t& LowSeed() {
		return dwSeed[0];
	}
	int32_t& HighSeed() {
		return dwSeed[1];
	}
	const int32_t& LowSeed() const {
		return dwSeed[0];
	}

	const int32_t& HighSeed() const {
		return dwSeed[1];
	}
};
static_assert(sizeof(D2SeedStrc) == 0x8);

class D2UnitStrc // [<D2R.exe> + 0x1d442e0 + (0x0 * 0x400) + (0x1 * 0x8)]
{
public:
	uint32_t dwUnitType; //0x0000
	uint32_t dwClassId; //0x0004
	uint32_t dwUnitId; //0x0008
	union //0x000C
	{
		uint32_t dwAnimMode; //0x0000
		uint32_t dwItemMode; //0x0000
		uint32_t dwMissileMode; //0x0000
	};
	union //0x0010
	{
		class D2PlayerDataStrc* pPlayerData; //0x0000
		class D2ItemDataStrc* pItemData; //0x0000
		class D2MonsterDataStrc* pMonsterData; //0x0000
		class D2ObjectDataStrc* pObjectData; //0x0000
		class D2MissileDataStrc* pMissileData; //0x0000
	};
	uint8_t nAct; //0x0018
	char pad_0019[7]; //0x0019
	int64_t* pDrlgAct; //0x0020
	class D2SeedStrc pSeed; //0x0028
	char pad_0030[8]; //0x0030
	union //0x0038
	{
		class D2DynamicPathStrc* pDynamicPath; //0x0000
		class D2StaticPathStrc* pStaticPath; //0x0000
	};
	char pad_0040[72]; //0x0040
	class D2StatListExStrc* pStatListEx; //0x0088
	class D2InventoryStrc* pInventory; //0x0090
	char pad_0098[140]; //0x0098
	uint32_t dwFlags; //0x0124
	uint32_t dwFlagsEx; //0x0128
	char pad_012C[36]; //0x012C
	class D2UnitStrc* pListNext; //0x0150
	class D2UnitStrc* pRoomNext; //0x0158
	char pad_0160[24]; //0x0160
	uint32_t dwSizeX; //0x0178
	uint32_t dwSizeY; //0x017C
	char pad_0180[56]; //0x0180
}; //Size: 0x01B8
static_assert(sizeof(D2UnitStrc) == 0x1B8);

class D2GameStrc {
public:
	char pad_0000[257]; //0x0000
	uint8_t nGameType; //0x0101
	uint8_t nHostLevel; //0x0102
	uint8_t nAllowedLevelDifference; //0x0103
	uint8_t nDifficulty; //0x0104
	char pad_0105[3]; //0x0105
	uint32_t bExpansion; //0x0108
	uint32_t dwGameType; //0x010C
	char pad_0110[48]; //0x0110
	uint32_t dwGameVersion; //0x0140
	int32_t dwInitSeed; //0x0144
	int32_t dwObjSeed; //0x0148
	char pad_014C[4]; //0x014C
	class D2ClientStrc* pClientList; //0x0150
	uint32_t nClients; //0x0158
	uint32_t dwLastUsedUnitGUID[6]; //0x015C
	char pad_0174[8]; //0x0174
	uint32_t dwGameFrame; //0x017C
	char pad_0180[96]; //0x0180
	class D2MonsterRegionStrc* pMonReg[1024]; //0x01E0
	char pad_21E0[88]; //0x21E0
	class D2UnitStrc* pUnitList[5][128]; //0x2238
	char pad_3638[592]; //0x3638
}; //Size: 0x3888
static_assert(sizeof(D2GameStrc) == 0x3888);

class D2PlayerDataStrc {
public:
	char szName[16]; //0x0000
	char pad_0010[48]; //0x0010
	class D2BitBufferStrc* pQuestData[3];
	char pad_0050[32]; //0x0010
	uint32_t nPortalFlags; //0x0078
	char pad_007C[116]; //0x007C
	class D2ClientStrc* pClient; //0x00F0
	char pad_00F8[236]; //0x00F8
	uint32_t dwGameFrame; //0x01E4
	char pad_01E8[152]; //0x01E8
}; //Size: 0x0280
static_assert(sizeof(D2PlayerDataStrc) == 0x280);

class D2ClientStrc {
public:
	uint32_t dwClientId; //0x0000
	uint32_t dwClientState; //0x0004
	char pad_0008[36]; //0x0008
	char szName[16]; //0x002C
	char pad_003C[336]; //0x003C
	uint32_t dwUnitType; //0x018C
	uint32_t dwUnitGUID; //0x0190
	char pad_0194[4]; //0x0194
	class D2UnitStrc* pPlayer; //0x0198
	class D2SaveHeadersStrc* pSaveHeaders; //0x01A0
	uint64_t nSaveHeaderSize; //0x01A8
	char pad_01B0[128]; //0x01B0
	class D2GameStrc* pGame; //0x0230
	char pad_0238[552]; //0x0238
	uint32_t dwFlags; //0x0460
	char pad_0464[244]; //0x0464
	class D2ClientStrc* pNext; //0x0558
	char pad_0560[2856]; //0x0560
}; //Size: 0x1088
static_assert(sizeof(D2ClientStrc) == 0x1088);

class D2ItemExtraDataStrc {
public:
	class D2InventoryStrc* pParentInv; //0x0000
	class D2UnitStrc* pPreviousItem; //0x0008
	class D2UnitStrc* pNextItem; //0x0010
	char pad_0018[96]; //0x0018
}; //Size: 0x0078
static_assert(sizeof(D2ItemExtraDataStrc) == 0x78);

class D2ItemDataStrc {
public:
	uint32_t dwQualityNo; //0x0000
	class D2SeedStrc pSeed; //0x0004
	uint32_t dwOwnerGUID; //0x000C
	uint32_t dwInitSeed; //0x0010
	uint32_t dwCommandFlags; //0x0014
	uint32_t dwItemFlags; //0x0018
	char pad_001C[24]; //0x001C
	int32_t dwFileIndex; //0x0034
	char pad_0038[10]; //0x0038
	uint16_t wRarePrefix; //0x0042
	uint16_t wRareSuffix; //0x0044
	char pad_0046[2]; //0x0046
	uint16_t wMagicPrefix[3]; //0x0048
	uint16_t wMagicSuffix[3]; //0x004E
	uint8_t nBodyLoc; //0x0054
	uint8_t nPage; //0x0055
	char pad_0056[8]; //0x0056
	uint8_t nInvGfxIdx; //0x005E
	char pad_005F[65]; //0x005F
	class D2ItemExtraDataStrc pExtraData; //0x00A0
}; //Size: 0x0118
static_assert(sizeof(D2ItemDataStrc) == 0x118);

class D2MonsterDataStrc {
public:
	class D2MonStatsTxt* pMonstatsTxt; //0x0000
	//uint8_t nComponent[16]; //0x0008
	char pad_0018[136]; //0x0018
}; //Size: 0x0090
static_assert(sizeof(D2MonsterDataStrc) == 0x90);

class D2ObjectDataStrc {
public:
	char pad_0000[136]; //0x0000
}; //Size: 0x0088
static_assert(sizeof(D2ObjectDataStrc) == 0x88);

class D2MissileDataStrc {
public:
	char pad_0000[136]; //0x0000
}; //Size: 0x0088
static_assert(sizeof(D2MissileDataStrc) == 0x88);

class D2CoordStrc {
public:
	uint32_t X; //0x0000
	uint32_t Y; //0x0004
}; //Size: 0x0008
static_assert(sizeof(D2CoordStrc) == 0x8);

class D2PathPointStrc {
public:
	uint16_t X; //0x0000
	uint16_t Y; //0x0002
}; //Size: 0x0004
static_assert(sizeof(D2PathPointStrc) == 0x4);

class D2DynamicPathStrc {
public:
	uint16_t wOffsetX; //0x0000
	uint16_t wPosX; //0x0002
	uint16_t wOffsetY; //0x0004
	uint16_t wPosY; //0x0006
	class D2CoordStrc tUnkCoord; //0x0008
	class D2PathPointStrc tTargetCoord; //0x0010
	class D2PathPointStrc tPrevTargetCoord; //0x0014
	class D2PathPointStrc tFinalTargetCoord; //0x0018
	char pad_001C[4]; //0x001C
	class D2ActiveRoomStrc* pRoom; //0x0020
	class D2ActiveRoomStrc* pPreviousRoom; //0x0028
	char pad_0030[16]; //0x0030
	class D2UnitStrc* pUnit; //0x0040
	char pad_0048[296]; //0x0048
}; //Size: 0x0170
static_assert(sizeof(D2DynamicPathStrc) == 0x170);

class D2StaticPathStrc {
public:
	class D2ActiveRoomStrc* pRoom; //0x0000
	class D2CoordStrc tUnkCoord; //0x0008
	class D2CoordStrc tGameCoord; //0x0010
	char pad_0018[112]; //0x0018
}; //Size: 0x0088
static_assert(sizeof(D2StaticPathStrc) == 0x88);

class D2Widget {
public:
	char pad_0000[184]; //0x0000
	uint8_t bVisible; //0x00B8
	char pad_00B9[191]; //0x00B9
}; //Size: 0x0178
static_assert(sizeof(D2Widget) == 0x178);

class D2PanelManager {
public:
	char pad_0008[112]; //0x0008

	virtual void Init();
}; //Size: 0x0078
static_assert(sizeof(D2PanelManager) == 0x78);

class D2MemoryPoolStrc {
public:
	char pad_0000[8]; //0x0000
	int32_t nStructSize; //0x0008
	char pad_000C[148]; //0x000C
}; //Size: 0x00A0
static_assert(sizeof(D2MemoryPoolStrc) == 0xA0);

class D2DataTablesStrc {
public:
	class D2PlayerClassTxt* pPlayerClassTxt; //0x0000
	char pad_0008[3824]; //0x0008
	class D2MonStatsTxt* pMonStatsTxt; //0x0EF8
	int64_t nMonStatsTxtRecordCount; //0x0F00
	char pad_0F08[48]; //0x0F08
	class D2MonStats2Txt* pMonStats2Txt; //0x0F38
	int64_t nMonStats2TxtRecordCount; //0x0F40
	char pad_0F48[408]; //0x0F48
	class D2MonLvlTxt* pMonLvlTxt; //0x10E0
	int64_t nMonLvlTxtRecordCount; //0x10E8
	char pad_10F0[96]; //0x10F0
	class D2SkillsTxt* pSkillsTxt; //0x1150
	int64_t nSkillsTxtRecordCount; //0x1158
	char pad_1160[152]; //0x1160
	class D2ItemStatCostTxt* pItemStatCostTxt; //0x11F8
	int64_t nItemStatCostTxtRecordCount; //0x1200
	char pad_1208[40]; //0x1208
	class D2MonEquipTxt* pMonEquipTxt; //0x1230
	int64_t nMonEquipTxtRecordId; //0x1238
	char pad_1240[208]; //0x1240
	class D2MonPropTxt* pMonPropTxt; //0x1310
	int64_t nMonPropTxtRecordCount; //0x1318
	char pad_1320[104]; //0x1320
	class D2LevelsTxt* pLevelsTxt; //0x1388
	int64_t nLevelsTxtRecordCount; //0x1390
	char pad_1398[1656]; //0x1398
	class D2ItemsTxt* pItemsTxt; //0x1A10
	int64_t nItemsTxtRecordCount; //0x1A18
	char pad_1A20[8]; //0x1A20
	class D2ItemsTxt* pWeapons; //0x1A28
	class D2ItemsTxt* pArmor; //0x1A30
	class D2ItemsTxt* pMisc; //0x1A38
	char pad_1A40[528]; //0x1A40
	class D2MemoryPoolStrc tUnitMemoryPool; //0x1C50
	char pad_1CF0[560]; //0x1CF0
}; //Size: 0x1F20
static_assert(sizeof(D2DataTablesStrc) == 0x1F20);

class D2PlayerClassTxt {
public:
	char pad_0000[136]; //0x0000
}; //Size: 0x0088
static_assert(sizeof(D2PlayerClassTxt) == 0x88);

class D2ActiveRoomStrc {
public:
	char pad_0000[24]; //0x0000
	class D2DrlgRoomStrc* pDrlgRoom; //0x0018
	char pad_0020[232]; //0x0020
}; //Size: 0x0108
static_assert(sizeof(D2ActiveRoomStrc) == 0x108);

class D2StatListExStrc // 0xB58
{
public:
	char pad_0000[2904]; //0x0000
}; //Size: 0x0B58
static_assert(sizeof(D2StatListExStrc) == 0xB58);

class D2InventoryStrc {
public:
	uint32_t dwSignature; //0x0000
	char pad_0004[4]; //0x0004
	class D2UnitStrc* pOwner; //0x0008
	class D2UnitStrc* pFirstItem; //0x0010
	class D2UnitStrc* pLastItem; //0x0018
	class D2InventoryGridStrc* pGrids; //0x0020
	char pad_0028[64]; //0x0028
	class D2SharedInventoryStrc* pFirstSharedInv; //0x0068
	class D2SharedInventoryStrc* pLastSharedInv; //0x0070
	uint32_t nSharedInvCount; //0x0078
}; //Size: 0x007C
static_assert(sizeof(D2InventoryStrc) == 0x7C);

class D2InventoryGridWidget {
public:
	char pad_0000[1420]; //0x0000
	uint32_t dwUnitId; //0x058C
	uint32_t dwUnitType; //0x0590
	char pad_0594[4]; //0x0594
	class D2UnitStrc* pGridOwner; //0x0598
	char pad_05A0[88]; //0x05A0
	uint8_t dwInventoryPage; //0x05F8
	char pad_05F9[2695]; //0x05F9
}; //Size: 0x1080
static_assert(sizeof(D2InventoryGridWidget) == 0x1080);

class D2InventoryGridStrc {
public:
	char pad_0000[136]; //0x0000
}; //Size: 0x0088
static_assert(sizeof(D2InventoryGridStrc) == 0x88);

class D2SharedInventoryStrc {
public:
	uint32_t dwUnitId; //0x0000
	char pad_0004[12]; //0x0004
	class D2SharedInventoryStrc* pNextCorpse; //0x0010
}; //Size: 0x0018
static_assert(sizeof(D2SharedInventoryStrc) == 0x18);

class D2TabBarWidget {
public:
	char pad_0000[2176]; //0x0000
}; //Size: 0x0880
static_assert(sizeof(D2TabBarWidget) == 0x880);

class D2SaveHeadersStrc {
public:
	class D2SaveHeaderStrc* pSaveHeader; //0x0000
	uint64_t dwSize; //0x0008
	char pad_0010[1144]; //0x0010
}; //Size: 0x0488
static_assert(sizeof(D2SaveHeadersStrc) == 0x488);

class D2SaveHeaderStrc {
public:
	uint32_t dwHeaderMagic; //0x0000
	uint32_t dwVersion; //0x0004
	uint32_t dwSize; //0x0008
	uint32_t dwChecksum; //0x000C
	char pad_0010[120]; //0x0010
}; //Size: 0x0088
static_assert(sizeof(D2SaveHeaderStrc) == 0x88);

class D2MonStatsTxt {
public:
	uint16_t nId; //0x0000
	char pad_0002[58]; //0x0002
	union //0x003C
	{
		uint8_t nMonStatsFlags[4]; //0x0000
		uint32_t dwMonStatsFlags; //0x0000
	};
	char pad_0040[8]; //0x0040
	uint16_t wMonStatsEx; //0x0048
	char pad_004A[112];
	uint16_t wTreasureClass[3][4];//0x004A
	char pad_004B[12];
	uint16_t nLevel[3]; //0x00DE
	char pad_00E4[248]; //0x00E4
}; //Size: 0x01DC
static_assert(sizeof(D2MonStatsTxt) == 0x1DC);

class D2MonStats2Txt {
public:
	uint32_t dwId; //0x0000
	char pad_0004[292]; //0x0004
}; //Size: 0x0128
static_assert(sizeof(D2MonStats2Txt) == 0x128);

class D2ItemStatCostTxt {
public:
	uint16_t wStatId; //0x0000
	char pad_0002[322]; //0x0002
}; //Size: 0x0144
static_assert(sizeof(D2ItemStatCostTxt) == 0x144);

class D2LevelsTxt // 388
{
public:
	char pad_0000[26]; //0x0000
	uint16_t wMonLvlEx[3]; //0x001A
	char pad_0020[356]; //0x0020
}; //Size: 0x0184
static_assert(sizeof(D2LevelsTxt) == 0x184);

class D2MonPropTxt {
public:
	char pad_0000[136]; //0x0000
}; //Size: 0x0088
static_assert(sizeof(D2MonPropTxt) == 0x88);

class D2SkillsTxt {
public:
	char pad_0000[136]; //0x0000
}; //Size: 0x0088
static_assert(sizeof(D2SkillsTxt) == 0x88);

class D2MonEquipTxt {
public:
	char pad_0000[136]; //0x0000
}; //Size: 0x0088
static_assert(sizeof(D2MonEquipTxt) == 0x88);

class D2ItemsTxt // 436
{
public:
	char szFlippyFile[32]; //0x0000
	char szInvFile[32]; //0x0020
	char szUniqueInvFile[32]; //0x0040
	char szSetInvFile[32]; //0x0060
	char szCode[4]; //0x0080
	char dwNormCode[4]; //0x0084
	char dwUberCode[4]; //0x0088
	char dwUltraCode[4]; //0x008C
	char dwAlternateGfx[4]; //0x0090
	int32_t dwPspell; //0x0094
	char pad_0098[40]; //0x0098
	uint32_t dwSpellDescCalc; //0x00C0
	char dwBetterGem[4]; //0x00C4
	char dwWeapClass[4]; //0x00C8
	char dwWeapClass2Hand[4]; //0x00CC
	char dwTransmogrifyType[4]; //0x00D0
	int32_t dwMinAc; //0x00D4
	int32_t dwMaxAc; //0x00D8
	uint32_t dwGambleCost; //0x00DC
	uint32_t dwSpeed; //0x00E0
	uint32_t dwBitField1; //0x00E4
	int32_t dwCost; //0x00E8
	uint32_t dwMinStack; //0x00EC
	uint32_t dwMaxStack; //0x00F0
	uint32_t dwSpawnStack; //0x00F4
	uint32_t dwGemOffset; //0x00F8
	uint16_t wNameStr; //0x00FC
	uint16_t wVersion; //0x00FE
	uint16_t wAutoPrefix; //0x0100
	uint16_t wMissileType; //0x0102
	uint8_t nRarity; //0x0104
	uint8_t nLevel; //0x0105
	char pad_0106[1]; //0x0106
	uint8_t nMinDam; //0x0107
	uint8_t nMaxDam; //0x0108
	char pad_0109[5]; //0x0109
	int16_t nStrBonus; //0x010E
	int16_t nDexBonus; //0x0110
	uint16_t wReqStr; //0x0112
	uint16_t wReqDex; //0x0114
	uint8_t nInvWidth; //0x0116
	uint8_t nInvHeight; //0x0117
	char pad_0118[1]; //0x0118
	int8_t nDurability; //0x0119
	char pad_011A[12]; //0x011A
	int16_t wType[2]; //0x0126
	char pad_012A[4]; //0x012A
	uint16_t wUseSound; //0x012E
	uint8_t nDropSfxFrame; //0x0130
	int8_t nUnique; //0x0131
	int8_t nQuest; //0x0132
	int8_t nQuestDiffCheck; //0x0133
	int8_t nTransparent; //0x0134
	int8_t nTransTbl; //0x0135
	char pad_0136[7]; //0x0136
	uint8_t nDurWarning; //0x013D
	uint8_t nQuantityWarning; //0x013E
	uint8_t nHasInv; //0x013F
	char pad_0140[7]; //0x0140
	uint8_t nLevelReq; //0x0147
	char pad_0148[92]; //0x0148
	char dwNightmareUpgrade[4]; //0x01A4
	char dwHellUpgrade[4]; //0x01A8
	char pad_01AC[8]; //0x01AC
}; //Size: 0x01B4
static_assert(sizeof(D2ItemsTxt) == 0x1B4);

class D2ItemDropStrc {
public:
	char pad_0000[16]; //0x0000
	class D2GameStrc* pGame; //0x0010
	char pad_0018[8]; //0x0018
	uint32_t nId; //0x0020
	uint32_t nSpawnType; //0x0024
	uint32_t nX; //0x0028
	uint32_t nY; //0x002C
	class D2ActiveRoomStrc* pRoom; //0x0030
	uint16_t wUnitInitFlags; //0x0038
	char pad_003A[94]; //0x003A
}; //Size: 0x0098
static_assert(sizeof(D2ItemDropStrc) == 0x98);

class D2PlayerCountBonusStrc {
public:
	int32_t nHP; //0x0000
	int32_t nExperience; //0x0004
	int32_t nMonsterSkillBonus; //0x0008
	int32_t nDifficulty; //0x000C
	int32_t nPlayerCount; //0x0010
}; //Size: 0x0014
static_assert(sizeof(D2PlayerCountBonusStrc) == 0x14);

class D2MonStatsInitStrc {
public:
	int32_t nMinHP; //0x0000
	int32_t nMaxHP; //0x0004
	int32_t nAC; //0x0008
	int32_t nTH; //0x000C
	int32_t nExp; //0x0010
	int32_t nA1MinD; //0x0014
	int32_t nA1MaxD; //0x0018
	int32_t nA2MinD; //0x001C
	int32_t nA2MaxD; //0x0020
	int32_t nS1MinD; //0x0024
	int32_t nS1MaxD; //0x0028
	int32_t nElMinD; //0x002C
	int32_t nElMaxD; //0x0030
	int32_t nElDur; //0x0034
}; //Size: 0x0038
static_assert(sizeof(D2MonStatsInitStrc) == 0x38);

class D2DrlgRoomStrc {
public:
	char pad_0000[144]; //0x0000
	class D2DrlgLevelStrc* pLevel; //0x0090
	char pad_0098[240]; //0x0098
}; //Size: 0x0188
static_assert(sizeof(D2DrlgRoomStrc) == 0x188);

class D2DrlgLevelStrc {
public:
	char pad_0000[456]; //0x0000
	class D2DrlgStrc* pDrlg; //0x01C8
	char pad_01D0[40]; //0x01D0
	uint32_t nLevelId; //0x01F8
	char pad_01FC[140]; //0x01FC
}; //Size: 0x0288
static_assert(sizeof(D2DrlgLevelStrc) == 0x288);

class D2MonLvlTxt {
public:
	char pad_0000[120]; //0x0000
}; //Size: 0x0078
static_assert(sizeof(D2MonLvlTxt) == 0x78);

class D2DamageStrc {
public:
	int32_t dwHitFlags; //0x0000
	int16_t wResultFlags; //0x0004
	int16_t wExtra; //0x0006
	char pad_0008[20]; //0x0008
	int32_t dwPhysDamage; //0x001C
	int32_t dwEnDmgPct; //0x0020
	int32_t dwFireDamage; //0x0024
	int32_t dwBurnDamage; //0x0028
	char pad_002C[4]; //0x002C
	int32_t dwLtngDamage; //0x0030
	int32_t dwMagDamage; //0x0034
	int32_t dwColdDamage; //0x0038
	int32_t dwPoisDamage; //0x003C
	int32_t dwPoisLen; //0x0040
	char pad_0044[220]; //0x0044
	int32_t dwColdLen; //0x0120
	int32_t dwBurnLen; //0x0124
	int32_t dwLifeLeech; //0x0128
	int32_t dwManaLeech; //0x012C
	int32_t dwStamLeech; //0x0130
	int32_t dwStunLen; //0x0134
	int32_t dwAbsLife; //0x0138
	int32_t dwDmgTotal; //0x013C
	char pad_0140[4]; //0x0140
	int32_t dwPiercePct; //0x0144
	int32_t dwDamageRate; //0x0148
	char pad_014C[4]; //0x014C
	int32_t dwHitClass; //0x0150
	uint8_t nHitClassActiveSet; //0x0154
	char pad_0155[3]; //0x0155
	int32_t dwConvPct; //0x0158
	int32_t nOverlay; //0x015C
}; //Size: 0x0160
static_assert(sizeof(D2DamageStrc) == 0x160);

class D2CombatStrc {
public:
	class D2GameStrc* pGame; //0x0000
	uint32_t dwAttackerType; //0x0008
	int32_t dwAttackerId; //0x000C
	int32_t dwDefenderType; //0x0010
	int32_t dwDefenderId; //0x0014
	char pad_0018[256]; //0x0018
}; //Size: 0x0118
static_assert(sizeof(D2CombatStrc) == 0x118);

class D2DamageInfoStrc {
public:
	class D2GameStrc* pGame; //0x0000
	class D2DifficultyLevelsTxt* pDifficultyLevelsTxt; //0x0008
	class D2UnitStrc* pAttacker; //0x0010
	class D2UnitStrc* pDefender; //0x0018
	int32_t bAttackerIsMonster; //0x0020
	int32_t bDefenderIsMonster; //0x0024
	class D2DamageStrc* pDamage; //0x0028
	int32_t nDamageReduction[4]; //0x0030
}; //Size: 0x0040
static_assert(sizeof(D2DamageInfoStrc) == 0x40);

class D2DamageStatTableStrc {
public:
	int32_t* pOffsetInDamageStrc; //0x0000
	int32_t nResStatId; //0x0008
	int32_t nMaxResStatId; //0x000C
	int32_t nPierceStatId; //0x0010
	int32_t nAbsorbPctStatId; //0x0014
	int32_t nAbsorbStatId; //0x0018
	int32_t nDamageReductionType; //0x001C
	char pad_0020[8]; //0x0020
	char* szName; //0x0028
	uint8_t nShiftDamage; //0x0030
}; //Size: 0x0031
static_assert(sizeof(D2DamageStatTableStrc) == 0x31);

class D2DifficultyLevelsTxt {
public:
	char pad_0000[136]; //0x0000
}; //Size: 0x0088
static_assert(sizeof(D2DifficultyLevelsTxt) == 0x88);

class D2MonsterRegionStrc {
public:
	char pad_0000[136]; //0x0000
}; //Size: 0x0088
static_assert(sizeof(D2MonsterRegionStrc) == 0x88);

class D2DrlgStrc {
public:
	char pad_0000[2096]; //0x0000
	uint8_t nDifficulty; //0x0830
	char pad_0831[1367]; //0x0831
}; //Size: 0x0D88
static_assert(sizeof(D2DrlgStrc) == 0xD88);

class D2BufferStrc {
public:
	uint8_t* pBuffer; //0x0000
	int64_t nMaxSize; //0x0008
	int64_t nUnk0x10; //0x0010
	int64_t nUnk0x18; //0x0018
}; //Size: 0x0020
static_assert(sizeof(D2BufferStrc) == 0x20);

class D2UnitRectStrc {
public:
	int32_t nX; //0x0000
	int32_t nY; //0x0004
	int32_t nW; //0x0008
	int32_t nH; //0x000C
	uint32_t dwUnitId; //0x0010
}; //Size: 0x0014
static_assert(sizeof(D2UnitRectStrc) == 0x14);
#pragma pack(pop)