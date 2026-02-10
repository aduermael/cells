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

#include "core/cells/builtin_themes.h"
#include "core/cells/crdt.h"
#include "core/cells/fill_range.h"
#include "core/cells/format_buffer.h"
#include "core/cells/format_code_formatter.h"
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
#include "core/log/include/Logger.h"

namespace cells::wasm {

// =============================================================================
// Helper: Compute Edit Value for Formula Bar
// =============================================================================
// Matches the helper in bindings_viewport.cc. See that file for details.
// CURRENCY/ACCOUNTING shows raw number, PERCENTAGE/DATE shows formatted value.

static std::string computeEditValueForCore(double num, const std::string& displayValue, const FormatBuffer& format) {
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
    Operation op = makeSheetSetOp(*_workbook, sheetId, payload);
    applyOperation(*_workbook, op);

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
    applyOperation(*_workbook, op);

    broadcastPendingOperations();

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
    Operation op = makeSheetSetOp(*_workbook, sheetId, payload);
    applyOperation(*_workbook, op);

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

    // Build full-state payload for resurrection correctness
    // Include style/format so operations are self-sufficient when arriving out of order
    std::string payload;
    if (typeChar == 'f') {
        _refConverter.setContext(*sheet);
        std::string uuidFormula = _refConverter.formulaToUuid(value);
        payload = "{\"t\":\"f\",\"v\":\"" + jsonEscape(uuidFormula) + "\"";
    } else if (typeChar == 'b') {
        payload = "{\"t\":\"b\",\"v\":\"" +
                  std::string(value == "TRUE" || value == "true" ? "true" : "false") + "\"";
    } else if (typeChar == 'n') {
        payload = "{\"t\":\"n\",\"v\":\"" + jsonEscape(value) + "\"";
    } else {
        payload = "{\"t\":\"s\",\"v\":\"" + jsonEscape(value) + "\"";
    }

    // Add existing style if present (full-state for resurrection)
    if (cell->hasStyle()) {
        const StyleBuffer* sty = _workbook->getEntityStyle(cell->id);
        if (sty) {
            payload += ",\"sty\":\"" + sty->toBase64() + "\"";
        }
    }

    // Add existing format if present (full-state for resurrection)
    if (cell->hasFormat()) {
        const FormatBuffer* fmt = _workbook->getEntityFormat(cell->id);
        if (fmt) {
            payload += ",\"fmt\":\"" + fmt->toBase64() + "\"";
        }
    }

    payload += ",\"col\":\"" + colIdStr + "\",\"row\":\"" + rowIdStr + "\"}";

    Operation op = makeCellSetOp(*_workbook, cellId, payload);
    applyOperation(*_workbook, op);

    broadcastPendingOperations();

    markDirty(sheet, cellId);
    std::vector<ID> changed = {cellId};
    cells::recalculate(_workbook.get(), changed);
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

    // Build full-state payload for resurrection correctness
    // Include style/format so operations are self-sufficient when arriving out of order
    std::string payload;
    ID detectedFormatId;

    if (!value.empty() && value[0] == '=') {
        // Use AST-based conversion to properly handle cross-sheet references
        FormulaParser parser(value);
        auto ast = parser.parse();

        if (ast && ast->type != ASTNodeType::ERROR_NODE) {
            FormulaResolver resolver(*_workbook, *sheet, _workbook->getNamedRanges());

            // CRDT-compliant resolution: discover and create entities first
            RequiredEntities required = resolver.getRequiredEntities(ast.get());

            // Create required columns via CRDT operations
            // Note: size is omitted to use local default (sizeSet=false)
            for (const auto& pending : required.columns) {
                std::string colPayload = "{\"pos\":" + std::to_string(pending.position) + "}";
                Operation colOp = makeColSetOp(*_workbook, pending.id, pending.sheetId, colPayload);
                applyOperation(*_workbook, colOp);
            }

            // Create required rows via CRDT operations
            // Note: size is omitted to use local default (sizeSet=false)
            for (const auto& pending : required.rows) {
                std::string rowPayload = "{\"pos\":" + std::to_string(pending.position) + "}";
                Operation rowOp = makeRowSetOp(*_workbook, pending.id, pending.sheetId, rowPayload);
                applyOperation(*_workbook, rowOp);
            }

            // Create required cells via CRDT operations (empty cells for references)
            for (const auto& pending : required.cells) {
                std::string cellPayload = "{\"t\":\"s\",\"v\":\"\",\"col\":\"" +
                                          pending.colId.toString() + "\",\"row\":\"" +
                                          pending.rowId.toString() + "\"}";
                Operation cellOp = makeCellSetOp(*_workbook, pending.id, pending.sheetId, cellPayload);
                applyOperation(*_workbook, cellOp);
            }

            // Now resolve (all entities should exist)
            ResolveResult resolveResult = resolver.resolve(ast.get());

            if (resolveResult.success) {
                // Serialize AST to UUID format for storage
                std::string uuidFormula = FormulaSerializer::serialize(ast.get());
                payload = "{\"t\":\"f\",\"v\":\"" + jsonEscape(uuidFormula) + "\"";
            } else {
                // Resolution failed (e.g., sheet not found) - store as error formula
                // Fall back to string-based conversion for the formula text
                _refConverter.setContext(*sheet);
                std::string uuidFormula = _refConverter.formulaToUuid(value);
                payload = "{\"t\":\"f\",\"v\":\"" + jsonEscape(uuidFormula) + "\"";
            }
        } else {
            // Parse failed - store original formula text (will show as error)
            _refConverter.setContext(*sheet);
            std::string uuidFormula = _refConverter.formulaToUuid(value);
            payload = "{\"t\":\"f\",\"v\":\"" + jsonEscape(uuidFormula) + "\"";
        }
    } else if (value == "TRUE" || value == "true") {
        payload = "{\"t\":\"b\",\"v\":\"true\"";
    } else if (value == "FALSE" || value == "false") {
        payload = "{\"t\":\"b\",\"v\":\"false\"";
    } else if (value.empty()) {
        payload = "{\"t\":\"s\",\"v\":\"\"";
    } else {
        ParsedInput parsed = parseUserInput(value);

        if (parsed.success && parsed.valueType == CellValueType::NUMBER) {
            std::ostringstream numStr;
            numStr << std::setprecision(15) << parsed.numericValue;
            payload = "{\"t\":\"n\",\"v\":\"" + numStr.str() + "\"";
            detectedFormatId = parsed.formatId;
        } else {
            payload = "{\"t\":\"s\",\"v\":\"" + jsonEscape(value) + "\"";
        }
    }

    // Add existing style if present (full-state for resurrection)
    if (cell->hasStyle()) {
        const StyleBuffer* sty = _workbook->getEntityStyle(cell->id);
        if (sty) {
            payload += ",\"sty\":\"" + sty->toBase64() + "\"";
        }
    }

    // Add existing format if present (full-state for resurrection)
    if (cell->hasFormat()) {
        const FormatBuffer* fmt = _workbook->getEntityFormat(cell->id);
        if (fmt) {
            payload += ",\"fmt\":\"" + fmt->toBase64() + "\"";
        }
    }

    payload += ",\"col\":\"" + colIdStr + "\",\"row\":\"" + rowIdStr + "\"}";

    // Check if this cell's position is part of a spill range BEFORE updating
    // If so, the spill master will need to be recalculated after the update
    const ID spillMasterIdBeforeUpdate = sheet->getSpillMaster(cell->colId, cell->rowId);

    Operation op = makeCellSetOp(*_workbook, cellId, sheet->id, payload);
    applyOperation(*_workbook, op);

    // Convert detected format ID to FormatBuffer
    std::string formatBase64;
    if (!detectedFormatId.isNull()) {
        // Check if cell already has the same format
        const FormatBuffer* existingFormat = _workbook->getEntityFormat(cell->id);

        // Parse the format ID to get properties
        ParsedFormatId parsed = parseFormatId(detectedFormatId.toString());
        if (parsed.valid) {
            FormatBuffer newFormat;
            newFormat.setCategory(parsed.category);
            if (parsed.decimalPlaces > 0) {
                newFormat.setDecimals(parsed.decimalPlaces);
            }
            if (parsed.useThousandsSeparator) {
                newFormat.setThousandsSeparator(true);
            }
            if (!parsed.currencyCode.empty()) {
                newFormat.setCurrencySymbol(getCurrencySymbol(parsed.currencyCode));
            }

            // Only apply if different from existing format
            if (existingFormat == nullptr || *existingFormat != newFormat) {
                Operation formatOp = makeCellSetFormatOp(*_workbook, cellId, newFormat);
                applyOperation(*_workbook, formatOp);
                formatBase64 = newFormat.toBase64();
            } else if (existingFormat != nullptr) {
                formatBase64 = existingFormat->toBase64();
            }
        }
    }

    broadcastPendingOperations();

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
    for (const auto& cellId : sheet->getCellIds()) {
        Cell* cellPtr = _workbook->getCell(cellId);
        if (cellPtr && cellPtr->value.type == CellValueType::FORMULA_ERROR &&
            cellPtr->value.error == CellError::SPILL) {
            markDirty(sheet, cellId);
            changed.push_back(cellId);
        }
    }

    cells::recalculate(_workbook.get(), changed);
    cells::recalculateVolatile(sheet);

    notifyListeners(ChangeType::CELL_CHANGED);

    // Return format as base64 instead of format ID
    return "{\"success\":true,\"format\":\"" + formatBase64 + "\"}";
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
    Axis* colAxis = sheet->getColumnByPosition(col);
    if (colAxis != nullptr) {
        colId = colAxis->id;
    }
    if (colId.isNull()) {
        colId = generate_id();
        colCreated = true;
        // Note: size is omitted to use local default (sizeSet=false)
        std::string colPayload = "{\"pos\":" + std::to_string(col) + "}";
        Operation colOp = makeColSetOp(*_workbook, colId, sheet->id, colPayload);
        applyOperation(*_workbook, colOp);
    }

    ID rowId;
    bool rowCreated = false;
    Axis* rowAxis = sheet->getRowByPosition(row);
    if (rowAxis != nullptr) {
        rowId = rowAxis->id;
    }
    if (rowId.isNull()) {
        rowId = generate_id();
        rowCreated = true;
        // Note: size is omitted to use local default (sizeSet=false)
        std::string rowPayload = "{\"pos\":" + std::to_string(row) + "}";
        Operation rowOp = makeRowSetOp(*_workbook, rowId, sheet->id, rowPayload);
        applyOperation(*_workbook, rowOp);
    }

    // Check if this position is part of a spill range BEFORE creating the cell
    // If so, the spill master will need to be recalculated after the cell is created
    const ID spillMasterIdBeforeCreate = sheet->getSpillMaster(colId, rowId);

    ID cellId = generate_id();
    std::string idSuffix =
        ",\"col\":\"" + colId.toString() + "\",\"row\":\"" + rowId.toString() + "\"}";

    std::string payload;
    if (!value.empty() && value[0] == '=') {
        // Use AST-based conversion to properly handle cross-sheet references
        FormulaParser parser(value);
        auto ast = parser.parse();

        if (ast && ast->type != ASTNodeType::ERROR_NODE) {
            FormulaResolver resolver(*_workbook, *sheet, _workbook->getNamedRanges());

            // CRDT-compliant resolution: discover and create entities first
            RequiredEntities required = resolver.getRequiredEntities(ast.get());

            // Create required columns via CRDT operations
            // Note: size is omitted to use local default (sizeSet=false)
            for (const auto& pending : required.columns) {
                std::string colPayload = "{\"pos\":" + std::to_string(pending.position) + "}";
                Operation colOp = makeColSetOp(*_workbook, pending.id, pending.sheetId, colPayload);
                applyOperation(*_workbook, colOp);
            }

            // Create required rows via CRDT operations
            // Note: size is omitted to use local default (sizeSet=false)
            for (const auto& pending : required.rows) {
                std::string rowPayload = "{\"pos\":" + std::to_string(pending.position) + "}";
                Operation rowOp = makeRowSetOp(*_workbook, pending.id, pending.sheetId, rowPayload);
                applyOperation(*_workbook, rowOp);
            }

            // Create required cells via CRDT operations (empty cells for references)
            for (const auto& pending : required.cells) {
                std::string cellPayload = "{\"t\":\"s\",\"v\":\"\",\"col\":\"" +
                                          pending.colId.toString() + "\",\"row\":\"" +
                                          pending.rowId.toString() + "\"}";
                Operation cellOp = makeCellSetOp(*_workbook, pending.id, pending.sheetId, cellPayload);
                applyOperation(*_workbook, cellOp);
            }

            // Now resolve (all entities should exist)
            ResolveResult resolveResult = resolver.resolve(ast.get());

            if (resolveResult.success) {
                // Serialize AST to UUID format for storage
                std::string uuidFormula = FormulaSerializer::serialize(ast.get());
                payload = "{\"t\":\"f\",\"v\":\"" + jsonEscape(uuidFormula) + "\"" + idSuffix;
            } else {
                // Resolution failed (e.g., sheet not found) - store as error formula
                _refConverter.setContext(*sheet);
                std::string uuidFormula = _refConverter.formulaToUuid(value);
                payload = "{\"t\":\"f\",\"v\":\"" + jsonEscape(uuidFormula) + "\"" + idSuffix;
            }
        } else {
            // Parse failed - store original formula text (will show as error)
            _refConverter.setContext(*sheet);
            std::string uuidFormula = _refConverter.formulaToUuid(value);
            payload = "{\"t\":\"f\",\"v\":\"" + jsonEscape(uuidFormula) + "\"" + idSuffix;
        }
    } else if (value == "TRUE" || value == "true") {
        payload = "{\"t\":\"b\",\"v\":\"true\"" + idSuffix;
    } else if (value == "FALSE" || value == "false") {
        payload = "{\"t\":\"b\",\"v\":\"false\"" + idSuffix;
    } else if (!value.empty()) {
        char* endptr = nullptr;
        strtod(value.c_str(), &endptr);
        if (endptr != nullptr && *endptr == '\0' && endptr != value.c_str()) {
            payload = "{\"t\":\"n\",\"v\":\"" + jsonEscape(value) + "\"" + idSuffix;
        } else {
            payload = "{\"t\":\"s\",\"v\":\"" + jsonEscape(value) + "\"" + idSuffix;
        }
    } else {
        payload = "{\"t\":\"s\",\"v\":\"\"" + idSuffix;
    }

    Operation op = makeCellSetOp(*_workbook, cellId, sheet->id, payload);
    applyOperation(*_workbook, op);


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

    cells::recalculate(_workbook.get(), changed);
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
    Axis* colAxis = sheet->getColumnByPosition(col);
    if (colAxis != nullptr) {
        colId = colAxis->id;
    }
    if (colId.isNull()) {
        colId = generate_id();
        colCreated = true;
        // Note: size is omitted to use local default (sizeSet=false)
        std::string colPayload = "{\"pos\":" + std::to_string(col) + "}";
        Operation colOp = makeColSetOp(*_workbook, colId, sheet->id, colPayload);
        applyOperation(*_workbook, colOp);
    }

    ID rowId;
    bool rowCreated = false;
    Axis* rowAxis = sheet->getRowByPosition(row);
    if (rowAxis != nullptr) {
        rowId = rowAxis->id;
    }
    if (rowId.isNull()) {
        rowId = generate_id();
        rowCreated = true;
        // Note: size is omitted to use local default (sizeSet=false)
        std::string rowPayload = "{\"pos\":" + std::to_string(row) + "}";
        Operation rowOp = makeRowSetOp(*_workbook, rowId, sheet->id, rowPayload);
        applyOperation(*_workbook, rowOp);
    }

    Cell* existingCell = sheet->getCellAt(colId, rowId);
    if (existingCell) {
        if (colCreated) {
            _viewportIndex.onAxisInserted(colId, true, col, DEFAULT_COLUMN_WIDTH);
        }
        if (rowCreated) {
            _viewportIndex.onAxisInserted(rowId, false, row, DEFAULT_ROW_HEIGHT);
        }

        std::ostringstream json;
        json << "{\"success\":true,\"id\":\"" << existingCell->id.toString()
             << "\",\"existed\":true,";

        // Compute editValue for formatted numbers (dates, percentages, etc.)
        // Currency shows raw number in formula bar; percentage shows formatted value
        std::string editValue = existingCell->value.raw;
        const FormatBuffer* cellFormat = _workbook->getEntityFormat(existingCell->id);
        if (existingCell->value.type == CellValueType::NUMBER &&
            cellFormat != nullptr && !cellFormat->isEmpty()) {
            double numValue = existingCell->value.asNumber();
            FormatCodeResult formatted = cells::formatWithCode(numValue, cellFormat->toFormatCode());
            if (formatted.success) {
                editValue = computeEditValueForCore(numValue, formatted.text, *cellFormat);
            }
        }

        if (existingCell->isFormula()) {
            Formula* formula = existingCell->getFormula();
            if (formula != nullptr && formula->ast != nullptr) {
                const std::string uuidFormula = FormulaSerializer::serialize(formula->ast);
                std::string a1Formula = _refConverter.formulaToA1(uuidFormula);
                json << "\"formula\":\"" << jsonEscape(a1Formula) << "\",";
            }
            json << "\"value\":\"" << jsonEscape(existingCell->value.raw) << "\",";
            json << "\"editValue\":\"" << jsonEscape(editValue) << "\"";
        } else {
            json << "\"value\":\"" << jsonEscape(existingCell->value.raw) << "\",";
            json << "\"editValue\":\"" << jsonEscape(editValue) << "\"";
        }
        json << "}";
        return json.str();
    }

    ID cellId = generate_id();
    std::string payload = "{\"t\":\"s\",\"v\":\"\",\"col\":\"" + colId.toString() +
                          "\",\"row\":\"" + rowId.toString() + "\"}";

    Operation op = makeCellSetOp(*_workbook, cellId, sheet->id, payload);
    applyOperation(*_workbook, op);


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

    Cell* cellToDelete = _workbook->getCell(cellId);
    if (!cellToDelete) {
        return "{\"error\":\"Cell not found\"}";
    }

    const ID colId = cellToDelete->colId;
    const ID rowId = cellToDelete->rowId;

    Operation op = makeCellDeleteOp(*_workbook, cellId);
    applyOperation(*_workbook, op);

    broadcastPendingOperations();

    _viewportIndex.onCellRemoved(colId, rowId);

    // After deleting a cell, recalculate all cells with #SPILL! error
    // This allows blocked spills to be restored when the blocking cell is removed
    std::vector<ID> spillErrorCells;
    for (const auto& cId : sheet->getCellIds()) {
        Cell* cellPtr = _workbook->getCell(cId);
        if (cellPtr && cellPtr->value.type == CellValueType::FORMULA_ERROR &&
            cellPtr->value.error == CellError::SPILL) {
            spillErrorCells.push_back(cId);
            markDirty(sheet, cId);
        }
    }
    if (!spillErrorCells.empty()) {
        cells::recalculate(_workbook.get(), spillErrorCells);
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
    Axis* colAxis = sheet->getColumnByPosition(col);
    if (colAxis != nullptr) {
        colId = colAxis->id;
    }
    if (colId.isNull()) {
        return "{\"success\":true,\"deleted\":false}";
    }

    ID rowId;
    Axis* rowAxis = sheet->getRowByPosition(row);
    if (rowAxis != nullptr) {
        rowId = rowAxis->id;
    }
    if (rowId.isNull()) {
        return "{\"success\":true,\"deleted\":false}";
    }

    Cell* cellToDelete = sheet->getCellAt(colId, rowId);
    if (cellToDelete) {
        Operation op = makeCellDeleteOp(*_workbook, cellToDelete->id);
        applyOperation(*_workbook, op);

        broadcastPendingOperations();

        _viewportIndex.onCellRemoved(colId, rowId);

        // After deleting a cell, recalculate all cells with #SPILL! error
        // This allows blocked spills to be restored when the blocking cell is removed
        std::vector<ID> spillErrorCells;
        for (const auto& cId : sheet->getCellIds()) {
            Cell* cellPtr = _workbook->getCell(cId);
            if (cellPtr && cellPtr->value.type == CellValueType::FORMULA_ERROR &&
                cellPtr->value.error == CellError::SPILL) {
                spillErrorCells.push_back(cId);
                markDirty(sheet, cId);
            }
        }
        if (!spillErrorCells.empty()) {
            cells::recalculate(_workbook.get(), spillErrorCells);
        }

        notifyListeners(ChangeType::CELL_CHANGED);
        return "{\"success\":true,\"deleted\":true}";
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

    Axis* colAxis = sheet->getColumn(colId);
    if (colAxis == nullptr) {
        return "{\"error\":\"Column not found\"}";
    }

    if (width < 20) width = 20;
    if (width > 1000) width = 1000;

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
    applyOperation(*_workbook, op);

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

    if (width < 20) width = 20;
    if (width > 1000) width = 1000;

    Axis* column = sheet->getColumnByPosition(pos);
    ID colId;
    if (column != nullptr) {
        colId = column->id;
    }

    bool colCreated = false;
    if (!column) {
        colId = generate_id();
        colCreated = true;
        // New column - no existing state to preserve
        std::string insertPayload =
            "{\"pos\":" + std::to_string(pos) + ",\"size\":" + std::to_string(width) + "}";
        Operation insertOp = makeColSetOp(*_workbook, colId, insertPayload);
        applyOperation(*_workbook, insertOp);
        column = sheet->getColumn(colId);
    } else {
        // Build full-state payload for resurrection correctness
        std::string resizePayload = "{\"pos\":" + std::to_string(column->position);
        resizePayload += ",\"size\":" + std::to_string(width);
        if (!column->name.empty()) {
            resizePayload += ",\"name\":\"" + jsonEscape(column->name) + "\"";
        }
        if (column->hasStyle()) {
            const StyleBuffer* sty = _workbook->getEntityStyle(colId);
            if (sty) {
                resizePayload += ",\"sty\":\"" + sty->toBase64() + "\"";
            }
        }
        if (column->hasFormat()) {
            const FormatBuffer* fmt = _workbook->getEntityFormat(colId);
            if (fmt) {
                resizePayload += ",\"fmt\":\"" + fmt->toBase64() + "\"";
            }
        }
        if (column->hidden()) {
            resizePayload += ",\"hidden\":true";
        }
        resizePayload += "}";
        Operation resizeOp = makeColSetOp(*_workbook, colId, resizePayload);
        applyOperation(*_workbook, resizeOp);
    }


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

    if (height < 10) height = 10;
    if (height > 500) height = 500;

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
    applyOperation(*_workbook, op);

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

    if (height < 10) height = 10;
    if (height > 500) height = 500;

    Axis* row = sheet->getRowByPosition(pos);
    ID rowId;
    if (row != nullptr) {
        rowId = row->id;
    }

    bool rowCreated = false;
    if (!row) {
        rowId = generate_id();
        rowCreated = true;
        // New row - no existing state to preserve
        std::string insertPayload =
            "{\"pos\":" + std::to_string(pos) + ",\"size\":" + std::to_string(height) + "}";
        Operation insertOp = makeRowSetOp(*_workbook, rowId, insertPayload);
        applyOperation(*_workbook, insertOp);
        row = sheet->getRow(rowId);
    } else {
        // Build full-state payload for resurrection correctness
        std::string resizePayload = "{\"pos\":" + std::to_string(row->position);
        resizePayload += ",\"size\":" + std::to_string(height);
        if (row->hasStyle()) {
            const StyleBuffer* sty = _workbook->getEntityStyle(rowId);
            if (sty) {
                resizePayload += ",\"sty\":\"" + sty->toBase64() + "\"";
            }
        }
        if (row->hasFormat()) {
            const FormatBuffer* fmt = _workbook->getEntityFormat(rowId);
            if (fmt) {
                resizePayload += ",\"fmt\":\"" + fmt->toBase64() + "\"";
            }
        }
        if (row->hidden()) {
            resizePayload += ",\"hidden\":true";
        }
        resizePayload += "}";
        Operation resizeOp = makeRowSetOp(*_workbook, rowId, resizePayload);
        applyOperation(*_workbook, resizeOp);
    }


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
    applyOperation(*_workbook, op);


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
        applyOperation(*_workbook, insertOp);

        std::string renamePayload = "{\"name\":\"" + jsonEscape(name) + "\"}";
        Operation renameOp = makeColSetOp(*_workbook, colId, renamePayload);
        applyOperation(*_workbook, renameOp);

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
        applyOperation(*_workbook, op);
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
            if (sty) p += ",\"sty\":\"" + sty->toBase64() + "\"";
        }
        if (col->hasFormat()) {
            const FormatBuffer* fmt = _workbook->getEntityFormat(col->id);
            if (fmt) p += ",\"fmt\":\"" + fmt->toBase64() + "\"";
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
            if (col != nullptr && col->id != colId &&
                col->position >= targetPos && col->position < currentPos) {
                std::string shiftPayload = buildColPayload(col, col->position + 1);
                Operation shiftOp = makeColSetOp(*_workbook, col->id, shiftPayload);
                applyOperation(*_workbook, shiftOp);
            }
        }
    } else {
        // Moving right: shift columns in (currentPos, targetPos) left by 1
        for (const ID& id : sheet->getColumnIds()) {
            Axis* col = sheet->getColumn(id);
            if (col != nullptr && col->id != colId &&
                col->position > currentPos && col->position < targetPos) {
                std::string shiftPayload = buildColPayload(col, col->position - 1);
                Operation shiftOp = makeColSetOp(*_workbook, col->id, shiftPayload);
                applyOperation(*_workbook, shiftOp);
            }
        }
    }

    // Move the target column to its final position
    std::string payload = buildColPayload(colAxis, finalPos);
    Operation op = makeColSetOp(*_workbook, colId, payload);
    applyOperation(*_workbook, op);

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
            if (sty) p += ",\"sty\":\"" + sty->toBase64() + "\"";
        }
        if (row->hasFormat()) {
            const FormatBuffer* fmt = _workbook->getEntityFormat(row->id);
            if (fmt) p += ",\"fmt\":\"" + fmt->toBase64() + "\"";
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
            if (row != nullptr && row->id != rowId &&
                row->position >= targetPos && row->position < currentPos) {
                std::string shiftPayload = buildRowPayload(row, row->position + 1);
                Operation shiftOp = makeRowSetOp(*_workbook, row->id, shiftPayload);
                applyOperation(*_workbook, shiftOp);
            }
        }
    } else {
        // Moving down: shift rows in (currentPos, targetPos) up by 1
        for (const ID& id : sheet->getRowIds()) {
            Axis* row = sheet->getRow(id);
            if (row != nullptr && row->id != rowId &&
                row->position > currentPos && row->position < targetPos) {
                std::string shiftPayload = buildRowPayload(row, row->position - 1);
                Operation shiftOp = makeRowSetOp(*_workbook, row->id, shiftPayload);
                applyOperation(*_workbook, shiftOp);
            }
        }
    }

    // Move the target row to its final position
    std::string payload = buildRowPayload(rowAxis, finalPos);
    Operation op = makeRowSetOp(*_workbook, rowId, payload);
    applyOperation(*_workbook, op);

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
    ApplyResult result = applyOperation(*_workbook, op);

    if (result != ApplyResult::SUCCESS && result != ApplyResult::ALREADY_APPLIED) {
        return "{\"error\":\"Failed to delete column\"}";
    }

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
    ApplyResult result = applyOperation(*_workbook, op);

    if (result != ApplyResult::SUCCESS && result != ApplyResult::ALREADY_APPLIED) {
        return "{\"error\":\"Failed to delete row\"}";
    }

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

    rebuildViewportIndex();
    notifyListeners(ChangeType::CELL_CHANGED);

    return "{\"success\":true,\"cellsFilled\":" + std::to_string(result.cellsFilled) + "}";
}

