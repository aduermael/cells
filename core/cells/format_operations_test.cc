// =============================================================================
// Format Operations Unit Tests
// =============================================================================
//
// Tests for applying formats through CRDT operations to cells, columns, rows,
// and ranges. Verifies format inheritance hierarchy and display rendering.
//
// =============================================================================

#include "core/cells/crdt.h"
#include "core/cells/format_buffer.h"
#include "core/cells/id.h"
#include "core/cells/number_format.h"
#include "core/cells/number_formatter.h"
#include "core/cells/range.h"

#include "gtest/gtest.h"

namespace cells {
namespace {

// =============================================================================
// Test Fixture
// =============================================================================

class FormatOperationsTest : public ::testing::Test {
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

        // Create a cell at (0, 0) with a numeric value
        cellId = generate_id();
        auto cell = std::make_unique<Cell>(cellId, colIds[0], rowIds[0]);
        cell->value = CellValue(1234.5678);
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
// 2a: Test applying formats to individual cells via CELL_SET
// =============================================================================

TEST_F(FormatOperationsTest, ApplyCellFormatNumber) {
    FormatBuffer format;
    format.setCategory(NumberFormatCategory::NUMBER);
    format.setDecimals(2);

    Operation op = makeCellSetFormatOp(*workbook, cellId, format);
    ApplyResult result = applyOperation(*workbook, op);
    EXPECT_EQ(result, ApplyResult::SUCCESS);

    const FormatBuffer* appliedFormat = workbook->getEntityFormat(cellId);
    ASSERT_NE(appliedFormat, nullptr);
    EXPECT_TRUE(appliedFormat->hasCategory());
    EXPECT_EQ(appliedFormat->getCategory(), NumberFormatCategory::NUMBER);
    EXPECT_TRUE(appliedFormat->hasDecimals());
    EXPECT_EQ(appliedFormat->getDecimals(), 2);
}

TEST_F(FormatOperationsTest, ApplyCellFormatPercentage) {
    FormatBuffer format;
    format.setCategory(NumberFormatCategory::PERCENTAGE);
    format.setDecimals(1);

    Operation op = makeCellSetFormatOp(*workbook, cellId, format);
    EXPECT_EQ(applyOperation(*workbook, op), ApplyResult::SUCCESS);

    const FormatBuffer* appliedFormat = workbook->getEntityFormat(cellId);
    ASSERT_NE(appliedFormat, nullptr);
    EXPECT_EQ(appliedFormat->getCategory(), NumberFormatCategory::PERCENTAGE);
    EXPECT_EQ(appliedFormat->getDecimals(), 1);
}

TEST_F(FormatOperationsTest, ApplyCellFormatCurrency) {
    FormatBuffer format;
    format.setCategory(NumberFormatCategory::CURRENCY);
    format.setDecimals(2);
    format.setCurrencySymbol("$");
    format.setThousandsSeparator(true);

    Operation op = makeCellSetFormatOp(*workbook, cellId, format);
    EXPECT_EQ(applyOperation(*workbook, op), ApplyResult::SUCCESS);

    const FormatBuffer* appliedFormat = workbook->getEntityFormat(cellId);
    ASSERT_NE(appliedFormat, nullptr);
    EXPECT_EQ(appliedFormat->getCategory(), NumberFormatCategory::CURRENCY);
    EXPECT_EQ(appliedFormat->getDecimals(), 2);
    EXPECT_EQ(appliedFormat->getCurrencySymbol(), "$");
    EXPECT_TRUE(appliedFormat->getThousandsSeparator());
}

TEST_F(FormatOperationsTest, UpdateCellFormatOverwrites) {
    // First format
    FormatBuffer format1;
    format1.setCategory(NumberFormatCategory::NUMBER);
    format1.setDecimals(4);
    applyOperation(*workbook, makeCellSetFormatOp(*workbook, cellId, format1));

    // Second format overwrites
    FormatBuffer format2;
    format2.setCategory(NumberFormatCategory::PERCENTAGE);
    format2.setDecimals(2);
    applyOperation(*workbook, makeCellSetFormatOp(*workbook, cellId, format2));

    const FormatBuffer* appliedFormat = workbook->getEntityFormat(cellId);
    ASSERT_NE(appliedFormat, nullptr);
    // New format replaces old completely
    EXPECT_EQ(appliedFormat->getCategory(), NumberFormatCategory::PERCENTAGE);
    EXPECT_EQ(appliedFormat->getDecimals(), 2);
}

TEST_F(FormatOperationsTest, ClearCellFormat) {
    // Apply format
    FormatBuffer format;
    format.setCategory(NumberFormatCategory::NUMBER);
    format.setDecimals(3);
    applyOperation(*workbook, makeCellSetFormatOp(*workbook, cellId, format));

    // Clear format
    Operation clearOp = makeCellClearFormatOp(*workbook, cellId);
    EXPECT_EQ(applyOperation(*workbook, clearOp), ApplyResult::SUCCESS);

    const FormatBuffer* appliedFormat = workbook->getEntityFormat(cellId);
    // Should be nullptr or empty after clearing
    EXPECT_TRUE(appliedFormat == nullptr || appliedFormat->isEmpty());
}

TEST_F(FormatOperationsTest, ApplyFormatToNewCell) {
    // Create a new cell via CRDT operation with format
    ID newCellId = generate_id();
    std::string payload = R"({"col":")" + colIds[1].toString() + R"(","row":")" +
                          rowIds[1].toString() + R"(","t":"n","v":"100"})";

    HLC hlc = workbook->getCurrentHLC();
    Operation createOp(hlc, OpType::CELL_SET, newCellId, payload);
    createOp.sheetId = sheet_id;
    EXPECT_EQ(applyOperation(*workbook, createOp), ApplyResult::SUCCESS);

    // Now apply format to the new cell
    FormatBuffer format;
    format.setCategory(NumberFormatCategory::PERCENTAGE);
    format.setDecimals(0);
    Operation formatOp = makeCellSetFormatOp(*workbook, newCellId, format);
    EXPECT_EQ(applyOperation(*workbook, formatOp), ApplyResult::SUCCESS);

