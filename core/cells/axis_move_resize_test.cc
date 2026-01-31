// =============================================================================
// Axis Move/Resize Unit Tests
// =============================================================================
//
// Tests for moving columns/rows to new positions and resizing columns/rows.
// Verifies position updates, formula reference preservation, cell data integrity,
// range boundary handling, hidden state, and CRDT convergence for concurrent moves.
//
// =============================================================================

#include "core/cells/crdt.h"
#include "core/cells/formula_display.h"
#include "core/cells/formula_parser.h"
#include "core/cells/formula_resolver.h"
#include "core/cells/id.h"
#include "core/cells/range.h"

#include "gtest/gtest.h"

namespace cells {
namespace {

// =============================================================================
// Test Fixture
// =============================================================================

class AxisMoveResizeTest : public ::testing::Test {
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
            col->size = 100;  // Default width
            colIds[i] = col->id;
            sheet->addColumn(std::move(col));
        }

        // Create 5 rows at positions 0, 1, 2, 3, 4
        for (int i = 0; i < 5; i++) {
            auto row = std::make_unique<Axis>(generate_id(), false);
            row->position = static_cast<uint32_t>(i);
            row->size = 21;  // Default height
            rowIds[i] = row->id;
            sheet->addRow(std::move(row));
        }

        // Create some cells for testing
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

    // Helper to set a resolved formula on a cell
    bool setResolvedFormula(const ID& cellId, const std::string& formulaText) {
        FormulaParser parser(formulaText);
        auto ast = parser.parse();
        if (!ast) {
            return false;
        }

        FormulaResolver resolver(*workbook, *sheet_ptr, workbook->getNamedRanges());
        auto result = resolver.resolve(ast.get());
        if (!result.success) {
            return false;
        }

        auto setResult = sheet_ptr->setCellFormula(cellId, formulaText, ast.release());
        return setResult.success;
    }

    // Helper to get the A1 display string of a formula
    std::string getFormulaDisplay(const ID& cellId) {
        Cell* cell = workbook->getCell(cellId);
        if (cell == nullptr || !cell->isFormula()) {
            return "";
        }
        Formula* formula = cell->getFormula();
        if (formula == nullptr || formula->ast == nullptr) {
            return "";
        }
        FormulaDisplayConverter converter(*sheet_ptr, workbook.get());
        return converter.toDisplayString(formula->ast);
    }

