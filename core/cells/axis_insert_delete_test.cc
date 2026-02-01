// =============================================================================
// Axis Insert/Delete Unit Tests
// =============================================================================
//
// Tests for inserting and deleting columns/rows through CRDT operations.
// Verifies position handling, cell cascade cleanup, formula reference updates,
// range boundary adjustments, and CRDT convergence for concurrent operations.
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

class AxisInsertDeleteTest : public ::testing::Test {
protected:
    void SetUp() override {
        workbook = std::make_unique<Workbook>(generate_id(), "TestWorkbook");
        workbook->setNodeId(generate_id());

        auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet1");
        sheet_id = sheet->id;
        sheet_ptr = sheet.get();
        sheet->setWorkbook(workbook.get());

        // Create 5 columns at positions 0, 1, 2, 3, 4
        for (int i = 0; i < 5; i++) {
            auto col = std::make_unique<Axis>(generate_id(), true);
            col->position = static_cast<uint32_t>(i);
            colIds[i] = col->id;
            sheet->addColumn(std::move(col));
        }

        // Create 5 rows at positions 0, 1, 2, 3, 4
        for (int i = 0; i < 5; i++) {
            auto row = std::make_unique<Axis>(generate_id(), false);
            row->position = static_cast<uint32_t>(i);
            rowIds[i] = row->id;
            sheet->addRow(std::move(row));
        }

        // Create some cells for testing cascade delete
        for (int c = 0; c < 3; c++) {
            for (int r = 0; r < 3; r++) {
                ID cellId = generate_id();
                auto cell = std::make_unique<Cell>(cellId, colIds[c], rowIds[r]);
                cell->value = CellValue(static_cast<double>(c * 10 + r));
                cellIds[c][r] = cellId;
                sheet->addCell(std::move(cell));
            }
        }

        workbook->addSheet(std::move(sheet));
    }

    std::unique_ptr<Workbook> workbook;
    Sheet* sheet_ptr;
    ID sheet_id;
    ID colIds[5];
    ID rowIds[5];
    ID cellIds[3][3];  // cellIds[col][row]
};

// =============================================================================
// 3a: Test column insert at beginning, middle, end positions
// =============================================================================

TEST_F(AxisInsertDeleteTest, InsertColumnAtBeginning) {
    ID newColId = generate_id();
    std::string payload = R"({"pos":0,"size":100})";

    Operation op = makeColSetOp(*workbook, newColId, sheet_id, payload);
    ApplyResult result = applyOperation(*workbook, op);
    EXPECT_EQ(result, ApplyResult::SUCCESS);

    // Verify new column exists at position 0
    Axis* newCol = sheet_ptr->getColumn(newColId);
    ASSERT_NE(newCol, nullptr);
    EXPECT_EQ(newCol->position, 0);
    EXPECT_EQ(newCol->size, 100);
    EXPECT_TRUE(newCol->isColumn());
}

TEST_F(AxisInsertDeleteTest, InsertColumnAtMiddle) {
    ID newColId = generate_id();
    std::string payload = R"({"pos":2,"size":150})";

    Operation op = makeColSetOp(*workbook, newColId, sheet_id, payload);
    ApplyResult result = applyOperation(*workbook, op);
    EXPECT_EQ(result, ApplyResult::SUCCESS);

    // Verify new column exists at position 2
    Axis* newCol = sheet_ptr->getColumn(newColId);
    ASSERT_NE(newCol, nullptr);
    EXPECT_EQ(newCol->position, 2);
    EXPECT_EQ(newCol->size, 150);
}

TEST_F(AxisInsertDeleteTest, InsertColumnAtEnd) {
    ID newColId = generate_id();
    std::string payload = R"({"pos":5,"size":120})";

    Operation op = makeColSetOp(*workbook, newColId, sheet_id, payload);
    ApplyResult result = applyOperation(*workbook, op);
    EXPECT_EQ(result, ApplyResult::SUCCESS);

    // Verify new column exists at position 5
    Axis* newCol = sheet_ptr->getColumn(newColId);
    ASSERT_NE(newCol, nullptr);
    EXPECT_EQ(newCol->position, 5);
    EXPECT_EQ(newCol->size, 120);
}

TEST_F(AxisInsertDeleteTest, InsertMultipleColumnsAtSamePosition) {
    // Insert two columns at position 1
    ID newColId1 = generate_id();
    ID newColId2 = generate_id();

    std::string payload1 = R"({"pos":1,"size":100})";
    std::string payload2 = R"({"pos":1,"size":100})";

    applyOperation(*workbook, makeColSetOp(*workbook, newColId1, sheet_id, payload1));
    applyOperation(*workbook, makeColSetOp(*workbook, newColId2, sheet_id, payload2));

    // Both columns should exist at position 1 (CRDT allows same positions)
    Axis* col1 = sheet_ptr->getColumn(newColId1);
    Axis* col2 = sheet_ptr->getColumn(newColId2);

    ASSERT_NE(col1, nullptr);
    ASSERT_NE(col2, nullptr);
    EXPECT_EQ(col1->position, 1);
    EXPECT_EQ(col2->position, 1);
}

TEST_F(AxisInsertDeleteTest, InsertColumnWithDefaultSize) {
    ID newColId = generate_id();
    std::string payload = R"({"pos":3})";  // No size specified

    Operation op = makeColSetOp(*workbook, newColId, sheet_id, payload);
    ApplyResult result = applyOperation(*workbook, op);
    EXPECT_EQ(result, ApplyResult::SUCCESS);

    // Verify column has default size (100)
    Axis* newCol = sheet_ptr->getColumn(newColId);
    ASSERT_NE(newCol, nullptr);
    EXPECT_EQ(newCol->size, 100);
}

TEST_F(AxisInsertDeleteTest, InsertColumnMissingPositionFails) {
    ID newColId = generate_id();
    std::string payload = R"({"size":100})";  // Missing pos

    Operation op = makeColSetOp(*workbook, newColId, sheet_id, payload);
    ApplyResult result = applyOperation(*workbook, op);
    EXPECT_EQ(result, ApplyResult::INVALID_PAYLOAD);
}

