// Complex Operation Sequences Unit Tests
// Phase 13 of Comprehensive Unit Test Coverage Plan
//
// Tests complex sequences of operations to verify consistency, CRDT convergence
// with multiple peers, and correctness of copy-paste and fill operations.

#include <memory>
#include <string>
#include <vector>

#include "core/cells/crdt.h"
#include "core/cells/dependency_graph.h"
#include "core/cells/fill_range.h"
#include "core/cells/formula_display.h"
#include "core/cells/formula_eval.h"
#include "core/cells/formula_parser.h"
#include "core/cells/formula_recalc.h"
#include "core/cells/formula_resolver.h"
#include "core/cells/id.h"
#include "core/cells/model.h"
#include "core/cells/number_format.h"
#include "core/cells/style_buffer.h"

#include "gtest/gtest.h"

namespace cells {
namespace {

// =============================================================================
// Test Fixture for Operation Sequence Tests
// =============================================================================

class OperationSequenceTest : public ::testing::Test {
protected:
    void SetUp() override {
        workbook = std::make_unique<Workbook>(generate_id(), "Test");
        workbook->setNodeId(generate_id());
        workbook->addSheet(std::make_unique<Sheet>(generate_id(), "Sheet1"));
        sheet = workbook->getSheetByIndex(0);
        sheet->setWorkbook(workbook.get());

        // Create columns A-J (positions 0-9)
        for (uint32_t i = 0; i < 10; i++) {
            auto col = std::make_unique<Axis>(generate_id(), true);
            col->position = i;
            col->name = Sheet::positionToColumnName(i);
            colIds[i] = col->id;
            sheet->addColumn(std::move(col));
        }

        // Create rows 1-20 (positions 0-19)
        for (uint32_t i = 0; i < 20; i++) {
            auto row = std::make_unique<Axis>(generate_id(), false);
            row->position = i;
            rowIds[i] = row->id;
            sheet->addRow(std::move(row));
        }
    }

    // Set a cell value at a given column/row position (0-indexed)
    Cell* setCellValue(uint32_t col, uint32_t row, double value) {
        Cell* cell = sheet->getOrCreateCellAt(colIds[col], rowIds[row]);
        cell->value = CellValue(value);
        return cell;
    }

    Cell* setCellValue(uint32_t col, uint32_t row, const std::string& value) {
        Cell* cell = sheet->getOrCreateCellAt(colIds[col], rowIds[row]);
        cell->value = CellValue(value);
        return cell;
    }

    // Set a formula on a cell
    Cell* setCellFormula(uint32_t col, uint32_t row, const std::string& formula) {
        Cell* cell = sheet->getOrCreateCellAt(colIds[col], rowIds[row]);

        FormulaParser parser(formula);
        auto ast = parser.parse();
        if (!ast || parser.hasErrors()) {
            return nullptr;
        }

        FormulaResolver resolver(*workbook, *sheet);
        createRequiredEntities(resolver, ast.get());
        resolver.resolve(ast.get());

        auto result = sheet->setCellFormula(cell->id, formula, ast.release());
        if (!result.success) {
            return nullptr;
        }

        return cell;
    }

    // Helper to create missing entities before resolution
    void createRequiredEntities(FormulaResolver& resolver, ASTNode* ast) {
        RequiredEntities required = resolver.getRequiredEntities(ast);
        for (const auto& pendingCell : required.cells) {
            auto findColPos = [&required, this](const ID& colId) -> uint32_t {
                for (const auto& c : required.columns) {
                    if (c.id == colId)
                        return c.position;
                }
                const Axis* axis = sheet->getColumn(colId);
                return axis ? axis->position : 0;
            };
            auto findRowPos = [&required, this](const ID& rowId) -> uint32_t {
                for (const auto& r : required.rows) {
                    if (r.id == rowId)
                        return r.position;
                }
                const Axis* axis = sheet->getRow(rowId);
                return axis ? axis->position : 0;
            };
            uint32_t colPos = findColPos(pendingCell.colId);
            uint32_t rowPos = findRowPos(pendingCell.rowId);
            const Axis* c = sheet->getColumnByPosition(colPos);
            const Axis* r = sheet->getRowByPosition(rowPos);
            if (c && r) {
                sheet->getOrCreateCellAt(c->id, r->id);
            }
        }
    }

    // Get cell value as double
    double getCellNumber(uint32_t col, uint32_t row) {
        Cell* cell = sheet->getCellAt(colIds[col], rowIds[row]);
        if (!cell) {
            return 0.0;
        }
        return cell->value.asNumber();
    }

    // Get cell formula display
    std::string getCellFormulaDisplay(uint32_t col, uint32_t row) {
        Cell* cell = sheet->getCellAt(colIds[col], rowIds[row]);
        if (!cell || !cell->isFormula()) {
            return "";
        }
        FormulaDisplayConverter converter(*sheet, workbook.get());
        return converter.toDisplayString(cell->getFormula()->ast);
    }

    // Check if cell has error
    bool cellHasError(uint32_t col, uint32_t row, CellError expectedError) {
        Cell* cell = sheet->getCellAt(colIds[col], rowIds[row]);
        if (!cell) {
            return false;
        }
        const bool isError = cell->value.type == CellValueType::ERROR ||
                             cell->value.type == CellValueType::FORMULA_ERROR;
        return isError && cell->value.error == expectedError;
    }

    // Evaluate a cell formula
    void evaluateCell(uint32_t col, uint32_t row) {
        Cell* cell = sheet->getCellAt(colIds[col], rowIds[row]);
        if (cell && cell->isFormula()) {
            cells::evaluateCell(sheet, cell);
        }
    }

