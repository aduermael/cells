// =============================================================================
// Style Operations Unit Tests
// =============================================================================
//
// Tests for applying styles through CRDT operations to cells, columns, rows,
// and ranges. Verifies style inheritance hierarchy and convergence.
//
// =============================================================================

#include "core/cells/crdt.h"
#include "core/cells/id.h"
#include "core/cells/range.h"
#include "core/cells/style_buffer.h"

#include "gtest/gtest.h"

namespace cells {
namespace {

// =============================================================================
// Test Fixture
// =============================================================================

class StyleOperationsTest : public ::testing::Test {
protected:
    void SetUp() override {
        workbook = std::make_unique<Workbook>(generate_id(), "TestWorkbook");
        workbook->setNodeId(generate_id());

        auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet1");
        sheet_id = sheet->id;
        sheet_ptr = sheet.get();
        sheet->setWorkbook(workbook.get());

        // Create 3 columns at positions 0, 1, 2
        for (int i = 0; i < 3; i++) {
            auto col = std::make_unique<Axis>(generate_id(), true);
            col->position = static_cast<uint32_t>(i);
            colIds[i] = col->id;
            sheet->addColumn(std::move(col));
        }

        // Create 3 rows at positions 0, 1, 2
        for (int i = 0; i < 3; i++) {
            auto row = std::make_unique<Axis>(generate_id(), false);
            row->position = static_cast<uint32_t>(i);
            rowIds[i] = row->id;
            sheet->addRow(std::move(row));
        }

        // Create a cell at (0, 0)
        cellId = generate_id();
        auto cell = std::make_unique<Cell>(cellId, colIds[0], rowIds[0]);
        cell->value = CellValue(42.0);
        sheet->addCell(std::move(cell));

        workbook->addSheet(std::move(sheet));
    }

    std::unique_ptr<Workbook> workbook;
    Sheet* sheet_ptr;
    ID sheet_id;
    ID colIds[3];
    ID rowIds[3];
    ID cellId;
};

// =============================================================================
// 1a: Test applying styles to individual cells via CELL_SET
// =============================================================================

TEST_F(StyleOperationsTest, ApplyCellStyleBold) {
    StyleBuffer style;
    style.setBold(true);

    Operation op = makeCellSetStyleOp(*workbook, cellId, style);
    ApplyResult result = applyOperation(*workbook, op);
    EXPECT_EQ(result, ApplyResult::SUCCESS);

    const StyleBuffer* appliedStyle = workbook->getEntityStyle(cellId);
    ASSERT_NE(appliedStyle, nullptr);
    EXPECT_TRUE(appliedStyle->hasBold());
    EXPECT_TRUE(appliedStyle->getBold());
}

TEST_F(StyleOperationsTest, ApplyCellStyleItalic) {
    StyleBuffer style;
    style.setItalic(true);

    Operation op = makeCellSetStyleOp(*workbook, cellId, style);
    EXPECT_EQ(applyOperation(*workbook, op), ApplyResult::SUCCESS);

    const StyleBuffer* appliedStyle = workbook->getEntityStyle(cellId);
    ASSERT_NE(appliedStyle, nullptr);
    EXPECT_TRUE(appliedStyle->hasItalic());
    EXPECT_TRUE(appliedStyle->getItalic());
}

TEST_F(StyleOperationsTest, ApplyCellStyleMultipleProperties) {
    StyleBuffer style;
    style.setBold(true);
    style.setItalic(true);
    style.setBgColorHex("#FF0000");
    style.setFontSize(14);

    Operation op = makeCellSetStyleOp(*workbook, cellId, style);
    EXPECT_EQ(applyOperation(*workbook, op), ApplyResult::SUCCESS);

    const StyleBuffer* appliedStyle = workbook->getEntityStyle(cellId);
    ASSERT_NE(appliedStyle, nullptr);
    EXPECT_TRUE(appliedStyle->getBold());
    EXPECT_TRUE(appliedStyle->getItalic());
    EXPECT_EQ(appliedStyle->getBgColorHex(), "#FF0000");
    EXPECT_EQ(appliedStyle->getFontSize(), 14);
}

TEST_F(StyleOperationsTest, UpdateCellStyleOverwrites) {
    // First style
    StyleBuffer style1;
    style1.setBold(true);
    style1.setBgColorHex("#FF0000");
    applyOperation(*workbook, makeCellSetStyleOp(*workbook, cellId, style1));

    // Second style overwrites
    StyleBuffer style2;
    style2.setBold(false);
    style2.setItalic(true);
    applyOperation(*workbook, makeCellSetStyleOp(*workbook, cellId, style2));

    const StyleBuffer* appliedStyle = workbook->getEntityStyle(cellId);
    ASSERT_NE(appliedStyle, nullptr);
    // New style replaces old completely
    EXPECT_TRUE(appliedStyle->hasBold());
    EXPECT_FALSE(appliedStyle->getBold());
    EXPECT_TRUE(appliedStyle->hasItalic());
    EXPECT_TRUE(appliedStyle->getItalic());
    // Old bgColor is gone
    EXPECT_FALSE(appliedStyle->hasBgColor());
}

TEST_F(StyleOperationsTest, ClearCellStyle) {
    // Apply style
    StyleBuffer style;
    style.setBold(true);
    applyOperation(*workbook, makeCellSetStyleOp(*workbook, cellId, style));

    // Clear style
    Operation clearOp = makeCellClearStyleOp(*workbook, cellId);
    EXPECT_EQ(applyOperation(*workbook, clearOp), ApplyResult::SUCCESS);

    const StyleBuffer* appliedStyle = workbook->getEntityStyle(cellId);
    // Should be nullptr or empty after clearing
    EXPECT_TRUE(appliedStyle == nullptr || appliedStyle->isEmpty());
}

TEST_F(StyleOperationsTest, ApplyStyleToNewCell) {
    // Create a new cell via CRDT operation with style
    ID newCellId = generate_id();
    std::string payload = R"({"col":")" + colIds[1].toString() + R"(","row":")" +
                          rowIds[1].toString() + R"(","t":"n","v":"100"})";

    HLC hlc = workbook->getCurrentHLC();
    Operation createOp(hlc, OpType::CELL_SET, newCellId, payload);
    createOp.sheetId = sheet_id;
    EXPECT_EQ(applyOperation(*workbook, createOp), ApplyResult::SUCCESS);

    // Now apply style to the new cell
    StyleBuffer style;
    style.setBold(true);
    Operation styleOp = makeCellSetStyleOp(*workbook, newCellId, style);
    EXPECT_EQ(applyOperation(*workbook, styleOp), ApplyResult::SUCCESS);

    const StyleBuffer* appliedStyle = workbook->getEntityStyle(newCellId);
    ASSERT_NE(appliedStyle, nullptr);
    EXPECT_TRUE(appliedStyle->getBold());
}

