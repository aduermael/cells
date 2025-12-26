#include "core/cells/formula_recalc.h"

#include <cmath>

#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "core/cells/dependency_graph.h"
#include "core/cells/formula_ast.h"
#include "core/cells/formula_eval.h"
#include "core/cells/formula_parser.h"
#include "core/cells/formula_resolver.h"
#include "core/cells/id.h"
#include "core/cells/model.h"

#include "gtest/gtest.h"

namespace cells {
namespace {

// Helper class for recalculation tests
class FormulaRecalcTest : public ::testing::Test {
protected:
    void SetUp() override {
        workbook = std::make_unique<Workbook>(generate_id(), "Test");
        workbook->addSheet(std::make_unique<Sheet>(generate_id(), "Sheet1"));
        sheet = workbook->getSheetByIndex(0);

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

    Cell* setCellError(uint32_t col, uint32_t row, CellError error) {
        Cell* cell = sheet->getOrCreateCellAt(colIds[col], rowIds[row]);
        cell->value = CellValue(error);
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
        resolver.resolve(ast.get());

        auto result = sheet->setCellFormula(cell->id, formula, ast.release());
        if (!result.success) {
            return nullptr;
        }

        return cell;
    }

    // Get cell value as double (assumes it's a number)
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
        return cell->value.type == CellValueType::ERROR && cell->value.error == expectedError;
    }

    std::unique_ptr<Workbook> workbook;
    Sheet* sheet;
    ID colIds[26];
    ID rowIds[100];
};

// =============================================================================
// Single Cell Evaluation Tests
// =============================================================================

TEST_F(FormulaRecalcTest, EvaluateCellWithNoFormula) {
    // A1 = 42
    Cell* a1 = setCellValue(0, 0, 42.0);

    EvalResult result = evaluateCell(sheet, a1);
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 42.0);
}

TEST_F(FormulaRecalcTest, EvaluateCellWithFormula) {
    // A1 = 10, B1 = =A1*2
    setCellValue(0, 0, 10.0);
    Cell* b1 = setCellFormula(1, 0, "=A1*2");
    ASSERT_NE(b1, nullptr);

    EvalResult result = evaluateCell(sheet, b1);
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 20.0);
    EXPECT_DOUBLE_EQ(b1->value.asNumber(), 20.0);
}

TEST_F(FormulaRecalcTest, EvaluateCellById) {
    // A1 = 5
    Cell* a1 = setCellValue(0, 0, 5.0);

    EvalResult result = evaluateCell(sheet, a1->id);
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 5.0);
}

TEST_F(FormulaRecalcTest, EvaluateNonExistentCell) {
    EvalResult result = evaluateCell(sheet, ID("nonexistent"));
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 0.0);  // Empty cell = 0
}

TEST_F(FormulaRecalcTest, EvaluateCellMarksDirtyAsFalse) {
    setCellValue(0, 0, 10.0);
    Cell* b1 = setCellFormula(1, 0, "=A1+5");
    ASSERT_NE(b1, nullptr);

    // Formula should start dirty
    EXPECT_TRUE(b1->getFormula()->dirty);

    evaluateCell(sheet, b1);

    // Formula should be clean after evaluation
    EXPECT_FALSE(b1->getFormula()->dirty);
}

// =============================================================================
// Dependency Chain Tests
// =============================================================================

TEST_F(FormulaRecalcTest, DependencyChainSimple) {
    // A1=5, B1=A1*2, C1=B1+1
    Cell* a1 = setCellValue(0, 0, 5.0);
    setCellFormula(1, 0, "=A1*2");
    setCellFormula(2, 0, "=B1+1");

    // Recalculate after A1 changes
    recalculate(sheet, {a1->id});

    EXPECT_DOUBLE_EQ(getCellNumber(1, 0), 10.0);  // B1 = 5*2 = 10
    EXPECT_DOUBLE_EQ(getCellNumber(2, 0), 11.0);  // C1 = 10+1 = 11
}