    std::unique_ptr<Workbook> workbook;
    Sheet* sheet;
    ID colIds[10];
    ID rowIds[20];
};

// =============================================================================
// 13a: Test rapid insert/delete/move sequences maintain consistency
// =============================================================================

TEST_F(OperationSequenceTest, RapidInsertDeleteMaintainsConsistency) {
    // Set up initial data
    setCellValue(0, 0, 10.0);  // A1 = 10
    setCellValue(0, 1, 20.0);  // A2 = 20
    setCellValue(0, 2, 30.0);  // A3 = 30

    // Set formula that references a range
    Cell* b1 = setCellFormula(1, 0, "=SUM(A1:A3)");
    ASSERT_NE(b1, nullptr);
    evaluateCell(1, 0);
    EXPECT_DOUBLE_EQ(b1->value.asNumber(), 60.0);

    // Insert a row at position 1 (between A1 and A2)
    sheet->insertRowAt(1);

    // Add value to new row
    ID newRowId = sheet->getRowByPosition(1)->id;
    Cell* newCell = sheet->getOrCreateCellAt(colIds[0], newRowId);
    newCell->value = CellValue(15.0);

    // Recalculate - formula should now cover 4 rows
    evaluateCell(1, 0);
    // Note: After insert, the formula still references original cells by UUID
    // SUM should still be 60 since we haven't added the new row to the formula's range
    EXPECT_DOUBLE_EQ(b1->value.asNumber(), 60.0);

    // Delete the new row
    sheet->deleteRow(newRowId);

    // Recalculate
    evaluateCell(1, 0);
    EXPECT_DOUBLE_EQ(b1->value.asNumber(), 60.0);
}

TEST_F(OperationSequenceTest, MultipleColumnInsertDeleteSequence) {
    // Set up data in columns A, B, C
    setCellValue(0, 0, 1.0);
    setCellValue(1, 0, 2.0);
    setCellValue(2, 0, 3.0);

    // Formula referencing all three
    Cell* d1 = setCellFormula(3, 0, "=A1+B1+C1");
    ASSERT_NE(d1, nullptr);
    evaluateCell(3, 0);
    EXPECT_DOUBLE_EQ(d1->value.asNumber(), 6.0);

    // Insert column at position 1 (between A and B)
    sheet->insertColumnAt(1);

    // The formula still references original cells by UUID, so result unchanged
    evaluateCell(3, 0);
    EXPECT_DOUBLE_EQ(d1->value.asNumber(), 6.0);

    // Delete the new column
    ID newColId = sheet->getColumnByPosition(1)->id;
    sheet->deleteColumn(newColId);

    // Result should still be correct
    evaluateCell(3, 0);
    EXPECT_DOUBLE_EQ(d1->value.asNumber(), 6.0);
}

TEST_F(OperationSequenceTest, AlternatingInsertDelete) {
    // Start with value in A1
    setCellValue(0, 0, 100.0);

    // Perform multiple insert/delete cycles
    for (int i = 0; i < 5; i++) {
        // Insert row at position 0
        sheet->insertRowAt(0);

        // Delete the new row
        ID newRowId = sheet->getRowByPosition(0)->id;
        sheet->deleteRow(newRowId);
    }

    // Original cell should still have its value
    // Note: After inserts, the original row has moved position
    // But cell lookup by UUID should still work
    Cell* a1 = sheet->getCellAt(colIds[0], rowIds[0]);
    ASSERT_NE(a1, nullptr);
    EXPECT_DOUBLE_EQ(a1->value.asNumber(), 100.0);
}

TEST_F(OperationSequenceTest, InsertDeleteWithFormulaChain) {
    // Create a chain of formulas: A1=1, B1=A1+1, C1=B1+1, D1=C1+1
    setCellValue(0, 0, 1.0);
    setCellFormula(1, 0, "=A1+1");
    setCellFormula(2, 0, "=B1+1");
    setCellFormula(3, 0, "=C1+1");

    // Evaluate chain
    evaluateCell(1, 0);
    evaluateCell(2, 0);
    evaluateCell(3, 0);

    EXPECT_DOUBLE_EQ(getCellNumber(1, 0), 2.0);
    EXPECT_DOUBLE_EQ(getCellNumber(2, 0), 3.0);
    EXPECT_DOUBLE_EQ(getCellNumber(3, 0), 4.0);

    // Insert column at position 2 (between B and C)
    sheet->insertColumnAt(2);

    // Re-evaluate - formulas reference cells by UUID so should be unaffected
    evaluateCell(1, 0);
    evaluateCell(2, 0);  // This is now the new empty column
    evaluateCell(3, 0);
    evaluateCell(4, 0);

    // Original cells still have correct values
    Cell* b1 = sheet->getCellAt(colIds[1], rowIds[0]);
    Cell* c1 = sheet->getCellAt(colIds[2], rowIds[0]);
    Cell* d1 = sheet->getCellAt(colIds[3], rowIds[0]);
    ASSERT_NE(b1, nullptr);
    ASSERT_NE(c1, nullptr);
    ASSERT_NE(d1, nullptr);
    EXPECT_DOUBLE_EQ(b1->value.asNumber(), 2.0);
    EXPECT_DOUBLE_EQ(c1->value.asNumber(), 3.0);
    EXPECT_DOUBLE_EQ(d1->value.asNumber(), 4.0);
}

TEST_F(OperationSequenceTest, MoveAfterInsertDelete) {
    // Set up initial data
    setCellValue(0, 0, 10.0);  // A1
    setCellValue(1, 0, 20.0);  // B1
    setCellValue(2, 0, 30.0);  // C1

    // Insert a column between A and B
    sheet->insertColumnAt(1);

    // Move the new column to the end
    ID newColId = sheet->getColumnByPosition(1)->id;
    Axis* newCol = sheet->getColumn(newColId);
    newCol->position = 4;

    // Delete another column and verify structure
    sheet->deleteColumn(newColId);

    // Original cells should still be accessible
    Cell* a1 = sheet->getCellAt(colIds[0], rowIds[0]);
    Cell* b1 = sheet->getCellAt(colIds[1], rowIds[0]);
    Cell* c1 = sheet->getCellAt(colIds[2], rowIds[0]);
    ASSERT_NE(a1, nullptr);
    ASSERT_NE(b1, nullptr);
    ASSERT_NE(c1, nullptr);
}

TEST_F(OperationSequenceTest, RapidInsertAtSamePosition) {
    // Insert 5 rows at the same position
    std::vector<ID> insertedRowIds;
    for (int i = 0; i < 5; i++) {
        sheet->insertRowAt(0);
        insertedRowIds.push_back(sheet->getRowByPosition(0)->id);
    }

    // Verify all rows exist
    for (const auto& rowId : insertedRowIds) {
        EXPECT_NE(sheet->getRow(rowId), nullptr);
    }

    // Delete all inserted rows in reverse order
    for (auto it = insertedRowIds.rbegin(); it != insertedRowIds.rend(); ++it) {
        sheet->deleteRow(*it);
    }

    // Original rows should still exist
    for (int i = 0; i < 10; i++) {
        EXPECT_NE(sheet->getRow(rowIds[i]), nullptr);
    }
}

TEST_F(OperationSequenceTest, InsertDeleteWithRangeFormula) {
    // Set up data and range formula
    setCellValue(0, 0, 1.0);
    setCellValue(0, 1, 2.0);
    setCellValue(0, 2, 3.0);
    setCellValue(0, 3, 4.0);
    setCellValue(0, 4, 5.0);

    Cell* sumCell = setCellFormula(1, 0, "=SUM(A1:A5)");
    ASSERT_NE(sumCell, nullptr);
    evaluateCell(1, 0);
    EXPECT_DOUBLE_EQ(sumCell->value.asNumber(), 15.0);

    // Insert row inside the range
    sheet->insertRowAt(2);

    // Evaluate - the range is defined by corner cell UUIDs, not positions
    evaluateCell(1, 0);
    // Sum should still be 15 (same cells by UUID)
    EXPECT_DOUBLE_EQ(sumCell->value.asNumber(), 15.0);

    // Delete the inserted row
    ID insertedRowId = sheet->getRowByPosition(2)->id;
    sheet->deleteRow(insertedRowId);

    evaluateCell(1, 0);
    EXPECT_DOUBLE_EQ(sumCell->value.asNumber(), 15.0);
}

TEST_F(OperationSequenceTest, SequentialDeletesFromEnd) {
    // Set up 5 rows of data
    for (int i = 0; i < 5; i++) {
        setCellValue(0, static_cast<uint32_t>(i), static_cast<double>(i + 1));
    }

    Cell* sumCell = setCellFormula(1, 0, "=SUM(A1:A5)");
    ASSERT_NE(sumCell, nullptr);
    evaluateCell(1, 0);
    EXPECT_DOUBLE_EQ(sumCell->value.asNumber(), 15.0);

    // Delete rows from the end, one by one
    for (int i = 4; i >= 1; i--) {
        // Delete the row
        sheet->deleteRow(rowIds[i]);

        // Re-evaluate
        evaluateCell(1, 0);

        // The formula still tries to evaluate the full range
        // but deleted cells will be treated as empty/zero
    }
}

// =============================================================================
// 13b: Test style + format + value changes in single operation batch
// =============================================================================

TEST_F(OperationSequenceTest, StyleFormatValueInBatch) {
    workbook->startCollaboration();

    // Create cell with value
    Cell* a1 = setCellValue(0, 0, 42.0);

    // Apply style
    StyleBuffer styleBuf;
    styleBuf.setBold(true);
    styleBuf.setItalic(true);
    Operation styleOp = makeCellSetStyleOp(*workbook, a1->id, styleBuf);
    ApplyResult styleResult = applyOperation(*workbook, styleOp);
    EXPECT_EQ(styleResult, ApplyResult::SUCCESS);

    // Apply format
    FormatBuffer formatBuf;
    formatBuf.setCategory(NumberFormatCategory::PERCENTAGE);
    Operation formatOp = makeCellSetFormatOp(*workbook, a1->id, formatBuf);
    ApplyResult formatResult = applyOperation(*workbook, formatOp);
    EXPECT_EQ(formatResult, ApplyResult::SUCCESS);

    // Update value
    Operation valueOp = makeCellSetOp(*workbook, a1->id, R"({"t":"n","v":"100"})");
    ApplyResult valueResult = applyOperation(*workbook, valueOp);
    EXPECT_EQ(valueResult, ApplyResult::SUCCESS);

    // Verify all changes applied
    EXPECT_DOUBLE_EQ(a1->value.asNumber(), 100.0);
    const StyleBuffer* entityStyle = workbook->getEntityStyle(a1->id);
    ASSERT_NE(entityStyle, nullptr);
    EXPECT_TRUE(entityStyle->getBold());
    EXPECT_TRUE(entityStyle->getItalic());
}

TEST_F(OperationSequenceTest, MultiCellBatchOperations) {
    workbook->startCollaboration();

    // Create multiple cells
    Cell* a1 = setCellValue(0, 0, 1.0);
    Cell* a2 = setCellValue(0, 1, 2.0);
    Cell* a3 = setCellValue(0, 2, 3.0);

    // Apply same style to all cells
    StyleBuffer styleBuf;
    styleBuf.setBgColorHex("#FF0000");

    Operation op1 = makeCellSetStyleOp(*workbook, a1->id, styleBuf);
    Operation op2 = makeCellSetStyleOp(*workbook, a2->id, styleBuf);
    Operation op3 = makeCellSetStyleOp(*workbook, a3->id, styleBuf);

    EXPECT_EQ(applyOperation(*workbook, op1), ApplyResult::SUCCESS);
    EXPECT_EQ(applyOperation(*workbook, op2), ApplyResult::SUCCESS);
    EXPECT_EQ(applyOperation(*workbook, op3), ApplyResult::SUCCESS);

    // Verify all cells have the style
    for (Cell* cell : {a1, a2, a3}) {
        const StyleBuffer* style = workbook->getEntityStyle(cell->id);
        ASSERT_NE(style, nullptr);
        EXPECT_EQ(style->getBgColorHex(), "#FF0000");
    }
}

TEST_F(OperationSequenceTest, AlternatingStyleAndValue) {
    workbook->startCollaboration();

    Cell* a1 = setCellValue(0, 0, 10.0);

    // Alternate between style and value changes
    for (int i = 0; i < 5; i++) {
        // Update value
        Operation valueOp = makeCellSetOp(*workbook, a1->id,
                                          R"({"t":"n","v":")" + std::to_string(i * 10) + R"("})");
        applyOperation(*workbook, valueOp);

        // Update style
        StyleBuffer styleBuf;
        styleBuf.setBold(i % 2 == 0);
        Operation styleOp = makeCellSetStyleOp(*workbook, a1->id, styleBuf);
        applyOperation(*workbook, styleOp);
    }

    // Final value should be 40 (last iteration i=4: 4*10=40)
    EXPECT_DOUBLE_EQ(a1->value.asNumber(), 40.0);

    // Final style should have bold=true (4 % 2 == 0)
    const StyleBuffer* style = workbook->getEntityStyle(a1->id);
    ASSERT_NE(style, nullptr);
    EXPECT_TRUE(style->getBold());
}

TEST_F(OperationSequenceTest, FormatChangesWithFormulaCell) {
    workbook->startCollaboration();

    // Create formula cell
    setCellValue(0, 0, 0.5);  // 50%
    Cell* b1 = setCellFormula(1, 0, "=A1*100");
    ASSERT_NE(b1, nullptr);
    evaluateCell(1, 0);
    EXPECT_DOUBLE_EQ(b1->value.asNumber(), 50.0);

    // Apply percentage format to source cell
    FormatBuffer formatBuf;
    formatBuf.setCategory(NumberFormatCategory::PERCENTAGE);
    Operation formatOp =
        makeCellSetFormatOp(*workbook, sheet->getCellAt(colIds[0], rowIds[0])->id, formatBuf);
    EXPECT_EQ(applyOperation(*workbook, formatOp), ApplyResult::SUCCESS);

    // Formula result should be unchanged (format doesn't affect calculation)
    evaluateCell(1, 0);
    EXPECT_DOUBLE_EQ(b1->value.asNumber(), 50.0);
}

TEST_F(OperationSequenceTest, ClearStyleResetToDefault) {
    workbook->startCollaboration();

    Cell* a1 = setCellValue(0, 0, 42.0);

    // Apply style
    StyleBuffer styleBuf;
    styleBuf.setBold(true);
    styleBuf.setItalic(true);
    styleBuf.setBgColorHex("#FF0000");
    Operation setOp = makeCellSetStyleOp(*workbook, a1->id, styleBuf);
    EXPECT_EQ(applyOperation(*workbook, setOp), ApplyResult::SUCCESS);

    // Verify style applied
    const StyleBuffer* style1 = workbook->getEntityStyle(a1->id);
    ASSERT_NE(style1, nullptr);
    EXPECT_TRUE(style1->getBold());

    // Clear style
    Operation clearOp = makeCellClearStyleOp(*workbook, a1->id);
    EXPECT_EQ(applyOperation(*workbook, clearOp), ApplyResult::SUCCESS);

    // Style should be cleared
    const StyleBuffer* style2 = workbook->getEntityStyle(a1->id);
    EXPECT_TRUE(style2 == nullptr || style2->isEmpty());
}

// =============================================================================
// 13c: Test concurrent operations from 3+ peers converge correctly
// =============================================================================

class ThreePeerConvergenceTest : public ::testing::Test {
protected:
    void SetUp() override {
        node_a = ID("NodeAAAA");
        node_b = ID("NodeBBBB");
        node_c = ID("NodeCCCC");

        workbook_a = createWorkbook(node_a);
        workbook_b = createWorkbook(node_b);
        workbook_c = createWorkbook(node_c);
    }

