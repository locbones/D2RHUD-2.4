#include "SpectateVideo.h"
#include "../ProcessLifetime.h"
#include "../RuntimeTelemetry.h"
#include "SpectateDiagnostics.h"
#include "SpectateTransport.h"
#include <atlbase.h>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <thread>

namespace Spectate::Video {
namespace {
struct EncodeTask {
    CComPtr<ID3D12Resource> resource;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    CaptureRequest request;
    std::shared_ptr<std::atomic<bool>> busy;
};
struct EncodeWorker {
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<EncodeTask> tasks;
    std::thread thread;
    EncodeWorker() : thread([this] { Run(); }) {}
    void Queue(EncodeTask task) {
        std::lock_guard lock(mutex);
        if (tasks.size() >= 1) {
            *tasks.back().busy = false;
            tasks.pop_back();
        }
        tasks.push_back(std::move(task));
        wake.notify_one();
    }
    void Run() {
        HudTelemetry::FeatureScope feature(HudTelemetry::Feature::SpectateEncode);
        std::unique_ptr<Stream::Encoder> encoder;
        for (;;) {
            EncodeTask task;
            {
                std::unique_lock lock(mutex);
                wake.wait(lock, [&] { return !tasks.empty(); });
                task = std::move(tasks.front());
                tasks.pop_front();
            }
            struct ReleaseBusy {
                std::shared_ptr<std::atomic<bool>> busy;
                ~ReleaseBusy() { *busy = false; }
            } release{task.busy};
            try {
                if (!encoder)
                    encoder = std::make_unique<Stream::Encoder>();
                void* pointer = nullptr;
                const auto& f = task.footprint.Footprint;
                const D3D12_RANGE range{0, size_t(f.RowPitch) * f.Height};
                if (SUCCEEDED(task.resource->Map(0, &range, &pointer))) {
                    struct Mapping {
                        ID3D12Resource* resource;
                        ~Mapping() {
                            const D3D12_RANGE written{0, 0};
                            resource->Unmap(0, &written);
                        }
                    } mapping{task.resource};
                    const double fit = (std::min)({1.0, double(task.request.settings.width) / f.Width,
                                                   double(Data::MaxHeight) / f.Height});
                    const double quality = task.request.settings.quality / 100.0;
                    const double estimate = f.Width * fit * f.Height * fit * task.request.settings.fps *
                                            (.02 + .2 * quality * quality * quality);
                    const auto bitrate =
                        (std::min)(task.request.bitrate, unsigned((std::max)(128000.0, estimate)));
                    auto frame = encoder->Encode(static_cast<const uint8_t*>(pointer), f.Width, f.Height,
                                                 f.RowPitch, unsigned(f.Format),
                                                 {task.request.settings.fps, task.request.settings.width,
                                                  task.request.settings.quality},
                                                 bitrate, task.request.stream, task.request.frame);
                    if (frame)
                        Submit(task.request, std::move(*frame));
                    else if (!encoder->Error().empty()) {
                        VideoError(encoder->Error());
                        Stream::Local().ReportCaptureError(encoder->Error());
                        CaptureFailed(task.request);
                    }
                } else {
                    Stream::Local().ReportCaptureError("GPU capture readback failed.");
                    CaptureFailed(task.request);
                }
            } catch (...) {
                Diagnostics::Log("encoder", "Video encoding threw an exception; resetting the encoder.");
                Stream::Local().ReportCaptureError("Video encoder failed; retrying capture.");
                VideoError("Video encoder failed; retrying capture.");
                encoder.reset();
                CaptureFailed(task.request);
            }
        }
    }
};
struct DecodeWorker {
    std::mutex mutex;
    std::shared_ptr<Image> latest;
    uint64_t generation = 0;
    std::thread thread;
    DecodeWorker() : thread([this] { Run(); }) {}
    void Run() {
        HudTelemetry::FeatureScope feature(HudTelemetry::Feature::SpectateDecode);
        std::unique_ptr<Stream::Decoder> decoder;
        uint64_t viewGeneration = 0;
        for (;;) {
            try {
                const auto view = Current();
                if (!view.open || !view.inGame) {
                    decoder.reset();
                    viewGeneration = 0;
                    Stream::Local().Drain();
                    std::this_thread::sleep_for(std::chrono::milliseconds(30));
                    continue;
                }
                if (!decoder || view.generation != viewGeneration) {
                    decoder = std::make_unique<Stream::Decoder>();
                    viewGeneration = view.generation;
                }
                for (const auto& received : Stream::Local().Drain()) {
                    if (received.target != view.target || received.request != view.request)
                        continue;
                    try {
                        if (auto pixels = decoder->Decode(*received.frame)) {
                            auto image = std::make_shared<Image>();
                            image->width = int(pixels->width);
                            image->height = int(pixels->height);
                            image->rgba = std::move(pixels->rgba);
                            image->revision = received.frame->sequence;
                            std::lock_guard lock(mutex);
                            generation = view.generation;
                            latest = std::move(image);
                        } else if (!decoder->Error().empty()) {
                            VideoError(decoder->Error());
                            Stream::Local().Resync();
                            decoder.reset();
                            break;
                        }
                    } catch (...) {
                        Diagnostics::Log("decoder",
                                         "Video decoding threw an exception; resetting the decoder.");
                        VideoError("Video decoder failed; reconnecting the video channel.");
                        Stream::Local().Resync();
                        decoder.reset();
                        break;
                    }
                }
            } catch (...) {
                Diagnostics::Log("decoder", "Video worker initialization failed; retrying.");
                VideoError("Video decoder initialization failed; retrying.");
                decoder.reset();
                Stream::Local().Resync();
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(3));
        }
    }
    std::shared_ptr<Image> Poll(uint64_t expected) {
        std::lock_guard lock(mutex);
        auto result = generation == expected ? std::move(latest) : nullptr;
        latest.reset();
        return result;
    }
};
EncodeWorker& Encoder() { return HudLifetime::State<EncodeWorker>(); }
DecodeWorker& Decoder() { return HudLifetime::State<DecodeWorker>(); }
struct CaptureSlot {
    CComPtr<ID3D12Resource> readback;
    CComPtr<ID3D12CommandAllocator> allocator;
    CComPtr<ID3D12GraphicsCommandList> commands;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    CaptureRequest request;
    uint64_t reading = 0;
    std::shared_ptr<std::atomic<bool>> busy = std::make_shared<std::atomic<bool>>(false);
};
struct Slot {
    CComPtr<ID3D12Resource> texture, upload;
    CComPtr<ID3D12CommandAllocator> allocator;
    CComPtr<ID3D12GraphicsCommandList> commands;
    uint64_t used = 0;
};
struct State {
    CComPtr<ID3D12Device> device;
    CComPtr<ID3D12Fence> fence;
    CComPtr<ID3D12CommandQueue> queue;
    std::array<CaptureSlot, 2> captures;
    uint64_t nextFence = 0, generation = 0, texture = 0, lastActive = 0;
    std::array<Slot, TextureSlots> slots;
    int active = -1;
    std::shared_ptr<const Image> image;
    std::shared_ptr<Image> pending;
};
State& Runtime() { return HudLifetime::State<State>(); }
D3D12_RESOURCE_DESC Buffer(uint64_t bytes) {
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    d.Width = bytes;
    d.Height = 1;
    d.DepthOrArraySize = 1;
    d.MipLevels = 1;
    d.SampleDesc.Count = 1;
    d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return d;
}
D3D12_HEAP_PROPERTIES Heap(D3D12_HEAP_TYPE type) {
    D3D12_HEAP_PROPERTIES h{};
    h.Type = type;
    return h;
}
bool List(ID3D12Device* device, CComPtr<ID3D12CommandAllocator>& allocator,
          CComPtr<ID3D12GraphicsCommandList>& commands) {
    if (!allocator &&
        FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))))
        return false;
    if (!commands) {
        if (FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr,
                                             IID_PPV_ARGS(&commands))))
            return false;
        return SUCCEEDED(commands->Close());
    }
    return true;
}
void Barrier(ID3D12GraphicsCommandList* list, ID3D12Resource* resource, D3D12_RESOURCE_STATES before,
             D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = resource;
    b.Transition.StateBefore = before;
    b.Transition.StateAfter = after;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    list->ResourceBarrier(1, &b);
}
bool Signal(State& s, uint64_t& value) {
    value = ++s.nextFence;
    return SUCCEEDED(s.queue->Signal(s.fence, value));
}
bool Capture(State& s, CaptureSlot& slot, ID3D12Resource* source, const CaptureRequest& request) {
    const auto desc = source->GetDesc();
    if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || desc.SampleDesc.Count != 1 ||
        desc.DepthOrArraySize != 1 || !desc.Width || desc.Width > 8192 || !desc.Height ||
        desc.Height > 4320 ||
        (desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM && desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM_SRGB &&
         desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM && desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM_SRGB))
        return false;
    uint64_t bytes = 0;
    s.device->GetCopyableFootprints(&desc, 0, 1, 0, &slot.footprint, nullptr, nullptr, &bytes);
    bytes = (std::max)(bytes, uint64_t(slot.footprint.Footprint.RowPitch) * desc.Height);
    if (!bytes || bytes > 128 * 1024 * 1024)
        return false;
    if (!slot.readback || slot.readback->GetDesc().Width != bytes) {
        slot.readback.Release();
        const auto heap = Heap(D3D12_HEAP_TYPE_READBACK);
        const auto buffer = Buffer(bytes);
        if (FAILED(s.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
                                                     D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                     IID_PPV_ARGS(&slot.readback))))
            return false;
    }
    if (!List(s.device, slot.allocator, slot.commands) || FAILED(slot.allocator->Reset()) ||
        FAILED(slot.commands->Reset(slot.allocator, nullptr)))
        return false;
    Barrier(slot.commands, source, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION from{}, to{};
    from.pResource = source;
    from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    to.pResource = slot.readback;
    to.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    to.PlacedFootprint = slot.footprint;
    slot.commands->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
    Barrier(slot.commands, source, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PRESENT);
    if (FAILED(slot.commands->Close()))
        return false;
    ID3D12CommandList* lists[]{slot.commands};
    s.queue->ExecuteCommandLists(1, lists);
    if (!Signal(s, slot.reading))
        return false;
    slot.request = request;
    return true;
}
bool Upload(State& s, D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu, unsigned stride) {
    if (!s.pending)
        return true;
    int index = -1;
    for (unsigned n = 0; n < TextureSlots; ++n)
        if (int(n) != s.active && s.slots[n].used <= s.fence->GetCompletedValue()) {
            index = int(n);
            break;
        }
    if (index < 0)
        return true;
    auto& slot = s.slots[size_t(index)];
    const auto image = s.pending;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = image->width;
    desc.Height = UINT(image->height);
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    uint64_t bytes = 0;
    s.device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr, &bytes);
    const bool resize = !slot.texture || !slot.upload || slot.texture->GetDesc().Width != desc.Width ||
                        slot.texture->GetDesc().Height != desc.Height;
    if (resize) {
        slot.texture.Release();
        slot.upload.Release();
        const auto heap = Heap(D3D12_HEAP_TYPE_DEFAULT);
        const auto uploadHeap = Heap(D3D12_HEAP_TYPE_UPLOAD);
        const auto buffer = Buffer(bytes);
        if (FAILED(s.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                     D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                     IID_PPV_ARGS(&slot.texture))) ||
            FAILED(s.device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &buffer,
                                                     D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                     IID_PPV_ARGS(&slot.upload))))
            return false;
    }
    void* mapped = nullptr;
    const D3D12_RANGE read{0, 0};
    if (FAILED(slot.upload->Map(0, &read, &mapped)))
        return false;
    for (int y = 0; y < image->height; ++y)
        memcpy(static_cast<uint8_t*>(mapped) + size_t(y) * footprint.Footprint.RowPitch,
               image->rgba.data() + size_t(y) * image->width * 4, size_t(image->width) * 4);
    slot.upload->Unmap(0, nullptr);
    if (!List(s.device, slot.allocator, slot.commands) || FAILED(slot.allocator->Reset()) ||
        FAILED(slot.commands->Reset(slot.allocator, nullptr)))
        return false;
    D3D12_TEXTURE_COPY_LOCATION from{}, to{};
    from.pResource = slot.upload;
    from.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    from.PlacedFootprint = footprint;
    to.pResource = slot.texture;
    to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    if (!resize)
        Barrier(slot.commands, slot.texture, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                D3D12_RESOURCE_STATE_COPY_DEST);
    slot.commands->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
    Barrier(slot.commands, slot.texture, D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    if (FAILED(slot.commands->Close()))
        return false;
    ID3D12CommandList* lists[]{slot.commands};
    s.queue->ExecuteCommandLists(1, lists);
    if (!Signal(s, slot.used))
        return false;
    cpu.ptr += size_t(index) * stride;
    gpu.ptr += uint64_t(index) * stride;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = desc.Format;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = 1;
    s.device->CreateShaderResourceView(slot.texture, &srv, cpu);
    s.active = index;
    s.texture = gpu.ptr;
    s.image = image;
    s.pending.reset();
    Displayed(s.generation, image->revision);
    return true;
}
} // namespace
bool NeedsRender() { return VideoDemand() || Runtime().device; }
void Render(ID3D12Device* device, ID3D12CommandQueue* queue, ID3D12Resource* backbuffer,
            D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu, unsigned stride) {
    if (!device || !queue || !backbuffer || !stride)
        return;
    auto& s = Runtime();
    const auto view = Current();
    if (s.generation != view.generation || !view.open) {
        s.image.reset();
        s.pending.reset();
        s.texture = 0;
        s.active = -1;
        s.generation = view.generation;
    }
    const auto now = GetTickCount64();
    const bool demand = VideoDemand();
    if (demand)
        s.lastActive = now;
    else if (now - s.lastActive >= 5000) {
        if (s.device)
            Reset();
        return;
    }
    if (!s.device) {
        if (!demand)
            return;
        s.device = device;
        s.queue = queue;
        if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&s.fence)))) {
            s.device.Release();
            return;
        }
    }
    if (s.device.p != device || s.queue.p != queue)
        return;
    // Two readback slots let capture N+1 overlap CPU encoding of capture N.
    // No GPU waits, pixel copies, socket writes, or codec work on this thread.
    for (auto& slot : s.captures) {
        if (slot.reading && s.fence->GetCompletedValue() >= slot.reading) {
            slot.reading = 0;
            *slot.busy = true;
            try {
                Encoder().Queue({slot.readback, slot.footprint, slot.request, slot.busy});
            } catch (...) {
                *slot.busy = false;
                Stream::Local().ReportCaptureError("Could not start the video capture worker.");
                VideoError("Could not start the video capture worker.");
                CaptureFailed(slot.request);
            }
        }
    }
    if (view.open && view.inGame) {
        try {
            if (auto image = Decoder().Poll(view.generation))
                s.pending = std::move(image);
        } catch (...) {
            VideoError("Could not start the video decoder worker.");
        }
    }
    if (!Upload(s, cpu, gpu, stride))
        VideoError("Spectate could not upload the video image to the GPU.");
    for (auto& slot : s.captures)
        if (!slot.reading && !*slot.busy) {
            if (const auto request = TakeCapture())
                if (!Capture(s, slot, backbuffer, *request)) {
                    Stream::Local().ReportCaptureError(
                        "GPU capture failed. Check the game's display format and graphics device.");
                    CaptureFailed(*request);
                }
            break;
        }
}
void Presented(ID3D12CommandQueue* queue) {
    auto& s = Runtime();
    if (s.fence && s.active >= 0 && s.texture && queue == s.queue)
        Signal(s, s.slots[size_t(s.active)].used);
}
uint64_t Texture() { return Runtime().texture; }
std::shared_ptr<const Image> DisplayImage() { return Runtime().image; }
void Reset() {
    auto& s = Runtime();
    // Resize is rare. Normal capture/upload never waits on the GPU. Retain
    // submitted resources if a removed/hung device cannot retire them safely.
    if (s.fence && s.queue && SUCCEEDED(s.queue->Signal(s.fence, ++s.nextFence)) &&
        s.fence->GetCompletedValue() < s.nextFence) {
        HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        bool retired = false;
        if (event) {
            if (SUCCEEDED(s.fence->SetEventOnCompletion(s.nextFence, event)))
                retired = WaitForSingleObject(event, 2000) == WAIT_OBJECT_0;
            CloseHandle(event);
        }
        if (!retired) {
            s.texture = 0;
            s.image.reset();
            return;
        }
    }
    for (const auto& slot : s.captures)
        if (slot.reading)
            CaptureFailed(slot.request);
    s.texture = 0;
    s.active = -1;
    s.image.reset();
    s.pending.reset();
    s.nextFence = 0;
    s.generation = 0;
    s.slots = {};
    s.captures = {};
    s.fence.Release();
    s.queue.Release();
    s.device.Release();
}
} // namespace Spectate::Video
