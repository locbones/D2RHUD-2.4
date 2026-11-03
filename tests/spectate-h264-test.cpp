#define NOMINMAX
#include "../d2rhud/plugin/SpectateH264.cpp"
#include <Windows.h>
#include <cassert>
#include <iostream>

int main() {
    using namespace Spectate::Stream;
    Encoder encoder;
    Decoder decoder;
    const unsigned width = 1920, height = 1080;
    std::vector<uint8_t> rgba(size_t(width) * height * 4);
    size_t total = 0, keyBytes = 0, interBytes = 0, decoded = 0;
    const auto start = GetTickCount64();
    std::vector<uint8_t> last;
    for (unsigned n = 0; n < 60; ++n) {
        for (unsigned y = 0; y < height; ++y)
            for (unsigned x = 0; x < width; ++x) {
                const auto at = (size_t(y) * width + x) * 4;
                rgba[at] = uint8_t(x * 255 / width);
                rgba[at + 1] = uint8_t(y * 255 / height);
                rgba[at + 2] = (x >= n * 8 && x < n * 8 + 120 && y > 200 && y < 400) ? 200 : 33;
                rgba[at + 3] = 255;
            }
        auto frame =
            encoder.Encode(rgba.data(), width, height, width * 4, 28, {30, 1920, 80}, 6000000, 1, n + 1);
        if (!frame)
            std::cerr << encoder.Error() << '\n';
        assert(frame && frame->width == width && frame->height == height);
        if (!n)
            assert(frame->key);
        total += frame->bytes.size();
        (frame->key ? keyBytes : interBytes) += frame->bytes.size();
        auto image = decoder.Decode(*frame);
        if (!image)
            std::cerr << decoder.Error() << '\n';
        assert(image && image->width == width && image->height == height);
        const auto at = (size_t(height / 2) * width + width / 2) * 4;
        assert(abs(int(image->rgba[at]) - 127) < 12 && abs(int(image->rgba[at + 1]) - 127) < 12 &&
               abs(int(image->rgba[at + 2]) - 33) < 12 && image->rgba[at + 3] == 255);
        ++decoded;
        assert(!decoder.Decode(*frame)); // Replays do not advance a video stream.
    }
    const auto elapsed = GetTickCount64() - start;
    assert(decoded == 60 && keyBytes && interBytes && total < 3000000);
    auto resized = encoder.Encode(rgba.data(), width, height, width * 4, 28, {30, 1280, 95}, 6000000, 2, 1);
    assert(resized && resized->key && resized->width == 1280 && resized->height == 720);
    auto image = decoder.Decode(*resized);
    assert(image && image->width == 1280 && image->height == 720);
    auto invalid = *resized;
    invalid.stream = 3;
    invalid.key = false;
    assert(!decoder.Decode(invalid)); // A late viewer waits for a new keyframe.
    invalid = *resized;
    invalid.width = 99999;
    assert(!decoder.Decode(invalid));
    assert(!encoder.Encode(nullptr, width, height, width * 4, 28, {30, 1920, 80}, 6000000, 1, 1));
    const auto startup = Api().startup;
    Api().startup = nullptr;
    {
        Encoder unavailable;
        assert(!unavailable.Encode(rgba.data(), width, height, width * 4, 28, {30, 1920, 80}, 6000000, 3, 1));
        assert(unavailable.Error().find("Media Foundation") != std::string::npos);
        Decoder missingDecoder;
        assert(!missingDecoder.Decode(*resized) &&
               missingDecoder.Error().find("Media Foundation") != std::string::npos);
    }
    Api().startup = startup;
    std::cout << "PASS: 60 complete 1080p RGBA -> NV12 -> H.264 -> NV12 -> RGBA frames in " << elapsed
              << " ms (" << decoded * 1000. / elapsed << " fps); " << total << " bytes, "
              << total * 8. * 30 / decoded / 1000000
              << " Mbps; inter frames, keyframes, color, live resize, "
                 "late-join and malformed-frame guards\n";
}
