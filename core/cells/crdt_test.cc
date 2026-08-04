#include "core/cells/crdt.h"

#include "core/cells/format_buffer.h"
#include "core/cells/id.h"
#include "core/cells/number_format.h"
#include "core/cells/range.h"
#include "core/cells/style_buffer.h"

#include "gtest/gtest.h"

namespace cells {
namespace {

class CRDTTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a workbook with a sheet and some cells
        workbook = std::make_unique<Workbook>(generate_id(), "TestWorkbook");
        workbook->setNodeId(generate_id());

        auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet1");
        sheet_id = sheet->id;
        sheet->setWorkbook(workbook.get());  // Set workbook early so cells get stored properly

        // Create columns
        col1 = generate_id();
        col2 = generate_id();
        auto c1 = std::make_unique<Axis>(col1, true);
        auto c2 = std::make_unique<Axis>(col2, true);
        sheet->addColumn(std::move(c1));
        sheet->addColumn(std::move(c2));

        // Create rows
        row1 = generate_id();
        row2 = generate_id();
        auto r1 = std::make_unique<Axis>(row1, false);
        auto r2 = std::make_unique<Axis>(row2, false);
        sheet->addRow(std::move(r1));
        sheet->addRow(std::move(r2));

        // Create cells
        cell1 = generate_id();
        cell2 = generate_id();
        auto c1_cell = std::make_unique<Cell>(cell1, col1, row1);
        c1_cell->value = CellValue(42.0);
        auto c2_cell = std::make_unique<Cell>(cell2, col2, row1);
        c2_cell->value = CellValue("Hello");

        sheet->addCell(std::move(c1_cell));
        sheet->addCell(std::move(c2_cell));

        workbook->addSheet(std::move(sheet));
    }

    std::unique_ptr<Workbook> workbook;
    ID sheet_id;
    ID col1, col2;
    ID row1, row2;
    ID cell1, cell2;
};

TEST_F(CRDTTest, ApplyCellSetValue) {
    // Create an operation to set cell1's value
    Operation op = makeCellSetOp(*workbook, cell1, R"({"t":"n","v":"100"})");

    ApplyResult result = applyOperation(*workbook, op);
    EXPECT_EQ(result, ApplyResult::SUCCESS);

    // Verify the value changed
    Sheet* sheet = workbook->getSheetByIndex(0);
    Cell* cell = sheet->getCell(cell1);
    EXPECT_EQ(cell->value.asNumber(), 100);
}

TEST_F(CRDTTest, ApplyCellClear) {
    // First verify cell exists and has a value
    Sheet* sheet = workbook->getSheetByIndex(0);
    Cell* cell = sheet->getCell(cell1);
    ASSERT_NE(cell, nullptr);
    EXPECT_EQ(cell->value.type, CellValueType::NUMBER);

    // Create and apply clear operation
    Operation op = makeCellDeleteOp(*workbook, cell1);
    ApplyResult result = applyOperation(*workbook, op);
    EXPECT_EQ(result, ApplyResult::SUCCESS);

    // Verify the cell was removed from the sheet
    cell = sheet->getCell(cell1);
    EXPECT_EQ(cell, nullptr);
}

TEST_F(CRDTTest, DuplicateOperationRejected) {
    Operation op = makeCellSetOp(*workbook, cell1, R"({"t":"n","v":"100"})");

    ApplyResult result1 = applyOperation(*workbook, op);
    EXPECT_EQ(result1, ApplyResult::SUCCESS);

    // Apply same operation again
    ApplyResult result2 = applyOperation(*workbook, op);
    EXPECT_EQ(result2, ApplyResult::ALREADY_APPLIED);
}

TEST_F(CRDTTest, LastWriterWins) {
    // Apply an older operation
    ID node1("Node1111");
    HLC old_hlc(1000, 0, node1);
    Operation old_op(old_hlc, OpType::CELL_SET, cell1, R"({"t":"n","v":"50"})");

    // Apply a newer operation first
    Operation new_op = makeCellSetOp(*workbook, cell1, R"({"t":"n","v":"100"})");
    applyOperation(*workbook, new_op);

    // Now apply the older operation
    ApplyResult result = applyOperation(*workbook, old_op);
    EXPECT_EQ(result, ApplyResult::SUPERSEDED);

    // Value should still be 100 (newer operation wins)
    Sheet* sheet = workbook->getSheetByIndex(0);
    Cell* cell = sheet->getCell(cell1);
    EXPECT_EQ(cell->value.asNumber(), 100);
}

TEST_F(CRDTTest, OperationsAddedToOpLog) {
    Operation op1 = makeCellSetOp(*workbook, cell1, R"({"t":"n","v":"100"})");
    Operation op2 = makeCellSetOp(*workbook, cell2, R"({"t":"s","v":"World"})");

    applyOperation(*workbook, op1);
    applyOperation(*workbook, op2);

    const OpLog* oplog = workbook->getOpLog();
    EXPECT_EQ(oplog->size(), 2);
}

TEST_F(CRDTTest, ApplyMultipleOperations) {
    std::vector<Operation> ops;
    ops.push_back(makeCellSetOp(*workbook, cell1, R"({"t":"n","v":"1"})"));
    ops.push_back(makeCellSetOp(*workbook, cell1, R"({"t":"n","v":"2"})"));
    ops.push_back(makeCellSetOp(*workbook, cell1, R"({"t":"n","v":"3"})"));

    size_t applied = applyOperations(*workbook, ops);
    EXPECT_EQ(applied, 3);

    // Last value should be 3 (highest HLC)
    Sheet* sheet = workbook->getSheetByIndex(0);
    Cell* cell = sheet->getCell(cell1);
    EXPECT_EQ(cell->value.asNumber(), 3);
}

TEST_F(CRDTTest, IsSuperseded) {
    // Apply an operation
    Operation op1 = makeCellSetOp(*workbook, cell1, R"({"t":"n","v":"100"})");
    applyOperation(*workbook, op1);

    // Create an older operation
    ID node1("Node1111");
    HLC old_hlc(1000, 0, node1);
    Operation old_op(old_hlc, OpType::CELL_SET, cell1, R"({"t":"n","v":"50"})");

    EXPECT_TRUE(isSuperseded(*workbook, old_op));
    // op1 has the same HLC as the latest, so it's considered superseded (>= check)
    EXPECT_TRUE(isSuperseded(*workbook, op1));

    // A newer operation is not superseded
    Operation op2 = makeCellSetOp(*workbook, cell1, R"({"t":"n","v":"200"})");
    EXPECT_FALSE(isSuperseded(*workbook, op2));  // op2 is newer than what's in OpLog
}

TEST_F(CRDTTest, InvalidTargetReturnsError) {
    // Try to create a cell with col/row that don't exist
    ID fake_cell("FAKECELL");
    ID fake_col("FAKECOL");
    ID fake_row("FAKEROW");
    HLC hlc = workbook->getCurrentHLC();
    std::string payload = R"({"col":")" + fake_col.toString() + R"(","row":")" +
                          fake_row.toString() + R"(","t":"n","v":"100"})";
    Operation bad_op(hlc, OpType::CELL_SET, fake_cell, payload);
    bad_op.sheetId = sheet_id;

    ApplyResult result = applyOperation(*workbook, bad_op);
    EXPECT_EQ(result, ApplyResult::INVALID_TARGET);
}

TEST_F(CRDTTest, InvalidPayloadReturnsError) {
    // For CELL_SET, invalid payload means trying to create a cell without col/row
    HLC hlc = workbook->getCurrentHLC();
    ID new_cell_id = generate_id();
    Operation bad_op(hlc, OpType::CELL_SET, new_cell_id, R"({invalid json})");
    bad_op.sheetId = sheet_id;

    ApplyResult result = applyOperation(*workbook, bad_op);
    EXPECT_EQ(result, ApplyResult::INVALID_PAYLOAD);
}

TEST_F(CRDTTest, SheetRename) {
    Operation op = makeSheetSetOp(*workbook, sheet_id, R"({"name":"RenamedSheet"})");
    ApplyResult result = applyOperation(*workbook, op);
    EXPECT_EQ(result, ApplyResult::SUCCESS);

    Sheet* sheet = workbook->getSheetByIndex(0);
    EXPECT_EQ(sheet->name, "RenamedSheet");
}

TEST_F(CRDTTest, AxisResize) {
    Operation op = makeColSetOp(*workbook, col1, R"({"size":200})");
    ApplyResult result = applyOperation(*workbook, op);
    EXPECT_EQ(result, ApplyResult::SUCCESS);

    Sheet* sheet = workbook->getSheetByIndex(0);
    Axis* axis = sheet->getColumn(col1);
    EXPECT_EQ(axis->size, 200);
}

// Test that getColumnByPosition returns correct column after swapping positions
// via CRDT COL_SET operations. This catches bugs where _columnIndex is not
// updated when column positions change.
TEST_F(CRDTTest, ColumnPositionIndexUpdatedAfterSwap) {
    Sheet* sheet = workbook->getSheetByIndex(0);

    // First, set explicit positions for both columns
    Operation op1 = makeColSetOp(*workbook, col1, R"({"pos":1})");
    Operation op2 = makeColSetOp(*workbook, col2, R"({"pos":2})");
    EXPECT_EQ(applyOperation(*workbook, op1), ApplyResult::SUCCESS);
    EXPECT_EQ(applyOperation(*workbook, op2), ApplyResult::SUCCESS);

    // Verify initial positions
    EXPECT_EQ(sheet->getColumn(col1)->position, 1);
    EXPECT_EQ(sheet->getColumn(col2)->position, 2);
    EXPECT_EQ(sheet->getColumnByPosition(1)->id, col1);
    EXPECT_EQ(sheet->getColumnByPosition(2)->id, col2);

    // Swap positions: col1 goes to pos 2, col2 goes to pos 1
    Operation swap1 = makeColSetOp(*workbook, col1, R"({"pos":2})");
    Operation swap2 = makeColSetOp(*workbook, col2, R"({"pos":1})");
    EXPECT_EQ(applyOperation(*workbook, swap1), ApplyResult::SUCCESS);
    EXPECT_EQ(applyOperation(*workbook, swap2), ApplyResult::SUCCESS);

    // Verify positions changed
    EXPECT_EQ(sheet->getColumn(col1)->position, 2);
    EXPECT_EQ(sheet->getColumn(col2)->position, 1);

    // CRITICAL: Verify getColumnByPosition returns the correct columns
    // This is the bug that was fixed - before the fix, these would fail
    // because _columnIndex was not updated when positions changed via CRDT ops
    Axis* colAtPos1 = sheet->getColumnByPosition(1);
    Axis* colAtPos2 = sheet->getColumnByPosition(2);
    ASSERT_NE(colAtPos1, nullptr);
    ASSERT_NE(colAtPos2, nullptr);
    EXPECT_EQ(colAtPos1->id, col2) << "Column at position 1 should be col2 after swap";
    EXPECT_EQ(colAtPos2->id, col1) << "Column at position 2 should be col1 after swap";
}

// Same test for rows
TEST_F(CRDTTest, RowPositionIndexUpdatedAfterSwap) {
    Sheet* sheet = workbook->getSheetByIndex(0);

    // First, set explicit positions for both rows
    Operation op1 = makeRowSetOp(*workbook, row1, R"({"pos":1})");
    Operation op2 = makeRowSetOp(*workbook, row2, R"({"pos":2})");
    EXPECT_EQ(applyOperation(*workbook, op1), ApplyResult::SUCCESS);
    EXPECT_EQ(applyOperation(*workbook, op2), ApplyResult::SUCCESS);

    // Verify initial positions
    EXPECT_EQ(sheet->getRow(row1)->position, 1);
    EXPECT_EQ(sheet->getRow(row2)->position, 2);
    EXPECT_EQ(sheet->getRowByPosition(1)->id, row1);
    EXPECT_EQ(sheet->getRowByPosition(2)->id, row2);

    // Swap positions: row1 goes to pos 2, row2 goes to pos 1
    Operation swap1 = makeRowSetOp(*workbook, row1, R"({"pos":2})");
    Operation swap2 = makeRowSetOp(*workbook, row2, R"({"pos":1})");
    EXPECT_EQ(applyOperation(*workbook, swap1), ApplyResult::SUCCESS);
    EXPECT_EQ(applyOperation(*workbook, swap2), ApplyResult::SUCCESS);

    // Verify positions changed
    EXPECT_EQ(sheet->getRow(row1)->position, 2);
    EXPECT_EQ(sheet->getRow(row2)->position, 1);

    // CRITICAL: Verify getRowByPosition returns the correct rows
    Axis* rowAtPos1 = sheet->getRowByPosition(1);
    Axis* rowAtPos2 = sheet->getRowByPosition(2);
    ASSERT_NE(rowAtPos1, nullptr);
    ASSERT_NE(rowAtPos2, nullptr);
    EXPECT_EQ(rowAtPos1->id, row2) << "Row at position 1 should be row2 after swap";
    EXPECT_EQ(rowAtPos2->id, row1) << "Row at position 2 should be row1 after swap";
}

