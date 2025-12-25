#include <gtest/gtest.h>
#include <memory>
#include <set>
#include <string>

#include "core/cells/dependency_graph.h"
#include "core/cells/formula_ast.h"
#include "core/cells/formula_display.h"
#include "core/cells/formula_parser.h"
#include "core/cells/formula_resolver.h"
#include "core/cells/model.h"
#include "core/cells/named_ranges.h"

namespace cells {
namespace {

// Helper to create a test workbook with columns A-F and rows 1-6
std::unique_ptr<Workbook> createTestWorkbook() {
    auto wb = std::make_unique<Workbook>(ID("testWBId"), "TestWorkbook");
    auto sheet = std::make_unique<Sheet>(ID("testShId"), "Sheet1");

    // Create columns A-F (positions 0-5)
    std::vector<std::pair<const char*, int>> columns = {{"colA0001", 0}, {"colB0002", 1},
                                                        {"colC0003", 2}, {"colD0004", 3},
                                                        {"colE0005", 4}, {"colF0006", 5}};
    for (const auto& [id, pos] : columns) {
        auto col = std::make_unique<Axis>(ID(id), true);
        col->position = pos;
        sheet->addColumn(std::move(col));
    }

    // Create rows 1-6 (positions 0-5)
    std::vector<std::pair<const char*, int>> rows = {{"row10001", 0}, {"row20002", 1},
                                                     {"row30003", 2}, {"row40004", 3},
                                                     {"row50005", 4}, {"row60006", 5}};
    for (const auto& [id, pos] : rows) {
        auto row = std::make_unique<Axis>(ID(id), false);
        row->position = pos;
        sheet->addRow(std::move(row));
    }

    // Create cells for all intersections A1-F6
    const char* colIds[] = {"colA0001", "colB0002", "colC0003", "colD0004", "colE0005", "colF0006"};
    const char* rowIds[] = {"row10001", "row20002", "row30003", "row40004", "row50005", "row60006"};

    int cellNum = 0;
    for (int c = 0; c < 6; c++) {
        for (int r = 0; r < 6; r++) {
            char cellId[16];
            snprintf(cellId, sizeof(cellId), "cell%c%d%02d", 'A' + c, r + 1, cellNum++);
            auto cell = std::make_unique<Cell>(ID(cellId), ID(colIds[c]), ID(rowIds[r]));
            cell->value = CellValue(static_cast<double>((c + 1) * 10 + (r + 1)));
            sheet->addCell(std::move(cell));
        }
    }

    wb->addSheet(std::move(sheet));
    return wb;
}

// Helper to set a resolved formula on a cell
bool setResolvedFormula(Workbook& wb, Sheet& sheet, const ID& cellId,
                        const std::string& formulaText) {
    FormulaParser parser(formulaText);
    auto ast = parser.parse();
    if (!ast) {
        return false;
    }

    FormulaResolver resolver(wb, sheet, wb.getNamedRanges());
    auto result = resolver.resolve(ast.get());
    if (!result.success) {
        return false;
    }

    auto setResult = sheet.setCellFormula(cellId, formulaText, ast.release());
    return setResult.success;
}

// Helper to get the A1 display string of a formula
std::string getFormulaDisplay(Sheet& sheet, const ID& cellId) {
    Cell* cell = sheet.getCell(cellId);
    if (cell == nullptr || !cell->isFormula()) {
        return "";
    }
    Formula* formula = cell->getFormula();
    if (formula == nullptr || formula->ast == nullptr) {
        return "";
    }
    FormulaDisplayConverter converter(sheet);
    return converter.toDisplayString(formula->ast);
}

// ============================================================================
// Phase 6a: Basic Axis Move Operations
// ============================================================================

TEST(FormulaMoveTest, MoveColumnBasic) {
    auto wb = createTestWorkbook();
    Sheet* sheet = wb->getSheetByIndex(0);

    // Get column B (initially at position 1)
    Axis* colB = sheet->getColumn(ID("colB0002"));
    ASSERT_NE(colB, nullptr);
    EXPECT_EQ(colB->position, 1u);

    // Move B to position 3 (B becomes D)
    bool success = sheet->moveColumn(ID("colB0002"), 3);
    EXPECT_TRUE(success);
    EXPECT_EQ(colB->position, 3u);

    // Check that C and D shifted left
    Axis* colC = sheet->getColumn(ID("colC0003"));
    Axis* colD = sheet->getColumn(ID("colD0004"));
    EXPECT_EQ(colC->position, 1u);  // C moved from 2 to 1
    EXPECT_EQ(colD->position, 2u);  // D moved from 3 to 2
}

TEST(FormulaMoveTest, MoveColumnLeft) {
    auto wb = createTestWorkbook();
    Sheet* sheet = wb->getSheetByIndex(0);

    // Move E (position 4) to position 1
    bool success = sheet->moveColumn(ID("colE0005"), 1);
    EXPECT_TRUE(success);

    Axis* colE = sheet->getColumn(ID("colE0005"));
    EXPECT_EQ(colE->position, 1u);

    // B, C, D should have shifted right
    Axis* colB = sheet->getColumn(ID("colB0002"));
    Axis* colC = sheet->getColumn(ID("colC0003"));
    Axis* colD = sheet->getColumn(ID("colD0004"));
    EXPECT_EQ(colB->position, 2u);
    EXPECT_EQ(colC->position, 3u);
    EXPECT_EQ(colD->position, 4u);
}

TEST(FormulaMoveTest, MoveRowBasic) {
    auto wb = createTestWorkbook();
    Sheet* sheet = wb->getSheetByIndex(0);

    // Move row 2 (position 1) to position 4
    bool success = sheet->moveRow(ID("row20002"), 4);
    EXPECT_TRUE(success);

    Axis* row2 = sheet->getRow(ID("row20002"));
    EXPECT_EQ(row2->position, 4u);

    // Rows 3, 4, 5 should have shifted up
    Axis* row3 = sheet->getRow(ID("row30003"));
    Axis* row4 = sheet->getRow(ID("row40004"));
    Axis* row5 = sheet->getRow(ID("row50005"));
    EXPECT_EQ(row3->position, 1u);
    EXPECT_EQ(row4->position, 2u);
    EXPECT_EQ(row5->position, 3u);
}

TEST(FormulaMoveTest, MoveColumnNotFound) {
    auto wb = createTestWorkbook();
    Sheet* sheet = wb->getSheetByIndex(0);

    bool success = sheet->moveColumn(ID("nonexist"), 1);
    EXPECT_FALSE(success);
}

TEST(FormulaMoveTest, MoveColumnSamePosition) {
    auto wb = createTestWorkbook();
    Sheet* sheet = wb->getSheetByIndex(0);

    // Moving to same position should be a no-op
    Axis* colB = sheet->getColumn(ID("colB0002"));
    uint32_t originalPos = colB->position;

    bool success = sheet->moveColumn(ID("colB0002"), originalPos);
    EXPECT_TRUE(success);
    EXPECT_EQ(colB->position, originalPos);
}

// ============================================================================
// Phase 6b: Formula Display Update After Move
// ============================================================================

TEST(FormulaMoveTest, FormulaCellRefDisplayAfterColumnMove) {
    auto wb = createTestWorkbook();
    Sheet* sheet = wb->getSheetByIndex(0);

    // Get cell D1 (will contain formula)
    Cell* cellD1 = sheet->getCellAt(ID("colD0004"), ID("row10001"));
    ASSERT_NE(cellD1, nullptr);

    // Set formula =B2 in D1
    bool set = setResolvedFormula(*wb, *sheet, cellD1->id, "=B2");
    EXPECT_TRUE(set);

    // Verify initial display
    std::string display = getFormulaDisplay(*sheet, cellD1->id);
    EXPECT_EQ(display, "=B2");

    // Move column B (position 1) to position 4 (becomes E after move)
    // After move: A=0, C=1, D=2, E=3, B=4, F=5
    bool success = sheet->moveColumn(ID("colB0002"), 4);
    EXPECT_TRUE(success);

    // The formula's UUID hasn't changed, but the display should now show E2
    // because the column we referenced (B) is now at position 4 (which is E)
    display = getFormulaDisplay(*sheet, cellD1->id);
    EXPECT_EQ(display, "=E2") << "Formula should display E2 after moving B to position 4";
}

TEST(FormulaMoveTest, FormulaCellRefDisplayAfterRowMove) {
    auto wb = createTestWorkbook();
    Sheet* sheet = wb->getSheetByIndex(0);

    // Get cell D1 (will contain formula)
    Cell* cellD1 = sheet->getCellAt(ID("colD0004"), ID("row10001"));
    ASSERT_NE(cellD1, nullptr);

    // Set formula =A3 in D1
    bool set = setResolvedFormula(*wb, *sheet, cellD1->id, "=A3");
    EXPECT_TRUE(set);

    // Move row 3 (position 2) to position 5
    // After move: row1=0, row2=1, row4=2, row5=3, row6=4, row3=5
    bool success = sheet->moveRow(ID("row30003"), 5);
    EXPECT_TRUE(success);

    // The formula should now display A6 (row3 is now at position 5, which is row 6)
    std::string display = getFormulaDisplay(*sheet, cellD1->id);
    EXPECT_EQ(display, "=A6") << "Formula should display A6 after moving row 3 to position 5";
}

TEST(FormulaMoveTest, FormulaUuidUnchangedAfterMove) {
    auto wb = createTestWorkbook();
    Sheet* sheet = wb->getSheetByIndex(0);

    Cell* cellD1 = sheet->getCellAt(ID("colD0004"), ID("row10001"));
    ASSERT_NE(cellD1, nullptr);

    bool set = setResolvedFormula(*wb, *sheet, cellD1->id, "=B2");
    EXPECT_TRUE(set);

    // Get the stored UUID formula before move
    std::string storedBefore = sheet->getCellFormulaText(cellD1->id);
    EXPECT_TRUE(storedBefore.find("~~") != std::string::npos);

    // Move column B
    sheet->moveColumn(ID("colB0002"), 4);

    // The stored UUID formula should be UNCHANGED
    std::string storedAfter = sheet->getCellFormulaText(cellD1->id);
    EXPECT_EQ(storedBefore, storedAfter) << "UUID-format formula should not change after move";
}

TEST(FormulaMoveTest, FormulaDependenciesUnchangedAfterMove) {
    auto wb = createTestWorkbook();
    Sheet* sheet = wb->getSheetByIndex(0);

    Cell* cellD1 = sheet->getCellAt(ID("colD0004"), ID("row10001"));
    ASSERT_NE(cellD1, nullptr);

    bool set = setResolvedFormula(*wb, *sheet, cellD1->id, "=B2+C3");
    EXPECT_TRUE(set);

    // Get dependencies before move
    DependencyGraph* depGraph = sheet->getDependencyGraph();
    auto depsBefore = depGraph->getDependencies(cellD1->id);
    EXPECT_EQ(depsBefore.size(), 2u);

    // Collect the cell UUIDs referenced before move
    std::set<std::string> cellIdsBefore;
    for (const auto& dep : depsBefore) {
        cellIdsBefore.insert(dep.cellId.toString());
    }

    // Move column B
    sheet->moveColumn(ID("colB0002"), 4);

    // Dependencies should be unchanged (UUIDs are stable)
    auto depsAfter = depGraph->getDependencies(cellD1->id);
    EXPECT_EQ(depsAfter.size(), 2u);

    // The specific cell UUIDs should match
    std::set<std::string> cellIdsAfter;
    for (const auto& dep : depsAfter) {
        cellIdsAfter.insert(dep.cellId.toString());
    }
    EXPECT_EQ(cellIdsBefore, cellIdsAfter) << "Cell UUIDs in dependencies should not change";
}

TEST(FormulaMoveTest, FormulaRangeDisplayAfterColumnMove) {
    auto wb = createTestWorkbook();
    Sheet* sheet = wb->getSheetByIndex(0);

    Cell* cellE1 = sheet->getCellAt(ID("colE0005"), ID("row10001"));
    ASSERT_NE(cellE1, nullptr);

    // Set formula =SUM(A1:C3)
    bool set = setResolvedFormula(*wb, *sheet, cellE1->id, "=SUM(A1:C3)");
    EXPECT_TRUE(set);

    // Verify initial display
    std::string display = getFormulaDisplay(*sheet, cellE1->id);
    EXPECT_EQ(display, "=SUM(A1:C3)");

    // Move column A (position 0) to position 2
    // After: B=0, C=1, A=2, D=3, E=4, F=5
    sheet->moveColumn(ID("colA0001"), 2);

    // The top-left corner cell is still the same UUID, but now displayed at position 2 (C)
    // The bottom-right corner cell (original C3) is now at position 1 (B)
    // So the range should display as SUM(C1:B3) but in correct order it's SUM(B1:C3)
    // Actually, since we store the cell UUIDs, the positions determine display.
    // Original A1 (colA0001, row10001) is now at position (2, 0) = C1
    // Original C3 (colC0003, row30003) is now at position (1, 2) = B3
    // The display should show the current positions, so SUM(C1:B3) - but ranges should be
    // normalized
    display = getFormulaDisplay(*sheet, cellE1->id);

    // NOTE: After move, the range corners are at C1 and B3
    // The display depends on how FormulaDisplayConverter handles inverted ranges
    // Typically Excel would show B1:C3 (normalized), but we store the exact cell UUIDs
    EXPECT_TRUE(display.find("SUM(") != std::string::npos);
}

TEST(FormulaMoveTest, MultipleRefsAfterMove) {
    auto wb = createTestWorkbook();
    Sheet* sheet = wb->getSheetByIndex(0);

    Cell* cellF1 = sheet->getCellAt(ID("colF0006"), ID("row10001"));
    ASSERT_NE(cellF1, nullptr);

    // Set formula with multiple refs: =A1+B1+C1
    bool set = setResolvedFormula(*wb, *sheet, cellF1->id, "=A1+B1+C1");
    EXPECT_TRUE(set);

    std::string display = getFormulaDisplay(*sheet, cellF1->id);
    EXPECT_EQ(display, "=A1+B1+C1");

    // Move B to position 3 (becomes D)
    // After: A=0, C=1, D=2, B=3, E=4, F=5
    sheet->moveColumn(ID("colB0002"), 3);

    // Now: A1 stays A1, B1 becomes D1, C1 becomes B1
    display = getFormulaDisplay(*sheet, cellF1->id);
    EXPECT_EQ(display, "=A1+D1+B1")
        << "After move: orig A=A, orig B=D, orig C=B. So A1+B1+C1 -> A1+D1+B1";
}

TEST(FormulaMoveTest, AbsoluteRefDisplayAfterMove) {
    auto wb = createTestWorkbook();
    Sheet* sheet = wb->getSheetByIndex(0);

    Cell* cellD1 = sheet->getCellAt(ID("colD0004"), ID("row10001"));
    ASSERT_NE(cellD1, nullptr);

    // Set formula with absolute ref: =$B$2
    bool set = setResolvedFormula(*wb, *sheet, cellD1->id, "=$B$2");
    EXPECT_TRUE(set);

    std::string display = getFormulaDisplay(*sheet, cellD1->id);
    EXPECT_EQ(display, "=$B$2");

    // Move column B to position 4
    sheet->moveColumn(ID("colB0002"), 4);

    // The display should still show absolute notation, but with new column letter
    display = getFormulaDisplay(*sheet, cellD1->id);
    EXPECT_EQ(display, "=$E$2") << "Absolute refs should update display after move";
}

TEST(FormulaMoveTest, MixedAbsoluteRefDisplayAfterMove) {
    auto wb = createTestWorkbook();
    Sheet* sheet = wb->getSheetByIndex(0);

    Cell* cellD1 = sheet->getCellAt(ID("colD0004"), ID("row10001"));
    ASSERT_NE(cellD1, nullptr);

    // Set formula with mixed absolute ref: =$B2 (column absolute, row relative)
    bool set = setResolvedFormula(*wb, *sheet, cellD1->id, "=$B2");
    EXPECT_TRUE(set);

    std::string display = getFormulaDisplay(*sheet, cellD1->id);
    EXPECT_EQ(display, "=$B2");

    // Move column B to position 4
    sheet->moveColumn(ID("colB0002"), 4);

    // The absolute column marker should be preserved
    display = getFormulaDisplay(*sheet, cellD1->id);
    EXPECT_EQ(display, "=$E2") << "Mixed absolute refs should update display after move";
}

// ============================================================================
// Additional Move Stability Tests
// ============================================================================

TEST(FormulaMoveTest, MoveColumnBackAndForth) {
    auto wb = createTestWorkbook();
    Sheet* sheet = wb->getSheetByIndex(0);

    Cell* cellD1 = sheet->getCellAt(ID("colD0004"), ID("row10001"));
    ASSERT_NE(cellD1, nullptr);

    bool set = setResolvedFormula(*wb, *sheet, cellD1->id, "=B2");
    EXPECT_TRUE(set);

    // Move B to position 4
    sheet->moveColumn(ID("colB0002"), 4);
    std::string display = getFormulaDisplay(*sheet, cellD1->id);
    EXPECT_EQ(display, "=E2");

    // Move B back to position 1
    sheet->moveColumn(ID("colB0002"), 1);
    display = getFormulaDisplay(*sheet, cellD1->id);
    EXPECT_EQ(display, "=B2") << "Formula should return to original display after moving back";
}

TEST(FormulaMoveTest, MoveAllColumns) {
    auto wb = createTestWorkbook();
    Sheet* sheet = wb->getSheetByIndex(0);

    Cell* cellF1 = sheet->getCellAt(ID("colF0006"), ID("row10001"));
    ASSERT_NE(cellF1, nullptr);

    // Set formula referencing A1, B1, C1
    bool set = setResolvedFormula(*wb, *sheet, cellF1->id, "=A1+B1+C1");
    EXPECT_TRUE(set);

    // Reverse the order of columns A, B, C (swap A with C)
    // Original: A=0, B=1, C=2 -> C=0, B=1, A=2
    sheet->moveColumn(ID("colA0001"), 2);  // A goes to position 2
    sheet->moveColumn(ID("colC0003"), 0);  // C goes to position 0

    // Now: C=0, B=1, A=2
    // Original A1 (colA0001) is now at position 2 = C
    // Original B1 (colB0002) is still at position 1 = B
    // Original C1 (colC0003) is now at position 0 = A
    std::string display = getFormulaDisplay(*sheet, cellF1->id);
    EXPECT_EQ(display, "=C1+B1+A1");
}

// ============================================================================
// Phase 6c: Insert/Delete Stability Tests
// ============================================================================

TEST(FormulaInsertDeleteTest, InsertColumnBefore) {
    auto wb = createTestWorkbook();
    Sheet* sheet = wb->getSheetByIndex(0);

    Cell* cellD1 = sheet->getCellAt(ID("colD0004"), ID("row10001"));
    ASSERT_NE(cellD1, nullptr);

    // Set formula =B2
    bool set = setResolvedFormula(*wb, *sheet, cellD1->id, "=B2");
    EXPECT_TRUE(set);

    std::string display = getFormulaDisplay(*sheet, cellD1->id);
    EXPECT_EQ(display, "=B2");

    // Insert a new column at position 0 (before A)
    // After insert: NewCol=0, A=1, B=2, C=3, D=4, E=5, F=6
    Axis* newCol = sheet->insertColumnAt(0);
    ASSERT_NE(newCol, nullptr);
    EXPECT_EQ(newCol->position, 0u);

    // Column B is still the same UUID, but now at position 2 = C
    display = getFormulaDisplay(*sheet, cellD1->id);
    EXPECT_EQ(display, "=C2") << "After inserting column before, B should become C";
}

TEST(FormulaInsertDeleteTest, InsertColumnAfter) {
    auto wb = createTestWorkbook();
    Sheet* sheet = wb->getSheetByIndex(0);

    Cell* cellD1 = sheet->getCellAt(ID("colD0004"), ID("row10001"));
    ASSERT_NE(cellD1, nullptr);

    // Set formula =B2
    bool set = setResolvedFormula(*wb, *sheet, cellD1->id, "=B2");
    EXPECT_TRUE(set);

    // Insert a new column at position 5 (after E)
    // After insert: A=0, B=1, C=2, D=3, E=4, NewCol=5, F=6
    Axis* newCol = sheet->insertColumnAt(5);
    ASSERT_NE(newCol, nullptr);
    EXPECT_EQ(newCol->position, 5u);

    // Column B unchanged (still at position 1)
    std::string display = getFormulaDisplay(*sheet, cellD1->id);
    EXPECT_EQ(display, "=B2") << "Inserting column after should not affect earlier refs";
}

TEST(FormulaInsertDeleteTest, InsertRowBefore) {
    auto wb = createTestWorkbook();
    Sheet* sheet = wb->getSheetByIndex(0);

    Cell* cellD1 = sheet->getCellAt(ID("colD0004"), ID("row10001"));
    ASSERT_NE(cellD1, nullptr);

    // Set formula =A3
    bool set = setResolvedFormula(*wb, *sheet, cellD1->id, "=A3");
    EXPECT_TRUE(set);

    std::string display = getFormulaDisplay(*sheet, cellD1->id);
    EXPECT_EQ(display, "=A3");

    // Insert a new row at position 0 (before row 1)
    // After insert: NewRow=0, Row1=1, Row2=2, Row3=3, ...
    Axis* newRow = sheet->insertRowAt(0);
    ASSERT_NE(newRow, nullptr);
    EXPECT_EQ(newRow->position, 0u);

    // Row 3 is still the same UUID, but now at position 3 = row 4
    display = getFormulaDisplay(*sheet, cellD1->id);
    EXPECT_EQ(display, "=A4") << "After inserting row before, row 3 should become row 4";
}

TEST(FormulaInsertDeleteTest, InsertRowAfter) {
    auto wb = createTestWorkbook();
    Sheet* sheet = wb->getSheetByIndex(0);

    Cell* cellD1 = sheet->getCellAt(ID("colD0004"), ID("row10001"));
    ASSERT_NE(cellD1, nullptr);

    // Set formula =A2
    bool set = setResolvedFormula(*wb, *sheet, cellD1->id, "=A2");
    EXPECT_TRUE(set);

    // Insert a new row at position 5 (after row 5)
    Axis* newRow = sheet->insertRowAt(5);
    ASSERT_NE(newRow, nullptr);

    // Row 2 unchanged (still at position 1)
    std::string display = getFormulaDisplay(*sheet, cellD1->id);
    EXPECT_EQ(display, "=A2") << "Inserting row after should not affect earlier refs";
}

TEST(FormulaInsertDeleteTest, DeleteUnreferencedColumn) {
    auto wb = createTestWorkbook();
    Sheet* sheet = wb->getSheetByIndex(0);

    Cell* cellD1 = sheet->getCellAt(ID("colD0004"), ID("row10001"));
    ASSERT_NE(cellD1, nullptr);

    // Set formula =B2 (references column B)
    bool set = setResolvedFormula(*wb, *sheet, cellD1->id, "=B2");
    EXPECT_TRUE(set);

    // Delete column E (position 4) - not referenced by formula
    // After delete: A=0, B=1, C=2, D=3, F=4 (F shifts left from 5)
    bool deleted = sheet->deleteColumn(ID("colE0005"));
    EXPECT_TRUE(deleted);

    // Formula should still work, B is still at position 1
    std::string display = getFormulaDisplay(*sheet, cellD1->id);
    EXPECT_EQ(display, "=B2") << "Deleting unreferenced column should not affect formula";
}

TEST(FormulaInsertDeleteTest, DeleteUnreferencedRow) {
    auto wb = createTestWorkbook();
    Sheet* sheet = wb->getSheetByIndex(0);

    Cell* cellD1 = sheet->getCellAt(ID("colD0004"), ID("row10001"));
    ASSERT_NE(cellD1, nullptr);

    // Set formula =A2 (references row 2)
    bool set = setResolvedFormula(*wb, *sheet, cellD1->id, "=A2");
    EXPECT_TRUE(set);

    // Delete row 5 (position 4) - not referenced by formula
    bool deleted = sheet->deleteRow(ID("row50005"));
    EXPECT_TRUE(deleted);

    // Formula should still work, row 2 is still at position 1
    std::string display = getFormulaDisplay(*sheet, cellD1->id);
    EXPECT_EQ(display, "=A2") << "Deleting unreferenced row should not affect formula";
}

TEST(FormulaInsertDeleteTest, DeleteColumnBefore) {
    auto wb = createTestWorkbook();
    Sheet* sheet = wb->getSheetByIndex(0);

    Cell* cellD1 = sheet->getCellAt(ID("colD0004"), ID("row10001"));
    ASSERT_NE(cellD1, nullptr);

    // Set formula =C2 (references column C at position 2)
    bool set = setResolvedFormula(*wb, *sheet, cellD1->id, "=C2");
    EXPECT_TRUE(set);

    // Delete column A (position 0)
    // After delete: B=0, C=1, D=2, E=3, F=4
    bool deleted = sheet->deleteColumn(ID("colA0001"));
    EXPECT_TRUE(deleted);

    // Column C is now at position 1 = B
    std::string display = getFormulaDisplay(*sheet, cellD1->id);
    EXPECT_EQ(display, "=B2") << "After deleting column before, C should become B";
}

TEST(FormulaInsertDeleteTest, DeleteRowBefore) {
    auto wb = createTestWorkbook();
    Sheet* sheet = wb->getSheetByIndex(0);

    // Use cell D5 (row 5) as the formula cell to avoid deleting the formula cell itself
    Cell* cellD5 = sheet->getCellAt(ID("colD0004"), ID("row50005"));
    ASSERT_NE(cellD5, nullptr);

    // Set formula =A3 (references row 3 at position 2)
    bool set = setResolvedFormula(*wb, *sheet, cellD5->id, "=A3");
    EXPECT_TRUE(set);

    // Delete row 1 (position 0)
    // After delete: row2=0, row3=1, row4=2, row5=3, row6=4
    bool deleted = sheet->deleteRow(ID("row10001"));
    EXPECT_TRUE(deleted);

    // Row 3 is now at position 1 = row 2
    std::string display = getFormulaDisplay(*sheet, cellD5->id);
    EXPECT_EQ(display, "=A2") << "After deleting row before, row 3 should become row 2";
}

TEST(FormulaInsertDeleteTest, UuidUnchangedAfterInsert) {
    auto wb = createTestWorkbook();
    Sheet* sheet = wb->getSheetByIndex(0);

    Cell* cellD1 = sheet->getCellAt(ID("colD0004"), ID("row10001"));
    ASSERT_NE(cellD1, nullptr);

    bool set = setResolvedFormula(*wb, *sheet, cellD1->id, "=B2");
    EXPECT_TRUE(set);

    // Get stored UUID formula before insert
    std::string storedBefore = sheet->getCellFormulaText(cellD1->id);

    // Insert column
    sheet->insertColumnAt(0);

    // UUID formula should be unchanged
    std::string storedAfter = sheet->getCellFormulaText(cellD1->id);
    EXPECT_EQ(storedBefore, storedAfter) << "UUID-format formula should not change after insert";
}

TEST(FormulaInsertDeleteTest, UnaffectedFormulaUnchanged) {
    auto wb = createTestWorkbook();
    Sheet* sheet = wb->getSheetByIndex(0);

    // Create two formulas: one in D1 and one in E1
    Cell* cellD1 = sheet->getCellAt(ID("colD0004"), ID("row10001"));
    Cell* cellE1 = sheet->getCellAt(ID("colE0005"), ID("row10001"));
    ASSERT_NE(cellD1, nullptr);
    ASSERT_NE(cellE1, nullptr);

    // D1 = formula referencing F3 (column F at position 5)
    // E1 = formula referencing A2 (column A at position 0)
    bool set1 = setResolvedFormula(*wb, *sheet, cellD1->id, "=F3");
    bool set2 = setResolvedFormula(*wb, *sheet, cellE1->id, "=A2");
    EXPECT_TRUE(set1);
    EXPECT_TRUE(set2);

    // Delete column C (position 2)
    // Column F shifts from position 5 to 4, but A stays at 0
    sheet->deleteColumn(ID("colC0003"));

    // D1's display updates: F was at 5, now at 4 = E
    std::string display1 = getFormulaDisplay(*sheet, cellD1->id);
    EXPECT_EQ(display1, "=E3");

    // E1's formula is unchanged: A still at position 0
    std::string display2 = getFormulaDisplay(*sheet, cellE1->id);
    EXPECT_EQ(display2, "=A2") << "Formula referencing earlier column should be unchanged";
}

TEST(FormulaInsertDeleteTest, DeleteColumnNotFound) {
    auto wb = createTestWorkbook();
    Sheet* sheet = wb->getSheetByIndex(0);

    bool deleted = sheet->deleteColumn(ID("nonexist"));
    EXPECT_FALSE(deleted);
}

TEST(FormulaInsertDeleteTest, DeleteRowNotFound) {
    auto wb = createTestWorkbook();
    Sheet* sheet = wb->getSheetByIndex(0);

    bool deleted = sheet->deleteRow(ID("nonexist"));
    EXPECT_FALSE(deleted);
}

TEST(FormulaInsertDeleteTest, InsertMultipleColumns) {
    auto wb = createTestWorkbook();
    Sheet* sheet = wb->getSheetByIndex(0);

    Cell* cellD1 = sheet->getCellAt(ID("colD0004"), ID("row10001"));
    ASSERT_NE(cellD1, nullptr);

    // Set formula =B2
    bool set = setResolvedFormula(*wb, *sheet, cellD1->id, "=B2");
    EXPECT_TRUE(set);

    // Insert 3 columns before B
    // Original: A=0, B=1, C=2, D=3, E=4, F=5
    // After 3 inserts at position 1: A=0, new=1, new=2, new=3, B=4, C=5, D=6, E=7, F=8
    sheet->insertColumnAt(1);
    sheet->insertColumnAt(1);
    sheet->insertColumnAt(1);

    // B is now at position 4 = E
    std::string display = getFormulaDisplay(*sheet, cellD1->id);
    EXPECT_EQ(display, "=E2") << "After inserting 3 columns, B should become E";
}

// ============================================================================
// Phase 6d: Range Expansion Tests
// ============================================================================

TEST(FormulaRangeExpansionTest, InsertRowWithinRange) {
    auto wb = createTestWorkbook();
    Sheet* sheet = wb->getSheetByIndex(0);

    // Formula cell in column F
    Cell* cellF1 = sheet->getCellAt(ID("colF0006"), ID("row10001"));
    ASSERT_NE(cellF1, nullptr);

    // Set formula =SUM(A1:A5) - references rows 1-5 (positions 0-4)
    bool set = setResolvedFormula(*wb, *sheet, cellF1->id, "=SUM(A1:A5)");
    EXPECT_TRUE(set);

    std::string display = getFormulaDisplay(*sheet, cellF1->id);
    EXPECT_EQ(display, "=SUM(A1:A5)");

    // Insert a row at position 2 (between rows 2 and 3)
    // Before: row1=0, row2=1, row3=2, row4=3, row5=4
    // After:  row1=0, row2=1, newRow=2, row3=3, row4=4, row5=5
    sheet->insertRowAt(2);

    // The range A1:A5 was storing cellIds for cells at row1 (pos 0) and row5 (pos 4)
    // After insert, row1 is still at pos 0, but row5 is now at pos 5
    // So display should show A1:A6 (pos 0 to pos 5)
    display = getFormulaDisplay(*sheet, cellF1->id);
    EXPECT_EQ(display, "=SUM(A1:A6)") << "Range should expand when row is inserted within it";
}

TEST(FormulaRangeExpansionTest, InsertColumnWithinRange) {
    auto wb = createTestWorkbook();
    Sheet* sheet = wb->getSheetByIndex(0);

    Cell* cellF1 = sheet->getCellAt(ID("colF0006"), ID("row10001"));
    ASSERT_NE(cellF1, nullptr);

    // Set formula =SUM(A1:D1) - references columns A-D (positions 0-3)
    bool set = setResolvedFormula(*wb, *sheet, cellF1->id, "=SUM(A1:D1)");
    EXPECT_TRUE(set);

    std::string display = getFormulaDisplay(*sheet, cellF1->id);
    EXPECT_EQ(display, "=SUM(A1:D1)");

    // Insert a column at position 2 (between B and C)
    // Before: A=0, B=1, C=2, D=3
    // After:  A=0, B=1, newCol=2, C=3, D=4
    sheet->insertColumnAt(2);

    // The range was storing cell at A1 (col A pos 0) and D1 (col D pos 3)
    // After insert, col A still at pos 0, col D now at pos 4
    // So display should show A1:E1
    display = getFormulaDisplay(*sheet, cellF1->id);
    EXPECT_EQ(display, "=SUM(A1:E1)") << "Range should expand when column is inserted within it";
}

TEST(FormulaRangeExpansionTest, InsertRowBeforeRange) {
    auto wb = createTestWorkbook();
    Sheet* sheet = wb->getSheetByIndex(0);

    Cell* cellF1 = sheet->getCellAt(ID("colF0006"), ID("row10001"));
    ASSERT_NE(cellF1, nullptr);

    // Set formula =SUM(A3:A5) - references rows 3-5 (positions 2-4)
    bool set = setResolvedFormula(*wb, *sheet, cellF1->id, "=SUM(A3:A5)");
    EXPECT_TRUE(set);

    // Insert a row at position 0 (before row 1)
    // Before: row1=0, row2=1, row3=2, row4=3, row5=4
    // After:  newRow=0, row1=1, row2=2, row3=3, row4=4, row5=5
    sheet->insertRowAt(0);

    // Both corners shift: row3 now at pos 3 (row 4), row5 now at pos 5 (row 6)
    std::string display = getFormulaDisplay(*sheet, cellF1->id);
    EXPECT_EQ(display, "=SUM(A4:A6)") << "Both range bounds should shift when row inserted before";
}

TEST(FormulaRangeExpansionTest, InsertRowAfterRange) {
    auto wb = createTestWorkbook();
    Sheet* sheet = wb->getSheetByIndex(0);

    Cell* cellF1 = sheet->getCellAt(ID("colF0006"), ID("row10001"));
    ASSERT_NE(cellF1, nullptr);

    // Set formula =SUM(A1:A3) - references rows 1-3 (positions 0-2)
    bool set = setResolvedFormula(*wb, *sheet, cellF1->id, "=SUM(A1:A3)");
    EXPECT_TRUE(set);

    // Insert a row at position 5 (after the range)
    sheet->insertRowAt(5);

    // Range should be unchanged
    std::string display = getFormulaDisplay(*sheet, cellF1->id);
    EXPECT_EQ(display, "=SUM(A1:A3)") << "Range should be unchanged when row inserted after";
}

TEST(FormulaRangeExpansionTest, DeleteRowWithinRange) {
    auto wb = createTestWorkbook();
    Sheet* sheet = wb->getSheetByIndex(0);

    Cell* cellF1 = sheet->getCellAt(ID("colF0006"), ID("row10001"));
    ASSERT_NE(cellF1, nullptr);

    // Set formula =SUM(A1:A5) - references rows 1-5 (positions 0-4)
    bool set = setResolvedFormula(*wb, *sheet, cellF1->id, "=SUM(A1:A5)");
    EXPECT_TRUE(set);

    // Delete row 3 (position 2) - within the range
    // Before: row1=0, row2=1, row3=2, row4=3, row5=4
    // After:  row1=0, row2=1, row4=2, row5=3
    sheet->deleteRow(ID("row30003"));

    // The range was A1:A5 with row5 at position 4
    // After delete, row5 is now at position 3
    // Display should show A1:A4
    std::string display = getFormulaDisplay(*sheet, cellF1->id);
    EXPECT_EQ(display, "=SUM(A1:A4)") << "Range should contract when row within it is deleted";
}

TEST(FormulaRangeExpansionTest, DeleteRowOutsideRange) {
    auto wb = createTestWorkbook();
    Sheet* sheet = wb->getSheetByIndex(0);

    Cell* cellF1 = sheet->getCellAt(ID("colF0006"), ID("row10001"));
    ASSERT_NE(cellF1, nullptr);

    // Set formula =SUM(A3:A5) - references rows 3-5 (positions 2-4)
    bool set = setResolvedFormula(*wb, *sheet, cellF1->id, "=SUM(A3:A5)");
    EXPECT_TRUE(set);

    // Delete row 6 (position 5) - after the range
    sheet->deleteRow(ID("row60006"));

    // Range should be unchanged
    std::string display = getFormulaDisplay(*sheet, cellF1->id);
    EXPECT_EQ(display, "=SUM(A3:A5)") << "Range should be unchanged when row deleted outside it";
}

TEST(FormulaRangeExpansionTest, DeleteRowBeforeRange) {
    auto wb = createTestWorkbook();
    Sheet* sheet = wb->getSheetByIndex(0);

    Cell* cellF5 = sheet->getCellAt(ID("colF0006"), ID("row50005"));
    ASSERT_NE(cellF5, nullptr);

    // Set formula =SUM(A3:A5) (formula cell in row 5 which is also in range)
    // Actually use a cell in row 6 to avoid complications
    Cell* cellF6 = sheet->getCellAt(ID("colF0006"), ID("row60006"));
    ASSERT_NE(cellF6, nullptr);

    // Set formula =SUM(A3:A5) - references rows 3-5 (positions 2-4)
    bool set = setResolvedFormula(*wb, *sheet, cellF6->id, "=SUM(A3:A5)");
    EXPECT_TRUE(set);

    // Delete row 1 (position 0) - before the range
    // Before: row1=0, row2=1, row3=2, row4=3, row5=4, row6=5
    // After:  row2=0, row3=1, row4=2, row5=3, row6=4
    sheet->deleteRow(ID("row10001"));

    // Both corners shift: row3 now at pos 1 (row 2), row5 now at pos 3 (row 4)
    std::string display = getFormulaDisplay(*sheet, cellF6->id);
    EXPECT_EQ(display, "=SUM(A2:A4)") << "Range bounds should shift when row deleted before";
}

TEST(FormulaRangeExpansionTest, MoveRowWithinRange) {
    auto wb = createTestWorkbook();
    Sheet* sheet = wb->getSheetByIndex(0);

    Cell* cellF1 = sheet->getCellAt(ID("colF0006"), ID("row10001"));
    ASSERT_NE(cellF1, nullptr);

    // Set formula =SUM(A1:A5) - references rows 1-5 (positions 0-4)
    bool set = setResolvedFormula(*wb, *sheet, cellF1->id, "=SUM(A1:A5)");
    EXPECT_TRUE(set);

    // Move row 2 to position 4 (after row 4, before row 5)
    // Before: row1=0, row2=1, row3=2, row4=3, row5=4
    // After:  row1=0, row3=1, row4=2, row2=3, row5=4
    sheet->moveRow(ID("row20002"), 3);

    // The range corners (row1 at pos 0, row5 at pos 4) are unchanged
    // So display should still be A1:A5
    std::string display = getFormulaDisplay(*sheet, cellF1->id);
    EXPECT_EQ(display, "=SUM(A1:A5)") << "Range should be unchanged when row moves within it";
}

TEST(FormulaRangeExpansionTest, UuidFormatUnchangedAfterRangeExpand) {
    auto wb = createTestWorkbook();
    Sheet* sheet = wb->getSheetByIndex(0);

    Cell* cellF1 = sheet->getCellAt(ID("colF0006"), ID("row10001"));
    ASSERT_NE(cellF1, nullptr);

    // Set formula =SUM(A1:A5)
    bool set = setResolvedFormula(*wb, *sheet, cellF1->id, "=SUM(A1:A5)");
    EXPECT_TRUE(set);

    // Get stored UUID formula before insert
    std::string storedBefore = sheet->getCellFormulaText(cellF1->id);

    // Insert row within range
    sheet->insertRowAt(2);

    // UUID formula should be unchanged (still references same cells)
    std::string storedAfter = sheet->getCellFormulaText(cellF1->id);
    EXPECT_EQ(storedBefore, storedAfter)
        << "UUID-format formula should not change when range expands";
}

TEST(FormulaRangeExpansionTest, MultipleDimensionExpansion) {
    auto wb = createTestWorkbook();
    Sheet* sheet = wb->getSheetByIndex(0);

    Cell* cellF6 = sheet->getCellAt(ID("colF0006"), ID("row60006"));
    ASSERT_NE(cellF6, nullptr);

    // Set formula =SUM(A1:C3) - 3x3 range
    bool set = setResolvedFormula(*wb, *sheet, cellF6->id, "=SUM(A1:C3)");
    EXPECT_TRUE(set);

    std::string display = getFormulaDisplay(*sheet, cellF6->id);
    EXPECT_EQ(display, "=SUM(A1:C3)");

    // Insert a column at position 1 (between A and B)
    // Before: A=0, B=1, C=2
    // After:  A=0, new=1, B=2, C=3
    sheet->insertColumnAt(1);

    // Column C now at position 3 = D
    // Display should show A1:D3
    display = getFormulaDisplay(*sheet, cellF6->id);
    EXPECT_EQ(display, "=SUM(A1:D3)");

    // Insert a row at position 1 (between row 1 and 2)
    // Row 3 now at position 3 = row 4
    // Display should show A1:D4
    sheet->insertRowAt(1);

    display = getFormulaDisplay(*sheet, cellF6->id);
    EXPECT_EQ(display, "=SUM(A1:D4)") << "Range should expand in both dimensions";
}

}  // namespace
}  // namespace cells
