// Test the integrated hook against the real client start in a private image.
// The path/resource providers are stubs; no running game is opened or changed.
namespace {
Unit* restartLocal = nullptr;
bool restartPathSuccess = true, restartReplaceStats = false;
int restartCalls = 0, restartResult = 0, restartPathCalls = 0;
uint32_t restartObserved = 0;
Bytes<0xad8> restartOtherStats{};
int __fastcall RestartOriginal(Unit* unit, int) {
    ++restartCalls;
    if (unit) {
        restartObserved = NativeData::Read<uint32_t>(unit->pStatListEx, 0xacc);
        Put(unit->pStatListEx, 0xacc, restartObserved | 0x800u);
        if (restartReplaceStats) Put(unit, 0x88, restartOtherStats.data());
    }
    return restartResult;
}
}
void NativeAssassinRestart(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate); assert(input);
    std::vector<uint8_t> file(size_t(input.tellg())); input.seekg(0);
    assert(input.read(reinterpret_cast<char*>(file.data()), file.size()));
    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(file.data());
    const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(file.data() + dos->e_lfanew);
    auto image = static_cast<uint8_t*>(VirtualAlloc(nullptr, nt->OptionalHeader.SizeOfImage,
        MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE)); assert(image);
    memcpy(image, file.data(), nt->OptionalHeader.SizeOfHeaders);
    for (auto section = IMAGE_FIRST_SECTION(nt); section < IMAGE_FIRST_SECTION(nt) + nt->FileHeader.NumberOfSections; ++section)
        memcpy(image + section->VirtualAddress, file.data() + section->PointerToRawData, section->SizeOfRawData);
    assert(WhirlwindRestart::Supported(image));
    auto stub = [&](uint32_t rva, auto function) {
        const uint8_t jump[]{0xff, 0x25, 0, 0, 0, 0}; memcpy(image + rva, jump, 6);
        const auto target = reinterpret_cast<uintptr_t>(function); memcpy(image + rva + 6, &target, 8);
    };
    stub(0x6b050, +[](int)->Unit* { return restartLocal; });
    stub(0x218ce0, +[](Unit*)->void* { return fixture.skill.data(); });
    stub(0x2082d0, +[](Unit*)->void* { return fixture.path.data(); });
    stub(0x209530, +[](Unit*)->void* { return fixture.room.data(); });
    stub(0x175ea0, +[](Unit*, void*)->int {
        ++restartPathCalls;
        assert(!(NativeData::Read<uint32_t>(fixture.stats.data(), 0xacc) & 0x400000u));
        return restartPathSuccess;
    });
    for (uint32_t rva : {0xa44b0u, 0x20e030u, 0x230c10u, 0x175af0u, 0x1754e0u})
        stub(rva, +[]()->uintptr_t { return 0; });
    FlushInstructionCache(GetCurrentProcess(), image, nt->OptionalHeader.SizeOfImage);
    Unit server{}; Bytes<0x50> serverList{}, serverSkill{};
    auto setup = [&] {
        fixture.Reset(); auto& f = fixture; enabled.store(false); Bind(0, image);
        f.owner.dwClassId = 6; Put(f.skill.data(), 0x14, uint32_t(0));
        Put(f.stats.data(), 0x1c, uint32_t(0x80000000)); Put(f.stats.data(), 0xacc, uint32_t(0x400220));
        f.game.nGameType = 3;
        server = {}; server.dwClassId = 6; server.dwUnitId = f.owner.dwUnitId; server.dwAnimMode = 18;
        serverList = {}; serverSkill = {}; Put(&server, 0x100, serverList.data());
        Put(serverList.data(), 0x18, serverSkill.data()); Put(serverSkill.data(), 0, f.row.data()); Put(serverSkill.data(), 0x14, uint32_t(1));
        f.game.pUnitList[0][f.owner.dwUnitId & 0x7f] = &server;
        Put(image, 0x1d637f0, &f.connection); Put(image, 0x1d442d8, uint32_t(0)); Put(image, 0x1c9ebf8, uint32_t(1000));
        Put(image, 0x1c9fb70, uint64_t(0)); // Native class speed falls back without a loaded class table.
        Put(f.path.data(), 0x10, uint16_t(100)); Put(f.path.data(), 0x12, uint16_t(200)); Put(f.path.data(), 0x34, int(1));
        Put(f.path.data(), 0xc8, uint16_t(100)); Put(f.path.data(), 0xca, uint16_t(200));
        restartLocal = &f.owner; restartCalls = restartPathCalls = 0; restartObserved = 0;
        restartReplaceStats = false; restartOtherStats = {}; restartPathSuccess = true;
        originalClientStart = RestartOriginal;
    };
    // The original is called exactly once on both return values. Restoring
    // state 54 must preserve unrelated state changes made during that call.
    for (int result : {0, 1}) {
        setup(); restartResult = result;
        assert(ClientStartHook(&fixture.owner, 151) == result && restartCalls == 1);
        assert(restartObserved == 0x220 && NativeData::Read<uint32_t>(fixture.stats.data(), 0xacc) == 0x400a20);
        assert(!client.active && channels.empty());
    }
    // All exclusions must delegate unchanged, including mismatched builds,
    // nonlocal sessions, other classes/skills and inactive server skills.
    for (int exclusion = 0; exclusion < 17; ++exclusion) {
        setup(); int id = 151; restartResult = 37;
        switch (exclusion) {
        case 0: enabled.store(true); break;
        case 1: fixture.owner.dwClassId = 4; break;
        case 2: fixture.owner.dwAnimMode = 1; break;
        case 3: id = 714; break;
        case 4: Put(fixture.skill.data(), 0x14, uint32_t(1)); break;
        case 5: Put(fixture.skill.data(), 0x14, uint32_t(3)); break;
        case 6: server.dwAnimMode = 1; break;
        case 7: server.dwClassId = 4; break;
        case 8: Put(serverSkill.data(), 0x14, uint32_t(0)); break;
        case 9: Put(serverSkill.data(), 0x14, uint32_t(3)); break;
        case 10: Put(serverList.data(), 0x18, static_cast<void*>(nullptr)); break;
        case 11: fixture.game.nGameType = 1; break;
        case 12: restartLocal = nullptr; break;
        case 13: image[0x17c019] ^= 1; break;
        case 14: Put(fixture.stats.data(), 0x1c, uint32_t(0)); break;
        case 15: Put(fixture.stats.data(), 0xacc, uint32_t(0x220)); break;
        case 16: Put(image, 0x1d637f0, static_cast<void*>(nullptr)); break;
        }
        const auto before = NativeData::Read<uint32_t>(fixture.stats.data(), 0xacc);
        assert(ClientStartHook(&fixture.owner, id) == 37 && restartCalls == 1 && restartObserved == before);
        assert(NativeData::Read<uint32_t>(fixture.stats.data(), 0xacc) == (before | 0x800u));
        if (exclusion == 13) image[0x17c019] ^= 1;
    }
    setup(); restartReplaceStats = true;
    assert(ClientStartHook(&fixture.owner, 151) == 37 && restartCalls == 1);
    assert(fixture.owner.pStatListEx == reinterpret_cast<D2StatListExStrc*>(restartOtherStats.data()));
    assert(!NativeData::Read<uint32_t>(restartOtherStats.data(), 0xacc));
    // Execute the actual native guard, path validation and return branches
    // through the integrated ClientStartHook rather than mocking its result.
    for (int scenario = 0; scenario < 3; ++scenario) {
        setup(); originalClientStart = reinterpret_cast<ClientSkill>(image + NativeData::ClientStart);
        if (scenario == 0) server.dwAnimMode = 1;
        if (scenario == 2) restartPathSuccess = false;
        const auto firstBytes = NativeData::Read<std::array<uint8_t, 32>>(image + NativeData::ClientStart);
        assert(ClientStartHook(&fixture.owner, 151) == (scenario == 1 ? 1 : 0));
        assert(restartPathCalls == (scenario == 0 ? 0 : 1));
        assert(NativeData::Read<uint32_t>(fixture.stats.data(), 0xacc) == 0x400220);
        assert(NativeData::Read<uint32_t>(fixture.skill.data(), 0x14) == (scenario == 1 ? 1u : 0u));
        assert((firstBytes == NativeData::Read<std::array<uint8_t, 32>>(image + NativeData::ClientStart)));
    }
    fixture.Reset(); restartLocal = nullptr; assert(VirtualFree(image, 0, MEM_RELEASE));
    std::cout << "Assassin restart masks only the eligible ordinary client state-54 guard, restores unrelated bits, calls native start once, preserves real native path failures and excludes D3/foreign/inactive contexts.\n";
}
