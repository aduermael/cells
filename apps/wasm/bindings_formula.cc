// =============================================================================
// WASM Bindings - Formula Operations
// =============================================================================
//
// Implementation of formula-related CellsEngine methods:
// - validateFormula: Parse and validate without side effects
// - getFormulaDisplay: Get A1 notation for a cell's formula
// - getCellDependencies/getCellDependents: Dependency graph queries
// - getFormulaReferences: Extract references for highlighting
// - detectCircularRef: Check for circular dependencies
// - getVolatileCells: Get cells with volatile functions
// - getCellDisplayValue: Evaluate a cell's formula
// - recalculate: Trigger recalculation of dirty cells
// - markCellDirty/getDirtyCellIds: Dirty tracking
// - debugParseFormula: Debug AST visualization
//
// =============================================================================

#include "apps/wasm/bindings.h"

#include <cmath>
#include <iomanip>
#include <set>
#include <sstream>

#include "core/cells/crdt.h"
#include "core/cells/dependency_graph.h"
#include "core/cells/formula_display.h"
#include "core/cells/formula_eval.h"
#include "core/cells/formula_parser.h"
#include "core/cells/formula_recalc.h"
#include "core/cells/formula_resolver.h"
#include "core/cells/formula_serializer.h"
#include "core/cells/operation.h"

namespace cells::wasm {

std::string CellsEngine::validateFormula(const std::string& formulaText) {
    FormulaParser parser(formulaText);
    auto ast = parser.parse();

    std::ostringstream json;
    json << "{";
    json << "\"formula\":\"" << jsonEscape(formulaText) << "\",";
    json << "\"valid\":" << (ast && parser.errors().empty() ? "true" : "false") << ",";

    json << "\"errors\":[";
    const auto& errors = parser.errors();
    for (size_t i = 0; i < errors.size(); ++i) {
        if (i > 0) json << ",";
        json << "\"" << jsonEscape(errors[i]) << "\"";
    }
    json << "],";

    json << "\"rootType\":";
    if (ast) {
        const char* typeStr = "unknown";
        switch (ast->type) {
            case ASTNodeType::NUMBER_LITERAL:
                typeStr = "NumberLiteral";
                break;
            case ASTNodeType::STRING_LITERAL:
                typeStr = "StringLiteral";
                break;
            case ASTNodeType::BOOLEAN_LITERAL:
                typeStr = "BooleanLiteral";
                break;
            case ASTNodeType::CELL_REF:
                typeStr = "CellRef";
                break;
            case ASTNodeType::RANGE_REF:
                typeStr = "RangeRef";
                break;
            case ASTNodeType::COLUMN_REF:
                typeStr = "ColumnRef";
                break;
            case ASTNodeType::ROW_REF:
                typeStr = "RowRef";
                break;
            case ASTNodeType::COLUMN_RANGE_REF:
                typeStr = "ColumnRangeRef";
                break;
            case ASTNodeType::ROW_RANGE_REF:
                typeStr = "RowRangeRef";
                break;
            case ASTNodeType::NAMED_REF:
                typeStr = "NamedRef";
                break;
            case ASTNodeType::BINARY_OP:
                typeStr = "BinaryOp";
                break;
            case ASTNodeType::UNARY_OP:
                typeStr = "UnaryOp";
                break;
            case ASTNodeType::FUNCTION_CALL:
                typeStr = "FunctionCall";
                break;
            case ASTNodeType::ERROR_NODE:
                typeStr = "Error";
                break;
        }
        json << "\"" << typeStr << "\"";
    } else {
        json << "null";
    }

    json << "}";
    return json.str();
}

std::string CellsEngine::getFormulaDisplay(const std::string& cellIdStr) {
    if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
        return "";
    }

    auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
    if (!sheet) {
        return "";
    }

    if (cellIdStr.size() != ID_LENGTH) {
        return "";
    }
    ID cellId(cellIdStr);

    Cell* cell = sheet->getCell(cellId);
    if (!cell || !cell->isFormula()) {
        return "";
    }

    Formula* formula = cell->getFormula();
    if (!formula || !formula->ast) {
        return "";
    }