    std::unique_ptr<Workbook> workbook;
    Sheet* sheet_ptr;
    ID sheet_id;
    ID colIds[5];
    ID rowIds[5];
    ID cellIds[3][3];  // cellIds[col][row]
};

// =============================================================================
// 4a: Test column move (position swap) with formula reference updates
// =============================================================================

TEST_F(AxisMoveResizeTest, MoveColumnToHigherPosition) {
    // Move column 0 to position 3
    bool success = sheet_ptr->moveColumn(colIds[0], 3);
    EXPECT_TRUE(success);

    // Verify the column is now at position 3
    Axis* col = sheet_ptr->getColumn(colIds[0]);
    ASSERT_NE(col, nullptr);
    EXPECT_EQ(col->position, 3u);

    // Other columns should have shifted
    EXPECT_EQ(sheet_ptr->getColumn(colIds[1])->position, 0u);
    EXPECT_EQ(sheet_ptr->getColumn(colIds[2])->position, 1u);
    EXPECT_EQ(sheet_ptr->getColumn(colIds[3])->position, 2u);
    EXPECT_EQ(sheet_ptr->getColumn(colIds[4])->position, 4u);
}

TEST_F(AxisMoveResizeTest, MoveColumnToLowerPosition) {
    // Move column 4 to position 1
    bool success = sheet_ptr->moveColumn(colIds[4], 1);
    EXPECT_TRUE(success);

    // Verify the column is now at position 1
    Axis* col = sheet_ptr->getColumn(colIds[4]);
    ASSERT_NE(col, nullptr);
    EXPECT_EQ(col->position, 1u);

    // Other columns should have shifted
    EXPECT_EQ(sheet_ptr->getColumn(colIds[0])->position, 0u);
    EXPECT_EQ(sheet_ptr->getColumn(colIds[1])->position, 2u);
    EXPECT_EQ(sheet_ptr->getColumn(colIds[2])->position, 3u);
    EXPECT_EQ(sheet_ptr->getColumn(colIds[3])->position, 4u);
}

TEST_F(AxisMoveResizeTest, MoveColumnToSamePosition) {
    // Move column 2 to position 2 (no change)
    bool success = sheet_ptr->moveColumn(colIds[2], 2);
    EXPECT_TRUE(success);

    // All positions should remain the same
    for (int i = 0; i < 5; i++) {
        EXPECT_EQ(sheet_ptr->getColumn(colIds[i])->position, static_cast<uint32_t>(i));
    }
}

TEST_F(AxisMoveResizeTest, MoveNonexistentColumnFails) {
    ID fakeColId = generate_id();
    bool success = sheet_ptr->moveColumn(fakeColId, 2);
    EXPECT_FALSE(success);
}

TEST_F(AxisMoveResizeTest, MoveColumnPreservesCellData) {
    // Get the cell value at col0, row0 before move
    Cell* cell = workbook->getCell(cellIds[0][0]);
    ASSERT_NE(cell, nullptr);
    double originalValue = cell->value.asNumber();

    // Move column 0 to position 3
    bool success = sheet_ptr->moveColumn(colIds[0], 3);
    EXPECT_TRUE(success);

    // The cell should still exist with the same value
    cell = workbook->getCell(cellIds[0][0]);
    ASSERT_NE(cell, nullptr);
    EXPECT_EQ(cell->value.asNumber(), originalValue);
    EXPECT_EQ(cell->colId, colIds[0]);  // Cell still references the same column ID
}

TEST_F(AxisMoveResizeTest, MoveColumnUpdatesFormulaDisplay) {
    // Create a formula cell that references column 1 (position 1 = B)
    // Cell at col2, row0 with formula referencing B1
    Cell* formulaCell = workbook->getCell(cellIds[2][0]);
    ASSERT_NE(formulaCell, nullptr);

    bool set = setResolvedFormula(formulaCell->id, "=B1");
    EXPECT_TRUE(set);
    EXPECT_EQ(getFormulaDisplay(formulaCell->id), "=B1");

    // Move column 0 (position 0 = A) to position 1
    // Now what was B becomes A, and what was A becomes B
    bool success = sheet_ptr->moveColumn(colIds[0], 1);
    EXPECT_TRUE(success);

    // The formula still references the same UUID, but display changes
    // Column 1 (originally B) is now at position 0 (A)
    std::string display = getFormulaDisplay(formulaCell->id);
    EXPECT_EQ(display, "=A1");
}

TEST_F(AxisMoveResizeTest, MoveColumnViaCrdtOp) {
    // Update column position via CRDT operation
    std::string payload = R"({"pos":3})";
    Operation op = makeColSetOp(*workbook, colIds[1], sheet_id, payload);
    ApplyResult result = applyOperation(*workbook, op);
    EXPECT_EQ(result, ApplyResult::SUCCESS);

    // Verify position updated
    Axis* col = sheet_ptr->getColumn(colIds[1]);
    ASSERT_NE(col, nullptr);
    EXPECT_EQ(col->position, 3u);
}

// =============================================================================
// 4b: Test row move (position swap) with formula reference updates
// =============================================================================

TEST_F(AxisMoveResizeTest, MoveRowToHigherPosition) {
    // Move row 0 to position 3
    bool success = sheet_ptr->moveRow(rowIds[0], 3);
    EXPECT_TRUE(success);

    // Verify the row is now at position 3
    Axis* row = sheet_ptr->getRow(rowIds[0]);
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(row->position, 3u);

    // Other rows should have shifted
    EXPECT_EQ(sheet_ptr->getRow(rowIds[1])->position, 0u);
    EXPECT_EQ(sheet_ptr->getRow(rowIds[2])->position, 1u);
    EXPECT_EQ(sheet_ptr->getRow(rowIds[3])->position, 2u);
    EXPECT_EQ(sheet_ptr->getRow(rowIds[4])->position, 4u);
}

TEST_F(AxisMoveResizeTest, MoveRowToLowerPosition) {
    // Move row 4 to position 1
    bool success = sheet_ptr->moveRow(rowIds[4], 1);
    EXPECT_TRUE(success);

    // Verify the row is now at position 1
    Axis* row = sheet_ptr->getRow(rowIds[4]);
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(row->position, 1u);

    // Other rows should have shifted
    EXPECT_EQ(sheet_ptr->getRow(rowIds[0])->position, 0u);
    EXPECT_EQ(sheet_ptr->getRow(rowIds[1])->position, 2u);
    EXPECT_EQ(sheet_ptr->getRow(rowIds[2])->position, 3u);
    EXPECT_EQ(sheet_ptr->getRow(rowIds[3])->position, 4u);
}

TEST_F(AxisMoveResizeTest, MoveRowToSamePosition) {
    // Move row 2 to position 2 (no change)
    bool success = sheet_ptr->moveRow(rowIds[2], 2);
    EXPECT_TRUE(success);

    // All positions should remain the same
    for (int i = 0; i < 5; i++) {
        EXPECT_EQ(sheet_ptr->getRow(rowIds[i])->position, static_cast<uint32_t>(i));
    }
}

TEST_F(AxisMoveResizeTest, MoveNonexistentRowFails) {
    ID fakeRowId = generate_id();
    bool success = sheet_ptr->moveRow(fakeRowId, 2);
    EXPECT_FALSE(success);
}

TEST_F(AxisMoveResizeTest, MoveRowPreservesCellData) {
    // Get the cell value at col0, row0 before move
    Cell* cell = workbook->getCell(cellIds[0][0]);
    ASSERT_NE(cell, nullptr);
    double originalValue = cell->value.asNumber();

    // Move row 0 to position 3
    bool success = sheet_ptr->moveRow(rowIds[0], 3);
    EXPECT_TRUE(success);

    // The cell should still exist with the same value
    cell = workbook->getCell(cellIds[0][0]);
    ASSERT_NE(cell, nullptr);
    EXPECT_EQ(cell->value.asNumber(), originalValue);
    EXPECT_EQ(cell->rowId, rowIds[0]);  // Cell still references the same row ID
}

TEST_F(AxisMoveResizeTest, MoveRowUpdatesFormulaDisplay) {
    // Create a formula cell that references row 1 (position 1 = row 2)
    // Cell at col0, row2 with formula referencing A2
    Cell* formulaCell = workbook->getCell(cellIds[0][2]);
    ASSERT_NE(formulaCell, nullptr);

    bool set = setResolvedFormula(formulaCell->id, "=A2");
    EXPECT_TRUE(set);
    EXPECT_EQ(getFormulaDisplay(formulaCell->id), "=A2");

    // Move row 0 (position 0 = row 1) to position 1
    // Now what was row 2 becomes row 1
    bool success = sheet_ptr->moveRow(rowIds[0], 1);
    EXPECT_TRUE(success);

    // The formula still references the same UUID, but display changes
    std::string display = getFormulaDisplay(formulaCell->id);
    EXPECT_EQ(display, "=A1");
}

TEST_F(AxisMoveResizeTest, MoveRowViaCrdtOp) {
    // Update row position via CRDT operation
    std::string payload = R"({"pos":3})";
    Operation op = makeRowSetOp(*workbook, rowIds[1], sheet_id, payload);
    ApplyResult result = applyOperation(*workbook, op);
    EXPECT_EQ(result, ApplyResult::SUCCESS);

    // Verify position updated
    Axis* row = sheet_ptr->getRow(rowIds[1]);
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(row->position, 3u);
}

// =============================================================================
// 4c: Test column resize preserving cell data
// =============================================================================

TEST_F(AxisMoveResizeTest, ResizeColumnViaDirectMutation) {
    // Get initial size
    Axis* col = sheet_ptr->getColumn(colIds[0]);
    ASSERT_NE(col, nullptr);
    EXPECT_EQ(col->size, 100u);

    // Resize directly
    col->size = 200;
    EXPECT_EQ(col->size, 200u);

    // Cell data should be preserved
    Cell* cell = workbook->getCell(cellIds[0][0]);
    ASSERT_NE(cell, nullptr);
    EXPECT_EQ(cell->value.asNumber(), 0.0);  // col 0, row 0 -> value 0
}

TEST_F(AxisMoveResizeTest, ResizeColumnViaCrdtOp) {
    // Resize via CRDT operation
    std::string payload = R"({"size":250})";
    Operation op = makeColSetOp(*workbook, colIds[1], sheet_id, payload);
    ApplyResult result = applyOperation(*workbook, op);
    EXPECT_EQ(result, ApplyResult::SUCCESS);

    // Verify size updated
    Axis* col = sheet_ptr->getColumn(colIds[1]);
    ASSERT_NE(col, nullptr);
    EXPECT_EQ(col->size, 250u);

    // Cell data should be preserved
    Cell* cell = workbook->getCell(cellIds[1][0]);
    ASSERT_NE(cell, nullptr);
    EXPECT_EQ(cell->value.asNumber(), 10.0);  // col 1, row 0 -> value 10
}

TEST_F(AxisMoveResizeTest, ResizeColumnToMinimum) {
    std::string payload = R"({"size":1})";
    Operation op = makeColSetOp(*workbook, colIds[2], sheet_id, payload);
    ApplyResult result = applyOperation(*workbook, op);
    EXPECT_EQ(result, ApplyResult::SUCCESS);

    Axis* col = sheet_ptr->getColumn(colIds[2]);
    ASSERT_NE(col, nullptr);
    EXPECT_EQ(col->size, 1u);
}

TEST_F(AxisMoveResizeTest, ResizeColumnToLargeValue) {
    std::string payload = R"({"size":10000})";
    Operation op = makeColSetOp(*workbook, colIds[3], sheet_id, payload);
    ApplyResult result = applyOperation(*workbook, op);
    EXPECT_EQ(result, ApplyResult::SUCCESS);

    Axis* col = sheet_ptr->getColumn(colIds[3]);
    ASSERT_NE(col, nullptr);
    EXPECT_EQ(col->size, 10000u);
}

TEST_F(AxisMoveResizeTest, ResizeMultipleColumns) {
    // Resize multiple columns in sequence
    for (int i = 0; i < 5; i++) {
        std::string payload = R"({"size":)" + std::to_string(100 + i * 50) + "}";
        Operation op = makeColSetOp(*workbook, colIds[i], sheet_id, payload);
        applyOperation(*workbook, op);
    }

    // Verify all sizes
    for (int i = 0; i < 5; i++) {
        Axis* col = sheet_ptr->getColumn(colIds[i]);
        EXPECT_EQ(col->size, static_cast<uint32_t>(100 + i * 50));
    }
}

// =============================================================================
// 4d: Test row resize preserving cell data
// =============================================================================

TEST_F(AxisMoveResizeTest, ResizeRowViaDirectMutation) {
    // Get initial size
    Axis* row = sheet_ptr->getRow(rowIds[0]);
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(row->size, 21u);

    // Resize directly
    row->size = 50;
    EXPECT_EQ(row->size, 50u);

    // Cell data should be preserved
    Cell* cell = workbook->getCell(cellIds[0][0]);
    ASSERT_NE(cell, nullptr);
    EXPECT_EQ(cell->value.asNumber(), 0.0);
}

TEST_F(AxisMoveResizeTest, ResizeRowViaCrdtOp) {
    // Resize via CRDT operation
    std::string payload = R"({"size":40})";
    Operation op = makeRowSetOp(*workbook, rowIds[1], sheet_id, payload);
    ApplyResult result = applyOperation(*workbook, op);
    EXPECT_EQ(result, ApplyResult::SUCCESS);

    // Verify size updated
    Axis* row = sheet_ptr->getRow(rowIds[1]);
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(row->size, 40u);

    // Cell data should be preserved
    Cell* cell = workbook->getCell(cellIds[0][1]);
    ASSERT_NE(cell, nullptr);
    EXPECT_EQ(cell->value.asNumber(), 1.0);  // col 0, row 1 -> value 1
}

TEST_F(AxisMoveResizeTest, ResizeRowToMinimum) {
    std::string payload = R"({"size":1})";
    Operation op = makeRowSetOp(*workbook, rowIds[2], sheet_id, payload);
    ApplyResult result = applyOperation(*workbook, op);
    EXPECT_EQ(result, ApplyResult::SUCCESS);

    Axis* row = sheet_ptr->getRow(rowIds[2]);
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(row->size, 1u);
}

TEST_F(AxisMoveResizeTest, ResizeRowToLargeValue) {
    std::string payload = R"({"size":5000})";
    Operation op = makeRowSetOp(*workbook, rowIds[3], sheet_id, payload);
    ApplyResult result = applyOperation(*workbook, op);
    EXPECT_EQ(result, ApplyResult::SUCCESS);

    Axis* row = sheet_ptr->getRow(rowIds[3]);
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(row->size, 5000u);
}

TEST_F(AxisMoveResizeTest, ResizeMultipleRows) {
    // Resize multiple rows in sequence
    for (int i = 0; i < 5; i++) {
        std::string payload = R"({"size":)" + std::to_string(20 + i * 10) + "}";
        Operation op = makeRowSetOp(*workbook, rowIds[i], sheet_id, payload);
        applyOperation(*workbook, op);
    }

    // Verify all sizes
    for (int i = 0; i < 5; i++) {
        Axis* row = sheet_ptr->getRow(rowIds[i]);
        EXPECT_EQ(row->size, static_cast<uint32_t>(20 + i * 10));
    }
}

// =============================================================================
// 4e: Test moving axis that is part of a range boundary
// =============================================================================

// Helper function to create a range
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

TEST_F(AxisMoveResizeTest, MoveColumnInRangePreservesRange) {
    // Create a range from col0 to col2
    ID rangeId = generate_id();
    createRange(*workbook, rangeId, sheet_id, colIds[0], rowIds[0], colIds[2], rowIds[2]);

    // Verify range exists
    Range* range = workbook->getRange(rangeId);
    ASSERT_NE(range, nullptr);
    EXPECT_EQ(range->startColId, colIds[0]);
    EXPECT_EQ(range->endColId, colIds[2]);

    // Move col1 (middle of range) to position 4
    bool success = sheet_ptr->moveColumn(colIds[1], 4);
    EXPECT_TRUE(success);

    // Range should still reference the same UUIDs (range defined by UUIDs, not positions)
    range = workbook->getRange(rangeId);
    ASSERT_NE(range, nullptr);
    EXPECT_EQ(range->startColId, colIds[0]);
    EXPECT_EQ(range->endColId, colIds[2]);
}

TEST_F(AxisMoveResizeTest, MoveStartColumnOfRange) {
    // Create a range from col1 to col3
    ID rangeId = generate_id();
    createRange(*workbook, rangeId, sheet_id, colIds[1], rowIds[0], colIds[3], rowIds[2]);

    // Verify range
    Range* range = workbook->getRange(rangeId);
    ASSERT_NE(range, nullptr);
    EXPECT_EQ(range->startColId, colIds[1]);

    // Move the start column (col1) to position 4
    bool success = sheet_ptr->moveColumn(colIds[1], 4);
    EXPECT_TRUE(success);

    // Range still references col1 UUID (positions changed, but UUIDs stable)
    range = workbook->getRange(rangeId);
    ASSERT_NE(range, nullptr);
    EXPECT_EQ(range->startColId, colIds[1]);
}

TEST_F(AxisMoveResizeTest, MoveEndRowOfRange) {
    // Create a range from row0 to row2
    ID rangeId = generate_id();
    createRange(*workbook, rangeId, sheet_id, colIds[0], rowIds[0], colIds[2], rowIds[2]);

    // Verify range
    Range* range = workbook->getRange(rangeId);
    ASSERT_NE(range, nullptr);
    EXPECT_EQ(range->endRowId, rowIds[2]);

    // Move the end row (row2) to position 4
    bool success = sheet_ptr->moveRow(rowIds[2], 4);
    EXPECT_TRUE(success);

    // Range still references row2 UUID
    range = workbook->getRange(rangeId);
    ASSERT_NE(range, nullptr);
    EXPECT_EQ(range->endRowId, rowIds[2]);
}

TEST_F(AxisMoveResizeTest, MoveAxisOutsideRangeDoesNotAffectRange) {
    // Create a range from col0 to col1
    ID rangeId = generate_id();
    createRange(*workbook, rangeId, sheet_id, colIds[0], rowIds[0], colIds[1], rowIds[1]);

    // Move col4 (outside range)
    bool success = sheet_ptr->moveColumn(colIds[4], 0);
    EXPECT_TRUE(success);

    // Range should be unchanged
    Range* range = workbook->getRange(rangeId);
    ASSERT_NE(range, nullptr);
    EXPECT_EQ(range->startColId, colIds[0]);
    EXPECT_EQ(range->endColId, colIds[1]);
}

// =============================================================================
// 4f: Test concurrent move operations from multiple peers
// =============================================================================

class AxisMoveConcurrencyTest : public ::testing::Test {
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
            col->size = 100;
            sheet->addColumn(std::move(col));
        }

