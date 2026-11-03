#pragma once
#include "D2RHUD/FrameContext.h"
#include "SpectateData.h"
#include "SpectateH264.h"
#include <Windows.h>
#include <optional>
class D2GameStrc;
class D2UnitStrc;
namespace Spectate {
struct Settings {
    bool enabled = true, sharing = true;
    int fps = 6, width = 640, quality = 55;
    int bandwidthMbps = 16;
    int videoPort = 6113;
};
void Configure(Settings settings);
Settings Configuration();
void LoadSettings();
struct Peer {
    uint32_t id = 0;
    std::string name;
    bool sharing = false, ready = false;
    uint64_t seen = 0;
};
struct Image {
    int width = 0, height = 0;
    std::vector<uint8_t> rgba;
    uint64_t revision = 0;
};
struct View {
    bool open = false, fullscreen = false, inGame = false, ready = false;
    uint32_t target = 0, request = 0;
    uint64_t generation = 0, receivedAt = 0;
    float receivedFps = 0;
    std::string name, status, videoError;
    std::vector<Peer> peers;
    uint64_t revision = 0;
};
void ClientTick(const HudFrame::Context& frame, uint64_t session);
void Receive(const uint8_t* bytes, size_t size);
bool Open(uint32_t player);
void Close();
void Fullscreen(bool enabled);
View Current();
bool BlocksGameInput();
bool VideoDemand();
void SetInputRegion(float x, float y, float width, float height);
void AddInputRegion(float x, float y, float width, float height);
bool WindowInput(HWND window, UINT message, WPARAM key, LPARAM parameter);
void Draw(bool showParty = true);
void DrawOptions();
void SetAvailable(bool enabled);
bool Command(D2GameStrc*, D2UnitStrc*, const uint8_t*, size_t);
void Tick(D2GameStrc*);
void Destroyed(D2GameStrc*);
namespace Native {
std::optional<uint32_t> LocalId();
bool TcpGame();
bool Hosting();
uint16_t GamePort();
const char* CompatibilityError();
bool CaptureReady();
void Send(const Data::CommandPacket&);
struct Eye {
    uint32_t player;
    float x, y, size;
    bool available;
    std::string name;
};
std::vector<Eye> PartyEyes(float width, float height, const std::vector<Peer>& peers);
} // namespace Native
struct CaptureRequest {
    uint32_t frame = 0;
    uint64_t generation = 0;
    Settings settings;
    uint64_t stream = 0;
    uint32_t bitrate = 0;
};
std::optional<CaptureRequest> TakeCapture();
void Submit(const CaptureRequest& request, Stream::Frame frame);
void Displayed(uint64_t generation, uint64_t revision);
void VideoError(std::string error);
void CaptureFailed(const CaptureRequest& request);
namespace Video {
uint64_t Texture();
std::shared_ptr<const Image> DisplayImage();
void Reset();
} // namespace Video
} // namespace Spectate
