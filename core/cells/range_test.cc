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

// =============================================================================
// Corner Deletion Tests
// =============================================================================

class CornerDeletionTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Set up a linear column/row sequence: A, B, C, D, E (positions 0-4)
        colA = ID("colA0000");
        colB = ID("colB0000");
        colC = ID("colC0000");
        colD = ID("colD0000");
        colE = ID("colE0000");

        rowA = ID("rowA0000");
        rowB = ID("rowB0000");
        rowC = ID("rowC0000");
        rowD = ID("rowD0000");
        rowE = ID("rowE0000");
    }

    // Helper functions that simulate axis traversal
    ID getNextCol(const ID& colId) const {
        if (colId == colA)
            return colB;
        if (colId == colB)
            return colC;
        if (colId == colC)
            return colD;
        if (colId == colD)
            return colE;
        return ID();  // null
    }

    ID getPrevCol(const ID& colId) const {
        if (colId == colB)
            return colA;
        if (colId == colC)
            return colB;
        if (colId == colD)
            return colC;
        if (colId == colE)
            return colD;
        return ID();  // null
    }

    ID getNextRow(const ID& rowId) const {
        if (rowId == rowA)
            return rowB;
        if (rowId == rowB)
            return rowC;
        if (rowId == rowC)
            return rowD;
        if (rowId == rowD)
            return rowE;
        return ID();  // null
    }

    ID getPrevRow(const ID& rowId) const {
        if (rowId == rowB)
            return rowA;
        if (rowId == rowC)
            return rowB;
        if (rowId == rowD)
            return rowC;
        if (rowId == rowE)
            return rowD;
        return ID();  // null
    }

    ID colA, colB, colC, colD, colE;
    ID rowA, rowB, rowC, rowD, rowE;
};

TEST_F(CornerDeletionTest, DeleteStartColumn) {
    // Range B:D (colB to colD) - delete colB
    Range range(ID("range001"), colB, rowA, colD, rowC);

    auto result = adjustRangeForColumnDeletion(
        range, colB, [this](const ID& id) { return getNextCol(id); },
        [this](const ID& id) { return getPrevCol(id); });

    EXPECT_EQ(result, CornerDeleteResult::SHRUNK);
    EXPECT_EQ(range.startColId, colC);  // Moved to next column
    EXPECT_EQ(range.endColId, colD);    // Unchanged
}

TEST_F(CornerDeletionTest, DeleteEndColumn) {
    // Range B:D (colB to colD) - delete colD
    Range range(ID("range001"), colB, rowA, colD, rowC);

    auto result = adjustRangeForColumnDeletion(
        range, colD, [this](const ID& id) { return getNextCol(id); },
        [this](const ID& id) { return getPrevCol(id); });

    EXPECT_EQ(result, CornerDeleteResult::SHRUNK);
    EXPECT_EQ(range.startColId, colB);  // Unchanged
    EXPECT_EQ(range.endColId, colC);    // Moved to previous column
}

TEST_F(CornerDeletionTest, DeleteMiddleColumn) {
    // Range B:D (colB to colD) - delete colC (middle, not a corner)
    Range range(ID("range001"), colB, rowA, colD, rowC);

    auto result = adjustRangeForColumnDeletion(
        range, colC, [this](const ID& id) { return getNextCol(id); },
        [this](const ID& id) { return getPrevCol(id); });

    EXPECT_EQ(result, CornerDeleteResult::UNCHANGED);
    EXPECT_EQ(range.startColId, colB);  // Unchanged
    EXPECT_EQ(range.endColId, colD);    // Unchanged
}

TEST_F(CornerDeletionTest, DeleteSingleColumnRange) {
    // Single column range C:C - delete colC
    Range range(ID("range001"), colC, rowA, colC, rowC);

    auto result = adjustRangeForColumnDeletion(
        range, colC, [this](const ID& id) { return getNextCol(id); },
        [this](const ID& id) { return getPrevCol(id); });

    EXPECT_EQ(result, CornerDeleteResult::INVALIDATED);
}

TEST_F(CornerDeletionTest, DeleteStartRow) {
    // Range rows A:C - delete rowA
    Range range(ID("range001"), colB, rowA, colD, rowC);

    auto result = adjustRangeForRowDeletion(
        range, rowA, [this](const ID& id) { return getNextRow(id); },
        [this](const ID& id) { return getPrevRow(id); });

    EXPECT_EQ(result, CornerDeleteResult::SHRUNK);
    EXPECT_EQ(range.startRowId, rowB);  // Moved to next row
    EXPECT_EQ(range.endRowId, rowC);    // Unchanged
}

TEST_F(CornerDeletionTest, DeleteEndRow) {
    // Range rows A:C - delete rowC
    Range range(ID("range001"), colB, rowA, colD, rowC);

    auto result = adjustRangeForRowDeletion(
        range, rowC, [this](const ID& id) { return getNextRow(id); },
        [this](const ID& id) { return getPrevRow(id); });

    EXPECT_EQ(result, CornerDeleteResult::SHRUNK);
    EXPECT_EQ(range.startRowId, rowA);  // Unchanged
    EXPECT_EQ(range.endRowId, rowB);    // Moved to previous row
}

TEST_F(CornerDeletionTest, DeleteSingleRowRange) {
    // Single row range row B - delete rowB
    Range range(ID("range001"), colB, rowB, colD, rowB);

    auto result = adjustRangeForRowDeletion(
        range, rowB, [this](const ID& id) { return getNextRow(id); },
        [this](const ID& id) { return getPrevRow(id); });

    EXPECT_EQ(result, CornerDeleteResult::INVALIDATED);
}

TEST_F(CornerDeletionTest, DeleteFirstColumnWithNoNext) {
    // Range A:A (first column, no previous) - delete colA
    Range range(ID("range001"), colA, rowA, colA, rowC);

    auto result = adjustRangeForColumnDeletion(
        range, colA, [this](const ID& id) { return getNextCol(id); },
        [this](const ID& id) { return getPrevCol(id); });

    // Single-column range → invalidated
    EXPECT_EQ(result, CornerDeleteResult::INVALIDATED);
}

TEST_F(CornerDeletionTest, DeleteLastColumnWithNoPrev) {
    // Range E:E (last column) - delete colE
    Range range(ID("range001"), colE, rowA, colE, rowC);

    auto result = adjustRangeForColumnDeletion(
        range, colE, [this](const ID& id) { return getNextCol(id); },
        [this](const ID& id) { return getPrevCol(id); });

    // Single-column range → invalidated
    EXPECT_EQ(result, CornerDeleteResult::INVALIDATED);
}

TEST_F(CornerDeletionTest, UnrelatedColumnDelete) {
    // Range B:D - delete colA (not in range)
    Range range(ID("range001"), colB, rowA, colD, rowC);

    auto result = adjustRangeForColumnDeletion(
        range, colA, [this](const ID& id) { return getNextCol(id); },
        [this](const ID& id) { return getPrevCol(id); });

    EXPECT_EQ(result, CornerDeleteResult::UNCHANGED);
}

}  // namespace
}  // namespace cells
