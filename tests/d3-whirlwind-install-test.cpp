// Exercise the real MinHook decoder/trampolines against an independently mapped
// executable image. Never execute game code or load/inject into a game process.
#include "../d2rhud/plugin/D3Whirlwind.cpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

namespace {
uint8_t* mappedImage = nullptr;
}
DWORD64 Pattern::Address(uint32_t rva) { return reinterpret_cast<uintptr_t>(mappedImage) + rva; }

int __fastcall ExistingManaHook(D2UnitStrc*, int) { return 0; }

std::array<uint8_t, 32> First32(uint32_t rva) {
    std::array<uint8_t, 32> bytes;
    std::memcpy(bytes.data(), mappedImage + rva, bytes.size());
    return bytes;
}

int main(int argc, char** argv) {
    assert(argc == 2);
    std::cout << std::unitbuf;
    std::ifstream input(std::filesystem::path(argv[1]), std::ios::binary | std::ios::ate);
    assert(input && input.tellg() >= std::streamoff(sizeof(IMAGE_DOS_HEADER)));
    std::vector<uint8_t> file(static_cast<size_t>(input.tellg()));
    input.seekg(0);
    assert(input.read(reinterpret_cast<char*>(file.data()), static_cast<std::streamsize>(file.size())));
    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(file.data());
    assert(dos->e_magic == IMAGE_DOS_SIGNATURE && dos->e_lfanew >= 0
        && size_t(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS64) <= file.size());
    const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(file.data() + dos->e_lfanew);
    assert(nt->Signature == IMAGE_NT_SIGNATURE && nt->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64
        && nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC);
    const auto imageSize = size_t(nt->OptionalHeader.SizeOfImage);
    assert(imageSize >= 0x1d637f8 && nt->OptionalHeader.SizeOfHeaders <= file.size()
        && nt->OptionalHeader.SizeOfHeaders <= imageSize);
    const auto sections = IMAGE_FIRST_SECTION(nt);
    assert(reinterpret_cast<const uint8_t*>(sections + nt->FileHeader.NumberOfSections) <= file.data() + file.size());
    mappedImage = static_cast<uint8_t*>(VirtualAlloc(nullptr, imageSize, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    assert(mappedImage);
    std::memcpy(mappedImage, file.data(), nt->OptionalHeader.SizeOfHeaders);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        const auto& section = sections[i];
        assert(size_t(section.PointerToRawData) + section.SizeOfRawData <= file.size()
            && size_t(section.VirtualAddress) + section.SizeOfRawData <= imageSize);
        if (section.SizeOfRawData) std::memcpy(mappedImage + section.VirtualAddress,
            file.data() + section.PointerToRawData, section.SizeOfRawData);
    }
    assert(FlushInstructionCache(GetCurrentProcess(), mappedImage, imageSize));
    using namespace D3Whirlwind;
    assert(Matches(NativeData::Signatures) && Matches(NativeData::InputSignatures) && Matches(Pickup::Signatures)&&Matches(Potion::Signatures));
    std::array<std::array<uint8_t, 32>, Hooks.size()> originals;
    for (size_t i = 0; i < Hooks.size(); ++i) originals[i] = First32(Hooks[i]);
    const auto manaOriginal = First32(0x294e70);
    assert(MH_Initialize() == MH_OK);
    void* originalMana = nullptr;
    const auto manaAddress = reinterpret_cast<void*>(Pattern::Address(0x294e70));
    assert(MH_CreateHook(manaAddress, reinterpret_cast<void*>(ExistingManaHook), &originalMana) == MH_OK && originalMana);
    assert(MH_EnableHook(manaAddress) == MH_OK);
    const auto manaPatched = First32(0x294e70);
    assert(manaPatched != manaOriginal);
    // These entry points already belong to the map item-use and transmog
    // features. Potion support must validate their bodies and retain the chain.
    const std::array<uint32_t,2> itemHooks{0x2b6450,0x2b7370};
    std::array<std::array<uint8_t,32>,2> itemOriginals,itemPatched;
    std::array<void*,2> itemTrampolines{};
    for(size_t i=0;i<itemHooks.size();++i){
        itemOriginals[i]=First32(itemHooks[i]);auto entry=reinterpret_cast<void*>(Pattern::Address(itemHooks[i]));
        assert(MH_CreateHook(entry,reinterpret_cast<void*>(ExistingManaHook),&itemTrampolines[i])==MH_OK);
        assert(MH_EnableHook(entry)==MH_OK);itemPatched[i]=First32(itemHooks[i]);
    }
    assert(Matches(Potion::Signatures));
    // CubeMap owns this prologue in production. The +0x20 signature window must
    // still validate while the first bytes contain an existing detour.
    assert(Matches(NativeData::Signatures));
    const bool installed = Install();
    if (!installed) std::cerr << Status() << '\n';
    assert(installed && ready.load());
    for (size_t i = 0; i < Hooks.size(); ++i) assert(First32(Hooks[i]) != originals[i]);
    assert(First32(0x294e70) == manaPatched);
    for(size_t i=0;i<itemHooks.size();++i)assert(First32(itemHooks[i])==itemPatched[i]);
    Uninstall();
    assert(!ready.load());
    for (size_t i = 0; i < Hooks.size(); ++i) assert(First32(Hooks[i]) == originals[i]);
    assert(First32(0x294e70) == manaPatched);

    // A changed executable must fail before creating or enabling any hook.
    const auto changed = NativeData::Signatures[0].rva;
    mappedImage[changed] ^= 1;
    std::array<std::array<uint8_t, 32>, Hooks.size()> beforeRejectedInstall;
    for (size_t i = 0; i < Hooks.size(); ++i) beforeRejectedInstall[i] = First32(Hooks[i]);
    assert(!Install() && !ready.load());
    for (size_t i = 0; i < Hooks.size(); ++i) assert(First32(Hooks[i]) == beforeRejectedInstall[i]);
    assert(First32(0x294e70) == manaPatched);
    mappedImage[changed] ^= 1;
    for(const auto signature:Pickup::Signatures){
        mappedImage[signature.rva]^=1;
        assert(!Install() && !ready.load());
        mappedImage[signature.rva]^=1;
        for(size_t i=0;i<Hooks.size();++i)assert(First32(Hooks[i])==originals[i]);
        assert(First32(0x294e70)==manaPatched);
    }
    for(const auto signature:Potion::Signatures){
        mappedImage[signature.rva]^=1;
        assert(!Install()&&!ready.load());
        mappedImage[signature.rva]^=1;
        for(size_t i=0;i<Hooks.size();++i)assert(First32(Hooks[i])==originals[i]);
    }
    for(size_t i=0;i<itemHooks.size();++i){auto entry=reinterpret_cast<void*>(Pattern::Address(itemHooks[i]));
        assert(First32(itemHooks[i])==itemPatched[i]);assert(MH_DisableHook(entry)==MH_OK&&MH_RemoveHook(entry)==MH_OK);
        assert(First32(itemHooks[i])==itemOriginals[i]);
    }
    assert(MH_DisableHook(manaAddress) == MH_OK && MH_RemoveHook(manaAddress) == MH_OK);
    assert(First32(0x294e70) == manaOriginal);
    assert(MH_Uninitialize() == MH_OK);
    assert(VirtualFree(mappedImage, 0, MEM_RELEASE)); mappedImage = nullptr;
    std::cout << "All ten real Whirlwind detours install beside existing mana, transmog and map hooks, restore every prologue on uninstall, and reject signature mismatch without changes. No game code executed.\n";
}