    std::unique_ptr<Workbook> createWorkbook(const ID& nodeId) {
        auto wb = std::make_unique<Workbook>(generate_id(), "Test");
        wb->setNodeId(nodeId);

        auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet1");
        sheet->setWorkbook(wb.get());

        // Create columns and rows
        for (uint32_t i = 0; i < 5; i++) {
            auto col = std::make_unique<Axis>(generate_id(), true);
            col->position = i;
            sheet->addColumn(std::move(col));

            auto row = std::make_unique<Axis>(generate_id(), false);
            row->position = i;
            sheet->addRow(std::move(row));
        }

        wb->addSheet(std::move(sheet));
        return wb;
    }

    // Add shared cell to all workbooks
    void addSharedCell(const ID& cellId, const ID& colId, const ID& rowId) {
        for (auto* wb : {workbook_a.get(), workbook_b.get(), workbook_c.get()}) {
            auto* sheet = wb->getSheetByIndex(0);

            // Add column if not exists
            if (!sheet->getColumn(colId)) {
                auto col = std::make_unique<Axis>(colId, true);
                col->position = 0;
                sheet->addColumn(std::move(col));
            }

            // Add row if not exists
            if (!sheet->getRow(rowId)) {
                auto row = std::make_unique<Axis>(rowId, false);
                row->position = 0;
                sheet->addRow(std::move(row));
            }

            // Add cell
            auto cell = std::make_unique<Cell>(cellId, colId, rowId);
            cell->value = CellValue(0.0);
            sheet->addCell(std::move(cell));
        }
    }