// ============================================================================
// Merge cell operations
// ============================================================================

std::string CellsEngine::addMergeRange(uint32_t startCol, uint32_t startRow,
                                        uint32_t endCol, uint32_t endRow) {
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
        applyOperation(*_workbook, colOp);
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
        applyOperation(*_workbook, rowOp);
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
        applyOperation(*_workbook, colOp);
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
        applyOperation(*_workbook, rowOp);
    }

    // Ensure all intermediate columns exist (for proper range expansion on insert)
    for (uint32_t c = minCol + 1; c < maxCol; c++) {
        Axis* colAxis = sheet->getColumnByPosition(c);
        if (colAxis == nullptr) {
            ID newColId = generate_id();
            // Note: size is omitted to use local default (sizeSet=false)
            std::string colPayload = "{\"pos\":" + std::to_string(c) + "}";
            Operation colOp = makeColSetOp(*_workbook, newColId, colPayload);
            applyOperation(*_workbook, colOp);
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
            applyOperation(*_workbook, rowOp);
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
    applyOperation(*_workbook, rangeOp);

    rebuildViewportIndex();
    notifyListeners(ChangeType::STRUCTURE_CHANGED);

    return "{\"success\":true,\"colSpan\":" + std::to_string(colSpan) +
           ",\"rowSpan\":" + std::to_string(rowSpan) +
           ",\"rangeId\":\"" + rangeId.toString() + "\"}";
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
    std::vector<Range*> mergeRanges =
        sheet->getRangesAt(col, row, RangeFlags::MERGE);

    if (mergeRanges.empty()) {
        return "{\"error\":\"Cell is not part of a merged region\"}";
    }

    // Remove the first merge range found (typically there should be only one)
    Range* mergeRange = mergeRanges[0];
    std::ostringstream payload;
    payload << "{\"sheet_id\":\"" << sheet->id.toString() << "\"}";

    Operation removeOp = makeRangeDeleteOp(*_workbook, mergeRange->id, payload.str());
    applyOperation(*_workbook, removeOp);

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
    if (!_workbook) {
        return;
    }

    _workbook->name = name;

    std::ostringstream payload;
    payload << "{\"name\":\"" << jsonEscape(name) << "\"}";
    Operation op = makeWorkbookSetOp(*_workbook, payload.str());
    applyOperation(*_workbook, op);

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
    if (!_workbook) {
        return "{\"success\":false,\"error\":\"No workbook\"}";
    }

    // Parse the theme JSON: { name, colorScheme: { colors: [...] }, fontScheme: { majorFont, minorFont } }
    // Simple JSON parsing — extract fields manually
    auto extractString = [](const std::string& json, const std::string& key) -> std::string {
        std::string needle = "\"" + key + "\":\"";
        auto pos = json.find(needle);
        if (pos == std::string::npos) {
            return {};
        }
        pos += needle.length();
        auto end = json.find('"', pos);
        if (end == std::string::npos) {
            return {};
        }
        return json.substr(pos, end - pos);
    };

    auto theme = std::make_unique<Theme>();
    theme->name = extractString(themeJson, "name");

    // Extract colors array
    auto colorsStart = themeJson.find("\"colors\":[");
    if (colorsStart != std::string::npos) {
        colorsStart += 10;  // skip past "colors":[
        auto colorsEnd = themeJson.find(']', colorsStart);
        if (colorsEnd != std::string::npos) {
            std::string colorsStr = themeJson.substr(colorsStart, colorsEnd - colorsStart);
            int colorIndex = 0;
            size_t searchPos = 0;
            while (colorIndex < 12 && searchPos < colorsStr.length()) {
                auto qStart = colorsStr.find('"', searchPos);
                if (qStart == std::string::npos) {
                    break;
                }
                auto qEnd = colorsStr.find('"', qStart + 1);
                if (qEnd == std::string::npos) {
                    break;
                }
                theme->colorScheme.setColor(colorIndex,
                                            colorsStr.substr(qStart + 1, qEnd - qStart - 1));
                colorIndex++;
                searchPos = qEnd + 1;
            }
        }
    }

    theme->fontScheme.majorFont = extractString(themeJson, "majorFont");
    theme->fontScheme.minorFont = extractString(themeJson, "minorFont");

    _workbook->setTheme(std::move(theme));

    // Notify that cells need re-rendering (theme colors resolve differently now)
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
