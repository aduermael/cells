// =============================================================================
// WASM Bindings - Core Operations
// =============================================================================
//
// Implementation of core CellsEngine methods:
// - Constructor/destructor
// - Listener registration
// - Sheet info and management
// - Cell operations (create, update, delete)
// - Column/row operations (resize, rename, move, insert, delete)
// - Fill range
// - Workbook name and creation
//
// =============================================================================

#include <algorithm>
#include <emscripten/bind.h>
#include <iomanip>
#include <sstream>

#include "core/cells/builtin_themes.h"
#include "core/cells/crdt.h"
#include "core/cells/fill_range.h"
#include "core/cells/format_buffer.h"
#include "core/cells/format_code_formatter.h"
#include "core/cells/formula_display.h"
#include "core/cells/formula_parser.h"
#include "core/cells/formula_recalc.h"
#include "core/cells/formula_resolver.h"
#include "core/cells/formula_serializer.h"
#include "core/cells/id.h"
#include "core/cells/input_parser.h"
#include "core/cells/named_ranges.h"
#include "core/cells/number_formatter.h"
#include "core/cells/operation.h"
#include "core/cells/range.h"
#include "core/cells/ui_mutation.h"
#include "core/log/include/Logger.h"

#include "apps/wasm/bindings.h"

namespace cells::wasm {

// =============================================================================
// Helper: Compute Edit Value for Formula Bar
// =============================================================================
// Matches the helper in bindings_viewport.cc. See that file for details.
// CURRENCY/ACCOUNTING shows raw number, PERCENTAGE/DATE shows formatted value.

static std::string computeEditValueForCore(double num, const std::string& displayValue,
                                           const FormatBuffer& format) {
    NumberFormatCategory category = format.getCategory();

    switch (category) {
        case NumberFormatCategory::CURRENCY:
        case NumberFormatCategory::ACCOUNTING: {
            std::ostringstream numStr;
            if (std::floor(num) == num && std::abs(num) < 1e15) {
                numStr << static_cast<long long>(num);
            } else {
                numStr << std::setprecision(15) << num;
                std::string result = numStr.str();
                size_t dot = result.find('.');
                if (dot != std::string::npos) {
                    size_t last = result.find_last_not_of('0');
                    if (last != std::string::npos && last > dot) {
                        return result.substr(0, last + 1);
                    } else if (last == dot) {
                        return result.substr(0, dot);
                    }
                }
                return result;
            }
            return numStr.str();
        }

        case NumberFormatCategory::PERCENTAGE:
        case NumberFormatCategory::DATE:
        case NumberFormatCategory::TIME:
        case NumberFormatCategory::DATE_TIME:
            return displayValue;

        default:
            return displayValue;
    }
}

// ============================================================================
// Constructor / Destructor
// ============================================================================

CellsEngine::CellsEngine() : _workbook(nullptr), _activeSheetIndex(0), _listener(val::null()) {}

CellsEngine::~CellsEngine() {
    disableSync();
}

// ============================================================================
// Listener registration
// ============================================================================

void CellsEngine::setListener(val callback) {
    _listener = callback;
}

void CellsEngine::removeListener() {
    _listener = val::null();
}

// ============================================================================
// Internal helpers
// ============================================================================

Sheet* CellsEngine::activeSheet() {
    if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
        return nullptr;
    }
    return _workbook->getSheetByIndex(_activeSheetIndex);
}

ApplyResult CellsEngine::applyLocalUiOp(const Operation& op) {
    Sheet* sheet = activeSheet();
    if (!_workbook || sheet == nullptr) {
        return ApplyResult::INVALID_TARGET;
    }
    return uiApplyOperation(_luauSandbox, *_workbook, *sheet, op);
}

void CellsEngine::rebuildViewportIndex() {
    if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
        return;
    }

    auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
    if (!sheet) {
        return;
    }

    _viewportIndex.clear();
    _viewportIndex.build(*sheet);
    _refConverter.setContext(*sheet);
    _refConverter.setWorkbook(_workbook.get());
}

void CellsEngine::notifyListeners(ChangeType type) {
    if (_listener.isNull() || _listener.isUndefined()) {
        return;
    }

    const char* typeStr = nullptr;
    switch (type) {
        case ChangeType::CELL_CHANGED:
            typeStr = "cell";
            break;
        case ChangeType::STRUCTURE_CHANGED:
            typeStr = "structure";
            break;
        case ChangeType::SHEET_CHANGED:
            typeStr = "sheet";
            break;
        case ChangeType::DATA_LOADED:
            typeStr = "loaded";
            break;
        case ChangeType::LOAD_PROGRESS:
            typeStr = "load_progress";
            break;
        case ChangeType::SYNC_STATE_CHANGED:
            typeStr = "sync_state";
            break;
        case ChangeType::PEER_JOINED:
            typeStr = "peer_joined";
            break;
        case ChangeType::PEER_LEFT:
            typeStr = "peer_left";
            break;
        case ChangeType::PRESENCE_CHANGED:
            typeStr = "presence";
            break;
    }

    _listener(std::string(typeStr));
}

void CellsEngine::notifyListenersWithData(ChangeType type, const std::string& data) {
    if (_listener.isNull() || _listener.isUndefined()) {
        return;
    }

    const char* typeStr = nullptr;
    switch (type) {
        case ChangeType::CELL_CHANGED:
            typeStr = "cell";
            break;
        case ChangeType::STRUCTURE_CHANGED:
            typeStr = "structure";
            break;
        case ChangeType::SHEET_CHANGED:
            typeStr = "sheet";
            break;
        case ChangeType::DATA_LOADED:
            typeStr = "loaded";
            break;
        case ChangeType::LOAD_PROGRESS:
            typeStr = "load_progress";
            break;
        case ChangeType::SYNC_STATE_CHANGED:
            typeStr = "sync_state";
            break;
        case ChangeType::PEER_JOINED:
            typeStr = "peer_joined";
            break;
        case ChangeType::PEER_LEFT:
            typeStr = "peer_left";
            break;
        case ChangeType::PRESENCE_CHANGED:
            typeStr = "presence";
            break;
    }

    _listener(std::string(typeStr), data);
}

void CellsEngine::notifyLoadProgress(size_t cellsLoaded, size_t totalEstimate) {
    if (_listener.isNull() || _listener.isUndefined()) {
        return;
    }
    std::ostringstream data;
    data << cellsLoaded << ":" << totalEstimate;
    _listener(std::string("load_progress"), data.str());
}

void CellsEngine::broadcastPendingOperations() {
    // If SyncClient exists, use it (it has its own SyncManager)
    // Note: SyncClient creates its own SyncManager internally, so we must use
    // its broadcastOperations() which queues to the right SyncManager AND sends.
    if (_syncClient) {
        _syncClient->broadcastOperations();
    } else if (_syncManager) {
        // Fallback: queue to CellsEngine's SyncManager for later retrieval
        // (used when collaboration is manual via handlePeerMessage/getOutgoingMessages)
        _syncManager->queueOperationsBroadcast();
    }
}

// ============================================================================
// Sheet info methods
// ============================================================================

