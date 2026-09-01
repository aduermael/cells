#include "core/cells/functions/fn_lookup.h"

#include <cmath>

#include <algorithm>
#include <string>
#include <vector>

#include "core/cells/formula_ast.h"
#include "core/cells/formula_functions.h"
#include "core/cells/model.h"

namespace cells {

namespace {

// Helper to get range dimensions
struct RangeDimensions {
    uint32_t rows = 0;
    uint32_t cols = 0;
    uint32_t startColPos = 0;
    uint32_t startRowPos = 0;
    bool valid = false;
};

// Helper to get range dimensions from RangeBounds
RangeDimensions getRangeDimensions(const RangeBounds& bounds, Sheet* sheet) {
    RangeDimensions dims;
    dims.valid = false;

    if (!sheet) {
        return dims;
    }

    switch (bounds.type) {
        case RangeType::CELL_RANGE: {
            // Get start and end columns
            const Axis* startCol = sheet->getColumn(bounds.startColId);
            const Axis* endCol = sheet->getColumn(bounds.endColId);

            if (!startCol || !endCol) {
                return dims;
            }

            dims.startColPos = startCol->position;
            dims.startRowPos = bounds.startRowPos;  // Use position bounds directly
            dims.cols = static_cast<uint32_t>(std::abs(static_cast<int>(endCol->position) -
                                                       static_cast<int>(startCol->position)) +
                                              1);
            dims.rows = static_cast<uint32_t>(bounds.endRowPos - bounds.startRowPos + 1);
            dims.valid = true;
            break;
        }
        case RangeType::COLUMN:
        case RangeType::COLUMN_RANGE: {
            // Whole column(s) - columns are bounded, rows are "unlimited"
            const Axis* startCol = sheet->getColumn(bounds.startColId);
            const Axis* endCol = sheet->getColumn(bounds.endColId);

            if (!startCol || !endCol) {
                return dims;
            }

            dims.startColPos = startCol->position;
            dims.startRowPos = 0;  // Start from first row
            dims.cols = static_cast<uint32_t>(std::abs(static_cast<int>(endCol->position) -
                                                       static_cast<int>(startCol->position)) +
                                              1);
            // Use a large number for rows - effectively unlimited
            // INDEX will validate against actual row index provided
            dims.rows = 1048576;  // Excel's max row count
            dims.valid = true;
            break;
        }
        case RangeType::ROW:
        case RangeType::ROW_RANGE: {
            // Whole row(s) - rows are bounded, columns are "unlimited"
            const Axis* startRow = sheet->getRow(bounds.startRowId);
            const Axis* endRow = sheet->getRow(bounds.endRowId);

            if (!startRow || !endRow) {
                return dims;
            }

            dims.startColPos = 0;  // Start from first column
            dims.startRowPos = startRow->position;
            dims.rows = static_cast<uint32_t>(std::abs(static_cast<int>(endRow->position) -
                                                       static_cast<int>(startRow->position)) +
                                              1);
            // Use a large number for cols - effectively unlimited
            dims.cols = 16384;  // Excel's max column count
            dims.valid = true;
            break;
        }
    }

    return dims;
}

// Helper to get cell value at position in range (0-indexed offsets from range start)
EvalResult getCellAtPosition(EvalContext& ctx, const RangeBounds& bounds, uint32_t rowOffset,
                             uint32_t colOffset) {
    if (!ctx.sheet) {
        return EvalResult::Error(CellError::REF);
    }

    uint32_t targetColPos = 0;
    uint32_t targetRowPos = 0;

    switch (bounds.type) {
        case RangeType::CELL_RANGE: {
            // Get start column
            const Axis* startCol = ctx.sheet->getColumn(bounds.startColId);
            if (!startCol) {
                return EvalResult::Error(CellError::REF);
            }
            targetColPos = startCol->position + colOffset;
            targetRowPos = bounds.startRowPos + rowOffset;
            break;
        }
        case RangeType::COLUMN:
        case RangeType::COLUMN_RANGE: {
            // Whole column(s) - start from column A, row 1
            const Axis* startCol = ctx.sheet->getColumn(bounds.startColId);
            if (!startCol) {
                return EvalResult::Error(CellError::REF);
            }
            targetColPos = startCol->position + colOffset;
            targetRowPos = rowOffset;  // 0-indexed from row 1
            break;
        }
        case RangeType::ROW:
        case RangeType::ROW_RANGE: {
            // Whole row(s) - start from column A
            const Axis* startRow = ctx.sheet->getRow(bounds.startRowId);
            if (!startRow) {
                return EvalResult::Error(CellError::REF);
            }
            targetColPos = colOffset;  // 0-indexed from column A
            targetRowPos = startRow->position + rowOffset;
            break;
        }
    }

    // Find the column and row at those positions
    const Axis* targetCol = ctx.sheet->getColumnByPosition(targetColPos);
    const Axis* targetRow = ctx.sheet->getRowByPosition(targetRowPos);

    if (!targetCol || !targetRow) {
        return EvalResult::Error(CellError::REF);
    }

    // Get the cell
    const Cell* cell = ctx.sheet->getCellAt(targetCol->id, targetRow->id);
    if (!cell) {
        return EvalResult::Empty();
    }

    // Convert cell value to EvalResult
    const CellValue& val = cell->value;
    switch (val.type) {
        case CellValueType::NUMBER:
            return EvalResult::Number(val.asNumber());
        case CellValueType::STRING:
            // Empty string is treated as empty cell
            if (val.raw.empty()) {
                return EvalResult::Empty();
            }
            return EvalResult::String(val.asString());
        case CellValueType::BOOLEAN:
            return EvalResult::Boolean(val.asBoolean());
        case CellValueType::ERROR:
            return EvalResult::Error(val.error);
        default:
            return EvalResult::Empty();
    }
}

// Helper to compare values for lookup
// Returns: -1 if a < b, 0 if a == b, 1 if a > b
int compareValues(const EvalResult& a, const EvalResult& b) {
    // Both numbers
    if (a.isNumber() && b.isNumber()) {
        if (a.getNumber() < b.getNumber()) {
            return -1;
        }
        if (a.getNumber() > b.getNumber()) {
            return 1;
        }
        return 0;
    }

    // Both strings - case-insensitive comparison
    if (a.isString() && b.isString()) {
        std::string strA = a.getString();
        std::string strB = b.getString();
        std::transform(strA.begin(), strA.end(), strA.begin(), ::tolower);
        std::transform(strB.begin(), strB.end(), strB.begin(), ::tolower);
        if (strA < strB) {
            return -1;
        }
        if (strA > strB) {
            return 1;
        }
        return 0;
    }

    // Mixed types - numbers come before strings in Excel
    if (a.isNumber() && b.isString()) {
        return -1;
    }
    if (a.isString() && b.isNumber()) {
        return 1;
    }

    // Booleans
    if (a.isBoolean() && b.isBoolean()) {
        if (!a.getBoolean() && b.getBoolean()) {
            return -1;
        }
        if (a.getBoolean() && !b.getBoolean()) {
            return 1;
        }
        return 0;
    }

    // Empty values
    if (a.isEmpty() && !b.isEmpty()) {
        return -1;
    }
    if (!a.isEmpty() && b.isEmpty()) {
        return 1;
    }

    return 0;  // Default equal
}

// Helper for exact match
bool valuesMatch(const EvalResult& a, const EvalResult& b) {
    if (a.isNumber() && b.isNumber()) {
        return a.getNumber() == b.getNumber();
    }
    if (a.isString() && b.isString()) {
        std::string strA = a.getString();
        std::string strB = b.getString();
        std::transform(strA.begin(), strA.end(), strA.begin(), ::tolower);
        std::transform(strB.begin(), strB.end(), strB.begin(), ::tolower);
        return strA == strB;
    }
    if (a.isBoolean() && b.isBoolean()) {
        return a.getBoolean() == b.getBoolean();
    }
    if (a.isEmpty() && b.isEmpty()) {
        return true;
    }
    // Coerce string to number for comparison
    if (a.isNumber() && b.isString()) {
        const EvalResult bNum = b.toNumber();
        if (bNum.isNumber()) {
            return a.getNumber() == bNum.getNumber();
        }
    }
    if (a.isString() && b.isNumber()) {
        const EvalResult aNum = a.toNumber();
        if (aNum.isNumber()) {
            return aNum.getNumber() == b.getNumber();
        }
    }
    return false;
}

}  // namespace

EvalResult fn_INDEX(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    // INDEX requires 2-3 arguments
    if (args.size() < 2 || args.size() > 3) {
        return EvalResult::Error(CellError::VALUE);
    }

    // First argument must be a range
    EvalResult rangeResult = evaluate(args[0], ctx);
    if (rangeResult.isError()) {
        return rangeResult;
    }

    // Get row number (1-indexed)
    EvalResult rowResult = evaluateAsNumber(args[1], ctx);
    if (rowResult.isError()) {
        return rowResult;
    }
    const int rowNum = static_cast<int>(rowResult.getNumber());

    // Get column number (1-indexed), default to 1
    int colNum = 1;
    if (args.size() >= 3) {
        EvalResult colResult = evaluateAsNumber(args[2], ctx);
        if (colResult.isError()) {
            return colResult;
        }
        colNum = static_cast<int>(colResult.getNumber());
    }

    // Handle single value (not a range)
    if (!rangeResult.isRange()) {
        if (rowNum == 1 && colNum == 1) {
            return rangeResult;
        }
        return EvalResult::Error(CellError::REF);
    }

    // Get range bounds and dimensions
    const RangeBounds& bounds = rangeResult.getRangeBounds();
    const RangeDimensions dims = getRangeDimensions(bounds, ctx.sheet);
    if (!dims.valid) {
        return EvalResult::Error(CellError::REF);
    }

    // Special case: 0 means return entire row/column (not supported, return error)
    if (rowNum == 0 || colNum == 0) {
        return EvalResult::Error(CellError::VALUE);
    }

    // Validate bounds (1-indexed)
    if (rowNum < 1 || static_cast<uint32_t>(rowNum) > dims.rows || colNum < 1 ||
        static_cast<uint32_t>(colNum) > dims.cols) {
        return EvalResult::Error(CellError::REF);
    }

    // Get the cell value (convert to 0-indexed)
    return getCellAtPosition(ctx, bounds, rowNum - 1, colNum - 1);
}

EvalResult fn_MATCH(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    // MATCH requires 2-3 arguments
    if (args.size() < 2 || args.size() > 3) {
        return EvalResult::Error(CellError::VALUE);
    }

    // Get lookup value
    EvalResult lookupValue = evaluate(args[0], ctx);
    if (lookupValue.isError()) {
        return lookupValue;
    }

    // Second argument must be a range
    EvalResult rangeResult = evaluate(args[1], ctx);
    if (rangeResult.isError()) {
        return rangeResult;
    }

    // Get match type (default 1)
    int matchType = 1;
    if (args.size() >= 3) {
        EvalResult mtResult = evaluateAsNumber(args[2], ctx);
        if (mtResult.isError()) {
            return mtResult;
        }
        matchType = static_cast<int>(mtResult.getNumber());
    }

    // Validate match type
    if (matchType != -1 && matchType != 0 && matchType != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    // If not a range, treat as single value
    if (!rangeResult.isRange()) {
        if (valuesMatch(lookupValue, rangeResult)) {
            return EvalResult::Number(1.0);
        }
        return EvalResult::Error(CellError::NA);
    }

    // Get range bounds and dimensions
    const RangeBounds& bounds = rangeResult.getRangeBounds();
    const RangeDimensions dims = getRangeDimensions(bounds, ctx.sheet);
    if (!dims.valid) {
        return EvalResult::Error(CellError::REF);
    }

    // MATCH works on 1D ranges (single row or column)
    const bool isHorizontal = dims.rows == 1;
    const bool isVertical = dims.cols == 1;

    if (!isHorizontal && !isVertical) {
        return EvalResult::Error(CellError::NA);  // Can't MATCH on 2D range
    }

    const uint32_t count = isHorizontal ? dims.cols : dims.rows;
    int bestMatch = -1;  // -1 means no match found

    for (uint32_t i = 0; i < count; ++i) {
        EvalResult cellValue;
        if (isHorizontal) {
            cellValue = getCellAtPosition(ctx, bounds, 0, i);
        } else {
            cellValue = getCellAtPosition(ctx, bounds, i, 0);
        }

        if (cellValue.isError()) {
            continue;  // Skip errors
        }

        const int cmp = compareValues(lookupValue, cellValue);

        if (matchType == 0) {
            // Exact match
            if (cmp == 0) {
                return EvalResult::Number(static_cast<double>(i + 1));  // 1-indexed
            }
        } else if (matchType == 1) {
            // Find largest value <= lookup_value
            // Assumes range is sorted ascending
            if (cmp >= 0) {  // lookup >= cell
                bestMatch = static_cast<int>(i);
            } else {
                // lookup < cell, we've gone too far
                break;
            }
        } else if (matchType == -1) {
            // Find smallest value >= lookup_value
            // Assumes range is sorted descending
            if (cmp <= 0) {  // lookup <= cell
                bestMatch = static_cast<int>(i);
            } else {
                // lookup > cell, we've gone too far
                break;
            }
        }
    }

    if (matchType != 0 && bestMatch >= 0) {
        return EvalResult::Number(static_cast<double>(bestMatch + 1));
    }

    return EvalResult::Error(CellError::NA);
}

EvalResult fn_VLOOKUP(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    // VLOOKUP requires 3-4 arguments
    if (args.size() < 3 || args.size() > 4) {
        return EvalResult::Error(CellError::VALUE);
    }

    // Get lookup value
    EvalResult lookupValue = evaluate(args[0], ctx);
    if (lookupValue.isError()) {
        return lookupValue;
    }

    // Second argument must be a range
    EvalResult rangeResult = evaluate(args[1], ctx);
    if (rangeResult.isError()) {
        return rangeResult;
    }
    if (!rangeResult.isRange()) {
        return EvalResult::Error(CellError::VALUE);
    }

    // Get column index (1-indexed)
    EvalResult colResult = evaluateAsNumber(args[2], ctx);
    if (colResult.isError()) {
        return colResult;
    }
    const int colIndex = static_cast<int>(colResult.getNumber());

    // Get range_lookup (default TRUE = approximate match)
    bool rangeLookup = true;
    if (args.size() >= 4) {
        EvalResult rlResult = evaluateAsBoolean(args[3], ctx);
        if (rlResult.isError()) {
            return rlResult;
        }
        rangeLookup = rlResult.getBoolean();
    }

    // Get range bounds and dimensions
    const RangeBounds& bounds = rangeResult.getRangeBounds();
    const RangeDimensions dims = getRangeDimensions(bounds, ctx.sheet);
    if (!dims.valid) {
        return EvalResult::Error(CellError::REF);
    }

    // Validate column index
    if (colIndex < 1 || static_cast<uint32_t>(colIndex) > dims.cols) {
        return EvalResult::Error(CellError::REF);
    }

    // Search first column for lookup value
    int matchRow = -1;
    int bestMatch = -1;

    for (uint32_t i = 0; i < dims.rows; ++i) {
        const EvalResult cellValue = getCellAtPosition(ctx, bounds, i, 0);
        if (cellValue.isError()) {
            continue;
        }

        if (rangeLookup) {
            // Approximate match - find largest value <= lookup
            // Assumes first column is sorted ascending
            const int cmp = compareValues(lookupValue, cellValue);
            if (cmp >= 0) {
                bestMatch = static_cast<int>(i);
            } else {
                break;  // Gone too far
            }
        } else {
            // Exact match
            if (valuesMatch(lookupValue, cellValue)) {
                matchRow = static_cast<int>(i);
                break;
            }
        }
    }

    if (rangeLookup) {
        matchRow = bestMatch;
    }

    if (matchRow < 0) {
        return EvalResult::Error(CellError::NA);
    }

    // Return value from specified column
    return getCellAtPosition(ctx, bounds, matchRow, colIndex - 1);
}

EvalResult fn_HLOOKUP(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    // HLOOKUP requires 3-4 arguments
    if (args.size() < 3 || args.size() > 4) {
        return EvalResult::Error(CellError::VALUE);
    }

    // Get lookup value
    EvalResult lookupValue = evaluate(args[0], ctx);
    if (lookupValue.isError()) {
        return lookupValue;
    }

    // Second argument must be a range
    EvalResult rangeResult = evaluate(args[1], ctx);
    if (rangeResult.isError()) {
        return rangeResult;
    }
    if (!rangeResult.isRange()) {
        return EvalResult::Error(CellError::VALUE);
    }

    // Get row index (1-indexed)
    EvalResult rowResult = evaluateAsNumber(args[2], ctx);
    if (rowResult.isError()) {
        return rowResult;
    }
    const int rowIndex = static_cast<int>(rowResult.getNumber());

    // Get range_lookup (default TRUE = approximate match)
    bool rangeLookup = true;
    if (args.size() >= 4) {
        EvalResult rlResult = evaluateAsBoolean(args[3], ctx);
        if (rlResult.isError()) {
            return rlResult;
        }
        rangeLookup = rlResult.getBoolean();
    }

    // Get range bounds and dimensions
    const RangeBounds& bounds = rangeResult.getRangeBounds();
    const RangeDimensions dims = getRangeDimensions(bounds, ctx.sheet);
    if (!dims.valid) {
        return EvalResult::Error(CellError::REF);
    }

    // Validate row index
    if (rowIndex < 1 || static_cast<uint32_t>(rowIndex) > dims.rows) {
        return EvalResult::Error(CellError::REF);
    }

    // Search first row for lookup value
    int matchCol = -1;
    int bestMatch = -1;

    for (uint32_t i = 0; i < dims.cols; ++i) {
        const EvalResult cellValue = getCellAtPosition(ctx, bounds, 0, i);
        if (cellValue.isError()) {
            continue;
        }

        if (rangeLookup) {
            // Approximate match - find largest value <= lookup
            // Assumes first row is sorted ascending
            const int cmp = compareValues(lookupValue, cellValue);
            if (cmp >= 0) {
                bestMatch = static_cast<int>(i);
            } else {
                break;  // Gone too far
            }
        } else {
            // Exact match
            if (valuesMatch(lookupValue, cellValue)) {
                matchCol = static_cast<int>(i);
                break;
            }
        }
    }

    if (rangeLookup) {
        matchCol = bestMatch;
    }

    if (matchCol < 0) {
        return EvalResult::Error(CellError::NA);
    }

    // Return value from specified row
    return getCellAtPosition(ctx, bounds, rowIndex - 1, matchCol);
}

EvalResult fn_ROW(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() > 1) {
        return EvalResult::Error(CellError::VALUE);
    }
    if (args.empty()) {
        if (ctx.currentCellId.isNull() || ctx.sheet == nullptr) {
            return EvalResult::Error(CellError::VALUE);
        }
        const Cell* cell = ctx.sheet->getCell(ctx.currentCellId);
        if (cell == nullptr) {
            return EvalResult::Error(CellError::REF);
        }
        const Axis* row = ctx.sheet->getRow(cell->rowId);
        if (row == nullptr) {
            return EvalResult::Error(CellError::REF);
        }
        return EvalResult::Number(static_cast<double>(row->position + 1));
    }
    const ASTNode* n = args[0];
    if (n->type == ASTNodeType::CELL_REF) {
        return EvalResult::Number(static_cast<const CellRefNode*>(n)->row);
    }
    if (n->type == ASTNodeType::RANGE_REF) {
        const auto* range = static_cast<const RangeRefNode*>(n);
        if (range->topLeft) {
            return EvalResult::Number(range->topLeft->row);
        }
    }
    if (n->type == ASTNodeType::ROW_REF) {
        return EvalResult::Number(static_cast<const RowRefNode*>(n)->row);
    }
    if (n->type == ASTNodeType::COLUMN_REF || n->type == ASTNodeType::COLUMN_RANGE_REF) {
        return EvalResult::Number(1.0);
    }
    EvalResult r = evaluate(n, ctx);
    if (r.isError()) {
        return r;
    }
    if (r.isRange()) {
        const RangeDimensions dims =
            getRangeDimensions(r.getRangeBounds(), r.targetSheet ? r.targetSheet : ctx.sheet);
        if (!dims.valid) {
            return EvalResult::Error(CellError::REF);
        }
        return EvalResult::Number(static_cast<double>(dims.startRowPos + 1));
    }
    return EvalResult::Error(CellError::VALUE);
}

EvalResult fn_ROWS(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }
    EvalResult r = evaluate(args[0], ctx);
    if (r.isError()) {
        return r;
    }
    if (r.isArray()) {
        return EvalResult::Number(static_cast<double>(r.getArrayRows()));
    }
    if (r.isRange()) {
        const RangeDimensions dims =
            getRangeDimensions(r.getRangeBounds(), r.targetSheet ? r.targetSheet : ctx.sheet);
        if (!dims.valid) {
            return EvalResult::Error(CellError::REF);
        }
        return EvalResult::Number(static_cast<double>(dims.rows));
    }
    return EvalResult::Number(1.0);
}

