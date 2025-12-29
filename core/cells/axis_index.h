#ifndef CELLS_AXIS_INDEX_H_
#define CELLS_AXIS_INDEX_H_

#include <cstdint>

#include <optional>
#include <utility>

#include "core/cells/ostree.h"
#include "core/cells/types.h"

namespace cells {

// Result of pixelToAxis lookup
struct AxisLookupResult {
    ID axisId;              // The axis (column/row) containing the pixel offset
    uint32_t offsetInAxis;  // Offset from the start of this axis (in pixels)
    size_t position;        // Position index (0-based) of this axis

    AxisLookupResult() : offsetInAxis(0), position(0) {}
    AxisLookupResult(const ID& id, uint32_t offset, size_t pos)
        : axisId(id), offsetInAxis(offset), position(pos) {}
};

// AxisIndex - manages a sequence of columns or rows with pixel-based indexing
//
// Wraps an Order-Statistic Tree to provide O(log n) operations for:
// - Converting pixel offsets to axis IDs (and vice versa)
// - Inserting/removing axes at specific positions
// - Resizing axes (changing their pixel width/height)
// - Reordering axes (moving to new positions)
//
// Usage:
//   AxisIndex columns;
//   columns.insert(colId1, 0, 100);  // Insert 100px wide column at position 0
//   columns.insert(colId2, 1, 150);  // Insert 150px wide column at position 1
//
//   auto result = columns.pixelToAxis(120);
//   // result.axisId == colId2, result.offsetInAxis == 20, result.position == 1
//
//   uint32_t offset = columns.axisToPixel(colId2);
//   // offset == 100 (start of second column)
//
class AxisIndex {
public:
    AxisIndex() = default;
    ~AxisIndex() = default;

    // Non-copyable
    AxisIndex(const AxisIndex&) = delete;
    AxisIndex& operator=(const AxisIndex&) = delete;

    // Movable
    AxisIndex(AxisIndex&&) noexcept = default;
    AxisIndex& operator=(AxisIndex&&) noexcept = default;

    // ========================================================================
    // Insert/Remove operations
    // ========================================================================

    // Insert a new axis at a specific position
    // Axes at position >= pos are shifted right
    // Returns true if successful, false if position is invalid (> count)
    bool insert(const ID& axisId, size_t position, uint32_t size);

    // Append a new axis at the end
    // Returns true (always succeeds)
    bool append(const ID& axisId, uint32_t size);

    // Remove an axis by ID
    // Returns true if the axis was found and removed
    bool remove(const ID& axisId);

    // ========================================================================
    // Lookup operations
    // ========================================================================

    // Find which axis contains a given pixel offset
    // Returns nullopt if offset is beyond total size or tree is empty
    [[nodiscard]] std::optional<AxisLookupResult> pixelToAxis(uint32_t pixelOffset) const;

    // Get the pixel offset of an axis's start
    // Returns nullopt if axis is not found
    [[nodiscard]] std::optional<uint32_t> axisToPixel(const ID& axisId) const;

    // Get the position (0-indexed) of an axis
    // Returns nullopt if axis is not found
    [[nodiscard]] std::optional<size_t> getPosition(const ID& axisId) const;

    // Get the size (width/height) of an axis
    // Returns nullopt if axis is not found
    [[nodiscard]] std::optional<uint32_t> getSize(const ID& axisId) const;

    // Get axis ID at a specific position
    // Returns nullopt if position is out of range
    [[nodiscard]] std::optional<ID> getAxisAt(size_t position) const;

    // Check if an axis exists
    [[nodiscard]] bool contains(const ID& axisId) const;

    // ========================================================================
    // Modification operations
    // ========================================================================

    // Resize an axis (change its pixel width/height)
    // Returns true if the axis was found and resized
    bool resize(const ID& axisId, uint32_t newSize);

    // Move an axis to a new position
    // Returns true if successful, false if axis not found or position invalid
    bool move(const ID& axisId, size_t newPosition);

    // ========================================================================
    // Utility operations
    // ========================================================================

    // Get total size (sum of all axis sizes in pixels)
    [[nodiscard]] uint32_t totalSize() const;

    // Get number of axes
    [[nodiscard]] size_t count() const;

    // Check if empty
    [[nodiscard]] bool empty() const;

    // Clear all axes
    void clear();

    // Iterate over all axes in order
    // Callback receives (axisId, position, size, pixelOffset)
    void forEach(const std::function<void(const ID&, size_t, uint32_t, uint32_t)>& callback) const;

    // Debug: verify internal invariants
    [[nodiscard]] bool verify() const;

private:
    OSTree _tree;
};

}  // namespace cells

#endif  // CELLS_AXIS_INDEX_H_