    const FormatBuffer* appliedFormat = workbook->getEntityFormat(newCellId);
    ASSERT_NE(appliedFormat, nullptr);
    EXPECT_EQ(appliedFormat->getCategory(), NumberFormatCategory::PERCENTAGE);
}

// =============================================================================
// 2b: Test applying formats to columns/rows via COL_SET/ROW_SET
// =============================================================================

TEST_F(FormatOperationsTest, ApplyColumnFormat) {
    FormatBuffer format;
    format.setCategory(NumberFormatCategory::CURRENCY);
    format.setDecimals(2);
    format.setCurrencySymbol("€");

    Operation op = makeAxisSetFormatOp(*workbook, colIds[0], format);
    EXPECT_EQ(applyOperation(*workbook, op), ApplyResult::SUCCESS);

    const FormatBuffer* appliedFormat = workbook->getEntityFormat(colIds[0]);
    ASSERT_NE(appliedFormat, nullptr);
    EXPECT_EQ(appliedFormat->getCategory(), NumberFormatCategory::CURRENCY);
    EXPECT_EQ(appliedFormat->getCurrencySymbol(), "€");
}

TEST_F(FormatOperationsTest, ApplyRowFormat) {
    FormatBuffer format;
    format.setCategory(NumberFormatCategory::PERCENTAGE);
    format.setDecimals(1);

    Operation op = makeAxisSetFormatOp(*workbook, rowIds[0], format);
    EXPECT_EQ(applyOperation(*workbook, op), ApplyResult::SUCCESS);

    const FormatBuffer* appliedFormat = workbook->getEntityFormat(rowIds[0]);
    ASSERT_NE(appliedFormat, nullptr);
    EXPECT_EQ(appliedFormat->getCategory(), NumberFormatCategory::PERCENTAGE);
    EXPECT_EQ(appliedFormat->getDecimals(), 1);
}

TEST_F(FormatOperationsTest, ClearColumnFormat) {
    // Apply column format
    FormatBuffer format;
    format.setCategory(NumberFormatCategory::NUMBER);
    format.setDecimals(4);
    applyOperation(*workbook, makeAxisSetFormatOp(*workbook, colIds[0], format));

    // Clear column format
    Operation clearOp = makeAxisClearFormatOp(*workbook, colIds[0]);
    EXPECT_EQ(applyOperation(*workbook, clearOp), ApplyResult::SUCCESS);

    const FormatBuffer* appliedFormat = workbook->getEntityFormat(colIds[0]);
    EXPECT_TRUE(appliedFormat == nullptr || appliedFormat->isEmpty());
}

TEST_F(FormatOperationsTest, ClearRowFormat) {
    // Apply row format
    FormatBuffer format;
    format.setCategory(NumberFormatCategory::PERCENTAGE);
    applyOperation(*workbook, makeAxisSetFormatOp(*workbook, rowIds[0], format));

    // Clear row format
    Operation clearOp = makeAxisClearFormatOp(*workbook, rowIds[0]);
    EXPECT_EQ(applyOperation(*workbook, clearOp), ApplyResult::SUCCESS);

    const FormatBuffer* appliedFormat = workbook->getEntityFormat(rowIds[0]);
    EXPECT_TRUE(appliedFormat == nullptr || appliedFormat->isEmpty());
}

TEST_F(FormatOperationsTest, MultipleColumnsWithDifferentFormats) {
    FormatBuffer format1;
    format1.setCategory(NumberFormatCategory::CURRENCY);
    format1.setCurrencySymbol("$");

    FormatBuffer format2;
    format2.setCategory(NumberFormatCategory::PERCENTAGE);
    format2.setDecimals(2);

    applyOperation(*workbook, makeAxisSetFormatOp(*workbook, colIds[0], format1));
    applyOperation(*workbook, makeAxisSetFormatOp(*workbook, colIds[1], format2));

    const FormatBuffer* col0Format = workbook->getEntityFormat(colIds[0]);
    const FormatBuffer* col1Format = workbook->getEntityFormat(colIds[1]);

    ASSERT_NE(col0Format, nullptr);
    ASSERT_NE(col1Format, nullptr);

    EXPECT_EQ(col0Format->getCategory(), NumberFormatCategory::CURRENCY);
    EXPECT_EQ(col0Format->getCurrencySymbol(), "$");

    EXPECT_EQ(col1Format->getCategory(), NumberFormatCategory::PERCENTAGE);
    EXPECT_EQ(col1Format->getDecimals(), 2);
}

// =============================================================================
// 2c: Test applying formats to ranges via RANGE_SET with FORMAT flag
// =============================================================================

TEST_F(FormatOperationsTest, ApplyRangeFormat) {
    // Create a range with FORMAT flag
    ID rangeId = generate_id();
    std::string rangePayload = "{\"startCol\":\"" + colIds[0].toString() + "\",";
    rangePayload += "\"startRow\":\"" + rowIds[0].toString() + "\",";
    rangePayload += "\"endCol\":\"" + colIds[1].toString() + "\",";
    rangePayload += "\"endRow\":\"" + rowIds[1].toString() + "\",";
    rangePayload += "\"flags\":4}";  // FORMAT flag = 4

    Operation rangeOp = makeRangeSetOp(*workbook, rangeId, rangePayload);
    EXPECT_EQ(applyOperation(*workbook, rangeOp), ApplyResult::SUCCESS);

    // Apply format to the range
    FormatBuffer format;
    format.setCategory(NumberFormatCategory::NUMBER);
    format.setDecimals(3);
    format.setThousandsSeparator(true);

    Operation formatOp = makeRangeSetFormatOp(*workbook, rangeId, format);
    EXPECT_EQ(applyOperation(*workbook, formatOp), ApplyResult::SUCCESS);

    // Verify the range has the format
    const Range* range = workbook->getRange(rangeId);
    ASSERT_NE(range, nullptr);
    ASSERT_TRUE(range->format.has_value());
    EXPECT_EQ(range->format->getCategory(), NumberFormatCategory::NUMBER);
    EXPECT_EQ(range->format->getDecimals(), 3);
    EXPECT_TRUE(range->format->getThousandsSeparator());
}

