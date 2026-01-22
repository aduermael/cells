// =============================================================================
// Formula Reference Resolver
// =============================================================================
//
// Resolves A1-notation references in formula ASTs to UUID-based references.
//
// Key responsibilities:
// - Walk AST and resolve A1 refs (B2, $A$1) to cell/axis UUIDs
// - Handle cross-sheet references (Sheet2!A1)
// - Resolve named ranges via NamedRangeRegistry
// - Extract reference info for UI highlighting
// - Detect volatile functions (NOW, RAND, etc.)
// - Identify entities needed for formula resolution (getRequiredEntities)
//
// CRDT-Compliant Resolution:
// 1. Parse formula text -> AST (via FormulaParser)
// 2. Discover required entities (getRequiredEntities)
// 3. Create missing entities via applyOperation()
// 4. Resolve AST refs -> UUIDs (resolve)
// 5. Serialize AST -> UUID formula text (via FormulaSerializer)
//
// Dependencies: formula_ast.h, formula_display.h, model.h, named_ranges.h
// Used by: bindings.cc (cell editing), bindings_file.cc (XLSX import)
//
// =============================================================================

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

// =============================================================================
// Pending Entity Structs for CRDT-Compatible Resolution
// =============================================================================
//
// These structs describe entities that need to be created via CRDT operations
// before formula resolution can complete. The FormulaResolver identifies what
// needs to be created, but the caller is responsible for creating them via
// applyOperation() to maintain CRDT architecture.
//

// Pending axis (column or row) to be created
struct PendingAxis {
    ID id;                 // Pre-generated ID for the axis
    ID sheetId;            // Target sheet
    uint32_t position{0};  // Position (0-indexed)
    bool isColumn{true};   // true = column, false = row
};

// Pending cell to be created
struct PendingCell {
    ID id;       // Pre-generated ID for the cell
    ID sheetId;  // Target sheet
    ID colId;    // Column axis ID (may be a pending axis ID)
    ID rowId;    // Row axis ID (may be a pending axis ID)
};

// Entities required by a formula that don't yet exist
// Callers should create these via CRDT operations before resolving
struct RequiredEntities {
    std::vector<PendingAxis> columns;  // Columns to create
    std::vector<PendingAxis> rows;     // Rows to create
    std::vector<PendingCell> cells;    // Cells to create

    // Check if any entities need to be created
    [[nodiscard]] bool empty() const { return columns.empty() && rows.empty() && cells.empty(); }

    // Clear all pending entities
    void clear() {
        columns.clear();
        rows.clear();
        cells.clear();
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
    //
    // Returns error if any referenced entity doesn't exist.
    // Use with getRequiredEntities() for CRDT-compatible resolution:
    // caller should create entities via applyOperation() before calling resolve()
    ResolveResult resolve(ASTNode* ast);

    // Extract all references from a resolved AST
    // Used for UI highlighting and dependency tracking
    [[nodiscard]] std::vector<ReferenceInfo> extractReferences(const ASTNode* ast) const;

    // Check if AST contains any volatile functions
    [[nodiscard]] static bool containsVolatileFunction(const ASTNode* ast);

    // ==========================================================================
    // CRDT-Compatible Resolution (Two-Phase Approach)
    // ==========================================================================
    //
    // These methods support the CRDT architecture by separating entity discovery
    // from entity creation. Instead of auto-creating entities during resolution,
    // callers should:
    // 1. Call getRequiredEntities() to discover what needs to be created
    // 2. Create entities via CRDT operations (applyOperation)
    // 3. Call resolve() to complete resolution with existing entities
    //

    // Analyze AST and identify entities that need to be created
    // Returns RequiredEntities describing columns, rows, and cells that don't exist
    // The caller should create these via CRDT operations before calling resolve()
    [[nodiscard]] RequiredEntities getRequiredEntities(const ASTNode* ast) const;

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

    // Internal helpers for getRequiredEntities()
    void collectRequiredEntitiesFromNode(const ASTNode* node, RequiredEntities& required) const;
    void collectRequiredEntitiesFromCellRef(const CellRefNode* node,
                                            RequiredEntities& required) const;
    void collectRequiredEntitiesFromColumnRef(const ColumnRefNode* node,
                                              RequiredEntities& required) const;
    void collectRequiredEntitiesFromRowRef(const RowRefNode* node,
                                           RequiredEntities& required) const;
    void collectRequiredEntitiesFromColumnRangeRef(const ColumnRangeRefNode* node,
                                                   RequiredEntities& required) const;
    void collectRequiredEntitiesFromRowRangeRef(const RowRangeRefNode* node,
                                                RequiredEntities& required) const;

    Workbook& _workbook;
    Sheet& _sheet;
    NamedRangeRegistry* _namedRanges;
};

// Note: FormulaDisplayConverter is now in formula_display.h

}  // namespace cells

#endif  // CELLS_FORMULA_RESOLVER_H_
