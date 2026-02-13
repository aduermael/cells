// =============================================================================
// Formula Evaluation
// =============================================================================
//
// Evaluates formula ASTs to compute cell values. Handles type coercion,
// range iteration, and error propagation according to Excel semantics.
//
// Key responsibilities:
// - Evaluate AST nodes recursively to produce EvalResult values
// - Implement type coercion (number/string/boolean conversions)
// - Handle range references and iterate over cell values
// - Detect circular references during evaluation
// - Enforce recursion limits for deeply nested formulas
//
// EvalResult types:
// - NUMBER: numeric value (double)
// - STRING: text value
// - BOOLEAN: true/false
// - ERROR: CellError (VALUE, REF, DIV, etc.)
// - EMPTY: blank cell
// - RANGE: bounds for SUM, AVERAGE, etc.
//
// Dependencies: types.h
// Used by: formula_recalc.h, formula_functions.h, bindings.cc
//
// =============================================================================

#ifndef CELLS_FORMULA_EVAL_H_
#define CELLS_FORMULA_EVAL_H_

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>

#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

#include "core/cells/types.h"

namespace cells {

// Forward declarations
struct ASTNode;
struct Sheet;
struct Workbook;
struct Cell;
struct EvalContext;

// Range type for identifying what kind of range reference this is
enum class RangeType : std::uint8_t {
    CELL_RANGE,    // A1:C3 - rectangular range
    COLUMN,        // A:A - single whole column
    ROW,           // 1:1 - single whole row
    COLUMN_RANGE,  // A:C - multiple whole columns
    ROW_RANGE,     // 1:5 - multiple whole rows
};

// Range bounds - stores the column/row IDs that define a range
struct RangeBounds {
    ID startColId;  // First column ID (or empty for row-only ranges)
    ID endColId;    // Last column ID (or empty for row-only ranges)
    ID startRowId;  // First row ID (or empty for column-only ranges)
    ID endRowId;    // Last row ID (or empty for column-only ranges)
    // For CELL_RANGE: store row positions to handle sparse rows (rows may not exist as Axis)
    uint32_t startRowPos{0};
    uint32_t endRowPos{0};
    RangeType type{RangeType::CELL_RANGE};
};

// Result of evaluating a formula or sub-expression
struct EvalResult {
    enum class Type : std::uint8_t { NUMBER, STRING, BOOLEAN, ERROR, EMPTY, RANGE, ARRAY };
    Type type{Type::EMPTY};
    double numberValue{0.0};
    std::string stringValue;
    bool boolValue{false};
    CellError error{CellError::NONE};
    RangeBounds rangeBounds;      // For RANGE type
    Sheet* targetSheet{nullptr};  // For cross-sheet range references

    // For ARRAY type: row-major 2D array of results
    // arrayValue[row][col] - outer vector is rows, inner is columns
    std::vector<std::vector<EvalResult>> arrayValue;

    // Default constructor creates an empty result
    EvalResult() = default;

    // Factory methods
    static EvalResult Number(double v) {
        EvalResult r;
        r.type = Type::NUMBER;
        r.numberValue = v;
        return r;
    }

    static EvalResult String(std::string v) {
        EvalResult r;
        r.type = Type::STRING;
        r.stringValue = std::move(v);
        return r;
    }

    static EvalResult Boolean(bool v) {
        EvalResult r;
        r.type = Type::BOOLEAN;
        r.boolValue = v;
        return r;
    }

    static EvalResult Error(CellError e) {
        EvalResult r;
        r.type = Type::ERROR;
        r.error = e;
        return r;
    }

    static EvalResult Empty() {
        EvalResult r;
        r.type = Type::EMPTY;
        return r;
    }

    static EvalResult Range(RangeBounds bounds) {
        EvalResult r;
        r.type = Type::RANGE;
        r.rangeBounds = bounds;
        return r;
    }

    static EvalResult CellRange(const ID& startCol, const ID& endCol, uint32_t startRowPos,
                                uint32_t endRowPos) {
        RangeBounds bounds;
        bounds.startColId = startCol;
        bounds.endColId = endCol;
        bounds.startRowPos = startRowPos;
        bounds.endRowPos = endRowPos;
        bounds.type = RangeType::CELL_RANGE;
        return Range(bounds);
    }

    static EvalResult ColumnRange(const ID& startCol, const ID& endCol) {
        RangeBounds bounds;
        bounds.startColId = startCol;
        bounds.endColId = endCol;
        bounds.type = RangeType::COLUMN_RANGE;
        return Range(bounds);
    }

    static EvalResult SingleColumn(const ID& colId) {
        RangeBounds bounds;
        bounds.startColId = colId;
        bounds.endColId = colId;
        bounds.type = RangeType::COLUMN;
        return Range(bounds);
    }

    static EvalResult RowRange(const ID& startRow, const ID& endRow) {
        RangeBounds bounds;
        bounds.startRowId = startRow;
        bounds.endRowId = endRow;
        bounds.type = RangeType::ROW_RANGE;
        return Range(bounds);
    }

