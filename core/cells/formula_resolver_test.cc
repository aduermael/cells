#include "core/cells/formula_resolver.h"

#include <gtest/gtest.h>

#include "core/cells/formula_parser.h"
#include "core/cells/id.h"

namespace cells {
namespace {

class FormulaResolverTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a workbook with one sheet
        workbook = std::make_unique<Workbook>(generate_id(), "TestWorkbook");
        auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet1");
        sheet1 = sheet.get();
        workbook->addSheet(std::move(sheet));

        // Create columns A, B, C (positions 0, 1, 2)
        auto colA = std::make_unique<Axis>(generate_id(), true);
        colA->position = 0;
        colAId = colA->id;
        sheet1->addColumn(std::move(colA));

        auto colB = std::make_unique<Axis>(generate_id(), true);
        colB->position = 1;
        colBId = colB->id;
        sheet1->addColumn(std::move(colB));

        auto colC = std::make_unique<Axis>(generate_id(), true);
        colC->position = 2;
        colCId = colC->id;
        sheet1->addColumn(std::move(colC));

        // Create rows 1, 2, 3 (positions 0, 1, 2)
        auto row1 = std::make_unique<Axis>(generate_id(), false);
        row1->position = 0;
        row1Id = row1->id;
        sheet1->addRow(std::move(row1));

        auto row2 = std::make_unique<Axis>(generate_id(), false);
        row2->position = 1;
        row2Id = row2->id;
        sheet1->addRow(std::move(row2));

        auto row3 = std::make_unique<Axis>(generate_id(), false);
        row3->position = 2;
        row3Id = row3->id;
        sheet1->addRow(std::move(row3));

        // Create cells A1, B1, C1
        auto cellA1 = std::make_unique<Cell>(generate_id(), colAId, row1Id);
        cellA1Id = cellA1->id;
        sheet1->addCell(std::move(cellA1));

        auto cellB1 = std::make_unique<Cell>(generate_id(), colBId, row1Id);
        cellB1Id = cellB1->id;
        sheet1->addCell(std::move(cellB1));

        auto cellC1 = std::make_unique<Cell>(generate_id(), colCId, row1Id);
        cellC1Id = cellC1->id;
        sheet1->addCell(std::move(cellC1));
    }

    std::unique_ptr<ASTNode> parseFormula(const std::string& formula) {
        FormulaParser parser(formula);
        return parser.parse();
    }

    // Helper to resolve a formula, creating any missing entities first.
    // Uses the sheetId from RequiredEntities to create entities on the correct sheet.
    ResolveResult resolveWithEntityCreation(FormulaResolver& resolver, ASTNode* ast,
                                            Sheet* /* defaultSheet - unused */) {
        RequiredEntities required = resolver.getRequiredEntities(ast);

        // Helper to find sheet by ID
        auto getSheet = [this](const ID& sheetId) -> Sheet* {
            for (auto& sheet : workbook->sheets) {
                if (sheet->id == sheetId)
                    return sheet.get();
            }
            return nullptr;
        };

        // Create axes by position on their respective sheets
        for (const auto& col : required.columns) {
            Sheet* targetSheet = getSheet(col.sheetId);
            if (targetSheet) {
                targetSheet->getOrCreateColumnByPosition(col.position);
            }
        }
        for (const auto& row : required.rows) {
            Sheet* targetSheet = getSheet(row.sheetId);
            if (targetSheet) {
                targetSheet->getOrCreateRowByPosition(row.position);
            }
        }

        // Create cells - map pending IDs to positions, then to actual IDs
        for (const auto& pendingCell : required.cells) {
            Sheet* targetSheet = getSheet(pendingCell.sheetId);
            if (!targetSheet)
                continue;

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
            const Axis* col = targetSheet->getColumnByPosition(colPos);
            const Axis* row = targetSheet->getRowByPosition(rowPos);
            if (col && row) {
                targetSheet->getOrCreateCellAt(col->id, row->id);
            }
        }

        return resolver.resolve(ast);
    }

