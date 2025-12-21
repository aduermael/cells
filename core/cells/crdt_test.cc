#include "core/cells/crdt.h"

#include "core/cells/id.h"

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
    Operation op = makeCellSetValueOp(*workbook, cell1, R"({"type":"n","value":"100"})");

    ApplyResult result = applyOperation(*workbook, op);
    EXPECT_EQ(result, ApplyResult::SUCCESS);

    // Verify the value changed
    Sheet* sheet = workbook->getSheetByIndex(0);
    Cell* cell = sheet->getCell(cell1);
    EXPECT_EQ(cell->value.asNumber(), 100);
}

TEST_F(CRDTTest, ApplyCellClear) {
    // First verify cell has a value
    Sheet* sheet = workbook->getSheetByIndex(0);
    Cell* cell = sheet->getCell(cell1);
    EXPECT_EQ(cell->value.type, CellValueType::NUMBER);

    // Create and apply clear operation
    Operation op = makeCellClearOp(*workbook, cell1);
    ApplyResult result = applyOperation(*workbook, op);
    EXPECT_EQ(result, ApplyResult::SUCCESS);

    // Verify the cell was cleared
    EXPECT_EQ(cell->value.type, CellValueType::STRING);
    EXPECT_TRUE(cell->value.raw.empty());
}

TEST_F(CRDTTest, DuplicateOperationRejected) {
    Operation op = makeCellSetValueOp(*workbook, cell1, R"({"type":"n","value":"100"})");

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
    Operation old_op(old_hlc, OpType::CELL_SET_VALUE, cell1, R"({"type":"n","value":"50"})");

    // Apply a newer operation first
    Operation new_op = makeCellSetValueOp(*workbook, cell1, R"({"type":"n","value":"100"})");
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
    Operation op1 = makeCellSetValueOp(*workbook, cell1, R"({"type":"n","value":"100"})");
    Operation op2 = makeCellSetValueOp(*workbook, cell2, R"({"type":"s","value":"World"})");

    applyOperation(*workbook, op1);
    applyOperation(*workbook, op2);

    const OpLog* oplog = workbook->getOpLog();
    EXPECT_EQ(oplog->size(), 2);
}

TEST_F(CRDTTest, ApplyMultipleOperations) {
    std::vector<Operation> ops;
    ops.push_back(makeCellSetValueOp(*workbook, cell1, R"({"type":"n","value":"1"})"));
    ops.push_back(makeCellSetValueOp(*workbook, cell1, R"({"type":"n","value":"2"})"));
    ops.push_back(makeCellSetValueOp(*workbook, cell1, R"({"type":"n","value":"3"})"));

    size_t applied = applyOperations(*workbook, ops);
    EXPECT_EQ(applied, 3);

    // Last value should be 3 (highest HLC)
    Sheet* sheet = workbook->getSheetByIndex(0);
    Cell* cell = sheet->getCell(cell1);
    EXPECT_EQ(cell->value.asNumber(), 3);
}

TEST_F(CRDTTest, IsSuperseded) {
    // Apply an operation
    Operation op1 = makeCellSetValueOp(*workbook, cell1, R"({"type":"n","value":"100"})");
    applyOperation(*workbook, op1);

    // Create an older operation
    ID node1("Node1111");
    HLC old_hlc(1000, 0, node1);
    Operation old_op(old_hlc, OpType::CELL_SET_VALUE, cell1, R"({"type":"n","value":"50"})");

    EXPECT_TRUE(isSuperseded(*workbook, old_op));
    // op1 has the same HLC as the latest, so it's considered superseded (>= check)
    EXPECT_TRUE(isSuperseded(*workbook, op1));

    // A newer operation is not superseded
    Operation op2 = makeCellSetValueOp(*workbook, cell1, R"({"type":"n","value":"200"})");
    EXPECT_FALSE(isSuperseded(*workbook, op2));  // op2 is newer than what's in OpLog
}

TEST_F(CRDTTest, InvalidTargetReturnsError) {
    ID fake_cell("FAKECELL");
    Operation op = makeCellSetValueOp(*workbook, fake_cell, R"({"type":"n","value":"100"})");
    // Manually construct with a non-existent cell ID
    HLC hlc = workbook->getCurrentHLC();
    Operation bad_op(hlc, OpType::CELL_SET_VALUE, fake_cell, R"({"type":"n","value":"100"})");

    ApplyResult result = applyOperation(*workbook, bad_op);
    EXPECT_EQ(result, ApplyResult::INVALID_TARGET);
}

TEST_F(CRDTTest, InvalidPayloadReturnsError) {
    HLC hlc = workbook->getCurrentHLC();
    Operation bad_op(hlc, OpType::CELL_SET_VALUE, cell1, R"({invalid json})");

    ApplyResult result = applyOperation(*workbook, bad_op);
    EXPECT_EQ(result, ApplyResult::INVALID_PAYLOAD);
}