    std::unique_ptr<Workbook> workbook_a;
    std::unique_ptr<Workbook> workbook_b;
    std::unique_ptr<Workbook> workbook_c;
    ID node_a, node_b, node_c;
};

TEST_F(ThreePeerConvergenceTest, ThreePeersSameCellConverge) {
    ID sharedCell = generate_id();
    ID sharedCol = generate_id();
    ID sharedRow = generate_id();

    addSharedCell(sharedCell, sharedCol, sharedRow);

    // All three peers edit the same cell with same timestamp
    HLC hlc_a(1000, 0, node_a);
    HLC hlc_b(1000, 0, node_b);
    HLC hlc_c(1000, 0, node_c);

    Operation op_a(hlc_a, OpType::CELL_SET, sharedCell, R"({"t":"n","v":"100"})");
    Operation op_b(hlc_b, OpType::CELL_SET, sharedCell, R"({"t":"n","v":"200"})");
    Operation op_c(hlc_c, OpType::CELL_SET, sharedCell, R"({"t":"n","v":"300"})");

    // Apply in different orders on each workbook
    // Workbook A: A, B, C
    applyOperation(*workbook_a, op_a);
    applyOperation(*workbook_a, op_b);
    applyOperation(*workbook_a, op_c);

    // Workbook B: C, A, B
    applyOperation(*workbook_b, op_c);
    applyOperation(*workbook_b, op_a);
    applyOperation(*workbook_b, op_b);

    // Workbook C: B, C, A
    applyOperation(*workbook_c, op_b);
    applyOperation(*workbook_c, op_c);
    applyOperation(*workbook_c, op_a);

    // All should converge to same value (NodeCCCC > NodeBBBB > NodeAAAA)
    Cell* cell_a = workbook_a->getSheetByIndex(0)->getCell(sharedCell);
    Cell* cell_b = workbook_b->getSheetByIndex(0)->getCell(sharedCell);
    Cell* cell_c = workbook_c->getSheetByIndex(0)->getCell(sharedCell);

    ASSERT_NE(cell_a, nullptr);
    ASSERT_NE(cell_b, nullptr);
    ASSERT_NE(cell_c, nullptr);

    EXPECT_EQ(cell_a->value.asNumber(), cell_b->value.asNumber());
    EXPECT_EQ(cell_b->value.asNumber(), cell_c->value.asNumber());
    // Node C wins because 'C' > 'B' > 'A' lexicographically
    EXPECT_EQ(cell_a->value.asNumber(), 300);
}

TEST_F(ThreePeerConvergenceTest, ThreePeersDifferentCellsConverge) {
    // Create three different cells
    ID cell1 = generate_id();
    ID cell2 = generate_id();
    ID cell3 = generate_id();
    ID col = generate_id();
    ID row1 = generate_id();
    ID row2 = generate_id();
    ID row3 = generate_id();

    // Add all cells to all workbooks
    for (auto* wb : {workbook_a.get(), workbook_b.get(), workbook_c.get()}) {
        auto* sheet = wb->getSheetByIndex(0);

        if (!sheet->getColumn(col)) {
            auto c = std::make_unique<Axis>(col, true);
            c->position = 0;
            sheet->addColumn(std::move(c));
        }
        for (auto rowId : {row1, row2, row3}) {
            if (!sheet->getRow(rowId)) {
                auto r = std::make_unique<Axis>(rowId, false);
                r->position = sheet->getRowIds().size();
                sheet->addRow(std::move(r));
            }
        }

        sheet->addCell(std::make_unique<Cell>(cell1, col, row1));
        sheet->addCell(std::make_unique<Cell>(cell2, col, row2));
        sheet->addCell(std::make_unique<Cell>(cell3, col, row3));
    }

    // Each peer edits a different cell
    Operation op_a = makeCellSetOp(*workbook_a, cell1, R"({"t":"n","v":"100"})");
    Operation op_b = makeCellSetOp(*workbook_b, cell2, R"({"t":"n","v":"200"})");
    Operation op_c = makeCellSetOp(*workbook_c, cell3, R"({"t":"n","v":"300"})");

    // Apply all operations to all workbooks
    for (auto* wb : {workbook_a.get(), workbook_b.get(), workbook_c.get()}) {
        applyOperation(*wb, op_a);
        applyOperation(*wb, op_b);
        applyOperation(*wb, op_c);
    }

    // All workbooks should have all three values
    for (auto* wb : {workbook_a.get(), workbook_b.get(), workbook_c.get()}) {
        auto* sheet = wb->getSheetByIndex(0);
        EXPECT_DOUBLE_EQ(sheet->getCell(cell1)->value.asNumber(), 100);
        EXPECT_DOUBLE_EQ(sheet->getCell(cell2)->value.asNumber(), 200);
        EXPECT_DOUBLE_EQ(sheet->getCell(cell3)->value.asNumber(), 300);
    }
}

TEST_F(ThreePeerConvergenceTest, ThreePeersSequentialEditsToSameCell) {
    ID sharedCell = generate_id();
    ID sharedCol = generate_id();
    ID sharedRow = generate_id();

    addSharedCell(sharedCell, sharedCol, sharedRow);

    // Sequential edits with increasing timestamps
    HLC hlc_a(1000, 0, node_a);
    HLC hlc_b(2000, 0, node_b);
    HLC hlc_c(3000, 0, node_c);

    Operation op_a(hlc_a, OpType::CELL_SET, sharedCell, R"({"t":"n","v":"100"})");
    Operation op_b(hlc_b, OpType::CELL_SET, sharedCell, R"({"t":"n","v":"200"})");
    Operation op_c(hlc_c, OpType::CELL_SET, sharedCell, R"({"t":"n","v":"300"})");

    // Apply in different orders - should always converge to 300 (highest timestamp)
    // Workbook A: C, B, A (reverse order)
    applyOperation(*workbook_a, op_c);
    applyOperation(*workbook_a, op_b);
    applyOperation(*workbook_a, op_a);

    // Workbook B: A, C, B (out of order)
    applyOperation(*workbook_b, op_a);
    applyOperation(*workbook_b, op_c);
    applyOperation(*workbook_b, op_b);

    // Workbook C: B, A, C
    applyOperation(*workbook_c, op_b);
    applyOperation(*workbook_c, op_a);
    applyOperation(*workbook_c, op_c);

    // All should have value 300 (highest HLC timestamp)
    Cell* cell_a = workbook_a->getSheetByIndex(0)->getCell(sharedCell);
    Cell* cell_b = workbook_b->getSheetByIndex(0)->getCell(sharedCell);
    Cell* cell_c = workbook_c->getSheetByIndex(0)->getCell(sharedCell);

    EXPECT_EQ(cell_a->value.asNumber(), 300);
    EXPECT_EQ(cell_b->value.asNumber(), 300);
    EXPECT_EQ(cell_c->value.asNumber(), 300);
}

TEST_F(ThreePeerConvergenceTest, ThreePeersStyleOperationsConverge) {
    ID sharedCell = generate_id();
    ID sharedCol = generate_id();
    ID sharedRow = generate_id();

    addSharedCell(sharedCell, sharedCol, sharedRow);

    // Each peer sets a different style property
    StyleBuffer styleA;
    styleA.setBold(true);
    StyleBuffer styleB;
    styleB.setItalic(true);
    StyleBuffer styleC;
    styleC.setUnderline(true);

    Operation op_a = makeCellSetStyleOp(*workbook_a, sharedCell, styleA);
    Operation op_b = makeCellSetStyleOp(*workbook_b, sharedCell, styleB);
    Operation op_c = makeCellSetStyleOp(*workbook_c, sharedCell, styleC);

    // Apply all to all workbooks
    for (auto* wb : {workbook_a.get(), workbook_b.get(), workbook_c.get()}) {
        applyOperation(*wb, op_a);
        applyOperation(*wb, op_b);
        applyOperation(*wb, op_c);
    }

    // All workbooks should converge to the same style
    // The last-writer-wins means only one style survives
    const StyleBuffer* style_a = workbook_a->getEntityStyle(sharedCell);
    const StyleBuffer* style_b = workbook_b->getEntityStyle(sharedCell);
    const StyleBuffer* style_c = workbook_c->getEntityStyle(sharedCell);

    // At least verify they converge to the same state
    ASSERT_NE(style_a, nullptr);
    ASSERT_NE(style_b, nullptr);
    ASSERT_NE(style_c, nullptr);

    EXPECT_EQ(style_a->getBold(), style_b->getBold());
    EXPECT_EQ(style_b->getBold(), style_c->getBold());
    EXPECT_EQ(style_a->getItalic(), style_b->getItalic());
    EXPECT_EQ(style_b->getItalic(), style_c->getItalic());
    EXPECT_EQ(style_a->getUnderline(), style_b->getUnderline());
    EXPECT_EQ(style_b->getUnderline(), style_c->getUnderline());
}

TEST_F(ThreePeerConvergenceTest, ThreePeersInterleavedOperations) {
    // Create multiple shared cells
    ID cell1 = generate_id();
    ID cell2 = generate_id();
    ID col = generate_id();
    ID row1 = generate_id();
    ID row2 = generate_id();

    for (auto* wb : {workbook_a.get(), workbook_b.get(), workbook_c.get()}) {
        auto* sheet = wb->getSheetByIndex(0);

        if (!sheet->getColumn(col)) {
            auto c = std::make_unique<Axis>(col, true);
            c->position = 0;
            sheet->addColumn(std::move(c));
        }
        if (!sheet->getRow(row1)) {
            auto r = std::make_unique<Axis>(row1, false);
            r->position = 0;
            sheet->addRow(std::move(r));
        }
        if (!sheet->getRow(row2)) {
            auto r = std::make_unique<Axis>(row2, false);
            r->position = 1;
            sheet->addRow(std::move(r));
        }

        sheet->addCell(std::make_unique<Cell>(cell1, col, row1));
        sheet->addCell(std::make_unique<Cell>(cell2, col, row2));
    }

    // Interleaved operations: A edits cell1, B edits cell2, C edits cell1
    Operation op_a1 = makeCellSetOp(*workbook_a, cell1, R"({"t":"n","v":"10"})");
    Operation op_b2 = makeCellSetOp(*workbook_b, cell2, R"({"t":"n","v":"20"})");
    Operation op_c1 = makeCellSetOp(*workbook_c, cell1, R"({"t":"n","v":"30"})");

    // Apply in interleaved order
    // A: a1, b2, c1
    applyOperation(*workbook_a, op_a1);
    applyOperation(*workbook_a, op_b2);
    applyOperation(*workbook_a, op_c1);

    // B: b2, c1, a1
    applyOperation(*workbook_b, op_b2);
    applyOperation(*workbook_b, op_c1);
    applyOperation(*workbook_b, op_a1);

    // C: c1, a1, b2
    applyOperation(*workbook_c, op_c1);
    applyOperation(*workbook_c, op_a1);
    applyOperation(*workbook_c, op_b2);

    // All should converge
    // cell1: C wins (highest timestamp or node ID)
    // cell2: B's value (only one writer)
    for (auto* wb : {workbook_a.get(), workbook_b.get(), workbook_c.get()}) {
        auto* sheet = wb->getSheetByIndex(0);
        Cell* c1 = sheet->getCell(cell1);
        Cell* c2 = sheet->getCell(cell2);
        ASSERT_NE(c1, nullptr);
        ASSERT_NE(c2, nullptr);
        // Cell2 should always be 20 (only B edited it)
        EXPECT_DOUBLE_EQ(c2->value.asNumber(), 20);
    }

    // All workbooks should agree on cell1 value
    Cell* c1_a = workbook_a->getSheetByIndex(0)->getCell(cell1);
    Cell* c1_b = workbook_b->getSheetByIndex(0)->getCell(cell1);
    Cell* c1_c = workbook_c->getSheetByIndex(0)->getCell(cell1);

    EXPECT_EQ(c1_a->value.asNumber(), c1_b->value.asNumber());
    EXPECT_EQ(c1_b->value.asNumber(), c1_c->value.asNumber());
}

// =============================================================================
// 13e: Test copy-paste operation sequences with formulas
// =============================================================================

TEST_F(OperationSequenceTest, CopyPasteSimpleValues) {
    // Set up source data
    setCellValue(0, 0, 10.0);  // A1
    setCellValue(0, 1, 20.0);  // A2
    setCellValue(0, 2, 30.0);  // A3

    // Simulate copy-paste to column B
    for (int i = 0; i < 3; i++) {
        Cell* src = sheet->getCellAt(colIds[0], rowIds[i]);
        Cell* dst = sheet->getOrCreateCellAt(colIds[1], rowIds[i]);
        dst->value = src->value;
    }

    // Verify
    EXPECT_DOUBLE_EQ(getCellNumber(1, 0), 10.0);
    EXPECT_DOUBLE_EQ(getCellNumber(1, 1), 20.0);
    EXPECT_DOUBLE_EQ(getCellNumber(1, 2), 30.0);
}

TEST_F(OperationSequenceTest, CopyPasteFormulaAdjustsReferences) {
    // Set up: A1=1, A2=2, B1=A1+A2
    setCellValue(0, 0, 1.0);
    setCellValue(0, 1, 2.0);
    setCellFormula(1, 0, "=A1+A2");

    evaluateCell(1, 0);
    EXPECT_DOUBLE_EQ(getCellNumber(1, 0), 3.0);

    // Copy B1 to C1 (fill right)
    FillResult result = fillRange(workbook.get(), sheet, 1, 0, 1, 0, 1, 0, 2, 0);
    EXPECT_TRUE(result.success);

    // C1 should be =B1+B2
    EXPECT_EQ(getCellFormulaDisplay(2, 0), "=B1+B2");
}

TEST_F(OperationSequenceTest, CopyPasteFormulaChain) {
    // Create a chain: A1=1, B1=A1*2, C1=B1*2
    setCellValue(0, 0, 1.0);
    setCellFormula(1, 0, "=A1*2");
    setCellFormula(2, 0, "=B1*2");

    evaluateCell(1, 0);
    evaluateCell(2, 0);

    EXPECT_DOUBLE_EQ(getCellNumber(1, 0), 2.0);
    EXPECT_DOUBLE_EQ(getCellNumber(2, 0), 4.0);

    // Fill row 1 down to row 2
    FillResult r1 = fillRange(workbook.get(), sheet, 0, 0, 2, 0, 0, 0, 2, 1);
    EXPECT_TRUE(r1.success);

    // Row 2 should have: A2=1, B2=A2*2, C2=B2*2
    EXPECT_EQ(getCellFormulaDisplay(1, 1), "=A2*2");
    EXPECT_EQ(getCellFormulaDisplay(2, 1), "=B2*2");
}

TEST_F(OperationSequenceTest, CopyPasteWithAbsoluteReferences) {
    // A1=100, B1=$A$1*2 (absolute reference)
    setCellValue(0, 0, 100.0);
    setCellFormula(1, 0, "=$A$1*2");

    evaluateCell(1, 0);
    EXPECT_DOUBLE_EQ(getCellNumber(1, 0), 200.0);

    // Fill B1 down to B3
    FillResult result = fillRange(workbook.get(), sheet, 1, 0, 1, 0, 1, 0, 1, 2);
    EXPECT_TRUE(result.success);

    // All should still reference $A$1
    EXPECT_EQ(getCellFormulaDisplay(1, 0), "=$A$1*2");
    EXPECT_EQ(getCellFormulaDisplay(1, 1), "=$A$1*2");
    EXPECT_EQ(getCellFormulaDisplay(1, 2), "=$A$1*2");
}

TEST_F(OperationSequenceTest, CopyPasteMixedReferences) {
    // A1=10, B1=$A1+A$1 (mixed references)
    setCellValue(0, 0, 10.0);
    setCellFormula(1, 0, "=$A1+A$1");

    // Fill B1 down to B2 (row shift)
    FillResult result = fillRange(workbook.get(), sheet, 1, 0, 1, 0, 1, 0, 1, 1);
    EXPECT_TRUE(result.success);

    // B2 should be =$A2+A$1 ($A1 row changes, A$1 row stays)
    EXPECT_EQ(getCellFormulaDisplay(1, 1), "=$A2+A$1");
}

TEST_F(OperationSequenceTest, CopyPasteRangeFormula) {
    // Set up data
    setCellValue(0, 0, 1.0);
    setCellValue(0, 1, 2.0);
    setCellValue(0, 2, 3.0);

    // B1 = SUM(A1:A3)
    setCellFormula(1, 0, "=SUM(A1:A3)");
    evaluateCell(1, 0);
    EXPECT_DOUBLE_EQ(getCellNumber(1, 0), 6.0);

    // Fill B1 right to C1
    FillResult result = fillRange(workbook.get(), sheet, 1, 0, 1, 0, 1, 0, 2, 0);
    EXPECT_TRUE(result.success);

    // C1 should be =SUM(B1:B3)
    EXPECT_EQ(getCellFormulaDisplay(2, 0), "=SUM(B1:B3)");
}

// =============================================================================
// 13f: Test fill operation sequences (fill down, fill right)
// =============================================================================

TEST_F(OperationSequenceTest, FillDownLinearSequence) {
    // Set up: A1=1, A2=2
    setCellValue(0, 0, 1.0);
    setCellValue(0, 1, 2.0);

    // Fill down to A5
    FillResult result = fillRange(workbook.get(), sheet, 0, 0, 0, 1, 0, 0, 0, 4);
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.cellsFilled, 3);