std::string CellsEngine::getSheetInfo() {
    if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
        return "{\"error\":\"No sheet available\"}";
    }

    auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);

    constexpr uint32_t MIN_COLS = 26;
    constexpr uint32_t MIN_ROWS = 100;

    uint32_t colCount = std::max(MIN_COLS, static_cast<uint32_t>(sheet->columnCount()));
    uint32_t rowCount = std::max(MIN_ROWS, static_cast<uint32_t>(sheet->rowCount()));

    std::ostringstream json;
    json << "{";
    json << "\"name\":\"" << jsonEscape(sheet->name) << "\",";
    json << "\"rowCount\":" << rowCount << ",";
    json << "\"colCount\":" << colCount << ",";
    json << "\"defaultColWidth\":" << DEFAULT_COLUMN_WIDTH << ",";
    json << "\"defaultRowHeight\":" << DEFAULT_ROW_HEIGHT << ",";
    json << "\"showGridLines\":" << (sheet->showGridLines ? "true" : "false") << ",";
    json << "\"zoomScale\":" << sheet->zoomScale << ",";
    json << "\"freezeCol\":" << sheet->freezeCol << ",";
    json << "\"freezeRow\":" << sheet->freezeRow;
    json << "}";

    return json.str();
}

int CellsEngine::getSheetCount() {
    return _workbook ? static_cast<int>(_workbook->sheetCount()) : 0;
}

std::string CellsEngine::getSheetName(int index) {
    if (!_workbook || index < 0 || static_cast<size_t>(index) >= _workbook->sheetCount()) {
        return "";
    }
    return _workbook->getSheetByIndex(index)->name;
}

int CellsEngine::getActiveSheetIndex() {
    return static_cast<int>(_activeSheetIndex);
}

void CellsEngine::syncActiveSheetIdFromIndex() {
    _activeSheetId = ID();
    if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
        return;
    }
    const Sheet* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
    if (sheet != nullptr) {
        _activeSheetId = sheet->id;
    }
}

void CellsEngine::setActiveSheetIndex(size_t index) {
    _activeSheetIndex = index;
    syncActiveSheetIdFromIndex();
}

void CellsEngine::applyResolvedActiveSheet(size_t resolvedIndex) {
    if (resolvedIndex != _activeSheetIndex) {
        _activeSheetIndex = resolvedIndex;
    }
    // Always refresh id — sheet at this index may have been replaced/recreated.
    syncActiveSheetIdFromIndex();
}

void CellsEngine::setActiveSheet(int index) {
    if (_workbook && index >= 0 && static_cast<size_t>(index) < _workbook->sheetCount()) {
        setActiveSheetIndex(static_cast<size_t>(index));
        rebuildViewportIndex();
        notifyListeners(ChangeType::SHEET_CHANGED);
    }
}

void CellsEngine::setFreezePanes(int freezeCol, int freezeRow) {
    Sheet* sheet = activeSheet();
    if (!_workbook || sheet == nullptr) {
        return;
    }
    const ScriptResult sr = uiFreezePanes(_luauSandbox, *_workbook, *sheet, freezeCol, freezeRow);
    if (!sr.success) {
        return;
    }
    notifyListeners(ChangeType::SHEET_CHANGED);
}

std::string CellsEngine::addSheet(const std::string& name) {
    if (!_workbook) {
        return "{\"error\":\"No workbook\"}";
    }

    std::string sheetName = name.empty() ? "Sheet" : name;
    int suffix = 1;

    auto isNameTaken = [this](const std::string& n) {
        for (size_t i = 0; i < _workbook->sheetCount(); i++) {
            if (_workbook->getSheetByIndex(i)->name == n) {
                return true;
            }
        }
        return false;
    };

    if (name.empty() || isNameTaken(sheetName)) {
        std::string baseName = name.empty() ? "Sheet" : name;
        do {
            sheetName = baseName + std::to_string(suffix++);
        } while (isNameTaken(sheetName));
    }

    ID sheetId = generate_id();
    size_t newIndex = _workbook->sheetCount();

    std::string payload = "{\"name\":\"" + jsonEscape(sheetName) + "\"}";
    Operation op = makeSheetSetOp(*_workbook, sheetId, payload);
    applyLocalUiOp(op);

    broadcastPendingOperations();

    notifyListeners(ChangeType::SHEET_CHANGED);

    std::ostringstream json;
    json << "{\"success\":true,\"index\":" << newIndex << ",\"name\":\"" << jsonEscape(sheetName)
         << "\"}";
    return json.str();
}

std::string CellsEngine::deleteSheet(int index) {
    if (!_workbook) {
        return "{\"error\":\"No workbook\"}";
    }

    if (index < 0 || static_cast<size_t>(index) >= _workbook->sheetCount()) {
        return "{\"error\":\"Invalid sheet index\"}";
    }

    if (_workbook->sheetCount() <= 1) {
        return "{\"error\":\"Cannot delete last sheet\"}";
    }

    Sheet* sheet = _workbook->getSheetByIndex(static_cast<size_t>(index));
    ID sheetId = sheet->id;

    Operation op = makeSheetDeleteOp(*_workbook, sheetId);
    applyLocalUiOp(op);

    broadcastPendingOperations();

    if (_activeSheetIndex >= _workbook->sheetCount()) {
        setActiveSheetIndex(_workbook->sheetCount() - 1);
    } else if (static_cast<size_t>(index) < _activeSheetIndex) {
        setActiveSheetIndex(_activeSheetIndex - 1);
    } else if (static_cast<size_t>(index) == _activeSheetIndex) {
        if (_activeSheetIndex >= _workbook->sheetCount()) {
            setActiveSheetIndex(_workbook->sheetCount() - 1);
        } else {
            syncActiveSheetIdFromIndex();
        }
    }

    rebuildViewportIndex();
    notifyListeners(ChangeType::SHEET_CHANGED);

    std::ostringstream json;
    json << "{\"success\":true,\"activeIndex\":" << _activeSheetIndex << "}";
    return json.str();
}

std::string CellsEngine::renameSheet(int index, const std::string& name) {
    if (!_workbook) {
        return "{\"error\":\"No workbook\"}";
    }

    if (index < 0 || static_cast<size_t>(index) >= _workbook->sheetCount()) {
        return "{\"error\":\"Invalid sheet index\"}";
    }

    if (name.empty()) {
        return "{\"error\":\"Sheet name cannot be empty\"}";
    }

    for (size_t i = 0; i < _workbook->sheetCount(); i++) {
        if (static_cast<int>(i) != index && _workbook->getSheetByIndex(i)->name == name) {
            return "{\"error\":\"Sheet name already exists\"}";
        }
    }

    Sheet* sheet = _workbook->getSheetByIndex(static_cast<size_t>(index));
    ID sheetId = sheet->id;

    std::string payload = "{\"name\":\"" + jsonEscape(name) + "\"}";
    Operation op = makeSheetSetOp(*_workbook, sheetId, payload);
    applyLocalUiOp(op);

    broadcastPendingOperations();

    notifyListeners(ChangeType::SHEET_CHANGED);
    return "{\"success\":true}";
}