        // Create shared rows at positions 0, 1, 2
        for (int i = 0; i < 3; i++) {
            auto row = std::make_unique<Axis>(sharedRowIds[i], false);
            row->position = static_cast<uint32_t>(i);
            row->size = 21;
            sheet->addRow(std::move(row));
        }

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
};

TEST_F(AxisMoveConcurrencyTest, ConcurrentColumnPositionUpdate) {
    // Both peers update the same column's position concurrently
    HLC hlc_a(1000, 0, node_a);
    HLC hlc_b(1000, 0, node_b);

    Operation opA(hlc_a, OpType::COL_SET, sharedColIds[0], R"({"pos":1})");
    opA.sheetId = sharedSheetId;
    Operation opB(hlc_b, OpType::COL_SET, sharedColIds[0], R"({"pos":2})");
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
    EXPECT_EQ(col_a->position, 2u);  // NodeBBBB wins with pos=2
}

TEST_F(AxisMoveConcurrencyTest, ConcurrentRowPositionUpdate) {
    // Both peers update the same row's position concurrently
    HLC hlc_a(1000, 0, node_a);
    HLC hlc_b(1000, 0, node_b);

    Operation opA(hlc_a, OpType::ROW_SET, sharedRowIds[1], R"({"pos":0})");
    opA.sheetId = sharedSheetId;
    Operation opB(hlc_b, OpType::ROW_SET, sharedRowIds[1], R"({"pos":2})");
    opB.sheetId = sharedSheetId;

    // Apply in different orders
    applyOperation(*workbook_a, opA);
    applyOperation(*workbook_a, opB);

    applyOperation(*workbook_b, opB);
    applyOperation(*workbook_b, opA);

    // Both should converge
    Sheet* sheet_a = workbook_a->getSheetByIndex(0);
    Sheet* sheet_b = workbook_b->getSheetByIndex(0);

    Axis* row_a = sheet_a->getRow(sharedRowIds[1]);
    Axis* row_b = sheet_b->getRow(sharedRowIds[1]);

    ASSERT_NE(row_a, nullptr);
    ASSERT_NE(row_b, nullptr);
    EXPECT_EQ(row_a->position, row_b->position);
}