TEST_F(CRDTTest, ConcurrentEditsConverge) {
    // Simulate two nodes making concurrent edits
    ID node_a("NodeAAAA");
    ID node_b("NodeBBBB");

    // Node A's edit at time 1000
    HLC hlc_a(1000, 0, node_a);
    Operation op_a(hlc_a, OpType::CELL_SET, cell1, R"({"t":"n","v":"100"})");

    // Node B's edit at same time 1000 but different node
    HLC hlc_b(1000, 0, node_b);
    Operation op_b(hlc_b, OpType::CELL_SET, cell1, R"({"t":"n","v":"200"})");

    // Apply in one order
    applyOperation(*workbook, op_a);
    applyOperation(*workbook, op_b);

    // The value should be deterministic based on HLC ordering
    // NodeBBBB > NodeAAAA lexicographically, so op_b wins
    Sheet* sheet = workbook->getSheetByIndex(0);
    Cell* cell = sheet->getCell(cell1);
    EXPECT_EQ(cell->value.asNumber(), 200);
}

TEST_F(CRDTTest, HLCMonotonicallyIncreases) {
    HLC hlc1 = workbook->getCurrentHLC();
    HLC hlc2 = workbook->getCurrentHLC();
    HLC hlc3 = workbook->getCurrentHLC();

    EXPECT_LT(hlc1, hlc2);
    EXPECT_LT(hlc2, hlc3);
}

TEST_F(CRDTTest, GetOperationsSinceForSync) {
    // Apply some operations
    applyOperation(*workbook, makeCellSetOp(*workbook, cell1, R"({"t":"n","v":"1"})"));
    HLC sync_point = workbook->getOpLog()->getCurrentHLC();
    applyOperation(*workbook, makeCellSetOp(*workbook, cell1, R"({"t":"n","v":"2"})"));
    applyOperation(*workbook, makeCellSetOp(*workbook, cell2, R"({"t":"s","v":"test"})"));

    // Get operations since sync point
    auto ops = workbook->getOpLog()->getOperationsSince(sync_point);
    EXPECT_EQ(ops.size(), 2);
}

// =============================================================================
// Multi-peer convergence tests
// =============================================================================

class CRDTConvergenceTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create two workbooks simulating two peers
        node_a = ID("NodeAAAA");
        node_b = ID("NodeBBBB");

        workbook_a = createWorkbook(node_a);
        workbook_b = createWorkbook(node_b);

        // Store shared IDs for use across both workbooks
        cell_id = workbook_a->getSheetByIndex(0)->getCellIds().front();
    }

    std::unique_ptr<Workbook> createWorkbook(const ID& node_id) {
        auto wb = std::make_unique<Workbook>(generate_id(), "TestWorkbook");
        wb->setNodeId(node_id);

        auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet1");
        sheet->setWorkbook(wb.get());  // Set workbook early so cells get stored properly

        // Create a column and row
        ID col = generate_id();
        ID row = generate_id();
        auto c = std::make_unique<Axis>(col, true);
        auto r = std::make_unique<Axis>(row, false);
        sheet->addColumn(std::move(c));
        sheet->addRow(std::move(r));

        // Create a cell
        ID cell = generate_id();
        auto cell_obj = std::make_unique<Cell>(cell, col, row);
        cell_obj->value = CellValue(0.0);
        sheet->addCell(std::move(cell_obj));

        wb->addSheet(std::move(sheet));
        return wb;
    }

    // Synchronize operations from source to target
    void syncOperations(Workbook& source, Workbook& target, const HLC& since = HLC()) {
        auto ops = source.getOpLog()->getOperationsSince(since);
        for (const auto& op : ops) {
            applyOperation(target, op);
        }
    }

    std::unique_ptr<Workbook> workbook_a;
    std::unique_ptr<Workbook> workbook_b;
    ID node_a, node_b;
    ID cell_id;
};

TEST_F(CRDTConvergenceTest, TwoPeersConvergeOnConcurrentEdits) {
    // Create shared cell with shared column/row IDs that both workbooks know about
    ID shared_col = generate_id();
    ID shared_row = generate_id();
    ID shared_cell1 = generate_id();
    ID shared_cell2 = generate_id();

    auto* sheet_a = workbook_a->getSheetByIndex(0);
    auto* sheet_b = workbook_b->getSheetByIndex(0);

    // Add shared column, row, and two cells to both workbooks
    auto col_a = std::make_unique<Axis>(shared_col, true);
    auto col_b = std::make_unique<Axis>(shared_col, true);
    auto row_a = std::make_unique<Axis>(shared_row, false);
    auto row_b = std::make_unique<Axis>(shared_row, false);
    auto cell1_a = std::make_unique<Cell>(shared_cell1, shared_col, shared_row);
    auto cell1_b = std::make_unique<Cell>(shared_cell1, shared_col, shared_row);
    auto cell2_a = std::make_unique<Cell>(shared_cell2, shared_col, shared_row);
    auto cell2_b = std::make_unique<Cell>(shared_cell2, shared_col, shared_row);
    cell1_a->value = CellValue(0.0);
    cell1_b->value = CellValue(0.0);
    cell2_a->value = CellValue(0.0);
    cell2_b->value = CellValue(0.0);

    sheet_a->addColumn(std::move(col_a));
    sheet_a->addRow(std::move(row_a));
    sheet_a->addCell(std::move(cell1_a));
    sheet_a->addCell(std::move(cell2_a));
    sheet_b->addColumn(std::move(col_b));
    sheet_b->addRow(std::move(row_b));
    sheet_b->addCell(std::move(cell1_b));
    sheet_b->addCell(std::move(cell2_b));

    // Peer A makes an edit to cell1
    Operation op_a = makeCellSetOp(*workbook_a, shared_cell1, R"({"t":"n","v":"100"})");
    applyOperation(*workbook_a, op_a);

    // Peer B makes an edit to cell2 (different cell, same workbook state)
    Operation op_b = makeCellSetOp(*workbook_b, shared_cell2, R"({"t":"n","v":"200"})");
    applyOperation(*workbook_b, op_b);

    // Sync A -> B
    applyOperation(*workbook_b, op_a);

    // Sync B -> A
    applyOperation(*workbook_a, op_b);

    // Both should have the operations in their OpLogs
    EXPECT_EQ(workbook_a->getOpLog()->size(), 2);
    EXPECT_EQ(workbook_b->getOpLog()->size(), 2);

    // Verify convergence: both workbooks should have the same values
    Cell* cell1_final_a = sheet_a->getCell(shared_cell1);
    Cell* cell1_final_b = sheet_b->getCell(shared_cell1);
    Cell* cell2_final_a = sheet_a->getCell(shared_cell2);
    Cell* cell2_final_b = sheet_b->getCell(shared_cell2);

    EXPECT_EQ(cell1_final_a->value.asNumber(), 100);
    EXPECT_EQ(cell1_final_b->value.asNumber(), 100);
    EXPECT_EQ(cell2_final_a->value.asNumber(), 200);
    EXPECT_EQ(cell2_final_b->value.asNumber(), 200);
}

TEST_F(CRDTConvergenceTest, SameCellConcurrentEditsConverge) {
    // Create a shared cell ID that both workbooks know about
    ID shared_cell = generate_id();
    ID shared_col = generate_id();
    ID shared_row = generate_id();

    // Add the same cell to both workbooks
    auto* sheet_a = workbook_a->getSheetByIndex(0);
    auto* sheet_b = workbook_b->getSheetByIndex(0);

    auto col_a = std::make_unique<Axis>(shared_col, true);
    auto col_b = std::make_unique<Axis>(shared_col, true);
    auto row_a = std::make_unique<Axis>(shared_row, false);
    auto row_b = std::make_unique<Axis>(shared_row, false);
    auto cell_a = std::make_unique<Cell>(shared_cell, shared_col, shared_row);
    auto cell_b = std::make_unique<Cell>(shared_cell, shared_col, shared_row);
    cell_a->value = CellValue(0.0);
    cell_b->value = CellValue(0.0);

    sheet_a->addColumn(std::move(col_a));
    sheet_a->addRow(std::move(row_a));
    sheet_a->addCell(std::move(cell_a));
    sheet_b->addColumn(std::move(col_b));
    sheet_b->addRow(std::move(row_b));
    sheet_b->addCell(std::move(cell_b));

    // Both peers edit the same cell concurrently
    // Use fixed timestamps to control ordering
    HLC hlc_a(1000, 0, node_a);
    HLC hlc_b(1000, 0, node_b);

    Operation op_a(hlc_a, OpType::CELL_SET, shared_cell, R"({"t":"n","v":"100"})");
    Operation op_b(hlc_b, OpType::CELL_SET, shared_cell, R"({"t":"n","v":"200"})");

    // Apply in different orders on each workbook
    applyOperation(*workbook_a, op_a);
    applyOperation(*workbook_a, op_b);

    applyOperation(*workbook_b, op_b);
    applyOperation(*workbook_b, op_a);

    // Both should converge to the same value
    // Node B > Node A lexicographically, so op_b wins
    Cell* cell_final_a = sheet_a->getCell(shared_cell);
    Cell* cell_final_b = sheet_b->getCell(shared_cell);

    EXPECT_EQ(cell_final_a->value.asNumber(), 200);
    EXPECT_EQ(cell_final_b->value.asNumber(), 200);
}

TEST_F(CRDTConvergenceTest, ThreePeersConverge) {
    // Add a third peer
    ID node_c("NodeCCCC");
    auto workbook_c = createWorkbook(node_c);

    // Create a shared cell ID
    ID shared_cell = generate_id();
    ID shared_col = generate_id();
    ID shared_row = generate_id();

    // Add the same cell to all three workbooks
    for (auto* wb : {workbook_a.get(), workbook_b.get(), workbook_c.get()}) {
        auto* sheet = wb->getSheetByIndex(0);
        auto col = std::make_unique<Axis>(shared_col, true);
        auto row = std::make_unique<Axis>(shared_row, false);
        auto cell = std::make_unique<Cell>(shared_cell, shared_col, shared_row);
        cell->value = CellValue(0.0);
        sheet->addColumn(std::move(col));
        sheet->addRow(std::move(row));
        sheet->addCell(std::move(cell));
    }

    // All three peers edit concurrently
    HLC hlc_a(1000, 0, node_a);
    HLC hlc_b(1000, 0, node_b);
    HLC hlc_c(1000, 0, node_c);

    Operation op_a(hlc_a, OpType::CELL_SET, shared_cell, R"({"t":"n","v":"100"})");
    Operation op_b(hlc_b, OpType::CELL_SET, shared_cell, R"({"t":"n","v":"200"})");
    Operation op_c(hlc_c, OpType::CELL_SET, shared_cell, R"({"t":"n","v":"300"})");

    // Apply in different orders on each workbook
    applyOperation(*workbook_a, op_a);
    applyOperation(*workbook_a, op_b);
    applyOperation(*workbook_a, op_c);

    applyOperation(*workbook_b, op_c);
    applyOperation(*workbook_b, op_a);
    applyOperation(*workbook_b, op_b);

    applyOperation(*workbook_c, op_b);
    applyOperation(*workbook_c, op_c);
    applyOperation(*workbook_c, op_a);

    // All should converge to the same value (NodeCCCC > NodeBBBB > NodeAAAA)
    Cell* cell_a = workbook_a->getSheetByIndex(0)->getCell(shared_cell);
    Cell* cell_b = workbook_b->getSheetByIndex(0)->getCell(shared_cell);
    Cell* cell_c = workbook_c->getSheetByIndex(0)->getCell(shared_cell);

    EXPECT_EQ(cell_a->value.asNumber(), 300);
    EXPECT_EQ(cell_b->value.asNumber(), 300);
    EXPECT_EQ(cell_c->value.asNumber(), 300);
}

