#include "core/cells/functions/fn_array.h"

#include <cstdlib>

#include <algorithm>
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
    const EvalResult parsed = parseFlattenArgs(args, ctx, &data, &ignore, &byColumn);
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
    const EvalResult parsed = parseFlattenArgs(args, ctx, &data, &ignore, &byColumn);
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
            row.push_back(
                gridAt(data, static_cast<size_t>(rowStart + r), static_cast<size_t>(colStart + c)));
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
    const EvalResult rowsRes = evaluateAsNumber(args[1], ctx);
    if (rowsRes.isError()) {
        return rowsRes;
    }
    const int rows = static_cast<int>(rowsRes.getNumber());
    if (args.size() == 3) {
        const EvalResult colsRes = evaluateAsNumber(args[2], ctx);
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
    const EvalResult rowsRes = evaluateAsNumber(args[1], ctx);
    if (rowsRes.isError()) {
        return rowsRes;
    }
    const int rows = static_cast<int>(rowsRes.getNumber());
    if (args.size() == 3) {
        const EvalResult colsRes = evaluateAsNumber(args[2], ctx);
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
    for (int idx : indices) {
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
            for (int c : resolved) {
                out[r].push_back(gridAt(data, r, static_cast<size_t>(c)));
            }
        }
    } else {
        for (int r : resolved) {
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
            const EvalResult maybeOrder = evaluate(args[i], ctx);
            if (maybeOrder.isError()) {
                return maybeOrder;
            }
            if (!maybeOrder.isRange() && !maybeOrder.isArray()) {
                const EvalResult n = maybeOrder.toNumber();
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
    for (size_t r : order) {
        out.push_back(data[r]);
    }
    return EvalResult::Array(std::move(out));
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
}

}  // namespace cells