    // Use FormulaDisplayConverter for context-aware display:
    // - For refs on the current sheet: shows "B2"
    // - For refs on other sheets: shows "Sheet2!B2"
    FormulaDisplayConverter converter(*sheet, _workbook.get());
    return converter.toDisplayString(formula->ast);
}

std::string CellsEngine::getCellDependencies(const std::string& cellIdStr) {
    if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
        return "{\"error\":\"No sheet available\"}";
    }

    auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
    if (!sheet) return "{\"error\":\"Sheet not found\"}";

    if (cellIdStr.size() != ID_LENGTH) return "{\"error\":\"Invalid cell ID\"}";
    ID cellId(cellIdStr);

    DependencyGraph* depGraph = sheet->getDependencyGraph();
    if (!depGraph) return "{\"error\":\"No dependency graph\"}";

    std::vector<DependencyRef> deps = depGraph->getDependencies(cellId);

    std::ostringstream json;
    json << "{\"dependencies\":[";

    for (size_t i = 0; i < deps.size(); ++i) {
        if (i > 0) json << ",";
        const auto& dep = deps[i];

        json << "{";
        switch (dep.type) {
            case DependencyRef::Type::CELL:
                json << "\"type\":\"cell\",";
                json << "\"cellId\":\"" << dep.cellId.toString() << "\"";
                break;
            case DependencyRef::Type::RANGE:
                json << "\"type\":\"range\",";
                json << "\"startCellId\":\"" << dep.startCellId.toString() << "\",";
                json << "\"endCellId\":\"" << dep.endCellId.toString() << "\"";
                break;
            case DependencyRef::Type::COLUMN:
                json << "\"type\":\"column\",";
                json << "\"columnId\":\"" << dep.columnId.toString() << "\"";
                break;
            case DependencyRef::Type::ROW:
                json << "\"type\":\"row\",";
                json << "\"rowId\":\"" << dep.rowId.toString() << "\"";
                break;
            case DependencyRef::Type::COLUMN_RANGE:
                json << "\"type\":\"columnRange\",";
                json << "\"startColumnId\":\"" << dep.startColumnId.toString() << "\",";
                json << "\"endColumnId\":\"" << dep.endColumnId.toString() << "\"";
                break;
            case DependencyRef::Type::ROW_RANGE:
                json << "\"type\":\"rowRange\",";
                json << "\"startRowId\":\"" << dep.startRowId.toString() << "\",";
                json << "\"endRowId\":\"" << dep.endRowId.toString() << "\"";
                break;
        }
        json << ",\"sourceStart\":" << dep.sourceStart;
        json << ",\"sourceEnd\":" << dep.sourceEnd;
        json << "}";
    }

    json << "]}";
    return json.str();
}

std::string CellsEngine::getCellDependents(const std::string& cellIdStr) {
    if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
        return "{\"error\":\"No sheet available\"}";
    }

    auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
    if (!sheet) return "{\"error\":\"Sheet not found\"}";

    if (cellIdStr.size() != ID_LENGTH) return "{\"error\":\"Invalid cell ID\"}";
    ID cellId(cellIdStr);

    DependencyGraph* depGraph = sheet->getDependencyGraph();
    if (!depGraph) return "{\"error\":\"No dependency graph\"}";

    std::vector<ID> dependents = depGraph->getDependents(cellId);

    std::ostringstream json;
    json << "{\"dependents\":[";

    for (size_t i = 0; i < dependents.size(); ++i) {
        if (i > 0) json << ",";
        json << "\"" << dependents[i].toString() << "\"";
    }

    json << "]}";
    return json.str();
}