static int columnLettersToNumber(const std::string& letters) {
    const int32_t pos = Sheet::columnNameToPosition(letters);
    if (pos < 0) {
        return 0;
    }
    return pos + 1;
}

EvalResult fn_COLUMN(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() > 1) {
        return EvalResult::Error(CellError::VALUE);
    }
    if (args.empty()) {
        if (ctx.currentCellId.isNull() || ctx.sheet == nullptr) {
            return EvalResult::Error(CellError::VALUE);
        }
        const Cell* cell = ctx.sheet->getCell(ctx.currentCellId);
        if (cell == nullptr) {
            return EvalResult::Error(CellError::REF);
        }
        const Axis* col = ctx.sheet->getColumn(cell->colId);
        if (col == nullptr) {
            return EvalResult::Error(CellError::REF);
        }
        return EvalResult::Number(static_cast<double>(col->position + 1));
    }
    const ASTNode* n = args[0];
    if (n->type == ASTNodeType::CELL_REF) {
        return EvalResult::Number(
            columnLettersToNumber(static_cast<const CellRefNode*>(n)->column));
    }
    if (n->type == ASTNodeType::RANGE_REF) {
        const auto* range = static_cast<const RangeRefNode*>(n);
        if (range->topLeft) {
            return EvalResult::Number(columnLettersToNumber(range->topLeft->column));
        }
    }
    if (n->type == ASTNodeType::COLUMN_REF) {
        return EvalResult::Number(
            columnLettersToNumber(static_cast<const ColumnRefNode*>(n)->column));
    }
    if (n->type == ASTNodeType::ROW_REF || n->type == ASTNodeType::ROW_RANGE_REF) {
        return EvalResult::Number(1.0);
    }
    EvalResult r = evaluate(n, ctx);
    if (r.isError()) {
        return r;
    }
    if (r.isRange()) {
        const RangeDimensions dims =
            getRangeDimensions(r.getRangeBounds(), r.targetSheet ? r.targetSheet : ctx.sheet);
        if (!dims.valid) {
            return EvalResult::Error(CellError::REF);
        }
        return EvalResult::Number(static_cast<double>(dims.startColPos + 1));
    }
    return EvalResult::Error(CellError::VALUE);
}

