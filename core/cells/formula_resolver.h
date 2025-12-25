#ifndef CELLS_FORMULA_RESOLVER_H_
#define CELLS_FORMULA_RESOLVER_H_

#include <memory>
#include <string>
#include <vector>

#include "core/cells/formula_ast.h"
#include "core/cells/formula_display.h"
#include "core/cells/model.h"
#include "core/cells/named_ranges.h"
#include "core/cells/types.h"

namespace cells {

// Forward declarations
struct Workbook;
struct Sheet;

// Result of reference resolution
struct ResolveResult {
    bool success;
    std::string errorMessage;
    SourcePosition errorPosition;

    static ResolveResult ok() { return {true, "", {}}; }
    static ResolveResult error(const std::string& msg, SourcePosition pos = {}) {
        return {false, msg, pos};
    }
};

// Reference information extracted from AST for UI highlighting
struct ReferenceInfo {
    enum class Type : std::uint8_t { CELL, RANGE, COLUMN, ROW, COLUMN_RANGE, ROW_RANGE, NAMED };

    Type type{Type::CELL};
    SourcePosition sourcePosition;  // Position in formula text

    // For cell references
    ID cellId;

    // For range references (two corners)
    ID topLeftCellId;
    ID bottomRightCellId;

    // For column/row references
    ID axisId;

    // For column/row range references
    ID startAxisId;
    ID endAxisId;

    // For named references (resolved)
    std::string namedRangeName;

    // Sheet ID (null if same sheet)
    ID sheetId;
};

// Formula reference resolver
// Walks AST and converts A1 references to UUID-based references
class FormulaResolver {
public:
    // Construct resolver for a specific sheet context
    // The workbook is used for cross-sheet references and named ranges
    FormulaResolver(Workbook& workbook, Sheet& sheet, NamedRangeRegistry* namedRanges = nullptr);

    // Resolve all references in an AST
    // Modifies the AST in place, filling in UUID fields
    // Auto-creates cells/axes as needed for non-existent references
    ResolveResult resolve(ASTNode* ast);

    // Extract all references from a resolved AST
    // Used for UI highlighting and dependency tracking
    [[nodiscard]] std::vector<ReferenceInfo> extractReferences(const ASTNode* ast) const;

    // Check if AST contains any volatile functions
    [[nodiscard]] static bool containsVolatileFunction(const ASTNode* ast);

private:
    // Internal resolution methods for each node type
    ResolveResult resolveNode(ASTNode* node);
    ResolveResult resolveCellRef(CellRefNode* node);
    ResolveResult resolveRangeRef(RangeRefNode* node);
    ResolveResult resolveColumnRef(ColumnRefNode* node);
    ResolveResult resolveRowRef(RowRefNode* node);
    ResolveResult resolveColumnRangeRef(ColumnRangeRefNode* node);
    ResolveResult resolveRowRangeRef(RowRangeRefNode* node);
    ResolveResult resolveNamedRef(NamedRefNode* node);
    ResolveResult resolveBinaryOp(BinaryOpNode* node);
    ResolveResult resolveUnaryOp(UnaryOpNode* node);
    ResolveResult resolveFunctionCall(FunctionCallNode* node);

    // Get sheet for a reference (handles cross-sheet references)
    Sheet* getTargetSheet(const std::string& sheetName);

    // Internal reference extraction
    void extractReferencesFromNode(const ASTNode* node, std::vector<ReferenceInfo>& refs) const;

    Workbook& _workbook;
    Sheet& _sheet;
    NamedRangeRegistry* _namedRanges;
};

// Note: FormulaDisplayConverter is now in formula_display.h
// It's included above for backward compatibility with existing code

}  // namespace cells

#endif  // CELLS_FORMULA_RESOLVER_H_