std::string CellsEngine::getFormulaReferences(const std::string& formulaText) {
    if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
        return "{\"error\":\"No sheet available\"}";
    }

    auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
    if (!sheet) return "{\"error\":\"Sheet not found\"}";

    FormulaParser parser(formulaText);
    auto ast = parser.parse();

    if (!ast) {
        return "{\"error\":\"Parse failed\",\"references\":[]}";
    }

    FormulaResolver resolver(*_workbook, *sheet, _workbook->getNamedRanges());

    // CRDT-compliant resolution: discover and create entities first
    RequiredEntities required = resolver.getRequiredEntities(ast.get());

    // Create required columns via CRDT operations
    for (const auto& pending : required.columns) {
        std::string colPayload = "{\"pos\":" + std::to_string(pending.position) +
                                 ",\"size\":" + std::to_string(DEFAULT_COLUMN_WIDTH) + "}";
        Operation colOp = makeColInsertOp(*_workbook, pending.id, pending.sheetId, colPayload);
        applyOperation(*_workbook, colOp);
    }

    // Create required rows via CRDT operations
    for (const auto& pending : required.rows) {
        std::string rowPayload = "{\"pos\":" + std::to_string(pending.position) +
                                 ",\"size\":" + std::to_string(DEFAULT_ROW_HEIGHT) + "}";
        Operation rowOp = makeRowInsertOp(*_workbook, pending.id, pending.sheetId, rowPayload);
        applyOperation(*_workbook, rowOp);
    }

    // Create required cells via CRDT operations (empty cells for references)
    for (const auto& pending : required.cells) {
        std::string cellPayload = "{\"type\":\"s\",\"value\":\"\",\"col_id\":\"" +
                                  pending.colId.toString() + "\",\"row_id\":\"" +
                                  pending.rowId.toString() + "\"}";
        Operation cellOp = makeCellSetValueOp(*_workbook, pending.id, pending.sheetId, cellPayload);
        applyOperation(*_workbook, cellOp);
    }

    // Now resolve (all entities should exist)
    ResolveResult result = resolver.resolve(ast.get());

    broadcastPendingOperations();

    rebuildViewportIndex();

    std::vector<ReferenceInfo> refs = resolver.extractReferences(ast.get());

    std::ostringstream json;
    json << "{\"references\":[";

    for (size_t i = 0; i < refs.size(); ++i) {
        if (i > 0) json << ",";
        const auto& ref = refs[i];

        json << "{";
        switch (ref.type) {
            case ReferenceInfo::Type::CELL: {
                json << "\"type\":\"cell\",";
                json << "\"cellId\":\"" << ref.cellId.toString() << "\"";
                const Cell* cell = sheet->getCell(ref.cellId);
                if (cell) {
                    const Axis* col = sheet->getColumn(cell->colId);
                    const Axis* row = sheet->getRow(cell->rowId);
                    if (col && row) {
                        json << ",\"col\":" << col->position;
                        json << ",\"row\":" << row->position;
                    }
                }
                break;
            }
            case ReferenceInfo::Type::RANGE: {
                json << "\"type\":\"range\",";
                json << "\"topLeftCellId\":\"" << ref.topLeftCellId.toString() << "\",";
                json << "\"bottomRightCellId\":\"" << ref.bottomRightCellId.toString() << "\"";
                const Cell* topLeft = sheet->getCell(ref.topLeftCellId);
                const Cell* bottomRight = sheet->getCell(ref.bottomRightCellId);
                if (topLeft && bottomRight) {
                    const Axis* startCol = sheet->getColumn(topLeft->colId);
                    const Axis* startRow = sheet->getRow(topLeft->rowId);
                    const Axis* endCol = sheet->getColumn(bottomRight->colId);
                    const Axis* endRow = sheet->getRow(bottomRight->rowId);
                    if (startCol && startRow && endCol && endRow) {
                        json << ",\"startCol\":" << startCol->position;
                        json << ",\"startRow\":" << startRow->position;
                        json << ",\"endCol\":" << endCol->position;
                        json << ",\"endRow\":" << endRow->position;
                    }
                }
                break;
            }
            case ReferenceInfo::Type::COLUMN: {
                json << "\"type\":\"column\",";
                json << "\"axisId\":\"" << ref.axisId.toString() << "\"";
                const Axis* axis = sheet->getColumn(ref.axisId);
                if (axis) {
                    json << ",\"col\":" << axis->position;
                }
                break;
            }
            case ReferenceInfo::Type::ROW: {
                json << "\"type\":\"row\",";
                json << "\"axisId\":\"" << ref.axisId.toString() << "\"";
                const Axis* axis = sheet->getRow(ref.axisId);
                if (axis) {
                    json << ",\"row\":" << axis->position;
                }
                break;
            }
            case ReferenceInfo::Type::COLUMN_RANGE: {
                json << "\"type\":\"columnRange\",";
                json << "\"startAxisId\":\"" << ref.startAxisId.toString() << "\",";
                json << "\"endAxisId\":\"" << ref.endAxisId.toString() << "\"";
                const Axis* startAxis = sheet->getColumn(ref.startAxisId);
                const Axis* endAxis = sheet->getColumn(ref.endAxisId);
                if (startAxis && endAxis) {
                    json << ",\"startCol\":" << startAxis->position;
                    json << ",\"endCol\":" << endAxis->position;
                }
                break;
            }
            case ReferenceInfo::Type::ROW_RANGE: {
                json << "\"type\":\"rowRange\",";
                json << "\"startAxisId\":\"" << ref.startAxisId.toString() << "\",";
                json << "\"endAxisId\":\"" << ref.endAxisId.toString() << "\"";
                const Axis* startAxis = sheet->getRow(ref.startAxisId);
                const Axis* endAxis = sheet->getRow(ref.endAxisId);
                if (startAxis && endAxis) {
                    json << ",\"startRow\":" << startAxis->position;
                    json << ",\"endRow\":" << endAxis->position;
                }
                break;
            }
            case ReferenceInfo::Type::NAMED: {
                json << "\"type\":\"named\",";
                json << "\"name\":\"" << jsonEscape(ref.namedRangeName) << "\"";
                // Resolve the named range to get target coordinates
                const NamedRange* nr = _workbook->getNamedRanges()->resolve(
                    ref.namedRangeName, sheet->id);
                if (nr) {
                    const auto& target = nr->target;
                    // Get the target sheet (may be different from current sheet)
                    Sheet* targetSheet = target.sheetId.isNull()
                        ? sheet
                        : _workbook->getSheet(target.sheetId);
                    if (targetSheet) {
                        switch (target.type) {
                            case NamedRangeTarget::Type::CELL: {
                                json << ",\"targetType\":\"cell\"";
                                const Cell* cell = targetSheet->getCell(target.id1);
                                if (cell) {
                                    const Axis* col = targetSheet->getColumn(cell->colId);
                                    const Axis* row = targetSheet->getRow(cell->rowId);
                                    if (col && row) {
                                        json << ",\"col\":" << col->position;
                                        json << ",\"row\":" << row->position;
                                    }
                                }
                                break;
                            }
                            case NamedRangeTarget::Type::RANGE: {
                                json << ",\"targetType\":\"range\"";
                                const Cell* topLeft = targetSheet->getCell(target.id1);
                                const Cell* bottomRight = targetSheet->getCell(target.id2);
                                if (topLeft && bottomRight) {
                                    const Axis* startCol = targetSheet->getColumn(topLeft->colId);
                                    const Axis* startRow = targetSheet->getRow(topLeft->rowId);
                                    const Axis* endCol = targetSheet->getColumn(bottomRight->colId);
                                    const Axis* endRow = targetSheet->getRow(bottomRight->rowId);
                                    if (startCol && startRow && endCol && endRow) {
                                        json << ",\"startCol\":" << startCol->position;
                                        json << ",\"startRow\":" << startRow->position;
                                        json << ",\"endCol\":" << endCol->position;
                                        json << ",\"endRow\":" << endRow->position;
                                    }
                                }
                                break;
                            }
                            case NamedRangeTarget::Type::COLUMN: {
                                json << ",\"targetType\":\"column\"";
                                const Axis* axis = targetSheet->getColumn(target.id1);
                                if (axis) {
                                    json << ",\"col\":" << axis->position;
                                }
                                break;
                            }
                            case NamedRangeTarget::Type::ROW: {
                                json << ",\"targetType\":\"row\"";
                                const Axis* axis = targetSheet->getRow(target.id1);
                                if (axis) {
                                    json << ",\"row\":" << axis->position;
                                }
                                break;
                            }
                            case NamedRangeTarget::Type::COLUMN_RANGE: {
                                json << ",\"targetType\":\"column\"";
                                const Axis* startAxis = targetSheet->getColumn(target.id1);
                                const Axis* endAxis = targetSheet->getColumn(target.id2);
                                if (startAxis && endAxis) {
                                    json << ",\"startCol\":" << startAxis->position;
                                    json << ",\"endCol\":" << endAxis->position;
                                }
                                break;
                            }
                            case NamedRangeTarget::Type::ROW_RANGE: {
                                json << ",\"targetType\":\"row\"";
                                const Axis* startAxis = targetSheet->getRow(target.id1);
                                const Axis* endAxis = targetSheet->getRow(target.id2);
                                if (startAxis && endAxis) {
                                    json << ",\"startRow\":" << startAxis->position;
                                    json << ",\"endRow\":" << endAxis->position;
                                }
                                break;
                            }
                        }
                    }
                }
                break;
            }
        }

        if (!ref.sheetId.isNull()) {
            json << ",\"sheetId\":\"" << ref.sheetId.toString() << "\"";
        }

        json << ",\"sourceStart\":" << ref.sourcePosition.start;
        json << ",\"sourceEnd\":" << ref.sourcePosition.end;
        json << "}";
    }

    json << "]}";
    return json.str();
}