EvalResult fn_COLUMNS(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }
    EvalResult r = evaluate(args[0], ctx);
    if (r.isError()) {
        return r;
    }
    if (r.isArray()) {
        return EvalResult::Number(static_cast<double>(r.getArrayCols()));
    }
    if (r.isRange()) {
        const RangeDimensions dims =
            getRangeDimensions(r.getRangeBounds(), r.targetSheet ? r.targetSheet : ctx.sheet);
        if (!dims.valid) {
            return EvalResult::Error(CellError::REF);
        }
        return EvalResult::Number(static_cast<double>(dims.cols));
    }
    return EvalResult::Number(1.0);
}

EvalResult fn_ADDRESS(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() < 2 || args.size() > 5) {
        return EvalResult::Error(CellError::VALUE);
    }
    EvalResult rowRes = evaluateAsNumber(args[0], ctx);
    if (rowRes.isError()) {
        return rowRes;
    }
    EvalResult colRes = evaluateAsNumber(args[1], ctx);
    if (colRes.isError()) {
        return colRes;
    }
    int absNum = 1;
    if (args.size() >= 3) {
        const EvalResult a = evaluateAsNumber(args[2], ctx);
        if (a.isError()) {
            return a;
        }
        absNum = static_cast<int>(a.getNumber());
    }
    bool a1 = true;
    if (args.size() >= 4) {
        const EvalResult a1Res = evaluateAsBoolean(args[3], ctx);
        if (a1Res.isError()) {
            return a1Res;
        }
        a1 = a1Res.getBoolean();
    }
    std::string sheetName;
    if (args.size() == 5) {
        const EvalResult s = evaluateAsString(args[4], ctx);
        if (s.isError()) {
            return s;
        }
        sheetName = s.getString();
    }
    const int row = static_cast<int>(rowRes.getNumber());
    const int col = static_cast<int>(colRes.getNumber());
    if (row < 1 || col < 1) {
        return EvalResult::Error(CellError::VALUE);
    }
    if (!a1) {
        return EvalResult::Error(CellError::VALUE);  // R1C1 not supported
    }
    const bool absCol = (absNum == 1 || absNum == 3);
    const bool absRow = (absNum == 1 || absNum == 2);
    if (absNum < 1 || absNum > 4) {
        return EvalResult::Error(CellError::VALUE);
    }
    std::string ref;
    if (absCol) {
        ref += '$';
    }
    ref += Sheet::positionToColumnName(static_cast<uint32_t>(col - 1));
    if (absRow) {
        ref += '$';
    }
    ref += std::to_string(row);
    if (!sheetName.empty()) {
        const bool quote =
            sheetName.find(' ') != std::string::npos || sheetName.find('!') != std::string::npos;
        if (quote) {
            ref = "'" + sheetName + "'!" + ref;
        } else {
            ref = sheetName + "!" + ref;
        }
    }
    return EvalResult::String(ref);
}

