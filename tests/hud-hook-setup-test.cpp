#include <Windows.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <iostream>
#include <vector>
#include "../d2rhud/plugin/D2RHUD/HudHookSetup.cpp"

namespace {
int checks = 0;
void Check(bool value, const char* message) {
    ++checks;
    if (!value) { std::cerr << "FAILED: " << message << '\n'; std::exit(1); }
}
using Fn = int(__fastcall*)(int);
Fn originals[7]{};
template<int N> __declspec(noinline) int Native(int input) {
    volatile int value = input + N + 3;
    value = value * (N + 1);
    return value + 17;
}
template<int N> __declspec(noinline) int Replacement(int input) { return originals[N](input) + 1000 + N; }
__declspec(noinline) int Call(Fn function, int input) { return function(input); }
const std::array<Fn,7> targets{Native<0>, Native<1>, Native<2>, Native<3>, Native<4>, Native<5>, Native<6>};
const std::array<Fn,7> replacements{Replacement<0>, Replacement<1>, Replacement<2>, Replacement<3>, Replacement<4>, Replacement<5>, Replacement<6>};

enum class Fault { None, Begin, Update, Attach, Commit };
struct Calls { int begin = 0, update = 0, attach = 0, commit = 0, abort = 0; } calls;
Fault fault = Fault::None;
int failAt = 0;
bool transactionActive = false, real = false;
LONG WINAPI Begin() {
    ++calls.begin;
    if (fault == Fault::Begin) return ERROR_ACCESS_DENIED;
    transactionActive = true;
    return real ? DetourTransactionBegin() : NO_ERROR;
}
LONG WINAPI Update(HANDLE thread) {
    ++calls.update;
    Check(thread == GetCurrentThread(), "transaction enlists current thread");
    if (fault == Fault::Update) return ERROR_INVALID_HANDLE;
    return real ? DetourUpdateThread(thread) : NO_ERROR;
}
LONG WINAPI Attach(PVOID* slot, PVOID replacement) {
    const auto index = calls.attach++;
    if (fault == Fault::Attach && index == failAt) return ERROR_INVALID_BLOCK;
    if (real) return DetourAttach(slot, replacement);
    // Deliberately mutate the slots even before commit to test our rollback,
    // independently of Detours' own pointer publication behavior.
    *slot = reinterpret_cast<PVOID>(uintptr_t(0x100000 + index * 16));
    return NO_ERROR;
}
LONG WINAPI Commit() {
    ++calls.commit; transactionActive = false;
    if (fault == Fault::Commit) return ERROR_INVALID_DATA;
    return real ? DetourTransactionCommit() : NO_ERROR;
}
LONG WINAPI Abort() {
    ++calls.abort;
    Check(transactionActive, "abort only used for active transaction");
    transactionActive = false;
    return real ? DetourTransactionAbort() : NO_ERROR;
}
HudHooks::Operations Operations() { return {Begin, Update, Attach, Commit, Abort}; }
HudHooks::Installer Seven(HudHooks::Operations operations) {
    using HudHooks::Bind;
    return HudHooks::Installer("Core fixture", {
        Bind(originals[0], replacements[0], reinterpret_cast<uintptr_t>(targets[0]), "hook zero"),
        Bind(originals[1], replacements[1], reinterpret_cast<uintptr_t>(targets[1]), "hook one"),
        Bind(originals[2], replacements[2], reinterpret_cast<uintptr_t>(targets[2]), "hook two"),
        Bind(originals[3], replacements[3], reinterpret_cast<uintptr_t>(targets[3]), "hook three"),
        Bind(originals[4], replacements[4], reinterpret_cast<uintptr_t>(targets[4]), "hook four"),
        Bind(originals[5], replacements[5], reinterpret_cast<uintptr_t>(targets[5]), "hook five"),
        Bind(originals[6], replacements[6], reinterpret_cast<uintptr_t>(targets[6]), "hook six")
    }, operations);
}
void Reset() { calls = {}; fault = Fault::None; failAt = 0; transactionActive = false; real = false; std::fill(std::begin(originals), std::end(originals), nullptr); }
void FailureTests() {
    using HudHooks::Outcome;
    for (const auto stage : {Fault::Begin, Fault::Update, Fault::Attach, Fault::Commit}) {
        for (int position = 0; position < (stage == Fault::Attach ? 7 : 1); ++position) {
            Reset(); fault = stage; failAt = position;
            originals[2] = targets[2];
            const std::array<Fn,7> before{originals[0],originals[1],originals[2],originals[3],originals[4],originals[5],originals[6]};
            auto installer = Seven(Operations());
            const auto failure = installer.Tick(0);
            Check(failure.outcome == Outcome::Failed && failure.error != NO_ERROR && !installer.Ready(), "failure never publishes installed state");
            Check(failure.group == "Core fixture" && failure.diagnosticChanged, "first failure names group and emits diagnostic");
            for (size_t i = 0; i < before.size(); ++i) Check(originals[i] == before[i], "all original slots restored after failure");
            Check(!transactionActive && calls.abort == ((stage == Fault::Update || stage == Fault::Attach) ? 1 : 0), "failed transaction closes/aborts exactly once");
            if (stage == Fault::Attach) Check(failure.phase == "attach" && !failure.hook.empty(), "attach failure identifies exact hook");
            if (stage == Fault::Commit) Check(failure.phase == "commit", "commit failure identified");
            const auto attemptedCalls = calls.begin;
            for (int frame = 1; frame < 1000; ++frame)
                if (installer.Tick(frame).outcome != Outcome::Waiting) std::abort();
            Check(calls.begin == attemptedCalls && installer.Attempts() == 1, "failed install has no frame-by-frame transaction retries");
            // Repeating the same failure does not spam the same diagnostic.
            calls = {};
            const auto repeated = installer.Tick(1000);
            Check(repeated.outcome == Outcome::Failed && !repeated.diagnosticChanged && installer.Attempts() == 2, "one-second retry suppresses unchanged diagnostics");
            fault = Fault::None; calls = {};
            const auto recovered = installer.Tick(2000);
            Check(recovered.outcome == Outcome::Installed && installer.Ready(), "non-null original slots never suppress a valid retry");
            Check(calls.begin == 1 && calls.update == 1 && calls.attach == 7 && calls.commit == 1 && calls.abort == 0, "all seven hooks share one checked transaction");
            const auto beginCount = calls.begin;
            for (int frame = 0; frame < 10000; ++frame)
                if (installer.Tick(2001 + frame).outcome != Outcome::AlreadyInstalled) std::abort();
            Check(calls.begin == beginCount && installer.Attempts() == 3, "ready frames do no installation work");
        }
    }
    Reset();
    HudHooks::Installer empty("empty", {}, Operations());
    Check(empty.Tick(0).error == ERROR_INVALID_PARAMETER && calls.begin == 0, "empty group rejected before Detours");
    HudHooks::Installer missing("missing", {{nullptr, reinterpret_cast<void*>(targets[0]), reinterpret_cast<void*>(replacements[0]), "missing slot"}}, Operations());
    Check(missing.Tick(0).hook == "missing slot" && calls.begin == 0, "invalid slot identifies configuration failure before Detours");
    HudHooks::Installer duplicate("duplicate", {
        HudHooks::Bind(originals[0], replacements[0], reinterpret_cast<uintptr_t>(targets[0]), "first"),
        HudHooks::Bind(originals[0], replacements[1], reinterpret_cast<uintptr_t>(targets[1]), "duplicate slot")}, Operations());
    Check(duplicate.Tick(0).phase == "duplicate hook" && calls.begin == 0, "duplicate slot rejected without partial installation");
    HudHooks::Installer duplicateTarget("duplicate target", {
        HudHooks::Bind(originals[0], replacements[0], reinterpret_cast<uintptr_t>(targets[0]), "first"),
        HudHooks::Bind(originals[1], replacements[1], reinterpret_cast<uintptr_t>(targets[0]), "duplicate target")}, Operations());
    Check(duplicateTarget.Tick(0).phase == "duplicate hook" && calls.begin == 0, "duplicate target rejected");
    Reset(); fault = Fault::Begin;
    auto rollbackClock = Seven(Operations());
    rollbackClock.Tick(5000);
    Check(rollbackClock.Tick(4000).outcome == Outcome::Failed && rollbackClock.Attempts() == 2, "clock rollback cannot stall retry forever");
}
void Detach() {
    Check(DetourTransactionBegin() == NO_ERROR && DetourUpdateThread(GetCurrentThread()) == NO_ERROR, "real detach transaction opens");
    for (size_t i = 0; i < targets.size(); ++i)
        Check(DetourDetach(reinterpret_cast<PVOID*>(&originals[i]), reinterpret_cast<PVOID>(replacements[i])) == NO_ERROR, "real hook detached");
    Check(DetourTransactionCommit() == NO_ERROR, "real detach commits");
}
void NativeTests() {
    Reset(); real = true;
    std::array<int,7> baseline{};
    for (size_t i = 0; i < targets.size(); ++i) baseline[i] = Call(targets[i], 9);
    auto installer = Seven(Operations());
    Check(installer.Tick(0).outcome == HudHooks::Outcome::Installed, "real seven-hook group installs");
    Check(calls.begin == 1 && calls.attach == 7 && calls.commit == 1, "real group commits seven hooks once");
    for (size_t i = 0; i < targets.size(); ++i) {
        Check(Call(targets[i], 9) == baseline[i] + 1000 + int(i), "real native call reaches correct detour");
        Check(Call(originals[i], 9) == baseline[i], "real original trampoline preserves native result");
    }
    Detach();
    for (size_t i = 0; i < targets.size(); ++i) Check(Call(targets[i],9) == baseline[i], "fixture native function restored");

    std::vector<double> separate, grouped;
    for (int sample = 0; sample < 5; ++sample) {
        Reset(); real = true;
        auto start = std::chrono::steady_clock::now();
        for (size_t i = 0; i < targets.size(); ++i) {
            originals[i] = targets[i];
            HudHooks::Transaction transaction(Operations());
            Check(transaction.Attach(originals[i], replacements[i]) && transaction.Commit(), "legacy separate real transaction installed");
        }
        separate.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
        Check(calls.begin == 7 && calls.commit == 7, "legacy path uses seven commits");
        Detach();
        Reset(); real = true;
        auto group = Seven(Operations());
        start = std::chrono::steady_clock::now();
        Check(group.Tick(0).outcome == HudHooks::Outcome::Installed, "grouped benchmark installs");
        grouped.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
        Check(calls.begin == 1 && calls.commit == 1, "grouped path uses one commit");
        Detach();
    }
    std::sort(separate.begin(),separate.end()); std::sort(grouped.begin(),grouped.end());
    std::cout << "Real Detours fixture, five-sample median: seven transactions " << separate[2]
              << " ms; one grouped transaction " << grouped[2] << " ms. Not a game startup/FPS measurement.\n";
}
void GameImageTests(const wchar_t* path) {
    // Map resources without executing the EXE or resolving imports. Detours
    // touches a private executable copy only; no game code is ever called.
    const auto mapped = LoadLibraryExW(path, nullptr, LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    Check(mapped != nullptr, "game EXE mapped without execution");
    const auto image = reinterpret_cast<const uint8_t*>(reinterpret_cast<uintptr_t>(mapped) & ~uintptr_t(3));
    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
    Check(dos->e_magic == IMAGE_DOS_SIGNATURE, "game DOS header");
    const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(image + dos->e_lfanew);
    Check(nt->Signature == IMAGE_NT_SIGNATURE && nt->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64, "game x64 PE header");
    const size_t size = nt->OptionalHeader.SizeOfImage;
    const auto copy = static_cast<uint8_t*>(VirtualAlloc(nullptr,size,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    Check(copy != nullptr, "private game image fixture allocated");
    for (size_t at = 0; at < size;) {
        MEMORY_BASIC_INFORMATION region{};
        Check(VirtualQuery(image+at,&region,sizeof(region)) != 0, "mapped image readable-region query");
        const auto count = (std::min)(size-at,size_t(static_cast<const uint8_t*>(region.BaseAddress)+region.RegionSize-(image+at)));
        if (region.State == MEM_COMMIT && !(region.Protect & (PAGE_NOACCESS|PAGE_GUARD))) {
            SIZE_T got = 0;
            Check(ReadProcessMemory(GetCurrentProcess(),image+at,copy+at,count,&got) && got == count, "game image readable bytes copied");
        }
        at += count;
    }
    constexpr std::array<uint32_t,12> rvas{0xbc88c0,0x2ad6f0,0x107ca0,0x2e1b60,0x33f380,0x2f9d50,0x1fc0b0,
        0x3253d0,0x56e9a0,0x341120,0xbb2a90,0x5766f0};
    std::array<std::array<uint8_t,32>,12> before{};
    for (size_t i=0;i<rvas.size();++i) {
        Check(rvas[i]+32 < size, "audited HUD hook RVA in image");
        std::memcpy(before[i].data(),copy+rvas[i],32);
    }
    Reset(); real = true;
    const auto hook = [&](size_t i) { return HudHooks::Bind(originals[i],replacements[i],reinterpret_cast<uintptr_t>(copy+rvas[i]),"audited game function"); };
    HudHooks::Installer core("mapped game core", {hook(0),hook(1),hook(2),hook(3),hook(4),hook(5),hook(6)}, Operations());
    Check(core.Tick(0).outcome == HudHooks::Outcome::Installed, "all seven real game prologues accept one Detours transaction");
    PVOID extra[5]{};
    const auto auxiliary = [&](size_t i) { return HudHooks::Hook{&extra[i],copy+rvas[i+7],reinterpret_cast<PVOID>(replacements[0]),"audited auxiliary function"}; };
    HudHooks::Installer extras("mapped game auxiliary", {auxiliary(0),auxiliary(1),auxiliary(2),auxiliary(3),auxiliary(4)}, Operations());
    Check(extras.Tick(0).outcome == HudHooks::Outcome::Installed, "damage/chat/scaling/automap game prologues accept checked installation");
    for (size_t i=0;i<rvas.size();++i) Check(std::memcmp(before[i].data(),copy+rvas[i],32)!=0, "real game hook patched in private copy");
    Check(DetourTransactionBegin()==NO_ERROR && DetourUpdateThread(GetCurrentThread())==NO_ERROR, "game-copy detach transaction opens");
    for(size_t i=0;i<7;++i) Check(DetourDetach(reinterpret_cast<PVOID*>(&originals[i]),reinterpret_cast<PVOID>(replacements[i]))==NO_ERROR,"game-copy core hook detaches");
    for(size_t i=0;i<5;++i) Check(DetourDetach(&extra[i],reinterpret_cast<PVOID>(replacements[0]))==NO_ERROR,"game-copy auxiliary hook detaches");
    Check(DetourTransactionCommit()==NO_ERROR,"game-copy detach commits");
    for(size_t i=0;i<rvas.size();++i) Check(std::memcmp(before[i].data(),copy+rvas[i],32)==0,"game-copy original prologue restored");
    VirtualFree(copy,0,MEM_RELEASE); FreeLibrary(mapped);
    std::cout << "All 12 audited D2R hook targets patched and restored in a private image copy; no game code executed.\n";
}
}
int wmain(int argc, wchar_t** argv) {
    FailureTests(); NativeTests();
    if (argc > 1) GameImageTests(argv[1]);
    std::cout << "PASS: " << checks << " HUD hook setup checks (injected failures and real native Detours).\n";
}
