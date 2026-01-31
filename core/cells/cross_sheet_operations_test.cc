#include <memory>
#include <string>
#include <vector>

#include "core/cells/crdt.h"
#include "core/cells/dependency_graph.h"
#include "core/cells/formula_display.h"
#include "core/cells/formula_eval.h"
#include "core/cells/formula_parser.h"
#include "core/cells/formula_recalc.h"
#include "core/cells/formula_resolver.h"
#include "core/cells/formula_serializer.h"
#include "core/cells/id.h"
#include "core/cells/model.h"

#include "gtest/gtest.h"

namespace cells {
namespace {

// =============================================================================
// Test Fixture for Cross-Sheet Operations Tests
// =============================================================================
// Tests cross-sheet formula references, sheet rename/delete effects on formulas,
// cross-sheet range references, and concurrent cross-sheet edits.
// =============================================================================

class CrossSheetOperationsTest : public ::testing::Test {
protected:
    void SetUp() override {
        workbook = std::make_unique<Workbook>(generate_id(), "Test");

        // Create Sheet1
        workbook->addSheet(std::make_unique<Sheet>(generate_id(), "Sheet1"));
        sheet1 = workbook->getSheetByIndex(0);

        // Create Sheet2
        workbook->addSheet(std::make_unique<Sheet>(generate_id(), "Sheet2"));
        sheet2 = workbook->getSheetByIndex(1);

        // Create columns A-J (positions 0-9) on Sheet1
        for (uint32_t i = 0; i < 10; i++) {
            auto col = std::make_unique<Axis>(generate_id(), true);
            col->position = i;
            col->name = Sheet::positionToColumnName(i);
            sheet1ColIds[i] = col->id;
            sheet1->addColumn(std::move(col));
        }

        // Create rows 1-20 (positions 0-19) on Sheet1
        for (uint32_t i = 0; i < 20; i++) {
            auto row = std::make_unique<Axis>(generate_id(), false);
            row->position = i;
            sheet1RowIds[i] = row->id;
            sheet1->addRow(std::move(row));
        }

        // Create columns A-J (positions 0-9) on Sheet2
        for (uint32_t i = 0; i < 10; i++) {
            auto col = std::make_unique<Axis>(generate_id(), true);
            col->position = i;
            col->name = Sheet::positionToColumnName(i);
            sheet2ColIds[i] = col->id;
            sheet2->addColumn(std::move(col));
        }

        // Create rows 1-20 (positions 0-19) on Sheet2
        for (uint32_t i = 0; i < 20; i++) {
            auto row = std::make_unique<Axis>(generate_id(), false);
            row->position = i;
            sheet2RowIds[i] = row->id;
            sheet2->addRow(std::move(row));
        }
    }

    // Set a cell value at a given column/row position (0-indexed) on specified sheet
    Cell* setCellValue(Sheet* sheet, const ID* colIds, const ID* rowIds, uint32_t col, uint32_t row,
                       double value) {
        Cell* cell = sheet->getOrCreateCellAt(colIds[col], rowIds[row]);
        cell->value = CellValue(value);
        return cell;
    }

    Cell* setCellValue(Sheet* sheet, const ID* colIds, const ID* rowIds, uint32_t col, uint32_t row,
                       const std::string& value) {
        Cell* cell = sheet->getOrCreateCellAt(colIds[col], rowIds[row]);
        cell->value = CellValue(value);
        return cell;
    }

    // Convenience methods for Sheet1
    Cell* setSheet1Value(uint32_t col, uint32_t row, double value) {
        return setCellValue(sheet1, sheet1ColIds, sheet1RowIds, col, row, value);
    }

    Cell* setSheet1Value(uint32_t col, uint32_t row, const std::string& value) {
        return setCellValue(sheet1, sheet1ColIds, sheet1RowIds, col, row, value);
    }

    // Convenience methods for Sheet2
    Cell* setSheet2Value(uint32_t col, uint32_t row, double value) {
        return setCellValue(sheet2, sheet2ColIds, sheet2RowIds, col, row, value);
    }

    Cell* setSheet2Value(uint32_t col, uint32_t row, const std::string& value) {
        return setCellValue(sheet2, sheet2ColIds, sheet2RowIds, col, row, value);
    }

    // Set a formula on a cell on the specified sheet
    Cell* setCellFormula(Sheet* sheet, const ID* colIds, const ID* rowIds, uint32_t col,
                         uint32_t row, const std::string& formula) {
        Cell* cell = sheet->getOrCreateCellAt(colIds[col], rowIds[row]);

        FormulaParser parser(formula);
        auto ast = parser.parse();
        if (!ast || parser.hasErrors()) {
            return nullptr;
        }

        FormulaResolver resolver(*workbook, *sheet);
        createRequiredEntities(resolver, ast.get(), sheet, colIds, rowIds);
        ResolveResult resolveResult = resolver.resolve(ast.get());
        if (!resolveResult.success) {
            return nullptr;
        }

        auto result = sheet->setCellFormula(cell->id, formula, ast.release());
        if (!result.success) {
            return nullptr;
        }

        return cell;
    }

    // Convenience for Sheet1
    Cell* setSheet1Formula(uint32_t col, uint32_t row, const std::string& formula) {
        return setCellFormula(sheet1, sheet1ColIds, sheet1RowIds, col, row, formula);
    }

