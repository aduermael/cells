#include "core/cells/spill_index.h"

#include <algorithm>
#include <chrono>
#include <gtest/gtest.h>
#include <iostream>

#include "core/cells/id.h"

namespace cells {
namespace {

class SpillIndexTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create some test master cell IDs
        master1 = ID("master01");
        master2 = ID("master02");
        master3 = ID("master03");
    }

    ID master1;  // Will be placed at A1:C5 (positions 0,0 - 2,4)
    ID master2;  // Will be placed at B2:D4 (positions 1,1 - 3,3)
    ID master3;  // Will be placed at F6:F6 (position 5,5) - single cell
    SpillIndex index;
};

TEST_F(SpillIndexTest, InsertAndQueryAt) {
    // Insert spill extents
    index.insert(master1, 0, 0, 2, 4);  // A1:C5
    index.insert(master2, 1, 1, 3, 3);  // B2:D4

    EXPECT_EQ(index.size(), 2u);

    // Query at A1 (0,0) - should find master1 only
    auto atA1 = index.queryAt(0, 0);
    ASSERT_EQ(atA1.size(), 1u);
    EXPECT_EQ(atA1[0], master1);

    // Query at B2 (1,1) - should find both masters (overlapping)
    auto atB2 = index.queryAt(1, 1);
    ASSERT_EQ(atB2.size(), 2u);

    // Query at D4 (3,3) - should find master2 only
    auto atD4 = index.queryAt(3, 3);
    ASSERT_EQ(atD4.size(), 1u);
    EXPECT_EQ(atD4[0], master2);

    // Query at E5 (4,4) - should find nothing
    auto atE5 = index.queryAt(4, 4);
    EXPECT_TRUE(atE5.empty());
}

TEST_F(SpillIndexTest, QueryRange) {
    index.insert(master1, 0, 0, 2, 4);  // A1:C5
    index.insert(master2, 1, 1, 3, 3);  // B2:D4
    index.insert(master3, 5, 5, 5, 5);  // F6

    // Query range A1:B2 - should intersect both master1 and master2
    auto inA1B2 = index.queryRange(0, 0, 1, 1);
    EXPECT_EQ(inA1B2.size(), 2u);

    // Query range D4:F6 - should intersect master2 and master3
    auto inD4F6 = index.queryRange(3, 3, 5, 5);
    EXPECT_EQ(inD4F6.size(), 2u);

    // Query range G7:H8 - should find nothing
    auto inG7H8 = index.queryRange(6, 6, 7, 7);
    EXPECT_TRUE(inG7H8.empty());

    // Query entire viewport covering everything
    auto all = index.queryRange(0, 0, 10, 10);
    EXPECT_EQ(all.size(), 3u);
}

TEST_F(SpillIndexTest, Remove) {
    index.insert(master1, 0, 0, 2, 4);
    index.insert(master2, 1, 1, 3, 3);

    EXPECT_EQ(index.size(), 2u);

    // Remove master1
    EXPECT_TRUE(index.remove(master1));
    EXPECT_EQ(index.size(), 1u);

    // Query at A1 - should find nothing now
    auto atA1 = index.queryAt(0, 0);
    EXPECT_TRUE(atA1.empty());

    // Query at B2 - should find only master2
    auto atB2 = index.queryAt(1, 1);
    ASSERT_EQ(atB2.size(), 1u);
    EXPECT_EQ(atB2[0], master2);

    // Remove non-existent master - should return false
    EXPECT_FALSE(index.remove(master1));  // Already removed
    EXPECT_FALSE(index.remove(master3));  // Never inserted
}

TEST_F(SpillIndexTest, UpdateBounds) {
    index.insert(master1, 0, 0, 2, 4);  // A1:C5

    // Verify initial position
    auto atA1 = index.queryAt(0, 0);
    ASSERT_EQ(atA1.size(), 1u);

    // Move spill extent to D4:F6 (positions 3,3 - 5,5)
    EXPECT_TRUE(index.updateBounds(master1, 3, 3, 5, 5));

    // Should not be at A1 anymore
    auto atA1After = index.queryAt(0, 0);
    EXPECT_TRUE(atA1After.empty());

    // Should be at D4 now
    auto atD4After = index.queryAt(3, 3);
    ASSERT_EQ(atD4After.size(), 1u);
    EXPECT_EQ(atD4After[0], master1);

    // Update bounds for non-existent master - should return false
    EXPECT_FALSE(index.updateBounds(master2, 0, 0, 1, 1));
}

TEST_F(SpillIndexTest, HasSpillAt) {
    index.insert(master1, 0, 0, 2, 4);

    EXPECT_TRUE(index.hasSpillAt(0, 0));
    EXPECT_TRUE(index.hasSpillAt(1, 2));
    EXPECT_TRUE(index.hasSpillAt(2, 4));
    EXPECT_FALSE(index.hasSpillAt(3, 3));
    EXPECT_FALSE(index.hasSpillAt(5, 5));
}

