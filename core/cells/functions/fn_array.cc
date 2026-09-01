#include "core/cells/functions/fn_array.h"

#include <cmath>
#include <cstdlib>

#include <algorithm>
#include <string>
#include <vector>

#include "core/cells/formula_ast.h"
#include "core/cells/formula_functions.h"

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

size_t gridCols(const std::vector<std::vector<EvalResult>>& grid) {
    size_t cols = 0;
    for (const auto& row : grid) {
        cols = std::max(cols, row.size());
    }
    return cols;
}

EvalResult gridAt(const std::vector<std::vector<EvalResult>>& grid, size_t r, size_t c) {
    if (r >= grid.size() || c >= grid[r].size()) {
        return EvalResult::Empty();
    }
    return grid[r][c];
}

int resolveIndex(int index, int count) {
    if (index == 0 || count < 1) {
        return -1;
    }
    if (index < 0) {
        index = count + index + 1;
    }
    if (index < 1 || index > count) {
        return -1;
    }
    return index - 1;
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

    auto [data, error] = collectAs2D(rangeResult, ctx);
    if (error.isError()) {
        return error;
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

    auto [data, error] = collectAs2D(rangeResult, ctx);
    if (error.isError()) {
        return error;
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
        const auto keyIdx = static_cast<size_t>(sortIndex - 1);
        std::stable_sort(
            transposed.begin(), transposed.end(),
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

    const auto keyIdx = static_cast<size_t>(sortIndex - 1);
    std::stable_sort(
        data.begin(), data.end(),
        [keyIdx, sortOrder](const std::vector<EvalResult>& a, const std::vector<EvalResult>& b) {
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

    auto [data, error] = collectAs2D(rangeResult, ctx);
    if (error.isError()) {
        return error;
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

    auto [includeData, incError] = collectAs2D(includeResult, ctx);
    if (incError.isError()) {
        return incError;
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
            const EvalResult& inc = includeData[r][0];
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
            for (const size_t c : keepCols) {
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
    const int rows = static_cast<int>(rowsResult.getNumber());
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

EvalResult fn_TRANSPOSE(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    // Validate arguments: TRANSPOSE(array)
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    // Evaluate the array argument
    EvalResult rangeResult = evaluate(args[0], ctx);
    if (rangeResult.isError()) {
        return rangeResult;
    }

    auto [data, error] = collectAs2D(rangeResult, ctx);
    if (error.isError()) {
        return error;
    }

    // Handle empty input
    if (data.empty()) {
        return EvalResult::EmptyArray();
    }

    // Handle single value (1x1 stays 1x1)
    if (data.size() == 1 && data[0].size() == 1) {
        return EvalResult::Array(std::move(data));
    }

    // Calculate dimensions
    const size_t numRows = data.size();
    size_t numCols = 0;
    for (const auto& row : data) {
        numCols = std::max(numCols, row.size());
    }

    // If it's a single cell, just return it as is
    if (numCols == 0) {
        return EvalResult::EmptyArray();
    }

    // Create transposed result
    std::vector<std::vector<EvalResult>> result;
    result.reserve(numCols);

    for (size_t c = 0; c < numCols; ++c) {
        std::vector<EvalResult> row;
        row.reserve(numRows);
        for (size_t r = 0; r < numRows; ++r) {
            if (c < data[r].size()) {
                row.push_back(data[r][c]);
            } else {
                row.push_back(EvalResult::Empty());
            }
        }
        result.push_back(std::move(row));
    }

    return EvalResult::Array(std::move(result));
}

EvalResult fn_VSTACK(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.empty()) {
        return EvalResult::Error(CellError::VALUE);
    }
    std::vector<std::vector<std::vector<EvalResult>>> arrays;
    arrays.reserve(args.size());
    size_t maxCols = 0;
    for (const ASTNode* arg : args) {
        auto [grid, error] = evaluateAs2D(arg, ctx);
        if (error.isError()) {
            return error;
        }
        maxCols = std::max(maxCols, gridCols(grid));
        arrays.push_back(std::move(grid));
    }
    std::vector<std::vector<EvalResult>> out;
    for (auto& grid : arrays) {
        for (auto& row : grid) {
            while (row.size() < maxCols) {
                row.push_back(EvalResult::Error(CellError::NA));
            }
            out.push_back(std::move(row));
        }
    }
    if (out.empty()) {
        return EvalResult::EmptyArray();
    }
    return EvalResult::Array(std::move(out));
}

EvalResult fn_HSTACK(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.empty()) {
        return EvalResult::Error(CellError::VALUE);
    }
    std::vector<std::vector<std::vector<EvalResult>>> arrays;
    arrays.reserve(args.size());
    size_t maxRows = 0;
    for (const ASTNode* arg : args) {
        auto [grid, error] = evaluateAs2D(arg, ctx);
        if (error.isError()) {
            return error;
        }
        maxRows = std::max(maxRows, grid.size());
        arrays.push_back(std::move(grid));
    }
    std::vector<std::vector<EvalResult>> out(maxRows);
    for (auto& grid : arrays) {
        const size_t cols = gridCols(grid);
        for (size_t r = 0; r < maxRows; ++r) {
            for (size_t c = 0; c < cols; ++c) {
                if (r < grid.size() && c < grid[r].size()) {
                    out[r].push_back(grid[r][c]);
                } else {
                    out[r].push_back(EvalResult::Error(CellError::NA));
                }
            }
        }
    }
    if (out.empty()) {
        return EvalResult::EmptyArray();
    }
    return EvalResult::Array(std::move(out));
}

static std::pair<std::vector<EvalResult>, EvalResult> flattenGrid(
    const std::vector<std::vector<EvalResult>>& data, int ignore, bool byColumn) {
    if (ignore < 0 || ignore > 3) {
        return {{}, EvalResult::Error(CellError::VALUE)};
    }
    const bool dropBlanks = ignore == 1 || ignore == 3;
    const bool dropErrors = ignore == 2 || ignore == 3;
    std::vector<EvalResult> flat;
    if (byColumn) {
        const size_t cols = gridCols(data);
        for (size_t c = 0; c < cols; ++c) {
            for (size_t r = 0; r < data.size(); ++r) {
                const EvalResult v = gridAt(data, r, c);
                if ((dropBlanks && v.isEmpty()) || (dropErrors && v.isError())) {
                    continue;
                }
                flat.push_back(v);
            }
        }
    } else {
        for (const auto& row : data) {
            for (const EvalResult& v : row) {
                if ((dropBlanks && v.isEmpty()) || (dropErrors && v.isError())) {
                    continue;
                }
                flat.push_back(v);
            }
        }
    }
    if (flat.empty()) {
        return {{}, EvalResult::Error(CellError::CALC)};
    }
    return {std::move(flat), EvalResult::Empty()};
}

static EvalResult parseFlattenArgs(const std::vector<const ASTNode*>& args, EvalContext& ctx,
                                   std::vector<std::vector<EvalResult>>* data, int* ignore,
                                   bool* byColumn) {
    if (args.empty() || args.size() > 3) {
        return EvalResult::Error(CellError::VALUE);
    }
    auto [grid, error] = evaluateAs2D(args[0], ctx);
    if (error.isError()) {
        return error;
    }
    *data = std::move(grid);
    *ignore = 0;
    *byColumn = false;
    if (args.size() >= 2) {
        const EvalResult ign = evaluateAsNumber(args[1], ctx);
        if (ign.isError()) {
            return ign;
        }
        *ignore = static_cast<int>(ign.getNumber());
    }
    if (args.size() >= 3) {
        const EvalResult scan = evaluateAsBoolean(args[2], ctx);
        if (scan.isError()) {
            return scan;
        }
        *byColumn = scan.getBoolean();
    }
    return EvalResult::Empty();
}

EvalResult fn_TOCOL(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    std::vector<std::vector<EvalResult>> data;
    int ignore = 0;
    bool byColumn = false;
    EvalResult parsed = parseFlattenArgs(args, ctx, &data, &ignore, &byColumn);
    if (parsed.isError()) {
        return parsed;
    }
    auto [flat, err] = flattenGrid(data, ignore, byColumn);
    if (err.isError()) {
        return err;
    }
    return EvalResult::ColumnArray(std::move(flat));
}

EvalResult fn_TOROW(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    std::vector<std::vector<EvalResult>> data;
    int ignore = 0;
    bool byColumn = false;
    EvalResult parsed = parseFlattenArgs(args, ctx, &data, &ignore, &byColumn);
    if (parsed.isError()) {
        return parsed;
    }
    auto [flat, err] = flattenGrid(data, ignore, byColumn);
    if (err.isError()) {
        return err;
    }
    return EvalResult::RowArray(std::move(flat));
}

static EvalResult takeOrDrop(const std::vector<std::vector<EvalResult>>& data, const int* rows,
                             const int* cols, bool dropping) {
    const int height = static_cast<int>(data.size());
    const int width = static_cast<int>(gridCols(data));
    int rowStart = 0;
    int rowCount = height;
    if (rows != nullptr) {
        if (!dropping && *rows == 0) {
            return EvalResult::Error(CellError::VALUE);
        }
        if (dropping) {
            const int n = *rows;
            if (n == 0) {
                // keep all rows
            } else if (n > 0) {
                if (n >= height) {
                    return EvalResult::Error(CellError::CALC);
                }
                rowStart = n;
                rowCount = height - n;
            } else {
                const int drop = -n;
                if (drop >= height) {
                    return EvalResult::Error(CellError::CALC);
                }
                rowCount = height - drop;
            }
        } else {
            const int n = *rows;
            if (n > 0) {
                rowCount = std::min(n, height);
            } else {
                rowCount = std::min(-n, height);
                rowStart = height - rowCount;
            }
        }
    }
    int colStart = 0;
    int colCount = width;
    if (cols != nullptr) {
        if (!dropping && *cols == 0) {
            return EvalResult::Error(CellError::VALUE);
        }
        if (dropping) {
            const int n = *cols;
            if (n == 0) {
                // keep all cols
            } else if (n > 0) {
                if (n >= width) {
                    return EvalResult::Error(CellError::CALC);
                }
                colStart = n;
                colCount = width - n;
            } else {
                const int drop = -n;
                if (drop >= width) {
                    return EvalResult::Error(CellError::CALC);
                }
                colCount = width - drop;
            }
        } else {
            const int n = *cols;
            if (n > 0) {
                colCount = std::min(n, width);
            } else {
                colCount = std::min(-n, width);
                colStart = width - colCount;
            }
        }
    }
    if (rowCount <= 0 || colCount <= 0) {
        return EvalResult::Error(CellError::CALC);
    }
    std::vector<std::vector<EvalResult>> out;
    out.reserve(static_cast<size_t>(rowCount));
    for (int r = 0; r < rowCount; ++r) {
        std::vector<EvalResult> row;
        row.reserve(static_cast<size_t>(colCount));
        for (int c = 0; c < colCount; ++c) {
            row.push_back(gridAt(data, static_cast<size_t>(rowStart) + static_cast<size_t>(r),
                                static_cast<size_t>(colStart) + static_cast<size_t>(c)));
        }
        out.push_back(std::move(row));
    }
    return EvalResult::Array(std::move(out));
}

EvalResult fn_TAKE(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() < 2 || args.size() > 3) {
        return EvalResult::Error(CellError::VALUE);
    }
    auto [data, error] = evaluateAs2D(args[0], ctx);
    if (error.isError()) {
        return error;
    }
    EvalResult rowsRes = evaluateAsNumber(args[1], ctx);
    if (rowsRes.isError()) {
        return rowsRes;
    }
    const int rows = static_cast<int>(rowsRes.getNumber());
    if (args.size() == 3) {
        EvalResult colsRes = evaluateAsNumber(args[2], ctx);
        if (colsRes.isError()) {
            return colsRes;
        }
        const int cols = static_cast<int>(colsRes.getNumber());
        return takeOrDrop(data, &rows, &cols, false);
    }
    return takeOrDrop(data, &rows, nullptr, false);
}

EvalResult fn_DROP(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() < 2 || args.size() > 3) {
        return EvalResult::Error(CellError::VALUE);
    }
    auto [data, error] = evaluateAs2D(args[0], ctx);
    if (error.isError()) {
        return error;
    }
    EvalResult rowsRes = evaluateAsNumber(args[1], ctx);
    if (rowsRes.isError()) {
        return rowsRes;
    }
    const int rows = static_cast<int>(rowsRes.getNumber());
    if (args.size() == 3) {
        EvalResult colsRes = evaluateAsNumber(args[2], ctx);
        if (colsRes.isError()) {
            return colsRes;
        }
        const int cols = static_cast<int>(colsRes.getNumber());
        return takeOrDrop(data, &rows, &cols, true);
    }
    return takeOrDrop(data, &rows, nullptr, true);
}

static EvalResult chooseAxes(const std::vector<std::vector<EvalResult>>& data,
                             const std::vector<int>& indices, bool byColumn) {
    const int count = byColumn ? static_cast<int>(gridCols(data)) : static_cast<int>(data.size());
    std::vector<int> resolved;
    resolved.reserve(indices.size());
    for (const int idx : indices) {
        const int zero = resolveIndex(idx, count);
        if (zero < 0) {
            return EvalResult::Error(CellError::VALUE);
        }
        resolved.push_back(zero);
    }
    std::vector<std::vector<EvalResult>> out;
    if (byColumn) {
        out.resize(data.size());
        for (size_t r = 0; r < data.size(); ++r) {
            for (const int c : resolved) {
                out[r].push_back(gridAt(data, r, static_cast<size_t>(c)));
            }
        }
    } else {
        for (const int r : resolved) {
            const size_t cols = gridCols(data);
            std::vector<EvalResult> row;
            row.reserve(cols);
            for (size_t c = 0; c < cols; ++c) {
                row.push_back(gridAt(data, static_cast<size_t>(r), c));
            }
            out.push_back(std::move(row));
        }
    }
    if (out.empty()) {
        return EvalResult::Error(CellError::VALUE);
    }
    return EvalResult::Array(std::move(out));
}

EvalResult fn_CHOOSECOLS(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() < 2) {
        return EvalResult::Error(CellError::VALUE);
    }
    auto [data, error] = evaluateAs2D(args[0], ctx);
    if (error.isError()) {
        return error;
    }
    std::vector<int> indices;
    for (size_t i = 1; i < args.size(); ++i) {
        const EvalResult n = evaluateAsNumber(args[i], ctx);
        if (n.isError()) {
            return n;
        }
        indices.push_back(static_cast<int>(n.getNumber()));
    }
    return chooseAxes(data, indices, true);
}

EvalResult fn_CHOOSEROWS(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() < 2) {
        return EvalResult::Error(CellError::VALUE);
    }
    auto [data, error] = evaluateAs2D(args[0], ctx);
    if (error.isError()) {
        return error;
    }
    std::vector<int> indices;
    for (size_t i = 1; i < args.size(); ++i) {
        const EvalResult n = evaluateAsNumber(args[i], ctx);
        if (n.isError()) {
            return n;
        }
        indices.push_back(static_cast<int>(n.getNumber()));
    }
    return chooseAxes(data, indices, false);
}

EvalResult fn_SORTBY(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() < 2) {
        return EvalResult::Error(CellError::VALUE);
    }
    auto [data, error] = evaluateAs2D(args[0], ctx);
    if (error.isError()) {
        return error;
    }
    if (data.empty()) {
        return EvalResult::EmptyArray();
    }

    struct SortKey {
        std::vector<EvalResult> values;
        int order = 1;
    };
    std::vector<SortKey> keys;
    size_t i = 1;
    while (i < args.size()) {
        auto [by, byErr] = evaluateAs2D(args[i], ctx);
        if (byErr.isError()) {
            return byErr;
        }
        ++i;
        int order = 1;
        if (i < args.size()) {
            EvalResult maybeOrder = evaluate(args[i], ctx);
            if (maybeOrder.isError()) {
                return maybeOrder;
            }
            if (!maybeOrder.isRange() && !maybeOrder.isArray()) {
                EvalResult n = maybeOrder.toNumber();
                if (n.isError()) {
                    return n;
                }
                order = static_cast<int>(n.getNumber());
                if (order != 1 && order != -1) {
                    return EvalResult::Error(CellError::VALUE);
                }
                ++i;
            }
        }
        std::vector<EvalResult> values;
        if (by.size() == data.size()) {
            values.reserve(data.size());
            for (size_t r = 0; r < data.size(); ++r) {
                values.push_back(gridAt(by, r, 0));
            }
        } else if (by.size() == 1 && gridCols(by) == data.size()) {
            values = by[0];
        } else {
            return EvalResult::Error(CellError::VALUE);
        }
        keys.push_back(SortKey{std::move(values), order});
    }
    if (keys.empty()) {
        return EvalResult::Error(CellError::VALUE);
    }

    std::vector<size_t> order(data.size());
    for (size_t r = 0; r < data.size(); ++r) {
        order[r] = r;
    }
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        for (const SortKey& key : keys) {
            const int cmp = compareEvalResults(key.values[a], key.values[b]);
            if (cmp != 0) {
                return key.order > 0 ? cmp < 0 : cmp > 0;
            }
        }
        return false;
    });
    std::vector<std::vector<EvalResult>> out;
    out.reserve(data.size());
    for (const size_t r : order) {
        out.push_back(data[r]);
    }
    return EvalResult::Array(std::move(out));
}

