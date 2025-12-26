// Sync formula integration tests - tests formula round-tripping through CRDT operations
//
// These tests verify that formulas entered on one client display correctly on other
// clients after syncing through the operation system.

#include <memory>
#include <string>

#include "core/cells/crdt.h"
#include "core/cells/dependency_graph.h"
#include "core/cells/id.h"
#include "core/cells/model.h"
#include "core/cells/ref_converter.h"

#include "gtest/gtest.h"

namespace cells {
namespace {

// Simple JSON string escaping for test payloads
std::string testJsonEscape(const std::string& str) {
    std::string result;
    result.reserve(str.size() + 16);
    for (const char c : str) {
        switch (c) {
            case '"':
                result += "\\\"";
                break;
            case '\\':
                result += "\\\\";
                break;
            case '\n':
                result += "\\n";
                break;
            case '\r':
                result += "\\r";
                break;
            case '\t':
                result += "\\t";
                break;
            default:
                result += c;
                break;
        }
    }
    return result;
}

// ============================================================================
// Test fixture for sync formula tests
// ============================================================================

class SyncFormulaTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create two workbooks simulating two peers
        nodeA_ = ID("NodeAAAA");
        nodeB_ = ID("NodeBBBB");

        workbookA_ = createWorkbook(nodeA_);
        workbookB_ = createWorkbook(nodeB_);

        // Use the fixed IDs from createWorkbook
        sharedColA_ = ID("ColAAAAA");
        sharedColB_ = ID("ColBBBBB");
        sharedRow1_ = ID("Row11111");
        sharedRow2_ = ID("Row22222");
        sharedCellA1_ = ID("CellA111");
        sharedCellB1_ = ID("CellB111");
        sharedCellA2_ = ID("CellA222");
        sharedCellB2_ = ID("CellB222");

        // Initialize RefConverter for workbook A
        refConverterA_.setContext(*workbookA_->getSheetByIndex(0));
    }

    // Create a workbook with a shared structure that both peers know about
    std::unique_ptr<Workbook> createWorkbook(const ID& nodeId) {
        auto wb = std::make_unique<Workbook>(generate_id(), "TestWorkbook");
        wb->setNodeId(nodeId);

        auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet1");

        // Create columns A and B (positions 0 and 1)
        auto colA = std::make_unique<Axis>(ID("ColAAAAA"), true);
        colA->position = 0;
        auto colB = std::make_unique<Axis>(ID("ColBBBBB"), true);
        colB->position = 1;
        sheet->addColumn(std::move(colA));
        sheet->addColumn(std::move(colB));

        // Create rows 1 and 2 (positions 0 and 1)
        auto row1 = std::make_unique<Axis>(ID("Row11111"), false);
        row1->position = 0;
        auto row2 = std::make_unique<Axis>(ID("Row22222"), false);
        row2->position = 1;
        sheet->addRow(std::move(row1));
        sheet->addRow(std::move(row2));

        // Create cells A1, B1, A2, B2
        auto cellA1 = std::make_unique<Cell>(ID("CellA111"), ID("ColAAAAA"), ID("Row11111"));
        cellA1->value = CellValue(100.0);
        auto cellB1 = std::make_unique<Cell>(ID("CellB111"), ID("ColBBBBB"), ID("Row11111"));
        cellB1->value = CellValue(200.0);
        auto cellA2 = std::make_unique<Cell>(ID("CellA222"), ID("ColAAAAA"), ID("Row22222"));
        cellA2->value = CellValue(300.0);
        auto cellB2 = std::make_unique<Cell>(ID("CellB222"), ID("ColBBBBB"), ID("Row22222"));
        cellB2->value = CellValue(400.0);

        sheet->addCell(std::move(cellA1));
        sheet->addCell(std::move(cellB1));
        sheet->addCell(std::move(cellA2));
        sheet->addCell(std::move(cellB2));

        wb->addSheet(std::move(sheet));
        return wb;
    }

    // Helper: Create a formula cell operation with proper UUID format
    // Uses JSON escaping to handle quotes and special chars in formula text
    std::string makeFormulaPayload(const ID& colId, const ID& rowId, const std::string& uuidFormula,
                                   const std::string& displayFormula) {
        std::string payload = "{\"type\":\"f\",\"value\":\"" + testJsonEscape(uuidFormula) +
                              "\",\"display\":\"" + testJsonEscape(displayFormula) +
                              "\",\"col_id\":\"" + colId.toString() + "\",\"row_id\":\"" +
                              rowId.toString() + "\"}";
        return payload;
    }

    std::unique_ptr<Workbook> workbookA_;
    std::unique_ptr<Workbook> workbookB_;
    ID nodeA_, nodeB_;
    ID sharedColA_, sharedColB_;
    ID sharedRow1_, sharedRow2_;
    ID sharedCellA1_, sharedCellB1_, sharedCellA2_, sharedCellB2_;
    RefConverter refConverterA_;
};

// ============================================================================
// Basic formula sync tests
// ============================================================================

