#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <vector>

namespace AutomapLayers::Data {
inline constexpr size_t NativeLayers = 100;
inline constexpr size_t HeaderBytes = NativeLayers * sizeof(uint32_t);
inline constexpr size_t RecordBytes = 8 * sizeof(uint32_t);
inline constexpr uint32_t MaxLayer = 0x7fffffff;
struct Cell { uint16_t image; int16_t x, y; };
static_assert(sizeof(Cell) == 6);
struct RecordView {
    uint32_t layer = 0, special = 0, metadata = 0;
    std::array<std::span<const uint8_t>, 4> cells;
};
struct Record {
    uint32_t special = 0;
    std::array<std::vector<Cell>, 4> cells;
};
inline uint32_t Word(std::span<const uint8_t> bytes, size_t at) {
    uint32_t value; std::memcpy(&value, bytes.data() + at, sizeof(value)); return value;
}
inline void Put(std::vector<uint8_t>& bytes, size_t at, uint32_t value) {
    std::memcpy(bytes.data() + at, &value, sizeof(value));
}
// Native .ma0-.ma3 files have a 400-byte head table followed by consecutive
// records: next, layer, special-sprite-set, metadata, four byte counts, cells.
// The layer in each record is already 32-bit. Only the native head-table lookup
// is limited to 0..99. Scan record boundaries for extended IDs, never clamp or
// alias them into a vanilla slot. Leave native records and their table intact.
template<class Visit>
bool Walk(std::span<const uint8_t> bytes, Visit&& visit) {
    if (bytes.empty()) return true;
    if (bytes.size() < HeaderBytes || bytes.size() > UINT32_MAX) return false;
    size_t at = HeaderBytes;
    while (at < bytes.size()) {
        if (bytes.size() - at < RecordBytes) return false;
        RecordView record;
        record.layer = Word(bytes, at + 4);
        record.special = Word(bytes, at + 8);
        record.metadata = Word(bytes, at + 12);
        size_t end = at + RecordBytes;
        for (size_t group = 0; group < record.cells.size(); ++group) {
            const auto count = Word(bytes, at + 16 + group * 4);
            if (count % sizeof(Cell) || count > bytes.size() - end) return false;
            record.cells[group] = bytes.subspan(end, count);
            end += count;
        }
        visit(at, record);
        at = end;
    }
    return true;
}
inline bool Load(std::span<const uint8_t> bytes, uint32_t layer, std::vector<Record>& output) {
    if (layer < NativeLayers || layer > MaxLayer) return false;
    std::vector<Record> selected;
    if (!Walk(bytes, [&](size_t, const RecordView& view) {
        if (view.layer != layer) return;
        Record record; record.special = view.special;
        for (size_t group = 0; group < view.cells.size(); ++group) {
            auto& cells = record.cells[group];
            cells.resize(view.cells[group].size() / sizeof(Cell));
            if (!cells.empty()) std::memcpy(cells.data(), view.cells[group].data(), view.cells[group].size());
        }
        selected.push_back(std::move(record));
    })) return false;
    output = std::move(selected);
    return true;
}
inline bool Append(std::span<const uint8_t> bytes, const RecordView& record, std::vector<uint8_t>& output) {
    if (record.layer < NativeLayers || record.layer > MaxLayer || record.special > 3) return false;
    if (bytes.size() > UINT32_MAX - HeaderBytes - RecordBytes) return false;
    size_t size = (bytes.empty() ? HeaderBytes : bytes.size()) + RecordBytes;
    for (const auto cells : record.cells) {
        if (cells.size() % sizeof(Cell) || cells.size() > UINT32_MAX - size) return false;
        size += cells.size();
    }
    if (size > UINT32_MAX) return false;
    size_t previous = 0;
    if (!Walk(bytes, [&](size_t at, const RecordView& view) {
        if (view.layer == record.layer) previous = at;
    })) return false;
    std::vector<uint8_t> updated(bytes.begin(), bytes.end());
    if (updated.empty()) updated.resize(HeaderBytes, 0);
    const auto at = updated.size(); updated.resize(size, 0);
    // Link subsequent batches for the same extended layer as native saves do.
    // Its first record is found by ID instead of an out-of-bounds table access.
    if (previous) Put(updated, previous, static_cast<uint32_t>(at));
    Put(updated, at + 4, record.layer);
    Put(updated, at + 8, record.special);
    Put(updated, at + 12, record.metadata);
    size_t end = at + RecordBytes;
    for (size_t group = 0; group < record.cells.size(); ++group) {
        const auto cells = record.cells[group];
        Put(updated, at + 16 + group * 4, static_cast<uint32_t>(cells.size()));
        if (!cells.empty()) std::memcpy(updated.data() + end, cells.data(), cells.size());
        end += cells.size();
    }
    output = std::move(updated);
    return true;
}
}