    // Helper to create missing entities before resolution
    void createRequiredEntities(FormulaResolver& resolver, ASTNode* ast, Sheet* contextSheet,
                                const ID* colIds, const ID* rowIds) {
        RequiredEntities required = resolver.getRequiredEntities(ast);
        for (const auto& pendingCell : required.cells) {
            // Determine which sheet the cell belongs to
            Sheet* targetSheet = contextSheet;
            if (!pendingCell.sheetId.isNull()) {
                Sheet* crossSheet = workbook->getSheet(pendingCell.sheetId);
                if (crossSheet) {
                    targetSheet = crossSheet;
                }
            }

            auto findColPos = [&required, targetSheet](const ID& colId) -> uint32_t {
                for (const auto& c : required.columns) {
                    if (c.id == colId)
                        return c.position;
                }
                const Axis* axis = targetSheet->getColumn(colId);
                return axis ? axis->position : 0;
            };
            auto findRowPos = [&required, targetSheet](const ID& rowId) -> uint32_t {
                for (const auto& r : required.rows) {
                    if (r.id == rowId)
                        return r.position;
                }
                const Axis* axis = targetSheet->getRow(rowId);
                return axis ? axis->position : 0;
            };
            uint32_t colPos = findColPos(pendingCell.colId);
            uint32_t rowPos = findRowPos(pendingCell.rowId);
            const Axis* c = targetSheet->getColumnByPosition(colPos);
            const Axis* r = targetSheet->getRowByPosition(rowPos);
            if (c && r) {
                targetSheet->getOrCreateCellAt(c->id, r->id);
            }
        }
    }

    // Get cell value as double
    double getCellNumber(Sheet* sheet, const ID* colIds, const ID* rowIds, uint32_t col,
                         uint32_t row) {
        Cell* cell = sheet->getCellAt(colIds[col], rowIds[row]);
        if (!cell) {
            return 0.0;
        }
        return cell->value.asNumber();
    }

    double getSheet1Number(uint32_t col, uint32_t row) {
        return getCellNumber(sheet1, sheet1ColIds, sheet1RowIds, col, row);
    }

    double getSheet2Number(uint32_t col, uint32_t row) {
        return getCellNumber(sheet2, sheet2ColIds, sheet2RowIds, col, row);
    }

    // Get cell value as string
    std::string getCellString(Sheet* sheet, const ID* colIds, const ID* rowIds, uint32_t col,
                              uint32_t row) {
        Cell* cell = sheet->getCellAt(colIds[col], rowIds[row]);
        if (!cell) {
            return "";
        }
        return cell->value.asString();
    }

    std::string getSheet1String(uint32_t col, uint32_t row) {
        return getCellString(sheet1, sheet1ColIds, sheet1RowIds, col, row);
    }

    // Check if cell has error
    bool cellHasError(Sheet* sheet, const ID* colIds, const ID* rowIds, uint32_t col, uint32_t row,
                      CellError expectedError) {
        Cell* cell = sheet->getCellAt(colIds[col], rowIds[row]);
        if (!cell) {
            return false;
        }
        const bool isError = cell->value.type == CellValueType::ERROR ||
                             cell->value.type == CellValueType::FORMULA_ERROR;
        return isError && cell->value.error == expectedError;
    }

    bool sheet1HasError(uint32_t col, uint32_t row, CellError error) {
        return cellHasError(sheet1, sheet1ColIds, sheet1RowIds, col, row, error);
    }

    // Get cell at position
    Cell* getCell(Sheet* sheet, const ID* colIds, const ID* rowIds, uint32_t col, uint32_t row) {
        return sheet->getCellAt(colIds[col], rowIds[row]);
    }

    Cell* getSheet1Cell(uint32_t col, uint32_t row) {
        return getCell(sheet1, sheet1ColIds, sheet1RowIds, col, row);
    }

    Cell* getSheet2Cell(uint32_t col, uint32_t row) {
        return getCell(sheet2, sheet2ColIds, sheet2RowIds, col, row);
    }

    // Get formula display string
    std::string getFormulaDisplay(Sheet* contextSheet, Cell* cell) {
        if (!cell || !cell->getFormula() || !cell->getFormula()->ast) {
            return "";
        }
        FormulaDisplayConverter converter(*contextSheet, workbook.get());
        return converter.toDisplayString(cell->getFormula()->ast);
    }

    std::unique_ptr<Workbook> workbook;
    Sheet* sheet1 = nullptr;
    Sheet* sheet2 = nullptr;
    ID sheet1ColIds[10];  // A=0, B=1, ..., J=9
    ID sheet1RowIds[20];  // Row 1=0, ..., Row 20=19
    ID sheet2ColIds[10];  // A=0, B=1, ..., J=9
    ID sheet2RowIds[20];  // Row 1=0, ..., Row 20=19
};

// =============================================================================
// 9a: Test Cross-Sheet Formula References (Sheet2!A1)
// =============================================================================
// Tests basic cross-sheet cell references including parsing, resolution,
// evaluation, and display.

TEST_F(CrossSheetOperationsTest, CrossSheetRef_SimpleCellReference) {
    // Set up Sheet2!A1 = 42
    setSheet2Value(0, 0, 42.0);

    // Create formula on Sheet1!B1 = =Sheet2!A1
    Cell* b1 = setSheet1Formula(1, 0, "=Sheet2!A1");
    ASSERT_NE(b1, nullptr);

    // Evaluate and check
    EvalResult result = evaluateCell(sheet1, b1);
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 42.0);
}

TEST_F(CrossSheetOperationsTest, CrossSheetRef_ArithmeticWithCrossSheetRef) {
    // Sheet2!A1 = 10, Sheet2!A2 = 20
    setSheet2Value(0, 0, 10.0);
    setSheet2Value(0, 1, 20.0);

    // Sheet1!B1 = =Sheet2!A1 + Sheet2!A2
    Cell* b1 = setSheet1Formula(1, 0, "=Sheet2!A1+Sheet2!A2");
    ASSERT_NE(b1, nullptr);

    EvalResult result = evaluateCell(sheet1, b1);
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 30.0);
}

