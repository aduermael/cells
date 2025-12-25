#include "core/cells/rtree.h"

#include <algorithm>
#include <random>
#include <set>
#include <string>

#include "gtest/gtest.h"

namespace cells {
namespace {

// ============================================================================
// BoundingRect Tests
// ============================================================================

TEST(BoundingRectTest, DefaultConstructor) {
    BoundingRect r;
    EXPECT_EQ(r.minCol, 0);
    EXPECT_EQ(r.minRow, 0);
    EXPECT_EQ(r.maxCol, 0);
    EXPECT_EQ(r.maxRow, 0);
}

TEST(BoundingRectTest, ParameterizedConstructor) {
    BoundingRect r(1, 2, 10, 20);
    EXPECT_EQ(r.minCol, 1);
    EXPECT_EQ(r.minRow, 2);
    EXPECT_EQ(r.maxCol, 10);
    EXPECT_EQ(r.maxRow, 20);
}

TEST(BoundingRectTest, Point) {
    auto r = BoundingRect::point(5, 10);
    EXPECT_EQ(r.minCol, 5);
    EXPECT_EQ(r.minRow, 10);
    EXPECT_EQ(r.maxCol, 5);
    EXPECT_EQ(r.maxRow, 10);
}

TEST(BoundingRectTest, WholeColumn) {
    auto r = BoundingRect::wholeColumn(3);
    EXPECT_EQ(r.minCol, 3);
    EXPECT_EQ(r.minRow, 0);
    EXPECT_EQ(r.maxCol, 3);
    EXPECT_EQ(r.maxRow, BoundingRect::MAX_COORD);
}

TEST(BoundingRectTest, ColumnRange) {
    auto r = BoundingRect::columnRange(2, 5);
    EXPECT_EQ(r.minCol, 2);
    EXPECT_EQ(r.minRow, 0);
    EXPECT_EQ(r.maxCol, 5);
    EXPECT_EQ(r.maxRow, BoundingRect::MAX_COORD);
}

TEST(BoundingRectTest, WholeRow) {
    auto r = BoundingRect::wholeRow(7);
    EXPECT_EQ(r.minCol, 0);
    EXPECT_EQ(r.minRow, 7);
    EXPECT_EQ(r.maxCol, BoundingRect::MAX_COORD);
    EXPECT_EQ(r.maxRow, 7);
}

TEST(BoundingRectTest, RowRange) {
    auto r = BoundingRect::rowRange(3, 10);
    EXPECT_EQ(r.minCol, 0);
    EXPECT_EQ(r.minRow, 3);
    EXPECT_EQ(r.maxCol, BoundingRect::MAX_COORD);
    EXPECT_EQ(r.maxRow, 10);
}

TEST(BoundingRectTest, ContainsPoint) {
    BoundingRect r(0, 0, 10, 10);
    EXPECT_TRUE(r.containsPoint(0, 0));
    EXPECT_TRUE(r.containsPoint(5, 5));
    EXPECT_TRUE(r.containsPoint(10, 10));
    EXPECT_FALSE(r.containsPoint(-1, 0));
    EXPECT_FALSE(r.containsPoint(0, -1));
    EXPECT_FALSE(r.containsPoint(11, 5));
    EXPECT_FALSE(r.containsPoint(5, 11));
}

TEST(BoundingRectTest, WholeColumnContainsAnyRow) {
    auto r = BoundingRect::wholeColumn(5);
    EXPECT_TRUE(r.containsPoint(5, 0));
    EXPECT_TRUE(r.containsPoint(5, 1000000));
    EXPECT_TRUE(r.containsPoint(5, BoundingRect::MAX_COORD));
    EXPECT_FALSE(r.containsPoint(4, 0));
    EXPECT_FALSE(r.containsPoint(6, 0));
}

TEST(BoundingRectTest, WholeRowContainsAnyColumn) {
    auto r = BoundingRect::wholeRow(5);
    EXPECT_TRUE(r.containsPoint(0, 5));
    EXPECT_TRUE(r.containsPoint(1000000, 5));
    EXPECT_TRUE(r.containsPoint(BoundingRect::MAX_COORD, 5));
    EXPECT_FALSE(r.containsPoint(0, 4));
    EXPECT_FALSE(r.containsPoint(0, 6));
}

TEST(BoundingRectTest, Intersects) {
    BoundingRect r1(0, 0, 10, 10);
    BoundingRect r2(5, 5, 15, 15);
    BoundingRect r3(20, 20, 30, 30);
    BoundingRect r4(10, 10, 20, 20);

    EXPECT_TRUE(r1.intersects(r2));  // Overlap
    EXPECT_TRUE(r2.intersects(r1));
    EXPECT_FALSE(r1.intersects(r3));  // No overlap
    EXPECT_FALSE(r3.intersects(r1));
    EXPECT_TRUE(r1.intersects(r4));  // Corner touch
    EXPECT_TRUE(r4.intersects(r1));
}

TEST(BoundingRectTest, Contains) {
    BoundingRect outer(0, 0, 100, 100);
    BoundingRect inner(10, 10, 20, 20);
    BoundingRect partial(50, 50, 150, 150);

    EXPECT_TRUE(outer.contains(inner));
    EXPECT_FALSE(inner.contains(outer));
    EXPECT_FALSE(outer.contains(partial));
}

TEST(BoundingRectTest, Area) {
    BoundingRect r1(0, 0, 9, 9);
    EXPECT_DOUBLE_EQ(r1.area(), 100.0);  // 10x10

    BoundingRect r2(0, 0, 0, 0);
    EXPECT_DOUBLE_EQ(r2.area(), 1.0);  // 1x1 point
}

TEST(BoundingRectTest, Expand) {
    BoundingRect r1(5, 5, 10, 10);
    BoundingRect r2(0, 0, 7, 7);
    r1.expand(r2);
    EXPECT_EQ(r1.minCol, 0);
    EXPECT_EQ(r1.minRow, 0);
    EXPECT_EQ(r1.maxCol, 10);
    EXPECT_EQ(r1.maxRow, 10);
}

TEST(BoundingRectTest, Equality) {
    BoundingRect r1(1, 2, 3, 4);
    BoundingRect r2(1, 2, 3, 4);
    BoundingRect r3(1, 2, 3, 5);
    EXPECT_EQ(r1, r2);
    EXPECT_FALSE(r1 == r3);
}

// ============================================================================
// RTree Basic Tests
// ============================================================================

TEST(RTreeTest, EmptyTree) {
    RTree<std::string> tree;
    EXPECT_TRUE(tree.empty());
    EXPECT_EQ(tree.size(), 0u);
}

TEST(RTreeTest, InsertAndQuery) {
    RTree<std::string> tree;
    tree.insert(0, 0, 10, 10, "rect1");
    EXPECT_EQ(tree.size(), 1u);
    EXPECT_FALSE(tree.empty());

    auto results = tree.query(5, 5);
    EXPECT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0], "rect1");
}