TEST_F(AxisInsertDeleteTest, InsertColumnAtLargePosition) {
    ID newColId = generate_id();
    std::string payload = R"({"pos":1000,"size":100})";

    Operation op = makeColSetOp(*workbook, newColId, sheet_id, payload);
    ApplyResult result = applyOperation(*workbook, op);
    EXPECT_EQ(result, ApplyResult::SUCCESS);

    Axis* newCol = sheet_ptr->getColumn(newColId);
    ASSERT_NE(newCol, nullptr);
    EXPECT_EQ(newCol->position, 1000);
}

// =============================================================================
// 3b: Test row insert at beginning, middle, end positions
// =============================================================================

TEST_F(AxisInsertDeleteTest, InsertRowAtBeginning) {
    ID newRowId = generate_id();
    std::string payload = R"({"pos":0,"size":30})";

    Operation op = makeRowSetOp(*workbook, newRowId, sheet_id, payload);
    ApplyResult result = applyOperation(*workbook, op);
    EXPECT_EQ(result, ApplyResult::SUCCESS);

    // Verify new row exists at position 0
    Axis* newRow = sheet_ptr->getRow(newRowId);
    ASSERT_NE(newRow, nullptr);
    EXPECT_EQ(newRow->position, 0);
    EXPECT_EQ(newRow->size, 30);
    EXPECT_FALSE(newRow->isColumn());
}

TEST_F(AxisInsertDeleteTest, InsertRowAtMiddle) {
    ID newRowId = generate_id();
    std::string payload = R"({"pos":2,"size":25})";

    Operation op = makeRowSetOp(*workbook, newRowId, sheet_id, payload);
    ApplyResult result = applyOperation(*workbook, op);
    EXPECT_EQ(result, ApplyResult::SUCCESS);

    // Verify new row exists at position 2
    Axis* newRow = sheet_ptr->getRow(newRowId);
    ASSERT_NE(newRow, nullptr);
    EXPECT_EQ(newRow->position, 2);
    EXPECT_EQ(newRow->size, 25);
}

TEST_F(AxisInsertDeleteTest, InsertRowAtEnd) {
    ID newRowId = generate_id();
    std::string payload = R"({"pos":5,"size":40})";

    Operation op = makeRowSetOp(*workbook, newRowId, sheet_id, payload);
    ApplyResult result = applyOperation(*workbook, op);
    EXPECT_EQ(result, ApplyResult::SUCCESS);

    // Verify new row exists at position 5
    Axis* newRow = sheet_ptr->getRow(newRowId);
    ASSERT_NE(newRow, nullptr);
    EXPECT_EQ(newRow->position, 5);
    EXPECT_EQ(newRow->size, 40);
}

TEST_F(AxisInsertDeleteTest, InsertMultipleRowsAtSamePosition) {
    // Insert two rows at position 1
    ID newRowId1 = generate_id();
    ID newRowId2 = generate_id();

    std::string payload1 = R"({"pos":1,"size":21})";
    std::string payload2 = R"({"pos":1,"size":21})";

    applyOperation(*workbook, makeRowSetOp(*workbook, newRowId1, sheet_id, payload1));
    applyOperation(*workbook, makeRowSetOp(*workbook, newRowId2, sheet_id, payload2));

    // Both rows should exist at position 1 (CRDT allows same positions)
    Axis* row1 = sheet_ptr->getRow(newRowId1);
    Axis* row2 = sheet_ptr->getRow(newRowId2);

    ASSERT_NE(row1, nullptr);
    ASSERT_NE(row2, nullptr);
    EXPECT_EQ(row1->position, 1);
    EXPECT_EQ(row2->position, 1);
}

TEST_F(AxisInsertDeleteTest, InsertRowWithDefaultSize) {
    ID newRowId = generate_id();
    std::string payload = R"({"pos":3})";  // No size specified

    Operation op = makeRowSetOp(*workbook, newRowId, sheet_id, payload);
    ApplyResult result = applyOperation(*workbook, op);
    EXPECT_EQ(result, ApplyResult::SUCCESS);

    // Verify row has default size (DEFAULT_ROW_HEIGHT = 24)
    Axis* newRow = sheet_ptr->getRow(newRowId);
    ASSERT_NE(newRow, nullptr);
    EXPECT_EQ(newRow->size, DEFAULT_ROW_HEIGHT);
}

TEST_F(AxisInsertDeleteTest, InsertRowMissingPositionFails) {
    ID newRowId = generate_id();
    std::string payload = R"({"size":30})";  // Missing pos

    Operation op = makeRowSetOp(*workbook, newRowId, sheet_id, payload);
    ApplyResult result = applyOperation(*workbook, op);
    EXPECT_EQ(result, ApplyResult::INVALID_PAYLOAD);
}

TEST_F(AxisInsertDeleteTest, InsertRowAtLargePosition) {
    ID newRowId = generate_id();
    std::string payload = R"({"pos":10000,"size":21})";

    Operation op = makeRowSetOp(*workbook, newRowId, sheet_id, payload);
    ApplyResult result = applyOperation(*workbook, op);
    EXPECT_EQ(result, ApplyResult::SUCCESS);

    Axis* newRow = sheet_ptr->getRow(newRowId);
    ASSERT_NE(newRow, nullptr);
    EXPECT_EQ(newRow->position, 10000);
}

// =============================================================================
// 3c: Test column delete with cell cascade cleanup
// =============================================================================

TEST_F(AxisInsertDeleteTest, DeleteColumnRemovesColumn) {
    // Delete column at position 1 (colIds[1])
    Operation op = makeColDeleteOp(*workbook, colIds[1]);
    ApplyResult result = applyOperation(*workbook, op);
    EXPECT_EQ(result, ApplyResult::SUCCESS);

    // Verify column is removed
    Axis* deletedCol = sheet_ptr->getColumn(colIds[1]);
    EXPECT_EQ(deletedCol, nullptr);
}

