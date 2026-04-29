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
	static DWORD64 ScanProcess(const char* signature);

	static uintptr_t BaseAddress_SS() {
		static uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
		return base;
	}
	static uintptr_t Address_SS(uint32_t rva) {
		return BaseAddress_SS() + rva;
	}
};