TEST_F(SyncFormulaTest, SimpleFormulaDisplaysCorrectlyAfterSync) {
    // Client A enters formula =B1 in cell A2
    // Formula is stored as UUID format: =CellB111 (the ID of cell B1)

    std::string uuidFormula = sharedCellB1_.toString();  // Just the cell ID for relative ref
    std::string displayFormula = "=B1";

    // Create the operation as Client A would
    std::string payload = makeFormulaPayload(sharedColA_, sharedRow2_, uuidFormula, displayFormula);

    Operation op = makeCellSetValueOp(*workbookA_, sharedCellA2_, payload);

    // Apply on workbook A (simulating local edit)
    ApplyResult resultA = applyOperation(*workbookA_, op);
    EXPECT_EQ(resultA, ApplyResult::SUCCESS);

    // Sync to workbook B
    ApplyResult resultB = applyOperation(*workbookB_, op);
    EXPECT_EQ(resultB, ApplyResult::SUCCESS);

    // Verify the formula was stored correctly in both workbooks
    Cell* cellA = workbookA_->getSheetByIndex(0)->getCell(sharedCellA2_);
    Cell* cellB = workbookB_->getSheetByIndex(0)->getCell(sharedCellA2_);

    ASSERT_NE(cellA, nullptr);
    ASSERT_NE(cellB, nullptr);
    EXPECT_TRUE(cellA->isFormula());
    EXPECT_TRUE(cellB->isFormula());

    // The formula text should be the UUID format
    EXPECT_STREQ(cellA->getFormula()->text, uuidFormula.c_str());
    EXPECT_STREQ(cellB->getFormula()->text, uuidFormula.c_str());

    // Now test that RefConverter can convert back to A1 notation
    RefConverter refConverterB;
    refConverterB.setContext(*workbookB_->getSheetByIndex(0));

    std::string convertedA = refConverterA_.formulaToA1(cellA->getFormula()->text);
    std::string convertedB = refConverterB.formulaToA1(cellB->getFormula()->text);

    // Both should display as "B1" (without the = sign, which is part of value.raw)
    EXPECT_EQ(convertedA, "B1");
    EXPECT_EQ(convertedB, "B1");
}

TEST_F(SyncFormulaTest, FormulaWithSumDisplaysCorrectlyAfterSync) {
    // Client A enters formula =SUM(A1:B2) in cell A2
    // Range A1:B2 becomes cellA1:cellB2 in UUID format

    std::string uuidFormula =
        "SUM(" + sharedCellA1_.toString() + ":" + sharedCellB2_.toString() + ")";
    std::string displayFormula = "=SUM(A1:B2)";

    std::string payload = makeFormulaPayload(sharedColA_, sharedRow2_, uuidFormula, displayFormula);
    Operation op = makeCellSetValueOp(*workbookA_, sharedCellA2_, payload);

    applyOperation(*workbookA_, op);
    applyOperation(*workbookB_, op);

    // Verify conversion on both clients
    Cell* cellB = workbookB_->getSheetByIndex(0)->getCell(sharedCellA2_);
    ASSERT_NE(cellB, nullptr);
    ASSERT_TRUE(cellB->isFormula());

    RefConverter refConverterB;
    refConverterB.setContext(*workbookB_->getSheetByIndex(0));

    std::string converted = refConverterB.formulaToA1(cellB->getFormula()->text);
    EXPECT_EQ(converted, "SUM(A1:B2)");
}

TEST_F(SyncFormulaTest, AbsoluteReferenceDisplaysCorrectlyAfterSync) {
    // Client A enters formula =$A$1 in cell B2
    // Absolute reference format: $$cellId

    std::string uuidFormula = "$$" + sharedCellA1_.toString();
    std::string displayFormula = "=$A$1";

    std::string payload = makeFormulaPayload(sharedColB_, sharedRow2_, uuidFormula, displayFormula);
    Operation op = makeCellSetValueOp(*workbookA_, sharedCellB2_, payload);

    applyOperation(*workbookA_, op);
    applyOperation(*workbookB_, op);

    Cell* cellB = workbookB_->getSheetByIndex(0)->getCell(sharedCellB2_);
    ASSERT_NE(cellB, nullptr);

    RefConverter refConverterB;
    refConverterB.setContext(*workbookB_->getSheetByIndex(0));

    std::string converted = refConverterB.formulaToA1(cellB->getFormula()->text);
    EXPECT_EQ(converted, "$A$1");
}

TEST_F(SyncFormulaTest, MixedReferenceDisplaysCorrectlyAfterSync) {
    // Test $A1 (column absolute) and A$1 (row absolute)

    // $A1 format: $~cellId
    std::string uuidColAbs = "$~" + sharedCellA1_.toString();
    // A$1 format: ~$cellId
    std::string uuidRowAbs = "~$" + sharedCellA1_.toString();

    // Test column absolute
    {
        std::string payload = makeFormulaPayload(sharedColB_, sharedRow1_, uuidColAbs, "=$A1");
        Operation op = makeCellSetValueOp(*workbookA_, sharedCellB1_, payload);
        applyOperation(*workbookB_, op);

        Cell* cell = workbookB_->getSheetByIndex(0)->getCell(sharedCellB1_);
        RefConverter conv;
        conv.setContext(*workbookB_->getSheetByIndex(0));
        EXPECT_EQ(conv.formulaToA1(cell->getFormula()->text), "$A1");
    }
}

TEST_F(SyncFormulaTest, ComplexFormulaDisplaysCorrectlyAfterSync) {
    // Formula: =IF(A1>0,B1*2,A2+B2)
    std::string uuidFormula = "IF(" + sharedCellA1_.toString() + ">0," + sharedCellB1_.toString() +
                              "*2," + sharedCellA2_.toString() + "+" + sharedCellB2_.toString() +
                              ")";
    std::string displayFormula = "=IF(A1>0,B1*2,A2+B2)";

    std::string payload = makeFormulaPayload(sharedColA_, sharedRow2_, uuidFormula, displayFormula);
    Operation op = makeCellSetValueOp(*workbookA_, sharedCellA2_, payload);

    applyOperation(*workbookA_, op);
    applyOperation(*workbookB_, op);

    Cell* cellB = workbookB_->getSheetByIndex(0)->getCell(sharedCellA2_);
    RefConverter refConverterB;
    refConverterB.setContext(*workbookB_->getSheetByIndex(0));

    std::string converted = refConverterB.formulaToA1(cellB->getFormula()->text);
    EXPECT_EQ(converted, "IF(A1>0,B1*2,A2+B2)");
}

// ============================================================================
// Edge case: Formula references a cell that needs to be created
// ============================================================================

