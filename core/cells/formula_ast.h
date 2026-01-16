// =============================================================================
// Formula Abstract Syntax Tree (AST)
// =============================================================================
//
// Defines AST node types for representing parsed Excel-style formulas.
// The AST is the canonical representation of formulas; text is derived.
//
// Key responsibilities:
// - Define node types: literals, cell/range refs, operators, functions
// - Support both A1 notation (for UI) and UUID-based references (for storage)
// - Provide cloning, JSON serialization, and error detection
//
// Node types:
// - Literals: NUMBER, STRING, BOOLEAN
// - References: CELL_REF, RANGE_REF, COLUMN_REF, ROW_REF, NAMED_REF
// - Operators: BINARY_OP (+, -, *, /, etc.), UNARY_OP (-, +)
// - FUNCTION_CALL: SUM, IF, VLOOKUP, etc.
// - ERROR_NODE: Parse errors with partial recovery
//
// Dependencies: formula_lexer.h (for SourcePosition)
// Used by: formula_parser.h, formula_eval.h, formula_resolver.h, formula_serializer.h
//
// =============================================================================

#ifndef CELLS_FORMULA_AST_H_
#define CELLS_FORMULA_AST_H_

#include <cstdint>

#include <memory>
#include <string>
#include <vector>

#include "core/cells/formula_lexer.h"

namespace cells {

// Forward declaration
struct ASTNode;

// AST node types
enum class ASTNodeType : std::uint8_t {
    // Literals
    NUMBER_LITERAL,
    STRING_LITERAL,
    BOOLEAN_LITERAL,

    // References
    CELL_REF,          // Single cell reference (A1, $B$2)
    RANGE_REF,         // Range reference (A1:C3)
    COLUMN_REF,        // Whole column (A:A)
    ROW_REF,           // Whole row (1:1)
    COLUMN_RANGE_REF,  // Column range (A:C)
    ROW_RANGE_REF,     // Row range (1:5)
    NAMED_REF,         // Named range
    SPILL_RANGE_REF,   // Spill range reference (A1#)

    // Operators
    BINARY_OP,  // +, -, *, /, ^, &, =, <>, <, <=, >, >=
    UNARY_OP,   // Unary + and -

    // Function call
    FUNCTION_CALL,

    // Error node for error recovery
    ERROR_NODE,
};

// Binary operators
enum class BinaryOp : std::uint8_t {
    ADD,            // +
    SUBTRACT,       // -
    MULTIPLY,       // *
    DIVIDE,         // /
    POWER,          // ^
    CONCAT,         // &
    EQUAL,          // =
    NOT_EQUAL,      // <>
    LESS,           // <
    LESS_EQUAL,     // <=
    GREATER,        // >
    GREATER_EQUAL,  // >=
};

// Unary operators
enum class UnaryOp : std::uint8_t {
    NEGATE,    // -
    POSITIVE,  // +
};

// Named range scope (for AST nodes)
// Note: This duplicates the enum in named_ranges.h intentionally
// to avoid circular dependencies (AST is lower-level than named_ranges)
enum class ASTNamedRangeScope : std::uint8_t {
    WORKBOOK,  // Global name
    SHEET,     // Sheet-local name
};

// Base AST node
struct ASTNode {
    ASTNodeType type;
    SourcePosition position;

    explicit ASTNode(ASTNodeType t) : type(t) {}
    ASTNode(ASTNodeType t, SourcePosition pos) : type(t), position(pos) {}
    virtual ~ASTNode() = default;

    // Clone the node (deep copy)
    [[nodiscard]] virtual std::unique_ptr<ASTNode> clone() const = 0;

    // Convert to JSON for debugging
    [[nodiscard]] virtual std::string toJson() const = 0;

    // Check if this node or any child contains an error
    [[nodiscard]] virtual bool hasError() const { return type == ASTNodeType::ERROR_NODE; }
};

// Number literal: 42, 3.14, 1.5e10
struct NumberLiteralNode : public ASTNode {
    double value;