TEST(RTreeTest, QueryOutsideRect) {
    RTree<std::string> tree;
    tree.insert(0, 0, 10, 10, "rect1");

    auto results = tree.query(20, 20);
    EXPECT_TRUE(results.empty());
}

TEST(RTreeTest, MultipleInserts) {
    RTree<std::string> tree;
    tree.insert(0, 0, 10, 10, "rect1");
    tree.insert(5, 5, 15, 15, "rect2");
    tree.insert(20, 20, 30, 30, "rect3");
    EXPECT_EQ(tree.size(), 3u);

    // Query point in rect1 only
    auto results1 = tree.query(2, 2);
    EXPECT_EQ(results1.size(), 1u);
    EXPECT_EQ(results1[0], "rect1");

    // Query point in overlap of rect1 and rect2
    auto results2 = tree.query(7, 7);
    EXPECT_EQ(results2.size(), 2u);

    // Query point in rect3 only
    auto results3 = tree.query(25, 25);
    EXPECT_EQ(results3.size(), 1u);
    EXPECT_EQ(results3[0], "rect3");
}

TEST(RTreeTest, PointInsertions) {
    RTree<int> tree;
    tree.insert(BoundingRect::point(0, 0), 0);
    tree.insert(BoundingRect::point(10, 10), 1);
    tree.insert(BoundingRect::point(20, 20), 2);

    EXPECT_EQ(tree.query(0, 0).size(), 1u);
    EXPECT_EQ(tree.query(10, 10).size(), 1u);
    EXPECT_EQ(tree.query(20, 20).size(), 1u);
    EXPECT_TRUE(tree.query(5, 5).empty());
}

