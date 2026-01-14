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

}  // namespace
}  // namespace cells