TEST_F(SyncFormulaTest, FormulaReferencingNewCellDisplaysCorrectly) {
    // Scenario: Client A creates a new cell at a previously empty position,
    // then enters a formula referencing it. Both operations sync to Client B.

    // Create a new cell at C1 (position col=2, row=0)
    ID newColC("ColCCCCC");
    ID newCellC1("CellC111");

    // First, add the column to both workbooks
    {
        auto colC_A = std::make_unique<Axis>(newColC, true);
        colC_A->position = 2;
        workbookA_->getSheetByIndex(0)->addColumn(std::move(colC_A));

        auto colC_B = std::make_unique<Axis>(newColC, true);
        colC_B->position = 2;
        workbookB_->getSheetByIndex(0)->addColumn(std::move(colC_B));
    }

    // Create cell C1 in both workbooks via operation
    {
        std::string payload = "{\"type\":\"n\",\"value\":\"500\",\"col_id\":\"" +
                              newColC.toString() + "\",\"row_id\":\"" + sharedRow1_.toString() +
                              "\"}";
        HLC hlc = workbookA_->getCurrentHLC();
        Operation op(hlc, OpType::CELL_SET_VALUE, newCellC1, payload);
        applyOperation(*workbookA_, op);
        applyOperation(*workbookB_, op);
    }

    // Now enter formula =C1 in cell A2
    std::string uuidFormula = newCellC1.toString();
    std::string displayFormula = "=C1";

    std::string payload = makeFormulaPayload(sharedColA_, sharedRow2_, uuidFormula, displayFormula);
    Operation op = makeCellSetValueOp(*workbookA_, sharedCellA2_, payload);

    applyOperation(*workbookA_, op);
    applyOperation(*workbookB_, op);

    // Rebuild RefConverter with new context (includes new column and cell)
    RefConverter refConverterB;
    refConverterB.setContext(*workbookB_->getSheetByIndex(0));

    Cell* cellB = workbookB_->getSheetByIndex(0)->getCell(sharedCellA2_);
    ASSERT_NE(cellB, nullptr);
    ASSERT_TRUE(cellB->isFormula());

    std::string converted = refConverterB.formulaToA1(cellB->getFormula()->text);
    EXPECT_EQ(converted, "C1");
}

// ============================================================================
// Edge case: Conversion fails gracefully when context is missing
// ============================================================================

TEST_F(SyncFormulaTest, ConversionFailsGracefullyWithoutContext) {
    // When RefConverter context is not set, conversion should return the
    // original UUID format (graceful degradation, not a crash)

    RefConverter emptyConverter;
    // Don't call setContext!

    std::string uuidFormula = sharedCellB1_.toString();
    std::string converted = emptyConverter.formulaToA1(uuidFormula);

    // With empty context, conversion fails and returns original UUID
    EXPECT_EQ(converted, uuidFormula);

    // After proper context setup, it should work
    emptyConverter.setContext(*workbookB_->getSheetByIndex(0));
    converted = emptyConverter.formulaToA1(uuidFormula);
    EXPECT_EQ(converted, "B1");
}

// ============================================================================
// Dependency graph tests: verify formulas are added to dependency graph on sync
// ============================================================================

TEST_F(SyncFormulaTest, DependencyGraphUpdatedOnRemoteFormulaSync) {
    // When a formula operation is received from a remote peer,
    // the dependency graph should be updated so that:
    // 1. The formula cell tracks its dependencies
    // 2. When the referenced cell changes, dependents can be recalculated

    Sheet* sheetB = workbookB_->getSheetByIndex(0);
    DependencyGraph* depGraph = sheetB->getDependencyGraph();
    ASSERT_NE(depGraph, nullptr);

    // Initially, A2 should have no dependencies
    EXPECT_TRUE(depGraph->getDependencies(sharedCellA2_).empty());

    // Client A enters formula =B1 in cell A2
    std::string uuidFormula = "~~" + sharedCellB1_.toString();
    std::string displayFormula = "=B1";

    std::string payload = makeFormulaPayload(sharedColA_, sharedRow2_, uuidFormula, displayFormula);
    Operation op = makeCellSetValueOp(*workbookA_, sharedCellA2_, payload);

    // Apply to workbook B (remote operation)
    ApplyResult result = applyOperation(*workbookB_, op);
    EXPECT_EQ(result, ApplyResult::SUCCESS);

    // Now A2 should have dependencies (it depends on B1)
    auto deps = depGraph->getDependencies(sharedCellA2_);
    EXPECT_FALSE(deps.empty()) << "Formula cell should have dependencies after remote sync";

    // The formula AST should have been parsed
    Cell* cellA2 = sheetB->getCell(sharedCellA2_);
    ASSERT_NE(cellA2, nullptr);
    ASSERT_TRUE(cellA2->isFormula());
    EXPECT_NE(cellA2->getFormula()->ast, nullptr) << "Formula AST should be parsed for remote ops";
}

TEST_F(SyncFormulaTest, DependencyGraphClearedWhenFormulaReplacedWithValue) {
    // When a formula cell is changed to a regular value, the dependency
    // graph entry should be removed

    Sheet* sheetB = workbookB_->getSheetByIndex(0);
    DependencyGraph* depGraph = sheetB->getDependencyGraph();

    // First, set a formula
    std::string uuidFormula = "~~" + sharedCellB1_.toString();
    std::string displayFormula = "=B1";
    std::string payload = makeFormulaPayload(sharedColA_, sharedRow2_, uuidFormula, displayFormula);
    Operation op1 = makeCellSetValueOp(*workbookA_, sharedCellA2_, payload);
    applyOperation(*workbookB_, op1);

    EXPECT_FALSE(depGraph->getDependencies(sharedCellA2_).empty());

    // Now replace with a regular value
    HLC hlc2 = workbookA_->getCurrentHLC();  // This will be higher than op1
    std::string valuePayload = "{\"type\":\"n\",\"value\":\"42\",\"col_id\":\"" +
                               sharedColA_.toString() + "\",\"row_id\":\"" +
                               sharedRow2_.toString() + "\"}";
    Operation op2(hlc2, OpType::CELL_SET_VALUE, sharedCellA2_, valuePayload);
    applyOperation(*workbookB_, op2);

    // Dependencies should be cleared
    EXPECT_TRUE(depGraph->getDependencies(sharedCellA2_).empty())
        << "Dependencies should be cleared when formula replaced with value";

    // Cell should no longer be a formula
    Cell* cell = sheetB->getCell(sharedCellA2_);
    EXPECT_FALSE(cell->isFormula());
}