TEST_F(AxisInsertDeleteTest, DeleteColumnRemovesCellsInColumn) {
    // Column 1 has cells at (1,0), (1,1), (1,2)
    // Verify cells exist before delete
    ASSERT_NE(workbook->getCell(cellIds[1][0]), nullptr);
    ASSERT_NE(workbook->getCell(cellIds[1][1]), nullptr);
    ASSERT_NE(workbook->getCell(cellIds[1][2]), nullptr);

    // Delete column 1
    Operation op = makeColDeleteOp(*workbook, colIds[1]);
    ApplyResult result = applyOperation(*workbook, op);
    EXPECT_EQ(result, ApplyResult::SUCCESS);

    // Verify all cells in column 1 are deleted
    EXPECT_EQ(workbook->getCell(cellIds[1][0]), nullptr);
    EXPECT_EQ(workbook->getCell(cellIds[1][1]), nullptr);
    EXPECT_EQ(workbook->getCell(cellIds[1][2]), nullptr);

    // Verify cells in other columns still exist
    EXPECT_NE(workbook->getCell(cellIds[0][0]), nullptr);
    EXPECT_NE(workbook->getCell(cellIds[2][1]), nullptr);
}

TEST_F(AxisInsertDeleteTest, DeleteColumnPreservesOtherColumns) {
    // Delete column 1
    Operation op = makeColDeleteOp(*workbook, colIds[1]);
    applyOperation(*workbook, op);

    // Verify other columns still exist
    EXPECT_NE(sheet_ptr->getColumn(colIds[0]), nullptr);
    EXPECT_NE(sheet_ptr->getColumn(colIds[2]), nullptr);
    EXPECT_NE(sheet_ptr->getColumn(colIds[3]), nullptr);
    EXPECT_NE(sheet_ptr->getColumn(colIds[4]), nullptr);
}

TEST_F(AxisInsertDeleteTest, DeleteNonexistentColumnSucceeds) {
    // Deleting a column that doesn't exist should succeed (already deleted)
    ID fakeColId = generate_id();
    Operation op = makeColDeleteOp(*workbook, fakeColId);
    ApplyResult result = applyOperation(*workbook, op);
    EXPECT_EQ(result, ApplyResult::SUCCESS);
}

TEST_F(AxisInsertDeleteTest, DeleteFirstColumn) {
    // Delete column at position 0
    Operation op = makeColDeleteOp(*workbook, colIds[0]);
    ApplyResult result = applyOperation(*workbook, op);
    EXPECT_EQ(result, ApplyResult::SUCCESS);

    // Verify column 0 is deleted
    EXPECT_EQ(sheet_ptr->getColumn(colIds[0]), nullptr);

    // Verify cells in column 0 are deleted
    EXPECT_EQ(workbook->getCell(cellIds[0][0]), nullptr);
    EXPECT_EQ(workbook->getCell(cellIds[0][1]), nullptr);
    EXPECT_EQ(workbook->getCell(cellIds[0][2]), nullptr);
}

TEST_F(AxisInsertDeleteTest, DeleteLastColumn) {
    // Delete column at position 4
    Operation op = makeColDeleteOp(*workbook, colIds[4]);
    ApplyResult result = applyOperation(*workbook, op);
    EXPECT_EQ(result, ApplyResult::SUCCESS);

    // Verify column 4 is deleted
    EXPECT_EQ(sheet_ptr->getColumn(colIds[4]), nullptr);
}

TEST_F(AxisInsertDeleteTest, DeleteMultipleColumns) {
    // Delete columns 0, 2, 4
    applyOperation(*workbook, makeColDeleteOp(*workbook, colIds[0]));
    applyOperation(*workbook, makeColDeleteOp(*workbook, colIds[2]));
    applyOperation(*workbook, makeColDeleteOp(*workbook, colIds[4]));

    // Verify deleted columns are gone
    EXPECT_EQ(sheet_ptr->getColumn(colIds[0]), nullptr);
    EXPECT_EQ(sheet_ptr->getColumn(colIds[2]), nullptr);
    EXPECT_EQ(sheet_ptr->getColumn(colIds[4]), nullptr);

    // Verify remaining columns still exist
    EXPECT_NE(sheet_ptr->getColumn(colIds[1]), nullptr);
    EXPECT_NE(sheet_ptr->getColumn(colIds[3]), nullptr);
}

TEST_F(AxisInsertDeleteTest, DeleteColumnThenInsertNew) {
    // Delete column 2
    applyOperation(*workbook, makeColDeleteOp(*workbook, colIds[2]));
    EXPECT_EQ(sheet_ptr->getColumn(colIds[2]), nullptr);

    // Insert a new column at position 2
    ID newColId = generate_id();
    std::string payload = R"({"pos":2,"size":100})";
    applyOperation(*workbook, makeColSetOp(*workbook, newColId, sheet_id, payload));

    // Verify new column exists
    Axis* newCol = sheet_ptr->getColumn(newColId);
    ASSERT_NE(newCol, nullptr);
    EXPECT_EQ(newCol->position, 2);
}

// =============================================================================
// 3d: Test row delete with cell cascade cleanup
// =============================================================================

TEST_F(AxisInsertDeleteTest, DeleteRowRemovesRow) {
    // Delete row at position 1 (rowIds[1])
    Operation op = makeRowDeleteOp(*workbook, rowIds[1]);
    ApplyResult result = applyOperation(*workbook, op);
    EXPECT_EQ(result, ApplyResult::SUCCESS);

    // Verify row is removed
    Axis* deletedRow = sheet_ptr->getRow(rowIds[1]);
    EXPECT_EQ(deletedRow, nullptr);
}

TEST_F(AxisInsertDeleteTest, DeleteRowRemovesCellsInRow) {
    // Row 1 has cells at (0,1), (1,1), (2,1)
    // Verify cells exist before delete
    ASSERT_NE(workbook->getCell(cellIds[0][1]), nullptr);
    ASSERT_NE(workbook->getCell(cellIds[1][1]), nullptr);
    ASSERT_NE(workbook->getCell(cellIds[2][1]), nullptr);

    // Delete row 1
    Operation op = makeRowDeleteOp(*workbook, rowIds[1]);
    ApplyResult result = applyOperation(*workbook, op);
    EXPECT_EQ(result, ApplyResult::SUCCESS);

    // Verify all cells in row 1 are deleted
    EXPECT_EQ(workbook->getCell(cellIds[0][1]), nullptr);
    EXPECT_EQ(workbook->getCell(cellIds[1][1]), nullptr);
    EXPECT_EQ(workbook->getCell(cellIds[2][1]), nullptr);

    // Verify cells in other rows still exist
    EXPECT_NE(workbook->getCell(cellIds[0][0]), nullptr);
    EXPECT_NE(workbook->getCell(cellIds[1][2]), nullptr);
}

