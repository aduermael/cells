#include "core/cells/ostree.h"

#include <algorithm>
#include <random>
#include <vector>

#include "core/cells/id.h"

#include "gtest/gtest.h"

namespace cells {
namespace {

// Test fixture with helper methods
class OSTreeTest : public ::testing::Test {
protected:
    // Helper to generate unique IDs
    ID makeId(int n) {
        return generate_id();
    }

    // Helper to verify tree invariants
    void verifyTree(const OSTree& tree) {
        EXPECT_TRUE(tree.verify()) << "Tree invariants violated";
    }
};

// ============================================================================
// Basic operations
// ============================================================================

TEST_F(OSTreeTest, EmptyTree) {
    OSTree tree;
    EXPECT_EQ(tree.count(), 0u);
    EXPECT_EQ(tree.totalSize(), 0u);
    EXPECT_TRUE(tree.empty());
    EXPECT_EQ(tree.first(), nullptr);
    EXPECT_EQ(tree.last(), nullptr);
    verifyTree(tree);
}

TEST_F(OSTreeTest, AppendSingleNode) {
    OSTree tree;
    ID id = makeId(1);

    OSNode* node = tree.append(id, 100);

    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->id, id);
    EXPECT_EQ(node->size, 100u);
    EXPECT_EQ(node->subtree_total, 100u);
    EXPECT_EQ(tree.count(), 1u);
    EXPECT_EQ(tree.totalSize(), 100u);
    EXPECT_EQ(tree.first(), node);
    EXPECT_EQ(tree.last(), node);
    verifyTree(tree);
}

TEST_F(OSTreeTest, AppendMultipleNodes) {
    OSTree tree;
    std::vector<OSNode*> nodes;

    for (int i = 0; i < 10; i++) {
        OSNode* node = tree.append(makeId(i), static_cast<uint32_t>((i + 1) * 10));
        nodes.push_back(node);
    }

    EXPECT_EQ(tree.count(), 10u);
    // Total: 10+20+30+40+50+60+70+80+90+100 = 550
    EXPECT_EQ(tree.totalSize(), 550u);

    // Verify order
    EXPECT_EQ(tree.first(), nodes[0]);
    EXPECT_EQ(tree.last(), nodes[9]);

    for (size_t i = 0; i < nodes.size() - 1; i++) {
        EXPECT_EQ(OSTree::next(nodes[i]), nodes[i + 1]);
        EXPECT_EQ(OSTree::prev(nodes[i + 1]), nodes[i]);
    }

    verifyTree(tree);
}

TEST_F(OSTreeTest, InsertAtBeginning) {
    OSTree tree;

    // Insert 3 nodes in reverse order at position 0
    ID id1 = makeId(1);
    ID id2 = makeId(2);
    ID id3 = makeId(3);

    tree.insertAt(0, id3, 30);
    tree.insertAt(0, id2, 20);
    tree.insertAt(0, id1, 10);

    EXPECT_EQ(tree.count(), 3u);
    EXPECT_EQ(tree.totalSize(), 60u);

    // Verify order: id1, id2, id3
    OSNode* first = tree.first();
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first->id, id1);
    EXPECT_EQ(first->size, 10u);

    OSNode* second = OSTree::next(first);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(second->id, id2);
    EXPECT_EQ(second->size, 20u);

    OSNode* third = OSTree::next(second);
    ASSERT_NE(third, nullptr);
    EXPECT_EQ(third->id, id3);
    EXPECT_EQ(third->size, 30u);

    verifyTree(tree);
}

TEST_F(OSTreeTest, InsertAtMiddle) {
    OSTree tree;

    ID id1 = makeId(1);
    ID id2 = makeId(2);
    ID id3 = makeId(3);

    tree.append(id1, 10);
    tree.append(id3, 30);
    tree.insertAt(1, id2, 20);  // Insert between id1 and id3

    EXPECT_EQ(tree.count(), 3u);

    // Verify order: id1, id2, id3
    OSNode* first = tree.first();
    OSNode* second = OSTree::next(first);
    OSNode* third = OSTree::next(second);

    EXPECT_EQ(first->id, id1);
    EXPECT_EQ(second->id, id2);
    EXPECT_EQ(third->id, id3);

    verifyTree(tree);
}

