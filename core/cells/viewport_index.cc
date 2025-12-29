#include "core/cells/viewport_index.h"

#include <algorithm>
#include <utility>

namespace cells {

// Helper struct for sorting axes by position (avoids pointer comparison)
struct AxisSortEntry {
    ID id;
    uint32_t position;
    uint32_t size;
};

// ============================================================================
// Build from sheet
// ============================================================================

void ViewportIndex::build(const Sheet& sheet) {
    clear();
    _sheet = &sheet;

    // Collect columns and sort by position
    std::vector<AxisSortEntry> columns;
    columns.reserve(sheet.columns.size());
    for (const auto& [id, col] : sheet.columns) {
        columns.push_back({col->id, col->position, col->size});
    }
    std::sort(columns.begin(), columns.end(), [](const AxisSortEntry& a, const AxisSortEntry& b) {
        return a.position < b.position;
    });

    // Insert columns in order
    for (const auto& col : columns) {
        _columns.append(col.id, col.size);
    }

    // Collect rows and sort by position
    std::vector<AxisSortEntry> rows;
    rows.reserve(sheet.rows.size());
    for (const auto& [id, row] : sheet.rows) {
        rows.push_back({row->id, row->position, row->size});
    }
    std::sort(rows.begin(), rows.end(), [](const AxisSortEntry& a, const AxisSortEntry& b) {
        return a.position < b.position;
    });

    // Insert rows in order
    for (const auto& row : rows) {
        _rows.append(row.id, row.size);
    }

    // Index all cells
    for (const auto& [id, cell] : sheet.cells) {
        const std::string key = makeCellKey(cell->colId, cell->rowId);
        _cells[key] = cell.get();
    }
}

void ViewportIndex::clear() {
    _columns.clear();
    _rows.clear();
    _cells.clear();
    _sheet = nullptr;
}

// ============================================================================
// Viewport query
// ============================================================================

std::vector<ViewportEntry> ViewportIndex::queryViewport(uint32_t x1, uint32_t y1, uint32_t x2,
                                                        uint32_t y2) const {
    std::vector<ViewportEntry> results;

    if (_columns.empty() || _rows.empty() || x1 >= x2 || y1 >= y2) {
        return results;
    }

    // Find visible column range
    const auto [firstCol, lastCol] = getVisibleColumnRange(x1, x2);

    // Find visible row range
    const auto [firstRow, lastRow] = getVisibleRowRange(y1, y2);

    // Iterate over visible cells
    // For each visible column and row, check if a cell exists at that position
    for (size_t colPos = firstCol; colPos <= lastCol; ++colPos) {
        auto colIdOpt = _columns.getAxisAt(colPos);
        if (!colIdOpt) {
            continue;
        }
        const ID& colId = *colIdOpt;

        auto colPixelOpt = _columns.axisToPixel(colId);
        auto colWidthOpt = _columns.getSize(colId);
        if (!colPixelOpt || !colWidthOpt) {
            continue;
        }
        const uint32_t colX = *colPixelOpt;
        const uint32_t colWidth = *colWidthOpt;

        for (size_t rowPos = firstRow; rowPos <= lastRow; ++rowPos) {
            auto rowIdOpt = _rows.getAxisAt(rowPos);
            if (!rowIdOpt) {
                continue;
            }
            const ID& rowId = *rowIdOpt;

            // Check if cell exists at this position
            const std::string key = makeCellKey(colId, rowId);
            auto it = _cells.find(key);
            if (it == _cells.end()) {
                continue;
            }

            auto rowPixelOpt = _rows.axisToPixel(rowId);
            auto rowHeightOpt = _rows.getSize(rowId);
            if (!rowPixelOpt || !rowHeightOpt) {
                continue;
            }
            const uint32_t rowY = *rowPixelOpt;
            const uint32_t rowHeight = *rowHeightOpt;

            results.emplace_back(it->second, colX, rowY, colWidth, rowHeight);
        }
    }

    return results;
}

std::pair<size_t, size_t> ViewportIndex::getVisibleColumnRange(uint32_t x1, uint32_t x2) const {
    if (_columns.empty() || x1 >= x2) {
        return {0, 0};
    }

    // Find first column that intersects viewport
    auto firstResult = _columns.pixelToAxis(x1);
    const size_t firstCol = firstResult ? firstResult->position : 0;

    // Find last column that intersects viewport
    // x2 is exclusive, so we look for x2 - 1
    auto lastResult = _columns.pixelToAxis(x2 > 0 ? x2 - 1 : 0);
    size_t lastCol = lastResult ? lastResult->position : 0;

    // Clamp to valid range
    const size_t maxCol = _columns.count() > 0 ? _columns.count() - 1 : 0;
    lastCol = std::min(lastCol, maxCol);

    return {firstCol, lastCol};
}

std::pair<size_t, size_t> ViewportIndex::getVisibleRowRange(uint32_t y1, uint32_t y2) const {
    if (_rows.empty() || y1 >= y2) {
        return {0, 0};
    }

    // Find first row that intersects viewport
    auto firstResult = _rows.pixelToAxis(y1);
    const size_t firstRow = firstResult ? firstResult->position : 0;

    // Find last row that intersects viewport
    // y2 is exclusive, so we look for y2 - 1
    auto lastResult = _rows.pixelToAxis(y2 > 0 ? y2 - 1 : 0);
    size_t lastRow = lastResult ? lastResult->position : 0;

    // Clamp to valid range
    const size_t maxRow = _rows.count() > 0 ? _rows.count() - 1 : 0;
    lastRow = std::min(lastRow, maxRow);

    return {firstRow, lastRow};
}

// ============================================================================
// Coordinate conversion
// ============================================================================

std::optional<AxisLookupResult> ViewportIndex::pixelToColumn(uint32_t x) const {
    return _columns.pixelToAxis(x);
}

std::optional<AxisLookupResult> ViewportIndex::pixelToRow(uint32_t y) const {
    return _rows.pixelToAxis(y);
}

std::optional<uint32_t> ViewportIndex::columnToPixel(const ID& colId) const {
    return _columns.axisToPixel(colId);
}

std::optional<uint32_t> ViewportIndex::rowToPixel(const ID& rowId) const {
    return _rows.axisToPixel(rowId);
}

std::optional<uint32_t> ViewportIndex::getColumnWidth(const ID& colId) const {
    return _columns.getSize(colId);
}

std::optional<uint32_t> ViewportIndex::getRowHeight(const ID& rowId) const {
    return _rows.getSize(rowId);
}

std::optional<ID> ViewportIndex::getColumnAt(size_t position) const {
    return _columns.getAxisAt(position);
}

std::optional<ID> ViewportIndex::getRowAt(size_t position) const {
    return _rows.getAxisAt(position);
}

// ============================================================================
// Incremental cell updates
// ============================================================================

void ViewportIndex::onCellAdded(Cell* cell) {
    if (cell == nullptr) {
        return;
    }
    const std::string key = makeCellKey(cell->colId, cell->rowId);
    _cells[key] = cell;
}

void ViewportIndex::onCellRemoved(Cell* cell) {
    if (cell == nullptr) {
        return;
    }
    const std::string key = makeCellKey(cell->colId, cell->rowId);
    _cells.erase(key);
}

void ViewportIndex::onCellChanged(Cell* /*cell*/) {
    // No-op: we don't index by value, only by position
    // The cell's bounding box doesn't change when its value changes
}

// ============================================================================
// Incremental axis updates
// ============================================================================

void ViewportIndex::onAxisInserted(const ID& axisId, bool isColumn, size_t position,
                                   uint32_t size) {
    if (isColumn) {
        _columns.insert(axisId, position, size);
    } else {
        _rows.insert(axisId, position, size);
    }
}

void ViewportIndex::onAxisDeleted(const ID& axisId, bool isColumn) {
    if (isColumn) {
        // Remove all cells in this column
        std::vector<std::string> keysToRemove;
        for (const auto& [key, cell] : _cells) {
            if (cell->colId == axisId) {
                keysToRemove.push_back(key);
            }
        }
        for (const auto& key : keysToRemove) {
            _cells.erase(key);
        }
        _columns.remove(axisId);
    } else {
        // Remove all cells in this row
        std::vector<std::string> keysToRemove;
        for (const auto& [key, cell] : _cells) {
            if (cell->rowId == axisId) {
                keysToRemove.push_back(key);
            }
        }
        for (const auto& key : keysToRemove) {
            _cells.erase(key);
        }
        _rows.remove(axisId);
    }
}

void ViewportIndex::onAxisResized(const ID& axisId, bool isColumn, uint32_t newSize) {
    if (isColumn) {
        _columns.resize(axisId, newSize);
    } else {
        _rows.resize(axisId, newSize);
    }
}

void ViewportIndex::onAxisMoved(const ID& axisId, bool isColumn, size_t newPosition) {
    if (isColumn) {
        _columns.move(axisId, newPosition);
    } else {
        _rows.move(axisId, newPosition);
    }
}

// ============================================================================
// Utility
// ============================================================================

uint32_t ViewportIndex::totalWidth() const {
    return _columns.totalSize();
}

uint32_t ViewportIndex::totalHeight() const {
    return _rows.totalSize();
}

size_t ViewportIndex::columnCount() const {
    return _columns.count();
}

size_t ViewportIndex::rowCount() const {
    return _rows.count();
}

size_t ViewportIndex::cellCount() const {
    return _cells.size();
}

bool ViewportIndex::empty() const {
    return _columns.empty() && _rows.empty() && _cells.empty();
}

bool ViewportIndex::verify() const {
    return _columns.verify() && _rows.verify();
}

std::string ViewportIndex::makeCellKey(const ID& colId, const ID& rowId) {
    return colId.toString() + rowId.toString();
}

}  // namespace cells