    explicit NumberLiteralNode(double v) : ASTNode(ASTNodeType::NUMBER_LITERAL), value(v) {}

    NumberLiteralNode(double v, SourcePosition pos)
        : ASTNode(ASTNodeType::NUMBER_LITERAL, pos), value(v) {}

    [[nodiscard]] std::unique_ptr<ASTNode> clone() const override {
        auto n = std::make_unique<NumberLiteralNode>(value);
        n->position = position;
        return n;
    }

    [[nodiscard]] std::string toJson() const override;
};

// String literal: "Hello"
struct StringLiteralNode : public ASTNode {
    std::string value;

    explicit StringLiteralNode(std::string v)
        : ASTNode(ASTNodeType::STRING_LITERAL), value(std::move(v)) {}

    StringLiteralNode(std::string v, SourcePosition pos)
        : ASTNode(ASTNodeType::STRING_LITERAL, pos), value(std::move(v)) {}

    [[nodiscard]] std::unique_ptr<ASTNode> clone() const override {
        auto n = std::make_unique<StringLiteralNode>(value);
        n->position = position;
        return n;
    }

    [[nodiscard]] std::string toJson() const override;
};

// Boolean literal: TRUE, FALSE
struct BooleanLiteralNode : public ASTNode {
    bool value;

    explicit BooleanLiteralNode(bool v) : ASTNode(ASTNodeType::BOOLEAN_LITERAL), value(v) {}

    BooleanLiteralNode(bool v, SourcePosition pos)
        : ASTNode(ASTNodeType::BOOLEAN_LITERAL, pos), value(v) {}

    [[nodiscard]] std::unique_ptr<ASTNode> clone() const override {
        auto n = std::make_unique<BooleanLiteralNode>(value);
        n->position = position;
        return n;
    }

    [[nodiscard]] std::string toJson() const override;
};

// Cell reference: A1, $B$2, Sheet1!A1
// Stores the original A1 text; UUID resolution happens later
struct CellRefNode : public ASTNode {
    std::string column;     // Column letters (e.g., "A", "AA")
    int row;                // Row number (1-based)
    bool colAbsolute;       // $A1 or $A$1
    bool rowAbsolute;       // A$1 or $A$1
    std::string sheetName;  // Empty if same sheet (for A1 display)
    std::string sheetId;    // Sheet UUID for cross-sheet refs (for storage)

    // After resolution, stores the cell UUID
    std::string cellId;

    CellRefNode(std::string col, int r, bool colAbs, bool rowAbs)
        : ASTNode(ASTNodeType::CELL_REF),
          column(std::move(col)),
          row(r),
          colAbsolute(colAbs),
          rowAbsolute(rowAbs) {}

    CellRefNode(std::string col, int r, bool colAbs, bool rowAbs, SourcePosition pos)
        : ASTNode(ASTNodeType::CELL_REF, pos),
          column(std::move(col)),
          row(r),
          colAbsolute(colAbs),
          rowAbsolute(rowAbs) {}

    [[nodiscard]] std::unique_ptr<ASTNode> clone() const override {
        auto n = std::make_unique<CellRefNode>(column, row, colAbsolute, rowAbsolute);
        n->position = position;
        n->sheetName = sheetName;
        n->sheetId = sheetId;
        n->cellId = cellId;
        return n;
    }

    [[nodiscard]] std::string toJson() const override;
};

// Range reference: A1:C3, $A$1:$C$3
struct RangeRefNode : public ASTNode {
    std::unique_ptr<CellRefNode> topLeft;
    std::unique_ptr<CellRefNode> bottomRight;

    RangeRefNode(std::unique_ptr<CellRefNode> tl, std::unique_ptr<CellRefNode> br)
        : ASTNode(ASTNodeType::RANGE_REF), topLeft(std::move(tl)), bottomRight(std::move(br)) {}