// =============================================================================
// 1b: Test applying styles to columns/rows via COL_SET/ROW_SET
// =============================================================================

TEST_F(StyleOperationsTest, ApplyColumnStyle) {
    StyleBuffer style;
    style.setBold(true);
    style.setBgColorHex("#00FF00");

    Operation op = makeAxisSetStyleOp(*workbook, colIds[0], style);
    EXPECT_EQ(applyOperation(*workbook, op), ApplyResult::SUCCESS);

    const StyleBuffer* appliedStyle = workbook->getEntityStyle(colIds[0]);
    ASSERT_NE(appliedStyle, nullptr);
    EXPECT_TRUE(appliedStyle->getBold());
    EXPECT_EQ(appliedStyle->getBgColorHex(), "#00FF00");
}

TEST_F(StyleOperationsTest, ApplyRowStyle) {
    StyleBuffer style;
    style.setItalic(true);
    style.setTextColorHex("#0000FF");

    Operation op = makeAxisSetStyleOp(*workbook, rowIds[0], style);
    EXPECT_EQ(applyOperation(*workbook, op), ApplyResult::SUCCESS);

    const StyleBuffer* appliedStyle = workbook->getEntityStyle(rowIds[0]);
    ASSERT_NE(appliedStyle, nullptr);
    EXPECT_TRUE(appliedStyle->getItalic());
    EXPECT_EQ(appliedStyle->getTextColorHex(), "#0000FF");
}

TEST_F(StyleOperationsTest, ClearColumnStyle) {
    // Apply column style
    StyleBuffer style;
    style.setBold(true);
    applyOperation(*workbook, makeAxisSetStyleOp(*workbook, colIds[0], style));

    // Clear column style
    Operation clearOp = makeAxisClearStyleOp(*workbook, colIds[0]);
    EXPECT_EQ(applyOperation(*workbook, clearOp), ApplyResult::SUCCESS);

    const StyleBuffer* appliedStyle = workbook->getEntityStyle(colIds[0]);
    EXPECT_TRUE(appliedStyle == nullptr || appliedStyle->isEmpty());
}

TEST_F(StyleOperationsTest, ClearRowStyle) {
    // Apply row style
    StyleBuffer style;
    style.setItalic(true);
    applyOperation(*workbook, makeAxisSetStyleOp(*workbook, rowIds[0], style));

    // Clear row style
    Operation clearOp = makeAxisClearStyleOp(*workbook, rowIds[0]);
    EXPECT_EQ(applyOperation(*workbook, clearOp), ApplyResult::SUCCESS);

    const StyleBuffer* appliedStyle = workbook->getEntityStyle(rowIds[0]);
    EXPECT_TRUE(appliedStyle == nullptr || appliedStyle->isEmpty());
}

TEST_F(StyleOperationsTest, MultipleColumnsWithDifferentStyles) {
    StyleBuffer style1;
    style1.setBold(true);

    StyleBuffer style2;
    style2.setItalic(true);

    applyOperation(*workbook, makeAxisSetStyleOp(*workbook, colIds[0], style1));
    applyOperation(*workbook, makeAxisSetStyleOp(*workbook, colIds[1], style2));

    const StyleBuffer* col0Style = workbook->getEntityStyle(colIds[0]);
    const StyleBuffer* col1Style = workbook->getEntityStyle(colIds[1]);

    ASSERT_NE(col0Style, nullptr);
    ASSERT_NE(col1Style, nullptr);

    EXPECT_TRUE(col0Style->getBold());
    EXPECT_FALSE(col0Style->hasItalic());

    EXPECT_FALSE(col1Style->hasBold());
    EXPECT_TRUE(col1Style->getItalic());
}

// =============================================================================
// 1c: Test applying styles to ranges via RANGE_SET with STYLE flag
// =============================================================================

TEST_F(StyleOperationsTest, ApplyRangeStyle) {
    // Create a range with STYLE flag
    ID rangeId = generate_id();
    std::string rangePayload = "{\"startCol\":\"" + colIds[0].toString() + "\",";
    rangePayload += "\"startRow\":\"" + rowIds[0].toString() + "\",";
    rangePayload += "\"endCol\":\"" + colIds[1].toString() + "\",";
    rangePayload += "\"endRow\":\"" + rowIds[1].toString() + "\",";
    rangePayload += "\"flags\":2}";  // STYLE flag = 2

    Operation rangeOp = makeRangeSetOp(*workbook, rangeId, rangePayload);
    EXPECT_EQ(applyOperation(*workbook, rangeOp), ApplyResult::SUCCESS);

    // Apply style to the range
    StyleBuffer style;
    style.setBgColorHex("#FFFF00");
    style.setBold(true);

    Operation styleOp = makeRangeSetStyleOp(*workbook, rangeId, style);
    EXPECT_EQ(applyOperation(*workbook, styleOp), ApplyResult::SUCCESS);

    // Verify the range has the style
    const Range* range = workbook->getRange(rangeId);
    ASSERT_NE(range, nullptr);
    ASSERT_TRUE(range->style.has_value());
    EXPECT_EQ(range->style->getBgColorHex(), "#FFFF00");
    EXPECT_TRUE(range->style->getBold());
}

TEST_F(StyleOperationsTest, ClearRangeStyle) {
    // Create a range and apply style
    ID rangeId = generate_id();
    std::string rangePayload = "{\"startCol\":\"" + colIds[0].toString() + "\",";
    rangePayload += "\"startRow\":\"" + rowIds[0].toString() + "\",";
    rangePayload += "\"endCol\":\"" + colIds[0].toString() + "\",";
    rangePayload += "\"endRow\":\"" + rowIds[0].toString() + "\",";
    rangePayload += "\"flags\":2}";

    applyOperation(*workbook, makeRangeSetOp(*workbook, rangeId, rangePayload));

    StyleBuffer style;
    style.setBold(true);
    applyOperation(*workbook, makeRangeSetStyleOp(*workbook, rangeId, style));

    // Clear the style
    Operation clearOp = makeRangeClearStyleOp(*workbook, rangeId);
    EXPECT_EQ(applyOperation(*workbook, clearOp), ApplyResult::SUCCESS);

    const Range* range = workbook->getRange(rangeId);
    ASSERT_NE(range, nullptr);
    EXPECT_FALSE(range->style.has_value());
}