// ============================================================================
// Range Query Tests
// ============================================================================

TEST(RTreeTest, RangeQuery) {
    RTree<std::string> tree;
    tree.insert(0, 0, 10, 10, "rect1");
    tree.insert(5, 5, 15, 15, "rect2");
    tree.insert(20, 20, 30, 30, "rect3");

    // Range that hits rect1 and rect2
    auto results = tree.queryRange(0, 0, 10, 10);
    EXPECT_EQ(results.size(), 2u);

    // Range that hits only rect3
    auto results2 = tree.queryRange(21, 21, 29, 29);
    EXPECT_EQ(results2.size(), 1u);
    EXPECT_EQ(results2[0], "rect3");

    // Range that misses all
    auto results3 = tree.queryRange(100, 100, 200, 200);
    EXPECT_TRUE(results3.empty());
}

// ============================================================================
// Removal Tests
// ============================================================================

TEST(RTreeTest, RemoveExisting) {
    RTree<std::string> tree;
    tree.insert(0, 0, 10, 10, "rect1");
    tree.insert(5, 5, 15, 15, "rect2");

    EXPECT_EQ(tree.size(), 2u);

    bool removed = tree.remove(0, 0, 10, 10, "rect1");
    EXPECT_TRUE(removed);
    EXPECT_EQ(tree.size(), 1u);

    // rect1 should no longer be found
    auto results = tree.query(2, 2);
    EXPECT_TRUE(results.empty());

    // rect2 should still be there
    auto results2 = tree.query(10, 10);
    EXPECT_EQ(results2.size(), 1u);
    EXPECT_EQ(results2[0], "rect2");
}

TEST(RTreeTest, RemoveNonexistent) {
    RTree<std::string> tree;
    tree.insert(0, 0, 10, 10, "rect1");

    bool removed = tree.remove(0, 0, 10, 10, "nonexistent");
    EXPECT_FALSE(removed);
    EXPECT_EQ(tree.size(), 1u);

    // Wrong bounds
    removed = tree.remove(1, 1, 10, 10, "rect1");
    EXPECT_FALSE(removed);
    EXPECT_EQ(tree.size(), 1u);
}

TEST(RTreeTest, RemoveFromEmpty) {
    RTree<std::string> tree;
    bool removed = tree.remove(0, 0, 10, 10, "anything");
    EXPECT_FALSE(removed);
}

TEST(RTreeTest, Clear) {
    RTree<std::string> tree;
    tree.insert(0, 0, 10, 10, "rect1");
    tree.insert(5, 5, 15, 15, "rect2");
    EXPECT_EQ(tree.size(), 2u);

    tree.clear();
    EXPECT_TRUE(tree.empty());
    EXPECT_EQ(tree.size(), 0u);
}

// ============================================================================
// Whole Column/Row Tests
// ============================================================================

TEST(RTreeTest, WholeColumnQuery) {
    RTree<std::string> tree;
    tree.insert(BoundingRect::wholeColumn(5), "colA");

    // Any row in column 5 should match
    EXPECT_EQ(tree.query(5, 0).size(), 1u);
    EXPECT_EQ(tree.query(5, 1000).size(), 1u);
    EXPECT_EQ(tree.query(5, 1000000).size(), 1u);

    // Other columns should not match
    EXPECT_TRUE(tree.query(4, 0).empty());
    EXPECT_TRUE(tree.query(6, 0).empty());
}

