// Map an independent image and exercise the four relays with synthetic return
// tails. The renderer, shaders and game are never started by this test.
#include "../d2rhud/plugin/WidescreenBars.cpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

namespace { uint8_t* image = nullptr; }
DWORD64 Pattern::Address(uint32_t rva) { return reinterpret_cast<uintptr_t>(image) + rva; }

int main(int argc, char** argv) {
    using namespace WidescreenBars;
    assert(argc == 2);
    std::ifstream input(std::filesystem::path(argv[1]), std::ios::binary | std::ios::ate);
    assert(input && input.tellg() >= std::streamoff(sizeof(IMAGE_DOS_HEADER)));
    std::vector<uint8_t> file(static_cast<size_t>(input.tellg()));
    input.seekg(0);
    assert(input.read(reinterpret_cast<char*>(file.data()), static_cast<std::streamsize>(file.size())));
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(file.data());
    assert(dos->e_magic == IMAGE_DOS_SIGNATURE && dos->e_lfanew >= 0 &&
        size_t(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS64) <= file.size());
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(file.data() + dos->e_lfanew);
    assert(nt->Signature == IMAGE_NT_SIGNATURE && nt->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 &&
        nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC);
    auto size = size_t(nt->OptionalHeader.SizeOfImage);
    assert(size > Native::Callback + Native::CallbackSize && nt->OptionalHeader.SizeOfHeaders <= file.size());
    image = static_cast<uint8_t*>(VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    assert(image);
    std::memcpy(image, file.data(), nt->OptionalHeader.SizeOfHeaders);
    auto sections = IMAGE_FIRST_SECTION(nt);
    assert(reinterpret_cast<const uint8_t*>(sections + nt->FileHeader.NumberOfSections) <= file.data() + file.size());
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        const auto& s = sections[i];
        assert(size_t(s.PointerToRawData) + s.SizeOfRawData <= file.size() && size_t(s.VirtualAddress) + s.SizeOfRawData <= size);
        std::memcpy(image + s.VirtualAddress, file.data() + s.PointerToRawData, s.SizeOfRawData);
    }
    assert(!Available());
    auto original = std::array<uint8_t, Native::CallbackSize>{};
    std::memcpy(original.data(), image + Native::Callback, original.size());
    assert(Native::Hash(original) == Native::CallbackHash);
    // Every guarded path must match before any hook can be created.
    for (const auto& signature : Native::Signatures) {
        assert(Native::Hash({image + signature.rva, signature.size}) == signature.hash);
        image[signature.rva] ^= 1;
        assert(!Install() && !Available() && !created);
        for (const auto& hook : hooks) assert(!hook.attached && !hook.trampoline);
        image[signature.rva] ^= 1;
    }
    SetEnabled(true); // Saved preference before installation.
    assert(Install() && Install() && Available());
    assert(Equal(hooks[0].trampoline, Native::StoreBytes));

    struct Tail { uint32_t rva; std::vector<uint8_t> original; };
    std::vector<Tail> tails;
    auto replaceTail = [&](uint32_t rva, std::initializer_list<uint8_t> code) {
        tails.push_back({rva, {image + rva, image + rva + code.size()}});
        std::memcpy(image + rva, code.begin(), code.size());
        FlushInstructionCache(GetCurrentProcess(), image + rva, code.size());
    };

    // Return immediately after the native store; never run the renderer tail.
    replaceTail(Native::Store + uint32_t(Native::StoreBytes.size()), {0xc3});
    // Disabled branches execute the relocated MOVAPS/MOVD, then return 17;
    // enabled branches skip the native body and return 34 from its resume site.
    replaceTail(Native::Bars + 7, {0xb8,17,0,0,0,0xc3});
    replaceTail(Native::BarsResume, {0xb8,34,0,0,0,0xc3});
    replaceTail(Native::Cursor + 5, {0xb8,17,0,0,0,0xc3});
    replaceTail(Native::CursorResume, {0xb8,34,0,0,0,0xc3});
    // Report the native dirty-byte CMP result instead of entering the renderer.
    replaceTail(Native::Refresh + 7, {0x0f,0x95,0xc0,0x0f,0xb6,0xc0,0xc3});
    // Test wrapper preserves ABI, puts output in R8, and captures RAX/flags on
    // both sides of a real MinHook -> relay -> trampoline -> native return.
    std::vector<uint8_t> wrapper{
        0x53, 0x48,0x83,0xec,0x20,       // push rbx; sub rsp,20h
        0x48,0x89,0xd3,                 // mov rbx,rdx
        0x49,0x89,0xc8,                 // mov r8,rcx
        0x48,0xb8, 0x88,0x77,0x66,0x55,0x44,0x33,0x22,0x11,
        0x48,0x39,0xc0,                 // cmp rax,rax
        0x9c,0x41,0x5a,                 // pushfq; pop r10
        0x4c,0x89,0x13,                 // mov [rbx],r10
        0x49,0xbb, 0,0,0,0,0,0,0,0,     // mov r11,target
        0x41,0xff,0xd3,                 // call r11
        0x48,0x89,0x43,0x08,            // mov [rbx+8],rax
        0x9c,0x41,0x5a,                 // pushfq; pop r10
        0x4c,0x89,0x53,0x10,            // mov [rbx+16],r10
        0x48,0x83,0xc4,0x20,0x5b,0xc3,
    };
    const auto address = reinterpret_cast<uintptr_t>(hooks[0].target);
    std::memcpy(wrapper.data() + 32, &address, sizeof(address));
    auto executable = VirtualAlloc(nullptr, wrapper.size(), MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    assert(executable);
    std::memcpy(executable, wrapper.data(), wrapper.size());
    FlushInstructionCache(GetCurrentProcess(), executable, wrapper.size());
    auto invoke = reinterpret_cast<void(*)(void*, void*)>(executable);
    auto check = [&](uint32_t expected) {
        std::array<uint32_t, 4> constants{0x45800000, 0x45070000, 0xdeadbeef, 0x12345678};
        std::array<uint64_t, 3> registers{};
        invoke(constants.data(), registers.data());
        assert(constants[0] == 0x45800000 && constants[1] == 0x45070000 && constants[2] == expected && constants[3] == 0x12345678);
        assert(registers[1] == 0x1122334455667788ull && registers[0] == registers[2]);
    };
    // A saved enabled preference must leave the initial main menu untouched.
    check(Native::NativeAspect);
    assert(requested.load() && !enabled.load());
    SetInGame(true);
    check(Native::UnmaskedAspect);
    for (int i = 0; i < 100; ++i) {
        SetEnabled(false); check(Native::NativeAspect);
        SetEnabled(true); check(Native::UnmaskedAspect);
    }

    // Capture the native instruction's XMM result as well as the chosen tail.
    // Both XMM0 and XMM2 begin with the same sentinel vector; R9D is the input
    // for the cursor hook's original MOVD XMM2,R9D.
    std::vector<uint8_t> branchWrapper{
        0x53,0x48,0x83,0xec,0x20,       // push rbx; sub rsp,20h
        0x48,0x89,0xcb,                 // mov rbx,rcx
        0x0f,0x10,0x03,                 // movups xmm0,[rbx]
        0x0f,0x10,0x13,                 // movups xmm2,[rbx]
        0x41,0xb9,0x78,0x56,0x34,0x12, // mov r9d,12345678h
        0x49,0xbb,0,0,0,0,0,0,0,0,     // mov r11,target
        0x41,0xff,0xd3,                 // call r11
        0x89,0x43,0x30,                 // mov [rbx+48],eax
        0x0f,0x11,0x43,0x10,            // movups [rbx+16],xmm0
        0x0f,0x11,0x53,0x20,            // movups [rbx+32],xmm2
        0x48,0x83,0xc4,0x20,0x5b,0xc3,
    };
    auto branchExecutable = VirtualAlloc(nullptr, branchWrapper.size(), MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    assert(branchExecutable);
    auto checkBranches = [&](bool active) {
      for (size_t hookIndex : {size_t(1), size_t(2)}) {
        const auto branchAddress = reinterpret_cast<uintptr_t>(hooks[hookIndex].target);
        std::memcpy(branchWrapper.data() + 22, &branchAddress, sizeof(branchAddress));
        std::memcpy(branchExecutable, branchWrapper.data(), branchWrapper.size());
        FlushInstructionCache(GetCurrentProcess(), branchExecutable, branchWrapper.size());
            std::array<uint32_t,13> result{0x11223344,0x55667788,0x99aabbcc,0xddeeff00};
            reinterpret_cast<void(*)(void*)>(branchExecutable)(result.data());
            assert(result[12] == (active ? 34u : 17u));
            for (size_t lane = 0; lane < 4; ++lane) {
                const auto barsExpected = !active && hookIndex == 1 ? (lane == 3 ? 0x3f800000u : 0u) : result[lane];
                const auto cursorExpected = !active && hookIndex == 2 ? (lane == 0 ? 0x12345678u : 0u) : result[lane];
                assert(result[4 + lane] == barsExpected && result[8 + lane] == cursorExpected);
            }
      }
    };
    for (bool active : {false, true}) {
        SetEnabled(active);
        checkBranches(active);
    }

    std::vector<uint8_t> renderState(Native::DirtyOffset + 16, 0xa5);
    auto renderRefresh = reinterpret_cast<int(*)(void*)>(hooks[3].target);
    auto checkRefresh = [&](bool refreshRequested) {
        assert(refreshPending.load() == refreshRequested);
        renderState[Native::DirtyOffset] = 0;
        assert(renderRefresh(renderState.data()) == (refreshRequested ? 1 : 0));
        assert(renderState[Native::DirtyOffset] == (refreshRequested ? 1 : 0));
        assert(!refreshPending.load());
        for (size_t offset = 0; offset < renderState.size(); ++offset)
            if (offset != Native::DirtyOffset) assert(renderState[offset] == 0xa5);
    };
    checkRefresh(true); // Installation/toggles request a cache refresh.
    checkRefresh(false); // Consumed exactly once, even on the next frame.
    SetEnabled(false); checkRefresh(true); checkRefresh(false);
    SetEnabled(false); checkRefresh(false); // An unchanged preference adds no work.
    SetEnabled(true); checkRefresh(true); checkRefresh(false);

    // Leaving play restores the cached fade, solid bars and cursor clamp.
    // The preference survives main-menu draws and takes effect on re-entry.
    SetInGame(false);
    check(Native::NativeAspect); checkBranches(false);
    checkRefresh(true); checkRefresh(false);
    assert(requested.load());
    SetInGame(false); checkRefresh(false);
    SetEnabled(false); SetEnabled(true); // Editing preference on the main menu.
    check(Native::NativeAspect); checkBranches(false); checkRefresh(false);
    SetInGame(true);
    check(Native::UnmaskedAspect); checkBranches(true);
    checkRefresh(true); checkRefresh(false);
    SetInGame(true); checkRefresh(false); // Stable gameplay does not regenerate.

    Uninstall(); assert(!Available()); check(Native::NativeAspect);
    assert(Equal(image + Native::Store, Native::StoreBytes));
    assert(requested.load()); // The saved preference survives hook reinstall.
    assert(Install() && Available()); check(Native::UnmaskedAspect);
    SetEnabled(false); Uninstall(); Uninstall(); check(Native::NativeAspect);
    for (const auto& tail : tails) {
        std::memcpy(image + tail.rva, tail.original.data(), tail.original.size());
        FlushInstructionCache(GetCurrentProcess(), image + tail.rva, tail.original.size());
    }
    assert(Equal(image + Native::Callback, original));
    assert(Supported());
    for (const auto& hook : hooks) assert(!hook.attached);
    VirtualFree(branchExecutable, 0, MEM_RELEASE);
    VirtualFree(executable, 0, MEM_RELEASE);
    // Module retains trampoline/relay until process exit by design.
    std::cout << "Widescreen bars: signatures, native relays, one-shot refresh, main-menu/game transitions and restore passed.\n";
}