TEST_F(StyleOperationsTest, MultipleOverlappingStyledRanges) {
    // Create two overlapping ranges with different styles
    ID rangeId1 = generate_id();
    ID rangeId2 = generate_id();

    // Range 1: A1:B2
    std::string payload1 = "{\"startCol\":\"" + colIds[0].toString() + "\",";
    payload1 += "\"startRow\":\"" + rowIds[0].toString() + "\",";
    payload1 += "\"endCol\":\"" + colIds[1].toString() + "\",";
    payload1 += "\"endRow\":\"" + rowIds[1].toString() + "\",";
    payload1 += "\"flags\":2}";
    applyOperation(*workbook, makeRangeSetOp(*workbook, rangeId1, payload1));

    StyleBuffer style1;
    style1.setBgColorHex("#FF0000");
    applyOperation(*workbook, makeRangeSetStyleOp(*workbook, rangeId1, style1));

    // Range 2: B2:C3 (overlaps at B2)
    std::string payload2 = "{\"startCol\":\"" + colIds[1].toString() + "\",";
    payload2 += "\"startRow\":\"" + rowIds[1].toString() + "\",";
    payload2 += "\"endCol\":\"" + colIds[2].toString() + "\",";
    payload2 += "\"endRow\":\"" + rowIds[2].toString() + "\",";
    payload2 += "\"flags\":2}";
    applyOperation(*workbook, makeRangeSetOp(*workbook, rangeId2, payload2));

    StyleBuffer style2;
    style2.setBgColorHex("#00FF00");
    applyOperation(*workbook, makeRangeSetStyleOp(*workbook, rangeId2, style2));

    // Verify both ranges exist with their styles
    const Range* range1 = workbook->getRange(rangeId1);
    const Range* range2 = workbook->getRange(rangeId2);

    ASSERT_NE(range1, nullptr);
    ASSERT_NE(range2, nullptr);
    ASSERT_TRUE(range1->style.has_value());
    ASSERT_TRUE(range2->style.has_value());
    EXPECT_EQ(range1->style->getBgColorHex(), "#FF0000");
    EXPECT_EQ(range2->style->getBgColorHex(), "#00FF00");
}

// =============================================================================
// 1d: Test style inheritance hierarchy (cell > range > row > column > sheet)
// =============================================================================

TEST_F(StyleOperationsTest, EffectiveStyleCellOverridesColumn) {
    // Set column style
    StyleBuffer colStyle;
    colStyle.setBgColorHex("#FF0000");  // Red
    colStyle.setBold(true);
    applyOperation(*workbook, makeAxisSetStyleOp(*workbook, colIds[0], colStyle));

    // Set cell style (should override column)
    StyleBuffer cellStyle;
    cellStyle.setBgColorHex("#00FF00");  // Green
    applyOperation(*workbook, makeCellSetStyleOp(*workbook, cellId, cellStyle));

    // Compute effective style
    const StyleBuffer* colStylePtr = workbook->getEntityStyle(colIds[0]);
    const StyleBuffer* cellStylePtr = workbook->getEntityStyle(cellId);

    std::vector<const StyleBuffer*> rangeStyles;
    StyleBuffer effective =
        StyleBuffer::getEffectiveStyle(colStylePtr, nullptr, rangeStyles, cellStylePtr);

    // Cell's green should override column's red
    EXPECT_EQ(effective.getBgColorHex(), "#00FF00");
    // Column's bold should still be present (not overridden by cell)
    EXPECT_TRUE(effective.getBold());
}

TEST_F(StyleOperationsTest, EffectiveStyleRowOverridesColumn) {
    // Set column style
    StyleBuffer colStyle;
    colStyle.setBgColorHex("#FF0000");  // Red
    applyOperation(*workbook, makeAxisSetStyleOp(*workbook, colIds[0], colStyle));

    // Set row style (should override column for bgColor)
    StyleBuffer rowStyle;
    rowStyle.setBgColorHex("#0000FF");  // Blue
    applyOperation(*workbook, makeAxisSetStyleOp(*workbook, rowIds[0], rowStyle));

    const StyleBuffer* colStylePtr = workbook->getEntityStyle(colIds[0]);
    const StyleBuffer* rowStylePtr = workbook->getEntityStyle(rowIds[0]);

    std::vector<const StyleBuffer*> rangeStyles;
    StyleBuffer effective =
        StyleBuffer::getEffectiveStyle(colStylePtr, rowStylePtr, rangeStyles, nullptr);

    // Row's blue should override column's red
    EXPECT_EQ(effective.getBgColorHex(), "#0000FF");
}

TEST_F(StyleOperationsTest, EffectiveStyleRangeOverridesRowAndColumn) {
    // Set column style
    StyleBuffer colStyle;
    colStyle.setBgColorHex("#FF0000");  // Red
    applyOperation(*workbook, makeAxisSetStyleOp(*workbook, colIds[0], colStyle));

    // Set row style
    StyleBuffer rowStyle;
    rowStyle.setBgColorHex("#0000FF");  // Blue
    applyOperation(*workbook, makeAxisSetStyleOp(*workbook, rowIds[0], rowStyle));

    // Create and style a range
    ID rangeId = generate_id();
    std::string rangePayload = "{\"startCol\":\"" + colIds[0].toString() + "\",";
    rangePayload += "\"startRow\":\"" + rowIds[0].toString() + "\",";
    rangePayload += "\"endCol\":\"" + colIds[0].toString() + "\",";
    rangePayload += "\"endRow\":\"" + rowIds[0].toString() + "\",";
    rangePayload += "\"flags\":2}";
    applyOperation(*workbook, makeRangeSetOp(*workbook, rangeId, rangePayload));

    StyleBuffer rangeStyle;
    rangeStyle.setBgColorHex("#00FF00");  // Green
    applyOperation(*workbook, makeRangeSetStyleOp(*workbook, rangeId, rangeStyle));

    const StyleBuffer* colStylePtr = workbook->getEntityStyle(colIds[0]);
    const StyleBuffer* rowStylePtr = workbook->getEntityStyle(rowIds[0]);
    const Range* range = workbook->getRange(rangeId);
    ASSERT_NE(range, nullptr);

    std::vector<const StyleBuffer*> rangeStyles;
    if (range->style.has_value()) {
        rangeStyles.push_back(&(range->style.value()));
    }

    StyleBuffer effective =
        StyleBuffer::getEffectiveStyle(colStylePtr, rowStylePtr, rangeStyles, nullptr);

    // Range's green should win
    EXPECT_EQ(effective.getBgColorHex(), "#00FF00");
}