    RangeRefNode(std::unique_ptr<CellRefNode> tl, std::unique_ptr<CellRefNode> br,
                 SourcePosition pos)
        : ASTNode(ASTNodeType::RANGE_REF, pos),
          topLeft(std::move(tl)),
          bottomRight(std::move(br)) {}

    [[nodiscard]] std::unique_ptr<ASTNode> clone() const override {
        auto tlClone =
            std::unique_ptr<CellRefNode>(static_cast<CellRefNode*>(topLeft->clone().release()));
        auto brClone =
            std::unique_ptr<CellRefNode>(static_cast<CellRefNode*>(bottomRight->clone().release()));
        auto n = std::make_unique<RangeRefNode>(std::move(tlClone), std::move(brClone));
        n->position = position;
        return n;
    }

    [[nodiscard]] std::string toJson() const override;

    [[nodiscard]] bool hasError() const override {
        return topLeft->hasError() || bottomRight->hasError();
    }
};

// Whole column reference: A:A
struct ColumnRefNode : public ASTNode {
    std::string column;
    bool absolute;
    std::string sheetName;  // For A1 display
    std::string sheetId;    // Sheet UUID for storage

    // After resolution
    std::string columnId;

    ColumnRefNode(std::string col, bool abs)
        : ASTNode(ASTNodeType::COLUMN_REF), column(std::move(col)), absolute(abs) {}

    ColumnRefNode(std::string col, bool abs, SourcePosition pos)
        : ASTNode(ASTNodeType::COLUMN_REF, pos), column(std::move(col)), absolute(abs) {}

    [[nodiscard]] std::unique_ptr<ASTNode> clone() const override {
        auto n = std::make_unique<ColumnRefNode>(column, absolute);
        n->position = position;
        n->sheetName = sheetName;
        n->sheetId = sheetId;
        n->columnId = columnId;
        return n;
    }

    [[nodiscard]] std::string toJson() const override;
};

// Whole row reference: 1:1
struct RowRefNode : public ASTNode {
    int row;
    bool absolute;
    std::string sheetName;  // For A1 display
    std::string sheetId;    // Sheet UUID for storage

    // After resolution
    std::string rowId;

    RowRefNode(int r, bool abs) : ASTNode(ASTNodeType::ROW_REF), row(r), absolute(abs) {}

    RowRefNode(int r, bool abs, SourcePosition pos)
        : ASTNode(ASTNodeType::ROW_REF, pos), row(r), absolute(abs) {}

    [[nodiscard]] std::unique_ptr<ASTNode> clone() const override {
        auto n = std::make_unique<RowRefNode>(row, absolute);
        n->position = position;
        n->sheetName = sheetName;
        n->sheetId = sheetId;
        n->rowId = rowId;
        return n;
    }

    [[nodiscard]] std::string toJson() const override;
};

// Column range: A:C
struct ColumnRangeRefNode : public ASTNode {
    std::string startColumn;
    std::string endColumn;
    bool startAbsolute;
    bool endAbsolute;
    std::string sheetName;  // For A1 display
    std::string sheetId;    // Sheet UUID for storage

    // After resolution
    std::string startColumnId;
    std::string endColumnId;

    ColumnRangeRefNode(std::string startCol, std::string endCol, bool startAbs, bool endAbs)
        : ASTNode(ASTNodeType::COLUMN_RANGE_REF),
          startColumn(std::move(startCol)),
          endColumn(std::move(endCol)),
          startAbsolute(startAbs),
          endAbsolute(endAbs) {}

    [[nodiscard]] std::unique_ptr<ASTNode> clone() const override {
        auto n = std::make_unique<ColumnRangeRefNode>(startColumn, endColumn, startAbsolute,
                                                      endAbsolute);
        n->position = position;
        n->sheetName = sheetName;
        n->sheetId = sheetId;
        n->startColumnId = startColumnId;
        n->endColumnId = endColumnId;
        return n;
    }

