#pragma once
#include "D2Enums.h"
#include "Reclass.h"
#include <cstdint>

struct D2Client {
	uint32_t dwClientId; //0x0000
	uint32_t dwClientState; //0x0004
	char pad_0008[36]; //0x0008
	char szName[16]; //0x002C
	char pad_003C[336]; //0x003C
	uint32_t dwUnitType; //0x018C
	uint32_t dwUnitGUID; //0x0190
	char pad_0194[4]; //0x0194
	uint64_t pPlayer;
	uint8_t unknown2[0x90];
	uint64_t pGame;
	char pad_0238[552]; //0x0238
	uint32_t dwFlags; //0x0460
	char pad_0464[244]; //0x0464
	D2Client* pNext; //0x0558
};

template<size_t T>
struct small_string_opt {
	const char* str;
	size_t length;
	size_t alloc;
	char data[T];
};

struct blz_string {
	const char* str;
	size_t length;
	size_t alloc;
	char data[16];
};


#pragma pack(1)
struct D2MouseHoverStruct {
	uint8_t IsHovered;
	uint8_t PlayerMoving; //??
	uint8_t Unk0x2;
	uint8_t Unk0x3;
	D2C_UnitTypes HoveredUnitType;
	uint32_t HoveredUnitId;
};

#pragma pack()

constexpr uint64_t fnv1a_64(const char* s, size_t count) {
	return ((count ? fnv1a_64(s, count - 1) : 14695981039346656037u) ^ s[count]) * 1099511628211u;
}

constexpr uint64_t operator"" _hash64(const char* s, size_t count) {
	return fnv1a_64(s, count - 1);
}