std::string CellsEngine::moveSheet(int fromIndex, int toIndex) {
    if (!_workbook) {
        return "{\"error\":\"No workbook\"}";
    }

    size_t count = _workbook->sheetCount();
    if (fromIndex < 0 || static_cast<size_t>(fromIndex) >= count) {
        return "{\"error\":\"Invalid source index\"}";
    }
    if (toIndex < 0 || static_cast<size_t>(toIndex) > count) {
        return "{\"error\":\"Invalid target index\"}";
    }

    if (fromIndex == toIndex || fromIndex + 1 == toIndex) {
        return "{\"success\":true}";
    }

    Sheet* ctxSheet = activeSheet();
    if (ctxSheet == nullptr) {
        ctxSheet = _workbook->getSheetByIndex(0);
    }
    if (ctxSheet == nullptr) {
        return "{\"error\":\"No sheet available\"}";
    }
    const ScriptResult sr = uiMoveSheet(_luauSandbox, *_workbook, *ctxSheet, fromIndex, toIndex);
    if (!sr.success) {
        return "{\"error\":\"" + jsonEscape(sr.error.empty() ? "Luau execution failed" : sr.error) +
               "\"}";
    }

    int insertAt = toIndex > fromIndex ? toIndex - 1 : toIndex;

    if (static_cast<size_t>(fromIndex) == _activeSheetIndex) {
        setActiveSheetIndex(static_cast<size_t>(insertAt));
    } else if (fromIndex < toIndex) {
        if (_activeSheetIndex > static_cast<size_t>(fromIndex) &&
            _activeSheetIndex <= static_cast<size_t>(insertAt)) {
            setActiveSheetIndex(_activeSheetIndex - 1);
        }
    } else {
        if (_activeSheetIndex >= static_cast<size_t>(toIndex) &&
            _activeSheetIndex < static_cast<size_t>(fromIndex)) {
            setActiveSheetIndex(_activeSheetIndex + 1);
        }
    }

    notifyListeners(ChangeType::SHEET_CHANGED);

    std::ostringstream json;
    json << "{\"success\":true,\"activeIndex\":" << _activeSheetIndex << "}";
    return json.str();
}

// ============================================================================
// Cell operations
// ============================================================================

std::string CellsEngine::updateCell(const std::string& cellIdStr, const std::string& value) {
    Sheet* sheet = activeSheet();
    if (!_workbook || sheet == nullptr) {
        return "{\"error\":\"No sheet available\"}";
    }
    if (cellIdStr.size() != ID_LENGTH) {
        return "{\"error\":\"Invalid cell ID\"}";
    }
    const UiCellWriteResult wr =
        uiWriteCellById(_luauSandbox, *_workbook, *sheet, ID(cellIdStr), value, false);
    if (!wr.success) {
        return "{\"error\":\"" + jsonEscape(wr.error) + "\"}";
    }
    broadcastPendingOperations();
    rebuildViewportIndex();
    notifyListeners(ChangeType::CELL_CHANGED);
    return "{\"success\":true}";
}

std::string CellsEngine::updateCellWithFormatDetection(const std::string& cellIdStr,
                                                       const std::string& value) {
    Sheet* sheet = activeSheet();
    if (!_workbook || sheet == nullptr) {
        return "{\"error\":\"No sheet available\"}";
    }
    if (cellIdStr.size() != ID_LENGTH) {
        return "{\"error\":\"Invalid cell ID\"}";
    }
    const UiCellWriteResult wr =
        uiWriteCellById(_luauSandbox, *_workbook, *sheet, ID(cellIdStr), value, true);
    if (!wr.success) {
        return "{\"error\":\"" + jsonEscape(wr.error) + "\"}";
    }
    broadcastPendingOperations();
    rebuildViewportIndex();
    notifyListeners(ChangeType::CELL_CHANGED);
    return "{\"success\":true,\"format\":\"" + wr.formatBase64 + "\"}";
}

std::string CellsEngine::createCell(uint32_t col, uint32_t row, const std::string& value) {
    Sheet* sheet = activeSheet();
    if (!_workbook || sheet == nullptr) {
        return "{\"error\":\"No sheet available\"}";
    }
    const UiCellWriteResult wr =
        uiWriteCell(_luauSandbox, *_workbook, *sheet, col, row, value, false);
    if (!wr.success) {
        return "{\"error\":\"" + jsonEscape(wr.error) + "\"}";
    }
    broadcastPendingOperations();
    rebuildViewportIndex();
    notifyListeners(ChangeType::CELL_CHANGED);
    return "{\"success\":true,\"id\":\"" + wr.cellId.toString() + "\"}";
}

std::string CellsEngine::getOrCreateCellAt(uint32_t col, uint32_t row) {
    Sheet* sheet = activeSheet();
    if (!_workbook || sheet == nullptr) {
        return "{\"error\":\"No sheet available\"}";
    }

    Axis* colAxis = sheet->getColumnByPosition(col);
    Axis* rowAxis = sheet->getRowByPosition(row);
    Cell* existing = nullptr;
    if (colAxis != nullptr && rowAxis != nullptr) {
        existing = sheet->getCellAt(colAxis->id, rowAxis->id);
    }
    const bool existed = existing != nullptr;

    const UiCellWriteResult wr = uiEnsureCell(_luauSandbox, *_workbook, *sheet, col, row);
    if (!wr.success) {
        return "{\"error\":\"" + jsonEscape(wr.error) + "\"}";
    }
    Cell* cell = sheet->getCell(wr.cellId);
    if (cell == nullptr) {
        return "{\"error\":\"Cell not found after create\"}";
    }

    broadcastPendingOperations();
    rebuildViewportIndex();
    if (!existed) {
        notifyListeners(ChangeType::CELL_CHANGED);
    }

    std::ostringstream json;
    json << "{\"success\":true,\"id\":\"" << cell->id.toString()
         << "\",\"existed\":" << (existed ? "true" : "false") << ",";

    std::string editValue = cell->value.raw;
    const FormatBuffer* cellFormat = _workbook->getEntityFormat(cell->id);
    if (cell->value.type == CellValueType::NUMBER && cellFormat != nullptr &&
        !cellFormat->isEmpty()) {
        double numValue = cell->value.asNumber();
        FormatCodeResult formatted = cells::formatWithCode(numValue, cellFormat->toFormatCode());
        if (formatted.success) {
            editValue = computeEditValueForCore(numValue, formatted.text, *cellFormat);
        }
    }

    if (cell->isFormula()) {
        Formula* formula = cell->getFormula();
        if (formula != nullptr && formula->ast != nullptr) {
            const FormulaDisplayConverter displayConverter(*sheet, _workbook.get());
            const std::string a1Formula = displayConverter.toDisplayString(formula->ast);
            json << "\"formula\":\"" << jsonEscape(a1Formula) << "\",";
        }
        json << "\"value\":\"" << jsonEscape(cell->value.raw) << "\",";
        json << "\"editValue\":\"" << jsonEscape(editValue) << "\"";
    } else {
        json << "\"value\":\"" << jsonEscape(cell->value.raw) << "\",";
        json << "\"editValue\":\"" << jsonEscape(editValue) << "\"";
    }
    json << "}";
    return json.str();
}

std::string CellsEngine::deleteCell(const std::string& cellIdStr) {
    Sheet* sheet = activeSheet();
    if (!_workbook || sheet == nullptr) {
        return "{\"error\":\"No sheet available\"}";
    }
    if (cellIdStr.size() != ID_LENGTH) {
        return "{\"error\":\"Invalid cell ID\"}";
    }
    std::string error;
    if (!uiDeleteCell(_luauSandbox, *_workbook, *sheet, ID(cellIdStr), &error)) {
        return "{\"error\":\"" + jsonEscape(error) + "\"}";
    }
    broadcastPendingOperations();
    rebuildViewportIndex();
    notifyListeners(ChangeType::CELL_CHANGED);
    return "{\"success\":true}";
}