TEST_F(AxisMoveConcurrencyTest, ConcurrentResizeOperations) {
    // Both peers resize the same column concurrently
    HLC hlc_a(1000, 0, node_a);
    HLC hlc_b(1000, 0, node_b);

    Operation opA(hlc_a, OpType::COL_SET, sharedColIds[1], R"({"size":150})");
    opA.sheetId = sharedSheetId;
    Operation opB(hlc_b, OpType::COL_SET, sharedColIds[1], R"({"size":200})");
    opB.sheetId = sharedSheetId;

    // Apply in different orders
    applyOperation(*workbook_a, opA);
    applyOperation(*workbook_a, opB);

    applyOperation(*workbook_b, opB);
    applyOperation(*workbook_b, opA);

    // Both should converge to the same size
    Sheet* sheet_a = workbook_a->getSheetByIndex(0);
    Sheet* sheet_b = workbook_b->getSheetByIndex(0);

    Axis* col_a = sheet_a->getColumn(sharedColIds[1]);
    Axis* col_b = sheet_b->getColumn(sharedColIds[1]);

    ASSERT_NE(col_a, nullptr);
    ASSERT_NE(col_b, nullptr);
    EXPECT_EQ(col_a->size, col_b->size);
    EXPECT_EQ(col_a->size, 200u);  // NodeBBBB wins
}

