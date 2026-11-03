#define NOMINMAX
#include "../d2rhud/plugin/SpectateVideo.cpp"
#include <cassert>
#include <d3d12sdklayers.h>
#include <dxgi1_4.h>
#include <iostream>
namespace {
Spectate::View view;
std::mutex viewMutex;
Spectate::Stream::Client source;
std::atomic<unsigned> encoded{0};
std::atomic<unsigned> displayed{0};
Spectate::Settings settings{true, true, 10, 640, 55};
std::optional<Spectate::CaptureRequest> capture;
std::atomic<unsigned> failures{0};
const DWORD renderThread = GetCurrentThreadId();
std::atomic<bool> failWorkerSnapshot{false};
} // namespace
Spectate::View Spectate::Current() {
    if (GetCurrentThreadId() != renderThread && failWorkerSnapshot.exchange(false))
        throw std::runtime_error("Injected decoder initialization failure");
    std::lock_guard lock(viewMutex);
    return view;
}
Spectate::Settings Spectate::Configuration() { return settings; }
bool Spectate::VideoDemand() { return settings.enabled && (capture.has_value() || view.open); }
std::optional<Spectate::CaptureRequest> Spectate::TakeCapture() {
    auto result = capture;
    capture.reset();
    return result;
}
void Spectate::Submit(const CaptureRequest&, Stream::Frame frame) {
    assert(source.Publish(std::make_shared<const Stream::Frame>(std::move(frame))));
    ++encoded;
}
void Spectate::Displayed(uint64_t generation, uint64_t revision) {
    std::lock_guard lock(viewMutex);
    assert(generation == view.generation);
    view.revision = revision;
    ++displayed;
}
void Spectate::VideoError(std::string error) {
    std::cerr << error << '\n';
    ++failures;
}
void Spectate::CaptureFailed(const CaptureRequest&) { ++failures; }
int main() {
    using namespace Spectate;
    using namespace Spectate::Video;
    CComPtr<ID3D12Debug> debug;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
        debug->EnableDebugLayer();
    CComPtr<IDXGIFactory4> factory;
    assert(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))));
    CComPtr<IDXGIAdapter> adapter;
    assert(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter))));
    CComPtr<ID3D12Device> device;
    assert(SUCCEEDED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))));
    CComPtr<ID3D12CommandQueue> queue;
    D3D12_COMMAND_QUEUE_DESC q{};
    assert(SUCCEEDED(device->CreateCommandQueue(&q, IID_PPV_ARGS(&queue))));
    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.NumDescriptors = TextureSlots;
    hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    CComPtr<ID3D12DescriptorHeap> heap;
    assert(SUCCEEDED(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&heap))));
    const auto stride = device->GetDescriptorHandleIncrementSize(hd.Type);
    const auto cpu = heap->GetCPUDescriptorHandleForHeapStart();
    const auto gpu = heap->GetGPUDescriptorHandleForHeapStart();
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    d.Width = 3840;
    d.Height = 2160;
    d.DepthOrArraySize = 1;
    d.MipLevels = 1;
    d.SampleDesc.Count = 1;
    d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    CComPtr<ID3D12Resource> backbuffer;
    auto normalHeap = Heap(D3D12_HEAP_TYPE_DEFAULT);
    assert(SUCCEEDED(device->CreateCommittedResource(&normalHeap, D3D12_HEAP_FLAG_NONE, &d,
                                                     D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                     IID_PPV_ARGS(&backbuffer))));
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
    uint64_t bytes = 0;
    device->GetCopyableFootprints(&d, 0, 1, 0, &fp, nullptr, nullptr, &bytes);
    bytes = (std::max)(bytes, uint64_t(fp.Footprint.RowPitch) * d.Height);
    auto buffer = Buffer(bytes);
    auto uploadHeap = Heap(D3D12_HEAP_TYPE_UPLOAD);
    CComPtr<ID3D12Resource> upload;
    assert(SUCCEEDED(device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &buffer,
                                                     D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                     IID_PPV_ARGS(&upload))));
    void* mapped = nullptr;
    assert(SUCCEEDED(upload->Map(0, nullptr, &mapped)));
    for (unsigned y = 0; y < d.Height; ++y)
        for (unsigned x = 0; x < d.Width; ++x) {
            auto p = static_cast<uint8_t*>(mapped) + y * fp.Footprint.RowPitch + x * 4;
            p[0] = uint8_t(x * 255 / d.Width);
            p[1] = uint8_t(y * 255 / d.Height);
            p[2] = 33;
            p[3] = 255;
        }
    upload->Unmap(0, nullptr);
    CComPtr<ID3D12CommandAllocator> allocator;
    CComPtr<ID3D12GraphicsCommandList> list;
    assert(List(device, allocator, list));
    assert(SUCCEEDED(allocator->Reset()));
    assert(SUCCEEDED(list->Reset(allocator, nullptr)));
    D3D12_TEXTURE_COPY_LOCATION from{}, to{};
    from.pResource = upload;
    from.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    from.PlacedFootprint = fp;
    to.pResource = backbuffer;
    to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
    Barrier(list, backbuffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);
    assert(SUCCEEDED(list->Close()));
    ID3D12CommandList* lists[]{list};
    queue->ExecuteCommandLists(1, lists);
    auto step = [&] {
        Render(device, queue, backbuffer, cpu, gpu, stride);
        Presented(queue);
        Sleep(1);
    };
    const auto start = GetTickCount64();
    Stream::Authority authority;
    authority.seed = 55;
    authority.epoch = 99;
    authority.port = uint16_t(40000 + GetCurrentProcessId() % 20000);
    authority.bandwidthMbps = 128;
    authority.refreshed = GetTickCount64();
    Stream::Member sourceMember{1}, viewerMember{2};
    sourceMember.token.fill(11);
    viewerMember.token.fill(22);
    sourceMember.enabled = sourceMember.ready = sourceMember.sharing = true;
    viewerMember.enabled = viewerMember.ready = viewerMember.sharing = true;
    authority.members = {sourceMember, viewerMember};
    Stream::Server server;
    server.Update(authority);
    Stream::Connection sourceConnection;
    sourceConnection.credential = {1, 55, 99, sourceMember.token};
    sourceConnection.addresses = {0x0100007f};
    sourceConnection.port = authority.port;
    sourceConnection.ready = sourceConnection.sharing = true;
    sourceConnection.bandwidthMbps = 128;
    auto viewerConnection = sourceConnection;
    viewerConnection.credential = {2, 55, 99, viewerMember.token};
    viewerConnection.watch = Stream::Watch{1, 1, {30, 1920, 80}};
    source.Update(sourceConnection);
    Stream::Local().Update(viewerConnection);
    auto until = [&](auto condition) {
        const auto deadline = GetTickCount64() + 10000;
        while (!condition() && GetTickCount64() < deadline) {
            authority.refreshed = GetTickCount64();
            server.Update(authority);
            step();
        }
        assert(condition());
    };
    until([&] { return source.Current().demand.stream != 0; });
    {
        std::lock_guard lock(viewMutex);
        view.open = view.inGame = true;
        view.generation = 1;
        view.target = view.request = 1;
    }
    settings = {true, true, 30, 1920, 80, 128};
    uint32_t sequence = 0;
    for (unsigned n = 0; n < 12; ++n) {
        const auto stream = source.Current().demand;
        capture = CaptureRequest{++sequence, 1, settings, stream.stream, stream.bitrate};
        until([&] { return encoded.load() >= sequence; });
        until([&] { return displayed.load() >= sequence; });
    }
    assert(Texture() && DisplayImage() && DisplayImage()->width == 1920 && DisplayImage()->height == 1080 &&
           !failures);
    const auto pixel = DisplayImage()->rgba.data() + ((1080 / 2) * 1920 + 1920 / 2) * 4;
    assert(abs(int(pixel[0]) - 127) < 12 && abs(int(pixel[1]) - 127) < 12 && abs(int(pixel[2]) - 33) < 12);
    // Persistent viewer allocations survive further frames at the same size.
    auto texture0 = Runtime().slots[0].texture.p;
    for (unsigned n = 0; n < 4; ++n) {
        const auto stream = source.Current().demand;
        capture = CaptureRequest{++sequence, 1, settings, stream.stream, stream.bitrate};
        until([&] { return displayed.load() >= sequence; });
    }
    assert(Runtime().slots[0].texture.p == texture0);
    const auto previous = source.Current().demand.stream;
    viewerConnection.watch->profile = {30, 3840, 100};
    Stream::Local().Update(viewerConnection);
    until([&] {
        const auto d = source.Current().demand;
        return d.stream && d.stream != previous && d.profile.width == 3840;
    });
    settings = {true, true, 30, 3840, 100, 128};
    const auto stream = source.Current().demand;
    capture = CaptureRequest{1, 1, settings, stream.stream, stream.bitrate};
    until([&] { return DisplayImage() && DisplayImage()->width == 3840; });
    assert(Texture() && DisplayImage()->height == 2160);
    const auto beforeRecovery = displayed.load();
    const auto beforeResync = source.Current().demand.stream;
    failWorkerSnapshot = true;
    until([&] { return failures.load() == 1; });
    until([&] { return source.Current().demand.stream && source.Current().demand.stream != beforeResync; });
    const auto recoveredStream = source.Current().demand;
    capture = CaptureRequest{1, 1, settings, recoveredStream.stream, recoveredStream.bitrate};
    until([&] { return displayed.load() > beforeRecovery; });
    assert(failures.load() == 1); // The worker survives and decodes a fresh stream after the exception.
    Reset();
    assert(!Texture() && !DisplayImage());
    {
        std::lock_guard lock(viewMutex);
        view.open = false;
        view.generation = 2;
    }
    Stream::Local().Update(std::nullopt);
    source.Update(std::nullopt);
    settings.enabled = false;
    Reset();
    assert(!Runtime().device && !Texture());
    source.Stop();
    Stream::Local().Stop();
    server.Stop();
    CComPtr<ID3D12InfoQueue> info;
    if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&info))))
        for (uint64_t i = 0; i < info->GetNumStoredMessages(); ++i) {
            SIZE_T size = 0;
            info->GetMessage(i, nullptr, &size);
            std::vector<uint8_t> message(size);
            auto m = reinterpret_cast<D3D12_MESSAGE*>(message.data());
            if (SUCCEEDED(info->GetMessage(i, m, &size)) &&
                (m->Severity == D3D12_MESSAGE_SEVERITY_ERROR ||
                 m->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION)) {
                std::cerr << m->pDescription << '\n';
                return 1;
            }
        }
    std::cout
        << "PASS: WARP GPU capture -> persistent H264 -> authenticated TCP relay -> decoder -> GPU "
           "texture; 1080p/4K, color, allocation reuse, reset, idle, injected worker failure and recovery ("
        << GetTickCount64() - start << " ms)\n";
}