TEST_F(CrossSheetOperationsTest, CrossSheetRef_MixedLocalAndCrossSheetRefs) {
    // Sheet1!A1 = 5
    setSheet1Value(0, 0, 5.0);
    // Sheet2!A1 = 10
    setSheet2Value(0, 0, 10.0);

    // Sheet1!B1 = =A1 + Sheet2!A1 (local + cross-sheet)
    Cell* b1 = setSheet1Formula(1, 0, "=A1+Sheet2!A1");
    ASSERT_NE(b1, nullptr);

    EvalResult result = evaluateCell(sheet1, b1);
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 15.0);
}

TEST_F(CrossSheetOperationsTest, CrossSheetRef_AbsoluteCellReference) {
    // Sheet2!B2 = 100
    setSheet2Value(1, 1, 100.0);

    // Sheet1!A1 = =Sheet2!$B$2 (absolute reference)
    Cell* a1 = setSheet1Formula(0, 0, "=Sheet2!$B$2");
    ASSERT_NE(a1, nullptr);

    EvalResult result = evaluateCell(sheet1, a1);
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 100.0);
}

TEST_F(CrossSheetOperationsTest, CrossSheetRef_MixedAbsoluteRelativeReference) {
    // Sheet2!B3 = 77
    setSheet2Value(1, 2, 77.0);

    // Sheet1!A1 = =Sheet2!$B3 (absolute column, relative row)
    Cell* a1 = setSheet1Formula(0, 0, "=Sheet2!$B3");
    ASSERT_NE(a1, nullptr);

    EvalResult result = evaluateCell(sheet1, a1);
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 77.0);
}

TEST_F(CrossSheetOperationsTest, CrossSheetRef_DisplayCorrectlyFromSourceSheet) {
    // Sheet2!A1 = 50
    setSheet2Value(0, 0, 50.0);

    // Sheet1!B1 = =Sheet2!A1
    Cell* b1 = setSheet1Formula(1, 0, "=Sheet2!A1");
    ASSERT_NE(b1, nullptr);

    // Display from Sheet1 should show "=Sheet2!A1"
    std::string display = getFormulaDisplay(sheet1, b1);
    EXPECT_EQ(display, "=Sheet2!A1");
}

TEST_F(CrossSheetOperationsTest, CrossSheetRef_RecalculatesWhenSourceChanges) {
    // Sheet2!A1 = 10
    Cell* source = setSheet2Value(0, 0, 10.0);

    // Sheet1!B1 = =Sheet2!A1
    Cell* b1 = setSheet1Formula(1, 0, "=Sheet2!A1");
    ASSERT_NE(b1, nullptr);

    EvalResult result1 = evaluateCell(sheet1, b1);
    EXPECT_DOUBLE_EQ(result1.getNumber(), 10.0);

    // Change Sheet2!A1 to 99
    source->value = CellValue(99.0);

    // Recalculate at workbook level
    recalculate(workbook.get(), {source->id});

    EXPECT_DOUBLE_EQ(b1->value.asNumber(), 99.0);
}

TEST_F(CrossSheetOperationsTest, CrossSheetRef_InvalidSheetNameReturnsError) {
    // Try to reference a non-existent sheet
    FormulaParser parser("=NonExistentSheet!A1");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);

    FormulaResolver resolver(*workbook, *sheet1);
    ResolveResult resolveResult = resolver.resolve(ast.get());

    // Resolution should fail because sheet doesn't exist
    EXPECT_FALSE(resolveResult.success);
    EXPECT_TRUE(resolveResult.errorMessage.find("not found") != std::string::npos ||
                resolveResult.errorMessage.find("Sheet") != std::string::npos);
}

TEST_F(CrossSheetOperationsTest, CrossSheetRef_QuotedSheetNameWithSpaces) {
    // Rename Sheet2 to have a space
    sheet2->name = "My Sheet";

    // Set value
    setSheet2Value(0, 0, 123.0);

    // Sheet1!A1 = ='My Sheet'!A1
    Cell* a1 = setSheet1Formula(0, 0, "='My Sheet'!A1");
    ASSERT_NE(a1, nullptr);

    EvalResult result = evaluateCell(sheet1, a1);
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 123.0);

    // Display should use quoted format
    std::string display = getFormulaDisplay(sheet1, a1);
    EXPECT_EQ(display, "='My Sheet'!A1");
}

// =============================================================================
// 9b: Test Cross-Sheet Reference Updates When Target Sheet Renamed
// =============================================================================
// Tests that formulas automatically update their display when the referenced
// sheet is renamed. The formula's internal UUID representation remains unchanged,
// but the display should reflect the new sheet name.