TEST_F(AxisMoveConcurrencyTest, LaterHLCWinsPositionUpdate) {
    // Test that later HLC wins regardless of node ID
    HLC hlc_early(1000, 0, node_b);  // Earlier timestamp but "higher" node
    HLC hlc_late(1001, 0, node_a);   // Later timestamp but "lower" node

    Operation opEarly(hlc_early, OpType::COL_SET, sharedColIds[2], R"({"pos":0})");
    opEarly.sheetId = sharedSheetId;
    Operation opLate(hlc_late, OpType::COL_SET, sharedColIds[2], R"({"pos":1})");
    opLate.sheetId = sharedSheetId;

    // Apply in both orders
    applyOperation(*workbook_a, opEarly);
    applyOperation(*workbook_a, opLate);

    applyOperation(*workbook_b, opLate);
    applyOperation(*workbook_b, opEarly);

    // Both should converge to pos=1 (later HLC wins)
    Sheet* sheet_a = workbook_a->getSheetByIndex(0);
    Sheet* sheet_b = workbook_b->getSheetByIndex(0);

    Axis* col_a = sheet_a->getColumn(sharedColIds[2]);
    Axis* col_b = sheet_b->getColumn(sharedColIds[2]);

    ASSERT_NE(col_a, nullptr);
    ASSERT_NE(col_b, nullptr);
    EXPECT_EQ(col_a->position, 1u);
    EXPECT_EQ(col_b->position, 1u);
}

