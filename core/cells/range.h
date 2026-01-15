// =============================================================================
// Range System - Unified Range Primitive
// =============================================================================
//
// A unified Range primitive for all range-based operations in the spreadsheet.
// Ranges can serve multiple purposes (merged cells, styles, conditional formats,
// etc.) determined by their flags bitmask.
//
// Key design decisions:
// - Ranges use column/row UUIDs for corners, not positions or cell UUIDs
// - This allows ranges to automatically expand when columns/rows are inserted
// - A single range can serve multiple purposes via the flags bitmask
// - Metadata (styles, validation rules, etc.) stored separately keyed by range ID
//
// Dependencies: types.h
// Used by: model.h, crdt.cc, viewport rendering
//
// =============================================================================

#ifndef CELLS_RANGE_H_
#define CELLS_RANGE_H_

#include <cstdint>

#include <vector>

#include "core/cells/types.h"

namespace cells {

// =============================================================================
// Range Flags - Bitmask for Range Purposes
// =============================================================================
//
// A range can serve multiple purposes simultaneously. For example, merged cells
// with a red background would have flags = RANGE_MERGE | RANGE_STYLE.
//
// This allows efficient storage: the Range struct stays small (just coordinates
// + flags), while metadata is stored separately in hash maps keyed by range ID.
//

enum class RangeFlags : uint8_t {
    NONE = 0,
    MERGE = 1 << 0,               // Cells are merged (anchor at top-left)
    STYLE = 1 << 1,               // Has style metadata (background, etc.)
    CONDITIONAL_FORMAT = 1 << 2,  // Has conditional format rules
    DATA_VALIDATION = 1 << 3,     // Has data validation rules
    NAMED = 1 << 4,               // Is a named range
    PRINT_AREA = 1 << 5,          // Defines print area
    FILTER = 1 << 6,              // Has auto-filter
    // 1 bit reserved for future use
};

// Bitwise operators for RangeFlags
inline RangeFlags operator|(RangeFlags a, RangeFlags b) {
    return static_cast<RangeFlags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

inline RangeFlags operator&(RangeFlags a, RangeFlags b) {
    return static_cast<RangeFlags>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}

inline RangeFlags operator~(RangeFlags a) {
    return static_cast<RangeFlags>(~static_cast<uint8_t>(a));
}

inline RangeFlags& operator|=(RangeFlags& a, RangeFlags b) {
    a = a | b;
    return a;
}

inline RangeFlags& operator&=(RangeFlags& a, RangeFlags b) {
    a = a & b;
    return a;
}

// Check if a flag is set
inline bool hasFlag(RangeFlags flags, RangeFlags flag) {
    return (static_cast<uint8_t>(flags) & static_cast<uint8_t>(flag)) != 0;
}

// =============================================================================
// Range - Core Range Structure
// =============================================================================
//
// A Range defines a rectangular region using column/row UUID corners.
// Using UUIDs instead of positions means:
// - Inserting a column between start and end automatically expands the range
// - Moving cells out of a range cleanly removes them (no holes to track)
// - Deleting a corner column/row shrinks the range
//
// The Range struct is kept compact (~81 bytes: 5 UUIDs + 1 byte flags).
// Metadata is stored separately in hash maps keyed by range.id.
//

struct Range {
    ID id;                               // Range's own UUID (for CRDT operations)
    ID startColId;                       // Column UUID for left edge
    ID startRowId;                       // Row UUID for top edge
    ID endColId;                         // Column UUID for right edge
    ID endRowId;                         // Row UUID for bottom edge
    RangeFlags flags{RangeFlags::NONE};  // Bitmask of purposes

    Range() = default;

    // Create a range with all IDs
    Range(const ID& rangeId, const ID& startCol, const ID& startRow, const ID& endCol,
          const ID& endRow, RangeFlags f = RangeFlags::NONE)
        : id(rangeId),
          startColId(startCol),
          startRowId(startRow),
          endColId(endCol),
          endRowId(endRow),
          flags(f) {}

    // Create a single-cell range (start == end)
    Range(const ID& rangeId, const ID& colId, const ID& rowId, RangeFlags f = RangeFlags::NONE)
        : id(rangeId),
          startColId(colId),
          startRowId(rowId),
          endColId(colId),
          endRowId(rowId),
          flags(f) {}

    // Check if this range has a specific flag
    [[nodiscard]] bool hasFlag(RangeFlags flag) const { return cells::hasFlag(flags, flag); }

    // Check if range is valid (has non-null ID and corners)
    [[nodiscard]] bool isValid() const {
        return !id.isNull() && !startColId.isNull() && !startRowId.isNull() && !endColId.isNull() &&
               !endRowId.isNull();
    }

    // Check if this is a single-cell range
    [[nodiscard]] bool isSingleCell() const {
        return startColId == endColId && startRowId == endRowId;
    }

    // Check if this is a merge range
    [[nodiscard]] bool isMerge() const { return hasFlag(RangeFlags::MERGE); }

    // Check if this has style metadata
    [[nodiscard]] bool hasStyle() const { return hasFlag(RangeFlags::STYLE); }

    // Equality comparison (by ID only - ranges with same ID are the same range)
    bool operator==(const Range& other) const { return id == other.id; }
    bool operator!=(const Range& other) const { return !(*this == other); }
};

// =============================================================================
// Range Containment Check
// =============================================================================
//
// Check if a cell position is contained within a range.
// This requires knowing the positions of the range corners and the cell.
// Since Range stores UUIDs, the caller must provide the resolved positions.
//
// The positions are column/row indices (0-based), not pixel coordinates.
//

// Check if a cell at (cellCol, cellRow) is contained within the range bounds
// defined by [startCol, endCol] x [startRow, endRow].
//
// All parameters are 0-based position indices.
// The range is inclusive on both ends: a cell at startCol or endCol is inside.
//
// Returns true if startCol <= cellCol <= endCol AND startRow <= cellRow <= endRow
inline bool rangeContainsPosition(uint32_t startCol, uint32_t startRow, uint32_t endCol,
                                  uint32_t endRow, uint32_t cellCol, uint32_t cellRow) {
    // Handle case where start > end (shouldn't happen with valid ranges, but be safe)
    const uint32_t minCol = startCol <= endCol ? startCol : endCol;
    const uint32_t maxCol = startCol <= endCol ? endCol : startCol;
    const uint32_t minRow = startRow <= endRow ? startRow : endRow;
    const uint32_t maxRow = startRow <= endRow ? endRow : startRow;

    return cellCol >= minCol && cellCol <= maxCol && cellRow >= minRow && cellRow <= maxRow;
}

// Check if a cell at (cellCol, cellRow) is the anchor (top-left) of the range
// The anchor is the cell at (min(startCol, endCol), min(startRow, endRow))
inline bool rangeIsAnchorPosition(uint32_t startCol, uint32_t startRow, uint32_t endCol,
                                  uint32_t endRow, uint32_t cellCol, uint32_t cellRow) {
    const uint32_t anchorCol = startCol <= endCol ? startCol : endCol;
    const uint32_t anchorRow = startRow <= endRow ? startRow : endRow;
    return cellCol == anchorCol && cellRow == anchorRow;
}

// Check if two ranges overlap (share at least one cell)
// Both ranges defined by their corner positions
inline bool rangesOverlap(uint32_t r1StartCol, uint32_t r1StartRow, uint32_t r1EndCol,
                          uint32_t r1EndRow, uint32_t r2StartCol, uint32_t r2StartRow,
                          uint32_t r2EndCol, uint32_t r2EndRow) {
    // Normalize ranges (ensure start <= end)
    const uint32_t r1MinCol = r1StartCol <= r1EndCol ? r1StartCol : r1EndCol;
    const uint32_t r1MaxCol = r1StartCol <= r1EndCol ? r1EndCol : r1StartCol;
    const uint32_t r1MinRow = r1StartRow <= r1EndRow ? r1StartRow : r1EndRow;
    const uint32_t r1MaxRow = r1StartRow <= r1EndRow ? r1EndRow : r1StartRow;

    const uint32_t r2MinCol = r2StartCol <= r2EndCol ? r2StartCol : r2EndCol;
    const uint32_t r2MaxCol = r2StartCol <= r2EndCol ? r2EndCol : r2StartCol;
    const uint32_t r2MinRow = r2StartRow <= r2EndRow ? r2StartRow : r2EndRow;
    const uint32_t r2MaxRow = r2StartRow <= r2EndRow ? r2EndRow : r2StartRow;

    // Check for overlap: ranges overlap if they intersect in both dimensions
    return r1MinCol <= r2MaxCol && r1MaxCol >= r2MinCol && r1MinRow <= r2MaxRow &&
           r1MaxRow >= r2MinRow;
}

// =============================================================================
// Range Corner Deletion (Shrinking)
// =============================================================================
//
// When a column or row that forms a range corner is deleted, the range shrinks.
// If both corners in a dimension are deleted, the range becomes invalid.
//
// Behavior:
// - Delete startCol (not equal to endCol) → startCol becomes the next column
// - Delete endCol (not equal to startCol) → endCol becomes the previous column
// - Delete startCol == endCol → range invalid (zero-width)
// - Same logic applies to rows
//
// These functions compute the new corner IDs after a deletion.
// The caller must provide functions to find adjacent columns/rows.
//

// Result of a corner deletion operation
enum class CornerDeleteResult : uint8_t {
    UNCHANGED,    // Deleted ID was not a corner of the range
    SHRUNK,       // Range shrunk - new corner ID is set
    INVALIDATED,  // Range became invalid (zero-width or zero-height)
};

// Adjust a range when a column is deleted
// deletedColId: the column being deleted
// getNextColId: function to get the column after a given column (returns null ID if none)
// getPrevColId: function to get the column before a given column (returns null ID if none)
// Returns the result status, and modifies range.startColId/endColId if shrinking
template <typename GetNextFn, typename GetPrevFn>
CornerDeleteResult adjustRangeForColumnDeletion(Range& range, const ID& deletedColId,
                                                GetNextFn getNextColId, GetPrevFn getPrevColId) {
    const bool startsHere = (range.startColId == deletedColId);
    const bool endsHere = (range.endColId == deletedColId);

    if (!startsHere && !endsHere) {
        return CornerDeleteResult::UNCHANGED;
    }

    // Single-column range (start == end == deleted) → invalidated
    if (startsHere && endsHere) {
        return CornerDeleteResult::INVALIDATED;
    }

    // Start column deleted → move start to next column
    if (startsHere) {
        const ID nextCol = getNextColId(deletedColId);
        if (nextCol.isNull()) {
            return CornerDeleteResult::INVALIDATED;  // No next column
        }
        range.startColId = nextCol;
        return CornerDeleteResult::SHRUNK;
    }

    // End column deleted → move end to previous column
    // (endsHere must be true here)
    const ID prevCol = getPrevColId(deletedColId);
    if (prevCol.isNull()) {
        return CornerDeleteResult::INVALIDATED;  // No previous column
    }
    range.endColId = prevCol;
    return CornerDeleteResult::SHRUNK;
}

// Adjust a range when a row is deleted
// deletedRowId: the row being deleted
// getNextRowId: function to get the row after a given row (returns null ID if none)
// getPrevRowId: function to get the row before a given row (returns null ID if none)
// Returns the result status, and modifies range.startRowId/endRowId if shrinking
template <typename GetNextFn, typename GetPrevFn>
CornerDeleteResult adjustRangeForRowDeletion(Range& range, const ID& deletedRowId,
                                             GetNextFn getNextRowId, GetPrevFn getPrevRowId) {
    const bool startsHere = (range.startRowId == deletedRowId);
    const bool endsHere = (range.endRowId == deletedRowId);

    if (!startsHere && !endsHere) {
        return CornerDeleteResult::UNCHANGED;
    }

    // Single-row range (start == end == deleted) → invalidated
    if (startsHere && endsHere) {
        return CornerDeleteResult::INVALIDATED;
    }

    // Start row deleted → move start to next row
    if (startsHere) {
        const ID nextRow = getNextRowId(deletedRowId);
        if (nextRow.isNull()) {
            return CornerDeleteResult::INVALIDATED;  // No next row
        }
        range.startRowId = nextRow;
        return CornerDeleteResult::SHRUNK;
    }

    // End row deleted → move end to previous row
    // (endsHere must be true here)
    const ID prevRow = getPrevRowId(deletedRowId);
    if (prevRow.isNull()) {
        return CornerDeleteResult::INVALIDATED;  // No previous row
    }
    range.endRowId = prevRow;
    return CornerDeleteResult::SHRUNK;
}

// =============================================================================
// Rectangle Subtraction for Overlapping Range Styles
// =============================================================================
//
// When applying a new range style with the same property as an existing range,
// the overlapping area must be removed from the old range. This is done by
// computing the "rectangle subtraction" A - B, which produces 0-4 non-overlapping
// rectangles that represent the parts of A not covered by B.
//
// Example: Old range A (blue bgColor) and new range B (red bgColor) overlap.
// After subtraction, A becomes 0-4 smaller rectangles with blue bgColor,
// and B covers the overlapping area with red bgColor.
//
// These functions work with position-based rectangles (uint32_t coordinates).
// The caller is responsible for converting Range UUID corners to positions
// and creating new Range objects from the result rectangles.
//

// A simple rectangle defined by position coordinates (inclusive bounds)
struct PositionRect {
    uint32_t minCol{0};
    uint32_t minRow{0};
    uint32_t maxCol{0};
    uint32_t maxRow{0};

    // Check if this rectangle is valid (non-empty)
    [[nodiscard]] bool isValid() const { return minCol <= maxCol && minRow <= maxRow; }

    // Check if this rectangle overlaps with another
    [[nodiscard]] bool overlaps(const PositionRect& other) const {
        return minCol <= other.maxCol && maxCol >= other.minCol && minRow <= other.maxRow &&
               maxRow >= other.minRow;
    }

    // Check if this rectangle fully contains another
    [[nodiscard]] bool contains(const PositionRect& other) const {
        return minCol <= other.minCol && maxCol >= other.maxCol && minRow <= other.minRow &&
               maxRow >= other.maxRow;
    }

    // Equality comparison
    bool operator==(const PositionRect& other) const {
        return minCol == other.minCol && maxCol == other.maxCol && minRow == other.minRow &&
               maxRow == other.maxRow;
    }
    bool operator!=(const PositionRect& other) const { return !(*this == other); }
};

// Compute the rectangle subtraction A - B.
// Returns a vector of 0-4 rectangles representing the parts of A not covered by B.
//
// If A and B don't overlap, returns a single rectangle equal to A.
// If B completely covers A, returns an empty vector.
// Otherwise, returns up to 4 rectangles (left, right, top, bottom strips).
//
// The algorithm prioritizes left/right strips (full height of A where applicable),
// then top/bottom strips (clipped to not include left/right areas).
//
// Example:
//   A = (0,0)-(10,10), B = (3,3)-(7,7)
//   Result: Left (0,0)-(2,10), Right (8,0)-(10,10), Top (3,0)-(7,2), Bottom (3,8)-(7,10)
//
inline std::vector<PositionRect> subtractRectangle(const PositionRect& a, const PositionRect& b) {
    std::vector<PositionRect> result;

    // If no overlap, return A unchanged
    if (!a.overlaps(b)) {
        result.push_back(a);
        return result;
    }

    // If B completely covers A, return empty
    if (b.minCol <= a.minCol && b.maxCol >= a.maxCol && b.minRow <= a.minRow &&
        b.maxRow >= a.maxRow) {
        return result;  // Empty - A is fully covered
    }

    // Left strip: part of A to the left of B (full height of A)
    if (b.minCol > a.minCol) {
        PositionRect left;
        left.minCol = a.minCol;
        left.maxCol = b.minCol - 1;
        left.minRow = a.minRow;
        left.maxRow = a.maxRow;
        if (left.isValid()) {
            result.push_back(left);
        }
    }

    // Right strip: part of A to the right of B (full height of A)
    if (b.maxCol < a.maxCol) {
        PositionRect right;
        right.minCol = b.maxCol + 1;
        right.maxCol = a.maxCol;
        right.minRow = a.minRow;
        right.maxRow = a.maxRow;
        if (right.isValid()) {
            result.push_back(right);
        }
    }

    // Compute the horizontal overlap region (for top/bottom strips)
    const uint32_t overlapMinCol = (b.minCol > a.minCol) ? b.minCol : a.minCol;
    const uint32_t overlapMaxCol = (b.maxCol < a.maxCol) ? b.maxCol : a.maxCol;

    // Top strip: part of A above B (clipped to overlap columns to avoid including left/right)
    if (b.minRow > a.minRow) {
        PositionRect top;
        top.minCol = overlapMinCol;
        top.maxCol = overlapMaxCol;
        top.minRow = a.minRow;
        top.maxRow = b.minRow - 1;
        if (top.isValid()) {
            result.push_back(top);
        }
    }

    // Bottom strip: part of A below B (clipped to overlap columns)
    if (b.maxRow < a.maxRow) {
        PositionRect bottom;
        bottom.minCol = overlapMinCol;
        bottom.maxCol = overlapMaxCol;
        bottom.minRow = b.maxRow + 1;
        bottom.maxRow = a.maxRow;
        if (bottom.isValid()) {
            result.push_back(bottom);
        }
    }

    return result;
}

}  // namespace cells

#endif  // CELLS_RANGE_H_
