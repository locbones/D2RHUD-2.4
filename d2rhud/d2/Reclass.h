// Created with ReClass.NET 1.2 by KN4CK3R
#pragma once

#include <cstdint>

#pragma pack(push, 1)
// Created with ReClass.NET 1.2 by KN4CK3R

class D2SeedStrc {
public:
	uint32_t nLowSeed; //0x0000
	uint32_t nHighSeed; //0x0004
}; //Size: 0x0008
static_assert(sizeof(D2SeedStrc) == 0x8);

class D2UnitStrc {
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
	class D2DrlgActStrc* pDrlgAct; //0x0020
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
	char pad_0098[144]; //0x0098
	uint32_t dwFlagEx; //0x0128
	char pad_012C[36]; //0x012C
	class D2UnitStrc* pListNext; //0x0150
	class D2UnitStrc* pRoomNext; //0x0158
	char pad_0160[24]; //0x0160
	uint32_t dwSizeX; //0x0178
	uint32_t dwSizeY; //0x017C
	char pad_0180[216]; //0x0180
}; //Size: 0x0258
static_assert(sizeof(D2UnitStrc) == 0x258);

class D2GameStrc {
public:
	char pad_0000[336]; //0x0000
	class D2ClientStrc* pClientList; //0x0150
	uint32_t nClients; //0x0158
	char pad_015C[32]; //0x015C
	uint32_t dwGameFrame; //0x017C
	char pad_0180[768]; //0x0180
}; //Size: 0x0480
static_assert(sizeof(D2GameStrc) == 0x480);

class D2PlayerDataStrc {
public:
	char pad_0000[120]; //0x0000
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
	char pad_0464[252]; //0x0464
	class D2ClientStrc* pNext; //0x0560
	char pad_0568[2856]; //0x0568
}; //Size: 0x1090
static_assert(sizeof(D2ClientStrc) == 0x1090);


class D2ItemDataStrc {
public:
	uint32_t dwQualityNo; //0x0000
	class D2SeedStrc pSeed; //0x0004
	char pad_000C[128]; //0x000C
}; //Size: 0x008C
static_assert(sizeof(D2ItemDataStrc) == 0x8C);

class D2MonsterDataStrc {
public:
	char pad_0000[136]; //0x0000
}; //Size: 0x0088
static_assert(sizeof(D2MonsterDataStrc) == 0x88);

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

class D2DataTablesStrc {
public:
	class D2PlayerClassTxt* pPlayerClassTxt; //0x0000
	char pad_0008[7288]; //0x0008
}; //Size: 0x1C80
static_assert(sizeof(D2DataTablesStrc) == 0x1C80);

class D2PlayerClassTxt {
public:
	char pad_0000[136]; //0x0000
}; //Size: 0x0088
static_assert(sizeof(D2PlayerClassTxt) == 0x88);

class D2ActiveRoomStrc {
public:
	char pad_0000[136]; //0x0000
}; //Size: 0x0088
static_assert(sizeof(D2ActiveRoomStrc) == 0x88);

class D2DrlgActStrc {
public:
	char pad_0000[136]; //0x0000
}; //Size: 0x0088
static_assert(sizeof(D2DrlgActStrc) == 0x88);

class D2StatListExStrc {
public:
	char pad_0000[136]; //0x0000
}; //Size: 0x0088
static_assert(sizeof(D2StatListExStrc) == 0x88);

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
#pragma pack(pop)