TEST_F(StyleOperationsTest, EffectiveStyleCellOverridesAll) {
    // Set column style
    StyleBuffer colStyle;
    colStyle.setBgColorHex("#FF0000");  // Red
    colStyle.setBold(true);
    applyOperation(*workbook, makeAxisSetStyleOp(*workbook, colIds[0], colStyle));

    // Set row style
    StyleBuffer rowStyle;
    rowStyle.setItalic(true);
    applyOperation(*workbook, makeAxisSetStyleOp(*workbook, rowIds[0], rowStyle));

    // Create and style a range
    ID rangeId = generate_id();
    std::string rangePayload = "{\"startCol\":\"" + colIds[0].toString() + "\",";
    rangePayload += "\"startRow\":\"" + rowIds[0].toString() + "\",";
    rangePayload += "\"endCol\":\"" + colIds[0].toString() + "\",";
    rangePayload += "\"endRow\":\"" + rowIds[0].toString() + "\",";
    rangePayload += "\"flags\":2}";
    applyOperation(*workbook, makeRangeSetOp(*workbook, rangeId, rangePayload));

    StyleBuffer rangeStyle;
    rangeStyle.setUnderline(true);
    applyOperation(*workbook, makeRangeSetStyleOp(*workbook, rangeId, rangeStyle));

    // Set cell style (highest priority)
    StyleBuffer cellStyle;
    cellStyle.setBgColorHex("#FFFFFF");  // White - overrides column's red
    applyOperation(*workbook, makeCellSetStyleOp(*workbook, cellId, cellStyle));

    // Compute effective style
    const StyleBuffer* colStylePtr = workbook->getEntityStyle(colIds[0]);
    const StyleBuffer* rowStylePtr = workbook->getEntityStyle(rowIds[0]);
    const StyleBuffer* cellStylePtr = workbook->getEntityStyle(cellId);
    const Range* range = workbook->getRange(rangeId);

    std::vector<const StyleBuffer*> rangeStyles;
    if (range && range->style.has_value()) {
        rangeStyles.push_back(&(range->style.value()));
    }

    StyleBuffer effective =
        StyleBuffer::getEffectiveStyle(colStylePtr, rowStylePtr, rangeStyles, cellStylePtr);

    // Cell's white bgColor overrides all
    EXPECT_EQ(effective.getBgColorHex(), "#FFFFFF");
    // Column's bold should still be present
    EXPECT_TRUE(effective.getBold());
    // Row's italic should still be present
    EXPECT_TRUE(effective.getItalic());
    // Range's underline should still be present
    EXPECT_TRUE(effective.getUnderline());
}

TEST_F(StyleOperationsTest, EffectiveStyleMultipleRangesLaterWins) {
    // Create two ranges at the same location
    ID rangeId1 = generate_id();
    ID rangeId2 = generate_id();

    std::string payload = "{\"startCol\":\"" + colIds[0].toString() + "\",";
    payload += "\"startRow\":\"" + rowIds[0].toString() + "\",";
    payload += "\"endCol\":\"" + colIds[0].toString() + "\",";
    payload += "\"endRow\":\"" + rowIds[0].toString() + "\",";
    payload += "\"flags\":2}";

    applyOperation(*workbook, makeRangeSetOp(*workbook, rangeId1, payload));
    applyOperation(*workbook, makeRangeSetOp(*workbook, rangeId2, payload));

    StyleBuffer style1;
    style1.setBgColorHex("#FF0000");  // Red
    applyOperation(*workbook, makeRangeSetStyleOp(*workbook, rangeId1, style1));

    StyleBuffer style2;
    style2.setBgColorHex("#00FF00");  // Green
    applyOperation(*workbook, makeRangeSetStyleOp(*workbook, rangeId2, style2));

    const Range* range1 = workbook->getRange(rangeId1);
    const Range* range2 = workbook->getRange(rangeId2);

    // Later range in the list has higher priority
    std::vector<const StyleBuffer*> rangeStyles;
    if (range1 && range1->style.has_value()) {
        rangeStyles.push_back(&(range1->style.value()));
    }
    if (range2 && range2->style.has_value()) {
        rangeStyles.push_back(&(range2->style.value()));
    }

    StyleBuffer effective = StyleBuffer::getEffectiveStyle(nullptr, nullptr, rangeStyles, nullptr);

    // Second range's green should win
    EXPECT_EQ(effective.getBgColorHex(), "#00FF00");
}

// =============================================================================
// 1e: Test concurrent style changes from multiple peers (CRDT convergence)
// =============================================================================

class StyleConvergenceTest : public ::testing::Test {
protected:
    void SetUp() override {
        node_a = ID("NodeAAAA");
        node_b = ID("NodeBBBB");

        workbook_a = createWorkbook(node_a);
        workbook_b = createWorkbook(node_b);
    }

    std::unique_ptr<Workbook> createWorkbook(const ID& nodeId) {
        auto wb = std::make_unique<Workbook>(generate_id(), "TestWorkbook");
        wb->setNodeId(nodeId);

        // Shared sheet id so COL_SET with sheetId hits the same sheet on every peer
        auto sheet = std::make_unique<Sheet>(shared_sheet, "Sheet1");
        sheet->setWorkbook(wb.get());

        auto col = std::make_unique<Axis>(shared_col, true);
        auto row = std::make_unique<Axis>(shared_row, false);
        sheet->addColumn(std::move(col));
        sheet->addRow(std::move(row));

        auto cell = std::make_unique<Cell>(shared_cell, shared_col, shared_row);
        cell->value = CellValue(0.0);
        sheet->addCell(std::move(cell));

        wb->addSheet(std::move(sheet));
        return wb;
    }

