#define NOMINMAX
#include "../d2rhud/ProcessLifetime.h"
#include "../d2rhud/ThemeImageRequests.h"
#include <Windows.h>
#include <atlbase.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#define STB_IMAGE_IMPLEMENTATION
#include "../d2rhud/stb_image.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>

namespace fs = std::filesystem;
unsigned checks = 0;
void Check(bool condition, const char* message) { ++checks; if(!condition)throw std::runtime_error(message); }
void CheckHR(HRESULT status) { if(FAILED(status))throw std::runtime_error("Native D3D12 operation failed"); }
void Scheduling() {
    using namespace ThemeImages;
    Requests requests;
    Check(requests.TakeDue(0).empty(), "Unused slots do no work");
    Check(!requests.Set(SlotCount, "bad") && !requests.Set(size_t(-1), "bad"), "Out of range requests rejected");
    for(size_t slot=0;slot<SlotCount;++slot)Check(requests.Set(slot,"valid.bmp"), "Every theme slot accepts requests");
    auto due=requests.TakeDue(100);
    Check(due.size()==SlotCount, "All slot families are scheduled");
    Check(requests.TakeDue(10000).empty(), "In-progress attempts are not reissued");
    for(const auto& request:due)requests.Complete(request,false,500);
    Check(requests.TakeDue(1499).empty(), "Backoff starts at failure completion");
    due=requests.TakeDue(1500);Check(due.size()==SlotCount, "Retry at exact threshold");
    for(const auto& request:due)requests.Complete(request,true,1500);
    bool idle=true;for(int frame=0;frame<100000;++frame)idle &= requests.TakeDue(100000).empty();
    Check(idle, "Successful slots stay idle for 100000 frames");
    requests.ResetResources();due=requests.TakeDue(1500);
    Check(due.size()==SlotCount, "Renderer reset rearms all chosen filenames");
    auto stale=due.front();requests.Set(stale.slot, "");
    requests.Complete(stale,true,1500);
    auto clear=requests.TakeDue(1500);
    Check(clear.size()==1 && clear.front().filename.empty(), "Clear replaces a pending load; stale completion is ignored");
    requests.Complete(clear.front(),true,1500);
    for(size_t i=1;i<due.size();++i)requests.Complete(due[i],true,1500);
    requests.ResetResources();due=requests.TakeDue(1500);
    Check(due.size()==SlotCount-1 && due.front().slot==1, "Cleared images do not reappear after reset");
    for(const auto& request:due)requests.Complete(request,true,1500);
    requests.Set(1,"missing.bmp");due=requests.TakeDue(2000);requests.Complete(due.front(),false,2000);
    requests.Set(1,"new.bmp");due=requests.TakeDue(2001);
    Check(due.size()==1 && due[0].filename=="new.bmp", "New selection bypasses previous file backoff");
    requests.Complete(due[0],false,2001);
    due=requests.TakeDue(1);Check(due.size()==1, "Clock rollback does not stall retries");
    requests.ResetResources();requests.Complete(due[0],true,1);
    Check(!requests.TakeDue(1).empty(), "Reset rejects completions from destroyed renderer generation");
}
namespace LegacyTransmog::Skin {struct Sprite {};}
namespace D3D12 {
#include "d3d12-reset-types.inl"
static UINT g_FrameBufferCount=3;
static D3D12_GPU_DESCRIPTOR_HANDLE g_WindowBgTextureGPUHandle[kWindowBgSlots]{};
static D3D12_GPU_DESCRIPTOR_HANDLE g_FrameBgTextureGPUHandle[kFrameBgSlots]{};
static D3D12_GPU_DESCRIPTOR_HANDLE g_CheckboxTextureGPUHandle[kCheckboxSlots]{};
static D3D12_GPU_DESCRIPTOR_HANDLE g_ButtonTextureGPUHandle[kButtonSlots]{};
static uint64_t now=100;
static unsigned directoryReads=0,decodeCalls=0;
static std::string directory;
static uint64_t TestClock(){return now;}
static std::string GetD2RHUDImagesDir(){++directoryReads;return directory;}
static unsigned char* CountedStbiLoad(const char* path,int* w,int* h,int* channels,int desired){++decodeCalls;return stbi_load(path,w,h,channels,desired);}
#include "theme-image-native.inl"
struct DeviceSource {
    ID3D12Device* device;
    unsigned gets=0;
    bool fail=false;
    HRESULT GetDevice(REFIID iid,void** result){++gets;*result=nullptr;return fail?E_FAIL:device->QueryInterface(iid,result);}
};
ULONG References(ID3D12Device* device){const auto value=device->AddRef();device->Release();return value;}
void SetAll(const std::string& file){for(size_t slot=0;slot<ThemeImages::SlotCount;++slot)ThemeImages::Runtime().Set(slot,file);}
void CheckAll(bool populated){
    for(size_t i=0;i<ThemeImages::SlotCount;++i){
        auto slot=ThemeTexture(i);
        Check(bool(*slot.resource)==populated && bool(slot.handle->ptr)==populated,"Texture and handle remain consistent for every slot");
        if(populated){const auto desc=(*slot.resource)->GetDesc();Check(desc.Width==2 && desc.Height==2,"Native texture has decoded dimensions");}
    }
}
void Native(const fs::path& root){
    fs::create_directories(root);directory=root.string();
    // A 2x2, 24-bit BMP with two padded eight-byte rows.
    unsigned char bmp[70]{};bmp[0]='B';bmp[1]='M';bmp[2]=70;bmp[10]=54;bmp[14]=40;
    bmp[18]=2;bmp[22]=2;bmp[26]=1;bmp[28]=24;bmp[54]=255;bmp[58]=255;bmp[64]=255;
    {std::ofstream image(root/"valid.bmp",std::ios::binary);image.write(reinterpret_cast<char*>(bmp),sizeof(bmp));}
    {std::ofstream broken(root/"broken.bmp");broken<<"not an image";}
    CComPtr<IDXGIFactory4> factory;CheckHR(CreateDXGIFactory2(0,IID_PPV_ARGS(&factory)));
    CComPtr<IDXGIAdapter> adapter;CheckHR(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
    CComPtr<ID3D12Device> device;CheckHR(D3D12CreateDevice(adapter,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
    D3D12_DESCRIPTOR_HEAP_DESC desc{};desc.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.NumDescriptors=64;desc.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    CheckHR(device->CreateDescriptorHeap(&desc,IID_PPV_ARGS(&Graphics().srv)));
    DeviceSource source{device};
    ProcessThemeImageRequests(&source);Check(source.gets==0 && directoryReads==0,"Idle renderer does no device/directory work");
    SetAll("valid.bmp");ProcessThemeImageRequests(&source);CheckAll(true);
    Check(source.gets==1 && directoryReads==1 && decodeCalls==17,"One device acquisition and directory lookup per reload batch");
    const auto increment=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    for(size_t i=0;i<ThemeImages::SlotCount;++i){auto slot=ThemeTexture(i);Check(slot.handle->ptr==Graphics().srv->GetGPUDescriptorHandleForHeapStart().ptr+uint64_t(slot.offset)*increment,"Every slot keeps its original descriptor offset");}
    auto original=Graphics().window[0];const auto handle=g_WindowBgTextureGPUHandle[0];
    unsigned char pixels[16]{};
    Check(!LoadTexturePixels(device,nullptr,2,2,DXGI_FORMAT_R8G8B8A8_UNORM,Graphics().window[0],g_WindowBgTextureGPUHandle[0],4),"Null pixels rejected");
    Check(!LoadTexturePixels(device,pixels,0,2,DXGI_FORMAT_R8G8B8A8_UNORM,Graphics().window[0],g_WindowBgTextureGPUHandle[0],4),"Zero dimensions rejected");
    Check(!LoadTexturePixels(device,pixels,INT_MAX,2,DXGI_FORMAT_R8G8B8A8_UNORM,Graphics().window[0],g_WindowBgTextureGPUHandle[0],4),"Oversized dimensions rejected before arithmetic/allocation");
    Check(!LoadTexturePixels(device,pixels,2,2,DXGI_FORMAT_UNKNOWN,Graphics().window[0],g_WindowBgTextureGPUHandle[0],4),"Unsupported formats rejected");
    Check(!LoadTexturePixels(device,pixels,2,2,DXGI_FORMAT_R8G8B8A8_UNORM,Graphics().window[0],g_WindowBgTextureGPUHandle[0],64),"Descriptor overflow rejected");
    Check(Graphics().window[0]==original && g_WindowBgTextureGPUHandle[0].ptr==handle.ptr,"Rejected uploads preserve original texture and handle");
    CComPtr<ID3D12Resource> compressed;D3D12_GPU_DESCRIPTOR_HANDLE compressedHandle{};
    Check(LoadTexturePixels(device,pixels,4,4,DXGI_FORMAT_BC3_UNORM,compressed,compressedHandle,63),"Existing BC3 atlas upload remains supported");
    Check(compressed->GetDesc().Format==DXGI_FORMAT_BC3_UNORM && compressedHandle.ptr!=0,"Native BC3 texture and descriptor created");
    compressed.Release();
    const auto baseline=References(device);
    SetAll("missing.bmp");decodeCalls=0;source.gets=0;directoryReads=0;
    for(unsigned frame=0;frame<10000;++frame){now=200+frame;ProcessThemeImageRequests(&source);}
    Check(decodeCalls==170 && source.gets==10 && directoryReads==10,"10000 frames cause only ten failed reload batches");
    Check(References(device)==baseline,"Repeated failed loads do not leak device references");CheckAll(true);
    fs::copy_file(root/"valid.bmp",root/"missing.bmp");now=10200;
    ProcessThemeImageRequests(&source);CheckAll(true);
    Check(Graphics().window[0]!=original,"Missing image recovers automatically once its file appears");
    original=Graphics().window[0];
    SetAll("broken.bmp");now=11000;ProcessThemeImageRequests(&source);CheckAll(true);
    Check(Graphics().window[0]==original,"Corrupt images retain previous texture");
    SetAll("");const auto getsBeforeClear=source.gets;const auto decodesBeforeClear=decodeCalls;
    ProcessThemeImageRequests(&source);CheckAll(false);
    now+=10000;ProcessThemeImageRequests(&source);
    Check(source.gets==getsBeforeClear && decodeCalls==decodesBeforeClear,"Clearing cancels failed loads and needs no device or decoder");
    ThemeImages::Runtime().ResetResources();ProcessThemeImageRequests(&source);CheckAll(false);
    SetAll("valid.bmp");source.fail=true;now+=1000;const auto before=source.gets;
    ProcessThemeImageRequests(&source);Check(source.gets==before+1,"Device failure is acquired once per batch");CheckAll(false);
    ProcessThemeImageRequests(&source);Check(source.gets==before+1,"Device failures obey retry backoff");
    now+=1000;source.fail=false;ProcessThemeImageRequests(&source);CheckAll(true);
    for(size_t i=0;i<ThemeImages::SlotCount;++i){auto slot=ThemeTexture(i);slot.resource->Release();*slot.handle={};}
    ThemeImages::Runtime().ResetResources();ProcessThemeImageRequests(&source);CheckAll(true);
    CheckHR(device->GetDeviceRemovedReason());
    // Exercise a genuine native allocation failure after upload preflight.
    // Removal is confined to this private WARP device, after normal GPU tests.
    CComPtr<ID3D12Device5> removable;CheckHR(device->QueryInterface(IID_PPV_ARGS(&removable)));
    auto lastTexture=Graphics().window[0];const auto lastHandle=g_WindowBgTextureGPUHandle[0];
    removable->RemoveDevice();
    Check(FAILED(device->GetDeviceRemovedReason()),"Private device removal injected");
    Check(!LoadTexturePixels(device,pixels,2,2,DXGI_FORMAT_R8G8B8A8_UNORM,Graphics().window[0],g_WindowBgTextureGPUHandle[0],4),"Native upload failure reported");
    Check(Graphics().window[0]==lastTexture && g_WindowBgTextureGPUHandle[0].ptr==lastHandle.ptr,"Native allocation failure retains original texture and handle");
    // Release native objects outside process teardown/loader lock.
    SetAll("");ProcessThemeImageRequests(&source);lastTexture.Release();original.Release();Graphics().srv.Release();
    std::cout<<"WARP: 10000 missing-image frames used 170 decode attempts instead of 170000; device references balanced.\n";
}
}
int main(int argc,char** argv){try{Check(argc==2,"Expected private output path");Scheduling();D3D12::Native(fs::path(argv[1])/("theme-"+std::to_string(GetTickCount64())));std::cout<<checks<<" theme image checks passed.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
