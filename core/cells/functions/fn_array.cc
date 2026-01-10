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

// Helper to compare two EvalResults for sorting
// Returns: -1 if a < b, 0 if a == b, 1 if a > b
// Excel sort order: numbers < text < logical < errors < empty
int compareEvalResults(const EvalResult& a, const EvalResult& b) {
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
        std::transform(strA.begin(), strA.end(), strA.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        std::transform(strB.begin(), strB.end(), strB.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        if (strA < strB) {
            return -1;
        }
        if (strA > strB) {
            return 1;
        }
        return 0;
    }

    // Both booleans
    if (a.isBoolean() && b.isBoolean()) {
        if (!a.getBoolean() && b.getBoolean()) {
            return -1;  // FALSE < TRUE
        }
        if (a.getBoolean() && !b.getBoolean()) {
            return 1;
        }
        return 0;
    }

    // Both errors
    if (a.isError() && b.isError()) {
        return 0;  // Errors are equal for sorting
    }

    // Both empty
    if (a.isEmpty() && b.isEmpty()) {
        return 0;
    }

    // Mixed types - Excel sort order: numbers < text < logical < errors < empty
    auto typeOrder = [](const EvalResult& e) -> int {
        if (e.isNumber()) {
            return 0;
        }
        if (e.isString()) {
            return 1;
        }
        if (e.isBoolean()) {
            return 2;
        }
        if (e.isError()) {
            return 3;
        }
        if (e.isEmpty()) {
            return 4;
        }
        return 5;  // Arrays or ranges
    };

    return typeOrder(a) - typeOrder(b) < 0 ? -1 : 1;
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

EvalResult fn_SORT(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    // Validate arguments: SORT(array, [sort_index], [sort_order], [by_col])
    if (args.empty() || args.size() > 4) {
        return EvalResult::Error(CellError::VALUE);
    }

    // Evaluate first argument (array/range)
    EvalResult rangeResult = evaluate(args[0], ctx);
    if (rangeResult.isError()) {
        return rangeResult;
    }

    // Parse optional sort_index argument (default: 1 = first column/row)
    int sortIndex = 1;
    if (args.size() >= 2) {
        EvalResult sortIndexResult = evaluateAsNumber(args[1], ctx);
        if (sortIndexResult.isError()) {
            return sortIndexResult;
        }
        sortIndex = static_cast<int>(sortIndexResult.getNumber());
        if (sortIndex < 1) {
            return EvalResult::Error(CellError::VALUE);
        }
    }

    // Parse optional sort_order argument (default: 1 = ascending, -1 = descending)
    int sortOrder = 1;
    if (args.size() >= 3) {
        EvalResult sortOrderResult = evaluateAsNumber(args[2], ctx);
        if (sortOrderResult.isError()) {
            return sortOrderResult;
        }
        sortOrder = static_cast<int>(sortOrderResult.getNumber());
        // Excel accepts 1 for ascending, -1 for descending
        if (sortOrder != 1 && sortOrder != -1) {
            return EvalResult::Error(CellError::VALUE);
        }
    }

    // Parse optional by_col argument (default: false = sort by rows)
    bool byCol = false;
    if (args.size() >= 4) {
        EvalResult byColResult = evaluateAsBoolean(args[3], ctx);
        if (byColResult.isError()) {
            return byColResult;
        }
        byCol = byColResult.getBoolean();
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
        // Sorting by columns: transpose, sort, transpose back
        const size_t numRows = data.size();
        const size_t numCols = data.empty() ? 0 : data[0].size();

        // Validate sort_index
        if (static_cast<size_t>(sortIndex) > numRows) {
            return EvalResult::Error(CellError::VALUE);
        }

        // Transpose: columns become rows
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

        // Sort transposed rows by the sort key
        const size_t keyIdx = static_cast<size_t>(sortIndex - 1);
        std::stable_sort(transposed.begin(), transposed.end(),
                         [keyIdx, sortOrder](const std::vector<EvalResult>& a,
                                             const std::vector<EvalResult>& b) {
                             const EvalResult& va = (keyIdx < a.size()) ? a[keyIdx] : EvalResult::Empty();
                             const EvalResult& vb = (keyIdx < b.size()) ? b[keyIdx] : EvalResult::Empty();
                             const int cmp = compareEvalResults(va, vb);
                             return sortOrder > 0 ? cmp < 0 : cmp > 0;
                         });

        // Transpose back
        const size_t sortedCols = transposed.size();
        std::vector<std::vector<EvalResult>> result;
        result.reserve(numRows);
        for (size_t r = 0; r < numRows; ++r) {
            std::vector<EvalResult> row;
            row.reserve(sortedCols);
            for (size_t c = 0; c < sortedCols; ++c) {
                if (r < transposed[c].size()) {
                    row.push_back(transposed[c][r]);
                } else {
                    row.push_back(EvalResult::Empty());
                }
            }
            result.push_back(std::move(row));
        }
        return EvalResult::Array(std::move(result));
    }

    // Sort by rows (default behavior)
    const size_t numCols = data.empty() ? 0 : data[0].size();

    // Validate sort_index
    if (static_cast<size_t>(sortIndex) > numCols) {
        return EvalResult::Error(CellError::VALUE);
    }

    const size_t keyIdx = static_cast<size_t>(sortIndex - 1);
    std::stable_sort(data.begin(), data.end(),
                     [keyIdx, sortOrder](const std::vector<EvalResult>& a,
                                         const std::vector<EvalResult>& b) {
                         const EvalResult& va = (keyIdx < a.size()) ? a[keyIdx] : EvalResult::Empty();
                         const EvalResult& vb = (keyIdx < b.size()) ? b[keyIdx] : EvalResult::Empty();
                         const int cmp = compareEvalResults(va, vb);
                         return sortOrder > 0 ? cmp < 0 : cmp > 0;
                     });

    return EvalResult::Array(std::move(data));
}

EvalResult fn_FILTER(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    // Validate arguments: FILTER(array, include, [if_empty])
    if (args.size() < 2 || args.size() > 3) {
        return EvalResult::Error(CellError::VALUE);
    }

    // Evaluate first argument (array/range)
    EvalResult rangeResult = evaluate(args[0], ctx);
    if (rangeResult.isError()) {
        return rangeResult;
    }

    // Evaluate second argument (include criteria - array of TRUE/FALSE)
    EvalResult includeResult = evaluate(args[1], ctx);
    if (includeResult.isError()) {
        return includeResult;
    }

    // Parse optional if_empty argument (value to return if all filtered out)
    EvalResult ifEmptyResult = EvalResult::Empty();
    bool hasIfEmpty = false;
    if (args.size() >= 3) {
        ifEmptyResult = evaluate(args[2], ctx);
        if (ifEmptyResult.isError()) {
            return ifEmptyResult;
        }
        hasIfEmpty = true;
    }

    // Collect array data
    std::vector<std::vector<EvalResult>> data;
    if (rangeResult.isArray()) {
        data = rangeResult.getArray();
    } else {
        auto [rangeData, error] = collectRangeAs2D(rangeResult, ctx);
        if (error.isError()) {
            return error;
        }
        data = std::move(rangeData);
    }

    // Handle empty input
    if (data.empty() || (data.size() == 1 && data[0].empty())) {
        if (hasIfEmpty) {
            // Return if_empty as a single-cell array
            std::vector<std::vector<EvalResult>> result;
            result.push_back({ifEmptyResult});
            return EvalResult::Array(std::move(result));
        }
        return EvalResult::Error(CellError::CALC);
    }

    // Collect include criteria
    std::vector<std::vector<EvalResult>> includeData;
    if (includeResult.isArray()) {
        includeData = includeResult.getArray();
    } else {
        auto [incData, error] = collectRangeAs2D(includeResult, ctx);
        if (error.isError()) {
            return error;
        }
        includeData = std::move(incData);
    }

    // Determine filter direction based on include array dimensions
    const size_t dataRows = data.size();
    const size_t dataCols = data.empty() ? 0 : data[0].size();
    const size_t incRows = includeData.size();
    const size_t incCols = includeData.empty() ? 0 : includeData[0].size();

    // Check if include is a single column (filter rows) or single row (filter columns)
    const bool filterRows = (incCols == 1 && incRows > 1) || (incRows == dataRows);
    const bool filterCols = (incRows == 1 && incCols > 1) || (incCols == dataCols);

    if (filterRows && incRows != dataRows) {
        // Include array row count must match data row count
        return EvalResult::Error(CellError::VALUE);
    }

    if (filterCols && !filterRows && incCols != dataCols) {
        // Include array column count must match data column count
        return EvalResult::Error(CellError::VALUE);
    }

    // Helper to check if a value is truthy for filtering
    auto isTruthy = [](const EvalResult& val) -> bool {
        if (val.isBoolean()) {
            return val.getBoolean();
        }
        if (val.isNumber()) {
            return val.getNumber() != 0;
        }
        // Strings, errors, empty are considered FALSE
        return false;
    };

    std::vector<std::vector<EvalResult>> result;

    if (filterRows) {
        // Filter rows based on include column
        for (size_t r = 0; r < dataRows; ++r) {
            const EvalResult& inc =
                (incCols == 1) ? includeData[r][0] : includeData[r][0];
            if (isTruthy(inc)) {
                result.push_back(data[r]);
            }
        }
    } else if (filterCols) {
        // Filter columns based on include row
        // First determine which columns to keep
        std::vector<size_t> keepCols;
        for (size_t c = 0; c < dataCols; ++c) {
            if (c < incCols && isTruthy(includeData[0][c])) {
                keepCols.push_back(c);
            }
        }

        // Build result with only kept columns
        for (size_t r = 0; r < dataRows; ++r) {
            std::vector<EvalResult> row;
            for (size_t c : keepCols) {
                if (c < data[r].size()) {
                    row.push_back(data[r][c]);
                } else {
                    row.push_back(EvalResult::Empty());
                }
            }
            result.push_back(std::move(row));
        }
    } else {
        // Include dimensions don't match - error
        return EvalResult::Error(CellError::VALUE);
    }

    // If no rows/cols matched, return if_empty or error
    if (result.empty() || (result.size() == 1 && result[0].empty())) {
        if (hasIfEmpty) {
            std::vector<std::vector<EvalResult>> emptyResult;
            emptyResult.push_back({ifEmptyResult});
            return EvalResult::Array(std::move(emptyResult));
        }
        return EvalResult::Error(CellError::CALC);
    }

    return EvalResult::Array(std::move(result));
}

EvalResult fn_SEQUENCE(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    // Validate arguments: SEQUENCE(rows, [cols], [start], [step])
    if (args.empty() || args.size() > 4) {
        return EvalResult::Error(CellError::VALUE);
    }

    // Evaluate rows argument (required)
    EvalResult rowsResult = evaluateAsNumber(args[0], ctx);
    if (rowsResult.isError()) {
        return rowsResult;
    }
    int rows = static_cast<int>(rowsResult.getNumber());
    if (rows < 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    // Evaluate cols argument (default: 1)
    int cols = 1;
    if (args.size() >= 2) {
        EvalResult colsResult = evaluateAsNumber(args[1], ctx);
        if (colsResult.isError()) {
            return colsResult;
        }
        cols = static_cast<int>(colsResult.getNumber());
        if (cols < 1) {
            return EvalResult::Error(CellError::VALUE);
        }
    }

    // Evaluate start argument (default: 1)
    double start = 1.0;
    if (args.size() >= 3) {
        EvalResult startResult = evaluateAsNumber(args[2], ctx);
        if (startResult.isError()) {
            return startResult;
        }
        start = startResult.getNumber();
    }

    // Evaluate step argument (default: 1)
    double step = 1.0;
    if (args.size() >= 4) {
        EvalResult stepResult = evaluateAsNumber(args[3], ctx);
        if (stepResult.isError()) {
            return stepResult;
        }
        step = stepResult.getNumber();
    }

    // Generate the sequence
    std::vector<std::vector<EvalResult>> result;
    result.reserve(static_cast<size_t>(rows));

    double value = start;
    for (int r = 0; r < rows; ++r) {
        std::vector<EvalResult> row;
        row.reserve(static_cast<size_t>(cols));
        for (int c = 0; c < cols; ++c) {
            row.push_back(EvalResult::Number(value));
            value += step;
        }
        result.push_back(std::move(row));
    }

    return EvalResult::Array(std::move(result));
}

void registerArrayFunctions(FunctionRegistry& registry) {
    registry.registerFunction("UNIQUE", fn_UNIQUE, "(array, [by_col], [exactly_once])",
                              "Returns unique values from a range", "Array");
    registry.registerFunction("SORT", fn_SORT, "(array, [sort_index], [sort_order], [by_col])",
                              "Sorts a range of data", "Array");
    registry.registerFunction("FILTER", fn_FILTER, "(array, include, [if_empty])",
                              "Filters a range based on criteria", "Array");
    registry.registerFunction("SEQUENCE", fn_SEQUENCE, "(rows, [cols], [start], [step])",
                              "Generates a sequence of numbers", "Array");
}

}  // namespace cells
