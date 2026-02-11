// =============================================================================
// Edge Cases and Boundary Conditions Unit Tests
// =============================================================================
//
// Phase 14 of Comprehensive Unit Test Coverage Plan
//
// Tests edge cases and boundary conditions including:
// - Operations on first column/row (position 0)
// - Operations at large positions
// - Empty cell references in formulas
// - Very long text values
// - Very large and very small numbers
// - Deeply nested formulas
// - Ranges spanning entire columns/rows
//
// =============================================================================

#include <cfloat>
#include <cmath>

#include <limits>
#include <memory>
#include <string>
#include <unordered_set>

#include "core/cells/crdt.h"
#include "core/cells/dependency_graph.h"
#include "core/cells/formula_display.h"
#include "core/cells/formula_eval.h"
#include "core/cells/formula_parser.h"
#include "core/cells/formula_recalc.h"
#include "core/cells/formula_resolver.h"
#include "core/cells/id.h"
#include "core/cells/model.h"
#include "core/cells/named_ranges.h"
#include "core/cells/number_format.h"
#include "core/cells/style_buffer.h"

#include "gtest/gtest.h"

namespace cells {
namespace {

// =============================================================================
// Test Fixture for Edge Cases Tests
// =============================================================================

class EdgeCasesTest : public ::testing::Test {
protected:
    void SetUp() override {
        workbook = std::make_unique<Workbook>(generate_id(), "Test");
        workbook->setNodeId(generate_id());
        workbook->addSheet(std::make_unique<Sheet>(generate_id(), "Sheet1"));
        sheet = workbook->getSheetByIndex(0);
        sheet->setWorkbook(workbook.get());

        // Create columns A-Z (positions 0-25)
        for (uint32_t i = 0; i < 26; i++) {
            auto col = std::make_unique<Axis>(generate_id(), true);
            col->position = i;
            col->name = Sheet::positionToColumnName(i);
            colIds[i] = col->id;
            sheet->addColumn(std::move(col));
        }

        // Create rows 1-100 (positions 0-99)
        for (uint32_t i = 0; i < 100; i++) {
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

    Cell* setCellBool(uint32_t col, uint32_t row, bool value) {
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

        FormulaResolver resolver(*workbook, *sheet, workbook->getNamedRanges());
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

    // Parse and evaluate a formula directly, returning the result
    EvalResult eval(const std::string& formula) {
        FormulaParser parser(formula);
        auto ast = parser.parse();
        if (!ast || parser.hasErrors()) {
            return EvalResult::Error(CellError::VALUE);
        }

        FormulaResolver resolver(*workbook, *sheet, workbook->getNamedRanges());
        createRequiredEntities(resolver, ast.get());
        resolver.resolve(ast.get());

        std::unordered_set<ID> evaluating;
        EvalContext ctx;
        ctx.sheet = sheet;
        ctx.workbook = workbook.get();
        ctx.namedRanges = workbook->getNamedRanges();
        ctx.evaluatingCells = &evaluating;
        ctx.recursionDepth = 0;

        return evaluate(ast.get(), ctx);
    }

    std::unique_ptr<Workbook> workbook;
    Sheet* sheet;
    ID colIds[26];
    ID rowIds[100];
};

// =============================================================================
// 14a: Test operations on first column (A / position 0)
// =============================================================================

TEST_F(EdgeCasesTest, FirstColumnSetValue) {
    // Set value in A1 (col 0, row 0)
    Cell* a1 = setCellValue(0, 0, 42.0);
    ASSERT_NE(a1, nullptr);
    EXPECT_DOUBLE_EQ(a1->value.asNumber(), 42.0);
}

TEST_F(EdgeCasesTest, FirstColumnFormula) {
    // A1 = 10, A2 = A1 + 5
    setCellValue(0, 0, 10.0);
    Cell* a2 = setCellFormula(0, 1, "=A1+5");
    ASSERT_NE(a2, nullptr);
    evaluateCell(0, 1);
    EXPECT_DOUBLE_EQ(a2->value.asNumber(), 15.0);
}

TEST_F(EdgeCasesTest, FirstColumnRangeSum) {
    // Set values in A1:A5 and sum them
    for (uint32_t i = 0; i < 5; i++) {
        setCellValue(0, i, static_cast<double>(i + 1));
    }

    Cell* a6 = setCellFormula(0, 5, "=SUM(A1:A5)");
    ASSERT_NE(a6, nullptr);
    evaluateCell(0, 5);
    EXPECT_DOUBLE_EQ(a6->value.asNumber(), 15.0);  // 1+2+3+4+5
}

TEST_F(EdgeCasesTest, FirstColumnDelete) {
    // Set value in first column
    setCellValue(0, 0, 100.0);
    ID colId = colIds[0];

    // Delete the first column
    bool deleted = sheet->deleteColumn(colId);
    EXPECT_TRUE(deleted);

    // Column should be removed
    EXPECT_EQ(sheet->getColumn(colId), nullptr);
}

TEST_F(EdgeCasesTest, FirstColumnInsertBefore) {
    workbook->startCollaboration();

    // Insert a column at position 0 (before current first column)
    ID newColId = generate_id();
    std::string payload = R"({"pos":0,"size":100})";

    Operation op = makeColSetOp(*workbook, newColId, sheet->id, payload);
    ApplyResult result = applyOperation(*workbook, op);
    EXPECT_EQ(result, ApplyResult::SUCCESS);

    // New column should exist at position 0
    Axis* newCol = sheet->getColumn(newColId);
    ASSERT_NE(newCol, nullptr);
    EXPECT_EQ(newCol->position, 0);
}

TEST_F(EdgeCasesTest, FirstColumnStyleApplication) {
    workbook->startCollaboration();

    Cell* a1 = setCellValue(0, 0, 42.0);

    // Apply style to first column cell
    StyleBuffer styleBuf;
    styleBuf.setBold(true);
    Operation op = makeCellSetStyleOp(*workbook, a1->id, styleBuf);
    EXPECT_EQ(applyOperation(*workbook, op), ApplyResult::SUCCESS);

    const StyleBuffer* style = workbook->getEntityStyle(a1->id);
    ASSERT_NE(style, nullptr);
    EXPECT_TRUE(style->getBold());
}

TEST_F(EdgeCasesTest, FirstColumnFormulaReferenceFromOther) {
    // B1 references A1 (first column)
    setCellValue(0, 0, 50.0);
    Cell* b1 = setCellFormula(1, 0, "=A1*2");
    ASSERT_NE(b1, nullptr);
    evaluateCell(1, 0);
    EXPECT_DOUBLE_EQ(b1->value.asNumber(), 100.0);
}

// =============================================================================
// 14b: Test operations on first row (1 / position 0)
// =============================================================================

TEST_F(EdgeCasesTest, FirstRowSetValue) {
    // Set value in A1 (col 0, row 0)
    Cell* a1 = setCellValue(0, 0, 123.0);
    ASSERT_NE(a1, nullptr);
    EXPECT_DOUBLE_EQ(a1->value.asNumber(), 123.0);
}

TEST_F(EdgeCasesTest, FirstRowFormula) {
    // A1 = 5, B1 = A1 + 10
    setCellValue(0, 0, 5.0);
    Cell* b1 = setCellFormula(1, 0, "=A1+10");
    ASSERT_NE(b1, nullptr);
    evaluateCell(1, 0);
    EXPECT_DOUBLE_EQ(b1->value.asNumber(), 15.0);
}

TEST_F(EdgeCasesTest, FirstRowRangeSum) {
    // Set values in A1:E1 and sum them
    for (uint32_t i = 0; i < 5; i++) {
        setCellValue(i, 0, static_cast<double>(i + 1));
    }

    Cell* f1 = setCellFormula(5, 0, "=SUM(A1:E1)");
    ASSERT_NE(f1, nullptr);
    evaluateCell(5, 0);
    EXPECT_DOUBLE_EQ(f1->value.asNumber(), 15.0);  // 1+2+3+4+5
}

TEST_F(EdgeCasesTest, FirstRowDelete) {
    // Set value in first row
    setCellValue(0, 0, 100.0);
    ID rowId = rowIds[0];

    // Delete the first row
    bool deleted = sheet->deleteRow(rowId);
    EXPECT_TRUE(deleted);

    // Row should be removed
    EXPECT_EQ(sheet->getRow(rowId), nullptr);
}

TEST_F(EdgeCasesTest, FirstRowInsertBefore) {
    workbook->startCollaboration();

    // Insert a row at position 0 (before current first row)
    ID newRowId = generate_id();
    std::string payload = R"({"pos":0,"size":25})";

    Operation op = makeRowSetOp(*workbook, newRowId, sheet->id, payload);
    ApplyResult result = applyOperation(*workbook, op);
    EXPECT_EQ(result, ApplyResult::SUCCESS);

    // New row should exist at position 0
    Axis* newRow = sheet->getRow(newRowId);
    ASSERT_NE(newRow, nullptr);
    EXPECT_EQ(newRow->position, 0);
}

TEST_F(EdgeCasesTest, FirstRowStyleApplication) {
    workbook->startCollaboration();

    Cell* a1 = setCellValue(0, 0, 42.0);

    // Apply style to first row cell
    StyleBuffer styleBuf;
    styleBuf.setItalic(true);
    Operation op = makeCellSetStyleOp(*workbook, a1->id, styleBuf);
    EXPECT_EQ(applyOperation(*workbook, op), ApplyResult::SUCCESS);

    const StyleBuffer* style = workbook->getEntityStyle(a1->id);
    ASSERT_NE(style, nullptr);
    EXPECT_TRUE(style->getItalic());
}

TEST_F(EdgeCasesTest, FirstRowFormulaReferenceFromOther) {
    // A2 references A1 (first row)
    setCellValue(0, 0, 25.0);
    Cell* a2 = setCellFormula(0, 1, "=A1*4");
    ASSERT_NE(a2, nullptr);
    evaluateCell(0, 1);
    EXPECT_DOUBLE_EQ(a2->value.asNumber(), 100.0);
}

// =============================================================================
// 14c: Test operations at large positions
// =============================================================================

class LargePositionTest : public ::testing::Test {
protected:
    void SetUp() override {
        workbook = std::make_unique<Workbook>(generate_id(), "Test");
        workbook->setNodeId(generate_id());
        workbook->addSheet(std::make_unique<Sheet>(generate_id(), "Sheet1"));
        sheet = workbook->getSheetByIndex(0);
        sheet->setWorkbook(workbook.get());
    }

    std::unique_ptr<Workbook> workbook;
    Sheet* sheet;
};

TEST_F(LargePositionTest, LargeColumnPosition) {
    workbook->startCollaboration();

    // Create column at position 1000 (which would be column ALL in Excel)
    ID colId = generate_id();
    std::string payload = R"({"pos":1000,"size":100})";

    Operation op = makeColSetOp(*workbook, colId, sheet->id, payload);
    ApplyResult result = applyOperation(*workbook, op);
    EXPECT_EQ(result, ApplyResult::SUCCESS);

    Axis* col = sheet->getColumn(colId);
    ASSERT_NE(col, nullptr);
    EXPECT_EQ(col->position, 1000);
}

TEST_F(LargePositionTest, LargeRowPosition) {
    workbook->startCollaboration();

    // Create row at position 10000
    ID rowId = generate_id();
    std::string payload = R"({"pos":10000,"size":25})";

    Operation op = makeRowSetOp(*workbook, rowId, sheet->id, payload);
    ApplyResult result = applyOperation(*workbook, op);
    EXPECT_EQ(result, ApplyResult::SUCCESS);

    Axis* row = sheet->getRow(rowId);
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(row->position, 10000);
}

TEST_F(LargePositionTest, CellAtLargePosition) {
    workbook->startCollaboration();

    // Create column and row at large positions
    ID colId = generate_id();
    ID rowId = generate_id();

    std::string colPayload = R"({"pos":500,"size":100})";
    std::string rowPayload = R"({"pos":5000,"size":25})";

    applyOperation(*workbook, makeColSetOp(*workbook, colId, sheet->id, colPayload));
    applyOperation(*workbook, makeRowSetOp(*workbook, rowId, sheet->id, rowPayload));

    // Create cell at intersection
    Cell* cell = sheet->getOrCreateCellAt(colId, rowId);
    cell->value = CellValue(999.0);

    EXPECT_DOUBLE_EQ(cell->value.asNumber(), 999.0);
}

TEST_F(LargePositionTest, ColumnNameConversionLargePosition) {
    // Test column name conversion for large positions
    // Position 0 = A, 25 = Z, 26 = AA, 701 = ZZ, 702 = AAA

    EXPECT_EQ(Sheet::positionToColumnName(0), "A");
    EXPECT_EQ(Sheet::positionToColumnName(25), "Z");
    EXPECT_EQ(Sheet::positionToColumnName(26), "AA");
    EXPECT_EQ(Sheet::positionToColumnName(27), "AB");
    EXPECT_EQ(Sheet::positionToColumnName(51), "AZ");
    EXPECT_EQ(Sheet::positionToColumnName(52), "BA");
    EXPECT_EQ(Sheet::positionToColumnName(701), "ZZ");
    EXPECT_EQ(Sheet::positionToColumnName(702), "AAA");
}

TEST_F(LargePositionTest, ColumnPositionFromLargeName) {
    // Reverse conversion
    EXPECT_EQ(Sheet::columnNameToPosition("A"), 0);
    EXPECT_EQ(Sheet::columnNameToPosition("Z"), 25);
    EXPECT_EQ(Sheet::columnNameToPosition("AA"), 26);
    EXPECT_EQ(Sheet::columnNameToPosition("AB"), 27);
    EXPECT_EQ(Sheet::columnNameToPosition("AZ"), 51);
    EXPECT_EQ(Sheet::columnNameToPosition("BA"), 52);
    EXPECT_EQ(Sheet::columnNameToPosition("ZZ"), 701);
    EXPECT_EQ(Sheet::columnNameToPosition("AAA"), 702);
}

TEST_F(LargePositionTest, VeryLargeColumnPosition) {
    workbook->startCollaboration();

    // Test position at uint32_t boundary (but reasonable for spreadsheets)
    // Position 16383 = XFD (Excel's max column)
    ID colId = generate_id();
    std::string payload = R"({"pos":16383,"size":100})";

    Operation op = makeColSetOp(*workbook, colId, sheet->id, payload);
    ApplyResult result = applyOperation(*workbook, op);
    EXPECT_EQ(result, ApplyResult::SUCCESS);

    Axis* col = sheet->getColumn(colId);
    ASSERT_NE(col, nullptr);
    EXPECT_EQ(col->position, 16383);
}

TEST_F(LargePositionTest, VeryLargeRowPosition) {
    workbook->startCollaboration();

    // Test large row position (1 million rows)
    ID rowId = generate_id();
    std::string payload = R"({"pos":1000000,"size":25})";

    Operation op = makeRowSetOp(*workbook, rowId, sheet->id, payload);
    ApplyResult result = applyOperation(*workbook, op);
    EXPECT_EQ(result, ApplyResult::SUCCESS);

    Axis* row = sheet->getRow(rowId);
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(row->position, 1000000);
}

// =============================================================================
// 14d: Test empty cell references in formulas
// =============================================================================

TEST_F(EdgeCasesTest, EmptyCellReferenceReturnsZero) {
    // Reference to empty cell in arithmetic should return 0
    // A1 is empty, B1 = A1 + 5
    Cell* b1 = setCellFormula(1, 0, "=A1+5");
    ASSERT_NE(b1, nullptr);
    evaluateCell(1, 0);
    EXPECT_DOUBLE_EQ(b1->value.asNumber(), 5.0);  // 0 + 5
}

TEST_F(EdgeCasesTest, EmptyCellInSum) {
    // SUM should treat empty cells as 0
    setCellValue(0, 0, 1.0);
    // A2 is empty
    setCellValue(0, 2, 3.0);

    Cell* a4 = setCellFormula(0, 3, "=SUM(A1:A3)");
    ASSERT_NE(a4, nullptr);
    evaluateCell(0, 3);
    EXPECT_DOUBLE_EQ(a4->value.asNumber(), 4.0);  // 1 + 0 + 3
}

TEST_F(EdgeCasesTest, EmptyCellInAverage) {
    // AVERAGE should skip empty cells
    setCellValue(0, 0, 10.0);
    // A2 is empty
    setCellValue(0, 2, 20.0);

    Cell* a4 = setCellFormula(0, 3, "=AVERAGE(A1:A3)");
    ASSERT_NE(a4, nullptr);
    evaluateCell(0, 3);
    EXPECT_DOUBLE_EQ(a4->value.asNumber(), 15.0);  // (10 + 20) / 2, empty skipped
}

TEST_F(EdgeCasesTest, EmptyCellInCount) {
    // COUNT should not count empty cells
    setCellValue(0, 0, 1.0);
    // A2 is empty
    setCellValue(0, 2, 3.0);

    Cell* a4 = setCellFormula(0, 3, "=COUNT(A1:A3)");
    ASSERT_NE(a4, nullptr);
    evaluateCell(0, 3);
    EXPECT_DOUBLE_EQ(a4->value.asNumber(), 2.0);  // Only A1 and A3 counted
}

TEST_F(EdgeCasesTest, EmptyCellInCountA) {
    // COUNTA counts non-empty cells including text
    setCellValue(0, 0, 1.0);
    // A2 is empty
    setCellValue(0, 2, "text");

    Cell* a4 = setCellFormula(0, 3, "=COUNTA(A1:A3)");
    ASSERT_NE(a4, nullptr);
    evaluateCell(0, 3);
    EXPECT_DOUBLE_EQ(a4->value.asNumber(), 2.0);  // A1 and A3 counted
}

TEST_F(EdgeCasesTest, EmptyCellInMultiplication) {
    // Empty cell treated as 0 in multiplication
    // A1 is empty, B1 = A1 * 10
    Cell* b1 = setCellFormula(1, 0, "=A1*10");
    ASSERT_NE(b1, nullptr);
    evaluateCell(1, 0);
    EXPECT_DOUBLE_EQ(b1->value.asNumber(), 0.0);  // 0 * 10
}

TEST_F(EdgeCasesTest, EmptyCellInDivision) {
    // Division by empty cell (treated as 0) should give #DIV/0!
    setCellValue(0, 0, 10.0);
    // B1 is empty
    Cell* c1 = setCellFormula(2, 0, "=A1/B1");
    ASSERT_NE(c1, nullptr);
    evaluateCell(2, 0);
    EXPECT_TRUE(cellHasError(2, 0, CellError::DIV));
}

TEST_F(EdgeCasesTest, EmptyCellInConcat) {
    // Concatenation with empty cell
    setCellValue(0, 0, "Hello");
    // A2 is empty

    Cell* a3 = setCellFormula(0, 2, "=A1&A2");
    ASSERT_NE(a3, nullptr);
    evaluateCell(0, 2);
    EXPECT_EQ(a3->value.asString(), "Hello");  // Empty treated as empty string
}

TEST_F(EdgeCasesTest, EmptyCellInIf) {
    // IF with empty cell condition
    // A1 is empty (treated as FALSE/0)
    Cell* b1 = setCellFormula(1, 0, "=IF(A1,1,2)");
    ASSERT_NE(b1, nullptr);
    evaluateCell(1, 0);
    EXPECT_DOUBLE_EQ(b1->value.asNumber(), 2.0);  // Empty is falsy
}

// =============================================================================
// 14e: Test very long text values
// =============================================================================

TEST_F(EdgeCasesTest, LongTextStorage) {
    // Create a long text string
    std::string longText(10000, 'A');  // 10,000 character string
    Cell* a1 = setCellValue(0, 0, longText);
    ASSERT_NE(a1, nullptr);
    EXPECT_EQ(a1->value.asString(), longText);
}

TEST_F(EdgeCasesTest, LongTextConcatenation) {
    std::string text1(5000, 'A');
    std::string text2(5000, 'B');
    setCellValue(0, 0, text1);
    setCellValue(0, 1, text2);

    Cell* a3 = setCellFormula(0, 2, "=A1&A2");
    ASSERT_NE(a3, nullptr);
    evaluateCell(0, 2);

    std::string expected = text1 + text2;
    EXPECT_EQ(a3->value.asString(), expected);
    EXPECT_EQ(a3->value.asString().length(), 10000u);
}

TEST_F(EdgeCasesTest, LongTextLen) {
    std::string longText(1000, 'X');
    setCellValue(0, 0, longText);

    Cell* b1 = setCellFormula(1, 0, "=LEN(A1)");
    ASSERT_NE(b1, nullptr);
    evaluateCell(1, 0);
    EXPECT_DOUBLE_EQ(b1->value.asNumber(), 1000.0);
}

TEST_F(EdgeCasesTest, LongTextLeft) {
    std::string longText(1000, 'X');
    setCellValue(0, 0, longText);

    Cell* b1 = setCellFormula(1, 0, "=LEFT(A1,100)");
    ASSERT_NE(b1, nullptr);
    evaluateCell(1, 0);
    EXPECT_EQ(b1->value.asString().length(), 100u);
}

TEST_F(EdgeCasesTest, LongTextRight) {
    std::string longText(1000, 'X');
    setCellValue(0, 0, longText);

    Cell* b1 = setCellFormula(1, 0, "=RIGHT(A1,100)");
    ASSERT_NE(b1, nullptr);
    evaluateCell(1, 0);
    EXPECT_EQ(b1->value.asString().length(), 100u);
}

TEST_F(EdgeCasesTest, LongTextMid) {
    // Create string with distinguishable characters
    std::string longText;
    for (int i = 0; i < 1000; i++) {
        longText += static_cast<char>('A' + (i % 26));
    }
    setCellValue(0, 0, longText);

    Cell* b1 = setCellFormula(1, 0, "=MID(A1,500,10)");
    ASSERT_NE(b1, nullptr);
    evaluateCell(1, 0);
    EXPECT_EQ(b1->value.asString().length(), 10u);
}

TEST_F(EdgeCasesTest, UnicodeText) {
    // Test Unicode characters
    std::string unicodeText = "Hello 世界 🌍";
    Cell* a1 = setCellValue(0, 0, unicodeText);
    ASSERT_NE(a1, nullptr);
    EXPECT_EQ(a1->value.asString(), unicodeText);
}

TEST_F(EdgeCasesTest, EmptyString) {
    Cell* a1 = setCellValue(0, 0, "");
    ASSERT_NE(a1, nullptr);
    EXPECT_EQ(a1->value.asString(), "");
    EXPECT_EQ(a1->value.type, CellValueType::STRING);
}

// =============================================================================
// 14f: Test very large and very small numbers
// =============================================================================

TEST_F(EdgeCasesTest, LargeNumber) {
    // Test large number storage
    double largeNum = 1e100;
    Cell* a1 = setCellValue(0, 0, largeNum);
    ASSERT_NE(a1, nullptr);
    EXPECT_DOUBLE_EQ(a1->value.asNumber(), largeNum);
}

TEST_F(EdgeCasesTest, SmallNumber) {
    // Test very small number - should preserve full double precision
    double smallNum = 1e-100;
    Cell* a1 = setCellValue(0, 0, smallNum);
    ASSERT_NE(a1, nullptr);
    EXPECT_DOUBLE_EQ(a1->value.asNumber(), smallNum);
}

TEST_F(EdgeCasesTest, NegativeLargeNumber) {
    double negLarge = -1e100;
    Cell* a1 = setCellValue(0, 0, negLarge);
    ASSERT_NE(a1, nullptr);
    EXPECT_DOUBLE_EQ(a1->value.asNumber(), negLarge);
}

TEST_F(EdgeCasesTest, MaxDouble) {
    double maxVal = DBL_MAX;
    Cell* a1 = setCellValue(0, 0, maxVal);
    ASSERT_NE(a1, nullptr);
    EXPECT_DOUBLE_EQ(a1->value.asNumber(), maxVal);
}

TEST_F(EdgeCasesTest, MinPositiveDouble) {
    // Test DBL_MIN - should preserve full double precision
    double minVal = DBL_MIN;
    Cell* a1 = setCellValue(0, 0, minVal);
    ASSERT_NE(a1, nullptr);
    EXPECT_DOUBLE_EQ(a1->value.asNumber(), minVal);
}

TEST_F(EdgeCasesTest, LargeNumberArithmetic) {
    setCellValue(0, 0, 1e50);
    setCellValue(0, 1, 2e50);

    Cell* a3 = setCellFormula(0, 2, "=A1+A2");
    ASSERT_NE(a3, nullptr);
    evaluateCell(0, 2);
    EXPECT_DOUBLE_EQ(a3->value.asNumber(), 3e50);
}

TEST_F(EdgeCasesTest, SmallNumberArithmetic) {
    // Test arithmetic with very small numbers
    setCellValue(0, 0, 1e-50);
    setCellValue(0, 1, 2e-50);

    Cell* a3 = setCellFormula(0, 2, "=A1+A2");
    ASSERT_NE(a3, nullptr);
    evaluateCell(0, 2);
    EXPECT_DOUBLE_EQ(a3->value.asNumber(), 3e-50);
}

TEST_F(EdgeCasesTest, OverflowToNumError) {
    // Multiplying very large numbers should give #NUM! (Excel behavior)
    setCellValue(0, 0, 1e200);
    setCellValue(0, 1, 1e200);

    Cell* a3 = setCellFormula(0, 2, "=A1*A2");
    ASSERT_NE(a3, nullptr);
    evaluateCell(0, 2);
    EXPECT_TRUE(cellHasError(0, 2, CellError::NUM));
}

TEST_F(EdgeCasesTest, UnderflowToZero) {
    // Dividing very small numbers should give very small result (possibly 0)
    setCellValue(0, 0, 1e-300);
    setCellValue(0, 1, 1e300);

    Cell* a3 = setCellFormula(0, 2, "=A1/A2");
    ASSERT_NE(a3, nullptr);
    evaluateCell(0, 2);
    // Result should be 0 or very small
    EXPECT_TRUE(a3->value.asNumber() == 0.0 || std::abs(a3->value.asNumber()) < DBL_MIN);
}

TEST_F(EdgeCasesTest, NaNFromInvalidOperation) {
    // 0/0 should produce NaN or #DIV/0!
    setCellValue(0, 0, 0.0);
    setCellValue(0, 1, 0.0);

    Cell* a3 = setCellFormula(0, 2, "=A1/A2");
    ASSERT_NE(a3, nullptr);
    evaluateCell(0, 2);
    // Should be either NaN or an error
    EXPECT_TRUE(std::isnan(a3->value.asNumber()) || cellHasError(0, 2, CellError::DIV));
}

TEST_F(EdgeCasesTest, InfinityArithmetic) {
    // Test arithmetic with infinity — Excel returns #NUM!
    double inf = std::numeric_limits<double>::infinity();
    setCellValue(0, 0, inf);
    setCellValue(0, 1, 10.0);

    // Infinity + number = #NUM!
    Cell* a3 = setCellFormula(0, 2, "=A1+A2");
    ASSERT_NE(a3, nullptr);
    evaluateCell(0, 2);
    EXPECT_TRUE(cellHasError(0, 2, CellError::NUM));
}

TEST_F(EdgeCasesTest, NegativeZero) {
    double negZero = -0.0;
    Cell* a1 = setCellValue(0, 0, negZero);
    ASSERT_NE(a1, nullptr);
    // -0.0 == 0.0 in IEEE 754
    EXPECT_DOUBLE_EQ(a1->value.asNumber(), 0.0);
}

// =============================================================================
// 14g: Test formula with maximum nesting depth
// =============================================================================

TEST_F(EdgeCasesTest, DeeplyNestedParentheses) {
    // Test formula with deeply nested parentheses
    // (((((...((1))...))))
    std::string formula = "=";
    int depth = 50;
    for (int i = 0; i < depth; i++) {
        formula += "(";
    }
    formula += "1+2";
    for (int i = 0; i < depth; i++) {
        formula += ")";
    }

    Cell* a1 = setCellFormula(0, 0, formula);
    ASSERT_NE(a1, nullptr);
    evaluateCell(0, 0);
    EXPECT_DOUBLE_EQ(a1->value.asNumber(), 3.0);
}

TEST_F(EdgeCasesTest, DeeplyNestedFunctions) {
    // Test nested function calls: SUM(SUM(SUM(...)))
    std::string formula = "=";
    int depth = 20;
    for (int i = 0; i < depth; i++) {
        formula += "SUM(";
    }
    formula += "1,2";
    for (int i = 0; i < depth; i++) {
        formula += ")";
    }

    Cell* a1 = setCellFormula(0, 0, formula);
    ASSERT_NE(a1, nullptr);
    evaluateCell(0, 0);
    EXPECT_DOUBLE_EQ(a1->value.asNumber(), 3.0);
}

TEST_F(EdgeCasesTest, NestedIfStatements) {
    // Test nested IF: IF(1, IF(1, IF(1, ...)))
    std::string formula = "=";
    int depth = 10;
    for (int i = 0; i < depth; i++) {
        formula += "IF(TRUE,";
    }
    formula += "42";
    for (int i = 0; i < depth; i++) {
        formula += ",0)";
    }

    Cell* a1 = setCellFormula(0, 0, formula);
    ASSERT_NE(a1, nullptr);
    evaluateCell(0, 0);
    EXPECT_DOUBLE_EQ(a1->value.asNumber(), 42.0);
}

TEST_F(EdgeCasesTest, LongFormulaChain) {
    // Create a chain of cell references: A1=1, A2=A1+1, A3=A2+1, ...
    setCellValue(0, 0, 1.0);

    for (uint32_t i = 1; i < 50; i++) {
        std::string formula = "=A" + std::to_string(i) + "+1";
        Cell* cell = setCellFormula(0, i, formula);
        ASSERT_NE(cell, nullptr);
    }

    // Evaluate the chain in order
    for (uint32_t i = 1; i < 50; i++) {
        evaluateCell(0, i);
    }

    // A50 should be 50
    EXPECT_DOUBLE_EQ(getCellNumber(0, 49), 50.0);
}

TEST_F(EdgeCasesTest, ComplexArithmeticExpression) {
    // Test complex arithmetic: 1+2*3-4/2+5*(6-7)/8
    Cell* a1 = setCellFormula(0, 0, "=1+2*3-4/2+5*(6-7)/8");
    ASSERT_NE(a1, nullptr);
    evaluateCell(0, 0);
    // 1 + 6 - 2 + 5*(-1)/8 = 1 + 6 - 2 - 0.625 = 4.375
    EXPECT_NEAR(a1->value.asNumber(), 4.375, 0.0001);
}

TEST_F(EdgeCasesTest, MultipleFunctionArguments) {
    // Test function with many arguments
    setCellValue(0, 0, 1.0);
    setCellValue(0, 1, 2.0);
    setCellValue(0, 2, 3.0);
    setCellValue(0, 3, 4.0);
    setCellValue(0, 4, 5.0);

    Cell* b1 = setCellFormula(1, 0, "=SUM(A1,A2,A3,A4,A5)");
    ASSERT_NE(b1, nullptr);
    evaluateCell(1, 0);
    EXPECT_DOUBLE_EQ(b1->value.asNumber(), 15.0);
}

// =============================================================================
// 14h: Test range spanning entire columns/rows
// =============================================================================

TEST_F(EdgeCasesTest, SumEntireColumn) {
    // Put values in different rows of column A
    setCellValue(0, 0, 10.0);
    setCellValue(0, 10, 20.0);
    setCellValue(0, 50, 30.0);

    // Sum all values in column A up to row 99
    Cell* result = setCellFormula(1, 0, "=SUM(A1:A100)");
    ASSERT_NE(result, nullptr);
    evaluateCell(1, 0);
    EXPECT_DOUBLE_EQ(result->value.asNumber(), 60.0);
}

TEST_F(EdgeCasesTest, SumEntireRow) {
    // Put values in different columns of row 1
    setCellValue(0, 0, 5.0);
    setCellValue(10, 0, 15.0);
    setCellValue(20, 0, 25.0);

    // Sum all values in row 1 up to column Z
    Cell* result = setCellFormula(25, 0, "=SUM(A1:Y1)");
    ASSERT_NE(result, nullptr);
    evaluateCell(25, 0);
    EXPECT_DOUBLE_EQ(result->value.asNumber(), 45.0);
}

TEST_F(EdgeCasesTest, CountEntireColumn) {
    // Put numbers in some cells of column A
    setCellValue(0, 0, 1.0);
    setCellValue(0, 5, 2.0);
    setCellValue(0, 20, 3.0);
    setCellValue(0, 50, "text");  // Text shouldn't be counted by COUNT

    Cell* result = setCellFormula(1, 0, "=COUNT(A1:A100)");
    ASSERT_NE(result, nullptr);
    evaluateCell(1, 0);
    EXPECT_DOUBLE_EQ(result->value.asNumber(), 3.0);  // Only numeric cells
}

TEST_F(EdgeCasesTest, MaxInLargeRange) {
    // Find max in a large range
    setCellValue(0, 0, 10.0);
    setCellValue(0, 50, 100.0);  // This should be the max
    setCellValue(0, 99, 50.0);

    Cell* result = setCellFormula(1, 0, "=MAX(A1:A100)");
    ASSERT_NE(result, nullptr);
    evaluateCell(1, 0);
    EXPECT_DOUBLE_EQ(result->value.asNumber(), 100.0);
}

TEST_F(EdgeCasesTest, MinInLargeRange) {
    // Find min in a large range
    setCellValue(0, 0, 10.0);
    setCellValue(0, 50, -5.0);  // This should be the min
    setCellValue(0, 99, 50.0);

    Cell* result = setCellFormula(1, 0, "=MIN(A1:A100)");
    ASSERT_NE(result, nullptr);
    evaluateCell(1, 0);
    EXPECT_DOUBLE_EQ(result->value.asNumber(), -5.0);
}

TEST_F(EdgeCasesTest, AverageEntireColumn) {
    // Average of sparse column
    setCellValue(0, 0, 10.0);
    setCellValue(0, 25, 20.0);
    setCellValue(0, 75, 30.0);

    Cell* result = setCellFormula(1, 0, "=AVERAGE(A1:A100)");
    ASSERT_NE(result, nullptr);
    evaluateCell(1, 0);
    EXPECT_DOUBLE_EQ(result->value.asNumber(), 20.0);  // (10+20+30)/3
}

TEST_F(EdgeCasesTest, RangeCrossesMultipleColumnsAndRows) {
    // Create a 5x5 grid of values
    for (uint32_t c = 0; c < 5; c++) {
        for (uint32_t r = 0; r < 5; r++) {
            setCellValue(c, r, static_cast<double>(c * 5 + r + 1));
        }
    }

    // Sum the entire grid
    Cell* result = setCellFormula(5, 0, "=SUM(A1:E5)");
    ASSERT_NE(result, nullptr);
    evaluateCell(5, 0);
    // Sum of 1 to 25 = 325
    EXPECT_DOUBLE_EQ(result->value.asNumber(), 325.0);
}

TEST_F(EdgeCasesTest, EmptyLargeRange) {
    // SUM of completely empty range should be 0
    Cell* result = setCellFormula(0, 0, "=SUM(A1:A100)");
    ASSERT_NE(result, nullptr);
    evaluateCell(0, 0);
    EXPECT_DOUBLE_EQ(result->value.asNumber(), 0.0);
}

}  // namespace
}  // namespace cells
