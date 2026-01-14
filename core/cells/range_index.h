// =============================================================================
// Range Index - Spatial Index for Range Lookup
// =============================================================================
//
// Uses an R-tree to efficiently find ranges containing a given cell position.
// This is the bridge between UUID-based Range storage and position-based queries.
//
// Key responsibilities:
// - Store Range objects with their resolved position bounds
// - Query ranges at a specific cell position: O(log n + k)
// - Query ranges by position AND flag filter
// - Update index when columns/rows are inserted/deleted (positions change)
//
// Architecture notes:
// - Range objects store UUIDs; this index stores position bounds
// - When column/row positions change, ranges must be re-indexed
// - The R-tree stores Range pointers; the actual Range objects are owned elsewhere
// - For sheets: Sheet owns Ranges in a map, RangeIndex indexes them
//
// Dependencies: range.h, rtree.h, types.h
// Used by: Sheet (for range queries during rendering)
//
// =============================================================================

#ifndef CELLS_RANGE_INDEX_H_
#define CELLS_RANGE_INDEX_H_

#include <cstdint>

#include <functional>
#include <unordered_map>
#include <vector>

#include "core/cells/range.h"
#include "core/cells/rtree.h"
#include "core/cells/types.h"

namespace cells {

// Position bounds for a range (resolved from UUIDs)
// Used internally by RangeIndex to track R-tree entries
struct RangePositionBounds {
    uint32_t startCol;
    uint32_t startRow;
    uint32_t endCol;
    uint32_t endRow;

    RangePositionBounds() : startCol(0), startRow(0), endCol(0), endRow(0) {}
    RangePositionBounds(uint32_t sc, uint32_t sr, uint32_t ec, uint32_t er)
        : startCol(sc), startRow(sr), endCol(ec), endRow(er) {}
};

// RangeIndex - spatial index for Range lookup
//
// Usage:
//   RangeIndex index;
//   index.insert(&myRange, 0, 0, 2, 2);  // Range at positions A1:C3
//   auto ranges = index.queryAt(1, 1);   // Find ranges containing B2
//   auto merges = index.queryAt(1, 1, RangeFlags::MERGE);  // Only merge ranges
//
class RangeIndex {
public:
    RangeIndex() = default;
    ~RangeIndex() = default;

    // Non-copyable (owns R-tree which may have complex state)
    RangeIndex(const RangeIndex&) = delete;
    RangeIndex& operator=(const RangeIndex&) = delete;

    // Movable
    RangeIndex(RangeIndex&&) noexcept = default;
    RangeIndex& operator=(RangeIndex&&) noexcept = default;

    // ========================================================================
    // Insert/Remove operations
    // ========================================================================

    // Insert a range with its resolved position bounds
    // The Range pointer must remain valid while in the index
    void insert(Range* range, uint32_t startCol, uint32_t startRow, uint32_t endCol,
                uint32_t endRow);

    // Remove a range from the index
    // Returns true if the range was found and removed
    bool remove(Range* range);

    // Remove a range by ID
    // Returns true if the range was found and removed
    bool removeById(const ID& rangeId);

    // Update the position bounds of an existing range
    // Returns true if the range was found and updated
    bool updateBounds(Range* range, uint32_t newStartCol, uint32_t newStartRow, uint32_t newEndCol,
                      uint32_t newEndRow);

    // ========================================================================
    // Query operations
    // ========================================================================

    // Find all ranges containing the cell at (col, row)
    // Returns pointers to ranges (owned elsewhere)
    [[nodiscard]] std::vector<Range*> queryAt(uint32_t col, uint32_t row) const;

    // Find all ranges containing the cell at (col, row) with specific flag(s)
    // flagMask: bitmask of flags to filter by (range must have at least one)
    [[nodiscard]] std::vector<Range*> queryAt(uint32_t col, uint32_t row, RangeFlags flagMask) const;

    // Find all ranges intersecting the given rectangle
    [[nodiscard]] std::vector<Range*> queryRange(uint32_t startCol, uint32_t startRow,
                                                 uint32_t endCol, uint32_t endRow) const;

    // Find all ranges intersecting the given rectangle with specific flag(s)
    [[nodiscard]] std::vector<Range*> queryRange(uint32_t startCol, uint32_t startRow,
                                                 uint32_t endCol, uint32_t endRow,
                                                 RangeFlags flagMask) const;

    // Check if any range contains the cell at (col, row)
    [[nodiscard]] bool hasRangeAt(uint32_t col, uint32_t row) const;

    // Check if any range with specific flag(s) contains the cell at (col, row)
    [[nodiscard]] bool hasRangeAt(uint32_t col, uint32_t row, RangeFlags flagMask) const;

    // Get the position bounds of a range (returns nullptr if not indexed)
    [[nodiscard]] const RangePositionBounds* getBounds(const ID& rangeId) const;

    // ========================================================================
    // Utility operations
    // ========================================================================

    // Get the number of ranges in the index
    [[nodiscard]] size_t size() const { return _rtree.size(); }

    // Check if empty
    [[nodiscard]] bool empty() const { return _rtree.empty(); }

    // Clear all ranges from the index
    void clear();

    // Iterate over all indexed ranges
    void forEach(const std::function<void(Range*, const RangePositionBounds&)>& callback) const;

private:
    // R-tree for spatial queries (stores Range pointers)
    RTree<Range*> _rtree;

    // Track position bounds for each range (needed for removal/update)
    // Maps range ID -> position bounds
    std::unordered_map<ID, RangePositionBounds, IDHash> _bounds;

    // Helper to create BoundingRect from position bounds
    static BoundingRect makeBounds(uint32_t startCol, uint32_t startRow, uint32_t endCol,
                                   uint32_t endRow);
};

}  // namespace cells

#endif  // CELLS_RANGE_INDEX_H_