TEST_F(CrossSheetOperationsTest, SheetRename_FormulaDisplayUpdates) {
    // Set up Sheet2!A1 = 50
    setSheet2Value(0, 0, 50.0);

    // Create formula on Sheet1!B1 = =Sheet2!A1
    Cell* b1 = setSheet1Formula(1, 0, "=Sheet2!A1");
    ASSERT_NE(b1, nullptr);

    // Verify initial display
    std::string display1 = getFormulaDisplay(sheet1, b1);
    EXPECT_EQ(display1, "=Sheet2!A1");

    // Rename Sheet2 to "Data"
    sheet2->name = "Data";

    // Display should now show "=Data!A1"
    std::string display2 = getFormulaDisplay(sheet1, b1);
    EXPECT_EQ(display2, "=Data!A1");
}

TEST_F(CrossSheetOperationsTest, SheetRename_FormulaValueStillWorks) {
    // Set up Sheet2!A1 = 42
    setSheet2Value(0, 0, 42.0);

    // Create formula on Sheet1!B1 = =Sheet2!A1
    Cell* b1 = setSheet1Formula(1, 0, "=Sheet2!A1");
    ASSERT_NE(b1, nullptr);

    EvalResult result1 = evaluateCell(sheet1, b1);
    EXPECT_DOUBLE_EQ(result1.getNumber(), 42.0);

    // Rename Sheet2 to "RenamedSheet"
    sheet2->name = "RenamedSheet";

    // Formula should still evaluate correctly
    EvalResult result2 = evaluateCell(sheet1, b1);
    EXPECT_DOUBLE_EQ(result2.getNumber(), 42.0);
}

TEST_F(CrossSheetOperationsTest, SheetRename_MultipleReferencesUpdate) {
    // Set up values on Sheet2
    setSheet2Value(0, 0, 10.0);  // A1
    setSheet2Value(0, 1, 20.0);  // A2

    // Create formula with multiple cross-sheet refs: =Sheet2!A1+Sheet2!A2
    Cell* b1 = setSheet1Formula(1, 0, "=Sheet2!A1+Sheet2!A2");
    ASSERT_NE(b1, nullptr);

    std::string display1 = getFormulaDisplay(sheet1, b1);
    EXPECT_EQ(display1, "=Sheet2!A1+Sheet2!A2");

    // Rename Sheet2
    sheet2->name = "Values";

    // Both references should update
    std::string display2 = getFormulaDisplay(sheet1, b1);
    EXPECT_EQ(display2, "=Values!A1+Values!A2");
}

TEST_F(CrossSheetOperationsTest, SheetRename_RangeReferenceUpdates) {
    // Set up values on Sheet2
    setSheet2Value(0, 0, 1.0);
    setSheet2Value(0, 1, 2.0);
    setSheet2Value(0, 2, 3.0);

    // Create formula with range: =SUM(Sheet2!A1:A3)
    Cell* b1 = setSheet1Formula(1, 0, "=SUM(Sheet2!A1:A3)");
    ASSERT_NE(b1, nullptr);

    std::string display1 = getFormulaDisplay(sheet1, b1);
    EXPECT_EQ(display1, "=SUM(Sheet2!A1:A3)");

    // Rename Sheet2
    sheet2->name = "Numbers";

    // Range reference should update
    std::string display2 = getFormulaDisplay(sheet1, b1);
    EXPECT_EQ(display2, "=SUM(Numbers!A1:A3)");
}

TEST_F(CrossSheetOperationsTest, SheetRename_ToNameWithSpaces_QuotedInDisplay) {
    // Set up Sheet2!A1 = 100
    setSheet2Value(0, 0, 100.0);

    // Create formula
    Cell* b1 = setSheet1Formula(1, 0, "=Sheet2!A1");
    ASSERT_NE(b1, nullptr);

    // Rename to a name with spaces
    sheet2->name = "Sales Data";

    // Display should use quoted format
    std::string display = getFormulaDisplay(sheet1, b1);
    EXPECT_EQ(display, "='Sales Data'!A1");
}

TEST_F(CrossSheetOperationsTest, SheetRename_FromSpacesToNoSpaces) {
    // Start with a sheet name that has spaces
    sheet2->name = "My Data";
    setSheet2Value(0, 0, 55.0);

    // Create formula
    Cell* b1 = setSheet1Formula(1, 0, "='My Data'!A1");
    ASSERT_NE(b1, nullptr);

    std::string display1 = getFormulaDisplay(sheet1, b1);
    EXPECT_EQ(display1, "='My Data'!A1");

    // Rename to a simple name
    sheet2->name = "Data";

    // Display should no longer need quotes
    std::string display2 = getFormulaDisplay(sheet1, b1);
    EXPECT_EQ(display2, "=Data!A1");
}

TEST_F(CrossSheetOperationsTest, SheetRename_EvaluationStillWorks) {
    // Sheet renames have ZERO impact on formula evaluation because
    // everything is UUID-based. The evaluator checks cellId first
    // (which is globally unique) before falling back to sheetName.

    // Set up Sheet2!A1 = 10
    Cell* source = setSheet2Value(0, 0, 10.0);

    // Create formula on Sheet1
    Cell* b1 = setSheet1Formula(1, 0, "=Sheet2!A1");
    ASSERT_NE(b1, nullptr);

    // Verify initial evaluation works
    EvalResult result1 = evaluateCell(sheet1, b1);
    EXPECT_DOUBLE_EQ(result1.getNumber(), 10.0);

    // Rename sheet
    sheet2->name = "Renamed";

    // Change source value and recalculate
    source->value = CellValue(77.0);
    recalculate(workbook.get(), {source->id});

    // Formula should still evaluate correctly after rename
    EXPECT_DOUBLE_EQ(b1->value.asNumber(), 77.0);
}

