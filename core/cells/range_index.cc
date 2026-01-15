#include "core/cells/range_index.h"

#include <algorithm>

namespace cells {

BoundingRect RangeIndex::makeBounds(uint32_t startCol, uint32_t startRow, uint32_t endCol,
                                    uint32_t endRow) {
    // Normalize (ensure min <= max)
    const auto minCol = static_cast<int32_t>(std::min(startCol, endCol));
    const auto maxCol = static_cast<int32_t>(std::max(startCol, endCol));
    const auto minRow = static_cast<int32_t>(std::min(startRow, endRow));
    const auto maxRow = static_cast<int32_t>(std::max(startRow, endRow));
    return {minCol, minRow, maxCol, maxRow};
}

void RangeIndex::insertIntoFlagTrees(Range* range, const BoundingRect& rect) {
    if (hasFlag(range->flags, RangeFlags::MERGE)) {
        _mergeTrees.insert(rect, range);
    }
    if (hasFlag(range->flags, RangeFlags::STYLE)) {
        _styleTrees.insert(rect, range);
    }
}

void RangeIndex::removeFromFlagTrees(Range* range, const BoundingRect& rect) {
    if (hasFlag(range->flags, RangeFlags::MERGE)) {
        _mergeTrees.remove(rect, range);
    }
    if (hasFlag(range->flags, RangeFlags::STYLE)) {
        _styleTrees.remove(rect, range);
    }
}

void RangeIndex::insert(Range* range, uint32_t startCol, uint32_t startRow, uint32_t endCol,
                        uint32_t endRow) {
    if (range == nullptr) {
        return;
    }

    // Remove existing entry if already indexed
    if (_bounds.find(range->id) != _bounds.end()) {
        remove(range);
    }

    // Store bounds for later removal/update
    _bounds[range->id] = RangePositionBounds(startCol, startRow, endCol, endRow);

    // Insert into R-tree
    const BoundingRect rect = makeBounds(startCol, startRow, endCol, endRow);
    _rtree.insert(rect, range);

    // Also insert into flag-specific trees
    insertIntoFlagTrees(range, rect);
}

bool RangeIndex::remove(Range* range) {
    if (range == nullptr) {
        return false;
    }
    return removeById(range->id);
}

bool RangeIndex::removeById(const ID& rangeId) {
    auto it = _bounds.find(rangeId);
    if (it == _bounds.end()) {
        return false;  // Not indexed
    }

    // Find and remove from R-tree (need to find the range pointer)
    bool found = false;
    Range* foundRange = nullptr;
    BoundingRect foundBounds;
    _rtree.forEach([&](const BoundingRect& entryBounds, Range* entryRange) {
        if (!found && entryRange != nullptr && entryRange->id == rangeId) {
            // Found it - save for removal
            foundRange = entryRange;
            foundBounds = entryBounds;
            found = true;
        }
    });

    if (found && foundRange != nullptr) {
        // Remove from main R-tree
        _rtree.remove(foundBounds, foundRange);
        // Remove from flag-specific trees
        removeFromFlagTrees(foundRange, foundBounds);
        _bounds.erase(it);
    }

    return found;
}

bool RangeIndex::updateBounds(Range* range, uint32_t newStartCol, uint32_t newStartRow,
                              uint32_t newEndCol, uint32_t newEndRow) {
    if (range == nullptr) {
        return false;
    }

    auto it = _bounds.find(range->id);
    if (it == _bounds.end()) {
        return false;  // Not indexed
    }

    // Remove old entry from all trees
    const auto& oldBounds = it->second;
    const BoundingRect oldRect =
        makeBounds(oldBounds.startCol, oldBounds.startRow, oldBounds.endCol, oldBounds.endRow);
    _rtree.remove(oldRect, range);
    removeFromFlagTrees(range, oldRect);

    // Update stored bounds
    it->second = RangePositionBounds(newStartCol, newStartRow, newEndCol, newEndRow);

    // Insert with new bounds into all trees
    const BoundingRect newRect = makeBounds(newStartCol, newStartRow, newEndCol, newEndRow);
    _rtree.insert(newRect, range);
    insertIntoFlagTrees(range, newRect);

    return true;
}

std::vector<Range*> RangeIndex::queryAt(uint32_t col, uint32_t row) const {
    return _rtree.query(static_cast<int32_t>(col), static_cast<int32_t>(row));
}

std::vector<Range*> RangeIndex::queryAt(uint32_t col, uint32_t row, RangeFlags flagMask) const {
    // Use flag-specific trees for common single-flag queries (fast path)
    // This avoids iterating through all ranges at the position
    if (flagMask == RangeFlags::MERGE) {
        return _mergeTrees.query(static_cast<int32_t>(col), static_cast<int32_t>(row));
    }
    if (flagMask == RangeFlags::STYLE) {
        return _styleTrees.query(static_cast<int32_t>(col), static_cast<int32_t>(row));
    }

    // Fall back to filtering for other/combined flags
    auto all = queryAt(col, row);

    // Filter by flags
    std::vector<Range*> result;
    result.reserve(all.size());
    for (const Range* r : all) {
        if (r != nullptr && hasFlag(r->flags, flagMask)) {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast) - API returns non-const
            result.push_back(const_cast<Range*>(r));
        }
    }
    return result;
}

std::vector<Range*> RangeIndex::queryRange(uint32_t startCol, uint32_t startRow, uint32_t endCol,
                                           uint32_t endRow) const {
    const BoundingRect rect = makeBounds(startCol, startRow, endCol, endRow);
    return _rtree.queryRange(rect);
}

std::vector<Range*> RangeIndex::queryRange(uint32_t startCol, uint32_t startRow, uint32_t endCol,
                                           uint32_t endRow, RangeFlags flagMask) const {
    const BoundingRect rect = makeBounds(startCol, startRow, endCol, endRow);

    // Use flag-specific trees for common single-flag queries (fast path)
    if (flagMask == RangeFlags::MERGE) {
        return _mergeTrees.queryRange(rect);
    }
    if (flagMask == RangeFlags::STYLE) {
        return _styleTrees.queryRange(rect);
    }

    // Fall back to filtering for other/combined flags
    auto all = _rtree.queryRange(rect);

    // Filter by flags
    std::vector<Range*> result;
    result.reserve(all.size());
    for (const Range* r : all) {
        if (r != nullptr && hasFlag(r->flags, flagMask)) {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast) - API returns non-const
            result.push_back(const_cast<Range*>(r));
        }
    }
    return result;
}

bool RangeIndex::hasRangeAt(uint32_t col, uint32_t row) const {
    return !queryAt(col, row).empty();
}

bool RangeIndex::hasRangeAt(uint32_t col, uint32_t row, RangeFlags flagMask) const {
    return !queryAt(col, row, flagMask).empty();
}

const RangePositionBounds* RangeIndex::getBounds(const ID& rangeId) const {
    auto it = _bounds.find(rangeId);
    if (it == _bounds.end()) {
        return nullptr;
    }
    return &it->second;
}

void RangeIndex::clear() {
    _rtree.clear();
    _mergeTrees.clear();
    _styleTrees.clear();
    _bounds.clear();
}

void RangeIndex::forEach(
    const std::function<void(Range*, const RangePositionBounds&)>& callback) const {
    _rtree.forEach([&](const BoundingRect& /*bounds*/, Range* range) {
        if (range != nullptr) {
            auto it = _bounds.find(range->id);
            if (it != _bounds.end()) {
                callback(range, it->second);
            }
        }
    });
}

}  // namespace cells
