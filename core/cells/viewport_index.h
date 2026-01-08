// =============================================================================
// Viewport Index
// =============================================================================
//
// Spatial index for efficient viewport queries: "which cells are visible in
// this pixel rectangle?" Uses two AxisIndex instances (columns, rows) plus
// a cell hash map for O(log n + k) queries where k = visible cells.
//
// Key responsibilities:
// - Query cells within a viewport (pixel coordinates) for rendering
// - Convert pixel coordinates to column/row IDs and vice versa
// - Support incremental O(log n) updates for cell and axis changes
// - Maintain cell index for O(1) cell lookups by (colId, rowId)
//
// Unlike a quadtree which requires O(n) rebuilds, ViewportIndex supports:
// - Cell additions/removals: O(1)
// - Axis insertions/deletions: O(log n)
// - Axis resizing: O(log n)
// - Axis reordering: O(log n)
//
// Query algorithm:
// 1. Use AxisIndex to find visible column/row ranges
// 2. Iterate visible ranges, look up cells in hash map
// 3. Return ViewportEntry list with cell pointers and bounding boxes
//
// Dependencies: axis_index.h, model.h, types.h
// Used by: bindings.cc (WASM rendering), grid-renderer.ts (TypeScript UI)
//
// =============================================================================

#ifndef CELLS_VIEWPORT_INDEX_H_
#define CELLS_VIEWPORT_INDEX_H_

#include <cstdint>

#include <vector>

#include "core/cells/axis_index.h"
#include "core/cells/model.h"
#include "core/cells/types.h"

namespace cells {

// Forward declarations
struct Cell;
struct Sheet;

// Entry returned from viewport query
// Contains cell pointer and its bounding box in pixel coordinates
struct ViewportEntry {
    Cell* cell;
    uint32_t x;       // Left edge in pixels
    uint32_t y;       // Top edge in pixels
    uint32_t width;   // Cell width in pixels
    uint32_t height;  // Cell height in pixels

    ViewportEntry() : cell(nullptr), x(0), y(0), width(0), height(0) {}
    ViewportEntry(Cell* c, uint32_t x, uint32_t y, uint32_t w, uint32_t h)
        : cell(c), x(x), y(y), width(w), height(h) {}
};

// ViewportIndex - spatial index for cells using pixel coordinates
//
// Uses two Order-Statistic Trees (via AxisIndex) to provide O(log n) operations:
// - pixelToColumn(x): find which column contains pixel offset x
// - pixelToRow(y): find which row contains pixel offset y
// - columnToPixel(colId): get pixel offset of column's left edge
// - rowToPixel(rowId): get pixel offset of row's top edge
//
// Unlike the quadtree which requires O(n) full rebuilds, ViewportIndex supports
// incremental O(log n) updates for:
// - Cell additions/removals
// - Axis insertions/deletions
// - Axis resizing (width/height changes)
// - Axis reordering (move operations)
//
// Cells are stored by reference in a HashMap for O(1) access by (colId, rowId).
// The viewport query uses the AxisIndex to find visible columns/rows, then
// iterates the visible range to collect cells.
//
// Usage:
//   ViewportIndex index;
//   index.build(sheet);  // Populate from sheet data
//
//   // Query cells in viewport (pixel coordinates)
//   auto entries = index.queryViewport(0, 0, 800, 600);
//
//   // Incremental updates
//   index.onCellAdded(cell);
//   index.onAxisResized(colId, true, 150);  // true = column
//
class ViewportIndex {
public:
    ViewportIndex() = default;
    ~ViewportIndex() = default;

    // Non-copyable
    ViewportIndex(const ViewportIndex&) = delete;
    ViewportIndex& operator=(const ViewportIndex&) = delete;

    // Movable
    ViewportIndex(ViewportIndex&&) noexcept = default;
    ViewportIndex& operator=(ViewportIndex&&) noexcept = default;

    // ========================================================================
    // Build from sheet
    // ========================================================================

    // Populate index from sheet data
    // Clears any existing data and rebuilds from scratch
    // Time complexity: O(n log n) where n = max(columns, rows)
    void build(const Sheet& sheet);

    // Clear all data
    void clear();

    // ========================================================================
    // Viewport query
    // ========================================================================

    // Query cells within viewport (pixel coordinates)
    // Returns entries for all cells whose bounding boxes intersect the viewport
    // Time complexity: O(log n + k) where k = number of cells in viewport
    [[nodiscard]] std::vector<ViewportEntry> queryViewport(uint32_t x1, uint32_t y1, uint32_t x2,
                                                           uint32_t y2) const;