namespace {

std::vector<EvalResult> flattenPreserve(const std::vector<std::vector<EvalResult>>& data,
                                        bool byColumn) {
    std::vector<EvalResult> flat;
    if (byColumn) {
        const size_t cols = gridCols(data);
        for (size_t c = 0; c < cols; ++c) {
            for (size_t r = 0; r < data.size(); ++r) {
                flat.push_back(gridAt(data, r, c));
            }
        }
    } else {
        for (const auto& row : data) {
            for (const EvalResult& v : row) {
                flat.push_back(v);
            }
        }
    }
    return flat;
}

bool isVector1D(const std::vector<std::vector<EvalResult>>& data) {
    if (data.empty()) {
        return false;
    }
    const size_t cols = gridCols(data);
    return data.size() == 1 || cols <= 1;
}

EvalResult evalOptionalPad(const std::vector<const ASTNode*>& args, size_t index, EvalContext& ctx,
                           EvalResult* pad) {
    if (args.size() <= index) {
        *pad = EvalResult::Error(CellError::NA);
        return EvalResult::Empty();
    }
    *pad = evaluate(args[index], ctx);
    return EvalResult::Empty();
}

EvalResult wrapVector(const std::vector<EvalResult>& flat, int wrapCount, const EvalResult& pad,
                      bool wrapCols) {
    if (wrapCount < 1) {
        return EvalResult::Error(CellError::NUM);
    }
    if (flat.empty()) {
        return EvalResult::EmptyArray();
    }
    const int n = static_cast<int>(flat.size());
    const int groups = (n + wrapCount - 1) / wrapCount;
    std::vector<std::vector<EvalResult>> out;
    if (wrapCols) {
        out.resize(static_cast<size_t>(wrapCount),
                   std::vector<EvalResult>(static_cast<size_t>(groups), pad));
        for (int i = 0; i < n; ++i) {
            const int r = i % wrapCount;
            const int c = i / wrapCount;
            out[static_cast<size_t>(r)][static_cast<size_t>(c)] = flat[static_cast<size_t>(i)];
        }
    } else {
        out.resize(static_cast<size_t>(groups),
                   std::vector<EvalResult>(static_cast<size_t>(wrapCount), pad));
        for (int i = 0; i < n; ++i) {
            const int r = i / wrapCount;
            const int c = i % wrapCount;
            out[static_cast<size_t>(r)][static_cast<size_t>(c)] = flat[static_cast<size_t>(i)];
        }
    }
    return EvalResult::Array(std::move(out));
}

bool cellIsBlank(const EvalResult& v) {
    return v.isEmpty() || (v.isString() && v.getString().empty());
}

int trimMode(const EvalResult& n) {
    if (n.isError()) {
        return -1;
    }
    const int m = static_cast<int>(n.getNumber());
    if (m < 0 || m > 3) {
        return -1;
    }
    return m;
}

EvalResult asMatrixNumber(const EvalResult& v) {
    if (v.isError()) {
        return v;
    }
    if (v.isNumber()) {
        return v;
    }
    if (v.isEmpty()) {
        return EvalResult::Number(0.0);
    }
    return EvalResult::Error(CellError::VALUE);
}

std::pair<std::vector<std::vector<double>>, EvalResult> toNumericMatrix(
    const std::vector<std::vector<EvalResult>>& data) {
    const size_t rows = data.size();
    const size_t cols = gridCols(data);
    if (rows == 0 || cols == 0) {
        return {{}, EvalResult::Error(CellError::VALUE)};
    }
    std::vector<std::vector<double>> m(rows, std::vector<double>(cols, 0.0));
    for (size_t r = 0; r < rows; ++r) {
        for (size_t c = 0; c < cols; ++c) {
            const EvalResult n = asMatrixNumber(gridAt(data, r, c));
            if (n.isError()) {
                return {{}, n};
            }
            m[r][c] = n.getNumber();
        }
    }
    return {std::move(m), EvalResult::Empty()};
}

double matrixAbsMax(const std::vector<std::vector<double>>& m) {
    double mx = 0.0;
    for (const auto& row : m) {
        for (const double v : row) {
            mx = std::max(mx, std::fabs(v));
        }
    }
    return mx;
}

EvalResult gaussDeterminant(std::vector<std::vector<double>> a) {
    const size_t n = a.size();
    const double scale = matrixAbsMax(a);
    const double eps = std::max(1e-14, 1e-12 * scale);
    double det = 1.0;
    for (size_t k = 0; k < n; ++k) {
        size_t pivot = k;
        double best = std::fabs(a[k][k]);
        for (size_t i = k + 1; i < n; ++i) {
            const double mag = std::fabs(a[i][k]);
            if (mag > best) {
                best = mag;
                pivot = i;
            }
        }
        if (best <= eps) {
            return EvalResult::Number(0.0);
        }
        if (pivot != k) {
            std::swap(a[k], a[pivot]);
            det = -det;
        }
        det *= a[k][k];
        for (size_t i = k + 1; i < n; ++i) {
            const double f = a[i][k] / a[k][k];
            for (size_t j = k; j < n; ++j) {
                a[i][j] -= f * a[k][j];
            }
        }
    }
    if (!std::isfinite(det)) {
        return EvalResult::Error(CellError::NUM);
    }
    return EvalResult::Number(excelNormalize(det));
}

EvalResult gaussInverse(std::vector<std::vector<double>> a) {
    const size_t n = a.size();
    const double scale = matrixAbsMax(a);
    const double eps = std::max(1e-14, 1e-12 * scale);
    std::vector<std::vector<double>> inv(n, std::vector<double>(n, 0.0));
    for (size_t i = 0; i < n; ++i) {
        inv[i][i] = 1.0;
    }
    for (size_t k = 0; k < n; ++k) {
        size_t pivot = k;
        double best = std::fabs(a[k][k]);
        for (size_t i = k + 1; i < n; ++i) {
            const double mag = std::fabs(a[i][k]);
            if (mag > best) {
                best = mag;
                pivot = i;
            }
        }
        if (best <= eps) {
            return EvalResult::Error(CellError::NUM);
        }
        if (pivot != k) {
            std::swap(a[k], a[pivot]);
            std::swap(inv[k], inv[pivot]);
        }
        const double diag = a[k][k];
        for (size_t j = 0; j < n; ++j) {
            a[k][j] /= diag;
            inv[k][j] /= diag;
        }
        for (size_t i = 0; i < n; ++i) {
            if (i == k) {
                continue;
            }
            const double f = a[i][k];
            for (size_t j = 0; j < n; ++j) {
                a[i][j] -= f * a[k][j];
                inv[i][j] -= f * inv[k][j];
            }
        }
    }
    std::vector<std::vector<EvalResult>> out(n, std::vector<EvalResult>(n));
    for (size_t r = 0; r < n; ++r) {
        for (size_t c = 0; c < n; ++c) {
            if (!std::isfinite(inv[r][c])) {
                return EvalResult::Error(CellError::NUM);
            }
            out[r][c] = EvalResult::Number(excelNormalize(inv[r][c]));
        }
    }
    return EvalResult::Array(std::move(out));
}

std::string arrayCellText(const EvalResult& v, bool strict) {
    if (v.isError()) {
        return errorToString(v.getError());
    }
    if (v.isEmpty()) {
        return "";
    }
    if (strict && v.isString()) {
        std::string out = "\"";
        for (const char c : v.getString()) {
            if (c == '"') {
                out += "\"\"";
            } else {
                out.push_back(c);
            }
        }
        out.push_back('"');
        return out;
    }
    const EvalResult s = v.toString();
    if (s.isError()) {
        return errorToString(s.getError());
    }
    return s.getString();
}

}  // namespace