    std::unique_ptr<Workbook> workbook_a;
    std::unique_ptr<Workbook> workbook_b;
    ID node_a, node_b;

    // Shared IDs across workbooks
    ID shared_sheet = ID("Sheet111");
    ID shared_col = generate_id();
    ID shared_row = generate_id();
    ID shared_cell = generate_id();
};

TEST_F(StyleConvergenceTest, ConcurrentCellStyleChangesConverge) {
    // Peer A sets bold style
    StyleBuffer styleA;
    styleA.setBold(true);

    HLC hlc_a(1000, 0, node_a);
    std::string payloadA = R"({"sty":")" + styleA.toBase64() + R"("})";
    Operation opA(hlc_a, OpType::CELL_SET, shared_cell, payloadA);
    opA.sheetId = shared_sheet;

    // Peer B sets italic style at same time
    StyleBuffer styleB;
    styleB.setItalic(true);

    HLC hlc_b(1000, 0, node_b);
    std::string payloadB = R"({"sty":")" + styleB.toBase64() + R"("})";
    Operation opB(hlc_b, OpType::CELL_SET, shared_cell, payloadB);
    opB.sheetId = shared_sheet;

    // Apply in different orders
    applyOperation(*workbook_a, opA);
    applyOperation(*workbook_a, opB);

    applyOperation(*workbook_b, opB);
    applyOperation(*workbook_b, opA);

    // Both should converge to the same style (NodeBBBB > NodeAAAA)
    const StyleBuffer* styleFromA = workbook_a->getEntityStyle(shared_cell);
    const StyleBuffer* styleFromB = workbook_b->getEntityStyle(shared_cell);

    ASSERT_NE(styleFromA, nullptr);
    ASSERT_NE(styleFromB, nullptr);

    // Both should have italic (from opB which wins due to node ID ordering)
    EXPECT_TRUE(styleFromA->hasItalic());
    EXPECT_TRUE(styleFromB->hasItalic());
}

TEST_F(StyleConvergenceTest, ConcurrentColumnStyleChangesConverge) {
    // Peer A sets column to red background
    StyleBuffer styleA;
    styleA.setBgColorHex("#FF0000");

    HLC hlc_a(1000, 0, node_a);
    std::string payloadA = R"({"sty":")" + styleA.toBase64() + R"("})";
    Operation opA(hlc_a, OpType::COL_SET, shared_col, payloadA);
    opA.sheetId = shared_sheet;

    // Peer B sets column to blue background at same time
    StyleBuffer styleB;
    styleB.setBgColorHex("#0000FF");

    HLC hlc_b(1000, 0, node_b);
    std::string payloadB = R"({"sty":")" + styleB.toBase64() + R"("})";
    Operation opB(hlc_b, OpType::COL_SET, shared_col, payloadB);
    opB.sheetId = shared_sheet;

    // Apply in different orders
    applyOperation(*workbook_a, opA);
    applyOperation(*workbook_a, opB);

    applyOperation(*workbook_b, opB);
    applyOperation(*workbook_b, opA);

    // Both should converge
    const StyleBuffer* styleFromA = workbook_a->getEntityStyle(shared_col);
    const StyleBuffer* styleFromB = workbook_b->getEntityStyle(shared_col);

    ASSERT_NE(styleFromA, nullptr);
    ASSERT_NE(styleFromB, nullptr);

    // Both should have the same color
    EXPECT_EQ(styleFromA->getBgColorHex(), styleFromB->getBgColorHex());
}

TEST_F(StyleConvergenceTest, ThreePeersStyleConvergence) {
    ID node_c("NodeCCCC");
    auto workbook_c = createWorkbook(node_c);

    // All three peers set different styles at same time
    StyleBuffer styleA;
    styleA.setBgColorHex("#FF0000");  // Red

    StyleBuffer styleB;
    styleB.setBgColorHex("#00FF00");  // Green

    StyleBuffer styleC;
    styleC.setBgColorHex("#0000FF");  // Blue

    HLC hlc_a(1000, 0, node_a);
    HLC hlc_b(1000, 0, node_b);
    HLC hlc_c(1000, 0, node_c);

    std::string payloadA = R"({"sty":")" + styleA.toBase64() + R"("})";
    std::string payloadB = R"({"sty":")" + styleB.toBase64() + R"("})";
    std::string payloadC = R"({"sty":")" + styleC.toBase64() + R"("})";

    Operation opA(hlc_a, OpType::CELL_SET, shared_cell, payloadA);
    opA.sheetId = shared_sheet;
    Operation opB(hlc_b, OpType::CELL_SET, shared_cell, payloadB);
    opB.sheetId = shared_sheet;
    Operation opC(hlc_c, OpType::CELL_SET, shared_cell, payloadC);
    opC.sheetId = shared_sheet;

    // Apply in different orders on each workbook
    applyOperation(*workbook_a, opA);
    applyOperation(*workbook_a, opB);
    applyOperation(*workbook_a, opC);

    applyOperation(*workbook_b, opC);
    applyOperation(*workbook_b, opA);
    applyOperation(*workbook_b, opB);

    applyOperation(*workbook_c, opB);
    applyOperation(*workbook_c, opC);
    applyOperation(*workbook_c, opA);

    // All should converge (NodeCCCC > NodeBBBB > NodeAAAA)
    const StyleBuffer* fromA = workbook_a->getEntityStyle(shared_cell);
    const StyleBuffer* fromB = workbook_b->getEntityStyle(shared_cell);
    const StyleBuffer* fromC = workbook_c->getEntityStyle(shared_cell);

    ASSERT_NE(fromA, nullptr);
    ASSERT_NE(fromB, nullptr);
    ASSERT_NE(fromC, nullptr);

    // All should have blue (from NodeCCCC)
    EXPECT_EQ(fromA->getBgColorHex(), "#0000FF");
    EXPECT_EQ(fromB->getBgColorHex(), "#0000FF");
    EXPECT_EQ(fromC->getBgColorHex(), "#0000FF");
}