TEST_F(CRDTConvergenceTest, RapidSequentialEditsPreserveOrder) {
    // Create a shared cell
    ID shared_cell = generate_id();
    ID shared_col = generate_id();
    ID shared_row = generate_id();

    auto* sheet_a = workbook_a->getSheetByIndex(0);
    auto* sheet_b = workbook_b->getSheetByIndex(0);

    auto col_a = std::make_unique<Axis>(shared_col, true);
    auto col_b = std::make_unique<Axis>(shared_col, true);
    auto row_a = std::make_unique<Axis>(shared_row, false);
    auto row_b = std::make_unique<Axis>(shared_row, false);
    auto cell_a = std::make_unique<Cell>(shared_cell, shared_col, shared_row);
    auto cell_b = std::make_unique<Cell>(shared_cell, shared_col, shared_row);
    cell_a->value = CellValue(0.0);
    cell_b->value = CellValue(0.0);

    sheet_a->addColumn(std::move(col_a));
    sheet_a->addRow(std::move(row_a));
    sheet_a->addCell(std::move(cell_a));
    sheet_b->addColumn(std::move(col_b));
    sheet_b->addRow(std::move(row_b));
    sheet_b->addCell(std::move(cell_b));

    // Peer A makes rapid sequential edits
    std::vector<Operation> ops;
    for (int i = 1; i <= 10; i++) {
        Operation op = makeCellSetOp(*workbook_a, shared_cell,
                                     R"({"t":"n","v":")" + std::to_string(i) + R"("})");
        ops.push_back(op);
        applyOperation(*workbook_a, op);
    }

    // Apply to workbook_b in reverse order (out of order delivery)
    for (auto it = ops.rbegin(); it != ops.rend(); ++it) {
        applyOperation(*workbook_b, *it);
    }

    // Both should have value 10 (latest operation)
    Cell* cell_final_a = sheet_a->getCell(shared_cell);
    Cell* cell_final_b = sheet_b->getCell(shared_cell);

    EXPECT_EQ(cell_final_a->value.asNumber(), 10);
    EXPECT_EQ(cell_final_b->value.asNumber(), 10);
}

TEST_F(CRDTConvergenceTest, DifferentCellsConcurrentEdits) {
    // Create two shared cells
    ID cell1 = generate_id();
    ID cell2 = generate_id();
    ID col = generate_id();
    ID row1 = generate_id();
    ID row2 = generate_id();

    // Add to both workbooks
    for (auto* wb : {workbook_a.get(), workbook_b.get()}) {
        auto* sheet = wb->getSheetByIndex(0);
        sheet->addColumn(std::make_unique<Axis>(col, true));
        sheet->addRow(std::make_unique<Axis>(row1, false));
        sheet->addRow(std::make_unique<Axis>(row2, false));

        auto c1 = std::make_unique<Cell>(cell1, col, row1);
        auto c2 = std::make_unique<Cell>(cell2, col, row2);
        c1->value = CellValue(0.0);
        c2->value = CellValue(0.0);
        sheet->addCell(std::move(c1));
        sheet->addCell(std::move(c2));
    }

    // Peer A edits cell1
    Operation op_a = makeCellSetOp(*workbook_a, cell1, R"({"t":"n","v":"100"})");
    applyOperation(*workbook_a, op_a);

    // Peer B edits cell2
    Operation op_b = makeCellSetOp(*workbook_b, cell2, R"({"t":"n","v":"200"})");
    applyOperation(*workbook_b, op_b);

    // Sync both ways
    applyOperation(*workbook_b, op_a);
    applyOperation(*workbook_a, op_b);

    // Both should have both values
    auto* sheet_a = workbook_a->getSheetByIndex(0);
    auto* sheet_b = workbook_b->getSheetByIndex(0);

    EXPECT_EQ(sheet_a->getCell(cell1)->value.asNumber(), 100);
    EXPECT_EQ(sheet_a->getCell(cell2)->value.asNumber(), 200);
    EXPECT_EQ(sheet_b->getCell(cell1)->value.asNumber(), 100);
    EXPECT_EQ(sheet_b->getCell(cell2)->value.asNumber(), 200);
}

// =============================================================================
// Style Tests
// =============================================================================

TEST_F(CRDTTest, CellStyleDefaults) {
    // Test that CellStyle has correct default values
    CellStyle style;
    EXPECT_FALSE(style.bold);
    EXPECT_FALSE(style.italic);
    EXPECT_FALSE(style.underline);
    EXPECT_FALSE(style.wrapText);
    EXPECT_TRUE(style.bgColor.empty());
    EXPECT_TRUE(style.textColor.empty());
    EXPECT_TRUE(style.fontFamily.empty());
    EXPECT_EQ(style.fontSize, 0);
    // Default hAlign is GENERAL (content-type-based alignment, like Excel)
    EXPECT_EQ(style.hAlign, TextAlign::GENERAL);
    EXPECT_EQ(style.vAlign, VerticalAlign::BOTTOM);
    EXPECT_TRUE(style.isEmpty());
}

TEST_F(CRDTTest, CellStyleNonEmpty) {
    CellStyle style;
    style.bold = true;
    style.setDefined(DEFINED_BOLD);
    EXPECT_FALSE(style.isEmpty());

    CellStyle style2;
    style2.bgColor = "#FF0000";
    style2.setDefined(DEFINED_BGCOLOR);
    EXPECT_FALSE(style2.isEmpty());
}

TEST_F(CRDTTest, CellStyleEquality) {
    CellStyle a, b;
    EXPECT_EQ(a, b);

    a.bold = true;
    a.setDefined(DEFINED_BOLD);
    EXPECT_NE(a, b);

    b.bold = true;
    b.setDefined(DEFINED_BOLD);
    EXPECT_EQ(a, b);

    a.bgColor = "#FF0000";
    a.setDefined(DEFINED_BGCOLOR);
    EXPECT_NE(a, b);
}

TEST_F(CRDTTest, ApplyCellSetStyle) {
    // Create a StyleBuffer with bold=true
    StyleBuffer styleBuf;
    styleBuf.setBold(true);

    // Create and apply style operation using content-addressed style
    Operation op = makeCellSetStyleOp(*workbook, cell1, styleBuf);
    ApplyResult result = applyOperation(*workbook, op);
    EXPECT_EQ(result, ApplyResult::SUCCESS);

    // Verify the style was set (read from workbook entity styles)
    Sheet* sheet = workbook->getSheetByIndex(0);
    Cell* cell = sheet->getCell(cell1);
    const StyleBuffer* entityStyle = workbook->getEntityStyle(cell->id);
    ASSERT_NE(entityStyle, nullptr);
    EXPECT_TRUE(entityStyle->getBold());
}

TEST_F(CRDTTest, CellSetStyleNonExistentCell) {
    // Try to set style on non-existent cell without col/row (invalid payload for creation)
    ID fakeCell("FakeCelX");
    StyleBuffer styleBuf;
    styleBuf.setBold(true);
    Operation op = makeCellSetStyleOp(*workbook, fakeCell, styleBuf);
    ApplyResult result = applyOperation(*workbook, op);
    // Without col/row, this is INVALID_PAYLOAD (can't create cell without position)
    EXPECT_EQ(result, ApplyResult::INVALID_PAYLOAD);
}

// =============================================================================
// Range adjustment on column/row deletion tests
// =============================================================================

class RangeAdjustmentTest : public ::testing::Test {
protected:
    void SetUp() override {
        workbook = std::make_unique<Workbook>(generate_id(), "TestWorkbook");
        workbook->setNodeId(generate_id());

        auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet1");
        sheet_ptr = sheet.get();
        sheet->setWorkbook(workbook.get());  // Set workbook early for axis storage

        // Create 5 columns (A, B, C, D, E) at positions 0-4
        for (int i = 0; i < 5; i++) {
            auto col = std::make_unique<Axis>(generate_id(), true);
            col->position = static_cast<uint32_t>(i);
            colIds[i] = col->id;
            sheet->addColumn(std::move(col));
        }

        // Create 5 rows at positions 0-4
        for (int i = 0; i < 5; i++) {
            auto row = std::make_unique<Axis>(generate_id(), false);
            row->position = static_cast<uint32_t>(i);
            rowIds[i] = row->id;
            sheet->addRow(std::move(row));
        }

        workbook->addSheet(std::move(sheet));
    }

    std::unique_ptr<Workbook> workbook;
    Sheet* sheet_ptr;
    ID colIds[5];
    ID rowIds[5];
};

TEST_F(RangeAdjustmentTest, DeleteStartColumnShrinksRange) {
    // Create a range from col B (1) to col D (3), row 0 to row 2
    ID rangeId = generate_id();
    auto range = std::make_unique<Range>(rangeId, colIds[1], rowIds[0], colIds[3], rowIds[2]);
    range->flags = RangeFlags::STYLE;
    sheet_ptr->addRange(std::move(range));

    ASSERT_EQ(sheet_ptr->getRangeIds().size(), 1);
    EXPECT_EQ(sheet_ptr->getRange(rangeId)->startColId, colIds[1]);

    // Delete column B (start column)
    HLC hlc = workbook->getCurrentHLC();
    Operation op(hlc, OpType::COL_DELETE, colIds[1], "{}");
    ApplyResult result = applyOperation(*workbook, op);
    EXPECT_EQ(result, ApplyResult::SUCCESS);

    // Range should still exist but start at column C
    ASSERT_EQ(sheet_ptr->getRangeIds().size(), 1);
    Range* r = sheet_ptr->getRange(rangeId);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->startColId, colIds[2]);  // Now starts at C
    EXPECT_EQ(r->endColId, colIds[3]);    // Still ends at D
}

TEST_F(RangeAdjustmentTest, DeleteEndColumnShrinksRange) {
    // Create a range from col B (1) to col D (3)
    ID rangeId = generate_id();
    auto range = std::make_unique<Range>(rangeId, colIds[1], rowIds[0], colIds[3], rowIds[2]);
    range->flags = RangeFlags::STYLE;
    sheet_ptr->addRange(std::move(range));

    // Delete column D (end column)
    HLC hlc = workbook->getCurrentHLC();
    Operation op(hlc, OpType::COL_DELETE, colIds[3], "{}");
    applyOperation(*workbook, op);

    // Range should still exist but end at column C
    Range* r = sheet_ptr->getRange(rangeId);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->startColId, colIds[1]);  // Still starts at B
    EXPECT_EQ(r->endColId, colIds[2]);    // Now ends at C
}

TEST_F(RangeAdjustmentTest, DeleteSingleColumnRangeRemovesRange) {
    // Create a single-column range at col C (2)
    ID rangeId = generate_id();
    auto range = std::make_unique<Range>(rangeId, colIds[2], rowIds[0], colIds[2], rowIds[2]);
    range->flags = RangeFlags::STYLE;
    sheet_ptr->addRange(std::move(range));

    ASSERT_EQ(sheet_ptr->getRangeIds().size(), 1);

    // Delete column C (the only column in the range)
    HLC hlc = workbook->getCurrentHLC();
    Operation op(hlc, OpType::COL_DELETE, colIds[2], "{}");
    applyOperation(*workbook, op);

    // Range should be removed
    EXPECT_EQ(sheet_ptr->getRangeIds().size(), 0);
    EXPECT_EQ(sheet_ptr->getRange(rangeId), nullptr);
}