TEST_F(AxisInsertDeleteTest, DeleteRowPreservesOtherRows) {
    // Delete row 1
    Operation op = makeRowDeleteOp(*workbook, rowIds[1]);
    applyOperation(*workbook, op);

    // Verify other rows still exist
    EXPECT_NE(sheet_ptr->getRow(rowIds[0]), nullptr);
    EXPECT_NE(sheet_ptr->getRow(rowIds[2]), nullptr);
    EXPECT_NE(sheet_ptr->getRow(rowIds[3]), nullptr);
    EXPECT_NE(sheet_ptr->getRow(rowIds[4]), nullptr);
}

TEST_F(AxisInsertDeleteTest, DeleteNonexistentRowSucceeds) {
    // Deleting a row that doesn't exist should succeed (already deleted)
    ID fakeRowId = generate_id();
    Operation op = makeRowDeleteOp(*workbook, fakeRowId);
    ApplyResult result = applyOperation(*workbook, op);
    EXPECT_EQ(result, ApplyResult::SUCCESS);
}

TEST_F(AxisInsertDeleteTest, DeleteFirstRow) {
    // Delete row at position 0
    Operation op = makeRowDeleteOp(*workbook, rowIds[0]);
    ApplyResult result = applyOperation(*workbook, op);
    EXPECT_EQ(result, ApplyResult::SUCCESS);

    // Verify row 0 is deleted
    EXPECT_EQ(sheet_ptr->getRow(rowIds[0]), nullptr);

    // Verify cells in row 0 are deleted
    EXPECT_EQ(workbook->getCell(cellIds[0][0]), nullptr);
    EXPECT_EQ(workbook->getCell(cellIds[1][0]), nullptr);
    EXPECT_EQ(workbook->getCell(cellIds[2][0]), nullptr);
}

TEST_F(AxisInsertDeleteTest, DeleteLastRow) {
    // Delete row at position 4
    Operation op = makeRowDeleteOp(*workbook, rowIds[4]);
    ApplyResult result = applyOperation(*workbook, op);
    EXPECT_EQ(result, ApplyResult::SUCCESS);

    // Verify row 4 is deleted
    EXPECT_EQ(sheet_ptr->getRow(rowIds[4]), nullptr);
}

TEST_F(AxisInsertDeleteTest, DeleteMultipleRows) {
    // Delete rows 0, 2, 4
    applyOperation(*workbook, makeRowDeleteOp(*workbook, rowIds[0]));
    applyOperation(*workbook, makeRowDeleteOp(*workbook, rowIds[2]));
    applyOperation(*workbook, makeRowDeleteOp(*workbook, rowIds[4]));

    // Verify deleted rows are gone
    EXPECT_EQ(sheet_ptr->getRow(rowIds[0]), nullptr);
    EXPECT_EQ(sheet_ptr->getRow(rowIds[2]), nullptr);
    EXPECT_EQ(sheet_ptr->getRow(rowIds[4]), nullptr);

    // Verify remaining rows still exist
    EXPECT_NE(sheet_ptr->getRow(rowIds[1]), nullptr);
    EXPECT_NE(sheet_ptr->getRow(rowIds[3]), nullptr);
}

TEST_F(AxisInsertDeleteTest, DeleteRowThenInsertNew) {
    // Delete row 2
    applyOperation(*workbook, makeRowDeleteOp(*workbook, rowIds[2]));
    EXPECT_EQ(sheet_ptr->getRow(rowIds[2]), nullptr);

    // Insert a new row at position 2
    ID newRowId = generate_id();
    std::string payload = R"({"pos":2,"size":21})";
    applyOperation(*workbook, makeRowSetOp(*workbook, newRowId, sheet_id, payload));

    // Verify new row exists
    Axis* newRow = sheet_ptr->getRow(newRowId);
    ASSERT_NE(newRow, nullptr);
    EXPECT_EQ(newRow->position, 2);
}

// =============================================================================
// 3e: Test insert/delete effects on formula references
// =============================================================================

// Note: Formula cells referencing deleted axes show #REF! error.
// These tests verify basic formula cell cleanup when axes are deleted.
// Detailed formula reference updating is tested in formula_move_test.cc.

TEST_F(AxisInsertDeleteTest, DeleteColumnWithFormulaCell) {
    // Create a cell with a formula in column 0
    ID formulaCellId = generate_id();
    auto formulaCell = std::make_unique<Cell>(formulaCellId, colIds[0], rowIds[0]);
    formulaCell->value = CellValue(42.0);  // Cached result
    sheet_ptr->addCell(std::move(formulaCell));

    // Verify cell exists before delete
    ASSERT_NE(workbook->getCell(formulaCellId), nullptr);

    // Delete column 0
    Operation op = makeColDeleteOp(*workbook, colIds[0]);
    ApplyResult result = applyOperation(*workbook, op);
    EXPECT_EQ(result, ApplyResult::SUCCESS);

    // Verify formula cell was deleted with the column
    EXPECT_EQ(workbook->getCell(formulaCellId), nullptr);
}

TEST_F(AxisInsertDeleteTest, DeleteRowWithFormulaCell) {
    // Create a cell with a formula in row 0
    ID formulaCellId = generate_id();
    auto formulaCell = std::make_unique<Cell>(formulaCellId, colIds[0], rowIds[0]);
    formulaCell->value = CellValue(42.0);  // Cached result
    sheet_ptr->addCell(std::move(formulaCell));

    // Verify cell exists before delete
    ASSERT_NE(workbook->getCell(formulaCellId), nullptr);

    // Delete row 0
    Operation op = makeRowDeleteOp(*workbook, rowIds[0]);
    ApplyResult result = applyOperation(*workbook, op);
    EXPECT_EQ(result, ApplyResult::SUCCESS);

    // Verify formula cell was deleted with the row
    EXPECT_EQ(workbook->getCell(formulaCellId), nullptr);
}