TEST_F(FormulaRecalcTest, DependencyChainValueChange) {
    // A1=5, B1=A1*2, C1=B1+1
    Cell* a1 = setCellValue(0, 0, 5.0);
    setCellFormula(1, 0, "=A1*2");
    setCellFormula(2, 0, "=B1+1");
    recalculate(sheet, {a1->id});

    // Change A1 to 10
    a1->value = CellValue(10.0);
    recalculate(sheet, {a1->id});

    EXPECT_DOUBLE_EQ(getCellNumber(1, 0), 20.0);  // B1 = 10*2 = 20
    EXPECT_DOUBLE_EQ(getCellNumber(2, 0), 21.0);  // C1 = 20+1 = 21
}

TEST_F(FormulaRecalcTest, DiamondDependency) {
    // A1→B1, A1→C1, B1→D1, C1→D1 (D1 should only recalc once)
    // A1=5, B1=A1*2, C1=A1+3, D1=B1+C1
    Cell* a1 = setCellValue(0, 0, 5.0);
    setCellFormula(1, 0, "=A1*2");   // B1
    setCellFormula(2, 0, "=A1+3");   // C1
    setCellFormula(3, 0, "=B1+C1");  // D1

    recalculate(sheet, {a1->id});

    EXPECT_DOUBLE_EQ(getCellNumber(1, 0), 10.0);  // B1 = 5*2 = 10
    EXPECT_DOUBLE_EQ(getCellNumber(2, 0), 8.0);   // C1 = 5+3 = 8
    EXPECT_DOUBLE_EQ(getCellNumber(3, 0), 18.0);  // D1 = 10+8 = 18
}

TEST_F(FormulaRecalcTest, LongDependencyChain) {
    // A1→A2→A3→...→A10
    Cell* a1 = setCellValue(0, 0, 1.0);
    for (int i = 1; i < 10; i++) {
        std::string formula = "=A" + std::to_string(i) + "+1";
        setCellFormula(0, i, formula);
    }

    recalculate(sheet, {a1->id});

    // A1=1, A2=2, A3=3, ..., A10=10
    for (int i = 0; i < 10; i++) {
        EXPECT_DOUBLE_EQ(getCellNumber(0, i), static_cast<double>(i + 1))
            << "Failed at A" << (i + 1);
    }
}

TEST_F(FormulaRecalcTest, MultipleChangedCells) {
    // A1, A2, A3 all feed into B1
    setCellValue(0, 0, 10.0);  // A1
    setCellValue(0, 1, 20.0);  // A2
    setCellValue(0, 2, 30.0);  // A3
    setCellFormula(1, 0, "=A1+A2+A3");

    Cell* a1 = sheet->getCellAt(colIds[0], rowIds[0]);
    Cell* a2 = sheet->getCellAt(colIds[0], rowIds[1]);

    recalculate(sheet, {a1->id, a2->id});

    EXPECT_DOUBLE_EQ(getCellNumber(1, 0), 60.0);
}

// =============================================================================
// Circular Reference Tests
// =============================================================================

TEST_F(FormulaRecalcTest, CircularReferenceTwoCells) {
    // A1=B1, B1=A1 → both should show #CIRCULAR!
    Cell* a1 = setCellFormula(0, 0, "=B1");
    Cell* b1 = setCellFormula(1, 0, "=A1");
    ASSERT_NE(a1, nullptr);
    ASSERT_NE(b1, nullptr);

    recalculate(sheet, {a1->id});

    // Both cells should have circular error
    EXPECT_TRUE(cellHasError(0, 0, CellError::CIRCULAR));
    EXPECT_TRUE(cellHasError(1, 0, CellError::CIRCULAR));
}

TEST_F(FormulaRecalcTest, CircularReferenceThreeCells) {
    // A1=B1+1, B1=C1+1, C1=A1+1 → all show #CIRCULAR!
    Cell* a1 = setCellFormula(0, 0, "=B1+1");
    Cell* b1 = setCellFormula(1, 0, "=C1+1");
    Cell* c1 = setCellFormula(2, 0, "=A1+1");
    ASSERT_NE(a1, nullptr);
    ASSERT_NE(b1, nullptr);
    ASSERT_NE(c1, nullptr);

    recalculate(sheet, {a1->id});

    EXPECT_TRUE(cellHasError(0, 0, CellError::CIRCULAR));
    EXPECT_TRUE(cellHasError(1, 0, CellError::CIRCULAR));
    EXPECT_TRUE(cellHasError(2, 0, CellError::CIRCULAR));
}