std::string CellsEngine::getReferencesFromPartial(const std::string& formulaText) {
    return getFormulaReferences(formulaText);
}

std::string CellsEngine::detectCircularRef(const std::string& cellIdStr) {
    if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
        return "{\"error\":\"No sheet available\"}";
    }

    auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
    if (!sheet) return "{\"error\":\"Sheet not found\"}";

    if (cellIdStr.size() != ID_LENGTH) return "{\"error\":\"Invalid cell ID\"}";
    ID cellId(cellIdStr);

    DependencyGraph* depGraph = sheet->getDependencyGraph();
    if (!depGraph) return "{\"error\":\"No dependency graph\"}";

    std::vector<ID> cycle = depGraph->detectCycle(cellId);

    std::ostringstream json;
    json << "{\"hasCycle\":" << (cycle.empty() ? "false" : "true") << ",";
    json << "\"cycle\":[";

    for (size_t i = 0; i < cycle.size(); ++i) {
        if (i > 0) json << ",";
        json << "\"" << cycle[i].toString() << "\"";
    }

    json << "]}";
    return json.str();
}

std::string CellsEngine::getVolatileCells() {
    if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
        return "{\"error\":\"No sheet available\"}";
    }

    auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
    if (!sheet) return "{\"error\":\"Sheet not found\"}";

    DependencyGraph* depGraph = sheet->getDependencyGraph();
    if (!depGraph) return "{\"error\":\"No dependency graph\"}";

    std::vector<ID> volatile_cells = depGraph->getVolatileCells();

    std::ostringstream json;
    json << "{\"volatileCells\":[";

    for (size_t i = 0; i < volatile_cells.size(); ++i) {
        if (i > 0) json << ",";
        json << "\"" << volatile_cells[i].toString() << "\"";
    }

    json << "]}";
    return json.str();
}

