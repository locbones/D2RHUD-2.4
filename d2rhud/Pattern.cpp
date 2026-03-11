#include "Pattern.h"
#include <Psapi.h>
#include <vector>
#include <unordered_map>
#include <algorithm>

static HANDLE hProcess = GetCurrentProcess();
static std::unordered_map<const wchar_t*, LPMODULEINFO> mModuleInfoMap = {};

static auto pattern_to_byte = [](const char* pattern)
	{
		auto bytes = std::vector<char>{};
		auto start = const_cast<char*>(pattern);
		auto end = const_cast<char*>(pattern) + strlen(pattern);

		for (auto current = start; current < end; ++current)
		{
			if (*current == '?')
			{
				++current;
				if (*current == '?')
					++current;
				bytes.push_back('\?');
			}
			else
			{
				bytes.push_back(static_cast<uint8_t>(strtoul(current, &current, 16)));
			}
		}
		return bytes;
	};

static LPMODULEINFO GetModuleInfo(const wchar_t* szModule) {
	if (mModuleInfoMap[szModule]) {
		return mModuleInfoMap[szModule];
	}
	HMODULE hModule = GetModuleHandle(szModule);
	mModuleInfoMap[szModule] = new MODULEINFO();
	GetModuleInformation(hProcess, hModule, mModuleInfoMap[szModule], sizeof(MODULEINFO));
	return mModuleInfoMap[szModule];
}

DWORD64 Pattern::BaseAddress(const wchar_t* szModule)
{
	auto lpmInfo = GetModuleInfo(szModule);
	return (DWORD64)lpmInfo->lpBaseOfDll;
}

DWORD64 Pattern::Address(uint32_t offset)
{
	return BaseAddress(nullptr) + offset;
}

DWORD64 Pattern::Scan(const wchar_t* szModule, const char* signature)
{
	auto lpmInfo = GetModuleInfo(szModule);
	DWORD64 base = (DWORD64)lpmInfo->lpBaseOfDll;
	DWORD64 sizeOfImage = (DWORD64)lpmInfo->SizeOfImage;
	auto patternBytes = pattern_to_byte(signature);

	DWORD64 patternLength = patternBytes.size();
	auto data = patternBytes.data();

	for (DWORD64 i = 0; i < sizeOfImage - patternLength; i++)
	{
		bool found = true;
		for (DWORD64 j = 0; j < patternLength; j++)
		{
			char a = '\?';
			char b = *(char*)(base + i + j);
			found &= data[j] == a || data[j] == b;
		}
		if (found)
		{
			return base + i;
		}
	}
	return NULL;
}

DWORD64 Pattern::ScanRef(const wchar_t* szModule, const char* signature, int32_t nOpCodeByteOffset)
{
	auto lpmInfo = GetModuleInfo(szModule);
	DWORD64 base = (DWORD64)lpmInfo->lpBaseOfDll;
	DWORD64 sizeOfImage = (DWORD64)lpmInfo->SizeOfImage;
	auto patternBytes = pattern_to_byte(signature);

	DWORD64 patternLength = patternBytes.size();
	auto data = patternBytes.data();

	for (DWORD64 i = 0; i < sizeOfImage - patternLength; i++)
	{
		bool found = true;
		for (DWORD64 j = 0; j < patternLength; j++)
		{
			char a = '\?';
			char b = *(char*)(base + i + j);
			found &= data[j] == a || data[j] == b;
		}
		if (found)
		{
			//generally the size of what your looking for is a dword. relative addr to a func/variable.
			int32_t relativeAddress = *(int32_t*)(base + i + nOpCodeByteOffset);
			return base + i + relativeAddress + nOpCodeByteOffset + sizeof(int32_t);
		}
	}
	return NULL;
}

// Scan the full process virtual space for the given pattern.
// Uses chunked reads into a local buffer for cache-friendly scanning (much faster
// than byte-by-byte reads across the whole address space).
DWORD64 Pattern::ScanProcess(const char* signature)
{
	auto patternBytes = pattern_to_byte(signature);
	size_t patternLength = patternBytes.size();
	if (patternLength == 0)
		return 0;

	const char* data = patternBytes.data();

	SYSTEM_INFO sysInfo{};
	GetSystemInfo(&sysInfo);

	DWORD64 start = reinterpret_cast<DWORD64>(sysInfo.lpMinimumApplicationAddress);
	DWORD64 end = reinterpret_cast<DWORD64>(sysInfo.lpMaximumApplicationAddress);

	// Chunk size: read this much at a time for cache-friendly scanning.
	const size_t kChunkSize = 256 * 1024;
	std::vector<char> buffer(kChunkSize + patternLength - 1);

	MEMORY_BASIC_INFORMATION mbi;

	for (DWORD64 addr = start; addr < end; )
	{
		if (!VirtualQuery(reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi)))
			break;

		DWORD64 regionBase = reinterpret_cast<DWORD64>(mbi.BaseAddress);
		SIZE_T regionSize = mbi.RegionSize;

		bool readable =
			(mbi.State == MEM_COMMIT) &&
			!(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD));

		if (!readable || regionSize < patternLength)
		{
			addr = regionBase + regionSize;
			continue;
		}

		// Skip very large regions (camera data is in a small allocation; huge heaps are slow and unlikely).
		const SIZE_T kMaxRegionToScan = 64 * 1024 * 1024;
		if (regionSize > kMaxRegionToScan)
		{
			addr = regionBase + regionSize;
			continue;
		}

		// Scan region in chunks to avoid cache thrash and improve speed.
		size_t offsetInRegion = 0;
		size_t bufferValid = 0;

		while (offsetInRegion < regionSize)
		{
			size_t toRead = (std::min)(kChunkSize, static_cast<size_t>(regionSize - offsetInRegion));

			if (bufferValid > 0)
			{
				// Keep overlap at the end of buffer for patterns spanning chunks.
				size_t overlap = (bufferValid >= patternLength - 1) ? patternLength - 1 : bufferValid;
				memmove(buffer.data(), buffer.data() + bufferValid - overlap, overlap);
				bufferValid = overlap;
			}

			// Read next chunk from process memory (same process, so direct copy).
			char* dest = buffer.data() + bufferValid;
			memcpy(dest, reinterpret_cast<void*>(regionBase + offsetInRegion), toRead);
			bufferValid += toRead;
			offsetInRegion += toRead;

			size_t scanEnd = bufferValid - patternLength + 1;
			if (scanEnd > bufferValid)
				continue;

			for (size_t i = 0; i < scanEnd; ++i)
			{
				bool found = true;
				for (size_t j = 0; j < patternLength; ++j)
				{
					char a = '\?';
					char b = buffer[i + j];
					found &= (data[j] == a) || (data[j] == b);
					if (!found)
						break;
				}
				if (found)
				{
					DWORD64 result = regionBase + offsetInRegion - bufferValid + i;
					return result;
				}
			}
		}

		addr = regionBase + regionSize;
	}

	return 0;
}