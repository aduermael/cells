#ifndef CELLS_RTREE_H_
#define CELLS_RTREE_H_

#include <cstdint>

#include <algorithm>
#include <functional>
#include <limits>
#include <memory>
#include <vector>

namespace cells {

// Bounding rectangle for 2D spatial indexing
// Uses int32_t coordinates to support sparse cell positions
// Whole-column refs: row bounds = 0 to MAX
// Whole-row refs: col bounds = 0 to MAX
struct BoundingRect {
    int32_t minCol;
    int32_t minRow;
    int32_t maxCol;
    int32_t maxRow;

    static constexpr int32_t MAX_COORD = std::numeric_limits<int32_t>::max();

    BoundingRect() : minCol(0), minRow(0), maxCol(0), maxRow(0) {}

    BoundingRect(int32_t minC, int32_t minR, int32_t maxC, int32_t maxR)
        : minCol(minC), minRow(minR), maxCol(maxC), maxRow(maxR) {}

    // Create a point rectangle (1x1)
    static BoundingRect point(int32_t col, int32_t row) { return {col, row, col, row}; }

    // Create a whole-column rectangle
    static BoundingRect wholeColumn(int32_t col) { return {col, 0, col, MAX_COORD}; }

    // Create a column range rectangle
    static BoundingRect columnRange(int32_t startCol, int32_t endCol) {
        return {startCol, 0, endCol, MAX_COORD};
    }

    // Create a whole-row rectangle
    static BoundingRect wholeRow(int32_t row) { return {0, row, MAX_COORD, row}; }

    // Create a row range rectangle
    static BoundingRect rowRange(int32_t startRow, int32_t endRow) {
        return {0, startRow, MAX_COORD, endRow};
    }

    // Check if this rectangle contains a point
    [[nodiscard]] bool containsPoint(int32_t col, int32_t row) const {
        return col >= minCol && col <= maxCol && row >= minRow && row <= maxRow;
    }

    // Check if this rectangle intersects another
    [[nodiscard]] bool intersects(const BoundingRect& other) const {
        return other.minCol <= maxCol && other.maxCol >= minCol && other.minRow <= maxRow &&
               other.maxRow >= minRow;
    }

    // Check if this rectangle contains another entirely
    [[nodiscard]] bool contains(const BoundingRect& other) const {
        return other.minCol >= minCol && other.maxCol <= maxCol && other.minRow >= minRow &&
               other.maxRow <= maxRow;
    }

    // Calculate area (for R-tree node selection)
    // Returns double to handle MAX_COORD without overflow
    [[nodiscard]] double area() const {
        return static_cast<double>(maxCol - minCol + 1) * static_cast<double>(maxRow - minRow + 1);
    }

    // Calculate enlarged area if we include another rect
    [[nodiscard]] double enlargedArea(const BoundingRect& other) const {
        const int32_t newMinCol = std::min(minCol, other.minCol);
        const int32_t newMaxCol = std::max(maxCol, other.maxCol);
        const int32_t newMinRow = std::min(minRow, other.minRow);
        const int32_t newMaxRow = std::max(maxRow, other.maxRow);
        return static_cast<double>(newMaxCol - newMinCol + 1) *
               static_cast<double>(newMaxRow - newMinRow + 1);
    }

    // Expand this rectangle to include another
    void expand(const BoundingRect& other) {
        minCol = std::min(minCol, other.minCol);
        maxCol = std::max(maxCol, other.maxCol);
        minRow = std::min(minRow, other.minRow);
        maxRow = std::max(maxRow, other.maxRow);
    }

    bool operator==(const BoundingRect& other) const {
        return minCol == other.minCol && maxCol == other.maxCol && minRow == other.minRow &&
               maxRow == other.maxRow;
    }
};

// R-tree entry: a bounding rectangle with associated data
template <typename T>
struct RTreeEntry {
    BoundingRect bounds;
    T value{};  // Value-initialize to satisfy lint