    // Get visible column range for a viewport (pixel coordinates)
    // Returns (firstColPosition, lastColPosition) - positions are 0-indexed
    // Returns (0, 0) if viewport is empty or no columns exist
    [[nodiscard]] std::pair<size_t, size_t> getVisibleColumnRange(uint32_t x1, uint32_t x2) const;

    // Get visible row range for a viewport (pixel coordinates)
    // Returns (firstRowPosition, lastRowPosition) - positions are 0-indexed
    // Returns (0, 0) if viewport is empty or no rows exist
    [[nodiscard]] std::pair<size_t, size_t> getVisibleRowRange(uint32_t y1, uint32_t y2) const;

    // ========================================================================
    // Coordinate conversion (pixel <-> axis)
    // ========================================================================

    // Find which column contains a pixel X offset
    // Returns (columnId, offsetWithinColumn, columnPosition)
    [[nodiscard]] std::optional<AxisLookupResult> pixelToColumn(uint32_t x) const;

    // Find which row contains a pixel Y offset
    // Returns (rowId, offsetWithinRow, rowPosition)
    [[nodiscard]] std::optional<AxisLookupResult> pixelToRow(uint32_t y) const;

    // Get pixel X offset of a column's left edge
    [[nodiscard]] std::optional<uint32_t> columnToPixel(const ID& colId) const;

    // Get pixel Y offset of a row's top edge
    [[nodiscard]] std::optional<uint32_t> rowToPixel(const ID& rowId) const;

    // Get column width
    [[nodiscard]] std::optional<uint32_t> getColumnWidth(const ID& colId) const;

    // Get row height
    [[nodiscard]] std::optional<uint32_t> getRowHeight(const ID& rowId) const;

    // Get column ID at position
    [[nodiscard]] std::optional<ID> getColumnAt(size_t position) const;

    // Get row ID at position
    [[nodiscard]] std::optional<ID> getRowAt(size_t position) const;

    // ========================================================================
    // Incremental cell updates
    // ========================================================================

    // Called when a cell is added to the sheet
    void onCellAdded(Cell* cell);

    // Called when a cell is removed from the sheet
    void onCellRemoved(Cell* cell);

    // Called when a cell is removed - use when cell pointer may be invalid
    // This overload is safe to call after the cell has been deleted
    void onCellRemoved(const ID& colId, const ID& rowId);

    // Called when a cell's value/formula changes (no spatial update needed)
    // This is a no-op since we don't index by value, but included for API completeness
    void onCellChanged(Cell* cell);

    // ========================================================================
    // Incremental axis updates
    // ========================================================================

    // Called when a new axis (column/row) is inserted
    // isColumn: true = column, false = row
    void onAxisInserted(const ID& axisId, bool isColumn, size_t position, uint32_t size);

    // Called when an axis is deleted
    void onAxisDeleted(const ID& axisId, bool isColumn);

    // Called when an axis is resized
    void onAxisResized(const ID& axisId, bool isColumn, uint32_t newSize);

    // Called when an axis is moved to a new position
    void onAxisMoved(const ID& axisId, bool isColumn, size_t newPosition);

    // ========================================================================
    // Utility
    // ========================================================================

    // Get total width (sum of all column widths)
    [[nodiscard]] uint32_t totalWidth() const;

    // Get total height (sum of all row heights)
    [[nodiscard]] uint32_t totalHeight() const;

    // Get number of columns
    [[nodiscard]] size_t columnCount() const;

    // Get number of rows
    [[nodiscard]] size_t rowCount() const;

    // Get number of indexed cells
    [[nodiscard]] size_t cellCount() const;

    // Check if empty
    [[nodiscard]] bool empty() const;

    // Debug: verify internal invariants
    [[nodiscard]] bool verify() const;

private:
    // Column and row indices
    AxisIndex _columns;
    AxisIndex _rows;

    // Cell storage: (colId, rowId) -> Cell*
    // Composite key is colId.toString() + rowId.toString()
    std::unordered_map<std::string, Cell*> _cells;

    // Pointer to the sheet (for cell lookups during queries)
    const Sheet* _sheet{nullptr};

    // Helper: make composite key for cell lookup
    [[nodiscard]] static std::string makeCellKey(const ID& colId, const ID& rowId);
};

}  // namespace cells

#endif  // CELLS_VIEWPORT_INDEX_H_
