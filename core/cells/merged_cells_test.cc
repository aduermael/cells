// =============================================================================
// Merged Cells Unit Tests
// =============================================================================
//
// Tests for merged cell operations using the unified Range system with MERGE flag.
// Merged cells use the top-left cell as the anchor for value/formula storage.
// Other cells in the merge are visually part of the merge but don't hold values.
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

class MergedCellsTest : public ::testing::Test {
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

    // Helper to create a merge range
    ID createMergeRange(const ID& startCol, const ID& startRow, const ID& endCol,
                        const ID& endRow) {
        ID rangeId = generate_id();
        std::string payload = "{\"startCol\":\"" + startCol.toString() + "\",";
        payload += "\"startRow\":\"" + startRow.toString() + "\",";
        payload += "\"endCol\":\"" + endCol.toString() + "\",";
        payload += "\"endRow\":\"" + endRow.toString() + "\",";
        payload += "\"flags\":" + std::to_string(static_cast<uint8_t>(RangeFlags::MERGE)) + "}";

        Operation op = makeRangeSetOp(*workbook, rangeId, payload);
        applyOperation(*workbook, op);
        return rangeId;
    }

    // Helper to create a cell at a position
    ID createCell(const ID& colId, const ID& rowId, double value) {
        ID cellId = generate_id();
        std::string payload = R"({"col":")" + colId.toString() + R"(","row":")" + rowId.toString() +
                              R"(","t":"n","v":")" + std::to_string(value) + R"("})";

        Operation op = makeCellSetOp(*workbook, cellId, sheet_id, payload);
        applyOperation(*workbook, op);
        return cellId;
    }

    // Helper to create a cell with text value
    ID createTextCell(const ID& colId, const ID& rowId, const std::string& text) {
        ID cellId = generate_id();
        std::string payload = R"({"col":")" + colId.toString() + R"(","row":")" + rowId.toString() +
                              R"(","t":"s","v":")" + text + R"("})";

        Operation op = makeCellSetOp(*workbook, cellId, sheet_id, payload);
        applyOperation(*workbook, op);
        return cellId;
    }

    // Helper to create a cell with formula
    ID createFormulaCell(const ID& colId, const ID& rowId, const std::string& formula) {
        ID cellId = generate_id();
        std::string payload = R"({"col":")" + colId.toString() + R"(","row":")" + rowId.toString() +
                              R"(","t":"f","v":")" + formula + R"("})";

        Operation op = makeCellSetOp(*workbook, cellId, sheet_id, payload);
        applyOperation(*workbook, op);
        return cellId;
    }

    // Helper to get a merge range's column span
    int getMergeColSpan(const ID& rangeId) {
        Range* range = workbook->getRange(rangeId);
        if (!range)
            return -1;

        Axis* startCol = sheet_ptr->getColumn(range->startColId);
        Axis* endCol = sheet_ptr->getColumn(range->endColId);
        if (!startCol || !endCol)
            return -1;

        return static_cast<int>(endCol->position) - static_cast<int>(startCol->position) + 1;
    }

    // Helper to get a merge range's row span
    int getMergeRowSpan(const ID& rangeId) {
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
// 6a: Test creating merged cell ranges
// =============================================================================

TEST_F(MergedCellsTest, CreateMergeRangeBasic) {
    // Create a 2x2 merge starting at A1 (col0, row0)
    ID mergeId = createMergeRange(colIds[0], rowIds[0], colIds[1], rowIds[1]);

    Range* range = workbook->getRange(mergeId);
    ASSERT_NE(range, nullptr);
    EXPECT_TRUE(range->hasFlag(RangeFlags::MERGE));
    EXPECT_TRUE(range->isMerge());
    EXPECT_EQ(range->startColId, colIds[0]);
    EXPECT_EQ(range->startRowId, rowIds[0]);
    EXPECT_EQ(range->endColId, colIds[1]);
    EXPECT_EQ(range->endRowId, rowIds[1]);
}

TEST_F(MergedCellsTest, CreateMergeRangeSingleRow) {
    // Create a horizontal merge spanning 3 columns
    ID mergeId = createMergeRange(colIds[0], rowIds[0], colIds[2], rowIds[0]);

    Range* range = workbook->getRange(mergeId);
    ASSERT_NE(range, nullptr);
    EXPECT_TRUE(range->isMerge());
    EXPECT_EQ(getMergeColSpan(mergeId), 3);
    EXPECT_EQ(getMergeRowSpan(mergeId), 1);
}

TEST_F(MergedCellsTest, CreateMergeRangeSingleColumn) {
    // Create a vertical merge spanning 4 rows
    ID mergeId = createMergeRange(colIds[0], rowIds[0], colIds[0], rowIds[3]);

    Range* range = workbook->getRange(mergeId);
    ASSERT_NE(range, nullptr);
    EXPECT_TRUE(range->isMerge());
    EXPECT_EQ(getMergeColSpan(mergeId), 1);
    EXPECT_EQ(getMergeRowSpan(mergeId), 4);
}

TEST_F(MergedCellsTest, CreateLargeMergeRange) {
    // Create a large 4x5 merge
    ID mergeId = createMergeRange(colIds[1], rowIds[0], colIds[4], rowIds[4]);

    Range* range = workbook->getRange(mergeId);
    ASSERT_NE(range, nullptr);
    EXPECT_TRUE(range->isMerge());
    EXPECT_EQ(getMergeColSpan(mergeId), 4);
    EXPECT_EQ(getMergeRowSpan(mergeId), 5);
}

TEST_F(MergedCellsTest, MergeRangeIndexedInRTree) {
    // Create a merge at B2:C3
    ID mergeId = createMergeRange(colIds[1], rowIds[1], colIds[2], rowIds[2]);

    // Query positions inside the merge
    auto rangesAtB2 = sheet_ptr->getRangesAt(1, 1, RangeFlags::MERGE);
    ASSERT_EQ(rangesAtB2.size(), 1);
    EXPECT_EQ(rangesAtB2[0]->id, mergeId);

    auto rangesAtC3 = sheet_ptr->getRangesAt(2, 2, RangeFlags::MERGE);
    ASSERT_EQ(rangesAtC3.size(), 1);
    EXPECT_EQ(rangesAtC3[0]->id, mergeId);

    // Query position outside the merge
    auto rangesAtA1 = sheet_ptr->getRangesAt(0, 0, RangeFlags::MERGE);
    EXPECT_TRUE(rangesAtA1.empty());

    auto rangesAtD4 = sheet_ptr->getRangesAt(3, 3, RangeFlags::MERGE);
    EXPECT_TRUE(rangesAtD4.empty());
}

TEST_F(MergedCellsTest, MultipleMergeRangesNonOverlapping) {
    // Create two non-overlapping merges
    ID merge1 = createMergeRange(colIds[0], rowIds[0], colIds[1], rowIds[1]);
    ID merge2 = createMergeRange(colIds[3], rowIds[3], colIds[4], rowIds[4]);

    // Both merges should exist
    Range* range1 = workbook->getRange(merge1);
    Range* range2 = workbook->getRange(merge2);
    ASSERT_NE(range1, nullptr);
    ASSERT_NE(range2, nullptr);
    EXPECT_TRUE(range1->isMerge());
    EXPECT_TRUE(range2->isMerge());

    // Each position should have only one merge
    auto rangesAtA1 = sheet_ptr->getRangesAt(0, 0, RangeFlags::MERGE);
    ASSERT_EQ(rangesAtA1.size(), 1);
    EXPECT_EQ(rangesAtA1[0]->id, merge1);

    auto rangesAtD4 = sheet_ptr->getRangesAt(3, 3, RangeFlags::MERGE);
    ASSERT_EQ(rangesAtD4.size(), 1);
    EXPECT_EQ(rangesAtD4[0]->id, merge2);
}

TEST_F(MergedCellsTest, CreateMergeRangeAtSheetEdge) {
    // Create a merge at the first column/row (position 0)
    ID mergeId = createMergeRange(colIds[0], rowIds[0], colIds[2], rowIds[0]);

    Range* range = workbook->getRange(mergeId);
    ASSERT_NE(range, nullptr);
    EXPECT_TRUE(range->isMerge());
    EXPECT_EQ(range->startColId, colIds[0]);
    EXPECT_EQ(range->startRowId, rowIds[0]);
}

// =============================================================================
// 6b: Test merged cell anchor (top-left) holds value, others empty
// =============================================================================

TEST_F(MergedCellsTest, AnchorCellHoldsValue) {
    // Create a cell at the anchor position (top-left)
    ID cellId = createCell(colIds[0], rowIds[0], 42.0);

    // Create a merge that covers this cell
    createMergeRange(colIds[0], rowIds[0], colIds[1], rowIds[1]);

    // Verify the anchor cell still has its value
    Cell* anchorCell = sheet_ptr->getCellAt(colIds[0], rowIds[0]);
    ASSERT_NE(anchorCell, nullptr);
    EXPECT_EQ(anchorCell->id, cellId);
    EXPECT_EQ(anchorCell->value.type, CellValueType::NUMBER);
    EXPECT_EQ(anchorCell->value.asNumber(), 42.0);
}

TEST_F(MergedCellsTest, NonAnchorCellsEmpty) {
    // Create a cell at the anchor position
    createCell(colIds[0], rowIds[0], 42.0);

    // Create a 2x2 merge
    createMergeRange(colIds[0], rowIds[0], colIds[1], rowIds[1]);

    // Non-anchor positions should have no cells (unless created separately)
    Cell* cellB1 = sheet_ptr->getCellAt(colIds[1], rowIds[0]);
    Cell* cellA2 = sheet_ptr->getCellAt(colIds[0], rowIds[1]);
    Cell* cellB2 = sheet_ptr->getCellAt(colIds[1], rowIds[1]);

    // These are nullptr since no cells were created there
    EXPECT_EQ(cellB1, nullptr);
    EXPECT_EQ(cellA2, nullptr);
    EXPECT_EQ(cellB2, nullptr);
}

TEST_F(MergedCellsTest, AnchorIsTopLeft) {
    // Create a merge from B2:D4
    ID mergeId = createMergeRange(colIds[1], rowIds[1], colIds[3], rowIds[3]);

    Range* range = workbook->getRange(mergeId);
    ASSERT_NE(range, nullptr);

    // The anchor (top-left) should be at colIds[1], rowIds[1]
    Axis* startCol = sheet_ptr->getColumn(range->startColId);
    Axis* startRow = sheet_ptr->getRow(range->startRowId);

    ASSERT_NE(startCol, nullptr);
    ASSERT_NE(startRow, nullptr);
    EXPECT_EQ(startCol->position, 1);  // Column B
    EXPECT_EQ(startRow->position, 1);  // Row 2
}

TEST_F(MergedCellsTest, ValueAtAnchorPreservedAfterMerge) {
    // Create text value at A1
    createTextCell(colIds[0], rowIds[0], "Header");

    // Merge A1:C1
    createMergeRange(colIds[0], rowIds[0], colIds[2], rowIds[0]);

    // Verify text is preserved
    Cell* cell = sheet_ptr->getCellAt(colIds[0], rowIds[0]);
    ASSERT_NE(cell, nullptr);
    EXPECT_EQ(cell->value.type, CellValueType::STRING);
    EXPECT_EQ(cell->value.asString(), "Header");
}

TEST_F(MergedCellsTest, RangeIsAnchorPositionHelper) {
    // Test the rangeIsAnchorPosition helper function
    // Range from (1,1) to (3,3)
    EXPECT_TRUE(rangeIsAnchorPosition(1, 1, 3, 3, 1, 1));   // Top-left is anchor
    EXPECT_FALSE(rangeIsAnchorPosition(1, 1, 3, 3, 2, 2));  // Middle is not anchor
    EXPECT_FALSE(rangeIsAnchorPosition(1, 1, 3, 3, 3, 3));  // Bottom-right is not anchor
    EXPECT_FALSE(rangeIsAnchorPosition(1, 1, 3, 3, 1, 2));  // Not anchor
}

TEST_F(MergedCellsTest, RangeContainsPositionHelper) {
    // Test the rangeContainsPosition helper function
    // Range from (1,1) to (3,3)
    EXPECT_TRUE(rangeContainsPosition(1, 1, 3, 3, 1, 1));   // Top-left
    EXPECT_TRUE(rangeContainsPosition(1, 1, 3, 3, 2, 2));   // Middle
    EXPECT_TRUE(rangeContainsPosition(1, 1, 3, 3, 3, 3));   // Bottom-right
    EXPECT_TRUE(rangeContainsPosition(1, 1, 3, 3, 1, 3));   // Left edge
    EXPECT_FALSE(rangeContainsPosition(1, 1, 3, 3, 0, 0));  // Outside (top-left)
    EXPECT_FALSE(rangeContainsPosition(1, 1, 3, 3, 4, 4));  // Outside (bottom-right)
    EXPECT_FALSE(rangeContainsPosition(1, 1, 3, 3, 0, 2));  // Outside left
}

// =============================================================================
// 6c: Test inserting column/row inside merged range (expands merge)
// =============================================================================

TEST_F(MergedCellsTest, InsertColumnInsideMergeExpandsIt) {
    // Create a merge from col1 to col3 (positions 1, 2, 3)
    ID mergeId = createMergeRange(colIds[1], rowIds[0], colIds[3], rowIds[2]);

    // Verify initial span
    EXPECT_EQ(getMergeColSpan(mergeId), 3);

    // Insert a new column at position 2 (inside the merge)
    ID newColId = generate_id();
    std::string payload = R"({"pos":2,"size":100})";
    Operation op = makeColSetOp(*workbook, newColId, sheet_id, payload);
    applyOperation(*workbook, op);

    // The merge UUID corners haven't changed, but there's now an extra column inside
    Range* range = workbook->getRange(mergeId);
    ASSERT_NE(range, nullptr);
    EXPECT_EQ(range->startColId, colIds[1]);
    EXPECT_EQ(range->endColId, colIds[3]);

    // The new column should exist at position 2
    Axis* newCol = sheet_ptr->getColumn(newColId);
    ASSERT_NE(newCol, nullptr);
    EXPECT_EQ(newCol->position, 2);
}

TEST_F(MergedCellsTest, InsertRowInsideMergeExpandsIt) {
    // Create a merge from row1 to row3
    ID mergeId = createMergeRange(colIds[0], rowIds[1], colIds[2], rowIds[3]);

    // Verify initial span
    EXPECT_EQ(getMergeRowSpan(mergeId), 3);

    // Insert a new row at position 2 (inside the merge)
    ID newRowId = generate_id();
    std::string payload = R"({"pos":2,"size":21})";
    Operation op = makeRowSetOp(*workbook, newRowId, sheet_id, payload);
    applyOperation(*workbook, op);

    // The merge UUID corners haven't changed
    Range* range = workbook->getRange(mergeId);
    ASSERT_NE(range, nullptr);
    EXPECT_EQ(range->startRowId, rowIds[1]);
    EXPECT_EQ(range->endRowId, rowIds[3]);

    // The new row exists
    Axis* newRow = sheet_ptr->getRow(newRowId);
    ASSERT_NE(newRow, nullptr);
    EXPECT_EQ(newRow->position, 2);
}

TEST_F(MergedCellsTest, InsertColumnAtMergeStartBoundary) {
    // Create a merge from col1 to col3
    ID mergeId = createMergeRange(colIds[1], rowIds[0], colIds[3], rowIds[2]);

    // Insert a column at position 1 (same as start)
    ID newColId = generate_id();
    std::string payload = R"({"pos":1,"size":100})";
    Operation op = makeColSetOp(*workbook, newColId, sheet_id, payload);
    applyOperation(*workbook, op);

    // Merge corners unchanged (they use UUIDs)
    Range* range = workbook->getRange(mergeId);
    ASSERT_NE(range, nullptr);
    EXPECT_EQ(range->startColId, colIds[1]);
}

TEST_F(MergedCellsTest, InsertColumnOutsideMergeDoesNotAffectIt) {
    // Create a merge from col2 to col4
    ID mergeId = createMergeRange(colIds[2], rowIds[0], colIds[4], rowIds[2]);

    // Insert a column at position 0 (before the merge)
    ID newColId = generate_id();
    std::string payload = R"({"pos":0,"size":100})";
    Operation op = makeColSetOp(*workbook, newColId, sheet_id, payload);
    applyOperation(*workbook, op);

    // Merge corners unchanged
    Range* range = workbook->getRange(mergeId);
    ASSERT_NE(range, nullptr);
    EXPECT_EQ(range->startColId, colIds[2]);
    EXPECT_EQ(range->endColId, colIds[4]);
}

// =============================================================================
// 6d: Test deleting column/row inside merged range (shrinks merge)
// =============================================================================

TEST_F(MergedCellsTest, DeleteStartColumnShrinksMerge) {
    // Create a merge from col1 to col3
    ID mergeId = createMergeRange(colIds[1], rowIds[0], colIds[3], rowIds[2]);

    // Delete the start column
    Operation op = makeColDeleteOp(*workbook, colIds[1]);
    applyOperation(*workbook, op);

    // Merge should shrink: new start is col2
    Range* range = workbook->getRange(mergeId);
    ASSERT_NE(range, nullptr);
    EXPECT_EQ(range->startColId, colIds[2]);
    EXPECT_EQ(range->endColId, colIds[3]);
    EXPECT_TRUE(range->isMerge());
}

TEST_F(MergedCellsTest, DeleteEndColumnShrinksMerge) {
    // Create a merge from col1 to col3
    ID mergeId = createMergeRange(colIds[1], rowIds[0], colIds[3], rowIds[2]);

    // Delete the end column
    Operation op = makeColDeleteOp(*workbook, colIds[3]);
    applyOperation(*workbook, op);

    // Merge should shrink: new end is col2
    Range* range = workbook->getRange(mergeId);
    ASSERT_NE(range, nullptr);
    EXPECT_EQ(range->startColId, colIds[1]);
    EXPECT_EQ(range->endColId, colIds[2]);
}

TEST_F(MergedCellsTest, DeleteMiddleColumnDoesNotShrinkMerge) {
    // Create a merge from col1 to col4
    ID mergeId = createMergeRange(colIds[1], rowIds[0], colIds[4], rowIds[2]);

    // Delete middle column (not a corner)
    Operation op = makeColDeleteOp(*workbook, colIds[2]);
    applyOperation(*workbook, op);

    // Merge corners unchanged (col2 was not a corner)
    Range* range = workbook->getRange(mergeId);
    ASSERT_NE(range, nullptr);
    EXPECT_EQ(range->startColId, colIds[1]);
    EXPECT_EQ(range->endColId, colIds[4]);
}

TEST_F(MergedCellsTest, DeleteStartRowShrinksMerge) {
    // Create a merge from row1 to row3
    ID mergeId = createMergeRange(colIds[0], rowIds[1], colIds[2], rowIds[3]);

    // Delete the start row
    Operation op = makeRowDeleteOp(*workbook, rowIds[1]);
    applyOperation(*workbook, op);

    // Merge should shrink
    Range* range = workbook->getRange(mergeId);
    ASSERT_NE(range, nullptr);
    EXPECT_EQ(range->startRowId, rowIds[2]);
    EXPECT_EQ(range->endRowId, rowIds[3]);
}

TEST_F(MergedCellsTest, DeleteEndRowShrinksMerge) {
    // Create a merge from row1 to row3
    ID mergeId = createMergeRange(colIds[0], rowIds[1], colIds[2], rowIds[3]);

    // Delete the end row
    Operation op = makeRowDeleteOp(*workbook, rowIds[3]);
    applyOperation(*workbook, op);

    // Merge should shrink
    Range* range = workbook->getRange(mergeId);
    ASSERT_NE(range, nullptr);
    EXPECT_EQ(range->startRowId, rowIds[1]);
    EXPECT_EQ(range->endRowId, rowIds[2]);
}

TEST_F(MergedCellsTest, DeleteAllColumnsRemovesMerge) {
    // Create a 2-column merge
    ID mergeId = createMergeRange(colIds[2], rowIds[0], colIds[3], rowIds[2]);

    // Delete both boundary columns
    applyOperation(*workbook, makeColDeleteOp(*workbook, colIds[2]));

    // Merge should shrink to single column
    Range* range = workbook->getRange(mergeId);
    ASSERT_NE(range, nullptr);

    // Delete the remaining column
    applyOperation(*workbook, makeColDeleteOp(*workbook, colIds[3]));

    // Merge should be removed
    EXPECT_EQ(workbook->getRange(mergeId), nullptr);
}

TEST_F(MergedCellsTest, DeleteAllRowsRemovesMerge) {
    // Create a 2-row merge
    ID mergeId = createMergeRange(colIds[0], rowIds[2], colIds[2], rowIds[3]);

    // Delete both boundary rows
    applyOperation(*workbook, makeRowDeleteOp(*workbook, rowIds[2]));
    applyOperation(*workbook, makeRowDeleteOp(*workbook, rowIds[3]));

    // Merge should be removed
    EXPECT_EQ(workbook->getRange(mergeId), nullptr);
}

// =============================================================================
// 6e: Test formulas referencing merged cells (return anchor value)
// =============================================================================

TEST_F(MergedCellsTest, FormulaCanReferenceAnchorCell) {
    // Create a value at the anchor position
    createCell(colIds[0], rowIds[0], 100.0);

    // Create a merge starting at that cell
    createMergeRange(colIds[0], rowIds[0], colIds[1], rowIds[1]);

    // Create a formula that references the anchor cell
    createFormulaCell(colIds[3], rowIds[0], "=A1");

    // The anchor cell should still be accessible
    Cell* anchor = sheet_ptr->getCellAt(colIds[0], rowIds[0]);
    ASSERT_NE(anchor, nullptr);
    EXPECT_EQ(anchor->value.type, CellValueType::NUMBER);
    EXPECT_EQ(anchor->value.asNumber(), 100.0);
}

TEST_F(MergedCellsTest, CellAtNonAnchorPositionIsNull) {
    // Create a value at the anchor position
    createCell(colIds[0], rowIds[0], 100.0);

    // Create a 2x2 merge
    createMergeRange(colIds[0], rowIds[0], colIds[1], rowIds[1]);

    // Non-anchor position has no cell
    Cell* nonAnchor = sheet_ptr->getCellAt(colIds[1], rowIds[1]);
    EXPECT_EQ(nonAnchor, nullptr);
}

TEST_F(MergedCellsTest, MergeDoesNotDuplicateValue) {
    // Create a value at anchor
    createCell(colIds[0], rowIds[0], 42.0);

    // Create merge
    createMergeRange(colIds[0], rowIds[0], colIds[2], rowIds[2]);

    // Only one cell should exist in the merged area
    int cellCount = 0;
    for (int col = 0; col <= 2; col++) {
        for (int row = 0; row <= 2; row++) {
            Cell* cell = sheet_ptr->getCellAt(colIds[col], rowIds[row]);
            if (cell != nullptr) {
                cellCount++;
            }
        }
    }
    EXPECT_EQ(cellCount, 1);  // Only the anchor cell
}

TEST_F(MergedCellsTest, FormulaReferencingMergedRangeGetsAnchorValue) {
    // Create value at anchor
    createCell(colIds[0], rowIds[0], 50.0);

    // Create merge
    createMergeRange(colIds[0], rowIds[0], colIds[1], rowIds[1]);

    // Create a SUM formula that covers the merged range
    createFormulaCell(colIds[3], rowIds[0], "=SUM(A1:B2)");

    // The formula should see the value at A1 (anchor)
    // Non-anchor cells in merge are empty, so SUM should be 50
    Cell* anchor = sheet_ptr->getCellAt(colIds[0], rowIds[0]);
    ASSERT_NE(anchor, nullptr);
    EXPECT_EQ(anchor->value.asNumber(), 50.0);
}

// =============================================================================
// 6f: Test unmerging cells preserves anchor value
// =============================================================================

TEST_F(MergedCellsTest, UnmergeByDeletingRangePreservesValue) {
    // Create value at anchor
    ID cellId = createCell(colIds[0], rowIds[0], 123.0);

    // Create merge
    ID mergeId = createMergeRange(colIds[0], rowIds[0], colIds[1], rowIds[1]);

    // Delete the merge range
    std::string payload = "{\"sheet\":\"" + sheet_id.toString() + "\"}";
    Operation deleteOp = makeRangeDeleteOp(*workbook, mergeId, payload);
    applyOperation(*workbook, deleteOp);

    // Merge should be gone
    EXPECT_EQ(workbook->getRange(mergeId), nullptr);

    // But the anchor cell value should still exist
    Cell* cell = sheet_ptr->getCellAt(colIds[0], rowIds[0]);
    ASSERT_NE(cell, nullptr);
    EXPECT_EQ(cell->id, cellId);
    EXPECT_EQ(cell->value.type, CellValueType::NUMBER);
    EXPECT_EQ(cell->value.asNumber(), 123.0);
}

TEST_F(MergedCellsTest, UnmergeByRemovingMergeFlagPreservesValue) {
    // Create text value at anchor
    createTextCell(colIds[0], rowIds[0], "Preserved");

    // Create merge
    ID mergeId = createMergeRange(colIds[0], rowIds[0], colIds[2], rowIds[2]);

    // Remove MERGE flag by updating to flags=0
    std::string payload = "{\"flags\":0}";
    Operation updateOp = makeRangeSetOp(*workbook, mergeId, payload);
    applyOperation(*workbook, updateOp);

    // Range still exists but is not a merge
    Range* range = workbook->getRange(mergeId);
    ASSERT_NE(range, nullptr);
    EXPECT_FALSE(range->isMerge());

    // Anchor cell value is preserved
    Cell* cell = sheet_ptr->getCellAt(colIds[0], rowIds[0]);
    ASSERT_NE(cell, nullptr);
    EXPECT_EQ(cell->value.type, CellValueType::STRING);
    EXPECT_EQ(cell->value.asString(), "Preserved");
}

TEST_F(MergedCellsTest, UnmergeMultipleMergesPreservesAllValues) {
    // Create two merges with values
    createCell(colIds[0], rowIds[0], 111.0);
    createCell(colIds[3], rowIds[0], 222.0);

    ID merge1 = createMergeRange(colIds[0], rowIds[0], colIds[1], rowIds[1]);
    ID merge2 = createMergeRange(colIds[3], rowIds[0], colIds[4], rowIds[1]);

    // Delete both merges
    std::string payload = "{\"sheet\":\"" + sheet_id.toString() + "\"}";
    applyOperation(*workbook, makeRangeDeleteOp(*workbook, merge1, payload));
    applyOperation(*workbook, makeRangeDeleteOp(*workbook, merge2, payload));

    // Both values should be preserved
    Cell* cellA1 = sheet_ptr->getCellAt(colIds[0], rowIds[0]);
    Cell* cellD1 = sheet_ptr->getCellAt(colIds[3], rowIds[0]);

    ASSERT_NE(cellA1, nullptr);
    ASSERT_NE(cellD1, nullptr);
    EXPECT_EQ(cellA1->value.asNumber(), 111.0);
    EXPECT_EQ(cellD1->value.asNumber(), 222.0);
}

TEST_F(MergedCellsTest, RemergeAfterUnmerge) {
    // Create value and merge
    createCell(colIds[0], rowIds[0], 42.0);
    ID mergeId = createMergeRange(colIds[0], rowIds[0], colIds[1], rowIds[1]);

    // Unmerge
    std::string payload = "{\"sheet\":\"" + sheet_id.toString() + "\"}";
    applyOperation(*workbook, makeRangeDeleteOp(*workbook, mergeId, payload));

    // Re-merge with a new range
    ID newMergeId = createMergeRange(colIds[0], rowIds[0], colIds[2], rowIds[2]);

    // Value is still at anchor
    Cell* cell = sheet_ptr->getCellAt(colIds[0], rowIds[0]);
    ASSERT_NE(cell, nullptr);
    EXPECT_EQ(cell->value.asNumber(), 42.0);

    // New merge exists
    Range* range = workbook->getRange(newMergeId);
    ASSERT_NE(range, nullptr);
    EXPECT_TRUE(range->isMerge());
}

// =============================================================================
// 6g: Test concurrent merge operations from multiple peers
// =============================================================================

TEST_F(MergedCellsTest, ConcurrentMergeCreationsDifferentAreas) {
    // Simulate two peers creating non-overlapping merges
    // Peer 1 creates merge at A1:B2
    // Peer 2 creates merge at D4:E5

    // Peer 1 operation (lower HLC)
    HLC hlc1 = workbook->getCurrentHLC();
    ID mergeId1 = generate_id();
    std::string payload1 = "{\"startCol\":\"" + colIds[0].toString() + "\",";
    payload1 += "\"startRow\":\"" + rowIds[0].toString() + "\",";
    payload1 += "\"endCol\":\"" + colIds[1].toString() + "\",";
    payload1 += "\"endRow\":\"" + rowIds[1].toString() + "\",";
    payload1 += "\"flags\":" + std::to_string(static_cast<uint8_t>(RangeFlags::MERGE)) + "}";

    Operation op1(hlc1, OpType::RANGE_SET, mergeId1, payload1);
    op1.sheetId = sheet_id;
    applyOperation(*workbook, op1);

    // Peer 2 operation (higher HLC)
    HLC hlc2 = workbook->getCurrentHLC();
    ID mergeId2 = generate_id();
    std::string payload2 = "{\"startCol\":\"" + colIds[3].toString() + "\",";
    payload2 += "\"startRow\":\"" + rowIds[3].toString() + "\",";
    payload2 += "\"endCol\":\"" + colIds[4].toString() + "\",";
    payload2 += "\"endRow\":\"" + rowIds[4].toString() + "\",";
    payload2 += "\"flags\":" + std::to_string(static_cast<uint8_t>(RangeFlags::MERGE)) + "}";

    Operation op2(hlc2, OpType::RANGE_SET, mergeId2, payload2);
    op2.sheetId = sheet_id;
    applyOperation(*workbook, op2);

    // Both merges should exist
    Range* range1 = workbook->getRange(mergeId1);
    Range* range2 = workbook->getRange(mergeId2);
    ASSERT_NE(range1, nullptr);
    ASSERT_NE(range2, nullptr);
    EXPECT_TRUE(range1->isMerge());
    EXPECT_TRUE(range2->isMerge());
}

TEST_F(MergedCellsTest, ConcurrentMergeAndCellEdit) {
    // Peer 1 creates a merge, Peer 2 edits cell in the merge area
    // Both operations should succeed

    // Create merge
    ID mergeId = createMergeRange(colIds[0], rowIds[0], colIds[1], rowIds[1]);

    // Edit cell at anchor position after merge
    HLC hlc = workbook->getCurrentHLC();
    ID cellId = generate_id();
    std::string payload = R"({"col":")" + colIds[0].toString() + R"(","row":")" +
                          rowIds[0].toString() + R"(","t":"n","v":"999"})";

    Operation cellOp(hlc, OpType::CELL_SET, cellId, payload);
    cellOp.sheetId = sheet_id;
    applyOperation(*workbook, cellOp);

    // Both merge and cell should exist
    Range* range = workbook->getRange(mergeId);
    Cell* cell = sheet_ptr->getCellAt(colIds[0], rowIds[0]);

    ASSERT_NE(range, nullptr);
    ASSERT_NE(cell, nullptr);
    EXPECT_TRUE(range->isMerge());
    EXPECT_EQ(cell->value.asNumber(), 999.0);
}

