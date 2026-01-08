// =============================================================================
// Fill Range (Auto-Fill)
// =============================================================================
//
// Implements Excel-style fill-down/fill-right functionality.
// Detects patterns in source cells and extrapolates to target range.
//
// Key responsibilities:
// - Detect patterns: constant, linear (arithmetic), formula, string
// - Extrapolate values: 1,2,3 -> 4,5,6 or A1,A2 -> A3,A4
// - Adjust formula references during fill (relative refs shift)
// - Work with CRDT operations in collaboration mode
//
// Pattern types:
// - CONSTANT: Single value or all same values (repeat)
// - LINEAR: Arithmetic sequence (1,2,3 or 5,10,15)
// - FORMULA: Formulas with adjusted cell references
// - STRING: Text values (always repeat)
// - EMPTY: Empty cells (repeat empty)
//
// Fill directions:
// - DOWN: Fill rows below source
// - UP: Fill rows above source
// - RIGHT: Fill columns to right of source
// - LEFT: Fill columns to left of source
//
// Dependencies: formula_ast.h, types.h
// Used by: bindings.cc (fill handle drag)
//
// =============================================================================

#ifndef CELLS_FILL_RANGE_H_
#define CELLS_FILL_RANGE_H_

#include <cstdint>

#include <memory>
#include <string>
#include <vector>

#include "core/cells/formula_ast.h"
#include "core/cells/types.h"

namespace cells {

// Forward declarations
struct Sheet;
struct Cell;
struct Workbook;

// Result of a fill operation
struct FillResult {
    bool success{false};
    std::string error;
    int cellsFilled{0};  // Number of cells that were filled
};

// FillDirection indicates which direction to fill
enum class FillDirection : std::uint8_t {
    DOWN,   // Fill rows below source range
    UP,     // Fill rows above source range
    RIGHT,  // Fill columns to the right of source range
    LEFT    // Fill columns to the left of source range
};

// Detected pattern type for numeric sequences
enum class PatternType : std::uint8_t {
    CONSTANT,  // Single value or all same values (repeat)
    LINEAR,    // Arithmetic sequence (1, 2, 3... or 5, 10, 15...)
    STRING,    // String values (always constant/repeat)
    FORMULA,   // Formula values (adjust references)
    EMPTY      // Empty cells (repeat empty)
};

// Pattern detection result
struct DetectedPattern {
    PatternType type{PatternType::CONSTANT};
    double start{0.0};                                  // First value (for numeric)
    double step{0.0};                                   // Increment (for linear patterns)
    std::vector<std::string> stringValues;              // For string/constant patterns
    std::vector<std::unique_ptr<ASTNode>> formulaASTs;  // For formula patterns (AST nodes)
};

// Detect pattern from source cells
// Takes values from cells at positions (minCol, minRow) to (maxCol, maxRow)
// Direction determines which axis to analyze for patterns
DetectedPattern detectPattern(Sheet* sheet, int minCol, int minRow, int maxCol, int maxRow,
                              FillDirection direction);

// Fill a range based on source values
// sourceMinCol/Row to sourceMaxCol/Row: the original selection with values
// targetMinCol/Row to targetMaxCol/Row: the range to fill (includes source)
// The source values are used to detect a pattern and extrapolate
//
// For collaboration mode, uses CRDT operations
// Returns success/failure status
FillResult fillRange(Workbook* workbook, Sheet* sheet, int sourceMinCol, int sourceMinRow,
                     int sourceMaxCol, int sourceMaxRow, int targetMinCol, int targetMinRow,
                     int targetMaxCol, int targetMaxRow);

// Get the fill direction based on source and target ranges
FillDirection getFillDirection(int sourceMinCol, int sourceMinRow, int sourceMaxCol,
                               int sourceMaxRow, int targetMinCol, int targetMinRow,
                               int targetMaxCol, int targetMaxRow);

// Extrapolate the next value in a sequence
// index is the position relative to the last source value (1, 2, 3...)
double extrapolateValue(const DetectedPattern& pattern, int index);

}  // namespace cells

#endif  // CELLS_FILL_RANGE_H_