TEST_F(FormatOperationsTest, MultipleOverlappingFormattedRanges) {
    // Create two overlapping ranges with different formats
    ID rangeId1 = generate_id();
    ID rangeId2 = generate_id();

    // Range 1: A1:B2
    std::string payload1 = "{\"startCol\":\"" + colIds[0].toString() + "\",";
    payload1 += "\"startRow\":\"" + rowIds[0].toString() + "\",";
    payload1 += "\"endCol\":\"" + colIds[1].toString() + "\",";
    payload1 += "\"endRow\":\"" + rowIds[1].toString() + "\",";
    payload1 += "\"flags\":4}";
    applyOperation(*workbook, makeRangeSetOp(*workbook, rangeId1, payload1));

    FormatBuffer format1;
    format1.setCategory(NumberFormatCategory::CURRENCY);
    format1.setCurrencySymbol("$");
    applyOperation(*workbook, makeRangeSetFormatOp(*workbook, rangeId1, format1));

    // Range 2: B2:C3 (overlaps at B2)
    std::string payload2 = "{\"startCol\":\"" + colIds[1].toString() + "\",";
    payload2 += "\"startRow\":\"" + rowIds[1].toString() + "\",";
    payload2 += "\"endCol\":\"" + colIds[2].toString() + "\",";
    payload2 += "\"endRow\":\"" + rowIds[2].toString() + "\",";
    payload2 += "\"flags\":4}";
    applyOperation(*workbook, makeRangeSetOp(*workbook, rangeId2, payload2));

    FormatBuffer format2;
    format2.setCategory(NumberFormatCategory::PERCENTAGE);
    format2.setDecimals(1);
    applyOperation(*workbook, makeRangeSetFormatOp(*workbook, rangeId2, format2));

    // Verify both ranges exist with their formats
    const Range* range1 = workbook->getRange(rangeId1);
    const Range* range2 = workbook->getRange(rangeId2);

    ASSERT_NE(range1, nullptr);
    ASSERT_NE(range2, nullptr);
    ASSERT_TRUE(range1->format.has_value());
    ASSERT_TRUE(range2->format.has_value());
    EXPECT_EQ(range1->format->getCategory(), NumberFormatCategory::CURRENCY);
    EXPECT_EQ(range2->format->getCategory(), NumberFormatCategory::PERCENTAGE);
}

TEST_F(FormatOperationsTest, RangeFormatWithCustomCode) {
    // Create a range
    ID rangeId = generate_id();
    std::string rangePayload = "{\"startCol\":\"" + colIds[0].toString() + "\",";
    rangePayload += "\"startRow\":\"" + rowIds[0].toString() + "\",";
    rangePayload += "\"endCol\":\"" + colIds[0].toString() + "\",";
    rangePayload += "\"endRow\":\"" + rowIds[0].toString() + "\",";
    rangePayload += "\"flags\":4}";
    applyOperation(*workbook, makeRangeSetOp(*workbook, rangeId, rangePayload));

    // Apply custom format code
    FormatBuffer format;
    format.setCustomFormatCode("#,##0.00 \"units\"");
    applyOperation(*workbook, makeRangeSetFormatOp(*workbook, rangeId, format));

    const Range* range = workbook->getRange(rangeId);
    ASSERT_NE(range, nullptr);
    ASSERT_TRUE(range->format.has_value());
    EXPECT_TRUE(range->format->hasCustomFormatCode());
    EXPECT_EQ(range->format->getCustomFormatCode(), "#,##0.00 \"units\"");
}

// =============================================================================
// 2d: Test format inheritance hierarchy (cell > range > row > column)
// =============================================================================

TEST_F(FormatOperationsTest, EffectiveFormatCellOverridesColumn) {
    // Set column format
    FormatBuffer colFormat;
    colFormat.setCategory(NumberFormatCategory::CURRENCY);
    colFormat.setCurrencySymbol("$");
    colFormat.setDecimals(2);
    applyOperation(*workbook, makeAxisSetFormatOp(*workbook, colIds[0], colFormat));

    // Set cell format (should override column)
    FormatBuffer cellFormat;
    cellFormat.setCategory(NumberFormatCategory::PERCENTAGE);
    cellFormat.setDecimals(1);
    applyOperation(*workbook, makeCellSetFormatOp(*workbook, cellId, cellFormat));

    // Compute effective format
    const FormatBuffer* colFormatPtr = workbook->getEntityFormat(colIds[0]);
    const FormatBuffer* cellFormatPtr = workbook->getEntityFormat(cellId);

    std::vector<const FormatBuffer*> rangeFormats;
    FormatBuffer effective =
        FormatBuffer::getEffectiveFormat(colFormatPtr, nullptr, rangeFormats, cellFormatPtr);

    // Cell's percentage should override column's currency
    EXPECT_EQ(effective.getCategory(), NumberFormatCategory::PERCENTAGE);
    EXPECT_EQ(effective.getDecimals(), 1);
}

TEST_F(FormatOperationsTest, EffectiveFormatRowOverridesColumn) {
    // Set column format
    FormatBuffer colFormat;
    colFormat.setCategory(NumberFormatCategory::NUMBER);
    colFormat.setDecimals(4);
    applyOperation(*workbook, makeAxisSetFormatOp(*workbook, colIds[0], colFormat));

    // Set row format (should override column for category)
    FormatBuffer rowFormat;
    rowFormat.setCategory(NumberFormatCategory::PERCENTAGE);
    applyOperation(*workbook, makeAxisSetFormatOp(*workbook, rowIds[0], rowFormat));

    const FormatBuffer* colFormatPtr = workbook->getEntityFormat(colIds[0]);
    const FormatBuffer* rowFormatPtr = workbook->getEntityFormat(rowIds[0]);

    std::vector<const FormatBuffer*> rangeFormats;
    FormatBuffer effective =
        FormatBuffer::getEffectiveFormat(colFormatPtr, rowFormatPtr, rangeFormats, nullptr);

    // Row's percentage should override column's number
    EXPECT_EQ(effective.getCategory(), NumberFormatCategory::PERCENTAGE);
    // Column's decimals should still apply if row doesn't specify
    EXPECT_EQ(effective.getDecimals(), 4);
}