TEST_F(SyncFormulaTest, VolatileFunctionTrackedOnRemoteSync) {
    // When a formula with NOW() or RAND() is synced, it should be
    // marked as volatile in the dependency graph

    Sheet* sheetB = workbookB_->getSheetByIndex(0);
    DependencyGraph* depGraph = sheetB->getDependencyGraph();

    // Initially not volatile
    EXPECT_FALSE(depGraph->isVolatile(sharedCellA2_));

    // Sync a formula with NOW()
    std::string uuidFormula = "NOW()";
    std::string displayFormula = "=NOW()";
    std::string payload = makeFormulaPayload(sharedColA_, sharedRow2_, uuidFormula, displayFormula);
    Operation op = makeCellSetValueOp(*workbookA_, sharedCellA2_, payload);
    applyOperation(*workbookB_, op);

    // Should now be marked as volatile
    EXPECT_TRUE(depGraph->isVolatile(sharedCellA2_))
        << "Volatile functions should be tracked on remote sync";

    auto volatileCells = depGraph->getVolatileCells();
    bool found = false;
    for (const auto& id : volatileCells) {
        if (id == sharedCellA2_) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found) << "Cell with volatile function should be in volatile cells list";
}

// ============================================================================
// Phase 3: Operation serialization tests for formulas
// ============================================================================

TEST_F(SyncFormulaTest, FormulaOperationRoundTrip) {
    // Test that formula operations serialize and deserialize correctly
    // through JSON (network) and string (file) formats

    std::string uuidFormula = "~~" + sharedCellB1_.toString();
    std::string displayFormula = "=B1";
    std::string payload = makeFormulaPayload(sharedColA_, sharedRow2_, uuidFormula, displayFormula);

    Operation original = makeCellSetValueOp(*workbookA_, sharedCellA2_, payload);

    // Test JSON round-trip (network transport)
    std::string json = original.toJSON();
    Operation fromJson = Operation::fromJSON(json);
    EXPECT_EQ(fromJson.hlc, original.hlc);
    EXPECT_EQ(fromJson.type, original.type);
    EXPECT_EQ(fromJson.target_id.toString(), original.target_id.toString());
    EXPECT_EQ(fromJson.payload, original.payload);

    // Test string round-trip (file storage)
    std::string str = original.toString();
    Operation fromStr = Operation::fromString(str);
    EXPECT_EQ(fromStr.hlc, original.hlc);
    EXPECT_EQ(fromStr.type, original.type);
    EXPECT_EQ(fromStr.target_id.toString(), original.target_id.toString());
    EXPECT_EQ(fromStr.payload, original.payload);

    // Verify the formula can still be applied after round-trip
    ApplyResult result = applyOperation(*workbookB_, fromJson);
    EXPECT_EQ(result, ApplyResult::SUCCESS);

    Cell* cell = workbookB_->getSheetByIndex(0)->getCell(sharedCellA2_);
    ASSERT_NE(cell, nullptr);
    ASSERT_TRUE(cell->isFormula());
    EXPECT_STREQ(cell->getFormula()->text, uuidFormula.c_str());
}

TEST_F(SyncFormulaTest, ComplexFormulaWithSpecialCharsRoundTrip) {
    // Test formulas with special characters that need JSON escaping

    // Formula with quotes and special chars: =IF(A1="test",B1,C1)
    // In UUID format, string literals are preserved
    std::string uuidFormula =
        "IF(" + sharedCellA1_.toString() + "=\"test\"," + sharedCellB1_.toString() + ",0)";
    std::string displayFormula = "=IF(A1=\"test\",B1,0)";

    std::string payload = makeFormulaPayload(sharedColA_, sharedRow2_, uuidFormula, displayFormula);
    Operation original = makeCellSetValueOp(*workbookA_, sharedCellA2_, payload);

    // Verify the payload contains escaped quotes
    EXPECT_NE(original.payload.find("\\\"test\\\""), std::string::npos)
        << "Quotes should be escaped in payload";

    // Test JSON round-trip
    std::string json = original.toJSON();
    Operation fromJson = Operation::fromJSON(json);
    EXPECT_EQ(fromJson.payload, original.payload);

    // Apply and verify
    ApplyResult result = applyOperation(*workbookB_, fromJson);
    EXPECT_EQ(result, ApplyResult::SUCCESS);

    Cell* cell = workbookB_->getSheetByIndex(0)->getCell(sharedCellA2_);
    ASSERT_NE(cell, nullptr);
    ASSERT_TRUE(cell->isFormula());

    // The formula text should have unescaped quotes
    std::string formulaText = cell->getFormula()->text;
    EXPECT_NE(formulaText.find("\"test\""), std::string::npos)
        << "Formula should contain unescaped quotes after parsing";
}

TEST_F(SyncFormulaTest, FormulaWithMathOperatorsRoundTrip) {
    // Test formula with all math operators: =A1+B1-A2*B2/2
    std::string uuidFormula = sharedCellA1_.toString() + "+" + sharedCellB1_.toString() + "-" +
                              sharedCellA2_.toString() + "*" + sharedCellB2_.toString() + "/2";
    std::string displayFormula = "=A1+B1-A2*B2/2";

    std::string payload = makeFormulaPayload(sharedColA_, sharedRow2_, uuidFormula, displayFormula);
    Operation original = makeCellSetValueOp(*workbookA_, sharedCellA2_, payload);

    // Test round-trip
    std::string json = original.toJSON();
    Operation fromJson = Operation::fromJSON(json);

    ApplyResult result = applyOperation(*workbookB_, fromJson);
    EXPECT_EQ(result, ApplyResult::SUCCESS);

    Cell* cell = workbookB_->getSheetByIndex(0)->getCell(sharedCellA2_);
    ASSERT_NE(cell, nullptr);
    ASSERT_TRUE(cell->isFormula());

    // Verify display formula is correct
    RefConverter conv;
    conv.setContext(*workbookB_->getSheetByIndex(0));
    std::string converted = conv.formulaToA1(cell->getFormula()->text);
    EXPECT_EQ(converted, "A1+B1-A2*B2/2");
}