TEST_F(AxisMoveConcurrencyTest, ConcurrentMoveAndResizeSameOp) {
    // One peer moves and resizes, another moves and resizes differently
    // Both operations update BOTH fields - ensures proper convergence
    HLC hlc_a(1000, 0, node_a);
    HLC hlc_b(1000, 0, node_b);

    // Both operations update pos and size
    Operation opA(hlc_a, OpType::COL_SET, sharedColIds[0], R"({"pos":2,"size":150})");
    opA.sheetId = sharedSheetId;
    Operation opB(hlc_b, OpType::COL_SET, sharedColIds[0], R"({"pos":1,"size":300})");
    opB.sheetId = sharedSheetId;

    // Apply both operations in different orders
    applyOperation(*workbook_a, opA);
    applyOperation(*workbook_a, opB);

    applyOperation(*workbook_b, opB);
    applyOperation(*workbook_b, opA);

    // Both workbooks should converge to the same state
    Sheet* sheet_a = workbook_a->getSheetByIndex(0);
    Sheet* sheet_b = workbook_b->getSheetByIndex(0);

    Axis* col_a = sheet_a->getColumn(sharedColIds[0]);
    Axis* col_b = sheet_b->getColumn(sharedColIds[0]);

    ASSERT_NE(col_a, nullptr);
    ASSERT_NE(col_b, nullptr);

    // Both workbooks should converge to the same state
    EXPECT_EQ(col_a->size, col_b->size);
    EXPECT_EQ(col_a->position, col_b->position);

    // NodeBBBB's operation wins (pos=1, size=300)
    EXPECT_EQ(col_a->position, 1u);
    EXPECT_EQ(col_a->size, 300u);
}

