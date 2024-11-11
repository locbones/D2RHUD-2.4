#pragma once
#include <Windows.h>
#include <cstdint>
#include "Logging.h"

// https://www.unknowncheats.me/forum/general-programming-and-reversing/502738-ida-style-pattern-scanner.html
#define FUNC_DEF(ret, conv, name, args) \
	typedef ret conv name##_t##args##; \
	extern __declspec(selectany) name##_t* name##;

#define FUNC_PATTERN(name, mod, signature) \
	name = reinterpret_cast<decltype(##name##)>(Pattern::Scan(mod, signature)); \
	LOG("Found " #name " at {:#08x}", reinterpret_cast<uint64_t>(name) - Pattern::BaseAddress(mod));

#define FUNC_PATTERNREF(name, mod, signature, opCodeByteOffset) \
	name = reinterpret_cast<decltype(##name##)>(Pattern::ScanRef(mod, signature, opCodeByteOffset)); \
	LOG("Found " #name " at {:#08x}", reinterpret_cast<uint64_t>(name) - Pattern::BaseAddress(mod));

#define VAR_DEF(type, name) \
	extern __declspec(selectany) type* name##;

#define VAR_PATTERN(name, mod, signature) \
	name = reinterpret_cast<decltype(##name##)>(Pattern::Scan(mod, signature)); \
	LOG("Found " #name " at {:#08x}", reinterpret_cast<uint64_t>(name) - Pattern::BaseAddress(mod));

#define VAR_PATTERNREF(name, mod, signature, opCodeByteOffset) \
	name = reinterpret_cast<decltype(##name##)>(Pattern::ScanRef(mod, signature, opCodeByteOffset)); \
	LOG("Found " #name " at {:#08x}", reinterpret_cast<uint64_t>(name) - Pattern::BaseAddress(mod));

/*
Sample Usage:
Ptrs.h

#include "../Pattern.h"

FUNC_DEF(int64_t, __fastcall, STATLIST_GetUnitStatSigned, (D2UnitStrc* pUnit, uint32_t nStatId, uint16_t nLayer));
FUNC_DEF(D2UnitStrc*, __fastcall, UNITS_GetByIdAndType, (uint32_t nUnitId, uint32_t nUnitType));
VAR_DEF(D2MouseHoverStruct, MouseHover);

class Ptrs {
public:
	static void Initialize() {
		FUNC_PATTERN(STATLIST_GetUnitStatSigned, NULL, "48 83 EC 28 45 0F B7 C8 48 85 C9 74 42 48 8B 89 ? ? ? ? 48 85 C9 74 2F");
		FUNC_PATTERNREF(UNITS_GetByIdAndType, NULL, "E8 ? ? ? ? 8B 4F 05", 1);
		VAR_PATTERNREF(MouseHover, NULL, "48 8D 3D ? ? ? ? BB ? ? ? ? 48 8B CF E8 ? ? ? ? 48 83 C7 10 ", 3);
	}
};
*/

class Pattern
{
public:
	static DWORD64 BaseAddress(const wchar_t* szModule);
	static DWORD64 Address(uint32_t offset);
	static DWORD64 Scan(const wchar_t* szModule, const char* signature);
	/*
	Scans for a pattern that is address is referenced in an opcode. i.e. `call sub_7FF7615FD0D0`, direct reference: [actual address in first opcode] E8 ? ? ? ? 8B 4F 05
	*/
	static DWORD64 ScanRef(const wchar_t* szModule, const char* signature, int32_t nOpCodeByteOffset = 0);
};