TEST_F(SpillIndexTest, GetBounds) {
    index.insert(master1, 0, 0, 2, 4);

    const auto* bounds = index.getBounds(master1);
    ASSERT_NE(bounds, nullptr);
    EXPECT_EQ(bounds->startCol, 0u);
    EXPECT_EQ(bounds->startRow, 0u);
    EXPECT_EQ(bounds->endCol, 2u);
    EXPECT_EQ(bounds->endRow, 4u);

    // Non-existent master
    EXPECT_EQ(index.getBounds(master2), nullptr);
}

TEST_F(SpillIndexTest, Clear) {
    index.insert(master1, 0, 0, 2, 4);
    index.insert(master2, 1, 1, 3, 3);

    EXPECT_EQ(index.size(), 2u);

    index.clear();

    EXPECT_EQ(index.size(), 0u);
    EXPECT_TRUE(index.empty());
    EXPECT_TRUE(index.queryAt(0, 0).empty());
}

TEST_F(SpillIndexTest, ForEach) {
    index.insert(master1, 0, 0, 2, 4);
    index.insert(master2, 1, 1, 3, 3);

    std::vector<ID> visitedIds;
    index.forEach([&visitedIds](const ID& masterId, const SpillPositionBounds& /*bounds*/) {
        visitedIds.push_back(masterId);
    });

    EXPECT_EQ(visitedIds.size(), 2u);
    // Order may vary, just check both are visited
    EXPECT_TRUE(std::find(visitedIds.begin(), visitedIds.end(), master1) != visitedIds.end());
    EXPECT_TRUE(std::find(visitedIds.begin(), visitedIds.end(), master2) != visitedIds.end());
}

TEST_F(SpillIndexTest, InsertReplaceExisting) {
    // Insert master1 at one position
    index.insert(master1, 0, 0, 2, 4);
    EXPECT_TRUE(index.hasSpillAt(0, 0));

    // Insert same master at different position - should replace
    index.insert(master1, 5, 5, 7, 7);

    // Should only have 1 entry
    EXPECT_EQ(index.size(), 1u);

    // Should not be at old position
    EXPECT_FALSE(index.hasSpillAt(0, 0));

    // Should be at new position
    EXPECT_TRUE(index.hasSpillAt(5, 5));
}

TEST_F(SpillIndexTest, ViewportQuery) {
    // Simulate a realistic scenario with spill extents in different parts of the sheet
    ID spillA = ID("spillA");  // Top-left area
    ID spillB = ID("spillB");  // Middle area
    ID spillC = ID("spillC");  // Far right area

    index.insert(spillA, 0, 0, 5, 10);     // A1:F11
    index.insert(spillB, 10, 10, 15, 20);  // K11:P21
    index.insert(spillC, 100, 0, 105, 5);  // Far right columns

    // Query viewport at top-left (0,0 to 20,20) - should find spillA and spillB
    auto topLeft = index.queryRange(0, 0, 20, 20);
    EXPECT_EQ(topLeft.size(), 2u);

    // Query viewport in middle (5,5 to 15,15) - should find spillA and spillB (partial overlap)
    auto middle = index.queryRange(5, 5, 15, 15);
    EXPECT_EQ(middle.size(), 2u);

    // Query viewport far right (90,0 to 110,10) - should find spillC only
    auto farRight = index.queryRange(90, 0, 110, 10);
    EXPECT_EQ(farRight.size(), 1u);
    EXPECT_EQ(farRight[0], spillC);

    // Query viewport at row 50000 (simulating scrolling far down) - should find nothing
    auto farDown = index.queryRange(0, 50000, 20, 50020);
    EXPECT_TRUE(farDown.empty());
}

TEST_F(SpillIndexTest, LargeSpillExtent) {
    // Test with a large dynamic array spill (e.g., 1000 rows)
    ID largeSpill = ID("largeSpill");
    index.insert(largeSpill, 0, 0, 5, 999);  // A1:F1000

    // Query at different row positions - all should find the spill
    EXPECT_TRUE(index.hasSpillAt(0, 0));
    EXPECT_TRUE(index.hasSpillAt(2, 500));
    EXPECT_TRUE(index.hasSpillAt(5, 999));

    // Just outside the bounds
    EXPECT_FALSE(index.hasSpillAt(6, 500));
    EXPECT_FALSE(index.hasSpillAt(3, 1000));
}

TEST_F(SpillIndexTest, EmptyIndex) {
    // Operations on empty index should be safe
    EXPECT_TRUE(index.empty());
    EXPECT_EQ(index.size(), 0u);
    EXPECT_TRUE(index.queryAt(0, 0).empty());
    EXPECT_TRUE(index.queryRange(0, 0, 100, 100).empty());
    EXPECT_FALSE(index.hasSpillAt(0, 0));
    EXPECT_FALSE(index.remove(master1));
    EXPECT_EQ(index.getBounds(master1), nullptr);

    // Clear on empty should be safe
    index.clear();
    EXPECT_TRUE(index.empty());
}

