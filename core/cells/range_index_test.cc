#include "core/cells/range_index.h"

#include <gtest/gtest.h>

namespace cells {
namespace {

class RangeIndexTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create some test ranges
        range1.id = ID("range001");
        range1.startColId = ID("col00000");
        range1.startRowId = ID("row00000");
        range1.endColId = ID("col00002");
        range1.endRowId = ID("row00002");
        range1.flags = RangeFlags::MERGE;

        range2.id = ID("range002");
        range2.startColId = ID("col00001");
        range2.startRowId = ID("row00001");
        range2.endColId = ID("col00003");
        range2.endRowId = ID("row00003");
        range2.flags = RangeFlags::STYLE;

        range3.id = ID("range003");
        range3.startColId = ID("col00005");
        range3.startRowId = ID("row00005");
        range3.endColId = ID("col00005");
        range3.endRowId = ID("row00005");
        range3.flags = RangeFlags::MERGE | RangeFlags::STYLE;
    }

    Range range1;  // A1:C3 (positions 0,0 - 2,2) - MERGE
    Range range2;  // B2:D4 (positions 1,1 - 3,3) - STYLE
    Range range3;  // F6:F6 (position 5,5) - MERGE | STYLE
    RangeIndex index;
};

TEST_F(RangeIndexTest, InsertAndQuery) {
    // Insert ranges
    index.insert(&range1, 0, 0, 2, 2);  // A1:C3
    index.insert(&range2, 1, 1, 3, 3);  // B2:D4

    EXPECT_EQ(index.size(), 2u);

    // Query at A1 (0,0) - should find range1 only
    auto atA1 = index.queryAt(0, 0);
    ASSERT_EQ(atA1.size(), 1u);
    EXPECT_EQ(atA1[0]->id, range1.id);

    // Query at B2 (1,1) - should find both ranges (overlapping)
    auto atB2 = index.queryAt(1, 1);
    ASSERT_EQ(atB2.size(), 2u);

    // Query at D4 (3,3) - should find range2 only
    auto atD4 = index.queryAt(3, 3);
    ASSERT_EQ(atD4.size(), 1u);
    EXPECT_EQ(atD4[0]->id, range2.id);

    // Query at E5 (4,4) - should find nothing
    auto atE5 = index.queryAt(4, 4);
    EXPECT_TRUE(atE5.empty());
}

TEST_F(RangeIndexTest, QueryWithFlags) {
    index.insert(&range1, 0, 0, 2, 2);  // MERGE
    index.insert(&range2, 1, 1, 3, 3);  // STYLE

    // Query at B2 with MERGE flag - should find range1 only
    auto mergeAtB2 = index.queryAt(1, 1, RangeFlags::MERGE);
    ASSERT_EQ(mergeAtB2.size(), 1u);
    EXPECT_EQ(mergeAtB2[0]->id, range1.id);

    // Query at B2 with STYLE flag - should find range2 only
    auto styleAtB2 = index.queryAt(1, 1, RangeFlags::STYLE);
    ASSERT_EQ(styleAtB2.size(), 1u);
    EXPECT_EQ(styleAtB2[0]->id, range2.id);

    // Query at B2 with NAMED flag - should find nothing
    auto namedAtB2 = index.queryAt(1, 1, RangeFlags::NAMED);
    EXPECT_TRUE(namedAtB2.empty());
}

TEST_F(RangeIndexTest, QueryRange) {
    index.insert(&range1, 0, 0, 2, 2);  // A1:C3
    index.insert(&range2, 1, 1, 3, 3);  // B2:D4
    index.insert(&range3, 5, 5, 5, 5);  // F6

    // Query range A1:B2 - should intersect both range1 and range2
    auto inA1B2 = index.queryRange(0, 0, 1, 1);
    EXPECT_EQ(inA1B2.size(), 2u);

    // Query range D4:F6 - should intersect range2 and range3
    auto inD4F6 = index.queryRange(3, 3, 5, 5);
    EXPECT_EQ(inD4F6.size(), 2u);

    // Query range G7:H8 - should find nothing
    auto inG7H8 = index.queryRange(6, 6, 7, 7);
    EXPECT_TRUE(inG7H8.empty());
}

TEST_F(RangeIndexTest, Remove) {
    index.insert(&range1, 0, 0, 2, 2);
    index.insert(&range2, 1, 1, 3, 3);

    EXPECT_EQ(index.size(), 2u);

    // Remove range1
    EXPECT_TRUE(index.remove(&range1));
    EXPECT_EQ(index.size(), 1u);

    // Query at A1 - should find nothing now
    auto atA1 = index.queryAt(0, 0);
    EXPECT_TRUE(atA1.empty());

    // Query at B2 - should find only range2
    auto atB2 = index.queryAt(1, 1);
    ASSERT_EQ(atB2.size(), 1u);
    EXPECT_EQ(atB2[0]->id, range2.id);

    // Remove non-existent range - should return false
    EXPECT_FALSE(index.remove(&range1));  // Already removed
    EXPECT_FALSE(index.remove(&range3));  // Never inserted
}

