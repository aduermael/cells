#include "core/cells/dependency_graph.h"

#include <algorithm>
#include <stack>

#include "core/cells/formula_ast.h"

namespace cells {

namespace {

// Helper to extract references from an AST node and populate DependencyRef
class ReferenceExtractor {
public:
    void extract(const ASTNode* node, std::vector<DependencyRef>& refs) {
        if (!node) {
            return;
        }

        switch (node->type) {
            case ASTNodeType::CELL_REF: {
                auto* cellRef = static_cast<const CellRefNode*>(node);
                if (!cellRef->cellId.empty()) {
                    DependencyRef info;
                    info.type = DependencyRef::Type::CELL;
                    info.cellId = ID(cellRef->cellId);
                    info.sourceStart = static_cast<int32_t>(cellRef->position.start);
                    info.sourceEnd = static_cast<int32_t>(cellRef->position.end);
                    refs.push_back(info);
                }
                break;
            }

            case ASTNodeType::RANGE_REF: {
                auto* rangeRef = static_cast<const RangeRefNode*>(node);
                if (rangeRef->topLeft && rangeRef->bottomRight &&
                    !rangeRef->topLeft->cellId.empty() && !rangeRef->bottomRight->cellId.empty()) {
                    DependencyRef info;
                    info.type = DependencyRef::Type::RANGE;
                    info.startCellId = ID(rangeRef->topLeft->cellId);
                    info.endCellId = ID(rangeRef->bottomRight->cellId);
                    info.sourceStart = static_cast<int32_t>(rangeRef->position.start);
                    info.sourceEnd = static_cast<int32_t>(rangeRef->position.end);
                    refs.push_back(info);
                }
                break;
            }

            case ASTNodeType::COLUMN_REF: {
                auto* colRef = static_cast<const ColumnRefNode*>(node);
                if (!colRef->columnId.empty()) {
                    DependencyRef info;
                    info.type = DependencyRef::Type::COLUMN;
                    info.columnId = ID(colRef->columnId);
                    info.sourceStart = static_cast<int32_t>(colRef->position.start);
                    info.sourceEnd = static_cast<int32_t>(colRef->position.end);
                    refs.push_back(info);
                }
                break;
            }

            case ASTNodeType::ROW_REF: {
                auto* rowRef = static_cast<const RowRefNode*>(node);
                if (!rowRef->rowId.empty()) {
                    DependencyRef info;
                    info.type = DependencyRef::Type::ROW;
                    info.rowId = ID(rowRef->rowId);
                    info.sourceStart = static_cast<int32_t>(rowRef->position.start);
                    info.sourceEnd = static_cast<int32_t>(rowRef->position.end);
                    refs.push_back(info);
                }
                break;
            }

            case ASTNodeType::COLUMN_RANGE_REF: {
                auto* colRangeRef = static_cast<const ColumnRangeRefNode*>(node);
                if (!colRangeRef->startColumnId.empty() && !colRangeRef->endColumnId.empty()) {
                    DependencyRef info;
                    info.type = DependencyRef::Type::COLUMN_RANGE;
                    info.startColumnId = ID(colRangeRef->startColumnId);
                    info.endColumnId = ID(colRangeRef->endColumnId);
                    info.sourceStart = static_cast<int32_t>(colRangeRef->position.start);
                    info.sourceEnd = static_cast<int32_t>(colRangeRef->position.end);
                    refs.push_back(info);
                }
                break;
            }

            case ASTNodeType::ROW_RANGE_REF: {
                auto* rowRangeRef = static_cast<const RowRangeRefNode*>(node);
                if (!rowRangeRef->startRowId.empty() && !rowRangeRef->endRowId.empty()) {
                    DependencyRef info;
                    info.type = DependencyRef::Type::ROW_RANGE;
                    info.startRowId = ID(rowRangeRef->startRowId);
                    info.endRowId = ID(rowRangeRef->endRowId);
                    info.sourceStart = static_cast<int32_t>(rowRangeRef->position.start);
                    info.sourceEnd = static_cast<int32_t>(rowRangeRef->position.end);
                    refs.push_back(info);
                }
                break;
            }

            case ASTNodeType::BINARY_OP: {
                auto* binOp = static_cast<const BinaryOpNode*>(node);
                extract(binOp->left.get(), refs);
                extract(binOp->right.get(), refs);
                break;
            }

            case ASTNodeType::UNARY_OP: {
                auto* unaryOp = static_cast<const UnaryOpNode*>(node);
                extract(unaryOp->operand.get(), refs);
                break;
            }

            case ASTNodeType::FUNCTION_CALL: {
                auto* funcCall = static_cast<const FunctionCallNode*>(node);
                for (const auto& arg : funcCall->args) {
                    extract(arg.get(), refs);
                }
                break;
            }

            case ASTNodeType::ERROR_NODE: {
                auto* errorNode = static_cast<const ErrorNode*>(node);
                for (const auto& child : errorNode->partialChildren) {
                    extract(child.get(), refs);
                }
                break;
            }

            default:
                // Literals, named refs (need resolution first), etc.
                break;
        }
    }