TEST_F(OSTreeTest, InsertAtInvalidPosition) {
    OSTree tree;
    tree.append(makeId(1), 10);

    // Position 5 is invalid (only positions 0, 1 are valid)
    OSNode* node = tree.insertAt(5, makeId(2), 20);
    EXPECT_EQ(node, nullptr);
    EXPECT_EQ(tree.count(), 1u);

    verifyTree(tree);
}

// ============================================================================
// Removal operations
// ============================================================================

TEST_F(OSTreeTest, RemoveOnlyNode) {
    OSTree tree;
    ID id = makeId(1);
    tree.append(id, 100);

    EXPECT_TRUE(tree.remove(id));
    EXPECT_EQ(tree.count(), 0u);
    EXPECT_TRUE(tree.empty());
    EXPECT_EQ(tree.totalSize(), 0u);

    verifyTree(tree);
}

TEST_F(OSTreeTest, RemoveFirst) {
    OSTree tree;
    ID id1 = makeId(1);
    ID id2 = makeId(2);
    ID id3 = makeId(3);

    tree.append(id1, 10);
    tree.append(id2, 20);
    tree.append(id3, 30);

    EXPECT_TRUE(tree.remove(id1));
    EXPECT_EQ(tree.count(), 2u);
    EXPECT_EQ(tree.totalSize(), 50u);

    OSNode* first = tree.first();
    EXPECT_EQ(first->id, id2);

    verifyTree(tree);
}

TEST_F(OSTreeTest, RemoveMiddle) {
    OSTree tree;
    ID id1 = makeId(1);
    ID id2 = makeId(2);
    ID id3 = makeId(3);

    tree.append(id1, 10);
    tree.append(id2, 20);
    tree.append(id3, 30);

    EXPECT_TRUE(tree.remove(id2));
    EXPECT_EQ(tree.count(), 2u);
    EXPECT_EQ(tree.totalSize(), 40u);

    OSNode* first = tree.first();
    OSNode* second = OSTree::next(first);

    EXPECT_EQ(first->id, id1);
    EXPECT_EQ(second->id, id3);

    verifyTree(tree);
}

TEST_F(OSTreeTest, RemoveLast) {
    OSTree tree;
    ID id1 = makeId(1);
    ID id2 = makeId(2);
    ID id3 = makeId(3);

    tree.append(id1, 10);
    tree.append(id2, 20);
    tree.append(id3, 30);

    EXPECT_TRUE(tree.remove(id3));
    EXPECT_EQ(tree.count(), 2u);
    EXPECT_EQ(tree.totalSize(), 30u);

    OSNode* last = tree.last();
    EXPECT_EQ(last->id, id2);

    verifyTree(tree);
}

TEST_F(OSTreeTest, RemoveNonexistent) {
    OSTree tree;
    tree.append(makeId(1), 10);

    EXPECT_FALSE(tree.remove(makeId(999)));
    EXPECT_EQ(tree.count(), 1u);

    verifyTree(tree);
}

// ============================================================================
// Find operations
// ============================================================================

TEST_F(OSTreeTest, FindById) {
    OSTree tree;
    ID id1 = makeId(1);
    ID id2 = makeId(2);

    OSNode* node1 = tree.append(id1, 10);
    tree.append(id2, 20);

    OSNode* found = tree.find(id1);
    EXPECT_EQ(found, node1);

    OSNode* notFound = tree.find(makeId(999));
    EXPECT_EQ(notFound, nullptr);
}