std::string CellsEngine::getCellDisplayValue(const std::string& cellIdStr) {
    if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
        return "{\"error\":\"No sheet available\"}";
    }

    auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
    if (!sheet) return "{\"error\":\"Sheet not found\"}";

    if (cellIdStr.size() != ID_LENGTH) {
        return "{\"error\":\"Invalid cell ID\"}";
    }
    ID cellId(cellIdStr);

    Cell* cell = sheet->getCell(cellId);
    if (!cell) {
        return "{\"value\":\"\",\"type\":\"empty\"}";
    }

    EvalResult result = evaluateCell(sheet, cell);

    std::ostringstream json;
    json << "{";

    if (result.isError()) {
        json << "\"value\":\"" << jsonEscape(errorToString(result.getError())) << "\",";
        json << "\"type\":\"e\",";
        json << "\"error\":\"" << jsonEscape(errorToString(result.getError())) << "\"";
    } else if (result.isNumber()) {
        const double num = result.getNumber();
        if (std::floor(num) == num && std::abs(num) < 1e15) {
            json << "\"value\":\"" << static_cast<long long>(num) << "\",";
        } else {
            std::ostringstream numStr;
            numStr << std::setprecision(15) << num;
            json << "\"value\":\"" << numStr.str() << "\",";
        }
        json << "\"type\":\"n\"";
    } else if (result.isString()) {
        json << "\"value\":\"" << jsonEscape(result.getString()) << "\",";
        json << "\"type\":\"s\"";
    } else if (result.isBoolean()) {
        json << "\"value\":\"" << (result.getBoolean() ? "TRUE" : "FALSE") << "\",";
        json << "\"type\":\"b\"";
    } else if (result.isEmpty()) {
        json << "\"value\":\"\",";
        json << "\"type\":\"empty\"";
    } else {
        json << "\"value\":\"\",";
        json << "\"type\":\"empty\"";
    }

    json << "}";
    return json.str();
}