TEST_F(FormulaRecalcTest, SelfReference) {
    // A1=A1+1 → #CIRCULAR!
    Cell* a1 = setCellFormula(0, 0, "=A1+1");
    ASSERT_NE(a1, nullptr);

    EvalResult result = evaluateCell(sheet, a1);
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::CIRCULAR);
}

// =============================================================================
// Volatile Function Tests
// =============================================================================

TEST_F(FormulaRecalcTest, VolatileCellMarking) {
    // A1=NOW() should be volatile
    Cell* a1 = setCellFormula(0, 0, "=NOW()");
    ASSERT_NE(a1, nullptr);

    DependencyGraph* depGraph = sheet->getDependencyGraph();
    EXPECT_TRUE(depGraph->isVolatile(a1->id));
}

TEST_F(FormulaRecalcTest, VolatileCellRecalculates) {
    // A1=NOW(), B1=A1+1
    Cell* a1 = setCellFormula(0, 0, "=NOW()");
    Cell* b1 = setCellFormula(1, 0, "=A1+1");
    ASSERT_NE(a1, nullptr);
    ASSERT_NE(b1, nullptr);

    // First evaluation
    recalculateVolatile(sheet);
    double firstA1 = getCellNumber(0, 0);
    double firstB1 = getCellNumber(1, 0);

    // Both should have values
    EXPECT_GT(firstA1, 0.0);  // NOW() returns date serial number
    EXPECT_GT(firstB1, firstA1);

    // Mark formulas dirty and recalc again
    a1->getFormula()->dirty = true;
    recalculateVolatile(sheet);

    // Values may change (NOW() is dynamic)
    // Just verify B1 is still A1+1
    EXPECT_DOUBLE_EQ(getCellNumber(1, 0), getCellNumber(0, 0) + 1.0);
}

TEST_F(FormulaRecalcTest, TodayIsVolatile) {
    Cell* a1 = setCellFormula(0, 0, "=TODAY()");
    ASSERT_NE(a1, nullptr);

    DependencyGraph* depGraph = sheet->getDependencyGraph();
    EXPECT_TRUE(depGraph->isVolatile(a1->id));
}

TEST_F(FormulaRecalcTest, VolatileCellReferenceReturnsConsistentValue) {
    // Bug test: A1=RAND(), B1=A1 should have the same value after recalculation
    // Previously, evaluateCell didn't check dirty flag and would re-evaluate RAND()
    Cell* a1 = setCellFormula(0, 0, "=RAND()");
    Cell* b1 = setCellFormula(1, 0, "=A1");
    ASSERT_NE(a1, nullptr);
    ASSERT_NE(b1, nullptr);

    // Recalculate volatile cells
    recalculateVolatile(sheet);

    // A1 and B1 should have the same value (B1 references A1's cached result)
    double a1Value = getCellNumber(0, 0);
    double b1Value = getCellNumber(1, 0);
    EXPECT_DOUBLE_EQ(a1Value, b1Value)
        << "B1 (=A1) should have the same value as A1 (=RAND()) after recalculation";

    // Calling evaluateCell again should NOT change the value if formula is clean
    EvalResult result = evaluateCell(sheet, a1);
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), a1Value)
        << "evaluateCell on clean formula should return cached value, not re-evaluate";
}