EvalResult fn_WRAPCOLS(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() < 2 || args.size() > 3) {
        return EvalResult::Error(CellError::VALUE);
    }
    auto [data, error] = evaluateAs2D(args[0], ctx);
    if (error.isError()) {
        return error;
    }
    if (!isVector1D(data)) {
        return EvalResult::Error(CellError::VALUE);
    }
    EvalResult nRes = evaluateAsNumber(args[1], ctx);
    if (nRes.isError()) {
        return nRes;
    }
    EvalResult pad;
    EvalResult padErr = evalOptionalPad(args, 2, ctx, &pad);
    if (padErr.isError()) {
        return padErr;
    }
    return wrapVector(flattenPreserve(data, false), static_cast<int>(nRes.getNumber()), pad, true);
}

EvalResult fn_WRAPROWS(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() < 2 || args.size() > 3) {
        return EvalResult::Error(CellError::VALUE);
    }
    auto [data, error] = evaluateAs2D(args[0], ctx);
    if (error.isError()) {
        return error;
    }
    if (!isVector1D(data)) {
        return EvalResult::Error(CellError::VALUE);
    }
    EvalResult nRes = evaluateAsNumber(args[1], ctx);
    if (nRes.isError()) {
        return nRes;
    }
    EvalResult pad;
    EvalResult padErr = evalOptionalPad(args, 2, ctx, &pad);
    if (padErr.isError()) {
        return padErr;
    }
    return wrapVector(flattenPreserve(data, false), static_cast<int>(nRes.getNumber()), pad, false);
}