TEST_F(MergedCellsTest, ConcurrentMergeAndColumnInsert) {
    // Peer 1 creates merge, Peer 2 inserts column inside merge area
    ID mergeId = createMergeRange(colIds[1], rowIds[0], colIds[3], rowIds[2]);

    // Insert column at position 2 (inside merge)
    HLC hlc = workbook->getCurrentHLC();
    ID newColId = generate_id();
    std::string payload = R"({"pos":2,"size":100})";

    Operation insertOp(hlc, OpType::COL_SET, newColId, payload);
    insertOp.sheetId = sheet_id;
    applyOperation(*workbook, insertOp);

    // Merge should still exist with same corners (UUIDs)
    Range* range = workbook->getRange(mergeId);
    ASSERT_NE(range, nullptr);
    EXPECT_EQ(range->startColId, colIds[1]);
    EXPECT_EQ(range->endColId, colIds[3]);

    // New column should exist
    Axis* newCol = sheet_ptr->getColumn(newColId);
    ASSERT_NE(newCol, nullptr);
}

TEST_F(MergedCellsTest, ConcurrentMergeAndColumnDelete) {
    // Create merge and delete a boundary column
    ID mergeId = createMergeRange(colIds[1], rowIds[0], colIds[3], rowIds[2]);

    // Delete start column
    HLC hlc = workbook->getCurrentHLC();
    Operation deleteOp(hlc, OpType::COL_DELETE, colIds[1], "");
    applyOperation(*workbook, deleteOp);

    // Merge should shrink
    Range* range = workbook->getRange(mergeId);
    ASSERT_NE(range, nullptr);
    EXPECT_EQ(range->startColId, colIds[2]);
}