    [[nodiscard]] std::string toJson() const override;
};

// Row range: 1:5
struct RowRangeRefNode : public ASTNode {
    int startRow;
    int endRow;
    bool startAbsolute;
    bool endAbsolute;
    std::string sheetName;  // For A1 display
    std::string sheetId;    // Sheet UUID for storage

    // After resolution
    std::string startRowId;
    std::string endRowId;

    RowRangeRefNode(int startR, int endR, bool startAbs, bool endAbs)
        : ASTNode(ASTNodeType::ROW_RANGE_REF),
          startRow(startR),
          endRow(endR),
          startAbsolute(startAbs),
          endAbsolute(endAbs) {}

    [[nodiscard]] std::unique_ptr<ASTNode> clone() const override {
        auto n = std::make_unique<RowRangeRefNode>(startRow, endRow, startAbsolute, endAbsolute);
        n->position = position;
        n->sheetName = sheetName;
        n->sheetId = sheetId;
        n->startRowId = startRowId;
        n->endRowId = endRowId;
        return n;
    }

    [[nodiscard]] std::string toJson() const override;
};

// Named range reference: myRange, Sales_Total
struct NamedRefNode : public ASTNode {
    std::string name;
    ASTNamedRangeScope scope;

    explicit NamedRefNode(std::string n, ASTNamedRangeScope s = ASTNamedRangeScope::WORKBOOK)
        : ASTNode(ASTNodeType::NAMED_REF), name(std::move(n)), scope(s) {}

    NamedRefNode(std::string n, ASTNamedRangeScope s, SourcePosition pos)
        : ASTNode(ASTNodeType::NAMED_REF, pos), name(std::move(n)), scope(s) {}

    [[nodiscard]] std::unique_ptr<ASTNode> clone() const override {
        auto node = std::make_unique<NamedRefNode>(name, scope);
        node->position = position;
        return node;
    }

    [[nodiscard]] std::string toJson() const override;
};

// Spill range reference: A1# (refers to the spill range starting at A1)
// In Excel, if A1 contains a formula that spills into A1:C3, then A1# refers to A1:C3
struct SpillRangeRefNode : public ASTNode {
    std::unique_ptr<CellRefNode> anchor;  // The anchor cell (e.g., A1 in A1#)

    explicit SpillRangeRefNode(std::unique_ptr<CellRefNode> anchorCell)
        : ASTNode(ASTNodeType::SPILL_RANGE_REF), anchor(std::move(anchorCell)) {}

    SpillRangeRefNode(std::unique_ptr<CellRefNode> anchorCell, SourcePosition pos)
        : ASTNode(ASTNodeType::SPILL_RANGE_REF, pos), anchor(std::move(anchorCell)) {}

    [[nodiscard]] std::unique_ptr<ASTNode> clone() const override {
        auto anchorClone =
            std::unique_ptr<CellRefNode>(static_cast<CellRefNode*>(anchor->clone().release()));
        auto n = std::make_unique<SpillRangeRefNode>(std::move(anchorClone));
        n->position = position;
        return n;
    }

    [[nodiscard]] std::string toJson() const override;

    [[nodiscard]] bool hasError() const override { return anchor->hasError(); }
};

// Binary operation: A1 + B2
struct BinaryOpNode : public ASTNode {
    BinaryOp op;
    std::unique_ptr<ASTNode> left;
    std::unique_ptr<ASTNode> right;

    BinaryOpNode(BinaryOp o, std::unique_ptr<ASTNode> l, std::unique_ptr<ASTNode> r)
        : ASTNode(ASTNodeType::BINARY_OP), op(o), left(std::move(l)), right(std::move(r)) {}

    BinaryOpNode(BinaryOp o, std::unique_ptr<ASTNode> l, std::unique_ptr<ASTNode> r,
                 SourcePosition pos)
        : ASTNode(ASTNodeType::BINARY_OP, pos), op(o), left(std::move(l)), right(std::move(r)) {}