TEST_F(FormatOperationsTest, EffectiveFormatRangeOverridesRowAndColumn) {
    // Set column format
    FormatBuffer colFormat;
    colFormat.setCategory(NumberFormatCategory::NUMBER);
    colFormat.setDecimals(2);
    applyOperation(*workbook, makeAxisSetFormatOp(*workbook, colIds[0], colFormat));

    // Set row format
    FormatBuffer rowFormat;
    rowFormat.setThousandsSeparator(true);
    applyOperation(*workbook, makeAxisSetFormatOp(*workbook, rowIds[0], rowFormat));

    // Create and format a range
    ID rangeId = generate_id();
    std::string rangePayload = "{\"startCol\":\"" + colIds[0].toString() + "\",";
    rangePayload += "\"startRow\":\"" + rowIds[0].toString() + "\",";
    rangePayload += "\"endCol\":\"" + colIds[0].toString() + "\",";
    rangePayload += "\"endRow\":\"" + rowIds[0].toString() + "\",";
    rangePayload += "\"flags\":4}";
    applyOperation(*workbook, makeRangeSetOp(*workbook, rangeId, rangePayload));

    FormatBuffer rangeFormat;
    rangeFormat.setCategory(NumberFormatCategory::CURRENCY);
    rangeFormat.setCurrencySymbol("€");
    applyOperation(*workbook, makeRangeSetFormatOp(*workbook, rangeId, rangeFormat));

    const FormatBuffer* colFormatPtr = workbook->getEntityFormat(colIds[0]);
    const FormatBuffer* rowFormatPtr = workbook->getEntityFormat(rowIds[0]);
    const Range* range = workbook->getRange(rangeId);
    ASSERT_NE(range, nullptr);

    std::vector<const FormatBuffer*> rangeFormats;
    if (range->format.has_value()) {
        rangeFormats.push_back(&(range->format.value()));
    }

    FormatBuffer effective =
        FormatBuffer::getEffectiveFormat(colFormatPtr, rowFormatPtr, rangeFormats, nullptr);

    // Range's currency should win for category
    EXPECT_EQ(effective.getCategory(), NumberFormatCategory::CURRENCY);
    EXPECT_EQ(effective.getCurrencySymbol(), "€");
    // Column's decimals and row's separator should still apply
    EXPECT_EQ(effective.getDecimals(), 2);
    EXPECT_TRUE(effective.getThousandsSeparator());
}

TEST_F(FormatOperationsTest, EffectiveFormatCellOverridesAll) {
    // Set column format
    FormatBuffer colFormat;
    colFormat.setCategory(NumberFormatCategory::NUMBER);
    colFormat.setDecimals(4);
    applyOperation(*workbook, makeAxisSetFormatOp(*workbook, colIds[0], colFormat));

    // Set row format
    FormatBuffer rowFormat;
    rowFormat.setThousandsSeparator(true);
    applyOperation(*workbook, makeAxisSetFormatOp(*workbook, rowIds[0], rowFormat));

    // Create and format a range
    ID rangeId = generate_id();
    std::string rangePayload = "{\"startCol\":\"" + colIds[0].toString() + "\",";
    rangePayload += "\"startRow\":\"" + rowIds[0].toString() + "\",";
    rangePayload += "\"endCol\":\"" + colIds[0].toString() + "\",";
    rangePayload += "\"endRow\":\"" + rowIds[0].toString() + "\",";
    rangePayload += "\"flags\":4}";
    applyOperation(*workbook, makeRangeSetOp(*workbook, rangeId, rangePayload));

    FormatBuffer rangeFormat;
    rangeFormat.setCurrencySymbol("£");
    applyOperation(*workbook, makeRangeSetFormatOp(*workbook, rangeId, rangeFormat));

    // Set cell format (highest priority)
    FormatBuffer cellFormat;
    cellFormat.setCategory(NumberFormatCategory::PERCENTAGE);
    cellFormat.setDecimals(0);
    applyOperation(*workbook, makeCellSetFormatOp(*workbook, cellId, cellFormat));

    // Compute effective format
    const FormatBuffer* colFormatPtr = workbook->getEntityFormat(colIds[0]);
    const FormatBuffer* rowFormatPtr = workbook->getEntityFormat(rowIds[0]);
    const FormatBuffer* cellFormatPtr = workbook->getEntityFormat(cellId);
    const Range* range = workbook->getRange(rangeId);

    std::vector<const FormatBuffer*> rangeFormats;
    if (range && range->format.has_value()) {
        rangeFormats.push_back(&(range->format.value()));
    }

    FormatBuffer effective =
        FormatBuffer::getEffectiveFormat(colFormatPtr, rowFormatPtr, rangeFormats, cellFormatPtr);

    // Cell's percentage and 0 decimals override all
    EXPECT_EQ(effective.getCategory(), NumberFormatCategory::PERCENTAGE);
    EXPECT_EQ(effective.getDecimals(), 0);
    // Row's separator and range's currency should still be present
    EXPECT_TRUE(effective.getThousandsSeparator());
    EXPECT_EQ(effective.getCurrencySymbol(), "£");
}