TEST_F(FormulaRecalcTest, VolatileCellNotRecalculatedOnUnrelatedChange) {
    // A1=RAND(), B1=A1, C1=5
    // Changing C1 should NOT trigger A1 or B1 to recalculate
    Cell* a1 = setCellFormula(0, 0, "=RAND()");
    Cell* b1 = setCellFormula(1, 0, "=A1");
    Cell* c1 = setCellValue(2, 0, 5.0);
    ASSERT_NE(a1, nullptr);
    ASSERT_NE(b1, nullptr);
    ASSERT_NE(c1, nullptr);

    // Initial recalculation of volatile cells
    recalculateVolatile(sheet);
    double initialA1 = getCellNumber(0, 0);
    double initialB1 = getCellNumber(1, 0);
    EXPECT_DOUBLE_EQ(initialA1, initialB1);

    // Change unrelated cell C1
    c1->value = CellValue(10.0);
    recalculate(sheet, {c1->id});

    // A1 and B1 should NOT have changed (RAND not re-evaluated)
    EXPECT_DOUBLE_EQ(getCellNumber(0, 0), initialA1)
        << "RAND() should not recalculate when unrelated cell changes";
    EXPECT_DOUBLE_EQ(getCellNumber(1, 0), initialB1)
        << "Cell referencing RAND() should not change when unrelated cell changes";
}

TEST_F(FormulaRecalcTest, VolatileCellRecalculateTriggersDependents) {
    // A1=RAND(), B1=A1, C1=B1+1
    // When recalculateVolatile is called:
    // 1. A1 gets a new RAND value
    // 2. B1 should get A1's new value
    // 3. C1 should get B1's new value + 1
    Cell* a1 = setCellFormula(0, 0, "=RAND()");
    Cell* b1 = setCellFormula(1, 0, "=A1");
    Cell* c1 = setCellFormula(2, 0, "=B1+1");
    ASSERT_NE(a1, nullptr);
    ASSERT_NE(b1, nullptr);
    ASSERT_NE(c1, nullptr);

    // Initial recalculation
    recalculateVolatile(sheet);
    double firstA1 = getCellNumber(0, 0);
    double firstB1 = getCellNumber(1, 0);
    double firstC1 = getCellNumber(2, 0);

    EXPECT_DOUBLE_EQ(firstB1, firstA1) << "B1 should equal A1";
    EXPECT_DOUBLE_EQ(firstC1, firstA1 + 1.0) << "C1 should equal A1 + 1";

    // Recalculate volatile cells again - all values should update consistently
    recalculateVolatile(sheet);
    double secondA1 = getCellNumber(0, 0);
    double secondB1 = getCellNumber(1, 0);
    double secondC1 = getCellNumber(2, 0);

    // A1 should have a new value (RAND recalculated)
    // Note: There's a tiny chance RAND produces the same value, but very unlikely
    EXPECT_DOUBLE_EQ(secondB1, secondA1) << "B1 should equal new A1 value";
    EXPECT_DOUBLE_EQ(secondC1, secondA1 + 1.0) << "C1 should equal new A1 + 1";
}

// =============================================================================
// Dirty Cell Management Tests
// =============================================================================

TEST_F(FormulaRecalcTest, MarkDirtyPropagates) {
    // A1=5, B1=A1*2, C1=B1+1
    Cell* a1 = setCellValue(0, 0, 5.0);
    Cell* b1 = setCellFormula(1, 0, "=A1*2");
    Cell* c1 = setCellFormula(2, 0, "=B1+1");
    ASSERT_NE(b1, nullptr);
    ASSERT_NE(c1, nullptr);

    // Mark B1 and C1 as clean
    b1->getFormula()->dirty = false;
    c1->getFormula()->dirty = false;

    // Mark A1 as dirty
    markDirty(sheet, a1->id);

    // B1 and C1 should now be dirty
    EXPECT_TRUE(b1->getFormula()->dirty);
    EXPECT_TRUE(c1->getFormula()->dirty);
}

TEST_F(FormulaRecalcTest, HasDirtyCells) {
    // Initially no dirty cells
    EXPECT_FALSE(hasDirtyCells(sheet));

    // Add a formula (starts dirty)
    Cell* b1 = setCellFormula(1, 0, "=1+2");
    ASSERT_NE(b1, nullptr);

    EXPECT_TRUE(hasDirtyCells(sheet));

    // Evaluate makes it clean
    evaluateCell(sheet, b1);
    EXPECT_FALSE(hasDirtyCells(sheet));
}

