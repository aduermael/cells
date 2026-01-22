// =============================================================================
// Spill Index - Spatial Index for Spill Range Lookup
// =============================================================================
//
// Uses an R-tree to efficiently find spill ranges intersecting a viewport.
// This eliminates the O(n) iteration over all cells to find spill masters.
//
// Key responsibilities:
// - Store spill master IDs with their spill extent bounding boxes
// - Query spill masters whose spill ranges intersect a viewport: O(log n + k)
// - Update index when spill ranges are registered/cleared
//
// Architecture notes:
// - Spill data is owned by Workbook (SpillInfo structs)
// - SpillIndex stores master cell IDs indexed by their spill extent bounds
// - The bounding box includes the master cell AND all spilled positions
// - When column/row positions change, bounds must be updated
//
// Dependencies: rtree.h, types.h
// Used by: Sheet (for viewport queries) and Workbook (for spill registration)
//
// =============================================================================

#ifndef CELLS_SPILL_INDEX_H_
#define CELLS_SPILL_INDEX_H_

#include <cstdint>

#include <functional>
#include <unordered_map>
#include <vector>

#include "core/cells/rtree.h"
#include "core/cells/types.h"

namespace cells {

// Position bounds for a spill extent (master cell + all spilled positions)
struct SpillPositionBounds {
    uint32_t startCol;
    uint32_t startRow;
    uint32_t endCol;
    uint32_t endRow;

    SpillPositionBounds() : startCol(0), startRow(0), endCol(0), endRow(0) {}
    SpillPositionBounds(uint32_t sc, uint32_t sr, uint32_t ec, uint32_t er)
        : startCol(sc), startRow(sr), endCol(ec), endRow(er) {}
};

// SpillIndex - spatial index for spill range lookup
//
// Usage:
//   SpillIndex index;
//   index.insert(masterCellId, 0, 0, 2, 4);  // Spill extent at positions A1:C5
//   auto masters = index.queryRange(0, 0, 10, 10);  // Find spill masters in viewport
//
class SpillIndex {
public:
    SpillIndex() = default;
    ~SpillIndex() = default;

    // Non-copyable (owns R-tree which may have complex state)
    SpillIndex(const SpillIndex&) = delete;
    SpillIndex& operator=(const SpillIndex&) = delete;

    // Movable
    SpillIndex(SpillIndex&&) noexcept = default;
    SpillIndex& operator=(SpillIndex&&) noexcept = default;

    // ========================================================================
    // Insert/Remove operations
    // ========================================================================

    // Insert a spill master with its extent bounding box
    // The bounds should include the master cell position and all spilled positions
    void insert(const ID& masterCellId, uint32_t startCol, uint32_t startRow, uint32_t endCol,
                uint32_t endRow);

    // Remove a spill master from the index
    // Returns true if the master was found and removed
    bool remove(const ID& masterCellId);

    // Update the position bounds of an existing spill extent
    // Returns true if the master was found and updated
    bool updateBounds(const ID& masterCellId, uint32_t newStartCol, uint32_t newStartRow,
                      uint32_t newEndCol, uint32_t newEndRow);

    // ========================================================================
    // Query operations
    // ========================================================================

    // Find all spill masters whose extents contain the cell at (col, row)
    [[nodiscard]] std::vector<ID> queryAt(uint32_t col, uint32_t row) const;

    // Find all spill masters whose extents intersect the given viewport rectangle
    [[nodiscard]] std::vector<ID> queryRange(uint32_t startCol, uint32_t startRow, uint32_t endCol,
                                             uint32_t endRow) const;

    // Check if any spill extent contains the cell at (col, row)
    [[nodiscard]] bool hasSpillAt(uint32_t col, uint32_t row) const;

    // Get the position bounds of a spill extent (returns nullptr if not indexed)
    [[nodiscard]] const SpillPositionBounds* getBounds(const ID& masterCellId) const;

    // ========================================================================
    // Utility operations
    // ========================================================================

    // Get the number of spill extents in the index
    [[nodiscard]] size_t size() const { return _rtree.size(); }

    // Check if empty
    [[nodiscard]] bool empty() const { return _rtree.empty(); }

    // Clear all spill extents from the index
    void clear();

    // Iterate over all indexed spill masters
    void forEach(const std::function<void(const ID&, const SpillPositionBounds&)>& callback) const;

private:
    // R-tree for spatial queries (stores master cell IDs)
    RTree<ID> _rtree;

    // Track position bounds for each master (needed for removal/update)
    // Maps master cell ID -> position bounds
    std::unordered_map<ID, SpillPositionBounds, IDHash> _bounds;

    // Helper to create BoundingRect from position bounds
    static BoundingRect makeBounds(uint32_t startCol, uint32_t startRow, uint32_t endCol,
                                   uint32_t endRow);
};

}  // namespace cells

#endif  // CELLS_SPILL_INDEX_H_