TEST_F(FormatOperationsTest, EffectiveFormatMultipleRangesLaterWins) {
    // Create two ranges at the same location
    ID rangeId1 = generate_id();
    ID rangeId2 = generate_id();

    std::string payload = "{\"startCol\":\"" + colIds[0].toString() + "\",";
    payload += "\"startRow\":\"" + rowIds[0].toString() + "\",";
    payload += "\"endCol\":\"" + colIds[0].toString() + "\",";
    payload += "\"endRow\":\"" + rowIds[0].toString() + "\",";
    payload += "\"flags\":4}";

    applyOperation(*workbook, makeRangeSetOp(*workbook, rangeId1, payload));
    applyOperation(*workbook, makeRangeSetOp(*workbook, rangeId2, payload));

    FormatBuffer format1;
    format1.setCategory(NumberFormatCategory::CURRENCY);
    format1.setCurrencySymbol("$");
    applyOperation(*workbook, makeRangeSetFormatOp(*workbook, rangeId1, format1));

    FormatBuffer format2;
    format2.setCategory(NumberFormatCategory::PERCENTAGE);
    format2.setDecimals(2);
    applyOperation(*workbook, makeRangeSetFormatOp(*workbook, rangeId2, format2));

    const Range* range1 = workbook->getRange(rangeId1);
    const Range* range2 = workbook->getRange(rangeId2);

    // Later range in the list has higher priority
    std::vector<const FormatBuffer*> rangeFormats;
    if (range1 && range1->format.has_value()) {
        rangeFormats.push_back(&(range1->format.value()));
    }
    if (range2 && range2->format.has_value()) {
        rangeFormats.push_back(&(range2->format.value()));
    }

    FormatBuffer effective =
        FormatBuffer::getEffectiveFormat(nullptr, nullptr, rangeFormats, nullptr);

    // Second range's percentage should win
    EXPECT_EQ(effective.getCategory(), NumberFormatCategory::PERCENTAGE);
    EXPECT_EQ(effective.getDecimals(), 2);
}

// =============================================================================
// 2e: Test all format categories
// =============================================================================

TEST_F(FormatOperationsTest, FormatCategoryGeneral) {
    FormatBuffer format;
    format.setCategory(NumberFormatCategory::GENERAL);
    applyOperation(*workbook, makeCellSetFormatOp(*workbook, cellId, format));

    const FormatBuffer* applied = workbook->getEntityFormat(cellId);
    ASSERT_NE(applied, nullptr);
    EXPECT_EQ(applied->getCategory(), NumberFormatCategory::GENERAL);
}

TEST_F(FormatOperationsTest, FormatCategoryNumber) {
    FormatBuffer format;
    format.setCategory(NumberFormatCategory::NUMBER);
    format.setDecimals(3);
    format.setThousandsSeparator(true);
    applyOperation(*workbook, makeCellSetFormatOp(*workbook, cellId, format));

    const FormatBuffer* applied = workbook->getEntityFormat(cellId);
    ASSERT_NE(applied, nullptr);
    EXPECT_EQ(applied->getCategory(), NumberFormatCategory::NUMBER);
    EXPECT_EQ(applied->getDecimals(), 3);
    EXPECT_TRUE(applied->getThousandsSeparator());
}

TEST_F(FormatOperationsTest, FormatCategoryCurrency) {
    FormatBuffer format;
    format.setCategory(NumberFormatCategory::CURRENCY);
    format.setDecimals(2);
    format.setCurrencySymbol("$");
    format.setThousandsSeparator(true);
    applyOperation(*workbook, makeCellSetFormatOp(*workbook, cellId, format));

    const FormatBuffer* applied = workbook->getEntityFormat(cellId);
    ASSERT_NE(applied, nullptr);
    EXPECT_EQ(applied->getCategory(), NumberFormatCategory::CURRENCY);
    EXPECT_EQ(applied->getCurrencySymbol(), "$");
}

TEST_F(FormatOperationsTest, FormatCategoryCurrencyEuro) {
    FormatBuffer format;
    format.setCategory(NumberFormatCategory::CURRENCY);
    format.setDecimals(2);
    format.setCurrencySymbol("€");
    applyOperation(*workbook, makeCellSetFormatOp(*workbook, cellId, format));

    const FormatBuffer* applied = workbook->getEntityFormat(cellId);
    ASSERT_NE(applied, nullptr);
    EXPECT_EQ(applied->getCurrencySymbol(), "€");
}

TEST_F(FormatOperationsTest, FormatCategoryCurrencyPound) {
    FormatBuffer format;
    format.setCategory(NumberFormatCategory::CURRENCY);
    format.setCurrencySymbol("£");
    applyOperation(*workbook, makeCellSetFormatOp(*workbook, cellId, format));

    const FormatBuffer* applied = workbook->getEntityFormat(cellId);
    ASSERT_NE(applied, nullptr);
    EXPECT_EQ(applied->getCurrencySymbol(), "£");
}

TEST_F(FormatOperationsTest, FormatCategoryCurrencyYen) {
    FormatBuffer format;
    format.setCategory(NumberFormatCategory::CURRENCY);
    format.setCurrencySymbol("¥");
    applyOperation(*workbook, makeCellSetFormatOp(*workbook, cellId, format));

    const FormatBuffer* applied = workbook->getEntityFormat(cellId);
    ASSERT_NE(applied, nullptr);
    EXPECT_EQ(applied->getCurrencySymbol(), "¥");
}

TEST_F(FormatOperationsTest, FormatCategoryAccounting) {
    FormatBuffer format;
    format.setCategory(NumberFormatCategory::ACCOUNTING);
    format.setDecimals(2);
    format.setCurrencySymbol("$");
    applyOperation(*workbook, makeCellSetFormatOp(*workbook, cellId, format));

    const FormatBuffer* applied = workbook->getEntityFormat(cellId);
    ASSERT_NE(applied, nullptr);
    EXPECT_EQ(applied->getCategory(), NumberFormatCategory::ACCOUNTING);
}

TEST_F(FormatOperationsTest, FormatCategoryPercentage) {
    FormatBuffer format;
    format.setCategory(NumberFormatCategory::PERCENTAGE);
    format.setDecimals(2);
    applyOperation(*workbook, makeCellSetFormatOp(*workbook, cellId, format));

    const FormatBuffer* applied = workbook->getEntityFormat(cellId);
    ASSERT_NE(applied, nullptr);
    EXPECT_EQ(applied->getCategory(), NumberFormatCategory::PERCENTAGE);
}