TEST_F(OSTreeTest, FindByPosition) {
    OSTree tree;
    std::vector<OSNode*> nodes;

    for (int i = 0; i < 5; i++) {
        nodes.push_back(tree.append(makeId(i), static_cast<uint32_t>(i * 10)));
    }

    for (size_t i = 0; i < nodes.size(); i++) {
        EXPECT_EQ(tree.at(i), nodes[i]) << "Position " << i;
    }

    EXPECT_EQ(tree.at(100), nullptr);  // Out of range
}

// ============================================================================
// findByOffset tests (core OS tree functionality)
// ============================================================================

TEST_F(OSTreeTest, FindByOffsetSimple) {
    OSTree tree;

    // Create nodes: [100, 50, 75] pixels
    tree.append(makeId(1), 100);
    tree.append(makeId(2), 50);
    tree.append(makeId(3), 75);

    // Offset 0 -> first node, offset 0 within
    FindResult r0 = tree.findByOffset(0);
    EXPECT_TRUE(r0.found());
    EXPECT_EQ(r0.node, tree.first());
    EXPECT_EQ(r0.offsetInNode, 0u);

    // Offset 50 -> first node, offset 50 within
    FindResult r50 = tree.findByOffset(50);
    EXPECT_TRUE(r50.found());
    EXPECT_EQ(r50.node, tree.first());
    EXPECT_EQ(r50.offsetInNode, 50u);

    // Offset 99 -> first node, offset 99 within
    FindResult r99 = tree.findByOffset(99);
    EXPECT_TRUE(r99.found());
    EXPECT_EQ(r99.node, tree.first());
    EXPECT_EQ(r99.offsetInNode, 99u);

    // Offset 100 -> second node, offset 0 within
    FindResult r100 = tree.findByOffset(100);
    EXPECT_TRUE(r100.found());
    EXPECT_EQ(r100.node, tree.at(1));
    EXPECT_EQ(r100.offsetInNode, 0u);

    // Offset 120 -> second node, offset 20 within
    FindResult r120 = tree.findByOffset(120);
    EXPECT_TRUE(r120.found());
    EXPECT_EQ(r120.node, tree.at(1));
    EXPECT_EQ(r120.offsetInNode, 20u);

    // Offset 150 -> third node, offset 0 within
    FindResult r150 = tree.findByOffset(150);
    EXPECT_TRUE(r150.found());
    EXPECT_EQ(r150.node, tree.at(2));
    EXPECT_EQ(r150.offsetInNode, 0u);

    // Offset 200 -> third node, offset 50 within
    FindResult r200 = tree.findByOffset(200);
    EXPECT_TRUE(r200.found());
    EXPECT_EQ(r200.node, tree.at(2));
    EXPECT_EQ(r200.offsetInNode, 50u);

    // Offset 224 -> third node, offset 74 within (last valid)
    FindResult r224 = tree.findByOffset(224);
    EXPECT_TRUE(r224.found());
    EXPECT_EQ(r224.node, tree.at(2));
    EXPECT_EQ(r224.offsetInNode, 74u);

    // Offset 225 -> out of range (total is 225)
    FindResult r225 = tree.findByOffset(225);
    EXPECT_FALSE(r225.found());
}

TEST_F(OSTreeTest, FindByOffsetManyNodes) {
    OSTree tree;

    // Create 100 nodes, each 10 pixels wide
    for (int i = 0; i < 100; i++) {
        tree.append(makeId(i), 10);
    }

    EXPECT_EQ(tree.totalSize(), 1000u);

    // Test various offsets
    for (uint32_t offset = 0; offset < 1000; offset += 37) {
        FindResult r = tree.findByOffset(offset);
        EXPECT_TRUE(r.found()) << "Offset " << offset;

        size_t expectedNode = offset / 10;
        uint32_t expectedOffsetInNode = offset % 10;

        EXPECT_EQ(tree.getPosition(r.node), expectedNode) << "Offset " << offset;
        EXPECT_EQ(r.offsetInNode, expectedOffsetInNode) << "Offset " << offset;
    }
}

TEST_F(OSTreeTest, FindByOffsetEmptyTree) {
    OSTree tree;
    FindResult r = tree.findByOffset(0);
    EXPECT_FALSE(r.found());
}