TEST_F(StyleConvergenceTest, OutOfOrderStyleDeliveryConverges) {
    // Peer A makes sequential style changes
    std::vector<Operation> ops;
    for (int i = 0; i < 5; i++) {
        StyleBuffer style;
        style.setFontSize(static_cast<uint8_t>(10 + i));
        ops.push_back(makeCellSetStyleOp(*workbook_a, shared_cell, style));
        applyOperation(*workbook_a, ops.back());
    }

    // Apply to workbook_b in reverse order
    for (auto it = ops.rbegin(); it != ops.rend(); ++it) {
        applyOperation(*workbook_b, *it);
    }

    // Both should have font size 14 (last operation)
    const StyleBuffer* fromA = workbook_a->getEntityStyle(shared_cell);
    const StyleBuffer* fromB = workbook_b->getEntityStyle(shared_cell);

    ASSERT_NE(fromA, nullptr);
    ASSERT_NE(fromB, nullptr);

    EXPECT_EQ(fromA->getFontSize(), 14);
    EXPECT_EQ(fromB->getFontSize(), 14);
}

// =============================================================================
// 1f: Test all style properties
// =============================================================================

TEST_F(StyleOperationsTest, StylePropertyBold) {
    StyleBuffer style;
    style.setBold(true);
    applyOperation(*workbook, makeCellSetStyleOp(*workbook, cellId, style));

    const StyleBuffer* applied = workbook->getEntityStyle(cellId);
    ASSERT_NE(applied, nullptr);
    EXPECT_TRUE(applied->hasBold());
    EXPECT_TRUE(applied->getBold());
}

TEST_F(StyleOperationsTest, StylePropertyBoldFalse) {
    StyleBuffer style;
    style.setBold(false);  // Explicitly false
    applyOperation(*workbook, makeCellSetStyleOp(*workbook, cellId, style));

    const StyleBuffer* applied = workbook->getEntityStyle(cellId);
    ASSERT_NE(applied, nullptr);
    EXPECT_TRUE(applied->hasBold());
    EXPECT_FALSE(applied->getBold());
}

TEST_F(StyleOperationsTest, StylePropertyItalic) {
    StyleBuffer style;
    style.setItalic(true);
    applyOperation(*workbook, makeCellSetStyleOp(*workbook, cellId, style));

    const StyleBuffer* applied = workbook->getEntityStyle(cellId);
    ASSERT_NE(applied, nullptr);
    EXPECT_TRUE(applied->hasItalic());
    EXPECT_TRUE(applied->getItalic());
}

TEST_F(StyleOperationsTest, StylePropertyUnderline) {
    StyleBuffer style;
    style.setUnderline(true);
    applyOperation(*workbook, makeCellSetStyleOp(*workbook, cellId, style));

    const StyleBuffer* applied = workbook->getEntityStyle(cellId);
    ASSERT_NE(applied, nullptr);
    EXPECT_TRUE(applied->hasUnderline());
    EXPECT_TRUE(applied->getUnderline());
}

TEST_F(StyleOperationsTest, StylePropertyStrikethrough) {
    StyleBuffer style;
    style.setStrikethrough(true);
    applyOperation(*workbook, makeCellSetStyleOp(*workbook, cellId, style));

    const StyleBuffer* applied = workbook->getEntityStyle(cellId);
    ASSERT_NE(applied, nullptr);
    EXPECT_TRUE(applied->hasStrikethrough());
    EXPECT_TRUE(applied->getStrikethrough());
}

TEST_F(StyleOperationsTest, StylePropertyTextWrap) {
    StyleBuffer style;
    style.setTextWrap(true);
    applyOperation(*workbook, makeCellSetStyleOp(*workbook, cellId, style));

    const StyleBuffer* applied = workbook->getEntityStyle(cellId);
    ASSERT_NE(applied, nullptr);
    EXPECT_TRUE(applied->hasTextWrap());
    EXPECT_TRUE(applied->getTextWrap());
}

TEST_F(StyleOperationsTest, StylePropertyFontFamily) {
    StyleBuffer style;
    style.setFontFamily("Arial");
    applyOperation(*workbook, makeCellSetStyleOp(*workbook, cellId, style));

    const StyleBuffer* applied = workbook->getEntityStyle(cellId);
    ASSERT_NE(applied, nullptr);
    EXPECT_TRUE(applied->hasFontFamily());
    EXPECT_EQ(applied->getFontFamily(), "Arial");
}

TEST_F(StyleOperationsTest, StylePropertyFontFamilyWithSpaces) {
    StyleBuffer style;
    style.setFontFamily("Times New Roman");
    applyOperation(*workbook, makeCellSetStyleOp(*workbook, cellId, style));

    const StyleBuffer* applied = workbook->getEntityStyle(cellId);
    ASSERT_NE(applied, nullptr);
    EXPECT_EQ(applied->getFontFamily(), "Times New Roman");
}

TEST_F(StyleOperationsTest, StylePropertyFontSize) {
    StyleBuffer style;
    style.setFontSize(16);
    applyOperation(*workbook, makeCellSetStyleOp(*workbook, cellId, style));

    const StyleBuffer* applied = workbook->getEntityStyle(cellId);
    ASSERT_NE(applied, nullptr);
    EXPECT_TRUE(applied->hasFontSize());
    EXPECT_EQ(applied->getFontSize(), 16);
}

TEST_F(StyleOperationsTest, StylePropertyFontSizeRange) {
    // Test minimum
    StyleBuffer style1;
    style1.setFontSize(6);
    applyOperation(*workbook, makeCellSetStyleOp(*workbook, cellId, style1));
    const StyleBuffer* applied1 = workbook->getEntityStyle(cellId);
    ASSERT_NE(applied1, nullptr);
    EXPECT_EQ(applied1->getFontSize(), 6);

    // Test maximum practical
    StyleBuffer style2;
    style2.setFontSize(72);
    applyOperation(*workbook, makeCellSetStyleOp(*workbook, cellId, style2));
    const StyleBuffer* applied2 = workbook->getEntityStyle(cellId);
    ASSERT_NE(applied2, nullptr);
    EXPECT_EQ(applied2->getFontSize(), 72);
}

TEST_F(StyleOperationsTest, StylePropertyBgColor) {
    StyleBuffer style;
    style.setBgColorHex("#FF5733");
    applyOperation(*workbook, makeCellSetStyleOp(*workbook, cellId, style));

    const StyleBuffer* applied = workbook->getEntityStyle(cellId);
    ASSERT_NE(applied, nullptr);
    EXPECT_TRUE(applied->hasBgColor());
    EXPECT_EQ(applied->getBgColorHex(), "#FF5733");
}