EvalResult fn_EXPAND(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.empty() || args.size() > 4) {
        return EvalResult::Error(CellError::VALUE);
    }
    auto [data, error] = evaluateAs2D(args[0], ctx);
    if (error.isError()) {
        return error;
    }
    const int curRows = static_cast<int>(data.size());
    const int curCols = static_cast<int>(gridCols(data));
    int rows = curRows;
    int cols = curCols;
    if (args.size() >= 2) {
        EvalResult r = evaluate(args[1], ctx);
        if (r.isError()) {
            return r;
        }
        if (!r.isEmpty()) {
            const EvalResult n = r.toNumber();
            if (n.isError()) {
                return n;
            }
            rows = static_cast<int>(n.getNumber());
        }
    }
    if (args.size() >= 3) {
        EvalResult c = evaluate(args[2], ctx);
        if (c.isError()) {
            return c;
        }
        if (!c.isEmpty()) {
            const EvalResult n = c.toNumber();
            if (n.isError()) {
                return n;
            }
            cols = static_cast<int>(n.getNumber());
        }
    }
    if (rows < 1 || cols < 1) {
        return EvalResult::Error(CellError::NUM);
    }
    if (rows < curRows || cols < curCols) {
        return EvalResult::Error(CellError::VALUE);
    }
    EvalResult pad = EvalResult::Error(CellError::NA);
    if (args.size() >= 4) {
        pad = evaluate(args[3], ctx);
    }
    std::vector<std::vector<EvalResult>> out(
        static_cast<size_t>(rows), std::vector<EvalResult>(static_cast<size_t>(cols), pad));
    for (int r = 0; r < curRows; ++r) {
        for (int c = 0; c < curCols; ++c) {
            out[static_cast<size_t>(r)][static_cast<size_t>(c)] =
                gridAt(data, static_cast<size_t>(r), static_cast<size_t>(c));
        }
    }
    return EvalResult::Array(std::move(out));
}