    std::unique_ptr<Workbook> workbook;
    Sheet* sheet1;
    ID colAId, colBId, colCId;
    ID row1Id, row2Id, row3Id;
    ID cellA1Id, cellB1Id, cellC1Id;
};

// ===========================================================================
// Cell reference resolution
// ===========================================================================

TEST_F(FormulaResolverTest, ResolveCellRef_ExistingCell) {
    auto ast = parseFormula("=A1");
    ASSERT_NE(ast, nullptr);

    FormulaResolver resolver(*workbook, *sheet1);
    auto result = resolveWithEntityCreation(resolver, ast.get(), sheet1);

    EXPECT_TRUE(result.success);

    // The cell ref should now have the cell ID
    auto* cellRef = static_cast<CellRefNode*>(ast.get());
    EXPECT_EQ(cellRef->cellId, cellA1Id.toString());
}

TEST_F(FormulaResolverTest, ResolveCellRef_AutoCreateCell) {
    // A2 doesn't exist yet
    EXPECT_EQ(sheet1->getCellAt(colAId, row2Id), nullptr);

    auto ast = parseFormula("=A2");
    ASSERT_NE(ast, nullptr);

    FormulaResolver resolver(*workbook, *sheet1);
    auto result = resolveWithEntityCreation(resolver, ast.get(), sheet1);

    EXPECT_TRUE(result.success);

    // Cell should have been auto-created
    auto* cellRef = static_cast<CellRefNode*>(ast.get());
    EXPECT_FALSE(cellRef->cellId.empty());

    // Cell should exist in sheet
    Cell* createdCell = sheet1->getCellAt(colAId, row2Id);
    EXPECT_NE(createdCell, nullptr);
    EXPECT_EQ(cellRef->cellId, createdCell->id.toString());
}

TEST_F(FormulaResolverTest, ResolveCellRef_AutoCreateAxis) {
    // Column D and row 10 don't exist yet
    size_t colCountBefore = sheet1->columnCount();
    size_t rowCountBefore = sheet1->rowCount();

    auto ast = parseFormula("=D10");
    ASSERT_NE(ast, nullptr);

    FormulaResolver resolver(*workbook, *sheet1);
    auto result = resolveWithEntityCreation(resolver, ast.get(), sheet1);

    EXPECT_TRUE(result.success);

    // Column and row should have been auto-created
    EXPECT_GT(sheet1->columnCount(), colCountBefore);
    EXPECT_GT(sheet1->rowCount(), rowCountBefore);

    // Cell should be created
    auto* cellRef = static_cast<CellRefNode*>(ast.get());
    EXPECT_FALSE(cellRef->cellId.empty());
}

TEST_F(FormulaResolverTest, ResolveCellRef_AbsoluteReference) {
    auto ast = parseFormula("=$A$1");
    ASSERT_NE(ast, nullptr);

    FormulaResolver resolver(*workbook, *sheet1);
    auto result = resolveWithEntityCreation(resolver, ast.get(), sheet1);

    EXPECT_TRUE(result.success);

    auto* cellRef = static_cast<CellRefNode*>(ast.get());
    EXPECT_TRUE(cellRef->colAbsolute);
    EXPECT_TRUE(cellRef->rowAbsolute);
    EXPECT_EQ(cellRef->cellId, cellA1Id.toString());
}

TEST_F(FormulaResolverTest, ResolveCellRef_MixedReference) {
    auto ast = parseFormula("=$A1");
    ASSERT_NE(ast, nullptr);

    FormulaResolver resolver(*workbook, *sheet1);
    auto result = resolveWithEntityCreation(resolver, ast.get(), sheet1);

    EXPECT_TRUE(result.success);

    auto* cellRef = static_cast<CellRefNode*>(ast.get());
    EXPECT_TRUE(cellRef->colAbsolute);
    EXPECT_FALSE(cellRef->rowAbsolute);
}

// ===========================================================================
// Range reference resolution
// ===========================================================================

