#include "D3D12Hook.h"
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
	static std::vector<FrameContext> g_FrameContext;
	static UINT						g_FrameBufferCount = 0;

	static CComPtr<ID3D12DescriptorHeap> g_pD3DRtvDescHeap = NULL;
	static CComPtr<ID3D12DescriptorHeap> g_pD3DSrvDescHeap = NULL;
	static CComPtr<ID3D12CommandQueue> g_pD3DCommandQueue = NULL;
	static CComPtr<ID3D12GraphicsCommandList> g_pD3DCommandList = NULL;

	static HMODULE g_DllModule = NULL;
	static CComPtr<ID3D12Resource> g_GearTexture = NULL;
	static D3D12_GPU_DESCRIPTOR_HANDLE g_GearTextureGPUHandle = {};
	static int g_GearTextureWidth = 0;
	static int g_GearTextureHeight = 0;
	static std::string s_PendingGearFilename;  // non-empty = reload this file next frame

	static const int kWindowBgSlots = 8;
	static CComPtr<ID3D12Resource> g_WindowBgTexture[kWindowBgSlots] = {};
	static D3D12_GPU_DESCRIPTOR_HANDLE g_WindowBgTextureGPUHandle[kWindowBgSlots] = {};
	static std::string s_PendingWindowBgFilename[kWindowBgSlots];
	static bool s_ClearWindowBg[kWindowBgSlots] = {};

	static const int kFrameBgSlots = 3;
	static const int kCheckboxSlots = 3;
	static const int kButtonSlots = 3;
	static CComPtr<ID3D12Resource> g_FrameBgTexture[kFrameBgSlots] = {};
	static D3D12_GPU_DESCRIPTOR_HANDLE g_FrameBgTextureGPUHandle[kFrameBgSlots] = {};
	static std::string s_PendingFrameBgFilename[kFrameBgSlots];
	static bool s_ClearFrameBg[kFrameBgSlots] = {};

	static CComPtr<ID3D12Resource> g_CheckboxTexture[kCheckboxSlots] = {};
	static D3D12_GPU_DESCRIPTOR_HANDLE g_CheckboxTextureGPUHandle[kCheckboxSlots] = {};
	static std::string s_PendingCheckboxFilename[kCheckboxSlots];
	static bool s_ClearCheckbox[kCheckboxSlots] = {};

	static CComPtr<ID3D12Resource> g_ButtonTexture[kButtonSlots] = {};
	static D3D12_GPU_DESCRIPTOR_HANDLE g_ButtonTextureGPUHandle[kButtonSlots] = {};
	static std::string s_PendingButtonFilename[kButtonSlots];
	static bool s_ClearButton[kButtonSlots] = {};

	static CComPtr<ID3D12Resource> g_CameraButtonTexture = NULL;
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

	static bool LoadGearTextureFromPath(ID3D12Device* pD3DDevice, const std::string& fullPath)
	{
		if (!pD3DDevice || fullPath.empty() || !std::filesystem::exists(fullPath)) return false;
		int texW = 0, texH = 0, texCh = 0;
		unsigned char* pixels = stbi_load(fullPath.c_str(), &texW, &texH, &texCh, 4);
		if (!pixels || texW <= 0 || texH <= 0) return false;
		g_GearTexture = NULL;
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
		if (FAILED(pD3DDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &texDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&g_GearTexture)))) {
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
		dstLoc.pResource = g_GearTexture;
		dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		dstLoc.SubresourceIndex = 0;
		uploadList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);
		D3D12_RESOURCE_BARRIER barrier = {};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		barrier.Transition.pResource = g_GearTexture;
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
				WaitForSingleObject(evt, 5000);
			}
			if (evt) CloseHandle(evt);
		}
		const UINT srvDescSize = pD3DDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		D3D12_CPU_DESCRIPTOR_HANDLE srvCpu = g_pD3DSrvDescHeap->GetCPUDescriptorHandleForHeapStart();
		D3D12_GPU_DESCRIPTOR_HANDLE srvGpu = g_pD3DSrvDescHeap->GetGPUDescriptorHandleForHeapStart();
		srvCpu.ptr += (SIZE_T)srvDescSize * (SIZE_T)g_FrameBufferCount;
		srvGpu.ptr += (SIZE_T)srvDescSize * (SIZE_T)g_FrameBufferCount;
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Texture2D.MipLevels = 1;
		pD3DDevice->CreateShaderResourceView(g_GearTexture, &srvDesc, srvCpu);
		g_GearTextureGPUHandle = srvGpu;
		g_GearTextureWidth = texW;
		g_GearTextureHeight = texH;
		return true;
	}

	static bool LoadThemeTextureFromPath(ID3D12Device* pD3DDevice, const std::string& fullPath,
		CComPtr<ID3D12Resource>& outTex, D3D12_GPU_DESCRIPTOR_HANDLE& outGpuHandle, UINT descriptorOffset)
	{
		if (!pD3DDevice || fullPath.empty() || !std::filesystem::exists(fullPath)) return false;
		int texW = 0, texH = 0, texCh = 0;
		unsigned char* pixels = stbi_load(fullPath.c_str(), &texW, &texH, &texCh, 4);
		if (!pixels || texW <= 0 || texH <= 0) return false;
		outTex = NULL;
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
		if (FAILED(pD3DDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &texDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&outTex)))) {
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
		dstLoc.pResource = outTex;
		dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		dstLoc.SubresourceIndex = 0;
		uploadList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);
		D3D12_RESOURCE_BARRIER barrier = {};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		barrier.Transition.pResource = outTex;
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
				WaitForSingleObject(evt, 5000);
			}
			if (evt) CloseHandle(evt);
		}
		const UINT srvDescSize = pD3DDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		D3D12_CPU_DESCRIPTOR_HANDLE srvCpu = g_pD3DSrvDescHeap->GetCPUDescriptorHandleForHeapStart();
		D3D12_GPU_DESCRIPTOR_HANDLE srvGpu = g_pD3DSrvDescHeap->GetGPUDescriptorHandleForHeapStart();
		srvCpu.ptr += (SIZE_T)srvDescSize * (SIZE_T)descriptorOffset;
		srvGpu.ptr += (SIZE_T)srvDescSize * (SIZE_T)descriptorOffset;
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Texture2D.MipLevels = 1;
		pD3DDevice->CreateShaderResourceView(outTex, &srvDesc, srvCpu);
		outGpuHandle = srvGpu;
		return true;
	}

	static bool LoadCameraButtonTextureFromPath(ID3D12Device* pD3DDevice, const std::string& fullPath)
	{
		if (!pD3DDevice || fullPath.empty() || !std::filesystem::exists(fullPath)) return false;
		int texW = 0, texH = 0, texCh = 0;
		unsigned char* pixels = stbi_load(fullPath.c_str(), &texW, &texH, &texCh, 4);
		if (!pixels || texW <= 0 || texH <= 0) return false;
		g_CameraButtonTexture = NULL;
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
		if (FAILED(pD3DDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &texDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&g_CameraButtonTexture)))) {
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
		dstLoc.pResource = g_CameraButtonTexture;
		dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		dstLoc.SubresourceIndex = 0;
		uploadList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);
		D3D12_RESOURCE_BARRIER barrier = {};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		barrier.Transition.pResource = g_CameraButtonTexture;
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
				WaitForSingleObject(evt, 5000);
			}
			if (evt) CloseHandle(evt);
		}
		const UINT srvDescSize = pD3DDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		D3D12_CPU_DESCRIPTOR_HANDLE srvCpu = g_pD3DSrvDescHeap->GetCPUDescriptorHandleForHeapStart();
		D3D12_GPU_DESCRIPTOR_HANDLE srvGpu = g_pD3DSrvDescHeap->GetGPUDescriptorHandleForHeapStart();
		srvCpu.ptr += (SIZE_T)srvDescSize * (SIZE_T)(g_FrameBufferCount + 10);
		srvGpu.ptr += (SIZE_T)srvDescSize * (SIZE_T)(g_FrameBufferCount + 10);
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Texture2D.MipLevels = 1;
		pD3DDevice->CreateShaderResourceView(g_CameraButtonTexture, &srvDesc, srvCpu);
		g_CameraButtonTextureGPUHandle = srvGpu;
		g_CameraButtonTextureWidth = texW;
		g_CameraButtonTextureHeight = texH;
		return true;
	}

	LRESULT APIENTRY WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

	typedef long(__fastcall* Present) (IDXGISwapChain3* pSwapChain, UINT SyncInterval, UINT Flags);
	static Present OriginalPresent;

	typedef void(*ExecuteCommandLists)(ID3D12CommandQueue* queue, UINT NumCommandLists, ID3D12CommandList* ppCommandLists);
	static ExecuteCommandLists OriginalExecuteCommandLists;

	typedef long(__fastcall* ResizeBuffers)(IDXGISwapChain3* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags);
	static ResizeBuffers OriginalResizeBuffers;

	WNDPROC OriginalWndProc;
	HWND Window = nullptr;
	bool FontLoaded = false;

	static uint64_t* g_MethodsTable = NULL;
	static bool g_Initialized = false;


	static PluginManager* g_PluginManager;

	long __fastcall HookPresent(IDXGISwapChain3* pSwapChain, UINT SyncInterval, UINT Flags) {
		if (g_pD3DCommandQueue == nullptr) {
			return OriginalPresent(pSwapChain, SyncInterval, Flags);
		}
		if (!g_Initialized) {
			ID3D12Device* pD3DDevice;

			if (FAILED(pSwapChain->GetDevice(__uuidof(ID3D12Device), (void**)&pD3DDevice))) {
				return OriginalPresent(pSwapChain, SyncInterval, Flags);
			}

			{
				DXGI_SWAP_CHAIN_DESC desc;
				pSwapChain->GetDesc(&desc);
				Window = desc.OutputWindow;
				if (!OriginalWndProc) {
					OriginalWndProc = (WNDPROC)SetWindowLongPtr(Window, GWLP_WNDPROC, (__int3264)(LONG_PTR)WndProc);
				}
				g_FrameBufferCount = desc.BufferCount;
				g_FrameContext.clear();
				g_FrameContext.resize(g_FrameBufferCount);
			}

			{
				D3D12_DESCRIPTOR_HEAP_DESC desc = {};
				desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
				desc.NumDescriptors = (UINT)(g_FrameBufferCount + 17);  // gear, window bg x9, camera, frame0-2, button0-2
				desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

				if (pD3DDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&g_pD3DSrvDescHeap)) != S_OK) {
					return OriginalPresent(pSwapChain, SyncInterval, Flags);
				}
			}

			{
				D3D12_DESCRIPTOR_HEAP_DESC desc;
				desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
				desc.NumDescriptors = g_FrameBufferCount;
				desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
				desc.NodeMask = 1;

				if (pD3DDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&g_pD3DRtvDescHeap)) != S_OK) {
					return OriginalPresent(pSwapChain, SyncInterval, Flags);
				}

				const auto rtvDescriptorSize = pD3DDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
				D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = g_pD3DRtvDescHeap->GetCPUDescriptorHandleForHeapStart();

				for (UINT i = 0; i < g_FrameBufferCount; i++) {

					g_FrameContext[i].main_render_target_descriptor = rtvHandle;
					pSwapChain->GetBuffer(i, IID_PPV_ARGS(&g_FrameContext[i].main_render_target_resource));
					pD3DDevice->CreateRenderTargetView(g_FrameContext[i].main_render_target_resource, nullptr, rtvHandle);
					rtvHandle.ptr += rtvDescriptorSize;
				}

			}

			{
				ID3D12CommandAllocator* allocator;
				if (pD3DDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)) != S_OK) {
					return OriginalPresent(pSwapChain, SyncInterval, Flags);
				}

				for (size_t i = 0; i < g_FrameBufferCount; i++) {
					if (pD3DDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_FrameContext[i].command_allocator)) != S_OK) {
						return OriginalPresent(pSwapChain, SyncInterval, Flags);
					}
				}

				if (pD3DDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_FrameContext[0].command_allocator, NULL, IID_PPV_ARGS(&g_pD3DCommandList)) != S_OK || g_pD3DCommandList->Close() != S_OK) {
					return OriginalPresent(pSwapChain, SyncInterval, Flags);
				}
			}

			IMGUI_CHECKVERSION();
			ImGui::CreateContext();
			ImGuiIO& io = ImGui::GetIO();
			io.IniFilename = nullptr;
			//io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
			//io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

			// Setup Dear ImGui style
			ImGui::StyleColorsDark();
			//ImGui::StyleColorsClassic();

				// Setup Platform/Renderer backends
			ImGui_ImplWin32_Init(Window);
			ImGui_ImplDX12_Init(pD3DDevice, g_FrameBufferCount,
				DXGI_FORMAT_R8G8B8A8_UNORM, g_pD3DSrvDescHeap,
				g_pD3DSrvDescHeap->GetCPUDescriptorHandleForHeapStart(),
				g_pD3DSrvDescHeap->GetGPUDescriptorHandleForHeapStart());

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

			pD3DDevice->Release();


			//Add Default D2R font to drawtable
			namespace fs = std::filesystem;

			std::string fontPath = "Mods/" + modName + "/" + modName + ".mpq/data/hd/ui/fonts/exocetblizzardot-medium.otf";

			if (fs::exists(fontPath)) {
				std::cout << "Adding fonts from file..." << std::endl;
				const ImWchar* ranges = io.Fonts->GetGlyphRangesCyrillic();
				io.Fonts->AddFontFromFileTTF(fontPath.c_str(), 12.0f); //Add multiple font sizes
				io.Fonts->AddFontFromFileTTF(fontPath.c_str(), 15.0f);
				io.Fonts->AddFontFromFileTTF(fontPath.c_str(), 18.0f);
				io.Fonts->AddFontFromFileTTF(fontPath.c_str(), 24.0f);
				io.Fonts->AddFontFromFileTTF(fontPath.c_str(), 36.0f);
				io.Fonts->AddFontFromFileTTF(fontPath.c_str(), 48.0f);
				FontLoaded = true;
			}
			else {
				std::cerr << "Font file does not exist: " << fontPath << std::endl;
			}
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
		// Clear or reload window background images (per-window slots)
		for (int wi = 0; wi < kWindowBgSlots; ++wi) {
			if (g_Initialized && s_ClearWindowBg[wi]) {
				g_WindowBgTexture[wi] = nullptr;
				g_WindowBgTextureGPUHandle[wi] = {};
				s_ClearWindowBg[wi] = false;
			}
			if (g_Initialized && !s_PendingWindowBgFilename[wi].empty()) {
				ID3D12Device* pDev = nullptr;
				if (SUCCEEDED(pSwapChain->GetDevice(__uuidof(ID3D12Device), (void**)&pDev))) {
					std::string path = (std::filesystem::path(GetD2RHUDImagesDir()) / s_PendingWindowBgFilename[wi]).string();
					if (LoadThemeTextureFromPath(pDev, path, g_WindowBgTexture[wi], g_WindowBgTextureGPUHandle[wi], g_FrameBufferCount + 1 + wi))
						s_PendingWindowBgFilename[wi].clear();
					pDev->Release();
				}
			}
		}
		for (int fi = 0; fi < kFrameBgSlots; ++fi) {
			if (g_Initialized && s_ClearFrameBg[fi]) {
				g_FrameBgTexture[fi] = nullptr;
				g_FrameBgTextureGPUHandle[fi] = {};
				s_ClearFrameBg[fi] = false;
			}
			if (g_Initialized && !s_PendingFrameBgFilename[fi].empty()) {
				ID3D12Device* pDev = nullptr;
				if (SUCCEEDED(pSwapChain->GetDevice(__uuidof(ID3D12Device), (void**)&pDev))) {
					std::string path = (std::filesystem::path(GetD2RHUDImagesDir()) / s_PendingFrameBgFilename[fi]).string();
					if (LoadThemeTextureFromPath(pDev, path, g_FrameBgTexture[fi], g_FrameBgTextureGPUHandle[fi], g_FrameBufferCount + 11 + fi))
						s_PendingFrameBgFilename[fi].clear();
				}
			}
		}
		for (int ci = 0; ci < kCheckboxSlots; ++ci) {
			if (g_Initialized && s_ClearCheckbox[ci]) {
				g_CheckboxTexture[ci] = nullptr;
				g_CheckboxTextureGPUHandle[ci] = {};
				s_ClearCheckbox[ci] = false;
			}
			if (g_Initialized && !s_PendingCheckboxFilename[ci].empty()) {
				ID3D12Device* pDev = nullptr;
				if (SUCCEEDED(pSwapChain->GetDevice(__uuidof(ID3D12Device), (void**)&pDev))) {
					std::string path = (std::filesystem::path(GetD2RHUDImagesDir()) / s_PendingCheckboxFilename[ci]).string();
					if (LoadThemeTextureFromPath(pDev, path, g_CheckboxTexture[ci], g_CheckboxTextureGPUHandle[ci], g_FrameBufferCount + 14 + ci))
						s_PendingCheckboxFilename[ci].clear();
				}
			}
		}
		for (int bi = 0; bi < kButtonSlots; ++bi) {
			if (g_Initialized && s_ClearButton[bi]) {
				g_ButtonTexture[bi] = nullptr;
				g_ButtonTextureGPUHandle[bi] = {};
				s_ClearButton[bi] = false;
			}
			if (g_Initialized && !s_PendingButtonFilename[bi].empty()) {
				ID3D12Device* pDev = nullptr;
				if (SUCCEEDED(pSwapChain->GetDevice(__uuidof(ID3D12Device), (void**)&pDev))) {
					std::string path = (std::filesystem::path(GetD2RHUDImagesDir()) / s_PendingButtonFilename[bi]).string();
					if (LoadThemeTextureFromPath(pDev, path, g_ButtonTexture[bi], g_ButtonTextureGPUHandle[bi], g_FrameBufferCount + 17 + bi))
						s_PendingButtonFilename[bi].clear();
				}
			}
		}

		ImGui_ImplWin32_NewFrame();
		ImGui_ImplDX12_NewFrame();
		ImGui::NewFrame();

		g_PluginManager->Present();

		FrameContext& currentFrameContext = g_FrameContext[pSwapChain->GetCurrentBackBufferIndex()];
		currentFrameContext.command_allocator->Reset();

		D3D12_RESOURCE_BARRIER barrier;
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		barrier.Transition.pResource = currentFrameContext.main_render_target_resource;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
		g_pD3DCommandList->Reset(currentFrameContext.command_allocator, nullptr);
		g_pD3DCommandList->ResourceBarrier(1, &barrier);
		g_pD3DCommandList->OMSetRenderTargets(1, &currentFrameContext.main_render_target_descriptor, FALSE, nullptr);
		g_pD3DCommandList->SetDescriptorHeaps(1, &g_pD3DSrvDescHeap);
		ImGui::Render();
		ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_pD3DCommandList);
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
		g_pD3DCommandList->ResourceBarrier(1, &barrier);
		g_pD3DCommandList->Close();

		g_pD3DCommandQueue->ExecuteCommandLists(1, (ID3D12CommandList**)&g_pD3DCommandList);
		return OriginalPresent(pSwapChain, SyncInterval, Flags);
	}

	void HookExecuteCommandLists(ID3D12CommandQueue* queue, UINT NumCommandLists, ID3D12CommandList* ppCommandLists) {
		if (!g_pD3DCommandQueue && queue->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT) {
			g_pD3DCommandQueue = queue;
		}

		OriginalExecuteCommandLists(queue, NumCommandLists, ppCommandLists);
	}

	void ResetState() {
		if (g_Initialized) {
			g_Initialized = false;
			ImGui_ImplWin32_Shutdown();
			ImGui_ImplDX12_Shutdown();
		}
		g_pD3DCommandQueue = nullptr;
		g_FrameContext.clear();
		g_pD3DCommandList = nullptr;
		g_pD3DRtvDescHeap = nullptr;
		g_pD3DSrvDescHeap = nullptr;
		g_GearTexture = nullptr;
		g_GearTextureGPUHandle = {};
		g_GearTextureWidth = 0;
		g_GearTextureHeight = 0;
		for (int wi = 0; wi < kWindowBgSlots; ++wi) {
			g_WindowBgTexture[wi] = nullptr;
			g_WindowBgTextureGPUHandle[wi] = {};
		}
		for (int fi = 0; fi < kFrameBgSlots; ++fi) {
			g_FrameBgTexture[fi] = nullptr;
			g_FrameBgTextureGPUHandle[fi] = {};
		}
		for (int ci = 0; ci < kCheckboxSlots; ++ci) {
			g_CheckboxTexture[ci] = nullptr;
			g_CheckboxTextureGPUHandle[ci] = {};
		}
		for (int bi = 0; bi < kButtonSlots; ++bi) {
			g_ButtonTexture[bi] = nullptr;
			g_ButtonTextureGPUHandle[bi] = {};
		}
		g_CameraButtonTexture = nullptr;
		g_CameraButtonTextureGPUHandle = {};
		g_CameraButtonTextureWidth = 0;
		g_CameraButtonTextureHeight = 0;
	}

	long HookResizeBuffers(IDXGISwapChain3* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags) {
		ResetState();
		return OriginalResizeBuffers(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);
	}

	Status Init() {
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

		::DestroyWindow(window);
		::UnregisterClass(windowClass.lpszClassName, windowClass.hInstance);
		return Status::Success;
	}

	Status Hook(uint16_t _index, void** _original, void* _function) {
		void* target = (void*)g_MethodsTable[_index];
#ifdef USE_DETOURS
		* _original = target;
		DetourTransactionBegin();
		DetourUpdateThread(GetCurrentThread());
		DetourAttach(&(PVOID&)*_original, _function);
		DetourTransactionCommit();
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
		if (g_Initialized && g_PluginManager)
		{
			ImGuiIO& io = ImGui::GetIO();
			g_PluginManager->WndProc(hWnd, msg, wParam, lParam);
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
			case WM_MOUSEMOVE:
				ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
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
#ifdef USE_DETOURS
		DetourRestoreAfterWith();
#endif
#ifdef USE_MINHOOK
		MH_Initialize();
#endif

		Hook(54, (void**)&OriginalExecuteCommandLists, HookExecuteCommandLists);
		Hook(140, (void**)&OriginalPresent, HookPresent);
		Hook(145, (void**)&OriginalResizeBuffers, HookResizeBuffers);

		g_PluginManager = new PluginManager();

		return Status::Success;
	}

	Status RemoveHooks() {
		Unhook(54, (void**)&OriginalExecuteCommandLists, HookExecuteCommandLists);
		Unhook(140, (void**)&OriginalPresent, HookPresent);
		Unhook(145, (void**)&OriginalResizeBuffers, HookResizeBuffers);

		if (Window && OriginalWndProc) {
			SetWindowLongPtr(Window, GWLP_WNDPROC, (__int3264)(LONG_PTR)OriginalWndProc);
		}

		g_PluginManager = nullptr;
		ResetState();
		ImGui::DestroyContext();

#ifdef USE_MINHOOK
		MH_Uninitialize();
#endif
		//wait for hooks to finish if in one. maybe not needed, but seemed more stable after adding it.
		Sleep(1000);
		return Status::Success;
	}

	void SetDllModule(HMODULE hModule) {
		g_DllModule = hModule;
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
		if (filename.empty())
			s_ClearWindowBg[slot] = true;
		else
			s_PendingWindowBgFilename[slot] = filename;
	}

	uint64_t GetFrameBgTextureId(int slot) {
		if (slot < 0 || slot >= kFrameBgSlots) return 0;
		return (uint64_t)g_FrameBgTextureGPUHandle[slot].ptr;
	}

	void RequestFrameBgImageReload(int slot, const std::string& filename) {
		if (slot < 0 || slot >= kFrameBgSlots) return;
		if (filename.empty())
			s_ClearFrameBg[slot] = true;
		else
			s_PendingFrameBgFilename[slot] = filename;
	}

	uint64_t GetCheckboxTextureId(int slot) {
		if (slot < 0 || slot >= kCheckboxSlots) return 0;
		return (uint64_t)g_CheckboxTextureGPUHandle[slot].ptr;
	}

	void RequestCheckboxImageReload(int slot, const std::string& filename) {
		if (slot < 0 || slot >= kCheckboxSlots) return;
		if (filename.empty())
			s_ClearCheckbox[slot] = true;
		else
			s_PendingCheckboxFilename[slot] = filename;
	}

	uint64_t GetButtonTextureId(int slot) {
		if (slot < 0 || slot >= kButtonSlots) return 0;
		return (uint64_t)g_ButtonTextureGPUHandle[slot].ptr;
	}

	void RequestButtonImageReload(int slot, const std::string& filename) {
		if (slot < 0 || slot >= kButtonSlots) return;
		if (filename.empty())
			s_ClearButton[slot] = true;
		else
			s_PendingButtonFilename[slot] = filename;
	}

	uint64_t GetCameraButtonTextureId() {
		return (uint64_t)g_CameraButtonTextureGPUHandle.ptr;
	}

	void GetCameraButtonTextureSize(int* outWidth, int* outHeight) {
		if (outWidth) *outWidth = g_CameraButtonTextureWidth;
		if (outHeight) *outHeight = g_CameraButtonTextureHeight;
	}

}