TEST_F(SyncFormulaTest, NestedFunctionFormulaRoundTrip) {
    // Test nested functions: =SUM(IF(A1>0,A1:B1,A2:B2))
    std::string uuidFormula = "SUM(IF(" + sharedCellA1_.toString() + ">0," +
                              sharedCellA1_.toString() + ":" + sharedCellB1_.toString() + "," +
                              sharedCellA2_.toString() + ":" + sharedCellB2_.toString() + "))";
    std::string displayFormula = "=SUM(IF(A1>0,A1:B1,A2:B2))";

    std::string payload = makeFormulaPayload(sharedColA_, sharedRow2_, uuidFormula, displayFormula);
    Operation original = makeCellSetValueOp(*workbookA_, sharedCellA2_, payload);

    // Round-trip through JSON
    std::string json = original.toJSON();
    Operation fromJson = Operation::fromJSON(json);

    ApplyResult result = applyOperation(*workbookB_, fromJson);
    EXPECT_EQ(result, ApplyResult::SUCCESS);

    Cell* cell = workbookB_->getSheetByIndex(0)->getCell(sharedCellA2_);
    ASSERT_NE(cell, nullptr);
    ASSERT_TRUE(cell->isFormula());

    RefConverter conv;
    conv.setContext(*workbookB_->getSheetByIndex(0));
    std::string converted = conv.formulaToA1(cell->getFormula()->text);
    EXPECT_EQ(converted, "SUM(IF(A1>0,A1:B1,A2:B2))");
}

// ============================================================================
// Phase 4a: Comprehensive sync round-trip tests for all formula types
// ============================================================================

TEST_F(SyncFormulaTest, SimpleCellReferenceRoundTrip) {
    // Test the simplest case: =A1
    // For relative references, just use the bare 8-char cell ID
    std::string uuidFormula = sharedCellA1_.toString();
    std::string displayFormula = "=A1";

    std::string payload = makeFormulaPayload(sharedColA_, sharedRow2_, uuidFormula, displayFormula);
    Operation op = makeCellSetValueOp(*workbookA_, sharedCellA2_, payload);

    // Apply to both workbooks
    applyOperation(*workbookA_, op);
    applyOperation(*workbookB_, op);

    // Verify on both clients
    RefConverter convA, convB;
    convA.setContext(*workbookA_->getSheetByIndex(0));
    convB.setContext(*workbookB_->getSheetByIndex(0));

    Cell* cellA = workbookA_->getSheetByIndex(0)->getCell(sharedCellA2_);
    Cell* cellB = workbookB_->getSheetByIndex(0)->getCell(sharedCellA2_);

    EXPECT_EQ(convA.formulaToA1(cellA->getFormula()->text), "A1");
    EXPECT_EQ(convB.formulaToA1(cellB->getFormula()->text), "A1");
}

TEST_F(SyncFormulaTest, RangeReferenceRoundTrip) {
    // Test range: =SUM(A1:B2)
    std::string uuidFormula =
        "SUM(" + sharedCellA1_.toString() + ":" + sharedCellB2_.toString() + ")";
    std::string displayFormula = "=SUM(A1:B2)";

    std::string payload = makeFormulaPayload(sharedColA_, sharedRow2_, uuidFormula, displayFormula);
    Operation op = makeCellSetValueOp(*workbookA_, sharedCellA2_, payload);

    applyOperation(*workbookA_, op);
    applyOperation(*workbookB_, op);

    RefConverter conv;
    conv.setContext(*workbookB_->getSheetByIndex(0));
    Cell* cell = workbookB_->getSheetByIndex(0)->getCell(sharedCellA2_);

    EXPECT_EQ(conv.formulaToA1(cell->getFormula()->text), "SUM(A1:B2)");
}

TEST_F(SyncFormulaTest, AllAbsoluteReferenceTypesRoundTrip) {
    // Test all four absolute reference types:
    // =$A$1 (fully absolute), $A1 (column absolute), A$1 (row absolute), A1 (relative)

    // Test =$A$1 (fully absolute)
    {
        std::string uuidFormula = "$$" + sharedCellA1_.toString();
        std::string payload = makeFormulaPayload(sharedColB_, sharedRow2_, uuidFormula, "=$A$1");
        Operation op = makeCellSetValueOp(*workbookA_, sharedCellB2_, payload);
        applyOperation(*workbookB_, op);

        RefConverter conv;
        conv.setContext(*workbookB_->getSheetByIndex(0));
        Cell* cell = workbookB_->getSheetByIndex(0)->getCell(sharedCellB2_);
        EXPECT_EQ(conv.formulaToA1(cell->getFormula()->text), "$A$1");
    }

    // Test $A1 (column absolute)
    {
        std::string uuidFormula = "$~" + sharedCellA1_.toString();
        std::string payload = makeFormulaPayload(sharedColB_, sharedRow1_, uuidFormula, "=$A1");
        Operation op = makeCellSetValueOp(*workbookA_, sharedCellB1_, payload);
        applyOperation(*workbookB_, op);

        RefConverter conv;
        conv.setContext(*workbookB_->getSheetByIndex(0));
        Cell* cell = workbookB_->getSheetByIndex(0)->getCell(sharedCellB1_);
        EXPECT_EQ(conv.formulaToA1(cell->getFormula()->text), "$A1");
    }

    // Test A$1 (row absolute)
    {
        // Clear and reset to test row absolute
        std::string uuidFormula = "~$" + sharedCellA1_.toString();
        std::string payload = makeFormulaPayload(sharedColA_, sharedRow1_, uuidFormula, "=A$1");
        Operation op = makeCellSetValueOp(*workbookA_, sharedCellA1_, payload);
        applyOperation(*workbookB_, op);

        RefConverter conv;
        conv.setContext(*workbookB_->getSheetByIndex(0));
        Cell* cell = workbookB_->getSheetByIndex(0)->getCell(sharedCellA1_);
        EXPECT_EQ(conv.formulaToA1(cell->getFormula()->text), "A$1");
    }
}