EvalResult fn_TRIMRANGE(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.empty() || args.size() > 3) {
        return EvalResult::Error(CellError::VALUE);
    }
    auto [data, error] = evaluateAs2D(args[0], ctx);
    if (error.isError()) {
        return error;
    }
    int rowMode = 3;
    int colMode = 3;
    if (args.size() >= 2) {
        EvalResult m = evaluateAsNumber(args[1], ctx);
        if (m.isError()) {
            return m;
        }
        rowMode = trimMode(m);
        if (rowMode < 0) {
            return EvalResult::Error(CellError::VALUE);
        }
    }
    if (args.size() >= 3) {
        EvalResult m = evaluateAsNumber(args[2], ctx);
        if (m.isError()) {
            return m;
        }
        colMode = trimMode(m);
        if (colMode < 0) {
            return EvalResult::Error(CellError::VALUE);
        }
    }
    const int height = static_cast<int>(data.size());
    const int width = static_cast<int>(gridCols(data));
    if (height == 0 || width == 0) {
        return EvalResult::EmptyArray();
    }
    auto rowBlank = [&](int r) {
        for (int c = 0; c < width; ++c) {
            if (!cellIsBlank(gridAt(data, static_cast<size_t>(r), static_cast<size_t>(c)))) {
                return false;
            }
        }
        return true;
    };
    auto colBlank = [&](int c) {
        for (int r = 0; r < height; ++r) {
            if (!cellIsBlank(gridAt(data, static_cast<size_t>(r), static_cast<size_t>(c)))) {
                return false;
            }
        }
        return true;
    };
    int r0 = 0;
    int r1 = height - 1;
    int c0 = 0;
    int c1 = width - 1;
    if (rowMode == 1 || rowMode == 3) {
        while (r0 <= r1 && rowBlank(r0)) {
            ++r0;
        }
    }
    if (rowMode == 2 || rowMode == 3) {
        while (r1 >= r0 && rowBlank(r1)) {
            --r1;
        }
    }
    if (colMode == 1 || colMode == 3) {
        while (c0 <= c1 && colBlank(c0)) {
            ++c0;
        }
    }
    if (colMode == 2 || colMode == 3) {
        while (c1 >= c0 && colBlank(c1)) {
            --c1;
        }
    }
    if (r0 > r1 || c0 > c1) {
        return EvalResult::EmptyArray();
    }
    std::vector<std::vector<EvalResult>> out;
    out.reserve(static_cast<size_t>(r1) - static_cast<size_t>(r0) + 1);
    for (int r = r0; r <= r1; ++r) {
        std::vector<EvalResult> row;
        row.reserve(static_cast<size_t>(c1) - static_cast<size_t>(c0) + 1);
        for (int c = c0; c <= c1; ++c) {
            row.push_back(gridAt(data, static_cast<size_t>(r), static_cast<size_t>(c)));
        }
        out.push_back(std::move(row));
    }
    return EvalResult::Array(std::move(out));
}

