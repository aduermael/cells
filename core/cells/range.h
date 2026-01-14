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
    ID id;          // Range's own UUID (for CRDT operations)
    ID startColId;  // Column UUID for left edge
    ID startRowId;  // Row UUID for top edge
    ID endColId;    // Column UUID for right edge
    ID endRowId;    // Row UUID for bottom edge
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
        : id(rangeId), startColId(colId), startRowId(rowId), endColId(colId), endRowId(rowId), flags(f) {}

    // Check if this range has a specific flag
    [[nodiscard]] bool hasFlag(RangeFlags flag) const {
        return cells::hasFlag(flags, flag);
    }

    // Check if range is valid (has non-null ID and corners)
    [[nodiscard]] bool isValid() const {
        return !id.isNull() && !startColId.isNull() && !startRowId.isNull() &&
               !endColId.isNull() && !endRowId.isNull();
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

}  // namespace cells

#endif  // CELLS_RANGE_H_
