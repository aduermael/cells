// =============================================================================
// Range Boundary Unit Tests
// =============================================================================
//
// Tests for range expansion and shrinking when columns/rows are inserted or
// deleted. Since ranges use UUIDs for corners (not positions), inserting an
// axis inside a range doesn't change the range IDs but conceptually expands it.
// Deleting a boundary axis shrinks or invalidates the range.
//
// =============================================================================

#include "core/cells/crdt.h"
#include "core/cells/id.h"
#include "core/cells/range.h"

#include "gtest/gtest.h"

namespace cells {
namespace {

// =============================================================================
// Test Fixture
// =============================================================================

class RangeBoundaryTest : public ::testing::Test {
protected:
    void SetUp() override {
        workbook = std::make_unique<Workbook>(generate_id(), "TestWorkbook");
        workbook->setNodeId(generate_id());

        auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet1");
        sheet_id = sheet->id;
        sheet_ptr = sheet.get();
        sheet->setWorkbook(workbook.get());

        // Create 6 columns at positions 0, 1, 2, 3, 4, 5
        for (int i = 0; i < 6; i++) {
            auto col = std::make_unique<Axis>(generate_id(), true);
            col->position = static_cast<uint32_t>(i);
            colIds[i] = col->id;
            sheet->addColumn(std::move(col));
        }

        // Create 6 rows at positions 0, 1, 2, 3, 4, 5
        for (int i = 0; i < 6; i++) {
            auto row = std::make_unique<Axis>(generate_id(), false);
            row->position = static_cast<uint32_t>(i);
            rowIds[i] = row->id;
            sheet->addRow(std::move(row));
        }

        workbook->addSheet(std::move(sheet));
    }

    // Helper to create a range with specified corners and flags
    ID createRange(const ID& startCol, const ID& startRow, const ID& endCol, const ID& endRow,
                   uint8_t flags = 0) {
        ID rangeId = generate_id();
        std::string payload = "{\"startCol\":\"" + startCol.toString() + "\",";
        payload += "\"startRow\":\"" + startRow.toString() + "\",";
        payload += "\"endCol\":\"" + endCol.toString() + "\",";
        payload += "\"endRow\":\"" + endRow.toString() + "\",";
        payload += "\"flags\":" + std::to_string(flags) + "}";

        Operation op = makeRangeSetOp(*workbook, rangeId, payload);
        applyOperation(*workbook, op);
        return rangeId;
    }

    // Helper to get a range's column span (number of columns based on positions)
    int getRangeColSpan(const ID& rangeId) {
        Range* range = workbook->getRange(rangeId);
        if (!range)
            return -1;

        Axis* startCol = sheet_ptr->getColumn(range->startColId);
        Axis* endCol = sheet_ptr->getColumn(range->endColId);
        if (!startCol || !endCol)
            return -1;

        return static_cast<int>(endCol->position) - static_cast<int>(startCol->position) + 1;
    }

    // Helper to get a range's row span (number of rows based on positions)
    int getRangeRowSpan(const ID& rangeId) {
        Range* range = workbook->getRange(rangeId);
        if (!range)
            return -1;

        Axis* startRow = sheet_ptr->getRow(range->startRowId);
        Axis* endRow = sheet_ptr->getRow(range->endRowId);
        if (!startRow || !endRow)
            return -1;

        return static_cast<int>(endRow->position) - static_cast<int>(startRow->position) + 1;
    }

