#include "core/cells/dependency_graph.h"

#include <algorithm>
#include <stack>

#include "core/cells/formula_ast.h"
#include "core/cells/named_ranges.h"

namespace cells {

namespace {

// Maximum recursion depth for named reference resolution (prevents infinite loops)
constexpr int kMaxNamedRefDepth = 32;

// Helper to extract references from an AST node and populate DependencyRef
class ReferenceExtractor {
public:
    ReferenceExtractor() = default;

    ReferenceExtractor(const NamedRangeRegistry* registry, const ID& sheetId)
        : namedRegistry_(registry), sheetId_(sheetId) {}

    // Get the named range keys that were resolved during extraction
    // Keys are in the format: name (for workbook scope) or "sheetId:name" (for sheet scope)
    [[nodiscard]] const std::vector<std::string>& getNamedRangeKeys() const {
        return namedRangeKeys_;
    }

    void extract(const ASTNode* node, std::vector<DependencyRef>& refs, int depth = 0) {
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
                extract(binOp->left.get(), refs, depth);
                extract(binOp->right.get(), refs, depth);
                break;
            }

            case ASTNodeType::UNARY_OP: {
                auto* unaryOp = static_cast<const UnaryOpNode*>(node);
                extract(unaryOp->operand.get(), refs, depth);
                break;
            }

            case ASTNodeType::FUNCTION_CALL: {
                auto* funcCall = static_cast<const FunctionCallNode*>(node);
                for (const auto& arg : funcCall->args) {
                    extract(arg.get(), refs, depth);
                }
                break;
            }

            case ASTNodeType::ERROR_NODE: {
                auto* errorNode = static_cast<const ErrorNode*>(node);
                for (const auto& child : errorNode->partialChildren) {
                    extract(child.get(), refs, depth);
                }
                break;
            }

            case ASTNodeType::NAMED_REF: {
                // Resolve named reference to its underlying target
                if (namedRegistry_ && depth < kMaxNamedRefDepth) {
                    auto* namedRef = static_cast<const NamedRefNode*>(node);
                    const NamedRange* range = namedRegistry_->resolve(namedRef->name, sheetId_);
                    if (range) {
                        // Record the named range key for dependency tracking
                        // This allows us to mark formulas dirty when the named range is deleted
                        if (range->scope == NamedRangeScope::WORKBOOK) {
                            namedRangeKeys_.push_back(namedRef->name);
                        } else {
                            // Sheet-scoped: use "sheetId:name" format
                            namedRangeKeys_.push_back(range->scopeSheetId.toString() + ":" +
                                                      namedRef->name);
                        }

                        // Convert the named range target to DependencyRef
                        DependencyRef info;
                        info.sourceStart = static_cast<int32_t>(namedRef->position.start);
                        info.sourceEnd = static_cast<int32_t>(namedRef->position.end);

                        switch (range->target.type) {
                            case NamedRangeTarget::Type::CELL:
                                info.type = DependencyRef::Type::CELL;
                                info.cellId = range->target.id1;
                                refs.push_back(info);
                                break;
                            case NamedRangeTarget::Type::RANGE:
                                info.type = DependencyRef::Type::RANGE;
                                info.startCellId = range->target.id1;
                                info.endCellId = range->target.id2;
                                refs.push_back(info);
                                break;
                            case NamedRangeTarget::Type::COLUMN:
                                info.type = DependencyRef::Type::COLUMN;
                                info.columnId = range->target.id1;
                                refs.push_back(info);
                                break;
                            case NamedRangeTarget::Type::ROW:
                                info.type = DependencyRef::Type::ROW;
                                info.rowId = range->target.id1;
                                refs.push_back(info);
                                break;
                            case NamedRangeTarget::Type::COLUMN_RANGE:
                                info.type = DependencyRef::Type::COLUMN_RANGE;
                                info.startColumnId = range->target.id1;
                                info.endColumnId = range->target.id2;
                                refs.push_back(info);
                                break;
                            case NamedRangeTarget::Type::ROW_RANGE:
                                info.type = DependencyRef::Type::ROW_RANGE;
                                info.startRowId = range->target.id1;
                                info.endRowId = range->target.id2;
                                refs.push_back(info);
                                break;
                        }
                    }
                }
                break;
            }