std::string CellsEngine::deleteCellAt(uint32_t col, uint32_t row) {
    Sheet* sheet = activeSheet();
    if (!_workbook || sheet == nullptr) {
        return "{\"error\":\"No sheet available\"}";
    }
    Axis* colAxis = sheet->getColumnByPosition(col);
    Axis* rowAxis = sheet->getRowByPosition(row);
    if (colAxis == nullptr || rowAxis == nullptr) {
        return "{\"success\":true,\"deleted\":false}";
    }
    Cell* cell = sheet->getCellAt(colAxis->id, rowAxis->id);
    if (cell == nullptr) {
        return "{\"success\":true,\"deleted\":false}";
    }
    std::string error;
    if (!uiDeleteCell(_luauSandbox, *_workbook, *sheet, cell->id, &error)) {
        return "{\"error\":\"" + jsonEscape(error) + "\"}";
    }
    broadcastPendingOperations();
    rebuildViewportIndex();
    notifyListeners(ChangeType::CELL_CHANGED);
    return "{\"success\":true,\"deleted\":true}";
}

// ============================================================================
// Column/row resize operations
// ============================================================================

std::string CellsEngine::resizeColumn(const std::string& colIdStr, uint32_t width) {
    if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
        return "{\"error\":\"No sheet available\"}";
    }

    auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
    if (!sheet) {
        return "{\"error\":\"Sheet not found\"}";
    }

    if (colIdStr.size() != ID_LENGTH) {
        return "{\"error\":\"Invalid column ID\"}";
    }
    ID colId(colIdStr);

    Axis* colAxis = sheet->getColumn(colId);
    if (colAxis == nullptr) {
        return "{\"error\":\"Column not found\"}";
    }

    if (width < 20)
        width = 20;
    if (width > 1000)
        width = 1000;

    // Build full-state payload for resurrection correctness
    std::string payload = "{\"pos\":" + std::to_string(colAxis->position);
    payload += ",\"size\":" + std::to_string(width);
    if (!colAxis->name.empty()) {
        payload += ",\"name\":\"" + jsonEscape(colAxis->name) + "\"";
    }
    if (colAxis->hasStyle()) {
        const StyleBuffer* sty = _workbook->getEntityStyle(colId);
        if (sty) {
            payload += ",\"sty\":\"" + sty->toBase64() + "\"";
        }
    }
    if (colAxis->hasFormat()) {
        const FormatBuffer* fmt = _workbook->getEntityFormat(colId);
        if (fmt) {
            payload += ",\"fmt\":\"" + fmt->toBase64() + "\"";
        }
    }
    if (colAxis->hidden()) {
        payload += ",\"hidden\":true";
    }
    payload += "}";
    Operation op = makeColSetOp(*_workbook, colId, payload);
    applyLocalUiOp(op);

    broadcastPendingOperations();

    _viewportIndex.onAxisResized(colId, true, width);
    notifyListeners(ChangeType::STRUCTURE_CHANGED);
    return "{\"success\":true}";
}

std::string CellsEngine::resizeColumnByPos(uint32_t pos, uint32_t width) {
    if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
        return "{\"error\":\"No sheet available\"}";
    }

    auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
    if (!sheet) {
        return "{\"error\":\"Sheet not found\"}";
    }

    if (width < 20)
        width = 20;
    if (width > 1000)
        width = 1000;

    // Shared core path (same as Luau setColumnWidth)
    bool colCreated = false;
    Axis* column = setColumnWidthByPosition(*_workbook, *sheet, pos, width, &colCreated);
    if (column == nullptr) {
        return "{\"error\":\"Failed to resize column\"}";
    }
    const ID colId = column->id;

    broadcastPendingOperations();

    if (colCreated) {
        _viewportIndex.onAxisInserted(colId, true, pos, width);
    } else {
        _viewportIndex.onAxisResized(colId, true, width);
    }
    notifyListeners(ChangeType::STRUCTURE_CHANGED);

    std::ostringstream json;
    json << "{\"success\":true,\"id\":\"" << colId.toString() << "\"}";
    return json.str();
}

std::string CellsEngine::resizeRow(const std::string& rowIdStr, uint32_t height) {
    if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
        return "{\"error\":\"No sheet available\"}";
    }

    auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
    if (!sheet) {
        return "{\"error\":\"Sheet not found\"}";
    }

    if (rowIdStr.size() != ID_LENGTH) {
        return "{\"error\":\"Invalid row ID\"}";
    }
    ID rowId(rowIdStr);

    Axis* rowAxis = sheet->getRow(rowId);
    if (rowAxis == nullptr) {
        return "{\"error\":\"Row not found\"}";
    }

    if (height < 10)
        height = 10;
    if (height > 500)
        height = 500;

    // Build full-state payload for resurrection correctness
    std::string payload = "{\"pos\":" + std::to_string(rowAxis->position);
    payload += ",\"size\":" + std::to_string(height);
    if (rowAxis->hasStyle()) {
        const StyleBuffer* sty = _workbook->getEntityStyle(rowId);
        if (sty) {
            payload += ",\"sty\":\"" + sty->toBase64() + "\"";
        }
    }
    if (rowAxis->hasFormat()) {
        const FormatBuffer* fmt = _workbook->getEntityFormat(rowId);
        if (fmt) {
            payload += ",\"fmt\":\"" + fmt->toBase64() + "\"";
        }
    }
    if (rowAxis->hidden()) {
        payload += ",\"hidden\":true";
    }
    payload += "}";
    Operation op = makeRowSetOp(*_workbook, rowId, payload);
    applyLocalUiOp(op);

    broadcastPendingOperations();

    _viewportIndex.onAxisResized(rowId, false, height);
    notifyListeners(ChangeType::STRUCTURE_CHANGED);
    return "{\"success\":true}";
}

std::string CellsEngine::resizeRowByPos(uint32_t pos, uint32_t height) {
    if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
        return "{\"error\":\"No sheet available\"}";
    }

    auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
    if (!sheet) {
        return "{\"error\":\"Sheet not found\"}";
    }

    if (height < 10)
        height = 10;
    if (height > 500)
        height = 500;

    // Shared core path (same as Luau setRowHeight)
    bool rowCreated = false;
    Axis* row = setRowHeightByPosition(*_workbook, *sheet, pos, height, &rowCreated);
    if (row == nullptr) {
        return "{\"error\":\"Failed to resize row\"}";
    }
    const ID rowId = row->id;

    broadcastPendingOperations();

    if (rowCreated) {
        _viewportIndex.onAxisInserted(rowId, false, pos, height);
    } else {
        _viewportIndex.onAxisResized(rowId, false, height);
    }
    notifyListeners(ChangeType::STRUCTURE_CHANGED);

    std::ostringstream json;
    json << "{\"success\":true,\"id\":\"" << rowId.toString() << "\"}";
    return json.str();
}

// ============================================================================
// Column/row rename operations
// ============================================================================