    // Should continue pattern: 3, 4, 5
    EXPECT_DOUBLE_EQ(getCellNumber(0, 2), 3.0);
    EXPECT_DOUBLE_EQ(getCellNumber(0, 3), 4.0);
    EXPECT_DOUBLE_EQ(getCellNumber(0, 4), 5.0);
}

TEST_F(OperationSequenceTest, FillRightLinearSequence) {
    // Set up: A1=10, B1=20
    setCellValue(0, 0, 10.0);
    setCellValue(1, 0, 20.0);

    // Fill right to E1
    FillResult result = fillRange(workbook.get(), sheet, 0, 0, 1, 0, 0, 0, 4, 0);
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.cellsFilled, 3);

    // Should continue pattern: 30, 40, 50
    EXPECT_DOUBLE_EQ(getCellNumber(2, 0), 30.0);
    EXPECT_DOUBLE_EQ(getCellNumber(3, 0), 40.0);
    EXPECT_DOUBLE_EQ(getCellNumber(4, 0), 50.0);
}

TEST_F(OperationSequenceTest, FillDownConstant) {
    // Set up: A1=42 (single value = constant fill)
    setCellValue(0, 0, 42.0);

    // Fill down to A4
    FillResult result = fillRange(workbook.get(), sheet, 0, 0, 0, 0, 0, 0, 0, 3);
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.cellsFilled, 3);

    // All should be 42
    EXPECT_DOUBLE_EQ(getCellNumber(0, 1), 42.0);
    EXPECT_DOUBLE_EQ(getCellNumber(0, 2), 42.0);
    EXPECT_DOUBLE_EQ(getCellNumber(0, 3), 42.0);
}