// =============================================================================
// 9c: Test Cross-Sheet Reference Becomes #REF! When Target Sheet Deleted
// =============================================================================
// When a sheet is deleted, all formulas referencing that sheet should
// evaluate to #REF! error because the referenced cells no longer exist.

TEST_F(CrossSheetOperationsTest, SheetDelete_SimpleRefBecomesRefError) {
    // Set up Sheet2!A1 = 42
    Cell* sheet2A1 = setSheet2Value(0, 0, 42.0);
    ID sheet2A1Id = sheet2A1->id;

    // Create formula on Sheet1!B1 = =Sheet2!A1
    Cell* b1 = setSheet1Formula(1, 0, "=Sheet2!A1");
    ASSERT_NE(b1, nullptr);

    // Check the AST has the cellId
    ASSERT_NE(b1->getFormula(), nullptr);
    ASSERT_NE(b1->getFormula()->ast, nullptr);

    // Verify formula works initially
    EvalResult result1 = evaluateCell(sheet1, b1);
    EXPECT_TRUE(result1.isNumber());
    EXPECT_DOUBLE_EQ(result1.getNumber(), 42.0);

    // Verify Sheet2!A1 cell exists in workbook before delete
    EXPECT_NE(workbook->getCell(sheet2A1Id), nullptr);

    // Delete Sheet2
    ID sheet2Id = sheet2->id;
    bool removed = workbook->removeSheet(sheet2Id);
    EXPECT_TRUE(removed);
    sheet2 = nullptr;  // Invalidate pointer

    // Verify Sheet2!A1 cell was removed from workbook
    EXPECT_EQ(workbook->getCell(sheet2A1Id), nullptr);

    // Re-evaluate formula - should now be #REF!
    // The removeSheet operation should have marked dependent formulas as dirty
    EvalResult result2 = evaluateCell(sheet1, b1);
    EXPECT_TRUE(result2.isError());
    EXPECT_EQ(result2.getError(), CellError::REF);
}

TEST_F(CrossSheetOperationsTest, SheetDelete_MultipleRefsAllBecomeRefError) {
    // Set up Sheet2 values
    setSheet2Value(0, 0, 10.0);  // A1
    setSheet2Value(0, 1, 20.0);  // A2

    // Create formula with multiple cross-sheet refs
    Cell* b1 = setSheet1Formula(1, 0, "=Sheet2!A1+Sheet2!A2");
    ASSERT_NE(b1, nullptr);

    // Verify formula works initially
    EvalResult result1 = evaluateCell(sheet1, b1);
    EXPECT_DOUBLE_EQ(result1.getNumber(), 30.0);

    // Delete Sheet2
    ID sheet2Id = sheet2->id;
    workbook->removeSheet(sheet2Id);
    sheet2 = nullptr;

    // Formula should be #REF!
    EvalResult result2 = evaluateCell(sheet1, b1);
    EXPECT_TRUE(result2.isError());
    EXPECT_EQ(result2.getError(), CellError::REF);
}

TEST_F(CrossSheetOperationsTest, SheetDelete_MixedRefsOnlyDeletedBecomesRefError) {
    // Set up Sheet1!A1 = 5, Sheet2!A1 = 10
    setSheet1Value(0, 0, 5.0);
    setSheet2Value(0, 0, 10.0);

    // Create formula: =A1 + Sheet2!A1 (mixed local and cross-sheet)
    Cell* b1 = setSheet1Formula(1, 0, "=A1+Sheet2!A1");
    ASSERT_NE(b1, nullptr);

    // Verify formula works initially
    EvalResult result1 = evaluateCell(sheet1, b1);
    EXPECT_DOUBLE_EQ(result1.getNumber(), 15.0);

    // Delete Sheet2
    ID sheet2Id = sheet2->id;
    workbook->removeSheet(sheet2Id);
    sheet2 = nullptr;

    // Formula should be #REF! (error from Sheet2!A1 propagates)
    EvalResult result2 = evaluateCell(sheet1, b1);
    EXPECT_TRUE(result2.isError());
    EXPECT_EQ(result2.getError(), CellError::REF);
}

TEST_F(CrossSheetOperationsTest, SheetDelete_RangeRefBecomesRefError) {
    // Set up Sheet2 values
    setSheet2Value(0, 0, 1.0);
    setSheet2Value(0, 1, 2.0);
    setSheet2Value(0, 2, 3.0);

    // Create formula with range: =SUM(Sheet2!A1:A3)
    Cell* b1 = setSheet1Formula(1, 0, "=SUM(Sheet2!A1:A3)");
    ASSERT_NE(b1, nullptr);

    // Verify formula works initially
    EvalResult result1 = evaluateCell(sheet1, b1);
    EXPECT_DOUBLE_EQ(result1.getNumber(), 6.0);

    // Delete Sheet2
    ID sheet2Id = sheet2->id;
    workbook->removeSheet(sheet2Id);
    sheet2 = nullptr;

    // Formula should be #REF!
    EvalResult result2 = evaluateCell(sheet1, b1);
    EXPECT_TRUE(result2.isError());
    EXPECT_EQ(result2.getError(), CellError::REF);
}