// ============================================================================
// getOffset tests
// ============================================================================

TEST_F(OSTreeTest, GetOffset) {
    OSTree tree;

    // Create nodes: [100, 50, 75, 25] pixels
    tree.append(makeId(1), 100);
    tree.append(makeId(2), 50);
    tree.append(makeId(3), 75);
    tree.append(makeId(4), 25);

    EXPECT_EQ(tree.getOffset(tree.at(0)), 0u);
    EXPECT_EQ(tree.getOffset(tree.at(1)), 100u);
    EXPECT_EQ(tree.getOffset(tree.at(2)), 150u);
    EXPECT_EQ(tree.getOffset(tree.at(3)), 225u);
}

TEST_F(OSTreeTest, GetOffsetManyNodes) {
    OSTree tree;

    uint32_t expectedOffset = 0;
    for (int i = 0; i < 50; i++) {
        uint32_t size = static_cast<uint32_t>((i + 1) * 10);
        tree.append(makeId(i), size);
    }

    expectedOffset = 0;
    for (size_t i = 0; i < 50; i++) {
        OSNode* node = tree.at(i);
        EXPECT_EQ(tree.getOffset(node), expectedOffset) << "Node " << i;
        expectedOffset += node->size;
    }
}

// ============================================================================
// getPosition tests
// ============================================================================

TEST_F(OSTreeTest, GetPosition) {
    OSTree tree;

    std::vector<OSNode*> nodes;
    for (int i = 0; i < 20; i++) {
        nodes.push_back(tree.append(makeId(i), 10));
    }

    for (size_t i = 0; i < nodes.size(); i++) {
        EXPECT_EQ(tree.getPosition(nodes[i]), i);
    }
}

// ============================================================================
// updateSize tests
// ============================================================================

TEST_F(OSTreeTest, UpdateSize) {
    OSTree tree;

    tree.append(makeId(1), 100);
    tree.append(makeId(2), 50);
    tree.append(makeId(3), 75);

    EXPECT_EQ(tree.totalSize(), 225u);

    // Resize second node from 50 to 200
    OSNode* node = tree.at(1);
    tree.updateSize(node, 200);

    EXPECT_EQ(node->size, 200u);
    EXPECT_EQ(tree.totalSize(), 375u);

    // Verify getOffset still works
    EXPECT_EQ(tree.getOffset(tree.at(0)), 0u);
    EXPECT_EQ(tree.getOffset(tree.at(1)), 100u);
    EXPECT_EQ(tree.getOffset(tree.at(2)), 300u);

    verifyTree(tree);
}

TEST_F(OSTreeTest, UpdateSizeToZero) {
    OSTree tree;

    tree.append(makeId(1), 100);
    tree.append(makeId(2), 50);

    tree.updateSize(tree.at(0), 0);

    EXPECT_EQ(tree.totalSize(), 50u);
    EXPECT_EQ(tree.getOffset(tree.at(0)), 0u);
    EXPECT_EQ(tree.getOffset(tree.at(1)), 0u);

    verifyTree(tree);
}

// ============================================================================
// Move tests
// ============================================================================

TEST_F(OSTreeTest, MoveForward) {
    OSTree tree;

    ID id1 = makeId(1);
    ID id2 = makeId(2);
    ID id3 = makeId(3);
    ID id4 = makeId(4);

    tree.append(id1, 10);
    tree.append(id2, 20);
    tree.append(id3, 30);
    tree.append(id4, 40);

    // Move first node to position 2 (after id3)
    OSNode* node = tree.find(id1);
    EXPECT_TRUE(tree.move(node, 2));

    // New order: id2, id3, id1, id4
    EXPECT_EQ(tree.at(0)->id, id2);
    EXPECT_EQ(tree.at(1)->id, id3);
    EXPECT_EQ(tree.at(2)->id, id1);
    EXPECT_EQ(tree.at(3)->id, id4);

    EXPECT_EQ(tree.totalSize(), 100u);
    verifyTree(tree);
}