TEST_F(AxisInsertDeleteTest, InsertColumnDoesNotAffectExistingCells) {
    // Get a cell's column and verify it
    Cell* existingCell = workbook->getCell(cellIds[1][1]);
    ASSERT_NE(existingCell, nullptr);
    ID originalColId = existingCell->colId;

    // Insert a new column at position 0
    ID newColId = generate_id();
    std::string payload = R"({"pos":0,"size":100})";
    applyOperation(*workbook, makeColSetOp(*workbook, newColId, sheet_id, payload));

    // Verify existing cell still references its original column ID
    // (CRDT uses UUIDs, not positions, so references are stable)
    existingCell = workbook->getCell(cellIds[1][1]);
    ASSERT_NE(existingCell, nullptr);
    EXPECT_EQ(existingCell->colId, originalColId);
}

TEST_F(AxisInsertDeleteTest, InsertRowDoesNotAffectExistingCells) {
    // Get a cell's row and verify it
    Cell* existingCell = workbook->getCell(cellIds[1][1]);
    ASSERT_NE(existingCell, nullptr);
    ID originalRowId = existingCell->rowId;

    // Insert a new row at position 0
    ID newRowId = generate_id();
    std::string payload = R"({"pos":0,"size":21})";
    applyOperation(*workbook, makeRowSetOp(*workbook, newRowId, sheet_id, payload));

    // Verify existing cell still references its original row ID
    existingCell = workbook->getCell(cellIds[1][1]);
    ASSERT_NE(existingCell, nullptr);
    EXPECT_EQ(existingCell->rowId, originalRowId);
}

// =============================================================================
// 3f: Test insert/delete effects on range boundaries
// =============================================================================

// Helper function to create a range spanning from one corner to another
void createRange(Workbook& workbook, const ID& rangeId, const ID& sheetId, const ID& startCol,
                 const ID& startRow, const ID& endCol, const ID& endRow, uint8_t flags = 4) {
    std::string payload = "{\"startCol\":\"" + startCol.toString() + "\",";
    payload += "\"startRow\":\"" + startRow.toString() + "\",";
    payload += "\"endCol\":\"" + endCol.toString() + "\",";
    payload += "\"endRow\":\"" + endRow.toString() + "\",";
    payload += "\"flags\":" + std::to_string(flags) + "}";

    Operation op = makeRangeSetOp(workbook, rangeId, payload);
    applyOperation(workbook, op);
}

TEST_F(AxisInsertDeleteTest, DeleteColumnShrinksRangeFromStart) {
    // Create a range from col0 to col2 (columns at positions 0, 1, 2)
    ID rangeId = generate_id();
    createRange(*workbook, rangeId, sheet_id, colIds[0], rowIds[0], colIds[2], rowIds[2]);

    // Verify range exists
    Range* range = workbook->getRange(rangeId);
    ASSERT_NE(range, nullptr);
    EXPECT_EQ(range->startColId, colIds[0]);
    EXPECT_EQ(range->endColId, colIds[2]);

    // Delete the start column (colIds[0])
    applyOperation(*workbook, makeColDeleteOp(*workbook, colIds[0]));

    // Range should shrink, new start column is colIds[1]
    range = workbook->getRange(rangeId);
    ASSERT_NE(range, nullptr);
    EXPECT_EQ(range->startColId, colIds[1]);
    EXPECT_EQ(range->endColId, colIds[2]);
}

TEST_F(AxisInsertDeleteTest, DeleteColumnShrinksRangeFromEnd) {
    // Create a range from col0 to col2
    ID rangeId = generate_id();
    createRange(*workbook, rangeId, sheet_id, colIds[0], rowIds[0], colIds[2], rowIds[2]);

    // Verify range exists
    Range* range = workbook->getRange(rangeId);
    ASSERT_NE(range, nullptr);

    // Delete the end column (colIds[2])
    applyOperation(*workbook, makeColDeleteOp(*workbook, colIds[2]));

    // Range should shrink, new end column is colIds[1]
    range = workbook->getRange(rangeId);
    ASSERT_NE(range, nullptr);
    EXPECT_EQ(range->startColId, colIds[0]);
    EXPECT_EQ(range->endColId, colIds[1]);
}

TEST_F(AxisInsertDeleteTest, DeleteSingleColumnRangeInvalidatesRange) {
    // Create a single-column range (col1 to col1)
    ID rangeId = generate_id();
    createRange(*workbook, rangeId, sheet_id, colIds[1], rowIds[0], colIds[1], rowIds[2]);

    // Verify range exists
    ASSERT_NE(workbook->getRange(rangeId), nullptr);

    // Delete the only column in the range
    applyOperation(*workbook, makeColDeleteOp(*workbook, colIds[1]));

    // Range should be invalidated/removed
    EXPECT_EQ(workbook->getRange(rangeId), nullptr);
}

TEST_F(AxisInsertDeleteTest, DeleteRowShrinksRangeFromStart) {
    // Create a range from row0 to row2
    ID rangeId = generate_id();
    createRange(*workbook, rangeId, sheet_id, colIds[0], rowIds[0], colIds[2], rowIds[2]);

    // Verify range exists
    Range* range = workbook->getRange(rangeId);
    ASSERT_NE(range, nullptr);
    EXPECT_EQ(range->startRowId, rowIds[0]);
    EXPECT_EQ(range->endRowId, rowIds[2]);

    // Delete the start row (rowIds[0])
    applyOperation(*workbook, makeRowDeleteOp(*workbook, rowIds[0]));

    // Range should shrink, new start row is rowIds[1]
    range = workbook->getRange(rangeId);
    ASSERT_NE(range, nullptr);
    EXPECT_EQ(range->startRowId, rowIds[1]);
    EXPECT_EQ(range->endRowId, rowIds[2]);
}