    static EvalResult SingleRow(const ID& rowId) {
        RangeBounds bounds;
        bounds.startRowId = rowId;
        bounds.endRowId = rowId;
        bounds.type = RangeType::ROW;
        return Range(bounds);
    }

    // Create an array result from a 2D vector of results
    // The vector is row-major: arrayValue[row][col]
    static EvalResult Array(std::vector<std::vector<EvalResult>> arr) {
        EvalResult r;
        r.type = Type::ARRAY;
        r.arrayValue = std::move(arr);
        return r;
    }

    // Create an empty array (0x0)
    static EvalResult EmptyArray() {
        EvalResult r;
        r.type = Type::ARRAY;
        return r;
    }

    // Create a single-column array from a 1D vector
    static EvalResult ColumnArray(std::vector<EvalResult> col) {
        std::vector<std::vector<EvalResult>> arr;
        arr.reserve(col.size());
        for (auto& v : col) {
            arr.push_back({std::move(v)});
        }
        return Array(std::move(arr));
    }

    // Create a single-row array from a 1D vector
    static EvalResult RowArray(std::vector<EvalResult> row) {
        std::vector<std::vector<EvalResult>> arr;
        arr.push_back(std::move(row));
        return Array(std::move(arr));
    }

    // Type coercion methods
    // Converts to number:
    // - Number: returns as-is
    // - String: parses as number, returns VALUE error if invalid
    // - Boolean: true=1, false=0
    // - Error: propagates error
    // - Empty: returns 0
    [[nodiscard]] EvalResult toNumber() const {
        switch (type) {
            case Type::NUMBER:
                return *this;
            case Type::STRING: {
                if (stringValue.empty()) {
                    return Number(0.0);
                }
                // Try to parse as number (no exceptions for WASM compatibility)
                // NOLINTNEXTLINE(misc-const-correctness) - strtod requires non-const
                char* endptr = nullptr;
                const double val = std::strtod(stringValue.c_str(), &endptr);
                // Check if entire string was consumed (skip trailing whitespace)
                if (endptr != stringValue.c_str()) {
                    while (*endptr != '\0' &&
                           std::isspace(static_cast<unsigned char>(*endptr)) != 0) {
                        ++endptr;
                    }
                    if (*endptr == '\0') {
                        return Number(val);
                    }
                }
                return Error(CellError::VALUE);
            }
            case Type::BOOLEAN:
                return Number(boolValue ? 1.0 : 0.0);
            case Type::ERROR:
                return *this;
            case Type::EMPTY:
                return Number(0.0);
            case Type::RANGE:
            case Type::ARRAY:
                // Ranges/arrays can't be converted to a single number
                return Error(CellError::VALUE);
        }
        return Error(CellError::VALUE);
    }

    // Converts to string:
    // - String: returns as-is
    // - Number: formats as string
    // - Boolean: "TRUE" or "FALSE"
    // - Error: error string
    // - Empty: empty string
    [[nodiscard]] EvalResult toString() const {
        switch (type) {
            case Type::STRING:
                return *this;
            case Type::NUMBER: {
                // Format number, avoiding unnecessary decimal places
                if (std::floor(numberValue) == numberValue && std::abs(numberValue) < 1e15) {
                    return String(std::to_string(static_cast<long long>(numberValue)));
                }
                std::string s = std::to_string(numberValue);
                // Remove trailing zeros after decimal point
                const size_t dot = s.find('.');
                if (dot != std::string::npos) {
                    const size_t last = s.find_last_not_of('0');
                    if (last != std::string::npos && last > dot) {
                        s = s.substr(0, last + 1);
                    } else if (last == dot) {
                        s = s.substr(0, dot);
                    }
                }
                return String(s);
            }
            case Type::BOOLEAN:
                return String(boolValue ? "TRUE" : "FALSE");
            case Type::ERROR:
                return String(errorToString(error));
            case Type::EMPTY:
                return String("");
            case Type::RANGE:
            case Type::ARRAY:
                // Ranges/arrays can't be converted to a single string
                return Error(CellError::VALUE);
        }
        return String("");
    }

    // Converts to boolean:
    // - Boolean: returns as-is
    // - Number: 0=false, non-zero=true
    // - String: not directly convertible, returns VALUE error
    // - Error: propagates error
    // - Empty: returns false
    [[nodiscard]] EvalResult toBoolean() const {
        switch (type) {
            case Type::BOOLEAN:
                return *this;
            case Type::NUMBER:
                return Boolean(numberValue != 0.0);
            case Type::STRING:
                // Strings don't implicitly convert to boolean
                return Error(CellError::VALUE);
            case Type::ERROR:
                return *this;
            case Type::EMPTY:
                return Boolean(false);
            case Type::RANGE:
            case Type::ARRAY:
                // Ranges/arrays can't be converted to a single boolean
                return Error(CellError::VALUE);
        }
        return Error(CellError::VALUE);
    }

