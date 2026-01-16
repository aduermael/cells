#include "core/cells/formula_eval.h"

#include <cmath>
#include <cstdlib>

#include <algorithm>
#include <string>
#include <vector>

#include "core/cells/formula_ast.h"
#include "core/cells/formula_functions.h"
#include "core/cells/model.h"
#include "core/cells/named_ranges.h"

namespace cells {

// Forward declarations for evaluation functions
static EvalResult evaluateLiteral(const ASTNode* node);
static EvalResult evaluateCellRef(const CellRefNode* node, EvalContext& ctx);
static EvalResult evaluateBinaryOp(const BinaryOpNode* node, EvalContext& ctx);
static EvalResult evaluateUnaryOp(const UnaryOpNode* node, EvalContext& ctx);
static EvalResult evaluateRangeRef(const RangeRefNode* node, EvalContext& ctx);
static EvalResult evaluateColumnRef(const ColumnRefNode* node, EvalContext& ctx);
static EvalResult evaluateRowRef(const RowRefNode* node, EvalContext& ctx);
static EvalResult evaluateColumnRangeRef(const ColumnRangeRefNode* node, EvalContext& ctx);
static EvalResult evaluateRowRangeRef(const RowRangeRefNode* node, EvalContext& ctx);
static EvalResult evaluateFunctionCall(const FunctionCallNode* node, EvalContext& ctx);
static EvalResult evaluateSpillRangeRef(const SpillRangeRefNode* node, EvalContext& ctx);
static EvalResult evaluateNamedRef(const NamedRefNode* node, EvalContext& ctx);

// Convert a CellValue to an EvalResult
static EvalResult cellValueToEvalResult(const CellValue& value) {
    switch (value.type) {
        case CellValueType::NUMBER:
        case CellValueType::FORMULA_NUMBER:
            return EvalResult::Number(std::strtod(value.raw.c_str(), nullptr));
        case CellValueType::STRING:
        case CellValueType::FORMULA_STRING:
            // Empty string is treated as empty cell (returns 0 in numeric context)
            if (value.raw.empty()) {
                return EvalResult::Empty();
            }
            return EvalResult::String(value.asString());
        case CellValueType::BOOLEAN:
        case CellValueType::FORMULA_BOOLEAN:
            return EvalResult::Boolean(value.raw == "true");
        case CellValueType::ERROR:
        case CellValueType::FORMULA_ERROR:
            return EvalResult::Error(value.error);
        case CellValueType::FORMULA_EMPTY:
            return EvalResult::Empty();
        case CellValueType::FORMULA:
            // Unevaluated formula - parse raw value
            if (value.error != CellError::NONE) {
                return EvalResult::Error(value.error);
            }
            // Try to parse as number first (using strtod to avoid exceptions in WASM)
            if (!value.raw.empty()) {
                char* endPtr = nullptr;  // NOLINT(misc-const-correctness)
                const double val = std::strtod(value.raw.c_str(), &endPtr);
                // Check if entire string was consumed (successful number parse)
                if (endPtr != nullptr && *endPtr == '\0') {
                    return EvalResult::Number(val);
                }
                return EvalResult::String(value.raw);
            }
            return EvalResult::Empty();
        case CellValueType::DATE:
        case CellValueType::DATE_TIME:
            // Dates are stored as serial numbers (days since epoch)
            return EvalResult::Number(std::strtod(value.raw.c_str(), nullptr));
    }
    return EvalResult::Empty();
}

// Evaluate a literal node (NUMBER, STRING, BOOLEAN)
static EvalResult evaluateLiteral(const ASTNode* node) {
    switch (node->type) {
        case ASTNodeType::NUMBER_LITERAL:
            return EvalResult::Number(static_cast<const NumberLiteralNode*>(node)->value);
        case ASTNodeType::STRING_LITERAL:
            return EvalResult::String(static_cast<const StringLiteralNode*>(node)->value);
        case ASTNodeType::BOOLEAN_LITERAL:
            return EvalResult::Boolean(static_cast<const BooleanLiteralNode*>(node)->value);
        default:
            return EvalResult::Error(CellError::VALUE);
    }
}

// Evaluate a cell reference
static EvalResult evaluateCellRef(const CellRefNode* node, EvalContext& ctx) {
    if (!ctx.sheet) {
        return EvalResult::Error(CellError::REF);
    }

    // Get the cell ID (should be resolved already)
    const ID cellId(node->cellId);
    if (cellId.isNull()) {
        // Unresolved reference
        return EvalResult::Error(CellError::REF);
    }

    // Check for circular reference
    if (ctx.evaluatingCells && (ctx.evaluatingCells->count(cellId) != 0u)) {
        return EvalResult::Error(CellError::CIRCULAR);
    }

    // Get the target sheet (may be different from ctx.sheet for cross-sheet refs)
    Sheet* targetSheet = ctx.sheet;
    if (!node->sheetName.empty() && ctx.workbook) {
        targetSheet = ctx.workbook->getSheetByName(node->sheetName);
        if (!targetSheet) {
            // Referenced sheet doesn't exist
            return EvalResult::Error(CellError::REF);
        }
    }

    Cell* cell = targetSheet->getCell(cellId);
    if (!cell) {
        // Empty cell reference returns 0
        return EvalResult::Number(0.0);
    }

    // If cell has a formula that needs evaluation, evaluate it
    Formula* formula = cell->getFormula();
    if (formula && formula->dirty && formula->ast) {
        // Mark that we're evaluating this cell (circular reference detection)
        bool addedToSet = false;
        if (ctx.evaluatingCells) {
            ctx.evaluatingCells->insert(cellId);
            addedToSet = true;
        }

        // Check recursion depth
        if (ctx.recursionDepth >= EvalContext::MAX_RECURSION) {
            if (addedToSet) {
                ctx.evaluatingCells->erase(cellId);
            }
            return EvalResult::Error(CellError::CIRCULAR);
        }

        // Recursively evaluate on the target sheet
        EvalContext subCtx = ctx;
        subCtx.sheet = targetSheet;  // Evaluate on the cell's sheet, not the caller's
        subCtx.currentCellId = cellId;
        subCtx.recursionDepth++;

        EvalResult result = evaluate(formula->ast, subCtx);

        // Store result in cell value using FORMULA_* result types
        // This preserves the formula nature while indicating the computed result type.
        if (result.isError()) {
            cell->value = CellValue(result.getError());
            cell->value.type = CellValueType::FORMULA_ERROR;
        } else if (result.isNumber()) {
            cell->value = CellValue(result.getNumber());
            cell->value.type = CellValueType::FORMULA_NUMBER;
        } else if (result.isString()) {
            cell->value = CellValue(result.getString());
            cell->value.type = CellValueType::FORMULA_STRING;
        } else if (result.isBoolean()) {
            cell->value = CellValue(result.getBoolean());
            cell->value.type = CellValueType::FORMULA_BOOLEAN;
        } else {
            cell->value = CellValue("");  // Empty
            cell->value.type = CellValueType::FORMULA_EMPTY;
        }

        // Mark as clean
        formula->dirty = false;

        // Remove from evaluating set
        if (addedToSet) {
            ctx.evaluatingCells->erase(cellId);
        }

        return result;
    }

    // Return the cell's current value
    return cellValueToEvalResult(cell->value);
}

// Compare two EvalResults for equality
static EvalResult compareEqual(const EvalResult& left, const EvalResult& right) {
    // Type coercion for comparison:
    // If both are same type, compare directly
    // If one is number and one is string that looks like number, coerce
    // Booleans compare as numbers (true=1, false=0) when compared to numbers

    if (left.type == right.type) {
        switch (left.type) {
            case EvalResult::Type::NUMBER:
                return EvalResult::Boolean(left.numberValue == right.numberValue);
            case EvalResult::Type::STRING:
                // Case-insensitive string comparison (Excel behavior)
                {
                    std::string l = left.stringValue;
                    std::string r = right.stringValue;
                    for (auto& c : l) {
                        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                    }
                    for (auto& c : r) {
                        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                    }
                    return EvalResult::Boolean(l == r);
                }
            case EvalResult::Type::BOOLEAN:
                return EvalResult::Boolean(left.boolValue == right.boolValue);
            default:
                return EvalResult::Boolean(false);
        }
    }

    // Mixed type comparison - try to coerce to number
    const EvalResult leftNum = left.toNumber();
    const EvalResult rightNum = right.toNumber();
    if (!leftNum.isError() && !rightNum.isError()) {
        return EvalResult::Boolean(leftNum.numberValue == rightNum.numberValue);
    }

    // Different types that can't be coerced are not equal
    return EvalResult::Boolean(false);
}

// Compare two EvalResults (returns negative, zero, or positive)
static int compareValues(const EvalResult& left, const EvalResult& right) {
    // Same type comparison
    if (left.type == right.type) {
        switch (left.type) {
            case EvalResult::Type::NUMBER:
                if (left.numberValue < right.numberValue) {
                    return -1;
                }
                if (left.numberValue > right.numberValue) {
                    return 1;
                }
                return 0;
            case EvalResult::Type::STRING:
                return left.stringValue.compare(right.stringValue);
            case EvalResult::Type::BOOLEAN:
                if (left.boolValue == right.boolValue) {
                    return 0;
                }
                return left.boolValue ? 1 : -1;  // true > false
            default:
                return 0;
        }
    }

    // Mixed type - try to coerce to number
    const EvalResult leftNum = left.toNumber();
    const EvalResult rightNum = right.toNumber();
    if (!leftNum.isError() && !rightNum.isError()) {
        if (leftNum.numberValue < rightNum.numberValue) {
            return -1;
        }
        if (leftNum.numberValue > rightNum.numberValue) {
            return 1;
        }
        return 0;
    }

    // Fallback: type ordering (number < string < boolean)
    auto typeOrder = [](EvalResult::Type t) -> int {
        switch (t) {
            case EvalResult::Type::NUMBER:
                return 0;
            case EvalResult::Type::STRING:
                return 1;
            case EvalResult::Type::BOOLEAN:
                return 2;
            default:
                return 3;
        }
    };
    return typeOrder(left.type) - typeOrder(right.type);
}

// Evaluate a binary operation
static EvalResult evaluateBinaryOp(const BinaryOpNode* node, EvalContext& ctx) {
    EvalResult left = evaluate(node->left.get(), ctx);
    EvalResult right = evaluate(node->right.get(), ctx);

    // Error propagation
    if (left.isError()) {
        return left;
    }
    if (right.isError()) {
        return right;
    }

    switch (node->op) {
        case BinaryOp::ADD: {
            // NOLINTNEXTLINE(misc-const-correctness)
            EvalResult leftNum = left.toNumber();
            // NOLINTNEXTLINE(misc-const-correctness)
            EvalResult rightNum = right.toNumber();
            if (leftNum.isError()) {
                return leftNum;
            }
            if (rightNum.isError()) {
                return rightNum;
            }
            return EvalResult::Number(leftNum.numberValue + rightNum.numberValue);
        }
        case BinaryOp::SUBTRACT: {
            // NOLINTNEXTLINE(misc-const-correctness)
            EvalResult leftNum = left.toNumber();
            // NOLINTNEXTLINE(misc-const-correctness)
            EvalResult rightNum = right.toNumber();
            if (leftNum.isError()) {
                return leftNum;
            }
            if (rightNum.isError()) {
                return rightNum;
            }
            return EvalResult::Number(leftNum.numberValue - rightNum.numberValue);
        }
        case BinaryOp::MULTIPLY: {
            // NOLINTNEXTLINE(misc-const-correctness)
            EvalResult leftNum = left.toNumber();
            // NOLINTNEXTLINE(misc-const-correctness)
            EvalResult rightNum = right.toNumber();
            if (leftNum.isError()) {
                return leftNum;
            }
            if (rightNum.isError()) {
                return rightNum;
            }
            return EvalResult::Number(leftNum.numberValue * rightNum.numberValue);
        }
        case BinaryOp::DIVIDE: {
            // NOLINTNEXTLINE(misc-const-correctness)
            EvalResult leftNum = left.toNumber();
            // NOLINTNEXTLINE(misc-const-correctness)
            EvalResult rightNum = right.toNumber();
            if (leftNum.isError()) {
                return leftNum;
            }
            if (rightNum.isError()) {
                return rightNum;
            }
            if (rightNum.numberValue == 0.0) {
                return EvalResult::Error(CellError::DIV);
            }
            return EvalResult::Number(leftNum.numberValue / rightNum.numberValue);
        }
        case BinaryOp::POWER: {
            // NOLINTNEXTLINE(misc-const-correctness)
            EvalResult leftNum = left.toNumber();
            // NOLINTNEXTLINE(misc-const-correctness)
            EvalResult rightNum = right.toNumber();
            if (leftNum.isError()) {
                return leftNum;
            }
            if (rightNum.isError()) {
                return rightNum;
            }
            const double result = std::pow(leftNum.numberValue, rightNum.numberValue);
            if (std::isnan(result) || std::isinf(result)) {
                return EvalResult::Error(CellError::NUM);
            }
            return EvalResult::Number(result);
        }
        case BinaryOp::CONCAT: {
            // NOLINTNEXTLINE(misc-const-correctness)
            EvalResult leftStr = left.toString();
            // NOLINTNEXTLINE(misc-const-correctness)
            EvalResult rightStr = right.toString();
            if (leftStr.isError()) {
                return leftStr;
            }
            if (rightStr.isError()) {
                return rightStr;
            }
            return EvalResult::String(leftStr.stringValue + rightStr.stringValue);
        }
        case BinaryOp::EQUAL:
            return compareEqual(left, right);
        case BinaryOp::NOT_EQUAL: {
            // NOLINTNEXTLINE(misc-const-correctness)
            EvalResult eq = compareEqual(left, right);
            if (eq.isError()) {
                return eq;
            }
            return EvalResult::Boolean(!eq.boolValue);
        }
        case BinaryOp::LESS:
            return EvalResult::Boolean(compareValues(left, right) < 0);
        case BinaryOp::LESS_EQUAL:
            return EvalResult::Boolean(compareValues(left, right) <= 0);
        case BinaryOp::GREATER:
            return EvalResult::Boolean(compareValues(left, right) > 0);
        case BinaryOp::GREATER_EQUAL:
            return EvalResult::Boolean(compareValues(left, right) >= 0);
    }

    return EvalResult::Error(CellError::VALUE);
}

// Evaluate a unary operation
static EvalResult evaluateUnaryOp(const UnaryOpNode* node, EvalContext& ctx) {
    // NOLINTNEXTLINE(misc-const-correctness)
    EvalResult operand = evaluate(node->operand.get(), ctx);
    if (operand.isError()) {
        return operand;
    }

    // NOLINTNEXTLINE(misc-const-correctness)
    EvalResult num = operand.toNumber();
    if (num.isError()) {
        return num;
    }

    switch (node->op) {
        case UnaryOp::NEGATE:
            return EvalResult::Number(-num.numberValue);
        case UnaryOp::POSITIVE:
            return EvalResult::Number(num.numberValue);
    }

    return EvalResult::Error(CellError::VALUE);
}

// =============================================================================
// Function Call Evaluation
// =============================================================================

// Evaluate a function call
static EvalResult evaluateFunctionCall(const FunctionCallNode* node, EvalContext& ctx) {
    // Collect raw AST node pointers for the arguments
    // Functions receive unevaluated AST nodes so they can implement lazy evaluation
    std::vector<const ASTNode*> args;
    args.reserve(node->args.size());
    for (const auto& arg : node->args) {
        args.push_back(arg.get());
    }

    // Call the function through the registry
    return FunctionRegistry::instance().call(node->name, args, ctx);
}

// =============================================================================
// Range Reference Evaluation
// =============================================================================

// Evaluate a range reference (A1:C3)
static EvalResult evaluateRangeRef(const RangeRefNode* node, EvalContext& ctx) {
    if (!ctx.sheet) {
        return EvalResult::Error(CellError::REF);
    }

    // Try to look up columns using resolved cell IDs first (from UUID formula),
    // then fall back to column name lookup (for A1 notation formulas)
    const Axis* startCol = nullptr;
    const Axis* endCol = nullptr;
    uint32_t startRowPos = 0;
    uint32_t endRowPos = 0;

    // Try resolved cell IDs first
    if (!node->topLeft->cellId.empty()) {
        const Cell* startCell = ctx.sheet->getCell(ID(node->topLeft->cellId));
        if (startCell) {
            startCol = ctx.sheet->getColumn(startCell->colId);
            const Axis* startRow = ctx.sheet->getRow(startCell->rowId);
            if (startRow) {
                startRowPos = startRow->position;
            }
        }
    }
    if (!node->bottomRight->cellId.empty()) {
        const Cell* endCell = ctx.sheet->getCell(ID(node->bottomRight->cellId));
        if (endCell) {
            endCol = ctx.sheet->getColumn(endCell->colId);
            const Axis* endRow = ctx.sheet->getRow(endCell->rowId);
            if (endRow) {
                endRowPos = endRow->position;
            }
        }
    }

    // Fall back to column name lookup if cell IDs weren't resolved
    if (!startCol) {
        startCol = ctx.sheet->getColumnByName(node->topLeft->column);
        // Use row position from AST (1-based in AST, 0-based for storage)
        startRowPos = static_cast<uint32_t>(node->topLeft->row - 1);
    }
    if (!endCol) {
        endCol = ctx.sheet->getColumnByName(node->bottomRight->column);
        endRowPos = static_cast<uint32_t>(node->bottomRight->row - 1);
    }

    // Columns must exist
    if (!startCol || !endCol) {
        return EvalResult::Error(CellError::REF);
    }

    // Ensure proper ordering (swap if needed)
    ID startColId = startCol->id;
    ID endColId = endCol->id;
    if (startCol->position > endCol->position) {
        std::swap(startColId, endColId);
    }
    if (startRowPos > endRowPos) {
        std::swap(startRowPos, endRowPos);
    }

    return EvalResult::CellRange(startColId, endColId, startRowPos, endRowPos);
}

// Evaluate a whole column reference (A:A)
static EvalResult evaluateColumnRef(const ColumnRefNode* node, EvalContext& ctx) {
    if (!ctx.sheet) {
        return EvalResult::Error(CellError::REF);
    }

    // Look up column by name or resolved ID
    const Axis* col = nullptr;
    if (!node->columnId.empty()) {
        col = ctx.sheet->getColumn(ID(node->columnId));
    }
    if (!col) {
        col = ctx.sheet->getColumnByName(node->column);
    }

    if (!col) {
        return EvalResult::Error(CellError::REF);
    }

    return EvalResult::SingleColumn(col->id);
}

// Evaluate a whole row reference (1:1)
static EvalResult evaluateRowRef(const RowRefNode* node, EvalContext& ctx) {
    if (!ctx.sheet) {
        return EvalResult::Error(CellError::REF);
    }

    // Look up row by position or resolved ID
    const Axis* row = nullptr;
    if (!node->rowId.empty()) {
        row = ctx.sheet->getRow(ID(node->rowId));
    }
    if (!row) {
        row = ctx.sheet->getRowByPosition(static_cast<uint32_t>(node->row - 1));
    }

    if (!row) {
        return EvalResult::Error(CellError::REF);
    }

    return EvalResult::SingleRow(row->id);
}

// Evaluate a column range reference (A:C)
static EvalResult evaluateColumnRangeRef(const ColumnRangeRefNode* node, EvalContext& ctx) {
    if (!ctx.sheet) {
        return EvalResult::Error(CellError::REF);
    }

    // Look up columns by name or resolved ID
    const Axis* startCol = nullptr;
    const Axis* endCol = nullptr;

    if (!node->startColumnId.empty()) {
        startCol = ctx.sheet->getColumn(ID(node->startColumnId));
    }
    if (!startCol) {
        startCol = ctx.sheet->getColumnByName(node->startColumn);
    }

    if (!node->endColumnId.empty()) {
        endCol = ctx.sheet->getColumn(ID(node->endColumnId));
    }
    if (!endCol) {
        endCol = ctx.sheet->getColumnByName(node->endColumn);
    }

    if (!startCol || !endCol) {
        return EvalResult::Error(CellError::REF);
    }

    // Ensure proper ordering
    if (startCol->position > endCol->position) {
        std::swap(startCol, endCol);
    }

    return EvalResult::ColumnRange(startCol->id, endCol->id);
}

// Evaluate a row range reference (1:5)
static EvalResult evaluateRowRangeRef(const RowRangeRefNode* node, EvalContext& ctx) {
    if (!ctx.sheet) {
        return EvalResult::Error(CellError::REF);
    }

    // Look up rows by position or resolved ID
    const Axis* startRow = nullptr;
    const Axis* endRow = nullptr;

    if (!node->startRowId.empty()) {
        startRow = ctx.sheet->getRow(ID(node->startRowId));
    }
    if (!startRow) {
        startRow = ctx.sheet->getRowByPosition(static_cast<uint32_t>(node->startRow - 1));
    }

    if (!node->endRowId.empty()) {
        endRow = ctx.sheet->getRow(ID(node->endRowId));
    }
    if (!endRow) {
        endRow = ctx.sheet->getRowByPosition(static_cast<uint32_t>(node->endRow - 1));
    }

    if (!startRow || !endRow) {
        return EvalResult::Error(CellError::REF);
    }

    // Ensure proper ordering
    if (startRow->position > endRow->position) {
        std::swap(startRow, endRow);
    }

    return EvalResult::RowRange(startRow->id, endRow->id);
}

// =============================================================================
// Range Iteration Utilities
// =============================================================================

// Helper to get cell value as EvalResult
static EvalResult getCellEvalResult(Cell* cell, EvalContext& ctx) {
    if (!cell) {
        return EvalResult::Empty();
    }

    // If cell has a formula that needs evaluation, evaluate it first
    const Formula* formula = cell->getFormula();
    if (formula && formula->dirty && formula->ast) {
        // Use the existing evaluateCellRef logic for consistency
        CellRefNode tempNode("", 0, false, false);
        tempNode.cellId = cell->id.toString();
        return evaluateCellRef(&tempNode, ctx);
    }

    return cellValueToEvalResult(cell->value);
}

// Iterate over a bounded cell range (A1:C3)
static size_t iterateCellRange(const RangeBounds& bounds, Sheet* sheet,
                               const RangeCellCallback& callback) {
    const Axis* startCol = sheet->getColumn(bounds.startColId);
    const Axis* endCol = sheet->getColumn(bounds.endColId);

    if (!startCol || !endCol) {
        return 0;
    }

    // Collect columns in the range, sorted by position
    std::vector<std::pair<uint32_t, ID>> cols;
    for (const auto& [id, axis] : sheet->columns) {
        if (axis->position >= startCol->position && axis->position <= endCol->position) {
            cols.emplace_back(axis->position, axis->id);
        }
    }

    // Collect rows in the position range (use position bounds, not row IDs)
    // This allows ranges to work even with sparse rows
    std::vector<std::pair<uint32_t, ID>> rows;
    for (const auto& [id, axis] : sheet->rows) {
        if (axis->position >= bounds.startRowPos && axis->position <= bounds.endRowPos) {
            rows.emplace_back(axis->position, axis->id);
        }
    }

    // Sort by position (first element of pair)
    std::sort(cols.begin(), cols.end());
    std::sort(rows.begin(), rows.end());

    size_t count = 0;
    // Iterate in row-major order (A1, B1, C1, A2, B2, C2...)
    for (const auto& [rowPos, rowId] : rows) {
        for (const auto& [colPos, colId] : cols) {
            Cell* cell = sheet->getCellAt(colId, rowId);
            if (!callback(cell, colPos, rowPos)) {
                return count;
            }
            count++;
        }
    }

    return count;
}

// Iterate over a whole column (A:A) - only populated cells
static size_t iterateSingleColumn(const RangeBounds& bounds, Sheet* sheet,
                                  const RangeCellCallback& callback) {
    const Axis* col = sheet->getColumn(bounds.startColId);
    if (!col) {
        return 0;
    }

    size_t count = 0;
    // Collect all cells in this column: (row position, cell ID)
    std::vector<std::pair<uint32_t, ID>> cellsInCol;
    for (const auto& [id, cell] : sheet->cells) {
        if (cell->colId == col->id) {
            const Axis* row = sheet->getRow(cell->rowId);
            if (row) {
                cellsInCol.emplace_back(row->position, cell->id);
            }
        }
    }

    // Sort by row position
    std::sort(cellsInCol.begin(), cellsInCol.end());

    for (const auto& [rowPos, cellId] : cellsInCol) {
        Cell* cell = sheet->getCell(cellId);
        if (!callback(cell, col->position, rowPos)) {
            return count;
        }
        count++;
    }

    return count;
}

// Iterate over a whole row (1:1) - only populated cells
static size_t iterateSingleRow(const RangeBounds& bounds, Sheet* sheet,
                               const RangeCellCallback& callback) {
    const Axis* row = sheet->getRow(bounds.startRowId);
    if (!row) {
        return 0;
    }

    size_t count = 0;
    // Collect all cells in this row: (col position, cell ID)
    std::vector<std::pair<uint32_t, ID>> cellsInRow;
    for (const auto& [id, cell] : sheet->cells) {
        if (cell->rowId == row->id) {
            const Axis* col = sheet->getColumn(cell->colId);
            if (col) {
                cellsInRow.emplace_back(col->position, cell->id);
            }
        }
    }

    // Sort by column position
    std::sort(cellsInRow.begin(), cellsInRow.end());

    for (const auto& [colPos, cellId] : cellsInRow) {
        Cell* cell = sheet->getCell(cellId);
        if (!callback(cell, colPos, row->position)) {
            return count;
        }
        count++;
    }

    return count;
}

// Iterate over a column range (A:C) - only populated cells
static size_t iterateColumnRange(const RangeBounds& bounds, Sheet* sheet,
                                 const RangeCellCallback& callback) {
    const Axis* startCol = sheet->getColumn(bounds.startColId);
    const Axis* endCol = sheet->getColumn(bounds.endColId);
    if (!startCol || !endCol) {
        return 0;
    }

    size_t count = 0;
    // Collect all cells in these columns: (row, col, cell ID)
    std::vector<std::tuple<uint32_t, uint32_t, ID>> cellsInRange;
    for (const auto& [id, cell] : sheet->cells) {
        const Axis* col = sheet->getColumn(cell->colId);
        if (col && col->position >= startCol->position && col->position <= endCol->position) {
            const Axis* row = sheet->getRow(cell->rowId);
            if (row) {
                cellsInRange.emplace_back(row->position, col->position, cell->id);
            }
        }
    }

    // Sort by row, then column (row-major order)
    std::sort(cellsInRange.begin(), cellsInRange.end());

    for (const auto& [rowPos, colPos, cellId] : cellsInRange) {
        Cell* cell = sheet->getCell(cellId);
        if (!callback(cell, colPos, rowPos)) {
            return count;
        }
        count++;
    }

    return count;
}

// Iterate over a row range (1:5) - only populated cells
static size_t iterateRowRange(const RangeBounds& bounds, Sheet* sheet,
                              const RangeCellCallback& callback) {
    const Axis* startRow = sheet->getRow(bounds.startRowId);
    const Axis* endRow = sheet->getRow(bounds.endRowId);
    if (!startRow || !endRow) {
        return 0;
    }

    size_t count = 0;
    // Collect all cells in these rows: (row, col, cell ID)
    std::vector<std::tuple<uint32_t, uint32_t, ID>> cellsInRange;
    for (const auto& [id, cell] : sheet->cells) {
        const Axis* row = sheet->getRow(cell->rowId);
        if (row && row->position >= startRow->position && row->position <= endRow->position) {
            const Axis* col = sheet->getColumn(cell->colId);
            if (col) {
                cellsInRange.emplace_back(row->position, col->position, cell->id);
            }
        }
    }

    // Sort by row, then column (row-major order)
    std::sort(cellsInRange.begin(), cellsInRange.end());

    for (const auto& [rowPos, colPos, cellId] : cellsInRange) {
        Cell* cell = sheet->getCell(cellId);
        if (!callback(cell, colPos, rowPos)) {
            return count;
        }
        count++;
    }

    return count;
}

// Public function: Iterate over all cells in a range
size_t iterateRange(const RangeBounds& bounds, Sheet* sheet, const RangeCellCallback& callback) {
    if (!sheet) {
        return 0;
    }

    switch (bounds.type) {
        case RangeType::CELL_RANGE:
            return iterateCellRange(bounds, sheet, callback);
        case RangeType::COLUMN:
            return iterateSingleColumn(bounds, sheet, callback);
        case RangeType::ROW:
            return iterateSingleRow(bounds, sheet, callback);
        case RangeType::COLUMN_RANGE:
            return iterateColumnRange(bounds, sheet, callback);
        case RangeType::ROW_RANGE:
            return iterateRowRange(bounds, sheet, callback);
    }

    return 0;
}

// Public function: Collect all EvalResults from cells in a range
std::vector<EvalResult> collectRangeValues(const RangeBounds& bounds, EvalContext& ctx) {
    std::vector<EvalResult> results;

    if (!ctx.sheet) {
        return results;
    }

    // Pre-allocate if we can estimate size
    const size_t estimatedSize = getRangeSize(bounds, ctx.sheet);
    if (estimatedSize > 0) {
        results.reserve(estimatedSize);
    }

    iterateRange(bounds, ctx.sheet, [&results, &ctx](Cell* cell, uint32_t, uint32_t) {
        results.push_back(getCellEvalResult(cell, ctx));
        return true;  // Continue iteration
    });

    return results;
}

// Public function: Get the count of cells in a range
size_t getRangeSize(const RangeBounds& bounds, Sheet* sheet) {
    if (!sheet) {
        return 0;
    }

    switch (bounds.type) {
        case RangeType::CELL_RANGE: {
            const Axis* startCol = sheet->getColumn(bounds.startColId);
            const Axis* endCol = sheet->getColumn(bounds.endColId);

            if (!startCol || !endCol) {
                return 0;
            }

            // Count columns in range
            size_t colCount = 0;
            for (const auto& [id, axis] : sheet->columns) {
                if (axis->position >= startCol->position && axis->position <= endCol->position) {
                    colCount++;
                }
            }

            // Count rows in position range (using position bounds, not row IDs)
            size_t rowCount = 0;
            for (const auto& [id, axis] : sheet->rows) {
                if (axis->position >= bounds.startRowPos && axis->position <= bounds.endRowPos) {
                    rowCount++;
                }
            }

            return colCount * rowCount;
        }
        case RangeType::COLUMN:
            [[fallthrough]];
        case RangeType::ROW:
            [[fallthrough]];
        case RangeType::COLUMN_RANGE:
            [[fallthrough]];
        case RangeType::ROW_RANGE: {
            // For unbounded ranges, count populated cells
            size_t count = 0;
            iterateRange(bounds, sheet, [&count](Cell*, uint32_t, uint32_t) {
                count++;
                return true;
            });
            return count;
        }
    }

    return 0;
}

// Main evaluation function
EvalResult evaluate(const ASTNode* node, EvalContext& ctx) {
    if (!node) {
        return EvalResult::Error(CellError::VALUE);
    }

    switch (node->type) {
        // Literals
        case ASTNodeType::NUMBER_LITERAL:
        case ASTNodeType::STRING_LITERAL:
        case ASTNodeType::BOOLEAN_LITERAL:
            return evaluateLiteral(node);

        // References
        case ASTNodeType::CELL_REF:
            return evaluateCellRef(static_cast<const CellRefNode*>(node), ctx);

        // Operators
        case ASTNodeType::BINARY_OP:
            return evaluateBinaryOp(static_cast<const BinaryOpNode*>(node), ctx);

        case ASTNodeType::UNARY_OP:
            return evaluateUnaryOp(static_cast<const UnaryOpNode*>(node), ctx);

        // Range references - evaluate to Range result type
        // They're consumed by functions like SUM, AVERAGE, etc.
        case ASTNodeType::RANGE_REF:
            return evaluateRangeRef(static_cast<const RangeRefNode*>(node), ctx);
        case ASTNodeType::COLUMN_REF:
            return evaluateColumnRef(static_cast<const ColumnRefNode*>(node), ctx);
        case ASTNodeType::ROW_REF:
            return evaluateRowRef(static_cast<const RowRefNode*>(node), ctx);
        case ASTNodeType::COLUMN_RANGE_REF:
            return evaluateColumnRangeRef(static_cast<const ColumnRangeRefNode*>(node), ctx);
        case ASTNodeType::ROW_RANGE_REF:
            return evaluateRowRangeRef(static_cast<const RowRangeRefNode*>(node), ctx);

        // Function calls - dispatch to function registry
        case ASTNodeType::FUNCTION_CALL:
            return evaluateFunctionCall(static_cast<const FunctionCallNode*>(node), ctx);

        // Spill range reference (A1#) - evaluates to the spill range of the anchor cell
        case ASTNodeType::SPILL_RANGE_REF:
            return evaluateSpillRangeRef(static_cast<const SpillRangeRefNode*>(node), ctx);

        // Named references
        case ASTNodeType::NAMED_REF:
            return evaluateNamedRef(static_cast<const NamedRefNode*>(node), ctx);

        // Error node - parse the error message to determine the correct error type
        case ASTNodeType::ERROR_NODE: {
            const auto* errorNode = static_cast<const ErrorNode*>(node);
            // Use stringToError to convert the error message (e.g., "#REF!") to CellError
            CellError error = stringToError(errorNode->message);
            // Default to VALUE error if the message doesn't match a known error type
            if (error == CellError::NONE) {
                error = CellError::VALUE;
            }
            return EvalResult::Error(error);
        }
    }

    return EvalResult::Error(CellError::VALUE);
}

// Evaluate a spill range reference (A1#)
// Returns a RANGE result covering the anchor cell plus all spilled cells.
// If the anchor cell has no spill data (non-spilling formula), returns just the anchor as a range.
static EvalResult evaluateSpillRangeRef(const SpillRangeRefNode* node, EvalContext& ctx) {
    if (!ctx.sheet || !node->anchor) {
        return EvalResult::Error(CellError::REF);
    }

    // Get the anchor cell ID from the CellRefNode
    const ID anchorCellId(node->anchor->cellId);
    if (anchorCellId.isNull()) {
        return EvalResult::Error(CellError::REF);
    }

    // Look up the spill info for this cell
    const SpillInfo* spillInfo = ctx.sheet->getSpillInfo(anchorCellId);

    if (!spillInfo || spillInfo->spilledPositions.empty()) {
        // No spill data - the anchor cell is not a spill master or has no spilled values.
        // In Excel, this would be a #SPILL! error if the formula should spill but doesn't,
        // or it could just return the single cell as a range.
        // For now, we return the single cell as a 1x1 range.
        // We need to get the anchor cell's column and row IDs.
        const Cell* anchorCell = ctx.sheet->getCell(anchorCellId);
        if (!anchorCell) {
            return EvalResult::Error(CellError::REF);
        }

        // Get position info from the cell
        const Axis* col = ctx.sheet->getColumn(anchorCell->colId);
        const Axis* row = ctx.sheet->getRow(anchorCell->rowId);
        if (!col || !row) {
            return EvalResult::Error(CellError::REF);
        }

        // Return a 1x1 cell range for the anchor
        return EvalResult::CellRange(anchorCell->colId, anchorCell->colId, row->position,
                                     row->position);
    }

    // We have spill data. Build a range that covers the anchor + all spilled positions.
    // The SpillInfo stores spilledPositions as (colId, rowId) pairs.
    // We need to find the bounding rectangle.

    // First, get the anchor cell's position
    const Cell* anchorCell = ctx.sheet->getCell(anchorCellId);
    if (!anchorCell) {
        return EvalResult::Error(CellError::REF);
    }

    const Axis* anchorCol = ctx.sheet->getColumn(anchorCell->colId);
    const Axis* anchorRow = ctx.sheet->getRow(anchorCell->rowId);
    if (!anchorCol || !anchorRow) {
        return EvalResult::Error(CellError::REF);
    }

    // Start with anchor position as bounds
    uint32_t minColPos = anchorCol->position;
    uint32_t maxColPos = anchorCol->position;
    uint32_t minRowPos = anchorRow->position;
    uint32_t maxRowPos = anchorRow->position;
    ID minColId = anchorCell->colId;
    ID maxColId = anchorCell->colId;

    // Expand bounds to include all spilled positions
    for (const auto& [colId, rowId] : spillInfo->spilledPositions) {
        const Axis* col = ctx.sheet->getColumn(colId);
        const Axis* row = ctx.sheet->getRow(rowId);
        if (col && row) {
            if (col->position < minColPos) {
                minColPos = col->position;
                minColId = colId;
            }
            if (col->position > maxColPos) {
                maxColPos = col->position;
                maxColId = colId;
            }
            if (row->position < minRowPos) {
                minRowPos = row->position;
            }
            if (row->position > maxRowPos) {
                maxRowPos = row->position;
            }
        }
    }

    // Return the bounding range
    return EvalResult::CellRange(minColId, maxColId, minRowPos, maxRowPos);
}

// Evaluate a named range reference
// Looks up the named range in the registry and returns the appropriate value/range
static EvalResult evaluateNamedRef(const NamedRefNode* node, EvalContext& ctx) {
    if (!ctx.namedRanges) {
        // No named range registry available
        return EvalResult::Error(CellError::NAME);
    }

    // Resolve the named range using the current sheet's ID for scoping
    const ID currentSheetId = ctx.sheet ? ctx.sheet->id : ID();
    const NamedRange* namedRange = ctx.namedRanges->resolve(node->name, currentSheetId);

    if (!namedRange) {
        // Named range not found
        return EvalResult::Error(CellError::NAME);
    }

    // Get the target sheet (if specified)
    Sheet* targetSheet = ctx.sheet;
    if (!namedRange->target.sheetId.isNull() && ctx.workbook) {
        // Named range targets a specific sheet
        for (const auto& s : ctx.workbook->sheets) {
            if (s->id == namedRange->target.sheetId) {
                targetSheet = s.get();
                break;
            }
        }
    }

    if (!targetSheet) {
        return EvalResult::Error(CellError::REF);
    }

    // Evaluate based on target type
    switch (namedRange->target.type) {
        case NamedRangeTarget::Type::CELL: {
            // Single cell reference - get the cell's value
            Cell* cell = targetSheet->getCell(namedRange->target.id1);
            if (!cell) {
                // Empty cell reference returns 0
                return EvalResult::Number(0.0);
            }

            // Check for circular reference
            if (ctx.evaluatingCells && (ctx.evaluatingCells->count(namedRange->target.id1) != 0u)) {
                return EvalResult::Error(CellError::CIRCULAR);
            }

            // If cell has a dirty formula, evaluate it
            Formula* formula = cell->getFormula();
            if (formula && formula->dirty && formula->ast) {
                // Mark that we're evaluating this cell (circular reference detection)
                bool addedToSet = false;
                if (ctx.evaluatingCells) {
                    ctx.evaluatingCells->insert(namedRange->target.id1);
                    addedToSet = true;
                }

                // Check recursion depth
                if (ctx.recursionDepth >= EvalContext::MAX_RECURSION) {
                    if (addedToSet) {
                        ctx.evaluatingCells->erase(namedRange->target.id1);
                    }
                    return EvalResult::Error(CellError::CIRCULAR);
                }

                // Recursively evaluate
                EvalContext subCtx = ctx;
                subCtx.currentCellId = namedRange->target.id1;
                subCtx.recursionDepth++;

                EvalResult result = evaluate(formula->ast, subCtx);

                // Store result in cell value
                if (result.isError()) {
                    cell->value = CellValue(result.getError());
                    cell->value.type = CellValueType::FORMULA_ERROR;
                } else if (result.isNumber()) {
                    cell->value = CellValue(result.getNumber());
                    cell->value.type = CellValueType::FORMULA_NUMBER;
                } else if (result.isString()) {
                    cell->value = CellValue(result.getString());
                    cell->value.type = CellValueType::FORMULA_STRING;
                } else if (result.isBoolean()) {
                    cell->value = CellValue(result.getBoolean());
                    cell->value.type = CellValueType::FORMULA_BOOLEAN;
                } else {
                    cell->value = CellValue("");
                    cell->value.type = CellValueType::FORMULA_EMPTY;
                }

                formula->dirty = false;

                // Remove from evaluating set
                if (addedToSet) {
                    ctx.evaluatingCells->erase(namedRange->target.id1);
                }

                return result;
            }

            // Return the cell's current value
            return cellValueToEvalResult(cell->value);
        }

        case NamedRangeTarget::Type::RANGE: {
            // Range reference - return range bounds for iteration by functions
            // Need to look up the cells to get their column/row information
            const Cell* topLeftCell = targetSheet->getCell(namedRange->target.id1);
            const Cell* bottomRightCell = targetSheet->getCell(namedRange->target.id2);

            if (!topLeftCell || !bottomRightCell) {
                return EvalResult::Error(CellError::REF);
            }

            const Axis* startCol = targetSheet->getColumn(topLeftCell->colId);
            const Axis* endCol = targetSheet->getColumn(bottomRightCell->colId);
            const Axis* startRow = targetSheet->getRow(topLeftCell->rowId);
            const Axis* endRow = targetSheet->getRow(bottomRightCell->rowId);

            if (!startCol || !endCol || !startRow || !endRow) {
                return EvalResult::Error(CellError::REF);
            }

            // Ensure proper ordering
            ID startColId = startCol->id;
            ID endColId = endCol->id;
            uint32_t startRowPos = startRow->position;
            uint32_t endRowPos = endRow->position;

            if (startCol->position > endCol->position) {
                std::swap(startColId, endColId);
            }
            if (startRowPos > endRowPos) {
                std::swap(startRowPos, endRowPos);
            }

            return EvalResult::CellRange(startColId, endColId, startRowPos, endRowPos);
        }

        case NamedRangeTarget::Type::COLUMN: {
            // Single column reference
            const Axis* col = targetSheet->getColumn(namedRange->target.id1);
            if (!col) {
                return EvalResult::Error(CellError::REF);
            }
            return EvalResult::SingleColumn(col->id);
        }

        case NamedRangeTarget::Type::ROW: {
            // Single row reference
            const Axis* row = targetSheet->getRow(namedRange->target.id1);
            if (!row) {
                return EvalResult::Error(CellError::REF);
            }
            return EvalResult::SingleRow(row->id);
        }

        case NamedRangeTarget::Type::COLUMN_RANGE: {
            // Column range reference
            const Axis* startCol = targetSheet->getColumn(namedRange->target.id1);
            const Axis* endCol = targetSheet->getColumn(namedRange->target.id2);

            if (!startCol || !endCol) {
                return EvalResult::Error(CellError::REF);
            }

            // Ensure proper ordering
            if (startCol->position > endCol->position) {
                std::swap(startCol, endCol);
            }

            return EvalResult::ColumnRange(startCol->id, endCol->id);
        }

        case NamedRangeTarget::Type::ROW_RANGE: {
            // Row range reference
            const Axis* startRow = targetSheet->getRow(namedRange->target.id1);
            const Axis* endRow = targetSheet->getRow(namedRange->target.id2);

            if (!startRow || !endRow) {
                return EvalResult::Error(CellError::REF);
            }

            // Ensure proper ordering
            if (startRow->position > endRow->position) {
                std::swap(startRow, endRow);
            }

            return EvalResult::RowRange(startRow->id, endRow->id);
        }
    }

    return EvalResult::Error(CellError::NAME);
}

}  // namespace cells