std::string CellsEngine::renameColumn(const std::string& colIdStr, const std::string& name) {
    if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
        return "{\"error\":\"No sheet available\"}";
    }

    auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
    if (!sheet) {
        return "{\"error\":\"Sheet not found\"}";
    }

    if (colIdStr.size() != ID_LENGTH) {
        return "{\"error\":\"Invalid column ID\"}";
    }
    ID colId(colIdStr);

    Axis* colAxis = sheet->getColumn(colId);
    if (colAxis == nullptr) {
        return "{\"error\":\"Column not found\"}";
    }

    // Build full-state payload for resurrection correctness
    std::string payload = "{\"pos\":" + std::to_string(colAxis->position);
    if (colAxis->sizeSet()) {
        payload += ",\"size\":" + std::to_string(colAxis->size);
    }
    payload += ",\"name\":\"" + jsonEscape(name) + "\"";
    if (colAxis->hasStyle()) {
        const StyleBuffer* sty = _workbook->getEntityStyle(colId);
        if (sty) {
            payload += ",\"sty\":\"" + sty->toBase64() + "\"";
        }
    }
    if (colAxis->hasFormat()) {
        const FormatBuffer* fmt = _workbook->getEntityFormat(colId);
        if (fmt) {
            payload += ",\"fmt\":\"" + fmt->toBase64() + "\"";
        }
    }
    if (colAxis->hidden()) {
        payload += ",\"hidden\":true";
    }
    payload += "}";
    Operation op = makeColSetOp(*_workbook, colId, payload);
    applyLocalUiOp(op);

    broadcastPendingOperations();
    notifyListeners(ChangeType::STRUCTURE_CHANGED);
    return "{\"success\":true}";
}

std::string CellsEngine::renameColumnByPos(uint32_t pos, const std::string& name) {
    if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
        return "{\"error\":\"No sheet available\"}";
    }

    auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
    if (!sheet) {
        return "{\"error\":\"Sheet not found\"}";
    }

    Axis* column = sheet->getColumnByPosition(pos);
    ID colId;
    if (column != nullptr) {
        colId = column->id;
    }

    if (!column) {
        colId = generate_id();
        std::string insertPayload = "{\"pos\":" + std::to_string(pos) +
                                    ",\"size\":" + std::to_string(DEFAULT_COLUMN_WIDTH) + "}";
        Operation insertOp = makeColSetOp(*_workbook, colId, insertPayload);
        applyLocalUiOp(insertOp);

        std::string renamePayload = "{\"name\":\"" + jsonEscape(name) + "\"}";
        Operation renameOp = makeColSetOp(*_workbook, colId, renamePayload);
        applyLocalUiOp(renameOp);

        column = sheet->getColumn(colId);
    } else {
        // Build full-state payload for resurrection correctness
        std::string payload = "{\"pos\":" + std::to_string(column->position);
        if (column->sizeSet()) {
            payload += ",\"size\":" + std::to_string(column->size);
        }
        payload += ",\"name\":\"" + jsonEscape(name) + "\"";
        if (column->hasStyle()) {
            const StyleBuffer* sty = _workbook->getEntityStyle(colId);
            if (sty) {
                payload += ",\"sty\":\"" + sty->toBase64() + "\"";
            }
        }
        if (column->hasFormat()) {
            const FormatBuffer* fmt = _workbook->getEntityFormat(colId);
            if (fmt) {
                payload += ",\"fmt\":\"" + fmt->toBase64() + "\"";
            }
        }
        if (column->hidden()) {
            payload += ",\"hidden\":true";
        }
        payload += "}";
        Operation op = makeColSetOp(*_workbook, colId, payload);
        applyLocalUiOp(op);
    }

    broadcastPendingOperations();
    notifyListeners(ChangeType::STRUCTURE_CHANGED);

    std::ostringstream json;
    json << "{\"success\":true,\"id\":\"" << (column ? column->id.toString() : colId.toString())
         << "\"}";
    return json.str();
}

// ============================================================================
// Column/row move operations
// ============================================================================

std::string CellsEngine::shiftColumnsForEmptyMove(uint32_t sourcePos, uint32_t targetPos) {
    if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
        return "{\"error\":\"No sheet available\"}";
    }

    auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
    if (!sheet) {
        return "{\"error\":\"Sheet not found\"}";
    }

    if (sourcePos == targetPos || sourcePos + 1 == targetPos) {
        return "{\"success\":true}";
    }

    if (sourcePos > targetPos) {
        for (const ID& id : sheet->getColumnIds()) {
            Axis* col = sheet->getColumn(id);
            if (col != nullptr && col->position >= targetPos && col->position < sourcePos) {
                col->position++;
            }
        }
    } else {
        for (const ID& id : sheet->getColumnIds()) {
            Axis* col = sheet->getColumn(id);
            if (col != nullptr && col->position > sourcePos && col->position < targetPos) {
                col->position--;
            }
        }
    }

    rebuildViewportIndex();
    notifyListeners(ChangeType::STRUCTURE_CHANGED);
    return "{\"success\":true}";
}

std::string CellsEngine::shiftRowsForEmptyMove(uint32_t sourcePos, uint32_t targetPos) {
    if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
        return "{\"error\":\"No sheet available\"}";
    }

    auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
    if (!sheet) {
        return "{\"error\":\"Sheet not found\"}";
    }

    if (sourcePos == targetPos || sourcePos + 1 == targetPos) {
        return "{\"success\":true}";
    }

    if (sourcePos > targetPos) {
        for (const ID& id : sheet->getRowIds()) {
            Axis* row = sheet->getRow(id);
            if (row != nullptr && row->position >= targetPos && row->position < sourcePos) {
                row->position++;
            }
        }
    } else {
        for (const ID& id : sheet->getRowIds()) {
            Axis* row = sheet->getRow(id);
            if (row != nullptr && row->position > sourcePos && row->position < targetPos) {
                row->position--;
            }
        }
    }

    rebuildViewportIndex();
    notifyListeners(ChangeType::STRUCTURE_CHANGED);
    return "{\"success\":true}";
}

std::string CellsEngine::moveColumn(const std::string& colIdStr, uint32_t targetPos) {
    if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
        return "{\"error\":\"No sheet available\"}";
    }

    auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
    if (!sheet) {
        return "{\"error\":\"Sheet not found\"}";
    }

    if (colIdStr.size() != ID_LENGTH) {
        return "{\"error\":\"Invalid column ID\"}";
    }
    ID colId(colIdStr);

    Axis* colAxis = sheet->getColumn(colId);
    if (colAxis == nullptr) {
        return "{\"error\":\"Column not found\"}";
    }

    uint32_t currentPos = colAxis->position;

    if (targetPos == currentPos || targetPos == currentPos + 1) {
        return "{\"success\":true}";
    }

    // Calculate the final position for the moved column
    // When moving right, we insert before targetPos, so the column ends up at targetPos - 1
    // When moving left, we insert before targetPos, so the column ends up at targetPos
    uint32_t finalPos = (currentPos < targetPos) ? targetPos - 1 : targetPos;

    // Helper to build full-state column payload
    auto buildColPayload = [this](Axis* col, uint32_t newPos) {
        std::string p = "{\"pos\":" + std::to_string(newPos);
        if (col->sizeSet()) {
            p += ",\"size\":" + std::to_string(col->size);
        }
        if (!col->name.empty()) {
            p += ",\"name\":\"" + jsonEscape(col->name) + "\"";
        }
        if (col->hasStyle()) {
            const StyleBuffer* sty = _workbook->getEntityStyle(col->id);
            if (sty)
                p += ",\"sty\":\"" + sty->toBase64() + "\"";
        }
        if (col->hasFormat()) {
            const FormatBuffer* fmt = _workbook->getEntityFormat(col->id);
            if (fmt)
                p += ",\"fmt\":\"" + fmt->toBase64() + "\"";
        }
        if (col->hidden()) {
            p += ",\"hidden\":true";
        }
        p += "}";
        return p;
    };

    // Shift other columns to make room (using CRDT operations for sync)
    if (currentPos > targetPos) {
        // Moving left: shift columns in [targetPos, currentPos) right by 1
        for (const ID& id : sheet->getColumnIds()) {
            Axis* col = sheet->getColumn(id);
            if (col != nullptr && col->id != colId && col->position >= targetPos &&
                col->position < currentPos) {
                std::string shiftPayload = buildColPayload(col, col->position + 1);
                Operation shiftOp = makeColSetOp(*_workbook, col->id, shiftPayload);
                applyLocalUiOp(shiftOp);
            }
        }
    } else {
        // Moving right: shift columns in (currentPos, targetPos) left by 1
        for (const ID& id : sheet->getColumnIds()) {
            Axis* col = sheet->getColumn(id);
            if (col != nullptr && col->id != colId && col->position > currentPos &&
                col->position < targetPos) {
                std::string shiftPayload = buildColPayload(col, col->position - 1);
                Operation shiftOp = makeColSetOp(*_workbook, col->id, shiftPayload);
                applyLocalUiOp(shiftOp);
            }
        }
    }

    // Move the target column to its final position
    std::string payload = buildColPayload(colAxis, finalPos);
    Operation op = makeColSetOp(*_workbook, colId, payload);
    applyLocalUiOp(op);

    broadcastPendingOperations();
    rebuildViewportIndex();
    notifyListeners(ChangeType::STRUCTURE_CHANGED);

    return "{\"success\":true}";
}

