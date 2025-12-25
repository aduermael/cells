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
    auto result = resolver.resolve(ast.get());

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
    auto result = resolver.resolve(ast.get());

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
    auto result = resolver.resolve(ast.get());

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
    auto result = resolver.resolve(ast.get());

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
    auto result = resolver.resolve(ast.get());

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
    auto result = resolver.resolve(ast.get());

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
    auto result = resolver.resolve(ast.get());

    EXPECT_TRUE(result.success);

    auto* colRef = static_cast<ColumnRefNode*>(ast.get());
    EXPECT_EQ(colRef->columnId, colAId.toString());
}

TEST_F(FormulaResolverTest, ResolveRowRef) {
    auto ast = parseFormula("=1:1");
    ASSERT_NE(ast, nullptr);

    FormulaResolver resolver(*workbook, *sheet1);
    auto result = resolver.resolve(ast.get());

    EXPECT_TRUE(result.success);

    auto* rowRef = static_cast<RowRefNode*>(ast.get());
    EXPECT_EQ(rowRef->rowId, row1Id.toString());
}

TEST_F(FormulaResolverTest, ResolveColumnRangeRef) {
    auto ast = parseFormula("=A:C");
    ASSERT_NE(ast, nullptr);

    FormulaResolver resolver(*workbook, *sheet1);
    auto result = resolver.resolve(ast.get());

    EXPECT_TRUE(result.success);

    auto* colRangeRef = static_cast<ColumnRangeRefNode*>(ast.get());
    EXPECT_EQ(colRangeRef->startColumnId, colAId.toString());
    EXPECT_EQ(colRangeRef->endColumnId, colCId.toString());
}

TEST_F(FormulaResolverTest, ResolveRowRangeRef) {
    auto ast = parseFormula("=1:3");
    ASSERT_NE(ast, nullptr);

    FormulaResolver resolver(*workbook, *sheet1);
    auto result = resolver.resolve(ast.get());

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
    auto result = resolver.resolve(ast.get());

    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.errorMessage.find("not found") != std::string::npos);
}

TEST_F(FormulaResolverTest, ResolveNamedRef_Found) {
    NamedRangeRegistry registry;
    registry.defineWorkbook("TotalSales", NamedRangeTarget::cell(cellA1Id, sheet1->id));

    auto ast = parseFormula("=TotalSales");
    ASSERT_NE(ast, nullptr);

    FormulaResolver resolver(*workbook, *sheet1, &registry);
    auto result = resolver.resolve(ast.get());

    EXPECT_TRUE(result.success);
}

TEST_F(FormulaResolverTest, ResolveNamedRef_SheetScopeShadows) {
    NamedRangeRegistry registry;
    registry.defineWorkbook("MyName", NamedRangeTarget::cell(cellA1Id, sheet1->id));
    registry.defineSheet("MyName", sheet1->id, NamedRangeTarget::cell(cellB1Id, sheet1->id));

    auto ast = parseFormula("=MyName");
    ASSERT_NE(ast, nullptr);

    FormulaResolver resolver(*workbook, *sheet1, &registry);
    auto result = resolver.resolve(ast.get());

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
    auto result = resolver.resolve(ast.get());

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
    auto result = resolver.resolve(ast.get());

    EXPECT_TRUE(result.success);

    auto* funcCall = static_cast<FunctionCallNode*>(ast.get());
    EXPECT_FALSE(funcCall->isVolatile);
}

TEST_F(FormulaResolverTest, ResolveVolatileFunction) {
    auto ast = parseFormula("=NOW()");
    ASSERT_NE(ast, nullptr);

    FormulaResolver resolver(*workbook, *sheet1);
    auto result = resolver.resolve(ast.get());

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
    resolver.resolve(ast.get());

    auto refs = resolver.extractReferences(ast.get());
    EXPECT_EQ(refs.size(), 1);
    EXPECT_EQ(refs[0].type, ReferenceInfo::Type::CELL);
    EXPECT_EQ(refs[0].cellId, cellA1Id);
}

TEST_F(FormulaResolverTest, ExtractReferences_Multiple) {
    auto ast = parseFormula("=A1+B1+C1");
    FormulaResolver resolver(*workbook, *sheet1);
    resolver.resolve(ast.get());

    auto refs = resolver.extractReferences(ast.get());
    EXPECT_EQ(refs.size(), 3);
}

TEST_F(FormulaResolverTest, ExtractReferences_Range) {
    auto ast = parseFormula("=A1:C3");
    FormulaResolver resolver(*workbook, *sheet1);
    resolver.resolve(ast.get());

    auto refs = resolver.extractReferences(ast.get());
    EXPECT_EQ(refs.size(), 1);
    EXPECT_EQ(refs[0].type, ReferenceInfo::Type::RANGE);
}

