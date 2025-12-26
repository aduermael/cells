#ifndef CELLS_DEPENDENCY_GRAPH_H_
#define CELLS_DEPENDENCY_GRAPH_H_

#include <cstdint>

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

// Dependency graph for tracking which cells depend on which
// Uses R-tree for efficient "what depends on this cell/range" queries
class DependencyGraph {
public:
    DependencyGraph() = default;

    // Add dependencies for a formula cell
    // Extracts references from AST and adds them to the graph
    void addFormula(const ID& cellId, const ASTNode* ast);

    // Remove all dependencies for a cell
    // Call this when clearing a formula or before updating it
    void removeFormula(const ID& cellId);

    // Get cells that depend on the given cell (cells whose formulas read this cell)
    [[nodiscard]] std::vector<ID> getDependents(const ID& cellId) const;

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
