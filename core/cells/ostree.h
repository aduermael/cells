#ifndef CELLS_OSTREE_H_
#define CELLS_OSTREE_H_

#include <cstdint>

#include <functional>
#include <memory>
#include <optional>
#include <utility>

#include "core/cells/types.h"

namespace cells {

// Order-Statistic Tree (augmented red-black tree)
//
// Each node stores:
// - id: unique identifier (8-char base62)
// - size: the "width" of this node (e.g., column width or row height in pixels)
// - subtree_total: sum of all sizes in the subtree rooted at this node
//
// This allows O(log n) operations for:
// - findByOffset(offset): find which node contains a given cumulative offset
// - getOffset(node): get the cumulative offset of a node's start
// - insert/delete: maintain sorted order by position with balanced tree
// - updateSize: change a node's size and bubble up the change
//
// The tree maintains nodes in sorted order by their position (implicit from
// tree structure), not by ID. Nodes are positioned left-to-right in order.

// Node color for red-black tree
enum class NodeColor : uint8_t { RED, BLACK };

// Forward declaration
class OSTree;

// Tree node
struct OSNode {
    ID id;                   // Unique identifier
    uint32_t size;           // Size of this node (width/height in pixels)
    uint32_t subtree_total;  // Sum of sizes in this subtree (including this node)

    NodeColor color;
    OSNode* parent;
    OSNode* left;
    OSNode* right;

    OSNode()
        : size(0),
          subtree_total(0),
          color(NodeColor::RED),
          parent(nullptr),
          left(nullptr),
          right(nullptr) {}

    explicit OSNode(const ID& id, uint32_t size = 0)
        : id(id),
          size(size),
          subtree_total(size),
          color(NodeColor::RED),
          parent(nullptr),
          left(nullptr),
          right(nullptr) {}

    // Check if this node is a left child
    [[nodiscard]] bool isLeftChild() const { return parent != nullptr && parent->left == this; }

    // Check if this node is a right child
    [[nodiscard]] bool isRightChild() const { return parent != nullptr && parent->right == this; }

    // Get sibling node (nullptr if none)
    [[nodiscard]] OSNode* sibling() const {
        if (parent == nullptr)
            return nullptr;
        return isLeftChild() ? parent->right : parent->left;
    }

    // Get uncle node (parent's sibling, nullptr if none)
    [[nodiscard]] OSNode* uncle() const {
        if (parent == nullptr)
            return nullptr;
        return parent->sibling();
    }

    // Get grandparent node (nullptr if none)
    [[nodiscard]] OSNode* grandparent() const {
        if (parent == nullptr)
            return nullptr;
        return parent->parent;
    }
};

// Result of findByOffset - contains the node and the offset within that node
struct FindResult {
    OSNode* node;           // The node containing the offset (nullptr if not found)
    uint32_t offsetInNode;  // Offset from the start of this node

    FindResult() : node(nullptr), offsetInNode(0) {}
    FindResult(OSNode* node, uint32_t offsetInNode) : node(node), offsetInNode(offsetInNode) {}

    [[nodiscard]] bool found() const { return node != nullptr; }
};

// Order-Statistic Tree
class OSTree {
public:
    OSTree();
    ~OSTree();

    // Non-copyable
    OSTree(const OSTree&) = delete;
    OSTree& operator=(const OSTree&) = delete;

    // Movable
    OSTree(OSTree&& other) noexcept;
    OSTree& operator=(OSTree&& other) noexcept;

    // ========================================================================
    // Core operations
    // ========================================================================

    // Insert a new node at the end (appends to rightmost position)
    // Returns pointer to the new node
    OSNode* append(const ID& id, uint32_t size);

    // Insert a new node at a specific position (0-indexed)
    // Nodes at position >= pos are shifted right
    // Returns pointer to the new node, or nullptr if position is invalid
    OSNode* insertAt(size_t position, const ID& id, uint32_t size);

    // Remove a node by ID
    // Returns true if the node was found and removed
    bool remove(const ID& id);

    // Remove a node by pointer
    // The pointer must be valid and belong to this tree
    void removeNode(OSNode* node);

