#include "core/cells/spill_index.h"

#include <algorithm>

namespace cells {

BoundingRect SpillIndex::makeBounds(uint32_t startCol, uint32_t startRow, uint32_t endCol,
                                    uint32_t endRow) {
    // Normalize (ensure min <= max)
    const auto minCol = static_cast<int32_t>(std::min(startCol, endCol));
    const auto maxCol = static_cast<int32_t>(std::max(startCol, endCol));
    const auto minRow = static_cast<int32_t>(std::min(startRow, endRow));
    const auto maxRow = static_cast<int32_t>(std::max(startRow, endRow));
    return {minCol, minRow, maxCol, maxRow};
}

void SpillIndex::insert(const ID& masterCellId, uint32_t startCol, uint32_t startRow,
                        uint32_t endCol, uint32_t endRow) {
    // Remove existing entry if already indexed
    if (_bounds.find(masterCellId) != _bounds.end()) {
        remove(masterCellId);
    }

    // Store bounds for later removal/update
    _bounds[masterCellId] = SpillPositionBounds(startCol, startRow, endCol, endRow);

    // Insert into R-tree
    const BoundingRect rect = makeBounds(startCol, startRow, endCol, endRow);
    _rtree.insert(rect, masterCellId);
}

bool SpillIndex::remove(const ID& masterCellId) {
    auto it = _bounds.find(masterCellId);
    if (it == _bounds.end()) {
        return false;  // Not indexed
    }

    // Get the bounds for removal
    const auto& bounds = it->second;
    const BoundingRect rect =
        makeBounds(bounds.startCol, bounds.startRow, bounds.endCol, bounds.endRow);

    // Remove from R-tree
    const bool removed = _rtree.remove(rect, masterCellId);

    // Remove from bounds map
    _bounds.erase(it);

    return removed;
}

bool SpillIndex::updateBounds(const ID& masterCellId, uint32_t newStartCol, uint32_t newStartRow,
                              uint32_t newEndCol, uint32_t newEndRow) {
    auto it = _bounds.find(masterCellId);
    if (it == _bounds.end()) {
        return false;  // Not indexed
    }

    // Remove old entry
    const auto& oldBounds = it->second;
    const BoundingRect oldRect =
        makeBounds(oldBounds.startCol, oldBounds.startRow, oldBounds.endCol, oldBounds.endRow);
    _rtree.remove(oldRect, masterCellId);

    // Update stored bounds
    it->second = SpillPositionBounds(newStartCol, newStartRow, newEndCol, newEndRow);

    // Insert with new bounds
    const BoundingRect newRect = makeBounds(newStartCol, newStartRow, newEndCol, newEndRow);
    _rtree.insert(newRect, masterCellId);

    return true;
}

std::vector<ID> SpillIndex::queryAt(uint32_t col, uint32_t row) const {
    return _rtree.query(static_cast<int32_t>(col), static_cast<int32_t>(row));
}

std::vector<ID> SpillIndex::queryRange(uint32_t startCol, uint32_t startRow, uint32_t endCol,
                                       uint32_t endRow) const {
    const BoundingRect rect = makeBounds(startCol, startRow, endCol, endRow);
    return _rtree.queryRange(rect);
}

bool SpillIndex::hasSpillAt(uint32_t col, uint32_t row) const {
    return !queryAt(col, row).empty();
}

const SpillPositionBounds* SpillIndex::getBounds(const ID& masterCellId) const {
    auto it = _bounds.find(masterCellId);
    if (it == _bounds.end()) {
        return nullptr;
    }
    return &it->second;
}

void SpillIndex::clear() {
    _rtree.clear();
    _bounds.clear();
}

void SpillIndex::forEach(
    const std::function<void(const ID&, const SpillPositionBounds&)>& callback) const {
    _rtree.forEach([&](const BoundingRect& /*bounds*/, const ID& masterId) {
        auto it = _bounds.find(masterId);
        if (it != _bounds.end()) {
            callback(masterId, it->second);
        }
    });
}

}  // namespace cells