std::string CellsEngine::moveRow(const std::string& rowIdStr, uint32_t targetPos) {
    if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
        return "{\"error\":\"No sheet available\"}";
    }

    auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
    if (!sheet) {
        return "{\"error\":\"Sheet not found\"}";
    }

    if (rowIdStr.size() != ID_LENGTH) {
        return "{\"error\":\"Invalid row ID\"}";
    }
    ID rowId(rowIdStr);

    Axis* rowAxis = sheet->getRow(rowId);
    if (rowAxis == nullptr) {
        return "{\"error\":\"Row not found\"}";
    }

    uint32_t currentPos = rowAxis->position;

    if (targetPos == currentPos || targetPos == currentPos + 1) {
        return "{\"success\":true}";
    }

    // Calculate the final position for the moved row
    // When moving down, we insert before targetPos, so the row ends up at targetPos - 1
    // When moving up, we insert before targetPos, so the row ends up at targetPos
    uint32_t finalPos = (currentPos < targetPos) ? targetPos - 1 : targetPos;

    // Helper to build full-state row payload
    auto buildRowPayload = [this](Axis* row, uint32_t newPos) {
        std::string p = "{\"pos\":" + std::to_string(newPos);
        if (row->sizeSet()) {
            p += ",\"size\":" + std::to_string(row->size);
        }
        if (row->hasStyle()) {
            const StyleBuffer* sty = _workbook->getEntityStyle(row->id);
            if (sty)
                p += ",\"sty\":\"" + sty->toBase64() + "\"";
        }
        if (row->hasFormat()) {
            const FormatBuffer* fmt = _workbook->getEntityFormat(row->id);
            if (fmt)
                p += ",\"fmt\":\"" + fmt->toBase64() + "\"";
        }
        if (row->hidden()) {
            p += ",\"hidden\":true";
        }
        p += "}";
        return p;
    };

    // Shift other rows to make room (using CRDT operations for sync)
    if (currentPos > targetPos) {
        // Moving up: shift rows in [targetPos, currentPos) down by 1
        for (const ID& id : sheet->getRowIds()) {
            Axis* row = sheet->getRow(id);
            if (row != nullptr && row->id != rowId && row->position >= targetPos &&
                row->position < currentPos) {
                std::string shiftPayload = buildRowPayload(row, row->position + 1);
                Operation shiftOp = makeRowSetOp(*_workbook, row->id, shiftPayload);
                applyLocalUiOp(shiftOp);
            }
        }
    } else {
        // Moving down: shift rows in (currentPos, targetPos) up by 1
        for (const ID& id : sheet->getRowIds()) {
            Axis* row = sheet->getRow(id);
            if (row != nullptr && row->id != rowId && row->position > currentPos &&
                row->position < targetPos) {
                std::string shiftPayload = buildRowPayload(row, row->position - 1);
                Operation shiftOp = makeRowSetOp(*_workbook, row->id, shiftPayload);
                applyLocalUiOp(shiftOp);
            }
        }
    }

    // Move the target row to its final position
    std::string payload = buildRowPayload(rowAxis, finalPos);
    Operation op = makeRowSetOp(*_workbook, rowId, payload);
    applyLocalUiOp(op);

    broadcastPendingOperations();
    rebuildViewportIndex();
    notifyListeners(ChangeType::STRUCTURE_CHANGED);

    return "{\"success\":true}";
}

// ============================================================================
// Column/row insert/delete operations
// ============================================================================

std::string CellsEngine::insertColumnAt(uint32_t position, bool insertBefore) {
    if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
        return "{\"error\":\"No sheet available\"}";
    }

    auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
    if (!sheet) {
        return "{\"error\":\"Sheet not found\"}";
    }

    uint32_t insertPos = insertBefore ? position : position + 1;

    Axis* newCol = sheet->insertColumnAt(insertPos);
    if (!newCol) {
        return "{\"error\":\"Failed to insert column\"}";
    }

    _viewportIndex.onAxisInserted(newCol->id, true, newCol->position, newCol->size);
    notifyListeners(ChangeType::STRUCTURE_CHANGED);

    return "{\"success\":true,\"id\":\"" + newCol->id.toString() +
           "\",\"position\":" + std::to_string(newCol->position) + "}";
}

std::string CellsEngine::insertRowAt(uint32_t position, bool insertBefore) {
    if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
        return "{\"error\":\"No sheet available\"}";
    }

    auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
    if (!sheet) {
        return "{\"error\":\"Sheet not found\"}";
    }

    uint32_t insertPos = insertBefore ? position : position + 1;

    Axis* newRow = sheet->insertRowAt(insertPos);
    if (!newRow) {
        return "{\"error\":\"Failed to insert row\"}";
    }

    _viewportIndex.onAxisInserted(newRow->id, false, newRow->position, newRow->size);
    notifyListeners(ChangeType::STRUCTURE_CHANGED);

    return "{\"success\":true,\"id\":\"" + newRow->id.toString() +
           "\",\"position\":" + std::to_string(newRow->position) + "}";
}

std::string CellsEngine::deleteColumnById(const std::string& colIdStr) {
    if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
        return "{\"error\":\"No sheet available\"}";
    }

    auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
    if (!sheet) {
        return "{\"error\":\"Sheet not found\"}";
    }

    if (colIdStr.size() != ID_LENGTH) {
        return "{\"error\":\"Invalid column ID\"}";
    }
    ID colId(colIdStr);

    // Verify column exists
    if (sheet->getColumn(colId) == nullptr) {
        return "{\"error\":\"Column not found\"}";
    }

    // Use CRDT operation to delete column (this triggers range adjustment)
    Operation op = makeColDeleteOp(*_workbook, colId);
    ApplyResult result = applyLocalUiOp(op);

    if (result != ApplyResult::SUCCESS && result != ApplyResult::ALREADY_APPLIED) {
        return "{\"error\":\"Failed to delete column\"}";
    }

    broadcastPendingOperations();
    _viewportIndex.onAxisDeleted(colId, true);
    notifyListeners(ChangeType::STRUCTURE_CHANGED);

    return "{\"success\":true}";
}