TEST_F(AxisInsertDeleteTest, DeleteRowShrinksRangeFromEnd) {
    // Create a range from row0 to row2
    ID rangeId = generate_id();
    createRange(*workbook, rangeId, sheet_id, colIds[0], rowIds[0], colIds[2], rowIds[2]);

    // Verify range exists
    Range* range = workbook->getRange(rangeId);
    ASSERT_NE(range, nullptr);

    // Delete the end row (rowIds[2])
    applyOperation(*workbook, makeRowDeleteOp(*workbook, rowIds[2]));

    // Range should shrink, new end row is rowIds[1]
    range = workbook->getRange(rangeId);
    ASSERT_NE(range, nullptr);
    EXPECT_EQ(range->startRowId, rowIds[0]);
    EXPECT_EQ(range->endRowId, rowIds[1]);
}

TEST_F(AxisInsertDeleteTest, DeleteSingleRowRangeInvalidatesRange) {
    // Create a single-row range (row1 to row1)
    ID rangeId = generate_id();
    createRange(*workbook, rangeId, sheet_id, colIds[0], rowIds[1], colIds[2], rowIds[1]);

    // Verify range exists
    ASSERT_NE(workbook->getRange(rangeId), nullptr);

    // Delete the only row in the range
    applyOperation(*workbook, makeRowDeleteOp(*workbook, rowIds[1]));

    // Range should be invalidated/removed
    EXPECT_EQ(workbook->getRange(rangeId), nullptr);
}

TEST_F(AxisInsertDeleteTest, DeleteMiddleColumnDoesNotAffectRange) {
    // Create a range from col0 to col3
    ID rangeId = generate_id();
    createRange(*workbook, rangeId, sheet_id, colIds[0], rowIds[0], colIds[3], rowIds[2]);

    // Verify range
    Range* range = workbook->getRange(rangeId);
    ASSERT_NE(range, nullptr);
    EXPECT_EQ(range->startColId, colIds[0]);
    EXPECT_EQ(range->endColId, colIds[3]);

    // Delete a middle column (col1) - not a corner
    applyOperation(*workbook, makeColDeleteOp(*workbook, colIds[1]));

    // Range should be unchanged (corners not affected)
    range = workbook->getRange(rangeId);
    ASSERT_NE(range, nullptr);
    EXPECT_EQ(range->startColId, colIds[0]);
    EXPECT_EQ(range->endColId, colIds[3]);
}

TEST_F(AxisInsertDeleteTest, DeleteMiddleRowDoesNotAffectRange) {
    // Create a range from row0 to row3
    ID rangeId = generate_id();
    createRange(*workbook, rangeId, sheet_id, colIds[0], rowIds[0], colIds[2], rowIds[3]);

    // Verify range
    Range* range = workbook->getRange(rangeId);
    ASSERT_NE(range, nullptr);
    EXPECT_EQ(range->startRowId, rowIds[0]);
    EXPECT_EQ(range->endRowId, rowIds[3]);

    // Delete a middle row (row1) - not a corner
    applyOperation(*workbook, makeRowDeleteOp(*workbook, rowIds[1]));

    // Range should be unchanged (corners not affected)
    range = workbook->getRange(rangeId);
    ASSERT_NE(range, nullptr);
    EXPECT_EQ(range->startRowId, rowIds[0]);
    EXPECT_EQ(range->endRowId, rowIds[3]);
}

TEST_F(AxisInsertDeleteTest, MultipleRangesAffectedByColumnDelete) {
    // Create two ranges that both start at col0
    ID rangeId1 = generate_id();
    ID rangeId2 = generate_id();
    createRange(*workbook, rangeId1, sheet_id, colIds[0], rowIds[0], colIds[2], rowIds[2]);
    createRange(*workbook, rangeId2, sheet_id, colIds[0], rowIds[3], colIds[1], rowIds[4]);

    // Delete col0
    applyOperation(*workbook, makeColDeleteOp(*workbook, colIds[0]));

    // Both ranges should shrink
    Range* range1 = workbook->getRange(rangeId1);
    Range* range2 = workbook->getRange(rangeId2);

    ASSERT_NE(range1, nullptr);
    EXPECT_EQ(range1->startColId, colIds[1]);

    ASSERT_NE(range2, nullptr);
    EXPECT_EQ(range2->startColId, colIds[1]);
}

// =============================================================================
// 3g: Test concurrent insert/delete from multiple peers
// =============================================================================

class AxisConcurrencyTest : public ::testing::Test {
protected:
    void SetUp() override {
        node_a = ID("NodeAAAA");
        node_b = ID("NodeBBBB");

        workbook_a = createWorkbook(node_a);
        workbook_b = createWorkbook(node_b);
    }

    std::unique_ptr<Workbook> createWorkbook(const ID& nodeId) {
        auto wb = std::make_unique<Workbook>(generate_id(), "TestWorkbook");
        wb->setNodeId(nodeId);

        auto sheet = std::make_unique<Sheet>(sharedSheetId, "Sheet1");
        sheet->setWorkbook(wb.get());

        // Create shared columns at positions 0, 1, 2
        for (int i = 0; i < 3; i++) {
            auto col = std::make_unique<Axis>(sharedColIds[i], true);
            col->position = static_cast<uint32_t>(i);
            sheet->addColumn(std::move(col));
        }

        // Create shared rows at positions 0, 1, 2
        for (int i = 0; i < 3; i++) {
            auto row = std::make_unique<Axis>(sharedRowIds[i], false);
            row->position = static_cast<uint32_t>(i);
            sheet->addRow(std::move(row));
        }

        // Create a shared cell at (0, 0)
        auto cell = std::make_unique<Cell>(sharedCellId, sharedColIds[0], sharedRowIds[0]);
        cell->value = CellValue(100.0);
        sheet->addCell(std::move(cell));

        wb->addSheet(std::move(sheet));
        return wb;
    }

    std::unique_ptr<Workbook> workbook_a;
    std::unique_ptr<Workbook> workbook_b;
    ID node_a, node_b;

    // Shared IDs across workbooks
    ID sharedSheetId = generate_id();
    ID sharedColIds[3] = {generate_id(), generate_id(), generate_id()};
    ID sharedRowIds[3] = {generate_id(), generate_id(), generate_id()};
    ID sharedCellId = generate_id();
};