TEST_F(CRDTTest, SheetRename) {
    Operation op = makeSheetRenameOp(*workbook, sheet_id, R"({"name":"RenamedSheet"})");
    ApplyResult result = applyOperation(*workbook, op);
    EXPECT_EQ(result, ApplyResult::SUCCESS);

    Sheet* sheet = workbook->getSheetByIndex(0);
    EXPECT_EQ(sheet->name, "RenamedSheet");
}

TEST_F(CRDTTest, AxisResize) {
    Operation op = makeDimResizeAxisOp(*workbook, col1, R"({"size":200})");
    ApplyResult result = applyOperation(*workbook, op);
    EXPECT_EQ(result, ApplyResult::SUCCESS);

    Sheet* sheet = workbook->getSheetByIndex(0);
    Axis* axis = sheet->getColumn(col1);
    EXPECT_EQ(axis->size, 200);
}

TEST_F(CRDTTest, ConcurrentEditsConverge) {
    // Simulate two nodes making concurrent edits
    ID node_a("NodeAAAA");
    ID node_b("NodeBBBB");

    // Node A's edit at time 1000
    HLC hlc_a(1000, 0, node_a);
    Operation op_a(hlc_a, OpType::CELL_SET_VALUE, cell1, R"({"type":"n","value":"100"})");

    // Node B's edit at same time 1000 but different node
    HLC hlc_b(1000, 0, node_b);
    Operation op_b(hlc_b, OpType::CELL_SET_VALUE, cell1, R"({"type":"n","value":"200"})");

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
    applyOperation(*workbook, makeCellSetValueOp(*workbook, cell1, R"({"type":"n","value":"1"})"));
    HLC sync_point = workbook->getOpLog()->getCurrentHLC();
    applyOperation(*workbook, makeCellSetValueOp(*workbook, cell1, R"({"type":"n","value":"2"})"));
    applyOperation(*workbook,
                   makeCellSetValueOp(*workbook, cell2, R"({"type":"s","value":"test"})"));

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
        cell_id = workbook_a->getSheetByIndex(0)->cells.begin()->first;
    }

    std::unique_ptr<Workbook> createWorkbook(const ID& node_id) {
        auto wb = std::make_unique<Workbook>(generate_id(), "TestWorkbook");
        wb->setNodeId(node_id);

        auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet1");

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
    // Get cell IDs from both workbooks (they have different IDs since created separately)
    auto* sheet_a = workbook_a->getSheetByIndex(0);
    auto* sheet_b = workbook_b->getSheetByIndex(0);
    ID cell_a = sheet_a->cells.begin()->first;
    ID cell_b = sheet_b->cells.begin()->first;

    // Peer A makes an edit
    Operation op_a = makeCellSetValueOp(*workbook_a, cell_a, R"({"type":"n","value":"100"})");
    applyOperation(*workbook_a, op_a);

    // Peer B makes an edit (to its own cell, different ID)
    Operation op_b = makeCellSetValueOp(*workbook_b, cell_b, R"({"type":"n","value":"200"})");
    applyOperation(*workbook_b, op_b);

    // Sync A -> B
    applyOperation(*workbook_b, op_a);

    // Sync B -> A
    applyOperation(*workbook_a, op_b);

    // Both should have the operation in their OpLogs
    EXPECT_EQ(workbook_a->getOpLog()->size(), 2);
    EXPECT_EQ(workbook_b->getOpLog()->size(), 2);
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

    Operation op_a(hlc_a, OpType::CELL_SET_VALUE, shared_cell, R"({"type":"n","value":"100"})");
    Operation op_b(hlc_b, OpType::CELL_SET_VALUE, shared_cell, R"({"type":"n","value":"200"})");

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

    Operation op_a(hlc_a, OpType::CELL_SET_VALUE, shared_cell, R"({"type":"n","value":"100"})");
    Operation op_b(hlc_b, OpType::CELL_SET_VALUE, shared_cell, R"({"type":"n","value":"200"})");
    Operation op_c(hlc_c, OpType::CELL_SET_VALUE, shared_cell, R"({"type":"n","value":"300"})");

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
        Operation op = makeCellSetValueOp(*workbook_a, shared_cell,
                                          R"({"type":"n","value":")" + std::to_string(i) + R"("})");
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
    Operation op_a = makeCellSetValueOp(*workbook_a, cell1, R"({"type":"n","value":"100"})");
    applyOperation(*workbook_a, op_a);

    // Peer B edits cell2
    Operation op_b = makeCellSetValueOp(*workbook_b, cell2, R"({"type":"n","value":"200"})");
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

}  // namespace
}  // namespace cells