EvalResult fn_CHOOSE(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() < 2) {
        return EvalResult::Error(CellError::VALUE);
    }
    EvalResult idxRes = evaluateAsNumber(args[0], ctx);
    if (idxRes.isError()) {
        return idxRes;
    }
    const int idx = static_cast<int>(idxRes.getNumber());
    if (idx < 1 || static_cast<size_t>(idx) >= args.size()) {
        return EvalResult::Error(CellError::VALUE);
    }
    return evaluate(args[static_cast<size_t>(idx)], ctx);
}

namespace {

int sheetIndex1Based(const Workbook* wb, const Sheet* sh) {
    if (wb == nullptr || sh == nullptr) {
        return 0;
    }
    for (size_t i = 0; i < wb->sheets.size(); ++i) {
        if (wb->sheets[i].get() == sh) {
            return static_cast<int>(i) + 1;
        }
    }
    return 0;
}

const Sheet* sheetFromRefNode(const ASTNode* n, EvalContext& ctx) {
    std::string sheetId;
    std::string sheetName;
    if (n->type == ASTNodeType::CELL_REF) {
        const auto* cell = static_cast<const CellRefNode*>(n);
        sheetId = cell->sheetId;
        sheetName = cell->sheetName;
    } else if (n->type == ASTNodeType::RANGE_REF) {
        const auto* range = static_cast<const RangeRefNode*>(n);
        if (range->topLeft) {
            sheetId = range->topLeft->sheetId;
            sheetName = range->topLeft->sheetName;
        }
    } else if (n->type == ASTNodeType::COLUMN_REF) {
        const auto* col = static_cast<const ColumnRefNode*>(n);
        sheetId = col->sheetId;
        sheetName = col->sheetName;
    }
    if (ctx.workbook == nullptr) {
        return ctx.sheet;
    }
    if (!sheetId.empty()) {
        const Sheet* s = ctx.workbook->getSheetById(ID(sheetId));
        if (s != nullptr) {
            return s;
        }
    }
    if (!sheetName.empty()) {
        const Sheet* s = ctx.workbook->getSheetByName(sheetName);
        if (s != nullptr) {
            return s;
        }
    }
    return ctx.sheet;
}

}  // namespace