TEST_F(CrossSheetOperationsTest, SheetDelete_ChainedFormulasAllBecomeRefError) {
    // Chain: Sheet2!A1 -> Sheet1!B1 -> Sheet1!C1
    setSheet2Value(0, 0, 100.0);

    Cell* b1 = setSheet1Formula(1, 0, "=Sheet2!A1");
    ASSERT_NE(b1, nullptr);

    Cell* c1 = setSheet1Formula(2, 0, "=B1*2");
    ASSERT_NE(c1, nullptr);

    // Verify chain works initially
    EvalResult resultB1 = evaluateCell(sheet1, b1);
    EXPECT_DOUBLE_EQ(resultB1.getNumber(), 100.0);

    EvalResult resultC1 = evaluateCell(sheet1, c1);
    EXPECT_DOUBLE_EQ(resultC1.getNumber(), 200.0);

    // Delete Sheet2
    ID sheet2Id = sheet2->id;
    workbook->removeSheet(sheet2Id);
    sheet2 = nullptr;

    // B1 should be #REF! directly
    EvalResult result2B1 = evaluateCell(sheet1, b1);
    EXPECT_TRUE(result2B1.isError());
    EXPECT_EQ(result2B1.getError(), CellError::REF);

    // C1 should propagate the #REF! error
    EvalResult result2C1 = evaluateCell(sheet1, c1);
    EXPECT_TRUE(result2C1.isError());
    EXPECT_EQ(result2C1.getError(), CellError::REF);
}

TEST_F(CrossSheetOperationsTest, SheetDelete_RecalculatesCorrectlyAfterDelete) {
    // Set up Sheet2!A1 = 50
    setSheet2Value(0, 0, 50.0);

    // Create formula
    Cell* b1 = setSheet1Formula(1, 0, "=Sheet2!A1");
    ASSERT_NE(b1, nullptr);

    // Evaluate and store in cell
    evaluateCell(sheet1, b1);
    EXPECT_DOUBLE_EQ(b1->value.asNumber(), 50.0);

    // Delete Sheet2 - this should mark dependent formulas as dirty
    ID sheet2Id = sheet2->id;
    workbook->removeSheet(sheet2Id);
    sheet2 = nullptr;

    // Recalculate - formula should already be dirty from sheet deletion
    recalculate(workbook.get(), {b1->id});

    // Cell should now have #REF! error
    EXPECT_TRUE(b1->value.type == CellValueType::ERROR ||
                b1->value.type == CellValueType::FORMULA_ERROR);
    EXPECT_EQ(b1->value.error, CellError::REF);
}

TEST_F(CrossSheetOperationsTest, SheetDelete_AbsoluteRefBecomesRefError) {
    // Set up Sheet2!B2 = 77
    setSheet2Value(1, 1, 77.0);

    // Create formula with absolute reference
    Cell* a1 = setSheet1Formula(0, 0, "=Sheet2!$B$2");
    ASSERT_NE(a1, nullptr);

    // Verify formula works initially
    EvalResult result1 = evaluateCell(sheet1, a1);
    EXPECT_DOUBLE_EQ(result1.getNumber(), 77.0);

    // Delete Sheet2
    ID sheet2Id = sheet2->id;
    workbook->removeSheet(sheet2Id);
    sheet2 = nullptr;

    // Formula should be #REF!
    EvalResult result2 = evaluateCell(sheet1, a1);
    EXPECT_TRUE(result2.isError());
    EXPECT_EQ(result2.getError(), CellError::REF);
}

// =============================================================================
// 9d: Test Cross-Sheet Range References
// =============================================================================
// Tests cross-sheet range references (Sheet2!A1:C3) including SUM, AVERAGE, MIN,
// MAX, COUNT operations, recalculation, and multi-dimensional ranges.

TEST_F(CrossSheetOperationsTest, CrossSheetRange_SumFunction) {
    // Set up Sheet2!A1:A3 = 1, 2, 3
    setSheet2Value(0, 0, 1.0);
    setSheet2Value(0, 1, 2.0);
    setSheet2Value(0, 2, 3.0);

    // Create formula on Sheet1!B1 = =SUM(Sheet2!A1:A3)
    Cell* b1 = setSheet1Formula(1, 0, "=SUM(Sheet2!A1:A3)");
    ASSERT_NE(b1, nullptr);

    EvalResult result = evaluateCell(sheet1, b1);
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 6.0);
}

TEST_F(CrossSheetOperationsTest, CrossSheetRange_AverageFunction) {
    // Set up Sheet2!A1:A4 = 10, 20, 30, 40
    setSheet2Value(0, 0, 10.0);
    setSheet2Value(0, 1, 20.0);
    setSheet2Value(0, 2, 30.0);
    setSheet2Value(0, 3, 40.0);

    // Create formula on Sheet1!B1 = =AVERAGE(Sheet2!A1:A4)
    Cell* b1 = setSheet1Formula(1, 0, "=AVERAGE(Sheet2!A1:A4)");
    ASSERT_NE(b1, nullptr);

    EvalResult result = evaluateCell(sheet1, b1);
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 25.0);
}

