#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <set>
#include <tuple>
#include <vector>

namespace DynamicAutomap::Data {
constexpr size_t MaxCells = 65536, MaxUnits = 16384;
constexpr int DefaultInterval = 100, MinimumInterval = 50, MaximumInterval = 5000;
inline int Interval(int value) { return std::clamp(value, MinimumInterval, MaximumInterval); }
struct Cell {
    int32_t image = 0, x = 0, y = 0;
    auto Key() const { return std::tuple{y, x, image}; }
    bool operator<(const Cell& other) const { return Key() < other.Key(); }
    bool operator==(const Cell& other) const = default;
};
inline Cell Position(int image, int32_t projectedX, int32_t projectedY) {
    // Native D2R.exe+94400: signed division truncates toward zero.
    return {image, projectedX / 10 + 1, projectedY / 10 - 3};
}
inline bool Live(uint32_t mode, uint32_t flags) {
    // Monster death/dead modes, removed unit, and native discovery bits.
    return mode != 0 && mode != 12 && !(flags & 0x10000) && (flags & 0x30000000);
}
struct Ownership {
    std::set<Cell> monsters, objects;
    bool Remember(const Cell& cell, bool monster) {
        auto& target = monster ? monsters : objects;
        if (target.size() < MaxCells)
            return target.insert(cell).second;
        return false;
    }
    bool Remove(const Cell& cell, const std::set<int>& monsterImages,
                const std::set<int>& objectImages) const {
        // Object provenance wins when a mod shares the exact same sprite and
        // position. Ambiguous old cached cells are preserved, never guessed.
        return !objects.contains(cell) && (monsters.contains(cell) || (monsterImages.contains(cell.image) &&
                                                                       !objectImages.contains(cell.image)));
    }
};
// Renderer-only copies. They never enter the engine's allocator, exploration
// trees, saved flags or .ma serializer.
struct Node {
    Node *parent = nullptr, *left = nullptr, *right = nullptr;
    uint64_t color = 0;
    uint8_t saved = 1, pad = 0;
    int16_t image = 0;
    int32_t x = 0, y = 0, padding = 0;
};
struct Tree {
    Node *root = nullptr, *first = nullptr, *last = nullptr;
    uint64_t color = 0, count = 0;
};
static_assert(sizeof(Node) == 0x30 && offsetof(Node, image) == 0x22 && sizeof(Tree) == 0x28);
struct Geometry {
    Tree tree;
    std::vector<Node> nodes;
    explicit Geometry(std::vector<Cell> cells) {
        std::sort(cells.begin(), cells.end());
        cells.erase(std::unique(cells.begin(), cells.end()), cells.end());
        nodes.reserve(cells.size());
        for (const auto& cell : cells) {
            Node node;
            node.image = int16_t(cell.image);
            node.x = cell.x;
            node.y = cell.y;
            nodes.push_back(node);
        }
        auto header = reinterpret_cast<Node*>(&tree);
        const auto link = [&](auto&& self, size_t first, size_t last, Node* parent) -> Node* {
            if (first == last)
                return nullptr;
            const auto middle = first + (last - first) / 2;
            auto node = &nodes[middle];
            node->parent = parent;
            node->left = self(self, first, middle, node);
            node->right = self(self, middle + 1, last, node);
            return node;
        };
        tree.root = link(link, 0, nodes.size(), header);
        tree.first = nodes.empty() ? header : &nodes.front();
        tree.last = nodes.empty() ? header : &nodes.back();
        tree.count = nodes.size();
    }
    Geometry(const Geometry&) = delete;
    Geometry& operator=(const Geometry&) = delete;
};
} // namespace DynamicAutomap::Data