    [[nodiscard]] std::unique_ptr<ASTNode> clone() const override {
        auto n = std::make_unique<BinaryOpNode>(op, left->clone(), right->clone());
        n->position = position;
        return n;
    }

    [[nodiscard]] std::string toJson() const override;

    [[nodiscard]] bool hasError() const override { return left->hasError() || right->hasError(); }

    // Get operator as string for display
    [[nodiscard]] static const char* opToString(BinaryOp op);
};

// Unary operation: -A1
struct UnaryOpNode : public ASTNode {
    UnaryOp op;
    std::unique_ptr<ASTNode> operand;

    UnaryOpNode(UnaryOp o, std::unique_ptr<ASTNode> operand)
        : ASTNode(ASTNodeType::UNARY_OP), op(o), operand(std::move(operand)) {}

    UnaryOpNode(UnaryOp o, std::unique_ptr<ASTNode> operand, SourcePosition pos)
        : ASTNode(ASTNodeType::UNARY_OP, pos), op(o), operand(std::move(operand)) {}

    [[nodiscard]] std::unique_ptr<ASTNode> clone() const override {
        auto n = std::make_unique<UnaryOpNode>(op, operand->clone());
        n->position = position;
        return n;
    }

    [[nodiscard]] std::string toJson() const override;

    [[nodiscard]] bool hasError() const override { return operand->hasError(); }

    // Get operator as string for display
    [[nodiscard]] static const char* opToString(UnaryOp op);
};

// Function call: SUM(A1:A10), IF(A1>0, B1, C1)
struct FunctionCallNode : public ASTNode {
    std::string name;
    std::vector<std::unique_ptr<ASTNode>> args;
    bool isVolatile;  // NOW(), RAND(), TODAY(), etc.

    explicit FunctionCallNode(std::string n)
        : ASTNode(ASTNodeType::FUNCTION_CALL), name(std::move(n)), isVolatile(false) {}

    FunctionCallNode(std::string n, SourcePosition pos)
        : ASTNode(ASTNodeType::FUNCTION_CALL, pos), name(std::move(n)), isVolatile(false) {}

    [[nodiscard]] std::unique_ptr<ASTNode> clone() const override {
        auto n = std::make_unique<FunctionCallNode>(name);
        n->position = position;
        n->isVolatile = isVolatile;
        for (const auto& arg : args) {
            n->args.push_back(arg->clone());
        }
        return n;
    }

    [[nodiscard]] std::string toJson() const override;

    [[nodiscard]] bool hasError() const override {
        for (const auto& arg : args) {
            if (arg->hasError()) {
                return true;
            }
        }
        return false;
    }

    // Check if function name is volatile
    [[nodiscard]] static bool isVolatileFunction(const std::string& name);
};

// Error node for error recovery
// Contains partial children that were parsed before the error
struct ErrorNode : public ASTNode {
    std::string message;
    std::string rawText;  // Original unparseable text (for editing)
    std::vector<std::unique_ptr<ASTNode>> partialChildren;

    explicit ErrorNode(std::string msg)
        : ASTNode(ASTNodeType::ERROR_NODE), message(std::move(msg)) {}

    ErrorNode(std::string msg, SourcePosition pos)
        : ASTNode(ASTNodeType::ERROR_NODE, pos), message(std::move(msg)) {}

    [[nodiscard]] std::unique_ptr<ASTNode> clone() const override {
        auto n = std::make_unique<ErrorNode>(message);
        n->position = position;
        n->rawText = rawText;
        for (const auto& child : partialChildren) {
            n->partialChildren.push_back(child->clone());
        }
        return n;
    }

    [[nodiscard]] std::string toJson() const override;

    [[nodiscard]] bool hasError() const override { return true; }
};

}  // namespace cells

#endif  // CELLS_FORMULA_AST_H_
