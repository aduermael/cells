#include "core/cells/range.h"

#include <gtest/gtest.h>

namespace cells {
namespace {

TEST(RangeFlagsTest, BitwiseOperations) {
    // Test OR
    RangeFlags merged_styled = RangeFlags::MERGE | RangeFlags::STYLE;
    EXPECT_TRUE(hasFlag(merged_styled, RangeFlags::MERGE));
    EXPECT_TRUE(hasFlag(merged_styled, RangeFlags::STYLE));
    EXPECT_FALSE(hasFlag(merged_styled, RangeFlags::NAMED));

    // Test AND
    RangeFlags result = merged_styled & RangeFlags::MERGE;
    EXPECT_TRUE(hasFlag(result, RangeFlags::MERGE));
    EXPECT_FALSE(hasFlag(result, RangeFlags::STYLE));

    // Test compound assignment
    RangeFlags flags = RangeFlags::NONE;
    flags |= RangeFlags::FILTER;
    EXPECT_TRUE(hasFlag(flags, RangeFlags::FILTER));

    flags &= ~RangeFlags::FILTER;
    EXPECT_FALSE(hasFlag(flags, RangeFlags::FILTER));
}

TEST(RangeFlagsTest, AllFlags) {
    // Verify all flags can be combined
    RangeFlags all = RangeFlags::MERGE | RangeFlags::STYLE | RangeFlags::CONDITIONAL_FORMAT |
                     RangeFlags::DATA_VALIDATION | RangeFlags::NAMED | RangeFlags::PRINT_AREA |
                     RangeFlags::FILTER;

    EXPECT_TRUE(hasFlag(all, RangeFlags::MERGE));
    EXPECT_TRUE(hasFlag(all, RangeFlags::STYLE));
    EXPECT_TRUE(hasFlag(all, RangeFlags::CONDITIONAL_FORMAT));
    EXPECT_TRUE(hasFlag(all, RangeFlags::DATA_VALIDATION));
    EXPECT_TRUE(hasFlag(all, RangeFlags::NAMED));
    EXPECT_TRUE(hasFlag(all, RangeFlags::PRINT_AREA));
    EXPECT_TRUE(hasFlag(all, RangeFlags::FILTER));
}

TEST(RangeTest, DefaultConstruction) {
    Range r;
    EXPECT_TRUE(r.id.isNull());
    EXPECT_TRUE(r.startColId.isNull());
    EXPECT_TRUE(r.startRowId.isNull());
    EXPECT_TRUE(r.endColId.isNull());
    EXPECT_TRUE(r.endRowId.isNull());
    EXPECT_EQ(r.flags, RangeFlags::NONE);
    EXPECT_FALSE(r.isValid());
}

TEST(RangeTest, FullConstruction) {
    ID rangeId("range001");
    ID startCol("colA0001");
    ID startRow("row00001");
    ID endCol("colC0001");
    ID endRow("row00003");

    Range r(rangeId, startCol, startRow, endCol, endRow, RangeFlags::MERGE);

    EXPECT_EQ(r.id, rangeId);
    EXPECT_EQ(r.startColId, startCol);
    EXPECT_EQ(r.startRowId, startRow);
    EXPECT_EQ(r.endColId, endCol);
    EXPECT_EQ(r.endRowId, endRow);
    EXPECT_TRUE(r.hasFlag(RangeFlags::MERGE));
    EXPECT_TRUE(r.isValid());
    EXPECT_FALSE(r.isSingleCell());
    EXPECT_TRUE(r.isMerge());
}

TEST(RangeTest, SingleCellConstruction) {
    ID rangeId("range002");
    ID colId("colB0001");
    ID rowId("row00002");

    Range r(rangeId, colId, rowId, RangeFlags::STYLE);

    EXPECT_EQ(r.id, rangeId);
    EXPECT_EQ(r.startColId, colId);
    EXPECT_EQ(r.startRowId, rowId);
    EXPECT_EQ(r.endColId, colId);
    EXPECT_EQ(r.endRowId, rowId);
    EXPECT_TRUE(r.hasFlag(RangeFlags::STYLE));
    EXPECT_TRUE(r.isValid());
    EXPECT_TRUE(r.isSingleCell());
    EXPECT_TRUE(r.hasStyle());
    EXPECT_FALSE(r.isMerge());
}

TEST(RangeTest, Equality) {
    ID rangeId("range003");
    ID col1("colA0001");
    ID row1("row00001");
    ID col2("colB0001");
    ID row2("row00002");

    // Same ID = equal (even if other fields differ)
    Range r1(rangeId, col1, row1, col2, row2, RangeFlags::MERGE);
    Range r2(rangeId, col1, row1, col1, row1, RangeFlags::STYLE);  // Different corners and flags

    EXPECT_EQ(r1, r2);  // Equal because same ID

    // Different ID = not equal
    ID otherId("range004");
    Range r3(otherId, col1, row1, col2, row2, RangeFlags::MERGE);
    EXPECT_NE(r1, r3);
}

TEST(RangeTest, FlagHelpers) {
    ID rangeId("range005");
    ID col("colA0001");
    ID row("row00001");

    Range r(rangeId, col, row, RangeFlags::MERGE | RangeFlags::STYLE);

    EXPECT_TRUE(r.hasFlag(RangeFlags::MERGE));
    EXPECT_TRUE(r.hasFlag(RangeFlags::STYLE));
    EXPECT_FALSE(r.hasFlag(RangeFlags::NAMED));

    EXPECT_TRUE(r.isMerge());
    EXPECT_TRUE(r.hasStyle());
}

TEST(RangeTest, InvalidRange) {
    // Range with null ID is invalid
    Range r1;
    r1.startColId = ID("colA0001");
    r1.startRowId = ID("row00001");
    r1.endColId = ID("colA0001");
    r1.endRowId = ID("row00001");
    EXPECT_FALSE(r1.isValid());  // id is null

    // Range with null corner is invalid
    Range r2;
    r2.id = ID("range006");
    r2.startColId = ID("colA0001");
    // startRowId is null
    r2.endColId = ID("colA0001");
    r2.endRowId = ID("row00001");
    EXPECT_FALSE(r2.isValid());
}

// =============================================================================
// Range Containment Tests
// =============================================================================

TEST(RangeContainmentTest, BasicContainment) {
    // Range A1:C3 -> positions (0,0) to (2,2)
    // Should contain cells at: A1(0,0), B2(1,1), C3(2,2), A3(0,2), C1(2,0)
    EXPECT_TRUE(rangeContainsPosition(0, 0, 2, 2, 0, 0));  // A1 - top-left corner
    EXPECT_TRUE(rangeContainsPosition(0, 0, 2, 2, 1, 1));  // B2 - middle
    EXPECT_TRUE(rangeContainsPosition(0, 0, 2, 2, 2, 2));  // C3 - bottom-right corner
    EXPECT_TRUE(rangeContainsPosition(0, 0, 2, 2, 0, 2));  // A3 - bottom-left corner
    EXPECT_TRUE(rangeContainsPosition(0, 0, 2, 2, 2, 0));  // C1 - top-right corner

    // Should NOT contain cells outside
    EXPECT_FALSE(rangeContainsPosition(0, 0, 2, 2, 3, 0));  // D1 - outside right
    EXPECT_FALSE(rangeContainsPosition(0, 0, 2, 2, 0, 3));  // A4 - outside bottom
    EXPECT_FALSE(rangeContainsPosition(0, 0, 2, 2, 3, 3));  // D4 - outside both
}

TEST(RangeContainmentTest, SingleCellRange) {
    // Range B2:B2 -> positions (1,1) to (1,1)
    EXPECT_TRUE(rangeContainsPosition(1, 1, 1, 1, 1, 1));   // B2 - the cell itself
    EXPECT_FALSE(rangeContainsPosition(1, 1, 1, 1, 0, 0));  // A1 - outside
    EXPECT_FALSE(rangeContainsPosition(1, 1, 1, 1, 1, 0));  // B1 - outside
    EXPECT_FALSE(rangeContainsPosition(1, 1, 1, 1, 0, 1));  // A2 - outside
    EXPECT_FALSE(rangeContainsPosition(1, 1, 1, 1, 2, 1));  // C2 - outside
    EXPECT_FALSE(rangeContainsPosition(1, 1, 1, 1, 1, 2));  // B3 - outside
}

TEST(RangeContainmentTest, ReversedCorners) {
    // If corners are reversed (end < start), should still work
    // Range specified as C3:A1 -> (2,2) to (0,0) should behave like A1:C3
    EXPECT_TRUE(rangeContainsPosition(2, 2, 0, 0, 1, 1));  // B2 - middle
    EXPECT_TRUE(rangeContainsPosition(2, 2, 0, 0, 0, 0));  // A1 - corner
    EXPECT_TRUE(rangeContainsPosition(2, 2, 0, 0, 2, 2));  // C3 - corner
}

TEST(RangeContainmentTest, IsAnchor) {
    // Range A1:C3 -> anchor is A1 (0,0)
    EXPECT_TRUE(rangeIsAnchorPosition(0, 0, 2, 2, 0, 0));   // A1 is anchor
    EXPECT_FALSE(rangeIsAnchorPosition(0, 0, 2, 2, 1, 1));  // B2 is not anchor
    EXPECT_FALSE(rangeIsAnchorPosition(0, 0, 2, 2, 2, 2));  // C3 is not anchor

    // Reversed corners: C3:A1 -> anchor is still A1 (min corner)
    EXPECT_TRUE(rangeIsAnchorPosition(2, 2, 0, 0, 0, 0));   // A1 is anchor
    EXPECT_FALSE(rangeIsAnchorPosition(2, 2, 0, 0, 2, 2));  // C3 is not anchor
}

TEST(RangeOverlapTest, NoOverlap) {
    // A1:B2 (0,0 - 1,1) and D4:E5 (3,3 - 4,4) - no overlap
    EXPECT_FALSE(rangesOverlap(0, 0, 1, 1, 3, 3, 4, 4));

    // A1:B2 (0,0 - 1,1) and C1:D2 (2,0 - 3,1) - adjacent horizontally, no overlap
    EXPECT_FALSE(rangesOverlap(0, 0, 1, 1, 2, 0, 3, 1));

    // A1:B2 (0,0 - 1,1) and A3:B4 (0,2 - 1,3) - adjacent vertically, no overlap
    EXPECT_FALSE(rangesOverlap(0, 0, 1, 1, 0, 2, 1, 3));
}

TEST(RangeOverlapTest, FullOverlap) {
    // Same range overlaps itself
    EXPECT_TRUE(rangesOverlap(0, 0, 2, 2, 0, 0, 2, 2));

    // One contains the other
    EXPECT_TRUE(rangesOverlap(0, 0, 4, 4, 1, 1, 2, 2));  // Larger contains smaller
    EXPECT_TRUE(rangesOverlap(1, 1, 2, 2, 0, 0, 4, 4));  // Smaller inside larger
}

TEST(RangeOverlapTest, PartialOverlap) {
    // A1:C3 (0,0 - 2,2) and B2:D4 (1,1 - 3,3) - overlap at B2:C3
    EXPECT_TRUE(rangesOverlap(0, 0, 2, 2, 1, 1, 3, 3));

    // Single cell overlap
    // A1:B2 (0,0 - 1,1) and B2:C3 (1,1 - 2,2) - overlap at B2
    EXPECT_TRUE(rangesOverlap(0, 0, 1, 1, 1, 1, 2, 2));
}

TEST(RangeOverlapTest, EdgeCases) {
    // Single cell ranges
    EXPECT_TRUE(rangesOverlap(1, 1, 1, 1, 1, 1, 1, 1));   // Same cell
    EXPECT_FALSE(rangesOverlap(1, 1, 1, 1, 2, 2, 2, 2));  // Different cells

    // Reversed corners should still work
    EXPECT_TRUE(rangesOverlap(2, 2, 0, 0, 1, 1, 3, 3));
}

}  // namespace
}  // namespace cells