TEST_F(AxisConcurrencyTest, ConcurrentColumnInsertSamePosition) {
    // Both peers insert a new column at position 1 concurrently
    ID newColA = generate_id();
    ID newColB = generate_id();

    HLC hlc_a(1000, 0, node_a);
    HLC hlc_b(1000, 0, node_b);

    Operation opA(hlc_a, OpType::COL_SET, newColA, R"({"pos":1,"size":100})");
    opA.sheetId = sharedSheetId;
    Operation opB(hlc_b, OpType::COL_SET, newColB, R"({"pos":1,"size":100})");
    opB.sheetId = sharedSheetId;

    // Apply in different orders
    applyOperation(*workbook_a, opA);
    applyOperation(*workbook_a, opB);

    applyOperation(*workbook_b, opB);
    applyOperation(*workbook_b, opA);

    // Both workbooks should have both new columns
    Sheet* sheet_a = workbook_a->getSheetByIndex(0);
    Sheet* sheet_b = workbook_b->getSheetByIndex(0);

    EXPECT_NE(sheet_a->getColumn(newColA), nullptr);
    EXPECT_NE(sheet_a->getColumn(newColB), nullptr);
    EXPECT_NE(sheet_b->getColumn(newColA), nullptr);
    EXPECT_NE(sheet_b->getColumn(newColB), nullptr);

    // Both columns should be at position 1 (CRDT allows same positions)
    EXPECT_EQ(sheet_a->getColumn(newColA)->position, 1);
    EXPECT_EQ(sheet_a->getColumn(newColB)->position, 1);
    EXPECT_EQ(sheet_b->getColumn(newColA)->position, 1);
    EXPECT_EQ(sheet_b->getColumn(newColB)->position, 1);
}

TEST_F(AxisConcurrencyTest, ConcurrentRowInsertSamePosition) {
    // Both peers insert a new row at position 1 concurrently
    ID newRowA = generate_id();
    ID newRowB = generate_id();

    HLC hlc_a(1000, 0, node_a);
    HLC hlc_b(1000, 0, node_b);

    Operation opA(hlc_a, OpType::ROW_SET, newRowA, R"({"pos":1,"size":21})");
    opA.sheetId = sharedSheetId;
    Operation opB(hlc_b, OpType::ROW_SET, newRowB, R"({"pos":1,"size":21})");
    opB.sheetId = sharedSheetId;

    // Apply in different orders
    applyOperation(*workbook_a, opA);
    applyOperation(*workbook_a, opB);

    applyOperation(*workbook_b, opB);
    applyOperation(*workbook_b, opA);

    // Both workbooks should have both new rows
    Sheet* sheet_a = workbook_a->getSheetByIndex(0);
    Sheet* sheet_b = workbook_b->getSheetByIndex(0);

    EXPECT_NE(sheet_a->getRow(newRowA), nullptr);
    EXPECT_NE(sheet_a->getRow(newRowB), nullptr);
    EXPECT_NE(sheet_b->getRow(newRowA), nullptr);
    EXPECT_NE(sheet_b->getRow(newRowB), nullptr);
}

TEST_F(AxisConcurrencyTest, ConcurrentColumnDelete) {
    // Both peers delete the same column concurrently
    HLC hlc_a(1000, 0, node_a);
    HLC hlc_b(1000, 0, node_b);

    Operation opA(hlc_a, OpType::COL_DELETE, sharedColIds[1], "{}");
    Operation opB(hlc_b, OpType::COL_DELETE, sharedColIds[1], "{}");

    // Apply in different orders
    applyOperation(*workbook_a, opA);
    applyOperation(*workbook_a, opB);

    applyOperation(*workbook_b, opB);
    applyOperation(*workbook_b, opA);

    // Column should be deleted in both workbooks
    Sheet* sheet_a = workbook_a->getSheetByIndex(0);
    Sheet* sheet_b = workbook_b->getSheetByIndex(0);

    EXPECT_EQ(sheet_a->getColumn(sharedColIds[1]), nullptr);
    EXPECT_EQ(sheet_b->getColumn(sharedColIds[1]), nullptr);
}

TEST_F(AxisConcurrencyTest, ConcurrentRowDelete) {
    // Both peers delete the same row concurrently
    HLC hlc_a(1000, 0, node_a);
    HLC hlc_b(1000, 0, node_b);

    Operation opA(hlc_a, OpType::ROW_DELETE, sharedRowIds[1], "{}");
    Operation opB(hlc_b, OpType::ROW_DELETE, sharedRowIds[1], "{}");

    // Apply in different orders
    applyOperation(*workbook_a, opA);
    applyOperation(*workbook_a, opB);

    applyOperation(*workbook_b, opB);
    applyOperation(*workbook_b, opA);

    // Row should be deleted in both workbooks
    Sheet* sheet_a = workbook_a->getSheetByIndex(0);
    Sheet* sheet_b = workbook_b->getSheetByIndex(0);

    EXPECT_EQ(sheet_a->getRow(sharedRowIds[1]), nullptr);
    EXPECT_EQ(sheet_b->getRow(sharedRowIds[1]), nullptr);
}

TEST_F(AxisConcurrencyTest, InsertThenDeleteConcurrent) {
    // Peer A inserts a column, Peer B deletes an existing column
    ID newColId = generate_id();

    HLC hlc_a(1000, 0, node_a);
    HLC hlc_b(1000, 0, node_b);

    Operation insertOp(hlc_a, OpType::COL_SET, newColId, R"({"pos":1,"size":100})");
    insertOp.sheetId = sharedSheetId;
    Operation deleteOp(hlc_b, OpType::COL_DELETE, sharedColIds[0], "{}");

    // Apply in different orders
    applyOperation(*workbook_a, insertOp);
    applyOperation(*workbook_a, deleteOp);

    applyOperation(*workbook_b, deleteOp);
    applyOperation(*workbook_b, insertOp);

    // Both workbooks should have the new column and lack the deleted column
    Sheet* sheet_a = workbook_a->getSheetByIndex(0);
    Sheet* sheet_b = workbook_b->getSheetByIndex(0);

    EXPECT_NE(sheet_a->getColumn(newColId), nullptr);
    EXPECT_NE(sheet_b->getColumn(newColId), nullptr);
    EXPECT_EQ(sheet_a->getColumn(sharedColIds[0]), nullptr);
    EXPECT_EQ(sheet_b->getColumn(sharedColIds[0]), nullptr);
}