    RTreeEntry() = default;
    RTreeEntry(const BoundingRect& b, const T& v) : bounds(b), value(v) {}
};

// Forward declaration
template <typename T>
struct RTreeNode;

// R-tree for efficient 2D spatial indexing
// Optimized for sparse cell coordinates in spreadsheets
// M = max entries per node, m = min entries per node (M/2)
template <typename T>
class RTree {
public:
    static constexpr size_t MAX_ENTRIES = 8;  // Max entries per node
    static constexpr size_t MIN_ENTRIES = 4;  // Min entries per node (half of max)

    RTree() : root_(std::make_unique<RTreeNode<T>>(true)) {}

    // Insert a rectangle with associated value
    void insert(const BoundingRect& bounds, const T& value) {
        insert(RTreeEntry<T>(bounds, value));
    }

    void insert(int32_t minCol, int32_t minRow, int32_t maxCol, int32_t maxRow, const T& value) {
        insert(BoundingRect(minCol, minRow, maxCol, maxRow), value);
    }

    // Remove a rectangle with matching value
    // Returns true if found and removed
    bool remove(const BoundingRect& bounds, const T& value) {
        if (!root_) {
            return false;
        }

        std::vector<RTreeEntry<T>> orphans;
        const bool found = removeFromNode(root_.get(), bounds, value, orphans);

        if (found) {
            --size_;
            // Reinsert orphaned entries (without incrementing size - they're already counted)
            for (const auto& entry : orphans) {
                insertInternal(entry);
            }
            // Shrink tree if root has only one child
            while (!root_->isLeaf && root_->children.size() == 1) {
                root_ = std::move(root_->children[0]);
            }
        }

        return found;
    }

    bool remove(int32_t minCol, int32_t minRow, int32_t maxCol, int32_t maxRow, const T& value) {
        return remove(BoundingRect(minCol, minRow, maxCol, maxRow), value);
    }

    // Find all values whose rectangles contain the given point
    [[nodiscard]] std::vector<T> query(int32_t col, int32_t row) const {
        std::vector<T> results;
        if (root_) {
            queryPoint(root_.get(), col, row, results);
        }
        return results;
    }

    // Find all values whose rectangles intersect the given range
    [[nodiscard]] std::vector<T> queryRange(const BoundingRect& range) const {
        std::vector<T> results;
        if (root_) {
            queryRange(root_.get(), range, results);
        }
        return results;
    }

    [[nodiscard]] std::vector<T> queryRange(int32_t minCol, int32_t minRow, int32_t maxCol,
                                            int32_t maxRow) const {
        return queryRange(BoundingRect(minCol, minRow, maxCol, maxRow));
    }

    // Get the number of entries in the tree
    [[nodiscard]] size_t size() const { return size_; }

    // Check if tree is empty
    [[nodiscard]] bool empty() const { return size_ == 0; }

    // Clear all entries
    void clear() {
        root_ = std::make_unique<RTreeNode<T>>(true);
        size_ = 0;
    }

    // Iterate over all entries (for debugging/testing)
    void forEach(const std::function<void(const BoundingRect&, const T&)>& callback) const {
        if (root_) {
            forEachNode(root_.get(), callback);
        }
    }

private:
    std::unique_ptr<RTreeNode<T>> root_;
    size_t size_{0};

    void insert(const RTreeEntry<T>& entry) {
        insertInternal(entry);
        ++size_;
    }

    // Insert without incrementing size (used for orphan reinsertion)
    void insertInternal(const RTreeEntry<T>& entry) {
        if (!root_) {
            root_ = std::make_unique<RTreeNode<T>>(true);
        }

        auto splitResult = insertIntoNode(root_.get(), entry);
        if (splitResult) {
            // Root was split, create new root
            auto newRoot = std::make_unique<RTreeNode<T>>(false);
            newRoot->bounds = root_->bounds;
            newRoot->bounds.expand(splitResult->bounds);
            newRoot->children.push_back(std::move(root_));
            newRoot->children.push_back(std::move(splitResult));
            root_ = std::move(newRoot);
        }
    }

