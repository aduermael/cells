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

#include "apps/wasm/bindings.h"

#include <emscripten/bind.h>

#include <algorithm>
#include <iomanip>
#include <sstream>

#include "core/cells/crdt.h"
#include "core/cells/fill_range.h"
#include "core/cells/formula_recalc.h"
#include "core/cells/formula_serializer.h"
#include "core/cells/id.h"
#include "core/cells/input_parser.h"
#include "core/cells/named_ranges.h"
#include "core/cells/number_formatter.h"
#include "core/cells/operation.h"
#include "core/log/include/Logger.h"

namespace cells::wasm {

// ============================================================================
// Constructor / Destructor
// ============================================================================

CellsEngine::CellsEngine()
    : _workbook(nullptr),
      _activeSheetIndex(0),
      _listener(val::null()),
      _agentListener(val::null()) {}

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

void CellsEngine::setActiveSheet(int index) {
    if (_workbook && index >= 0 && static_cast<size_t>(index) < _workbook->sheetCount()) {
        _activeSheetIndex = static_cast<size_t>(index);
        rebuildViewportIndex();
        notifyListeners(ChangeType::SHEET_CHANGED);
    }
}

void CellsEngine::setFreezePanes(int freezeCol, int freezeRow) {
    Sheet* sheet = activeSheet();
    if (!sheet) return;

    sheet->freezeCol = static_cast<uint16_t>(std::max(0, freezeCol));
    sheet->freezeRow = static_cast<uint16_t>(std::max(0, freezeRow));
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
    Operation op = makeSheetCreateOp(*_workbook, sheetId, payload);
    applyOperation(*_workbook, op);

    if (_syncManager) {
        _syncManager->queueOperationsBroadcast();
        _syncManager->pruneOpLog();
    }

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
    applyOperation(*_workbook, op);

    if (_syncManager) {
        _syncManager->queueOperationsBroadcast();
        _syncManager->pruneOpLog();
    }

    if (_activeSheetIndex >= _workbook->sheetCount()) {
        _activeSheetIndex = _workbook->sheetCount() - 1;
    } else if (static_cast<size_t>(index) < _activeSheetIndex) {
        _activeSheetIndex--;
    } else if (static_cast<size_t>(index) == _activeSheetIndex) {
        if (_activeSheetIndex >= _workbook->sheetCount()) {
            _activeSheetIndex = _workbook->sheetCount() - 1;
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
    Operation op = makeSheetRenameOp(*_workbook, sheetId, payload);
    applyOperation(*_workbook, op);

    if (_syncManager) {
        _syncManager->queueOperationsBroadcast();
        _syncManager->pruneOpLog();
    }

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

    auto sheet = std::move(_workbook->sheets[fromIndex]);
    _workbook->sheets.erase(_workbook->sheets.begin() + fromIndex);

    int insertAt = toIndex > fromIndex ? toIndex - 1 : toIndex;
    _workbook->sheets.insert(_workbook->sheets.begin() + insertAt, std::move(sheet));

    if (static_cast<size_t>(fromIndex) == _activeSheetIndex) {
        _activeSheetIndex = insertAt;
    } else if (fromIndex < toIndex) {
        if (_activeSheetIndex > static_cast<size_t>(fromIndex) &&
            _activeSheetIndex <= static_cast<size_t>(insertAt)) {
            _activeSheetIndex--;
        }
    } else {
        if (_activeSheetIndex >= static_cast<size_t>(toIndex) &&
            _activeSheetIndex < static_cast<size_t>(fromIndex)) {
            _activeSheetIndex++;
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
    if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
        return "{\"error\":\"No sheet available\"}";
    }

    auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
    if (!sheet) {
        return "{\"error\":\"Sheet not found\"}";
    }

    if (cellIdStr.size() != ID_LENGTH) {
        return "{\"error\":\"Invalid cell ID\"}";
    }
    ID cellId(cellIdStr);

    Cell* cell = sheet->getCell(cellId);
    if (!cell) {
        return "{\"error\":\"Cell not found\"}";
    }

    char typeChar = 's';
    if (!value.empty() && value[0] == '=') {
        typeChar = 'f';
    } else if (value.empty()) {
        typeChar = 's';
    } else if (value == "TRUE" || value == "true") {
        typeChar = 'b';
    } else if (value == "FALSE" || value == "false") {
        typeChar = 'b';
    } else {
        char* endptr = nullptr;
        strtod(value.c_str(), &endptr);
        if (endptr != nullptr && *endptr == '\0' && endptr != value.c_str()) {
            typeChar = 'n';
        }
    }

    std::string colIdStr = cell->colId.toString();
    std::string rowIdStr = cell->rowId.toString();
    std::string idSuffix = ",\"col_id\":\"" + colIdStr + "\",\"row_id\":\"" + rowIdStr + "\"}";

    std::string payload;
    if (typeChar == 'f') {
        _refConverter.setContext(*sheet);
        std::string uuidFormula = _refConverter.formulaToUuid(value);
        payload = "{\"type\":\"f\",\"value\":\"" + jsonEscape(uuidFormula) + "\"" + idSuffix;
    } else if (typeChar == 'b') {
        payload = "{\"type\":\"b\",\"value\":\"" +
                  std::string(value == "TRUE" || value == "true" ? "true" : "false") + "\"" +
                  idSuffix;
    } else if (typeChar == 'n') {
        payload = "{\"type\":\"n\",\"value\":\"" + jsonEscape(value) + "\"" + idSuffix;
    } else {
        payload = "{\"type\":\"s\",\"value\":\"" + jsonEscape(value) + "\"" + idSuffix;
    }

    Operation op = makeCellSetValueOp(*_workbook, cellId, payload);
    applyOperation(*_workbook, op);

    if (_syncManager) {
        _syncManager->queueOperationsBroadcast();
        _syncManager->pruneOpLog();
    }

    markDirty(sheet, cellId);
    std::vector<ID> changed = {cellId};
    cells::recalculate(sheet, changed);
    cells::recalculateVolatile(sheet);

    notifyListeners(ChangeType::CELL_CHANGED);
    return "{\"success\":true}";
}

std::string CellsEngine::updateCellWithFormatDetection(const std::string& cellIdStr,
                                                        const std::string& value) {
    if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
        return "{\"error\":\"No sheet available\"}";
    }

    auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
    if (!sheet) {
        return "{\"error\":\"Sheet not found\"}";
    }

    if (cellIdStr.size() != ID_LENGTH) {
        return "{\"error\":\"Invalid cell ID\"}";
    }
    ID cellId(cellIdStr);

    Cell* cell = sheet->getCell(cellId);
    if (!cell) {
        return "{\"error\":\"Cell not found\"}";
    }

    std::string colIdStr = cell->colId.toString();
    std::string rowIdStr = cell->rowId.toString();
    std::string idSuffix = ",\"col_id\":\"" + colIdStr + "\",\"row_id\":\"" + rowIdStr + "\"}";

    std::string payload;
    ID detectedFormatId;

    if (!value.empty() && value[0] == '=') {
        _refConverter.setContext(*sheet);
        std::string uuidFormula = _refConverter.formulaToUuid(value);
        payload = "{\"type\":\"f\",\"value\":\"" + jsonEscape(uuidFormula) + "\"" + idSuffix;
    } else if (value == "TRUE" || value == "true") {
        payload = "{\"type\":\"b\",\"value\":\"true\"" + idSuffix;
    } else if (value == "FALSE" || value == "false") {
        payload = "{\"type\":\"b\",\"value\":\"false\"" + idSuffix;
    } else if (value.empty()) {
        payload = "{\"type\":\"s\",\"value\":\"\"" + idSuffix;
    } else {
        ParsedInput parsed = parseUserInput(value);

        if (parsed.success && parsed.valueType == CellValueType::NUMBER) {
            std::ostringstream numStr;
            numStr << std::setprecision(15) << parsed.numericValue;
            payload = "{\"type\":\"n\",\"value\":\"" + numStr.str() + "\"" + idSuffix;
            detectedFormatId = parsed.formatId;
        } else {
            payload = "{\"type\":\"s\",\"value\":\"" + jsonEscape(value) + "\"" + idSuffix;
        }
    }

    // Check if this cell's position is part of a spill range BEFORE updating
    // If so, the spill master will need to be recalculated after the update
    const ID spillMasterIdBeforeUpdate = sheet->getSpillMaster(cell->colId, cell->rowId);

    Operation op = makeCellSetValueOp(*_workbook, cellId, payload);
    applyOperation(*_workbook, op);

    if (!detectedFormatId.isNull() && detectedFormatId != cell->formatId) {
        std::string formatPayload = "{\"format_id\":\"" + detectedFormatId.toString() + "\"}";
        Operation formatOp = makeCellSetFormatOp(*_workbook, cellId, formatPayload);
        applyOperation(*_workbook, formatOp);
    }

    if (_syncManager) {
        _syncManager->queueOperationsBroadcast();
        _syncManager->pruneOpLog();
    }

    markDirty(sheet, cellId);
    std::vector<ID> changed = {cellId};

    // If this position was part of a spill range, recalculate the spill master
    // This will detect the blocking cell and show #SPILL! error on the master
    if (!spillMasterIdBeforeUpdate.isNull()) {
        markDirty(sheet, spillMasterIdBeforeUpdate);
        changed.push_back(spillMasterIdBeforeUpdate);
    }

    // Also recalculate all cells with #SPILL! error - the value change might
    // have removed a blocking condition, allowing spills to be restored
    for (const auto& [id, cellPtr] : sheet->cells) {
        if (cellPtr->value.type == CellValueType::FORMULA_ERROR &&
            cellPtr->value.error == CellError::SPILL) {
            markDirty(sheet, id);
            changed.push_back(id);
        }
    }

    cells::recalculate(sheet, changed);
    cells::recalculateVolatile(sheet);

    notifyListeners(ChangeType::CELL_CHANGED);

    std::string formatIdStr = detectedFormatId.isNull() ? "~" : detectedFormatId.toString();
    return "{\"success\":true,\"formatId\":\"" + formatIdStr + "\"}";
}

std::string CellsEngine::createCell(uint32_t col, uint32_t row, const std::string& value) {
    if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
        return "{\"error\":\"No sheet available\"}";
    }

    auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
    if (!sheet) {
        return "{\"error\":\"Sheet not found\"}";
    }

    ID colId;
    bool colCreated = false;
    for (const auto& [id, axis] : sheet->columns) {
        if (axis->position == col) {
            colId = id;
            break;
        }
    }
    if (colId.isNull()) {
        colId = generate_id();
        colCreated = true;
        std::string colPayload = "{\"pos\":" + std::to_string(col) +
                                 ",\"size\":" + std::to_string(DEFAULT_COLUMN_WIDTH) + "}";
        Operation colOp = makeColInsertOp(*_workbook, colId, colPayload);
        applyOperation(*_workbook, colOp);
    }

    ID rowId;
    bool rowCreated = false;
    for (const auto& [id, axis] : sheet->rows) {
        if (axis->position == row) {
            rowId = id;
            break;
        }
    }
    if (rowId.isNull()) {
        rowId = generate_id();
        rowCreated = true;
        std::string rowPayload = "{\"pos\":" + std::to_string(row) +
                                 ",\"size\":" + std::to_string(DEFAULT_ROW_HEIGHT) + "}";
        Operation rowOp = makeRowInsertOp(*_workbook, rowId, rowPayload);
        applyOperation(*_workbook, rowOp);
    }

    // Check if this position is part of a spill range BEFORE creating the cell
    // If so, the spill master will need to be recalculated after the cell is created
    const ID spillMasterIdBeforeCreate = sheet->getSpillMaster(colId, rowId);

    ID cellId = generate_id();
    std::string idSuffix =
        ",\"col_id\":\"" + colId.toString() + "\",\"row_id\":\"" + rowId.toString() + "\"}";

    std::string payload;
    if (!value.empty() && value[0] == '=') {
        _refConverter.setContext(*sheet);
        std::string uuidFormula = _refConverter.formulaToUuid(value);
        payload = "{\"type\":\"f\",\"value\":\"" + jsonEscape(uuidFormula) + "\"" + idSuffix;
    } else if (value == "TRUE" || value == "true") {
        payload = "{\"type\":\"b\",\"value\":\"true\"" + idSuffix;
    } else if (value == "FALSE" || value == "false") {
        payload = "{\"type\":\"b\",\"value\":\"false\"" + idSuffix;
    } else if (!value.empty()) {
        char* endptr = nullptr;
        strtod(value.c_str(), &endptr);
        if (endptr != nullptr && *endptr == '\0' && endptr != value.c_str()) {
            payload = "{\"type\":\"n\",\"value\":\"" + jsonEscape(value) + "\"" + idSuffix;
        } else {
            payload = "{\"type\":\"s\",\"value\":\"" + jsonEscape(value) + "\"" + idSuffix;
        }
    } else {
        payload = "{\"type\":\"s\",\"value\":\"\"" + idSuffix;
    }

    Operation op = makeCellSetValueOp(*_workbook, cellId, payload);
    applyOperation(*_workbook, op);

    if (_syncManager) {
        _syncManager->pruneOpLog();
    }

    if (colCreated) {
        _viewportIndex.onAxisInserted(colId, true, col, DEFAULT_COLUMN_WIDTH);
    }
    if (rowCreated) {
        _viewportIndex.onAxisInserted(rowId, false, row, DEFAULT_ROW_HEIGHT);
    }
    Cell* newCell = sheet->getCell(cellId);
    if (newCell) {
        _viewportIndex.onCellAdded(newCell);
    }

    markDirty(sheet, cellId);
    std::vector<ID> changed = {cellId};

    // If this position was part of a spill range, recalculate the spill master
    // This will detect the blocking cell and show #SPILL! error on the master
    if (!spillMasterIdBeforeCreate.isNull()) {
        markDirty(sheet, spillMasterIdBeforeCreate);
        changed.push_back(spillMasterIdBeforeCreate);
    }

    cells::recalculate(sheet, changed);
    cells::recalculateVolatile(sheet);

    notifyListeners(ChangeType::CELL_CHANGED);

    std::ostringstream json;
    json << "{\"success\":true,\"id\":\"" << cellId.toString() << "\"}";
    return json.str();
}

std::string CellsEngine::getOrCreateCellAt(uint32_t col, uint32_t row) {
    if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
        return "{\"error\":\"No sheet available\"}";
    }

    auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
    if (!sheet) {
        return "{\"error\":\"Sheet not found\"}";
    }

    ID colId;
    bool colCreated = false;
    for (const auto& [id, axis] : sheet->columns) {
        if (axis->position == col) {
            colId = id;
            break;
        }
    }
    if (colId.isNull()) {
        colId = generate_id();
        colCreated = true;
        std::string colPayload = "{\"pos\":" + std::to_string(col) +
                                 ",\"size\":" + std::to_string(DEFAULT_COLUMN_WIDTH) + "}";
        Operation colOp = makeColInsertOp(*_workbook, colId, colPayload);
        applyOperation(*_workbook, colOp);
    }

    ID rowId;
    bool rowCreated = false;
    for (const auto& [id, axis] : sheet->rows) {
        if (axis->position == row) {
            rowId = id;
            break;
        }
    }
    if (rowId.isNull()) {
        rowId = generate_id();
        rowCreated = true;
        std::string rowPayload = "{\"pos\":" + std::to_string(row) +
                                 ",\"size\":" + std::to_string(DEFAULT_ROW_HEIGHT) + "}";
        Operation rowOp = makeRowInsertOp(*_workbook, rowId, rowPayload);
        applyOperation(*_workbook, rowOp);
    }

    for (const auto& [id, cell] : sheet->cells) {
        if (cell->colId == colId && cell->rowId == rowId) {
            if (_syncManager) {
                _syncManager->pruneOpLog();
            }

            if (colCreated) {
                _viewportIndex.onAxisInserted(colId, true, col, DEFAULT_COLUMN_WIDTH);
            }
            if (rowCreated) {
                _viewportIndex.onAxisInserted(rowId, false, row, DEFAULT_ROW_HEIGHT);
            }

            std::ostringstream json;
            json << "{\"success\":true,\"id\":\"" << id.toString() << "\",\"existed\":true,";

            // Compute editValue for formatted numbers (dates, percentages, etc.)
            std::string editValue = cell->value.raw;
            if (cell->value.type == CellValueType::NUMBER && !cell->formatId.isNull()) {
                double numValue = cell->value.asNumber();
                editValue = formatEditValue(_formatRegistry, numValue, cell->formatId);
            }

            if (cell->isFormula()) {
                Formula* formula = cell->getFormula();
                if (formula != nullptr && formula->ast != nullptr) {
                    const std::string uuidFormula = FormulaSerializer::serialize(formula->ast);
                    std::string a1Formula = _refConverter.formulaToA1(uuidFormula);
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
    }

    ID cellId = generate_id();
    std::string payload = "{\"type\":\"s\",\"value\":\"\",\"col_id\":\"" + colId.toString() +
                          "\",\"row_id\":\"" + rowId.toString() + "\"}";

    Operation op = makeCellSetValueOp(*_workbook, cellId, payload);
    applyOperation(*_workbook, op);

    if (_syncManager) {
        _syncManager->pruneOpLog();
    }

    if (colCreated) {
        _viewportIndex.onAxisInserted(colId, true, col, DEFAULT_COLUMN_WIDTH);
    }
    if (rowCreated) {
        _viewportIndex.onAxisInserted(rowId, false, row, DEFAULT_ROW_HEIGHT);
    }
    Cell* newCell = sheet->getCell(cellId);
    if (newCell) {
        _viewportIndex.onCellAdded(newCell);
    }
    notifyListeners(ChangeType::CELL_CHANGED);

    std::ostringstream json;
    json << "{\"success\":true,\"id\":\"" << cellId.toString()
         << "\",\"existed\":false,\"value\":\"\",\"editValue\":\"\"}";
    return json.str();
}

std::string CellsEngine::deleteCell(const std::string& cellIdStr) {
    if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
        return "{\"error\":\"No sheet available\"}";
    }

    auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
    if (!sheet) {
        return "{\"error\":\"Sheet not found\"}";
    }

    if (cellIdStr.size() != ID_LENGTH) {
        return "{\"error\":\"Invalid cell ID\"}";
    }
    ID cellId(cellIdStr);

    auto it = sheet->cells.find(cellId);
    if (it == sheet->cells.end()) {
        return "{\"error\":\"Cell not found\"}";
    }

    const ID colId = it->second->colId;
    const ID rowId = it->second->rowId;

    Operation op = makeCellClearOp(*_workbook, cellId);
    applyOperation(*_workbook, op);

    if (_syncManager) {
        _syncManager->queueOperationsBroadcast();
        _syncManager->pruneOpLog();
    }

    _viewportIndex.onCellRemoved(colId, rowId);

    // After deleting a cell, recalculate all cells with #SPILL! error
    // This allows blocked spills to be restored when the blocking cell is removed
    std::vector<ID> spillErrorCells;
    for (const auto& [id, cellPtr] : sheet->cells) {
        if (cellPtr->value.type == CellValueType::FORMULA_ERROR &&
            cellPtr->value.error == CellError::SPILL) {
            spillErrorCells.push_back(id);
            markDirty(sheet, id);
        }
    }
    if (!spillErrorCells.empty()) {
        cells::recalculate(sheet, spillErrorCells);
    }

    notifyListeners(ChangeType::CELL_CHANGED);

    return "{\"success\":true}";
}

std::string CellsEngine::deleteCellAt(uint32_t col, uint32_t row) {
    if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
        return "{\"error\":\"No sheet available\"}";
    }

    auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
    if (!sheet) {
        return "{\"error\":\"Sheet not found\"}";
    }

    ID colId;
    for (const auto& [id, axis] : sheet->columns) {
        if (axis->position == col) {
            colId = id;
            break;
        }
    }
    if (colId.isNull()) {
        return "{\"success\":true,\"deleted\":false}";
    }

    ID rowId;
    for (const auto& [id, axis] : sheet->rows) {
        if (axis->position == row) {
            rowId = id;
            break;
        }
    }
    if (rowId.isNull()) {
        return "{\"success\":true,\"deleted\":false}";
    }

    for (const auto& [id, cell] : sheet->cells) {
        if (cell->colId == colId && cell->rowId == rowId) {
            Operation op = makeCellClearOp(*_workbook, id);
            applyOperation(*_workbook, op);

            if (_syncManager) {
                _syncManager->queueOperationsBroadcast();
                _syncManager->pruneOpLog();
            }

            _viewportIndex.onCellRemoved(colId, rowId);

            // After deleting a cell, recalculate all cells with #SPILL! error
            // This allows blocked spills to be restored when the blocking cell is removed
            std::vector<ID> spillErrorCells;
            for (const auto& [cellId, cellPtr] : sheet->cells) {
                if (cellPtr->value.type == CellValueType::FORMULA_ERROR &&
                    cellPtr->value.error == CellError::SPILL) {
                    spillErrorCells.push_back(cellId);
                    markDirty(sheet, cellId);
                }
            }
            if (!spillErrorCells.empty()) {
                cells::recalculate(sheet, spillErrorCells);
            }

            notifyListeners(ChangeType::CELL_CHANGED);
            return "{\"success\":true,\"deleted\":true}";
        }
    }

    return "{\"success\":true,\"deleted\":false}";
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

    auto it = sheet->columns.find(colId);
    if (it == sheet->columns.end()) {
        return "{\"error\":\"Column not found\"}";
    }

    if (width < 20) width = 20;
    if (width > 1000) width = 1000;

    std::string payload = "{\"size\":" + std::to_string(width) + "}";
    Operation op = makeColResizeOp(*_workbook, colId, payload);
    applyOperation(*_workbook, op);

    if (_syncManager) {
        _syncManager->pruneOpLog();
    }

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

    if (width < 20) width = 20;
    if (width > 1000) width = 1000;

    Axis* column = nullptr;
    ID colId;
    for (auto& [id, col] : sheet->columns) {
        if (col->position == pos) {
            column = col.get();
            colId = id;
            break;
        }
    }

    bool colCreated = false;
    if (!column) {
        colId = generate_id();
        colCreated = true;
        std::string insertPayload =
            "{\"pos\":" + std::to_string(pos) + ",\"size\":" + std::to_string(width) + "}";
        Operation insertOp = makeColInsertOp(*_workbook, colId, insertPayload);
        applyOperation(*_workbook, insertOp);
        column = sheet->getColumn(colId);
    } else {
        std::string resizePayload = "{\"size\":" + std::to_string(width) + "}";
        Operation resizeOp = makeColResizeOp(*_workbook, colId, resizePayload);
        applyOperation(*_workbook, resizeOp);
    }

    if (_syncManager) {
        _syncManager->pruneOpLog();
    }

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

    auto it = sheet->rows.find(rowId);
    if (it == sheet->rows.end()) {
        return "{\"error\":\"Row not found\"}";
    }

    if (height < 10) height = 10;
    if (height > 500) height = 500;

    std::string payload = "{\"size\":" + std::to_string(height) + "}";
    Operation op = makeRowResizeOp(*_workbook, rowId, payload);
    applyOperation(*_workbook, op);

    if (_syncManager) {
        _syncManager->pruneOpLog();
    }

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

    if (height < 10) height = 10;
    if (height > 500) height = 500;

    Axis* row = nullptr;
    ID rowId;
    for (auto& [id, r] : sheet->rows) {
        if (r->position == pos) {
            row = r.get();
            rowId = id;
            break;
        }
    }

    bool rowCreated = false;
    if (!row) {
        rowId = generate_id();
        rowCreated = true;
        std::string insertPayload =
            "{\"pos\":" + std::to_string(pos) + ",\"size\":" + std::to_string(height) + "}";
        Operation insertOp = makeRowInsertOp(*_workbook, rowId, insertPayload);
        applyOperation(*_workbook, insertOp);
        row = sheet->getRow(rowId);
    } else {
        std::string resizePayload = "{\"size\":" + std::to_string(height) + "}";
        Operation resizeOp = makeRowResizeOp(*_workbook, rowId, resizePayload);
        applyOperation(*_workbook, resizeOp);
    }

    if (_syncManager) {
        _syncManager->pruneOpLog();
    }

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

    auto it = sheet->columns.find(colId);
    if (it == sheet->columns.end()) {
        return "{\"error\":\"Column not found\"}";
    }

    std::string payload = "{\"name\":\"" + jsonEscape(name) + "\"}";
    Operation op = makeColRenameOp(*_workbook, colId, payload);
    applyOperation(*_workbook, op);

    if (_syncManager) {
        _syncManager->pruneOpLog();
    }

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

    Axis* column = nullptr;
    ID colId;
    for (auto& [id, col] : sheet->columns) {
        if (col->position == pos) {
            column = col.get();
            colId = id;
            break;
        }
    }

    if (!column) {
        colId = generate_id();
        std::string insertPayload = "{\"pos\":" + std::to_string(pos) +
                                    ",\"size\":" + std::to_string(DEFAULT_COLUMN_WIDTH) + "}";
        Operation insertOp = makeColInsertOp(*_workbook, colId, insertPayload);
        applyOperation(*_workbook, insertOp);

        std::string renamePayload = "{\"name\":\"" + jsonEscape(name) + "\"}";
        Operation renameOp = makeColRenameOp(*_workbook, colId, renamePayload);
        applyOperation(*_workbook, renameOp);

        column = sheet->getColumn(colId);
    } else {
        std::string payload = "{\"name\":\"" + jsonEscape(name) + "\"}";
        Operation op = makeColRenameOp(*_workbook, colId, payload);
        applyOperation(*_workbook, op);
    }

    if (_syncManager) {
        _syncManager->pruneOpLog();
    }

    notifyListeners(ChangeType::STRUCTURE_CHANGED);

    std::ostringstream json;
    json << "{\"success\":true,\"id\":\""
         << (column ? column->id.toString() : colId.toString()) << "\"}";
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
        for (auto& [id, col] : sheet->columns) {
            if (col->position >= targetPos && col->position < sourcePos) {
                col->position++;
            }
        }
    } else {
        for (auto& [id, col] : sheet->columns) {
            if (col->position > sourcePos && col->position < targetPos) {
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
        for (auto& [id, row] : sheet->rows) {
            if (row->position >= targetPos && row->position < sourcePos) {
                row->position++;
            }
        }
    } else {
        for (auto& [id, row] : sheet->rows) {
            if (row->position > sourcePos && row->position < targetPos) {
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

    auto it = sheet->columns.find(colId);
    if (it == sheet->columns.end()) {
        return "{\"error\":\"Column not found\"}";
    }

    uint32_t currentPos = it->second->position;

    if (targetPos == currentPos || targetPos == currentPos + 1) {
        return "{\"success\":true}";
    }

    std::string payload = "{\"targetPos\":" + std::to_string(targetPos) + "}";
    Operation op = makeColMoveOp(*_workbook, colId, payload);
    applyOperation(*_workbook, op);

    if (_syncManager) {
        _syncManager->pruneOpLog();
    }

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

    auto it = sheet->rows.find(rowId);
    if (it == sheet->rows.end()) {
        return "{\"error\":\"Row not found\"}";
    }

    uint32_t currentPos = it->second->position;

    if (targetPos == currentPos || targetPos == currentPos + 1) {
        return "{\"success\":true}";
    }

    std::string payload = "{\"targetPos\":" + std::to_string(targetPos) + "}";
    Operation op = makeRowMoveOp(*_workbook, rowId, payload);
    applyOperation(*_workbook, op);

    if (_syncManager) {
        _syncManager->pruneOpLog();
    }

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

    _viewportIndex.onAxisDeleted(colId, true);

    if (!sheet->deleteColumn(colId)) {
        return "{\"error\":\"Column not found\"}";
    }

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

    _viewportIndex.onAxisDeleted(rowId, false);

    if (!sheet->deleteRow(rowId)) {
        return "{\"error\":\"Row not found\"}";
    }

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

    rebuildViewportIndex();
    notifyListeners(ChangeType::CELL_CHANGED);

    return "{\"success\":true,\"cellsFilled\":" + std::to_string(result.cellsFilled) + "}";
}

// ============================================================================
// Workbook name
// ============================================================================

std::string CellsEngine::getWorkbookName() {
    return _workbook ? _workbook->name : "";
}

void CellsEngine::setWorkbookName(const std::string& name) {
    if (!_workbook) {
        return;
    }

    _workbook->name = name;

    std::ostringstream payload;
    payload << "{\"name\":\"" << jsonEscape(name) << "\"}";
    Operation op = makeWorkbookRenameOp(*_workbook, payload.str());
    applyOperation(*_workbook, op);

    notifyListeners(ChangeType::STRUCTURE_CHANGED);
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
        json << ",\"scope\":\"" << (nr->scope == NamedRangeScope::WORKBOOK ? "workbook" : "sheet") << "\"";
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
    _workbook = std::make_unique<Workbook>(generate_id(), "Untitled");
    auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet1");
    _workbook->addSheet(std::move(sheet));
    _activeSheetIndex = 0;
    rebuildViewportIndex();
    notifyListeners(ChangeType::DATA_LOADED);
    LOG_INFO("Created empty workbook with id=%s", _workbook->id.toString().c_str());
}

}  // namespace cells::wasm