EvalResult fn_FLATTEN(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.empty()) {
        return EvalResult::Error(CellError::VALUE);
    }
    std::vector<EvalResult> flat;
    for (const ASTNode* arg : args) {
        auto [data, error] = evaluateAs2D(arg, ctx);
        if (error.isError()) {
            return error;
        }
        auto part = flattenPreserve(data, false);
        flat.insert(flat.end(), part.begin(), part.end());
    }
    if (flat.empty()) {
        return EvalResult::EmptyArray();
    }
    return EvalResult::ColumnArray(std::move(flat));
}

EvalResult fn_ARRAYTOTEXT(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.empty() || args.size() > 2) {
        return EvalResult::Error(CellError::VALUE);
    }
    auto [data, error] = evaluateAs2D(args[0], ctx);
    if (error.isError()) {
        return error;
    }
    int format = 0;
    if (args.size() == 2) {
        EvalResult f = evaluateAsNumber(args[1], ctx);
        if (f.isError()) {
            return f;
        }
        format = static_cast<int>(f.getNumber());
        if (format != 0 && format != 1) {
            return EvalResult::Error(CellError::VALUE);
        }
    }
    const bool strict = format == 1;
    const size_t rows = data.size();
    const size_t cols = gridCols(data);
    std::string out;
    if (strict) {
        out.push_back('{');
    }
    for (size_t r = 0; r < rows; ++r) {
        if (r > 0) {
            out += strict ? ";" : "; ";
        }
        for (size_t c = 0; c < cols; ++c) {
            if (c > 0) {
                out += strict ? "," : ", ";
            }
            out += arrayCellText(gridAt(data, r, c), strict);
        }
    }
    if (strict) {
        out.push_back('}');
    }
    return EvalResult::String(std::move(out));
}

