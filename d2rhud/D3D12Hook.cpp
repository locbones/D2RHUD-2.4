#include "plugin/HUDControls.h"
#include "D3D12Hook.h"
#include "plugin/D2RHUD/HudCommands.h"
#include "plugin/MarketplacePanel.h"
#include "plugin/HudLocalization.h"
#include "WineQueue.h"
#include "plugin/QuestJournalClient.h"
#include "plugin/WindowLayout.h"
#include "plugin/SettingsUI.h"
#include "plugin/BuffIconsData.h"
#include "plugin/CubeMap.h"
#include "plugin/D3Whirlwind.h"
#include "plugin/LegacyTransmog.h"
#include "plugin/StackSplitSkin.h"
#include "plugin/Inspect.h"
#include "plugin/SpectateVideo.h"
#include "plugin/MapEventFonts.h"
#include <map>
#include <mutex>
#include <atomic>
#include "resource.h"
#include "ProcessLifetime.h"
#include "HitchDiagnostics.h"
#include "RuntimeTelemetry.h"
#include "ThemeImageRequests.h"
#include <d3d12.h>
#include <dxgi1_4.h>
#include <imgui.h>
#include <imgui_impl_dx12.h>
#include <imgui_impl_win32.h>
#include <stdio.h>
#include <string>
#include <chrono>
#include <thread>
#include "plugin/PluginManager.h"
#include <atlbase.h>
#include <fstream>
#include <iostream>
#include <windows.h>
#include <sys/stat.h>
#include <filesystem>
#include "plugin/D2RHUD/D2RHUD.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <algorithm>
#include <vector>
#include <memory>
#include <cctype>