TEST_F(FormulaResolverTest, ResolveRangeRef) {
    auto ast = parseFormula("=A1:C3");
    ASSERT_NE(ast, nullptr);

    FormulaResolver resolver(*workbook, *sheet1);
    auto result = resolveWithEntityCreation(resolver, ast.get(), sheet1);

    EXPECT_TRUE(result.success);

    auto* rangeRef = static_cast<RangeRefNode*>(ast.get());
    EXPECT_FALSE(rangeRef->topLeft->cellId.empty());
    EXPECT_FALSE(rangeRef->bottomRight->cellId.empty());
}

// ===========================================================================
// Column/Row reference resolution
// ===========================================================================

TEST_F(FormulaResolverTest, ResolveColumnRef) {
    auto ast = parseFormula("=A:A");
    ASSERT_NE(ast, nullptr);

    FormulaResolver resolver(*workbook, *sheet1);
    auto result = resolveWithEntityCreation(resolver, ast.get(), sheet1);

    EXPECT_TRUE(result.success);

    auto* colRef = static_cast<ColumnRefNode*>(ast.get());
    EXPECT_EQ(colRef->columnId, colAId.toString());
}

TEST_F(FormulaResolverTest, ResolveRowRef) {
    auto ast = parseFormula("=1:1");
    ASSERT_NE(ast, nullptr);

    FormulaResolver resolver(*workbook, *sheet1);
    auto result = resolveWithEntityCreation(resolver, ast.get(), sheet1);

    EXPECT_TRUE(result.success);

    auto* rowRef = static_cast<RowRefNode*>(ast.get());
    EXPECT_EQ(rowRef->rowId, row1Id.toString());
}

TEST_F(FormulaResolverTest, ResolveColumnRangeRef) {
    auto ast = parseFormula("=A:C");
    ASSERT_NE(ast, nullptr);

    FormulaResolver resolver(*workbook, *sheet1);
    auto result = resolveWithEntityCreation(resolver, ast.get(), sheet1);

    EXPECT_TRUE(result.success);

    auto* colRangeRef = static_cast<ColumnRangeRefNode*>(ast.get());
    EXPECT_EQ(colRangeRef->startColumnId, colAId.toString());
    EXPECT_EQ(colRangeRef->endColumnId, colCId.toString());
}

TEST_F(FormulaResolverTest, ResolveRowRangeRef) {
    auto ast = parseFormula("=1:3");
    ASSERT_NE(ast, nullptr);

    FormulaResolver resolver(*workbook, *sheet1);
    auto result = resolveWithEntityCreation(resolver, ast.get(), sheet1);

    EXPECT_TRUE(result.success);

    auto* rowRangeRef = static_cast<RowRangeRefNode*>(ast.get());
    EXPECT_EQ(rowRangeRef->startRowId, row1Id.toString());
    EXPECT_EQ(rowRangeRef->endRowId, row3Id.toString());
}

// ===========================================================================
// Named range resolution
// ===========================================================================

TEST_F(FormulaResolverTest, ResolveNamedRef_NotFound) {
    auto ast = parseFormula("=MyRange");
    ASSERT_NE(ast, nullptr);

    // No named range registry
    FormulaResolver resolver(*workbook, *sheet1);
    auto result = resolveWithEntityCreation(resolver, ast.get(), sheet1);

    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.errorMessage.find("not found") != std::string::npos);
}

TEST_F(FormulaResolverTest, ResolveNamedRef_Found) {
    NamedRangeRegistry registry;
    registry.defineWorkbook("TotalSales", NamedRangeTarget::cell(cellA1Id, sheet1->id));

    auto ast = parseFormula("=TotalSales");
    ASSERT_NE(ast, nullptr);

    FormulaResolver resolver(*workbook, *sheet1, &registry);
    auto result = resolveWithEntityCreation(resolver, ast.get(), sheet1);

    EXPECT_TRUE(result.success);
}

TEST_F(FormulaResolverTest, ResolveNamedRef_SheetScopeShadows) {
    NamedRangeRegistry registry;
    registry.defineWorkbook("MyName", NamedRangeTarget::cell(cellA1Id, sheet1->id));
    registry.defineSheet("MyName", sheet1->id, NamedRangeTarget::cell(cellB1Id, sheet1->id));

    auto ast = parseFormula("=MyName");
    ASSERT_NE(ast, nullptr);

    FormulaResolver resolver(*workbook, *sheet1, &registry);
    auto result = resolveWithEntityCreation(resolver, ast.get(), sheet1);

    EXPECT_TRUE(result.success);

    // Should have resolved to sheet scope
    auto* namedRef = static_cast<NamedRefNode*>(ast.get());
    EXPECT_EQ(namedRef->scope, ASTNamedRangeScope::SHEET);
}

