#define NOMINMAX
#include "../d2rhud/plugin/SpectateNative.cpp"
#include <cassert>
#include <detours/detours.h>
#include <fstream>
#include <iostream>
namespace {
using namespace Spectate;
D2GameStrc game{};
D2UnitStrc units[2]{};
D2PlayerDataStrc data[2]{};
D2ClientStrc clients[2]{};
uint32_t selector = 0;
uint8_t hosting = 2, status = 1, loading = 0;
uint16_t gamePort = 4000;
int online = 2;
bool signatureMode = false;
std::vector<uint8_t> executable;
std::map<uint32_t, uintptr_t> signatures;
std::vector<Data::Packet> sent;
std::array<uint8_t, 0x600> manager{}, panel{}, row{}, label{}, squelch{}, loot{}, invite{}, partyInfo{};
void* managerPointer = manager.data();
void* managerChildren[]{panel.data()};
void* panelChildren[]{row.data()};
void* rowChildren[]{label.data(), squelch.data(), loot.data(), invite.data(), partyInfo.data()};
template <class T> void Put(void* p, size_t offset, const T& value) {
    memcpy(static_cast<uint8_t*>(p) + offset, &value, sizeof(value));
}
D2UnitStrc* __fastcall Local(uint32_t index) {
    assert(index == 0);
    return &units[0];
}
void __fastcall ToHost(void* bytes, int size) {
    assert(size == 210);
    assert(Spectate::Command(&game, &units[0], static_cast<uint8_t*>(bytes), size));
}
void __fastcall ToClient(void*, uint32_t client, void** range) {
    assert(uintptr_t(range[1]) - uintptr_t(range[0]) == 206);
    const auto packet = *static_cast<Data::Packet*>(range[0]);
    assert(client < 2 && packet.recipient == client);
    sent.push_back(packet);
    if (client == 0)
        Receive(reinterpret_cast<const uint8_t*>(&packet), sizeof(packet));
}
void* __fastcall FindPanel(void* root, const char* name) {
    assert(root == manager.data());
    return !strcmp(name, "PartyPanel") ? panel.data() : nullptr;
}
uint64_t* __fastcall Position(void* widget, uint64_t* xy) {
    const auto rect = Spectate::Field<WidgetRect>(widget, 0x70);
    const auto scale = Spectate::Scale(widget);
    const auto x = int32_t(100 + (rect[0] - 108) * scale), y = int32_t(200 + (rect[1] - 14) * scale);
    *xy = uint32_t(x) | (uint64_t(uint32_t(y)) << 32);
    return xy;
}
void __fastcall QueueHook(void*, int) {}
void Executable(const char* path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    assert(in);
    executable.resize(size_t(in.tellg()));
    in.seekg(0);
    in.read(reinterpret_cast<char*>(executable.data()), executable.size());
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(executable.data());
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(executable.data() + dos->e_lfanew);
    const auto sections = IMAGE_FIRST_SECTION(nt);
    for (uint32_t rva : {0x6b050u, 0x10dce0u, 0x422ac0u, 0x597320u, 0x576f00u})
        for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
            const auto& section = sections[i];
            if (rva >= section.VirtualAddress && rva - section.VirtualAddress < section.SizeOfRawData)
                signatures[rva] =
                    uintptr_t(executable.data() + section.PointerToRawData + rva - section.VirtualAddress);
        }
    signatureMode = true;
    assert(Spectate::CodeSupported());
    // Exercise a real Microsoft Detours patch on a private executable copy.
    // No game process is modified, and neither native function is executed.
    auto memory =
        static_cast<uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    assert(memory);
    const auto original = signatures.at(0x10dce0);
    memcpy(memory, reinterpret_cast<const void*>(original), 256);
    signatures[0x10dce0] = uintptr_t(memory);
    auto trampoline = memory;
    assert(DetourTransactionBegin() == NO_ERROR);
    assert(DetourUpdateThread(GetCurrentThread()) == NO_ERROR);
    assert(DetourAttach(reinterpret_cast<void**>(&trampoline), reinterpret_cast<void*>(QueueHook)) ==
           NO_ERROR);
    assert(DetourTransactionCommit() == NO_ERROR);
    assert(memory[0] == 0xe9 && Spectate::CodeSupported());
    // This is the actual startup order from the reported game: the queue has
    // already been detoured when Spectate first checks compatibility.
    assert(Spectate::Supported());
    memory[16] ^= 1;
    assert(!Spectate::CodeSupported());
    memory[16] ^= 1;
    assert(DetourTransactionBegin() == NO_ERROR);
    assert(DetourUpdateThread(GetCurrentThread()) == NO_ERROR);
    assert(DetourDetach(reinterpret_cast<void**>(&trampoline), reinterpret_cast<void*>(QueueHook)) ==
           NO_ERROR);
    assert(DetourTransactionCommit() == NO_ERROR);
    assert(Spectate::CodeSupported());
    memory[0] = 0xcc;
    assert(!Spectate::CodeSupported());
    signatures[0x10dce0] = original;
    VirtualFree(memory, 0, MEM_RELEASE);
    signatureMode = false;
}
} // namespace
DWORD64 Pattern::Address(uint32_t rva) {
    if (signatureMode)
        return signatures.at(rva);
    switch (rva) {
    case 0x6b050:
        return uintptr_t(Local);
    case 0x10dce0:
        return uintptr_t(ToHost);
    case 0x422ac0:
        return uintptr_t(ToClient);
    case 0x597320:
        return uintptr_t(FindPanel);
    case 0x576f00:
        return uintptr_t(Position);
    case 0x1d442d8:
        return uintptr_t(&selector);
    case 0x1bf0bf6:
        return uintptr_t(&gamePort);
    case 0x1bfa31c:
        return uintptr_t(&online);
    case 0x18882e8:
        return uintptr_t(&hosting);
    case 0x1dc76f8:
        return uintptr_t(&status);
    case 0x1cb410a:
        return uintptr_t(&loading);
    case 0x1d7c4e8:
        return uintptr_t(&managerPointer);
    default:
        return rva;
    }
}
int main(int argc, char** argv) {
    using namespace Spectate;
    assert(argc == 2);
    Executable(argv[1]);
    game.dwInitSeed = 77;
    game.nClients = 2;
    game.pClientList = &clients[0];
    for (unsigned i = 0; i < 2; ++i) {
        auto& unit = units[i];
        unit.dwUnitId = i;
        unit.dwAnimMode = 1;
        unit.pPlayerData = &data[i];
        data[i].pClient = &clients[i];
        auto& c = clients[i];
        c.dwClientId = c.dwUnitGUID = i;
        c.dwClientState = 4;
        c.pGame = &game;
        c.pPlayer = &unit;
        strcpy_s(c.szName, i ? "OtherAct" : "Viewer");
        strcpy_s(data[i].szName, c.szName);
    }
    clients[0].pNext = &clients[1];
    SetAvailable(true);
    Configure({true, true, 60, 3840, 100, 128});
    HudFrame::Context context;
    context.inGame = true;
    context.status = 1;
    ClientTick(context, 1);
    Tick(&game);
    assert(Current().ready);
    assert(Native::CompatibilityError() == nullptr);
    ClientTick(context, 1);
    Tick(&game);
    auto announce = Data::CommandPacket{};
    announce.packet.opcode = 3;
    announce.packet.action = Data::Advertise;
    announce.packet.recipient = 1;
    announce.packet.seed = 77;
    for (const auto& p : sent)
        if (p.action == Data::Hello)
            announce.packet.epoch = p.epoch;
    announce.packet.total = 7;
    assert(Command(&game, &units[1], reinterpret_cast<const uint8_t*>(&announce), sizeof(announce)));
    Tick(&game);
    // Publish an actual roster update without waiting for a real two-second tick.
    Data::Packet peer;
    peer.recipient = 0;
    peer.seed = 77;
    peer.epoch = announce.packet.epoch;
    peer.action = Data::Peer;
    peer.target = 1;
    peer.total = 7;
    peer.length = 8;
    memcpy(peer.bytes.data(), "OtherAct", 8);
    Receive(reinterpret_cast<const uint8_t*>(&peer), sizeof(peer));
    assert(Open(1));
    ClientTick(context, 1);
    sent.clear();
    Tick(&game);
    assert(std::none_of(sent.begin(), sent.end(),
                        [](const auto& p) { return p.action == Data::Capture || p.action == Data::Chunk; }));
    // Native traffic authorizes sockets, and never carries video. Force a fresh
    // periodic hello in the isolated fixture to examine the owning-client grant.
    auto& server = Runtime().servers.at(&game);
    Sleep(2010);
    sent.clear();
    Tick(&game);
    const auto grant = std::find_if(sent.begin(), sent.end(), [](const auto& p) {
        return p.action == Data::ChannelGrant && p.recipient == 0;
    });
    assert(grant != sent.end() && grant->length == 34 &&
           (grant->bytes[0] | (unsigned(grant->bytes[1]) << 8)) == 6113);
    assert(std::equal(server.tokens.at(0).begin(), server.tokens.at(0).end(), grant->bytes.begin() + 2));
    const auto token0 = server.tokens.at(0), token1 = server.tokens.at(1);
    assert(token0 != token1);
    assert(server.host.Flags(1, GetTickCount64()) == 7);
    // Authoritative sender identity wins over spoofed recipient IDs.
    auto forged = announce;
    forged.packet.recipient = 0;
    forged.packet.total = 0;
    Command(&game, &units[1], reinterpret_cast<const uint8_t*>(&forged), sizeof(forged));
    Tick(&game);
    assert(Current().open && server.host.Flags(0, GetTickCount64()) == 7);
    assert(Native::CaptureReady());
    loading = 1;
    assert(!Native::CaptureReady());
    loading = 0;
    status = 0;
    assert(!Native::CaptureReady());
    status = 1;
    online = 1;
    assert(!Native::CaptureReady());
    online = 0;
    assert(!Native::TcpGame() && !Native::CaptureReady());
    online = -1;
    assert(!Native::TcpGame() && !Native::CaptureReady());
    online = 2;
    assert(Native::TcpGame() && Native::CaptureReady());
    // Native row positions come from real widget geometry; no hardcoded row order.
    Put(manager.data(), 0xb9, uint8_t(1));
    Put(manager.data(), 0x58, managerChildren + 0);
    Put(manager.data(), 0x60, uint64_t(1));
    for (auto* w : {panel.data(), row.data(), label.data()}) {
        Put(w, 0x50, uint8_t(1));
        Put(w, 0x51, uint8_t(1));
        Put(w, 0x80, 1.f);
    }
    Put<const char*>(panel.data(), 8, "PartyPanel");
    Put(panel.data(), 0x58, panelChildren + 0);
    Put(panel.data(), 0x60, uint64_t(1));
    Put(panel.data(), 0x80, .5f);
    Put<const char*>(row.data(), 8, "FriendsListTemplate");
    Put(row.data(), 0x30, panel.data());
    Put(row.data(), 0x58, rowChildren + 0);
    Put(row.data(), 0x60, uint64_t(std::size(rowChildren)));
    Put(row.data(), 0x70, std::array<int32_t, 4>{0, 0, 996, 130});
    Put<const char*>(label.data(), 8, "PlayerName");
    Put(label.data(), 0x30, row.data());
    Put(label.data(), 0x70, std::array<int32_t, 4>{108, 14, 340, 46});
    Put(label.data(), 0, uintptr_t(0x15acb98));
    Put<const char*>(label.data(), 0x88, "OtherAct");
    Put(label.data(), 0x90, uint64_t(8));
    for (auto* control : {squelch.data(), loot.data(), invite.data(), partyInfo.data()}) {
        Put(control, 0x30, row.data());
        Put(control, 0x50, uint8_t(1));
        Put(control, 0x80, 1.f);
    }
    Put<const char*>(squelch.data(), 8, "SquelchPlayerButton");
    Put(squelch.data(), 0x51, uint8_t(1));
    Put(squelch.data(), 0x70, std::array<int32_t, 4>{835, 33, 75, 75});
    Put<const char*>(loot.data(), 8, "LootBodyButton");
    Put(loot.data(), 0x51, uint8_t(0));
    Put(loot.data(), 0x70, std::array<int32_t, 4>{785, 33, 75, 75});
    Put<const char*>(invite.data(), 8, "InviteButton");
    Put(invite.data(), 0x51, uint8_t(1));
    Put(invite.data(), 0x70, std::array<int32_t, 4>{458, 29, 279, 79});
    Put<const char*>(partyInfo.data(), 8, "PartyInfoText");
    Put(partyInfo.data(), 0x51, uint8_t(0));
    Put(partyInfo.data(), 0x70, std::array<int32_t, 4>{452, 10, 296, 120});
    std::vector<Peer> peers{{1, "OtherAct", true, true, GetTickCount64()}};
    auto eyes = Native::PartyEyes(1280, 720, peers);
    auto aligned = [&] {
        assert(eyes.size() == 1 && eyes[0].player == 1);
        uint64_t xy = 0;
        Position(squelch.data(), &xy);
        assert(abs(eyes[0].size - 75 * Spectate::Scale(squelch.data())) < .01);
        assert(abs(eyes[0].y - int32_t(xy >> 32)) < .01);
        const auto scale = Spectate::Scale(squelch.data());
        const float expectedGap = (std::max)(4.f, Spectate::PartyLayout::Gap * scale);
        assert(abs(eyes[0].x - (int32_t(xy) + eyes[0].size + expectedGap)) < .01);
        assert(eyes[0].x > int32_t(xy) + eyes[0].size);
    };
    aligned();
    assert(eyes[0].available);
    assert(Spectate::Field<WidgetRect>(squelch.data(), 0x70)[0] == 835);
    const auto firstEye = eyes[0];
    eyes = Native::PartyEyes(1280, 720, peers);
    assert(eyes[0].x == firstEye.x && eyes[0].y == firstEye.y); // No drift on repeated frames.
    assert(Native::PartyEyes(1280, 720, {}).empty());
    assert(Spectate::Field<WidgetRect>(squelch.data(), 0x70)[0] == 835);
    eyes = Native::PartyEyes(1280, 720, peers);
    Put(row.data(), 0x51, uint8_t(0));
    assert(Native::PartyEyes(1280, 720, peers).empty());
    Put(row.data(), 0x51, uint8_t(1));
    peers.push_back({2, "OtherAct", true, true, GetTickCount64()});
    assert(Native::PartyEyes(1280, 720, peers).empty());
    peers.pop_back();
    Put(label.data(), 0x90, uint64_t(999999));
    assert(Native::PartyEyes(1280, 720, peers).empty());
    Put(label.data(), 0x90, uint64_t(8));
    // Replay the live 1280x720 party hierarchy read from both TCP/IP games:
    // PanelManager -> PartyPanel -> ListContainer -> View -> Container ->
    // ListItem0 -> PlayerName. The unopened template remains attached/hidden.
    std::array<std::array<uint8_t, 0x600>, 4> layout{};
    const char* names[]{"ListContainer", "View", "Container", "ListItem0"};
    void* descendants[]{layout[1].data(), layout[2].data(), layout[3].data(), label.data()};
    for (size_t i = 0; i < layout.size(); ++i) {
        auto* widget = layout[i].data();
        Put(widget, 8, names[i]);
        Put(widget, 0x30, i ? layout[i - 1].data() : panel.data());
        Put(widget, 0x50, uint8_t(1));
        Put(widget, 0x51, uint8_t(1));
        Put(widget, 0x58, descendants + i);
        Put(widget, 0x60, uint64_t(1));
        Put(widget, 0x80, 1.f);
    }
    void* liveChildren[]{layout[0].data(), row.data()};
    Put(row.data(), 0x51, uint8_t(0)); // Native hidden FriendsListTemplate.
    Put(panel.data(), 0x58, liveChildren + 0);
    Put(panel.data(), 0x60, uint64_t(2));
    Put(panel.data(), 0x30, manager.data());
    Put(panel.data(), 0x70, std::array<int32_t, 4>{0, -651, 1162, 1487});
    Put(panel.data(), 0x80, 1.f);
    Put(manager.data(), 0x80, 0.3333333432674408f);
    Put(label.data(), 0x30, layout.back().data());
    Put(layout.back().data(), 0x58, rowChildren + 0);
    Put(layout.back().data(), 0x60, uint64_t(std::size(rowChildren)));
    Put(layout.back().data(), 0x70, std::array<int32_t, 4>{0, 0, 996, 130});
    for (auto* control : {squelch.data(), loot.data(), invite.data(), partyInfo.data()})
        Put(control, 0x30, layout.back().data());
    eyes = Native::PartyEyes(1280, 720, peers);
    aligned();
    assert(abs(eyes[0].size - 25.f) < .01);
    // Anchor after the unmodified native squelch control at every HD scale.
    // HC's extra Loot Body control must also retain its native position.
    for (float scale : {.33333334f, .5f, .6666667f, 1.f}) {
        Put(manager.data(), 0x80, scale);
        eyes = Native::PartyEyes(3840, 2160, peers);
        aligned();
    }
    Put(manager.data(), 0x80, .33333334f);
    Put(squelch.data(), 0x70, std::array<int32_t, 4>{885, 33, 75, 75});
    Put(loot.data(), 0x51, uint8_t(1));
    Put(partyInfo.data(), 0x51, uint8_t(1));
    Put(invite.data(), 0x51, uint8_t(0));
    eyes = Native::PartyEyes(1280, 720, peers);
    aligned();
    const auto movedLoot = Spectate::Field<WidgetRect>(loot.data(), 0x70);
    const auto movedSquelch = Spectate::Field<WidgetRect>(squelch.data(), 0x70);
    assert(movedLoot[0] == 785 && movedSquelch[0] == 885);
    assert(Native::PartyEyes(1280, 720, {}).empty());
    assert(Spectate::Field<WidgetRect>(squelch.data(), 0x70)[0] == 885);
    assert(Spectate::Field<WidgetRect>(loot.data(), 0x70)[0] == 785);
    Put(layout[1].data(), 0x51, uint8_t(0));
    assert(Native::PartyEyes(1280, 720, peers).empty());
    Put(layout[1].data(), 0x51, uint8_t(1));
    peers[0].sharing = false;
    eyes = Native::PartyEyes(1280, 720, peers);
    assert(eyes.size() == 1 && !eyes[0].available);
    Put(manager.data(), 0xb9, uint8_t(0));
    assert(Native::PartyEyes(1280, 720, peers).empty());
    clients[1].dwClientState = 0;
    sent.clear();
    Tick(&game);
    assert(std::none_of(sent.begin(), sent.end(), [](const auto& p) { return p.action == Data::Chunk; }));
    assert(!PartyLayout::Place(100, 100, 75, 75, .5f, 160, 500));
    assert(!PartyLayout::Place(100, 100, 75, 75, 0, 500, 500));
    const auto nonsquare = PartyLayout::Place(100, 100, 80, 75, .5f, 500, 500);
    assert(nonsquare && nonsquare->x == 148 && nonsquare->size == 37.5f);
    Destroyed(&game);
    Stream::Local().Stop();
    Stream::Host().Stop();
    SetAvailable(false);
    assert(!Command(&game, &units[0], nullptr, 0));
    std::cout << "PASS: native Spectate client/server dispatch, real-executable guards, sender identity, "
                 "cross-area roster, detoured packet queue, TCP/IP session kind, native party geometry, "
                 "visibility, malformed labels, duplicate names "
                 "and loading\n";
}