#if __has_include(<detours/detours.h>)
#include <detours/detours.h>
#define USE_DETOURS
#elif __has_include(<MinHook.h>)
#include <MinHook.h>
#ifndef USE_DETOURS
#define USE_MINHOOK
#endif
#else
#error "No hooking library defined!"
#endif

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d12.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace D3D12 {

	template<typename T>
	static void SafeRelease(T*& res) {
		if (res)
			res->Release();
		res = NULL;
	}

	//https://github.com/ocornut/imgui/blob/master/examples/example_win32_directx12/main.cpp
	struct FrameContext {
		CComPtr<ID3D12CommandAllocator> command_allocator = NULL;
		CComPtr<ID3D12Resource> main_render_target_resource = NULL;
		D3D12_CPU_DESCRIPTOR_HANDLE main_render_target_descriptor;
	};

	// Data
    static constexpr int kWindowBgSlots=8,kFrameBgSlots=3,kCheckboxSlots=3,kButtonSlots=3;
    static constexpr UINT kBuffAtlasSlots=32,kInspectSlots=UINT(Inspect::Data::MaxItems+3);
    struct BuffAtlasTexture {CComPtr<ID3D12Resource> resource;D3D12_GPU_DESCRIPTOR_HANDLE handle{};};
    struct GraphicsResources {
        std::vector<FrameContext> frames;
        CComPtr<ID3D12DescriptorHeap> rtv,srv;
        CComPtr<ID3D12CommandQueue> queue;
        CComPtr<ID3D12GraphicsCommandList> commands;
        CComPtr<ID3D12Resource> gear,camera;
        CComPtr<ID3D12Resource> window[kWindowBgSlots],frame[kFrameBgSlots],checkbox[kCheckboxSlots],button[kButtonSlots];
        std::map<std::string,BuffAtlasTexture> atlases;
        BuffAtlasTexture quantity;
        std::shared_ptr<const LegacyTransmog::Skin::Sprite> quantitySprite;
        std::shared_ptr<const Inspect::Assets> inspectAssets;
        std::vector<BuffAtlasTexture> inspectTextures;
        CComPtr<ID3D12Fence> inspectRetirement;
        int inspectFrame=-1,inspectUploads=0;
    };
    // ResetState releases these during resize/explicit unload. At process exit,
    // keep native COM releases out of CRT destruction under the loader lock:
    // the graphics driver or its worker threads may already be torn down.
    static GraphicsResources& Graphics(){return HudLifetime::State<GraphicsResources>();}
	static UINT						g_FrameBufferCount = 0;

    static std::mutex g_QueueMutex;
    static std::recursive_mutex g_RenderMutex;
    static std::atomic<bool> g_HooksReady{false};
    static bool g_QueueHookInstalled = false, g_PresentHookInstalled = false, g_ResizeHookInstalled = false;
    static bool g_WineMacOS = false;
    static bool g_UseSwapchainQueue = false;
    static WineQueue::Layout g_WineQueueLayout;
    static IDXGISwapChain3* g_RenderSwapchain = nullptr; // identity only; owned by the game
    static std::array<uintptr_t, 18> g_SwapchainMethods{};

	static HMODULE g_DllModule = NULL;
	static D3D12_GPU_DESCRIPTOR_HANDLE g_GearTextureGPUHandle = {};
	static int g_GearTextureWidth = 0;
	static int g_GearTextureHeight = 0;
	static std::string s_PendingGearFilename;  // non-empty = reload this file next frame

	static D3D12_GPU_DESCRIPTOR_HANDLE g_WindowBgTextureGPUHandle[kWindowBgSlots] = {};

	static D3D12_GPU_DESCRIPTOR_HANDLE g_FrameBgTextureGPUHandle[kFrameBgSlots] = {};

	static D3D12_GPU_DESCRIPTOR_HANDLE g_CheckboxTextureGPUHandle[kCheckboxSlots] = {};

	static D3D12_GPU_DESCRIPTOR_HANDLE g_ButtonTextureGPUHandle[kButtonSlots] = {};

	static D3D12_GPU_DESCRIPTOR_HANDLE g_CameraButtonTextureGPUHandle = {};
	static int g_CameraButtonTextureWidth = 0;
	static int g_CameraButtonTextureHeight = 0;

	static std::string GetD2RHUDImagesDir()
	{
		std::string dir;
		char buf[MAX_PATH];
		if (g_DllModule && GetModuleFileNameA(g_DllModule, buf, MAX_PATH)) {
			std::filesystem::path p(buf);
			dir = (p.parent_path() / "D2RHUD_Images").string();
		}
		if (dir.empty() || !std::filesystem::exists(dir)) {
			if (GetModuleFileNameA(NULL, buf, MAX_PATH)) {
				std::filesystem::path p(buf);
				dir = (p.parent_path() / "D2RHUD_Images").string();
			}
		}
		if (dir.empty() || !std::filesystem::exists(dir))
			dir = (std::filesystem::current_path() / "D2RHUD_Images").string();
		return dir;
	}

	static std::string GetD2RHUDFontsDir()
	{
		std::string dir;
		char buf[MAX_PATH];
		if (g_DllModule && GetModuleFileNameA(g_DllModule, buf, MAX_PATH)) {
			std::filesystem::path p(buf);
			dir = (p.parent_path() / "D2RHUD_Fonts").string();
		}
		if (dir.empty() || !std::filesystem::exists(dir)) {
			if (GetModuleFileNameA(NULL, buf, MAX_PATH)) {
				std::filesystem::path p(buf);
				dir = (p.parent_path() / "D2RHUD_Fonts").string();
			}
		}
		if (dir.empty() || !std::filesystem::exists(dir))
			dir = (std::filesystem::current_path() / "D2RHUD_Fonts").string();
		return dir;
	}

	static const int kEmbeddedFloatingDamageFontResourceIds[D3D12::kFloatingDamageFontCount] = {
		IDR_FD_FONT_0, IDR_FD_FONT_1, IDR_FD_FONT_2, IDR_FD_FONT_3,
		IDR_FD_FONT_4, IDR_FD_FONT_5, IDR_FD_FONT_6, IDR_FD_FONT_7,
		IDR_FD_FONT_8, IDR_FD_FONT_9, IDR_FD_FONT_10, IDR_FD_FONT_11
	};

	static std::vector<std::vector<unsigned char>> g_EmbeddedFloatingDamageFontData;
	static ImFont* g_BundledFloatingDamageFonts[D3D12::kFloatingDamageFontCount] = {};
	static bool g_EmbeddedFloatingDamageFontsInitialized = false;
    static CubeMap::Text::FontBank g_MapEventFonts;
    static ImFont* GetMapEventFont(float size){return g_MapEventFonts.ForSize(size);}

	static bool LoadEmbeddedFloatingDamageFontResource(int index, const unsigned char** outData, size_t* outSize)
	{
		if (!outData || !outSize || index < 0 || index >= D3D12::kFloatingDamageFontCount)
			return false;

		if (!g_DllModule)
			return false;

		const int resourceId = kEmbeddedFloatingDamageFontResourceIds[index];
		HRSRC resource = FindResourceW(g_DllModule, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
		if (!resource)
			return false;

		HGLOBAL resourceData = LoadResource(g_DllModule, resource);
		if (!resourceData)
			return false;

		const void* lockedData = LockResource(resourceData);
		const DWORD resourceSize = SizeofResource(g_DllModule, resource);
		if (!lockedData || resourceSize == 0)
			return false;

		*outData = static_cast<const unsigned char*>(lockedData);
		*outSize = static_cast<size_t>(resourceSize);
		return true;
	}

	static bool TryLoadSingleEmbeddedFloatingDamageFont(int index)
	{
		if (index < 0 || index >= D3D12::kFloatingDamageFontCount)
			return false;
		if (g_BundledFloatingDamageFonts[index])
			return true;

		const unsigned char* resourceData = nullptr;
		size_t resourceSize = 0;
		if (!LoadEmbeddedFloatingDamageFontResource(index, &resourceData, &resourceSize))
			return false;

		g_EmbeddedFloatingDamageFontData.emplace_back(resourceData, resourceData + resourceSize);
		std::vector<unsigned char>& fontData = g_EmbeddedFloatingDamageFontData.back();

		ImGuiIO& io = ImGui::GetIO();
		ImFontConfig cfg{};
		cfg.OversampleH = 1;
		cfg.OversampleV = 1;
		cfg.PixelSnapH = true;
		cfg.FontDataOwnedByAtlas = false;
		std::string fontLabel = "FloatingDamageFont" + std::to_string(index);
		if (fontLabel.size() >= IM_ARRAYSIZE(cfg.Name))
			fontLabel.resize(IM_ARRAYSIZE(cfg.Name) - 1);
		strcpy_s(cfg.Name, fontLabel.c_str());

		const float rasterSize = (index == 0) ? 24.0f : 32.0f;
		ImFont* font = io.Fonts->AddFontFromMemoryTTF(
			fontData.data(),
			static_cast<int>(fontData.size()),
			rasterSize,
			&cfg,
			io.Fonts->GetGlyphRangesDefault());
		if (!font)
			return false;

		g_BundledFloatingDamageFonts[index] = font;
		return true;
	}

	static void LoadAllEmbeddedFloatingDamageFonts()
	{
		if (g_EmbeddedFloatingDamageFontsInitialized)
			return;

		for (int i = 0; i < D3D12::kFloatingDamageFontCount; ++i)
			TryLoadSingleEmbeddedFloatingDamageFont(i);

		g_EmbeddedFloatingDamageFontsInitialized = true;
	}

    static ImVector<ImWchar> localizationRanges;
    static ImFont* AddLocalizedFont(float size){
        auto atlas=ImGui::GetIO().Fonts;ImFontConfig config;config.OversampleH=config.OversampleV=1;
        const auto path=HudLocale::FontPath();
        if(!path.empty())return atlas->AddFontFromFileTTF(path.c_str(),size,&config,localizationRanges.Data);
        size_t bytes=0;const auto data=HudLocale::FontBytes(bytes);config.FontDataOwnedByAtlas=false;
        return data&&bytes<=INT_MAX?atlas->AddFontFromMemoryTTF(const_cast<unsigned char*>(data),int(bytes),size,&config,localizationRanges.Data):nullptr;
    }
    static void LoadMapEventFonts(const std::string& path){
        if(HudLocale::UnicodeFont()){
            size_t bytes=0;const auto data=HudLocale::FontBytes(bytes);
            g_MapEventFonts.Load(*ImGui::GetIO().Fonts,HudLocale::FontPath(),data,bytes,localizationRanges.Data,true);
            CubeMap::SetFontProvider(GetMapEventFont);return;
        }
        const unsigned char* fallback=nullptr;size_t bytes=0;
        if(path.empty())LoadEmbeddedFloatingDamageFontResource(0,&fallback,&bytes);
        g_MapEventFonts.Load(*ImGui::GetIO().Fonts,path,fallback,bytes);
        CubeMap::SetFontProvider(GetMapEventFont);
    }

	static bool LoadGearTextureFromPath(ID3D12Device* pD3DDevice, const std::string& fullPath)
	{
		if (!pD3DDevice || fullPath.empty() || !std::filesystem::exists(fullPath)) return false;
		int texW = 0, texH = 0, texCh = 0;
		unsigned char* pixels = stbi_load(fullPath.c_str(), &texW, &texH, &texCh, 4);
		if (!pixels || texW <= 0 || texH <= 0) return false;
		Graphics().gear = NULL;
		D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
		UINT64 totalBytes = 0;
		D3D12_RESOURCE_DESC texDesc = {};
		texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		texDesc.Alignment = 0;
		texDesc.Width = (UINT)texW;
		texDesc.Height = (UINT)texH;
		texDesc.DepthOrArraySize = 1;
		texDesc.MipLevels = 1;
		texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		texDesc.SampleDesc.Count = 1;
		texDesc.SampleDesc.Quality = 0;
		texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
		pD3DDevice->GetCopyableFootprints(&texDesc, 0, 1, 0, &footprint, nullptr, nullptr, &totalBytes);
		footprint.Offset = 0;
		UINT64 rowPitch = footprint.Footprint.RowPitch;
		D3D12_RESOURCE_DESC bufDesc = {};
		bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		bufDesc.Alignment = 0;
		bufDesc.Width = totalBytes;
		bufDesc.Height = 1;
		bufDesc.DepthOrArraySize = 1;
		bufDesc.MipLevels = 1;
		bufDesc.Format = DXGI_FORMAT_UNKNOWN;
		bufDesc.SampleDesc.Count = 1;
		bufDesc.SampleDesc.Quality = 0;
		bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		bufDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
		D3D12_HEAP_PROPERTIES heapProps = {};
		heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		CComPtr<ID3D12Resource> uploadBuffer;
		heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
		if (FAILED(pD3DDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &bufDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadBuffer)))) {
			stbi_image_free(pixels);
			return false;
		}
		heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
		if (FAILED(pD3DDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &texDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&Graphics().gear)))) {
			stbi_image_free(pixels);
			return false;
		}
		void* mapped = nullptr;
		if (SUCCEEDED(uploadBuffer->Map(0, nullptr, &mapped))) {
			for (int y = 0; y < texH; ++y)
				memcpy((char*)mapped + y * (size_t)rowPitch, pixels + y * texW * 4, (size_t)texW * 4);
			uploadBuffer->Unmap(0, nullptr);
		}
		stbi_image_free(pixels);
		CComPtr<ID3D12CommandQueue> uploadQueue;
		D3D12_COMMAND_QUEUE_DESC qDesc = {};
		qDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		if (FAILED(pD3DDevice->CreateCommandQueue(&qDesc, IID_PPV_ARGS(&uploadQueue)))) return false;
		CComPtr<ID3D12CommandAllocator> uploadAlloc;
		CComPtr<ID3D12GraphicsCommandList> uploadList;
		if (FAILED(pD3DDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&uploadAlloc))) ||
			FAILED(pD3DDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, uploadAlloc, nullptr, IID_PPV_ARGS(&uploadList)))) return false;
		D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
		srcLoc.pResource = uploadBuffer;
		srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		srcLoc.PlacedFootprint = footprint;
		D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
		dstLoc.pResource = Graphics().gear;
		dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		dstLoc.SubresourceIndex = 0;
		uploadList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);
		D3D12_RESOURCE_BARRIER barrier = {};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		barrier.Transition.pResource = Graphics().gear;
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		uploadList->ResourceBarrier(1, &barrier);
		uploadList->Close();
		ID3D12CommandList* lists[] = { uploadList };
		uploadQueue->ExecuteCommandLists(1, lists);
		CComPtr<ID3D12Fence> fence;
		UINT64 fenceVal = 1;
		if (SUCCEEDED(pD3DDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) {
			HANDLE evt = CreateEventEx(nullptr, nullptr, 0, EVENT_MODIFY_STATE | SYNCHRONIZE);
			if (evt && SUCCEEDED(fence->SetEventOnCompletion(fenceVal, evt))) {
				uploadQueue->Signal(fence, fenceVal);
				{HudTelemetry::Scope measured(HudTelemetry::Feature::GpuWait);WaitForSingleObject(evt, 5000);}
			}
			if (evt) CloseHandle(evt);
		}
		const UINT srvDescSize = pD3DDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		D3D12_CPU_DESCRIPTOR_HANDLE srvCpu = Graphics().srv->GetCPUDescriptorHandleForHeapStart();
		D3D12_GPU_DESCRIPTOR_HANDLE srvGpu = Graphics().srv->GetGPUDescriptorHandleForHeapStart();
		srvCpu.ptr += (SIZE_T)srvDescSize * (SIZE_T)g_FrameBufferCount;
		srvGpu.ptr += (SIZE_T)srvDescSize * (SIZE_T)g_FrameBufferCount;
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Texture2D.MipLevels = 1;
		pD3DDevice->CreateShaderResourceView(Graphics().gear, &srvDesc, srvCpu);
		g_GearTextureGPUHandle = srvGpu;
		g_GearTextureWidth = texW;
		g_GearTextureHeight = texH;
		return true;
	}

	static bool LoadTexturePixels(ID3D12Device* pD3DDevice, const unsigned char* pixels, int texW, int texH, DXGI_FORMAT format,
		CComPtr<ID3D12Resource>& outTex, D3D12_GPU_DESCRIPTOR_HANDLE& outGpuHandle, UINT descriptorOffset)
	{
		if (!pD3DDevice || !pixels || !Graphics().srv || texW <= 0 || texH <= 0 ||
            texW > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION || texH > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
            (format != DXGI_FORMAT_R8G8B8A8_UNORM && format != DXGI_FORMAT_BC3_UNORM) ||
            descriptorOffset >= Graphics().srv->GetDesc().NumDescriptors) return false;
		CComPtr<ID3D12Resource> texture;
		D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
		UINT64 totalBytes = 0;
		D3D12_RESOURCE_DESC texDesc = {};
		texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		texDesc.Alignment = 0;
		texDesc.Width = format == DXGI_FORMAT_BC3_UNORM ? (texW + 3) & ~3 : texW;
		texDesc.Height = format == DXGI_FORMAT_BC3_UNORM ? (texH + 3) & ~3 : texH;
		texDesc.DepthOrArraySize = 1;
		texDesc.MipLevels = 1;
		texDesc.Format = format;
		texDesc.SampleDesc.Count = 1;
		texDesc.SampleDesc.Quality = 0;
		texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
		pD3DDevice->GetCopyableFootprints(&texDesc, 0, 1, 0, &footprint, nullptr, nullptr, &totalBytes);
		footprint.Offset = 0;
		UINT64 rowPitch = footprint.Footprint.RowPitch;
		D3D12_RESOURCE_DESC bufDesc = {};
		bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		bufDesc.Alignment = 0;
		bufDesc.Width = totalBytes;
		bufDesc.Height = 1;
		bufDesc.DepthOrArraySize = 1;
		bufDesc.MipLevels = 1;
		bufDesc.Format = DXGI_FORMAT_UNKNOWN;
		bufDesc.SampleDesc.Count = 1;
		bufDesc.SampleDesc.Quality = 0;
		bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		bufDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
		D3D12_HEAP_PROPERTIES heapProps = {};
		heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		CComPtr<ID3D12Resource> uploadBuffer;
		heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
		if (FAILED(pD3DDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &bufDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadBuffer)))) {
			return false;
		}
		heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
		if (FAILED(pD3DDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &texDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&texture)))) {
			return false;
		}
		void* mapped = nullptr;
		if (SUCCEEDED(uploadBuffer->Map(0, nullptr, &mapped))) {
			const bool bc3 = format == DXGI_FORMAT_BC3_UNORM;
            const size_t rows = bc3 ? (texH + 3) / 4 : texH;
            const size_t bytesPerRow = bc3 ? ((texW + 3) / 4) * 16 : texW * 4;
            for (size_t y = 0; y < rows; ++y)
                memcpy((char*)mapped + y * (size_t)rowPitch, pixels + y * bytesPerRow, bytesPerRow);
			uploadBuffer->Unmap(0, nullptr);
		} else return false;
		CComPtr<ID3D12CommandQueue> uploadQueue;
		D3D12_COMMAND_QUEUE_DESC qDesc = {};
		qDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		if (FAILED(pD3DDevice->CreateCommandQueue(&qDesc, IID_PPV_ARGS(&uploadQueue)))) return false;
		CComPtr<ID3D12CommandAllocator> uploadAlloc;
		CComPtr<ID3D12GraphicsCommandList> uploadList;
		if (FAILED(pD3DDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&uploadAlloc))) ||
			FAILED(pD3DDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, uploadAlloc, nullptr, IID_PPV_ARGS(&uploadList)))) return false;
		D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
		srcLoc.pResource = uploadBuffer;
		srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		srcLoc.PlacedFootprint = footprint;
		D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
		dstLoc.pResource = texture;
		dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		dstLoc.SubresourceIndex = 0;
		uploadList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);
		D3D12_RESOURCE_BARRIER barrier = {};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		barrier.Transition.pResource = texture;
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		uploadList->ResourceBarrier(1, &barrier);
        if (FAILED(uploadList->Close())) return false;
        CComPtr<ID3D12Fence> fence;
        if (FAILED(pD3DDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) return false;
        HANDLE completed = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!completed) return false;
        if (FAILED(fence->SetEventOnCompletion(1, completed))) { CloseHandle(completed); return false; }
        ID3D12CommandList* lists[] = { uploadList };
        uploadQueue->ExecuteCommandLists(1, lists);
        const HRESULT signaled = uploadQueue->Signal(fence, 1);
        // Keep the staging buffer and command objects alive until the GPU finishes.
        if (SUCCEEDED(signaled)){HudTelemetry::Scope measured(HudTelemetry::Feature::GpuWait);WaitForSingleObject(completed, INFINITE);}
        CloseHandle(completed);
        if (FAILED(signaled) || fence->GetCompletedValue() == UINT64_MAX) return false;

		const UINT srvDescSize = pD3DDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		D3D12_CPU_DESCRIPTOR_HANDLE srvCpu = Graphics().srv->GetCPUDescriptorHandleForHeapStart();
		D3D12_GPU_DESCRIPTOR_HANDLE srvGpu = Graphics().srv->GetGPUDescriptorHandleForHeapStart();
		srvCpu.ptr += (SIZE_T)srvDescSize * (SIZE_T)descriptorOffset;
		srvGpu.ptr += (SIZE_T)srvDescSize * (SIZE_T)descriptorOffset;
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = format;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Texture2D.MipLevels = 1;
		pD3DDevice->CreateShaderResourceView(texture, &srvDesc, srvCpu);
		outTex = texture;
		outGpuHandle = srvGpu;
		return true;
	}

    static bool LoadThemeTextureFromPath(ID3D12Device* device, const std::string& path,
        CComPtr<ID3D12Resource>& texture, D3D12_GPU_DESCRIPTOR_HANDLE& handle, UINT offset)
    {
        int width = 0, height = 0, channels = 0;
        unsigned char* pixels = stbi_load(path.c_str(), &width, &height, &channels, 4);
        if (!pixels) return false;
        const bool ok = LoadTexturePixels(device, pixels, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, texture, handle, offset);
        stbi_image_free(pixels);
        return ok;
    }

    struct ThemeTextureSlot {
        CComPtr<ID3D12Resource>* resource;
        D3D12_GPU_DESCRIPTOR_HANDLE* handle;
        UINT offset;
    };
    static_assert(kWindowBgSlots == 8 && kFrameBgSlots == 3 && kCheckboxSlots == 3 && kButtonSlots == 3 &&
        ThemeImages::SlotCount == kWindowBgSlots + kFrameBgSlots + kCheckboxSlots + kButtonSlots);
    static ThemeTextureSlot ThemeTexture(size_t slot)
    {
        if (slot < 8) return { std::addressof(Graphics().window[slot]), &g_WindowBgTextureGPUHandle[slot], g_FrameBufferCount + 1 + UINT(slot) };
        if (slot < 11) return { std::addressof(Graphics().frame[slot - 8]), &g_FrameBgTextureGPUHandle[slot - 8], g_FrameBufferCount + 11 + UINT(slot - 8) };
        if (slot < 14) return { std::addressof(Graphics().checkbox[slot - 11]), &g_CheckboxTextureGPUHandle[slot - 11], g_FrameBufferCount + 14 + UINT(slot - 11) };
        return { std::addressof(Graphics().button[slot - 14]), &g_ButtonTextureGPUHandle[slot - 14], g_FrameBufferCount + 17 + UINT(slot - 14) };
    }
    // The template permits a test device source while production passes its
    // native swapchain. All texture and device references still use real COM.
    template<class SwapChain> static void ProcessThemeImageRequests(SwapChain* swapchain)
    {
        const auto requests = ThemeImages::Runtime().TakeDue(GetTickCount64());
        CComPtr<ID3D12Device> device;
        bool deviceAttempted = false, directoryRead = false;
        std::string directory;
        for (const auto& request : requests) {
            auto slot = ThemeTexture(request.slot);
            bool success = false;
            if (request.filename.empty()) {
                slot.resource->Release();
                *slot.handle = {};
                success = true;
            }
            else try {
                if (!deviceAttempted) {
                    deviceAttempted = true;
                    if (FAILED(swapchain->GetDevice(IID_PPV_ARGS(&device)))) device.Release();
                }
                if (device) {
                    if (!directoryRead) { directory = GetD2RHUDImagesDir(); directoryRead = true; }
                    const auto path = (std::filesystem::path(directory) / request.filename).string();
                    success = LoadThemeTextureFromPath(device, path, *slot.resource, *slot.handle, slot.offset);
                }
            }
            catch (...) { /* Keep the request, and retry after the normal backoff. */ }
            ThemeImages::Runtime().Complete(request, success, GetTickCount64());
        }
    }

    uint64_t GetBuffIconAtlas(const std::string& key, const BuffIcons::SpriteAtlas& atlas)
    {
        auto found = Graphics().atlases.find(key);
        if (found != Graphics().atlases.end()) return found->second.handle.ptr;
        if (!Graphics().srv || !Graphics().queue || Graphics().atlases.size() >= kBuffAtlasSlots || atlas.pixels.empty()) return 0;
        CComPtr<ID3D12Device> device;
        if (FAILED(Graphics().queue->GetDevice(IID_PPV_ARGS(&device)))) return 0;
        BuffAtlasTexture texture;
        const UINT slot = g_FrameBufferCount + 20 + static_cast<UINT>(Graphics().atlases.size());
        if (!LoadTexturePixels(device, atlas.pixels.data(), atlas.width, atlas.height,
            atlas.bc3 ? DXGI_FORMAT_BC3_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM, texture.resource, texture.handle, slot)) return 0;
        const uint64_t id = texture.handle.ptr;
        Graphics().atlases.emplace(key, std::move(texture));
        return id;
    }

    static LegacyTransmog::Skin::Image GetInspectArtwork(const std::shared_ptr<const Inspect::Assets>& assets,size_t index){
        if(!assets || index>=assets->sprites.size() || assets->sprites.size()>kInspectSlots || !Graphics().srv || !Graphics().queue)return {};
        auto& g=Graphics();CComPtr<ID3D12Device> device;if(FAILED(g.queue->GetDevice(IID_PPV_ARGS(&device))))return {};
        if(g.inspectAssets!=assets){
            if(g.inspectAssets && !g.inspectTextures.empty()){
                // Never replace a descriptor still referenced by a submitted
                // HUD frame. Poll a queue fence without blocking the render loop.
                if(!g.inspectRetirement){
                    if(FAILED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&g.inspectRetirement))))return {};
                    if(FAILED(g.queue->Signal(g.inspectRetirement,1))){g.inspectRetirement.Release();return {};}
                    return {};
                }
                const auto completed=g.inspectRetirement->GetCompletedValue();if(completed<1 || completed==UINT64_MAX)return {};
            }
            g.inspectTextures.clear();g.inspectRetirement.Release();g.inspectAssets=assets;g.inspectTextures.resize(assets->sprites.size());
        }
        const auto sprite=assets->sprites[index];if(!sprite)return {};
        auto& texture=g.inspectTextures[index];const auto& a=sprite->atlas;
        if(!texture.resource){
            if(g.inspectFrame!=ImGui::GetFrameCount()){g.inspectFrame=ImGui::GetFrameCount();g.inspectUploads=0;}
            if(g.inspectUploads>=2)return {}; ++g.inspectUploads;
            if(!LoadTexturePixels(device,a.pixels.data(),a.width,a.height,a.bc3?DXGI_FORMAT_BC3_UNORM:DXGI_FORMAT_R8G8B8A8_UNORM,
                texture.resource,texture.handle,g_FrameBufferCount+20+kBuffAtlasSlots+1+UINT(index)))return {};
        }
        return {texture.handle.ptr,a.width,a.height,a.frameWidth,a.frames,sprite->pitch};
    }

    static LegacyTransmog::Skin::Image GetQuantityItemArtwork(const std::string& code){
        const auto sprite=LegacyTransmog::Skin::ItemSprite(code);
        if(!sprite || !Graphics().srv || !Graphics().queue)return {};
        if(sprite!=Graphics().quantitySprite){
            CComPtr<ID3D12Device> device;
            if(FAILED(Graphics().queue->GetDevice(IID_PPV_ARGS(&device))))return {};
            // One reusable descriptor, independent of the buff atlas cache. Finish
            // earlier HUD draws before replacing its descriptor or old resource.
            if(Graphics().quantity.resource){
                CComPtr<ID3D12Fence> fence;
                if(FAILED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence))))return {};
                HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!event)return {};
                const bool queued=SUCCEEDED(fence->SetEventOnCompletion(1,event)) && SUCCEEDED(Graphics().queue->Signal(fence,1));
                const bool complete=[&]{HudTelemetry::Scope measured(HudTelemetry::Feature::GpuWait);return queued && WaitForSingleObject(event,5000)==WAIT_OBJECT_0 && fence->GetCompletedValue()!=UINT64_MAX;}();
                CloseHandle(event);if(!complete)return {};
            }
            const auto& atlas=sprite->atlas;BuffAtlasTexture texture;
            if(!LoadTexturePixels(device,atlas.pixels.data(),atlas.width,atlas.height,atlas.bc3?DXGI_FORMAT_BC3_UNORM:DXGI_FORMAT_R8G8B8A8_UNORM,
                texture.resource,texture.handle,g_FrameBufferCount+20+kBuffAtlasSlots))return {};
            Graphics().quantity=std::move(texture);Graphics().quantitySprite=sprite;
        }
        const auto& a=sprite->atlas;
        return {Graphics().quantity.handle.ptr,a.width,a.height,a.frameWidth,a.frames,sprite->pitch};
    }

	static bool LoadCameraButtonTextureFromPath(ID3D12Device* pD3DDevice, const std::string& fullPath)
	{
		if (!pD3DDevice || fullPath.empty() || !std::filesystem::exists(fullPath)) return false;
		int texW = 0, texH = 0, texCh = 0;
		unsigned char* pixels = stbi_load(fullPath.c_str(), &texW, &texH, &texCh, 4);
		if (!pixels || texW <= 0 || texH <= 0) return false;
		Graphics().camera = NULL;
		D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
		UINT64 totalBytes = 0;
		D3D12_RESOURCE_DESC texDesc = {};
		texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		texDesc.Alignment = 0;
		texDesc.Width = (UINT)texW;
		texDesc.Height = (UINT)texH;
		texDesc.DepthOrArraySize = 1;
		texDesc.MipLevels = 1;
		texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		texDesc.SampleDesc.Count = 1;
		texDesc.SampleDesc.Quality = 0;
		texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
		pD3DDevice->GetCopyableFootprints(&texDesc, 0, 1, 0, &footprint, nullptr, nullptr, &totalBytes);
		footprint.Offset = 0;
		UINT64 rowPitch = footprint.Footprint.RowPitch;
		D3D12_RESOURCE_DESC bufDesc = {};
		bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		bufDesc.Alignment = 0;
		bufDesc.Width = totalBytes;
		bufDesc.Height = 1;
		bufDesc.DepthOrArraySize = 1;
		bufDesc.MipLevels = 1;
		bufDesc.Format = DXGI_FORMAT_UNKNOWN;
		bufDesc.SampleDesc.Count = 1;
		bufDesc.SampleDesc.Quality = 0;
		bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		bufDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
		D3D12_HEAP_PROPERTIES heapProps = {};
		heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		CComPtr<ID3D12Resource> uploadBuffer;
		heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
		if (FAILED(pD3DDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &bufDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadBuffer)))) {
			stbi_image_free(pixels);
			return false;
		}
		heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
		if (FAILED(pD3DDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &texDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&Graphics().camera)))) {
			stbi_image_free(pixels);
			return false;
		}
		void* mapped = nullptr;
		if (SUCCEEDED(uploadBuffer->Map(0, nullptr, &mapped))) {
			for (int y = 0; y < texH; ++y)
				memcpy((char*)mapped + y * (size_t)rowPitch, pixels + y * texW * 4, (size_t)texW * 4);
			uploadBuffer->Unmap(0, nullptr);
		}
		stbi_image_free(pixels);
		CComPtr<ID3D12CommandQueue> uploadQueue;
		D3D12_COMMAND_QUEUE_DESC qDesc = {};
		qDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		if (FAILED(pD3DDevice->CreateCommandQueue(&qDesc, IID_PPV_ARGS(&uploadQueue)))) return false;
		CComPtr<ID3D12CommandAllocator> uploadAlloc;
		CComPtr<ID3D12GraphicsCommandList> uploadList;
		if (FAILED(pD3DDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&uploadAlloc))) ||
			FAILED(pD3DDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, uploadAlloc, nullptr, IID_PPV_ARGS(&uploadList)))) return false;
		D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
		srcLoc.pResource = uploadBuffer;
		srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		srcLoc.PlacedFootprint = footprint;
		D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
		dstLoc.pResource = Graphics().camera;
		dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		dstLoc.SubresourceIndex = 0;
		uploadList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);
		D3D12_RESOURCE_BARRIER barrier = {};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		barrier.Transition.pResource = Graphics().camera;
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		uploadList->ResourceBarrier(1, &barrier);
		uploadList->Close();
		ID3D12CommandList* lists[] = { uploadList };
		uploadQueue->ExecuteCommandLists(1, lists);
		CComPtr<ID3D12Fence> fence;
		UINT64 fenceVal = 1;
		if (SUCCEEDED(pD3DDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) {
			HANDLE evt = CreateEventEx(nullptr, nullptr, 0, EVENT_MODIFY_STATE | SYNCHRONIZE);
			if (evt && SUCCEEDED(fence->SetEventOnCompletion(fenceVal, evt))) {
				uploadQueue->Signal(fence, fenceVal);
				{HudTelemetry::Scope measured(HudTelemetry::Feature::GpuWait);WaitForSingleObject(evt, 5000);}
			}
			if (evt) CloseHandle(evt);
		}
		const UINT srvDescSize = pD3DDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		D3D12_CPU_DESCRIPTOR_HANDLE srvCpu = Graphics().srv->GetCPUDescriptorHandleForHeapStart();
		D3D12_GPU_DESCRIPTOR_HANDLE srvGpu = Graphics().srv->GetGPUDescriptorHandleForHeapStart();
		srvCpu.ptr += (SIZE_T)srvDescSize * (SIZE_T)(g_FrameBufferCount + 10);
		srvGpu.ptr += (SIZE_T)srvDescSize * (SIZE_T)(g_FrameBufferCount + 10);
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Texture2D.MipLevels = 1;
		pD3DDevice->CreateShaderResourceView(Graphics().camera, &srvDesc, srvCpu);
		g_CameraButtonTextureGPUHandle = srvGpu;
		g_CameraButtonTextureWidth = texW;
		g_CameraButtonTextureHeight = texH;
		return true;
	}

	LRESULT APIENTRY WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

	typedef long(__fastcall* Present) (IDXGISwapChain3* pSwapChain, UINT SyncInterval, UINT Flags);
	static Present OriginalPresent;

	typedef void(*ExecuteCommandLists)(ID3D12CommandQueue* queue, UINT NumCommandLists, ID3D12CommandList* const* ppCommandLists);
	static ExecuteCommandLists OriginalExecuteCommandLists;

	typedef long(__fastcall* ResizeBuffers)(IDXGISwapChain3* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags);
	static ResizeBuffers OriginalResizeBuffers;

	WNDPROC OriginalWndProc;
	HWND Window = nullptr;
	bool FontLoaded = false;

	static uint64_t* g_MethodsTable = NULL;
	static std::atomic<bool> g_Initialized{false};


	static PluginManager* g_PluginManager;

	void DrawPresent(IDXGISwapChain3* pSwapChain, ID3D12CommandQueue* queue) {
		if (!g_Initialized) {
			CComPtr<ID3D12Device> pD3DDevice;

			if (FAILED(pSwapChain->GetDevice(__uuidof(ID3D12Device), (void**)&pD3DDevice))) {
				return;
			}

			{
				DXGI_SWAP_CHAIN_DESC desc;
				pSwapChain->GetDesc(&desc);
				Window = desc.OutputWindow;
				if (!OriginalWndProc) {
					OriginalWndProc = (WNDPROC)SetWindowLongPtr(Window, GWLP_WNDPROC, (__int3264)(LONG_PTR)WndProc);
				}
				g_FrameBufferCount = desc.BufferCount;
				Graphics().frames.clear();
				Graphics().frames.resize(g_FrameBufferCount);
			}

			{
				D3D12_DESCRIPTOR_HEAP_DESC desc = {};
				desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
				desc.NumDescriptors = g_FrameBufferCount + 20 + kBuffAtlasSlots + 1 + kInspectSlots + Spectate::Video::TextureSlots;
				desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

				if (pD3DDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&Graphics().srv)) != S_OK) {
					return;
				}
			}

			{
				D3D12_DESCRIPTOR_HEAP_DESC desc;
				desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
				desc.NumDescriptors = g_FrameBufferCount;
				desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
				desc.NodeMask = 1;

				if (pD3DDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&Graphics().rtv)) != S_OK) {
					return;
				}

				const auto rtvDescriptorSize = pD3DDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
				D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = Graphics().rtv->GetCPUDescriptorHandleForHeapStart();

				for (UINT i = 0; i < g_FrameBufferCount; i++) {

					Graphics().frames[i].main_render_target_descriptor = rtvHandle;
					pSwapChain->GetBuffer(i, IID_PPV_ARGS(&Graphics().frames[i].main_render_target_resource));
					pD3DDevice->CreateRenderTargetView(Graphics().frames[i].main_render_target_resource, nullptr, rtvHandle);
					rtvHandle.ptr += rtvDescriptorSize;
				}

			}

			{
				CComPtr<ID3D12CommandAllocator> allocator;
				if (pD3DDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)) != S_OK) {
					return;
				}

				for (size_t i = 0; i < g_FrameBufferCount; i++) {
					if (pD3DDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&Graphics().frames[i].command_allocator)) != S_OK) {
						return;
					}
				}

				if (pD3DDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, Graphics().frames[0].command_allocator, NULL, IID_PPV_ARGS(&Graphics().commands)) != S_OK || Graphics().commands->Close() != S_OK) {
					return;
				}
			}

			IMGUI_CHECKVERSION();
			ImGui::CreateContext();
			ImGuiIO& io = ImGui::GetIO();
			HudWindowLayout::Initialize();
            HudLocale::Initialize();
            if(HudLocale::UnicodeFont()){
                const auto language=HudLocale::LanguageCode();
                if(language=="jaJP"||language=="koKR"||language=="zhCN"||language=="zhTW")io.Fonts->TexDesiredWidth=4096;
                ImFontGlyphRangesBuilder builder;builder.AddRanges(io.Fonts->GetGlyphRangesDefault());
                const auto text=HudLocale::GlyphText();builder.AddText(text.c_str());builder.BuildRanges(&localizationRanges);
            }
			//io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
			//io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

			// Setup Dear ImGui style
			ImGui::StyleColorsDark();
			//ImGui::StyleColorsClassic();

				// Setup Platform/Renderer backends
			ImGui_ImplWin32_Init(Window);
			ImGui_ImplDX12_Init(pD3DDevice, g_FrameBufferCount,
				DXGI_FORMAT_R8G8B8A8_UNORM, Graphics().srv,
				Graphics().srv->GetCPUDescriptorHandleForHeapStart(),
				Graphics().srv->GetGPUDescriptorHandleForHeapStart());

			// Retain UI/damage font indices; append map fonts before one atlas upload.
            std::string eventFontPath;
			{
				namespace fs = std::filesystem;
				const std::string fontPath = "Mods/" + modName + "/" + modName + ".mpq/data/hd/ui/fonts/exocetblizzardot-medium.otf";
				if (HudLocale::UnicodeFont()) {
                    for(float size:{12.f,15.f,18.f,24.f,36.f,48.f})AddLocalizedFont(size);
                    FontLoaded=true;
                }
                else if (fs::exists(fontPath)) {
					std::cout << "Adding fonts from file..." << std::endl;
					io.Fonts->AddFontFromFileTTF(fontPath.c_str(), 12.0f);
					io.Fonts->AddFontFromFileTTF(fontPath.c_str(), 15.0f);
					io.Fonts->AddFontFromFileTTF(fontPath.c_str(), 18.0f);
					io.Fonts->AddFontFromFileTTF(fontPath.c_str(), 24.0f);
					io.Fonts->AddFontFromFileTTF(fontPath.c_str(), 36.0f);
					io.Fonts->AddFontFromFileTTF(fontPath.c_str(), 48.0f);
					FontLoaded = true;
                    eventFontPath=fontPath;
				}
				else {
					std::cerr << "Font file does not exist: " << fontPath << std::endl;
				}
			}

			LoadAllEmbeddedFloatingDamageFonts();
            {
                HudSettings::bodyFont=HudSettings::noteFont=HudSettings::headingFont=nullptr;
                const unsigned char* bytes=nullptr;size_t size=0;
                if(HudLocale::UnicodeFont()){
                    HudSettings::bodyFont=AddLocalizedFont(36.f);HudSettings::noteFont=AddLocalizedFont(32.f);
                    if(HudSettings::bodyFont)HudSettings::bodyFont->Scale=.5f;
                    if(HudSettings::noteFont)HudSettings::noteFont->Scale=.5f;
                }
                else if(LoadEmbeddedFloatingDamageFontResource(10,&bytes,&size)) {
                    ImFontConfig cfg{};cfg.FontDataOwnedByAtlas=false;
                    HudSettings::bodyFont=io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(bytes),int(size),36.f,&cfg);
                    HudSettings::noteFont=io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(bytes),int(size),32.f,&cfg);
                    if(HudSettings::bodyFont)HudSettings::bodyFont->Scale=.5f;
                    if(HudSettings::noteFont)HudSettings::noteFont->Scale=.5f;
                }
                HudSettings::headingFont=io.Fonts->Fonts.Size>3?io.Fonts->Fonts[3]:nullptr;
            }
            LoadMapEventFonts(eventFontPath);
            HudText::fallbackFont=HudLocale::UnicodeFont()?HudSettings::bodyFont:nullptr;
			ImGui_ImplDX12_CreateDeviceObjects();

			// Load settings icon from D2RHUD_Images/ (default gear.png)
			{
				std::string imagesDir = GetD2RHUDImagesDir();
				std::string gearPath = (std::filesystem::path(imagesDir) / "Theme.png").string();
				LoadGearTextureFromPath(pD3DDevice, gearPath);
			}
			// Load Camera.png for the "use image" button on each color row
			{
				std::string imagesDir = GetD2RHUDImagesDir();
				std::string cameraPath = (std::filesystem::path(imagesDir) / "Camera.png").string();
				LoadCameraButtonTextureFromPath(pD3DDevice, cameraPath);
			}

			g_Initialized = true;

			pD3DDevice.Release();
		}

		// Pending settings icon reload (user chose a different image in Settings)
		if (g_Initialized && !s_PendingGearFilename.empty()) {
			ID3D12Device* pDev = nullptr;
			if (SUCCEEDED(pSwapChain->GetDevice(__uuidof(ID3D12Device), (void**)&pDev))) {
				std::string path = (std::filesystem::path(GetD2RHUDImagesDir()) / s_PendingGearFilename).string();
				LoadGearTextureFromPath(pDev, path);
				pDev->Release();
				s_PendingGearFilename.clear();
			}
		}
		if (g_Initialized) ProcessThemeImageRequests(pSwapChain);

        if(g_Initialized && Graphics().srv && Spectate::Video::NeedsRender()){
            CComPtr<ID3D12Device> device;
            if(SUCCEEDED(pSwapChain->GetDevice(IID_PPV_ARGS(&device)))){
                const auto stride=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                const auto index=g_FrameBufferCount+20+kBuffAtlasSlots+1+kInspectSlots;
                auto cpu=Graphics().srv->GetCPUDescriptorHandleForHeapStart();cpu.ptr+=size_t(index)*stride;
                auto gpu=Graphics().srv->GetGPUDescriptorHandleForHeapStart();gpu.ptr+=uint64_t(index)*stride;
                Spectate::Video::Render(device,queue,Graphics().frames[pSwapChain->GetCurrentBackBufferIndex()].main_render_target_resource,cpu,gpu,stride);
            }
        }

		ImGui_ImplWin32_NewFrame();
		ImGui_ImplDX12_NewFrame();
		ImGui::NewFrame();

		g_PluginManager->Present();
		LegacyTransmog::DrawStackSplit();

		FrameContext& currentFrameContext = Graphics().frames[pSwapChain->GetCurrentBackBufferIndex()];
		currentFrameContext.command_allocator->Reset();

		D3D12_RESOURCE_BARRIER barrier;
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		barrier.Transition.pResource = currentFrameContext.main_render_target_resource;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
		Graphics().commands->Reset(currentFrameContext.command_allocator, nullptr);
		Graphics().commands->ResourceBarrier(1, &barrier);
		Graphics().commands->OMSetRenderTargets(1, &currentFrameContext.main_render_target_descriptor, FALSE, nullptr);
		Graphics().commands->SetDescriptorHeaps(1, &Graphics().srv);
		ImGui::Render();
		HudWindowLayout::Tick();
		ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), Graphics().commands);
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
		Graphics().commands->ResourceBarrier(1, &barrier);
		Graphics().commands->Close();

		queue->ExecuteCommandLists(1, (ID3D12CommandList**)&Graphics().commands);
        Spectate::Video::Presented(queue);
		return;
	}

    long __fastcall HookPresent(IDXGISwapChain3* pSwapChain, UINT SyncInterval, UINT Flags) {
        HudHitches::Present capture(pSwapChain,(Flags&DXGI_PRESENT_TEST)!=0,Window);
        if (g_HooksReady.load(std::memory_order_acquire) && !(Flags & DXGI_PRESENT_TEST)) {
            std::unique_lock<std::recursive_mutex> renderLock(g_RenderMutex,std::defer_lock);
            {HudTelemetry::Scope measured(HudTelemetry::Feature::RenderLock);renderLock.lock();}
            CComPtr<ID3D12CommandQueue> queue;
            if (g_UseSwapchainQueue) {
                // Do not render another swapchain using the current chain's backbuffers.
                uintptr_t table = 0;
                std::array<uintptr_t, 18> methods{};
                const bool compatible = (!g_RenderSwapchain || g_RenderSwapchain == pSwapChain)
                    && WineQueue::Read(reinterpret_cast<uintptr_t>(pSwapChain), &table, sizeof(table))
                    && WineQueue::Read(table, methods.data(), sizeof(methods)) && methods == g_SwapchainMethods;
                const auto address = compatible
                    ? WineQueue::Resolve(reinterpret_cast<uintptr_t>(pSwapChain), g_WineQueueLayout) : 0;
                if (address) {
                    auto candidate = reinterpret_cast<ID3D12CommandQueue*>(address);
                    if (candidate->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT) {
                        queue = candidate; // retain only after the complete vtable has matched
                        g_RenderSwapchain = pSwapChain;
                    }
                }
            } else {
                std::lock_guard<std::mutex> queueLock(g_QueueMutex);
                queue = Graphics().queue;
            }
            if (queue){HudTelemetry::Scope measured(HudTelemetry::Feature::PresentOverlay);DrawPresent(pSwapChain, queue);}
        }
        // Forward outside our locks. The driver may wait for another game thread.
        capture.BeforeDriver();
		HudTelemetry::Scope measured(HudTelemetry::Feature::PresentWait);
		return OriginalPresent(pSwapChain, SyncInterval, Flags);
	}

	void HookExecuteCommandLists(ID3D12CommandQueue* queue, UINT NumCommandLists, ID3D12CommandList* const* ppCommandLists) {
        {
            std::lock_guard<std::mutex> queueLock(g_QueueMutex);
		if (!Graphics().queue && queue->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT) {
			Graphics().queue = queue;
            }
		}

		OriginalExecuteCommandLists(queue, NumCommandLists, ppCommandLists);
	}

	void ResetState() {
        Spectate::Video::Reset();
        ThemeImages::Runtime().ResetResources();
		if (g_Initialized) {
			g_Initialized = false;
			HudWindowLayout::Shutdown();
			ImGui_ImplWin32_Shutdown();
			ImGui_ImplDX12_Shutdown();
		}
        {
            std::lock_guard<std::mutex> queueLock(g_QueueMutex);
            Graphics().queue.Release();
        }
        g_RenderSwapchain = nullptr;
		Graphics().frames.clear();
		Graphics().commands = nullptr;
		Graphics().rtv = nullptr;
		Graphics().srv = nullptr;
        Graphics().atlases.clear();
        Graphics().quantity={};Graphics().quantitySprite.reset();
        Graphics().inspectTextures.clear();Graphics().inspectAssets.reset();Graphics().inspectRetirement.Release();Graphics().inspectFrame=-1;Graphics().inspectUploads=0;
		Graphics().gear = nullptr;
		g_GearTextureGPUHandle = {};
		g_GearTextureWidth = 0;
		g_GearTextureHeight = 0;
		for (int wi = 0; wi < kWindowBgSlots; ++wi) {
			Graphics().window[wi] = nullptr;
			g_WindowBgTextureGPUHandle[wi] = {};
		}
		for (int fi = 0; fi < kFrameBgSlots; ++fi) {
			Graphics().frame[fi] = nullptr;
			g_FrameBgTextureGPUHandle[fi] = {};
		}
		for (int ci = 0; ci < kCheckboxSlots; ++ci) {
			Graphics().checkbox[ci] = nullptr;
			g_CheckboxTextureGPUHandle[ci] = {};
		}
		for (int bi = 0; bi < kButtonSlots; ++bi) {
			Graphics().button[bi] = nullptr;
			g_ButtonTextureGPUHandle[bi] = {};
		}
		Graphics().camera = nullptr;
		g_CameraButtonTextureGPUHandle = {};
		g_CameraButtonTextureWidth = 0;
		g_CameraButtonTextureHeight = 0;
	}

	long HookResizeBuffers(IDXGISwapChain3* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags) {
        std::lock_guard<std::recursive_mutex> renderLock(g_RenderMutex);
		ResetState();
		return OriginalResizeBuffers(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);
	}

	Status Init() {
        g_WineMacOS = WineQueue::IsMacOS();
		WNDCLASSEX windowClass;
		windowClass.cbSize = sizeof(WNDCLASSEX);
		windowClass.style = CS_HREDRAW | CS_VREDRAW;
		windowClass.lpfnWndProc = DefWindowProc;
		windowClass.cbClsExtra = 0;
		windowClass.cbWndExtra = 0;
		windowClass.hInstance = GetModuleHandle(NULL);
		windowClass.hIcon = NULL;
		windowClass.hCursor = NULL;
		windowClass.hbrBackground = NULL;
		windowClass.lpszMenuName = NULL;
		windowClass.lpszClassName = L"Fake Window";
		windowClass.hIconSm = NULL;

		::RegisterClassEx(&windowClass);

		HWND window = ::CreateWindow(windowClass.lpszClassName, L"Fake DirectX Window", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, NULL, NULL, windowClass.hInstance, NULL);

		HMODULE libDXGI;
		HMODULE libD3D12;

		if ((libDXGI = ::GetModuleHandle(L"dxgi.dll")) == NULL) {
			::DestroyWindow(window);
			::UnregisterClass(windowClass.lpszClassName, windowClass.hInstance);
			return Status::ModuleNotFoundError;
		}

		if ((libD3D12 = ::GetModuleHandle(L"d3d12.dll")) == NULL) {
			::DestroyWindow(window);
			::UnregisterClass(windowClass.lpszClassName, windowClass.hInstance);
			return Status::ModuleNotFoundError;
		}

		void* CreateDXGIFactory;
		if ((CreateDXGIFactory = ::GetProcAddress(libDXGI, "CreateDXGIFactory")) == NULL) {
			::DestroyWindow(window);
			::UnregisterClass(windowClass.lpszClassName, windowClass.hInstance);
			return Status::UnknownError;
		}

		CComPtr<IDXGIFactory> factory;
		if (((long(__stdcall*)(const IID&, void**))(CreateDXGIFactory))(__uuidof(IDXGIFactory), (void**)&factory) < 0) {
			::DestroyWindow(window);
			::UnregisterClass(windowClass.lpszClassName, windowClass.hInstance);
			return Status::UnknownError;
		}

		CComPtr<IDXGIAdapter> adapter;
		if (factory->EnumAdapters(0, &adapter) == DXGI_ERROR_NOT_FOUND) {
			::DestroyWindow(window);
			::UnregisterClass(windowClass.lpszClassName, windowClass.hInstance);
			return Status::UnknownError;
		}

		void* D3D12CreateDevice;
		if ((D3D12CreateDevice = ::GetProcAddress(libD3D12, "D3D12CreateDevice")) == NULL) {
			::DestroyWindow(window);
			::UnregisterClass(windowClass.lpszClassName, windowClass.hInstance);
			return Status::UnknownError;
		}

		CComPtr<ID3D12Device> device;
		if (((long(__stdcall*)(IUnknown*, D3D_FEATURE_LEVEL, const IID&, void**))(D3D12CreateDevice))(adapter, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), (void**)&device) < 0) {
			::DestroyWindow(window);
			::UnregisterClass(windowClass.lpszClassName, windowClass.hInstance);
			return Status::UnknownError;
		}

		D3D12_COMMAND_QUEUE_DESC queueDesc;
		queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		queueDesc.Priority = 0;
		queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		queueDesc.NodeMask = 0;

		CComPtr<ID3D12CommandQueue> commandQueue;
		if (device->CreateCommandQueue(&queueDesc, __uuidof(ID3D12CommandQueue), (void**)&commandQueue) < 0) {
			::DestroyWindow(window);
			::UnregisterClass(windowClass.lpszClassName, windowClass.hInstance);
			return Status::UnknownError;
		}

		CComPtr<ID3D12CommandAllocator> commandAllocator;
		if (device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void**)&commandAllocator) < 0) {
			::DestroyWindow(window);
			::UnregisterClass(windowClass.lpszClassName, windowClass.hInstance);
			return Status::UnknownError;
		}

		CComPtr<ID3D12GraphicsCommandList> commandList;
		if (device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator, NULL, __uuidof(ID3D12GraphicsCommandList), (void**)&commandList) < 0) {
			::DestroyWindow(window);
			::UnregisterClass(windowClass.lpszClassName, windowClass.hInstance);
			return Status::UnknownError;
		}

		DXGI_RATIONAL refreshRate;
		refreshRate.Numerator = 60;
		refreshRate.Denominator = 1;

		DXGI_MODE_DESC bufferDesc;
		bufferDesc.Width = 100;
		bufferDesc.Height = 100;
		bufferDesc.RefreshRate = refreshRate;
		bufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		bufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
		bufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;

		DXGI_SAMPLE_DESC sampleDesc;
		sampleDesc.Count = 1;
		sampleDesc.Quality = 0;

		DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
		swapChainDesc.BufferDesc = bufferDesc;
		swapChainDesc.SampleDesc = sampleDesc;
		swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		swapChainDesc.BufferCount = 2;
		swapChainDesc.OutputWindow = window;
		swapChainDesc.Windowed = 1;
		swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

		CComPtr<IDXGISwapChain> swapChain;
		if (factory->CreateSwapChain(commandQueue, &swapChainDesc, &swapChain) < 0) {
			::DestroyWindow(window);
			::UnregisterClass(windowClass.lpszClassName, windowClass.hInstance);
			return Status::UnknownError;
		}

		g_MethodsTable = (uint64_t*)::calloc(150, sizeof(uint64_t));
		::memcpy(g_MethodsTable, *(uint64_t**)(void*)device, 44 * sizeof(uint64_t));
		::memcpy(g_MethodsTable + 44, *(uint64_t**)(void*)commandQueue, 19 * sizeof(uint64_t));
		::memcpy(g_MethodsTable + 44 + 19, *(uint64_t**)(void*)commandAllocator, 9 * sizeof(uint64_t));
		::memcpy(g_MethodsTable + 44 + 19 + 9, *(uint64_t**)(void*)commandList, 60 * sizeof(uint64_t));
		::memcpy(g_MethodsTable + 44 + 19 + 9 + 60, *(uint64_t**)(void*)swapChain, 18 * sizeof(uint64_t));

        if (g_WineMacOS) {
            // A second live queue and swapchain rule out a single incidental pointer.
            // Inspired by REFramework PR #1847's wrapped-pointer adjustment; this
            // calibration performs read-only checks instead of calling arbitrary COM objects.
            std::copy_n(g_MethodsTable + 132, g_SwapchainMethods.size(), g_SwapchainMethods.begin());
            HWND secondWindow = ::CreateWindow(windowClass.lpszClassName, L"Queue calibration",
                WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, NULL, NULL, windowClass.hInstance, NULL);
            CComPtr<ID3D12CommandQueue> secondQueue;
            CComPtr<IDXGISwapChain> secondSwapchain;
            auto secondDesc = swapChainDesc;
            secondDesc.OutputWindow = secondWindow;
            if (secondWindow
                && SUCCEEDED(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&secondQueue)))
                && SUCCEEDED(factory->CreateSwapChain(secondQueue, &secondDesc, &secondSwapchain))) {
                WineQueue::Methods firstMethods{}, secondMethods{};
                if (WineQueue::ReadMethods(reinterpret_cast<uintptr_t>(commandQueue.p), firstMethods)
                    && WineQueue::ReadMethods(reinterpret_cast<uintptr_t>(secondQueue.p), secondMethods)
                    && firstMethods == secondMethods) {
                    const auto first = WineQueue::Capture(reinterpret_cast<uintptr_t>(swapChain.p), reinterpret_cast<uintptr_t>(commandQueue.p));
                    const auto second = WineQueue::Capture(reinterpret_cast<uintptr_t>(secondSwapchain.p), reinterpret_cast<uintptr_t>(secondQueue.p));
                    g_WineQueueLayout = WineQueue::Calibrate(first, second, firstMethods);
                    // Validate the actual reader on both known objects, too.
                    if (WineQueue::Resolve(reinterpret_cast<uintptr_t>(swapChain.p), g_WineQueueLayout) != reinterpret_cast<uintptr_t>(commandQueue.p)
                        || WineQueue::Resolve(reinterpret_cast<uintptr_t>(secondSwapchain.p), g_WineQueueLayout) != reinterpret_cast<uintptr_t>(secondQueue.p))
                        g_WineQueueLayout.valid = false;
                }
            }
            secondSwapchain.Release();
            secondQueue.Release();
            if (secondWindow) {
                ::DestroyWindow(secondWindow);
            }
        }

        // Preserve window-before-swapchain teardown on D3DMetal.
        // The local COM pointers then release in reverse order.
		::DestroyWindow(window);
		::UnregisterClass(windowClass.lpszClassName, windowClass.hInstance);
		return Status::Success;
	}

	Status Hook(uint16_t _index, void** _original, void* _function) {
		void* target = (void*)g_MethodsTable[_index];
#ifdef USE_DETOURS
		* _original = target;
        if (DetourTransactionBegin() != NO_ERROR)
            return Status::UnknownError;
        if (DetourUpdateThread(GetCurrentThread()) != NO_ERROR
            || DetourAttach(&(PVOID&)*_original, _function) != NO_ERROR) {
            DetourTransactionAbort();
            return Status::UnknownError;
        }
        if (DetourTransactionCommit() != NO_ERROR)
            return Status::UnknownError;
#endif
#ifdef USE_MINHOOK
		if (MH_CreateHook(target, _function, _original) != MH_OK || MH_EnableHook(target) != MH_OK) {
			return Status::UnknownError;
		}
#endif
		return Status::Success;
	}

	Status Unhook(uint16_t _index, void** _original, void* _function) {
		void* target = (void*)g_MethodsTable[_index];
#ifdef USE_DETOURS
		DetourTransactionBegin();
		DetourUpdateThread(GetCurrentThread());
		DetourDetach(&(PVOID&)*_original, _function);
		DetourTransactionCommit();
#endif
#ifdef USE_MINHOOK
		MH_DisableHook(target);
#endif
		return Status::Success;
	}

	LRESULT APIENTRY WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        if(g_Initialized && Spectate::WindowInput(hWnd,msg,wParam,lParam)){
            ImGui_ImplWin32_WndProcHandler(hWnd,msg,wParam,lParam);
            return msg==WM_INPUT?DefWindowProcW(hWnd,msg,wParam,lParam):0;
        }
        if(HudCommands::HandleMessage(hWnd,msg,wParam,lParam))return 0;
        if(Marketplace::Panel::StashSyncWindowInput(msg,wParam,lParam))return msg==WM_INPUT?DefWindowProcW(hWnd,msg,wParam,lParam):0;
        if(Marketplace::Panel::NativeWindowInput(msg,wParam,lParam))return msg==WM_INPUT?DefWindowProcW(hWnd,msg,wParam,lParam):0;
		bool splitOutsideClick=false;
		if(LegacyTransmog::SplitWindowMessage(hWnd,msg,wParam,lParam,&splitOutsideClick))return 0;
		if (g_Initialized && g_PluginManager)
		{
			ImGuiIO& io = ImGui::GetIO();
			if(splitOutsideClick){
				ImGui_ImplWin32_WndProcHandler(hWnd,msg,wParam,lParam);
				g_PluginManager->WndProc(hWnd,msg,wParam,lParam);
				return CallWindowProc(OriginalWndProc,hWnd,msg,wParam,lParam);
			}
			if(LegacyTransmog::SplitCapturesInput(msg,lParam)){
				ImGui_ImplWin32_WndProcHandler(hWnd,msg,wParam,lParam);
				if(msg==WM_INPUT)return DefWindowProcW(hWnd,msg,wParam,lParam);
				return 0;
			}
            if(HUDControls::HandleInput(msg,wParam,lParam,io.WantTextInput))return 0;
            QuestJournal::ClientUI::BeginInput(msg==WM_KEYDOWN && (lParam&(LPARAM(1)<<30))!=0);
            g_PluginManager->WndProc(hWnd, msg, wParam, lParam);
            if(QuestJournal::ClientUI::ConsumeInput()){
                ImGui_ImplWin32_WndProcHandler(hWnd,msg,wParam,lParam);
                return 0;
            }
			switch (msg) {
			case WM_LBUTTONDBLCLK:
			case WM_LBUTTONDOWN:
			case WM_LBUTTONUP:
			case WM_RBUTTONDBLCLK:
			case WM_RBUTTONDOWN:
			case WM_RBUTTONUP:
			case WM_MBUTTONDBLCLK:
			case WM_MBUTTONDOWN:
			case WM_MBUTTONUP:
			case WM_MOUSEWHEEL:
            case WM_MOUSEHWHEEL:
			case WM_MOUSEMOVE:
				ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
				if (!io.WantCaptureMouse) {
					if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK) D3Whirlwind::MouseButtonDown(1);
					if (msg == WM_RBUTTONDOWN || msg == WM_RBUTTONDBLCLK) D3Whirlwind::MouseButtonDown(2);
				}
				return io.WantCaptureMouse ? 0 : CallWindowProc(OriginalWndProc, hWnd, msg, wParam, lParam);
			case WM_KEYDOWN:
				if (wParam == VK_ESCAPE && g_PluginManager->TryCloseMenuOnEscape())
					return 0;
				// fall through
			case WM_KEYUP:
			case WM_CHAR:
				ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
				return io.WantCaptureKeyboard || io.WantTextInput ? 0 : CallWindowProc(OriginalWndProc, hWnd, msg, wParam, lParam);
			default:
				break;
			}
		}
		return CallWindowProc(OriginalWndProc, hWnd, msg, wParam, lParam);
	}

	Status InstallHooks() {
        if (!g_MethodsTable) return Status::NotInitializedError;
#ifdef USE_DETOURS
		DetourRestoreAfterWith();
#endif
#ifdef USE_MINHOOK
		MH_Initialize();
#endif
		g_PluginManager = new PluginManager();
        g_UseSwapchainQueue = g_WineMacOS && g_WineQueueLayout.valid;
        if (!g_UseSwapchainQueue) {
            MEMORY_BASIC_INFORMATION memory{};
            const bool nativeMacTarget = g_WineMacOS
                && (!VirtualQuery(reinterpret_cast<void*>(g_MethodsTable[54]), &memory, sizeof(memory)) || memory.State != MEM_COMMIT);
            if (nativeMacTarget) {
                return Status::NotSupportedError;
            }
            g_QueueHookInstalled = Hook(54, (void**)&OriginalExecuteCommandLists, HookExecuteCommandLists) == Status::Success;
            if (!g_QueueHookInstalled) return Status::UnknownError;
        }
        g_PresentHookInstalled = Hook(140, (void**)&OriginalPresent, HookPresent) == Status::Success;
        if (!g_PresentHookInstalled) return Status::UnknownError;
        g_ResizeHookInstalled = Hook(145, (void**)&OriginalResizeBuffers, HookResizeBuffers) == Status::Success;
        if (!g_ResizeHookInstalled) return Status::UnknownError;
        // Publish only after callbacks' shared state and all required hooks exist.
        g_HooksReady.store(true, std::memory_order_release);
		return Status::Success;
	}

	Status RemoveHooks() {
        HudCommands::Shutdown();
        g_HooksReady.store(false, std::memory_order_release);
        if (g_QueueHookInstalled) Unhook(54, (void**)&OriginalExecuteCommandLists, HookExecuteCommandLists);
        if (g_PresentHookInstalled) Unhook(140, (void**)&OriginalPresent, HookPresent);
        if (g_ResizeHookInstalled) Unhook(145, (void**)&OriginalResizeBuffers, HookResizeBuffers);

		if (Window && OriginalWndProc) {
			SetWindowLongPtr(Window, GWLP_WNDPROC, (__int3264)(LONG_PTR)OriginalWndProc);
		}

		g_PluginManager = nullptr;
        CubeMap::SetFontProvider(nullptr);g_MapEventFonts.fonts.fill(nullptr);
		ResetState();
		ImGui::DestroyContext();

#ifdef USE_MINHOOK
		MH_Uninitialize();
#endif
		//wait for hooks to finish if in one. maybe not needed, but seemed more stable after adding it.
		Sleep(1000);
		return Status::Success;
	}

    static uint64_t GetMapEventArtwork(){
        static BuffIcons::SpriteAtlas atlas;
        if(atlas.pixels.empty() && g_DllModule){
            const auto resource=FindResourceW(g_DllModule,MAKEINTRESOURCEW(IDR_MAP_EVENT_ART),RT_RCDATA);
            const auto data=resource ? LoadResource(g_DllModule,resource) : nullptr;
            const auto bytes=data ? static_cast<const unsigned char*>(LockResource(data)) : nullptr;
            const auto size=resource ? SizeofResource(g_DllModule,resource) : 0;
            int channels=0;
            if(bytes && size)if(auto pixels=stbi_load_from_memory(bytes,int(size),&atlas.width,&atlas.height,&channels,4)){
                atlas.frames=1;atlas.frameWidth=atlas.width;atlas.pixels.assign(pixels,pixels+size_t(atlas.width)*atlas.height*4);stbi_image_free(pixels);
            }
        }
        return GetBuffIconAtlas("__cube_map_event_art",atlas);
    }
	void SetDllModule(HMODULE hModule) {
		g_DllModule = hModule;
        CubeMap::SetArtworkProvider(GetMapEventArtwork);
        Inspect::SetArtwork(GetInspectArtwork);
        Inspect::SetFont(GetMapEventFont);
        LegacyTransmog::Skin::SetProviders([](LegacyTransmog::Skin::Part part){
            LegacyTransmog::Skin::Image image;auto assets=LegacyTransmog::Skin::Assets();if(!assets)return image;
            const auto& sprite=(*assets)[part];const auto& atlas=sprite.atlas;
            image={GetBuffIconAtlas("__quantity_ui_"+std::to_string(int(part)),atlas),atlas.width,atlas.height,atlas.frameWidth,atlas.frames,sprite.pitch};return image;
        },GetMapEventFont,GetQuantityItemArtwork);
	}

	uint64_t GetGearTextureId() {
		return (uint64_t)g_GearTextureGPUHandle.ptr;
	}

	void GetGearTextureSize(int* outWidth, int* outHeight) {
		if (outWidth) *outWidth = g_GearTextureWidth;
		if (outHeight) *outHeight = g_GearTextureHeight;
	}

	std::string GetD2RHUDImagesPath() {
		return GetD2RHUDImagesDir();
	}

	void RequestGearIconReload(const std::string& filename) {
		if (!filename.empty())
			s_PendingGearFilename = filename;
	}

	uint64_t GetWindowBgTextureId(int slot) {
		if (slot < 0 || slot >= kWindowBgSlots) return 0;
		return (uint64_t)g_WindowBgTextureGPUHandle[slot].ptr;
	}

	void RequestWindowBgImageReload(int slot, const std::string& filename) {
        if (slot < 0 || slot >= kWindowBgSlots) return;
        std::lock_guard<std::recursive_mutex> renderLock(g_RenderMutex);
        ThemeImages::Runtime().Set(0 + size_t(slot), filename);
	}

	uint64_t GetFrameBgTextureId(int slot) {
		if (slot < 0 || slot >= kFrameBgSlots) return 0;
		return (uint64_t)g_FrameBgTextureGPUHandle[slot].ptr;
	}

	void RequestFrameBgImageReload(int slot, const std::string& filename) {
        if (slot < 0 || slot >= kFrameBgSlots) return;
        std::lock_guard<std::recursive_mutex> renderLock(g_RenderMutex);
        ThemeImages::Runtime().Set(8 + size_t(slot), filename);
	}

	uint64_t GetCheckboxTextureId(int slot) {
		if (slot < 0 || slot >= kCheckboxSlots) return 0;
		return (uint64_t)g_CheckboxTextureGPUHandle[slot].ptr;
	}

	void RequestCheckboxImageReload(int slot, const std::string& filename) {
        if (slot < 0 || slot >= kCheckboxSlots) return;
        std::lock_guard<std::recursive_mutex> renderLock(g_RenderMutex);
        ThemeImages::Runtime().Set(11 + size_t(slot), filename);
	}

	uint64_t GetButtonTextureId(int slot) {
		if (slot < 0 || slot >= kButtonSlots) return 0;
		return (uint64_t)g_ButtonTextureGPUHandle[slot].ptr;
	}

	void RequestButtonImageReload(int slot, const std::string& filename) {
        if (slot < 0 || slot >= kButtonSlots) return;
        std::lock_guard<std::recursive_mutex> renderLock(g_RenderMutex);
        ThemeImages::Runtime().Set(14 + size_t(slot), filename);
	}

	uint64_t GetCameraButtonTextureId() {
		return (uint64_t)g_CameraButtonTextureGPUHandle.ptr;
	}

	void GetCameraButtonTextureSize(int* outWidth, int* outHeight) {
		if (outWidth) *outWidth = g_CameraButtonTextureWidth;
		if (outHeight) *outHeight = g_CameraButtonTextureHeight;
	}

	std::string GetD2RHUDFontsPath()
	{
		return GetD2RHUDFontsDir();
	}

	ImFont* GetFloatingDamageFont(int index)
	{
		if (index < 0 || index >= kFloatingDamageFontCount)
			return nullptr;
		return g_BundledFloatingDamageFonts[index];
	}

}