TEST_F(RangeAdjustmentTest, DeleteStartRowShrinksRange) {
    // Create a range from row 1 to row 3
    ID rangeId = generate_id();
    auto range = std::make_unique<Range>(rangeId, colIds[0], rowIds[1], colIds[2], rowIds[3]);
    range->flags = RangeFlags::STYLE;
    sheet_ptr->addRange(std::move(range));

    // Delete row 1 (start row)
    HLC hlc = workbook->getCurrentHLC();
    Operation op(hlc, OpType::ROW_DELETE, rowIds[1], "{}");
    applyOperation(*workbook, op);

    // Range should still exist but start at row 2
    Range* r = sheet_ptr->getRange(rangeId);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->startRowId, rowIds[2]);  // Now starts at row 2
    EXPECT_EQ(r->endRowId, rowIds[3]);    // Still ends at row 3
}

TEST_F(RangeAdjustmentTest, DeleteEndRowShrinksRange) {
    // Create a range from row 1 to row 3
    ID rangeId = generate_id();
    auto range = std::make_unique<Range>(rangeId, colIds[0], rowIds[1], colIds[2], rowIds[3]);
    range->flags = RangeFlags::STYLE;
    sheet_ptr->addRange(std::move(range));

    // Delete row 3 (end row)
    HLC hlc = workbook->getCurrentHLC();
    Operation op(hlc, OpType::ROW_DELETE, rowIds[3], "{}");
    applyOperation(*workbook, op);

    // Range should still exist but end at row 2
    Range* r = sheet_ptr->getRange(rangeId);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->startRowId, rowIds[1]);  // Still starts at row 1
    EXPECT_EQ(r->endRowId, rowIds[2]);    // Now ends at row 2
}

TEST_F(RangeAdjustmentTest, DeleteSingleRowRangeRemovesRange) {
    // Create a single-row range at row 2
    ID rangeId = generate_id();
    auto range = std::make_unique<Range>(rangeId, colIds[0], rowIds[2], colIds[2], rowIds[2]);
    range->flags = RangeFlags::STYLE;
    sheet_ptr->addRange(std::move(range));

    ASSERT_EQ(sheet_ptr->getRangeIds().size(), 1);

    // Delete row 2 (the only row in the range)
    HLC hlc = workbook->getCurrentHLC();
    Operation op(hlc, OpType::ROW_DELETE, rowIds[2], "{}");
    applyOperation(*workbook, op);

    // Range should be removed
    EXPECT_EQ(sheet_ptr->getRangeIds().size(), 0);
}

TEST_F(RangeAdjustmentTest, DeleteMiddleColumnKeepsRangeUnchanged) {
    // Create a range from col A (0) to col E (4)
    ID rangeId = generate_id();
    auto range = std::make_unique<Range>(rangeId, colIds[0], rowIds[0], colIds[4], rowIds[2]);
    range->flags = RangeFlags::STYLE;
    sheet_ptr->addRange(std::move(range));

    ID originalStartCol = colIds[0];
    ID originalEndCol = colIds[4];

    // Delete column C (middle column, not an edge)
    HLC hlc = workbook->getCurrentHLC();
    Operation op(hlc, OpType::COL_DELETE, colIds[2], "{}");
    applyOperation(*workbook, op);

    // Range should be unchanged (still spans A to E by UUID)
    Range* r = sheet_ptr->getRange(rangeId);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->startColId, originalStartCol);
    EXPECT_EQ(r->endColId, originalEndCol);
}

TEST_F(CRDTTest, FailedOperationNotAddedToOpLog) {
    // Test that failed operations don't add to oplog
    workbook->startCollaboration();  // Enable oplog

    size_t opCountBefore = workbook->getOpLog()->size();

    // Create an operation targeting a non-existent cell without col/row
    ID fakeCell = generate_id();
    Operation op = makeCellSetOp(*workbook, fakeCell, R"({"t":"n","v":"42"})");
    ApplyResult result = applyOperation(*workbook, op);

    // Without col/row, this is INVALID_PAYLOAD (can't create cell without position)
    EXPECT_EQ(result, ApplyResult::INVALID_PAYLOAD);

    // Oplog should NOT have grown (failed operation not added)
    EXPECT_EQ(workbook->getOpLog()->size(), opCountBefore);
}

// =============================================================================
// Styled Range Tests (content-addressed styles)
// =============================================================================

TEST_F(CRDTTest, StyledRangeWithContentAddressedStyles) {
    // This test verifies that styled ranges work with content-addressed styles.

    // Get the sheet and some column/row IDs
    Sheet* sheet = workbook->getSheetByIndex(0);
    ASSERT_NE(sheet, nullptr);

    const auto& colIdSet = sheet->getColumnIds();
    const auto& rowIdSet = sheet->getRowIds();
    std::vector<ID> colIds(colIdSet.begin(), colIdSet.end());
    std::vector<ID> rowIds(rowIdSet.begin(), rowIdSet.end());
    ASSERT_GE(colIds.size(), 2);
    ASSERT_GE(rowIds.size(), 2);

    // Create first styled range with red background
    StyleBuffer styleBuf1;
    styleBuf1.setBgColorHex("#ff0000");

    ID rangeId1 = generate_id();
    std::string rangePayload1 = "{\"startCol\":\"" + colIds[0].toString() + "\",";
    rangePayload1 += "\"startRow\":\"" + rowIds[0].toString() + "\",";
    rangePayload1 += "\"endCol\":\"" + colIds[0].toString() + "\",";
    rangePayload1 += "\"endRow\":\"" + rowIds[0].toString() + "\",";
    rangePayload1 += "\"flags\":2}";  // STYLE flag
    Operation rangeOp1 = makeRangeSetOp(*workbook, rangeId1, rangePayload1);
    ApplyResult rangeResult1 = applyOperation(*workbook, rangeOp1);
    EXPECT_EQ(rangeResult1, ApplyResult::SUCCESS);

    Operation setStyleOp1 = makeRangeSetStyleOp(*workbook, rangeId1, styleBuf1);
    ApplyResult setStyleResult1 = applyOperation(*workbook, setStyleOp1);
    EXPECT_EQ(setStyleResult1, ApplyResult::SUCCESS);

    // Verify range style
    const Range* range1 = workbook->getRange(rangeId1);
    ASSERT_NE(range1, nullptr);
    ASSERT_TRUE(range1->style.has_value());
    EXPECT_EQ(range1->style->getBgColorHex(), "#FF0000");

    // Create second styled range with green background
    StyleBuffer styleBuf2;
    styleBuf2.setBgColorHex("#00ff00");

    ID rangeId2 = generate_id();
    std::string rangePayload2 = "{\"startCol\":\"" + colIds[1].toString() + "\",";
    rangePayload2 += "\"startRow\":\"" + rowIds[1].toString() + "\",";
    rangePayload2 += "\"endCol\":\"" + colIds[1].toString() + "\",";
    rangePayload2 += "\"endRow\":\"" + rowIds[1].toString() + "\",";
    rangePayload2 += "\"flags\":2}";  // STYLE flag
    Operation rangeOp2 = makeRangeSetOp(*workbook, rangeId2, rangePayload2);
    ApplyResult rangeResult2 = applyOperation(*workbook, rangeOp2);
    EXPECT_EQ(rangeResult2, ApplyResult::SUCCESS);

    Operation setStyleOp2 = makeRangeSetStyleOp(*workbook, rangeId2, styleBuf2);
    ApplyResult setStyleResult2 = applyOperation(*workbook, setStyleOp2);
    EXPECT_EQ(setStyleResult2, ApplyResult::SUCCESS);

    // Verify range styles
    const Range* range2 = workbook->getRange(rangeId2);
    ASSERT_NE(range2, nullptr);
    ASSERT_TRUE(range2->style.has_value());
    EXPECT_EQ(range2->style->getBgColorHex(), "#00FF00");

    // Verify ranges are in the spatial index
    std::vector<Range*> rangesAtPos0 =
        sheet->getRangesAt(sheet->getColumn(colIds[0])->position,
                           sheet->getRow(rowIds[0])->position, RangeFlags::STYLE);
    EXPECT_GE(rangesAtPos0.size(), 1);

    std::vector<Range*> rangesAtPos1 =
        sheet->getRangesAt(sheet->getColumn(colIds[1])->position,
                           sheet->getRow(rowIds[1])->position, RangeFlags::STYLE);
    EXPECT_GE(rangesAtPos1.size(), 1);
}

// =============================================================================
// SIZE_SET Flag Tests
// =============================================================================
// Tests for the SIZE_SET flag which distinguishes between:
// - Unset size: "use your local default" (sizeSet=false)
// - Explicitly set size: "this exact size must be used" (sizeSet=true)

class SizeSetFlagTest : public ::testing::Test {
protected:
    void SetUp() override {
        workbook = std::make_unique<Workbook>(generate_id(), "TestWorkbook");
        workbook->setNodeId(generate_id());

        auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet1");
        sheetId = sheet->id;
        workbook->addSheet(std::move(sheet));
    }

    std::unique_ptr<Workbook> workbook;
    ID sheetId;
};

TEST_F(SizeSetFlagTest, NewlyCreatedAxisWithoutSizeHasSizeSetFalse) {
    // Create a column without size in payload
    ID colId = generate_id();
    std::string payload = "{\"pos\":0}";
    Operation op = makeColSetOp(*workbook, colId, sheetId, payload);
    ApplyResult result = applyOperation(*workbook, op);
    EXPECT_EQ(result, ApplyResult::SUCCESS);

    Sheet* sheet = workbook->getSheetByIndex(0);
    Axis* col = sheet->getColumn(colId);
    ASSERT_NE(col, nullptr);
    EXPECT_FALSE(col->sizeSet()) << "Axis created without size should have sizeSet=false";
    EXPECT_EQ(col->size, DEFAULT_COLUMN_WIDTH) << "Axis should use default size";
}

TEST_F(SizeSetFlagTest, NewlyCreatedRowWithoutSizeHasSizeSetFalse) {
    // Create a row without size in payload
    ID rowId = generate_id();
    std::string payload = "{\"pos\":0}";
    Operation op = makeRowSetOp(*workbook, rowId, sheetId, payload);
    ApplyResult result = applyOperation(*workbook, op);
    EXPECT_EQ(result, ApplyResult::SUCCESS);

    Sheet* sheet = workbook->getSheetByIndex(0);
    Axis* row = sheet->getRow(rowId);
    ASSERT_NE(row, nullptr);
    EXPECT_FALSE(row->sizeSet()) << "Axis created without size should have sizeSet=false";
    EXPECT_EQ(row->size, DEFAULT_ROW_HEIGHT) << "Axis should use default size";
}

TEST_F(SizeSetFlagTest, ExplicitlyResizedAxisHasSizeSetTrue) {
    // Create a column first
    ID colId = generate_id();
    std::string createPayload = "{\"pos\":0}";
    Operation createOp = makeColSetOp(*workbook, colId, sheetId, createPayload);
    applyOperation(*workbook, createOp);

    Sheet* sheet = workbook->getSheetByIndex(0);
    Axis* col = sheet->getColumn(colId);
    ASSERT_NE(col, nullptr);
    EXPECT_FALSE(col->sizeSet()) << "Initial creation should have sizeSet=false";

    // Now resize explicitly
    std::string resizePayload = "{\"size\":200}";
    Operation resizeOp = makeColSetOp(*workbook, colId, resizePayload);
    ApplyResult result = applyOperation(*workbook, resizeOp);
    EXPECT_EQ(result, ApplyResult::SUCCESS);

    EXPECT_TRUE(col->sizeSet()) << "After explicit resize, sizeSet should be true";
    EXPECT_EQ(col->size, 200) << "Size should be the explicitly set value";
}