    // Check if AST contains volatile functions
    bool hasVolatile(const ASTNode* node) {
        if (!node) {
            return false;
        }

        switch (node->type) {
            case ASTNodeType::FUNCTION_CALL: {
                auto* funcCall = static_cast<const FunctionCallNode*>(node);
                if (funcCall->isVolatile || FunctionCallNode::isVolatileFunction(funcCall->name)) {
                    return true;
                }
                for (const auto& arg : funcCall->args) {
                    if (hasVolatile(arg.get())) {
                        return true;
                    }
                }
                return false;
            }

            case ASTNodeType::BINARY_OP: {
                auto* binOp = static_cast<const BinaryOpNode*>(node);
                return hasVolatile(binOp->left.get()) || hasVolatile(binOp->right.get());
            }

            case ASTNodeType::UNARY_OP: {
                auto* unaryOp = static_cast<const UnaryOpNode*>(node);
                return hasVolatile(unaryOp->operand.get());
            }

            case ASTNodeType::ERROR_NODE: {
                auto* errorNode = static_cast<const ErrorNode*>(node);
                for (const auto& child : errorNode->partialChildren) {
                    if (hasVolatile(child.get())) {
                        return true;
                    }
                }
                return false;
            }

            default:
                return false;
        }
    }
};

}  // namespace

void DependencyGraph::addFormula(const ID& cellId, const ASTNode* ast) {
    // Remove old dependencies first
    removeFormula(cellId);

    if (!ast) {
        return;
    }

    // Extract references from AST
    ReferenceExtractor extractor;
    std::vector<DependencyRef> refs;
    extractor.extract(ast, refs);

    // Store for direct lookup
    dependencies_[cellId] = refs;

    // Populate reverse index for O(1) getDependents() lookups
    for (const auto& ref : refs) {
        if (ref.type == DependencyRef::Type::CELL) {
            reverseDeps_[ref.cellId].push_back(cellId);
        }
    }

    // Track volatile status
    if (extractor.hasVolatile(ast)) {
        volatileCells_.insert(cellId);
    }
}

void DependencyGraph::removeFormula(const ID& cellId) {
    // Clean up reverse index before removing dependencies
    auto depIt = dependencies_.find(cellId);
    if (depIt != dependencies_.end()) {
        for (const auto& ref : depIt->second) {
            if (ref.type == DependencyRef::Type::CELL) {
                auto revIt = reverseDeps_.find(ref.cellId);
                if (revIt != reverseDeps_.end()) {
                    auto& vec = revIt->second;
                    vec.erase(std::remove(vec.begin(), vec.end(), cellId), vec.end());
                    // Clean up empty vectors to save memory
                    if (vec.empty()) {
                        reverseDeps_.erase(revIt);
                    }
                }
            }
        }
    }

    // Remove from dependencies
    dependencies_.erase(cellId);

    // Remove from R-tree (if we had rects stored)
    auto it = cellRects_.find(cellId);
    if (it != cellRects_.end()) {
        for (const auto& rect : it->second) {
            rtree_.remove(rect, cellId);
        }
        cellRects_.erase(it);
    }

    // Remove volatile status
    volatileCells_.erase(cellId);
}

std::vector<ID> DependencyGraph::getDependents(const ID& cellId) const {
    // O(1) lookup using reverse index
    auto it = reverseDeps_.find(cellId);
    if (it != reverseDeps_.end()) {
        return it->second;
    }
    return {};
}

std::vector<ID> DependencyGraph::getDependentsInRange(int32_t minCol, int32_t minRow,
                                                      int32_t maxCol, int32_t maxRow) const {
    return rtree_.queryRange(minCol, minRow, maxCol, maxRow);
}

std::vector<DependencyRef> DependencyGraph::getDependencies(const ID& cellId) const {
    auto it = dependencies_.find(cellId);
    if (it != dependencies_.end()) {
        return it->second;
    }
    return {};
}

void DependencyGraph::markVolatile(const ID& cellId) {
    volatileCells_.insert(cellId);
}

void DependencyGraph::unmarkVolatile(const ID& cellId) {
    volatileCells_.erase(cellId);
}

std::vector<ID> DependencyGraph::getVolatileCells() const {
    std::vector<ID> result;
    result.reserve(volatileCells_.size());
    for (const auto& id : volatileCells_) {
        result.push_back(id);
    }
    return result;
}

bool DependencyGraph::isVolatile(const ID& cellId) const {
    return volatileCells_.count(cellId) > 0;
}

std::vector<ID> DependencyGraph::detectCycle(const ID& startCellId) const {
    std::unordered_set<ID> visited;
    std::unordered_set<ID> onStack;
    std::vector<ID> path;

    if (dfs(startCellId, visited, onStack, path)) {
        return path;
    }
    return {};
}

bool DependencyGraph::dfs(const ID& cellId, std::unordered_set<ID>& visited,
                          std::unordered_set<ID>& onStack, std::vector<ID>& path) const {
    visited.insert(cellId);
    onStack.insert(cellId);
    path.push_back(cellId);

    // Get dependencies for this cell
    auto it = dependencies_.find(cellId);
    if (it != dependencies_.end()) {
        for (const auto& ref : it->second) {
            ID depId;
            switch (ref.type) {
                case DependencyRef::Type::CELL:
                    depId = ref.cellId;
                    break;
                // For range refs, we'd need to check all cells in the range
                // For now, just check the corners
                case DependencyRef::Type::RANGE:
                    depId = ref.startCellId;
                    break;
                default:
                    continue;  // Skip non-cell refs for cycle detection
            }

            if (depId.isNull()) {
                continue;
            }

            if (onStack.count(depId) > 0) {
                // Found a cycle - add the cycle-starting cell to complete the path
                path.push_back(depId);
                return true;
            }

            if (visited.count(depId) == 0) {
                if (dfs(depId, visited, onStack, path)) {
                    return true;
                }
            }
        }
    }

    onStack.erase(cellId);
    path.pop_back();
    return false;
}

std::vector<ID> DependencyGraph::getRecalcOrder(const std::vector<ID>& changedCells,
                                                bool* hasCycle) const {
    if (hasCycle) {
        *hasCycle = false;
    }

    // Build a set of all cells that need recalculation
    std::unordered_set<ID> toRecalc(changedCells.begin(), changedCells.end());

    // Expand to include all transitive dependents
    std::vector<ID> queue = changedCells;
    while (!queue.empty()) {
        const ID cellId = queue.back();
        queue.pop_back();

        auto dependents = getDependents(cellId);
        for (const auto& dep : dependents) {
            if (toRecalc.insert(dep).second) {
                queue.push_back(dep);
            }
        }
    }

    // Also include all volatile cells
    for (const auto& volatileId : volatileCells_) {
        toRecalc.insert(volatileId);
    }

    // Topological sort using Kahn's algorithm
    // Build in-degree map and adjacency list
    std::unordered_map<ID, int> inDegree;
    std::unordered_map<ID, std::vector<ID>> adjList;

    for (const auto& cellId : toRecalc) {
        inDegree[cellId] = 0;
    }

    for (const auto& cellId : toRecalc) {
        auto deps = getDependencies(cellId);
        for (const auto& ref : deps) {
            ID depId;
            if (ref.type == DependencyRef::Type::CELL) {
                depId = ref.cellId;
            } else if (ref.type == DependencyRef::Type::RANGE) {
                depId = ref.startCellId;  // Simplified
            }

            if (!depId.isNull() && toRecalc.count(depId) > 0) {
                adjList[depId].push_back(cellId);
                inDegree[cellId]++;
            }
        }
    }

    // Start with cells that have no dependencies within the set
    std::vector<ID> result;
    std::vector<ID> zeroInDegree;
    for (const auto& [cellId, degree] : inDegree) {
        if (degree == 0) {
            zeroInDegree.push_back(cellId);
        }
    }

    while (!zeroInDegree.empty()) {
        const ID cellId = zeroInDegree.back();
        zeroInDegree.pop_back();
        result.push_back(cellId);

        for (const auto& dep : adjList[cellId]) {
            if (--inDegree[dep] == 0) {
                zeroInDegree.push_back(dep);
            }
        }
    }

    // Check for cycle
    if (result.size() < toRecalc.size()) {
        if (hasCycle) {
            *hasCycle = true;
        }
        return {};  // Cycle detected
    }

    return result;
}

void DependencyGraph::clear() {
    rtree_.clear();
    dependencies_.clear();
    reverseDeps_.clear();
    cellRects_.clear();
    volatileCells_.clear();
}

int32_t DependencyGraph::positionToCoord(uint32_t position) {
    return static_cast<int32_t>(position);
}

}  // namespace cells