    std::unique_ptr<Workbook> workbook;
    Sheet* sheet_ptr;
    ID sheet_id;
    ID colIds[6];
    ID rowIds[6];
};

// =============================================================================
// 5a: Test range expands when column inserted inside range
// =============================================================================

TEST_F(RangeBoundaryTest, InsertColumnInsideRangeExpandsConceptually) {
    // Create a range from col1 to col3 (positions 1, 2, 3)
    // Span: 3 columns
    ID rangeId = createRange(colIds[1], rowIds[0], colIds[3], rowIds[2]);

    // Verify initial span
    EXPECT_EQ(getRangeColSpan(rangeId), 3);

    // Insert a new column at position 2 (inside the range)
    ID newColId = generate_id();
    std::string payload = R"({"pos":2,"size":100})";
    Operation op = makeColSetOp(*workbook, newColId, sheet_id, payload);
    applyOperation(*workbook, op);

    // The range IDs haven't changed, but conceptually the range now covers
    // more area because there's a new column between its corners
    Range* range = workbook->getRange(rangeId);
    ASSERT_NE(range, nullptr);
    EXPECT_EQ(range->startColId, colIds[1]);  // Start unchanged
    EXPECT_EQ(range->endColId, colIds[3]);    // End unchanged

    // New column exists at position 2
    Axis* newCol = sheet_ptr->getColumn(newColId);
    ASSERT_NE(newCol, nullptr);
    EXPECT_EQ(newCol->position, 2);
}

TEST_F(RangeBoundaryTest, InsertColumnAtRangeStartBoundary) {
    // Create a range from col1 to col3
    ID rangeId = createRange(colIds[1], rowIds[0], colIds[3], rowIds[2]);

    // Insert a column at position 1 (same position as start)
    ID newColId = generate_id();
    std::string payload = R"({"pos":1,"size":100})";
    Operation op = makeColSetOp(*workbook, newColId, sheet_id, payload);
    applyOperation(*workbook, op);

    // Range corners are unchanged (they reference UUIDs, not positions)
    Range* range = workbook->getRange(rangeId);
    ASSERT_NE(range, nullptr);
    EXPECT_EQ(range->startColId, colIds[1]);
    EXPECT_EQ(range->endColId, colIds[3]);

    // New column at position 1, original col1 still exists with same ID
    Axis* newCol = sheet_ptr->getColumn(newColId);
    ASSERT_NE(newCol, nullptr);
    EXPECT_EQ(newCol->position, 1);
}

TEST_F(RangeBoundaryTest, InsertColumnAtRangeEndBoundary) {
    // Create a range from col1 to col3
    ID rangeId = createRange(colIds[1], rowIds[0], colIds[3], rowIds[2]);

    // Insert a column at position 3 (same position as end)
    ID newColId = generate_id();
    std::string payload = R"({"pos":3,"size":100})";
    Operation op = makeColSetOp(*workbook, newColId, sheet_id, payload);
    applyOperation(*workbook, op);

    // Range corners are unchanged
    Range* range = workbook->getRange(rangeId);
    ASSERT_NE(range, nullptr);
    EXPECT_EQ(range->startColId, colIds[1]);
    EXPECT_EQ(range->endColId, colIds[3]);
}

TEST_F(RangeBoundaryTest, InsertColumnBeforeRangeDoesNotAffectIt) {
    // Create a range from col2 to col4
    ID rangeId = createRange(colIds[2], rowIds[0], colIds[4], rowIds[2]);

    // Insert a column at position 0 (before the range)
    ID newColId = generate_id();
    std::string payload = R"({"pos":0,"size":100})";
    Operation op = makeColSetOp(*workbook, newColId, sheet_id, payload);
    applyOperation(*workbook, op);

    // Range corners are unchanged
    Range* range = workbook->getRange(rangeId);
    ASSERT_NE(range, nullptr);
    EXPECT_EQ(range->startColId, colIds[2]);
    EXPECT_EQ(range->endColId, colIds[4]);
}

TEST_F(RangeBoundaryTest, InsertColumnAfterRangeDoesNotAffectIt) {
    // Create a range from col1 to col3
    ID rangeId = createRange(colIds[1], rowIds[0], colIds[3], rowIds[2]);

    // Insert a column at position 5 (after the range)
    ID newColId = generate_id();
    std::string payload = R"({"pos":5,"size":100})";
    Operation op = makeColSetOp(*workbook, newColId, sheet_id, payload);
    applyOperation(*workbook, op);

    // Range corners are unchanged
    Range* range = workbook->getRange(rangeId);
    ASSERT_NE(range, nullptr);
    EXPECT_EQ(range->startColId, colIds[1]);
    EXPECT_EQ(range->endColId, colIds[3]);
}

TEST_F(RangeBoundaryTest, MultipleColumnInsertsInsideRange) {
    // Create a range from col1 to col4
    ID rangeId = createRange(colIds[1], rowIds[0], colIds[4], rowIds[2]);

    // Insert multiple columns inside the range
    for (int i = 0; i < 3; i++) {
        ID newColId = generate_id();
        std::string payload = R"({"pos":2,"size":100})";
        Operation op = makeColSetOp(*workbook, newColId, sheet_id, payload);
        applyOperation(*workbook, op);
    }

    // Range corners are unchanged (UUIDs don't change)
    Range* range = workbook->getRange(rangeId);
    ASSERT_NE(range, nullptr);
    EXPECT_EQ(range->startColId, colIds[1]);
    EXPECT_EQ(range->endColId, colIds[4]);
}

// =============================================================================
// 5b: Test range expands when row inserted inside range
// =============================================================================

TEST_F(RangeBoundaryTest, InsertRowInsideRangeExpandsConceptually) {
    // Create a range from row1 to row3 (positions 1, 2, 3)
    ID rangeId = createRange(colIds[0], rowIds[1], colIds[2], rowIds[3]);

    // Verify initial span
    EXPECT_EQ(getRangeRowSpan(rangeId), 3);

    // Insert a new row at position 2 (inside the range)
    ID newRowId = generate_id();
    std::string payload = R"({"pos":2,"size":21})";
    Operation op = makeRowSetOp(*workbook, newRowId, sheet_id, payload);
    applyOperation(*workbook, op);

    // Range IDs haven't changed
    Range* range = workbook->getRange(rangeId);
    ASSERT_NE(range, nullptr);
    EXPECT_EQ(range->startRowId, rowIds[1]);
    EXPECT_EQ(range->endRowId, rowIds[3]);

    // New row exists at position 2
    Axis* newRow = sheet_ptr->getRow(newRowId);
    ASSERT_NE(newRow, nullptr);
    EXPECT_EQ(newRow->position, 2);
}

TEST_F(RangeBoundaryTest, InsertRowAtRangeStartBoundary) {
    // Create a range from row1 to row3
    ID rangeId = createRange(colIds[0], rowIds[1], colIds[2], rowIds[3]);

    // Insert a row at position 1 (same position as start)
    ID newRowId = generate_id();
    std::string payload = R"({"pos":1,"size":21})";
    Operation op = makeRowSetOp(*workbook, newRowId, sheet_id, payload);
    applyOperation(*workbook, op);

    // Range corners are unchanged
    Range* range = workbook->getRange(rangeId);
    ASSERT_NE(range, nullptr);
    EXPECT_EQ(range->startRowId, rowIds[1]);
    EXPECT_EQ(range->endRowId, rowIds[3]);
}

TEST_F(RangeBoundaryTest, InsertRowAtRangeEndBoundary) {
    // Create a range from row1 to row3
    ID rangeId = createRange(colIds[0], rowIds[1], colIds[2], rowIds[3]);

    // Insert a row at position 3 (same position as end)
    ID newRowId = generate_id();
    std::string payload = R"({"pos":3,"size":21})";
    Operation op = makeRowSetOp(*workbook, newRowId, sheet_id, payload);
    applyOperation(*workbook, op);

    // Range corners are unchanged
    Range* range = workbook->getRange(rangeId);
    ASSERT_NE(range, nullptr);
    EXPECT_EQ(range->startRowId, rowIds[1]);
    EXPECT_EQ(range->endRowId, rowIds[3]);
}

TEST_F(RangeBoundaryTest, InsertRowBeforeRangeDoesNotAffectIt) {
    // Create a range from row2 to row4
    ID rangeId = createRange(colIds[0], rowIds[2], colIds[2], rowIds[4]);

    // Insert a row at position 0 (before the range)
    ID newRowId = generate_id();
    std::string payload = R"({"pos":0,"size":21})";
    Operation op = makeRowSetOp(*workbook, newRowId, sheet_id, payload);
    applyOperation(*workbook, op);

    // Range corners are unchanged
    Range* range = workbook->getRange(rangeId);
    ASSERT_NE(range, nullptr);
    EXPECT_EQ(range->startRowId, rowIds[2]);
    EXPECT_EQ(range->endRowId, rowIds[4]);
}

TEST_F(RangeBoundaryTest, InsertRowAfterRangeDoesNotAffectIt) {
    // Create a range from row1 to row3
    ID rangeId = createRange(colIds[0], rowIds[1], colIds[2], rowIds[3]);

    // Insert a row at position 5 (after the range)
    ID newRowId = generate_id();
    std::string payload = R"({"pos":5,"size":21})";
    Operation op = makeRowSetOp(*workbook, newRowId, sheet_id, payload);
    applyOperation(*workbook, op);

    // Range corners are unchanged
    Range* range = workbook->getRange(rangeId);
    ASSERT_NE(range, nullptr);
    EXPECT_EQ(range->startRowId, rowIds[1]);
    EXPECT_EQ(range->endRowId, rowIds[3]);
}

TEST_F(RangeBoundaryTest, MultipleRowInsertsInsideRange) {
    // Create a range from row1 to row4
    ID rangeId = createRange(colIds[0], rowIds[1], colIds[2], rowIds[4]);

    // Insert multiple rows inside the range
    for (int i = 0; i < 3; i++) {
        ID newRowId = generate_id();
        std::string payload = R"({"pos":2,"size":21})";
        Operation op = makeRowSetOp(*workbook, newRowId, sheet_id, payload);
        applyOperation(*workbook, op);
    }

    // Range corners are unchanged
    Range* range = workbook->getRange(rangeId);
    ASSERT_NE(range, nullptr);
    EXPECT_EQ(range->startRowId, rowIds[1]);
    EXPECT_EQ(range->endRowId, rowIds[4]);
}

// =============================================================================
// 5c: Test range shrinks when column deleted from inside range
// =============================================================================

TEST_F(RangeBoundaryTest, DeleteStartColumnShrinksRange) {
    // Create a range from col1 to col3
    ID rangeId = createRange(colIds[1], rowIds[0], colIds[3], rowIds[2]);

    // Delete the start column (col1)
    Operation op = makeColDeleteOp(*workbook, colIds[1]);
    applyOperation(*workbook, op);

    // Range should shrink: new start is col2
    Range* range = workbook->getRange(rangeId);
    ASSERT_NE(range, nullptr);
    EXPECT_EQ(range->startColId, colIds[2]);  // Moved to next column
    EXPECT_EQ(range->endColId, colIds[3]);    // Unchanged
}

TEST_F(RangeBoundaryTest, DeleteEndColumnShrinksRange) {
    // Create a range from col1 to col3
    ID rangeId = createRange(colIds[1], rowIds[0], colIds[3], rowIds[2]);

    // Delete the end column (col3)
    Operation op = makeColDeleteOp(*workbook, colIds[3]);
    applyOperation(*workbook, op);

    // Range should shrink: new end is col2
    Range* range = workbook->getRange(rangeId);
    ASSERT_NE(range, nullptr);
    EXPECT_EQ(range->startColId, colIds[1]);  // Unchanged
    EXPECT_EQ(range->endColId, colIds[2]);    // Moved to previous column
}

TEST_F(RangeBoundaryTest, DeleteMiddleColumnDoesNotShrinkRange) {
    // Create a range from col1 to col4
    ID rangeId = createRange(colIds[1], rowIds[0], colIds[4], rowIds[2]);

    // Delete a middle column (col2) - not a corner
    Operation op = makeColDeleteOp(*workbook, colIds[2]);
    applyOperation(*workbook, op);

    // Range corners are unchanged (col2 was not a corner)
    Range* range = workbook->getRange(rangeId);
    ASSERT_NE(range, nullptr);
    EXPECT_EQ(range->startColId, colIds[1]);
    EXPECT_EQ(range->endColId, colIds[4]);
}

TEST_F(RangeBoundaryTest, DeleteColumnOutsideRangeDoesNotAffectIt) {
    // Create a range from col2 to col4
    ID rangeId = createRange(colIds[2], rowIds[0], colIds[4], rowIds[2]);

    // Delete a column outside the range (col0)
    Operation op = makeColDeleteOp(*workbook, colIds[0]);
    applyOperation(*workbook, op);

    // Range should be unchanged
    Range* range = workbook->getRange(rangeId);
    ASSERT_NE(range, nullptr);
    EXPECT_EQ(range->startColId, colIds[2]);
    EXPECT_EQ(range->endColId, colIds[4]);
}

TEST_F(RangeBoundaryTest, DeleteBothBoundaryColumnsInvalidatesRange) {
    // Create a two-column range from col2 to col3
    ID rangeId = createRange(colIds[2], rowIds[0], colIds[3], rowIds[2]);

    // Delete the start column
    applyOperation(*workbook, makeColDeleteOp(*workbook, colIds[2]));

    // Range should shrink to single column
    Range* range = workbook->getRange(rangeId);
    ASSERT_NE(range, nullptr);
    EXPECT_EQ(range->startColId, colIds[3]);
    EXPECT_EQ(range->endColId, colIds[3]);

    // Delete the remaining column
    applyOperation(*workbook, makeColDeleteOp(*workbook, colIds[3]));

    // Range should be invalidated/removed
    EXPECT_EQ(workbook->getRange(rangeId), nullptr);
}

// =============================================================================
// 5d: Test range shrinks when row deleted from inside range
// =============================================================================

TEST_F(RangeBoundaryTest, DeleteStartRowShrinksRange) {
    // Create a range from row1 to row3
    ID rangeId = createRange(colIds[0], rowIds[1], colIds[2], rowIds[3]);

    // Delete the start row (row1)
    Operation op = makeRowDeleteOp(*workbook, rowIds[1]);
    applyOperation(*workbook, op);

    // Range should shrink: new start is row2
    Range* range = workbook->getRange(rangeId);
    ASSERT_NE(range, nullptr);
    EXPECT_EQ(range->startRowId, rowIds[2]);  // Moved to next row
    EXPECT_EQ(range->endRowId, rowIds[3]);    // Unchanged
}

TEST_F(RangeBoundaryTest, DeleteEndRowShrinksRange) {
    // Create a range from row1 to row3
    ID rangeId = createRange(colIds[0], rowIds[1], colIds[2], rowIds[3]);

    // Delete the end row (row3)
    Operation op = makeRowDeleteOp(*workbook, rowIds[3]);
    applyOperation(*workbook, op);

    // Range should shrink: new end is row2
    Range* range = workbook->getRange(rangeId);
    ASSERT_NE(range, nullptr);
    EXPECT_EQ(range->startRowId, rowIds[1]);  // Unchanged
    EXPECT_EQ(range->endRowId, rowIds[2]);    // Moved to previous row
}

TEST_F(RangeBoundaryTest, DeleteMiddleRowDoesNotShrinkRange) {
    // Create a range from row1 to row4
    ID rangeId = createRange(colIds[0], rowIds[1], colIds[2], rowIds[4]);

    // Delete a middle row (row2) - not a corner
    Operation op = makeRowDeleteOp(*workbook, rowIds[2]);
    applyOperation(*workbook, op);

    // Range corners are unchanged
    Range* range = workbook->getRange(rangeId);
    ASSERT_NE(range, nullptr);
    EXPECT_EQ(range->startRowId, rowIds[1]);
    EXPECT_EQ(range->endRowId, rowIds[4]);
}

TEST_F(RangeBoundaryTest, DeleteRowOutsideRangeDoesNotAffectIt) {
    // Create a range from row2 to row4
    ID rangeId = createRange(colIds[0], rowIds[2], colIds[2], rowIds[4]);

    // Delete a row outside the range (row0)
    Operation op = makeRowDeleteOp(*workbook, rowIds[0]);
    applyOperation(*workbook, op);

    // Range should be unchanged
    Range* range = workbook->getRange(rangeId);
    ASSERT_NE(range, nullptr);
    EXPECT_EQ(range->startRowId, rowIds[2]);
    EXPECT_EQ(range->endRowId, rowIds[4]);
}

TEST_F(RangeBoundaryTest, DeleteBothBoundaryRowsInvalidatesRange) {
    // Create a two-row range from row2 to row3
    ID rangeId = createRange(colIds[0], rowIds[2], colIds[2], rowIds[3]);

    // Delete the start row
    applyOperation(*workbook, makeRowDeleteOp(*workbook, rowIds[2]));

    // Range should shrink to single row
    Range* range = workbook->getRange(rangeId);
    ASSERT_NE(range, nullptr);
    EXPECT_EQ(range->startRowId, rowIds[3]);
    EXPECT_EQ(range->endRowId, rowIds[3]);

    // Delete the remaining row
    applyOperation(*workbook, makeRowDeleteOp(*workbook, rowIds[3]));

    // Range should be invalidated/removed
    EXPECT_EQ(workbook->getRange(rangeId), nullptr);
}

// =============================================================================
// 5e: Test range survives when boundary axis deleted (moves to next)
// =============================================================================

TEST_F(RangeBoundaryTest, RangeSurvivesWhenStartColumnDeleted) {
    // Create a range from col0 to col3
    ID rangeId = createRange(colIds[0], rowIds[0], colIds[3], rowIds[2]);

    // Delete start column - range should survive by moving start to col1
    applyOperation(*workbook, makeColDeleteOp(*workbook, colIds[0]));

    Range* range = workbook->getRange(rangeId);
    ASSERT_NE(range, nullptr);
    EXPECT_EQ(range->startColId, colIds[1]);
    EXPECT_EQ(range->endColId, colIds[3]);
}

TEST_F(RangeBoundaryTest, RangeSurvivesWhenEndColumnDeleted) {
    // Create a range from col1 to col4
    ID rangeId = createRange(colIds[1], rowIds[0], colIds[4], rowIds[2]);

    // Delete end column - range should survive by moving end to col3
    applyOperation(*workbook, makeColDeleteOp(*workbook, colIds[4]));

    Range* range = workbook->getRange(rangeId);
    ASSERT_NE(range, nullptr);
    EXPECT_EQ(range->startColId, colIds[1]);
    EXPECT_EQ(range->endColId, colIds[3]);
}

TEST_F(RangeBoundaryTest, RangeSurvivesWhenStartRowDeleted) {
    // Create a range from row0 to row3
    ID rangeId = createRange(colIds[0], rowIds[0], colIds[2], rowIds[3]);

    // Delete start row - range should survive by moving start to row1
    applyOperation(*workbook, makeRowDeleteOp(*workbook, rowIds[0]));

    Range* range = workbook->getRange(rangeId);
    ASSERT_NE(range, nullptr);
    EXPECT_EQ(range->startRowId, rowIds[1]);
    EXPECT_EQ(range->endRowId, rowIds[3]);
}

TEST_F(RangeBoundaryTest, RangeSurvivesWhenEndRowDeleted) {
    // Create a range from row1 to row4
    ID rangeId = createRange(colIds[0], rowIds[1], colIds[2], rowIds[4]);

    // Delete end row - range should survive by moving end to row3
    applyOperation(*workbook, makeRowDeleteOp(*workbook, rowIds[4]));

    Range* range = workbook->getRange(rangeId);
    ASSERT_NE(range, nullptr);
    EXPECT_EQ(range->startRowId, rowIds[1]);
    EXPECT_EQ(range->endRowId, rowIds[3]);
}

TEST_F(RangeBoundaryTest, RangeSurvivesMultipleBoundaryDeletions) {
    // Create a range from col1 to col4
    ID rangeId = createRange(colIds[1], rowIds[0], colIds[4], rowIds[2]);

    // Delete start column twice
    applyOperation(*workbook, makeColDeleteOp(*workbook, colIds[1]));

    Range* range = workbook->getRange(rangeId);
    ASSERT_NE(range, nullptr);
    EXPECT_EQ(range->startColId, colIds[2]);
    EXPECT_EQ(range->endColId, colIds[4]);

    // Delete new start column
    applyOperation(*workbook, makeColDeleteOp(*workbook, colIds[2]));

    range = workbook->getRange(rangeId);
    ASSERT_NE(range, nullptr);
    EXPECT_EQ(range->startColId, colIds[3]);
    EXPECT_EQ(range->endColId, colIds[4]);
}

// =============================================================================
// 5f: Test range deleted when all axes removed
// =============================================================================

TEST_F(RangeBoundaryTest, SingleColumnRangeDeletedWhenColumnRemoved) {
    // Create a single-column range (col2 to col2)
    ID rangeId = createRange(colIds[2], rowIds[0], colIds[2], rowIds[2]);

    // Delete the only column
    applyOperation(*workbook, makeColDeleteOp(*workbook, colIds[2]));

    // Range should be removed
    EXPECT_EQ(workbook->getRange(rangeId), nullptr);
}

TEST_F(RangeBoundaryTest, SingleRowRangeDeletedWhenRowRemoved) {
    // Create a single-row range (row2 to row2)
    ID rangeId = createRange(colIds[0], rowIds[2], colIds[2], rowIds[2]);

    // Delete the only row
    applyOperation(*workbook, makeRowDeleteOp(*workbook, rowIds[2]));

    // Range should be removed
    EXPECT_EQ(workbook->getRange(rangeId), nullptr);
}

TEST_F(RangeBoundaryTest, SingleCellRangeDeletedWhenColumnRemoved) {
    // Create a single-cell range (col2/row2)
    ID rangeId = createRange(colIds[2], rowIds[2], colIds[2], rowIds[2]);

    // Delete the column
    applyOperation(*workbook, makeColDeleteOp(*workbook, colIds[2]));

    // Range should be removed (invalidated by column deletion)
    EXPECT_EQ(workbook->getRange(rangeId), nullptr);
}

TEST_F(RangeBoundaryTest, SingleCellRangeDeletedWhenRowRemoved) {
    // Create a single-cell range (col2/row2)
    ID rangeId = createRange(colIds[2], rowIds[2], colIds[2], rowIds[2]);

    // Delete the row
    applyOperation(*workbook, makeRowDeleteOp(*workbook, rowIds[2]));

    // Range should be removed (invalidated by row deletion)
    EXPECT_EQ(workbook->getRange(rangeId), nullptr);
}

TEST_F(RangeBoundaryTest, RangeDeletedWhenAllColumnsRemoved) {
    // Create a multi-column range (col2 to col4)
    ID rangeId = createRange(colIds[2], rowIds[0], colIds[4], rowIds[2]);

    // Delete all columns in the range one by one
    applyOperation(*workbook, makeColDeleteOp(*workbook, colIds[2]));
    ASSERT_NE(workbook->getRange(rangeId), nullptr);  // Still has col3-col4

    applyOperation(*workbook, makeColDeleteOp(*workbook, colIds[3]));
    ASSERT_NE(workbook->getRange(rangeId), nullptr);  // Still has col4

    applyOperation(*workbook, makeColDeleteOp(*workbook, colIds[4]));
    EXPECT_EQ(workbook->getRange(rangeId), nullptr);  // All columns gone
}

TEST_F(RangeBoundaryTest, RangeDeletedWhenAllRowsRemoved) {
    // Create a multi-row range (row2 to row4)
    ID rangeId = createRange(colIds[0], rowIds[2], colIds[2], rowIds[4]);

    // Delete all rows in the range one by one
    applyOperation(*workbook, makeRowDeleteOp(*workbook, rowIds[2]));
    ASSERT_NE(workbook->getRange(rangeId), nullptr);  // Still has row3-row4

    applyOperation(*workbook, makeRowDeleteOp(*workbook, rowIds[3]));
    ASSERT_NE(workbook->getRange(rangeId), nullptr);  // Still has row4

    applyOperation(*workbook, makeRowDeleteOp(*workbook, rowIds[4]));
    EXPECT_EQ(workbook->getRange(rangeId), nullptr);  // All rows gone
}

// =============================================================================
// 5g: Test all range flags during boundary changes
// =============================================================================

TEST_F(RangeBoundaryTest, MergeRangePreservesFlagsDuringColumnDelete) {
    // Create a MERGE range
    ID rangeId = createRange(colIds[1], rowIds[0], colIds[3], rowIds[2],
                             static_cast<uint8_t>(RangeFlags::MERGE));

    // Verify MERGE flag is set
    Range* range = workbook->getRange(rangeId);
    ASSERT_NE(range, nullptr);
    EXPECT_TRUE(range->hasFlag(RangeFlags::MERGE));

    // Delete start column
    applyOperation(*workbook, makeColDeleteOp(*workbook, colIds[1]));

    // Range should survive with MERGE flag preserved
    range = workbook->getRange(rangeId);
    ASSERT_NE(range, nullptr);
    EXPECT_TRUE(range->hasFlag(RangeFlags::MERGE));
    EXPECT_EQ(range->startColId, colIds[2]);
}

TEST_F(RangeBoundaryTest, StyleRangePreservesFlagsDuringRowDelete) {
    // Create a STYLE range
    ID rangeId = createRange(colIds[0], rowIds[1], colIds[2], rowIds[3],
                             static_cast<uint8_t>(RangeFlags::STYLE));

    // Verify STYLE flag is set
    Range* range = workbook->getRange(rangeId);
    ASSERT_NE(range, nullptr);
    EXPECT_TRUE(range->hasFlag(RangeFlags::STYLE));

    // Delete end row
    applyOperation(*workbook, makeRowDeleteOp(*workbook, rowIds[3]));

    // Range should survive with STYLE flag preserved
    range = workbook->getRange(rangeId);
    ASSERT_NE(range, nullptr);
    EXPECT_TRUE(range->hasFlag(RangeFlags::STYLE));
    EXPECT_EQ(range->endRowId, rowIds[2]);
}

TEST_F(RangeBoundaryTest, FormatRangePreservesFlagsDuringColumnDelete) {
    // Create a FORMAT range
    ID rangeId = createRange(colIds[1], rowIds[0], colIds[4], rowIds[2],
                             static_cast<uint8_t>(RangeFlags::FORMAT));

    Range* range = workbook->getRange(rangeId);
    ASSERT_NE(range, nullptr);
    EXPECT_TRUE(range->hasFlag(RangeFlags::FORMAT));

    // Delete middle column (not a corner)
    applyOperation(*workbook, makeColDeleteOp(*workbook, colIds[2]));

    // Range should be unchanged
    range = workbook->getRange(rangeId);
    ASSERT_NE(range, nullptr);
    EXPECT_TRUE(range->hasFlag(RangeFlags::FORMAT));
}

TEST_F(RangeBoundaryTest, ConditionalFormatRangePreservesFlagsDuringDelete) {
    // Create a CONDITIONAL_FORMAT range
    ID rangeId = createRange(colIds[1], rowIds[1], colIds[3], rowIds[3],
                             static_cast<uint8_t>(RangeFlags::CONDITIONAL_FORMAT));

    Range* range = workbook->getRange(rangeId);
    ASSERT_NE(range, nullptr);
    EXPECT_TRUE(range->hasFlag(RangeFlags::CONDITIONAL_FORMAT));

    // Delete start column
    applyOperation(*workbook, makeColDeleteOp(*workbook, colIds[1]));

    // Range should survive with flag preserved
    range = workbook->getRange(rangeId);
    ASSERT_NE(range, nullptr);
    EXPECT_TRUE(range->hasFlag(RangeFlags::CONDITIONAL_FORMAT));
}

TEST_F(RangeBoundaryTest, DataValidationRangePreservesFlagsDuringDelete) {
    // Create a DATA_VALIDATION range
    ID rangeId = createRange(colIds[0], rowIds[1], colIds[2], rowIds[4],
                             static_cast<uint8_t>(RangeFlags::DATA_VALIDATION));

    Range* range = workbook->getRange(rangeId);
    ASSERT_NE(range, nullptr);
    EXPECT_TRUE(range->hasFlag(RangeFlags::DATA_VALIDATION));

    // Delete end row
    applyOperation(*workbook, makeRowDeleteOp(*workbook, rowIds[4]));

    // Range should survive with flag preserved
    range = workbook->getRange(rangeId);
    ASSERT_NE(range, nullptr);
    EXPECT_TRUE(range->hasFlag(RangeFlags::DATA_VALIDATION));
}

TEST_F(RangeBoundaryTest, NamedRangePreservesFlagsDuringDelete) {
    // Create a NAMED range
    ID rangeId = createRange(colIds[1], rowIds[1], colIds[4], rowIds[3],
                             static_cast<uint8_t>(RangeFlags::NAMED));

    Range* range = workbook->getRange(rangeId);
    ASSERT_NE(range, nullptr);
    EXPECT_TRUE(range->hasFlag(RangeFlags::NAMED));

    // Delete start row
    applyOperation(*workbook, makeRowDeleteOp(*workbook, rowIds[1]));

    // Range should survive with flag preserved
    range = workbook->getRange(rangeId);
    ASSERT_NE(range, nullptr);
    EXPECT_TRUE(range->hasFlag(RangeFlags::NAMED));
}

TEST_F(RangeBoundaryTest, PrintAreaRangePreservesFlagsDuringDelete) {
    // Create a PRINT_AREA range
    ID rangeId = createRange(colIds[0], rowIds[0], colIds[3], rowIds[4],
                             static_cast<uint8_t>(RangeFlags::PRINT_AREA));

    Range* range = workbook->getRange(rangeId);
    ASSERT_NE(range, nullptr);
    EXPECT_TRUE(range->hasFlag(RangeFlags::PRINT_AREA));

    // Delete end column
    applyOperation(*workbook, makeColDeleteOp(*workbook, colIds[3]));

    // Range should survive with flag preserved
    range = workbook->getRange(rangeId);
    ASSERT_NE(range, nullptr);
    EXPECT_TRUE(range->hasFlag(RangeFlags::PRINT_AREA));
}

TEST_F(RangeBoundaryTest, FilterRangePreservesFlagsDuringDelete) {
    // Create a FILTER range
    ID rangeId = createRange(colIds[1], rowIds[0], colIds[4], rowIds[3],
                             static_cast<uint8_t>(RangeFlags::FILTER));

    Range* range = workbook->getRange(rangeId);
    ASSERT_NE(range, nullptr);
    EXPECT_TRUE(range->hasFlag(RangeFlags::FILTER));

    // Delete start column
    applyOperation(*workbook, makeColDeleteOp(*workbook, colIds[1]));

    // Range should survive with flag preserved
    range = workbook->getRange(rangeId);
    ASSERT_NE(range, nullptr);
    EXPECT_TRUE(range->hasFlag(RangeFlags::FILTER));
}

TEST_F(RangeBoundaryTest, MultipleFlagsPreservedDuringBoundaryChange) {
    // Create a range with MERGE | STYLE flags
    uint8_t combinedFlags =
        static_cast<uint8_t>(RangeFlags::MERGE) | static_cast<uint8_t>(RangeFlags::STYLE);
    ID rangeId = createRange(colIds[1], rowIds[1], colIds[4], rowIds[4], combinedFlags);

    Range* range = workbook->getRange(rangeId);
    ASSERT_NE(range, nullptr);
    EXPECT_TRUE(range->hasFlag(RangeFlags::MERGE));
    EXPECT_TRUE(range->hasFlag(RangeFlags::STYLE));

    // Delete start column and end row
    applyOperation(*workbook, makeColDeleteOp(*workbook, colIds[1]));
    applyOperation(*workbook, makeRowDeleteOp(*workbook, rowIds[4]));

    // Range should survive with both flags preserved
    range = workbook->getRange(rangeId);
    ASSERT_NE(range, nullptr);
    EXPECT_TRUE(range->hasFlag(RangeFlags::MERGE));
    EXPECT_TRUE(range->hasFlag(RangeFlags::STYLE));
    EXPECT_EQ(range->startColId, colIds[2]);
    EXPECT_EQ(range->endRowId, rowIds[3]);
}

TEST_F(RangeBoundaryTest, FlagsPreservedWhenRangeShrinksToSingleCell) {
    // Create a 2x2 range with MERGE flag
    ID rangeId = createRange(colIds[2], rowIds[2], colIds[3], rowIds[3],
                             static_cast<uint8_t>(RangeFlags::MERGE));

    // Delete one column and one row to shrink to single cell
    applyOperation(*workbook, makeColDeleteOp(*workbook, colIds[2]));
    applyOperation(*workbook, makeRowDeleteOp(*workbook, rowIds[2]));

    // Range should survive as single cell with MERGE flag
    Range* range = workbook->getRange(rangeId);
    ASSERT_NE(range, nullptr);
    EXPECT_TRUE(range->hasFlag(RangeFlags::MERGE));
    EXPECT_TRUE(range->isSingleCell());
}

// =============================================================================
// Additional Edge Cases
// =============================================================================

TEST_F(RangeBoundaryTest, MultipleRangesAffectedByColumnDeletion) {
    // Create multiple ranges sharing the same start column
    ID rangeId1 = createRange(colIds[1], rowIds[0], colIds[3], rowIds[2]);
    ID rangeId2 = createRange(colIds[1], rowIds[3], colIds[2], rowIds[4]);

    // Delete the shared start column
    applyOperation(*workbook, makeColDeleteOp(*workbook, colIds[1]));

    // Both ranges should shrink
    Range* range1 = workbook->getRange(rangeId1);
    Range* range2 = workbook->getRange(rangeId2);

    ASSERT_NE(range1, nullptr);
    ASSERT_NE(range2, nullptr);
    EXPECT_EQ(range1->startColId, colIds[2]);
    EXPECT_EQ(range2->startColId, colIds[2]);
}

TEST_F(RangeBoundaryTest, MultipleRangesAffectedByRowDeletion) {
    // Create multiple ranges sharing the same end row
    ID rangeId1 = createRange(colIds[0], rowIds[1], colIds[2], rowIds[3]);
    ID rangeId2 = createRange(colIds[3], rowIds[2], colIds[4], rowIds[3]);

    // Delete the shared end row
    applyOperation(*workbook, makeRowDeleteOp(*workbook, rowIds[3]));

    // Both ranges should shrink
    Range* range1 = workbook->getRange(rangeId1);
    Range* range2 = workbook->getRange(rangeId2);

    ASSERT_NE(range1, nullptr);
    ASSERT_NE(range2, nullptr);
    EXPECT_EQ(range1->endRowId, rowIds[2]);
    EXPECT_EQ(range2->endRowId, rowIds[2]);
}

TEST_F(RangeBoundaryTest, InsertAndDeleteSequenceMaintainsRangeIntegrity) {
    // Create a range from col1 to col3
    ID rangeId = createRange(colIds[1], rowIds[0], colIds[3], rowIds[2]);

    // Insert a column inside the range
    ID newColId = generate_id();
    std::string payload = R"({"pos":2,"size":100})";
    applyOperation(*workbook, makeColSetOp(*workbook, newColId, sheet_id, payload));

    // Range corners unchanged
    Range* range = workbook->getRange(rangeId);
    ASSERT_NE(range, nullptr);
    EXPECT_EQ(range->startColId, colIds[1]);
    EXPECT_EQ(range->endColId, colIds[3]);

    // Delete the inserted column
    applyOperation(*workbook, makeColDeleteOp(*workbook, newColId));

    // Range should still be intact
    range = workbook->getRange(rangeId);
    ASSERT_NE(range, nullptr);
    EXPECT_EQ(range->startColId, colIds[1]);
    EXPECT_EQ(range->endColId, colIds[3]);
}

TEST_F(RangeBoundaryTest, OverlappingRangesAffectedDifferentlyByDeletion) {
    // Create overlapping ranges:
    // Range1: col1-col3, row0-row2
    // Range2: col2-col4, row1-row3
    ID rangeId1 = createRange(colIds[1], rowIds[0], colIds[3], rowIds[2]);
    ID rangeId2 = createRange(colIds[2], rowIds[1], colIds[4], rowIds[3]);

    // Delete col3 (end of range1, middle of range2)
    applyOperation(*workbook, makeColDeleteOp(*workbook, colIds[3]));

    Range* range1 = workbook->getRange(rangeId1);
    Range* range2 = workbook->getRange(rangeId2);

    // Range1 should shrink (col3 was its end column)
    ASSERT_NE(range1, nullptr);
    EXPECT_EQ(range1->endColId, colIds[2]);

    // Range2 should be unchanged (col3 was in the middle)
    ASSERT_NE(range2, nullptr);
    EXPECT_EQ(range2->startColId, colIds[2]);
    EXPECT_EQ(range2->endColId, colIds[4]);
}

}  // namespace
}  // namespace cells