std::string CellsEngine::recalculate() {
    if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
        return "{\"error\":\"No sheet available\"}";
    }

    auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
    if (!sheet) return "{\"error\":\"Sheet not found\"}";

    std::vector<ID> dirtyCells = getDirtyCells(sheet);

    int recalculated = 0;
    int errors = 0;

    for (const ID& cellId : dirtyCells) {
        Cell* cell = sheet->getCell(cellId);
        if (cell && cell->isFormula()) {
            EvalResult result = evaluateCell(sheet, cell);
            ++recalculated;
            if (result.isError()) {
                ++errors;
            }
        }
    }

    recalculateVolatile(sheet);

    std::ostringstream json;
    json << "{\"recalculated\":" << recalculated << ",\"errors\":" << errors << "}";
    return json.str();
}

bool CellsEngine::hasDirtyCellsCheck() {
    if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
        return false;
    }

    auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
    if (!sheet) return false;

    return hasDirtyCells(sheet);
}

std::string CellsEngine::markCellDirty(const std::string& cellIdStr) {
    if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
        return "{\"error\":\"No sheet available\"}";
    }

    auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
    if (!sheet) return "{\"error\":\"Sheet not found\"}";

    if (cellIdStr.size() != ID_LENGTH) {
        return "{\"error\":\"Invalid cell ID\"}";
    }
    ID cellId(cellIdStr);

    markDirty(sheet, cellId);

    int dirtyCount = 0;
    for (const auto& cellId : sheet->getCellIds()) {
        Cell* cell = _workbook->getCell(cellId);
        if (!cell) continue;
        const Formula* formula = cell->getFormula();
        if (formula && formula->dirty) {
            ++dirtyCount;
        }
    }

    std::ostringstream json;
    json << "{\"success\":true,\"markedDirty\":" << dirtyCount << "}";
    return json.str();
}

std::string CellsEngine::getDirtyCellIds() {
    if (!_workbook || _activeSheetIndex >= _workbook->sheetCount()) {
        return "{\"error\":\"No sheet available\"}";
    }

    auto* sheet = _workbook->getSheetByIndex(_activeSheetIndex);
    if (!sheet) return "{\"error\":\"Sheet not found\"}";

    std::vector<ID> dirtyCells = getDirtyCells(sheet);

    std::ostringstream json;
    json << "{\"dirtyCells\":[";

    for (size_t i = 0; i < dirtyCells.size(); ++i) {
        if (i > 0) json << ",";
        json << "\"" << dirtyCells[i].toString() << "\"";
    }

    json << "]}";
    return json.str();
}

std::string CellsEngine::debugParseFormula(const std::string& formulaText) {
    FormulaParser parser(formulaText);
    auto ast = parser.parse();

    std::ostringstream json;
    json << "{";
    json << "\"formula\":\"" << jsonEscape(formulaText) << "\",";

    json << "\"errors\":[";
    const auto& errors = parser.errors();
    for (size_t i = 0; i < errors.size(); ++i) {
        if (i > 0) {
            json << ",";
        }
        json << "\"" << jsonEscape(errors[i]) << "\"";
    }
    json << "],";

    json << "\"ast\":";
    if (ast) {
        json << ast->toJson();
    } else {
        json << "null";
    }

    json << "}";
    return json.str();
}

}  // namespace cells::wasm