EvalResult fn_AREAS(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }
    if (isReferenceNode(args[0])) {
        return EvalResult::Number(1.0);
    }
    EvalResult r = evaluate(args[0], ctx);
    if (r.isError()) {
        return r;
    }
    if (r.isRange() || r.isArray()) {
        return EvalResult::Number(1.0);
    }
    return EvalResult::Error(CellError::VALUE);
}

EvalResult fn_SHEET(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() > 1) {
        return EvalResult::Error(CellError::VALUE);
    }
    if (ctx.workbook == nullptr || ctx.sheet == nullptr) {
        return EvalResult::Error(CellError::NA);
    }
    if (args.empty()) {
        const int idx = sheetIndex1Based(ctx.workbook, ctx.sheet);
        if (idx == 0) {
            return EvalResult::Error(CellError::NA);
        }
        return EvalResult::Number(static_cast<double>(idx));
    }
    if (isReferenceNode(args[0])) {
        const Sheet* sh = sheetFromRefNode(args[0], ctx);
        const int idx = sheetIndex1Based(ctx.workbook, sh);
        if (idx == 0) {
            return EvalResult::Error(CellError::NA);
        }
        return EvalResult::Number(static_cast<double>(idx));
    }
    EvalResult r = evaluate(args[0], ctx);
    if (r.isError()) {
        return r;
    }
    if (r.isRange()) {
        const Sheet* sh = r.targetSheet != nullptr ? r.targetSheet : ctx.sheet;
        const int idx = sheetIndex1Based(ctx.workbook, sh);
        if (idx == 0) {
            return EvalResult::Error(CellError::NA);
        }
        return EvalResult::Number(static_cast<double>(idx));
    }
    const EvalResult name = r.toString();
    if (name.isError()) {
        return EvalResult::Error(CellError::NA);
    }
    const Sheet* named = ctx.workbook->getSheetByName(name.getString());
    if (named == nullptr) {
        return EvalResult::Error(CellError::NA);
    }
    return EvalResult::Number(static_cast<double>(sheetIndex1Based(ctx.workbook, named)));
}