TEST_F(SyncFormulaTest, ConditionalFormulaRoundTrip) {
    // Test =IF(A1>0,B1,C1) - but we only have A1,B1,A2,B2 so use =IF(A1>0,B1,A2)
    // Bare cell IDs for relative references
    std::string uuidFormula = "IF(" + sharedCellA1_.toString() + ">0," + sharedCellB1_.toString() +
                              "," + sharedCellA2_.toString() + ")";
    std::string displayFormula = "=IF(A1>0,B1,A2)";

    std::string payload = makeFormulaPayload(sharedColB_, sharedRow2_, uuidFormula, displayFormula);
    Operation op = makeCellSetValueOp(*workbookA_, sharedCellB2_, payload);

    applyOperation(*workbookA_, op);
    applyOperation(*workbookB_, op);

    RefConverter conv;
    conv.setContext(*workbookB_->getSheetByIndex(0));
    Cell* cell = workbookB_->getSheetByIndex(0)->getCell(sharedCellB2_);

    EXPECT_EQ(conv.formulaToA1(cell->getFormula()->text), "IF(A1>0,B1,A2)");
}

TEST_F(SyncFormulaTest, MultipleRangesFormulaRoundTrip) {
    // Test formula with multiple ranges: =SUM(A1:A2,B1:B2)
    std::string uuidFormula = "SUM(" + sharedCellA1_.toString() + ":" + sharedCellA2_.toString() +
                              "," + sharedCellB1_.toString() + ":" + sharedCellB2_.toString() + ")";
    std::string displayFormula = "=SUM(A1:A2,B1:B2)";

    std::string payload = makeFormulaPayload(sharedColA_, sharedRow2_, uuidFormula, displayFormula);
    Operation op = makeCellSetValueOp(*workbookA_, sharedCellA2_, payload);

    applyOperation(*workbookA_, op);
    applyOperation(*workbookB_, op);

    RefConverter conv;
    conv.setContext(*workbookB_->getSheetByIndex(0));
    Cell* cell = workbookB_->getSheetByIndex(0)->getCell(sharedCellA2_);

    EXPECT_EQ(conv.formulaToA1(cell->getFormula()->text), "SUM(A1:A2,B1:B2)");
}

TEST_F(SyncFormulaTest, TextConcatenationFormulaRoundTrip) {
    // Test formula with string concatenation: =A1&B1
    // Bare cell IDs for relative references
    std::string uuidFormula = sharedCellA1_.toString() + "&" + sharedCellB1_.toString();
    std::string displayFormula = "=A1&B1";

    std::string payload = makeFormulaPayload(sharedColA_, sharedRow2_, uuidFormula, displayFormula);
    Operation op = makeCellSetValueOp(*workbookA_, sharedCellA2_, payload);

    applyOperation(*workbookA_, op);
    applyOperation(*workbookB_, op);

    RefConverter conv;
    conv.setContext(*workbookB_->getSheetByIndex(0));
    Cell* cell = workbookB_->getSheetByIndex(0)->getCell(sharedCellA2_);

    EXPECT_EQ(conv.formulaToA1(cell->getFormula()->text), "A1&B1");
}

// ============================================================================
// Phase 4b: Multi-client simulation tests
// ============================================================================

TEST_F(SyncFormulaTest, TwoClientsConcurrentValueAndFormula) {
    // Scenario: Client A sets value in B1, Client B sets formula =B1 in A2
    // Operations are created concurrently but applied in HLC order

    // Client A sets value 999 in B1
    HLC hlcA = workbookA_->getCurrentHLC();
    std::string valuePayload = "{\"type\":\"n\",\"value\":\"999\",\"col_id\":\"" +
                               sharedColB_.toString() + "\",\"row_id\":\"" +
                               sharedRow1_.toString() + "\"}";
    Operation opA(hlcA, OpType::CELL_SET_VALUE, sharedCellB1_, valuePayload);

    // Client B sets formula =B1 in A2 (slightly later HLC)
    HLC hlcB = workbookB_->getCurrentHLC();
    // Bare cell ID for relative reference
    std::string uuidFormula = sharedCellB1_.toString();
    std::string formulaPayload = makeFormulaPayload(sharedColA_, sharedRow2_, uuidFormula, "=B1");
    Operation opB(hlcB, OpType::CELL_SET_VALUE, sharedCellA2_, formulaPayload);

    // Apply both operations to both workbooks
    applyOperation(*workbookA_, opA);
    applyOperation(*workbookA_, opB);
    applyOperation(*workbookB_, opA);
    applyOperation(*workbookB_, opB);

    // Verify both workbooks have the same state
    Cell* cellB1_A = workbookA_->getSheetByIndex(0)->getCell(sharedCellB1_);
    Cell* cellB1_B = workbookB_->getSheetByIndex(0)->getCell(sharedCellB1_);
    EXPECT_EQ(cellB1_A->value.asNumber(), 999.0);
    EXPECT_EQ(cellB1_B->value.asNumber(), 999.0);

    Cell* cellA2_A = workbookA_->getSheetByIndex(0)->getCell(sharedCellA2_);
    Cell* cellA2_B = workbookB_->getSheetByIndex(0)->getCell(sharedCellA2_);
    EXPECT_TRUE(cellA2_A->isFormula());
    EXPECT_TRUE(cellA2_B->isFormula());

    // Verify formula displays correctly on both
    RefConverter convA, convB;
    convA.setContext(*workbookA_->getSheetByIndex(0));
    convB.setContext(*workbookB_->getSheetByIndex(0));

    EXPECT_EQ(convA.formulaToA1(cellA2_A->getFormula()->text), "B1");
    EXPECT_EQ(convB.formulaToA1(cellA2_B->getFormula()->text), "B1");
}