TEST_F(SizeSetFlagTest, CreatedWithExplicitSizeHasSizeSetTrue) {
    // Create a column WITH size in payload
    ID colId = generate_id();
    std::string payload = "{\"pos\":0,\"size\":150}";
    Operation op = makeColSetOp(*workbook, colId, sheetId, payload);
    ApplyResult result = applyOperation(*workbook, op);
    EXPECT_EQ(result, ApplyResult::SUCCESS);

    Sheet* sheet = workbook->getSheetByIndex(0);
    Axis* col = sheet->getColumn(colId);
    ASSERT_NE(col, nullptr);
    EXPECT_TRUE(col->sizeSet()) << "Axis created with explicit size should have sizeSet=true";
    EXPECT_EQ(col->size, 150) << "Size should be the explicitly set value";
}

TEST_F(SizeSetFlagTest, BootstrapOpLogOmitsSizeWhenNotSet) {
    // Create a column without explicit size
    ID colId = generate_id();
    std::string payload = "{\"pos\":0}";
    Operation op = makeColSetOp(*workbook, colId, sheetId, payload);
    applyOperation(*workbook, op);

    // Bootstrap the OpLog
    bootstrapOpLog(*workbook);

    // Find the COL_SET operation for our column
    const OpLog* oplog = workbook->getOpLog();
    bool found = false;
    for (const auto& oper : oplog->getAllOperations()) {
        if (oper.type == OpType::COL_SET && oper.target_id == colId) {
            found = true;
            // The payload should NOT contain "size" since sizeSet is false
            EXPECT_EQ(oper.payload.find("\"size\""), std::string::npos)
                << "Payload should not contain size when sizeSet=false: " << oper.payload;
            break;
        }
    }
    EXPECT_TRUE(found) << "COL_SET operation should be in OpLog";
}

TEST_F(SizeSetFlagTest, BootstrapOpLogIncludesSizeWhenSet) {
    // Create a column WITH explicit size
    ID colId = generate_id();
    std::string payload = "{\"pos\":0,\"size\":150}";
    Operation op = makeColSetOp(*workbook, colId, sheetId, payload);
    applyOperation(*workbook, op);

    // Bootstrap the OpLog
    bootstrapOpLog(*workbook);

    // Find the COL_SET operation for our column
    const OpLog* oplog = workbook->getOpLog();
    bool found = false;
    for (const auto& oper : oplog->getAllOperations()) {
        if (oper.type == OpType::COL_SET && oper.target_id == colId) {
            found = true;
            // The payload SHOULD contain "size" since sizeSet is true
            EXPECT_NE(oper.payload.find("\"size\":150"), std::string::npos)
                << "Payload should contain size:150 when sizeSet=true: " << oper.payload;
            break;
        }
    }
    EXPECT_TRUE(found) << "COL_SET operation should be in OpLog";
}

TEST_F(SizeSetFlagTest, RowBootstrapOpLogBehavior) {
    // Create a row without explicit size
    ID rowId1 = generate_id();
    std::string payload1 = "{\"pos\":0}";
    Operation op1 = makeRowSetOp(*workbook, rowId1, sheetId, payload1);
    applyOperation(*workbook, op1);

    // Create a row WITH explicit size
    ID rowId2 = generate_id();
    std::string payload2 = "{\"pos\":1,\"size\":50}";
    Operation op2 = makeRowSetOp(*workbook, rowId2, sheetId, payload2);
    applyOperation(*workbook, op2);

    // Bootstrap the OpLog
    bootstrapOpLog(*workbook);

    const OpLog* oplog = workbook->getOpLog();
    bool foundRow1 = false;
    bool foundRow2 = false;

    for (const auto& oper : oplog->getAllOperations()) {
        if (oper.type == OpType::ROW_SET) {
            if (oper.target_id == rowId1) {
                foundRow1 = true;
                EXPECT_EQ(oper.payload.find("\"size\""), std::string::npos)
                    << "Row1 payload should NOT contain size: " << oper.payload;
            } else if (oper.target_id == rowId2) {
                foundRow2 = true;
                EXPECT_NE(oper.payload.find("\"size\":50"), std::string::npos)
                    << "Row2 payload should contain size:50: " << oper.payload;
            }
        }
    }
    EXPECT_TRUE(foundRow1) << "ROW_SET operation for row1 should be in OpLog";
    EXPECT_TRUE(foundRow2) << "ROW_SET operation for row2 should be in OpLog";
}

// =============================================================================
// Full-State SET Operation Tests for Resurrection Correctness
// =============================================================================
// Tests that SET operations include all entity properties (full-state) so that
// operations are self-sufficient for resurrection. This is critical for CRDT
// correctness when operations arrive out of order:
//
// Example scenario:
// 1. Peer A: DELETE cell at t=1000
// 2. Peer B: SET cell (value + style) at t=2000
// 3. On Peer C, DELETE arrives first, then SET
//
// With full-state SET, the cell is correctly resurrected with all properties.
// With sparse SET (only changed properties), style would be lost.

class FullStateOperationTest : public ::testing::Test {
protected:
    void SetUp() override {
        workbook = std::make_unique<Workbook>(generate_id(), "TestWorkbook");
        workbook->setNodeId(generate_id());

        auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet1");
        sheetId = sheet->id;
        sheet->setWorkbook(workbook.get());

        // Create column and row
        colId = generate_id();
        rowId = generate_id();
        auto col = std::make_unique<Axis>(colId, true);
        col->position = 0;
        col->size = 100;
        col->setSizeSet(true);  // Explicitly set size
        auto row = std::make_unique<Axis>(rowId, false);
        row->position = 0;
        row->size = 25;
        row->setSizeSet(true);  // Explicitly set size
        sheet->addColumn(std::move(col));
        sheet->addRow(std::move(row));

        // Create a cell with value
        cellId = generate_id();
        auto cell = std::make_unique<Cell>(cellId, colId, rowId);
        cell->value = CellValue(42.0);
        sheet->addCell(std::move(cell));

        workbook->addSheet(std::move(sheet));
    }

    std::unique_ptr<Workbook> workbook;
    ID sheetId;
    ID colId, rowId;
    ID cellId;
};

TEST_F(FullStateOperationTest, CellSetStyleOpIncludesValue) {
    // First, verify the cell has a value
    Cell* cell = workbook->getCell(cellId);
    ASSERT_NE(cell, nullptr);
    EXPECT_EQ(cell->value.asNumber(), 42.0);

    // Create a style operation
    StyleBuffer style;
    style.setBold(true);
    style.setBgColorHex("#ff0000");
    Operation op = makeCellSetStyleOp(*workbook, cellId, style);

    // Verify payload contains value information for resurrection
    EXPECT_NE(op.payload.find("\"t\":\"n\""), std::string::npos)
        << "Style op payload should include value type: " << op.payload;
    EXPECT_NE(op.payload.find("\"v\":\"42"), std::string::npos)
        << "Style op payload should include value: " << op.payload;
    EXPECT_NE(op.payload.find("\"col\":\"" + colId.toString() + "\""), std::string::npos)
        << "Style op payload should include colId: " << op.payload;
    EXPECT_NE(op.payload.find("\"row\":\"" + rowId.toString() + "\""), std::string::npos)
        << "Style op payload should include rowId: " << op.payload;
}

TEST_F(FullStateOperationTest, CellSetFormatOpIncludesValueAndStyle) {
    // First, set a style on the cell
    StyleBuffer style;
    style.setBold(true);
    Operation styleOp = makeCellSetStyleOp(*workbook, cellId, style);
    applyOperation(*workbook, styleOp);

    // Verify style was set
    Cell* cell = workbook->getCell(cellId);
    ASSERT_NE(cell, nullptr);
    EXPECT_TRUE(cell->hasStyle());

    // Now create a format operation
    FormatBuffer format;
    format.setCategory(NumberFormatCategory::CURRENCY);
    Operation formatOp = makeCellSetFormatOp(*workbook, cellId, format);

    // Verify payload contains both value AND style for resurrection
    EXPECT_NE(formatOp.payload.find("\"t\":\"n\""), std::string::npos)
        << "Format op payload should include value type: " << formatOp.payload;
    EXPECT_NE(formatOp.payload.find("\"sty\":\""), std::string::npos)
        << "Format op payload should include existing style: " << formatOp.payload;
    EXPECT_NE(formatOp.payload.find("\"fmt\":\""), std::string::npos)
        << "Format op payload should include new format: " << formatOp.payload;
}

TEST_F(FullStateOperationTest, CellClearStyleOpPreservesValue) {
    // First, set a style and format on the cell
    StyleBuffer style;
    style.setBold(true);
    Operation styleOp = makeCellSetStyleOp(*workbook, cellId, style);
    applyOperation(*workbook, styleOp);

    FormatBuffer format;
    format.setCategory(NumberFormatCategory::PERCENTAGE);
    Operation formatOp = makeCellSetFormatOp(*workbook, cellId, format);
    applyOperation(*workbook, formatOp);

    // Create a clear style operation
    Operation clearOp = makeCellClearStyleOp(*workbook, cellId);

    // Verify payload contains value and format (preserving them)
    EXPECT_NE(clearOp.payload.find("\"t\":\"n\""), std::string::npos)
        << "Clear style op should include value: " << clearOp.payload;
    EXPECT_NE(clearOp.payload.find("\"fmt\":\""), std::string::npos)
        << "Clear style op should include format: " << clearOp.payload;
    EXPECT_NE(clearOp.payload.find("\"sty\":\"\""), std::string::npos)
        << "Clear style op should have empty style: " << clearOp.payload;
}

TEST_F(FullStateOperationTest, AxisSetStyleOpIncludesAllProperties) {
    // First, set hidden and size on the column
    Sheet* sheet = workbook->getSheetByIndex(0);
    Axis* col = sheet->getColumn(colId);
    ASSERT_NE(col, nullptr);
    col->setHidden(true);
    col->name = "MyColumn";

    // Also set a format on the column
    FormatBuffer format;
    format.setCategory(NumberFormatCategory::NUMBER);
    Operation formatOp = makeAxisSetFormatOp(*workbook, colId, format);
    applyOperation(*workbook, formatOp);

    // Now create a style operation
    StyleBuffer style;
    style.setBold(true);
    Operation styleOp = makeAxisSetStyleOp(*workbook, colId, style);

    // Verify payload includes all axis properties
    EXPECT_NE(styleOp.payload.find("\"pos\":"), std::string::npos)
        << "Axis style op should include position: " << styleOp.payload;
    EXPECT_NE(styleOp.payload.find("\"size\":"), std::string::npos)
        << "Axis style op should include size (if sizeSet): " << styleOp.payload;
    EXPECT_NE(styleOp.payload.find("\"hidden\":true"), std::string::npos)
        << "Axis style op should include hidden state: " << styleOp.payload;
    EXPECT_NE(styleOp.payload.find("\"name\":\"MyColumn\""), std::string::npos)
        << "Axis style op should include name: " << styleOp.payload;
    EXPECT_NE(styleOp.payload.find("\"fmt\":\""), std::string::npos)
        << "Axis style op should include existing format: " << styleOp.payload;
}

TEST_F(FullStateOperationTest, AxisSetHiddenOpIncludesAllProperties) {
    // Set up axis with style and format
    StyleBuffer style;
    style.setBgColorHex("#00ff00");
    Operation styleOp = makeAxisSetStyleOp(*workbook, colId, style);
    applyOperation(*workbook, styleOp);

    // Now create a set hidden operation
    Operation hiddenOp = makeAxisSetHiddenOp(*workbook, colId, true);

    // Verify payload includes all axis properties
    EXPECT_NE(hiddenOp.payload.find("\"pos\":"), std::string::npos)
        << "Axis hidden op should include position: " << hiddenOp.payload;
    EXPECT_NE(hiddenOp.payload.find("\"sty\":\""), std::string::npos)
        << "Axis hidden op should include existing style: " << hiddenOp.payload;
    EXPECT_NE(hiddenOp.payload.find("\"hidden\":true"), std::string::npos)
        << "Axis hidden op should include hidden=true: " << hiddenOp.payload;
}