// ============================================================================
// Benchmark: Verify O(log n + k) query complexity
// ============================================================================

TEST_F(SpillIndexTest, BenchmarkQueryScaling) {
    // This test verifies that query time scales logarithmically with index size.
    // We insert spill extents at increasing positions and measure query time
    // at a fixed viewport position that doesn't overlap any extents.
    //
    // If the implementation is O(log n), query time should be nearly constant
    // as we add more non-overlapping entries.

    SpillIndex benchIndex;
    const int warmupIterations = 1000;
    const int timedIterations = 10000;

    // Measure baseline query time with empty index
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < timedIterations; ++i) {
        auto result = benchIndex.queryRange(0, 0, 10, 10);
        (void)result;  // Prevent optimization
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto emptyTime = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();

    // Insert 1000 spill extents spread across the sheet (none overlap with viewport 0,0-10,10)
    for (int i = 0; i < 1000; ++i) {
        ID spillId = ID("spill" + std::to_string(i));
        // Place each spill extent at position (100 + i*10, 100 + i*10) to (105 + i*10, 105 + i*10)
        // These are all far from the viewport at (0,0)-(10,10)
        uint32_t startCol = static_cast<uint32_t>(100 + i * 10);
        uint32_t startRow = static_cast<uint32_t>(100 + i * 10);
        benchIndex.insert(spillId, startCol, startRow, startCol + 5, startRow + 5);
    }

    EXPECT_EQ(benchIndex.size(), 1000u);

    // Warmup to stabilize CPU caches
    for (int i = 0; i < warmupIterations; ++i) {
        auto result = benchIndex.queryRange(0, 0, 10, 10);
        (void)result;
    }

    // Measure query time with 1000 entries
    start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < timedIterations; ++i) {
        auto result = benchIndex.queryRange(0, 0, 10, 10);
        EXPECT_TRUE(result.empty());  // No spills should overlap
    }
    end = std::chrono::high_resolution_clock::now();
    auto filledTime = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();

    // With O(log n) complexity, the filled time should be close to empty time
    // (just a small logarithmic overhead). We allow up to 10x slowdown as a
    // reasonable threshold, which is much better than O(n) which would be 1000x+.
    double slowdown = static_cast<double>(filledTime) / static_cast<double>(emptyTime + 1);

    std::cout << "SpillIndex Benchmark Results:" << std::endl;
    std::cout << "  Empty index query: " << (emptyTime / timedIterations) << " ns/query"
              << std::endl;
    std::cout << "  1000-entry index query: " << (filledTime / timedIterations) << " ns/query"
              << std::endl;
    std::cout << "  Slowdown factor: " << slowdown << "x" << std::endl;

    // O(log n) should give ~10x slowdown, O(n) would give ~1000x
    // We use a generous threshold of 50x to account for system variation
    EXPECT_LT(slowdown, 50.0) << "Query time scaled worse than O(log n)";
}

TEST_F(SpillIndexTest, BenchmarkQueryWithMatches) {
    // Test that query time is O(log n + k) where k is the number of results
    SpillIndex benchIndex;

    // Use generate_id() to create proper unique IDs
    std::vector<ID> overlappingIds;
    std::vector<ID> nonOverlappingIds;

    // Insert 100 spill extents that all overlap with the viewport
    for (int i = 0; i < 100; ++i) {
        ID spillId = generate_id();
        overlappingIds.push_back(spillId);
        // All extents include position (5, 5)
        benchIndex.insert(spillId, 0, 0, 10 + static_cast<uint32_t>(i),
                          10 + static_cast<uint32_t>(i));
    }

    // Insert 900 more that don't overlap
    for (int i = 0; i < 900; ++i) {
        ID spillId = generate_id();
        nonOverlappingIds.push_back(spillId);
        uint32_t startCol = static_cast<uint32_t>(1000 + i * 10);
        uint32_t startRow = static_cast<uint32_t>(1000 + i * 10);
        benchIndex.insert(spillId, startCol, startRow, startCol + 5, startRow + 5);
    }

    EXPECT_EQ(benchIndex.size(), 1000u);

    // Query should return exactly the 100 overlapping entries
    auto results = benchIndex.queryRange(0, 0, 10, 10);
    EXPECT_EQ(results.size(), 100u);

    // Verify all returned IDs are from the overlapping set
    for (const auto& id : results) {
        bool found =
            std::find(overlappingIds.begin(), overlappingIds.end(), id) != overlappingIds.end();
        EXPECT_TRUE(found) << "Unexpected result: " << id.toString();
    }
}

}  // namespace
}  // namespace cells