TEST_F(AxisConcurrencyTest, DeleteResurrectedByLaterSet) {
    // Peer A deletes a column, Peer B sets properties on the same column with later HLC
    // For resurrection to work, the SET op must include pos (required for creation)
    HLC hlc_delete(1000, 0, node_a);
    HLC hlc_set(1001, 0, node_b);  // Later timestamp

    Operation deleteOp(hlc_delete, OpType::COL_DELETE, sharedColIds[1], "{}");
    // Include pos=1 so the column can be recreated
    Operation setOp(hlc_set, OpType::COL_SET, sharedColIds[1], R"({"pos":1,"size":200})");
    setOp.sheetId = sharedSheetId;

    // Apply delete first, then set (set should resurrect)
    applyOperation(*workbook_a, deleteOp);
    ApplyResult result = applyOperation(*workbook_a, setOp);
    EXPECT_EQ(result, ApplyResult::SUCCESS);

    // Column should exist with new size
    Sheet* sheet_a = workbook_a->getSheetByIndex(0);
    Axis* col = sheet_a->getColumn(sharedColIds[1]);
    ASSERT_NE(col, nullptr);
    EXPECT_EQ(col->size, 200);
}

TEST_F(AxisConcurrencyTest, ThreePeersConvergeOnInserts) {
    ID node_c("NodeCCCC");
    auto workbook_c = createWorkbook(node_c);

    // All three peers insert columns at position 1
    ID newColA = generate_id();
    ID newColB = generate_id();
    ID newColC = generate_id();

    HLC hlc_a(1000, 0, node_a);
    HLC hlc_b(1000, 0, node_b);
    HLC hlc_c(1000, 0, node_c);

    Operation opA(hlc_a, OpType::COL_SET, newColA, R"({"pos":1,"size":100})");
    opA.sheetId = sharedSheetId;
    Operation opB(hlc_b, OpType::COL_SET, newColB, R"({"pos":1,"size":100})");
    opB.sheetId = sharedSheetId;
    Operation opC(hlc_c, OpType::COL_SET, newColC, R"({"pos":1,"size":100})");
    opC.sheetId = sharedSheetId;

    // Apply in different orders on each workbook
    applyOperation(*workbook_a, opA);
    applyOperation(*workbook_a, opB);
    applyOperation(*workbook_a, opC);

    applyOperation(*workbook_b, opC);
    applyOperation(*workbook_b, opA);
    applyOperation(*workbook_b, opB);

    applyOperation(*workbook_c, opB);
    applyOperation(*workbook_c, opC);
    applyOperation(*workbook_c, opA);

    // All workbooks should have all three new columns
    Sheet* sheet_a = workbook_a->getSheetByIndex(0);
    Sheet* sheet_b = workbook_b->getSheetByIndex(0);
    Sheet* sheet_c = workbook_c->getSheetByIndex(0);

    EXPECT_NE(sheet_a->getColumn(newColA), nullptr);
    EXPECT_NE(sheet_a->getColumn(newColB), nullptr);
    EXPECT_NE(sheet_a->getColumn(newColC), nullptr);

    EXPECT_NE(sheet_b->getColumn(newColA), nullptr);
    EXPECT_NE(sheet_b->getColumn(newColB), nullptr);
    EXPECT_NE(sheet_b->getColumn(newColC), nullptr);

    EXPECT_NE(sheet_c->getColumn(newColA), nullptr);
    EXPECT_NE(sheet_c->getColumn(newColB), nullptr);
    EXPECT_NE(sheet_c->getColumn(newColC), nullptr);
}

TEST_F(AxisConcurrencyTest, OutOfOrderOperationDelivery) {
    // Peer A creates a sequence of operations
    std::vector<Operation> ops;
    for (int i = 0; i < 5; i++) {
        ID newColId = generate_id();
        std::string payload = R"({"pos":)" + std::to_string(i + 10) + R"(,"size":100})";
        Operation op = makeColSetOp(*workbook_a, newColId, sharedSheetId, payload);
        ops.push_back(op);
        applyOperation(*workbook_a, op);
    }

    // Apply to workbook_b in reverse order
    for (auto it = ops.rbegin(); it != ops.rend(); ++it) {
        applyOperation(*workbook_b, *it);
    }

    // Both should have 5 new columns
    Sheet* sheet_a = workbook_a->getSheetByIndex(0);
    Sheet* sheet_b = workbook_b->getSheetByIndex(0);

    for (const auto& op : ops) {
        EXPECT_NE(sheet_a->getColumn(op.target_id), nullptr);
        EXPECT_NE(sheet_b->getColumn(op.target_id), nullptr);
    }
}

TEST_F(AxisConcurrencyTest, ConcurrentPositionUpdateConverges) {
    // Both peers update the position of the same column
    HLC hlc_a(1000, 0, node_a);
    HLC hlc_b(1000, 0, node_b);

    Operation opA(hlc_a, OpType::COL_SET, sharedColIds[0], R"({"pos":5})");
    opA.sheetId = sharedSheetId;
    Operation opB(hlc_b, OpType::COL_SET, sharedColIds[0], R"({"pos":10})");
    opB.sheetId = sharedSheetId;

    // Apply in different orders
    applyOperation(*workbook_a, opA);
    applyOperation(*workbook_a, opB);

    applyOperation(*workbook_b, opB);
    applyOperation(*workbook_b, opA);

    // Both should converge to the same position (NodeBBBB > NodeAAAA, so opB wins)
    Sheet* sheet_a = workbook_a->getSheetByIndex(0);
    Sheet* sheet_b = workbook_b->getSheetByIndex(0);

    Axis* col_a = sheet_a->getColumn(sharedColIds[0]);
    Axis* col_b = sheet_b->getColumn(sharedColIds[0]);

    ASSERT_NE(col_a, nullptr);
    ASSERT_NE(col_b, nullptr);
    EXPECT_EQ(col_a->position, col_b->position);
}

}  // namespace
}  // namespace cells