TEST_F(AxisMoveConcurrencyTest, ThreePeersConvergeOnMoves) {
    ID node_c("NodeCCCC");
    auto workbook_c = createWorkbook(node_c);

    // All three peers update positions
    HLC hlc_a(1000, 0, node_a);
    HLC hlc_b(1000, 0, node_b);
    HLC hlc_c(1000, 0, node_c);

    Operation opA(hlc_a, OpType::COL_SET, sharedColIds[0], R"({"pos":0})");
    opA.sheetId = sharedSheetId;
    Operation opB(hlc_b, OpType::COL_SET, sharedColIds[0], R"({"pos":1})");
    opB.sheetId = sharedSheetId;
    Operation opC(hlc_c, OpType::COL_SET, sharedColIds[0], R"({"pos":2})");
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

    // All should converge to same position (NodeCCCC > NodeBBBB > NodeAAAA)
    Sheet* sheet_a = workbook_a->getSheetByIndex(0);
    Sheet* sheet_b = workbook_b->getSheetByIndex(0);
    Sheet* sheet_c = workbook_c->getSheetByIndex(0);

    Axis* col_a = sheet_a->getColumn(sharedColIds[0]);
    Axis* col_b = sheet_b->getColumn(sharedColIds[0]);
    Axis* col_c = sheet_c->getColumn(sharedColIds[0]);

    EXPECT_EQ(col_a->position, col_b->position);
    EXPECT_EQ(col_b->position, col_c->position);
    EXPECT_EQ(col_a->position, 2u);  // NodeCCCC wins
}

// =============================================================================
// 4g: Test axis hidden/shown toggle with formula visibility
// =============================================================================

TEST_F(AxisMoveResizeTest, HideColumnViaDirectMutation) {
    Axis* col = sheet_ptr->getColumn(colIds[0]);
    ASSERT_NE(col, nullptr);
    EXPECT_FALSE(col->hidden());

    col->setHidden(true);
    EXPECT_TRUE(col->hidden());

    col->setHidden(false);
    EXPECT_FALSE(col->hidden());
}

TEST_F(AxisMoveResizeTest, HideColumnViaCrdtOp) {
    std::string payload = R"({"hidden":true})";
    Operation op = makeColSetOp(*workbook, colIds[1], sheet_id, payload);
    ApplyResult result = applyOperation(*workbook, op);
    EXPECT_EQ(result, ApplyResult::SUCCESS);

    Axis* col = sheet_ptr->getColumn(colIds[1]);
    ASSERT_NE(col, nullptr);
    EXPECT_TRUE(col->hidden());
}

TEST_F(AxisMoveResizeTest, ShowColumnViaCrdtOp) {
    // First hide it
    Axis* col = sheet_ptr->getColumn(colIds[2]);
    col->setHidden(true);
    EXPECT_TRUE(col->hidden());

    // Then show via CRDT
    std::string payload = R"({"hidden":false})";
    Operation op = makeColSetOp(*workbook, colIds[2], sheet_id, payload);
    ApplyResult result = applyOperation(*workbook, op);
    EXPECT_EQ(result, ApplyResult::SUCCESS);

    EXPECT_FALSE(col->hidden());
}

