#include <iostream>
#include <Windows.h>
#include <tlhelp32.h>
#include <vector>
#include <format>
#include <filesystem>
#include <wtsapi32.h>
#include <Psapi.h>

#pragma comment(lib, "Wtsapi32.lib")

std::vector<DWORD> GetPIDs(std::wstring processName);
std::wstring ExePath();
void EjectDLL(const int& pid, const std::wstring& path);
void InjectDLL(const int& pid, const std::wstring& path);

#define ORIGINAL_DLL_NAME L"d2rhud.dll"
#define RENAMED_DLL_NAME  L"d2rhudb.dll"

int main(int argc, char* argv[])
{
	if (argc != 2) {
		std::wcerr << L"Provide the process name as an argument or add it in Debug > Command Argument: ./d2rhud-injector.exe \"MyGame.exe\"" << std::endl;
		exit(-1);
	}
	wchar_t** wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
	std::wcout << L"[+]Looking for to inject into \"" << wargv[1] << "\"" << std::endl;
	std::vector<DWORD> pids = GetPIDs(wargv[1]);
	for (auto& pid : pids) {
		std::wstring originalDllPath = std::format(L"{}\\{}", ExePath(), ORIGINAL_DLL_NAME);
		std::wstring dllPath = std::format(L"{}\\{}", ExePath(), RENAMED_DLL_NAME);
		std::wcout << L"[+]Injecting into " << pid << std::endl;
		EjectDLL(pid, RENAMED_DLL_NAME);
		std::wcout << L"[+]Renaming DLL" << std::endl;
		std::filesystem::copy(originalDllPath, dllPath, std::filesystem::copy_options::update_existing);
		InjectDLL(pid, dllPath);
	}
	exit(0);
	//system("pause");
}

std::wstring ExePath() {
	TCHAR buffer[MAX_PATH] = { 0 };
	GetModuleFileName(NULL, buffer, MAX_PATH);
	std::wstring::size_type pos = std::wstring(buffer).find_last_of(L"\\/");
	return std::wstring(buffer).substr(0, pos);
}

std::vector<DWORD> GetPIDs(std::wstring processName) {
	std::vector<DWORD> pids;
	WTS_PROCESS_INFO* pWPIs = NULL;
	DWORD dwProcCount = 0;
	if (WTSEnumerateProcessesW(WTS_CURRENT_SERVER_HANDLE, NULL, 1, &pWPIs, &dwProcCount))
	{
		//Go through all processes retrieved
		for (DWORD i = 0; i < dwProcCount; i++)
		{
			if (!wcscmp(pWPIs[i].pProcessName, processName.c_str())) {
				pids.push_back(pWPIs[i].ProcessId);
			}
		}
	}

	//Free memory
	if (pWPIs)
	{
		WTSFreeMemory(pWPIs);
		pWPIs = NULL;
	}
	return pids;
}

void EjectDLL(const int& pid, const std::wstring& path) {
	HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);
	LPVOID dwModuleBaseAddress = 0;
	if (hSnapshot == INVALID_HANDLE_VALUE) {
		std::wcerr << L"[!]Fail to create tool help snapshot!" << std::endl;
		exit(-1);
	}

	MODULEENTRY32 ModuleEntry32 = { 0 };
	ModuleEntry32.dwSize = sizeof(MODULEENTRY32);
	bool found = false;
	if (Module32First(hSnapshot, &ModuleEntry32))
	{
		do
		{
			if (wcscmp(ModuleEntry32.szModule, path.c_str()) == 0)
			{
				found = true;
				break;
			}
		} while (Module32Next(hSnapshot, &ModuleEntry32));
	}
	CloseHandle(hSnapshot);

	if (found) {
		HANDLE hProc = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE, FALSE, pid);

		if (hProc == NULL) {
			DWORD error = GetLastError();
			std::wcerr << L"[!] Failed to open process. Error code: " << error << std::endl;
			exit(-1);
		}

		std::wcout << L"[+]DLL already injected. Ejecting first." << std::endl;

		HMODULE hKernel32 = LoadLibrary(L"kernel32");
		LPVOID lpStartAddress = GetProcAddress(hKernel32, "FreeLibrary");
		HANDLE hThread = CreateRemoteThread(hProc, NULL, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(lpStartAddress), ModuleEntry32.modBaseAddr, 0, NULL);

		if (hThread == NULL) {
			DWORD error = GetLastError();
			std::wcerr << L"[!] Failed to create Remote Thread. Error code: " << error << std::endl;
			CloseHandle(hProc); // Close process handle before exiting
			exit(-1);
		}

		WaitForSingleObject(hThread, INFINITE);

		// FreeLibrary(hKernel32); // Not needed here
		CloseHandle(hProc);
		CloseHandle(hThread);
	}

}

void InjectDLL(const int& pid, const std::wstring& path) {
	if (!std::filesystem::exists(path)) {
		std::wcerr << L"[!]Couldn't find DLL!" << std::endl;
		exit(-1);
	}

	long dll_size = path.length() * sizeof(wchar_t) + 1;
	HANDLE hProc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);

	if (hProc == NULL) {
		std::wcerr << L"[!]Fail to open target process!" << std::endl;
		exit(-1);
	}
	std::wcout << L"[+]Opening Target Process..." << std::endl;

	LPVOID lpAlloc = VirtualAllocEx(hProc, NULL, dll_size, MEM_COMMIT, PAGE_EXECUTE_READWRITE);
	if (lpAlloc == NULL) {
		std::wcerr << L"[!]Fail to allocate memory in Target Process." << std::endl;
		exit(-1);
	}

	std::wcout << L"[+]Allocating memory in Target Process." << std::endl;
	if (WriteProcessMemory(hProc, lpAlloc, path.c_str(), dll_size, 0) == 0) {
		std::wcerr << L"[!]Fail to write in Target Process memory." << std::endl;
		exit(-1);
	}
	std::wcout << L"[+]Creating Remote Thread in Target Process" << std::endl;

	HMODULE hKernel32 = LoadLibrary(L"kernel32");
	LPVOID lpStartAddress = GetProcAddress(hKernel32, "LoadLibraryW");
	HANDLE hThread = CreateRemoteThread(hProc, NULL, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(lpStartAddress), lpAlloc, 0, NULL);
	if (hThread == NULL) {
		std::wcerr << L"[!]Fail to create Remote Thread" << std::endl;
		exit(-1);
	}
	WaitForSingleObject(hThread, INFINITE);

	CloseHandle(hProc);
	CloseHandle(hThread);

	std::wcout << L"Press any key to exit..." << std::endl;
	std::cin.get();

}