TEST_F(CrossSheetOperationsTest, CrossSheetRange_MinFunction) {
    // Set up Sheet2!A1:A3 with mixed values
    setSheet2Value(0, 0, 100.0);
    setSheet2Value(0, 1, 5.0);
    setSheet2Value(0, 2, 50.0);

    // Create formula on Sheet1!B1 = =MIN(Sheet2!A1:A3)
    Cell* b1 = setSheet1Formula(1, 0, "=MIN(Sheet2!A1:A3)");
    ASSERT_NE(b1, nullptr);

    EvalResult result = evaluateCell(sheet1, b1);
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 5.0);
}

TEST_F(CrossSheetOperationsTest, CrossSheetRange_MaxFunction) {
    // Set up Sheet2!A1:A3 with mixed values
    setSheet2Value(0, 0, 15.0);
    setSheet2Value(0, 1, 99.0);
    setSheet2Value(0, 2, 42.0);

    // Create formula on Sheet1!B1 = =MAX(Sheet2!A1:A3)
    Cell* b1 = setSheet1Formula(1, 0, "=MAX(Sheet2!A1:A3)");
    ASSERT_NE(b1, nullptr);

    EvalResult result = evaluateCell(sheet1, b1);
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 99.0);
}

TEST_F(CrossSheetOperationsTest, CrossSheetRange_CountFunction) {
    // Set up Sheet2!A1:A5 with mix of numbers and empty cells
    setSheet2Value(0, 0, 1.0);
    setSheet2Value(0, 1, 2.0);
    // A3 is empty
    setSheet2Value(0, 3, 4.0);
    setSheet2Value(0, 4, 5.0);

    // Create formula on Sheet1!B1 = =COUNT(Sheet2!A1:A5)
    Cell* b1 = setSheet1Formula(1, 0, "=COUNT(Sheet2!A1:A5)");
    ASSERT_NE(b1, nullptr);

    EvalResult result = evaluateCell(sheet1, b1);
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 4.0);  // 4 numbers
}

TEST_F(CrossSheetOperationsTest, CrossSheetRange_MultiColumnRange) {
    // Set up 2x3 range on Sheet2 (A1:B3)
    // A1=1, B1=2
    // A2=3, B2=4
    // A3=5, B3=6
    setSheet2Value(0, 0, 1.0);
    setSheet2Value(1, 0, 2.0);
    setSheet2Value(0, 1, 3.0);
    setSheet2Value(1, 1, 4.0);
    setSheet2Value(0, 2, 5.0);
    setSheet2Value(1, 2, 6.0);

    // Create formula on Sheet1!C1 = =SUM(Sheet2!A1:B3)
    Cell* c1 = setSheet1Formula(2, 0, "=SUM(Sheet2!A1:B3)");
    ASSERT_NE(c1, nullptr);

    EvalResult result = evaluateCell(sheet1, c1);
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 21.0);  // 1+2+3+4+5+6
}

TEST_F(CrossSheetOperationsTest, CrossSheetRange_MultiColumnAverage) {
    // Set up 3x2 range on Sheet2 (A1:C2)
    // A1=10, B1=20, C1=30
    // A2=40, B2=50, C2=60
    setSheet2Value(0, 0, 10.0);
    setSheet2Value(1, 0, 20.0);
    setSheet2Value(2, 0, 30.0);
    setSheet2Value(0, 1, 40.0);
    setSheet2Value(1, 1, 50.0);
    setSheet2Value(2, 1, 60.0);

    // Create formula on Sheet1!D1 = =AVERAGE(Sheet2!A1:C2)
    Cell* d1 = setSheet1Formula(3, 0, "=AVERAGE(Sheet2!A1:C2)");
    ASSERT_NE(d1, nullptr);

    EvalResult result = evaluateCell(sheet1, d1);
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 35.0);  // (10+20+30+40+50+60)/6 = 210/6 = 35
}

TEST_F(CrossSheetOperationsTest, CrossSheetRange_RecalculatesWhenSourceChanges) {
    // Set up Sheet2!A1:A3 = 1, 2, 3
    Cell* a1 = setSheet2Value(0, 0, 1.0);
    setSheet2Value(0, 1, 2.0);
    setSheet2Value(0, 2, 3.0);

    // Create formula on Sheet1!B1 = =SUM(Sheet2!A1:A3)
    Cell* b1 = setSheet1Formula(1, 0, "=SUM(Sheet2!A1:A3)");
    ASSERT_NE(b1, nullptr);

    EvalResult result1 = evaluateCell(sheet1, b1);
    EXPECT_DOUBLE_EQ(result1.getNumber(), 6.0);

    // Change Sheet2!A1 to 100
    a1->value = CellValue(100.0);

    // Recalculate
    recalculate(workbook.get(), {a1->id});

    EXPECT_DOUBLE_EQ(b1->value.asNumber(), 105.0);  // 100+2+3
}

TEST_F(CrossSheetOperationsTest, CrossSheetRange_DisplayUpdatesOnSheetRename) {
    // Set up Sheet2!A1:A3
    setSheet2Value(0, 0, 1.0);
    setSheet2Value(0, 1, 2.0);
    setSheet2Value(0, 2, 3.0);

    // Create formula
    Cell* b1 = setSheet1Formula(1, 0, "=SUM(Sheet2!A1:A3)");
    ASSERT_NE(b1, nullptr);

    // Verify initial display
    std::string display1 = getFormulaDisplay(sheet1, b1);
    EXPECT_EQ(display1, "=SUM(Sheet2!A1:A3)");

    // Rename Sheet2
    sheet2->name = "DataSheet";

    // Display should update
    std::string display2 = getFormulaDisplay(sheet1, b1);
    EXPECT_EQ(display2, "=SUM(DataSheet!A1:A3)");
}