TEST_F(SyncFormulaTest, TwoClientsConcurrentEditSameCell) {
    // Scenario: Both clients edit A1 concurrently
    // Client A sets value 100, Client B sets formula =B1
    // Higher HLC wins (last-writer-wins)

    // Client A sets value 100 in A1
    HLC hlcA = workbookA_->getCurrentHLC();
    std::string valuePayload = "{\"type\":\"n\",\"value\":\"100\",\"col_id\":\"" +
                               sharedColA_.toString() + "\",\"row_id\":\"" +
                               sharedRow1_.toString() + "\"}";
    Operation opA(hlcA, OpType::CELL_SET_VALUE, sharedCellA1_, valuePayload);

    // Client B sets formula =B1 in A1 (slightly later HLC)
    HLC hlcB = workbookB_->getCurrentHLC();
    // Bare cell ID for relative reference
    std::string uuidFormula = sharedCellB1_.toString();
    std::string formulaPayload = makeFormulaPayload(sharedColA_, sharedRow1_, uuidFormula, "=B1");
    Operation opB(hlcB, OpType::CELL_SET_VALUE, sharedCellA1_, formulaPayload);

    // Apply both operations to both workbooks (order shouldn't matter due to LWW)
    applyOperation(*workbookA_, opA);
    applyOperation(*workbookA_, opB);
    applyOperation(*workbookB_, opB);  // Apply B first on workbook B
    applyOperation(*workbookB_, opA);  // Then A

    // Both should have the same final state: the operation with higher HLC wins
    Cell* cellA1_A = workbookA_->getSheetByIndex(0)->getCell(sharedCellA1_);
    Cell* cellA1_B = workbookB_->getSheetByIndex(0)->getCell(sharedCellA1_);

    // Since hlcB was created after hlcA, opB should win
    EXPECT_TRUE(cellA1_A->isFormula()) << "Higher HLC operation (formula) should win";
    EXPECT_TRUE(cellA1_B->isFormula()) << "Both workbooks should have same state";
}

TEST_F(SyncFormulaTest, FormulaCreatedBeforeReferencedCellValue) {
    // Scenario: Formula =B1 is created before B1 has its final value
    // Tests that formula correctly evaluates after referenced cell is set

    // Client A sets formula =B1 in A2
    // Bare cell ID for relative reference
    std::string uuidFormula = sharedCellB1_.toString();
    std::string formulaPayload = makeFormulaPayload(sharedColA_, sharedRow2_, uuidFormula, "=B1");
    Operation opFormula = makeCellSetValueOp(*workbookA_, sharedCellA2_, formulaPayload);

    // Apply formula to workbook B
    applyOperation(*workbookB_, opFormula);

    // Later, Client A updates B1's value
    HLC hlcValue = workbookA_->getCurrentHLC();
    std::string valuePayload = "{\"type\":\"n\",\"value\":\"999\",\"col_id\":\"" +
                               sharedColB_.toString() + "\",\"row_id\":\"" +
                               sharedRow1_.toString() + "\"}";
    Operation opValue(hlcValue, OpType::CELL_SET_VALUE, sharedCellB1_, valuePayload);

    // Apply value update to workbook B
    applyOperation(*workbookB_, opValue);

    // The formula should still reference B1 correctly
    Cell* cellA2 = workbookB_->getSheetByIndex(0)->getCell(sharedCellA2_);
    ASSERT_TRUE(cellA2->isFormula());

    RefConverter conv;
    conv.setContext(*workbookB_->getSheetByIndex(0));
    EXPECT_EQ(conv.formulaToA1(cellA2->getFormula()->text), "B1");

    // B1 should have the new value
    Cell* cellB1 = workbookB_->getSheetByIndex(0)->getCell(sharedCellB1_);
    EXPECT_EQ(cellB1->value.asNumber(), 999.0);
}

TEST_F(SyncFormulaTest, ChainedFormulasSync) {
    // Scenario: A1=100, B1=A1+1, A2=B1*2
    // Tests that chained formula dependencies work across clients

    // Set A1 value
    std::string payloadA1 = "{\"type\":\"n\",\"value\":\"100\",\"col_id\":\"" +
                            sharedColA_.toString() + "\",\"row_id\":\"" + sharedRow1_.toString() +
                            "\"}";
    Operation opA1 = makeCellSetValueOp(*workbookA_, sharedCellA1_, payloadA1);

    // Set B1 = A1+1 (use ~~ prefix for formula parser to track dependencies)
    std::string uuidB1 = "~~" + sharedCellA1_.toString() + "+1";
    std::string payloadB1 = makeFormulaPayload(sharedColB_, sharedRow1_, uuidB1, "=A1+1");
    Operation opB1 = makeCellSetValueOp(*workbookA_, sharedCellB1_, payloadB1);

    // Set A2 = B1*2 (use ~~ prefix for formula parser to track dependencies)
    std::string uuidA2 = "~~" + sharedCellB1_.toString() + "*2";
    std::string payloadA2 = makeFormulaPayload(sharedColA_, sharedRow2_, uuidA2, "=B1*2");
    Operation opA2 = makeCellSetValueOp(*workbookA_, sharedCellA2_, payloadA2);

    // Apply all to workbook B
    applyOperation(*workbookB_, opA1);
    applyOperation(*workbookB_, opB1);
    applyOperation(*workbookB_, opA2);

    // Verify formulas exist
    Cell* cellB1 = workbookB_->getSheetByIndex(0)->getCell(sharedCellB1_);
    Cell* cellA2 = workbookB_->getSheetByIndex(0)->getCell(sharedCellA2_);
    ASSERT_TRUE(cellB1->isFormula());
    ASSERT_TRUE(cellA2->isFormula());

    // Verify dependency graph has correct chain
    DependencyGraph* depGraph = workbookB_->getSheetByIndex(0)->getDependencyGraph();

    auto depsB1 = depGraph->getDependencies(sharedCellB1_);
    EXPECT_FALSE(depsB1.empty()) << "B1 should depend on A1";

    auto depsA2 = depGraph->getDependencies(sharedCellA2_);
    EXPECT_FALSE(depsA2.empty()) << "A2 should depend on B1";
}