EvalResult fn_SHEETS(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() > 1) {
        return EvalResult::Error(CellError::VALUE);
    }
    if (ctx.workbook == nullptr) {
        return EvalResult::Error(CellError::NA);
    }
    if (args.empty()) {
        return EvalResult::Number(static_cast<double>(ctx.workbook->sheetCount()));
    }
    if (isReferenceNode(args[0])) {
        return EvalResult::Number(1.0);
    }
    EvalResult r = evaluate(args[0], ctx);
    if (r.isError()) {
        return r;
    }
    if (r.isRange() || r.isArray()) {
        return EvalResult::Number(1.0);
    }
    return EvalResult::Error(CellError::NA);
}

EvalResult fn_HYPERLINK(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.empty() || args.size() > 2) {
        return EvalResult::Error(CellError::VALUE);
    }
    EvalResult loc = evaluate(args[0], ctx);
    if (loc.isError()) {
        return loc;
    }
    if (args.size() == 2) {
        return evaluate(args[1], ctx);
    }
    return loc;
}

namespace {

size_t gridWidth(const std::vector<std::vector<EvalResult>>& g) {
    size_t cols = 0;
    for (const auto& row : g) {
        cols = std::max(cols, row.size());
    }
    return cols;
}

EvalResult gridCell(const std::vector<std::vector<EvalResult>>& g, size_t r, size_t c) {
    if (r >= g.size() || c >= g[r].size()) {
        return EvalResult::Empty();
    }
    return g[r][c];
}

bool asVector(const std::vector<std::vector<EvalResult>>& g, bool* horizontal, size_t* n) {
    if (g.empty()) {
        return false;
    }
    const size_t rows = g.size();
    const size_t cols = gridWidth(g);
    if (rows == 1) {
        *horizontal = true;
        *n = cols;
        return true;
    }
    if (cols == 1) {
        *horizontal = false;
        *n = rows;
        return true;
    }
    return false;
}

EvalResult vectorAt(const std::vector<std::vector<EvalResult>>& g, bool horizontal, size_t i) {
    return horizontal ? gridCell(g, 0, i) : gridCell(g, i, 0);
}

bool lookupEquals(const EvalResult& lookup, const EvalResult& cell, int matchMode) {
    if (cell.isError()) {
        return false;
    }
    if (matchMode == 2) {
        if (lookup.isString() && cell.isString()) {
            return excelWildcardMatch(cell.getString(), lookup.getString());
        }
        return valuesMatch(lookup, cell);
    }
    return valuesMatch(lookup, cell);
}

int linearFind(const EvalResult& lookup, const std::vector<std::vector<EvalResult>>& arr,
               bool horizontal, size_t n, int matchMode, bool reverse) {
    int best = -1;
    const int start = reverse ? static_cast<int>(n) - 1 : 0;
    const int end = reverse ? -1 : static_cast<int>(n);
    const int step = reverse ? -1 : 1;
    for (int i = start; i != end; i += step) {
        const EvalResult cell = vectorAt(arr, horizontal, static_cast<size_t>(i));
        if (cell.isError()) {
            continue;
        }
        if (matchMode == 0 || matchMode == 2) {
            if (lookupEquals(lookup, cell, matchMode)) {
                return i;
            }
            continue;
        }
        const int cmp = compareValues(lookup, cell);
        if (matchMode == -1 && cmp >= 0) {
            if (best < 0 ||
                compareValues(vectorAt(arr, horizontal, static_cast<size_t>(best)), cell) < 0) {
                best = i;
            }
        } else if (matchMode == 1 && cmp <= 0) {
            if (best < 0 ||
                compareValues(cell, vectorAt(arr, horizontal, static_cast<size_t>(best))) < 0) {
                best = i;
            }
        }
    }
    return best;
}

int binaryFind(const EvalResult& lookup, const std::vector<std::vector<EvalResult>>& arr,
               bool horizontal, size_t n, int matchMode, bool descending) {
    if (n == 0) {
        return -1;
    }
    int lo = 0;
    int hi = static_cast<int>(n) - 1;
    if (matchMode == 0) {
        while (lo <= hi) {
            const int mid = lo + (hi - lo) / 2;
            const EvalResult cell = vectorAt(arr, horizontal, static_cast<size_t>(mid));
            if (cell.isError()) {
                lo = mid + 1;
                continue;
            }
            const int cmp = compareValues(lookup, cell);
            if (cmp == 0) {
                return mid;
            }
            if (descending) {
                if (cmp > 0) {
                    hi = mid - 1;
                } else {
                    lo = mid + 1;
                }
            } else if (cmp > 0) {
                lo = mid + 1;
            } else {
                hi = mid - 1;
            }
        }
        return -1;
    }

    int best = -1;
    while (lo <= hi) {
        const int mid = lo + (hi - lo) / 2;
        const EvalResult cell = vectorAt(arr, horizontal, static_cast<size_t>(mid));
        if (cell.isError()) {
            lo = mid + 1;
            continue;
        }
        const int cmp = compareValues(lookup, cell);
        if (matchMode == -1) {
            if (cmp >= 0) {
                best = mid;
                if (descending) {
                    hi = mid - 1;
                } else {
                    lo = mid + 1;
                }
            } else if (descending) {
                lo = mid + 1;
            } else {
                hi = mid - 1;
            }
        } else if (cmp <= 0) {
            best = mid;
            if (descending) {
                lo = mid + 1;
            } else {
                hi = mid - 1;
            }
        } else if (descending) {
            hi = mid - 1;
        } else {
            lo = mid + 1;
        }
    }
    return best;
}

int findMatch(const EvalResult& lookup, const std::vector<std::vector<EvalResult>>& arr,
              bool horizontal, size_t n, int matchMode, int searchMode) {
    // Wildcards cannot be binary-searched; ±2 only chooses scan direction.
    if (matchMode == 2 || searchMode == 1 || searchMode == -1) {
        return linearFind(lookup, arr, horizontal, n, matchMode, searchMode < 0);
    }
    return binaryFind(lookup, arr, horizontal, n, matchMode, searchMode == -2);
}

EvalResult returnFromArray(const std::vector<std::vector<EvalResult>>& ret, bool lookupHorizontal,
                           size_t matchIndex) {
    if (lookupHorizontal) {
        std::vector<EvalResult> col;
        col.reserve(ret.size());
        for (size_t r = 0; r < ret.size(); ++r) {
            col.push_back(gridCell(ret, r, matchIndex));
        }
        if (col.size() == 1) {
            return col[0];
        }
        return EvalResult::ColumnArray(std::move(col));
    }
    if (matchIndex >= ret.size()) {
        return EvalResult::Error(CellError::NA);
    }
    std::vector<EvalResult> row = ret[matchIndex];
    if (row.size() == 1) {
        return row[0];
    }
    return EvalResult::RowArray(std::move(row));
}

int approximateLookup(const EvalResult& lookup, const std::vector<std::vector<EvalResult>>& arr,
                      bool horizontal, size_t n) {
    int best = -1;
    for (size_t i = 0; i < n; ++i) {
        const EvalResult cell = vectorAt(arr, horizontal, i);
        if (cell.isError()) {
            continue;
        }
        const int cmp = compareValues(lookup, cell);
        if (cmp >= 0) {
            best = static_cast<int>(i);
        } else {
            break;
        }
    }
    return best;
}

}  // namespace