EvalResult fn_MUNIT(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }
    EvalResult nRes = evaluateAsNumber(args[0], ctx);
    if (nRes.isError()) {
        return nRes;
    }
    const int n = static_cast<int>(nRes.getNumber());
    if (n < 1) {
        return EvalResult::Error(CellError::VALUE);
    }
    std::vector<std::vector<EvalResult>> out(
        static_cast<size_t>(n),
        std::vector<EvalResult>(static_cast<size_t>(n), EvalResult::Number(0.0)));
    for (int i = 0; i < n; ++i) {
        out[static_cast<size_t>(i)][static_cast<size_t>(i)] = EvalResult::Number(1.0);
    }
    return EvalResult::Array(std::move(out));
}

EvalResult fn_MMULT(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 2) {
        return EvalResult::Error(CellError::VALUE);
    }
    auto [a, aErr] = evaluateAs2D(args[0], ctx);
    if (aErr.isError()) {
        return aErr;
    }
    auto [b, bErr] = evaluateAs2D(args[1], ctx);
    if (bErr.isError()) {
        return bErr;
    }
    auto [am, aNumErr] = toNumericMatrix(a);
    if (aNumErr.isError()) {
        return aNumErr;
    }
    auto [bm, bNumErr] = toNumericMatrix(b);
    if (bNumErr.isError()) {
        return bNumErr;
    }
    const size_t ar = am.size();
    const size_t ac = am.empty() ? 0 : am[0].size();
    const size_t br = bm.size();
    const size_t bc = bm.empty() ? 0 : bm[0].size();
    if (ac != br) {
        return EvalResult::Error(CellError::VALUE);
    }
    std::vector<std::vector<EvalResult>> out(ar, std::vector<EvalResult>(bc));
    for (size_t i = 0; i < ar; ++i) {
        for (size_t j = 0; j < bc; ++j) {
            double sum = 0.0;
            for (size_t k = 0; k < ac; ++k) {
                sum += am[i][k] * bm[k][j];
            }
            if (!std::isfinite(sum)) {
                return EvalResult::Error(CellError::NUM);
            }
            out[i][j] = EvalResult::Number(excelNormalize(sum));
        }
    }
    return EvalResult::Array(std::move(out));
}