TEST_F(FormulaResolverTest, ExtractReferences_FunctionArgs) {
    auto ast = parseFormula("=SUM(A1,B1,C1)");
    FormulaResolver resolver(*workbook, *sheet1);
    resolver.resolve(ast.get());

    auto refs = resolver.extractReferences(ast.get());
    EXPECT_EQ(refs.size(), 3);
}

// ===========================================================================
// Display conversion
// ===========================================================================

TEST_F(FormulaResolverTest, DisplayConversion_SimpleCellRef) {
    auto ast = parseFormula("=A1");
    FormulaResolver resolver(*workbook, *sheet1);
    resolver.resolve(ast.get());

    FormulaDisplayConverter converter(*sheet1);
    std::string display = converter.toDisplayString(ast.get());

    EXPECT_EQ(display, "=A1");
}

TEST_F(FormulaResolverTest, DisplayConversion_AbsoluteRef) {
    auto ast = parseFormula("=$A$1");
    FormulaResolver resolver(*workbook, *sheet1);
    resolver.resolve(ast.get());

    FormulaDisplayConverter converter(*sheet1);
    std::string display = converter.toDisplayString(ast.get());

    EXPECT_EQ(display, "=$A$1");
}

TEST_F(FormulaResolverTest, DisplayConversion_BinaryOp) {
    auto ast = parseFormula("=A1+B1*C1");
    FormulaResolver resolver(*workbook, *sheet1);
    resolver.resolve(ast.get());

    FormulaDisplayConverter converter(*sheet1);
    std::string display = converter.toDisplayString(ast.get());

    EXPECT_EQ(display, "=A1+B1*C1");
}

TEST_F(FormulaResolverTest, DisplayConversion_Function) {
    auto ast = parseFormula("=SUM(A1,B1)");
    FormulaResolver resolver(*workbook, *sheet1);
    resolver.resolve(ast.get());

    FormulaDisplayConverter converter(*sheet1);
    std::string display = converter.toDisplayString(ast.get());

    EXPECT_EQ(display, "=SUM(A1,B1)");
}

TEST_F(FormulaResolverTest, DisplayConversion_Literals) {
    auto ast = parseFormula("=1+2.5+TRUE+\"Hello\"");
    FormulaResolver resolver(*workbook, *sheet1);
    resolver.resolve(ast.get());

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
    resolver.resolve(ast.get());

    FormulaDisplayConverter converter(*sheet1);
    std::string display = converter.toDisplayString(ast.get());

    EXPECT_EQ(display, "=A:A");
}

TEST_F(FormulaResolverTest, DisplayConversion_RowRef) {
    auto ast = parseFormula("=1:1");
    FormulaResolver resolver(*workbook, *sheet1);
    resolver.resolve(ast.get());

    FormulaDisplayConverter converter(*sheet1);
    std::string display = converter.toDisplayString(ast.get());

    EXPECT_EQ(display, "=1:1");
}

// ===========================================================================
// Round-trip tests
// ===========================================================================

TEST_F(FormulaResolverTest, RoundTrip_ParseResolveDisplay) {
    std::vector<std::string> formulas = {
        "=A1",  "=$A$1", "=A1+B1", "=A1*B1+C1", "=SUM(A1:C3)", "=IF(A1,B1,C1)", "=A:A",    "=1:1",
        "=A:C", "=1:3",  "=-A1",   "=A1^2",     "=A1&B1",      "=A1=B1",        "=A1<>B1",
    };

    for (const auto& formula : formulas) {
        auto ast = parseFormula(formula);
        ASSERT_NE(ast, nullptr) << "Failed to parse: " << formula;

        FormulaResolver resolver(*workbook, *sheet1);
        auto result = resolver.resolve(ast.get());
        EXPECT_TRUE(result.success) << "Failed to resolve: " << formula;

        FormulaDisplayConverter converter(*sheet1);
        std::string display = converter.toDisplayString(ast.get());

        // Re-parse the display string
        auto ast2 = parseFormula(display);
        ASSERT_NE(ast2, nullptr) << "Failed to re-parse display: " << display;

        // Verify it resolves successfully
        FormulaResolver resolver2(*workbook, *sheet1);
        auto result2 = resolver2.resolve(ast2.get());
        EXPECT_TRUE(result2.success) << "Failed to resolve re-parsed: " << display;
    }
}

}  // namespace
}  // namespace cells