TEST_F(StyleOperationsTest, StylePropertyTextColor) {
    StyleBuffer style;
    style.setTextColorHex("#1A2B3C");
    applyOperation(*workbook, makeCellSetStyleOp(*workbook, cellId, style));

    const StyleBuffer* applied = workbook->getEntityStyle(cellId);
    ASSERT_NE(applied, nullptr);
    EXPECT_TRUE(applied->hasTextColor());
    EXPECT_EQ(applied->getTextColorHex(), "#1A2B3C");
}

TEST_F(StyleOperationsTest, StylePropertyBothColors) {
    StyleBuffer style;
    style.setBgColorHex("#FFFF00");    // Yellow background
    style.setTextColorHex("#000000");  // Black text
    applyOperation(*workbook, makeCellSetStyleOp(*workbook, cellId, style));

    const StyleBuffer* applied = workbook->getEntityStyle(cellId);
    ASSERT_NE(applied, nullptr);
    EXPECT_EQ(applied->getBgColorHex(), "#FFFF00");
    EXPECT_EQ(applied->getTextColorHex(), "#000000");
}

TEST_F(StyleOperationsTest, StylePropertyHAlignLeft) {
    StyleBuffer style;
    style.setHAlign(TextAlign::LEFT);
    applyOperation(*workbook, makeCellSetStyleOp(*workbook, cellId, style));

    const StyleBuffer* applied = workbook->getEntityStyle(cellId);
    ASSERT_NE(applied, nullptr);
    EXPECT_TRUE(applied->hasHAlign());
    EXPECT_EQ(applied->getHAlign(), TextAlign::LEFT);
}

TEST_F(StyleOperationsTest, StylePropertyHAlignCenter) {
    StyleBuffer style;
    style.setHAlign(TextAlign::CENTER);
    applyOperation(*workbook, makeCellSetStyleOp(*workbook, cellId, style));

    const StyleBuffer* applied = workbook->getEntityStyle(cellId);
    ASSERT_NE(applied, nullptr);
    EXPECT_EQ(applied->getHAlign(), TextAlign::CENTER);
}

TEST_F(StyleOperationsTest, StylePropertyHAlignRight) {
    StyleBuffer style;
    style.setHAlign(TextAlign::RIGHT);
    applyOperation(*workbook, makeCellSetStyleOp(*workbook, cellId, style));

    const StyleBuffer* applied = workbook->getEntityStyle(cellId);
    ASSERT_NE(applied, nullptr);
    EXPECT_EQ(applied->getHAlign(), TextAlign::RIGHT);
}

TEST_F(StyleOperationsTest, StylePropertyHAlignJustify) {
    StyleBuffer style;
    style.setHAlign(TextAlign::JUSTIFY);
    applyOperation(*workbook, makeCellSetStyleOp(*workbook, cellId, style));

    const StyleBuffer* applied = workbook->getEntityStyle(cellId);
    ASSERT_NE(applied, nullptr);
    EXPECT_EQ(applied->getHAlign(), TextAlign::JUSTIFY);
}

TEST_F(StyleOperationsTest, StylePropertyHAlignGeneral) {
    StyleBuffer style;
    style.setHAlign(TextAlign::GENERAL);
    applyOperation(*workbook, makeCellSetStyleOp(*workbook, cellId, style));

    const StyleBuffer* applied = workbook->getEntityStyle(cellId);
    ASSERT_NE(applied, nullptr);
    EXPECT_EQ(applied->getHAlign(), TextAlign::GENERAL);
}

TEST_F(StyleOperationsTest, StylePropertyVAlignTop) {
    StyleBuffer style;
    style.setVAlign(VerticalAlign::TOP);
    applyOperation(*workbook, makeCellSetStyleOp(*workbook, cellId, style));

    const StyleBuffer* applied = workbook->getEntityStyle(cellId);
    ASSERT_NE(applied, nullptr);
    EXPECT_TRUE(applied->hasVAlign());
    EXPECT_EQ(applied->getVAlign(), VerticalAlign::TOP);
}

TEST_F(StyleOperationsTest, StylePropertyVAlignMiddle) {
    StyleBuffer style;
    style.setVAlign(VerticalAlign::MIDDLE);
    applyOperation(*workbook, makeCellSetStyleOp(*workbook, cellId, style));

    const StyleBuffer* applied = workbook->getEntityStyle(cellId);
    ASSERT_NE(applied, nullptr);
    EXPECT_EQ(applied->getVAlign(), VerticalAlign::MIDDLE);
}

TEST_F(StyleOperationsTest, StylePropertyVAlignBottom) {
    StyleBuffer style;
    style.setVAlign(VerticalAlign::BOTTOM);
    applyOperation(*workbook, makeCellSetStyleOp(*workbook, cellId, style));

    const StyleBuffer* applied = workbook->getEntityStyle(cellId);
    ASSERT_NE(applied, nullptr);
    EXPECT_EQ(applied->getVAlign(), VerticalAlign::BOTTOM);
}

TEST_F(StyleOperationsTest, StylePropertyBorderTop) {
    StyleBuffer style;
    style.setBorderTop(BorderStyle::THIN, 0x00, 0x00, 0x00);
    applyOperation(*workbook, makeCellSetStyleOp(*workbook, cellId, style));

    const StyleBuffer* applied = workbook->getEntityStyle(cellId);
    ASSERT_NE(applied, nullptr);
    EXPECT_TRUE(applied->hasBorderTop());
    EXPECT_EQ(applied->getBorderTopStyle(), BorderStyle::THIN);
    EXPECT_EQ(applied->getBorderTopColorHex(), "#000000");
}

TEST_F(StyleOperationsTest, StylePropertyBorderRight) {
    StyleBuffer style;
    style.setBorderRight(BorderStyle::MEDIUM, 0xFF, 0x00, 0x00);
    applyOperation(*workbook, makeCellSetStyleOp(*workbook, cellId, style));

    const StyleBuffer* applied = workbook->getEntityStyle(cellId);
    ASSERT_NE(applied, nullptr);
    EXPECT_TRUE(applied->hasBorderRight());
    EXPECT_EQ(applied->getBorderRightStyle(), BorderStyle::MEDIUM);
    EXPECT_EQ(applied->getBorderRightColorHex(), "#FF0000");
}

