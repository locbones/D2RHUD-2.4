// Previous JPEG encoder, retained only for bandwidth comparison tests. Not linked into the HUD.
#include "../d2rhud/plugin/Spectate.h"
#include <atlbase.h>
#include <shlwapi.h>
#include <wincodec.h>
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "shlwapi.lib")

namespace Spectate::Video {
namespace {
struct Apartment {
    HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ~Apartment() {
        if (SUCCEEDED(result))
            CoUninitialize();
    }
};
CComPtr<IWICImagingFactory> Factory() {
    CComPtr<IWICImagingFactory> result;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&result));
    return result;
}
struct Attempt {
    std::vector<uint8_t> bytes;
    bool oversized = false;
};
Attempt Compress(IWICImagingFactory* factory, IWICBitmap* bitmap, unsigned w, unsigned h, int qualityValue) {
    CComPtr<IWICBitmapScaler> scaler;
    if (FAILED(factory->CreateBitmapScaler(&scaler)) ||
        FAILED(scaler->Initialize(bitmap, w, h, WICBitmapInterpolationModeFant)))
        return {};
    CComPtr<IStream> stream;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream)))
        return {};
    CComPtr<IWICBitmapEncoder> encoder;
    CComPtr<IWICBitmapFrameEncode> frame;
    CComPtr<IPropertyBag2> properties;
    if (FAILED(factory->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, &encoder)) ||
        FAILED(encoder->Initialize(stream, WICBitmapEncoderNoCache)) ||
        FAILED(encoder->CreateNewFrame(&frame, &properties)))
        return {};
    PROPBAG2 property{};
    property.pstrName = const_cast<wchar_t*>(L"ImageQuality");
    VARIANT quality{};
    quality.vt = VT_R4;
    quality.fltVal = qualityValue / 100.f;
    if (!properties || FAILED(properties->Write(1, &property, &quality)))
        return {};
    // Keep colored text/lines sharp at high quality: the default JPEG chroma
    // subsampling throws away color detail even at the maximum quality.
    if (qualityValue >= 90) {
        property.pstrName = const_cast<wchar_t*>(L"JpegYCrCbSubsampling");
        VARIANT subsampling{};
        subsampling.vt = VT_UI1;
        subsampling.bVal = WICJpegYCrCbSubsampling444;
        if (FAILED(properties->Write(1, &property, &subsampling)))
            return {};
    }
    WICPixelFormatGUID pixel = GUID_WICPixelFormat24bppBGR;
    if (FAILED(frame->Initialize(properties)) || FAILED(frame->SetSize(w, h)) ||
        FAILED(frame->SetPixelFormat(&pixel)))
        return {};
    CComPtr<IWICFormatConverter> converter;
    if (FAILED(factory->CreateFormatConverter(&converter)) ||
        FAILED(converter->Initialize(scaler, pixel, WICBitmapDitherTypeNone, nullptr, 0,
                                     WICBitmapPaletteTypeCustom)) ||
        FAILED(frame->WriteSource(converter, nullptr)) || FAILED(frame->Commit()) ||
        FAILED(encoder->Commit()))
        return {};
    STATSTG stat{};
    HGLOBAL memory = nullptr;
    if (FAILED(stream->Stat(&stat, STATFLAG_NONAME)) || !stat.cbSize.QuadPart)
        return {};
    if (stat.cbSize.QuadPart > Data::MaxBytes)
        return {{}, true};
    if (FAILED(GetHGlobalFromStream(stream, &memory)))
        return {};
    const auto pointer = static_cast<const uint8_t*>(GlobalLock(memory));
    if (!pointer)
        return {};
    std::vector<uint8_t> bytes(pointer, pointer + size_t(stat.cbSize.QuadPart));
    GlobalUnlock(memory);
    return {std::move(bytes), false};
}
} // namespace
std::vector<uint8_t> Encode(const uint8_t* data, unsigned width, unsigned height, unsigned pitch,
                            unsigned format, Settings settings) {
    if (!data || !width || width > 8192 || !height || height > 4320 || pitch < uint64_t(width) * 4 ||
        uint64_t(pitch) * height > 128 * 1024 * 1024)
        return {};
    const bool rgba = format == 28 || format == 29, bgra = format == 87 || format == 91;
    if (!rgba && !bgra)
        return {}; // Never reinterpret HDR/packed pixels as RGBA.
    Apartment apartment;
    const auto factory = Factory();
    if (!factory)
        return {};
    CComPtr<IWICBitmap> bitmap;
    if (FAILED(factory->CreateBitmapFromMemory(
            width, height, rgba ? GUID_WICPixelFormat32bppRGBA : GUID_WICPixelFormat32bppBGRA, pitch,
            pitch * height, const_cast<BYTE*>(data), &bitmap)))
        return {};
    settings.width = std::clamp(settings.width, Data::MinWidth, Data::MaxWidth);
    settings.quality = std::clamp(settings.quality, Data::MinQuality, Data::MaxQuality);
    for (;;) {
        const double scale =
            (std::min)({1., double(settings.width) / width, double(Data::MaxHeight) / height});
        auto attempt = Compress(factory, bitmap, (std::max)(1u, unsigned(width * scale)),
                                (std::max)(1u, unsigned(height * scale)), settings.quality);
        if (!attempt.oversized)
            return std::move(attempt.bytes);
        // Reuse the full-size source bitmap; each retry retires its encoder and
        // scaled image before allocating another. Bounded memory in busy scenes.
        if (settings.width == Data::MinWidth)
            return {};
        settings.width = (std::max)(Data::MinWidth, settings.width * 3 / 4);
    }
}
std::shared_ptr<Image> Decode(std::span<const uint8_t> jpeg) {
    if (jpeg.size() < 4 || jpeg.size() > Data::MaxBytes || jpeg[0] != 0xff || jpeg[1] != 0xd8)
        return {};
    Apartment apartment;
    const auto factory = Factory();
    if (!factory)
        return {};
    CComPtr<IStream> stream;
    stream.Attach(SHCreateMemStream(jpeg.data(), UINT(jpeg.size())));
    if (!stream)
        return {};
    CComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad, &decoder)))
        return {};
    GUID container{};
    UINT frames = 0;
    if (FAILED(decoder->GetContainerFormat(&container)) || container != GUID_ContainerFormatJpeg ||
        FAILED(decoder->GetFrameCount(&frames)) || frames != 1)
        return {};
    CComPtr<IWICBitmapFrameDecode> frame;
    unsigned width = 0, height = 0;
    if (FAILED(decoder->GetFrame(0, &frame)) || FAILED(frame->GetSize(&width, &height)) || !width ||
        width > Data::MaxWidth || !height || height > Data::MaxHeight)
        return {};
    CComPtr<IWICFormatConverter> converter;
    if (FAILED(factory->CreateFormatConverter(&converter)) ||
        FAILED(converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0,
                                     WICBitmapPaletteTypeCustom)))
        return {};
    auto image = std::make_shared<Image>();
    image->width = int(width);
    image->height = int(height);
    image->rgba.resize(size_t(width) * height * 4);
    if (FAILED(converter->CopyPixels(nullptr, width * 4, UINT(image->rgba.size()), image->rgba.data())))
        return {};
    return image;
}
} // namespace Spectate::Video
