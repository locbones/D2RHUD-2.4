#define NOMINMAX
#include <Windows.h>
#include "../d2rhud/Pattern.h"
#include "../d2rhud/plugin/D2RHUD/CameraController.cpp"
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
int checks = 0;
void Check(bool value, const char* message) { ++checks; if (!value) throw std::runtime_error(message); }
uint8_t gameFlag = 0, loadingFlag = 0;
uint64_t nativeSession = 0;
std::atomic<int> addressCalls{0};
uintptr_t nativeAngles = 0, nativeZoom = 0;
struct Fixture {
    uintptr_t base, angles, zoom;
    bool allocated = true;
    explicit Fixture(size_t pitchOffset = 68) {
        base = reinterpret_cast<uintptr_t>(VirtualAlloc(nullptr, 8192, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        if (!base) throw std::runtime_error("fixture allocation");
        angles = base + pitchOffset; zoom = base + 512;
        const std::array<float,4> values{0.25f, -0.8f, 0.2f, 0.36f};
        std::memcpy(reinterpret_cast<void*>(angles - 4), values.data(), sizeof(values));
        *reinterpret_cast<float*>(zoom) = 1.25f;
    }
    ~Fixture() { Free(); }
    void Free() { if (allocated) { VirtualFree(reinterpret_cast<void*>(base), 0, MEM_RELEASE); allocated = false; } }
    float Pitch() const { return *reinterpret_cast<float*>(angles); }
    float Zoom() const { return *reinterpret_cast<float*>(zoom); }
    HudCamera::Operations Ops(std::atomic<int>& scans, std::atomic<int>& reads, std::atomic<int>& writes) const {
        return {
            [&, a = angles, z = zoom](const char* pattern) { ++scans; return pattern[0] == '4' ? a : z - 0x32; },
            [&](uintptr_t source, void* output, size_t size) { ++reads; return HudCamera::Read(source, output, size); },
            [&](uintptr_t target, const void* value, size_t size) { ++writes; return HudCamera::Write(target, value, size); }
        };
    }
};
void Drain(HudCamera::Controller& controller, uint64_t session = 1, bool inGame = true, bool loading = false) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (controller.Busy() && std::chrono::steady_clock::now() < until) {
        controller.Tick(session, inGame, loading);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Check(!controller.Busy(), "worker completes within bounded wait");
}
void BasicTests() {
    Fixture fixture;
    std::atomic<int> scans{0}, reads{0}, writes{0};
    HudCamera::Controller camera(fixture.Ops(scans, reads, writes));
    camera.Request();
    Check(!camera.Busy() && scans == 0, "no scan outside a game");
    camera.Tick(1, true, false); camera.Request(); Drain(camera);
    Check(camera.View().valuesFound && camera.View().zoomFound, "checked native capture");
    Check(scans == 2 && reads == 2 && writes == 0, "one worker scans both signatures and reads each block once");
    Check(camera.View().pitch == -0.8f && camera.View().height == 0.2f && camera.View().pan == 0.36f && camera.View().roll == 0.25f, "native angle layout preserved");
    Check(camera.View().defaultZoom == 1.25f && camera.View().defaultPitch == -0.8f, "defaults captured");
    for (int i = 0; i < 10000; ++i) { camera.Tick(1, true, false); camera.Request(); }
    Check(scans == 2 && reads == 2 && writes == 0, "ready frames perform no scans or native memory operations");
    camera.View().pitch = 1.5f; camera.View().height = 2.5f; camera.View().pan = 3.5f; camera.View().roll = 4.5f;
    Check(camera.ApplyAngles(), "apply angles");
    const auto values = reinterpret_cast<const float*>(fixture.angles - 4);
    Check(values[0] == 4.5f && values[1] == 1.5f && values[2] == 2.5f && values[3] == 3.5f, "all four fields written in native order");
    Check(writes == 1, "one native write replaces four raw stores");
    camera.View().zoom = 2.25f; Check(camera.ApplyZoom() && fixture.Zoom() == 2.25f, "apply zoom");
    Check(camera.View().defaultPitch == -0.8f && camera.View().defaultZoom == 1.25f, "applying preserves defaults");
    camera.View().pitch = camera.View().defaultPitch; camera.View().zoom = camera.View().defaultZoom;
    Check(camera.ApplyAngles() && camera.ApplyZoom() && fixture.Pitch() == -0.8f && fixture.Zoom() == 1.25f, "reset restores defaults");
    camera.View().programEnabled = false;
    const auto before = writes.load();
    Check(!camera.ApplyAngles() && !camera.ApplyZoom() && writes == before, "disabled controller performs no writes");
    camera.Tick(1, false, false);
    Check(!camera.View().valuesFound && !camera.View().zoomFound && !camera.View().available, "exit invalidates cached addresses");
    Check(!camera.ApplyAngles() && !camera.ApplyZoom() && writes == before, "no writes after exit");
    camera.Tick(0, true, false); camera.Request(); Drain(camera, 0);
    Check(camera.View().valuesFound && camera.View().zoomFound, "TCP/IP client with zero local game token supported");
    Check(!camera.View().programEnabled, "disabled preference survives boundary reset");
    const auto epoch = camera.Generation(); camera.Tick(0, true, true);
    Check(camera.Generation() != epoch && !camera.View().available && camera.View().loading, "act loading quarantines camera");
    camera.Request(true); Check(!camera.Busy(), "no scan during loading");
    camera.Tick(0, true, false); camera.Request(); Drain(camera, 0);
    Check(camera.View().valuesFound && camera.View().zoomFound, "camera can resolve after loading");
}
void StaleTests() {
    for (int boundary = 0; boundary < 3; ++boundary) {
        Fixture fixture;
        std::atomic<int> scans{0}, reads{0}, writes{0};
        auto operations = fixture.Ops(scans, reads, writes);
        std::promise<void> entered, release;
        auto enteredFuture = entered.get_future(); auto gate = release.get_future().share();
        operations.scan = [&, gate](const char* pattern) {
            if (++scans == 1) { entered.set_value(); gate.wait_for(std::chrono::seconds(5)); }
            return pattern[0] == '4' ? fixture.angles : fixture.zoom - 0x32;
        };
        HudCamera::Controller camera(operations);
        camera.Tick(1, true, false); camera.Request();
        Check(enteredFuture.wait_for(std::chrono::seconds(5)) == std::future_status::ready, "worker entered gated scan");
        for (int i = 0; i < 2000; ++i) camera.Request(true);
        Check(scans == 1, "repeated requests do not create additional workers");
        const auto epoch = camera.Generation();
        const uint64_t session = boundary == 0 ? 2 : 1;
        const bool inGame = boundary != 1, loading = boundary == 2;
        camera.Tick(session, inGame, loading);
        Check(camera.Generation() != epoch, "boundary changes scan generation");
        *reinterpret_cast<float*>(fixture.angles) = 0.75f; // Same address reused for the next camera.
        release.set_value(); Drain(camera, session, inGame, loading);
        Check(!camera.View().valuesFound && !camera.View().zoomFound && reads == 0 && writes == 0, "late result never dereferenced or published across boundary");
        Check(scans == 1, "stale worker skips second full-process scan");
        camera.Tick(2, true, false); camera.Request(); Drain(camera, 2);
        Check(camera.View().valuesFound && camera.View().defaultPitch == 0.75f, "new epoch captures fresh defaults even at same address");
        Check(scans == 3, "one fresh pair of scans after stale task drains");
    }
}
void FailureTests() {
    Fixture fixture;
    std::atomic<int> scans{0}, reads{0}, writes{0};
    std::atomic<bool> fail{true};
    auto operations = fixture.Ops(scans, reads, writes);
    operations.scan = [&](const char* pattern) -> uintptr_t {
        ++scans;
        if (pattern[0] == '4') { if (fail) throw std::runtime_error("scan failure"); return fixture.angles; }
        return fixture.zoom - 0x32;
    };
    HudCamera::Controller camera(operations);
    camera.Tick(1, true, false); camera.Request(); Drain(camera);
    Check(!camera.View().valuesFound && camera.View().zoomFound, "angle exception does not escape or prevent zoom capture");
    for (int i = 0; i < 1000; ++i) camera.Request();
    Check(scans == 2 && !camera.Busy(), "failed scan does not spin on every frame");
    fail = false; camera.Request(true); Drain(camera);
    Check(camera.View().valuesFound && camera.View().zoomFound && scans == 3, "page reopen retries only missing channel");
    const auto oldWrites = writes.load();
    camera.View().pitch = std::numeric_limits<float>::quiet_NaN();
    camera.View().zoom = std::numeric_limits<float>::infinity();
    Check(!camera.ApplyAngles() && !camera.ApplyZoom() && writes == oldWrites, "non-finite UI/preset values rejected");
    Check(camera.View().valuesFound && camera.View().zoomFound, "invalid edit does not discard valid addresses");
    camera.View().pitch = 0.5f; camera.View().zoom = 2;
    DWORD previous = 0, ignored = 0;
    Check(VirtualProtect(reinterpret_cast<void*>(fixture.base), 8192, PAGE_READONLY, &previous) != 0, "readonly fixture");
    Check(!camera.ApplyAngles() && !camera.ApplyZoom(), "readonly camera is rejected rather than force-written");
    Check(fixture.Pitch() == -0.8f && fixture.Zoom() == 1.25f, "readonly native values unchanged");
    Check(!camera.View().valuesFound && !camera.View().zoomFound, "failed writes invalidate affected addresses");
    VirtualProtect(reinterpret_cast<void*>(fixture.base), 8192, previous, &ignored);
    camera.Request(true); Drain(camera); fixture.Free();
    Check(!camera.ApplyAngles() && !camera.ApplyZoom(), "freed camera memory cannot crash a slider write");
    Check(!camera.View().valuesFound && !camera.View().zoomFound, "freed addresses discarded");

    Fixture crossing(4092);
    HudCamera::Controller split(crossing.Ops(scans, reads, writes));
    split.Tick(1, true, false); split.Request(); Drain(split);
    VirtualProtect(reinterpret_cast<void*>(crossing.base + 4096), 4096, PAGE_READONLY, &previous);
    split.View().pitch = 9; split.View().roll = 8;
    Check(!split.ApplyAngles() && crossing.Pitch() == -0.8f && *reinterpret_cast<float*>(crossing.angles - 4) == 0.25f, "whole write range checked before any cross-page mutation");
    VirtualProtect(reinterpret_cast<void*>(crossing.base + 4096), 4096, previous, &ignored);

    Fixture invalid;
    *reinterpret_cast<float*>(invalid.angles) = std::numeric_limits<float>::infinity();
    *reinterpret_cast<float*>(invalid.zoom) = std::numeric_limits<float>::quiet_NaN();
    HudCamera::Controller badFloats(invalid.Ops(scans, reads, writes));
    badFloats.Tick(1, true, false); badFloats.Request(); Drain(badFloats);
    Check(!badFloats.View().valuesFound && !badFloats.View().zoomFound, "non-finite native captures rejected");
    auto badOps = invalid.Ops(scans, reads, writes);
    badOps.scan = [](const char* pattern) { return pattern[0] == '4' ? uintptr_t(1) : (std::numeric_limits<uintptr_t>::max)() - 10; };
    HudCamera::Controller overflow(badOps);
    overflow.Tick(1, true, false); overflow.Request(); Drain(overflow);
    Check(!overflow.View().valuesFound && !overflow.View().zoomFound, "angle underflow and zoom offset overflow rejected");
    SetLastError(4567); float value = 0;
    Check(!HudCamera::Read(1, &value, sizeof(value)) && GetLastError() == 4567, "failed native read preserves Win32 last error");
    SetLastError(7654);
    Check(!HudCamera::Write(1, &value, sizeof(value)) && GetLastError() == 7654, "failed native write preserves Win32 last error");
}
void LoadingPreservationTests() {
    Fixture first, second;
    const Fixture* current = &first;
    std::atomic<int> scans{0}, reads{0}, writes{0};
    auto operations = first.Ops(scans, reads, writes);
    operations.scan = [&](const char* pattern) { ++scans; return pattern[0] == '4' ? current->angles : current->zoom - 0x32; };
    HudCamera::Controller camera(operations);
    camera.Tick(1, true, false); camera.Request(); Drain(camera);
    const auto revision = camera.SessionRevision(), capture = camera.CaptureRevision();
    camera.View().pitch = 8; camera.View().zoom = 2;
    Check(camera.ApplyAngles() && camera.ApplyZoom(), "customize camera before act travel");
    const auto readCount = reads.load(), writeCount = writes.load();
    camera.Tick(1, true, true);
    Check(!camera.ApplyAngles() && !camera.ApplyZoom() && writes == writeCount, "quarantined bindings cannot write while loading");
    for (int i = 0; i < 1000; ++i) { camera.Tick(1, true, true); camera.Request(); }
    Check(reads == readCount && scans == 2, "loading frames neither read camera blocks nor start scans");
    camera.Tick(1, true, false); camera.Request();
    Check(reads == readCount + 2 && scans == 2 && !camera.Busy(), "resume revalidates both known bindings without rescanning edited signature");
    Check(camera.View().available && camera.View().valuesFound && camera.View().zoomFound, "customized camera usable after travel");
    Check(camera.View().pitch == 8 && camera.View().zoom == 2 && first.Pitch() == 8 && first.Zoom() == 2, "travel preserves custom values and pending UI edits");
    Check(camera.View().defaultPitch == -0.8f && camera.View().defaultZoom == 1.25f, "travel does not replace original defaults with custom values");
    Check(camera.SessionRevision() == revision && camera.CaptureRevision() == capture, "same camera does not reset preset selection or reapply autoload");

    camera.Tick(1, true, true); current = &second; first.Free();
    camera.Tick(1, true, false);
    Check(!camera.View().valuesFound && !camera.View().zoomFound, "camera freed by act load loses its binding");
    camera.Request(); Drain(camera);
    Check(camera.View().valuesFound && camera.View().zoomFound && scans == 4, "replacement camera can resolve after travel");
    Check(camera.CaptureRevision() > capture && camera.SessionRevision() == revision, "replacement camera rearms preset autoload without changing selected slot");
    Check(camera.View().defaultPitch == -0.8f && camera.View().defaultZoom == 1.25f, "replacement camera captures valid defaults");

    Fixture freedDuringScan;
    HudCamera::Controller pending(freedDuringScan.Ops(scans, reads, writes));
    pending.Tick(1, true, false); pending.Request(); freedDuringScan.Free(); Drain(pending);
    Check(!pending.View().valuesFound && !pending.View().zoomFound, "address freed before result publication is safely rejected");
}
void IdleAndNativeBoundaryTests() {
    // No native flag lookups at all while the camera has never been requested.
    addressCalls = 0;
    const auto begin = std::chrono::steady_clock::now();
    for (int i = 0; i < 100000; ++i) HudCamera::Tick(false);
    Check(addressCalls == 0, "100000 hidden idle frames skip native state lookups");
    std::cout << "100000 hidden idle ticks: " << std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count()
        << " ms; no native lookups or scan workers. Not an FPS benchmark.\n";
    Fixture fixture;
    nativeAngles = fixture.angles; nativeZoom = fixture.zoom;
    gameFlag = 1; loadingFlag = 0; nativeSession = 9;
    HudCamera::Tick(); auto& camera = HudCamera::Runtime(); camera.Request(); Drain(camera, HudSession::Current());
    Check(camera.View().valuesFound && camera.View().zoomFound, "production runtime adapter captures test addresses");
    const auto snapshot=HudFrame::Capture();addressCalls=0;
    for(int i=0;i<10000;++i)HudCamera::Tick(snapshot,true);
    Check(addressCalls==0,"10000 shared-frame camera ticks avoid repeated native context sampling");
    camera.View().pitch = 3; loadingFlag = 1;
    Check(!HudCamera::ApplyAngles() && fixture.Pitch() == -0.8f, "native loading flag rechecked immediately before write");
    loadingFlag = 0; HudCamera::Tick(); camera.Request(); Drain(camera, HudSession::Current());
    camera.View().zoom = 3; nativeSession = 10;
    Check(!HudCamera::ApplyZoom() && fixture.Zoom() == 1.25f, "native game token rechecked immediately before write");
    HudCamera::Tick(); camera.Request(); Drain(camera, HudSession::Current());
    auto changedCharacter = HudFrame::Capture(); changedCharacter.character = 123;
    HudCamera::Tick(changedCharacter, true);
    const auto oldEpoch = HudSession::Current();
    changedCharacter.character = 456;
    HudCamera::Tick(changedCharacter, true);
    Check(HudSession::Current() != oldEpoch && !camera.View().valuesFound && !camera.View().zoomFound,
        "character change invalidates camera addresses even with the same native game token");
    camera.View().pitch = 3; gameFlag = 0;
    Check(!HudCamera::ApplyAngles() && fixture.Pitch() == -0.8f, "native game exit rechecked immediately before write");
}
}
DWORD64 Pattern::Address(uint32_t rva) {
    ++addressCalls;
    if (rva == 0x1dc76f8) return reinterpret_cast<DWORD64>(&gameFlag);
    if (rva == 0x1cb410a) return reinterpret_cast<DWORD64>(&loadingFlag);
    if (rva == 0x1d617d0) return reinterpret_cast<DWORD64>(&nativeSession);
    return 0;
}
DWORD64 Pattern::ScanProcess(const char* pattern) { return pattern[0] == '4' ? nativeAngles : nativeZoom - 0x32; }
int main() {
    try { BasicTests(); StaleTests(); FailureTests(); LoadingPreservationTests(); IdleAndNativeBoundaryTests();
        std::cout << "PASS: " << checks << " camera controller checks; private test memory only.\n"; return 0; }
    catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