TEST_F(StyleOperationsTest, StylePropertyBorderBottom) {
    StyleBuffer style;
    style.setBorderBottom(BorderStyle::THICK, 0x00, 0xFF, 0x00);
    applyOperation(*workbook, makeCellSetStyleOp(*workbook, cellId, style));

    const StyleBuffer* applied = workbook->getEntityStyle(cellId);
    ASSERT_NE(applied, nullptr);
    EXPECT_TRUE(applied->hasBorderBottom());
    EXPECT_EQ(applied->getBorderBottomStyle(), BorderStyle::THICK);
    EXPECT_EQ(applied->getBorderBottomColorHex(), "#00FF00");
}

TEST_F(StyleOperationsTest, StylePropertyBorderLeft) {
    StyleBuffer style;
    style.setBorderLeft(BorderStyle::DASHED, 0x00, 0x00, 0xFF);
    applyOperation(*workbook, makeCellSetStyleOp(*workbook, cellId, style));

    const StyleBuffer* applied = workbook->getEntityStyle(cellId);
    ASSERT_NE(applied, nullptr);
    EXPECT_TRUE(applied->hasBorderLeft());
    EXPECT_EQ(applied->getBorderLeftStyle(), BorderStyle::DASHED);
    EXPECT_EQ(applied->getBorderLeftColorHex(), "#0000FF");
}

TEST_F(StyleOperationsTest, StylePropertyAllBorders) {
    StyleBuffer style;
    style.setBorderTop(BorderStyle::THIN, 0xFF, 0x00, 0x00);
    style.setBorderRight(BorderStyle::MEDIUM, 0x00, 0xFF, 0x00);
    style.setBorderBottom(BorderStyle::THICK, 0x00, 0x00, 0xFF);
    style.setBorderLeft(BorderStyle::DOUBLE, 0xFF, 0xFF, 0x00);
    applyOperation(*workbook, makeCellSetStyleOp(*workbook, cellId, style));

    const StyleBuffer* applied = workbook->getEntityStyle(cellId);
    ASSERT_NE(applied, nullptr);

    EXPECT_TRUE(applied->hasBorder());
    EXPECT_EQ(applied->getBorderTopStyle(), BorderStyle::THIN);
    EXPECT_EQ(applied->getBorderRightStyle(), BorderStyle::MEDIUM);
    EXPECT_EQ(applied->getBorderBottomStyle(), BorderStyle::THICK);
    EXPECT_EQ(applied->getBorderLeftStyle(), BorderStyle::DOUBLE);

    EXPECT_EQ(applied->getBorderTopColorHex(), "#FF0000");
    EXPECT_EQ(applied->getBorderRightColorHex(), "#00FF00");
    EXPECT_EQ(applied->getBorderBottomColorHex(), "#0000FF");
    EXPECT_EQ(applied->getBorderLeftColorHex(), "#FFFF00");
}

TEST_F(StyleOperationsTest, StylePropertyAllBorderStyles) {
    // Test all border style values round-trip (except NONE which means no border)
    std::vector<BorderStyle> styles = {BorderStyle::THIN,          BorderStyle::MEDIUM,
                                       BorderStyle::THICK,         BorderStyle::DASHED,
                                       BorderStyle::DOTTED,        BorderStyle::DOUBLE,
                                       BorderStyle::HAIR,          BorderStyle::MEDIUM_DASHED,
                                       BorderStyle::DASH_DOT,      BorderStyle::MEDIUM_DASH_DOT,
                                       BorderStyle::DASH_DOT_DOT,  BorderStyle::MEDIUM_DASH_DOT_DOT,
                                       BorderStyle::SLANT_DASH_DOT};

    for (BorderStyle bs : styles) {
        StyleBuffer style;
        style.setBorderTop(bs, 0x00, 0x00, 0x00);
        applyOperation(*workbook, makeCellSetStyleOp(*workbook, cellId, style));

        const StyleBuffer* applied = workbook->getEntityStyle(cellId);
        ASSERT_NE(applied, nullptr);
        EXPECT_TRUE(applied->hasBorderTop());
        EXPECT_EQ(applied->getBorderTopStyle(), bs);
    }
}

TEST_F(StyleOperationsTest, ComplexStyleAllProperties) {
    StyleBuffer style;
    style.setBold(true);
    style.setItalic(true);
    style.setUnderline(true);
    style.setStrikethrough(false);
    style.setTextWrap(true);
    style.setFontFamily("Helvetica");
    style.setFontSize(14);
    style.setBgColorHex("#FBBF24");
    style.setTextColorHex("#1F2937");
    style.setHAlign(TextAlign::CENTER);
    style.setVAlign(VerticalAlign::MIDDLE);
    style.setBorderTop(BorderStyle::THIN, 0x00, 0x00, 0x00);
    style.setBorderBottom(BorderStyle::THIN, 0x00, 0x00, 0x00);

    applyOperation(*workbook, makeCellSetStyleOp(*workbook, cellId, style));

    const StyleBuffer* applied = workbook->getEntityStyle(cellId);
    ASSERT_NE(applied, nullptr);

    EXPECT_TRUE(applied->getBold());
    EXPECT_TRUE(applied->getItalic());
    EXPECT_TRUE(applied->getUnderline());
    EXPECT_TRUE(applied->hasStrikethrough());
    EXPECT_FALSE(applied->getStrikethrough());
    EXPECT_TRUE(applied->getTextWrap());
    EXPECT_EQ(applied->getFontFamily(), "Helvetica");
    EXPECT_EQ(applied->getFontSize(), 14);
    EXPECT_EQ(applied->getBgColorHex(), "#FBBF24");
    EXPECT_EQ(applied->getTextColorHex(), "#1F2937");
    EXPECT_EQ(applied->getHAlign(), TextAlign::CENTER);
    EXPECT_EQ(applied->getVAlign(), VerticalAlign::MIDDLE);
    EXPECT_TRUE(applied->hasBorderTop());
    EXPECT_TRUE(applied->hasBorderBottom());
    EXPECT_FALSE(applied->hasBorderLeft());
    EXPECT_FALSE(applied->hasBorderRight());
}

}  // namespace
}  // namespace cells
