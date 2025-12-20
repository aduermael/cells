#ifndef CELLS_QUADTREE_H_
#define CELLS_QUADTREE_H_

#include <cstdint>

#include <memory>
#include <vector>

#include "core/cells/model.h"
#include "core/cells/types.h"

namespace cells {

// Rectangle representing a bounding box
struct Rect {
    uint32_t x1;  // Left (inclusive)
    uint32_t y1;  // Top (inclusive)
    uint32_t x2;  // Right (exclusive)
    uint32_t y2;  // Bottom (exclusive)

    Rect() : x1(0), y1(0), x2(0), y2(0) {}
    Rect(uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2) : x1(x1), y1(y1), x2(x2), y2(y2) {}

    // Check if this rect contains a point
    [[nodiscard]] bool contains(uint32_t x, uint32_t y) const {
        return x >= x1 && x < x2 && y >= y1 && y < y2;
    }

    // Check if this rect intersects with another
    [[nodiscard]] bool intersects(const Rect& other) const {
        return x2 > other.x1 && other.x2 > x1 && y2 > other.y1 && other.y2 > y1;
    }

    // Get width and height
    [[nodiscard]] uint32_t width() const { return x2 - x1; }
    [[nodiscard]] uint32_t height() const { return y2 - y1; }
};

// Entry in the quadtree: cell pointer + position
struct QuadtreeEntry {
    Cell* cell;
    uint32_t x;  // Column position
    uint32_t y;  // Row position

    QuadtreeEntry() : cell(nullptr), x(0), y(0) {}
    QuadtreeEntry(Cell* cell, uint32_t x, uint32_t y) : cell(cell), x(x), y(y) {}
};

// Quadtree node - stores entries or subdivides into 4 children
class QuadtreeNode {
public:
    // Maximum entries before subdivision (tune for performance)
    static constexpr size_t MAX_ENTRIES = 16;

    // Maximum tree depth (prevents infinite subdivision)
    static constexpr size_t MAX_DEPTH = 20;

    explicit QuadtreeNode(const Rect& bounds, size_t depth = 0);

    // Insert a cell at the given position
    // Returns true if inserted, false if out of bounds
    bool insert(Cell* cell, uint32_t x, uint32_t y);

    // Remove a cell at the given position
    // Returns true if removed, false if not found
    bool remove(Cell* cell, uint32_t x, uint32_t y);

    // Query all cells within the given rectangle
    void query(const Rect& rect, std::vector<QuadtreeEntry>& results) const;

    // Get all entries in this node and children
    void all(std::vector<QuadtreeEntry>& results) const;

    // Get entry count (for debugging/testing)
    [[nodiscard]] size_t count() const;

    // Get bounds
    [[nodiscard]] const Rect& bounds() const { return _bounds; }

private:
    Rect _bounds;
    size_t _depth;
    std::vector<QuadtreeEntry> _entries;

    // Children: NW, NE, SW, SE (null if not subdivided)
    std::unique_ptr<QuadtreeNode> _nw;
    std::unique_ptr<QuadtreeNode> _ne;
    std::unique_ptr<QuadtreeNode> _sw;
    std::unique_ptr<QuadtreeNode> _se;

    // Subdivide this node into 4 children
    void subdivide();

    // Check if this node is a leaf (no children)
    [[nodiscard]] bool isLeaf() const { return _nw == nullptr; }

    // Get child node that contains the given point
    QuadtreeNode* childContaining(uint32_t x, uint32_t y);
    [[nodiscard]] const QuadtreeNode* childContaining(uint32_t x, uint32_t y) const;
};

// Quadtree - spatial index for cells
// Uses (column.position, row.position) as coordinates
class Quadtree {
public:
    // Create quadtree with bounds for Excel-sized grid
    // Default: 16384 columns x 1048576 rows
    Quadtree();
    explicit Quadtree(const Rect& bounds);

    // Build quadtree from sheet
    // Reads column/row positions from axes
    void build(const Sheet& sheet);

    // Insert a cell (looks up positions from sheet's axes)
    bool insert(Cell* cell, const Sheet& sheet);

    // Insert with explicit position
    bool insert(Cell* cell, uint32_t col, uint32_t row);

    // Remove a cell
    bool remove(Cell* cell, const Sheet& sheet);
    bool remove(Cell* cell, uint32_t col, uint32_t row);

    // Query cells in viewport
    [[nodiscard]] std::vector<QuadtreeEntry> query(const Rect& viewport) const;
    [[nodiscard]] std::vector<QuadtreeEntry> query(uint32_t x1, uint32_t y1, uint32_t x2,
                                                   uint32_t y2) const;

    // Get all entries
    [[nodiscard]] std::vector<QuadtreeEntry> all() const;

    // Get entry count
    [[nodiscard]] size_t count() const;

    // Clear all entries
    void clear();

    // Get bounds
    [[nodiscard]] const Rect& bounds() const { return _root.bounds(); }

private:
    QuadtreeNode _root;
};

}  // namespace cells

#endif  // CELLS_QUADTREE_H_