// ===========================================================================
// Complex formulas
// ===========================================================================

TEST_F(FormulaResolverTest, ResolveBinaryOp) {
    auto ast = parseFormula("=A1+B1");
    ASSERT_NE(ast, nullptr);

    FormulaResolver resolver(*workbook, *sheet1);
    auto result = resolveWithEntityCreation(resolver, ast.get(), sheet1);

    EXPECT_TRUE(result.success);

    // Both operands should be resolved
    auto* binOp = static_cast<BinaryOpNode*>(ast.get());
    auto* leftRef = static_cast<CellRefNode*>(binOp->left.get());
    auto* rightRef = static_cast<CellRefNode*>(binOp->right.get());

    EXPECT_EQ(leftRef->cellId, cellA1Id.toString());
    EXPECT_EQ(rightRef->cellId, cellB1Id.toString());
}

TEST_F(FormulaResolverTest, ResolveFunctionCall) {
    auto ast = parseFormula("=SUM(A1:C1)");
    ASSERT_NE(ast, nullptr);

    FormulaResolver resolver(*workbook, *sheet1);
    auto result = resolveWithEntityCreation(resolver, ast.get(), sheet1);

    EXPECT_TRUE(result.success);

    auto* funcCall = static_cast<FunctionCallNode*>(ast.get());
    EXPECT_FALSE(funcCall->isVolatile);
}

TEST_F(FormulaResolverTest, ResolveVolatileFunction) {
    auto ast = parseFormula("=NOW()");
    ASSERT_NE(ast, nullptr);

    FormulaResolver resolver(*workbook, *sheet1);
    auto result = resolveWithEntityCreation(resolver, ast.get(), sheet1);

    EXPECT_TRUE(result.success);

    auto* funcCall = static_cast<FunctionCallNode*>(ast.get());
    EXPECT_TRUE(funcCall->isVolatile);
}

TEST_F(FormulaResolverTest, ContainsVolatileFunction) {
    auto ast1 = parseFormula("=A1+B1");
    EXPECT_FALSE(FormulaResolver::containsVolatileFunction(ast1.get()));

    auto ast2 = parseFormula("=NOW()");
    EXPECT_TRUE(FormulaResolver::containsVolatileFunction(ast2.get()));

    auto ast3 = parseFormula("=A1+RAND()");
    EXPECT_TRUE(FormulaResolver::containsVolatileFunction(ast3.get()));
}

// ===========================================================================
// Reference extraction
// ===========================================================================

TEST_F(FormulaResolverTest, ExtractReferences_SingleCell) {
    auto ast = parseFormula("=A1");
    FormulaResolver resolver(*workbook, *sheet1);
    resolveWithEntityCreation(resolver, ast.get(), sheet1);

    auto refs = resolver.extractReferences(ast.get());
    EXPECT_EQ(refs.size(), 1);
    EXPECT_EQ(refs[0].type, ReferenceInfo::Type::CELL);
    EXPECT_EQ(refs[0].cellId, cellA1Id);
}

TEST_F(FormulaResolverTest, ExtractReferences_Multiple) {
    auto ast = parseFormula("=A1+B1+C1");
    FormulaResolver resolver(*workbook, *sheet1);
    resolveWithEntityCreation(resolver, ast.get(), sheet1);

    auto refs = resolver.extractReferences(ast.get());
    EXPECT_EQ(refs.size(), 3);
}

TEST_F(FormulaResolverTest, ExtractReferences_Range) {
    auto ast = parseFormula("=A1:C3");
    FormulaResolver resolver(*workbook, *sheet1);
    resolveWithEntityCreation(resolver, ast.get(), sheet1);

    auto refs = resolver.extractReferences(ast.get());
    EXPECT_EQ(refs.size(), 1);
    EXPECT_EQ(refs[0].type, ReferenceInfo::Type::RANGE);
}