TEST_F(FullStateOperationTest, RangeSetStyleOpIncludesAllProperties) {
    // Create a range first
    ID rangeId = generate_id();
    std::string createPayload = "{\"startCol\":\"" + colId.toString() + "\",";
    createPayload += "\"startRow\":\"" + rowId.toString() + "\",";
    createPayload += "\"endCol\":\"" + colId.toString() + "\",";
    createPayload += "\"endRow\":\"" + rowId.toString() + "\",";
    createPayload += "\"flags\":3}";  // MERGE | STYLE flags
    Operation createOp = makeRangeSetOp(*workbook, rangeId, createPayload);
    applyOperation(*workbook, createOp);

    // Set a format on the range
    FormatBuffer format;
    format.setCategory(NumberFormatCategory::DATE);
    Operation formatOp = makeRangeSetFormatOp(*workbook, rangeId, format);
    applyOperation(*workbook, formatOp);

    // Now create a style operation
    StyleBuffer style;
    style.setBold(true);
    Operation styleOp = makeRangeSetStyleOp(*workbook, rangeId, style);

    // Verify payload includes all range properties for resurrection
    EXPECT_NE(styleOp.payload.find("\"startCol\":\"" + colId.toString() + "\""), std::string::npos)
        << "Range style op should include startCol: " << styleOp.payload;
    EXPECT_NE(styleOp.payload.find("\"endCol\":\"" + colId.toString() + "\""), std::string::npos)
        << "Range style op should include endCol: " << styleOp.payload;
    EXPECT_NE(styleOp.payload.find("\"flags\":"), std::string::npos)
        << "Range style op should include flags: " << styleOp.payload;
    EXPECT_NE(styleOp.payload.find("\"fmt\":\""), std::string::npos)
        << "Range style op should include existing format: " << styleOp.payload;
    EXPECT_NE(styleOp.payload.find("\"sty\":\""), std::string::npos)
        << "Range style op should include new style: " << styleOp.payload;
}

TEST_F(FullStateOperationTest, RangeClearStyleOpPreservesFormat) {
    // Create a range with both style and format
    ID rangeId = generate_id();
    std::string createPayload = "{\"startCol\":\"" + colId.toString() + "\",";
    createPayload += "\"startRow\":\"" + rowId.toString() + "\",";
    createPayload += "\"endCol\":\"" + colId.toString() + "\",";
    createPayload += "\"endRow\":\"" + rowId.toString() + "\",";
    createPayload += "\"flags\":2}";  // STYLE flag
    Operation createOp = makeRangeSetOp(*workbook, rangeId, createPayload);
    applyOperation(*workbook, createOp);

    StyleBuffer style;
    style.setBold(true);
    Operation styleOp = makeRangeSetStyleOp(*workbook, rangeId, style);
    applyOperation(*workbook, styleOp);

    FormatBuffer format;
    format.setCategory(NumberFormatCategory::TIME);
    Operation formatOp = makeRangeSetFormatOp(*workbook, rangeId, format);
    applyOperation(*workbook, formatOp);

    // Now create a clear style operation
    Operation clearOp = makeRangeClearStyleOp(*workbook, rangeId);

    // Verify payload preserves format
    EXPECT_NE(clearOp.payload.find("\"fmt\":\""), std::string::npos)
        << "Range clear style op should preserve format: " << clearOp.payload;
    EXPECT_NE(clearOp.payload.find("\"sty\":\"\""), std::string::npos)
        << "Range clear style op should have empty style: " << clearOp.payload;
    EXPECT_NE(clearOp.payload.find("\"flags\":"), std::string::npos)
        << "Range clear style op should include flags: " << clearOp.payload;
}

// Test resurrection scenario: The full-state payload contains all info needed
// to resurrect a cell even when operations arrive out of order.
TEST_F(FullStateOperationTest, CellResurrectionWithFullState) {
    // First set up the cell with style and format to capture a full-state payload
    StyleBuffer style;
    style.setBold(true);
    style.setBgColorHex("#ff0000");
    Operation styleOp = makeCellSetStyleOp(*workbook, cellId, style);
    applyOperation(*workbook, styleOp);

    FormatBuffer format;
    format.setCategory(NumberFormatCategory::CURRENCY);
    Operation formatOp = makeCellSetFormatOp(*workbook, cellId, format);
    applyOperation(*workbook, formatOp);

    // Capture the full-state payload - this is what a peer would send
    std::string fullStatePayload = formatOp.payload;

    // Now create a fresh workbook to simulate a peer receiving operations out of order
    auto peer = std::make_unique<Workbook>(generate_id(), "PeerWorkbook");
    peer->setNodeId(generate_id());

    auto sheet = std::make_unique<Sheet>(sheetId, "Sheet1");
    sheet->setWorkbook(peer.get());

    // Create matching column and row in the peer
    auto col = std::make_unique<Axis>(colId, true);
    col->position = 0;
    col->size = 100;
    col->setSizeSet(true);
    auto row = std::make_unique<Axis>(rowId, false);
    row->position = 0;
    row->size = 25;
    row->setSizeSet(true);
    sheet->addColumn(std::move(col));
    sheet->addRow(std::move(row));

    peer->addSheet(std::move(sheet));

    // Scenario: Peer receives DELETE first (t=1000), then SET (t=2000)
    // With LWW semantics, SET wins and should resurrect the cell with full state
    ID node1("Node1111");
    HLC deleteHlc(1000, 0, node1);
    HLC setHlc(2000, 0, node1);

    Operation deleteOp(deleteHlc, OpType::CELL_DELETE, cellId, "{}");
    Operation setOp(setHlc, OpType::CELL_SET, cellId, fullStatePayload);

    // On the peer, DELETE arrives first
    // Since cell doesn't exist, delete is a no-op but adds to oplog
    ApplyResult deleteResult = applyOperation(*peer, deleteOp);
    // Cell was never created, so nothing to delete - this returns SUCCESS (idempotent)
    EXPECT_TRUE(deleteResult == ApplyResult::SUCCESS ||
                deleteResult == ApplyResult::INVALID_TARGET);
    EXPECT_EQ(peer->getCell(cellId), nullptr);

    // Now SET arrives - this creates/resurrects the cell with full state
    ApplyResult setResult = applyOperation(*peer, setOp);
    EXPECT_TRUE(setResult == ApplyResult::SUCCESS || setResult == ApplyResult::RESURRECTED);

    // Verify cell was created with all properties from the full-state payload
    Cell* cell = peer->getCell(cellId);
    ASSERT_NE(cell, nullptr) << "Cell should be created from full-state SET";
    EXPECT_EQ(cell->value.asNumber(), 42.0) << "Value should be restored";
    EXPECT_TRUE(cell->hasStyle()) << "Style should be present";
    EXPECT_TRUE(cell->hasFormat()) << "Format should be present";

    const StyleBuffer* restoredStyle = peer->getEntityStyle(cellId);
    ASSERT_NE(restoredStyle, nullptr);
    EXPECT_TRUE(restoredStyle->getBold());

    const FormatBuffer* restoredFormat = peer->getEntityFormat(cellId);
    ASSERT_NE(restoredFormat, nullptr);
    EXPECT_EQ(restoredFormat->getCategory(), NumberFormatCategory::CURRENCY);
}

// Test that a sparse SET (without full state) would lose properties
// This documents the problem we're solving with full-state operations
TEST_F(FullStateOperationTest, SparseSetLosesPropertiesOnResurrection) {
    // Capture a SPARSE payload (only the changed property) - this is what we DON'T want
    std::string sparsePayload = "{\"t\":\"n\",\"v\":\"42\",\"col\":\"" + colId.toString() +
                                "\",\"row\":\"" + rowId.toString() + "\"}";

    // Create a fresh peer workbook
    auto peer = std::make_unique<Workbook>(generate_id(), "PeerWorkbook");
    peer->setNodeId(generate_id());

    auto sheet = std::make_unique<Sheet>(sheetId, "Sheet1");
    sheet->setWorkbook(peer.get());

    auto col = std::make_unique<Axis>(colId, true);
    col->position = 0;
    auto row = std::make_unique<Axis>(rowId, false);
    row->position = 0;
    sheet->addColumn(std::move(col));
    sheet->addRow(std::move(row));

    peer->addSheet(std::move(sheet));

    // Apply the sparse SET operation
    ID node1("Node1111");
    HLC hlc(1000, 0, node1);
    Operation setOp(hlc, OpType::CELL_SET, cellId, sparsePayload);

    applyOperation(*peer, setOp);

    // Cell is created but WITHOUT style and format
    Cell* cell = peer->getCell(cellId);
    ASSERT_NE(cell, nullptr);
    EXPECT_EQ(cell->value.asNumber(), 42.0);
    EXPECT_FALSE(cell->hasStyle()) << "Sparse SET does NOT include style";
    EXPECT_FALSE(cell->hasFormat()) << "Sparse SET does NOT include format";
}

// Browser createEmptyWorkbook adds a sheet outside the oplog; live COL/ROW/CELL
// ops carry sheetId. Peers must materialize the sheet so those ops apply.
TEST(CrdtSheetMaterialize, ColRowCellApplyWithoutSheetSet) {
    auto peer = std::make_unique<Workbook>(generate_id(), "Peer");
    peer->startCollaboration();
    peer->setNodeId(generate_id());
    // Empty peer — no sheets

    const ID sheetId = generate_id();
    const ID colId = generate_id();
    const ID rowId = generate_id();
    const ID cellId = generate_id();

    // Simulate peer_ops=3 without SHEET_SET (historical browser bug)
    Operation colOp = makeColSetOp(*peer, colId, sheetId, R"({"pos":1})");
    Operation rowOp = makeRowSetOp(*peer, rowId, sheetId, R"({"pos":0})");
    Operation cellOp = makeCellSetOp(*peer, cellId, sheetId,
                                     "{\"t\":\"n\",\"v\":\"123\",\"col\":\"" + colId.toString() +
                                         "\",\"row\":\"" + rowId.toString() + "\"}");

    // mint HLCs on a temp workbook so ops have valid HLCs
    auto src = std::make_unique<Workbook>(generate_id(), "Src");
    src->startCollaboration();
    src->setNodeId(generate_id());
    colOp = makeColSetOp(*src, colId, sheetId, R"({"pos":1})");
    rowOp = makeRowSetOp(*src, rowId, sheetId, R"({"pos":0})");
    cellOp = makeCellSetOp(*src, cellId, sheetId,
                           "{\"t\":\"n\",\"v\":\"123\",\"col\":\"" + colId.toString() +
                               "\",\"row\":\"" + rowId.toString() + "\"}");

    std::vector<Operation> ops = {colOp, rowOp, cellOp};
    const size_t applied = applyOperations(*peer, ops);
    EXPECT_EQ(applied, 3u) << "COL/ROW/CELL must apply by materializing missing sheet";
    ASSERT_EQ(peer->sheets.size(), 1u);
    EXPECT_EQ(peer->sheets[0]->id, sheetId);
    Sheet* sh = peer->getSheet(sheetId);
    ASSERT_NE(sh, nullptr);
    Cell* c = sh->getCell(cellId);
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->value.raw, "123");
}

TEST(CrdtSheetMaterialize, BootstrapEmitsSheetSet) {
    auto wb = std::make_unique<Workbook>(generate_id(), "W");
    wb->startCollaboration();
    wb->setNodeId(generate_id());
    auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet1");
    const ID sid = sheet->id;
    wb->addSheet(std::move(sheet));

    const size_t n = bootstrapOpLog(*wb);
    EXPECT_GE(n, 1u);
    bool found_sheet = false;
    for (const auto& op : wb->getOpLog()->getAllOperations()) {
        if (op.type == OpType::SHEET_SET && op.target_id == sid) {
            found_sheet = true;
        }
    }
    EXPECT_TRUE(found_sheet) << "bootstrapOpLog must emit SHEET_SET for empty sheets";
}

// ---------------------------------------------------------------------------
// Late-join dual Sheet1: empty local shell must not compete with host document
// ---------------------------------------------------------------------------

TEST(CrdtJoinEmptyShell, DiscardEmptyPlaceholderSheets) {
    auto wb = std::make_unique<Workbook>(generate_id(), "Untitled");
    auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet1");
    wb->addSheet(std::move(sheet));
    EXPECT_TRUE(isWorkbookContentEmpty(*wb));
    EXPECT_EQ(wb->sheetCount(), 1u);

    const size_t removed = discardEmptyPlaceholderSheets(*wb);
    EXPECT_EQ(removed, 1u);
    EXPECT_EQ(wb->sheetCount(), 0u);
    EXPECT_TRUE(isWorkbookContentEmpty(*wb));
}