TEST_F(MergedCellsTest, ConcurrentMergeUpdateFlags) {
    // Create merge, then update its flags concurrently
    ID mergeId = createMergeRange(colIds[0], rowIds[0], colIds[1], rowIds[1]);

    // Update flags to add STYLE flag
    HLC hlc = workbook->getCurrentHLC();
    uint8_t newFlags =
        static_cast<uint8_t>(RangeFlags::MERGE) | static_cast<uint8_t>(RangeFlags::STYLE);
    std::string payload = "{\"flags\":" + std::to_string(newFlags) + "}";

    Operation updateOp(hlc, OpType::RANGE_SET, mergeId, payload);
    applyOperation(*workbook, updateOp);

    // Merge should have both flags
    Range* range = workbook->getRange(mergeId);
    ASSERT_NE(range, nullptr);
    EXPECT_TRUE(range->hasFlag(RangeFlags::MERGE));
    EXPECT_TRUE(range->hasFlag(RangeFlags::STYLE));
}

TEST_F(MergedCellsTest, LWWResolvesConflictingMergeUpdates) {
    // Create merge
    ID mergeId = createMergeRange(colIds[0], rowIds[0], colIds[1], rowIds[1]);

    // Two peers update the same merge with different flags
    // Peer 1 sets MERGE | STYLE (earlier HLC)
    HLC hlc1 = workbook->getCurrentHLC();
    uint8_t flags1 =
        static_cast<uint8_t>(RangeFlags::MERGE) | static_cast<uint8_t>(RangeFlags::STYLE);
    std::string payload1 = "{\"flags\":" + std::to_string(flags1) + "}";
    Operation op1(hlc1, OpType::RANGE_SET, mergeId, payload1);
    applyOperation(*workbook, op1);

    // Peer 2 sets MERGE | FORMAT (later HLC)
    HLC hlc2 = workbook->getCurrentHLC();
    uint8_t flags2 =
        static_cast<uint8_t>(RangeFlags::MERGE) | static_cast<uint8_t>(RangeFlags::FORMAT);
    std::string payload2 = "{\"flags\":" + std::to_string(flags2) + "}";
    Operation op2(hlc2, OpType::RANGE_SET, mergeId, payload2);
    applyOperation(*workbook, op2);

    // LWW: later operation wins
    Range* range = workbook->getRange(mergeId);
    ASSERT_NE(range, nullptr);
    EXPECT_TRUE(range->hasFlag(RangeFlags::MERGE));
    EXPECT_TRUE(range->hasFlag(RangeFlags::FORMAT));
    // STYLE flag may or may not be present depending on merge behavior
}

