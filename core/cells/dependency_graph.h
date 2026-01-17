// =============================================================================
// Dependency Graph
// =============================================================================
//
// Tracks formula dependencies between cells for efficient recalculation.
// Uses an R-tree for spatial queries ("what formulas reference this range?")
// combined with direct/reverse hash maps for point lookups.
//
// Key responsibilities:
// - Track which cells a formula depends on (forward dependencies)
// - Find which formulas depend on a cell (reverse dependencies for recalc)
// - Support range dependencies via R-tree spatial indexing
// - Detect circular references
// - Track volatile cells (NOW, RAND, etc.) for forced recalculation
//
// Data structures:
// - R-tree: Spatial index for range queries (O(log n) lookup)
// - dependencies_: cellId -> what it depends on (for formula display)
// - reverseDeps_: cellId -> what depends on it (for recalculation)
// - volatileCells_: Set of cells needing recalc every time
//
// Dependencies: rtree.h, types.h
// Used by: Sheet (owns DependencyGraph), formula_eval.cc (recalculation order)
//
// =============================================================================

#ifndef CELLS_DEPENDENCY_GRAPH_H_
#define CELLS_DEPENDENCY_GRAPH_H_

#include <cstdint>

#include <functional>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core/cells/rtree.h"
#include "core/cells/types.h"

namespace cells {

// Forward declarations
struct ASTNode;
class NamedRangeRegistry;

// Position resolver callback for converting cell IDs to grid positions
// Takes a cell ID and returns (col, row) position, or (-1, -1) if not found
using PositionResolver = std::function<std::pair<int32_t, int32_t>(const ID&)>;

// Dependency reference info for tracking formula dependencies
// Stores minimal info needed for dependency graph operations
struct DependencyRef {
    ID cellId;               // For cell refs
    ID startCellId;          // For range refs (top-left)
    ID endCellId;            // For range refs (bottom-right)
    ID columnId;             // For column refs
    ID rowId;                // For row refs
    ID startColumnId;        // For column range refs
    ID endColumnId;          // For column range refs
    ID startRowId;           // For row range refs
    ID endRowId;             // For row range refs
    int32_t sourceStart{0};  // Position in formula text (for highlighting)
    int32_t sourceEnd{0};

    enum class Type : uint8_t { CELL, RANGE, COLUMN, ROW, COLUMN_RANGE, ROW_RANGE };
    Type type{Type::CELL};

    DependencyRef() = default;
};

// Cross-sheet reference info for workbook-level dependency tracking
// Stores the referenced cell/range and the sheet it belongs to
struct CrossSheetRef {
    ID sheetId;      // Sheet containing the referenced cell
    ID cellId;       // For cell refs: the cell ID
    ID startCellId;  // For range refs: top-left cell ID
    ID endCellId;    // For range refs: bottom-right cell ID

    enum class Type : uint8_t { CELL, RANGE };
    Type type{Type::CELL};
};

// Extract cross-sheet references from an AST
// Returns references that have a sheetId set (pointing to a different sheet)
std::vector<CrossSheetRef> extractCrossSheetRefs(const ASTNode* ast);

// Dependency graph for tracking which cells depend on which
// Uses R-tree for efficient "what depends on this cell/range" queries
class DependencyGraph {
public:
    DependencyGraph() = default;

    // Add dependencies for a formula cell
    // Extracts references from AST and adds them to the graph
    // Use the overload with PositionResolver for R-tree population
    void addFormula(const ID& cellId, const ASTNode* ast);

    // Add dependencies with position resolution for R-tree-based range queries
    // The resolver converts cell IDs to (col, row) positions for spatial indexing
    void addFormula(const ID& cellId, const ASTNode* ast, const PositionResolver& resolver);

    // Add dependencies with named range resolution
    // The registry resolves named references to their underlying cell/range targets
    // sheetId is used for resolving sheet-scoped named ranges
    void addFormula(const ID& cellId, const ASTNode* ast, const PositionResolver& resolver,
                    const NamedRangeRegistry* namedRegistry, const ID& sheetId);

    // Remove all dependencies for a cell
    // Call this when clearing a formula or before updating it
    void removeFormula(const ID& cellId);

    // Get cells that depend on the given cell (cells whose formulas read this cell)
    // Note: This only returns direct cell dependencies, not range dependencies.
    // Use getDependentsForCell() with position info for complete dependency lookup.
    [[nodiscard]] std::vector<ID> getDependents(const ID& cellId) const;

    // Get cells that depend on a cell at the given position
    // Combines reverse index lookup (O(1)) + R-tree range query (O(log n))
    // This is the preferred method when you have position information
    [[nodiscard]] std::vector<ID> getDependentsForCell(const ID& cellId, int32_t col,
                                                       int32_t row) const;

    // Get cells that depend on any cell in the given range
    [[nodiscard]] std::vector<ID> getDependentsInRange(int32_t minCol, int32_t minRow,
                                                       int32_t maxCol, int32_t maxRow) const;

    // Get cells that this formula reads from (its dependencies)
    [[nodiscard]] std::vector<DependencyRef> getDependencies(const ID& cellId) const;

    // Mark a cell as containing volatile functions (NOW, RAND, etc.)
    void markVolatile(const ID& cellId);

    // Unmark a cell as volatile
    void unmarkVolatile(const ID& cellId);

    // Get all cells marked as volatile
    [[nodiscard]] std::vector<ID> getVolatileCells() const;

    // Check if a cell is volatile
    [[nodiscard]] bool isVolatile(const ID& cellId) const;

    // Detect circular reference starting from a cell
    // Returns the cycle path if found, empty vector if no cycle
    [[nodiscard]] std::vector<ID> detectCycle(const ID& startCellId) const;

    // Get recalculation order for a set of changed cells
    // Returns cells in topological order (dependencies before dependents)
    // If circular reference exists, returns empty vector and sets hasCycle to true
    [[nodiscard]] std::vector<ID> getRecalcOrder(const std::vector<ID>& changedCells,
                                                 bool* hasCycle = nullptr) const;

    // Clear all dependencies
    void clear();

    // Rebuild the R-tree using the provided position resolver
    // Call this after column/row move operations to update spatial indexing
    void rebuildRTree(const PositionResolver& resolver);

    // Get number of formula cells being tracked
    [[nodiscard]] size_t size() const { return dependencies_.size(); }

private:
    // R-tree mapping (col, row) rectangles to cell IDs that depend on them
    // When cell at (col, row) changes, query R-tree to find all dependents
    RTree<ID> rtree_;

    // Direct lookup: cellId -> references in that cell's formula
    // Used for displaying what a formula depends on
    std::unordered_map<ID, std::vector<DependencyRef>> dependencies_;

    // Reverse lookup: cellId -> formulas that depend on this cell (O(1) lookup)
    // Updated in addFormula()/removeFormula() for efficient getDependents() queries
    std::unordered_map<ID, std::vector<ID>> reverseDeps_;

    // Track which rectangles were inserted for each cell (for removal)
    std::unordered_map<ID, std::vector<BoundingRect>> cellRects_;

    // Set of cells containing volatile functions
    std::unordered_set<ID> volatileCells_;

    // Helper to convert column position to column coordinate
    // Position is 0-indexed, stored in Axis
    static int32_t positionToCoord(uint32_t position);

    // DFS helper for cycle detection
    bool dfs(const ID& cellId, std::unordered_set<ID>& visited, std::unordered_set<ID>& onStack,
             std::vector<ID>& path) const;
};

}  // namespace cells

#endif  // CELLS_DEPENDENCY_GRAPH_H_