EvalResult fn_XLOOKUP(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() < 3 || args.size() > 6) {
        return EvalResult::Error(CellError::VALUE);
    }
    EvalResult lookup = evaluate(args[0], ctx);
    if (lookup.isError()) {
        return lookup;
    }
    auto [lookupArr, lookupErr] = evaluateAs2D(args[1], ctx);
    if (lookupErr.isError()) {
        return lookupErr;
    }
    auto [returnArr, returnErr] = evaluateAs2D(args[2], ctx);
    if (returnErr.isError()) {
        return returnErr;
    }
    EvalResult ifNotFound = EvalResult::Error(CellError::NA);
    if (args.size() >= 4) {
        ifNotFound = evaluate(args[3], ctx);
    }
    int matchMode = 0;
    if (args.size() >= 5) {
        const EvalResult m = evaluateAsNumber(args[4], ctx);
        if (m.isError()) {
            return m;
        }
        matchMode = static_cast<int>(m.getNumber());
    }
    int searchMode = 1;
    if (args.size() >= 6) {
        const EvalResult s = evaluateAsNumber(args[5], ctx);
        if (s.isError()) {
            return s;
        }
        searchMode = static_cast<int>(s.getNumber());
    }
    if (matchMode != 0 && matchMode != -1 && matchMode != 1 && matchMode != 2) {
        return EvalResult::Error(CellError::VALUE);
    }
    if (searchMode != 1 && searchMode != -1 && searchMode != 2 && searchMode != -2) {
        return EvalResult::Error(CellError::VALUE);
    }

    bool lookupHoriz = false;
    size_t lookupN = 0;
    if (!asVector(lookupArr, &lookupHoriz, &lookupN)) {
        return EvalResult::Error(CellError::VALUE);
    }
    const size_t retRows = returnArr.size();
    const size_t retCols = gridWidth(returnArr);
    if (lookupHoriz) {
        if (retCols != lookupN) {
            return EvalResult::Error(CellError::VALUE);
        }
    } else if (retRows != lookupN) {
        return EvalResult::Error(CellError::VALUE);
    }

    const int match = findMatch(lookup, lookupArr, lookupHoriz, lookupN, matchMode, searchMode);
    if (match < 0) {
        return ifNotFound;
    }
    return returnFromArray(returnArr, lookupHoriz, static_cast<size_t>(match));
}