TEST(RTreeTest, WholeRowQuery) {
    RTree<std::string> tree;
    tree.insert(BoundingRect::wholeRow(10), "row1");

    // Any column in row 10 should match
    EXPECT_EQ(tree.query(0, 10).size(), 1u);
    EXPECT_EQ(tree.query(1000, 10).size(), 1u);
    EXPECT_EQ(tree.query(1000000, 10).size(), 1u);

    // Other rows should not match
    EXPECT_TRUE(tree.query(0, 9).empty());
    EXPECT_TRUE(tree.query(0, 11).empty());
}

TEST(RTreeTest, ColumnRangeQuery) {
    RTree<std::string> tree;
    tree.insert(BoundingRect::columnRange(2, 5), "cols");

    EXPECT_EQ(tree.query(2, 0).size(), 1u);
    EXPECT_EQ(tree.query(3, 100).size(), 1u);
    EXPECT_EQ(tree.query(5, 1000000).size(), 1u);

    EXPECT_TRUE(tree.query(1, 0).empty());
    EXPECT_TRUE(tree.query(6, 0).empty());
}

TEST(RTreeTest, RowRangeQuery) {
    RTree<std::string> tree;
    tree.insert(BoundingRect::rowRange(10, 20), "rows");

    EXPECT_EQ(tree.query(0, 10).size(), 1u);
    EXPECT_EQ(tree.query(1000, 15).size(), 1u);
    EXPECT_EQ(tree.query(0, 20).size(), 1u);

    EXPECT_TRUE(tree.query(0, 9).empty());
    EXPECT_TRUE(tree.query(0, 21).empty());
}

// ============================================================================
// Large Scale Tests
// ============================================================================