TEST_F(OSTreeTest, MoveBackward) {
    OSTree tree;

    ID id1 = makeId(1);
    ID id2 = makeId(2);
    ID id3 = makeId(3);
    ID id4 = makeId(4);

    tree.append(id1, 10);
    tree.append(id2, 20);
    tree.append(id3, 30);
    tree.append(id4, 40);

    // Move last node to position 1 (before id2)
    OSNode* node = tree.find(id4);
    EXPECT_TRUE(tree.move(node, 1));

    // New order: id1, id4, id2, id3
    EXPECT_EQ(tree.at(0)->id, id1);
    EXPECT_EQ(tree.at(1)->id, id4);
    EXPECT_EQ(tree.at(2)->id, id2);
    EXPECT_EQ(tree.at(3)->id, id3);

    verifyTree(tree);
}

TEST_F(OSTreeTest, MoveToSamePosition) {
    OSTree tree;

    ID id1 = makeId(1);
    ID id2 = makeId(2);

    tree.append(id1, 10);
    tree.append(id2, 20);

    OSNode* node = tree.find(id1);
    EXPECT_TRUE(tree.move(node, 0));  // Already at position 0

    EXPECT_EQ(tree.at(0)->id, id1);
    EXPECT_EQ(tree.at(1)->id, id2);

    verifyTree(tree);
}

// ============================================================================
// Iteration tests
// ============================================================================

TEST_F(OSTreeTest, ForEach) {
    OSTree tree;

    for (int i = 0; i < 10; i++) {
        tree.append(makeId(i), static_cast<uint32_t>(i * 10));
    }

    std::vector<size_t> positions;
    std::vector<uint32_t> sizes;

    tree.forEach([&](OSNode* node, size_t pos) {
        positions.push_back(pos);
        sizes.push_back(node->size);
    });

    EXPECT_EQ(positions.size(), 10u);
    for (size_t i = 0; i < 10; i++) {
        EXPECT_EQ(positions[i], i);
        EXPECT_EQ(sizes[i], static_cast<uint32_t>(i * 10));
    }
}

TEST_F(OSTreeTest, NextPrev) {
    OSTree tree;

    tree.append(makeId(1), 10);
    tree.append(makeId(2), 20);
    tree.append(makeId(3), 30);

    OSNode* first = tree.first();
    OSNode* second = OSTree::next(first);
    OSNode* third = OSTree::next(second);

    EXPECT_EQ(OSTree::prev(first), nullptr);
    EXPECT_EQ(OSTree::next(first), second);
    EXPECT_EQ(OSTree::prev(second), first);
    EXPECT_EQ(OSTree::next(second), third);
    EXPECT_EQ(OSTree::prev(third), second);
    EXPECT_EQ(OSTree::next(third), nullptr);
}

// ============================================================================
// Clear test
// ============================================================================

TEST_F(OSTreeTest, Clear) {
    OSTree tree;

    for (int i = 0; i < 100; i++) {
        tree.append(makeId(i), 10);
    }

    EXPECT_EQ(tree.count(), 100u);

    tree.clear();

    EXPECT_EQ(tree.count(), 0u);
    EXPECT_TRUE(tree.empty());
    EXPECT_EQ(tree.totalSize(), 0u);
    EXPECT_EQ(tree.first(), nullptr);

    verifyTree(tree);
}

// ============================================================================
// Stress tests
// ============================================================================

TEST_F(OSTreeTest, ManyInsertions) {
    OSTree tree;

    // Insert 1000 nodes
    for (int i = 0; i < 1000; i++) {
        tree.append(makeId(i), 100);
    }

    EXPECT_EQ(tree.count(), 1000u);
    EXPECT_EQ(tree.totalSize(), 100000u);
    verifyTree(tree);
}

