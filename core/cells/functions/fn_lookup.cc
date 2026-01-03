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

    if (!sheet || bounds.type != RangeType::CELL_RANGE) {
        return dims;  // Only support cell ranges for now
    }

    // Get start and end columns
    const Axis* startCol = sheet->getColumn(bounds.startColId);
    const Axis* endCol = sheet->getColumn(bounds.endColId);

    if (!startCol || !endCol) {
        return dims;
    }

    dims.startColPos = startCol->position;
    dims.startRowPos = bounds.startRowPos;  // Use position bounds directly
    dims.cols = static_cast<uint32_t>(
        std::abs(static_cast<int>(endCol->position) - static_cast<int>(startCol->position)) + 1);
    dims.rows = static_cast<uint32_t>(bounds.endRowPos - bounds.startRowPos + 1);
    dims.valid = true;

    return dims;
}

// Helper to get cell value at position in range (0-indexed offsets from range start)
EvalResult getCellAtPosition(EvalContext& ctx, const RangeBounds& bounds, uint32_t rowOffset,
                             uint32_t colOffset) {
    if (!ctx.sheet || bounds.type != RangeType::CELL_RANGE) {
        return EvalResult::Error(CellError::REF);
    }

    // Get start column
    const Axis* startCol = ctx.sheet->getColumn(bounds.startColId);

    if (!startCol) {
        return EvalResult::Error(CellError::REF);
    }

    // Calculate target position (use position bounds for rows)
    const uint32_t targetColPos = startCol->position + colOffset;
    const uint32_t targetRowPos = bounds.startRowPos + rowOffset;

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
}

}  // namespace cells