TEST_F(FormulaRecalcTest, GetDirtyCellsOrder) {
    // A1=5, B1=A1*2, C1=B1+1
    setCellValue(0, 0, 5.0);
    Cell* b1 = setCellFormula(1, 0, "=A1*2");
    Cell* c1 = setCellFormula(2, 0, "=B1+1");
    ASSERT_NE(b1, nullptr);
    ASSERT_NE(c1, nullptr);

    // Both are dirty
    std::vector<ID> dirtyCells = getDirtyCells(sheet);
    EXPECT_EQ(dirtyCells.size(), 2u);

    // B1 should come before C1 (dependency order)
    auto b1Pos = std::find(dirtyCells.begin(), dirtyCells.end(), b1->id);
    auto c1Pos = std::find(dirtyCells.begin(), dirtyCells.end(), c1->id);
    EXPECT_NE(b1Pos, dirtyCells.end());
    EXPECT_NE(c1Pos, dirtyCells.end());
    EXPECT_LT(b1Pos, c1Pos);
}

// =============================================================================
// Range Formula Tests
// =============================================================================

TEST_F(FormulaRecalcTest, SumRangeRecalculates) {
    // A1=1, A2=2, A3=3, B1=SUM(A1:A3)
    setCellValue(0, 0, 1.0);
    setCellValue(0, 1, 2.0);
    setCellValue(0, 2, 3.0);
    Cell* b1 = setCellFormula(1, 0, "=SUM(A1:A3)");
    ASSERT_NE(b1, nullptr);

    recalculate(sheet, {sheet->getCellAt(colIds[0], rowIds[0])->id});

    EXPECT_DOUBLE_EQ(getCellNumber(1, 0), 6.0);
}

TEST_F(FormulaRecalcTest, RangeValueChangeTriggers) {
    // A1=1, A2=2, A3=3, B1=SUM(A1:A3)
    Cell* a1 = setCellValue(0, 0, 1.0);
    setCellValue(0, 1, 2.0);
    setCellValue(0, 2, 3.0);
    Cell* b1 = setCellFormula(1, 0, "=SUM(A1:A3)");
    ASSERT_NE(b1, nullptr);
    recalculate(sheet, {a1->id});
    EXPECT_DOUBLE_EQ(getCellNumber(1, 0), 6.0);

    // Change A2 to 10
    Cell* a2 = sheet->getCellAt(colIds[0], rowIds[1]);
    a2->value = CellValue(10.0);
    recalculate(sheet, {a2->id});

    EXPECT_DOUBLE_EQ(getCellNumber(1, 0), 14.0);  // 1+10+3
}

// =============================================================================
// Error Propagation Tests
// =============================================================================

TEST_F(FormulaRecalcTest, ErrorPropagatesThroughChain) {
    // A1=1/0, B1=A1*2, C1=B1+1
    setCellFormula(0, 0, "=1/0");
    Cell* b1 = setCellFormula(1, 0, "=A1*2");
    Cell* c1 = setCellFormula(2, 0, "=B1+1");
    ASSERT_NE(b1, nullptr);
    ASSERT_NE(c1, nullptr);

    // Evaluate the chain
    Cell* a1 = sheet->getCellAt(colIds[0], rowIds[0]);
    recalculate(sheet, {a1->id});

    // All should have DIV error
    EXPECT_TRUE(cellHasError(0, 0, CellError::DIV));
    EXPECT_TRUE(cellHasError(1, 0, CellError::DIV));
    EXPECT_TRUE(cellHasError(2, 0, CellError::DIV));
}

TEST_F(FormulaRecalcTest, IferrorBlocksErrorPropagation) {
    // A1=1/0, B1=IFERROR(A1, 0)
    setCellFormula(0, 0, "=1/0");
    Cell* b1 = setCellFormula(1, 0, "=IFERROR(A1,0)");
    ASSERT_NE(b1, nullptr);

    Cell* a1 = sheet->getCellAt(colIds[0], rowIds[0]);
    recalculate(sheet, {a1->id});

    EXPECT_TRUE(cellHasError(0, 0, CellError::DIV));
    EXPECT_DOUBLE_EQ(getCellNumber(1, 0), 0.0);  // IFERROR returns 0
}

// =============================================================================
// Performance Tests
// =============================================================================

