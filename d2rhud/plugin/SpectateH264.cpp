#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "SpectateH264.h"
#include <Windows.h>
#include <atlbase.h>
#include <codecapi.h>
#include <cstdio>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mftransform.h>
#include <wmcodecdsp.h>
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "wmcodecdspuuid.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

namespace Spectate::Stream {
namespace {
struct Functions {
    // Resolve the optional Windows codec runtime only when video is used. A
    // static MFPlat import would prevent the entire HUD loading on Windows N.
    HMODULE library = LoadLibraryExW(L"mfplat.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    template <class T> T Find(const char* name) {
        return library ? reinterpret_cast<T>(GetProcAddress(library, name)) : nullptr;
    }
    decltype(&MFStartup) startup = Find<decltype(&MFStartup)>("MFStartup");
    decltype(&MFShutdown) shutdown = Find<decltype(&MFShutdown)>("MFShutdown");
    decltype(&MFCreateMediaType) mediaType = Find<decltype(&MFCreateMediaType)>("MFCreateMediaType");
    decltype(&MFCreateSample) sample = Find<decltype(&MFCreateSample)>("MFCreateSample");
    decltype(&MFCreateMemoryBuffer) buffer = Find<decltype(&MFCreateMemoryBuffer)>("MFCreateMemoryBuffer");
    decltype(&MFCreateAlignedMemoryBuffer) alignedBuffer =
        Find<decltype(&MFCreateAlignedMemoryBuffer)>("MFCreateAlignedMemoryBuffer");
    bool Ready() const { return startup && shutdown && mediaType && sample && buffer && alignedBuffer; }
};
Functions& Api() {
    static auto* const functions = new Functions;
    return *functions;
}
struct Foundation {
    HRESULT apartment = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    HRESULT startup =
        Api().Ready() ? Api().startup(MF_VERSION, MFSTARTUP_LITE) : HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND);
    ~Foundation() {
        if (SUCCEEDED(startup))
            Api().shutdown();
        if (SUCCEEDED(apartment))
            CoUninitialize();
    }
};
bool Check(HRESULT result, std::string& error, const char* operation) {
    if (SUCCEEDED(result))
        return true;
    char text[160]{};
    sprintf_s(text, "%s failed (0x%08lx).", operation, result);
    error = text;
    return false;
}
void Property(IMFTransform* transform, const GUID& key, unsigned value, bool boolean = false) {
    CComQIPtr<ICodecAPI> api(transform);
    if (!api)
        return;
    VARIANT option{};
    option.vt = VARTYPE(boolean ? VT_BOOL : VT_UI4);
    if (boolean)
        option.boolVal = value ? VARIANT_TRUE : VARIANT_FALSE;
    else
        option.ulVal = value;
    api->SetValue(&key, &option);
}
CComPtr<IMFMediaType> Type(const GUID& format, unsigned width, unsigned height, unsigned fps) {
    CComPtr<IMFMediaType> type;
    if (FAILED(Api().mediaType(&type)))
        return {};
    type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    type->SetGUID(MF_MT_SUBTYPE, format);
    MFSetAttributeSize(type, MF_MT_FRAME_SIZE, width, height);
    MFSetAttributeRatio(type, MF_MT_FRAME_RATE, fps, 1);
    MFSetAttributeRatio(type, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    type->SetUINT32(MF_MT_YUV_MATRIX, MFVideoTransferMatrix_BT709);
    type->SetUINT32(MF_MT_VIDEO_NOMINAL_RANGE, MFNominalRange_16_235);
    return type;
}
CComPtr<IMFSample> Sample(std::span<const uint8_t> bytes, uint64_t time, uint64_t duration) {
    if (bytes.empty() || bytes.size() > 64 * 1024 * 1024)
        return {};
    CComPtr<IMFSample> sample;
    CComPtr<IMFMediaBuffer> buffer;
    if (FAILED(Api().sample(&sample)) || FAILED(Api().buffer(DWORD(bytes.size()), &buffer)))
        return {};
    BYTE* pointer = nullptr;
    if (FAILED(buffer->Lock(&pointer, nullptr, nullptr)))
        return {};
    memcpy(pointer, bytes.data(), bytes.size());
    buffer->Unlock();
    if (FAILED(buffer->SetCurrentLength(DWORD(bytes.size()))) || FAILED(sample->AddBuffer(buffer)))
        return {};
    sample->SetSampleTime(LONGLONG(time));
    sample->SetSampleDuration(LONGLONG(duration));
    return sample;
}
HRESULT Output(IMFTransform* transform, CComPtr<IMFSample>& sample) {
    MFT_OUTPUT_STREAM_INFO info{};
    auto result = transform->GetOutputStreamInfo(0, &info);
    if (FAILED(result))
        return result;
    if (!(info.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES)) {
        CComPtr<IMFMediaBuffer> buffer;
        if (!info.cbSize || info.cbSize > 64 * 1024 * 1024)
            return E_INVALIDARG;
        result = Api().sample(&sample);
        if (SUCCEEDED(result))
            result = Api().alignedBuffer(info.cbSize, info.cbAlignment ? info.cbAlignment - 1 : 0, &buffer);
        if (SUCCEEDED(result))
            result = sample->AddBuffer(buffer);
        if (FAILED(result))
            return result;
    }
    MFT_OUTPUT_DATA_BUFFER output{};
    output.pSample = sample;
    DWORD status = 0;
    result = transform->ProcessOutput(0, 1, &output, &status);
    if (output.pEvents)
        output.pEvents->Release();
    if ((info.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES) && output.pSample)
        sample.Attach(output.pSample);
    return result;
}
void Start(IMFTransform* transform) {
    transform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
    transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
}
uint8_t Byte(int value) { return uint8_t(std::clamp(value, 0, 255)); }
std::vector<uint8_t> ToNv12(const uint8_t* pixels, unsigned sw, unsigned sh, unsigned pitch, bool bgra,
                            unsigned width, unsigned height) {
    std::vector<uint8_t> result(size_t(width) * height * 3 / 2);
    auto* uv = result.data() + size_t(width) * height;
    std::vector<unsigned> xs(width), ys(height);
    for (unsigned x = 0; x < width; ++x)
        xs[x] = unsigned(uint64_t(x) * sw / width);
    for (unsigned y = 0; y < height; ++y)
        ys[y] = unsigned(uint64_t(y) * sh / height);
    for (unsigned y = 0; y < height; y += 2)
        for (unsigned x = 0; x < width; x += 2) {
            int red = 0, green = 0, blue = 0;
            for (unsigned dy = 0; dy < 2; ++dy)
                for (unsigned dx = 0; dx < 2; ++dx) {
                    const auto* p = pixels + size_t(ys[y + dy]) * pitch + xs[x + dx] * 4;
                    const int r = p[bgra ? 2 : 0], g = p[1], b = p[bgra ? 0 : 2];
                    result[size_t(y + dy) * width + x + dx] =
                        Byte(((47 * r + 157 * g + 16 * b + 128) >> 8) + 16);
                    red += r;
                    green += g;
                    blue += b;
                }
            red /= 4;
            green /= 4;
            blue /= 4;
            uv[size_t(y / 2) * width + x] = Byte(((-26 * red - 87 * green + 112 * blue + 128) >> 8) + 128);
            uv[size_t(y / 2) * width + x + 1] =
                Byte(((112 * red - 102 * green - 10 * blue + 128) >> 8) + 128);
        }
    return result;
}
} // namespace

struct Encoder::State {
    Foundation foundation;
    CComPtr<IMFTransform> transform;
    unsigned width = 0, height = 0, fps = 0, bitrate = 0;
    uint64_t stream = 0, counter = 0;
    std::vector<uint8_t> header;
    std::string error;
};
Encoder::Encoder() : state_(std::make_unique<State>()) {}
Encoder::~Encoder() = default;
const std::string& Encoder::Error() const { return state_->error; }
std::optional<Frame> Encoder::Encode(const uint8_t* rgba, unsigned width, unsigned height, unsigned pitch,
                                     unsigned format, Data::Profile profile, unsigned bitrate,
                                     uint64_t stream, uint64_t sequence) {
    auto& s = *state_;
    s.error.clear();
    if (!Api().Ready()) {
        s.error = "Spectate requires Windows Media Foundation (Media Feature Pack on Windows N).";
        return {};
    }
    if (!rgba || !width || width > 8192 || !height || height > 4320 || pitch < uint64_t(width) * 4 ||
        uint64_t(pitch) * height > 128 * 1024 * 1024 || !stream || !sequence ||
        (format != 28 && format != 29 && format != 87 && format != 91) || profile.width < Data::MinWidth ||
        profile.width > Data::MaxWidth || profile.fps < Data::MinFps || profile.fps > Data::MaxFps) {
        s.error = "Unsupported video capture dimensions or format.";
        return {};
    }
    const double scale = (std::min)({1., double(profile.width) / width, double(Data::MaxHeight) / height});
    const unsigned w = unsigned(width * scale) & ~1u, h = unsigned(height * scale) & ~1u;
    if (w < 48 || h < 48) {
        s.error = "Video dimensions are below the H.264 minimum.";
        return {};
    }
    bitrate = std::clamp(bitrate, 128000u, 128000000u);
    if (!s.transform || s.width != w || s.height != h || s.fps != unsigned(profile.fps) ||
        s.bitrate != bitrate || s.stream != stream) {
        s.transform.Release();
        s.header.clear();
        s.counter = 0;
        if (!Check(s.foundation.startup, s.error, "Windows video startup") ||
            !Check(CoCreateInstance(CLSID_CMSH264EncoderMFT, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&s.transform)),
                   s.error, "H.264 encoder creation"))
            return {};
        Property(s.transform, CODECAPI_AVLowLatencyMode, 1, true);
        CComPtr<IMFAttributes> attributes;
        if (SUCCEEDED(s.transform->GetAttributes(&attributes)))
            attributes->SetUINT32(MF_LOW_LATENCY, TRUE);
        Property(s.transform, CODECAPI_AVEncCommonQualityVsSpeed, 0);
        Property(s.transform, CODECAPI_AVEncMPVDefaultBPictureCount, 0);
        Property(s.transform, CODECAPI_AVEncCommonRateControlMode, eAVEncCommonRateControlMode_CBR);
        Property(s.transform, CODECAPI_AVEncMPVGOPSize, unsigned(profile.fps));
        auto output = Type(MFVideoFormat_H264, w, h, unsigned(profile.fps));
        auto input = Type(MFVideoFormat_NV12, w, h, unsigned(profile.fps));
        if (!output || !input) {
            s.error = "H.264 media type allocation failed.";
            s.transform.Release();
            return {};
        }
        output->SetUINT32(MF_MT_AVG_BITRATE, bitrate);
        output->SetUINT32(MF_MT_MPEG2_PROFILE, eAVEncH264VProfile_Main);
        if (!Check(s.transform->SetOutputType(0, output, 0), s.error, "H.264 output configuration") ||
            !Check(s.transform->SetInputType(0, input, 0), s.error, "H.264 input configuration")) {
            s.transform.Release();
            return {};
        }
        CComPtr<IMFMediaType> actual;
        if (SUCCEEDED(s.transform->GetOutputCurrentType(0, &actual))) {
            UINT32 bytes = 0;
            if (SUCCEEDED(actual->GetBlobSize(MF_MT_MPEG_SEQUENCE_HEADER, &bytes)) && bytes <= 65536) {
                s.header.resize(bytes);
                if (FAILED(actual->GetBlob(MF_MT_MPEG_SEQUENCE_HEADER, s.header.data(), bytes, &bytes)))
                    s.header.clear();
            }
        }
        s.width = w;
        s.height = h;
        s.fps = unsigned(profile.fps);
        s.bitrate = bitrate;
        s.stream = stream;
        Start(s.transform);
    }
    const auto nv12 = ToNv12(rgba, width, height, pitch, format == 87 || format == 91, w, h);
    const auto duration = uint64_t(10000000 / s.fps);
    auto sample = Sample(nv12, s.counter++ * duration, duration);
    if (!sample || !Check(s.transform->ProcessInput(0, sample, 0), s.error, "H.264 frame input"))
        return {};
    CComPtr<IMFSample> output;
    const auto status = Output(s.transform, output);
    if (status == MF_E_TRANSFORM_NEED_MORE_INPUT)
        return {};
    if (!Check(status, s.error, "H.264 encoding") || !output)
        return {};
    CComPtr<IMFMediaBuffer> data;
    if (FAILED(output->ConvertToContiguousBuffer(&data)))
        return {};
    BYTE* pointer = nullptr;
    DWORD count = 0;
    if (FAILED(data->Lock(&pointer, nullptr, &count)))
        return {};
    Frame frame;
    frame.stream = stream;
    frame.sequence = sequence;
    frame.width = w;
    frame.height = h;
    frame.key = MFGetAttributeUINT32(output, MFSampleExtension_CleanPoint, FALSE) != FALSE;
    LONGLONG time = 0;
    output->GetSampleTime(&time);
    frame.time = uint64_t(time);
    if (count && count <= MaxFrameBytes) {
        if (frame.key)
            frame.bytes = s.header;
        frame.bytes.insert(frame.bytes.end(), pointer, pointer + count);
    }
    data->Unlock();
    if (frame.bytes.empty() || frame.bytes.size() > MaxFrameBytes) {
        s.error = "Encoded video frame exceeds its size limit.";
        return {};
    }
    return frame;
}