TEST_F(FormulaResolverTest, ExtractReferences_FunctionArgs) {
    auto ast = parseFormula("=SUM(A1,B1,C1)");
    FormulaResolver resolver(*workbook, *sheet1);
    resolveWithEntityCreation(resolver, ast.get(), sheet1);

    auto refs = resolver.extractReferences(ast.get());
    EXPECT_EQ(refs.size(), 3);
}

// ===========================================================================
// Display conversion
// ===========================================================================

TEST_F(FormulaResolverTest, DisplayConversion_SimpleCellRef) {
    auto ast = parseFormula("=A1");
    FormulaResolver resolver(*workbook, *sheet1);
    resolveWithEntityCreation(resolver, ast.get(), sheet1);

    FormulaDisplayConverter converter(*sheet1);
    std::string display = converter.toDisplayString(ast.get());

    EXPECT_EQ(display, "=A1");
}

TEST_F(FormulaResolverTest, DisplayConversion_AbsoluteRef) {
    auto ast = parseFormula("=$A$1");
    FormulaResolver resolver(*workbook, *sheet1);
    resolveWithEntityCreation(resolver, ast.get(), sheet1);

    FormulaDisplayConverter converter(*sheet1);
    std::string display = converter.toDisplayString(ast.get());

    EXPECT_EQ(display, "=$A$1");
}

TEST_F(FormulaResolverTest, DisplayConversion_BinaryOp) {
    auto ast = parseFormula("=A1+B1*C1");
    FormulaResolver resolver(*workbook, *sheet1);
    resolveWithEntityCreation(resolver, ast.get(), sheet1);

    FormulaDisplayConverter converter(*sheet1);
    std::string display = converter.toDisplayString(ast.get());

    EXPECT_EQ(display, "=A1+B1*C1");
}

TEST_F(FormulaResolverTest, DisplayConversion_Function) {
    auto ast = parseFormula("=SUM(A1,B1)");
    FormulaResolver resolver(*workbook, *sheet1);
    resolveWithEntityCreation(resolver, ast.get(), sheet1);

    FormulaDisplayConverter converter(*sheet1);
    std::string display = converter.toDisplayString(ast.get());

    EXPECT_EQ(display, "=SUM(A1,B1)");
}

TEST_F(FormulaResolverTest, DisplayConversion_Literals) {
    auto ast = parseFormula("=1+2.5+TRUE+\"Hello\"");
    FormulaResolver resolver(*workbook, *sheet1);
    resolveWithEntityCreation(resolver, ast.get(), sheet1);

    FormulaDisplayConverter converter(*sheet1);
    std::string display = converter.toDisplayString(ast.get());

    // Numbers may have different formatting
    EXPECT_TRUE(display.find("1") != std::string::npos);
    EXPECT_TRUE(display.find("TRUE") != std::string::npos);
    EXPECT_TRUE(display.find("Hello") != std::string::npos);
}

TEST_F(FormulaResolverTest, DisplayConversion_ColumnRef) {
    auto ast = parseFormula("=A:A");
    FormulaResolver resolver(*workbook, *sheet1);
    resolveWithEntityCreation(resolver, ast.get(), sheet1);

    FormulaDisplayConverter converter(*sheet1);
    std::string display = converter.toDisplayString(ast.get());

    EXPECT_EQ(display, "=A:A");
}

TEST_F(FormulaResolverTest, DisplayConversion_RowRef) {
    auto ast = parseFormula("=1:1");
    FormulaResolver resolver(*workbook, *sheet1);
    resolveWithEntityCreation(resolver, ast.get(), sheet1);

    FormulaDisplayConverter converter(*sheet1);
    std::string display = converter.toDisplayString(ast.get());

    EXPECT_EQ(display, "=1:1");
}

// ===========================================================================
// Round-trip tests
// ===========================================================================

