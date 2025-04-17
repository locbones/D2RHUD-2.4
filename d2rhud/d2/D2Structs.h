#pragma once
#include "D2Enums.h"
#include <cstdint>

struct D2Client {
	uint8_t unknown1[0x198];
	uint64_t pPlayer;
	uint8_t unknown2[0x90];
	uint64_t pGame;
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

struct D2UnitStrc {
	uint32_t dwUnitType; //0x0000
	uint32_t dwClassId; //0x0004
	uint32_t dwUnitId; //0x0008
};

struct D2GameStrc;
#pragma pack()

constexpr uint64_t fnv1a_64(const char* s, size_t count) {
	return ((count ? fnv1a_64(s, count - 1) : 14695981039346656037u) ^ s[count]) * 1099511628211u;
}

constexpr uint64_t operator"" _hash64(const char* s, size_t count) {
	return fnv1a_64(s, count - 1);
}