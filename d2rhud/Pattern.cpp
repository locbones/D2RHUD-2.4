#include "Pattern.h"
#include "PatternScan.h"
#include "ProcessPatternScan.h"
#include <Psapi.h>

// The main image lives for the process lifetime. Named modules are queried by
// value so missing/unloaded modules never leave stale pointer-keyed cache entries.
static MODULEINFO GetModuleInfo(const wchar_t* name) {
    const auto query = [](const wchar_t* moduleName) {
        MODULEINFO info{};
        const auto module = GetModuleHandleW(moduleName);
        if (!module || !GetModuleInformation(GetCurrentProcess(), module, &info, sizeof(info))) return MODULEINFO{};
        return info;
    };
    if (name) return query(name);
    static const MODULEINFO mainImage = query(nullptr);
    return mainImage;
}

DWORD64 Pattern::BaseAddress(const wchar_t* szModule)
{
	auto lpmInfo = GetModuleInfo(szModule);
	return (DWORD64)lpmInfo.lpBaseOfDll;
}

DWORD64 Pattern::Address(uint32_t offset)
{
	return BaseAddress(nullptr) + offset;
}

DWORD64 Pattern::Scan(const wchar_t* szModule, const char* signature)
{
    if (!signature) return 0;
    const auto module = GetModuleInfo(szModule);
    if (!module.lpBaseOfDll) return 0;
    const auto bytes = static_cast<const uint8_t*>(module.lpBaseOfDll);
    const auto match = PatternScan::Find(bytes, module.SizeOfImage, PatternScan::Compile(signature));
    return match == PatternScan::NotFound ? 0 : reinterpret_cast<DWORD64>(bytes) + match;
}

DWORD64 Pattern::ScanRef(const wchar_t* szModule, const char* signature, int32_t nOpCodeByteOffset)
{
    if (!signature) return 0;
    const auto module = GetModuleInfo(szModule);
    if (!module.lpBaseOfDll) return 0;
    const auto bytes = static_cast<const uint8_t*>(module.lpBaseOfDll);
    const auto match = PatternScan::Find(bytes, module.SizeOfImage, PatternScan::Compile(signature));
    return PatternScan::ResolveReference(bytes, module.SizeOfImage, reinterpret_cast<uintptr_t>(bytes), match, nOpCodeByteOffset);
}

// Camera scans use checked reads and the same compiled matcher as image scans.
DWORD64 Pattern::ScanProcess(const char* signature)
{
    return signature ? ProcessPatternScan::FindProcess(signature) : 0;
}