    // Insert into node, returns split node if overflow occurred
    std::unique_ptr<RTreeNode<T>> insertIntoNode(RTreeNode<T>* node, const RTreeEntry<T>& entry) {
        if (node->isLeaf) {
            // Add entry to leaf
            node->entries.push_back(entry);
            node->bounds.expand(entry.bounds);

            if (node->entries.size() > MAX_ENTRIES) {
                return splitLeaf(node);
            }
            return nullptr;
        }

        // Choose subtree with minimum enlargement
        size_t bestIdx = 0;
        double minEnlargement = std::numeric_limits<double>::max();
        double minArea = std::numeric_limits<double>::max();

        for (size_t i = 0; i < node->children.size(); ++i) {
            const double enlargement = node->children[i]->bounds.enlargedArea(entry.bounds) -
                                       node->children[i]->bounds.area();
            const double area = node->children[i]->bounds.area();

            if (enlargement < minEnlargement || (enlargement == minEnlargement && area < minArea)) {
                minEnlargement = enlargement;
                minArea = area;
                bestIdx = i;
            }
        }

        auto splitResult = insertIntoNode(node->children[bestIdx].get(), entry);
        node->bounds.expand(entry.bounds);

        if (splitResult) {
            node->children.push_back(std::move(splitResult));
            if (node->children.size() > MAX_ENTRIES) {
                return splitInternal(node);
            }
        }

        return nullptr;
    }

    // Split a leaf node
    std::unique_ptr<RTreeNode<T>> splitLeaf(RTreeNode<T>* node) {
        auto newNode = std::make_unique<RTreeNode<T>>(true);

        // Simple split: sort by center and divide
        std::sort(node->entries.begin(), node->entries.end(),
                  [](const RTreeEntry<T>& a, const RTreeEntry<T>& b) {
                      const int64_t centerA =
                          static_cast<int64_t>(a.bounds.minCol + a.bounds.maxCol) / 2 +
                          static_cast<int64_t>(a.bounds.minRow + a.bounds.maxRow) / 2;
                      const int64_t centerB =
                          static_cast<int64_t>(b.bounds.minCol + b.bounds.maxCol) / 2 +
                          static_cast<int64_t>(b.bounds.minRow + b.bounds.maxRow) / 2;
                      return centerA < centerB;
                  });

        const size_t splitPoint = node->entries.size() / 2;
        for (size_t i = splitPoint; i < node->entries.size(); ++i) {
            newNode->entries.push_back(std::move(node->entries[i]));
        }
        node->entries.resize(splitPoint);

        // Recalculate bounds
        recalculateBounds(node);
        recalculateBounds(newNode.get());

        return newNode;
    }

    // Split an internal node
    std::unique_ptr<RTreeNode<T>> splitInternal(RTreeNode<T>* node) {
        auto newNode = std::make_unique<RTreeNode<T>>(false);

        // Sort children by center and divide
        std::sort(
            node->children.begin(), node->children.end(),
            [](const std::unique_ptr<RTreeNode<T>>& a, const std::unique_ptr<RTreeNode<T>>& b) {
                const int64_t centerA =
                    static_cast<int64_t>(a->bounds.minCol + a->bounds.maxCol) / 2 +
                    static_cast<int64_t>(a->bounds.minRow + a->bounds.maxRow) / 2;
                const int64_t centerB =
                    static_cast<int64_t>(b->bounds.minCol + b->bounds.maxCol) / 2 +
                    static_cast<int64_t>(b->bounds.minRow + b->bounds.maxRow) / 2;
                return centerA < centerB;
            });

        const size_t splitPoint = node->children.size() / 2;
        for (size_t i = splitPoint; i < node->children.size(); ++i) {
            newNode->children.push_back(std::move(node->children[i]));
        }
        node->children.resize(splitPoint);

        // Recalculate bounds
        recalculateBounds(node);
        recalculateBounds(newNode.get());

        return newNode;
    }

