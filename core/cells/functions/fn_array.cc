#include "core/cells/functions/fn_array.h"

#include <algorithm>
#include <vector>

#include "core/cells/formula_ast.h"
#include "core/cells/formula_functions.h"
#include "core/cells/model.h"

namespace cells {

namespace {

// Helper to check if two EvalResults are equal for UNIQUE comparison
bool evalResultsEqual(const EvalResult& a, const EvalResult& b) {
    // Different types are never equal (except empty which matches empty)
    if (a.type != b.type) {
        // Special case: empty values are considered equal to empty strings
        return (a.isEmpty() && b.isString() && b.getString().empty()) ||
               (b.isEmpty() && a.isString() && a.getString().empty());
    }

    switch (a.type) {
        case EvalResult::Type::NUMBER:
            return a.getNumber() == b.getNumber();
        case EvalResult::Type::STRING:
            return a.getString() == b.getString();
        case EvalResult::Type::BOOLEAN:
            return a.getBoolean() == b.getBoolean();
        case EvalResult::Type::ERROR:
            return a.getError() == b.getError();
        case EvalResult::Type::EMPTY:
            return true;  // Both empty
        default:
            return false;  // Ranges/arrays not compared
    }
}

// Helper to check if two rows (vectors of EvalResult) are equal
bool rowsEqual(const std::vector<EvalResult>& a, const std::vector<EvalResult>& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        if (!evalResultsEqual(a[i], b[i])) {
            return false;
        }
    }
    return true;
}

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

// Collect range values as a 2D array (row-major)
// Returns: pair<2D array, error>. If error is set, array should be ignored.
std::pair<std::vector<std::vector<EvalResult>>, EvalResult> collectRangeAs2D(
    const EvalResult& rangeResult, EvalContext& ctx) {
    std::vector<std::vector<EvalResult>> result;

    if (!rangeResult.isRange()) {
        // Single value - return as 1x1 array
        result.push_back({rangeResult});
        return {result, EvalResult::Empty()};
    }

    if (!ctx.sheet) {
        return {{}, EvalResult::Error(CellError::VALUE)};
    }

    const RangeBounds& bounds = rangeResult.getRangeBounds();
    const RangeDimensions dims = getRangeDimensions(bounds, ctx.sheet);

    if (!dims.valid) {
        return {{}, EvalResult::Error(CellError::REF)};
    }

    // Build 2D array
    result.reserve(dims.rows);
    for (uint32_t r = 0; r < dims.rows; ++r) {
        std::vector<EvalResult> row;
        row.reserve(dims.cols);
        for (uint32_t c = 0; c < dims.cols; ++c) {
            row.push_back(getCellAtPosition(ctx, bounds, r, c));
        }
        result.push_back(std::move(row));
    }

    return {result, EvalResult::Empty()};
}

}  // namespace

EvalResult fn_UNIQUE(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    // Validate arguments: UNIQUE(array, [by_col], [exactly_once])
    if (args.empty() || args.size() > 3) {
        return EvalResult::Error(CellError::VALUE);
    }

    // Evaluate first argument (array/range)
    EvalResult rangeResult = evaluate(args[0], ctx);
    if (rangeResult.isError()) {
        return rangeResult;
    }

    // Parse optional by_col argument (default: false)
    bool byCol = false;
    if (args.size() >= 2) {
        EvalResult byColResult = evaluateAsBoolean(args[1], ctx);
        if (byColResult.isError()) {
            return byColResult;
        }
        byCol = byColResult.getBoolean();
    }

    // Parse optional exactly_once argument (default: false)
    bool exactlyOnce = false;
    if (args.size() >= 3) {
        EvalResult exactlyOnceResult = evaluateAsBoolean(args[2], ctx);
        if (exactlyOnceResult.isError()) {
            return exactlyOnceResult;
        }
        exactlyOnce = exactlyOnceResult.getBoolean();
    }

    // Handle array result type (from another spill function)
    std::vector<std::vector<EvalResult>> data;
    if (rangeResult.isArray()) {
        data = rangeResult.getArray();
    } else {
        // Collect range as 2D array
        auto [rangeData, error] = collectRangeAs2D(rangeResult, ctx);
        if (error.isError()) {
            return error;
        }
        data = std::move(rangeData);
    }

    // Handle empty input
    if (data.empty() || (data.size() == 1 && data[0].empty())) {
        return EvalResult::EmptyArray();
    }

    if (byCol) {
        // Transpose the data so we can compare columns as rows
        const size_t numRows = data.size();
        const size_t numCols = data.empty() ? 0 : data[0].size();

        std::vector<std::vector<EvalResult>> transposed;
        transposed.reserve(numCols);
        for (size_t c = 0; c < numCols; ++c) {
            std::vector<EvalResult> col;
            col.reserve(numRows);
            for (size_t r = 0; r < numRows; ++r) {
                if (c < data[r].size()) {
                    col.push_back(data[r][c]);
                } else {
                    col.push_back(EvalResult::Empty());
                }
            }
            transposed.push_back(std::move(col));
        }
        data = std::move(transposed);
    }

    // Find unique rows
    std::vector<std::vector<EvalResult>> uniqueRows;
    std::vector<size_t> rowCounts;  // Count occurrences of each unique row

    for (const auto& row : data) {
        // Check if this row already exists in uniqueRows
        bool found = false;
        for (size_t i = 0; i < uniqueRows.size(); ++i) {
            if (rowsEqual(row, uniqueRows[i])) {
                found = true;
                rowCounts[i]++;
                break;
            }
        }
        if (!found) {
            uniqueRows.push_back(row);
            rowCounts.push_back(1);
        }
    }

    // If exactlyOnce is true, filter to only rows that appear exactly once
    if (exactlyOnce) {
        std::vector<std::vector<EvalResult>> filtered;
        for (size_t i = 0; i < uniqueRows.size(); ++i) {
            if (rowCounts[i] == 1) {
                filtered.push_back(std::move(uniqueRows[i]));
            }
        }
        uniqueRows = std::move(filtered);
    }

    // Handle case where all rows were filtered out
    if (uniqueRows.empty()) {
        return EvalResult::EmptyArray();
    }

    if (byCol) {
        // Transpose back to original orientation
        const size_t numRows = uniqueRows.empty() ? 0 : uniqueRows[0].size();
        const size_t numCols = uniqueRows.size();

        std::vector<std::vector<EvalResult>> result;
        result.reserve(numRows);
        for (size_t r = 0; r < numRows; ++r) {
            std::vector<EvalResult> row;
            row.reserve(numCols);
            for (size_t c = 0; c < numCols; ++c) {
                if (r < uniqueRows[c].size()) {
                    row.push_back(uniqueRows[c][r]);
                } else {
                    row.push_back(EvalResult::Empty());
                }
            }
            result.push_back(std::move(row));
        }
        return EvalResult::Array(std::move(result));
    }

    return EvalResult::Array(std::move(uniqueRows));
}

void registerArrayFunctions(FunctionRegistry& registry) {
    registry.registerFunction("UNIQUE", fn_UNIQUE, "(array, [by_col], [exactly_once])",
                              "Returns unique values from a range", "Array");
}

}  // namespace cells