TEST_F(OSTreeTest, ManyDeletions) {
    OSTree tree;
    std::vector<ID> ids;

    // Insert 100 nodes
    for (int i = 0; i < 100; i++) {
        ID id = makeId(i);
        ids.push_back(id);
        tree.append(id, 10);
    }

    // Remove them all in random order
    std::mt19937 rng(42);
    std::shuffle(ids.begin(), ids.end(), rng);

    for (const ID& id : ids) {
        EXPECT_TRUE(tree.remove(id));
        verifyTree(tree);
    }

    EXPECT_TRUE(tree.empty());
}

TEST_F(OSTreeTest, RandomOperations) {
    OSTree tree;
    std::vector<ID> ids;
    std::mt19937 rng(12345);

    for (int i = 0; i < 500; i++) {
        int op = std::uniform_int_distribution<>(0, 2)(rng);

        if (op == 0 || ids.empty()) {
            // Insert
            ID id = makeId(i);
            ids.push_back(id);
            uint32_t size = std::uniform_int_distribution<uint32_t>(1, 1000)(rng);
            tree.append(id, size);
        } else if (op == 1 && !ids.empty()) {
            // Remove random
            size_t idx = std::uniform_int_distribution<size_t>(0, ids.size() - 1)(rng);
            EXPECT_TRUE(tree.remove(ids[idx]));
            ids.erase(ids.begin() + static_cast<std::ptrdiff_t>(idx));
        } else if (!ids.empty()) {
            // Update size
            size_t idx = std::uniform_int_distribution<size_t>(0, ids.size() - 1)(rng);
            OSNode* node = tree.find(ids[idx]);
            if (node != nullptr) {
                uint32_t newSize = std::uniform_int_distribution<uint32_t>(1, 1000)(rng);
                tree.updateSize(node, newSize);
            }
        }

        verifyTree(tree);
    }

    EXPECT_EQ(tree.count(), ids.size());
}

// ============================================================================
// Move semantics tests
// ============================================================================

TEST_F(OSTreeTest, MoveConstructor) {
    OSTree tree1;
    tree1.append(makeId(1), 100);
    tree1.append(makeId(2), 200);

    OSTree tree2(std::move(tree1));

    EXPECT_EQ(tree2.count(), 2u);
    EXPECT_EQ(tree2.totalSize(), 300u);
    EXPECT_TRUE(tree1.empty());  // NOLINT (use after move is intentional)
}

TEST_F(OSTreeTest, MoveAssignment) {
    OSTree tree1;
    tree1.append(makeId(1), 100);

    OSTree tree2;
    tree2.append(makeId(2), 200);
    tree2.append(makeId(3), 300);

    tree2 = std::move(tree1);

    EXPECT_EQ(tree2.count(), 1u);
    EXPECT_EQ(tree2.totalSize(), 100u);
}

// ============================================================================
// Edge cases
// ============================================================================

TEST_F(OSTreeTest, ZeroSizeNodes) {
    OSTree tree;

    tree.append(makeId(1), 0);
    tree.append(makeId(2), 100);
    tree.append(makeId(3), 0);

    EXPECT_EQ(tree.count(), 3u);
    EXPECT_EQ(tree.totalSize(), 100u);

    // Find by offset should handle zero-size nodes correctly
    FindResult r0 = tree.findByOffset(0);
    EXPECT_TRUE(r0.found());
    EXPECT_EQ(r0.node, tree.at(1));  // Skip zero-size first node
    EXPECT_EQ(r0.offsetInNode, 0u);

    verifyTree(tree);
}

TEST_F(OSTreeTest, LargeSizes) {
    OSTree tree;

    // Use sizes that might overflow 16-bit
    tree.append(makeId(1), 100000);
    tree.append(makeId(2), 200000);
    tree.append(makeId(3), 300000);

    EXPECT_EQ(tree.totalSize(), 600000u);

    FindResult r = tree.findByOffset(350000);
    EXPECT_TRUE(r.found());
    EXPECT_EQ(r.node, tree.at(2));
    EXPECT_EQ(r.offsetInNode, 50000u);

    verifyTree(tree);
}

}  // namespace
}  // namespace cells