TEST_F(FormatOperationsTest, FormatCategoryDate) {
    FormatBuffer format;
    format.setCategory(NumberFormatCategory::DATE);
    applyOperation(*workbook, makeCellSetFormatOp(*workbook, cellId, format));

    const FormatBuffer* applied = workbook->getEntityFormat(cellId);
    ASSERT_NE(applied, nullptr);
    EXPECT_EQ(applied->getCategory(), NumberFormatCategory::DATE);
}

TEST_F(FormatOperationsTest, FormatCategoryTime) {
    FormatBuffer format;
    format.setCategory(NumberFormatCategory::TIME);
    applyOperation(*workbook, makeCellSetFormatOp(*workbook, cellId, format));

    const FormatBuffer* applied = workbook->getEntityFormat(cellId);
    ASSERT_NE(applied, nullptr);
    EXPECT_EQ(applied->getCategory(), NumberFormatCategory::TIME);
}

TEST_F(FormatOperationsTest, FormatCategoryDateTime) {
    FormatBuffer format;
    format.setCategory(NumberFormatCategory::DATE_TIME);
    applyOperation(*workbook, makeCellSetFormatOp(*workbook, cellId, format));

    const FormatBuffer* applied = workbook->getEntityFormat(cellId);
    ASSERT_NE(applied, nullptr);
    EXPECT_EQ(applied->getCategory(), NumberFormatCategory::DATE_TIME);
}

TEST_F(FormatOperationsTest, FormatCategoryScientific) {
    FormatBuffer format;
    format.setCategory(NumberFormatCategory::SCIENTIFIC);
    format.setDecimals(2);
    applyOperation(*workbook, makeCellSetFormatOp(*workbook, cellId, format));

    const FormatBuffer* applied = workbook->getEntityFormat(cellId);
    ASSERT_NE(applied, nullptr);
    EXPECT_EQ(applied->getCategory(), NumberFormatCategory::SCIENTIFIC);
}

TEST_F(FormatOperationsTest, FormatCategoryFraction) {
    FormatBuffer format;
    format.setCategory(NumberFormatCategory::FRACTION);
    applyOperation(*workbook, makeCellSetFormatOp(*workbook, cellId, format));

    const FormatBuffer* applied = workbook->getEntityFormat(cellId);
    ASSERT_NE(applied, nullptr);
    EXPECT_EQ(applied->getCategory(), NumberFormatCategory::FRACTION);
}

TEST_F(FormatOperationsTest, FormatCategoryText) {
    FormatBuffer format;
    format.setCategory(NumberFormatCategory::TEXT);
    applyOperation(*workbook, makeCellSetFormatOp(*workbook, cellId, format));

    const FormatBuffer* applied = workbook->getEntityFormat(cellId);
    ASSERT_NE(applied, nullptr);
    EXPECT_EQ(applied->getCategory(), NumberFormatCategory::TEXT);
}

TEST_F(FormatOperationsTest, FormatCategoryCustom) {
    FormatBuffer format;
    format.setCategory(NumberFormatCategory::CUSTOM);
    format.setCustomFormatCode("# \"apples\"");
    applyOperation(*workbook, makeCellSetFormatOp(*workbook, cellId, format));

    const FormatBuffer* applied = workbook->getEntityFormat(cellId);
    ASSERT_NE(applied, nullptr);
    EXPECT_EQ(applied->getCategory(), NumberFormatCategory::CUSTOM);
    EXPECT_EQ(applied->getCustomFormatCode(), "# \"apples\"");
}

TEST_F(FormatOperationsTest, FormatDecimalPlacesRange) {
    // Test minimum decimals
    FormatBuffer format0;
    format0.setCategory(NumberFormatCategory::NUMBER);
    format0.setDecimals(0);
    applyOperation(*workbook, makeCellSetFormatOp(*workbook, cellId, format0));
    const FormatBuffer* applied0 = workbook->getEntityFormat(cellId);
    ASSERT_NE(applied0, nullptr);
    EXPECT_EQ(applied0->getDecimals(), 0);

    // Test maximum practical decimals (15)
    FormatBuffer format15;
    format15.setCategory(NumberFormatCategory::NUMBER);
    format15.setDecimals(15);
    applyOperation(*workbook, makeCellSetFormatOp(*workbook, cellId, format15));
    const FormatBuffer* applied15 = workbook->getEntityFormat(cellId);
    ASSERT_NE(applied15, nullptr);
    EXPECT_EQ(applied15->getDecimals(), 15);
}

// =============================================================================
// 2f: Test format display rendering for edge cases
// =============================================================================

TEST_F(FormatOperationsTest, DisplayRenderingLargeNumber) {
    NumberFormatRegistry registry;

    // Large number with thousands separator
    FormattedValue result = formatPlainNumber(1234567890.12, 2, true);
    EXPECT_FALSE(result.isError);
    EXPECT_EQ(result.text, "1,234,567,890.12");
}

TEST_F(FormatOperationsTest, DisplayRenderingSmallNumber) {
    NumberFormatRegistry registry;

    // Small number
    FormattedValue result = formatPlainNumber(0.00012345, 8, false);
    EXPECT_FALSE(result.isError);
    EXPECT_EQ(result.text, "0.00012345");
}

TEST_F(FormatOperationsTest, DisplayRenderingNegativeNumber) {
    NumberFormatRegistry registry;

    FormattedValue result = formatPlainNumber(-1234.56, 2, true);
    EXPECT_FALSE(result.isError);
    EXPECT_EQ(result.text, "-1,234.56");
}

TEST_F(FormatOperationsTest, DisplayRenderingZero) {
    NumberFormatRegistry registry;

    FormattedValue result = formatPlainNumber(0.0, 2, false);
    EXPECT_FALSE(result.isError);
    EXPECT_EQ(result.text, "0.00");
}

TEST_F(FormatOperationsTest, DisplayRenderingZeroWithSeparator) {
    NumberFormatRegistry registry;

    FormattedValue result = formatPlainNumber(0.0, 0, true);
    EXPECT_FALSE(result.isError);
    EXPECT_EQ(result.text, "0");
}