TEST_F(OperationSequenceTest, FillDownMultipleColumns) {
    // Set up two columns with different patterns
    setCellValue(0, 0, 1.0);
    setCellValue(0, 1, 2.0);
    setCellValue(1, 0, 10.0);
    setCellValue(1, 1, 20.0);

    // Fill both columns down
    FillResult result = fillRange(workbook.get(), sheet, 0, 0, 1, 1, 0, 0, 1, 3);
    EXPECT_TRUE(result.success);

    // Column A: 3, 4
    EXPECT_DOUBLE_EQ(getCellNumber(0, 2), 3.0);
    EXPECT_DOUBLE_EQ(getCellNumber(0, 3), 4.0);

    // Column B: 30, 40
    EXPECT_DOUBLE_EQ(getCellNumber(1, 2), 30.0);
    EXPECT_DOUBLE_EQ(getCellNumber(1, 3), 40.0);
}

TEST_F(OperationSequenceTest, FillDownFormulaSequence) {
    // Set up: A1=1, B1=A1
    setCellValue(0, 0, 1.0);
    setCellFormula(1, 0, "=A1");

    // Fill B1 down to B3
    FillResult result = fillRange(workbook.get(), sheet, 1, 0, 1, 0, 1, 0, 1, 2);
    EXPECT_TRUE(result.success);

    // B2=A2, B3=A3
    EXPECT_EQ(getCellFormulaDisplay(1, 1), "=A2");
    EXPECT_EQ(getCellFormulaDisplay(1, 2), "=A3");
}