std::string CellsEngine::deleteRowById(const std::string& rowIdStr) {
    if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
        return "{\"error\":\"No sheet available\"}";
    }

    auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
    if (!sheet) {
        return "{\"error\":\"Sheet not found\"}";
    }

    if (rowIdStr.size() != ID_LENGTH) {
        return "{\"error\":\"Invalid row ID\"}";
    }
    ID rowId(rowIdStr);

    // Verify row exists
    if (sheet->getRow(rowId) == nullptr) {
        return "{\"error\":\"Row not found\"}";
    }

    // Use CRDT operation to delete row (this triggers range adjustment)
    Operation op = makeRowDeleteOp(*_workbook, rowId);
    ApplyResult result = applyLocalUiOp(op);

    if (result != ApplyResult::SUCCESS && result != ApplyResult::ALREADY_APPLIED) {
        return "{\"error\":\"Failed to delete row\"}";
    }

    broadcastPendingOperations();
    _viewportIndex.onAxisDeleted(rowId, false);
    notifyListeners(ChangeType::STRUCTURE_CHANGED);

    return "{\"success\":true}";
}

// ============================================================================
// Fill range
// ============================================================================

std::string CellsEngine::fillRange(int sourceMinCol, int sourceMinRow, int sourceMaxCol,
                                   int sourceMaxRow, int targetMinCol, int targetMinRow,
                                   int targetMaxCol, int targetMaxRow) {
    if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
        return "{\"error\":\"No sheet available\"}";
    }

    auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
    if (!sheet) {
        return "{\"error\":\"Sheet not found\"}";
    }

    FillResult result =
        cells::fillRange(_workbook.get(), sheet, sourceMinCol, sourceMinRow, sourceMaxCol,
                         sourceMaxRow, targetMinCol, targetMinRow, targetMaxCol, targetMaxRow);

    if (!result.success) {
        return "{\"error\":\"" + jsonEscape(result.error) + "\"}";
    }

    broadcastPendingOperations();
    rebuildViewportIndex();
    notifyListeners(ChangeType::CELL_CHANGED);

    return "{\"success\":true,\"cellsFilled\":" + std::to_string(result.cellsFilled) + "}";
}

// ============================================================================
// Merge cell operations
// ============================================================================

std::string CellsEngine::addMergeRange(uint32_t startCol, uint32_t startRow, uint32_t endCol,
                                       uint32_t endRow) {
    if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
        return "{\"error\":\"No sheet available\"}";
    }

    auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
    if (!sheet) {
        return "{\"error\":\"Sheet not found\"}";
    }

    // Normalize range (ensure start <= end)
    uint32_t minCol = std::min(startCol, endCol);
    uint32_t maxCol = std::max(startCol, endCol);
    uint32_t minRow = std::min(startRow, endRow);
    uint32_t maxRow = std::max(startRow, endRow);

    // Calculate span
    uint16_t colSpan = static_cast<uint16_t>(maxCol - minCol + 1);
    uint16_t rowSpan = static_cast<uint16_t>(maxRow - minRow + 1);

    // Need at least a 2-cell merge
    if (colSpan == 1 && rowSpan == 1) {
        return "{\"error\":\"Cannot merge a single cell\"}";
    }

    // Get or create the start column (anchor)
    ID startColId;
    Axis* startColAxis = sheet->getColumnByPosition(minCol);
    if (startColAxis != nullptr) {
        startColId = startColAxis->id;
    }
    if (startColId.isNull()) {
        startColId = generate_id();
        // Note: size is omitted to use local default (sizeSet=false)
        std::string colPayload = "{\"pos\":" + std::to_string(minCol) + "}";
        Operation colOp = makeColSetOp(*_workbook, startColId, colPayload);
        applyLocalUiOp(colOp);
    }

    // Get or create the start row (anchor)
    ID startRowId;
    Axis* startRowAxis = sheet->getRowByPosition(minRow);
    if (startRowAxis != nullptr) {
        startRowId = startRowAxis->id;
    }
    if (startRowId.isNull()) {
        startRowId = generate_id();
        // Note: size is omitted to use local default (sizeSet=false)
        std::string rowPayload = "{\"pos\":" + std::to_string(minRow) + "}";
        Operation rowOp = makeRowSetOp(*_workbook, startRowId, rowPayload);
        applyLocalUiOp(rowOp);
    }

    // Get or create the end column
    ID endColId;
    Axis* endColAxis = sheet->getColumnByPosition(maxCol);
    if (endColAxis != nullptr) {
        endColId = endColAxis->id;
    }
    if (endColId.isNull()) {
        endColId = generate_id();
        // Note: size is omitted to use local default (sizeSet=false)
        std::string colPayload = "{\"pos\":" + std::to_string(maxCol) + "}";
        Operation colOp = makeColSetOp(*_workbook, endColId, colPayload);
        applyLocalUiOp(colOp);
    }

    // Get or create the end row
    ID endRowId;
    Axis* endRowAxis = sheet->getRowByPosition(maxRow);
    if (endRowAxis != nullptr) {
        endRowId = endRowAxis->id;
    }
    if (endRowId.isNull()) {
        endRowId = generate_id();
        // Note: size is omitted to use local default (sizeSet=false)
        std::string rowPayload = "{\"pos\":" + std::to_string(maxRow) + "}";
        Operation rowOp = makeRowSetOp(*_workbook, endRowId, rowPayload);
        applyLocalUiOp(rowOp);
    }

    // Ensure all intermediate columns exist (for proper range expansion on insert)
    for (uint32_t c = minCol + 1; c < maxCol; c++) {
        Axis* colAxis = sheet->getColumnByPosition(c);
        if (colAxis == nullptr) {
            ID newColId = generate_id();
            // Note: size is omitted to use local default (sizeSet=false)
            std::string colPayload = "{\"pos\":" + std::to_string(c) + "}";
            Operation colOp = makeColSetOp(*_workbook, newColId, colPayload);
            applyLocalUiOp(colOp);
        }
    }

    // Ensure all intermediate rows exist
    for (uint32_t r = minRow + 1; r < maxRow; r++) {
        Axis* rowAxis = sheet->getRowByPosition(r);
        if (rowAxis == nullptr) {
            ID newRowId = generate_id();
            // Note: size is omitted to use local default (sizeSet=false)
            std::string rowPayload = "{\"pos\":" + std::to_string(r) + "}";
            Operation rowOp = makeRowSetOp(*_workbook, newRowId, rowPayload);
            applyLocalUiOp(rowOp);
        }
    }

    // Create the merge range using the unified Range system with CRDT operation
    ID rangeId = generate_id();
    std::ostringstream payload;
    payload << "{\"startCol\":\"" << startColId.toString() << "\",";
    payload << "\"startRow\":\"" << startRowId.toString() << "\",";
    payload << "\"endCol\":\"" << endColId.toString() << "\",";
    payload << "\"endRow\":\"" << endRowId.toString() << "\",";
    payload << "\"flags\":" << static_cast<int>(RangeFlags::MERGE) << "}";

    Operation rangeOp = makeRangeSetOp(*_workbook, rangeId, payload.str());
    applyLocalUiOp(rangeOp);

    broadcastPendingOperations();
    rebuildViewportIndex();
    notifyListeners(ChangeType::STRUCTURE_CHANGED);

    return "{\"success\":true,\"colSpan\":" + std::to_string(colSpan) +
           ",\"rowSpan\":" + std::to_string(rowSpan) + ",\"rangeId\":\"" + rangeId.toString() +
           "\"}";
}