    // Type checking
    [[nodiscard]] bool isError() const { return type == Type::ERROR; }
    [[nodiscard]] bool isNumber() const { return type == Type::NUMBER; }
    [[nodiscard]] bool isString() const { return type == Type::STRING; }
    [[nodiscard]] bool isBoolean() const { return type == Type::BOOLEAN; }
    [[nodiscard]] bool isEmpty() const { return type == Type::EMPTY; }
    [[nodiscard]] bool isRange() const { return type == Type::RANGE; }
    [[nodiscard]] bool isArray() const { return type == Type::ARRAY; }

    // Get the number value (assumes type is NUMBER)
    [[nodiscard]] double getNumber() const { return numberValue; }

    // Get the string value (assumes type is STRING)
    [[nodiscard]] const std::string& getString() const { return stringValue; }

    // Get the boolean value (assumes type is BOOLEAN)
    [[nodiscard]] bool getBoolean() const { return boolValue; }

    // Get the error (assumes type is ERROR)
    [[nodiscard]] CellError getError() const { return error; }

    // Get the range bounds (assumes type is RANGE)
    [[nodiscard]] const RangeBounds& getRangeBounds() const { return rangeBounds; }

    // Get the array value (assumes type is ARRAY)
    [[nodiscard]] const std::vector<std::vector<EvalResult>>& getArray() const {
        return arrayValue;
    }

    // Get array dimensions (0 if not an array or empty)
    [[nodiscard]] size_t getArrayRows() const { return isArray() ? arrayValue.size() : 0; }
    [[nodiscard]] size_t getArrayCols() const {
        return (isArray() && !arrayValue.empty()) ? arrayValue[0].size() : 0;
    }

    // Get value at array position (returns Empty if out of bounds)
    [[nodiscard]] const EvalResult& getArrayAt(size_t row, size_t col) const {
        static const EvalResult empty = Empty();
        if (!isArray() || row >= arrayValue.size() || col >= arrayValue[row].size()) {
            return empty;
        }
        return arrayValue[row][col];
    }
};

// Power function — delegates to std::pow for all cases.
// Edge cases (0^0, 0^-n, base=1, extreme exponents, overflow) are handled
// by the callers (BinaryOp::POWER and fn_POWER), not here.
inline double excelPow(double base, double exponent) {
    return std::pow(base, exponent);
}

// Excel numeric normalization.
// Excel does not support IEEE754 subnormal (denormalized) numbers or negative zero.
// Subnormals (values smaller than 2^-1022 ≈ 2.225e-308) are flushed to +0, and
// -0 is normalized to +0. See Microsoft's documentation:
// https://learn.microsoft.com/en-us/troubleshoot/microsoft-365-apps/excel/floating-point-arithmetic-inaccurate-result
// Do NOT remove — these normalizations are required for Excel compatibility.
inline double excelNormalize(double value) {
    if (std::fpclassify(value) == FP_SUBNORMAL) {
        return 0.0;
    }
    if (value == 0.0 && std::signbit(value)) {
        return 0.0;
    }
    return value;
}

// Forward declaration
class NamedRangeRegistry;

// Context for evaluation (sheet access, cell positions, etc.)
struct EvalContext {
    Sheet* sheet{nullptr};
    Workbook* workbook{nullptr};
    NamedRangeRegistry* namedRanges{nullptr};  // For resolving named range references
    ID currentCellId;                          // For relative reference resolution
    int recursionDepth{0};
    static const int MAX_RECURSION = 1000;

    // Circular reference detection during evaluation
    std::unordered_set<ID>* evaluatingCells{nullptr};
};

// Main evaluation function (implemented in formula_eval.cc)
EvalResult evaluate(const ASTNode* node, EvalContext& ctx);

// =============================================================================
// Range iteration utilities
// =============================================================================

// Callback signature for iterating over range cells
// Parameters: cell (may be nullptr for empty cells), column position, row position
// Returns: true to continue iteration, false to stop
using RangeCellCallback = std::function<bool(Cell* cell, uint32_t col, uint32_t row)>;

// Iterate over all cells in a range, calling the callback for each cell
// For whole column/row ranges, only iterates over populated cells
// Returns: number of cells visited
size_t iterateRange(const RangeBounds& bounds, Sheet* sheet, const RangeCellCallback& callback);

// Collect all EvalResults from cells in a range
// Empty cells are included as EvalResult::Empty()
// For whole column/row ranges, only includes populated cells
std::vector<EvalResult> collectRangeValues(const RangeBounds& bounds, EvalContext& ctx);

// Overload that accepts EvalResult directly and uses targetSheet if set
// This is the preferred way to call for cross-sheet range references
std::vector<EvalResult> collectRangeValues(const EvalResult& rangeResult, EvalContext& ctx);

// Get the count of cells in a range (for pre-allocation)
// For bounded ranges (A1:C3), returns exact count
// For whole column/row ranges, returns count of populated cells
size_t getRangeSize(const RangeBounds& bounds, Sheet* sheet);

}  // namespace cells

#endif  // CELLS_FORMULA_EVAL_H_
