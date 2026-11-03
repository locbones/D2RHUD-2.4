#pragma once
#include "SpectateData.h"
#include <optional>

namespace Spectate::Stream {
inline constexpr size_t MaxFrameBytes = 4 * 1024 * 1024;
struct Frame {
    uint64_t stream = 0, sequence = 0, time = 0;
    uint32_t width = 0, height = 0;
    bool key = false;
    std::vector<uint8_t> bytes;
};
struct Pixels {
    unsigned width = 0, height = 0;
    std::vector<uint8_t> rgba;
};
// Instances belong to one persistent video worker. Retaining the transforms
// between frames is essential: P frames reuse the preceding decoded image.
class Encoder {
    struct State;
    std::unique_ptr<State> state_;

  public:
    Encoder();
    ~Encoder();
    std::optional<Frame> Encode(const uint8_t* rgba, unsigned width, unsigned height, unsigned pitch,
                                unsigned format, Data::Profile, unsigned bitrate, uint64_t stream,
                                uint64_t sequence);
    const std::string& Error() const;
};
class Decoder {
    struct State;
    std::unique_ptr<State> state_;

  public:
    Decoder();
    ~Decoder();
    std::shared_ptr<Pixels> Decode(const Frame&);
    const std::string& Error() const;
};
} // namespace Spectate::Stream
