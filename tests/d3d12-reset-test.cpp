#include "../d2rhud/ProcessLifetime.h"
#include "../d2rhud/ThemeImageRequests.h"
#include <Windows.h>
#include <atlbase.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <cassert>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// Only UI callbacks and CPU metadata are stubbed. GPU owners, native types,
// getter, and ResetState below are extracted verbatim from D3D12Hook.cpp.
namespace LegacyTransmog::Skin {struct Sprite {};}
namespace Inspect {struct Assets {};namespace Data {inline constexpr size_t MaxItems=256;}}
namespace Spectate::Video {void Reset(){}}
static unsigned windowShutdowns=0,win32Shutdowns=0,dx12Shutdowns=0;
namespace HudWindowLayout {void Shutdown(){++windowShutdowns;}}
void ImGui_ImplWin32_Shutdown(){++win32Shutdowns;}
void ImGui_ImplDX12_Shutdown(){++dx12Shutdowns;}

namespace D3D12 {
#include "d3d12-reset-types.inl"
static bool g_Initialized=false;
static std::mutex g_QueueMutex;
static IDXGISwapChain3* g_RenderSwapchain=nullptr;
static D3D12_GPU_DESCRIPTOR_HANDLE g_GearTextureGPUHandle{},g_CameraButtonTextureGPUHandle{};
static int g_GearTextureWidth=0,g_GearTextureHeight=0,g_CameraButtonTextureWidth=0,g_CameraButtonTextureHeight=0;
static D3D12_GPU_DESCRIPTOR_HANDLE g_WindowBgTextureGPUHandle[kWindowBgSlots]{};
static D3D12_GPU_DESCRIPTOR_HANDLE g_FrameBgTextureGPUHandle[kFrameBgSlots]{};
static D3D12_GPU_DESCRIPTOR_HANDLE g_CheckboxTextureGPUHandle[kCheckboxSlots]{};
static D3D12_GPU_DESCRIPTOR_HANDLE g_ButtonTextureGPUHandle[kButtonSlots]{};
#include "d3d12-reset-body.inl"

void CheckEmpty(){
    const auto& state=Graphics();
    assert(!state.queue&&!state.commands&&!state.rtv&&!state.srv&&!state.gear&&!state.camera);
    assert(state.frames.empty()&&state.atlases.empty()&&!state.quantity.resource&&!state.quantity.handle.ptr&&!state.quantitySprite);
    assert(state.inspectTextures.empty()&&!state.inspectAssets&&!state.inspectRetirement&&state.inspectFrame==-1&&state.inspectUploads==0);
    for(int i=0;i<kWindowBgSlots;++i)assert(!state.window[i]&&!g_WindowBgTextureGPUHandle[i].ptr);
    for(int i=0;i<kFrameBgSlots;++i)assert(!state.frame[i]&&!g_FrameBgTextureGPUHandle[i].ptr);
    for(int i=0;i<kCheckboxSlots;++i)assert(!state.checkbox[i]&&!g_CheckboxTextureGPUHandle[i].ptr);
    for(int i=0;i<kButtonSlots;++i)assert(!state.button[i]&&!g_ButtonTextureGPUHandle[i].ptr);
    assert(!g_Initialized&&!g_RenderSwapchain&&!g_GearTextureGPUHandle.ptr&&!g_CameraButtonTextureGPUHandle.ptr);
    assert(!g_GearTextureWidth&&!g_GearTextureHeight&&!g_CameraButtonTextureWidth&&!g_CameraButtonTextureHeight);
}
void Check(HRESULT status){if(FAILED(status)){std::cerr<<"D3D12 HRESULT 0x"<<std::hex<<status<<'\n';std::abort();}}
void Texture(ID3D12Device* device,CComPtr<ID3D12Resource>& output){
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width=16;desc.Height=16;desc.DepthOrArraySize=1;desc.MipLevels=1;
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    Check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&output)));
    assert(output);
}
void Fill(ID3D12Device* device){
    D3D12_DESCRIPTOR_HEAP_DESC desc{};desc.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;desc.NumDescriptors=3;
    Check(device->CreateDescriptorHeap(&desc,IID_PPV_ARGS(&Graphics().rtv)));
    desc.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;desc.NumDescriptors=64;desc.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    Check(device->CreateDescriptorHeap(&desc,IID_PPV_ARGS(&Graphics().srv)));
    D3D12_COMMAND_QUEUE_DESC queue{};queue.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
    Check(device->CreateCommandQueue(&queue,IID_PPV_ARGS(&Graphics().queue)));
    Graphics().frames.resize(3);
    for(auto& frame:Graphics().frames){
        Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&frame.command_allocator)));
        Texture(device,frame.main_render_target_resource);
        frame.main_render_target_descriptor=Graphics().rtv->GetCPUDescriptorHandleForHeapStart();
        device->CreateRenderTargetView(frame.main_render_target_resource,nullptr,frame.main_render_target_descriptor);
    }
    Check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,Graphics().frames[0].command_allocator,nullptr,IID_PPV_ARGS(&Graphics().commands)));
    Check(Graphics().commands->Close());
    Texture(device,Graphics().gear);Texture(device,Graphics().camera);
    for(int i=0;i<kWindowBgSlots;++i){Texture(device,Graphics().window[i]);g_WindowBgTextureGPUHandle[i].ptr=1+i;}
    for(int i=0;i<kFrameBgSlots;++i){Texture(device,Graphics().frame[i]);g_FrameBgTextureGPUHandle[i].ptr=1+i;}
    for(int i=0;i<kCheckboxSlots;++i){Texture(device,Graphics().checkbox[i]);g_CheckboxTextureGPUHandle[i].ptr=1+i;}
    for(int i=0;i<kButtonSlots;++i){Texture(device,Graphics().button[i]);g_ButtonTextureGPUHandle[i].ptr=1+i;}
    for(const auto* name:{"first","second","third"})Texture(device,Graphics().atlases[name].resource);
    Texture(device,Graphics().quantity.resource);Graphics().quantity.handle=Graphics().srv->GetGPUDescriptorHandleForHeapStart();
    Graphics().quantitySprite=std::make_shared<LegacyTransmog::Skin::Sprite>();
    Graphics().inspectAssets=std::make_shared<Inspect::Assets>();Graphics().inspectTextures.resize(3);
    for(auto& texture:Graphics().inspectTextures)Texture(device,texture.resource);
    Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&Graphics().inspectRetirement)));Graphics().inspectFrame=42;Graphics().inspectUploads=2;
    g_GearTextureGPUHandle=Graphics().srv->GetGPUDescriptorHandleForHeapStart();g_CameraButtonTextureGPUHandle=g_GearTextureGPUHandle;
    g_GearTextureWidth=g_GearTextureHeight=g_CameraButtonTextureWidth=g_CameraButtonTextureHeight=16;
    g_Initialized=true;
    CComPtr<ID3D12Fence> fence;Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));
    const auto event=CreateEventW(nullptr,FALSE,FALSE,nullptr);assert(event);
    Check(fence->SetEventOnCompletion(1,event));Check(Graphics().queue->Signal(fence,1));
    const auto completed=WaitForSingleObject(event,5000);CloseHandle(event);
    assert(completed==WAIT_OBJECT_0&&fence->GetCompletedValue()==1);
}
}

int main(){
    // This must be the first access to Graphics(), matching startup/resize.
    D3D12::ResetState();D3D12::CheckEmpty();D3D12::ResetState();D3D12::CheckEmpty();
    assert(!windowShutdowns&&!win32Shutdowns&&!dx12Shutdowns);
    CComPtr<IDXGIFactory4> factory;D3D12::Check(CreateDXGIFactory2(0,IID_PPV_ARGS(&factory)));
    CComPtr<IDXGIAdapter> adapter;D3D12::Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
    CComPtr<ID3D12Device> device;D3D12::Check(D3D12CreateDevice(adapter,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
    for(unsigned cycle=1;cycle<=3;++cycle){
        D3D12::Fill(device);D3D12::Check(device->GetDeviceRemovedReason());
        D3D12::ResetState();D3D12::CheckEmpty();D3D12::ResetState();D3D12::CheckEmpty();
        D3D12::Check(device->GetDeviceRemovedReason());
        assert(windowShutdowns==cycle&&win32Shutdowns==cycle&&dx12Shutdowns==cycle);
    }
    std::cout<<"Exact production ResetState passes first empty startup reset and 3 WARP D3D12 create/reset/refill cycles; all owners/handles clear and device stays valid.\n";
}