EvalResult fn_XMATCH(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() < 2 || args.size() > 4) {
        return EvalResult::Error(CellError::VALUE);
    }
    EvalResult lookup = evaluate(args[0], ctx);
    if (lookup.isError()) {
        return lookup;
    }
    auto [arr, err] = evaluateAs2D(args[1], ctx);
    if (err.isError()) {
        return err;
    }
    int matchMode = 0;
    if (args.size() >= 3) {
        const EvalResult m = evaluateAsNumber(args[2], ctx);
        if (m.isError()) {
            return m;
        }
        matchMode = static_cast<int>(m.getNumber());
    }
    int searchMode = 1;
    if (args.size() >= 4) {
        const EvalResult s = evaluateAsNumber(args[3], ctx);
        if (s.isError()) {
            return s;
        }
        searchMode = static_cast<int>(s.getNumber());
    }
    if (matchMode != 0 && matchMode != -1 && matchMode != 1 && matchMode != 2) {
        return EvalResult::Error(CellError::VALUE);
    }
    if (searchMode != 1 && searchMode != -1 && searchMode != 2 && searchMode != -2) {
        return EvalResult::Error(CellError::VALUE);
    }
    bool horiz = false;
    size_t n = 0;
    if (!asVector(arr, &horiz, &n)) {
        return EvalResult::Error(CellError::VALUE);
    }
    const int match = findMatch(lookup, arr, horiz, n, matchMode, searchMode);
    if (match < 0) {
        return EvalResult::Error(CellError::NA);
    }
    return EvalResult::Number(static_cast<double>(match + 1));
}

EvalResult fn_LOOKUP(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() < 2 || args.size() > 3) {
        return EvalResult::Error(CellError::VALUE);
    }
    EvalResult lookup = evaluate(args[0], ctx);
    if (lookup.isError()) {
        return lookup;
    }
    auto [arr, err] = evaluateAs2D(args[1], ctx);
    if (err.isError()) {
        return err;
    }
    if (args.size() == 2) {
        const size_t rows = arr.size();
        const size_t cols = gridWidth(arr);
        const bool byRow = cols > rows;
        const bool horiz = byRow;
        const size_t n = byRow ? cols : rows;
        const int match = approximateLookup(lookup, arr, horiz, n);
        if (match < 0) {
            return EvalResult::Error(CellError::NA);
        }
        if (byRow) {
            return gridCell(arr, rows - 1, static_cast<size_t>(match));
        }
        return gridCell(arr, static_cast<size_t>(match), cols - 1);
    }
    auto [resultArr, resultErr] = evaluateAs2D(args[2], ctx);
    if (resultErr.isError()) {
        return resultErr;
    }
    bool lookupHoriz = false;
    size_t lookupN = 0;
    if (!asVector(arr, &lookupHoriz, &lookupN)) {
        return EvalResult::Error(CellError::VALUE);
    }
    bool resultHoriz = false;
    size_t resultN = 0;
    if (!asVector(resultArr, &resultHoriz, &resultN) || resultN != lookupN) {
        return EvalResult::Error(CellError::NA);
    }
    const int match = approximateLookup(lookup, arr, lookupHoriz, lookupN);
    if (match < 0) {
        return EvalResult::Error(CellError::NA);
    }
    return vectorAt(resultArr, resultHoriz, static_cast<size_t>(match));
}

void registerLookupFunctions(FunctionRegistry& registry) {
    registry.registerFunction("INDEX", fn_INDEX, "(array, row_num, [col_num])",
                              "Returns a value at a position in a range", "Lookup");
    registry.registerFunction("MATCH", fn_MATCH, "(lookup_value, lookup_array, [match_type])",
                              "Returns the position of a value in a range", "Lookup");
    registry.registerFunction("VLOOKUP", fn_VLOOKUP,
                              "(lookup_value, table_array, col_index, [range_lookup])",
                              "Vertical lookup in first column", "Lookup");
    registry.registerFunction("HLOOKUP", fn_HLOOKUP,
                              "(lookup_value, table_array, row_index, [range_lookup])",
                              "Horizontal lookup in first row", "Lookup");
    registry.registerFunction("ROW", fn_ROW, "([reference])", "Row number of a reference",
                              "Lookup");
    registry.registerFunction("ROWS", fn_ROWS, "(array)", "Number of rows in a reference",
                              "Lookup");
    registry.registerFunction("COLUMN", fn_COLUMN, "([reference])", "Column number of a reference",
                              "Lookup");
    registry.registerFunction("COLUMNS", fn_COLUMNS, "(array)", "Number of columns in a reference",
                              "Lookup");
    registry.registerFunction("ADDRESS", fn_ADDRESS, "(row, col, [abs_num], [a1], [sheet])",
                              "Cell address as text", "Lookup");
    registry.registerFunction("CHOOSE", fn_CHOOSE, "(index, value1, [value2], ...)",
                              "Selects a value by index", "Lookup");
    registry.registerFunction("AREAS", fn_AREAS, "(reference)", "Number of areas in a reference",
                              "Lookup");
    registry.registerFunction("SHEET", fn_SHEET, "([value])", "Sheet number of a reference",
                              "Lookup");
    registry.registerFunction("SHEETS", fn_SHEETS, "([reference])", "Number of sheets", "Lookup");
    registry.registerFunction("HYPERLINK", fn_HYPERLINK, "(link_location, [friendly_name])",
                              "Creates a hyperlink value", "Lookup");
    registry.registerFunction("XLOOKUP", fn_XLOOKUP,
                              "(lookup_value, lookup_array, return_array, [if_not_found], "
                              "[match_mode], [search_mode])",
                              "Looks up a value and returns a corresponding result", "Lookup");
    registry.registerFunction("XMATCH", fn_XMATCH,
                              "(lookup_value, lookup_array, [match_mode], [search_mode])",
                              "Returns the relative position of a lookup value", "Lookup");
    registry.registerFunction("LOOKUP", fn_LOOKUP, "(lookup_value, lookup_vector, [result_vector])",
                              "Approximate lookup in a vector or array", "Lookup");
}

}  // namespace cells