TEST(CrdtJoinEmptyShell, DiscardLeavesSheetsWithContent) {
    auto wb = std::make_unique<Workbook>(generate_id(), "Untitled");
    wb->setNodeId(generate_id());
    wb->startCollaboration();

    const ID sheetId = generate_id();
    auto sheet = std::make_unique<Sheet>(sheetId, "Sheet1");
    sheet->setWorkbook(wb.get());
    wb->addSheet(std::move(sheet));
    Sheet* s = wb->getSheet(sheetId);
    ASSERT_NE(s, nullptr);

    // Empty sibling sheet (placeholder) + content sheet
    wb->addSheet(std::make_unique<Sheet>(generate_id(), "Sheet1"));

    const ID colId = generate_id();
    applyOperation(*wb, makeColSetOp(*wb, colId, sheetId, R"({"pos":0})"));
    EXPECT_FALSE(isWorkbookContentEmpty(*wb));

    const size_t removed = discardEmptyPlaceholderSheets(*wb);
    EXPECT_EQ(removed, 1u);
    EXPECT_EQ(wb->sheetCount(), 1u);
    EXPECT_EQ(wb->sheets[0]->id, sheetId);
}

TEST(CrdtJoinEmptyShell, PreferredActiveSheetIsContentSheet) {
    auto wb = std::make_unique<Workbook>(generate_id(), "Untitled");
    wb->setNodeId(generate_id());
    wb->startCollaboration();

    // Index 0: empty placeholder (late joiner shell)
    wb->addSheet(std::make_unique<Sheet>(generate_id(), "Sheet1"));

    // Index 1: host document with a cell
    const ID sheetId = generate_id();
    auto content = std::make_unique<Sheet>(sheetId, "Sheet1");
    content->setWorkbook(wb.get());
    wb->addSheet(std::move(content));
    Sheet* s = wb->getSheet(sheetId);
    ASSERT_NE(s, nullptr);

    const ID colId = generate_id();
    const ID rowId = generate_id();
    const ID cellId = generate_id();
    applyOperation(*wb, makeColSetOp(*wb, colId, sheetId, R"({"pos":0})"));
    applyOperation(*wb, makeRowSetOp(*wb, rowId, sheetId, R"({"pos":0})"));
    applyOperation(*wb, makeCellSetOp(*wb, cellId, sheetId,
                                      "{\"t\":\"s\",\"v\":\"foo\",\"col\":\"" + colId.toString() +
                                          "\",\"row\":\"" + rowId.toString() + "\"}"));

    EXPECT_EQ(preferredActiveSheetIndex(*wb), 1u);
    EXPECT_EQ(wb->getSheetByIndex(preferredActiveSheetIndex(*wb))->getCell(cellId)->value.raw,
              "foo");
}

// Remote fill/edit must not yank the user off Sheet2 to Sheet1 just because
// preferredActiveSheetIndex always returns the first non-empty sheet.
TEST(CrdtJoinEmptyShell, RemoteChangeKeepsActiveSheet) {
    auto wb = std::make_unique<Workbook>(generate_id(), "Untitled");
    wb->setNodeId(generate_id());
    wb->startCollaboration();

    // Sheet1 (index 0) with content — would be "preferred"
    const ID sheet1Id = generate_id();
    auto s1 = std::make_unique<Sheet>(sheet1Id, "Sheet1");
    s1->setWorkbook(wb.get());
    wb->addSheet(std::move(s1));
    applyOperation(*wb, makeColSetOp(*wb, generate_id(), sheet1Id, R"({"pos":0})"));
    applyOperation(*wb, makeRowSetOp(*wb, generate_id(), sheet1Id, R"({"pos":0})"));

    // Sheet2 (index 1) with content — user is viewing this when peer fills
    const ID sheet2Id = generate_id();
    auto s2 = std::make_unique<Sheet>(sheet2Id, "Sheet2");
    s2->setWorkbook(wb.get());
    wb->addSheet(std::move(s2));
    applyOperation(*wb, makeColSetOp(*wb, generate_id(), sheet2Id, R"({"pos":0})"));
    applyOperation(*wb, makeRowSetOp(*wb, generate_id(), sheet2Id, R"({"pos":0})"));

    // Empty Sheet3 — user may still be viewing it; do not auto-switch
    wb->addSheet(std::make_unique<Sheet>(generate_id(), "Sheet3"));

    EXPECT_EQ(preferredActiveSheetIndex(*wb), 0u);
    EXPECT_EQ(resolveActiveSheetAfterRemoteChange(*wb, 1u), 1u);  // stay on Sheet2
    EXPECT_EQ(resolveActiveSheetAfterRemoteChange(*wb, 0u), 0u);  // stay on Sheet1
    EXPECT_EQ(resolveActiveSheetAfterRemoteChange(*wb, 2u), 2u);  // stay on empty Sheet3
    // Sheet id wins over a stale/wrong index (e.g. mid-batch remote mutations).
    EXPECT_EQ(resolveActiveSheetAfterRemoteChange(*wb, 0u, sheet2Id), 1u);
    EXPECT_EQ(resolveActiveSheetAfterRemoteChange(*wb, 99u, sheet1Id), 0u);
}

// Only auto-switch when the active index is no longer valid (sheet deleted).
TEST(CrdtJoinEmptyShell, RemoteChangeSwitchesWhenActiveDeleted) {
    auto wb = std::make_unique<Workbook>(generate_id(), "Untitled");
    wb->setNodeId(generate_id());
    wb->startCollaboration();

    const ID sheet1Id = generate_id();
    const ID sheet2Id = generate_id();
    const ID sheet3Id = generate_id();
    wb->addSheet(std::make_unique<Sheet>(sheet1Id, "Sheet1"));
    wb->addSheet(std::make_unique<Sheet>(sheet2Id, "Sheet2"));
    wb->addSheet(std::make_unique<Sheet>(sheet3Id, "Sheet3"));

    EXPECT_EQ(wb->sheetCount(), 3u);
    // User on last sheet (index 2); peer deletes it → clamp to last remaining
    wb->removeSheet(wb->sheets[2]->id);
    EXPECT_EQ(wb->sheetCount(), 2u);
    EXPECT_EQ(resolveActiveSheetAfterRemoteChange(*wb, 2u), 1u);
    // Deleted sheet id: fall back to index clamp
    EXPECT_EQ(resolveActiveSheetAfterRemoteChange(*wb, 2u, sheet3Id), 1u);
    // Still-valid index is unchanged (peer deleted a different sheet)
    EXPECT_EQ(resolveActiveSheetAfterRemoteChange(*wb, 0u), 0u);
    EXPECT_EQ(resolveActiveSheetAfterRemoteChange(*wb, 1u), 1u);
    // Surviving sheet id still wins after a peer deletes another sheet
    EXPECT_EQ(resolveActiveSheetAfterRemoteChange(*wb, 0u, sheet2Id), 1u);
    // Far out-of-range still clamps to last
    EXPECT_EQ(resolveActiveSheetAfterRemoteChange(*wb, 99u), 1u);
}

// Peer refresh / remote sheet insert must not move the user off their sheet
// even if the active index becomes temporarily wrong relative to sheet order.
TEST(CrdtJoinEmptyShell, RemoteSheetInsertKeepsSheetById) {
    auto wb = std::make_unique<Workbook>(generate_id(), "Untitled");
    wb->setNodeId(generate_id());
    wb->startCollaboration();

    const ID sheet1Id = generate_id();
    const ID sheet2Id = generate_id();
    wb->addSheet(std::make_unique<Sheet>(sheet1Id, "Sheet1"));
    wb->addSheet(std::make_unique<Sheet>(sheet2Id, "Sheet2"));

    // User is on Sheet2 (index 1). Peer inserts a new sheet at the end.
    const ID sheet3Id = generate_id();
    applyOperation(*wb, makeSheetSetOp(*wb, sheet3Id, R"({"name":"Sheet3"})"));
    EXPECT_EQ(wb->sheetCount(), 3u);
    EXPECT_EQ(resolveActiveSheetAfterRemoteChange(*wb, 1u, sheet2Id), 1u);

    // Peer deletes Sheet1 → Sheet2 moves to index 0; id keeps the user on Sheet2.
    wb->removeSheet(sheet1Id);
    EXPECT_EQ(wb->sheetCount(), 2u);
    EXPECT_EQ(resolveActiveSheetAfterRemoteChange(*wb, 1u, sheet2Id), 0u);
    EXPECT_EQ(wb->getSheetByIndex(0)->id, sheet2Id);
}

TEST(CrdtJoinEmptyShell, LateJoinDoesNotPublishSecondSheet1) {
    // Host has content Sheet1 + document identity (Model B)
    auto host = std::make_unique<Workbook>(generate_id(), "HostDoc");
    host->setNodeId(generate_id());
    host->startCollaboration();
    const ID hostDocId = host->id;
    const ID hostSheet = generate_id();
    host->addSheet(std::make_unique<Sheet>(hostSheet, "Sheet1"));
    host->getSheet(hostSheet)->setWorkbook(host.get());
    const ID colId = generate_id();
    const ID rowId = generate_id();
    const ID cellId = generate_id();
    applyOperation(*host, makeWorkbookSetOp(*host, R"({"name":"HostDoc"})"));
    applyOperation(*host, makeSheetSetOp(*host, hostSheet, R"({"name":"Sheet1"})"));
    applyOperation(*host, makeColSetOp(*host, colId, hostSheet, R"({"pos":0})"));
    applyOperation(*host, makeRowSetOp(*host, rowId, hostSheet, R"({"pos":0})"));
    applyOperation(*host, makeCellSetOp(*host, cellId, hostSheet,
                                        "{\"t\":\"s\",\"v\":\"foo\",\"col\":\"" + colId.toString() +
                                            "\",\"row\":\"" + rowId.toString() + "\"}"));

    // Joiner: empty UI shell — prepareWorkbookForSync (shared CLI/WASM path)
    auto joiner = std::make_unique<Workbook>(generate_id(), "Untitled");
    joiner->setNodeId(generate_id());
    joiner->addSheet(std::make_unique<Sheet>(generate_id(), "Sheet1"));
    ASSERT_TRUE(isWorkbookContentEmpty(*joiner));
    ASSERT_NE(joiner->id, hostDocId);
    const PrepareForSyncResult prep = prepareWorkbookForSync(*joiner);
    EXPECT_EQ(prep.bootstrappedOps, 0u);
    EXPECT_GE(prep.discardedSheets, 1u);
    EXPECT_EQ(joiner->sheetCount(), 0u);
    EXPECT_EQ(joiner->getOpLog()->size(), 0u);
    EXPECT_TRUE(joiner->isCollaborating());

    // Apply host ops (full join pull)
    const size_t applied = applyOperations(*joiner, host->getOpLog()->getAllOperations());
    EXPECT_GE(applied, 5u);
    EXPECT_EQ(joiner->sheetCount(), 1u) << "must not have dual Sheet1 after join";
    EXPECT_EQ(joiner->sheets[0]->id, hostSheet);
    EXPECT_EQ(preferredActiveSheetIndex(*joiner), 0u);
    EXPECT_EQ(joiner->getSheet(hostSheet)->getCell(cellId)->value.raw, "foo");
    // Model B: empty joiner adopts host document UUID and name
    EXPECT_EQ(joiner->id, hostDocId);
    EXPECT_EQ(joiner->name, "HostDoc");
}

