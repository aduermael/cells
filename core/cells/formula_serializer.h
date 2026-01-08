// =============================================================================
// Formula Serializer
// =============================================================================
//
// Serializes resolved formula ASTs to UUID-based text format for storage.
// This is the inverse of parsing: AST -> UUID formula string.
//
// Key responsibilities:
// - Convert resolved AST to compact UUID-based formula text
// - Encode absolute/relative reference markers ($$, $~, ~$, ~~)
// - Handle operator precedence for correct parenthesization
//
// UUID reference format:
// - $$cellId: both absolute ($A$1)
// - $~cellId: column absolute, row relative ($A1)
// - ~$cellId: column relative, row absolute (A$1)
// - ~~cellId: both relative (A1)
//
// All cell refs are exactly 10 chars: 2-char prefix + 8-char UUID
// Example: =~~xK7mNp2Q+~~fR3pK7wN for =A1+B1
//
// Dependencies: formula_ast.h
// Used by: crdt.cc (storing formulas), file serialization
//
// =============================================================================

#ifndef CELLS_FORMULA_SERIALIZER_H_
#define CELLS_FORMULA_SERIALIZER_H_

#include <string>

#include "core/cells/formula_ast.h"

namespace cells {

// UUID reference format prefixes:
// - "$$" = both absolute ($A$1)
// - "$~" = column absolute, row relative ($A1)
// - "~$" = column relative, row absolute (A$1)
// - "~~" = both relative (A1)
//
// All refs are exactly 10 chars: 2-char prefix + 8-char cell UUID
// Example: =~~xK7mNp2Q+~~fR3pK7wN for =A1+B1

class FormulaSerializer {
public:
    // Convert a resolved AST to UUID-format formula text
    // The AST must already have cellId fields populated by FormulaResolver
    [[nodiscard]] static std::string serialize(const ASTNode* ast);

    // Generate the UUID reference prefix for absolute flags
    [[nodiscard]] static std::string refPrefix(bool colAbsolute, bool rowAbsolute);

private:
    // Convert individual nodes to UUID format
    [[nodiscard]] static std::string nodeToUuidString(const ASTNode* node);
    [[nodiscard]] static std::string cellRefToUuidString(const CellRefNode* node);
    [[nodiscard]] static std::string rangeRefToUuidString(const RangeRefNode* node);
    [[nodiscard]] static std::string columnRefToUuidString(const ColumnRefNode* node);
    [[nodiscard]] static std::string rowRefToUuidString(const RowRefNode* node);
    [[nodiscard]] static std::string columnRangeRefToUuidString(const ColumnRangeRefNode* node);
    [[nodiscard]] static std::string rowRangeRefToUuidString(const RowRangeRefNode* node);
    [[nodiscard]] static std::string namedRefToUuidString(const NamedRefNode* node);
    [[nodiscard]] static std::string binaryOpToUuidString(const BinaryOpNode* node);
    [[nodiscard]] static std::string unaryOpToUuidString(const UnaryOpNode* node);
    [[nodiscard]] static std::string functionCallToUuidString(const FunctionCallNode* node);
    [[nodiscard]] static std::string errorNodeToUuidString(const ErrorNode* node);

    // Helper: check if node needs parentheses for correct precedence
    [[nodiscard]] static bool needsParentheses(const ASTNode* parent, const ASTNode* child,
                                               bool isRight);
};

}  // namespace cells

#endif  // CELLS_FORMULA_SERIALIZER_H_