TEST_F(FormulaResolverTest, DisplayConversion_SpillRangeRef) {
    auto ast = parseFormula("=A1#");
    FormulaResolver resolver(*workbook, *sheet1);
    resolveWithEntityCreation(resolver, ast.get(), sheet1);

    FormulaDisplayConverter converter(*sheet1);
    std::string display = converter.toDisplayString(ast.get());

    EXPECT_EQ(display, "=A1#");
}

TEST_F(FormulaResolverTest, DisplayConversion_SpillRangeRefAbsolute) {
    auto ast = parseFormula("=$B$2#");
    FormulaResolver resolver(*workbook, *sheet1);
    resolveWithEntityCreation(resolver, ast.get(), sheet1);

    FormulaDisplayConverter converter(*sheet1);
    std::string display = converter.toDisplayString(ast.get());

    EXPECT_EQ(display, "=$B$2#");
}

TEST_F(FormulaResolverTest, DisplayConversion_SpillRangeInFunction) {
    auto ast = parseFormula("=SUM(A1#)");
    FormulaResolver resolver(*workbook, *sheet1);
    resolveWithEntityCreation(resolver, ast.get(), sheet1);

    FormulaDisplayConverter converter(*sheet1);
    std::string display = converter.toDisplayString(ast.get());

    EXPECT_EQ(display, "=SUM(A1#)");
}

TEST_F(FormulaResolverTest, RoundTrip_ParseResolveDisplay) {
    std::vector<std::string> formulas = {
        "=A1",    "=$A$1",  "=A1+B1",  "=A1*B1+C1", "=SUM(A1:C3)", "=IF(A1,B1,C1)",
        "=A:A",   "=1:1",   "=A:C",    "=1:3",      "=-A1",        "=A1^2",
        "=A1&B1", "=A1=B1", "=A1<>B1", "=A1#",      "=$A$1#",      "=SUM(A1#)",
    };

    for (const auto& formula : formulas) {
        auto ast = parseFormula(formula);
        ASSERT_NE(ast, nullptr) << "Failed to parse: " << formula;

        FormulaResolver resolver(*workbook, *sheet1);
        auto result = resolveWithEntityCreation(resolver, ast.get(), sheet1);
        EXPECT_TRUE(result.success) << "Failed to resolve: " << formula;

        FormulaDisplayConverter converter(*sheet1);
        std::string display = converter.toDisplayString(ast.get());

        // Re-parse the display string
        auto ast2 = parseFormula(display);
        ASSERT_NE(ast2, nullptr) << "Failed to re-parse display: " << display;

        // Verify it resolves successfully
        FormulaResolver resolver2(*workbook, *sheet1);
        auto result2 = resolveWithEntityCreation(resolver2, ast2.get(), sheet1);
        EXPECT_TRUE(result2.success) << "Failed to resolve re-parsed: " << display;
    }
}

// ===========================================================================
// Cross-sheet reference tests
// ===========================================================================

TEST_F(FormulaResolverTest, ResolveCrossSheetRef_SheetFound) {
    // Add a second sheet
    auto sheet2 = std::make_unique<Sheet>(generate_id(), "Sheet2");
    Sheet* sheet2Ptr = sheet2.get();
    workbook->addSheet(std::move(sheet2));

    // Parse a cross-sheet reference formula
    auto ast = parseFormula("=Sheet2!A1");
    ASSERT_NE(ast, nullptr);
    ASSERT_EQ(ast->type, ASTNodeType::CELL_REF);

    // Verify the sheetName was parsed correctly
    auto* cellRef = static_cast<CellRefNode*>(ast.get());
    EXPECT_EQ(cellRef->sheetName, "Sheet2");
    EXPECT_EQ(cellRef->column, "A");
    EXPECT_EQ(cellRef->row, 1);

    // Resolve the reference (from Sheet1's perspective)
    FormulaResolver resolver(*workbook, *sheet1);
    auto result = resolveWithEntityCreation(resolver, ast.get(), sheet1);

    EXPECT_TRUE(result.success) << "Resolution failed: " << result.errorMessage;

    // The cell should have been auto-created on Sheet2
    EXPECT_FALSE(cellRef->cellId.empty());

    // Verify the cell exists on Sheet2 (not Sheet1)
    Cell* createdCell = sheet2Ptr->getCell(ID(cellRef->cellId));
    EXPECT_NE(createdCell, nullptr) << "Cell was not created on Sheet2";
}

