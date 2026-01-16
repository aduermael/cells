// =============================================================================
// Formula Display Converter
// =============================================================================
//
// Converts formula ASTs back to human-readable A1 notation for UI display.
// This is the inverse of resolution: UUID-based AST -> A1 formula string.
//
// Key responsibilities:
// - Convert cell UUIDs back to column letters (A, B, ..., AA, AB, ...)
// - Convert row UUIDs back to row numbers (1, 2, 3, ...)
// - Preserve absolute reference markers ($A$1 vs A1)
// - Handle operator precedence for correct parenthesization
//
// This module is separate from FormulaResolver to avoid circular dependencies.
// The resolver needs model.h; display only needs AST and Sheet for lookups.
//
// Dependencies: formula_ast.h
// Used by: bindings.cc (formula bar display), formula_resolver.h
//
// =============================================================================

#ifndef CELLS_FORMULA_DISPLAY_H_
#define CELLS_FORMULA_DISPLAY_H_

#include <string>

#include "core/cells/formula_ast.h"

namespace cells {

// Forward declarations
struct Sheet;
struct Workbook;

// Convert AST back to display string (A1 notation)
// Used for showing formulas in the UI
// This is extracted from FormulaResolver to avoid circular dependencies
class FormulaDisplayConverter {
public:
    // Construct converter for a specific sheet context
    // Optionally accepts a workbook for looking up sheet names from sheet IDs
    explicit FormulaDisplayConverter(const Sheet& sheet, const Workbook* workbook = nullptr);

    // Convert AST to display string
    // If the AST contains resolved UUIDs, converts them back to A1 notation
    [[nodiscard]] std::string toDisplayString(const ASTNode* ast) const;

private:
    // Convert individual nodes
    [[nodiscard]] std::string nodeToString(const ASTNode* node) const;
    [[nodiscard]] std::string cellRefToString(const CellRefNode* node) const;
    [[nodiscard]] std::string rangeRefToString(const RangeRefNode* node) const;
    [[nodiscard]] std::string columnRefToString(const ColumnRefNode* node) const;
    [[nodiscard]] std::string rowRefToString(const RowRefNode* node) const;
    [[nodiscard]] std::string columnRangeRefToString(const ColumnRangeRefNode* node) const;
    [[nodiscard]] std::string rowRangeRefToString(const RowRangeRefNode* node) const;
    [[nodiscard]] std::string namedRefToString(const NamedRefNode* node) const;
    [[nodiscard]] std::string spillRangeRefToString(const SpillRangeRefNode* node) const;
    [[nodiscard]] std::string binaryOpToString(const BinaryOpNode* node) const;
    [[nodiscard]] std::string unaryOpToString(const UnaryOpNode* node) const;
    [[nodiscard]] std::string functionCallToString(const FunctionCallNode* node) const;
    [[nodiscard]] std::string errorNodeToString(const ErrorNode* node) const;

    // Helper: get sheet name prefix from sheetId or sheetName
    [[nodiscard]] std::string getSheetPrefix(const std::string& sheetId,
                                             const std::string& sheetName) const;

    // Helper: check if node needs parentheses
    [[nodiscard]] static bool needsParentheses(const ASTNode* parent, const ASTNode* child,
                                               bool isRight);

    const Sheet& _sheet;
    const Workbook* _workbook;
};

}  // namespace cells

#endif  // CELLS_FORMULA_DISPLAY_H_