TEST_F(OperationSequenceTest, FillUpSequence) {
    // Set up: A4=1, A5=2 (at positions 3 and 4)
    setCellValue(0, 3, 1.0);
    setCellValue(0, 4, 2.0);

    // Fill up to A1
    FillResult result = fillRange(workbook.get(), sheet, 0, 3, 0, 4, 0, 0, 0, 4);
    EXPECT_TRUE(result.success);

    // Should extrapolate backwards: 0, -1, -2
    EXPECT_DOUBLE_EQ(getCellNumber(0, 2), 0.0);
    EXPECT_DOUBLE_EQ(getCellNumber(0, 1), -1.0);
    EXPECT_DOUBLE_EQ(getCellNumber(0, 0), -2.0);
}

TEST_F(OperationSequenceTest, SequentialFillOperations) {
    // First fill down a column
    setCellValue(0, 0, 1.0);
    setCellValue(0, 1, 2.0);

    FillResult r1 = fillRange(workbook.get(), sheet, 0, 0, 0, 1, 0, 0, 0, 4);
    EXPECT_TRUE(r1.success);

    // Then fill right using those values as source
    FillResult r2 = fillRange(workbook.get(), sheet, 0, 0, 0, 4, 0, 0, 4, 4);
    EXPECT_TRUE(r2.success);

    // All columns should have the same values as column A
    for (uint32_t col = 1; col < 5; col++) {
        EXPECT_DOUBLE_EQ(getCellNumber(col, 0), 1.0);
        EXPECT_DOUBLE_EQ(getCellNumber(col, 1), 2.0);
        EXPECT_DOUBLE_EQ(getCellNumber(col, 2), 3.0);
        EXPECT_DOUBLE_EQ(getCellNumber(col, 3), 4.0);
        EXPECT_DOUBLE_EQ(getCellNumber(col, 4), 5.0);
    }
}

TEST_F(OperationSequenceTest, FillWithFormulaAndEvaluate) {
    // Set up: A1=5, B1=A1*2
    setCellValue(0, 0, 5.0);
    setCellFormula(1, 0, "=A1*2");
    evaluateCell(1, 0);
    EXPECT_DOUBLE_EQ(getCellNumber(1, 0), 10.0);

    // Fill both columns down
    FillResult r1 = fillRange(workbook.get(), sheet, 0, 0, 0, 0, 0, 0, 0, 2);
    FillResult r2 = fillRange(workbook.get(), sheet, 1, 0, 1, 0, 1, 0, 1, 2);
    EXPECT_TRUE(r1.success);
    EXPECT_TRUE(r2.success);

    // Set values in A2, A3 and evaluate formulas
    setCellValue(0, 1, 10.0);
    setCellValue(0, 2, 15.0);

    evaluateCell(1, 1);
    evaluateCell(1, 2);

    // B2 = A2*2 = 20, B3 = A3*2 = 30
    EXPECT_DOUBLE_EQ(getCellNumber(1, 1), 20.0);
    EXPECT_DOUBLE_EQ(getCellNumber(1, 2), 30.0);
}

}  // namespace
}  // namespace cells