TEST_F(FormulaResolverTest, ResolveCrossSheetRef_SheetNotFound) {
    // Parse a cross-sheet reference to a non-existent sheet
    auto ast = parseFormula("=NonExistent!A1");
    ASSERT_NE(ast, nullptr);

    // Resolve should fail
    FormulaResolver resolver(*workbook, *sheet1);
    auto result = resolveWithEntityCreation(resolver, ast.get(), sheet1);

    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.errorMessage.find("not found") != std::string::npos);
}

TEST_F(FormulaResolverTest, ResolveCrossSheetRange) {
    // Add a second sheet
    auto sheet2 = std::make_unique<Sheet>(generate_id(), "Sheet2");
    workbook->addSheet(std::move(sheet2));

    // Parse a cross-sheet range reference
    auto ast = parseFormula("=SUM(Sheet2!A1:A3)");
    ASSERT_NE(ast, nullptr);

    FormulaResolver resolver(*workbook, *sheet1);
    auto result = resolveWithEntityCreation(resolver, ast.get(), sheet1);

    EXPECT_TRUE(result.success) << "Resolution failed: " << result.errorMessage;
}

// ===========================================================================
// CRDT-compliant resolution tests (getRequiredEntities)
// ===========================================================================

TEST_F(FormulaResolverTest, GetRequiredEntities_ExistingCell_ReturnsEmpty) {
    // A1 exists, so no entities should be required
    auto ast = parseFormula("=A1");
    ASSERT_NE(ast, nullptr);

    FormulaResolver resolver(*workbook, *sheet1);
    RequiredEntities required = resolver.getRequiredEntities(ast.get());

    EXPECT_TRUE(required.empty());
    EXPECT_TRUE(required.columns.empty());
    EXPECT_TRUE(required.rows.empty());
    EXPECT_TRUE(required.cells.empty());
}

TEST_F(FormulaResolverTest, GetRequiredEntities_MissingColumn_ReturnsPendingColumn) {
    // Column D (position 3) doesn't exist
    auto ast = parseFormula("=D1");
    ASSERT_NE(ast, nullptr);

    FormulaResolver resolver(*workbook, *sheet1);
    RequiredEntities required = resolver.getRequiredEntities(ast.get());

    EXPECT_FALSE(required.empty());
    EXPECT_EQ(required.columns.size(), 1);
    EXPECT_EQ(required.columns[0].position, 3);  // D = position 3
    EXPECT_TRUE(required.columns[0].isColumn);
}

TEST_F(FormulaResolverTest, GetRequiredEntities_MissingRow_ReturnsPendingRow) {
    // Row 10 (position 9) doesn't exist
    auto ast = parseFormula("=A10");
    ASSERT_NE(ast, nullptr);

    FormulaResolver resolver(*workbook, *sheet1);
    RequiredEntities required = resolver.getRequiredEntities(ast.get());

    EXPECT_FALSE(required.empty());
    EXPECT_EQ(required.rows.size(), 1);
    EXPECT_EQ(required.rows[0].position, 9);  // Row 10 = position 9
    EXPECT_FALSE(required.rows[0].isColumn);
}

TEST_F(FormulaResolverTest, GetRequiredEntities_MissingCell_ReturnsPendingCell) {
    // A2 doesn't exist (row 2 exists, col A exists, but cell doesn't)
    EXPECT_EQ(sheet1->getCellAt(colAId, row2Id), nullptr);

    auto ast = parseFormula("=A2");
    ASSERT_NE(ast, nullptr);

    FormulaResolver resolver(*workbook, *sheet1);
    RequiredEntities required = resolver.getRequiredEntities(ast.get());

    EXPECT_FALSE(required.empty());
    EXPECT_TRUE(required.columns.empty());  // Column A exists
    EXPECT_TRUE(required.rows.empty());     // Row 2 exists
    EXPECT_EQ(required.cells.size(), 1);
    EXPECT_EQ(required.cells[0].colId, colAId);
    EXPECT_EQ(required.cells[0].rowId, row2Id);
}