TEST_F(RangeIndexTest, RemoveById) {
    index.insert(&range1, 0, 0, 2, 2);
    index.insert(&range2, 1, 1, 3, 3);

    // Remove by ID
    EXPECT_TRUE(index.removeById(range1.id));
    EXPECT_EQ(index.size(), 1u);

    // Try to remove again - should fail
    EXPECT_FALSE(index.removeById(range1.id));
}

TEST_F(RangeIndexTest, UpdateBounds) {
    index.insert(&range1, 0, 0, 2, 2);  // A1:C3

    // Verify initial position
    auto atA1 = index.queryAt(0, 0);
    ASSERT_EQ(atA1.size(), 1u);

    // Move range to D4:F6 (positions 3,3 - 5,5)
    EXPECT_TRUE(index.updateBounds(&range1, 3, 3, 5, 5));

    // Should not be at A1 anymore
    auto atA1After = index.queryAt(0, 0);
    EXPECT_TRUE(atA1After.empty());

    // Should be at D4 now
    auto atD4After = index.queryAt(3, 3);
    ASSERT_EQ(atD4After.size(), 1u);
    EXPECT_EQ(atD4After[0]->id, range1.id);
}

TEST_F(RangeIndexTest, HasRangeAt) {
    index.insert(&range1, 0, 0, 2, 2);  // MERGE

    EXPECT_TRUE(index.hasRangeAt(0, 0));
    EXPECT_TRUE(index.hasRangeAt(1, 1));
    EXPECT_FALSE(index.hasRangeAt(3, 3));

    EXPECT_TRUE(index.hasRangeAt(0, 0, RangeFlags::MERGE));
    EXPECT_FALSE(index.hasRangeAt(0, 0, RangeFlags::STYLE));
}

TEST_F(RangeIndexTest, GetBounds) {
    index.insert(&range1, 0, 0, 2, 2);

    const auto* bounds = index.getBounds(range1.id);
    ASSERT_NE(bounds, nullptr);
    EXPECT_EQ(bounds->startCol, 0u);
    EXPECT_EQ(bounds->startRow, 0u);
    EXPECT_EQ(bounds->endCol, 2u);
    EXPECT_EQ(bounds->endRow, 2u);

    // Non-existent range
    EXPECT_EQ(index.getBounds(range2.id), nullptr);
}

TEST_F(RangeIndexTest, Clear) {
    index.insert(&range1, 0, 0, 2, 2);
    index.insert(&range2, 1, 1, 3, 3);

    EXPECT_EQ(index.size(), 2u);

    index.clear();

    EXPECT_EQ(index.size(), 0u);
    EXPECT_TRUE(index.empty());
    EXPECT_TRUE(index.queryAt(0, 0).empty());
}

TEST_F(RangeIndexTest, ForEach) {
    index.insert(&range1, 0, 0, 2, 2);
    index.insert(&range2, 1, 1, 3, 3);

    std::vector<ID> visitedIds;
    index.forEach([&visitedIds](Range* range, const RangePositionBounds& /*bounds*/) {
        visitedIds.push_back(range->id);
    });

    EXPECT_EQ(visitedIds.size(), 2u);
    // Order may vary, just check both are visited
    EXPECT_TRUE(std::find(visitedIds.begin(), visitedIds.end(), range1.id) != visitedIds.end());
    EXPECT_TRUE(std::find(visitedIds.begin(), visitedIds.end(), range2.id) != visitedIds.end());
}

TEST_F(RangeIndexTest, InsertReplaceExisting) {
    // Insert range1 at one position
    index.insert(&range1, 0, 0, 2, 2);
    EXPECT_TRUE(index.hasRangeAt(0, 0));

    // Insert same range at different position - should replace
    index.insert(&range1, 5, 5, 7, 7);

    // Should only have 1 entry
    EXPECT_EQ(index.size(), 1u);

    // Should not be at old position
    EXPECT_FALSE(index.hasRangeAt(0, 0));

    // Should be at new position
    EXPECT_TRUE(index.hasRangeAt(5, 5));
}

TEST_F(RangeIndexTest, NullRange) {
    // Inserting null should be a no-op
    index.insert(nullptr, 0, 0, 2, 2);
    EXPECT_EQ(index.size(), 0u);

    // Removing null should return false
    EXPECT_FALSE(index.remove(nullptr));
}

TEST_F(RangeIndexTest, CombinedFlags) {
    // range3 has both MERGE and STYLE flags
    index.insert(&range3, 5, 5, 5, 5);

    // Should match queries for either flag
    auto mergeRanges = index.queryAt(5, 5, RangeFlags::MERGE);
    ASSERT_EQ(mergeRanges.size(), 1u);
    EXPECT_EQ(mergeRanges[0]->id, range3.id);

    auto styleRanges = index.queryAt(5, 5, RangeFlags::STYLE);
    ASSERT_EQ(styleRanges.size(), 1u);
    EXPECT_EQ(styleRanges[0]->id, range3.id);
}

}  // namespace
}  // namespace cells
