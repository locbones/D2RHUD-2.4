#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <span>
#include <vector>

namespace MercSeller::Data {
constexpr size_t HirelingSize = 0x150, MetadataSize = 0x1c, MenuSize = 0x48;
constexpr size_t NativeNpcCount = 43, MaxRows = 65536, DialogCapacity = 10, RosterCapacity = 69;
using Metadata = std::array<uint8_t, MetadataSize>;
using Menu = std::array<uint8_t, MenuSize>;
template <class T> T Get(const void* data, size_t offset) {
    T result;
    std::memcpy(&result, static_cast<const uint8_t*>(data) + offset, sizeof(result));
    return result;
}
template <class T> void Put(void* data, size_t offset, T value) {
    std::memcpy(static_cast<uint8_t*>(data) + offset, &value, sizeof(value));
}
inline bool RetailSeller(int id) { return id == 150 || id == 198 || id == 252 || id == 515; }
struct Catalog {
    struct UnitRow {
        size_t index;
        uint16_t first, last;
    };
    // Stable nodes are retained for the lifetime of any installed relay.
    std::array<std::map<int, Metadata>, 6> sellers;
    std::array<std::map<int, size_t>, 6> firstRows;
    std::array<std::map<int, std::vector<UnitRow>>, 6> unitRows;
    std::map<int, Metadata> native;
    const Metadata* Find(int seller, bool expansion, int difficulty) const {
        if (difficulty < 0 || difficulty > 2)
            return nullptr;
        const auto& group = sellers[size_t(expansion) * 3 + difficulty];
        const auto found = group.find(seller);
        return found == group.end() ? nullptr : &found->second;
    }
};
inline Catalog Build(std::span<const uint8_t> rows, std::span<const uint8_t> monsters,
                     std::span<const uint8_t> metadata) {
    Catalog result;
    if (rows.size() % HirelingSize || rows.size() / HirelingSize > MaxRows || monsters.size() % 0x1dc ||
        metadata.size() != NativeNpcCount * MetadataSize)
        return result;
    for (size_t i = 0; i < NativeNpcCount; ++i) {
        Metadata value;
        std::memcpy(value.data(), metadata.data() + i * MetadataSize, value.size());
        result.native.emplace(Get<int16_t>(value.data(), 0), value);
    }
    std::array<std::map<int, bool>, 6> examined;
    for (size_t offset = 0; offset < rows.size(); offset += HirelingSize) {
        const auto row = rows.data() + offset;
        const auto version = Get<uint16_t>(row, 0);
        const auto difficulty = Get<int32_t>(row, 0x10), seller = Get<int32_t>(row, 0x14);
        if ((version != 0 && version != 100) || difficulty < 1 || difficulty > 3 || seller < 0 ||
            seller > INT16_MAX)
            continue;
        const size_t group = size_t(version == 100) * 3 + difficulty - 1;
        // The engine selects the first matching row, too. Do not accept a
        // later valid row when that first row would fail native initialization.
        if (!examined[group].emplace(seller, true).second)
            continue;
        const auto act = Get<int32_t>(row, 0xc), id = Get<int32_t>(row, 4), type = Get<int32_t>(row, 8);
        const auto firstName = Get<uint16_t>(row, 0x14c), lastName = Get<uint16_t>(row, 0x14e);
        if (size_t(seller) >= monsters.size() / 0x1dc || !(monsters[size_t(seller) * 0x1dc + 0x3d] & 2) ||
            act < 1 || act > 5 || id < 0 || id > 255 || type < 0 || size_t(type) >= monsters.size() / 0x1dc ||
            firstName > lastName || size_t(lastName - firstName + 1) > RosterCapacity)
            continue;
        Metadata value{};
        const auto original = result.native.find(seller);
        if (original != result.native.end())
            value = original->second;
        else
            Put<int32_t>(value.data(), 0x18, -1);
        Put<int16_t>(value.data(), 0, int16_t(seller));
        value[7] = 1;
        value[0xa] = uint8_t(act - 1);
        // Existing merc vendors retain their own quest/level requirements.
        // A smith's or quest NPC's unrelated service gates do not become new
        // mercenary requirements merely because the mod assigns it Seller.
        if (!RetailSeller(seller)) {
            value[0x10] = 0;
            value[0x11] = 0;
        }
        result.sellers[group].emplace(seller, value);
        result.firstRows[group].emplace(seller, offset / HirelingSize);
    }
    // Attachment and client initialization know the unit class. Names alone
    // are ambiguous when a new type reuses another hireling's name range.
    for (size_t offset = 0; offset < rows.size(); offset += HirelingSize) {
        const auto row = rows.data() + offset;
        const auto version = Get<uint16_t>(row, 0);
        const auto difficulty = Get<int32_t>(row, 0x10), type = Get<int32_t>(row, 8);
        const auto id = Get<int32_t>(row, 4), act = Get<int32_t>(row, 0xc);
        const auto first = Get<uint16_t>(row, 0x14c), last = Get<uint16_t>(row, 0x14e);
        if ((version != 0 && version != 100) || difficulty < 1 || difficulty > 3 || type < 0 ||
            size_t(type) >= monsters.size() / 0x1dc || id < 0 || id > 255 || act < 1 || act > 5 ||
            first > last || size_t(last - first + 1) > RosterCapacity ||
            !result.Find(Get<int32_t>(row, 0x14), version == 100, difficulty - 1))
            continue;
        result.unitRows[size_t(version == 100) * 3 + difficulty - 1][type].push_back(
            {offset / HirelingSize, first, last});
    }
    return result;
}
struct DialogEntry {
    char text[120]{};
    uint32_t kind = 1, color = 0;
    uintptr_t callback = 0;
    uint32_t enabled = 1, padding = 0;
};
static_assert(sizeof(DialogEntry) == 0x90 && offsetof(DialogEntry, callback) == 0x80);
inline bool AddHire(void* dialog, const char* text, uintptr_t callback) {
    if (!dialog || !text || !*text || !callback)
        return false;
    const auto count = Get<int32_t>(dialog, 0x1c), actions = Get<int32_t>(dialog, 0x18);
    if (count < 2 || count >= int(DialogCapacity) || actions < 0 || actions >= count)
        return false;
    auto entries = reinterpret_cast<DialogEntry*>(static_cast<uint8_t*>(dialog) + 0x30);
    for (int i = 0; i < count; ++i)
        if (entries[i].callback == callback)
            return false;
    // The final native entry is always Quit. Keep its callback and position.
    entries[count] = entries[count - 1];
    entries[count - 1] = DialogEntry{};
    auto& hire = entries[count - 1];
    hire.callback = callback;
    const auto length = (std::min)(std::strlen(text), sizeof(hire.text) - 1);
    std::memcpy(hire.text, text, length);
    Put<int32_t>(dialog, 0x1c, count + 1);
    Put<int32_t>(dialog, 0x18, actions + 1);
    return true;
}
} // namespace MercSeller::Data