struct Decoder::State {
    Foundation foundation;
    CComPtr<IMFTransform> transform;
    uint64_t stream = 0, sequence = 0;
    unsigned width = 0, height = 0, stride = 0;
    std::string error;
    bool OutputType() {
        for (DWORD i = 0; i < 64; ++i) {
            CComPtr<IMFMediaType> type;
            if (FAILED(transform->GetOutputAvailableType(0, i, &type)))
                break;
            GUID format{};
            type->GetGUID(MF_MT_SUBTYPE, &format);
            if (format != MFVideoFormat_NV12)
                continue;
            UINT32 w = 0, h = 0;
            if (FAILED(MFGetAttributeSize(type, MF_MT_FRAME_SIZE, &w, &h)) || !w || w > 4096 || !h ||
                h > 2304)
                return false;
            if (FAILED(transform->SetOutputType(0, type, 0)))
                continue;
            width = w;
            height = h;
            stride = MFGetAttributeUINT32(type, MF_MT_DEFAULT_STRIDE, w);
            return stride >= width && stride <= 16384;
        }
        error = "H.264 decoder has no supported NV12 output format.";
        return false;
    }
};
Decoder::Decoder() : state_(std::make_unique<State>()) {}
Decoder::~Decoder() = default;
const std::string& Decoder::Error() const { return state_->error; }
std::shared_ptr<Pixels> Decoder::Decode(const Frame& frame) {
    auto& s = *state_;
    s.error.clear();
    if (!Api().Ready()) {
        s.error = "Spectate requires Windows Media Foundation (Media Feature Pack on Windows N).";
        return {};
    }
    if (!frame.stream || !frame.sequence || frame.bytes.empty() || frame.bytes.size() > MaxFrameBytes ||
        frame.width < 48 || frame.width > Data::MaxWidth || frame.height < 48 ||
        frame.height > Data::MaxHeight || (frame.width & 1) || (frame.height & 1)) {
        s.error = "Invalid H.264 frame.";
        return {};
    }
    if (s.stream == frame.stream && frame.sequence <= s.sequence)
        return {};
    if (!s.transform || s.stream != frame.stream) {
        if (!frame.key) {
            s.error = "Waiting for an H.264 keyframe.";
            return {};
        }
        s.transform.Release();
        s.sequence = 0;
        if (!Check(s.foundation.startup, s.error, "Windows video startup") ||
            !Check(CoCreateInstance(CLSID_CMSH264DecoderMFT, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&s.transform)),
                   s.error, "H.264 decoder creation"))
            return {};
        Property(s.transform, CODECAPI_AVLowLatencyMode, 1);
        CComPtr<IMFAttributes> attributes;
        if (SUCCEEDED(s.transform->GetAttributes(&attributes)))
            attributes->SetUINT32(MF_LOW_LATENCY, TRUE);
        auto input = Type(MFVideoFormat_H264, frame.width, frame.height, 30);
        if (!input || !Check(s.transform->SetInputType(0, input, 0), s.error, "H.264 decode input") ||
            !s.OutputType()) {
            s.transform.Release();
            return {};
        }
        s.stream = frame.stream;
        Start(s.transform);
    }
    auto input = Sample(frame.bytes, frame.time, 333333);
    if (!input || !Check(s.transform->ProcessInput(0, input, 0), s.error, "H.264 decode frame"))
        return {};
    CComPtr<IMFSample> sample;
    auto status = Output(s.transform, sample);
    if (status == MF_E_TRANSFORM_STREAM_CHANGE) {
        sample.Release();
        if (!s.OutputType())
            return {};
        status = Output(s.transform, sample);
    }
    s.sequence = frame.sequence;
    if (status == MF_E_TRANSFORM_NEED_MORE_INPUT)
        return {};
    if (!Check(status, s.error, "H.264 decoding") || !sample || s.width < frame.width ||
        s.height < frame.height)
        return {};
    CComPtr<IMFMediaBuffer> buffer;
    if (FAILED(sample->ConvertToContiguousBuffer(&buffer)))
        return {};
    BYTE* data = nullptr;
    DWORD bytes = 0;
    if (FAILED(buffer->Lock(&data, nullptr, &bytes)))
        return {};
    const size_t yBytes = size_t(s.stride) * s.height;
    if (yBytes + size_t(s.stride) * ((s.height + 1) / 2) > bytes) {
        buffer->Unlock();
        s.error = "Incomplete decoded video planes.";
        return {};
    }
    auto image = std::make_shared<Pixels>();
    image->width = frame.width;
    image->height = frame.height;
    image->rgba.resize(size_t(frame.width) * frame.height * 4);
    for (unsigned y = 0; y < frame.height; ++y)
        for (unsigned x = 0; x < frame.width; ++x) {
            const auto* uv = data + yBytes + size_t(y / 2) * s.stride + (x & ~1u);
            const int luma = int(data[size_t(y) * s.stride + x]) - 16, u = int(uv[0]) - 128,
                      v = int(uv[1]) - 128;
            auto* out = image->rgba.data() + (size_t(y) * frame.width + x) * 4;
            out[0] = Byte((298 * luma + 459 * v + 128) >> 8);
            out[1] = Byte((298 * luma - 55 * u - 136 * v + 128) >> 8);
            out[2] = Byte((298 * luma + 541 * u + 128) >> 8);
            out[3] = 255;
        }
    buffer->Unlock();
    return image;
}
} // namespace Spectate::Stream