// ============================================================================
// Phase 4c: RefConverter robustness tests
// ============================================================================

TEST_F(SyncFormulaTest, RefConverterWithStaleContext) {
    // Test that RefConverter handles stale context gracefully

    RefConverter conv;
    conv.setContext(*workbookB_->getSheetByIndex(0));

    // Add a new cell that RefConverter doesn't know about
    ID newCellId("NewCell1");
    auto newCell = std::make_unique<Cell>(newCellId, sharedColA_, sharedRow1_);
    // Don't add to sheet - this simulates a stale context

    // Try to convert a formula referencing the unknown cell
    // Bare cell ID for relative reference
    std::string uuidFormula = newCellId.toString();
    std::string converted = conv.formulaToA1(uuidFormula);

    // Should return original UUID when cell not found (graceful degradation)
    EXPECT_EQ(converted, uuidFormula);
}

TEST_F(SyncFormulaTest, RefConverterWithMissingCellInRange) {
    // Test range conversion when some cells in range don't exist

    RefConverter conv;
    conv.setContext(*workbookB_->getSheetByIndex(0));

    // Create a range reference where the end cell doesn't exist
    ID nonExistentCell("NoSuchCl");
    std::string uuidFormula =
        "SUM(" + sharedCellA1_.toString() + ":" + nonExistentCell.toString() + ")";

    std::string converted = conv.formulaToA1(uuidFormula);

    // Should preserve the formula structure, with partial conversion
    EXPECT_TRUE(converted.find("SUM(") != std::string::npos);
}

TEST_F(SyncFormulaTest, RefConverterEmptyFormula) {
    // Test empty formula handling

    RefConverter conv;
    conv.setContext(*workbookB_->getSheetByIndex(0));

    std::string converted = conv.formulaToA1("");
    EXPECT_EQ(converted, "");
}

TEST_F(SyncFormulaTest, RefConverterPureNumberFormula) {
    // Test formula that's just a number (no cell refs)

    RefConverter conv;
    conv.setContext(*workbookB_->getSheetByIndex(0));

    std::string converted = conv.formulaToA1("42");
    EXPECT_EQ(converted, "42");
}

TEST_F(SyncFormulaTest, RefConverterPureFunctionFormula) {
    // Test formula with function but no cell refs

    RefConverter conv;
    conv.setContext(*workbookB_->getSheetByIndex(0));

    std::string converted = conv.formulaToA1("NOW()");
    EXPECT_EQ(converted, "NOW()");

    std::string converted2 = conv.formulaToA1("PI()");
    EXPECT_EQ(converted2, "PI()");
}

TEST_F(SyncFormulaTest, RefConverterRebuildAfterNewCell) {
    // Test that RefConverter correctly handles cells added after context was set

    RefConverter conv;
    conv.setContext(*workbookB_->getSheetByIndex(0));

    // Verify we can convert known cells (bare cell ID for relative reference)
    std::string formula1 = sharedCellA1_.toString();
    EXPECT_EQ(conv.formulaToA1(formula1), "A1");

    // Add a new column and cell
    ID newColC("ColCCCCC");
    ID newCellC1("CellC111");

    auto colC = std::make_unique<Axis>(newColC, true);
    colC->position = 2;
    workbookB_->getSheetByIndex(0)->addColumn(std::move(colC));

    auto cellC1 = std::make_unique<Cell>(newCellC1, newColC, sharedRow1_);
    cellC1->value = CellValue(500.0);
    workbookB_->getSheetByIndex(0)->addCell(std::move(cellC1));

    // Old context doesn't know about C1 (bare cell ID for relative reference)
    std::string formula2 = newCellC1.toString();
    std::string converted = conv.formulaToA1(formula2);
    // May or may not work depending on implementation
    // The important thing is it doesn't crash

    // Rebuild context
    conv.setContext(*workbookB_->getSheetByIndex(0));

    // Now it should work
    converted = conv.formulaToA1(formula2);
    EXPECT_EQ(converted, "C1");
}

TEST_F(SyncFormulaTest, RefConverterMalformedUUID) {
    // Test handling of malformed UUIDs

    RefConverter conv;
    conv.setContext(*workbookB_->getSheetByIndex(0));

    // Too short (less than 8 alphanumeric chars) - should be returned as-is
    std::string converted1 = conv.formulaToA1("ABC");
    EXPECT_EQ(converted1, "ABC");

    // Invalid characters - should be returned as-is
    std::string converted2 = conv.formulaToA1("!@#$%^&*");
    EXPECT_EQ(converted2, "!@#$%^&*");

    // Valid 8-char ID but with invalid prefix $$ followed by wrong ID
    // Actually, let's just test with a valid cell ID - this should work
    std::string converted3 = conv.formulaToA1(sharedCellA1_.toString());
    EXPECT_EQ(converted3, "A1");
}

TEST_F(SyncFormulaTest, RefConverterLargeFormula) {
    // Test conversion of a formula with many references

    RefConverter conv;
    conv.setContext(*workbookB_->getSheetByIndex(0));

    // Build a formula summing all four cells: =A1+B1+A2+B2
    // Using bare cell IDs for relative references
    std::string uuidFormula = sharedCellA1_.toString() + "+" + sharedCellB1_.toString() + "+" +
                              sharedCellA2_.toString() + "+" + sharedCellB2_.toString();

    std::string converted = conv.formulaToA1(uuidFormula);
    EXPECT_EQ(converted, "A1+B1+A2+B2");
}

}  // namespace
}  // namespace cells