TEST(RTreeTest, ManyInsertions) {
    RTree<int> tree;
    const int count = 1000;

    for (int i = 0; i < count; ++i) {
        tree.insert(i, i, i + 10, i + 10, i);
    }

    EXPECT_EQ(tree.size(), static_cast<size_t>(count));

    // Verify queries still work
    for (int i = 0; i < count; ++i) {
        auto results = tree.query(i + 5, i + 5);
        EXPECT_GE(results.size(), 1u);
        bool found = false;
        for (int v : results) {
            if (v == i) {
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found) << "Expected to find " << i;
    }
}

TEST(RTreeTest, RandomInsertionsAndQueries) {
    RTree<int> tree;
    std::mt19937 gen(42);  // Fixed seed for reproducibility
    std::uniform_int_distribution<int32_t> dist(0, 10000);

    std::vector<std::pair<BoundingRect, int>> entries;
    const int count = 500;

    for (int i = 0; i < count; ++i) {
        int32_t x1 = dist(gen);
        int32_t y1 = dist(gen);
        int32_t x2 = x1 + (dist(gen) % 100);
        int32_t y2 = y1 + (dist(gen) % 100);
        BoundingRect bounds(x1, y1, x2, y2);
        tree.insert(bounds, i);
        entries.emplace_back(bounds, i);
    }

    EXPECT_EQ(tree.size(), static_cast<size_t>(count));

    // Verify all entries can be found
    for (const auto& [bounds, value] : entries) {
        int32_t midX = (bounds.minCol + bounds.maxCol) / 2;
        int32_t midY = (bounds.minRow + bounds.maxRow) / 2;
        auto results = tree.query(midX, midY);

        bool found = false;
        for (int v : results) {
            if (v == value) {
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found) << "Expected to find " << value << " at (" << midX << ", " << midY
                           << ")";
    }
}

TEST(RTreeTest, SparseCoordinates) {
    RTree<std::string> tree;

    // Insert at very sparse locations
    tree.insert(0, 0, 10, 10, "origin");
    tree.insert(1000000, 0, 1000010, 10, "far_right");
    tree.insert(0, 1000000, 10, 1000010, "far_down");
    tree.insert(1000000, 1000000, 1000010, 1000010, "far_corner");

    EXPECT_EQ(tree.size(), 4u);

    EXPECT_EQ(tree.query(5, 5)[0], "origin");
    EXPECT_EQ(tree.query(1000005, 5)[0], "far_right");
    EXPECT_EQ(tree.query(5, 1000005)[0], "far_down");
    EXPECT_EQ(tree.query(1000005, 1000005)[0], "far_corner");

    EXPECT_TRUE(tree.query(500000, 500000).empty());
}

TEST(RTreeTest, ManyRemovalsAndReinsertions) {
    RTree<int> tree;
    const int count = 100;

    // Insert entries
    for (int i = 0; i < count; ++i) {
        tree.insert(i * 10, i * 10, i * 10 + 9, i * 10 + 9, i);
    }
    EXPECT_EQ(tree.size(), static_cast<size_t>(count));

    // Remove half
    for (int i = 0; i < count; i += 2) {
        bool removed = tree.remove(i * 10, i * 10, i * 10 + 9, i * 10 + 9, i);
        EXPECT_TRUE(removed);
    }
    EXPECT_EQ(tree.size(), static_cast<size_t>(count / 2));

    // Verify removed entries are gone
    for (int i = 0; i < count; i += 2) {
        auto results = tree.query(i * 10 + 5, i * 10 + 5);
        for (int v : results) {
            EXPECT_NE(v, i) << "Found removed entry " << i;
        }
    }

    // Verify remaining entries are still there
    for (int i = 1; i < count; i += 2) {
        auto results = tree.query(i * 10 + 5, i * 10 + 5);
        bool found = false;
        for (int v : results) {
            if (v == i) {
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found) << "Missing entry " << i;
    }
}

// ============================================================================
// ForEach Tests
// ============================================================================

TEST(RTreeTest, ForEach) {
    RTree<std::string> tree;
    tree.insert(0, 0, 10, 10, "a");
    tree.insert(5, 5, 15, 15, "b");
    tree.insert(20, 20, 30, 30, "c");

    std::set<std::string> seen;
    tree.forEach(
        [&](const BoundingRect& /*bounds*/, const std::string& value) { seen.insert(value); });

    EXPECT_EQ(seen.size(), 3u);
    EXPECT_TRUE(seen.count("a"));
    EXPECT_TRUE(seen.count("b"));
    EXPECT_TRUE(seen.count("c"));
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST(RTreeTest, DuplicateEntries) {
    RTree<std::string> tree;
    tree.insert(0, 0, 10, 10, "same");
    tree.insert(0, 0, 10, 10, "same");

    EXPECT_EQ(tree.size(), 2u);

    auto results = tree.query(5, 5);
    EXPECT_EQ(results.size(), 2u);

    // Remove one
    tree.remove(0, 0, 10, 10, "same");
    EXPECT_EQ(tree.size(), 1u);
    EXPECT_EQ(tree.query(5, 5).size(), 1u);
}

TEST(RTreeTest, ZeroAreaRect) {
    RTree<std::string> tree;
    tree.insert(5, 5, 5, 5, "point");

    EXPECT_EQ(tree.query(5, 5).size(), 1u);
    EXPECT_TRUE(tree.query(4, 5).empty());
    EXPECT_TRUE(tree.query(6, 5).empty());
}

TEST(RTreeTest, DifferentValueTypes) {
    // Test with int
    RTree<int> intTree;
    intTree.insert(0, 0, 10, 10, 42);
    EXPECT_EQ(intTree.query(5, 5)[0], 42);

    // Test with ID-like struct
    struct SimpleId {
        int id;
        bool operator==(const SimpleId& other) const { return id == other.id; }
    };
    RTree<SimpleId> idTree;
    idTree.insert(0, 0, 10, 10, {123});
    EXPECT_EQ(idTree.query(5, 5)[0].id, 123);
}

}  // namespace
}  // namespace cells