            default:
                // Literals, etc.
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

private:
    const NamedRangeRegistry* namedRegistry_ = nullptr;
    ID sheetId_;
    std::vector<std::string> namedRangeKeys_;
};

}  // namespace

void DependencyGraph::addFormula(const ID& cellId, const ASTNode* ast) {
    // Delegate to overload without position resolver or named range registry
    addFormula(cellId, ast, nullptr, nullptr, ID());
}

void DependencyGraph::addFormula(const ID& cellId, const ASTNode* ast,
                                 const PositionResolver& resolver) {
    // Delegate to overload without named range registry
    addFormula(cellId, ast, resolver, nullptr, ID());
}

void DependencyGraph::addFormula(const ID& cellId, const ASTNode* ast,
                                 const PositionResolver& resolver,
                                 const NamedRangeRegistry* namedRegistry, const ID& sheetId) {
    // Remove old dependencies first
    removeFormula(cellId);

    if (!ast) {
        return;
    }

    // Extract references from AST (with named range resolution if registry provided)
    ReferenceExtractor extractor(namedRegistry, sheetId);
    std::vector<DependencyRef> refs;
    extractor.extract(ast, refs);

    // Store for direct lookup
    dependencies_[cellId] = refs;

    // Store named range dependencies (for marking dirty when named range is deleted)
    const auto& namedRangeKeys = extractor.getNamedRangeKeys();
    if (!namedRangeKeys.empty()) {
        cellNamedRangeDeps_[cellId] = namedRangeKeys;
        for (const auto& key : namedRangeKeys) {
            namedRangeDependents_[key].insert(cellId);
        }
    }

    // Populate reverse index for O(1) getDependents() lookups
    // and R-tree for range queries
    for (const auto& ref : refs) {
        if (ref.type == DependencyRef::Type::CELL) {
            reverseDeps_[ref.cellId].push_back(cellId);
        } else if (ref.type == DependencyRef::Type::RANGE && resolver) {
            // Get positions of range corners and insert into R-tree
            auto [startCol, startRow] = resolver(ref.startCellId);
            auto [endCol, endRow] = resolver(ref.endCellId);

            if (startCol >= 0 && startRow >= 0 && endCol >= 0 && endRow >= 0) {
                // Normalize bounds (in case start > end)
                const int32_t minCol = std::min(startCol, endCol);
                const int32_t maxCol = std::max(startCol, endCol);
                const int32_t minRow = std::min(startRow, endRow);
                const int32_t maxRow = std::max(startRow, endRow);

                const BoundingRect rect(minCol, minRow, maxCol, maxRow);
                rtree_.insert(rect, cellId);
                cellRects_[cellId].push_back(rect);
            }
        } else if (ref.type == DependencyRef::Type::COLUMN && resolver) {
            // Whole column reference - insert tall rectangle
            auto [col, row] = resolver(ref.columnId);
            (void)row;  // Unused for column refs
            if (col >= 0) {
                const BoundingRect rect = BoundingRect::wholeColumn(col);
                rtree_.insert(rect, cellId);
                cellRects_[cellId].push_back(rect);
            }
        } else if (ref.type == DependencyRef::Type::ROW && resolver) {
            // Whole row reference - insert wide rectangle
            auto [col, row] = resolver(ref.rowId);
            (void)col;  // Unused for row refs
            if (row >= 0) {
                const BoundingRect rect = BoundingRect::wholeRow(row);
                rtree_.insert(rect, cellId);
                cellRects_[cellId].push_back(rect);
            }
        } else if (ref.type == DependencyRef::Type::COLUMN_RANGE && resolver) {
            // Column range reference
            auto [startCol, dummy1] = resolver(ref.startColumnId);
            auto [endCol, dummy2] = resolver(ref.endColumnId);
            (void)dummy1;
            (void)dummy2;
            if (startCol >= 0 && endCol >= 0) {
                const BoundingRect rect = BoundingRect::columnRange(std::min(startCol, endCol),
                                                                    std::max(startCol, endCol));
                rtree_.insert(rect, cellId);
                cellRects_[cellId].push_back(rect);
            }
        } else if (ref.type == DependencyRef::Type::ROW_RANGE && resolver) {
            // Row range reference
            auto [dummy1, startRow] = resolver(ref.startRowId);
            auto [dummy2, endRow] = resolver(ref.endRowId);
            (void)dummy1;
            (void)dummy2;
            if (startRow >= 0 && endRow >= 0) {
                const BoundingRect rect =
                    BoundingRect::rowRange(std::min(startRow, endRow), std::max(startRow, endRow));
                rtree_.insert(rect, cellId);
                cellRects_[cellId].push_back(rect);
            }
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

    // Clean up named range dependencies
    auto namedIt = cellNamedRangeDeps_.find(cellId);
    if (namedIt != cellNamedRangeDeps_.end()) {
        for (const auto& key : namedIt->second) {
            auto rangeIt = namedRangeDependents_.find(key);
            if (rangeIt != namedRangeDependents_.end()) {
                rangeIt->second.erase(cellId);
                // Clean up empty sets to save memory
                if (rangeIt->second.empty()) {
                    namedRangeDependents_.erase(rangeIt);
                }
            }
        }
        cellNamedRangeDeps_.erase(namedIt);
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

std::vector<ID> DependencyGraph::getDependentsForCell(const ID& cellId, int32_t col,
                                                      int32_t row) const {
    std::vector<ID> result;

    // 1. Direct cell dependencies from reverse index (O(1))
    auto it = reverseDeps_.find(cellId);
    if (it != reverseDeps_.end()) {
        result = it->second;
    }

    // 2. Range dependencies from R-tree point query (O(log n))
    // Find all formulas with ranges that contain this cell's position
    const std::vector<ID> rangeDeps = rtree_.query(col, row);

    // Merge range deps, avoiding duplicates
    for (const ID& dep : rangeDeps) {
        if (std::find(result.begin(), result.end(), dep) == result.end()) {
            result.push_back(dep);
        }
    }

    return result;
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

    // NOTE: Volatile cells are NOT automatically included here.
    // They should only be recalculated when explicitly requested via recalculateVolatile().
    // This prevents RAND(), NOW(), etc. from recalculating on every unrelated cell change.

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
    namedRangeDependents_.clear();
    cellNamedRangeDeps_.clear();
}

void DependencyGraph::rebuildRTree(const PositionResolver& resolver) {
    if (!resolver) {
        return;
    }

    // Clear existing R-tree data
    rtree_.clear();
    cellRects_.clear();

    // Re-insert all range dependencies with updated positions
    for (const auto& [cellId, refs] : dependencies_) {
        for (const auto& ref : refs) {
            if (ref.type == DependencyRef::Type::RANGE) {
                auto [startCol, startRow] = resolver(ref.startCellId);
                auto [endCol, endRow] = resolver(ref.endCellId);

                if (startCol >= 0 && startRow >= 0 && endCol >= 0 && endRow >= 0) {
                    const int32_t minCol = std::min(startCol, endCol);
                    const int32_t maxCol = std::max(startCol, endCol);
                    const int32_t minRow = std::min(startRow, endRow);
                    const int32_t maxRow = std::max(startRow, endRow);

                    const BoundingRect rect(minCol, minRow, maxCol, maxRow);
                    rtree_.insert(rect, cellId);
                    cellRects_[cellId].push_back(rect);
                }
            } else if (ref.type == DependencyRef::Type::COLUMN) {
                auto [col, row] = resolver(ref.columnId);
                (void)row;
                if (col >= 0) {
                    const BoundingRect rect = BoundingRect::wholeColumn(col);
                    rtree_.insert(rect, cellId);
                    cellRects_[cellId].push_back(rect);
                }
            } else if (ref.type == DependencyRef::Type::ROW) {
                auto [col, row] = resolver(ref.rowId);
                (void)col;
                if (row >= 0) {
                    const BoundingRect rect = BoundingRect::wholeRow(row);
                    rtree_.insert(rect, cellId);
                    cellRects_[cellId].push_back(rect);
                }
            } else if (ref.type == DependencyRef::Type::COLUMN_RANGE) {
                auto [startCol, dummy1] = resolver(ref.startColumnId);
                auto [endCol, dummy2] = resolver(ref.endColumnId);
                (void)dummy1;
                (void)dummy2;
                if (startCol >= 0 && endCol >= 0) {
                    const BoundingRect rect = BoundingRect::columnRange(std::min(startCol, endCol),
                                                                        std::max(startCol, endCol));
                    rtree_.insert(rect, cellId);
                    cellRects_[cellId].push_back(rect);
                }
            } else if (ref.type == DependencyRef::Type::ROW_RANGE) {
                auto [dummy1, startRow] = resolver(ref.startRowId);
                auto [dummy2, endRow] = resolver(ref.endRowId);
                (void)dummy1;
                (void)dummy2;
                if (startRow >= 0 && endRow >= 0) {
                    const BoundingRect rect = BoundingRect::rowRange(std::min(startRow, endRow),
                                                                     std::max(startRow, endRow));
                    rtree_.insert(rect, cellId);
                    cellRects_[cellId].push_back(rect);
                }
            }
        }
    }
}

int32_t DependencyGraph::positionToCoord(uint32_t position) {
    return static_cast<int32_t>(position);
}

std::vector<ID> DependencyGraph::getDependentsForWorkbookNamedRange(const std::string& name) const {
    std::vector<ID> result;
    auto it = namedRangeDependents_.find(name);
    if (it != namedRangeDependents_.end()) {
        result.reserve(it->second.size());
        for (const auto& id : it->second) {
            result.push_back(id);
        }
    }
    return result;
}

std::vector<ID> DependencyGraph::getDependentsForSheetNamedRange(const std::string& name,
                                                                 const ID& sheetId) const {
    std::vector<ID> result;
    // Use the same key format as NamedRangeRegistry: "sheetId:name"
    const std::string key = sheetId.toString() + ":" + name;
    auto it = namedRangeDependents_.find(key);
    if (it != namedRangeDependents_.end()) {
        result.reserve(it->second.size());
        for (const auto& id : it->second) {
            result.push_back(id);
        }
    }
    return result;
}

}  // namespace cells
