#pragma once

#include <Windows.h>
#include <cstdint>
#include <string>
#include "plugin/D2RHUD/D2RHUD.h"

namespace D3D12 {

	enum class Status {
		UnknownError = -1,
		NotSupportedError = -2,
		ModuleNotFoundError = -3,

		AlreadyInitializedError = -4,
		NotInitializedError = -5,

		Success = 0,
	};

	Status Init();

	Status InstallHooks();
	Status RemoveHooks();

	extern DWORD PID;
	extern HWND Window;
	extern bool FontLoaded;

	// Call early so gear.png can be found next to the DLL. Pass the HMODULE from DllMain.
	void SetDllModule(HMODULE hModule);

	// Gear icon texture for Settings button (loaded from D2RHUD_Images/ folder). Returns 0 if not loaded.
	uint64_t GetGearTextureId();
	void GetGearTextureSize(int* outWidth, int* outHeight);

	// Folder path for images (D2RHUD_Images). Use for listing .png/.jpg for custom icon choice.
	std::string GetD2RHUDImagesPath();

	// Request reload of the settings icon from D2RHUD_Images/<filename>. Takes effect next frame.
	void RequestGearIconReload(const std::string& filename);

	// Window background images (from D2RHUD_Images). One per window (slot 0..8). Optional; when set, that window draws it instead of solid color.
	uint64_t GetWindowBgTextureId(int slot);  // slot 0..8
	void RequestWindowBgImageReload(int slot, const std::string& filename);  // empty = clear

	// Frame background, checkbox, and button images (from D2RHUD_Images). Slots 0,1,2 = default, hovered, active.
	uint64_t GetFrameBgTextureId(int slot = 0);
	void RequestFrameBgImageReload(int slot, const std::string& filename);  // empty = clear
	uint64_t GetCheckboxTextureId(int slot = 0);
	void RequestCheckboxImageReload(int slot, const std::string& filename);  // empty = clear
	uint64_t GetButtonTextureId(int slot = 0);
	void RequestButtonImageReload(int slot, const std::string& filename);  // empty = clear

	// Camera.png button icon for "use image" on each color row (from D2RHUD_Images/).
	uint64_t GetCameraButtonTextureId();
	void GetCameraButtonTextureSize(int* outWidth, int* outHeight);

}