TEST_F(CrossSheetOperationsTest, CrossSheetRange_EvaluationStillWorksAfterRename) {
    // Set up Sheet2!A1:A3 = 10, 20, 30
    setSheet2Value(0, 0, 10.0);
    setSheet2Value(0, 1, 20.0);
    setSheet2Value(0, 2, 30.0);

    // Create formula
    Cell* b1 = setSheet1Formula(1, 0, "=SUM(Sheet2!A1:A3)");
    ASSERT_NE(b1, nullptr);

    EvalResult result1 = evaluateCell(sheet1, b1);
    EXPECT_DOUBLE_EQ(result1.getNumber(), 60.0);

    // Rename Sheet2
    sheet2->name = "RenamedSheet";

    // Evaluation should still work (UUID-based)
    EvalResult result2 = evaluateCell(sheet1, b1);
    EXPECT_DOUBLE_EQ(result2.getNumber(), 60.0);
}

TEST_F(CrossSheetOperationsTest, CrossSheetRange_BecomesRefErrorWhenSheetDeleted) {
    // Set up Sheet2!A1:A3
    setSheet2Value(0, 0, 1.0);
    setSheet2Value(0, 1, 2.0);
    setSheet2Value(0, 2, 3.0);

    // Create formula
    Cell* b1 = setSheet1Formula(1, 0, "=SUM(Sheet2!A1:A3)");
    ASSERT_NE(b1, nullptr);

    EvalResult result1 = evaluateCell(sheet1, b1);
    EXPECT_DOUBLE_EQ(result1.getNumber(), 6.0);

    // Delete Sheet2
    ID sheet2Id = sheet2->id;
    workbook->removeSheet(sheet2Id);
    sheet2 = nullptr;

    // Formula should be #REF!
    EvalResult result2 = evaluateCell(sheet1, b1);
    EXPECT_TRUE(result2.isError());
    EXPECT_EQ(result2.getError(), CellError::REF);
}

TEST_F(CrossSheetOperationsTest, CrossSheetRange_AbsoluteRange) {
    // Set up Sheet2!A1:A3 = 5, 10, 15
    setSheet2Value(0, 0, 5.0);
    setSheet2Value(0, 1, 10.0);
    setSheet2Value(0, 2, 15.0);

    // Create formula with absolute references
    Cell* b1 = setSheet1Formula(1, 0, "=SUM(Sheet2!$A$1:$A$3)");
    ASSERT_NE(b1, nullptr);

    EvalResult result = evaluateCell(sheet1, b1);
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 30.0);
}

TEST_F(CrossSheetOperationsTest, CrossSheetRange_QuotedSheetNameWithSpaces) {
    // Rename Sheet2 to have spaces
    sheet2->name = "Sales Data";

    // Set up values
    setSheet2Value(0, 0, 100.0);
    setSheet2Value(0, 1, 200.0);
    setSheet2Value(0, 2, 300.0);

    // Create formula with quoted sheet name
    Cell* b1 = setSheet1Formula(1, 0, "=SUM('Sales Data'!A1:A3)");
    ASSERT_NE(b1, nullptr);

    EvalResult result = evaluateCell(sheet1, b1);
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 600.0);

    // Verify display
    std::string display = getFormulaDisplay(sheet1, b1);
    EXPECT_EQ(display, "=SUM('Sales Data'!A1:A3)");
}

TEST_F(CrossSheetOperationsTest, CrossSheetRange_MixedLocalAndCrossSheetRanges) {
    // Set up local values on Sheet1!A1:A2
    setSheet1Value(0, 0, 10.0);
    setSheet1Value(0, 1, 20.0);

    // Set up cross-sheet values on Sheet2!A1:A2
    setSheet2Value(0, 0, 100.0);
    setSheet2Value(0, 1, 200.0);

    // Create formula with both local and cross-sheet ranges
    Cell* c1 = setSheet1Formula(2, 0, "=SUM(A1:A2)+SUM(Sheet2!A1:A2)");
    ASSERT_NE(c1, nullptr);

    EvalResult result = evaluateCell(sheet1, c1);
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 330.0);  // (10+20) + (100+200)
}

TEST_F(CrossSheetOperationsTest, CrossSheetRange_EmptyRangeReturnsZeroForSum) {
    // Don't set any values on Sheet2 - all cells are empty

    // Create formula referencing empty range
    Cell* b1 = setSheet1Formula(1, 0, "=SUM(Sheet2!A1:A3)");
    ASSERT_NE(b1, nullptr);

    EvalResult result = evaluateCell(sheet1, b1);
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 0.0);
}

TEST_F(CrossSheetOperationsTest, CrossSheetRange_PartiallyFilledRange) {
    // Set only some values in the range
    setSheet2Value(0, 0, 10.0);  // A1
    // A2 is empty
    setSheet2Value(0, 2, 30.0);  // A3
    // A4 is empty
    setSheet2Value(0, 4, 50.0);  // A5

    // Create formula
    Cell* b1 = setSheet1Formula(1, 0, "=SUM(Sheet2!A1:A5)");
    ASSERT_NE(b1, nullptr);

    EvalResult result = evaluateCell(sheet1, b1);
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 90.0);  // 10+30+50
}

}  // namespace
}  // namespace cells
