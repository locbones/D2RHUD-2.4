#pragma once
#include "D2Enums.h"
#include <cstdint>

#pragma pack(1)
struct D2MouseHoverStruct {
	uint8_t IsHovered;
	uint8_t PlayerMoving; //??
	uint8_t Unk0x2;
	uint8_t Unk0x3;
	D2C_UnitTypes HoveredUnitType;
	uint32_t HoveredUnitId;
};

struct D2UnitStrc;

struct D2GameStrc;
#pragma pack()