    void recalculateBounds(RTreeNode<T>* node) {
        if (node->isLeaf) {
            if (node->entries.empty()) {
                node->bounds = BoundingRect();
                return;
            }
            node->bounds = node->entries[0].bounds;
            for (size_t i = 1; i < node->entries.size(); ++i) {
                node->bounds.expand(node->entries[i].bounds);
            }
        } else {
            if (node->children.empty()) {
                node->bounds = BoundingRect();
                return;
            }
            node->bounds = node->children[0]->bounds;
            for (size_t i = 1; i < node->children.size(); ++i) {
                node->bounds.expand(node->children[i]->bounds);
            }
        }
    }

    bool removeFromNode(RTreeNode<T>* node, const BoundingRect& bounds, const T& value,
                        std::vector<RTreeEntry<T>>& orphans) {
        if (node->isLeaf) {
            for (auto it = node->entries.begin(); it != node->entries.end(); ++it) {
                if (it->bounds == bounds && it->value == value) {
                    node->entries.erase(it);
                    recalculateBounds(node);
                    return true;
                }
            }
            return false;
        }

        // Search children
        for (size_t i = 0; i < node->children.size(); ++i) {
            if (!node->children[i]->bounds.intersects(bounds)) {
                continue;
            }

            if (removeFromNode(node->children[i].get(), bounds, value, orphans)) {
                // Check if child underflowed
                if (node->children[i]->isLeaf) {
                    if (node->children[i]->entries.size() < MIN_ENTRIES) {
                        // Collect orphans
                        for (auto& entry : node->children[i]->entries) {
                            orphans.push_back(std::move(entry));
                        }
                        node->children.erase(node->children.begin() +
                                             static_cast<std::ptrdiff_t>(i));
                    }
                } else {
                    if (node->children[i]->children.size() < MIN_ENTRIES) {
                        // Collect orphans from all leaves under this node
                        collectAllEntries(node->children[i].get(), orphans);
                        node->children.erase(node->children.begin() +
                                             static_cast<std::ptrdiff_t>(i));
                    }
                }
                recalculateBounds(node);
                return true;
            }
        }
        return false;
    }

    void collectAllEntries(RTreeNode<T>* node, std::vector<RTreeEntry<T>>& entries) {
        if (node->isLeaf) {
            for (auto& entry : node->entries) {
                entries.push_back(std::move(entry));
            }
        } else {
            for (auto& child : node->children) {
                collectAllEntries(child.get(), entries);
            }
        }
    }

    void queryPoint(const RTreeNode<T>* node, int32_t col, int32_t row,
                    std::vector<T>& results) const {
        if (!node->bounds.containsPoint(col, row)) {
            return;
        }

        if (node->isLeaf) {
            for (const auto& entry : node->entries) {
                if (entry.bounds.containsPoint(col, row)) {
                    results.push_back(entry.value);
                }
            }
        } else {
            for (const auto& child : node->children) {
                queryPoint(child.get(), col, row, results);
            }
        }
    }

    void queryRange(const RTreeNode<T>* node, const BoundingRect& range,
                    std::vector<T>& results) const {
        if (!node->bounds.intersects(range)) {
            return;
        }

        if (node->isLeaf) {
            for (const auto& entry : node->entries) {
                if (entry.bounds.intersects(range)) {
                    results.push_back(entry.value);
                }
            }
        } else {
            for (const auto& child : node->children) {
                queryRange(child.get(), range, results);
            }
        }
    }

    void forEachNode(const RTreeNode<T>* node,
                     const std::function<void(const BoundingRect&, const T&)>& callback) const {
        if (node->isLeaf) {
            for (const auto& entry : node->entries) {
                callback(entry.bounds, entry.value);
            }
        } else {
            for (const auto& child : node->children) {
                forEachNode(child.get(), callback);
            }
        }
    }
};

// R-tree node (internal structure)
template <typename T>
struct RTreeNode {
    bool isLeaf;
    BoundingRect bounds;
    std::vector<RTreeEntry<T>> entries;                   // For leaf nodes
    std::vector<std::unique_ptr<RTreeNode<T>>> children;  // For internal nodes

    explicit RTreeNode(bool leaf) : isLeaf(leaf) {}
};

}  // namespace cells

#endif  // CELLS_RTREE_H_