TEST_F(FormulaResolverTest, GetRequiredEntities_MissingAll_ReturnsAll) {
    // D10 requires column D, row 10, and the cell
    auto ast = parseFormula("=D10");
    ASSERT_NE(ast, nullptr);

    FormulaResolver resolver(*workbook, *sheet1);
    RequiredEntities required = resolver.getRequiredEntities(ast.get());

    EXPECT_FALSE(required.empty());
    EXPECT_EQ(required.columns.size(), 1);
    EXPECT_EQ(required.columns[0].position, 3);  // D = position 3
    EXPECT_EQ(required.rows.size(), 1);
    EXPECT_EQ(required.rows[0].position, 9);  // Row 10 = position 9
    EXPECT_EQ(required.cells.size(), 1);
}

TEST_F(FormulaResolverTest, GetRequiredEntities_Range_ReturnsCorners) {
    // Range D10:E12 requires corners:
    // - Columns D (pos 3), E (pos 4)
    // - Rows 10 (pos 9), 12 (pos 11) - only corner rows for range bounds
    // - Cells at the two corners (D10 and E12)
    auto ast = parseFormula("=SUM(D10:E12)");
    ASSERT_NE(ast, nullptr);

    FormulaResolver resolver(*workbook, *sheet1);
    RequiredEntities required = resolver.getRequiredEntities(ast.get());

    EXPECT_FALSE(required.empty());
    // Columns D and E
    EXPECT_EQ(required.columns.size(), 2);
    // Rows for corners (10 and 12)
    EXPECT_EQ(required.rows.size(), 2);
    // Cells at corners (D10 and E12)
    EXPECT_EQ(required.cells.size(), 2);
}

TEST_F(FormulaResolverTest, ExistingOnlyMode_FailsOnMissingCell) {
    // A2 doesn't exist
    EXPECT_EQ(sheet1->getCellAt(colAId, row2Id), nullptr);

    auto ast = parseFormula("=A2");
    ASSERT_NE(ast, nullptr);

    FormulaResolver resolver(*workbook, *sheet1);
    auto result = resolver.resolve(ast.get());

    // Should fail because cell doesn't exist
    EXPECT_FALSE(result.success);
}

TEST_F(FormulaResolverTest, ExistingOnlyMode_SucceedsWithExistingCell) {
    // A1 exists
    auto ast = parseFormula("=A1");
    ASSERT_NE(ast, nullptr);

    FormulaResolver resolver(*workbook, *sheet1);
    auto result = resolver.resolve(ast.get());

    // Should succeed because cell exists
    EXPECT_TRUE(result.success);

    auto* cellRef = static_cast<CellRefNode*>(ast.get());
    EXPECT_EQ(cellRef->cellId, cellA1Id.toString());
}

TEST_F(FormulaResolverTest, TwoPhaseApproach_CreateThenResolve) {
    // Verify the CRDT-compliant two-phase approach works:
    // 1. Get required entities
    // 2. Create entities manually
    // 3. Resolve

    // A2 doesn't exist
    EXPECT_EQ(sheet1->getCellAt(colAId, row2Id), nullptr);

    auto ast = parseFormula("=A2");
    ASSERT_NE(ast, nullptr);

    FormulaResolver resolver(*workbook, *sheet1);

    // Phase 1: Get required entities
    RequiredEntities required = resolver.getRequiredEntities(ast.get());
    EXPECT_EQ(required.cells.size(), 1);

    // Phase 2: Create the cell manually (simulating CRDT operation)
    auto newCell = std::make_unique<Cell>(required.cells[0].id, colAId, row2Id);
    sheet1->addCell(std::move(newCell));

    // Phase 3: Resolve should now succeed
    auto result = resolver.resolve(ast.get());
    EXPECT_TRUE(result.success) << "Resolution failed: " << result.errorMessage;

    // Cell reference should point to the created cell
    auto* cellRef = static_cast<CellRefNode*>(ast.get());
    EXPECT_EQ(cellRef->cellId, required.cells[0].id.toString());
}

}  // namespace
}  // namespace cells