// =============================================================================
// Additional Edge Cases
// =============================================================================

TEST_F(MergedCellsTest, SingleCellMerge) {
    // A merge can be a single cell (though unusual)
    ID mergeId = createMergeRange(colIds[2], rowIds[2], colIds[2], rowIds[2]);

    Range* range = workbook->getRange(mergeId);
    ASSERT_NE(range, nullptr);
    EXPECT_TRUE(range->isMerge());
    EXPECT_TRUE(range->isSingleCell());
    EXPECT_EQ(getMergeColSpan(mergeId), 1);
    EXPECT_EQ(getMergeRowSpan(mergeId), 1);
}

TEST_F(MergedCellsTest, MergeWithStyleAndFormat) {
    // Create a merge with MERGE | STYLE | FORMAT flags
    ID rangeId = generate_id();
    uint8_t flags = static_cast<uint8_t>(RangeFlags::MERGE) |
                    static_cast<uint8_t>(RangeFlags::STYLE) |
                    static_cast<uint8_t>(RangeFlags::FORMAT);
    std::string payload = "{\"startCol\":\"" + colIds[0].toString() + "\",";
    payload += "\"startRow\":\"" + rowIds[0].toString() + "\",";
    payload += "\"endCol\":\"" + colIds[2].toString() + "\",";
    payload += "\"endRow\":\"" + rowIds[2].toString() + "\",";
    payload += "\"flags\":" + std::to_string(flags) + "}";

    Operation op = makeRangeSetOp(*workbook, rangeId, payload);
    applyOperation(*workbook, op);

    Range* range = workbook->getRange(rangeId);
    ASSERT_NE(range, nullptr);
    EXPECT_TRUE(range->hasFlag(RangeFlags::MERGE));
    EXPECT_TRUE(range->hasFlag(RangeFlags::STYLE));
    EXPECT_TRUE(range->hasFlag(RangeFlags::FORMAT));
}

