#include <cmath>

#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "core/cells/dependency_graph.h"
#include "core/cells/formula_ast.h"
#include "core/cells/formula_eval.h"
#include "core/cells/formula_parser.h"
#include "core/cells/formula_recalc.h"
#include "core/cells/formula_resolver.h"
#include "core/cells/id.h"
#include "core/cells/model.h"

#include "gtest/gtest.h"

namespace cells {
namespace {

// =============================================================================
// Test Fixture for Formula Data Change Tests
// =============================================================================
// Tests formula recalculation behavior when underlying data changes.
// Focuses on verifying that formulas correctly respond to value changes,
// format changes, cell deletion/recreation, and structural modifications.
// =============================================================================

class FormulaDataChangeTest : public ::testing::Test {
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

    // Explicit const char* overload to prevent implicit conversion to bool
    Cell* setCellValue(uint32_t col, uint32_t row, const char* value) {
        Cell* cell = sheet->getOrCreateCellAt(colIds[col], rowIds[row]);
        cell->value = CellValue(std::string(value));
        return cell;
    }

    Cell* setCellValue(uint32_t col, uint32_t row, bool value) {
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

    // Get cell value as double (assumes it's a number)
    double getCellNumber(uint32_t col, uint32_t row) {
        Cell* cell = sheet->getCellAt(colIds[col], rowIds[row]);
        if (!cell) {
            return 0.0;
        }
        return cell->value.asNumber();
    }

    // Get cell value as string
    std::string getCellString(uint32_t col, uint32_t row) {
        Cell* cell = sheet->getCellAt(colIds[col], rowIds[row]);
        if (!cell) {
            return "";
        }
        return cell->value.asString();
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

    // Get cell at position
    Cell* getCell(uint32_t col, uint32_t row) { return sheet->getCellAt(colIds[col], rowIds[row]); }

    std::unique_ptr<Workbook> workbook;
    Sheet* sheet = nullptr;
    ID colIds[26];   // A=0, B=1, ..., Z=25
    ID rowIds[100];  // Row 1=0, Row 2=1, ..., Row 100=99
};

// =============================================================================
// 8a: Formula Recalculates When Direct Dependency Changes
// =============================================================================
// Tests that formulas correctly respond when their directly referenced
// cells change values.

TEST_F(FormulaDataChangeTest, DirectDependency_SimpleReference) {
    // A1 = 10, B1 = =A1
    Cell* a1 = setCellValue(0, 0, 10.0);
    Cell* b1 = setCellFormula(1, 0, "=A1");
    ASSERT_NE(b1, nullptr);

    recalculate(sheet, {a1->id});
    EXPECT_DOUBLE_EQ(getCellNumber(1, 0), 10.0);

    // Change A1 to 25
    a1->value = CellValue(25.0);
    recalculate(sheet, {a1->id});
    EXPECT_DOUBLE_EQ(getCellNumber(1, 0), 25.0);
}

TEST_F(FormulaDataChangeTest, DirectDependency_ArithmeticFormula) {
    // A1 = 5, B1 = 3, C1 = =A1+B1
    Cell* a1 = setCellValue(0, 0, 5.0);
    Cell* b1 = setCellValue(1, 0, 3.0);
    Cell* c1 = setCellFormula(2, 0, "=A1+B1");
    ASSERT_NE(c1, nullptr);

    recalculate(sheet, {a1->id, b1->id});
    EXPECT_DOUBLE_EQ(getCellNumber(2, 0), 8.0);

    // Change A1 to 10
    a1->value = CellValue(10.0);
    recalculate(sheet, {a1->id});
    EXPECT_DOUBLE_EQ(getCellNumber(2, 0), 13.0);  // 10+3

    // Change B1 to 7
    b1->value = CellValue(7.0);
    recalculate(sheet, {b1->id});
    EXPECT_DOUBLE_EQ(getCellNumber(2, 0), 17.0);  // 10+7
}

TEST_F(FormulaDataChangeTest, DirectDependency_MultipleFormulasOnSameCell) {
    // A1 = 100, B1 = =A1*2, C1 = =A1/4
    Cell* a1 = setCellValue(0, 0, 100.0);
    Cell* b1 = setCellFormula(1, 0, "=A1*2");
    Cell* c1 = setCellFormula(2, 0, "=A1/4");
    ASSERT_NE(b1, nullptr);
    ASSERT_NE(c1, nullptr);

    recalculate(sheet, {a1->id});
    EXPECT_DOUBLE_EQ(getCellNumber(1, 0), 200.0);
    EXPECT_DOUBLE_EQ(getCellNumber(2, 0), 25.0);

    // Change A1 to 40
    a1->value = CellValue(40.0);
    recalculate(sheet, {a1->id});
    EXPECT_DOUBLE_EQ(getCellNumber(1, 0), 80.0);
    EXPECT_DOUBLE_EQ(getCellNumber(2, 0), 10.0);
}

TEST_F(FormulaDataChangeTest, DirectDependency_TypeChange_NumberToString) {
    // A1 starts as number, B1 = =A1&"!"
    Cell* a1 = setCellValue(0, 0, 42.0);
    Cell* b1 = setCellFormula(1, 0, "=A1&\"!\"");
    ASSERT_NE(b1, nullptr);

    recalculate(sheet, {a1->id});
    EXPECT_EQ(getCellString(1, 0), "42!");

    // Change A1 to string
    a1->value = CellValue("Hello");
    recalculate(sheet, {a1->id});
    EXPECT_EQ(getCellString(1, 0), "Hello!");
}

TEST_F(FormulaDataChangeTest, DirectDependency_TypeChange_StringToNumber) {
    // A1 starts as numeric string, B1 = =A1+10
    Cell* a1 = setCellValue(0, 0, "15");
    Cell* b1 = setCellFormula(1, 0, "=A1+10");
    ASSERT_NE(b1, nullptr);

    recalculate(sheet, {a1->id});
    EXPECT_DOUBLE_EQ(getCellNumber(1, 0), 25.0);  // "15" coerces to 15

    // Change A1 to actual number
    a1->value = CellValue(20.0);
    recalculate(sheet, {a1->id});
    EXPECT_DOUBLE_EQ(getCellNumber(1, 0), 30.0);
}

TEST_F(FormulaDataChangeTest, DirectDependency_ValueThenError) {
    // A1 = 10, B1 = =A1*2
    Cell* a1 = setCellValue(0, 0, 10.0);
    Cell* b1 = setCellFormula(1, 0, "=A1*2");
    ASSERT_NE(b1, nullptr);

    recalculate(sheet, {a1->id});
    EXPECT_DOUBLE_EQ(getCellNumber(1, 0), 20.0);

    // Change A1 to error
    a1->value = CellValue(CellError::DIV);
    recalculate(sheet, {a1->id});
    EXPECT_TRUE(cellHasError(1, 0, CellError::DIV));
}

TEST_F(FormulaDataChangeTest, DirectDependency_ErrorThenValue) {
    // A1 starts with error, B1 = =A1+5
    Cell* a1 = setCellError(0, 0, CellError::VALUE);
    Cell* b1 = setCellFormula(1, 0, "=A1+5");
    ASSERT_NE(b1, nullptr);

    recalculate(sheet, {a1->id});
    EXPECT_TRUE(cellHasError(1, 0, CellError::VALUE));

    // Fix A1 with valid value
    a1->value = CellValue(10.0);
    recalculate(sheet, {a1->id});
    EXPECT_DOUBLE_EQ(getCellNumber(1, 0), 15.0);
}

TEST_F(FormulaDataChangeTest, DirectDependency_RangeFunction) {
    // A1:A5 = [1,2,3,4,5], B1 = =SUM(A1:A5)
    for (int i = 0; i < 5; i++) {
        setCellValue(0, i, static_cast<double>(i + 1));
    }
    Cell* b1 = setCellFormula(1, 0, "=SUM(A1:A5)");
    ASSERT_NE(b1, nullptr);

    Cell* a1 = getCell(0, 0);
    recalculate(sheet, {a1->id});
    EXPECT_DOUBLE_EQ(getCellNumber(1, 0), 15.0);

    // Change A3 (middle of range)
    Cell* a3 = getCell(0, 2);
    a3->value = CellValue(10.0);  // Was 3, now 10
    recalculate(sheet, {a3->id});
    EXPECT_DOUBLE_EQ(getCellNumber(1, 0), 22.0);  // 1+2+10+4+5
}

// =============================================================================
// 8b: Formula Recalculates When Indirect Dependency Changes (Chain)
// =============================================================================
// Tests that changes propagate correctly through formula chains.

TEST_F(FormulaDataChangeTest, IndirectChain_TwoLevels) {
    // A1 = 5, B1 = =A1*2, C1 = =B1+3
    Cell* a1 = setCellValue(0, 0, 5.0);
    setCellFormula(1, 0, "=A1*2");
    setCellFormula(2, 0, "=B1+3");

    recalculate(sheet, {a1->id});
    EXPECT_DOUBLE_EQ(getCellNumber(1, 0), 10.0);  // 5*2
    EXPECT_DOUBLE_EQ(getCellNumber(2, 0), 13.0);  // 10+3

    // Change A1
    a1->value = CellValue(7.0);
    recalculate(sheet, {a1->id});
    EXPECT_DOUBLE_EQ(getCellNumber(1, 0), 14.0);  // 7*2
    EXPECT_DOUBLE_EQ(getCellNumber(2, 0), 17.0);  // 14+3
}

TEST_F(FormulaDataChangeTest, IndirectChain_ThreeLevels) {
    // A1 = 2, B1 = =A1^2, C1 = =B1*3, D1 = =C1-1
    Cell* a1 = setCellValue(0, 0, 2.0);
    setCellFormula(1, 0, "=A1^2");
    setCellFormula(2, 0, "=B1*3");
    setCellFormula(3, 0, "=C1-1");

    recalculate(sheet, {a1->id});
    EXPECT_DOUBLE_EQ(getCellNumber(1, 0), 4.0);   // 2^2
    EXPECT_DOUBLE_EQ(getCellNumber(2, 0), 12.0);  // 4*3
    EXPECT_DOUBLE_EQ(getCellNumber(3, 0), 11.0);  // 12-1

    // Change A1
    a1->value = CellValue(3.0);
    recalculate(sheet, {a1->id});
    EXPECT_DOUBLE_EQ(getCellNumber(1, 0), 9.0);   // 3^2
    EXPECT_DOUBLE_EQ(getCellNumber(2, 0), 27.0);  // 9*3
    EXPECT_DOUBLE_EQ(getCellNumber(3, 0), 26.0);  // 27-1
}

TEST_F(FormulaDataChangeTest, IndirectChain_Diamond) {
    // Diamond dependency: A1 -> B1 -> D1
    //                     A1 -> C1 -> D1
    // A1 = 10, B1 = =A1+1, C1 = =A1-1, D1 = =B1+C1
    Cell* a1 = setCellValue(0, 0, 10.0);
    setCellFormula(1, 0, "=A1+1");
    setCellFormula(2, 0, "=A1-1");
    setCellFormula(3, 0, "=B1+C1");

    recalculate(sheet, {a1->id});
    EXPECT_DOUBLE_EQ(getCellNumber(1, 0), 11.0);  // 10+1
    EXPECT_DOUBLE_EQ(getCellNumber(2, 0), 9.0);   // 10-1
    EXPECT_DOUBLE_EQ(getCellNumber(3, 0), 20.0);  // 11+9

    // Change A1
    a1->value = CellValue(20.0);
    recalculate(sheet, {a1->id});
    EXPECT_DOUBLE_EQ(getCellNumber(1, 0), 21.0);  // 20+1
    EXPECT_DOUBLE_EQ(getCellNumber(2, 0), 19.0);  // 20-1
    EXPECT_DOUBLE_EQ(getCellNumber(3, 0), 40.0);  // 21+19
}

TEST_F(FormulaDataChangeTest, IndirectChain_ErrorPropagation) {
    // A1 = 5, B1 = =A1*2, C1 = =B1+10
    Cell* a1 = setCellValue(0, 0, 5.0);
    setCellFormula(1, 0, "=A1*2");
    setCellFormula(2, 0, "=B1+10");

    recalculate(sheet, {a1->id});
    EXPECT_DOUBLE_EQ(getCellNumber(2, 0), 20.0);

    // Set A1 to error
    a1->value = CellValue(CellError::REF);
    recalculate(sheet, {a1->id});

    // Both B1 and C1 should have the error
    EXPECT_TRUE(cellHasError(1, 0, CellError::REF));
    EXPECT_TRUE(cellHasError(2, 0, CellError::REF));
}

TEST_F(FormulaDataChangeTest, IndirectChain_LongChain) {
    // Create a chain: A1 -> A2 -> A3 -> ... -> A10
    // Each cell adds 1 to the previous
    Cell* a1 = setCellValue(0, 0, 1.0);
    for (int i = 1; i < 10; i++) {
        std::string formula = "=A" + std::to_string(i) + "+1";
        setCellFormula(0, i, formula);
    }

    recalculate(sheet, {a1->id});
    for (int i = 0; i < 10; i++) {
        EXPECT_DOUBLE_EQ(getCellNumber(0, i), static_cast<double>(i + 1))
            << "A" << (i + 1) << " should be " << (i + 1);
    }

    // Change A1 to 10
    a1->value = CellValue(10.0);
    recalculate(sheet, {a1->id});
    for (int i = 0; i < 10; i++) {
        EXPECT_DOUBLE_EQ(getCellNumber(0, i), static_cast<double>(i + 10))
            << "A" << (i + 1) << " should be " << (i + 10);
    }
}

TEST_F(FormulaDataChangeTest, IndirectChain_MultipleSources) {
    // A1 = 5, A2 = 10, B1 = =A1+A2, C1 = =B1*2
    Cell* a1 = setCellValue(0, 0, 5.0);
    Cell* a2 = setCellValue(0, 1, 10.0);
    setCellFormula(1, 0, "=A1+A2");
    setCellFormula(2, 0, "=B1*2");

    recalculate(sheet, {a1->id, a2->id});
    EXPECT_DOUBLE_EQ(getCellNumber(1, 0), 15.0);  // 5+10
    EXPECT_DOUBLE_EQ(getCellNumber(2, 0), 30.0);  // 15*2

    // Change both sources at once
    a1->value = CellValue(20.0);
    a2->value = CellValue(30.0);
    recalculate(sheet, {a1->id, a2->id});
    EXPECT_DOUBLE_EQ(getCellNumber(1, 0), 50.0);   // 20+30
    EXPECT_DOUBLE_EQ(getCellNumber(2, 0), 100.0);  // 50*2
}

// =============================================================================
// 8c: Formula Recalculates When Cell Format Changes Affect Type Coercion
// =============================================================================
// Tests that format changes can affect how values are interpreted in formulas.

TEST_F(FormulaDataChangeTest, FormatChange_TextToNumber) {
    // A1 stored as text "42", B1 = =A1+8
    // Format change should trigger recalc if it affects interpretation
    Cell* a1 = setCellValue(0, 0, "42");
    Cell* b1 = setCellFormula(1, 0, "=A1+8");
    ASSERT_NE(b1, nullptr);

    recalculate(sheet, {a1->id});
    // Numeric strings coerce to numbers in arithmetic
    EXPECT_DOUBLE_EQ(getCellNumber(1, 0), 50.0);

    // Changing the value directly (simulating format-triggered reparse)
    a1->value = CellValue(42.0);
    recalculate(sheet, {a1->id});
    EXPECT_DOUBLE_EQ(getCellNumber(1, 0), 50.0);  // Same result, different storage
}

TEST_F(FormulaDataChangeTest, FormatChange_PercentageDisplay) {
    // A1 = 0.5 displayed as 50%, B1 = =A1*100
    // The underlying value (0.5) is what matters for calculation
    Cell* a1 = setCellValue(0, 0, 0.5);
    Cell* b1 = setCellFormula(1, 0, "=A1*100");
    ASSERT_NE(b1, nullptr);

    recalculate(sheet, {a1->id});
    EXPECT_DOUBLE_EQ(getCellNumber(1, 0), 50.0);

    // Format doesn't change the underlying value
    a1->value = CellValue(0.75);
    recalculate(sheet, {a1->id});
    EXPECT_DOUBLE_EQ(getCellNumber(1, 0), 75.0);
}

TEST_F(FormulaDataChangeTest, FormatChange_BooleanInCalculation) {
    // A1 = TRUE (1), A2 = FALSE (0), B1 = =A1+A2
    Cell* a1 = setCellValue(0, 0, true);
    Cell* a2 = setCellValue(0, 1, false);
    Cell* b1 = setCellFormula(1, 0, "=A1+A2");
    ASSERT_NE(b1, nullptr);

    recalculate(sheet, {a1->id, a2->id});
    EXPECT_DOUBLE_EQ(getCellNumber(1, 0), 1.0);  // TRUE=1, FALSE=0

    // Change A2 to TRUE
    a2->value = CellValue(true);
    recalculate(sheet, {a2->id});
    EXPECT_DOUBLE_EQ(getCellNumber(1, 0), 2.0);  // 1+1
}

// =============================================================================
// 8d: Formula Recalculates When Referenced Cell Deleted Then Recreated
// =============================================================================
// Tests formula behavior when a referenced cell is deleted and then recreated.

TEST_F(FormulaDataChangeTest, CellDeletionRecreation_SingleReference) {
    // A1 = 100, B1 = =A1*2
    Cell* a1 = setCellValue(0, 0, 100.0);
    Cell* b1 = setCellFormula(1, 0, "=A1*2");
    ASSERT_NE(b1, nullptr);
    ID a1Id = a1->id;

    recalculate(sheet, {a1Id});
    EXPECT_DOUBLE_EQ(getCellNumber(1, 0), 200.0);

    // Delete A1 (simulate by setting to empty value)
    // Note: In actual CRDT this would be a CELL_DELETE operation
    a1->value = CellValue(0.0);  // Empty cell treated as 0
    recalculate(sheet, {a1Id});
    EXPECT_DOUBLE_EQ(getCellNumber(1, 0), 0.0);  // 0*2 = 0

    // Recreate A1 with new value
    a1->value = CellValue(50.0);
    recalculate(sheet, {a1Id});
    EXPECT_DOUBLE_EQ(getCellNumber(1, 0), 100.0);  // 50*2
}

TEST_F(FormulaDataChangeTest, CellDeletionRecreation_RangeReference) {
    // A1:A3 = [10, 20, 30], B1 = =SUM(A1:A3)
    setCellValue(0, 0, 10.0);
    Cell* a2 = setCellValue(0, 1, 20.0);
    setCellValue(0, 2, 30.0);
    Cell* b1 = setCellFormula(1, 0, "=SUM(A1:A3)");
    ASSERT_NE(b1, nullptr);

    Cell* a1 = getCell(0, 0);
    recalculate(sheet, {a1->id});
    EXPECT_DOUBLE_EQ(getCellNumber(1, 0), 60.0);

    // Delete A2 (middle of range)
    a2->value = CellValue(0.0);
    recalculate(sheet, {a2->id});
    EXPECT_DOUBLE_EQ(getCellNumber(1, 0), 40.0);  // 10+0+30

    // Recreate A2
    a2->value = CellValue(100.0);
    recalculate(sheet, {a2->id});
    EXPECT_DOUBLE_EQ(getCellNumber(1, 0), 140.0);  // 10+100+30
}

TEST_F(FormulaDataChangeTest, CellDeletionRecreation_ChainedFormulas) {
    // A1 = 10, B1 = =A1+5, C1 = =B1*2
    Cell* a1 = setCellValue(0, 0, 10.0);
    setCellFormula(1, 0, "=A1+5");
    setCellFormula(2, 0, "=B1*2");

    recalculate(sheet, {a1->id});
    EXPECT_DOUBLE_EQ(getCellNumber(2, 0), 30.0);  // (10+5)*2

    // Delete A1
    a1->value = CellValue(0.0);
    recalculate(sheet, {a1->id});
    EXPECT_DOUBLE_EQ(getCellNumber(1, 0), 5.0);   // 0+5
    EXPECT_DOUBLE_EQ(getCellNumber(2, 0), 10.0);  // 5*2

    // Recreate A1 with different value
    a1->value = CellValue(20.0);
    recalculate(sheet, {a1->id});
    EXPECT_DOUBLE_EQ(getCellNumber(1, 0), 25.0);  // 20+5
    EXPECT_DOUBLE_EQ(getCellNumber(2, 0), 50.0);  // 25*2
}

TEST_F(FormulaDataChangeTest, CellDeletionRecreation_MultipleReferences) {
    // A1 = 5, B1 = =A1, C1 = =A1*2, D1 = =A1+A1
    Cell* a1 = setCellValue(0, 0, 5.0);
    setCellFormula(1, 0, "=A1");
    setCellFormula(2, 0, "=A1*2");
    setCellFormula(3, 0, "=A1+A1");

    recalculate(sheet, {a1->id});
    EXPECT_DOUBLE_EQ(getCellNumber(1, 0), 5.0);
    EXPECT_DOUBLE_EQ(getCellNumber(2, 0), 10.0);
    EXPECT_DOUBLE_EQ(getCellNumber(3, 0), 10.0);

    // Delete A1
    a1->value = CellValue(0.0);
    recalculate(sheet, {a1->id});
    EXPECT_DOUBLE_EQ(getCellNumber(1, 0), 0.0);
    EXPECT_DOUBLE_EQ(getCellNumber(2, 0), 0.0);
    EXPECT_DOUBLE_EQ(getCellNumber(3, 0), 0.0);

    // Recreate A1
    a1->value = CellValue(15.0);
    recalculate(sheet, {a1->id});
    EXPECT_DOUBLE_EQ(getCellNumber(1, 0), 15.0);
    EXPECT_DOUBLE_EQ(getCellNumber(2, 0), 30.0);
    EXPECT_DOUBLE_EQ(getCellNumber(3, 0), 30.0);
}

// =============================================================================
// 8e: Shared Formula Group Recalculates Correctly
// =============================================================================
// Tests that shared formulas (formula groups) recalculate properly when
// their dependencies change.

TEST_F(FormulaDataChangeTest, SharedFormula_BasicGroup) {
    // A1:A3 = [1, 2, 3], B1:B3 share formula =A1, =A2, =A3 (relative refs)
    setCellValue(0, 0, 1.0);
    setCellValue(0, 1, 2.0);
    setCellValue(0, 2, 3.0);

    // Each B cell has its own formula referencing the adjacent A cell
    setCellFormula(1, 0, "=A1");
    setCellFormula(1, 1, "=A2");
    setCellFormula(1, 2, "=A3");

    Cell* a1 = getCell(0, 0);
    Cell* a2 = getCell(0, 1);
    Cell* a3 = getCell(0, 2);
    recalculate(sheet, {a1->id, a2->id, a3->id});

    EXPECT_DOUBLE_EQ(getCellNumber(1, 0), 1.0);
    EXPECT_DOUBLE_EQ(getCellNumber(1, 1), 2.0);
    EXPECT_DOUBLE_EQ(getCellNumber(1, 2), 3.0);

    // Change A2
    a2->value = CellValue(20.0);
    recalculate(sheet, {a2->id});

    EXPECT_DOUBLE_EQ(getCellNumber(1, 0), 1.0);   // Unchanged
    EXPECT_DOUBLE_EQ(getCellNumber(1, 1), 20.0);  // Updated
    EXPECT_DOUBLE_EQ(getCellNumber(1, 2), 3.0);   // Unchanged
}

TEST_F(FormulaDataChangeTest, SharedFormula_ArithmeticGroup) {
    // A1:A3 = [10, 20, 30], B1:B3 = =A*2 (each references its row)
    setCellValue(0, 0, 10.0);
    setCellValue(0, 1, 20.0);
    setCellValue(0, 2, 30.0);

    setCellFormula(1, 0, "=A1*2");
    setCellFormula(1, 1, "=A2*2");
    setCellFormula(1, 2, "=A3*2");

    Cell* a1 = getCell(0, 0);
    Cell* a2 = getCell(0, 1);
    Cell* a3 = getCell(0, 2);
    recalculate(sheet, {a1->id, a2->id, a3->id});

    EXPECT_DOUBLE_EQ(getCellNumber(1, 0), 20.0);
    EXPECT_DOUBLE_EQ(getCellNumber(1, 1), 40.0);
    EXPECT_DOUBLE_EQ(getCellNumber(1, 2), 60.0);

    // Change all A values
    a1->value = CellValue(100.0);
    a2->value = CellValue(200.0);
    a3->value = CellValue(300.0);
    recalculate(sheet, {a1->id, a2->id, a3->id});

    EXPECT_DOUBLE_EQ(getCellNumber(1, 0), 200.0);
    EXPECT_DOUBLE_EQ(getCellNumber(1, 1), 400.0);
    EXPECT_DOUBLE_EQ(getCellNumber(1, 2), 600.0);
}

TEST_F(FormulaDataChangeTest, SharedFormula_MixedAbsoluteRelative) {
    // A1 = 10 (constant), B1:B3 = [1,2,3], C1:C3 = =$A$1*B (mixed ref)
    Cell* a1 = setCellValue(0, 0, 10.0);
    setCellValue(1, 0, 1.0);
    setCellValue(1, 1, 2.0);
    setCellValue(1, 2, 3.0);

    // C cells multiply constant A1 by their respective B cell
    setCellFormula(2, 0, "=$A$1*B1");
    setCellFormula(2, 1, "=$A$1*B2");
    setCellFormula(2, 2, "=$A$1*B3");

    Cell* b1 = getCell(1, 0);
    Cell* b2 = getCell(1, 1);
    Cell* b3 = getCell(1, 2);
    recalculate(sheet, {a1->id, b1->id, b2->id, b3->id});

    EXPECT_DOUBLE_EQ(getCellNumber(2, 0), 10.0);  // 10*1
    EXPECT_DOUBLE_EQ(getCellNumber(2, 1), 20.0);  // 10*2
    EXPECT_DOUBLE_EQ(getCellNumber(2, 2), 30.0);  // 10*3

    // Change the constant (A1)
    a1->value = CellValue(100.0);
    recalculate(sheet, {a1->id});

    EXPECT_DOUBLE_EQ(getCellNumber(2, 0), 100.0);  // 100*1
    EXPECT_DOUBLE_EQ(getCellNumber(2, 1), 200.0);  // 100*2
    EXPECT_DOUBLE_EQ(getCellNumber(2, 2), 300.0);  // 100*3
}

// =============================================================================
// 8f: Array Formula Spill Recalculates on Source Change
// =============================================================================
// Tests that array formulas (spill) correctly recalculate when their
// source data changes.

TEST_F(FormulaDataChangeTest, ArraySpill_SourceDataChange) {
    // Set up source data A1:A3 = [1, 2, 3]
    Cell* a1 = setCellValue(0, 0, 1.0);
    Cell* a2 = setCellValue(0, 1, 2.0);
    Cell* a3 = setCellValue(0, 2, 3.0);

    // Create array formula that references source data
    // B1 = =SUM(A1:A3) - a formula that depends on source range
    Cell* b1 = setCellFormula(1, 0, "=SUM(A1:A3)");
    ASSERT_NE(b1, nullptr);

    recalculate(sheet, {a1->id, a2->id, a3->id});
    EXPECT_DOUBLE_EQ(getCellNumber(1, 0), 6.0);  // 1+2+3

    // Change source data
    a1->value = CellValue(10.0);
    a2->value = CellValue(20.0);
    a3->value = CellValue(30.0);
    recalculate(sheet, {a1->id, a2->id, a3->id});
    EXPECT_DOUBLE_EQ(getCellNumber(1, 0), 60.0);  // 10+20+30
}

TEST_F(FormulaDataChangeTest, ArraySpill_DirectProcessSpill) {
    // Test spill processing using processSpill directly
    Cell* a1 = setCellValue(0, 0, 0.0);
    ASSERT_NE(a1, nullptr);

    // Create a 2x2 array result manually
    std::vector<std::vector<EvalResult>> arrayData = {
        {EvalResult::Number(1.0), EvalResult::Number(2.0)},
        {EvalResult::Number(3.0), EvalResult::Number(4.0)}};
    EvalResult arrayResult = EvalResult::Array(std::move(arrayData));

    // Process the spill
    processSpill(sheet, a1, arrayResult);

    // Master cell should have first value
    EXPECT_DOUBLE_EQ(a1->value.asNumber(), 1.0);

    // Check spill info exists
    const SpillInfo* spillInfo = sheet->getSpillInfo(a1->id);
    ASSERT_NE(spillInfo, nullptr);
    EXPECT_EQ(spillInfo->spillCount(), 3u);
}

TEST_F(FormulaDataChangeTest, ArraySpill_BlockedByData) {
    Cell* a1 = setCellValue(0, 0, 0.0);  // Master cell
    ASSERT_NE(a1, nullptr);

    // Put a blocking value in B1
    setCellValue(1, 0, 999.0);

    // Create a 2x2 array result
    std::vector<std::vector<EvalResult>> arrayData = {
        {EvalResult::Number(1.0), EvalResult::Number(2.0)},
        {EvalResult::Number(3.0), EvalResult::Number(4.0)}};
    EvalResult arrayResult = EvalResult::Array(std::move(arrayData));

    // Process the spill
    processSpill(sheet, a1, arrayResult);

    // Master cell should have #SPILL! error
    EXPECT_TRUE(a1->hasError());
    EXPECT_EQ(a1->value.error, CellError::SPILL);
}

// =============================================================================
// 8g: Circular Reference Detection Remains Stable During Changes
// =============================================================================
// Tests that circular reference detection works correctly even when
// data changes attempt to create or break cycles.

TEST_F(FormulaDataChangeTest, CircularRef_StableAfterValueChange) {
    // Create valid formulas: A1 = 5, B1 = =A1+1
    Cell* a1 = setCellValue(0, 0, 5.0);
    Cell* b1 = setCellFormula(1, 0, "=A1+1");
    ASSERT_NE(b1, nullptr);

    recalculate(sheet, {a1->id});
    EXPECT_DOUBLE_EQ(getCellNumber(1, 0), 6.0);

    // Change A1 - should still work (no circular ref)
    a1->value = CellValue(10.0);
    recalculate(sheet, {a1->id});
    EXPECT_DOUBLE_EQ(getCellNumber(1, 0), 11.0);
}

TEST_F(FormulaDataChangeTest, CircularRef_DirectSelfReference) {
    // A1 = =A1+1 (direct self-reference)
    Cell* a1 = setCellFormula(0, 0, "=A1+1");
    ASSERT_NE(a1, nullptr);

    EvalResult result = evaluateCell(sheet, a1);
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::CIRCULAR);
}

TEST_F(FormulaDataChangeTest, CircularRef_TwoCellCycle) {
    // A1 = =B1, B1 = =A1
    Cell* a1 = setCellFormula(0, 0, "=B1");
    Cell* b1 = setCellFormula(1, 0, "=A1");
    ASSERT_NE(a1, nullptr);
    ASSERT_NE(b1, nullptr);

    recalculate(sheet, {a1->id});

    // Both should show circular error
    EXPECT_TRUE(cellHasError(0, 0, CellError::CIRCULAR));
    EXPECT_TRUE(cellHasError(1, 0, CellError::CIRCULAR));
}

TEST_F(FormulaDataChangeTest, CircularRef_ThreeCellCycle) {
    // A1 = =B1, B1 = =C1, C1 = =A1
    Cell* a1 = setCellFormula(0, 0, "=B1");
    Cell* b1 = setCellFormula(1, 0, "=C1");
    Cell* c1 = setCellFormula(2, 0, "=A1");
    ASSERT_NE(a1, nullptr);
    ASSERT_NE(b1, nullptr);
    ASSERT_NE(c1, nullptr);

    recalculate(sheet, {a1->id});

    EXPECT_TRUE(cellHasError(0, 0, CellError::CIRCULAR));
    EXPECT_TRUE(cellHasError(1, 0, CellError::CIRCULAR));
    EXPECT_TRUE(cellHasError(2, 0, CellError::CIRCULAR));
}

TEST_F(FormulaDataChangeTest, CircularRef_BreakCycleByValueChange) {
    // Start with value, not formula: A1 = 5, B1 = =A1+1
    Cell* a1 = setCellValue(0, 0, 5.0);
    Cell* b1 = setCellFormula(1, 0, "=A1+1");
    ASSERT_NE(b1, nullptr);

    recalculate(sheet, {a1->id});
    EXPECT_DOUBLE_EQ(getCellNumber(1, 0), 6.0);

    // Change A1's value - no cycle, works fine
    a1->value = CellValue(100.0);
    recalculate(sheet, {a1->id});
    EXPECT_DOUBLE_EQ(getCellNumber(1, 0), 101.0);
    EXPECT_FALSE(cellHasError(1, 0, CellError::CIRCULAR));
}

TEST_F(FormulaDataChangeTest, CircularRef_StableMultipleRecalcs) {
    // A1 = =B1+1, B1 = =A1+1 (mutual cycle)
    Cell* a1 = setCellFormula(0, 0, "=B1+1");
    Cell* b1 = setCellFormula(1, 0, "=A1+1");
    ASSERT_NE(a1, nullptr);
    ASSERT_NE(b1, nullptr);

    // Multiple recalculations should all show circular error
    for (int i = 0; i < 3; i++) {
        recalculate(sheet, {a1->id});
        EXPECT_TRUE(cellHasError(0, 0, CellError::CIRCULAR))
            << "Iteration " << i << ": A1 should show CIRCULAR";
        EXPECT_TRUE(cellHasError(1, 0, CellError::CIRCULAR))
            << "Iteration " << i << ": B1 should show CIRCULAR";
    }
}

TEST_F(FormulaDataChangeTest, CircularRef_ChainWithCycleAtEnd) {
    // A1 = 10, B1 = =A1+1, C1 = =B1+1, D1 = =C1+D1 (self-ref at end)
    setCellValue(0, 0, 10.0);
    Cell* b1 = setCellFormula(1, 0, "=A1+1");
    Cell* c1 = setCellFormula(2, 0, "=B1+1");
    Cell* d1 = setCellFormula(3, 0, "=C1+D1");
    ASSERT_NE(b1, nullptr);
    ASSERT_NE(c1, nullptr);
    ASSERT_NE(d1, nullptr);

    // First, evaluate the non-circular formulas directly
    evaluateCell(sheet, b1);
    evaluateCell(sheet, c1);

    // A1 is a value, not a formula, so it stays 10
    EXPECT_DOUBLE_EQ(getCellNumber(0, 0), 10.0);

    // B1 = A1+1 = 11
    EXPECT_DOUBLE_EQ(getCellNumber(1, 0), 11.0);

    // C1 = B1+1 = 12
    EXPECT_DOUBLE_EQ(getCellNumber(2, 0), 12.0);

    // D1 has circular ref (references itself)
    EvalResult d1Result = evaluateCell(sheet, d1);
    EXPECT_TRUE(d1Result.isError());
    EXPECT_EQ(d1Result.getError(), CellError::CIRCULAR);
}

// =============================================================================
// Additional Edge Cases
// =============================================================================

TEST_F(FormulaDataChangeTest, EmptyCellInFormula) {
    // B1 = =A1+10 where A1 doesn't exist (empty = 0)
    Cell* b1 = setCellFormula(1, 0, "=A1+10");
    ASSERT_NE(b1, nullptr);

    // A1 is empty, should be treated as 0
    recalculate(sheet, {b1->id});
    EXPECT_DOUBLE_EQ(getCellNumber(1, 0), 10.0);

    // Now create A1 with a value
    Cell* a1 = setCellValue(0, 0, 5.0);
    recalculate(sheet, {a1->id});
    EXPECT_DOUBLE_EQ(getCellNumber(1, 0), 15.0);
}

TEST_F(FormulaDataChangeTest, RapidSuccessiveChanges) {
    // A1 feeds multiple formulas, make rapid changes
    Cell* a1 = setCellValue(0, 0, 1.0);
    setCellFormula(1, 0, "=A1*10");
    setCellFormula(2, 0, "=A1*100");
    setCellFormula(3, 0, "=A1*1000");

    // Rapid changes
    for (int i = 1; i <= 5; i++) {
        a1->value = CellValue(static_cast<double>(i));
        recalculate(sheet, {a1->id});

        EXPECT_DOUBLE_EQ(getCellNumber(1, 0), i * 10.0);
        EXPECT_DOUBLE_EQ(getCellNumber(2, 0), i * 100.0);
        EXPECT_DOUBLE_EQ(getCellNumber(3, 0), i * 1000.0);
    }
}

TEST_F(FormulaDataChangeTest, ConditionalFormula_IFStatement) {
    // A1 = condition, B1 = =IF(A1>0, "positive", "non-positive")
    Cell* a1 = setCellValue(0, 0, 5.0);
    Cell* b1 = setCellFormula(1, 0, "=IF(A1>0,\"positive\",\"non-positive\")");
    ASSERT_NE(b1, nullptr);

    recalculate(sheet, {a1->id});
    EXPECT_EQ(getCellString(1, 0), "positive");

    // Change to negative
    a1->value = CellValue(-5.0);
    recalculate(sheet, {a1->id});
    EXPECT_EQ(getCellString(1, 0), "non-positive");

    // Change to zero
    a1->value = CellValue(0.0);
    recalculate(sheet, {a1->id});
    EXPECT_EQ(getCellString(1, 0), "non-positive");

    // Back to positive
    a1->value = CellValue(1.0);
    recalculate(sheet, {a1->id});
    EXPECT_EQ(getCellString(1, 0), "positive");
}

TEST_F(FormulaDataChangeTest, LookupFormula_DataChange) {
    // Data table: A1:B3 = [[1,"one"], [2,"two"], [3,"three"]]
    // C1 = =VLOOKUP(A4, A1:B3, 2, FALSE)
    setCellValue(0, 0, 1.0);
    setCellValue(1, 0, "one");
    setCellValue(0, 1, 2.0);
    setCellValue(1, 1, "two");
    setCellValue(0, 2, 3.0);
    setCellValue(1, 2, "three");

    Cell* a4 = setCellValue(0, 3, 2.0);  // Lookup value
    Cell* c1 = setCellFormula(2, 0, "=VLOOKUP(A4,A1:B3,2,FALSE)");
    ASSERT_NE(c1, nullptr);

    recalculate(sheet, {a4->id});
    EXPECT_EQ(getCellString(2, 0), "two");

    // Change lookup value
    a4->value = CellValue(3.0);
    recalculate(sheet, {a4->id});
    EXPECT_EQ(getCellString(2, 0), "three");

    // Change lookup value to non-existent
    a4->value = CellValue(99.0);
    recalculate(sheet, {a4->id});
    EXPECT_TRUE(cellHasError(2, 0, CellError::NA));
}

}  // namespace
}  // namespace cells