TEST_F(FormatOperationsTest, DisplayRenderingPercentage) {
    NumberFormatRegistry registry;

    // 0.15 should display as 15%
    FormattedValue result = formatPercentage(0.15, 0);
    EXPECT_FALSE(result.isError);
    EXPECT_EQ(result.text, "15%");
}

TEST_F(FormatOperationsTest, DisplayRenderingPercentageWithDecimals) {
    NumberFormatRegistry registry;

    FormattedValue result = formatPercentage(0.1567, 2);
    EXPECT_FALSE(result.isError);
    EXPECT_EQ(result.text, "15.67%");
}

TEST_F(FormatOperationsTest, DisplayRenderingNegativePercentage) {
    NumberFormatRegistry registry;

    FormattedValue result = formatPercentage(-0.25, 1);
    EXPECT_FALSE(result.isError);
    EXPECT_EQ(result.text, "-25.0%");
}

TEST_F(FormatOperationsTest, DisplayRenderingCurrency) {
    NumberFormatRegistry registry;

    FormattedValue result = formatCurrency(1234.56, 2, "$", false);
    EXPECT_FALSE(result.isError);
    EXPECT_EQ(result.text, "$1,234.56");
}

TEST_F(FormatOperationsTest, DisplayRenderingCurrencyNegative) {
    NumberFormatRegistry registry;

    FormattedValue result = formatCurrency(-1234.56, 2, "$", false);
    EXPECT_FALSE(result.isError);
    EXPECT_EQ(result.text, "-$1,234.56");
}

TEST_F(FormatOperationsTest, DisplayRenderingCurrencyAccounting) {
    NumberFormatRegistry registry;

    // Accounting format shows negatives in parentheses
    FormattedValue result = formatCurrency(-1234.56, 2, "$", true);
    EXPECT_FALSE(result.isError);
    // Accounting format typically shows negative in parentheses
    EXPECT_TRUE(result.text.find("(") != std::string::npos ||
                result.text.find("-") != std::string::npos);
}

TEST_F(FormatOperationsTest, DisplayRenderingScientific) {
    NumberFormatRegistry registry;

    FormattedValue result = formatScientific(1234567890.0, 2);
    EXPECT_FALSE(result.isError);
    EXPECT_TRUE(result.text.find("E") != std::string::npos ||
                result.text.find("e") != std::string::npos);
}

TEST_F(FormatOperationsTest, DisplayRenderingScientificSmall) {
    NumberFormatRegistry registry;

    FormattedValue result = formatScientific(0.00000123, 2);
    EXPECT_FALSE(result.isError);
    EXPECT_TRUE(result.text.find("E") != std::string::npos ||
                result.text.find("e") != std::string::npos);
}

TEST_F(FormatOperationsTest, DisplayRenderingGeneral) {
    NumberFormatRegistry registry;

    // General format should auto-detect
    FormattedValue result = formatGeneral(1234.5);
    EXPECT_FALSE(result.isError);
    // Should not have excessive decimals
    EXPECT_TRUE(result.text.find("1234.5") != std::string::npos ||
                result.text.find("1,234.5") != std::string::npos);
}

TEST_F(FormatOperationsTest, DisplayRenderingGeneralInteger) {
    NumberFormatRegistry registry;

    // Whole number should not show decimals in GENERAL
    FormattedValue result = formatGeneral(42.0);
    EXPECT_FALSE(result.isError);
    EXPECT_EQ(result.text, "42");
}

TEST_F(FormatOperationsTest, DisplayRenderingGeneralVeryLarge) {
    NumberFormatRegistry registry;

    // Very large numbers may switch to scientific notation in GENERAL
    FormattedValue result = formatGeneral(1e15);
    EXPECT_FALSE(result.isError);
    // Should be formatted somehow (scientific or full number)
    EXPECT_FALSE(result.text.empty());
}

TEST_F(FormatOperationsTest, DisplayRenderingGeneralVerySmall) {
    NumberFormatRegistry registry;

    // Very small numbers may switch to scientific notation in GENERAL
    FormattedValue result = formatGeneral(0.00000001);
    EXPECT_FALSE(result.isError);
    EXPECT_FALSE(result.text.empty());
}

TEST_F(FormatOperationsTest, DisplayRenderingEuroLocale) {
    NumberFormatRegistry registry;

    // European locale uses comma for decimals, period for thousands
    FormattedValue result = formatPlainNumber(1234.56, 2, true, FormatLocale::EU());
    EXPECT_FALSE(result.isError);
    EXPECT_EQ(result.text, "1.234,56");
}

TEST_F(FormatOperationsTest, DisplayRenderingCurrencyEuroLocale) {
    NumberFormatRegistry registry;

    FormattedValue result = formatCurrency(1234.56, 2, "€", false, FormatLocale::EU());
    EXPECT_FALSE(result.isError);
    EXPECT_TRUE(result.text.find("€") != std::string::npos);
    EXPECT_TRUE(result.text.find(",56") != std::string::npos);  // Euro decimal
}

// =============================================================================
// CRDT Convergence Tests for Format Operations
// =============================================================================

class FormatConvergenceTest : public ::testing::Test {
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

        // Shared sheet id across peers so COL_SET with sheetId targets the same sheet
        auto sheet = std::make_unique<Sheet>(shared_sheet, "Sheet1");
        sheet->setWorkbook(wb.get());

        auto col = std::make_unique<Axis>(shared_col, true);
        auto row = std::make_unique<Axis>(shared_row, false);
        sheet->addColumn(std::move(col));
        sheet->addRow(std::move(row));