TEST(CrdtJoinEmptyShell, PrepareBootstrapsLocalContent) {
    auto wb = std::make_unique<Workbook>(generate_id(), "Untitled");
    wb->setNodeId(generate_id());
    const ID sheetId = generate_id();
    auto sheet = std::make_unique<Sheet>(sheetId, "Sheet1");
    sheet->setWorkbook(wb.get());
    wb->addSheet(std::move(sheet));
    applyOperation(*wb, makeColSetOp(*wb, generate_id(), sheetId, R"({"pos":0})"));

    // Offline edits put ops in oplog; prepare bootstraps material state
    const PrepareForSyncResult prep = prepareWorkbookForSync(*wb);
    EXPECT_FALSE(prep.alreadyCollaborating);
    EXPECT_GE(prep.bootstrappedOps, 1u);
    EXPECT_TRUE(wb->isCollaborating());
    EXPECT_FALSE(wb->getOpLog()->empty());
}

TEST(CrdtJoinEmptyShell, PrepareAlreadyCollaboratingIsNoOp) {
    auto wb = std::make_unique<Workbook>(generate_id(), "Untitled");
    wb->setNodeId(generate_id());
    wb->startCollaboration();
    const size_t before = wb->getOpLog()->size();
    const PrepareForSyncResult prep = prepareWorkbookForSync(*wb);
    EXPECT_TRUE(prep.alreadyCollaborating);
    EXPECT_EQ(prep.bootstrappedOps, 0u);
    EXPECT_EQ(wb->getOpLog()->size(), before);
}

TEST(CrdtJoinEmptyShell, EnsureDefaultSheetOnlyWhenEmpty) {
    auto wb = std::make_unique<Workbook>(generate_id(), "Untitled");
    wb->setNodeId(generate_id());
    wb->startCollaboration();
    const ID docId = wb->id;
    EXPECT_TRUE(ensureDefaultSheetViaCrdt(*wb));
    EXPECT_EQ(wb->sheetCount(), 1u);
    // Model B: alone-online also publishes document identity
    bool hasDocOp = false;
    for (const auto& op : wb->getOpLog()->getAllOperations()) {
        if (op.type == OpType::WORKBOOK_SET) {
            hasDocOp = true;
            EXPECT_EQ(op.target_id, docId);
            break;
        }
    }
    EXPECT_TRUE(hasDocOp);
    EXPECT_FALSE(ensureDefaultSheetViaCrdt(*wb));  // already has sheet + doc identity
}

TEST(CrdtJoinEmptyShell, AloneOnlineDocumentIdentityPullsOntoJoiner) {
    // Host alone online mints sheet + document identity
    auto host = std::make_unique<Workbook>(generate_id(), "RoomDoc");
    host->setNodeId(generate_id());
    host->startCollaboration();
    const ID hostDocId = host->id;
    EXPECT_TRUE(ensureDefaultSheetViaCrdt(*host));
    EXPECT_EQ(host->sheetCount(), 1u);
    EXPECT_EQ(host->id, hostDocId);

    auto joiner = std::make_unique<Workbook>(generate_id(), "Untitled");
    joiner->setNodeId(generate_id());
    ASSERT_NE(joiner->id, hostDocId);
    prepareWorkbookForSync(*joiner);
    applyOperations(*joiner, host->getOpLog()->getAllOperations());

    EXPECT_EQ(joiner->id, hostDocId);
    EXPECT_EQ(joiner->name, "RoomDoc");
    EXPECT_EQ(joiner->sheetCount(), 1u);
}

// ---------------------------------------------------------------------------
// In-document sheet import (CSV/XLSX drop contract) via CRDT
// ---------------------------------------------------------------------------

namespace {

// Source workbook with one sheet and a few cells (simulates CSV import material).
std::unique_ptr<Workbook> makeSourceSheetWithCells(const std::string& sheetName, const char* a1,
                                                   const char* b1) {
    auto src = std::make_unique<Workbook>(generate_id(), "Imported");
    src->setNodeId(generate_id());
    const ID sid = generate_id();
    auto sheet = std::make_unique<Sheet>(sid, sheetName);
    sheet->setWorkbook(src.get());
    src->addSheet(std::move(sheet));

    const ID colA = generate_id();
    const ID colB = generate_id();
    const ID row1 = generate_id();
    applyOperation(*src, makeColSetOp(*src, colA, sid, R"({"pos":0})"));
    applyOperation(*src, makeColSetOp(*src, colB, sid, R"({"pos":1})"));
    applyOperation(*src, makeRowSetOp(*src, row1, sid, R"({"pos":0})"));
    applyOperation(*src,
                   makeCellSetOp(*src, generate_id(), sid,
                                 std::string("{\"t\":\"s\",\"v\":\"") + a1 + "\",\"col\":\"" +
                                     colA.toString() + "\",\"row\":\"" + row1.toString() + "\"}"));
    applyOperation(*src,
                   makeCellSetOp(*src, generate_id(), sid,
                                 std::string("{\"t\":\"s\",\"v\":\"") + b1 + "\",\"col\":\"" +
                                     colB.toString() + "\",\"row\":\"" + row1.toString() + "\"}"));
    return src;
}

}  // namespace

TEST(CrdtSheetImport, IntoCurrentEmptySheetNoExtraSheet) {
    auto target = std::make_unique<Workbook>(generate_id(), "Live");
    target->setNodeId(generate_id());
    target->startCollaboration();
    const ID emptyId = generate_id();
    target->addSheet(std::make_unique<Sheet>(emptyId, "Sheet1"));
    ASSERT_TRUE(isSheetContentEmpty(*target->getSheet(emptyId)));

    auto src = makeSourceSheetWithCells("Data", "hello", "world");
    const size_t beforeSheets = target->sheetCount();
    const SheetImportResult r =
        importSheetViaCrdt(*target, *src, 0, SheetImportMode::INTO_CURRENT, 0);
    ASSERT_TRUE(r.success) << r.error;
    EXPECT_EQ(target->sheetCount(), beforeSheets) << "must not create an extra sheet";
    EXPECT_EQ(target->sheets[0]->id, emptyId) << "same sheet id for into_current";
    EXPECT_GT(target->getSheet(emptyId)->cellCount(), 0u);
    EXPECT_GT(r.opsApplied, 0u);
    EXPECT_FALSE(isSheetContentEmpty(*target->getSheet(emptyId)));
}

TEST(CrdtSheetImport, NewSheetIncrementsCount) {
    auto target = std::make_unique<Workbook>(generate_id(), "Live");
    target->setNodeId(generate_id());
    target->startCollaboration();
    target->addSheet(std::make_unique<Sheet>(generate_id(), "Sheet1"));
    // Give current sheet content so it is not empty
    const ID sid = target->sheets[0]->id;
    target->getSheet(sid)->setWorkbook(target.get());
    applyOperation(*target, makeColSetOp(*target, generate_id(), sid, R"({"pos":0})"));

    auto src = makeSourceSheetWithCells("Imported", "x", "y");
    const size_t before = target->sheetCount();
    const SheetImportResult r = importSheetViaCrdt(*target, *src, 0, SheetImportMode::NEW_SHEET, 0);
    ASSERT_TRUE(r.success) << r.error;
    EXPECT_EQ(target->sheetCount(), before + 1);
    EXPECT_EQ(r.activeSheetIndex, before);
}

TEST(CrdtSheetImport, ReplaceDeletesOldSheetIdAndKeepsCount) {
    auto target = std::make_unique<Workbook>(generate_id(), "Live");
    target->setNodeId(generate_id());
    target->startCollaboration();
    const ID oldId = generate_id();
    auto oldSheet = std::make_unique<Sheet>(oldId, "Sheet1");
    oldSheet->setWorkbook(target.get());
    target->addSheet(std::move(oldSheet));
    applyOperation(*target, makeColSetOp(*target, generate_id(), oldId, R"({"pos":0})"));
    applyOperation(*target, makeRowSetOp(*target, generate_id(), oldId, R"({"pos":0})"));
    ASSERT_FALSE(isSheetContentEmpty(*target->getSheet(oldId)));

    auto src = makeSourceSheetWithCells("Sheet1", "from", "csv");
    const size_t before = target->sheetCount();
    const size_t oplogBefore = target->getOpLog()->size();
    const SheetImportResult r = importSheetViaCrdt(*target, *src, 0, SheetImportMode::REPLACE, 0);
    ASSERT_TRUE(r.success) << r.error;
    EXPECT_EQ(target->sheetCount(), before) << "replace must not leave N+1 sheets";
    EXPECT_EQ(target->getSheet(oldId), nullptr) << "old sheet id must be gone";
    ASSERT_EQ(target->sheetCount(), 1u);
    EXPECT_NE(target->sheets[0]->id, oldId);
    EXPECT_GT(target->sheets[0]->cellCount(), 0u);
    EXPECT_GT(target->getOpLog()->size(), oplogBefore);
}

TEST(CrdtSheetImport, ReplacePeerAppliesOpsWithoutDualSheet) {
    // Host: collaborating workbook with content sheet
    auto host = std::make_unique<Workbook>(generate_id(), "Live");
    host->setNodeId(generate_id());
    host->startCollaboration();
    const ID hostSheet = generate_id();
    auto hs = std::make_unique<Sheet>(hostSheet, "Sheet1");
    hs->setWorkbook(host.get());
    host->addSheet(std::move(hs));
    applyOperation(*host, makeSheetSetOp(*host, hostSheet, R"({"name":"Sheet1"})"));
    applyOperation(*host, makeColSetOp(*host, generate_id(), hostSheet, R"({"pos":0})"));

    // Peer starts with same sheet structure (as if synced)
    auto peer = std::make_unique<Workbook>(generate_id(), "Live");
    peer->setNodeId(generate_id());
    peer->startCollaboration();
    applyOperations(*peer, host->getOpLog()->getAllOperations());
    ASSERT_EQ(peer->sheetCount(), 1u);
    ASSERT_NE(peer->getSheet(hostSheet), nullptr);

    // Host replaces sheet via CRDT import
    auto src = makeSourceSheetWithCells("Sheet1", "peer", "sync");
    const SheetImportResult r = importSheetViaCrdt(*host, *src, 0, SheetImportMode::REPLACE, 0);
    ASSERT_TRUE(r.success) << r.error;
    EXPECT_EQ(host->sheetCount(), 1u);

    // Peer applies host oplog (dedupe by HLC for already-seen ops)
    const size_t applied = applyOperations(*peer, host->getOpLog()->getAllOperations());
    EXPECT_GT(applied, 0u);
    EXPECT_EQ(peer->sheetCount(), 1u) << "peer must not end with dual Sheet1";
    EXPECT_EQ(peer->getSheet(hostSheet), nullptr) << "old sheet deleted on peer";
    EXPECT_GT(peer->sheets[0]->cellCount(), 0u);
    EXPECT_EQ(host->sheetCount(), peer->sheetCount());
}

TEST(CrdtSheetImport, IntoCurrentRejectsNonEmpty) {
    auto target = std::make_unique<Workbook>(generate_id(), "Live");
    target->setNodeId(generate_id());
    target->startCollaboration();
    const ID sid = generate_id();
    auto s = std::make_unique<Sheet>(sid, "Sheet1");
    s->setWorkbook(target.get());
    target->addSheet(std::move(s));
    applyOperation(*target, makeColSetOp(*target, generate_id(), sid, R"({"pos":0})"));

    auto src = makeSourceSheetWithCells("Sheet1", "a", "b");
    const SheetImportResult r =
        importSheetViaCrdt(*target, *src, 0, SheetImportMode::INTO_CURRENT, 0);
    EXPECT_FALSE(r.success);
    EXPECT_EQ(target->sheetCount(), 1u);
}

TEST(CrdtSheetImport, OpenPathDisablesCollabPointerContract) {
    // Structural: loadFrom* must not leave collab on a swapped workbook.
    // Verified at engine level by disableSync() before replace; here we assert
    // import never changes Workbook identity (pointer stability).
    auto target = std::make_unique<Workbook>(generate_id(), "Live");
    Workbook* stable = target.get();
    target->setNodeId(generate_id());
    target->startCollaboration();
    target->addSheet(std::make_unique<Sheet>(generate_id(), "Sheet1"));

    auto src = makeSourceSheetWithCells("Sheet1", "ok", "ok");
    (void)importSheetViaCrdt(*target, *src, 0, SheetImportMode::INTO_CURRENT, 0);
    EXPECT_EQ(target.get(), stable);
}

}  // namespace
}  // namespace cells
