#include "core/cells/fill_range.h"

#include <cmath>

#include <sstream>

#include "core/cells/crdt.h"
#include "core/cells/formula_display.h"
#include "core/cells/formula_resolver.h"
#include "core/cells/formula_serializer.h"
#include "core/cells/id.h"
#include "core/cells/model.h"
#include "core/cells/ref_converter.h"

namespace cells {

namespace {

// Helper function to build JSON payload for cell value operation
std::string buildCellPayload(const std::string& typeChar, const std::string& valueStr,
                             const cells::ID& colId, const cells::ID& rowId) {
    std::ostringstream ss;
    ss << "{\"type\":\"" << typeChar << "\",\"value\":\"" << valueStr << "\",\"col_id\":\""
       << colId.toString() << "\",\"row_id\":\"" << rowId.toString() << "\"}";
    return ss.str();
}

// Helper to escape JSON string content
std::string jsonEscape(const std::string& str) {
    std::string result;
    result.reserve(str.size() * 2);
    for (const char c : str) {
        switch (c) {
            case '"':
                result += "\\\"";
                break;
            case '\\':
                result += "\\\\";
                break;
            case '\n':
                result += "\\n";
                break;
            case '\r':
                result += "\\r";
                break;
            case '\t':
                result += "\\t";
                break;
            default:
                result += c;
        }
    }
    return result;
}

// Helper function to get formula AST (cloned)
std::unique_ptr<ASTNode> getFormulaASTClone(const Cell* cell) {
    if (!cell || !cell->isFormula()) {
        return nullptr;
    }
    const Formula* formula = cell->getFormula();
    if (!formula || !formula->ast) {
        return nullptr;
    }
    return formula->ast->clone();
}

// Helper to populate A1 notation (column/row) in AST nodes from UUID cellIds
// This is needed because UUID-based formulas store cellId but have empty column/row
void populateA1FromUuid(ASTNode* node, Sheet* sheet) {
    if (!node || !sheet) {
        return;
    }

    switch (node->type) {
        case ASTNodeType::CELL_REF: {
            auto* cellRef = static_cast<CellRefNode*>(node);
            // If we have a cellId but no column, look up the position
            if (!cellRef->cellId.empty() && cellRef->column.empty()) {
                const Cell* cell = sheet->getCell(ID(cellRef->cellId));
                if (cell) {
                    const Axis* col = sheet->getColumn(cell->colId);
                    const Axis* row = sheet->getRow(cell->rowId);
                    if (col && row) {
                        cellRef->column = Sheet::positionToColumnName(col->position);
                        cellRef->row = static_cast<int>(row->position + 1);  // 1-based
                    }
                }
            }
            break;
        }
        case ASTNodeType::RANGE_REF: {
            auto* rangeRef = static_cast<RangeRefNode*>(node);
            populateA1FromUuid(rangeRef->topLeft.get(), sheet);
            populateA1FromUuid(rangeRef->bottomRight.get(), sheet);
            break;
        }
        case ASTNodeType::COLUMN_REF: {
            auto* colRef = static_cast<ColumnRefNode*>(node);
            if (!colRef->columnId.empty() && colRef->column.empty()) {
                const Axis* col = sheet->getColumn(ID(colRef->columnId));
                if (col) {
                    colRef->column = Sheet::positionToColumnName(col->position);
                }
            }
            break;
        }
        case ASTNodeType::ROW_REF: {
            auto* rowRef = static_cast<RowRefNode*>(node);
            if (!rowRef->rowId.empty() && rowRef->row == 0) {
                const Axis* row = sheet->getRow(ID(rowRef->rowId));
                if (row) {
                    rowRef->row = static_cast<int>(row->position + 1);  // 1-based
                }
            }
            break;
        }
        case ASTNodeType::COLUMN_RANGE_REF: {
            auto* colRangeRef = static_cast<ColumnRangeRefNode*>(node);
            if (!colRangeRef->startColumnId.empty() && colRangeRef->startColumn.empty()) {
                const Axis* col = sheet->getColumn(ID(colRangeRef->startColumnId));
                if (col) {
                    colRangeRef->startColumn = Sheet::positionToColumnName(col->position);
                }
            }
            if (!colRangeRef->endColumnId.empty() && colRangeRef->endColumn.empty()) {
                const Axis* col = sheet->getColumn(ID(colRangeRef->endColumnId));
                if (col) {
                    colRangeRef->endColumn = Sheet::positionToColumnName(col->position);
                }
            }
            break;
        }
        case ASTNodeType::ROW_RANGE_REF: {
            auto* rowRangeRef = static_cast<RowRangeRefNode*>(node);
            if (!rowRangeRef->startRowId.empty() && rowRangeRef->startRow == 0) {
                const Axis* row = sheet->getRow(ID(rowRangeRef->startRowId));
                if (row) {
                    rowRangeRef->startRow = static_cast<int>(row->position + 1);
                }
            }
            if (!rowRangeRef->endRowId.empty() && rowRangeRef->endRow == 0) {
                const Axis* row = sheet->getRow(ID(rowRangeRef->endRowId));
                if (row) {
                    rowRangeRef->endRow = static_cast<int>(row->position + 1);
                }
            }
            break;
        }
        case ASTNodeType::BINARY_OP: {
            auto* binOp = static_cast<BinaryOpNode*>(node);
            populateA1FromUuid(binOp->left.get(), sheet);
            populateA1FromUuid(binOp->right.get(), sheet);
            break;
        }
        case ASTNodeType::UNARY_OP: {
            auto* unaryOp = static_cast<UnaryOpNode*>(node);
            populateA1FromUuid(unaryOp->operand.get(), sheet);
            break;
        }
        case ASTNodeType::FUNCTION_CALL: {
            auto* funcCall = static_cast<FunctionCallNode*>(node);
            for (auto& arg : funcCall->args) {
                populateA1FromUuid(arg.get(), sheet);
            }
            break;
        }
        default:
            // Literals, named refs, errors - nothing to do
            break;
    }
}

// Helper struct to hold fill cell info
struct FillCellInfo {
    std::string value;
    std::string typeChar;  // "n" for number, "s" for string, "f" for formula
    bool skip{false};
};

// Helper function to determine what value to fill into a cell for STRING and FORMULA patterns
FillCellInfo getFillValueNonNumeric(const DetectedPattern& pattern, int index, int colOffset,
                                    int rowOffset, Workbook* workbook, Sheet* sheet) {
    FillCellInfo info;
    info.skip = false;

    switch (pattern.type) {
        case PatternType::EMPTY:
            info.skip = true;
            return info;

        case PatternType::STRING: {
            if (pattern.stringValues.empty()) {
                info.skip = true;
                return info;
            }
            const int srcIdx = (index - 1) % static_cast<int>(pattern.stringValues.size());
            info.value = pattern.stringValues[srcIdx];
            info.typeChar = "s";
            return info;
        }

        case PatternType::FORMULA: {
            if (pattern.formulaASTs.empty()) {
                info.skip = true;
                return info;
            }
            // Get the source AST to adjust
            const int srcIdx = (index - 1) % static_cast<int>(pattern.formulaASTs.size());
            const auto& sourceAST = pattern.formulaASTs[srcIdx];
            if (!sourceAST) {
                // Not a formula in this slot, use string value instead
                if (!pattern.stringValues.empty() &&
                    srcIdx < static_cast<int>(pattern.stringValues.size())) {
                    info.value = pattern.stringValues[srcIdx];
                    info.typeChar = "s";
                } else {
                    info.skip = true;
                }
                return info;
            }

            // Clone the source AST and populate A1 notation from UUID refs
            // This is needed because UUID-based formulas have empty column/row fields
            auto clonedAST = sourceAST->clone();
            populateA1FromUuid(clonedAST.get(), sheet);

            // Adjust formula references using AST-based adjustment
            // For index=1, offset = colOffset*1 or rowOffset*1, etc.
            auto adjustedAST = RefConverter::adjustASTReferences(clonedAST.get(), colOffset * index,
                                                                 rowOffset * index);

            // Check if adjustment resulted in an error (e.g., #REF!)
            if (adjustedAST->type == ASTNodeType::ERROR_NODE) {
                // Use display converter for error formulas
                const FormulaDisplayConverter converter(*sheet);
                info.value = converter.toDisplayString(adjustedAST.get());
                info.typeChar = "f";
                return info;
            }

            // CRDT-compliant resolution: discover and create entities first
            FormulaResolver resolver(*workbook, *sheet);
            const RequiredEntities required = resolver.getRequiredEntities(adjustedAST.get());

            // Create required columns via CRDT operations
            for (const auto& pending : required.columns) {
                const std::string colPayload = "{\"pos\":" + std::to_string(pending.position) +
                                               ",\"size\":" + std::to_string(DEFAULT_COLUMN_WIDTH) +
                                               "}";
                const Operation colOp =
                    makeColInsertOp(*workbook, pending.id, pending.sheetId, colPayload);
                applyOperation(*workbook, colOp);
            }

            // Create required rows via CRDT operations
            for (const auto& pending : required.rows) {
                const std::string rowPayload = "{\"pos\":" + std::to_string(pending.position) +
                                               ",\"size\":" + std::to_string(DEFAULT_ROW_HEIGHT) +
                                               "}";
                const Operation rowOp =
                    makeRowInsertOp(*workbook, pending.id, pending.sheetId, rowPayload);
                applyOperation(*workbook, rowOp);
            }

            // Create required cells via CRDT operations (empty cells for references)
            for (const auto& pending : required.cells) {
                const std::string cellPayload = "{\"type\":\"s\",\"value\":\"\",\"col_id\":\"" +
                                                pending.colId.toString() + "\",\"row_id\":\"" +
                                                pending.rowId.toString() + "\"}";
                const Operation cellOp =
                    makeCellSetValueOp(*workbook, pending.id, pending.sheetId, cellPayload);
                applyOperation(*workbook, cellOp);
            }

            // Now resolve with existingOnly=true (all entities should exist)
            resolver.resolve(adjustedAST.get(), true);

            // Serialize to UUID format for CRDT storage
            info.value = FormulaSerializer::serialize(adjustedAST.get());
            info.typeChar = "f";
            return info;
        }

        case PatternType::LINEAR:
        case PatternType::CONSTANT:
        default: {
            // This function shouldn't be used for numeric patterns
            // Numeric patterns need direction-specific handling
            info.skip = true;
            return info;
        }
    }
}

// Helper to build formula payload (similar to buildCellPayload but for formulas)
std::string buildFormulaPayload(const std::string& formula, const cells::ID& colId,
                                const cells::ID& rowId) {
    // Note: display field omitted - peers generate display strings from AST locally
    std::ostringstream ss;
    ss << "{\"type\":\"f\",\"value\":\"" << jsonEscape(formula) << "\",\"col_id\":\""
       << colId.toString() << "\",\"row_id\":\"" << rowId.toString() << "\"}";
    return ss.str();
}

}  // namespace

FillDirection getFillDirection(int sourceMinCol, int sourceMinRow, int sourceMaxCol,
                               int sourceMaxRow, int targetMinCol, int targetMinRow,
                               int targetMaxCol, int targetMaxRow) {
    // Determine direction based on how target extends beyond source
    if (targetMaxRow > sourceMaxRow) {
        return FillDirection::DOWN;
    }
    if (targetMinRow < sourceMinRow) {
        return FillDirection::UP;
    }
    if (targetMaxCol > sourceMaxCol) {
        return FillDirection::RIGHT;
    }
    if (targetMinCol < sourceMinCol) {
        return FillDirection::LEFT;
    }
    // No extension (shouldn't happen in normal use)
    return FillDirection::DOWN;
}

DetectedPattern detectPattern(Sheet* sheet, int minCol, int minRow, int maxCol, int maxRow,
                              FillDirection direction) {
    DetectedPattern pattern;
    pattern.type = PatternType::CONSTANT;

    // Collect values along the fill axis
    std::vector<double> numericValues;
    std::vector<std::string> stringValues;
    std::vector<std::unique_ptr<ASTNode>> formulaASTs;  // Store AST pointers
    bool allNumeric = true;
    bool allEmpty = true;
    bool hasFormula = false;

    // Determine iteration based on direction
    if (direction == FillDirection::DOWN || direction == FillDirection::UP) {
        // Iterate along rows (column is fixed at minCol for simplicity)
        // For multiple columns, we'd need to handle each column separately
        for (int row = minRow; row <= maxRow; ++row) {
            const Axis* rowAxis = sheet->getRowByPosition(static_cast<uint32_t>(row));
            const Axis* colAxis = sheet->getColumnByPosition(static_cast<uint32_t>(minCol));
            if (!rowAxis || !colAxis) {
                continue;
            }

            const Cell* cell = sheet->getCellAt(colAxis->id, rowAxis->id);

            // Check if cell is a formula first (formula cells may have empty value.raw)
            if (cell != nullptr && cell->isFormula()) {
                allEmpty = false;
                hasFormula = true;
                allNumeric = false;
                formulaASTs.push_back(getFormulaASTClone(cell));
                stringValues.push_back(cell->value.raw);
                continue;
            }

            if (!cell || cell->value.raw.empty()) {
                // Empty cell
                stringValues.emplace_back("");
                formulaASTs.push_back(nullptr);
                continue;
            }

            allEmpty = false;

            if (cell->value.type == CellValueType::NUMBER) {
                const double val = cell->value.asNumber();
                numericValues.push_back(val);
                stringValues.push_back(cell->value.raw);
                formulaASTs.push_back(nullptr);
            } else {
                allNumeric = false;
                stringValues.push_back(cell->value.raw);
                formulaASTs.push_back(nullptr);
            }
        }
    } else {
        // Iterate along columns (row is fixed at minRow)
        for (int col = minCol; col <= maxCol; ++col) {
            const Axis* rowAxis = sheet->getRowByPosition(static_cast<uint32_t>(minRow));
            const Axis* colAxis = sheet->getColumnByPosition(static_cast<uint32_t>(col));
            if (!rowAxis || !colAxis) {
                continue;
            }

            const Cell* cell = sheet->getCellAt(colAxis->id, rowAxis->id);

            // Check if cell is a formula first (formula cells may have empty value.raw)
            if (cell != nullptr && cell->isFormula()) {
                allEmpty = false;
                hasFormula = true;
                allNumeric = false;
                formulaASTs.push_back(getFormulaASTClone(cell));
                stringValues.push_back(cell->value.raw);
                continue;
            }

            if (!cell || cell->value.raw.empty()) {
                stringValues.emplace_back("");
                formulaASTs.push_back(nullptr);
                continue;
            }

            allEmpty = false;

            if (cell->value.type == CellValueType::NUMBER) {
                const double val = cell->value.asNumber();
                numericValues.push_back(val);
                stringValues.push_back(cell->value.raw);
                formulaASTs.push_back(nullptr);
            } else {
                allNumeric = false;
                stringValues.push_back(cell->value.raw);
                formulaASTs.push_back(nullptr);
            }
        }
    }

    // Determine pattern type
    if (allEmpty || stringValues.empty()) {
        pattern.type = PatternType::EMPTY;
        return pattern;
    }

    // Formula pattern takes priority - if any cell is a formula, fill with formulas
    if (hasFormula) {
        pattern.type = PatternType::FORMULA;
        pattern.formulaASTs = std::move(formulaASTs);
        pattern.stringValues = stringValues;
        return pattern;
    }

    if (!allNumeric || numericValues.empty()) {
        // String pattern - just repeat
        pattern.type = PatternType::STRING;
        pattern.stringValues = stringValues;
        return pattern;
    }

    // Numeric pattern detection
    if (numericValues.size() == 1) {
        // Single value - constant repeat
        pattern.type = PatternType::CONSTANT;
        pattern.start = numericValues[0];
        pattern.step = 0.0;
        return pattern;
    }

    // Check if values form a linear sequence
    // Calculate step as (last - first) / (count - 1)
    const double first = numericValues[0];
    const double last = numericValues[numericValues.size() - 1];
    const double step = (last - first) / static_cast<double>(numericValues.size() - 1);

    // Verify all intermediate values match the pattern
    bool isLinear = true;
    constexpr double epsilon = 1e-10;
    for (size_t i = 1; i < numericValues.size() - 1; ++i) {
        const double expected = first + step * static_cast<double>(i);
        if (std::abs(numericValues[i] - expected) > epsilon) {
            isLinear = false;
            break;
        }
    }

    if (isLinear && std::abs(step) > epsilon) {
        pattern.type = PatternType::LINEAR;
        pattern.start = last;  // Start from last value for extrapolation
        pattern.step = step;
    } else {
        // Constant or irregular - use last value
        pattern.type = PatternType::CONSTANT;
        pattern.start = last;
        pattern.step = 0.0;
    }

    pattern.stringValues = stringValues;
    return pattern;
}

double extrapolateValue(const DetectedPattern& pattern, int index) {
    switch (pattern.type) {
        case PatternType::LINEAR:
            return pattern.start + pattern.step * static_cast<double>(index);
        case PatternType::CONSTANT:
        case PatternType::STRING:
        case PatternType::EMPTY:
        default:
            return pattern.start;
    }
}

FillResult fillRange(Workbook* workbook, Sheet* sheet, int sourceMinCol, int sourceMinRow,
                     int sourceMaxCol, int sourceMaxRow, int targetMinCol, int targetMinRow,
                     int targetMaxCol, int targetMaxRow) {
    FillResult result;

    if (!workbook || !sheet) {
        result.error = "Invalid workbook or sheet";
        return result;
    }

    // Determine fill direction
    const FillDirection direction =
        getFillDirection(sourceMinCol, sourceMinRow, sourceMaxCol, sourceMaxRow, targetMinCol,
                         targetMinRow, targetMaxCol, targetMaxRow);

    // Fill based on direction
    int cellsFilled = 0;

    if (direction == FillDirection::DOWN) {
        // Fill rows below source
        for (int col = sourceMinCol; col <= sourceMaxCol; ++col) {
            // Get pattern for this column
            const DetectedPattern colPattern =
                detectPattern(sheet, col, sourceMinRow, col, sourceMaxRow, direction);

            for (int row = sourceMaxRow + 1; row <= targetMaxRow; ++row) {
                // Ensure axes exist
                const Axis* rowAxis = sheet->getOrCreateRowByPosition(static_cast<uint32_t>(row));
                const Axis* colAxis =
                    sheet->getOrCreateColumnByPosition(static_cast<uint32_t>(col));
                if (!rowAxis || !colAxis) {
                    continue;
                }

                // Calculate extrapolated value
                const int index = row - sourceMaxRow;  // 1, 2, 3...

                std::string valueStr;
                std::string typeChar = "n";

                if (colPattern.type == PatternType::EMPTY) {
                    continue;
                }
                if (colPattern.type == PatternType::STRING) {
                    if (!colPattern.stringValues.empty()) {
                        const int srcIdx =
                            (index - 1) % static_cast<int>(colPattern.stringValues.size());
                        valueStr = colPattern.stringValues[srcIdx];
                        typeChar = "s";
                    }
                } else if (colPattern.type == PatternType::FORMULA) {
                    // Get fill value for formula (adjusts references)
                    const FillCellInfo fillInfo =
                        getFillValueNonNumeric(colPattern, index, 0, 1, workbook, sheet);
                    if (fillInfo.skip) {
                        continue;
                    }
                    valueStr = fillInfo.value;
                    typeChar = fillInfo.typeChar;
                } else {
                    // Numeric (constant or linear)
                    const double val = extrapolateValue(colPattern, index);
                    std::ostringstream ss;
                    ss << val;
                    valueStr = ss.str();
                    typeChar = "n";
                }

                // Get or create cell
                const Cell* cell = sheet->getOrCreateCellAt(colAxis->id, rowAxis->id);
                if (!cell) {
                    continue;
                }

                // Build payload and apply
                std::string payload;
                if (typeChar == "f") {
                    payload = buildFormulaPayload(valueStr, colAxis->id, rowAxis->id);
                } else {
                    payload = buildCellPayload(typeChar, valueStr, colAxis->id, rowAxis->id);
                }

                // Always use CRDT operations
                const Operation op = makeCellSetValueOp(*workbook, cell->id, payload);
                applyOperation(*workbook, op);

                cellsFilled++;
            }
        }
    } else if (direction == FillDirection::UP) {
        // Fill rows above source
        for (int col = sourceMinCol; col <= sourceMaxCol; ++col) {
            const DetectedPattern colPattern =
                detectPattern(sheet, col, sourceMinRow, col, sourceMaxRow, direction);

            // For UP direction, we extrapolate backwards from the first source value
            // Get the first value in the sequence (at sourceMinRow)
            double firstValue = 0.0;
            if (colPattern.type == PatternType::LINEAR ||
                colPattern.type == PatternType::CONSTANT) {
                // start is the last value, first value = start - step * (count - 1)
                const int count = sourceMaxRow - sourceMinRow + 1;
                firstValue = colPattern.start - colPattern.step * static_cast<double>(count - 1);
            }

            for (int row = sourceMinRow - 1; row >= targetMinRow; --row) {
                const Axis* rowAxis = sheet->getOrCreateRowByPosition(static_cast<uint32_t>(row));
                const Axis* colAxis =
                    sheet->getOrCreateColumnByPosition(static_cast<uint32_t>(col));
                if (!rowAxis || !colAxis) {
                    continue;
                }

                const int index = sourceMinRow - row;  // 1, 2, 3... going up

                std::string valueStr;
                std::string typeChar = "n";

                if (colPattern.type == PatternType::EMPTY) {
                    continue;
                }
                if (colPattern.type == PatternType::STRING) {
                    if (!colPattern.stringValues.empty()) {
                        const int srcIdx =
                            (index - 1) % static_cast<int>(colPattern.stringValues.size());
                        valueStr = colPattern.stringValues[srcIdx];
                        typeChar = "s";
                    }
                } else if (colPattern.type == PatternType::FORMULA) {
                    // Get fill value for formula (adjusts references)
                    // For UP direction, row offset is negative
                    const FillCellInfo fillInfo =
                        getFillValueNonNumeric(colPattern, index, 0, -1, workbook, sheet);
                    if (fillInfo.skip) {
                        continue;
                    }
                    valueStr = fillInfo.value;
                    typeChar = fillInfo.typeChar;
                } else {
                    // For UP direction, extrapolate backwards from the first source value
                    const double val = firstValue - colPattern.step * static_cast<double>(index);
                    std::ostringstream ss;
                    ss << val;
                    valueStr = ss.str();
                    typeChar = "n";
                }

                const Cell* cell = sheet->getOrCreateCellAt(colAxis->id, rowAxis->id);
                if (!cell) {
                    continue;
                }

                // Build payload and apply
                std::string payload;
                if (typeChar == "f") {
                    payload = buildFormulaPayload(valueStr, colAxis->id, rowAxis->id);
                } else {
                    payload = buildCellPayload(typeChar, valueStr, colAxis->id, rowAxis->id);
                }

                // Always use CRDT operations
                const Operation op = makeCellSetValueOp(*workbook, cell->id, payload);
                applyOperation(*workbook, op);

                cellsFilled++;
            }
        }
    } else if (direction == FillDirection::RIGHT) {
        // Fill columns to the right
        for (int row = sourceMinRow; row <= sourceMaxRow; ++row) {
            const DetectedPattern rowPattern =
                detectPattern(sheet, sourceMinCol, row, sourceMaxCol, row, direction);

            for (int col = sourceMaxCol + 1; col <= targetMaxCol; ++col) {
                const Axis* rowAxis = sheet->getOrCreateRowByPosition(static_cast<uint32_t>(row));
                const Axis* colAxis =
                    sheet->getOrCreateColumnByPosition(static_cast<uint32_t>(col));
                if (!rowAxis || !colAxis) {
                    continue;
                }

                const int index = col - sourceMaxCol;

                std::string valueStr;
                std::string typeChar = "n";

                if (rowPattern.type == PatternType::EMPTY) {
                    continue;
                }
                if (rowPattern.type == PatternType::STRING) {
                    if (!rowPattern.stringValues.empty()) {
                        const int srcIdx =
                            (index - 1) % static_cast<int>(rowPattern.stringValues.size());
                        valueStr = rowPattern.stringValues[srcIdx];
                        typeChar = "s";
                    }
                } else if (rowPattern.type == PatternType::FORMULA) {
                    // Get fill value for formula (adjusts references)
                    // For RIGHT direction, col offset is +1 per step
                    const FillCellInfo fillInfo =
                        getFillValueNonNumeric(rowPattern, index, 1, 0, workbook, sheet);
                    if (fillInfo.skip) {
                        continue;
                    }
                    valueStr = fillInfo.value;
                    typeChar = fillInfo.typeChar;
                } else {
                    // Numeric (constant or linear)
                    const double val = extrapolateValue(rowPattern, index);
                    std::ostringstream ss;
                    ss << val;
                    valueStr = ss.str();
                    typeChar = "n";
                }

                const Cell* cell = sheet->getOrCreateCellAt(colAxis->id, rowAxis->id);
                if (!cell) {
                    continue;
                }

                // Build payload and apply
                std::string payload;
                if (typeChar == "f") {
                    payload = buildFormulaPayload(valueStr, colAxis->id, rowAxis->id);
                } else {
                    payload = buildCellPayload(typeChar, valueStr, colAxis->id, rowAxis->id);
                }

                // Always use CRDT operations
                const Operation op = makeCellSetValueOp(*workbook, cell->id, payload);
                applyOperation(*workbook, op);

                cellsFilled++;
            }
        }
    } else if (direction == FillDirection::LEFT) {
        // Fill columns to the left
        for (int row = sourceMinRow; row <= sourceMaxRow; ++row) {
            const DetectedPattern rowPattern =
                detectPattern(sheet, sourceMinCol, row, sourceMaxCol, row, direction);

            // For LEFT direction, we extrapolate backwards from the first source value
            double firstValue = 0.0;
            if (rowPattern.type == PatternType::LINEAR ||
                rowPattern.type == PatternType::CONSTANT) {
                const int count = sourceMaxCol - sourceMinCol + 1;
                firstValue = rowPattern.start - rowPattern.step * static_cast<double>(count - 1);
            }

            for (int col = sourceMinCol - 1; col >= targetMinCol; --col) {
                const Axis* rowAxis = sheet->getOrCreateRowByPosition(static_cast<uint32_t>(row));
                const Axis* colAxis =
                    sheet->getOrCreateColumnByPosition(static_cast<uint32_t>(col));
                if (!rowAxis || !colAxis) {
                    continue;
                }

                const int index = sourceMinCol - col;

                std::string valueStr;
                std::string typeChar = "n";

                if (rowPattern.type == PatternType::EMPTY) {
                    continue;
                }
                if (rowPattern.type == PatternType::STRING) {
                    if (!rowPattern.stringValues.empty()) {
                        const int srcIdx =
                            (index - 1) % static_cast<int>(rowPattern.stringValues.size());
                        valueStr = rowPattern.stringValues[srcIdx];
                        typeChar = "s";
                    }
                } else if (rowPattern.type == PatternType::FORMULA) {
                    // Get fill value for formula (adjusts references)
                    // For LEFT direction, col offset is -1 per step
                    const FillCellInfo fillInfo =
                        getFillValueNonNumeric(rowPattern, index, -1, 0, workbook, sheet);
                    if (fillInfo.skip) {
                        continue;
                    }
                    valueStr = fillInfo.value;
                    typeChar = fillInfo.typeChar;
                } else {
                    // Extrapolate backwards from the first source value
                    const double val = firstValue - rowPattern.step * static_cast<double>(index);
                    std::ostringstream ss;
                    ss << val;
                    valueStr = ss.str();
                    typeChar = "n";
                }

                const Cell* cell = sheet->getOrCreateCellAt(colAxis->id, rowAxis->id);
                if (!cell) {
                    continue;
                }

                // Build payload and apply
                std::string payload;
                if (typeChar == "f") {
                    payload = buildFormulaPayload(valueStr, colAxis->id, rowAxis->id);
                } else {
                    payload = buildCellPayload(typeChar, valueStr, colAxis->id, rowAxis->id);
                }

                // Always use CRDT operations
                const Operation op = makeCellSetValueOp(*workbook, cell->id, payload);
                applyOperation(*workbook, op);

                cellsFilled++;
            }
        }
    }

    result.success = true;
    result.cellsFilled = cellsFilled;
    return result;
}

}  // namespace cells
