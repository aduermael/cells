#ifndef CELLS_FORMULA_DISPLAY_H_
#define CELLS_FORMULA_DISPLAY_H_

#include <string>

#include "core/cells/formula_ast.h"

namespace cells {

// Forward declarations
struct Sheet;

// Convert AST back to display string (A1 notation)
// Used for showing formulas in the UI
// This is extracted from FormulaResolver to avoid circular dependencies
class FormulaDisplayConverter {
public:
    // Construct converter for a specific sheet context
    explicit FormulaDisplayConverter(const Sheet& sheet);

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
    [[nodiscard]] std::string binaryOpToString(const BinaryOpNode* node) const;
    [[nodiscard]] std::string unaryOpToString(const UnaryOpNode* node) const;
    [[nodiscard]] std::string functionCallToString(const FunctionCallNode* node) const;
    [[nodiscard]] std::string errorNodeToString(const ErrorNode* node) const;

    // Helper: check if node needs parentheses
    [[nodiscard]] static bool needsParentheses(const ASTNode* parent, const ASTNode* child,
                                               bool isRight);

    const Sheet& _sheet;
};

}  // namespace cells

#endif  // CELLS_FORMULA_DISPLAY_H_
