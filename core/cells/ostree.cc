#include "core/cells/ostree.h"

#include <cassert>

#include <algorithm>

namespace cells {

// ============================================================================
// Constructor / Destructor
// ============================================================================

OSTree::OSTree() = default;

OSTree::~OSTree() {
    clear();
}

OSTree::OSTree(OSTree&& other) noexcept
    : _root(other._root), _count(other._count), _nodeIndex(std::move(other._nodeIndex)) {
    other._root = nullptr;
    other._count = 0;
}

OSTree& OSTree::operator=(OSTree&& other) noexcept {
    if (this != &other) {
        clear();
        _root = other._root;
        _count = other._count;
        _nodeIndex = std::move(other._nodeIndex);
        other._root = nullptr;
        other._count = 0;
    }
    return *this;
}

// ============================================================================
// Core operations
// ============================================================================

OSNode* OSTree::append(const ID& id, uint32_t size) {
    return insertAt(_count, id, size);
}

OSNode* OSTree::insertAt(size_t position, const ID& id, uint32_t size) {
    // Position must be in range [0, _count]
    if (position > _count) {
        return nullptr;
    }

    // Create new node
    auto* node = new OSNode(id, size);

    // Add to index
    _nodeIndex[id] = node;

    if (_root == nullptr) {
        // Empty tree - new node becomes root
        node->color = NodeColor::BLACK;
        _root = node;
        _count = 1;
        return node;
    }

    // Find insertion point
    if (position == _count) {
        // Append at the end - find rightmost node
        OSNode* parent = maximum(_root);
        parent->right = node;
        node->parent = parent;
    } else {
        // Insert before the node at 'position'
        OSNode* successor = nodeAtPosition(position);
        if (successor->left == nullptr) {
            // Insert as left child of successor
            successor->left = node;
            node->parent = successor;
        } else {
            // Insert as right child of predecessor
            OSNode* pred = maximum(successor->left);
            pred->right = node;
            node->parent = pred;
        }
    }

    // Propagate subtree totals up from the new node's parent
    propagateSubtreeTotal(node->parent);

    // Fix red-black properties
    fixInsert(node);

    _count++;
    return node;
}

bool OSTree::remove(const ID& id) {
    OSNode* node = find(id);
    if (node == nullptr) {
        return false;
    }
    removeNode(node);
    return true;
}

void OSTree::removeNode(OSNode* node) {
    if (node == nullptr)
        return;

    // Remove from index
    _nodeIndex.erase(node->id);

    const OSNode* toDelete = node;
    OSNode* replacement = nullptr;
    OSNode* fixupParent = nullptr;
    NodeColor originalColor = node->color;

    if (node->left == nullptr) {
        // Case 1: No left child - replace with right child
        replacement = node->right;
        fixupParent = node->parent;
        transplant(node, node->right);
    } else if (node->right == nullptr) {
        // Case 2: No right child - replace with left child
        replacement = node->left;
        fixupParent = node->parent;
        transplant(node, node->left);
    } else {
        // Case 3: Two children - replace with in-order successor
        OSNode* successor = minimum(node->right);
        originalColor = successor->color;
        replacement = successor->right;
        fixupParent = successor;

        if (successor->parent == node) {
            // Successor is direct child
            if (replacement != nullptr) {
                replacement->parent = successor;
            }
            fixupParent = successor;
        } else {
            // Successor is deeper in the tree
            fixupParent = successor->parent;
            transplant(successor, successor->right);
            successor->right = node->right;
            if (successor->right != nullptr) {
                successor->right->parent = successor;
            }
        }

        transplant(node, successor);
        successor->left = node->left;
        if (successor->left != nullptr) {
            successor->left->parent = successor;
        }
        successor->color = node->color;

        // Update subtree total for successor
        updateSubtreeTotal(successor);
    }

    // Propagate subtree total changes up the tree
    if (fixupParent != nullptr) {
        propagateSubtreeTotal(fixupParent);
    }

    // Fix red-black properties if we removed a black node
    if (originalColor == NodeColor::BLACK) {
        fixDelete(replacement, fixupParent);
    }

    delete toDelete;
    _count--;
}

// ============================================================================
// Lookup operations
// ============================================================================

OSNode* OSTree::find(const ID& id) const {
    auto it = _nodeIndex.find(id);
    return it != _nodeIndex.end() ? it->second : nullptr;
}

OSNode* OSTree::at(size_t position) const {
    if (position >= _count) {
        return nullptr;
    }
    return nodeAtPosition(position);
}

FindResult OSTree::findByOffset(uint32_t offset) const {
    if (_root == nullptr) {
        return {};
    }

    // Check if offset is beyond total size
    if (offset >= _root->subtree_total) {
        return {};
    }

    OSNode* current = _root;
    uint32_t accumulatedOffset = 0;

    while (current != nullptr) {
        // Calculate left subtree total
        const uint32_t leftTotal = current->left != nullptr ? current->left->subtree_total : 0;

        if (offset < accumulatedOffset + leftTotal) {
            // Target is in left subtree
            current = current->left;
        } else if (offset < accumulatedOffset + leftTotal + current->size) {
            // Target is in this node
            const uint32_t offsetInNode = offset - (accumulatedOffset + leftTotal);
            return {current, offsetInNode};
        } else {
            // Target is in right subtree
            accumulatedOffset += leftTotal + current->size;
            current = current->right;
        }
    }

    return {};
}

uint32_t OSTree::getOffset(const OSNode* node) const {
    if (node == nullptr) {
        return 0;
    }

    uint32_t offset = 0;

    // Add left subtree total
    if (node->left != nullptr) {
        offset = node->left->subtree_total;
    }

    // Walk up the tree, adding left siblings' contributions
    const OSNode* current = node;
    while (current->parent != nullptr) {
        if (current->isRightChild()) {
            // Add parent and its left subtree
            offset += current->parent->size;
            if (current->parent->left != nullptr) {
                offset += current->parent->left->subtree_total;
            }
        }
        current = current->parent;
    }

    return offset;
}

size_t OSTree::getPosition(const OSNode* node) const {
    if (node == nullptr) {
        return 0;
    }

    size_t position = leftSubtreeCount(node);

    const OSNode* current = node;
    while (current->parent != nullptr) {
        if (current->isRightChild()) {
            position += 1 + leftSubtreeCount(current->parent);
        }
        current = current->parent;
    }

    return position;
}

// ============================================================================
// Modification operations
// ============================================================================

void OSTree::updateSize(OSNode* node, uint32_t newSize) {
    if (node == nullptr)
        return;

    node->size = newSize;
    updateSubtreeTotal(node);
    propagateSubtreeTotal(node->parent);
}

bool OSTree::move(OSNode* node, size_t newPosition) {
    if (node == nullptr || newPosition > _count - 1) {
        return false;
    }

    const size_t currentPos = getPosition(node);
    if (currentPos == newPosition) {
        return true;  // Already at target position
    }

    // Store node data
    const ID id = node->id;
    const uint32_t size = node->size;

    // Remove from current position
    removeNode(node);

    // DON'T adjust newPosition - if we're moving forward, removing ourselves
    // shifts everything after us down, but we want the final position in the
    // original numbering scheme, not the intermediate numbering after removal.
    // The insertAt will place us at the right spot.

    // Insert at new position
    insertAt(newPosition, id, size);

    return true;
}

// ============================================================================
// Utility operations
// ============================================================================

uint32_t OSTree::totalSize() const {
    return _root != nullptr ? _root->subtree_total : 0;
}

void OSTree::clear() {
    deleteSubtree(_root);
    _root = nullptr;
    _count = 0;
    _nodeIndex.clear();
}

OSNode* OSTree::first() const {
    return _root != nullptr ? minimum(_root) : nullptr;
}

OSNode* OSTree::last() const {
    return _root != nullptr ? maximum(_root) : nullptr;
}

OSNode* OSTree::next(const OSNode* node) {
    if (node == nullptr)
        return nullptr;

    // If there's a right subtree, return its minimum
    if (node->right != nullptr) {
        return minimum(node->right);
    }

    // Otherwise, go up until we find an ancestor we're the left child of
    const OSNode* current = node;
    while (current->parent != nullptr && current->isRightChild()) {
        current = current->parent;
    }

    // const_cast is safe here because we're returning a non-const pointer
    // to an element that was obtained from this tree
    return const_cast<OSNode*>(current->parent);
}

OSNode* OSTree::prev(const OSNode* node) {
    if (node == nullptr)
        return nullptr;

    // If there's a left subtree, return its maximum
    if (node->left != nullptr) {
        return maximum(node->left);
    }

    // Otherwise, go up until we find an ancestor we're the right child of
    const OSNode* current = node;
    while (current->parent != nullptr && current->isLeftChild()) {
        current = current->parent;
    }

    return const_cast<OSNode*>(current->parent);
}

void OSTree::forEach(const std::function<void(OSNode*, size_t)>& callback) const {
    size_t position = 0;
    OSNode* node = first();
    while (node != nullptr) {
        callback(node, position);
        node = next(node);
        position++;
    }
}

bool OSTree::verify() const {
    if (_root == nullptr) {
        return _count == 0;
    }

    // Root must be black
    if (_root->color != NodeColor::BLACK) {
        return false;
    }

    // Root must have no parent
    if (_root->parent != nullptr) {
        return false;
    }

    // Verify subtree and count black height
    uint32_t computedTotal = 0;
    const int blackHeight = verifySubtree(_root, computedTotal);

    if (blackHeight < 0) {
        return false;
    }

    // Verify total matches
    if (computedTotal != _root->subtree_total) {
        return false;
    }

    // Verify count matches
    size_t actualCount = 0;
    forEach([&actualCount](OSNode*, size_t) { actualCount++; });
    if (actualCount != _count) {
        return false;
    }

    // Verify index size matches count
    if (_nodeIndex.size() != _count) {
        return false;
    }

    return true;
}

// ============================================================================
// Private helpers
// ============================================================================

void OSTree::rotateLeft(OSNode* x) {
    OSNode* y = x->right;
    x->right = y->left;

    if (y->left != nullptr) {
        y->left->parent = x;
    }

    y->parent = x->parent;

    if (x->parent == nullptr) {
        _root = y;
    } else if (x->isLeftChild()) {
        x->parent->left = y;
    } else {
        x->parent->right = y;
    }

    y->left = x;
    x->parent = y;

    // Update subtree totals (x first, then y)
    updateSubtreeTotal(x);
    updateSubtreeTotal(y);
}

void OSTree::rotateRight(OSNode* y) {
    OSNode* x = y->left;
    y->left = x->right;

    if (x->right != nullptr) {
        x->right->parent = y;
    }

    x->parent = y->parent;

    if (y->parent == nullptr) {
        _root = x;
    } else if (y->isLeftChild()) {
        y->parent->left = x;
    } else {
        y->parent->right = x;
    }

    x->right = y;
    y->parent = x;

    // Update subtree totals (y first, then x)
    updateSubtreeTotal(y);
    updateSubtreeTotal(x);
}

void OSTree::fixInsert(OSNode* node) {
    while (node != _root && node->parent->color == NodeColor::RED) {
        OSNode* parent = node->parent;
        OSNode* grandparent = parent->parent;

        if (parent->isLeftChild()) {
            OSNode* uncle = grandparent->right;

            if (uncle != nullptr && uncle->color == NodeColor::RED) {
                // Case 1: Uncle is red - recolor
                parent->color = NodeColor::BLACK;
                uncle->color = NodeColor::BLACK;
                grandparent->color = NodeColor::RED;
                node = grandparent;
            } else {
                if (node->isRightChild()) {
                    // Case 2: Node is right child - rotate left
                    node = parent;
                    rotateLeft(node);
                    parent = node->parent;
                    grandparent = parent->parent;
                }
                // Case 3: Node is left child - rotate right
                parent->color = NodeColor::BLACK;
                grandparent->color = NodeColor::RED;
                rotateRight(grandparent);
            }
        } else {
            // Mirror cases for right child parent
            OSNode* uncle = grandparent->left;

            if (uncle != nullptr && uncle->color == NodeColor::RED) {
                // Case 1: Uncle is red - recolor
                parent->color = NodeColor::BLACK;
                uncle->color = NodeColor::BLACK;
                grandparent->color = NodeColor::RED;
                node = grandparent;
            } else {
                if (node->isLeftChild()) {
                    // Case 2: Node is left child - rotate right
                    node = parent;
                    rotateRight(node);
                    parent = node->parent;
                    grandparent = parent->parent;
                }
                // Case 3: Node is right child - rotate left
                parent->color = NodeColor::BLACK;
                grandparent->color = NodeColor::RED;
                rotateLeft(grandparent);
            }
        }
    }

    _root->color = NodeColor::BLACK;
}

void OSTree::fixDelete(OSNode* node, OSNode* parent) {
    while (node != _root && (node == nullptr || node->color == NodeColor::BLACK)) {
        if (parent == nullptr)
            break;

        if (node == parent->left) {
            OSNode* sibling = parent->right;

            if (sibling != nullptr && sibling->color == NodeColor::RED) {
                // Case 1: Sibling is red
                sibling->color = NodeColor::BLACK;
                parent->color = NodeColor::RED;
                rotateLeft(parent);
                sibling = parent->right;
            }

            const bool siblingLeftBlack = sibling == nullptr || sibling->left == nullptr ||
                                          sibling->left->color == NodeColor::BLACK;
            const bool siblingRightBlack = sibling == nullptr || sibling->right == nullptr ||
                                           sibling->right->color == NodeColor::BLACK;

            if (siblingLeftBlack && siblingRightBlack) {
                // Case 2: Sibling's children are both black
                if (sibling != nullptr) {
                    sibling->color = NodeColor::RED;
                }
                node = parent;
                parent = node->parent;
            } else {
                if (siblingRightBlack) {
                    // Case 3: Sibling's right child is black
                    if (sibling != nullptr && sibling->left != nullptr) {
                        sibling->left->color = NodeColor::BLACK;
                    }
                    if (sibling != nullptr) {
                        sibling->color = NodeColor::RED;
                        rotateRight(sibling);
                    }
                    sibling = parent->right;
                }

                // Case 4: Sibling's right child is red
                if (sibling != nullptr) {
                    sibling->color = parent->color;
                }
                parent->color = NodeColor::BLACK;
                if (sibling != nullptr && sibling->right != nullptr) {
                    sibling->right->color = NodeColor::BLACK;
                }
                rotateLeft(parent);
                node = _root;
                break;
            }
        } else {
            // Mirror cases for right child
            OSNode* sibling = parent->left;

            if (sibling != nullptr && sibling->color == NodeColor::RED) {
                sibling->color = NodeColor::BLACK;
                parent->color = NodeColor::RED;
                rotateRight(parent);
                sibling = parent->left;
            }

            const bool siblingLeftBlack = sibling == nullptr || sibling->left == nullptr ||
                                          sibling->left->color == NodeColor::BLACK;
            const bool siblingRightBlack = sibling == nullptr || sibling->right == nullptr ||
                                           sibling->right->color == NodeColor::BLACK;

            if (siblingLeftBlack && siblingRightBlack) {
                if (sibling != nullptr) {
                    sibling->color = NodeColor::RED;
                }
                node = parent;
                parent = node->parent;
            } else {
                if (siblingLeftBlack) {
                    if (sibling != nullptr && sibling->right != nullptr) {
                        sibling->right->color = NodeColor::BLACK;
                    }
                    if (sibling != nullptr) {
                        sibling->color = NodeColor::RED;
                        rotateLeft(sibling);
                    }
                    sibling = parent->left;
                }

                if (sibling != nullptr) {
                    sibling->color = parent->color;
                }
                parent->color = NodeColor::BLACK;
                if (sibling != nullptr && sibling->left != nullptr) {
                    sibling->left->color = NodeColor::BLACK;
                }
                rotateRight(parent);
                node = _root;
                break;
            }
        }
    }

    if (node != nullptr) {
        node->color = NodeColor::BLACK;
    }
}

void OSTree::transplant(OSNode* u, OSNode* v) {
    if (u->parent == nullptr) {
        _root = v;
    } else if (u->isLeftChild()) {
        u->parent->left = v;
    } else {
        u->parent->right = v;
    }

    if (v != nullptr) {
        v->parent = u->parent;
    }
}

OSNode* OSTree::minimum(OSNode* node) {
    while (node->left != nullptr) {
        node = node->left;
    }
    return node;
}

OSNode* OSTree::maximum(OSNode* node) {
    while (node->right != nullptr) {
        node = node->right;
    }
    return node;
}

void OSTree::updateSubtreeTotal(OSNode* node) {
    if (node == nullptr)
        return;

    node->subtree_total = node->size;
    if (node->left != nullptr) {
        node->subtree_total += node->left->subtree_total;
    }
    if (node->right != nullptr) {
        node->subtree_total += node->right->subtree_total;
    }
}

void OSTree::propagateSubtreeTotal(OSNode* node) {
    while (node != nullptr) {
        updateSubtreeTotal(node);
        node = node->parent;
    }
}

void OSTree::deleteSubtree(OSNode* node) {
    if (node == nullptr)
        return;
    deleteSubtree(node->left);
    deleteSubtree(node->right);
    delete node;
}

OSNode* OSTree::nodeAtPosition(size_t position) const {
    // Use in-order traversal to find node at position
    // This could be optimized with subtree counts, but keeping it simple for now
    OSNode* node = first();  // NOLINT(misc-const-correctness)
    size_t current = 0;

    while (node != nullptr && current < position) {
        node = next(node);
        current++;
    }

    return node;
}

size_t OSTree::leftSubtreeCount(const OSNode* node) {
    if (node == nullptr || node->left == nullptr) {
        return 0;
    }

    // Count nodes in left subtree
    size_t count = 0;
    const OSNode* current = minimum(node->left);
    while (current != nullptr && current != node) {
        count++;
        current = next(current);
    }
    return count;
}

int OSTree::verifySubtree(const OSNode* node, uint32_t& computedTotal) const {
    if (node == nullptr) {
        computedTotal = 0;
        return 0;  // Null nodes have black height 0
    }

    // Check parent pointers
    if (node->left != nullptr && node->left->parent != node) {
        return -1;
    }
    if (node->right != nullptr && node->right->parent != node) {
        return -1;
    }

    // Red node cannot have red children
    if (node->color == NodeColor::RED) {
        if ((node->left != nullptr && node->left->color == NodeColor::RED) ||
            (node->right != nullptr && node->right->color == NodeColor::RED)) {
            return -1;
        }
    }

    // Recursively verify children
    uint32_t leftTotal = 0;
    uint32_t rightTotal = 0;
    const int leftBlackHeight = verifySubtree(node->left, leftTotal);
    const int rightBlackHeight = verifySubtree(node->right, rightTotal);

    if (leftBlackHeight < 0 || rightBlackHeight < 0) {
        return -1;
    }

    // Black heights must match
    if (leftBlackHeight != rightBlackHeight) {
        return -1;
    }

    // Verify subtree total
    computedTotal = node->size + leftTotal + rightTotal;
    if (node->subtree_total != computedTotal) {
        return -1;
    }

    // Return black height (add 1 if this node is black)
    return leftBlackHeight + (node->color == NodeColor::BLACK ? 1 : 0);
}

}  // namespace cells
