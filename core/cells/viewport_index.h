// =============================================================================
// Viewport Index
// =============================================================================
//
// Spatial index for efficient viewport queries: "which cells are visible in
// this pixel rectangle?" Delegates coordinate conversion to Sheet's AxisIndex
// and maintains a cell hash map for O(log n + k) queries where k = visible cells.
//
// Key responsibilities:
// - Query cells within a viewport (pixel coordinates) for rendering
// - Convert pixel coordinates to column/row IDs and vice versa
// - Maintain cell index for O(1) cell lookups by (colId, rowId)
//
// Architecture:
// - Sheet owns AxisIndex for columns and rows (maintained incrementally)
// - ViewportIndex stores cell HashMap and delegates axis queries to Sheet
// - On sheet switch: only rebuild cell HashMap (O(k) where k = cells)
// - Axis operations are O(log n) via Sheet's AxisIndex
//
// Query algorithm:
// 1. Use Sheet's AxisIndex to find visible column/row ranges
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
// Delegates to Sheet's AxisIndex for coordinate conversions (O(log n)):
// - pixelToColumn(x): find which column contains pixel offset x
// - pixelToRow(y): find which row contains pixel offset y
// - columnToPixel(colId): get pixel offset of column's left edge
// - rowToPixel(rowId): get pixel offset of row's top edge
//
// Maintains cell HashMap for O(1) access by (colId, rowId).
// Sheet switch only rebuilds cell HashMap (O(k) where k = cells),
// not axis indexes (they persist in Sheet).
//
// Usage:
//   ViewportIndex index;
//   index.build(sheet);  // Build cell HashMap only
//
//   // Query cells in viewport (pixel coordinates)
//   auto entries = index.queryViewport(0, 0, 800, 600);
//
//   // Incremental updates
//   index.onCellAdded(cell);
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
    // Only builds the cell HashMap - axis indexes are owned by Sheet
    // Time complexity: O(k) where k = number of cells
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
    // Delegates to Sheet's AxisIndex for O(log n) lookups
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
    // Axis update notifications (for API compatibility)
    // These are now no-ops since Sheet maintains the AxisIndex
    // ========================================================================

    // Called when a new axis (column/row) is inserted
    // No-op: Sheet maintains the AxisIndex
    void onAxisInserted(const ID& axisId, bool isColumn, size_t position, uint32_t size);

    // Called when an axis is deleted
    // Removes cells in the deleted axis from the cell HashMap
    void onAxisDeleted(const ID& axisId, bool isColumn);

    // Called when an axis is resized
    // No-op: Sheet maintains the AxisIndex
    void onAxisResized(const ID& axisId, bool isColumn, uint32_t newSize);

    // Called when an axis is moved to a new position
    // No-op: Sheet maintains the AxisIndex
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
    // Cell storage: (colId, rowId) -> Cell*
    // Composite key is colId.toString() + rowId.toString()
    std::unordered_map<std::string, Cell*> _cells;

    // Pointer to the sheet (for axis lookups during queries)
    const Sheet* _sheet{nullptr};

    // Helper: make composite key for cell lookup
    [[nodiscard]] static std::string makeCellKey(const ID& colId, const ID& rowId);
};

}  // namespace cells

#endif  // CELLS_VIEWPORT_INDEX_H_
