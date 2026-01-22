#include "core/cells/viewport_index.h"

#include <algorithm>
#include <utility>

namespace cells {

// ============================================================================
// Build from sheet
// ============================================================================

void ViewportIndex::build(const Sheet& sheet) {
    clear();
    _sheet = &sheet;

    // Only build cell HashMap - axis indexes are owned by Sheet
    const Workbook* wb = sheet.getWorkbook();
    for (const ID& cellId : sheet.getCellIds()) {
        const Cell* cell = wb ? wb->getCell(cellId) : nullptr;
        if (cell) {
            const std::string key = makeCellKey(cell->colId, cell->rowId);
            // const_cast is safe here - the cell storage is mutable at runtime
            _cells[key] = const_cast<Cell*>(cell);
        }
    }
}

void ViewportIndex::clear() {
    _cells.clear();
    _sheet = nullptr;
}

// ============================================================================
// Viewport query
// ============================================================================

std::vector<ViewportEntry> ViewportIndex::queryViewport(uint32_t x1, uint32_t y1, uint32_t x2,
                                                        uint32_t y2) const {
    std::vector<ViewportEntry> results;

    if (_sheet == nullptr || x1 >= x2 || y1 >= y2) {
        return results;
    }

    const AxisIndex& columns = _sheet->getColumnAxisIndex();
    const AxisIndex& rows = _sheet->getRowAxisIndex();

    if (columns.empty() || rows.empty()) {
        return results;
    }

    // Find visible column range
    const auto [firstCol, lastCol] = getVisibleColumnRange(x1, x2);

    // Find visible row range
    const auto [firstRow, lastRow] = getVisibleRowRange(y1, y2);

    // Iterate over visible cells
    // For each visible column and row, check if a cell exists at that position
    for (size_t colPos = firstCol; colPos <= lastCol; ++colPos) {
        auto colIdOpt = columns.getAxisAt(colPos);
        if (!colIdOpt) {
            continue;
        }
        const ID& colId = *colIdOpt;

        auto colPixelOpt = columns.axisToPixel(colId);
        auto colWidthOpt = columns.getSize(colId);
        if (!colPixelOpt || !colWidthOpt) {
            continue;
        }
        const uint32_t colX = *colPixelOpt;
        const uint32_t colWidth = *colWidthOpt;

        for (size_t rowPos = firstRow; rowPos <= lastRow; ++rowPos) {
            auto rowIdOpt = rows.getAxisAt(rowPos);
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

            auto rowPixelOpt = rows.axisToPixel(rowId);
            auto rowHeightOpt = rows.getSize(rowId);
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
    if (_sheet == nullptr || x1 >= x2) {
        return {0, 0};
    }

    const AxisIndex& columns = _sheet->getColumnAxisIndex();
    if (columns.empty()) {
        return {0, 0};
    }

    // Find first column that intersects viewport
    auto firstResult = columns.pixelToAxis(x1);
    const size_t firstCol = firstResult ? firstResult->position : 0;

    // Find last column that intersects viewport
    // x2 is exclusive, so we look for x2 - 1
    auto lastResult = columns.pixelToAxis(x2 > 0 ? x2 - 1 : 0);
    size_t lastCol = lastResult ? lastResult->position : 0;

    // Clamp to valid range
    const size_t maxCol = columns.count() > 0 ? columns.count() - 1 : 0;
    lastCol = std::min(lastCol, maxCol);

    return {firstCol, lastCol};
}

std::pair<size_t, size_t> ViewportIndex::getVisibleRowRange(uint32_t y1, uint32_t y2) const {
    if (_sheet == nullptr || y1 >= y2) {
        return {0, 0};
    }

    const AxisIndex& rows = _sheet->getRowAxisIndex();
    if (rows.empty()) {
        return {0, 0};
    }

    // Find first row that intersects viewport
    auto firstResult = rows.pixelToAxis(y1);
    const size_t firstRow = firstResult ? firstResult->position : 0;

    // Find last row that intersects viewport
    // y2 is exclusive, so we look for y2 - 1
    auto lastResult = rows.pixelToAxis(y2 > 0 ? y2 - 1 : 0);
    size_t lastRow = lastResult ? lastResult->position : 0;

    // Clamp to valid range
    const size_t maxRow = rows.count() > 0 ? rows.count() - 1 : 0;
    lastRow = std::min(lastRow, maxRow);

    return {firstRow, lastRow};
}

// ============================================================================
// Coordinate conversion (delegates to Sheet's AxisIndex)
// ============================================================================

std::optional<AxisLookupResult> ViewportIndex::pixelToColumn(uint32_t x) const {
    if (_sheet == nullptr) {
        return std::nullopt;
    }
    return _sheet->getColumnAxisIndex().pixelToAxis(x);
}

std::optional<AxisLookupResult> ViewportIndex::pixelToRow(uint32_t y) const {
    if (_sheet == nullptr) {
        return std::nullopt;
    }
    return _sheet->getRowAxisIndex().pixelToAxis(y);
}

std::optional<uint32_t> ViewportIndex::columnToPixel(const ID& colId) const {
    if (_sheet == nullptr) {
        return std::nullopt;
    }
    return _sheet->getColumnAxisIndex().axisToPixel(colId);
}

std::optional<uint32_t> ViewportIndex::rowToPixel(const ID& rowId) const {
    if (_sheet == nullptr) {
        return std::nullopt;
    }
    return _sheet->getRowAxisIndex().axisToPixel(rowId);
}

std::optional<uint32_t> ViewportIndex::getColumnWidth(const ID& colId) const {
    if (_sheet == nullptr) {
        return std::nullopt;
    }
    return _sheet->getColumnAxisIndex().getSize(colId);
}

std::optional<uint32_t> ViewportIndex::getRowHeight(const ID& rowId) const {
    if (_sheet == nullptr) {
        return std::nullopt;
    }
    return _sheet->getRowAxisIndex().getSize(rowId);
}

std::optional<ID> ViewportIndex::getColumnAt(size_t position) const {
    if (_sheet == nullptr) {
        return std::nullopt;
    }
    return _sheet->getColumnAxisIndex().getAxisAt(position);
}

std::optional<ID> ViewportIndex::getRowAt(size_t position) const {
    if (_sheet == nullptr) {
        return std::nullopt;
    }
    return _sheet->getRowAxisIndex().getAxisAt(position);
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

void ViewportIndex::onCellRemoved(const ID& colId, const ID& rowId) {
    const std::string key = makeCellKey(colId, rowId);
    _cells.erase(key);
}

void ViewportIndex::onCellChanged(Cell* /*cell*/) {
    // No-op: we don't index by value, only by position
    // The cell's bounding box doesn't change when its value changes
}

// ============================================================================
// Axis update notifications (mostly no-ops since Sheet maintains AxisIndex)
// ============================================================================

void ViewportIndex::onAxisInserted(const ID& /*axisId*/, bool /*isColumn*/, size_t /*position*/,
                                   uint32_t /*size*/) {
    // No-op: Sheet maintains the AxisIndex
}

void ViewportIndex::onAxisDeleted(const ID& axisId, bool isColumn) {
    // Remove all cells in the deleted axis from the cell HashMap
    if (isColumn) {
        std::vector<std::string> keysToRemove;
        for (const auto& [key, cell] : _cells) {
            if (cell->colId == axisId) {
                keysToRemove.push_back(key);
            }
        }
        for (const auto& key : keysToRemove) {
            _cells.erase(key);
        }
    } else {
        std::vector<std::string> keysToRemove;
        for (const auto& [key, cell] : _cells) {
            if (cell->rowId == axisId) {
                keysToRemove.push_back(key);
            }
        }
        for (const auto& key : keysToRemove) {
            _cells.erase(key);
        }
    }
}

void ViewportIndex::onAxisResized(const ID& /*axisId*/, bool /*isColumn*/, uint32_t /*newSize*/) {
    // No-op: Sheet maintains the AxisIndex
}

void ViewportIndex::onAxisMoved(const ID& /*axisId*/, bool /*isColumn*/, size_t /*newPosition*/) {
    // No-op: Sheet maintains the AxisIndex
}

// ============================================================================
// Utility
// ============================================================================

uint32_t ViewportIndex::totalWidth() const {
    if (_sheet == nullptr) {
        return 0;
    }
    return _sheet->getColumnAxisIndex().totalSize();
}

uint32_t ViewportIndex::totalHeight() const {
    if (_sheet == nullptr) {
        return 0;
    }
    return _sheet->getRowAxisIndex().totalSize();
}

size_t ViewportIndex::columnCount() const {
    if (_sheet == nullptr) {
        return 0;
    }
    return _sheet->getColumnAxisIndex().count();
}

size_t ViewportIndex::rowCount() const {
    if (_sheet == nullptr) {
        return 0;
    }
    return _sheet->getRowAxisIndex().count();
}

size_t ViewportIndex::cellCount() const {
    return _cells.size();
}

bool ViewportIndex::empty() const {
    if (_sheet == nullptr) {
        return true;
    }
    return _sheet->getColumnAxisIndex().empty() && _sheet->getRowAxisIndex().empty() &&
           _cells.empty();
}

bool ViewportIndex::verify() const {
    if (_sheet == nullptr) {
        return true;
    }
    return _sheet->getColumnAxisIndex().verify() && _sheet->getRowAxisIndex().verify();
}

std::string ViewportIndex::makeCellKey(const ID& colId, const ID& rowId) {
    return colId.toString() + rowId.toString();
}

}  // namespace cells