EvalResult fn_MDETERM(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }
    auto [data, error] = evaluateAs2D(args[0], ctx);
    if (error.isError()) {
        return error;
    }
    auto [m, nerr] = toNumericMatrix(data);
    if (nerr.isError()) {
        return nerr;
    }
    if (m.size() != (m.empty() ? 0 : m[0].size())) {
        return EvalResult::Error(CellError::VALUE);
    }
    return gaussDeterminant(std::move(m));
}

EvalResult fn_MINVERSE(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }
    auto [data, error] = evaluateAs2D(args[0], ctx);
    if (error.isError()) {
        return error;
    }
    auto [m, nerr] = toNumericMatrix(data);
    if (nerr.isError()) {
        return nerr;
    }
    if (m.size() != (m.empty() ? 0 : m[0].size())) {
        return EvalResult::Error(CellError::VALUE);
    }
    return gaussInverse(std::move(m));
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
    registry.registerFunction("TRANSPOSE", fn_TRANSPOSE, "(array)", "Transposes rows and columns",
                              "Array");
    registry.registerFunction("VSTACK", fn_VSTACK, "(array1, [array2], ...)",
                              "Appends arrays vertically", "Array");
    registry.registerFunction("HSTACK", fn_HSTACK, "(array1, [array2], ...)",
                              "Appends arrays horizontally", "Array");
    registry.registerFunction("TOCOL", fn_TOCOL, "(array, [ignore], [scan_by_column])",
                              "Returns the array as a single column", "Array");
    registry.registerFunction("TOROW", fn_TOROW, "(array, [ignore], [scan_by_column])",
                              "Returns the array as a single row", "Array");
    registry.registerFunction("TAKE", fn_TAKE, "(array, rows, [cols])",
                              "Returns the first or last rows/columns of an array", "Array");
    registry.registerFunction("DROP", fn_DROP, "(array, rows, [cols])",
                              "Excludes the first or last rows/columns of an array", "Array");
    registry.registerFunction("CHOOSECOLS", fn_CHOOSECOLS, "(array, col_num1, [col_num2], ...)",
                              "Returns the specified columns from an array", "Array");
    registry.registerFunction("CHOOSEROWS", fn_CHOOSEROWS, "(array, row_num1, [row_num2], ...)",
                              "Returns the specified rows from an array", "Array");
    registry.registerFunction("SORTBY", fn_SORTBY, "(array, by_array1, [sort_order1], ...)",
                              "Sorts an array by the values in another array", "Array");
    registry.registerFunction("WRAPCOLS", fn_WRAPCOLS, "(vector, wrap_count, [pad_with])",
                              "Wraps a vector into columns", "Array");
    registry.registerFunction("WRAPROWS", fn_WRAPROWS, "(vector, wrap_count, [pad_with])",
                              "Wraps a vector into rows", "Array");
    registry.registerFunction("EXPAND", fn_EXPAND, "(array, rows, [columns], [pad_with])",
                              "Expands an array to the specified size", "Array");
    registry.registerFunction("TRIMRANGE", fn_TRIMRANGE,
                              "(range, [row_trim_mode], [col_trim_mode])",
                              "Trims empty rows and columns from a range", "Array");
    registry.registerFunction("FLATTEN", fn_FLATTEN, "(range1, [range2], ...)",
                              "Flattens arrays into a single column", "Array");
    registry.registerFunction("ARRAYTOTEXT", fn_ARRAYTOTEXT, "(array, [format])",
                              "Returns an array as text", "Array");
    registry.registerFunction("MUNIT", fn_MUNIT, "(dimension)", "Unit (identity) matrix", "Math");
    registry.registerFunction("MMULT", fn_MMULT, "(array1, array2)", "Matrix product", "Math");
    registry.registerFunction("MDETERM", fn_MDETERM, "(array)", "Matrix determinant", "Math");
    registry.registerFunction("MINVERSE", fn_MINVERSE, "(array)", "Inverse matrix", "Math");
}

}  // namespace cells
