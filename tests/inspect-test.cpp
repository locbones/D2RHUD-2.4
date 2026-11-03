#define NOMINMAX
#include "../d2rhud/plugin/D2RHUD/HotkeyNames.h"
#include "../d2rhud/plugin/Inspect.cpp"
#include "../d2rhud/plugin/InspectPresentation.h"
#include "../d2rhud/plugin/KeyMappings.cpp"
#include <cassert>
#include <iostream>
namespace {
std::optional<uint32_t> local = 0;
bool town = true, targetExists = true;
std::vector<Inspect::Data::CommandPacket> commands;
void Deliver(const Inspect::Data::Packet& p) {
    Inspect::Receive(reinterpret_cast<const uint8_t*>(&p), sizeof(p));
}
} // namespace
std::optional<uint32_t> Inspect::Native::LocalId() { return local; }
bool Inspect::Native::InTown() { return town; }
bool Inspect::Native::CanInspect(uint32_t type, uint32_t id) {
    return town && targetExists && type == 0 && id == 2;
}
void Inspect::Native::Send(const Data::CommandPacket& c) { commands.push_back(c); }
int main() {
    using namespace Inspect;
    using namespace Data;
    assert(HudHotkeys::VirtualKey("I", keyMap) == 0x49 && HudHotkeys::VirtualKey("VK_I", keyMap) == 0x49);
    assert(HudHotkeys::VirtualKey("ctrl", keyMap) == VK_CONTROL);
    for (char c = 'A'; c <= 'Z'; ++c)
        assert(HudHotkeys::VirtualKey(std::string(1, c), keyMap) == c);
    for (char c = '0'; c <= '9'; ++c)
        assert(HudHotkeys::VirtualKey(std::string(1, c), keyMap) == c);
    assert(HudHotkeys::VirtualKey("F24", keyMap) == VK_F24 &&
           HudHotkeys::VirtualKey("VK_F12", keyMap) == VK_F12);
    assert(!HudHotkeys::VirtualKey("F1bad", keyMap) && !HudHotkeys::VirtualKey("F25", keyMap));
    assert(HudHotkeys::VirtualKey("LBUTTON", keyMap) == VK_LBUTTON);
    assert(!GetAppearance().labels && GetAppearance().lift==96 && GetAppearance().textSize==22);
    SetAppearance({true,999,2});
    assert(GetAppearance().labels && GetAppearance().lift==240 && GetAppearance().textSize==16);
    SetAppearance({});
    Snapshot s;
    s.name = "PvP Ж Player";
    s.id = 2;
    s.owner = s.name;
    s.designation = "Player";
    Item sword;
    sword.id = 3;
    sword.slot = 4;
    sword.code = "7gd";
    sword.name = "The Grandfather";
    sword.flags = 0x10;
    sword.quality = 7;
    sword.width = 2;
    sword.height = 4;
    sword.stats = {{0, 0, 20}, {7, 0, 25600}};
    sword.tooltip = "ÿc3+20 to Strength\nÿc0Two-Hand Damage: 100 to 200\nÿc4The Grandfather";
    s.items.push_back(sword);
    Item charm;
    charm.id = 4;
    charm.code = "cm3";
    charm.name = "Grand Charm";
    charm.x = 3;
    charm.y = 1;
    charm.height = 3;
    charm.flags = 0x10;
    charm.stats = {{83, 1, 1}, {39, 0, 15}};
    s.items.push_back(charm);
    const auto encoded = Encode(s);
    assert(!encoded.empty());
    auto decoded = Decode(encoded);
    assert(decoded && decoded->items.size() == 2 && decoded->items[1].stats == charm.stats &&
           decoded->name == s.name && decoded->items[0].tooltip == sword.tooltip);
    const auto lines = Presentation::TooltipLines(sword.tooltip);
    assert(lines.size() == 3 && lines[0][0].text == "The Grandfather" && lines[0][0].color == 4 &&
           lines[2][0].text == "+20 to Strength" && lines[2][0].color == 3);
    assert(!Tooltip(std::string(MaxTooltip + 1, 'x')) && !Tooltip("hidden\x01control"));
    const auto panel = Presentation::Placement(2560, 1080, 1132, 1730);
    assert(panel.x == 140 && panel.y == 0 && panel.width == 566 && panel.height == 865);
    for (size_t n = 0; n < encoded.size(); ++n)
        assert(!Decode(std::span(encoded).first(n)));
    auto tail = encoded;
    tail.push_back(0);
    assert(!Decode(tail));
    auto duplicate = s;
    duplicate.items.push_back(charm);
    assert(Encode(duplicate).empty());
    duplicate = s;
    duplicate.items[1].slot = 4;
    assert(Encode(duplicate).empty());
    auto invalid = s;
    invalid.items[0].name = "\xc0\xaf";
    assert(Encode(invalid).empty());
    assert(!Text("\xed\xa0\x80") && !Text("\xf4\x90\x80\x80") && Text("中文 Ж"));
    invalid = s;
    invalid.items[0].stats.resize(MaxStats + 1);
    assert(Encode(invalid).empty());
    invalid = s;
    invalid.items[0].stats[0].id = 4096;
    assert(Encode(invalid).empty());
    HudFrame::Context frame;
    frame.inGame = true;
    ClientTick(frame, 17);
    Open(0, 2);
    ClientTick(frame, 17);
    assert(commands.empty() && Current().open && Current().busy);
    Close();
    Packet hello;
    hello.epoch = 9;
    hello.seed = 123;
    hello.recipient = 0;
    Deliver(hello); // Unit GUID zero is valid.
    Open(0, 2);
    ClientTick(frame, 17);
    assert(commands.size() == 1 && Current().busy);
    const auto request = commands.back().packet;
    Packet wrong = hello;
    wrong.action = Chunk;
    wrong.request = request.request;
    wrong.type = 0;
    wrong.target = 2;
    wrong.total = uint32_t(encoded.size());
    wrong.length = uint16_t((std::min)(Payload, encoded.size()));
    memcpy(wrong.bytes.data(), encoded.data(), wrong.length);
    wrong.recipient = 10;
    Deliver(wrong);
    assert(!Current().snapshot);
    wrong.recipient = 0;
    wrong.epoch = 8;
    Deliver(wrong);
    assert(!Current().snapshot);
    wrong.epoch = 9;
    wrong.target = 3;
    Deliver(wrong);
    assert(!Current().snapshot);
    Assembly assembly;
    for (size_t at = 0; at < encoded.size(); at += Payload) {
        auto p = request;
        p.opcode = 0x2b;
        p.action = Chunk;
        p.total = uint32_t(encoded.size());
        p.offset = uint32_t(at);
        p.length = uint16_t((std::min)(Payload, encoded.size() - at));
        memcpy(p.bytes.data(), encoded.data() + at, p.length);
        assert(assembly.Add(p));
        assert(assembly.Add(p));
        Deliver(p);
        if (at + Payload < encoded.size())
            assert(!Current().snapshot);
    }
    assert(assembly.Complete() && Current().snapshot && Current().snapshot->items[1].code == "cm3" &&
           !Current().busy);
    Refresh();
    ClientTick(frame, 17);
    assert(Current().busy && Current().snapshot &&
           commands.size() == 1); // Retain the last complete view while rate-limited.
    Client().lastRequest = GetTickCount64() - 1001;
    ClientTick(frame, 17);
    assert(commands.size() == 2);
    auto malformed = request;
    malformed.action = Chunk;
    malformed.total = 1;
    malformed.length = 2;
    assert(!Assembly{}.Add(malformed));
    malformed.total = MaxBytes + 1;
    assert(!Assembly{}.Add(malformed));
    frame.loading = true;
    ClientTick(frame, 17);
    assert(!Current().open);
    frame.loading = false;
    ClientTick(frame, 18);
    Open(0, 2);
    ClientTick(frame, 18);
    assert(commands.size() == 2); // stale hello cannot authorize a new game
    hello.epoch = 10;
    Deliver(hello);
    ClientTick(frame, 18);
    assert(commands.size() == 3 && Current().busy); // Opening before Hello resumes automatically.
    Close();
    targetExists = false;
    Open(0, 2);
    ClientTick(frame, 18);
    assert(!Current().open && !Current().snapshot && commands.size() == 3);
    Refresh();
    ClientTick(frame, 18);
    assert(!Current().open && commands.size() == 3); // No stale target from the previous inspection.
    targetExists = true;
    Open(0, 2);
    Close();
    ClientTick(frame, 18);
    assert(!Current().open && commands.size() == 3); // Closing cancels a queued pick too.
    Deliver(hello);
    town = false;
    ClientTick(frame, 18);
    assert(!Open(0, 2) && !Current().open);
    town = true;
    assert(Open(0, 2));
    town = false;
    ClientTick(frame, 18);
    Deliver(wrong);
    assert(!Current().open && !Current().snapshot); // Leaving town discards in-flight data.
    town = true;
    SetEnabled(false);
    Open(0, 2);
    assert(!Current().open);
    SetEnabled(true);
    std::cout << "PASS: binary equipment/backpack/stat roundtrip; all truncations; malformed UTF-8, sizes, "
                 "duplicate gear; host capability handshake; GUID zero; recipient, epoch and target binding; "
                 "atomic fragmented publication; duplicate chunks; disabled/loading/new-game handling.\n";
}
