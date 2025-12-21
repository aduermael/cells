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

}  // namespace
}  // namespace cells