TEST_F(FormulaRecalcTest, PerformanceLargeChain) {
    // 1000 cells in a chain: A1=1, A2=A1+1, A3=A2+1, ...
    Cell* first = setCellValue(0, 0, 1.0);

    for (int i = 1; i < 100; i++) {  // Use 100 for test, not 1000
        std::string formula = "=A" + std::to_string(i) + "+1";
        setCellFormula(0, i, formula);
    }

    recalculate(sheet, {first->id});

    // Verify last cell
    EXPECT_DOUBLE_EQ(getCellNumber(0, 99), 100.0);
}

TEST_F(FormulaRecalcTest, PerformanceGrid) {
    // 10x10 grid where each cell depends on neighbors
    // First row: values 1-10
    for (int c = 0; c < 10; c++) {
        setCellValue(c, 0, static_cast<double>(c + 1));
    }

    // Remaining rows: each cell = cell above + cell to left (when available)
    for (int r = 1; r < 10; r++) {
        for (int c = 0; c < 10; c++) {
            std::string col = Sheet::positionToColumnName(c);
            if (c == 0) {
                // First column: depends only on row above
                std::string formula = "=" + col + std::to_string(r);  // Above is row (r-1+1)=r
                setCellFormula(c, r, formula);
            } else {
                // Other columns: sum of above and left
                std::string colLeft = Sheet::positionToColumnName(c - 1);
                std::string formula =
                    "=" + col + std::to_string(r) + "+" + colLeft + std::to_string(r + 1);
                setCellFormula(c, r, formula);
            }
        }
    }

    // Recalculate from first row
    std::vector<ID> firstRow;
    for (int c = 0; c < 10; c++) {
        Cell* cell = sheet->getCellAt(colIds[c], rowIds[0]);
        if (cell) {
            firstRow.push_back(cell->id);
        }
    }
    recalculate(sheet, firstRow);

    // Verify some values
    EXPECT_DOUBLE_EQ(getCellNumber(0, 0), 1.0);
    EXPECT_DOUBLE_EQ(getCellNumber(9, 0), 10.0);
}

// =============================================================================
// Edge Cases
// =============================================================================

TEST_F(FormulaRecalcTest, EmptySheet) {
    // Should not crash on empty operations
    recalculate(sheet, {});
    recalculateVolatile(sheet);
    EXPECT_FALSE(hasDirtyCells(sheet));
}

TEST_F(FormulaRecalcTest, NullSheet) {
    // Should handle null gracefully
    EvalResult result = evaluateCell(nullptr, ID("test"));
    EXPECT_TRUE(result.isError());

    recalculate(nullptr, {ID("test")});  // Should not crash
    recalculateVolatile(nullptr);        // Should not crash
}

TEST_F(FormulaRecalcTest, FormulaWithMultipleDependencies) {
    // A1=1, A2=2, A3=3, B1=A1+A2+A3
    Cell* a1 = setCellValue(0, 0, 1.0);
    Cell* a2 = setCellValue(0, 1, 2.0);
    Cell* a3 = setCellValue(0, 2, 3.0);
    setCellFormula(1, 0, "=A1+A2+A3");

    // Change all at once
    a1->value = CellValue(10.0);
    a2->value = CellValue(20.0);
    a3->value = CellValue(30.0);

    recalculate(sheet, {a1->id, a2->id, a3->id});

    EXPECT_DOUBLE_EQ(getCellNumber(1, 0), 60.0);
}

TEST_F(FormulaRecalcTest, RecalculatePreservesFormulaText) {
    setCellValue(0, 0, 5.0);
    Cell* b1 = setCellFormula(1, 0, "=A1*2");
    ASSERT_NE(b1, nullptr);

    std::string formulaBefore = sheet->getCellFormulaText(b1->id);
    EXPECT_FALSE(formulaBefore.empty());

    Cell* a1 = sheet->getCellAt(colIds[0], rowIds[0]);
    recalculate(sheet, {a1->id});

    std::string formulaAfter = sheet->getCellFormulaText(b1->id);
    EXPECT_EQ(formulaBefore, formulaAfter);
}

}  // namespace
}  // namespace cells