TEST_F(AxisMoveResizeTest, HideRowViaDirectMutation) {
    Axis* row = sheet_ptr->getRow(rowIds[0]);
    ASSERT_NE(row, nullptr);
    EXPECT_FALSE(row->hidden());

    row->setHidden(true);
    EXPECT_TRUE(row->hidden());

    row->setHidden(false);
    EXPECT_FALSE(row->hidden());
}

TEST_F(AxisMoveResizeTest, HideRowViaCrdtOp) {
    std::string payload = R"({"hidden":true})";
    Operation op = makeRowSetOp(*workbook, rowIds[1], sheet_id, payload);
    ApplyResult result = applyOperation(*workbook, op);
    EXPECT_EQ(result, ApplyResult::SUCCESS);

    Axis* row = sheet_ptr->getRow(rowIds[1]);
    ASSERT_NE(row, nullptr);
    EXPECT_TRUE(row->hidden());
}

TEST_F(AxisMoveResizeTest, HiddenColumnDoesNotAffectCellData) {
    // Get cell value before hiding
    Cell* cell = workbook->getCell(cellIds[0][0]);
    ASSERT_NE(cell, nullptr);
    double originalValue = cell->value.asNumber();

    // Hide the column
    Axis* col = sheet_ptr->getColumn(colIds[0]);
    col->setHidden(true);

    // Cell data should be unchanged
    cell = workbook->getCell(cellIds[0][0]);
    ASSERT_NE(cell, nullptr);
    EXPECT_EQ(cell->value.asNumber(), originalValue);
}

TEST_F(AxisMoveResizeTest, HiddenRowDoesNotAffectFormulas) {
    // Create a formula referencing a cell
    Cell* formulaCell = workbook->getCell(cellIds[2][2]);
    ASSERT_NE(formulaCell, nullptr);

    bool set = setResolvedFormula(formulaCell->id, "=A1");
    EXPECT_TRUE(set);

    // Hide row 0 (where A1 is)
    Axis* row = sheet_ptr->getRow(rowIds[0]);
    row->setHidden(true);

    // Formula should still work and display correctly
    std::string display = getFormulaDisplay(formulaCell->id);
    EXPECT_EQ(display, "=A1");
}

TEST_F(AxisMoveResizeTest, HideAndResizeColumn) {
    // Hide and resize in one operation
    std::string payload = R"({"hidden":true,"size":50})";
    Operation op = makeColSetOp(*workbook, colIds[3], sheet_id, payload);
    ApplyResult result = applyOperation(*workbook, op);
    EXPECT_EQ(result, ApplyResult::SUCCESS);

    Axis* col = sheet_ptr->getColumn(colIds[3]);
    ASSERT_NE(col, nullptr);
    EXPECT_TRUE(col->hidden());
    EXPECT_EQ(col->size, 50u);
}

TEST_F(AxisMoveResizeTest, HideAndMoveColumn) {
    // Hide and move in one operation
    std::string payload = R"({"hidden":true,"pos":0})";
    Operation op = makeColSetOp(*workbook, colIds[4], sheet_id, payload);
    ApplyResult result = applyOperation(*workbook, op);
    EXPECT_EQ(result, ApplyResult::SUCCESS);

    Axis* col = sheet_ptr->getColumn(colIds[4]);
    ASSERT_NE(col, nullptr);
    EXPECT_TRUE(col->hidden());
    EXPECT_EQ(col->position, 0u);
}

TEST_F(AxisMoveResizeTest, UseAxisSetHiddenOpHelper) {
    // Use the convenience function
    Operation op = makeAxisSetHiddenOp(*workbook, colIds[0], true);
    ApplyResult result = applyOperation(*workbook, op);
    EXPECT_EQ(result, ApplyResult::SUCCESS);

    Axis* col = sheet_ptr->getColumn(colIds[0]);
    ASSERT_NE(col, nullptr);
    EXPECT_TRUE(col->hidden());

    // Unhide
    op = makeAxisSetHiddenOp(*workbook, colIds[0], false);
    result = applyOperation(*workbook, op);
    EXPECT_EQ(result, ApplyResult::SUCCESS);
    EXPECT_FALSE(col->hidden());
}

}  // namespace
}  // namespace cells