    // ========================================================================
    // Lookup operations
    // ========================================================================

    // Find node by ID
    // Returns nullptr if not found
    [[nodiscard]] OSNode* find(const ID& id) const;

    // Find node at a specific position (0-indexed)
    // Returns nullptr if position is out of range
    [[nodiscard]] OSNode* at(size_t position) const;

    // Find which node contains a given cumulative offset
    // Returns the node and the offset within that node
    // Example: if nodes have sizes [100, 50, 75], findByOffset(120) returns
    //          (node1, 20) because offset 120 is 20 pixels into the second node
    [[nodiscard]] FindResult findByOffset(uint32_t offset) const;

    // Get the cumulative offset of a node's start (sum of all preceding nodes' sizes)
    // Returns 0 for the first node
    [[nodiscard]] uint32_t getOffset(const OSNode* node) const;

    // Get the position (0-indexed) of a node
    [[nodiscard]] size_t getPosition(const OSNode* node) const;

    // ========================================================================
    // Modification operations
    // ========================================================================

    // Update a node's size and propagate the change up the tree
    void updateSize(OSNode* node, uint32_t newSize);

    // Move a node to a new position
    // Returns true if successful, false if position is invalid
    bool move(OSNode* node, size_t newPosition);

    // ========================================================================
    // Utility operations
    // ========================================================================

    // Get the total size (sum of all node sizes)
    [[nodiscard]] uint32_t totalSize() const;

    // Get the number of nodes
    [[nodiscard]] size_t count() const { return _count; }

    // Check if tree is empty
    [[nodiscard]] bool empty() const { return _root == nullptr; }

    // Clear all nodes
    void clear();

    // Get the first node (leftmost)
    [[nodiscard]] OSNode* first() const;

    // Get the last node (rightmost)
    [[nodiscard]] OSNode* last() const;

    // Get the next node in order (nullptr if this is the last)
    [[nodiscard]] static OSNode* next(const OSNode* node);

    // Get the previous node in order (nullptr if this is the first)
    [[nodiscard]] static OSNode* prev(const OSNode* node);

    // Iterate over all nodes in order
    // Callback receives (node, position)
    void forEach(const std::function<void(OSNode*, size_t)>& callback) const;

    // Debug: verify tree invariants (red-black properties, subtree totals)
    // Returns true if all invariants hold
    [[nodiscard]] bool verify() const;

private:
    OSNode* _root{nullptr};
    size_t _count{0};

    // ID -> Node lookup for O(1) find by ID
    std::unordered_map<ID, OSNode*, IDHash> _nodeIndex;

    // ========================================================================
    // Red-black tree operations
    // ========================================================================

    // Rotations (also update subtree_total)
    void rotateLeft(OSNode* x);
    void rotateRight(OSNode* y);

    // Fix red-black properties after insertion
    void fixInsert(OSNode* node);

    // Fix red-black properties after deletion
    void fixDelete(OSNode* node, OSNode* parent);

    // Transplant: replace subtree rooted at u with subtree rooted at v
    void transplant(OSNode* u, OSNode* v);

    // Find minimum node in subtree
    [[nodiscard]] static OSNode* minimum(OSNode* node);

    // Find maximum node in subtree
    [[nodiscard]] static OSNode* maximum(OSNode* node);

    // Update subtree_total for a node based on its children
    static void updateSubtreeTotal(OSNode* node);

    // Propagate subtree_total changes up to root
    void propagateSubtreeTotal(OSNode* node);

    // Delete all nodes in subtree
    void deleteSubtree(OSNode* node);

    // Helper to get node at position using in-order traversal
    [[nodiscard]] OSNode* nodeAtPosition(size_t position) const;

    // Helper to count nodes in left subtree (for position calculation)
    [[nodiscard]] static size_t leftSubtreeCount(const OSNode* node);

    // Verify helper - returns black height or -1 if invalid
    [[nodiscard]] int verifySubtree(const OSNode* node, uint32_t& computedTotal) const;
};

}  // namespace cells

#endif  // CELLS_OSTREE_H_