        auto cell = std::make_unique<Cell>(shared_cell, shared_col, shared_row);
        cell->value = CellValue(100.0);
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

TEST_F(FormatConvergenceTest, ConcurrentCellFormatChangesConverge) {
    // Peer A sets currency format
    FormatBuffer formatA;
    formatA.setCategory(NumberFormatCategory::CURRENCY);
    formatA.setCurrencySymbol("$");

    HLC hlc_a(1000, 0, node_a);
    std::string payloadA = R"({"fmt":")" + formatA.toBase64() + R"("})";
    Operation opA(hlc_a, OpType::CELL_SET, shared_cell, payloadA);
    opA.sheetId = shared_sheet;

    // Peer B sets percentage format at same time
    FormatBuffer formatB;
    formatB.setCategory(NumberFormatCategory::PERCENTAGE);
    formatB.setDecimals(2);

    HLC hlc_b(1000, 0, node_b);
    std::string payloadB = R"({"fmt":")" + formatB.toBase64() + R"("})";
    Operation opB(hlc_b, OpType::CELL_SET, shared_cell, payloadB);
    opB.sheetId = shared_sheet;

    // Apply in different orders
    applyOperation(*workbook_a, opA);
    applyOperation(*workbook_a, opB);

    applyOperation(*workbook_b, opB);
    applyOperation(*workbook_b, opA);

    // Both should converge to the same format (NodeBBBB > NodeAAAA)
    const FormatBuffer* formatFromA = workbook_a->getEntityFormat(shared_cell);
    const FormatBuffer* formatFromB = workbook_b->getEntityFormat(shared_cell);

    ASSERT_NE(formatFromA, nullptr);
    ASSERT_NE(formatFromB, nullptr);

    // Both should have the same category
    EXPECT_EQ(formatFromA->getCategory(), formatFromB->getCategory());
}

TEST_F(FormatConvergenceTest, ConcurrentColumnFormatChangesConverge) {
    // Peer A sets column to number format
    FormatBuffer formatA;
    formatA.setCategory(NumberFormatCategory::NUMBER);
    formatA.setDecimals(4);

    HLC hlc_a(1000, 0, node_a);
    std::string payloadA = R"({"fmt":")" + formatA.toBase64() + R"("})";
    Operation opA(hlc_a, OpType::COL_SET, shared_col, payloadA);
    opA.sheetId = shared_sheet;

    // Peer B sets column to currency format at same time
    FormatBuffer formatB;
    formatB.setCategory(NumberFormatCategory::CURRENCY);
    formatB.setCurrencySymbol("€");

    HLC hlc_b(1000, 0, node_b);
    std::string payloadB = R"({"fmt":")" + formatB.toBase64() + R"("})";
    Operation opB(hlc_b, OpType::COL_SET, shared_col, payloadB);
    opB.sheetId = shared_sheet;

    // Apply in different orders
    applyOperation(*workbook_a, opA);
    applyOperation(*workbook_a, opB);

    applyOperation(*workbook_b, opB);
    applyOperation(*workbook_b, opA);

    // Both should converge
    const FormatBuffer* formatFromA = workbook_a->getEntityFormat(shared_col);
    const FormatBuffer* formatFromB = workbook_b->getEntityFormat(shared_col);

    ASSERT_NE(formatFromA, nullptr);
    ASSERT_NE(formatFromB, nullptr);

    // Both should have the same category
    EXPECT_EQ(formatFromA->getCategory(), formatFromB->getCategory());
}

TEST_F(FormatConvergenceTest, ThreePeersFormatConvergence) {
    ID node_c("NodeCCCC");
    auto workbook_c = createWorkbook(node_c);

    // All three peers set different formats at same time
    FormatBuffer formatA;
    formatA.setCategory(NumberFormatCategory::NUMBER);

    FormatBuffer formatB;
    formatB.setCategory(NumberFormatCategory::CURRENCY);

    FormatBuffer formatC;
    formatC.setCategory(NumberFormatCategory::PERCENTAGE);

    HLC hlc_a(1000, 0, node_a);
    HLC hlc_b(1000, 0, node_b);
    HLC hlc_c(1000, 0, node_c);

    std::string payloadA = R"({"fmt":")" + formatA.toBase64() + R"("})";
    std::string payloadB = R"({"fmt":")" + formatB.toBase64() + R"("})";
    std::string payloadC = R"({"fmt":")" + formatC.toBase64() + R"("})";

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
    const FormatBuffer* fromA = workbook_a->getEntityFormat(shared_cell);
    const FormatBuffer* fromB = workbook_b->getEntityFormat(shared_cell);
    const FormatBuffer* fromC = workbook_c->getEntityFormat(shared_cell);

    ASSERT_NE(fromA, nullptr);
    ASSERT_NE(fromB, nullptr);
    ASSERT_NE(fromC, nullptr);

    // All should have percentage (from NodeCCCC which wins)
    EXPECT_EQ(fromA->getCategory(), NumberFormatCategory::PERCENTAGE);
    EXPECT_EQ(fromB->getCategory(), NumberFormatCategory::PERCENTAGE);
    EXPECT_EQ(fromC->getCategory(), NumberFormatCategory::PERCENTAGE);
}

TEST_F(FormatConvergenceTest, OutOfOrderFormatDeliveryConverges) {
    // Peer A makes sequential format changes
    std::vector<Operation> ops;
    for (int i = 0; i < 5; i++) {
        FormatBuffer format;
        format.setCategory(NumberFormatCategory::NUMBER);
        format.setDecimals(static_cast<uint8_t>(i));
        ops.push_back(makeCellSetFormatOp(*workbook_a, shared_cell, format));
        applyOperation(*workbook_a, ops.back());
    }

    // Apply to workbook_b in reverse order
    for (auto it = ops.rbegin(); it != ops.rend(); ++it) {
        applyOperation(*workbook_b, *it);
    }

    // Both should have 4 decimals (last operation)
    const FormatBuffer* fromA = workbook_a->getEntityFormat(shared_cell);
    const FormatBuffer* fromB = workbook_b->getEntityFormat(shared_cell);

    ASSERT_NE(fromA, nullptr);
    ASSERT_NE(fromB, nullptr);

    EXPECT_EQ(fromA->getDecimals(), 4);
    EXPECT_EQ(fromB->getDecimals(), 4);
}

}  // namespace
}  // namespace cells