TEST_F(MergedCellsTest, MergeFlagPreservedDuringBoundaryChange) {
    // Create a merge
    ID mergeId = createMergeRange(colIds[1], rowIds[0], colIds[3], rowIds[2]);

    // Delete a boundary column
    applyOperation(*workbook, makeColDeleteOp(*workbook, colIds[1]));

    // Merge should shrink but MERGE flag is preserved
    Range* range = workbook->getRange(mergeId);
    ASSERT_NE(range, nullptr);
    EXPECT_TRUE(range->isMerge());
    EXPECT_EQ(range->startColId, colIds[2]);
}

TEST_F(MergedCellsTest, QueryMergesAtPositionReturnsOnlyMerges) {
    // Create a merge and a non-merge range at overlapping positions
    ID mergeId = createMergeRange(colIds[0], rowIds[0], colIds[2], rowIds[2]);

    // Create a STYLE range (not MERGE) at same area
    ID styleRangeId = generate_id();
    std::string payload = "{\"startCol\":\"" + colIds[0].toString() + "\",";
    payload += "\"startRow\":\"" + rowIds[0].toString() + "\",";
    payload += "\"endCol\":\"" + colIds[2].toString() + "\",";
    payload += "\"endRow\":\"" + rowIds[2].toString() + "\",";
    payload += "\"flags\":" + std::to_string(static_cast<uint8_t>(RangeFlags::STYLE)) + "}";

    Operation op = makeRangeSetOp(*workbook, styleRangeId, payload);
    applyOperation(*workbook, op);

    // Query for MERGE ranges at position (1,1)
    auto mergeRanges = sheet_ptr->getRangesAt(1, 1, RangeFlags::MERGE);
    ASSERT_EQ(mergeRanges.size(), 1);
    EXPECT_EQ(mergeRanges[0]->id, mergeId);

    // Query for STYLE ranges at position (1,1)
    auto styleRanges = sheet_ptr->getRangesAt(1, 1, RangeFlags::STYLE);
    ASSERT_EQ(styleRanges.size(), 1);
    EXPECT_EQ(styleRanges[0]->id, styleRangeId);

    // Query for all ranges at position (1,1)
    auto allRanges = sheet_ptr->getRangesAt(1, 1);
    EXPECT_EQ(allRanges.size(), 2);
}

}  // namespace
}  // namespace cells