std::string CellsEngine::removeMergeRange(uint32_t col, uint32_t row) {
    if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
        return "{\"error\":\"No sheet available\"}";
    }

    auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
    if (!sheet) {
        return "{\"error\":\"Sheet not found\"}";
    }

    // Find the column ID at this position
    ID colId;
    Axis* colAxis = sheet->getColumnByPosition(col);
    if (colAxis != nullptr) {
        colId = colAxis->id;
    }
    if (colId.isNull()) {
        return "{\"error\":\"Column not found\"}";
    }

    // Find the row ID at this position
    ID rowId;
    Axis* rowAxis = sheet->getRowByPosition(row);
    if (rowAxis != nullptr) {
        rowId = rowAxis->id;
    }
    if (rowId.isNull()) {
        return "{\"error\":\"Row not found\"}";
    }

    // Find merge ranges containing this cell using the unified Range system
    std::vector<Range*> mergeRanges = sheet->getRangesAt(col, row, RangeFlags::MERGE);

    if (mergeRanges.empty()) {
        return "{\"error\":\"Cell is not part of a merged region\"}";
    }

    // Remove the first merge range found (typically there should be only one)
    Range* mergeRange = mergeRanges[0];
    std::ostringstream payload;
    payload << "{\"sheet_id\":\"" << sheet->id.toString() << "\"}";

    Operation removeOp = makeRangeDeleteOp(*_workbook, mergeRange->id, payload.str());
    applyLocalUiOp(removeOp);

    broadcastPendingOperations();
    rebuildViewportIndex();
    notifyListeners(ChangeType::STRUCTURE_CHANGED);

    return "{\"success\":true}";
}

// ============================================================================
// Workbook name
// ============================================================================

std::string CellsEngine::getWorkbookName() {
    return _workbook ? _workbook->name : "";
}

void CellsEngine::setWorkbookName(const std::string& name) {
    Sheet* sheet = activeSheet();
    if (!_workbook || sheet == nullptr) {
        return;
    }
    const ScriptResult sr = uiSetDocumentTitle(_luauSandbox, *_workbook, *sheet, name);
    if (!sr.success) {
        return;
    }
    broadcastPendingOperations();
    notifyListeners(ChangeType::STRUCTURE_CHANGED);
}

// ============================================================================
// Theme
// ============================================================================

std::string CellsEngine::getTheme() {
    if (!_workbook || !_workbook->hasTheme()) {
        return "null";
    }

    const Theme* theme = _workbook->getTheme();
    std::ostringstream ss;
    ss << "{\"name\":\"" << jsonEscape(theme->name) << "\",\"colorScheme\":{\"colors\":[";
    for (int i = 0; i < 12; ++i) {
        if (i > 0) {
            ss << ",";
        }
        ss << "\"" << jsonEscape(theme->colorScheme.getColor(i)) << "\"";
    }
    ss << "]},\"fontScheme\":{\"majorFont\":\"" << jsonEscape(theme->fontScheme.majorFont)
       << "\",\"minorFont\":\"" << jsonEscape(theme->fontScheme.minorFont) << "\"}}";
    return ss.str();
}

std::string CellsEngine::getBuiltinThemes() {
    auto themes = cells::getBuiltinThemes();

    std::ostringstream ss;
    ss << "[";
    bool first = true;
    for (const auto& bt : themes) {
        if (!first) {
            ss << ",";
        }
        first = false;

        const auto& t = bt.theme;
        ss << "{\"name\":\"" << jsonEscape(bt.name) << "\",\"colorScheme\":{\"colors\":[";
        for (int i = 0; i < 12; ++i) {
            if (i > 0) {
                ss << ",";
            }
            ss << "\"" << jsonEscape(t.colorScheme.getColor(i)) << "\"";
        }
        ss << "]},\"fontScheme\":{\"majorFont\":\"" << jsonEscape(t.fontScheme.majorFont)
           << "\",\"minorFont\":\"" << jsonEscape(t.fontScheme.minorFont) << "\"}}";
    }
    ss << "]";
    return ss.str();
}

std::string CellsEngine::setTheme(const std::string& themeJson) {
    Sheet* sheet = activeSheet();
    if (!_workbook || sheet == nullptr) {
        return "{\"success\":false,\"error\":\"No workbook\"}";
    }
    const ScriptResult sr = uiSetTheme(_luauSandbox, *_workbook, *sheet, themeJson);
    if (!sr.success) {
        return "{\"success\":false,\"error\":\"" +
               jsonEscape(sr.error.empty() ? "Luau execution failed" : sr.error) + "\"}";
    }
    notifyListeners(ChangeType::CELL_CHANGED);
    return "{\"success\":true}";
}

// ============================================================================
// Named ranges
// ============================================================================

std::string CellsEngine::getNamedRanges() {
    if (!_workbook) {
        return "[]";
    }

    const NamedRangeRegistry* registry = _workbook->getNamedRanges();
    if (registry == nullptr) {
        return "[]";
    }

    std::vector<const NamedRange*> allRanges = registry->getAll();
    if (allRanges.empty()) {
        return "[]";
    }

    std::ostringstream json;
    json << "[";
    bool first = true;
    for (const NamedRange* nr : allRanges) {
        if (!first) {
            json << ",";
        }
        first = false;

        json << "{";
        json << "\"name\":\"" << jsonEscape(nr->name) << "\"";

        // Scope
        json << ",\"scope\":\"" << (nr->scope == NamedRangeScope::WORKBOOK ? "workbook" : "sheet")
             << "\"";
        if (nr->scope == NamedRangeScope::SHEET) {
            json << ",\"scopeSheetId\":\"" << nr->scopeSheetId.toString() << "\"";
        }

        // Target type
        const char* typeStr = "";
        switch (nr->target.type) {
            case NamedRangeTarget::Type::CELL:
                typeStr = "cell";
                break;
            case NamedRangeTarget::Type::RANGE:
                typeStr = "range";
                break;
            case NamedRangeTarget::Type::COLUMN:
                typeStr = "column";
                break;
            case NamedRangeTarget::Type::ROW:
                typeStr = "row";
                break;
            case NamedRangeTarget::Type::COLUMN_RANGE:
                typeStr = "column_range";
                break;
            case NamedRangeTarget::Type::ROW_RANGE:
                typeStr = "row_range";
                break;
        }
        json << ",\"targetType\":\"" << typeStr << "\"";

        // Target IDs
        json << ",\"id1\":\"" << nr->target.id1.toString() << "\"";
        if (!nr->target.id2.isNull()) {
            json << ",\"id2\":\"" << nr->target.id2.toString() << "\"";
        }
        if (!nr->target.sheetId.isNull()) {
            json << ",\"sheetId\":\"" << nr->target.sheetId.toString() << "\"";
        }

        json << "}";
    }
    json << "]";

    return json.str();
}

// ============================================================================
// Create empty workbook
// ============================================================================

void CellsEngine::createEmptyWorkbook() {
    // New document: tear down collab before replacing Workbook under SyncClient.
    disableSync();
    _workbook = std::make_unique<Workbook>(generate_id(), "Untitled");
    auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet1");
    _workbook->addSheet(std::move(sheet));
    setActiveSheetIndex(0);
    rebuildViewportIndex();
    notifyListeners(ChangeType::DATA_LOADED);
    LOG_INFO("Created empty workbook with id=%s", _workbook->id.toString().c_str());
}

}  // namespace cells::wasm
